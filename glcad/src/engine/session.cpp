#include "engine/session.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#if !defined(__APPLE__)
#include <random>
#endif

namespace gio {
namespace {

void put16(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
}
void put32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (i * 8)) & 0xFF));
}
void put_string(std::vector<u8>& v, const std::string& s) {
    const u32 n = static_cast<u32>(std::min<std::size_t>(s.size(), 4096));
    put32(v, n);
    v.insert(v.end(), s.begin(), s.begin() + n);
}

bool take(const std::vector<u8>& v, std::size_t& at, std::size_t n, const u8*& p) {
    if (at + n > v.size()) return false;
    p = v.data() + at;
    at += n;
    return true;
}
bool get16(const std::vector<u8>& v, std::size_t& at, u32& out) {
    const u8* p = nullptr;
    if (!take(v, at, 2, p)) return false;
    out = static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8);
    return true;
}
bool get32(const std::vector<u8>& v, std::size_t& at, u32& out) {
    const u8* p = nullptr;
    if (!take(v, at, 4, p)) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) out |= static_cast<u32>(p[i]) << (i * 8);
    return true;
}
bool get_string(const std::vector<u8>& v, std::size_t& at, std::string& out) {
    u32 n = 0;
    if (!get32(v, at, n)) return false;
    if (n > 4096) return false;
    const u8* p = nullptr;
    if (!take(v, at, n, p)) return false;
    out.assign(reinterpret_cast<const char*>(p), n);
    return true;
}

}

const char* capability_name(Capability c) noexcept {
    switch (c) {
        case Capability::None:         return "none";
        case Capability::Documents:    return "documents";
        case Capability::Deltas:       return "deltas";
        case Capability::Transactions: return "transactions";
        case Capability::Thumbnails:   return "thumbnails";
        case Capability::Commands:     return "commands";
        case Capability::Compression:  return "compression";
    }
    return "";
}

std::string describe_capabilities(u32 set) {
    static constexpr Capability kAll[] = {
        Capability::Documents, Capability::Deltas, Capability::Transactions,
        Capability::Thumbnails, Capability::Commands, Capability::Compression,
    };
    std::string out;
    for (Capability c : kAll) {
        if (!has_capability(set, c)) continue;
        if (!out.empty()) out += ", ";
        out += capability_name(c);
    }
    return out.empty() ? "none" : out;
}

std::vector<u8> Hello::encode() const {
    std::vector<u8> v;
    put16(v, version);
    put32(v, capabilities);
    put_string(v, application);
    put_string(v, identity);
    v.push_back(requires_auth ? 1 : 0);
    v.insert(v.end(), nonce.begin(), nonce.end());
    return v;
}

bool Hello::decode(const std::vector<u8>& in, Hello& out) {
    std::size_t at = 0;
    u32 version = 0, caps = 0;
    if (!get16(in, at, version)) return false;
    if (!get32(in, at, caps)) return false;
    if (!get_string(in, at, out.application)) return false;
    if (!get_string(in, at, out.identity)) return false;
    const u8* flag = nullptr;
    if (!take(in, at, 1, flag)) return false;
    out.requires_auth = *flag != 0;
    const u8* p = nullptr;
    if (!take(in, at, out.nonce.size(), p)) return false;
    std::memcpy(out.nonce.data(), p, out.nonce.size());
    out.version = static_cast<u16>(version);
    out.capabilities = caps;
    return true;
}

std::vector<u8> AuthProof::encode() const {
    std::vector<u8> v;
    put_string(v, identity);
    v.insert(v.end(), proof.begin(), proof.end());
    return v;
}

bool AuthProof::decode(const std::vector<u8>& in, AuthProof& out) {
    std::size_t at = 0;
    if (!get_string(in, at, out.identity)) return false;
    const u8* p = nullptr;
    if (!take(in, at, out.proof.size(), p)) return false;
    std::memcpy(out.proof.data(), p, out.proof.size());
    return true;
}

namespace {

struct Sponge {
    u64 s[4] = {0x243F6A8885A308D3ull, 0x13198A2E03707344ull,
                0xA4093822299F31D0ull, 0x082EFA98EC4E6C89ull};

    void mix() {
        for (int round = 0; round < 12; ++round) {
            s[0] += s[1]; s[1] = (s[1] << 13) | (s[1] >> 51); s[1] ^= s[0];
            s[2] += s[3]; s[3] = (s[3] << 37) | (s[3] >> 27); s[3] ^= s[2];
            s[0] += s[3]; s[3] = (s[3] << 17) | (s[3] >> 47); s[3] ^= s[0];
            s[2] += s[1]; s[1] = (s[1] << 29) | (s[1] >> 35); s[1] ^= s[2];
            std::swap(s[0], s[2]);
            s[0] ^= 0x9E3779B97F4A7C15ull + static_cast<u64>(round);
        }
    }
    void absorb(const u8* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            // Index limbs by groups of eight bytes; `i & 3` made inputs cancel out.
            s[(i >> 3) & 3] ^= static_cast<u64>(p[i]) << ((i & 7) * 8);
            if ((i & 31) == 31) mix();
        }
        // Mix in the length so a prefix cannot reproduce a longer input's digest.
        s[3] ^= static_cast<u64>(n) * 0x9E3779B97F4A7C15ull;
        mix();
    }
};

}

std::array<u8, 32> auth_digest(std::string_view secret,
                               const std::array<u8, 16>& nonce,
                               std::string_view identity) {
    Sponge sp;
    sp.absorb(reinterpret_cast<const u8*>(secret.data()), secret.size());
    sp.absorb(nonce.data(), nonce.size());
    sp.absorb(reinterpret_cast<const u8*>(identity.data()), identity.size());

    std::array<u8, 32> out{};
    for (int block = 0; block < 4; ++block) {
        for (int byte = 0; byte < 8; ++byte)
            out[static_cast<std::size_t>(block * 8 + byte)] =
                static_cast<u8>((sp.s[block] >> (byte * 8)) & 0xFF);
    }
    return out;
}

bool digests_equal(const std::array<u8, 32>& a,
                   const std::array<u8, 32>& b) noexcept {
    // Constant time: an early-exit compare leaks the matching prefix.
    u8 diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

std::array<u8, 16> make_nonce() {
    std::array<u8, 16> out{};
#if defined(__APPLE__)
    arc4random_buf(out.data(), out.size());
#else
    // Failure propagates; never fall back to clock material.
    std::random_device random;
    std::uniform_int_distribution<unsigned> byte(0, 255);
    for (auto& value : out) value = static_cast<u8>(byte(random));
#endif
    return out;
}

u32 agreed_capabilities(u32 mine, u32 theirs) noexcept {
    return mine & theirs;
}

bool version_compatible(const SessionPolicy& p, u16 theirs, u16& agreed,
                        std::string& reason) {
    if (theirs < p.minimum_version) {
        reason = "the peer speaks version " + std::to_string(theirs) +
                 ", older than the minimum this build supports (" +
                 std::to_string(p.minimum_version) + ")";
        return false;
    }
    agreed = std::min(p.version, theirs);
    return true;
}

namespace {

bool send(Writer& out, FrameKind kind, const std::vector<u8>& payload) {
    return write_frame(out, kind, payload);
}

bool receive(Reader& in, FrameKind expect, std::vector<u8>& payload,
             std::string& reason) {
    const ReadFrameResult r = read_frame(in, payload);
    if (!r.ok) {
        reason = r.end_of_stream ? "the peer closed the connection"
                                 : ("the frame was rejected: " + r.error);
        return false;
    }
    if (r.header.kind != expect) {
        reason = "the peer sent an unexpected frame during the handshake";
        return false;
    }
    return true;
}

Session fail(std::string reason) {
    Session s;
    s.reason = std::move(reason);
    return s;
}

}

Session serve(Reader& in, Writer& out, const SessionPolicy& p) {
    Hello mine;
    mine.version = p.version;
    mine.capabilities = p.capabilities;
    mine.application = p.application;
    mine.identity = p.identity;
    mine.requires_auth = p.require_auth;
    mine.nonce = make_nonce();

    if (p.require_auth && p.secret.empty()) {
        // Fail closed, before the greeting.
        std::vector<u8> no;
        no.push_back(0);
        send(out, FrameKind::Ack, no);
        out.flush();
        return fail("authentication is required but no secret is configured");
    }

    if (!send(out, FrameKind::Hello, mine.encode()) || !out.flush())
        return fail("could not send the greeting");

    std::vector<u8> payload;
    std::string reason;
    if (!receive(in, FrameKind::Hello, payload, reason)) return fail(reason);

    Hello theirs;
    if (!Hello::decode(payload, theirs))
        return fail("the peer's greeting was malformed");

    Session s;
    if (!version_compatible(p, theirs.version, s.version, s.reason)) {
        std::vector<u8> no;
        no.push_back(0);
        send(out, FrameKind::Ack, no);
        out.flush();
        return s;
    }

    if (p.require_auth) {
        if (!receive(in, FrameKind::Ack, payload, reason)) return fail(reason);

        AuthProof proof;
        if (!AuthProof::decode(payload, proof))
            return fail("the peer's proof was malformed");

        const auto expected = auth_digest(p.secret, mine.nonce, proof.identity);
        if (!digests_equal(expected, proof.proof)) {
            std::vector<u8> no;
            no.push_back(0);
            send(out, FrameKind::Ack, no);
            out.flush();
            return fail("authentication failed");
        }
        s.peer_identity = proof.identity;
    } else {
        s.peer_identity = theirs.identity;
    }

    s.capabilities = agreed_capabilities(p.capabilities, theirs.capabilities);
    s.peer_application = theirs.application;
    s.established = true;

    std::vector<u8> yes;
    yes.push_back(1);
    if (!send(out, FrameKind::Ack, yes) || !out.flush())
        return fail("could not confirm the session");
    return s;
}

Session connect(Reader& in, Writer& out, const SessionPolicy& p) {
    std::vector<u8> payload;
    std::string reason;
    if (!receive(in, FrameKind::Hello, payload, reason)) return fail(reason);

    Hello theirs;
    if (!Hello::decode(payload, theirs))
        return fail("the peer's greeting was malformed");

    Session s;
    if (!version_compatible(p, theirs.version, s.version, s.reason)) return s;

    Hello mine;
    mine.version = p.version;
    mine.capabilities = p.capabilities;
    mine.application = p.application;
    mine.identity = p.identity;
    mine.requires_auth = p.require_auth;
    mine.nonce = make_nonce();

    if (theirs.requires_auth && p.secret.empty())
        return fail("the server requires authentication and no secret is "
                    "configured");

    if (!send(out, FrameKind::Hello, mine.encode()) || !out.flush())
        return fail("could not send the greeting");

    // Only when asked: an unsolicited proof frame would be read as a document.
    if (theirs.requires_auth) {
        AuthProof proof;
        proof.identity = p.identity;
        proof.proof = auth_digest(p.secret, theirs.nonce, p.identity);
        if (!send(out, FrameKind::Ack, proof.encode()) || !out.flush())
            return fail("could not send the proof");
    } else if (p.require_auth) {
        return fail("the server does not authenticate and this client requires "
                    "it");
    }

    if (!receive(in, FrameKind::Ack, payload, reason)) return fail(reason);
    if (payload.empty() || payload[0] != 1)
        return fail("the peer refused the session");

    s.capabilities = agreed_capabilities(p.capabilities, theirs.capabilities);
    s.peer_application = theirs.application;
    s.peer_identity = theirs.identity;
    s.established = true;
    return s;
}

}
