// Rounding is handled by widening one ULP, not by switching FPU modes.
#pragma once

#include <cmath>
#include <limits>

#include "math/config.hpp"
#include "math/fixed.hpp"
#include "math/scalar.hpp"

namespace gcm {

template <class S = F64>
struct Interval {
    using Scalar = S;

    f64 lo{0.0}, hi{0.0};

    constexpr Interval() = default;
    constexpr explicit Interval(f64 v) noexcept : lo(v), hi(v) {}
    constexpr Interval(f64 a, f64 b) noexcept : lo(a < b ? a : b), hi(a < b ? b : a) {}

    static Interval exact(f64 v) noexcept { return Interval(v); }

    static Interval around(f64 v) noexcept {
        return {std::nextafter(v, -std::numeric_limits<f64>::infinity()),
                std::nextafter(v, std::numeric_limits<f64>::infinity())};
    }

    constexpr f64  width() const noexcept { return hi - lo; }
    constexpr f64  mid() const noexcept { return (lo + hi) * 0.5; }
    constexpr bool contains(f64 v) const noexcept { return v >= lo && v <= hi; }
    constexpr bool is_point() const noexcept { return lo == hi; }
    bool empty() const noexcept { return !(lo <= hi); }   // NaN counts as empty

    constexpr bool certainly_positive() const noexcept { return lo > 0.0; }
    constexpr bool certainly_negative() const noexcept { return hi < 0.0; }
    constexpr bool straddles_zero() const noexcept { return lo <= 0.0 && hi >= 0.0; }

    // Sign::Zero means undecided here, never proved zero.
    Sign sign() const noexcept {
        if (certainly_positive()) return Sign::Positive;
        if (certainly_negative()) return Sign::Negative;
        return Sign::Zero;
    }
    bool decided() const noexcept { return !straddles_zero(); }
};

namespace detail {
inline f64 down(f64 v) noexcept {
    return std::nextafter(v, -std::numeric_limits<f64>::infinity());
}
inline f64 up(f64 v) noexcept {
    return std::nextafter(v, std::numeric_limits<f64>::infinity());
}
}

template <class S>
inline Interval<S> operator+(Interval<S> a, Interval<S> b) noexcept {
    return {detail::down(a.lo + b.lo), detail::up(a.hi + b.hi)};
}

template <class S>
inline Interval<S> operator-(Interval<S> a, Interval<S> b) noexcept {
    return {detail::down(a.lo - b.hi), detail::up(a.hi - b.lo)};
}

template <class S>
inline Interval<S> operator-(Interval<S> a) noexcept {
    return {-a.hi, -a.lo};
}

// All four corner products: operand signs may vary across the interval.
template <class S>
inline Interval<S> operator*(Interval<S> a, Interval<S> b) noexcept {
    const f64 p1 = a.lo * b.lo, p2 = a.lo * b.hi;
    const f64 p3 = a.hi * b.lo, p4 = a.hi * b.hi;
    f64 lo = p1, hi = p1;
    for (f64 p : {p2, p3, p4}) {
        if (p < lo) lo = p;
        if (p > hi) hi = p;
    }
    return {detail::down(lo), detail::up(hi)};
}

template <class S>
inline Interval<S> operator/(Interval<S> a, Interval<S> b) noexcept {
    if (b.straddles_zero()) {
        const f64 inf = std::numeric_limits<f64>::infinity();
        return {-inf, inf};
    }
    const f64 q1 = a.lo / b.lo, q2 = a.lo / b.hi;
    const f64 q3 = a.hi / b.lo, q4 = a.hi / b.hi;
    f64 lo = q1, hi = q1;
    for (f64 q : {q2, q3, q4}) {
        if (q < lo) lo = q;
        if (q > hi) hi = q;
    }
    return {detail::down(lo), detail::up(hi)};
}

template <class S>
inline Interval<S> abs(Interval<S> a) noexcept {
    if (a.lo >= 0.0) return a;
    if (a.hi <= 0.0) return {-a.hi, -a.lo};
    return {0.0, std::fmax(-a.lo, a.hi)};
}

template <class S>
inline Interval<S> sqrt(Interval<S> a) noexcept {
    if (a.hi < 0.0) return {std::numeric_limits<f64>::quiet_NaN(),
                            std::numeric_limits<f64>::quiet_NaN()};
    const f64 lo = a.lo <= 0.0 ? 0.0 : detail::down(std::sqrt(a.lo));
    return {lo, detail::up(std::sqrt(a.hi))};
}

template <class S>
inline Interval<S> square(Interval<S> a) noexcept {
    return a * a;
}

template <class S = F64>
inline Sign orient2d_interval(const Lattice2& a, const Lattice2& b,
                              const Lattice2& c) noexcept {
    using I = Interval<S>;
    // Differences are exact as integers; widen only afterwards.
    const I acx = I::around(static_cast<f64>(a.x - c.x));
    const I acy = I::around(static_cast<f64>(a.y - c.y));
    const I bcx = I::around(static_cast<f64>(b.x - c.x));
    const I bcy = I::around(static_cast<f64>(b.y - c.y));
    return (acx * bcy - acy * bcx).sign();
}

template <class S = F64>
inline Sign orient3d_interval(const Lattice3& a, const Lattice3& b,
                              const Lattice3& c, const Lattice3& d) noexcept {
    using I = Interval<S>;
    auto diff = [](i64 p, i64 q) { return I::around(static_cast<f64>(p - q)); };

    const I adx = diff(a.x, d.x), ady = diff(a.y, d.y), adz = diff(a.z, d.z);
    const I bdx = diff(b.x, d.x), bdy = diff(b.y, d.y), bdz = diff(b.z, d.z);
    const I cdx = diff(c.x, d.x), cdy = diff(c.y, d.y), cdz = diff(c.z, d.z);

    const I det = adx * (bdy * cdz - bdz * cdy) -
                  ady * (bdx * cdz - bdz * cdx) +
                  adz * (bdx * cdy - bdy * cdx);
    return det.sign();
}

}
