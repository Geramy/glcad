#pragma once

#include <array>
#include <string>
#include <vector>

#include "engine/frame.hpp"

namespace gio {

inline constexpr u16 kProtocolVersion = 1;

enum class Capability : u32 {
    None        = 0,
    Documents   = 1u << 0,
    Deltas      = 1u << 1,
    Transactions= 1u << 2,
    Thumbnails  = 1u << 3,
    Commands    = 1u << 4,
    Compression = 1u << 5,
};

inline constexpr u32 operator|(Capability a, Capability b) noexcept {
    return static_cast<u32>(a) | static_cast<u32>(b);
}
inline constexpr bool has_capability(u32 set, Capability c) noexcept {
    return (set & static_cast<u32>(c)) != 0;
}
const char* capability_name(Capability c) noexcept;
std::string describe_capabilities(u32 set);

struct Hello {
    u16         version{kProtocolVersion};
    u32         capabilities{0};
    std::string application{"GLCAD"};
    std::string identity;

    bool requires_auth{false};
    // The responder's challenge, so a replayed response cannot authenticate.
    std::array<u8, 16> nonce{};

    std::vector<u8> encode() const;
    static bool     decode(const std::vector<u8>& in, Hello& out);
};

struct AuthProof {
    std::string        identity;
    std::array<u8, 32> proof{};

    std::vector<u8> encode() const;
    static bool     decode(const std::vector<u8>& in, AuthProof& out);
};

struct Session {
    bool        established{false};
    u16         version{0};
    u32         capabilities{0};
    std::string peer_application;
    std::string peer_identity;
    std::string reason;

    bool can(Capability c) const noexcept {
        return established && has_capability(capabilities, c);
    }
};

struct SessionPolicy {
    u16 version{kProtocolVersion};
    u16 minimum_version{1};
    u32 capabilities{static_cast<u32>(Capability::Documents) |
                     static_cast<u32>(Capability::Deltas) |
                     static_cast<u32>(Capability::Transactions)};
    std::string application{"GLCAD"};
    std::string identity;

    std::string secret;
    bool        require_auth{false};
};

std::array<u8, 32> auth_digest(std::string_view secret,
                               const std::array<u8, 16>& nonce,
                               std::string_view identity);

// Constant time: an early-exit compare leaks the matching prefix.
bool digests_equal(const std::array<u8, 32>& a,
                   const std::array<u8, 32>& b) noexcept;

// Unique per session, not cryptographically random.
std::array<u8, 16> make_nonce();

Session serve(Reader& in, Writer& out, const SessionPolicy& p);

Session connect(Reader& in, Writer& out, const SessionPolicy& p);

u32 agreed_capabilities(u32 mine, u32 theirs) noexcept;
bool version_compatible(const SessionPolicy& p, u16 theirs, u16& agreed,
                        std::string& reason);

}
