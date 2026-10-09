#pragma once

#include "math/config.hpp"
#include "math/scalar.hpp"

namespace gcm {

template <Scalar S, Cls C>
struct Tagged : ScalarBase<Tagged<S, C>> {
    S v{};

    static constexpr int mantissa_bits = S::mantissa_bits;
    static constexpr Cls result_class  = C;

    constexpr Tagged() = default;
    constexpr explicit Tagged(S s) noexcept : v(s) {}

    static Tagged from(f64 x) noexcept { return Tagged(S::from(x)); }
    f64 to_f64() const noexcept { return v.to_f64(); }

    static Tagged add(Tagged a, Tagged b) noexcept { return Tagged(S::add(a.v, b.v)); }
    static Tagged sub(Tagged a, Tagged b) noexcept { return Tagged(S::sub(a.v, b.v)); }
    static Tagged mul(Tagged a, Tagged b) noexcept { return Tagged(S::mul(a.v, b.v)); }
    static Tagged div(Tagged a, Tagged b) noexcept { return Tagged(S::div(a.v, b.v)); }
    static Tagged neg(Tagged a) noexcept { return Tagged(S::neg(a.v)); }
    static Tagged abs(Tagged a) noexcept { return Tagged(S::abs(a.v)); }
    static Tagged sqrt(Tagged a) noexcept { return Tagged(S::sqrt(a.v)); }
    static Tagged fma(Tagged a, Tagged b, Tagged c) noexcept {
        return Tagged(S::fma(a.v, b.v, c.v));
    }
    static int cmp(Tagged a, Tagged b) noexcept { return S::cmp(a.v, b.v); }
};

template <Scalar S> using Auth = Tagged<S, Cls::Authoritative>;
template <Scalar S> using Disp = Tagged<S, Cls::Display>;

// Authoritative -> Display is the only legal direction.
template <Scalar S>
inline Disp<S> demote(Auth<S> a) noexcept { return Disp<S>(a.v); }

static_assert(Scalar<Auth<F64>>);
static_assert(Scalar<Disp<F32>>);

}
