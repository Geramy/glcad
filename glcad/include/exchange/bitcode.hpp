#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/config.hpp"

namespace gdwg {

using gcm::f64;
using gcm::i32;
using gcm::i64;
using gcm::u16;
using gcm::u32;
using gcm::u64;
using gcm::u8;

struct Handle {
    u8  code{0};
    u64 value{0};

    bool absolute() const noexcept { return code < 6; }
};

class BitReader {
public:
    BitReader() = default;
    BitReader(const u8* data, std::size_t size) : data_(data), size_(size) {}

    std::size_t bit_position() const noexcept { return bit_; }
    void        seek_bits(std::size_t bit) noexcept { bit_ = bit; }
    void        seek_bytes(std::size_t byte) noexcept { bit_ = byte * 8; }
    std::size_t size_bits() const noexcept { return size_ * 8; }
    bool        ok() const noexcept { return ok_; }
    bool        exhausted() const noexcept { return bit_ >= size_ * 8; }

    void align_byte() noexcept { bit_ = (bit_ + 7) & ~std::size_t{7}; }

    bool b();
    u8   bb();
    u8   threeb();

    u8   rc();
    u16  rs();
    u32  rl();
    f64  rd();

    // BS: 00 = 16 bits, 01 = 8 bits unsigned, 10 = 0, 11 = 256
    u16 bs();
    u32 bl();                       // BL : 00 = 32 bits, 01 = 8, 10 = 0
    f64 bd();                       // BD : 00 = 64 bits, 01 = 1.0, 10 = 0.0

    // MC: 7 bits per byte, low group first, high bit = more; sign is bit 6 of last
    i32 mc();
    u32 umc();
    // MS: 15 bits per group, low group first
    u32 ms();

    // Several handle codes are offsets from `from`.
    Handle handle(u64 from = 0);

    // TV (R2000): BS byte length, then the bytes; no terminator
    std::string tv();

    void rd2(f64& x, f64& y);
    void bd3(f64& x, f64& y, f64& z);
    // DD: 00 keeps default, 01 replaces low 4 bytes, 10 low 6, 11 full double
    f64  dd(f64 fallback);
    // BE: R2000 stores the common (0,0,1) as a single bit
    void be(f64& x, f64& y, f64& z);
    f64  bt();

private:
    u64 bits(int count);            // up to 64, most significant first

    const u8*   data_{nullptr};
    std::size_t size_{0};
    std::size_t bit_{0};
    bool        ok_{true};
};

class BitWriter {
public:
    const std::vector<u8>& bytes() const noexcept { return data_; }
    std::size_t bit_position() const noexcept { return bit_; }
    void        align_byte();

    void b(bool v);
    void bb(u8 v);
    void rc(u8 v);
    void rs(u16 v);
    void rl(u32 v);
    void rd(f64 v);
    void bs(u16 v);
    void bl(u32 v);
    void bd(f64 v);
    void mc(i32 v);
    void umc(u32 v);
    void ms(u32 v);
    void tv(const std::string& s);
    void rd2(f64 x, f64 y);
    void bd3(f64 x, f64 y, f64 z);
    void handle(const Handle& h);
    void dd(f64 v, f64 fallback);
    void be(f64 x, f64 y, f64 z);
    void bt(f64 v);

    void append(const BitWriter& other);

private:
    void put(u64 value, int count);

    std::vector<u8> data_;
    std::size_t     bit_{0};
};

// DWG's own CRC table, not zip's polynomial.
u16 crc8(u16 seed, const u8* data, std::size_t size);

}
