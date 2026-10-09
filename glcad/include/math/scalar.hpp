#pragma once

#include <cmath>
#include <concepts>

#include "math/config.hpp"
#include "math/eft.hpp"

namespace gcm {

template <class D>
struct ScalarBase {
    const D& self() const noexcept { return static_cast<const D&>(*this); }

    friend D operator+(D a, D b) noexcept { return D::add(a, b); }
    friend D operator-(D a, D b) noexcept { return D::sub(a, b); }
    friend D operator*(D a, D b) noexcept { return D::mul(a, b); }
    friend D operator/(D a, D b) noexcept { return D::div(a, b); }
    friend D operator-(D a) noexcept { return D::neg(a); }

    friend D& operator+=(D& a, D b) noexcept { return a = D::add(a, b); }
    friend D& operator-=(D& a, D b) noexcept { return a = D::sub(a, b); }
    friend D& operator*=(D& a, D b) noexcept { return a = D::mul(a, b); }
    friend D& operator/=(D& a, D b) noexcept { return a = D::div(a, b); }

    friend bool operator==(D a, D b) noexcept { return D::cmp(a, b) == 0; }
    friend bool operator!=(D a, D b) noexcept { return D::cmp(a, b) != 0; }
    friend bool operator< (D a, D b) noexcept { return D::cmp(a, b) <  0; }
    friend bool operator<=(D a, D b) noexcept { return D::cmp(a, b) <= 0; }
    friend bool operator> (D a, D b) noexcept { return D::cmp(a, b) >  0; }
    friend bool operator>=(D a, D b) noexcept { return D::cmp(a, b) >= 0; }
};

template <class D> inline D abs(D a) noexcept { return D::abs(a); }
template <class D> inline D sqrt(D a) noexcept { return D::sqrt(a); }
template <class D> inline D min(D a, D b) noexcept { return D::cmp(a, b) <= 0 ? a : b; }
template <class D> inline D max(D a, D b) noexcept { return D::cmp(a, b) >= 0 ? a : b; }
template <class D> inline Sign sign(D a) noexcept {
    return sign_of(static_cast<i32>(D::cmp(a, D::from(0.0))));
}

template <class S>
concept Scalar = requires(S a, S b) {
    { S::add(a, b) }    -> std::same_as<S>;
    { S::sub(a, b) }    -> std::same_as<S>;
    { S::mul(a, b) }    -> std::same_as<S>;
    { S::div(a, b) }    -> std::same_as<S>;
    { S::fma(a, b, a) } -> std::same_as<S>;
    { S::neg(a) }       -> std::same_as<S>;
    { S::abs(a) }       -> std::same_as<S>;
    { S::sqrt(a) }      -> std::same_as<S>;
    { S::cmp(a, b) }    -> std::same_as<int>;
    { S::from(0.0) }    -> std::same_as<S>;
    { a.to_f64() }      -> std::same_as<f64>;
    { S::mantissa_bits } -> std::convertible_to<int>;
};

struct F32 : ScalarBase<F32> {
    f32 v;

    static constexpr int  mantissa_bits = 24;
    static constexpr f64  eps           = 1.1920928955078125e-07; // 2^-23
    static constexpr f64  filter_eps    = 5.9604644775390625e-08; // 2^-24
    static constexpr bool correctly_rounded = true;

    constexpr F32() noexcept : v(0.0f) {}
    constexpr explicit F32(f32 x) noexcept : v(x) {}

    static constexpr F32 from(f64 x) noexcept { return F32(static_cast<f32>(x)); }
    constexpr f64 to_f64() const noexcept { return static_cast<f64>(v); }

    static constexpr F32 add(F32 a, F32 b) noexcept { return F32(a.v + b.v); }
    static constexpr F32 sub(F32 a, F32 b) noexcept { return F32(a.v - b.v); }
    static constexpr F32 mul(F32 a, F32 b) noexcept { return F32(a.v * b.v); }
    static constexpr F32 div(F32 a, F32 b) noexcept { return F32(a.v / b.v); }
    static constexpr F32 neg(F32 a) noexcept { return F32(-a.v); }
    static F32 fma(F32 a, F32 b, F32 c) noexcept { return F32(std::fmaf(a.v, b.v, c.v)); }
    static F32 abs(F32 a) noexcept { return F32(std::fabs(a.v)); }
    static F32 sqrt(F32 a) noexcept { return F32(std::sqrt(a.v)); }
    static constexpr int cmp(F32 a, F32 b) noexcept {
        return a.v < b.v ? -1 : (a.v > b.v ? 1 : 0);
    }
};

struct F64 : ScalarBase<F64> {
    f64 v;

    static constexpr int mantissa_bits = 53;
    static constexpr f64 eps           = 2.220446049250313e-16; // 2^-52
    static constexpr f64 filter_eps    = 1.1102230246251565e-16; // 2^-53
    static constexpr bool correctly_rounded = true;

    constexpr F64() noexcept : v(0.0) {}
    constexpr explicit F64(f64 x) noexcept : v(x) {}

    static constexpr F64 from(f64 x) noexcept { return F64(x); }
    constexpr f64 to_f64() const noexcept { return v; }

    static constexpr F64 add(F64 a, F64 b) noexcept { return F64(a.v + b.v); }
    static constexpr F64 sub(F64 a, F64 b) noexcept { return F64(a.v - b.v); }
    static constexpr F64 mul(F64 a, F64 b) noexcept { return F64(a.v * b.v); }
    static constexpr F64 div(F64 a, F64 b) noexcept { return F64(a.v / b.v); }
    static constexpr F64 neg(F64 a) noexcept { return F64(-a.v); }
    static F64 fma(F64 a, F64 b, F64 c) noexcept { return F64(std::fma(a.v, b.v, c.v)); }
    static F64 abs(F64 a) noexcept { return F64(std::fabs(a.v)); }
    static F64 sqrt(F64 a) noexcept { return F64(std::sqrt(a.v)); }
    static constexpr int cmp(F64 a, F64 b) noexcept {
        return a.v < b.v ? -1 : (a.v > b.v ? 1 : 0);
    }
};

// DF64: unevaluated hi + lo with |lo| <= ulp(hi)/2, ~48 bits.
struct DF64 : ScalarBase<DF64> {
    f32 hi;
    f32 lo;

    static constexpr int mantissa_bits = 48;
    static constexpr f64 eps           = 3.552713678800501e-15; // ~2^-48
    // Two bits of margin: double-float ops are not correctly rounded.
    static constexpr f64 filter_eps    = 1.4210854715202004e-14; // 2^-46
    static constexpr bool correctly_rounded = false;

    constexpr DF64() noexcept : hi(0.0f), lo(0.0f) {}
    constexpr DF64(f32 h, f32 l) noexcept : hi(h), lo(l) {}

    static constexpr DF64 from_f32(f32 x) noexcept { return DF64(x, 0.0f); }

    static DF64 from(f64 x) noexcept {
        const f32 h = static_cast<f32>(x);
        const f32 l = static_cast<f32>(x - static_cast<f64>(h));
        return DF64(h, l);
    }

    constexpr f64 to_f64() const noexcept {
        return static_cast<f64>(hi) + static_cast<f64>(lo);
    }

    // Accurate (not sloppy) double-float add: renormalises twice.
    static DF64 add(DF64 a, DF64 b) noexcept {
        const auto s = eft::two_sum(a.hi, b.hi);
        const auto t = eft::two_sum(a.lo, b.lo);
        auto       r = eft::quick_two_sum(s.hi, s.lo + t.hi);
        r            = eft::quick_two_sum(r.hi, r.lo + t.lo);
        return DF64(r.hi, r.lo);
    }

    static DF64 neg(DF64 a) noexcept { return DF64(-a.hi, -a.lo); }
    static DF64 sub(DF64 a, DF64 b) noexcept { return add(a, neg(b)); }

    static DF64 mul(DF64 a, DF64 b) noexcept {
        auto p = eft::two_prod(a.hi, b.hi);
        p.lo += a.hi * b.lo + a.lo * b.hi;
        const auto r = eft::quick_two_sum(p.hi, p.lo);
        return DF64(r.hi, r.lo);
    }

    // Three Newton corrections on the leading quotient.
    static DF64 div(DF64 a, DF64 b) noexcept {
        const f32 q1 = a.hi / b.hi;
        DF64      r  = sub(a, mul(b, from_f32(q1)));
        const f32 q2 = r.hi / b.hi;
        r            = sub(r, mul(b, from_f32(q2)));
        const f32 q3 = r.hi / b.hi;
        const auto s = eft::quick_two_sum(q1, q2);
        return add(DF64(s.hi, s.lo), from_f32(q3));
    }

    static DF64 fma(DF64 a, DF64 b, DF64 c) noexcept { return add(mul(a, b), c); }
    static DF64 abs(DF64 a) noexcept { return a.hi < 0.0f ? neg(a) : a; }

    static DF64 sqrt(DF64 a) noexcept {
        if (a.hi <= 0.0f) return DF64();
        const f32  x  = 1.0f / std::sqrt(a.hi);   // approximate inverse sqrt
        const f32  ax = a.hi * x;
        const DF64 axd = from_f32(ax);
        const DF64 d   = sub(a, mul(axd, axd));
        return add(axd, from_f32(d.hi * x * 0.5f));
    }

    static constexpr int cmp(DF64 a, DF64 b) noexcept {
        if (a.hi < b.hi) return -1;
        if (a.hi > b.hi) return 1;
        if (a.lo < b.lo) return -1;
        if (a.lo > b.lo) return 1;
        return 0;
    }
};

template <class S>
concept FilterScalar = Scalar<S> && requires {
    { S::filter_eps } -> std::convertible_to<f64>;
    { S::correctly_rounded } -> std::convertible_to<bool>;
};

static_assert(Scalar<F32>);
static_assert(Scalar<F64>);
static_assert(Scalar<DF64>);
static_assert(FilterScalar<F32>);
static_assert(FilterScalar<F64>);
static_assert(FilterScalar<DF64>);

}
