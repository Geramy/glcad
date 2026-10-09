#include "exchange/bitcode.hpp"

#include <cstring>

namespace gdwg {
namespace {

// Reflected, polynomial 0xA001, caller-seeded, no final inversion; not zip's.
u16 crc_step(u16 crc, u8 byte) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i)
        crc = (crc & 1u) ? static_cast<u16>((crc >> 1) ^ 0xA001u)
                         : static_cast<u16>(crc >> 1);
    return crc;
}

}

u16 crc8(u16 seed, const u8* data, std::size_t size) {
    u16 crc = seed;
    for (std::size_t i = 0; i < size; ++i) crc = crc_step(crc, data[i]);
    return crc;
}

u64 BitReader::bits(int count) {
    if (count <= 0) return 0;
    if (bit_ + static_cast<std::size_t>(count) > size_ * 8) {
        ok_ = false;
        bit_ = size_ * 8;
        return 0;
    }
    u64 value = 0;
    for (int i = 0; i < count; ++i) {
        const std::size_t at = bit_ + static_cast<std::size_t>(i);
        // Most significant bit of each byte first.
        const u8 bit = static_cast<u8>((data_[at >> 3] >> (7 - (at & 7))) & 1u);
        value = (value << 1) | bit;
    }
    bit_ += static_cast<std::size_t>(count);
    return value;
}

bool BitReader::b() { return bits(1) != 0; }
u8   BitReader::bb() { return static_cast<u8>(bits(2)); }

u8 BitReader::threeb() {
    // Two bits, and a third only when the first two are 11.
    const u8 two = static_cast<u8>(bits(2));
    if (two != 3) return two;
    return static_cast<u8>(3 + bits(1));
}

u8 BitReader::rc() { return static_cast<u8>(bits(8)); }

u16 BitReader::rs() {
    // Bytes little-endian, while bits within each byte run MSB first.
    const u16 lo = static_cast<u16>(bits(8));
    const u16 hi = static_cast<u16>(bits(8));
    return static_cast<u16>(lo | (hi << 8));
}

u32 BitReader::rl() {
    const u32 lo = rs();
    const u32 hi = rs();
    return lo | (hi << 16);
}

f64 BitReader::rd() {
    u64 raw = 0;
    for (int i = 0; i < 8; ++i) raw |= static_cast<u64>(bits(8)) << (i * 8);
    f64 out;
    std::memcpy(&out, &raw, sizeof out);
    return out;
}

u16 BitReader::bs() {
    switch (bb()) {
        case 0: return rs();
        case 1: return static_cast<u16>(bits(8));
        case 2: return 0;
        // 11 is the value 256, not a width.
        default: return 256;
    }
}

u32 BitReader::bl() {
    switch (bb()) {
        case 0: return rl();
        case 1: return static_cast<u32>(bits(8));
        case 2: return 0;
        default: ok_ = false; return 0;      // 11 is not defined for BL
    }
}

f64 BitReader::bd() {
    switch (bb()) {
        case 0: return rd();
        case 1: return 1.0;
        case 2: return 0.0;
        default: ok_ = false; return 0.0;
    }
}

i32 BitReader::mc() {
    // The last group carries six magnitude bits; its bit 6 is the sign.
    u32 value = 0;
    int shift = 0;
    for (int i = 0; i < 5; ++i) {
        const u8 byte = rc();
        if ((byte & 0x80u) != 0) {
            value |= static_cast<u32>(byte & 0x7Fu) << shift;
            shift += 7;
            continue;
        }
        value |= static_cast<u32>(byte & 0x3Fu) << shift;
        return (byte & 0x40u) != 0 ? -static_cast<i32>(value)
                                   : static_cast<i32>(value);
    }
    ok_ = false;
    return 0;
}

u32 BitReader::umc() {
    u32 value = 0;
    int shift = 0;
    for (int i = 0; i < 5; ++i) {
        const u8 byte = rc();
        value |= static_cast<u32>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) return value;
        shift += 7;
    }
    ok_ = false;
    return 0;
}

u32 BitReader::ms() {
    u32 value = 0;
    int shift = 0;
    for (int i = 0; i < 4; ++i) {
        const u16 word = rs();
        value |= static_cast<u32>(word & 0x7FFFu) << shift;
        if ((word & 0x8000u) == 0) return value;
        shift += 15;
    }
    ok_ = false;
    return 0;
}

Handle BitReader::handle(u64 from) {
    // Four bits of code, then four bits of byte count.
    const u8 code = static_cast<u8>(bits(4));
    const u8 count = static_cast<u8>(bits(4));

    u64 value = 0;
    for (int i = 0; i < count; ++i) value = (value << 8) | bits(8);

    Handle h;
    h.code = code;
    switch (code) {
        case 6:    h.value = from + 1; break;
        case 8:    h.value = from - 1; break;
        case 0x0A: h.value = from + value; break;
        case 0x0C: h.value = from - value; break;
        default:   h.value = value; break;
    }
    return h;
}

std::string BitReader::tv() {
    const u16 length = bs();
    std::string out;
    out.reserve(length);
    for (u16 i = 0; i < length; ++i) out += static_cast<char>(rc());
    return out;
}

void BitReader::rd2(f64& x, f64& y) {
    x = rd();
    y = rd();
}

void BitReader::bd3(f64& x, f64& y, f64& z) {
    x = bd();
    y = bd();
    z = bd();
}

f64 BitReader::dd(f64 fallback) {
    u64 raw;
    std::memcpy(&raw, &fallback, sizeof raw);
    switch (bb()) {
        case 0:
            break;
        case 1: {
            u64 patch = 0;
            for (int i = 0; i < 4; ++i)
                patch |= static_cast<u64>(bits(8)) << (i * 8);
            raw = (raw & 0xFFFFFFFF00000000ull) | patch;
            break;
        }
        case 2: {
            // Six bytes, not in address order: bytes 4 and 5 first, then the low four.
            u64 b4 = bits(8), b5 = bits(8);
            u64 low = 0;
            for (int i = 0; i < 4; ++i)
                low |= static_cast<u64>(bits(8)) << (i * 8);
            raw = (raw & 0xFFFF000000000000ull) | (b4 << 32) | (b5 << 40) | low;
            break;
        }
        default: {
            raw = 0;
            for (int i = 0; i < 8; ++i)
                raw |= static_cast<u64>(bits(8)) << (i * 8);
            break;
        }
    }
    f64 out;
    std::memcpy(&out, &raw, sizeof out);
    return out;
}

void BitReader::be(f64& x, f64& y, f64& z) {
    if (b()) {
        x = 0.0;
        y = 0.0;
        z = 1.0;
        return;
    }
    bd3(x, y, z);
}

f64 BitReader::bt() { return b() ? 0.0 : bd(); }

void BitWriter::put(u64 value, int count) {
    for (int i = count - 1; i >= 0; --i) {
        const bool bit = ((value >> i) & 1u) != 0;
        const std::size_t byte = bit_ >> 3;
        if (byte >= data_.size()) data_.push_back(0);
        if (bit) data_[byte] |= static_cast<u8>(1u << (7 - (bit_ & 7)));
        ++bit_;
    }
}

void BitWriter::align_byte() {
    while ((bit_ & 7) != 0) put(0, 1);
}

void BitWriter::b(bool v) { put(v ? 1u : 0u, 1); }
void BitWriter::bb(u8 v) { put(v, 2); }
void BitWriter::rc(u8 v) { put(v, 8); }

void BitWriter::rs(u16 v) {
    put(v & 0xFFu, 8);
    put((v >> 8) & 0xFFu, 8);
}

void BitWriter::rl(u32 v) {
    rs(static_cast<u16>(v & 0xFFFFu));
    rs(static_cast<u16>(v >> 16));
}

void BitWriter::rd(f64 v) {
    u64 raw;
    std::memcpy(&raw, &v, sizeof raw);
    for (int i = 0; i < 8; ++i) put((raw >> (i * 8)) & 0xFFu, 8);
}

void BitWriter::bs(u16 v) {
    if (v == 0) { bb(2); return; }
    if (v == 256) { bb(3); return; }
    if (v < 256) { bb(1); put(v, 8); return; }
    bb(0);
    rs(v);
}

void BitWriter::bl(u32 v) {
    if (v == 0) { bb(2); return; }
    if (v < 256) { bb(1); put(v, 8); return; }
    bb(0);
    rl(v);
}

void BitWriter::bd(f64 v) {
    if (v == 0.0) { bb(2); return; }
    if (v == 1.0) { bb(1); return; }
    bb(0);
    rd(v);
}

void BitWriter::umc(u32 v) {
    for (int i = 0; i < 5; ++i) {
        u8 byte = static_cast<u8>(v & 0x7Fu);
        v >>= 7;
        if (v != 0) byte |= 0x80u;
        rc(byte);
        if (v == 0) return;
    }
}

void BitWriter::mc(i32 v) {
    const bool negative = v < 0;
    u32 mag = negative ? static_cast<u32>(-static_cast<i64>(v))
                       : static_cast<u32>(v);

    // The final group holds six bits; bit 6 is the sign.
    for (int i = 0; i < 4 && mag >= 0x40u; ++i) {
        rc(static_cast<u8>((mag & 0x7Fu) | 0x80u));
        mag >>= 7;
    }
    u8 last = static_cast<u8>(mag & 0x3Fu);
    if (negative) last = static_cast<u8>(last | 0x40u);
    rc(last);
}

void BitWriter::ms(u32 v) {
    for (int i = 0; i < 4; ++i) {
        u16 word = static_cast<u16>(v & 0x7FFFu);
        v >>= 15;
        if (v != 0) word = static_cast<u16>(word | 0x8000u);
        rs(word);
        if (v == 0) return;
    }
}

void BitWriter::tv(const std::string& s) {
    bs(static_cast<u16>(s.size()));
    for (char c : s) rc(static_cast<u8>(c));
}

void BitWriter::rd2(f64 x, f64 y) {
    rd(x);
    rd(y);
}

void BitWriter::bd3(f64 x, f64 y, f64 z) {
    bd(x);
    bd(y);
    bd(z);
}

void BitWriter::dd(f64 v, f64 fallback) {
    if (v == fallback) { bb(0); return; }
    bb(3);
    rd(v);
}

void BitWriter::be(f64 x, f64 y, f64 z) {
    if (x == 0.0 && y == 0.0 && z == 1.0) { b(true); return; }
    b(false);
    bd3(x, y, z);
}

void BitWriter::bt(f64 v) {
    if (v == 0.0) { b(true); return; }
    b(false);
    bd(v);
}

void BitWriter::append(const BitWriter& other) {
    const std::vector<u8>& src = other.data_;
    for (std::size_t i = 0; i < other.bit_; ++i)
        put((src[i >> 3] >> (7 - (i & 7))) & 1u, 1);
}

void BitWriter::handle(const Handle& h) {
    u8 bytes[8];
    int count = 0;
    u64 v = h.value;
    while (v != 0 && count < 8) {
        bytes[count++] = static_cast<u8>(v & 0xFFu);
        v >>= 8;
    }
    put(h.code, 4);
    put(static_cast<u64>(count), 4);
    // Most significant byte first, unlike this format's raw integers.
    for (int i = count - 1; i >= 0; --i) put(bytes[i], 8);
}

}
