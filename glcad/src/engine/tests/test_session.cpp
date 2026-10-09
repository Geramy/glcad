#include "test.hpp"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "engine/session.hpp"
#include "engine/tcp.hpp"

using namespace gio;

namespace {

struct Handshake {
    Session server, client;
};

Handshake run(const SessionPolicy& server_policy,
              const SessionPolicy& client_policy) {
    Handshake out;

    MemoryWriter to_client;
    MemoryReader from_client;

    std::vector<u8> server_to_client, client_to_server;

    std::thread server_thread;
    std::thread client_thread;

    struct Pipe {
        std::vector<u8>   data;
        std::size_t       read_at{0};
        std::atomic<bool> closed{false};
        std::mutex        mu;
        std::condition_variable cv;

        void write(const u8* p, std::size_t n) {
            std::lock_guard<std::mutex> lock(mu);
            data.insert(data.end(), p, p + n);
            cv.notify_all();
        }
        std::size_t read(u8* p, std::size_t n) {
            std::unique_lock<std::mutex> lock(mu);
            cv.wait(lock, [&] { return read_at < data.size() || closed.load(); });
            const std::size_t have = std::min(n, data.size() - read_at);
            std::memcpy(p, data.data() + read_at, have);
            read_at += have;
            return have;
        }
        void close() {
            closed.store(true);
            cv.notify_all();
        }
    };

    auto up = std::make_shared<Pipe>();     // client -> server
    auto down = std::make_shared<Pipe>();   // server -> client

    struct PipeReader final : Reader {
        std::shared_ptr<Pipe> p;
        u64 at{0};
        std::size_t read(u8* dst, std::size_t n) override {
            const std::size_t got = p->read(dst, n);
            at += got;
            return got;
        }
        bool eof() const override { return p->closed.load(); }
        u64  position() const override { return at; }
    };
    struct PipeWriter final : Writer {
        std::shared_ptr<Pipe> p;
        u64 at{0};
        std::size_t write(const u8* src, std::size_t n) override {
            p->write(src, n);
            at += n;
            return n;
        }
        bool flush() override { return true; }
        u64  position() const override { return at; }
    };

    PipeReader server_in;  server_in.p = up;
    PipeWriter server_out; server_out.p = down;
    PipeReader client_in;  client_in.p = down;
    PipeWriter client_out; client_out.p = up;

    server_thread = std::thread([&] {
        out.server = serve(server_in, server_out, server_policy);
        up->close();
        down->close();
    });
    client_thread = std::thread([&] {
        out.client = connect(client_in, client_out, client_policy);
        up->close();
        down->close();
    });

    server_thread.join();
    client_thread.join();
    return out;
}

SessionPolicy open_policy(const char* who) {
    SessionPolicy p;
    p.identity = who;
    return p;
}

}

TEST(session_agrees_on_the_intersection) {
    const u32 mine = Capability::Documents | Capability::Deltas;
    const u32 theirs = Capability::Deltas | Capability::Commands;
    const u32 both = agreed_capabilities(mine, theirs);
    CHECK(has_capability(both, Capability::Deltas));
    CHECK(!has_capability(both, Capability::Documents));
    CHECK(!has_capability(both, Capability::Commands));
}

TEST(session_an_unknown_capability_from_a_newer_peer_is_not_an_error) {
    const u32 mine = static_cast<u32>(Capability::Documents);
    const u32 theirs = static_cast<u32>(Capability::Documents) | (1u << 30);
    const u32 both = agreed_capabilities(mine, theirs);
    CHECK(both == static_cast<u32>(Capability::Documents));
}

TEST(session_capability_names_and_description) {
    CHECK(std::string(capability_name(Capability::Deltas)) == "deltas");
    const std::string d = describe_capabilities(Capability::Documents |
                                                Capability::Deltas);
    CHECK(d.find("documents") != std::string::npos);
    CHECK(d.find("deltas") != std::string::npos);
    CHECK(describe_capabilities(0) == "none");
}

TEST(session_falls_back_to_the_lower_version) {
    SessionPolicy p;
    p.version = 3;
    p.minimum_version = 1;
    u16 agreed = 0;
    std::string why;
    CHECK(version_compatible(p, 2, agreed, why));
    CHECK_MSG(agreed == 2, "agreed on version %u", agreed);
    CHECK(version_compatible(p, 5, agreed, why));
    CHECK_MSG(agreed == 3, "agreed on version %u, above our own", agreed);
}

TEST(session_refuses_a_peer_below_the_minimum_and_says_why) {
    SessionPolicy p;
    p.version = 3;
    p.minimum_version = 2;
    u16 agreed = 0;
    std::string why;
    CHECK(!version_compatible(p, 1, agreed, why));
    CHECK_MSG(why.find("older") != std::string::npos, "the reason was '%s'",
              why.c_str());
}

TEST(auth_digest_depends_on_every_input) {
    const std::array<u8, 16> nonce{1, 2, 3};
    const auto base = auth_digest("secret", nonce, "alice");

    CHECK(!digests_equal(base, auth_digest("secre", nonce, "alice")));
    CHECK(!digests_equal(base, auth_digest("secret", nonce, "bob")));

    std::array<u8, 16> other = nonce;
    other[15] ^= 1;
    CHECK_MSG(!digests_equal(base, auth_digest("secret", other, "alice")),
              "one bit of the challenge did not change the proof");

    CHECK(digests_equal(base, auth_digest("secret", nonce, "alice")));
}

TEST(auth_digest_length_is_part_of_the_input) {
    const std::array<u8, 16> nonce{};
    CHECK(!digests_equal(auth_digest("ab", nonce, "cd"),
                         auth_digest("abc", nonce, "d")));
    CHECK(!digests_equal(auth_digest("", nonce, "abcd"),
                         auth_digest("abcd", nonce, "")));
}

TEST(auth_digest_spreads_a_single_bit) {
    const std::array<u8, 16> nonce{};
    const auto a = auth_digest("secret", nonce, "alice");
    const auto b = auth_digest("secret", nonce, "alicf");   // one bit
    int differing = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        u8 x = static_cast<u8>(a[i] ^ b[i]);
        while (x) { differing += x & 1; x = static_cast<u8>(x >> 1); }
    }
    CHECK_MSG(differing > 80, "only %d of 256 bits changed", differing);
}

TEST(digest_comparison_is_constant_time_in_shape) {
    std::array<u8, 32> a{}, b{};
    CHECK(digests_equal(a, b));
    b[31] = 1;
    CHECK(!digests_equal(a, b));
    b[31] = 0;
    b[0] = 1;
    CHECK(!digests_equal(a, b));
}

TEST(nonce_is_different_every_time) {
    std::vector<std::array<u8, 16>> seen;
    for (int i = 0; i < 200; ++i) {
        const auto n = make_nonce();
        for (const auto& old : seen)
            CHECK_MSG(!std::equal(n.begin(), n.end(), old.begin()),
                      "a nonce repeated");
        seen.push_back(n);
    }
}

TEST(hello_round_trips) {
    Hello h;
    h.version = 2;
    h.capabilities = Capability::Documents | Capability::Commands;
    h.application = "GLCAD Desktop";
    h.identity = "alice@studio";
    h.nonce = make_nonce();

    Hello back;
    CHECK(Hello::decode(h.encode(), back));
    CHECK(back.version == 2);
    CHECK(back.capabilities == h.capabilities);
    CHECK(back.application == "GLCAD Desktop");
    CHECK(back.identity == "alice@studio");
    CHECK(std::equal(h.nonce.begin(), h.nonce.end(), back.nonce.begin()));
}

TEST(hello_rejects_a_truncated_message_rather_than_reading_past_it) {
    Hello h;
    h.identity = "alice";
    h.nonce = make_nonce();
    const auto full = h.encode();

    for (std::size_t n = 0; n < full.size(); ++n) {
        const std::vector<u8> cut(full.begin(), full.begin() + static_cast<long>(n));
        Hello back;
        CHECK_MSG(!Hello::decode(cut, back), "%zu of %zu bytes decoded", n,
                  full.size());
    }
    Hello back;
    CHECK(Hello::decode(full, back));
}

TEST(hello_refuses_an_absurd_length_from_the_wire) {
    std::vector<u8> bad;
    bad.push_back(1); bad.push_back(0);            // version
    for (int i = 0; i < 4; ++i) bad.push_back(0);  // capabilities
    bad.push_back(0xFF); bad.push_back(0xFF);
    bad.push_back(0xFF); bad.push_back(0xFF);      // a four-gigabyte string
    Hello out;
    CHECK(!Hello::decode(bad, out));
}

TEST(auth_proof_round_trips_and_rejects_a_truncation) {
    AuthProof p;
    p.identity = "alice";
    p.proof = auth_digest("s", make_nonce(), "alice");
    AuthProof back;
    CHECK(AuthProof::decode(p.encode(), back));
    CHECK(back.identity == "alice");
    CHECK(digests_equal(p.proof, back.proof));

    const auto full = p.encode();
    const std::vector<u8> cut(full.begin(), full.end() - 1);
    CHECK(!AuthProof::decode(cut, back));
}

TEST(session_open_endpoints_establish) {
    const Handshake h = run(open_policy("server"), open_policy("client"));
    CHECK_MSG(h.server.established, "server: %s", h.server.reason.c_str());
    CHECK_MSG(h.client.established, "client: %s", h.client.reason.c_str());
    CHECK(h.server.peer_identity == "client");
    CHECK(h.client.peer_identity == "server");
    CHECK(h.server.version == kProtocolVersion);
}

TEST(session_both_sides_agree_on_the_same_capabilities) {
    SessionPolicy server = open_policy("server");
    server.capabilities = Capability::Documents | Capability::Deltas;
    SessionPolicy client = open_policy("client");
    client.capabilities = Capability::Deltas | Capability::Commands;

    const Handshake h = run(server, client);
    CHECK(h.server.established && h.client.established);
    CHECK_MSG(h.server.capabilities == h.client.capabilities,
              "the two sides disagree about what was agreed");
    CHECK(h.server.can(Capability::Deltas));
    CHECK(!h.server.can(Capability::Documents));
    CHECK(!h.client.can(Capability::Commands));
}

TEST(session_authentication_succeeds_with_the_right_secret) {
    SessionPolicy server = open_policy("server");
    server.require_auth = true;
    server.secret = "correct horse battery staple";
    SessionPolicy client = open_policy("alice");
    client.secret = "correct horse battery staple";

    const Handshake h = run(server, client);
    CHECK_MSG(h.server.established, "server: %s", h.server.reason.c_str());
    CHECK_MSG(h.client.established, "client: %s", h.client.reason.c_str());
    CHECK_MSG(h.server.peer_identity == "alice",
              "the server thinks the peer is '%s'", h.server.peer_identity.c_str());
}

TEST(session_authentication_fails_with_the_wrong_secret) {
    SessionPolicy server = open_policy("server");
    server.require_auth = true;
    server.secret = "the real secret";
    SessionPolicy client = open_policy("mallory");
    client.secret = "a guess";

    const Handshake h = run(server, client);
    CHECK_MSG(!h.server.established, "an unauthenticated peer got in");
    CHECK(h.server.reason.find("authentication") != std::string::npos);
    CHECK_MSG(!h.client.established, "the client thought it was in");
}

TEST(session_a_client_with_no_secret_cannot_reach_a_guarded_server) {
    SessionPolicy server = open_policy("server");
    server.require_auth = true;
    server.secret = "the real secret";
    SessionPolicy client = open_policy("mallory");   // no secret at all

    const Handshake h = run(server, client);
    CHECK(!h.server.established);
    CHECK(!h.client.established);
}

TEST(session_a_server_that_wants_auth_and_has_no_secret_refuses_itself) {
    SessionPolicy server = open_policy("server");
    server.require_auth = true;      // and no secret
    SessionPolicy client = open_policy("client");
    client.secret = "anything";

    const Handshake h = run(server, client);
    CHECK(!h.server.established);
    CHECK_MSG(h.server.reason.find("secret") != std::string::npos, "%s",
              h.server.reason.c_str());
}

TEST(session_a_version_mismatch_is_refused_on_both_sides) {
    SessionPolicy server = open_policy("server");
    server.version = 1;
    SessionPolicy client = open_policy("client");
    client.version = 1;
    client.minimum_version = 5;      // the client insists on something newer

    const Handshake h = run(server, client);
    CHECK_MSG(!h.client.established, "the client accepted an old server");
    CHECK(!h.client.reason.empty());
}

TEST(session_a_proof_is_useless_against_a_second_server) {
    const std::string secret = "shared";
    const auto first = make_nonce();
    const auto second = make_nonce();
    const auto proof = auth_digest(secret, first, "alice");
    const auto expected_elsewhere = auth_digest(secret, second, "alice");
    CHECK_MSG(!digests_equal(proof, expected_elsewhere),
              "the same proof authenticates two different sessions");
}

TEST(session_reports_the_peer_application) {
    SessionPolicy server = open_policy("server");
    server.application = "GLCAD Server";
    SessionPolicy client = open_policy("client");
    client.application = "GLCAD Desktop";

    const Handshake h = run(server, client);
    CHECK(h.server.established && h.client.established);
    CHECK(h.server.peer_application == "GLCAD Desktop");
    CHECK(h.client.peer_application == "GLCAD Server");
}

TEST(session_can_returns_false_before_it_is_established) {
    Session s;
    CHECK(!s.established);
    CHECK(!s.can(Capability::Documents));
    s.capabilities = static_cast<u32>(Capability::Documents);
    CHECK_MSG(!s.can(Capability::Documents),
              "an unestablished session claimed a capability");
}

TEST(session_a_server_advertises_that_it_wants_a_proof) {
    Hello h;
    h.requires_auth = true;
    h.nonce = make_nonce();
    Hello back;
    CHECK(Hello::decode(h.encode(), back));
    CHECK_MSG(back.requires_auth, "the demand did not survive the wire");

    h.requires_auth = false;
    CHECK(Hello::decode(h.encode(), back));
    CHECK(!back.requires_auth);
}

TEST(session_a_client_with_no_secret_fails_fast_rather_than_hanging) {
    SessionPolicy server = open_policy("server");
    server.require_auth = true;
    server.secret = "the real secret";
    SessionPolicy client = open_policy("nobody");

    const Handshake h = run(server, client);
    CHECK(!h.client.established);
    CHECK_MSG(h.client.reason.find("requires authentication") != std::string::npos,
              "the client said '%s'", h.client.reason.c_str());
    CHECK(!h.server.established);
}

TEST(session_a_proof_is_sent_only_when_it_was_asked_for) {
    SessionPolicy server = open_policy("server");     // open
    SessionPolicy client = open_policy("alice");
    client.secret = "unnecessary";

    const Handshake h = run(server, client);
    CHECK_MSG(h.server.established, "server: %s", h.server.reason.c_str());
    CHECK_MSG(h.client.established, "client: %s", h.client.reason.c_str());
}

TEST(session_a_client_that_requires_auth_refuses_an_open_server) {
    SessionPolicy server = open_policy("server");     // open
    SessionPolicy client = open_policy("alice");
    client.require_auth = true;
    client.secret = "shared";

    const Handshake h = run(server, client);
    CHECK_MSG(!h.client.established, "the client accepted an open server");
    CHECK_MSG(h.client.reason.find("does not authenticate") != std::string::npos,
              "the client said '%s'", h.client.reason.c_str());
}

TEST(session_negotiates_over_a_real_socket) {
    TcpTransport server;
    Status s;
    if (!server.listen(Endpoint::ipv4("127.0.0.1", 0), s)) {
        CHECK_MSG(false, "%s", s.message.c_str());
        return;
    }
    const u16 port = server.bound().port;
    server.set_timeout(3.0);

    SessionPolicy server_policy;
    server_policy.identity = "server";
    server_policy.require_auth = true;
    server_policy.secret = "shared";
    server_policy.capabilities = Capability::Documents | Capability::Deltas;

    SessionPolicy client_policy;
    client_policy.identity = "alice";
    client_policy.secret = "shared";
    client_policy.capabilities = Capability::Deltas | Capability::Commands;

    Session client_session;
    std::thread client([&] {
        TcpTransport t;
        Status cs;
        auto conn = t.connect(Endpoint::ipv4("127.0.0.1", port), Mode::ReadWrite, cs);
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
    CHECK_MSG(server_session.capabilities == client_session.capabilities,
              "the two sides disagree about what was agreed");
    CHECK(server_session.can(Capability::Deltas));
    CHECK(!server_session.can(Capability::Commands));
}

TEST(session_a_wrong_secret_is_refused_over_a_real_socket) {
    TcpTransport server;
    Status s;
    if (!server.listen(Endpoint::ipv4("127.0.0.1", 0), s)) return;
    const u16 port = server.bound().port;
    server.set_timeout(3.0);

    SessionPolicy server_policy;
    server_policy.identity = "server";
    server_policy.require_auth = true;
    server_policy.secret = "the real secret";

    SessionPolicy client_policy;
    client_policy.identity = "mallory";
    client_policy.secret = "a guess";

    Session client_session;
    std::thread client([&] {
        TcpTransport t;
        Status cs;
        auto conn = t.connect(Endpoint::ipv4("127.0.0.1", port), Mode::ReadWrite, cs);
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

    CHECK_MSG(!server_session.established, "a guessed secret got in");
    CHECK(!client_session.established);
}

TEST(auth_digest_does_not_collide_on_inputs_eight_bytes_apart) {
    const std::array<u8, 16> nonce{};
    std::vector<std::array<u8, 32>> seen;
    for (int i = 0; i < 64; ++i) {
        // Inputs differing only eight bytes apart: what the broken indexing loses.
        std::string secret(32, 'a');
        secret[static_cast<std::size_t>(i % 32)] =
            static_cast<char>('a' + (i / 32) + 1);
        const auto d = auth_digest(secret, nonce, "id");
        for (const auto& old : seen)
            CHECK_MSG(!digests_equal(d, old),
                      "two different secrets produced the same proof");
        seen.push_back(d);
    }
}

TEST(auth_digest_uses_every_byte_of_a_long_input) {
    const std::array<u8, 16> nonce{1, 2, 3, 4};
    const std::string base(32, 'x');
    const auto reference = auth_digest(base, nonce, "alice");
    for (std::size_t i = 0; i < base.size(); ++i) {
        std::string other = base;
        other[i] = 'y';
        CHECK_MSG(!digests_equal(reference, auth_digest(other, nonce, "alice")),
                  "byte %zu of the secret made no difference", i);
    }
}
