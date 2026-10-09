#include "engine/frame.hpp"

namespace gio {

namespace {

const u32* crc_table() {
    static u32 t[256];
    static bool built = false;
    if (!built) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        built = true;
    }
    return t;
}

}

u32 crc32(const u8* data, std::size_t n, u32 seed) {
    const u32* t = crc_table();
    u32 c = seed ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i)
        c = t[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

u32 crc32(const std::vector<u8>& v, u32 seed) {
    return crc32(v.data(), v.size(), seed);
}

bool write_frame(Writer& w, FrameKind kind, const u8* payload, std::size_t n) {
    if (!w.write_u32(kFrameMagic)) return false;
    if (!w.write_u16(kFrameVersion)) return false;
    if (!w.write_u16(static_cast<u16>(kind))) return false;
    if (!w.write_u64(static_cast<u64>(n))) return false;
    if (!w.write_u32(crc32(payload, n))) return false;
    if (!w.write_u32(0)) return false;                 // reserved
    return n == 0 || w.write_all(payload, n);
}

bool write_frame(Writer& w, FrameKind kind, const std::vector<u8>& payload) {
    return write_frame(w, kind, payload.data(), payload.size());
}

ReadFrameResult read_frame(Reader& r, std::vector<u8>& payload) {
    ReadFrameResult res;
    payload.clear();

    u8 head[kFrameHeaderSize];
    std::size_t got = 0;
    while (got < kFrameHeaderSize) {
        const std::size_t k = r.read(head + got, kFrameHeaderSize - got);
        if (k == 0) {
            if (got == 0) {
                res.end_of_stream = true;
                return res;
            }
            res.error = "truncated frame header";
            return res;
        }
        got += k;
    }

    MemoryReader hr(head, kFrameHeaderSize);
    u16 kind_raw = 0;
    hr.read_u32(res.header.magic);
    hr.read_u16(res.header.version);
    hr.read_u16(kind_raw);
    hr.read_u64(res.header.length);
    hr.read_u32(res.header.crc);
    hr.read_u32(res.header.reserved);
    res.header.kind = static_cast<FrameKind>(kind_raw);

    if (res.header.magic != kFrameMagic) {
        res.error = "not a GLCAD frame";
        return res;
    }
    if (res.header.version > kFrameVersion) {
        res.error = "frame is from a newer version";
        return res;
    }

    constexpr u64 kMaxPayload = 1ull << 32;   // 4 GB
    if (res.header.length > kMaxPayload) {
        res.error = "frame length is implausible";
        return res;
    }

    payload.resize(static_cast<std::size_t>(res.header.length));
    if (res.header.length &&
        !r.read_exact(payload.data(), payload.size())) {
        res.error = "truncated frame payload";
        payload.clear();
        return res;
    }

    if (crc32(payload) != res.header.crc) {
        res.error = "frame checksum mismatch";
        payload.clear();
        return res;
    }

    res.ok = true;
    return res;
}

bool skip_frame_payload(Reader& r, const FrameHeader& h) {
    if (h.length == 0) return true;
    if (r.seekable()) return r.seek(r.position() + h.length);

    u8 sink[8192];
    u64 left = h.length;
    while (left) {
        const std::size_t want =
            static_cast<std::size_t>(left < sizeof(sink) ? left : sizeof(sink));
        const std::size_t got = r.read(sink, want);
        if (got == 0) return false;
        left -= got;
    }
    return true;
}

}
