#include "test.hpp"

#include <cstring>
#include <string>
#include <thread>

#include "engine/frame.hpp"
#include "engine/tcp.hpp"

using namespace gio;

namespace {
std::vector<u8> bytes(const char* s) {
    return std::vector<u8>(reinterpret_cast<const u8*>(s),
                           reinterpret_cast<const u8*>(s) + std::strlen(s));
}
}

TEST(tcp_registering_fills_the_seam_that_was_left_open) {
    auto& reg = TransportRegistry::instance();
    register_tcp_transport();
    CHECK_MSG(reg.find(Family::IPv4) != nullptr,
              "ipv4 is still unserved after registering");

    CHECK(!register_tcp_transport());
}

TEST(tcp_endpoint_urls_now_resolve_to_a_transport) {
    register_tcp_transport();
    auto& reg = TransportRegistry::instance();
    Status s;
    auto c = reg.open("ipv4://127.0.0.1:1", Mode::ReadWrite, s);
    CHECK(c == nullptr);
    CHECK_MSG(s.message.find("no transport") == std::string::npos,
              "still unserved: %s", s.message.c_str());
}

TEST(tcp_listen_reports_the_port_it_actually_got) {
    TcpTransport t;
    Status s;
    CHECK_MSG(t.listen(Endpoint::ipv4("127.0.0.1", 0), s), "%s", s.message.c_str());
    CHECK(t.listening());
    CHECK_MSG(t.bound().port != 0, "the bound port was not reported back");
    t.close_listener();
    CHECK(!t.listening());
}

TEST(tcp_a_document_crosses_a_real_socket_unchanged) {
    TcpTransport server;
    Status s;
    CHECK_MSG(server.listen(Endpoint::ipv4("127.0.0.1", 0), s), "%s",
              s.message.c_str());
    server.set_timeout(3.0);
    const u16 port = server.bound().port;

    const auto payload = bytes("a whole document would go here");

    std::thread client([&] {
        TcpTransport out;
        Status cs;
        auto conn = out.connect(Endpoint::ipv4("127.0.0.1", port), Mode::ReadWrite,
                                cs);
        if (!conn || !conn->output()) return;
        write_frame(*conn->output(), FrameKind::Document, payload);
        conn->close();
    });

    Status as;
    auto incoming = server.accept(as);
    CHECK_MSG(incoming != nullptr, "%s", as.message.c_str());

    if (incoming) {
        CHECK(incoming->connected());
        CHECK(incoming->input() != nullptr);
        std::vector<u8> got;
        const auto res = read_frame(*incoming->input(), got);
        CHECK_MSG(res.ok, "%s", res.error.c_str());
        CHECK(res.header.kind == FrameKind::Document);
        CHECK_MSG(got == payload, "the payload changed in flight");
        incoming->close();
    }

    client.join();
    server.close_listener();
}

TEST(tcp_a_short_read_is_normal_and_framing_handles_it) {
    TcpTransport server;
    Status s;
    CHECK(server.listen(Endpoint::ipv4("127.0.0.1", 0), s));
    server.set_timeout(3.0);
    const u16 port = server.bound().port;

    std::vector<u8> big(300000);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<u8>(i * 7);

    std::thread client([&] {
        TcpTransport out;
        Status cs;
        auto conn = out.connect(Endpoint::ipv4("127.0.0.1", port), Mode::ReadWrite,
                                cs);
        if (!conn || !conn->output()) return;
        write_frame(*conn->output(), FrameKind::Document, big);
        conn->close();
    });

    Status as;
    auto incoming = server.accept(as);
    CHECK_MSG(incoming != nullptr, "%s", as.message.c_str());
    if (incoming) {
        std::vector<u8> got;
        const auto res = read_frame(*incoming->input(), got);
        CHECK_MSG(res.ok, "%s", res.error.c_str());
        CHECK_MSG(got.size() == big.size(), "%zu of %zu bytes arrived", got.size(),
                  big.size());
        CHECK(got == big);
        incoming->close();
    }
    client.join();
    server.close_listener();
}

TEST(tcp_several_frames_arrive_in_order_on_one_connection) {
    TcpTransport server;
    Status s;
    CHECK(server.listen(Endpoint::ipv4("127.0.0.1", 0), s));
    server.set_timeout(3.0);
    const u16 port = server.bound().port;

    std::thread client([&] {
        TcpTransport out;
        Status cs;
        auto conn = out.connect(Endpoint::ipv4("127.0.0.1", port), Mode::ReadWrite,
                                cs);
        if (!conn || !conn->output()) return;
        write_frame(*conn->output(), FrameKind::Hello, bytes("glcad/1"));
        write_frame(*conn->output(), FrameKind::Document, bytes("the drawing"));
        write_frame(*conn->output(), FrameKind::Ack, nullptr, 0);
        conn->close();
    });

    Status as;
    auto incoming = server.accept(as);
    CHECK(incoming != nullptr);
    if (incoming) {
        std::vector<u8> p;
        auto a = read_frame(*incoming->input(), p);
        CHECK(a.ok && a.header.kind == FrameKind::Hello);
        auto b = read_frame(*incoming->input(), p);
        CHECK(b.ok && b.header.kind == FrameKind::Document);
        CHECK(p == bytes("the drawing"));
        auto c = read_frame(*incoming->input(), p);
        CHECK(c.ok && c.header.kind == FrameKind::Ack);
        CHECK_MSG(read_frame(*incoming->input(), p).end_of_stream,
                  "the closed connection read as corrupt");
        incoming->close();
    }
    client.join();
    server.close_listener();
}

TEST(tcp_connect_to_nothing_fails_with_a_useful_message) {
    TcpTransport t;
    Status s;
    auto c = t.connect(Endpoint::ipv4("127.0.0.1", 1), Mode::ReadWrite, s);
    CHECK(c == nullptr);
    CHECK(!s.ok);
    CHECK_MSG(!s.message.empty(), "the failure said nothing");
}

TEST(tcp_a_hostname_is_refused_rather_than_resolved) {
    TcpTransport t;
    Status s;
    auto c = t.connect(Endpoint::ipv4("example.invalid", 80), Mode::ReadWrite, s);
    CHECK(c == nullptr);
    CHECK_MSG(s.message.find("dotted") != std::string::npos, "%s",
              s.message.c_str());
}

TEST(tcp_accept_without_listening_is_refused) {
    TcpTransport t;
    Status s;
    CHECK(t.accept(s) == nullptr);
    CHECK(s.message.find("listening") != std::string::npos);
}

TEST(tcp_accept_times_out_rather_than_hanging) {
    TcpTransport t;
    Status s;
    CHECK(t.listen(Endpoint::ipv4("127.0.0.1", 0), s));
    t.set_timeout(0.05);
    CHECK(t.accept(s) == nullptr);
    CHECK_MSG(s.message.find("timeout") != std::string::npos, "%s",
              s.message.c_str());
    t.close_listener();
}

TEST(tcp_refuses_the_families_it_does_not_serve) {
    TcpTransport t;
    CHECK(t.supports(Family::IPv4));
    CHECK(t.supports(Family::IPv6));
    CHECK(!t.supports(Family::File));
    CHECK(!t.supports(Family::Memory));

    Status s;
    CHECK(t.connect(Endpoint::file("/tmp/x"), Mode::Write, s) == nullptr);
    CHECK(!s.ok);

    CHECK(t.connect({Family::IPv6, "not-an-address", 80}, Mode::ReadWrite, s) ==
          nullptr);
    CHECK_MSG(s.message.find("IPv6 address") != std::string::npos, "%s",
              s.message.c_str());
}

TEST(ipv6_urls_bracket_the_address) {
    Endpoint e;
    CHECK(Endpoint::parse("ipv6://[::1]:5911", e));
    CHECK(e.family == Family::IPv6);
    CHECK_MSG(e.host == "::1", "host came out as '%s'", e.host.c_str());
    CHECK(e.port == 5911);
    CHECK_MSG(e.to_string() == "ipv6://[::1]:5911", "printed as '%s'",
              e.to_string().c_str());

    CHECK(Endpoint::parse("ipv6://[2001:db8::8a2e:370:7334]:443", e));
    CHECK(e.host == "2001:db8::8a2e:370:7334");
    CHECK(e.port == 443);
    CHECK(e.to_string() == "ipv6://[2001:db8::8a2e:370:7334]:443");
}

TEST(ipv6_an_unbracketed_address_is_refused_not_guessed) {
    // Guessing at the last colon turns "::1" into host ":" port "1".
    Endpoint e;
    CHECK(!Endpoint::parse("ipv6://::1:5911", e));
    CHECK(!Endpoint::parse("ipv6://[::1]", e));      // no port
    CHECK(!Endpoint::parse("ipv6://[::1]:", e));
    CHECK(!Endpoint::parse("ipv6://[::1:5911", e));  // no closing bracket
    CHECK(!Endpoint::parse("ipv6://[::1]:0", e));
    CHECK(!Endpoint::parse("ipv6://[::1]:99999", e));
}

TEST(ipv6_endpoints_store_the_address_without_brackets) {
    const Endpoint a = Endpoint::ipv6("[::1]", 80);
    const Endpoint b = Endpoint::ipv6("::1", 80);
    CHECK(a.host == "::1");
    CHECK(b.host == "::1");
    CHECK(a.to_string() == b.to_string());
}

TEST(ipv6_wildcard_is_recognised_in_both_families) {
    CHECK(Endpoint::ipv4("0.0.0.0", 1).is_wildcard());
    CHECK(Endpoint::ipv4("*", 1).is_wildcard());
    CHECK(!Endpoint::ipv4("127.0.0.1", 1).is_wildcard());
    CHECK(Endpoint::ipv6("::", 1).is_wildcard());
    CHECK(Endpoint::ipv6("*", 1).is_wildcard());
    CHECK(!Endpoint::ipv6("::1", 1).is_wildcard());
    CHECK(!Endpoint::file("/tmp/x").is_wildcard());
}

TEST(ipv6_a_document_travels_over_a_v6_socket) {
    if (!ipv6_available()) {
        return;
    }

    TcpTransport server;
    Status s;
    CHECK_MSG(server.listen(Endpoint::ipv6("::1", 0), s), "%s",
              s.message.c_str());
    const Endpoint bound = server.bound();
    CHECK_MSG(bound.port != 0, "the listener never reported its port");
    CHECK(bound.family == Family::IPv6);

    server.set_timeout(3.0);

    const std::vector<u8> payload = {'G', 'L', 'C', 'D', 0x01, 0xff, 0x00, 0x7f};
    std::thread client([&] {
        TcpTransport t;
        Status cs;
        auto conn = t.connect(Endpoint::ipv6("::1", bound.port), Mode::ReadWrite, cs);
        if (!conn) return;
        conn->output()->write(payload.data(), payload.size());
        conn->output()->flush();
        conn->close();
    });

    auto in = server.accept(s);
    CHECK_MSG(in != nullptr, "%s", s.message.c_str());
    if (in) {
        CHECK_MSG(in->peer().family == Family::IPv6, "peer family was %s",
                  family_name(in->peer().family));
        std::vector<u8> got(payload.size());
        std::size_t n = 0;
        while (n < got.size()) {
            const std::size_t r = in->input()->read(got.data() + n, got.size() - n);
            if (r == 0) break;
            n += r;
        }
        CHECK_MSG(n == payload.size(), "%zu of %zu bytes arrived", n,
                  payload.size());
        CHECK_MSG(got == payload, "the bytes changed on the way through");
        in->close();
    }
    client.join();
    server.close_listener();
}

TEST(ipv6_a_v6_only_listener_does_not_answer_on_v4) {
    if (!ipv6_available()) return;

    TcpTransport server;
    Status s;
    CHECK(server.listen(Endpoint::ipv6("::", 0), s));
    const u16 port = server.bound().port;
    CHECK(port != 0);
    server.set_timeout(0.2);

    TcpTransport client;
    Status cs;
    auto conn = client.connect(Endpoint::ipv4("127.0.0.1", port), Mode::ReadWrite, cs);
    CHECK_MSG(conn == nullptr,
              "a v6-only listener answered an IPv4 client");
    server.close_listener();
}

TEST(ipv6_a_bad_address_reports_the_address_not_the_family) {
    TcpTransport t;
    Status s;
    CHECK(t.connect(Endpoint::ipv6("192.168.1.1", 80), Mode::ReadWrite, s) ==
          nullptr);
    CHECK_MSG(s.message.find("IPv6 address") != std::string::npos, "%s",
              s.message.c_str());

    CHECK(t.connect(Endpoint::ipv4("::1", 80), Mode::ReadWrite, s) == nullptr);
    CHECK(s.message.find("IPv4") != std::string::npos);
}

TEST(ipv6_a_zone_index_is_stripped_rather_than_failing_obscurely) {
    if (!ipv6_available()) return;
    TcpTransport t;
    Status s;
    t.set_timeout(0.2);
    t.connect(Endpoint::ipv6("::1%lo0", 1), Mode::ReadWrite, s);
    CHECK_MSG(s.message.find("not an IPv6 address") == std::string::npos, "%s",
              s.message.c_str());
}

TEST(ipv6_the_registry_serves_both_families_through_one_transport) {
    register_tcp_transport();
    auto& reg = TransportRegistry::instance();
    CHECK(reg.find(Family::IPv4) != nullptr);
    CHECK_MSG(reg.find(Family::IPv6) != nullptr,
              "ipv6:// URLs have no transport");
    CHECK(reg.find(Family::IPv4) == reg.find(Family::IPv6));
}
