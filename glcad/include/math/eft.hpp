// Correct only under strict IEEE-754 round-to-nearest; never -ffast-math.
#pragma once

#include <cmath>

#include "math/config.hpp"

namespace gcm::eft {

// Unevaluated sum: hi is the rounded result, lo the exact rounding error.
template <class T>
struct Two {
    T hi;
    T lo;
};

// Knuth's TwoSum. Exact for all finite inputs. 6 flops, no branch.
template <class T>
constexpr Two<T> two_sum(T a, T b) noexcept {
    const T s  = a + b;
    const T bb = s - a;
    const T e  = (a - (s - bb)) + (b - bb);
    return {s, e};
}

// Dekker's FastTwoSum. Requires |a| >= |b| (or a == 0).
template <class T>
constexpr Two<T> quick_two_sum(T a, T b) noexcept {
    const T s = a + b;
    const T e = b - (s - a);
    return {s, e};
}

template <class T>
constexpr Two<T> two_diff(T a, T b) noexcept {
    const T s  = a - b;
    const T bb = s - a;
    const T e  = (a - (s - bb)) - (b + bb);
    return {s, e};
}

// Exact product via FMA: p is the rounded product, e the exact error.
inline Two<f32> two_prod(f32 a, f32 b) noexcept {
    const f32 p = a * b;
    const f32 e = std::fmaf(a, b, -p);
    return {p, e};
}

inline Two<f64> two_prod(f64 a, f64 b) noexcept {
    const f64 p = a * b;
    const f64 e = std::fma(a, b, -p);
    return {p, e};
}

template <class T, int HalfBits>
constexpr Two<T> split(T a) noexcept {
    constexpr T factor = static_cast<T>((u64{1} << HalfBits) + 1);
    const T c  = factor * a;
    const T hi = c - (c - a);
    const T lo = a - hi;
    return {hi, lo};
}

constexpr Two<f32> split_f32(f32 a) noexcept { return split<f32, 12>(a); }
constexpr Two<f64> split_f64(f64 a) noexcept { return split<f64, 27>(a); }

}
