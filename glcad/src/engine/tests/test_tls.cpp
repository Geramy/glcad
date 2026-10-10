#include "test.hpp"
#include <chrono>
#include <atomic>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "engine/session.hpp"
#include "engine/tls.hpp"

using namespace gio;

namespace {

struct Certificate {
    // Unique per process: parallel variants would truncate a shared pem.
    std::string cert{"/tmp/glcad_test_cert_" + std::to_string(getpid()) + ".pem"};
    std::string key{"/tmp/glcad_test_key_" + std::to_string(getpid()) + ".pem"};
    bool        ok{false};

    Certificate() {
        const std::string command =
            "openssl req -x509 -newkey rsa:2048 -nodes -days 2 "
            "-subj '/CN=localhost' "
            "-addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' "
            "-addext 'extendedKeyUsage=serverAuth' "
            "-keyout " + key + " -out " + cert + " >/dev/null 2>&1";
        ok = std::system(command.c_str()) == 0;
    }
    ~Certificate() {
        std::remove(cert.c_str());
        std::remove(key.c_str());
    }
};

const Certificate& certificate() {
    static Certificate c;
    return c;
}

}

TEST(tls_availability_is_asked_not_assumed) {
    const char* backend = tls_backend();
    CHECK(backend != nullptr && backend[0] != '\0');
    if (!tls_available()) CHECK(std::string(backend) == "none");
}

TEST(tls_urls_are_a_different_promise_from_plain_tcp) {
    Endpoint e;
    CHECK(Endpoint::parse("tls://cad.example.com:5911", e));
    CHECK(e.family == Family::Tls);
    CHECK(e.host == "cad.example.com");
    CHECK(e.port == 5911);
    CHECK(e.is_network());
    CHECK_MSG(e.is_encrypted(), "a tls:// endpoint did not report as encrypted");
    CHECK(e.to_string() == "tls://cad.example.com:5911");

    CHECK(!Endpoint::ipv4("127.0.0.1", 80).is_encrypted());
    CHECK(!Endpoint::file("/tmp/x").is_encrypted());

    CHECK(Endpoint::parse("tls://[::1]:443", e));
    CHECK(e.family == Family::Tls && e.host == "::1" && e.port == 443);
}

TEST(tls_transport_serves_only_its_own_family) {
    TlsTransport t;
    CHECK(t.supports(Family::Tls));
    CHECK(!t.supports(Family::IPv4));
    CHECK(!t.supports(Family::File));

    Status s;
    CHECK(t.connect(Endpoint::ipv4("127.0.0.1", 80), Mode::ReadWrite, s) == nullptr);
    CHECK(!s.ok);
}

TEST(tls_a_server_without_a_certificate_refuses_to_listen) {
    TlsTransport t;
    Status s;
    CHECK(!t.listen(Endpoint::tls("127.0.0.1", 0), s));
    CHECK(!s.message.empty());
    CHECK(!t.listening());
}

TEST(tls_verification_is_on_by_default) {
    TlsConfig c;
    CHECK_MSG(c.verify_peer, "verification is off by default");
    CHECK_MSG(!c.insecure_skip_verify, "the escape hatch is open by default");
    CHECK(!c.require_client_certificate);
    CHECK(c.minimum_version_major == 1 && c.minimum_version_minor >= 2);
}

TEST(tls_carries_a_document_end_to_end) {
    if (!tls_available() || !certificate().ok) return;

    TlsConfig server_config;
    server_config.certificate_file = certificate().cert;
    server_config.key_file = certificate().key;
    TlsTransport server(server_config);

    Status s;
    if (!server.listen(Endpoint::tls("127.0.0.1", 0), s)) {
        CHECK_MSG(false, "%s", s.message.c_str());
        return;
    }
    const u16 port = server.bound().port;
    CHECK(port != 0);
    CHECK(server.bound().family == Family::Tls);
    server.set_timeout(15.0);   // headroom for a machine running the whole suite

    const std::vector<u8> payload = {'G', 'L', 'C', 'D', 0x01, 0xff, 0x00, 0x7f};

    std::string client_error;
    std::atomic<bool> server_read{false};
    std::thread client([&] {
        TlsConfig c;
        c.ca_file = certificate().cert;      // trust our own self-signed root
        c.server_name = "localhost";
        TlsTransport t(c);
        t.set_timeout(15.0);                 // headroom for a loaded machine
        Status cs;
        auto conn = t.connect(Endpoint::tls("127.0.0.1", port), Mode::ReadWrite, cs);
        if (!conn) {
            client_error = cs.message.empty() ? t.last_tls_error() : cs.message;
            if (client_error.empty()) client_error = "connect failed, no detail";
            return;
        }
        conn->output()->write(payload.data(), payload.size());
        conn->output()->flush();
        // Do not close yet: the server's handshake runs inside accept().
        while (!server_read.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        conn->close();
    });

    auto in = server.accept(s);
    CHECK_MSG(in != nullptr, "server: %s / %s -- client: %s", s.message.c_str(),
              server.last_tls_error().c_str(),
              client_error.empty() ? "connected" : client_error.c_str());
    if (in) {
        CHECK_MSG(in->peer().family == Family::Tls,
                  "the accepted peer is not marked encrypted");
        std::vector<u8> got(payload.size());
        std::size_t n = 0;
        while (n < got.size()) {
            const std::size_t r = in->input()->read(got.data() + n, got.size() - n);
            if (r == 0) break;
            n += r;
        }
        CHECK_MSG(n == payload.size(), "%zu of %zu bytes arrived", n, payload.size());
        CHECK_MSG(got == payload, "the bytes changed on the way through");
        in->close();
    }
    server_read = true;      // the client may hang up now
    client.join();
    server.close_listener();
}

TEST(tls_an_untrusted_certificate_is_refused) {
    if (!tls_available() || !certificate().ok) return;

    for (const bool system_trust : {false, true}) {

        TlsConfig server_config;
        server_config.certificate_file = certificate().cert;
        server_config.key_file = certificate().key;
        TlsTransport server(server_config);

        Status s;
        if (!server.listen(Endpoint::tls("127.0.0.1", 0), s)) return;
        const u16 port = server.bound().port;
        server.set_timeout(2.0);

        bool connected = false;
        std::string reason;
        std::thread client([&] {
            TlsConfig c;
            if (!system_trust) c.ca_file = "/dev/null";
            c.server_name = "localhost";
            TlsTransport t(c);
            Status cs;
            auto conn = t.connect(Endpoint::tls("127.0.0.1", port), Mode::ReadWrite, cs);
            connected = conn != nullptr;
            reason = t.last_tls_error();
            if (conn) conn->close();
        });

        auto in = server.accept(s);
        if (in) in->close();
        client.join();
        server.close_listener();

        CHECK_MSG(!connected, "an untrusted certificate was accepted");
        CHECK_MSG(!reason.empty(),
                  "the client refused the certificate without saying why");
        if (system_trust)
            CHECK_MSG(reason.find("certificate was rejected") != std::string::npos,
                      "default trust-store verification did not run: %s", reason.c_str());
    }
}

TEST(tls_the_wrong_host_name_is_refused) {
    if (!tls_available() || !certificate().ok) return;

    TlsConfig server_config;
    server_config.certificate_file = certificate().cert;
    server_config.key_file = certificate().key;
    TlsTransport server(server_config);

    Status s;
    if (!server.listen(Endpoint::tls("127.0.0.1", 0), s)) return;
    const u16 port = server.bound().port;
    server.set_timeout(2.0);

    bool connected = false;
    std::thread client([&] {
        TlsConfig c;
        c.ca_file = certificate().cert;
        c.server_name = "not-the-right-host.example.com";
        TlsTransport t(c);
        Status cs;
        auto conn = t.connect(Endpoint::tls("127.0.0.1", port), Mode::ReadWrite, cs);
        connected = conn != nullptr;
        if (conn) conn->close();
    });

    auto in = server.accept(s);
    if (in) in->close();
    client.join();
    server.close_listener();

    CHECK_MSG(!connected, "a certificate for another host was accepted");
}

TEST(tls_skipping_verification_takes_an_explicit_flag) {
    if (!tls_available() || !certificate().ok) return;

    TlsConfig server_config;
    server_config.certificate_file = certificate().cert;
    server_config.key_file = certificate().key;
    TlsTransport server(server_config);

    Status s;
    if (!server.listen(Endpoint::tls("127.0.0.1", 0), s)) return;
    const u16 port = server.bound().port;
    server.set_timeout(3.0);

    bool connected = false;
    std::thread client([&] {
        TlsConfig c;
        c.insecure_skip_verify = true;
        TlsTransport t(c);
        Status cs;
        auto conn = t.connect(Endpoint::tls("127.0.0.1", port), Mode::ReadWrite, cs);
        connected = conn != nullptr;
        if (conn) conn->close();
    });

    auto in = server.accept(s);
    if (in) in->close();
    client.join();
    server.close_listener();

    CHECK_MSG(connected,
              "the explicit escape hatch did not let an unverified connection "
              "through");
}

TEST(tls_a_trust_store_that_cannot_be_loaded_is_reported) {
    if (!tls_available()) return;
    TlsConfig c;
    c.ca_file = "/nonexistent/path/to/ca.pem";
    TlsTransport t(c);
    Status s;
    auto conn = t.connect(Endpoint::tls("127.0.0.1", 1), Mode::ReadWrite, s);
    CHECK(conn == nullptr);
    CHECK_MSG(s.message.find("trust store") != std::string::npos, "%s",
              s.message.c_str());
}

TEST(tls_and_the_session_handshake_compose) {
    if (!tls_available() || !certificate().ok) return;

    TlsConfig server_config;
    server_config.certificate_file = certificate().cert;
    server_config.key_file = certificate().key;
    TlsTransport server(server_config);

    Status s;
    if (!server.listen(Endpoint::tls("127.0.0.1", 0), s)) return;
    const u16 port = server.bound().port;
    server.set_timeout(15.0);   // headroom for a machine running the whole suite

    SessionPolicy server_policy;
    server_policy.identity = "server";
    server_policy.require_auth = true;
    server_policy.secret = "shared";

    SessionPolicy client_policy;
    client_policy.identity = "alice";
    client_policy.secret = "shared";

    Session client_session;
    std::thread client([&] {
        TlsConfig c;
        c.ca_file = certificate().cert;
        c.server_name = "localhost";
        TlsTransport t(c);
        Status cs;
        auto conn = t.connect(Endpoint::tls("127.0.0.1", port), Mode::ReadWrite, cs);
        if (!conn) return;
        client_session = connect(*conn->input(), *conn->output(), client_policy);
        conn->close();
    });

    auto in = server.accept(s);
    Session server_session;
    if (in) {
        server_session = serve(*in->input(), *in->output(), server_policy);
        in->close();
    }
    client.join();
    server.close_listener();

    CHECK_MSG(server_session.established, "server: %s",
              server_session.reason.c_str());
    CHECK_MSG(client_session.established, "client: %s",
              client_session.reason.c_str());
    CHECK(server_session.peer_identity == "alice");
}

TEST(tls_registers_under_its_own_scheme) {
    if (!tls_available()) return;
    TlsConfig c;
    register_tls_transport(c);
    auto& reg = TransportRegistry::instance();
    CHECK_MSG(reg.find(Family::Tls) != nullptr, "tls:// has no transport");
    CHECK(reg.find(Family::Tls) != reg.find(Family::IPv4));
    CHECK(!register_tls_transport(c));
}

#if defined(__APPLE__) && defined(GLCAD_TEST_TLS_NETWORK)
#include "../tls_network.hpp"

namespace {

struct NetworkRun {
    bool        connected{false};
    std::string reason;
    std::string echoed;
    bool        timed_out_cleanly{false};
};

NetworkRun network_against_loopback(const TlsConfig& client_config, bool exchange) {
    NetworkRun run;
    TlsConfig server_config;
    server_config.certificate_file = certificate().cert;
    server_config.key_file = certificate().key;
    TlsTransport server(server_config);
    Status s;
    if (!server.listen(Endpoint::tls("127.0.0.1", 0), s)) {
        run.reason = s.message;
        return run;
    }
    const u16 port = server.bound().port;
    server.set_timeout(10.0);

    std::atomic<bool> done{false};
    std::thread client([&] {
        Status cs;
        std::string error;
        auto conn = detail::network_tls_connect(client_config, Endpoint::tls("127.0.0.1", port),
                                                exchange ? 0.5 : 5.0, cs, error);
        run.connected = conn != nullptr;
        run.reason = error.empty() ? cs.message : error;
        if (conn && exchange) {
            u8 probe[4];
            run.timed_out_cleanly = conn->input()->read(probe, sizeof probe) == 0 &&
                                    !conn->input()->eof();
            const std::string hello = "hello over Network.framework";
            conn->output()->write(reinterpret_cast<const u8*>(hello.data()), hello.size());
            std::string got;
            u8 buf[64];
            for (int i = 0; i < 40 && got.size() < hello.size(); ++i) {
                const std::size_t n = conn->input()->read(buf, sizeof buf);
                got.append(reinterpret_cast<const char*>(buf), n);
            }
            run.echoed = got;
        }
        while (exchange && conn && !done.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (conn) conn->close();
    });

    auto in = server.accept(s);
    if (in && exchange) {
        std::string got;
        u8 buf[64];
        for (int i = 0; i < 40 && got.size() < 28; ++i) {
            const std::size_t n = in->input()->read(buf, sizeof buf);
            if (n == 0) break;
            got.append(reinterpret_cast<const char*>(buf), n);
        }
        in->output()->write(reinterpret_cast<const u8*>(got.data()), got.size());
        in->output()->flush();
    }
    done = true;
    client.join();
    if (in) in->close();
    server.close_listener();
    return run;
}

}

TEST(tls_network_framework_round_trips_with_a_pinned_root) {
    if (!certificate().ok) return;
    TlsConfig c;
    c.ca_file = certificate().cert;
    c.server_name = "localhost";
    const auto run = network_against_loopback(c, true);
    CHECK_MSG(run.connected, "%s", run.reason.c_str());
    CHECK_MSG(run.timed_out_cleanly, "an idle read did not time out as a short read");
    CHECK_MSG(run.echoed == "hello over Network.framework", "echo was '%s'", run.echoed.c_str());
}

TEST(tls_network_framework_refuses_untrusted_and_wrong_host) {
    if (!certificate().ok) return;
    {
        TlsConfig c;
        c.server_name = "localhost";
        const auto run = network_against_loopback(c, false);
        CHECK_MSG(!run.connected, "a self-signed certificate passed system trust");
        CHECK_MSG(run.reason.find("certificate was rejected") != std::string::npos,
                  "reason: %s", run.reason.c_str());
    }
    {
        TlsConfig c;
        c.ca_file = "/dev/null";
        c.server_name = "localhost";
        CHECK_MSG(!network_against_loopback(c, false).connected, "an empty trust store accepted");
    }
    {
        TlsConfig c;
        c.ca_file = certificate().cert;
        c.server_name = "not-the-right-host.example.com";
        CHECK_MSG(!network_against_loopback(c, false).connected, "the wrong host was accepted");
    }
}

TEST(tls_network_framework_reports_an_unreachable_server) {
    TlsConfig c;
    Status s;
    std::string error;
    auto conn = detail::network_tls_connect(c, Endpoint::tls("127.0.0.1", 1), 3.0, s, error);
    CHECK(conn == nullptr);
    CHECK_MSG(!error.empty(), "no reason given");
}
#endif
