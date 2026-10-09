#pragma once

#include <vector>

#include "engine/stream.hpp"

namespace gio {

inline constexpr u32 kFrameMagic = 0x4443474Cu;   // 'LGCD', little-endian
inline constexpr u16 kFrameVersion = 1;
inline constexpr std::size_t kFrameHeaderSize = 24;

enum class FrameKind : u16 {
    Unknown   = 0,
    Document  = 1,
    Delta     = 2,
    Thumbnail = 3,
    Extension = 4,
    Transaction = 5,
    Cache     = 6,
    Manifest  = 7,
    Hello     = 16,   // the network kinds start at 16
    Ack       = 17,
    Command   = 18,
    Sync      = 19,
};

struct FrameHeader {
    u32       magic{kFrameMagic};
    u16       version{kFrameVersion};
    FrameKind kind{FrameKind::Unknown};
    u64       length{0};
    u32       crc{0};
    u32       reserved{0};

    bool valid() const noexcept {
        return magic == kFrameMagic && version <= kFrameVersion;
    }
};

u32 crc32(const u8* data, std::size_t n, u32 seed = 0);
u32 crc32(const std::vector<u8>& v, u32 seed = 0);

bool write_frame(Writer& w, FrameKind kind, const u8* payload, std::size_t n);
bool write_frame(Writer& w, FrameKind kind, const std::vector<u8>& payload);

struct ReadFrameResult {
    bool        ok{false};
    bool        end_of_stream{false};
    FrameHeader header{};
    std::string error;
};
ReadFrameResult read_frame(Reader& r, std::vector<u8>& payload);

bool skip_frame_payload(Reader& r, const FrameHeader& h);

}
