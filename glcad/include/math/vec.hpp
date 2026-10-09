#pragma once

#include "math/scalar.hpp"

namespace gcm {

template <Scalar S>
struct Vec2 {
    S x{}, y{};

    static Vec2 from(f64 a, f64 b) noexcept { return {S::from(a), S::from(b)}; }

    friend Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend Vec2 operator*(Vec2 a, S s) noexcept { return {a.x * s, a.y * s}; }
    friend Vec2 operator-(Vec2 a) noexcept { return {-a.x, -a.y}; }
};

template <Scalar S>
struct Vec3 {
    S x{}, y{}, z{};

    static Vec3 from(f64 a, f64 b, f64 c) noexcept {
        return {S::from(a), S::from(b), S::from(c)};
    }

    friend Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, S s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
    friend Vec3 operator-(Vec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
};

template <Scalar S> inline S dot(Vec2<S> a, Vec2<S> b) noexcept {
    return a.x * b.x + a.y * b.y;
}
template <Scalar S> inline S dot(Vec3<S> a, Vec3<S> b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

template <Scalar S> inline S cross(Vec2<S> a, Vec2<S> b) noexcept {
    return a.x * b.y - a.y * b.x;
}

template <Scalar S> inline Vec3<S> cross(Vec3<S> a, Vec3<S> b) noexcept {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

template <Scalar S> inline S length_squared(Vec2<S> a) noexcept { return dot(a, a); }
template <Scalar S> inline S length_squared(Vec3<S> a) noexcept { return dot(a, a); }
template <Scalar S> inline S length(Vec2<S> a) noexcept { return S::sqrt(dot(a, a)); }
template <Scalar S> inline S length(Vec3<S> a) noexcept { return S::sqrt(dot(a, a)); }

template <Scalar S> inline Vec3<S> normalized(Vec3<S> a) noexcept {
    const S len = length(a);
    return {a.x / len, a.y / len, a.z / len};
}

}
