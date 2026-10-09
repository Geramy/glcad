#pragma once

#include <cmath>

#include "geometry/common.hpp"

namespace ggeo::d2 {

template <Scalar S>
struct Vec {
    S x{}, y{};

    static Vec from(f64 a, f64 b) noexcept { return {S::from(a), S::from(b)}; }
    static Vec zero() noexcept { return {}; }
    static Vec unit_x() noexcept { return {S::from(1.0), S::from(0.0)}; }
    static Vec unit_y() noexcept { return {S::from(0.0), S::from(1.0)}; }

    friend Vec operator+(Vec a, Vec b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend Vec operator-(Vec a, Vec b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend Vec operator*(Vec a, S s) noexcept { return {a.x * s, a.y * s}; }
    friend Vec operator/(Vec a, S s) noexcept { return {a.x / s, a.y / s}; }
    friend Vec operator-(Vec a) noexcept { return {-a.x, -a.y}; }
};

template <Scalar S>
struct Point {
    S x{}, y{};

    static Point from(f64 a, f64 b) noexcept { return {S::from(a), S::from(b)}; }
    static Point origin() noexcept { return {}; }

    friend Vec<S>  operator-(Point a, Point b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend Point   operator+(Point p, Vec<S> v) noexcept { return {p.x + v.x, p.y + v.y}; }
    friend Point   operator-(Point p, Vec<S> v) noexcept { return {p.x - v.x, p.y - v.y}; }
    friend bool    operator==(Point a, Point b) noexcept { return a.x == b.x && a.y == b.y; }
};

template <Scalar S> inline S dot(Vec<S> a, Vec<S> b) noexcept { return a.x * b.x + a.y * b.y; }
template <Scalar S> inline S cross(Vec<S> a, Vec<S> b) noexcept { return a.x * b.y - a.y * b.x; }
template <Scalar S> inline S length_sq(Vec<S> v) noexcept { return dot(v, v); }
template <Scalar S> inline S length(Vec<S> v) noexcept { return S::sqrt(dot(v, v)); }
template <Scalar S> inline Vec<S> perp(Vec<S> v) noexcept { return {-v.y, v.x}; }
template <Scalar S> inline Vec<S> normalized(Vec<S> v) noexcept {
    const S len = length(v);
    return {v.x / len, v.y / len};
}
template <Scalar S> inline S distance(Point<S> a, Point<S> b) noexcept {
    return length(a - b);
}

using LatticePoint = gcm::Lattice2;

template <Scalar S>
inline Point<S> to_point(const gcm::WorkingVolume& v, LatticePoint p) noexcept {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    return Point<S>::from(x, y);
}

template <Scalar S>
inline LatticePoint to_lattice(const gcm::WorkingVolume& v, Point<S> p) noexcept {
    return gcm::to_lattice(v, p.x.to_f64(), p.y.to_f64());
}

template <Scalar S>
struct Segment {
    Point<S> a, b;
    Vec<S> direction() const noexcept { return b - a; }
    S      length() const noexcept { return d2::length(b - a); }
    Point<S> midpoint() const noexcept {
        return {(a.x + b.x) / S::from(2.0), (a.y + b.y) / S::from(2.0)};
    }
    Point<S> at(S t) const noexcept { return a + direction() * t; }
};

template <Scalar S>
struct Circle {
    Point<S> centre;
    S        radius{};
    Point<S> at(S radians) const noexcept {
        const f64 r = radians.to_f64();
        return {centre.x + S::from(std::cos(r)) * radius,
                centre.y + S::from(std::sin(r)) * radius};
    }
};

template <Scalar S>
struct Arc {
    Point<S> centre;
    S        radius{};
    S        start_angle{}, end_angle{};
};

template <Scalar S>
struct AABB {
    Point<S> lo, hi;

    static AABB empty() noexcept {
        return {Point<S>::from(1e300, 1e300), Point<S>::from(-1e300, -1e300)};
    }
    void expand(Point<S> p) noexcept {
        if (p.x < lo.x) lo.x = p.x;
        if (p.y < lo.y) lo.y = p.y;
        if (p.x > hi.x) hi.x = p.x;
        if (p.y > hi.y) hi.y = p.y;
    }
    bool contains(Point<S> p) const noexcept {
        return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
    }
    bool intersects(const AABB& o) const noexcept {
        return !(o.lo.x > hi.x || o.hi.x < lo.x || o.lo.y > hi.y || o.hi.y < lo.y);
    }
    Vec<S> size() const noexcept { return hi - lo; }
    bool   valid() const noexcept { return lo.x <= hi.x && lo.y <= hi.y; }
};

template <Scalar S>
struct Transform {
    // Row-major 2x3: [m00 m01 tx; m10 m11 ty]
    S m00{}, m01{}, tx{};
    S m10{}, m11{}, ty{};

    static Transform identity() noexcept {
        return {S::from(1.0), S::from(0.0), S::from(0.0),
                S::from(0.0), S::from(1.0), S::from(0.0)};
    }
    static Transform translation(Vec<S> v) noexcept {
        Transform t = identity();
        t.tx = v.x; t.ty = v.y;
        return t;
    }
    static Transform scaling(S s) noexcept {
        Transform t = identity();
        t.m00 = s; t.m11 = s;
        return t;
    }
    static Transform rotation(f64 radians) noexcept {
        const S c = S::from(std::cos(radians));
        const S s = S::from(std::sin(radians));
        return {c, -s, S::from(0.0), s, c, S::from(0.0)};
    }

    Point<S> apply(Point<S> p) const noexcept {
        return {m00 * p.x + m01 * p.y + tx, m10 * p.x + m11 * p.y + ty};
    }
    Vec<S> apply(Vec<S> v) const noexcept {
        return {m00 * v.x + m01 * v.y, m10 * v.x + m11 * v.y};
    }
    Transform compose(const Transform& o) const noexcept {
        return {m00 * o.m00 + m01 * o.m10, m00 * o.m01 + m01 * o.m11,
                m00 * o.tx + m01 * o.ty + tx,
                m10 * o.m00 + m11 * o.m10, m10 * o.m01 + m11 * o.m11,
                m10 * o.tx + m11 * o.ty + ty};
    }
    S determinant() const noexcept { return m00 * m11 - m01 * m10; }
};

struct Traits {
    using Dim = Dim2;
    static constexpr int dimension = 2;
    template <Scalar S> using Vec   = d2::Vec<S>;
    template <Scalar S> using Point = d2::Point<S>;
    template <Scalar S> using AABB  = d2::AABB<S>;
};

}
