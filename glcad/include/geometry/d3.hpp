#pragma once

#include <cmath>

#include "geometry/common.hpp"

namespace ggeo::d3 {

template <Scalar S>
struct Vec {
    S x{}, y{}, z{};

    static Vec from(f64 a, f64 b, f64 c) noexcept {
        return {S::from(a), S::from(b), S::from(c)};
    }
    static Vec zero() noexcept { return {}; }
    static Vec unit_x() noexcept { return from(1, 0, 0); }
    static Vec unit_y() noexcept { return from(0, 1, 0); }
    static Vec unit_z() noexcept { return from(0, 0, 1); }

    friend Vec operator+(Vec a, Vec b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec operator-(Vec a, Vec b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec operator*(Vec a, S s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
    friend Vec operator/(Vec a, S s) noexcept { return {a.x / s, a.y / s, a.z / s}; }
    friend Vec operator-(Vec a) noexcept { return {-a.x, -a.y, -a.z}; }
};

template <Scalar S>
struct Point {
    S x{}, y{}, z{};

    static Point from(f64 a, f64 b, f64 c) noexcept {
        return {S::from(a), S::from(b), S::from(c)};
    }
    static Point origin() noexcept { return {}; }

    friend Vec<S> operator-(Point a, Point b) noexcept {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    friend Point operator+(Point p, Vec<S> v) noexcept {
        return {p.x + v.x, p.y + v.y, p.z + v.z};
    }
    friend Point operator-(Point p, Vec<S> v) noexcept {
        return {p.x - v.x, p.y - v.y, p.z - v.z};
    }
    friend bool operator==(Point a, Point b) noexcept {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

template <Scalar S> inline S dot(Vec<S> a, Vec<S> b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
template <Scalar S> inline Vec<S> cross(Vec<S> a, Vec<S> b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <Scalar S> inline S length_sq(Vec<S> v) noexcept { return dot(v, v); }
template <Scalar S> inline S length(Vec<S> v) noexcept { return S::sqrt(dot(v, v)); }
template <Scalar S> inline Vec<S> normalized(Vec<S> v) noexcept {
    const S len = length(v);
    return {v.x / len, v.y / len, v.z / len};
}
template <Scalar S> inline S distance(Point<S> a, Point<S> b) noexcept {
    return length(a - b);
}

using LatticePoint = gcm::Lattice3;

template <Scalar S>
inline Point<S> to_point(const gcm::WorkingVolume& v, LatticePoint p) noexcept {
    f64 x, y, z;
    gcm::to_world(v, p, x, y, z);
    return Point<S>::from(x, y, z);
}
template <Scalar S>
inline LatticePoint to_lattice(const gcm::WorkingVolume& v, Point<S> p) noexcept {
    return gcm::to_lattice(v, p.x.to_f64(), p.y.to_f64(), p.z.to_f64());
}

template <Scalar S>
struct Segment {
    Point<S> a, b;
    Vec<S>   direction() const noexcept { return b - a; }
    S        length() const noexcept { return d3::length(b - a); }
    Point<S> midpoint() const noexcept {
        const S h = S::from(0.5);
        return {(a.x + b.x) * h, (a.y + b.y) * h, (a.z + b.z) * h};
    }
    Point<S> at(S t) const noexcept { return a + direction() * t; }
};

template <Scalar S>
struct Ray {
    Point<S> origin;
    Vec<S>   direction;
    Point<S> at(S t) const noexcept { return origin + direction * t; }
};

template <Scalar S>
struct Plane {
    Point<S> origin;
    Vec<S>   normal;

    S signed_distance(Point<S> p) const noexcept { return dot(p - origin, normal); }

    static Plane xy() noexcept { return {Point<S>::origin(), Vec<S>::unit_z()}; }
    static Plane xz() noexcept { return {Point<S>::origin(), Vec<S>::unit_y()}; }
    static Plane yz() noexcept { return {Point<S>::origin(), Vec<S>::unit_x()}; }
};

template <Scalar S>
struct Triangle {
    Point<S> a, b, c;
    Vec<S>   normal() const noexcept { return normalized(cross(b - a, c - a)); }
    S        area() const noexcept {
        return length(cross(b - a, c - a)) * S::from(0.5);
    }
};

template <Scalar S>
struct Sphere {
    Point<S> centre;
    S        radius{};
};

template <Scalar S>
struct AABB {
    Point<S> lo, hi;

    static AABB empty() noexcept {
        return {Point<S>::from(1e300, 1e300, 1e300),
                Point<S>::from(-1e300, -1e300, -1e300)};
    }
    void expand(Point<S> p) noexcept {
        if (p.x < lo.x) lo.x = p.x;
        if (p.y < lo.y) lo.y = p.y;
        if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x;
        if (p.y > hi.y) hi.y = p.y;
        if (p.z > hi.z) hi.z = p.z;
    }
    bool contains(Point<S> p) const noexcept {
        return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y &&
               p.z >= lo.z && p.z <= hi.z;
    }
    bool intersects(const AABB& o) const noexcept {
        return !(o.lo.x > hi.x || o.hi.x < lo.x || o.lo.y > hi.y ||
                 o.hi.y < lo.y || o.lo.z > hi.z || o.hi.z < lo.z);
    }
    Vec<S>   size() const noexcept { return hi - lo; }
    Point<S> centre() const noexcept {
        const S h = S::from(0.5);
        return {(lo.x + hi.x) * h, (lo.y + hi.y) * h, (lo.z + hi.z) * h};
    }
    bool valid() const noexcept {
        return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z;
    }
};

template <Scalar S>
struct Transform {
    S m[3][4]{};

    static Transform identity() noexcept {
        Transform t;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 4; ++j)
                t.m[i][j] = S::from(i == j ? 1.0 : 0.0);
        return t;
    }
    static Transform translation(Vec<S> v) noexcept {
        Transform t = identity();
        t.m[0][3] = v.x; t.m[1][3] = v.y; t.m[2][3] = v.z;
        return t;
    }
    static Transform scaling(S s) noexcept {
        Transform t = identity();
        t.m[0][0] = s; t.m[1][1] = s; t.m[2][2] = s;
        return t;
    }

    Point<S> apply(Point<S> p) const noexcept {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
    }
    Vec<S> apply(Vec<S> v) const noexcept {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    Transform compose(const Transform& o) const noexcept {
        Transform r;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                S s = S::from(0.0);
                for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
            S s = m[i][3];
            for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][3];
            r.m[i][3] = s;
        }
        return r;
    }
};

template <Scalar S>
struct Frame {
    Point<S> origin;
    Vec<S>   u, v, n;   // orthonormal

    static Frame world_xy() noexcept {
        return {Point<S>::origin(), Vec<S>::unit_x(), Vec<S>::unit_y(), Vec<S>::unit_z()};
    }

    Point<S> to_world(S x, S y) const noexcept { return origin + u * x + v * y; }

    void to_plane(Point<S> p, S& x, S& y) const noexcept {
        const Vec<S> d = p - origin;
        x = dot(d, u);
        y = dot(d, v);
    }

    Plane<S> plane() const noexcept { return {origin, n}; }
};

struct Traits {
    using Dim = Dim3;
    static constexpr int dimension = 3;
    template <Scalar S> using Vec   = d3::Vec<S>;
    template <Scalar S> using Point = d3::Point<S>;
    template <Scalar S> using AABB  = d3::AABB<S>;
};

}
