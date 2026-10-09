#pragma once

#include <array>

#include "math/config.hpp"

namespace gcm {

inline void umul64(u64 a, u64 b, u64& lo, u64& hi) noexcept {
    const u64 a0 = a & 0xFFFFFFFFull, a1 = a >> 32;
    const u64 b0 = b & 0xFFFFFFFFull, b1 = b >> 32;

    const u64 p00 = a0 * b0;
    const u64 p01 = a0 * b1;
    const u64 p10 = a1 * b0;
    const u64 p11 = a1 * b1;

    const u64 mid = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);

    lo = (p00 & 0xFFFFFFFFull) | (mid << 32);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

template <class Limb>
struct LimbOps;

template <>
struct LimbOps<u32> {
    static constexpr int bits = 32;
    static void mul(u32 a, u32 b, u32& lo, u32& hi) noexcept {
        const u64 p = static_cast<u64>(a) * static_cast<u64>(b);
        lo = static_cast<u32>(p);
        hi = static_cast<u32>(p >> 32);
    }
};

template <>
struct LimbOps<u64> {
    static constexpr int bits = 64;
    static void mul(u64 a, u64 b, u64& lo, u64& hi) noexcept { umul64(a, b, lo, hi); }
};

template <int BITS, class Limb = u64>
struct SInt {
    using LimbType = Limb;
    static constexpr int limb_bits = LimbOps<Limb>::bits;
    static constexpr int N         = BITS / limb_bits;
    static constexpr int bits      = BITS;
    static_assert(BITS % limb_bits == 0, "BITS must be a multiple of the limb width");
    static_assert(N >= 2, "use a native integer for single-limb values");

    std::array<Limb, N> l{};

    constexpr SInt() = default;

    static constexpr SInt from_i64(i64 v) noexcept {
        SInt r;
        const u64  uv  = static_cast<u64>(v);
        const Limb ext = (v < 0) ? static_cast<Limb>(~Limb{0}) : Limb{0};
        for (int i = 0; i < N; ++i) {
            const int shift = i * limb_bits;
            r.l[i] = (shift < 64) ? static_cast<Limb>(uv >> shift) : ext;
        }
        return r;
    }

    constexpr bool is_negative() const noexcept {
        return (l[N - 1] >> (limb_bits - 1)) != 0;
    }
    constexpr bool is_zero() const noexcept {
        for (int i = 0; i < N; ++i)
            if (l[i] != 0) return false;
        return true;
    }
    constexpr Sign sign() const noexcept {
        if (is_negative()) return Sign::Negative;
        return is_zero() ? Sign::Zero : Sign::Positive;
    }
};

template <int B, class L>
constexpr SInt<B, L> add(const SInt<B, L>& a, const SInt<B, L>& b) noexcept {
    SInt<B, L> r;
    L carry = 0;
    for (int i = 0; i < SInt<B, L>::N; ++i) {
        const L s1 = static_cast<L>(a.l[i] + b.l[i]);
        const L c1 = (s1 < a.l[i]) ? L{1} : L{0};
        const L s2 = static_cast<L>(s1 + carry);
        const L c2 = (s2 < s1) ? L{1} : L{0};
        r.l[i] = s2;
        carry  = static_cast<L>(c1 + c2);
    }
    return r;
}

template <int B, class L>
constexpr SInt<B, L> negate(const SInt<B, L>& a) noexcept {
    SInt<B, L> r;
    L carry = 1;
    for (int i = 0; i < SInt<B, L>::N; ++i) {
        const L v = static_cast<L>(static_cast<L>(~a.l[i]) + carry);
        carry  = (carry == 1 && v == 0) ? L{1} : L{0};
        r.l[i] = v;
    }
    return r;
}

template <int B, class L>
constexpr SInt<B, L> sub(const SInt<B, L>& a, const SInt<B, L>& b) noexcept {
    return add(a, negate(b));
}

template <int B, class L>
inline SInt<B, L> umul_small(const SInt<B, L>& a, L m) noexcept {
    SInt<B, L> r;
    L carry = 0;
    for (int i = 0; i < SInt<B, L>::N; ++i) {
        L lo, hi;
        LimbOps<L>::mul(a.l[i], m, lo, hi);
        const L s = static_cast<L>(lo + carry);
        const L c = (s < lo) ? L{1} : L{0};
        r.l[i] = s;
        carry  = static_cast<L>(hi + c);
    }
    return r;
}

// Correct for i64 min, where -v would overflow.
inline u64 abs_to_u64(i64 v) noexcept {
    return (v < 0) ? (static_cast<u64>(-(v + 1)) + 1ull) : static_cast<u64>(v);
}

template <int B, class L>
inline SInt<B, L> mul_i64(SInt<B, L> a, i64 m) noexcept {
    bool neg = false;
    if (a.is_negative()) { a = negate(a); neg = !neg; }
    if (m < 0) neg = !neg;

    const u64 mag = abs_to_u64(m);
    SInt<B, L> r;
    if constexpr (LimbOps<L>::bits >= 64) {
        r = umul_small(a, static_cast<L>(mag));
    } else {
        // Split the multiplier across limbs: r = a*lo + (a*hi << limb_bits).
        const L lo = static_cast<L>(mag & 0xFFFFFFFFull);
        const L hi = static_cast<L>(mag >> 32);
        r = umul_small(a, lo);
        if (hi != 0) {
            SInt<B, L> t = umul_small(a, hi);
            SInt<B, L> shifted;
            for (int i = SInt<B, L>::N - 1; i >= 1; --i) shifted.l[i] = t.l[i - 1];
            shifted.l[0] = 0;
            r = add(r, shifted);
        }
    }
    return neg ? negate(r) : r;
}

template <int B, class L = u64>
inline SInt<B, L> mul_i64_i64(i64 a, i64 b) noexcept {
    return mul_i64(SInt<B, L>::from_i64(a), b);
}

}
