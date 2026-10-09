#pragma once

#include "math/bigint.hpp"
#include "math/config.hpp"
#include "math/scalar.hpp"

namespace gcm {

struct SoftF64 : ScalarBase<SoftF64> {
    u64 b{0};   // raw IEEE-754 binary64 encoding

    static constexpr int  mantissa_bits = 53;
    static constexpr f64  eps           = 2.220446049250313e-16;
    static constexpr f64  filter_eps    = 1.1102230246251565e-16;
    static constexpr bool correctly_rounded = true;

    constexpr SoftF64() noexcept = default;
    constexpr explicit SoftF64(u64 bits) noexcept : b(bits) {}

    static SoftF64 from(f64 x) noexcept {
        SoftF64 r;
        __builtin_memcpy(&r.b, &x, sizeof(u64));
        return r;
    }
    f64 to_f64() const noexcept {
        f64 x;
        __builtin_memcpy(&x, &b, sizeof(f64));
        return x;
    }

    static constexpr u32 sign_of_bits(u64 v) noexcept { return u32(v >> 63); }
    static constexpr i32 exp_of(u64 v) noexcept { return i32((v >> 52) & 0x7FF); }
    static constexpr u64 frac_of(u64 v) noexcept { return v & 0xFFFFFFFFFFFFFull; }

    static constexpr bool is_nan(u64 v) noexcept {
        return exp_of(v) == 0x7FF && frac_of(v) != 0;
    }
    static constexpr bool is_inf(u64 v) noexcept {
        return exp_of(v) == 0x7FF && frac_of(v) == 0;
    }
    static constexpr bool is_zero(u64 v) noexcept { return (v << 1) == 0; }

    static constexpr u64 pack(u32 s, u64 exp, u64 frac) noexcept {
        return (u64(s) << 63) | (exp << 52) | frac;
    }

    static constexpr u64 kQNaN = 0x7FF8000000000000ull;
    static constexpr u64 inf(u32 s) noexcept { return pack(s, 0x7FF, 0); }
    static constexpr u64 zero(u32 s) noexcept { return pack(s, 0, 0); }

    // value = (-1)^s * m * 2^(e - 1075); subnormals use e = 1, no implicit bit.
    static void unpack(u64 v, u32& s, i32& e, u64& m) noexcept {
        s = sign_of_bits(v);
        e = exp_of(v);
        m = frac_of(v);
        if (e == 0) e = 1;
        else        m |= (1ull << 52);
    }

    // M has 3 guard bits: value = M * 2^(e - 1075 - 3). Round half to even.
    static u64 round_pack(u32 s, i32 e, u64 M, bool extra_sticky) noexcept {
        if (extra_sticky) M |= 1;

        // Normalise into [2^55, 2^56): 53 significand bits plus 3 guard bits.
        while (M >= (1ull << 56)) {
            const u64 sticky = M & 1;
            M = (M >> 1) | sticky;
            ++e;
        }
        while (M && M < (1ull << 55) && e > 1) {
            M <<= 1;
            --e;
        }

        // Subnormal range: shift right, accumulating sticky, until e == 1.
        if (e < 1) {
            const i32 shift = 1 - e;
            if (shift >= 64) return zero(s);
            const u64 sticky = (M & ((1ull << shift) - 1)) ? 1ull : 0ull;
            M = (M >> shift) | sticky;
            e = 1;
        }

        // Round to nearest, ties to even, on the 3 guard bits.
        const u64 guard = M & 7;
        u64 sig = M >> 3;
        if (guard > 4 || (guard == 4 && (sig & 1))) {
            ++sig;
            if (sig >= (1ull << 53)) {   // rounding carried out of the significand
                sig >>= 1;
                ++e;
            }
        }

        if (sig == 0) return zero(s);

        // Reached the implicit bit: normal; otherwise subnormal, exponent 0.
        if (sig < (1ull << 52)) return pack(s, 0, sig);

        if (e >= 0x7FF) return inf(s);
        return pack(s, u64(e), sig & 0xFFFFFFFFFFFFFull);
    }

    static SoftF64 add(SoftF64 x, SoftF64 y) noexcept {
        const u64 a = x.b, c = y.b;

        if (is_nan(a) || is_nan(c)) return SoftF64(kQNaN);
        if (is_inf(a)) {
            if (is_inf(c) && sign_of_bits(a) != sign_of_bits(c)) return SoftF64(kQNaN);
            return SoftF64(a);
        }
        if (is_inf(c)) return SoftF64(c);
        if (is_zero(a) && is_zero(c)) {
            // -0 + -0 = -0; every other zero pair gives +0 under RNE.
            return SoftF64((sign_of_bits(a) && sign_of_bits(c)) ? zero(1) : zero(0));
        }
        if (is_zero(a)) return SoftF64(c);
        if (is_zero(c)) return SoftF64(a);

        u32 sa, sc; i32 ea, ec; u64 ma, mc;
        unpack(a, sa, ea, ma);
        unpack(c, sc, ec, mc);

        if (ea < ec || (ea == ec && ma < mc)) {
            u32 ts = sa; sa = sc; sc = ts;
            i32 te = ea; ea = ec; ec = te;
            u64 tm = ma; ma = mc; mc = tm;
        }

        u64 Ma = ma << 3;
        u64 Mc = mc << 3;

        const i32 shift = ea - ec;
        bool sticky = false;
        if (shift >= 64) {
            sticky = (Mc != 0);
            Mc = 0;
        } else if (shift > 0) {
            sticky = (Mc & ((1ull << shift) - 1)) != 0;
            Mc >>= shift;
        }

        u64 M;
        if (sa == sc) {
            M = Ma + Mc;
            if (sticky) M |= 1;
        } else {
            // Borrow the sticky out of the subtrahend before subtracting.
            if (sticky) {
                if (Mc == 0) { Mc = 1; }        // becomes a tiny positive part
                M = Ma - Mc;
                if (M == 0) return SoftF64(zero(0));
                M -= 1;
                M |= 1;                          // result is inexact
            } else {
                M = Ma - Mc;
            }
            if (M == 0) return SoftF64(zero(0)); // exact cancellation -> +0 (RNE)
        }
        return SoftF64(round_pack(sa, ea, M, false));
    }

    static SoftF64 neg(SoftF64 x) noexcept {
        return SoftF64(x.b ^ (1ull << 63));
    }
    static SoftF64 sub(SoftF64 x, SoftF64 y) noexcept { return add(x, neg(y)); }

    static SoftF64 mul(SoftF64 x, SoftF64 y) noexcept {
        const u64 a = x.b, c = y.b;
        const u32 s = sign_of_bits(a) ^ sign_of_bits(c);

        if (is_nan(a) || is_nan(c)) return SoftF64(kQNaN);
        if (is_inf(a)) return SoftF64(is_zero(c) ? kQNaN : inf(s));
        if (is_inf(c)) return SoftF64(is_zero(a) ? kQNaN : inf(s));
        if (is_zero(a) || is_zero(c)) return SoftF64(zero(s));

        u32 sa, sc; i32 ea, ec; u64 ma, mc;
        unpack(a, sa, ea, ma);
        unpack(c, sc, ec, mc);

        u64 hi, lo;
        umul64(ma, mc, lo, hi);          // 106-bit product in (hi, lo)

        // (hi:lo) * 2^(ea+ec-2150) -> M * 2^(e-1078), M in [2^55, 2^56).
        i32 e = ea + ec - 1075 + 3;

        u64 M;
        bool sticky = false;
        {
            int top = 0;
            if (hi) {
                top = 64;
                u64 t = hi;
                while (t >>= 1) ++top;
            } else {
                top = 0;
                u64 t = lo;
                while (t >>= 1) ++top;
                ++top;
            }
            const int shift = top - 56;
            if (shift > 0) {
                if (shift >= 64) {
                    const int sh = shift - 64;
                    sticky = (lo != 0) || (sh > 0 && (hi & ((1ull << sh) - 1)) != 0);
                    M = (sh >= 64) ? 0 : (hi >> sh);
                } else {
                    sticky = (lo & ((1ull << shift) - 1)) != 0;
                    M = (lo >> shift) | (shift ? (hi << (64 - shift)) : 0);
                }
                e += shift;
            } else {
                const int l = -shift;
                M = (l >= 64) ? 0 : (lo << l);
                e -= l;
            }
        }
        return SoftF64(round_pack(s, e, M, sticky));
    }

    static SoftF64 div(SoftF64 x, SoftF64 y) noexcept {
        const u64 a = x.b, c = y.b;
        const u32 s = sign_of_bits(a) ^ sign_of_bits(c);

        if (is_nan(a) || is_nan(c)) return SoftF64(kQNaN);
        if (is_inf(a)) return SoftF64(is_inf(c) ? kQNaN : inf(s));
        if (is_inf(c)) return SoftF64(zero(s));
        if (is_zero(c)) return SoftF64(is_zero(a) ? kQNaN : inf(s));
        if (is_zero(a)) return SoftF64(zero(s));

        u32 sa, sc; i32 ea, ec; u64 ma, mc;
        unpack(a, sa, ea, ma);
        unpack(c, sc, ec, mc);

        while (ma < (1ull << 55)) { ma <<= 1; --ea; }
        while (mc < (1ull << 55)) { mc <<= 1; --ec; }

        // 56 quotient bits: 53 significand + 3 guard.
        u64 q = 0, rem = ma;
        for (int i = 0; i < 56; ++i) {
            q <<= 1;
            if (rem >= mc) { rem -= mc; q |= 1; }
            // rem < mc < 2^56, so shifting the remainder cannot overflow bit 63.
            rem <<= 1;
        }
        const bool sticky = (rem != 0);

        i32 e = ea - ec + 1075 + 3 - 55;
        return SoftF64(round_pack(s, e, q, sticky));
    }

    static SoftF64 fma(SoftF64 a, SoftF64 b, SoftF64 c) noexcept {
        return add(mul(a, b), c);   // NOT a fused op
    }

    static SoftF64 abs(SoftF64 a) noexcept {
        return SoftF64(a.b & ~(1ull << 63));
    }

    // Scale by 2^58 so the integer root lands in [2^55, 2^56) for round_pack.
    static SoftF64 sqrt(SoftF64 x) noexcept {
        const u64 a = x.b;
        if (is_nan(a)) return SoftF64(kQNaN);
        if (is_zero(a)) return SoftF64(a);
        if (sign_of_bits(a)) return SoftF64(kQNaN);
        if (is_inf(a)) return SoftF64(a);

        u32 s; i32 e; u64 m;
        unpack(a, s, e, m);

        while (m < (1ull << 52)) { m <<= 1; --e; }

        // E must be even for the scale's root to be exact.
        i32 E = e - 1075;
        if (E & 1) { m <<= 1; --E; }                  // now m in [2^52, 2^54)

        const u64 hi = m >> 6;
        const u64 lo = m << 58;

        // Digit recurrence, two bits per step, 56 steps.
        u64 root = 0, rem = 0;
        for (int i = 55; i >= 0; --i) {
            const int shift = 2 * i;
            const u64 two = (shift >= 64) ? ((hi >> (shift - 64)) & 3ull)
                                          : ((lo >> shift) & 3ull);
            rem = (rem << 2) | two;
            const u64 trial = (root << 2) | 1ull;
            root <<= 1;
            if (rem >= trial) { rem -= trial; root |= 1ull; }
        }
        const bool sticky = (rem != 0);

        // root ~= sqrt(m) * 2^29; round_pack reads M * 2^(ep - 1078).
        const i32 ep = E / 2 + 1049;
        return SoftF64(round_pack(0, ep, root, sticky));
    }

    static int cmp(SoftF64 x, SoftF64 y) noexcept {
        const u64 a = x.b, c = y.b;
        if (is_nan(a) || is_nan(c)) return 2;          // unordered
        if (is_zero(a) && is_zero(c)) return 0;        // +0 == -0

        const bool na = sign_of_bits(a) != 0;
        const bool nc = sign_of_bits(c) != 0;
        if (na != nc) return na ? -1 : 1;

        const u64 ma = a & ~(1ull << 63);
        const u64 mc = c & ~(1ull << 63);
        if (ma == mc) return 0;
        const bool less = na ? (ma > mc) : (ma < mc);
        return less ? -1 : 1;
    }
};

static_assert(Scalar<SoftF64>);
static_assert(FilterScalar<SoftF64>);

}
