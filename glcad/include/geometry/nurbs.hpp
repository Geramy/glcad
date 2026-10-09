#pragma once

#include <algorithm>
#include <vector>

#include "geometry/d2.hpp"
#include "geometry/d3.hpp"

namespace ggeo {

template <Scalar S>
struct ControlPoint2 {
    d2::Point<S> p{};
    S            w{S::from(1.0)};
};

template <Scalar S>
struct ControlPoint3 {
    d3::Point<S> p{};
    S            w{S::from(1.0)};
};

// Knot count must equal control_count + degree + 1.
struct KnotVector {
    std::vector<f64> knots;

    bool valid(int degree, std::size_t control_count) const;
    static KnotVector clamped_uniform(int degree, std::size_t control_count);
    static KnotVector uniform(int degree, std::size_t control_count);

    f64 first() const noexcept { return knots.empty() ? 0.0 : knots.front(); }
    f64 last() const noexcept { return knots.empty() ? 0.0 : knots.back(); }
    std::size_t size() const noexcept { return knots.size(); }

    int find_span(int degree, std::size_t control_count, f64 u) const;
    int multiplicity(f64 u, f64 tol = 1e-12) const;
};

template <Scalar S>
class NurbsCurve2 {
public:
    int degree{3};
    std::vector<ControlPoint2<S>> control;
    KnotVector knots;

    bool valid() const {
        return degree >= 1 && control.size() > static_cast<std::size_t>(degree) &&
               knots.valid(degree, control.size());
    }

    d2::Point<S> evaluate(f64 u) const;
    d2::Vec<S>   derivative(f64 u) const;
    d2::Point<S> start() const { return evaluate(knots.first()); }
    d2::Point<S> end() const { return evaluate(knots.last()); }

    // `tolerance` is chord height in model units.
    std::vector<d2::Point<S>> tessellate(f64 tolerance, int max_points = 4096) const;

    bool rational() const;
};

template <Scalar S>
class NurbsCurve3 {
public:
    int degree{3};
    std::vector<ControlPoint3<S>> control;
    KnotVector knots;

    bool valid() const {
        return degree >= 1 && control.size() > static_cast<std::size_t>(degree) &&
               knots.valid(degree, control.size());
    }

    d3::Point<S> evaluate(f64 u) const;
    d3::Vec<S>   derivative(f64 u) const;
    std::vector<d3::Point<S>> tessellate(f64 tolerance, int max_points = 4096) const;
    bool rational() const;
    f64 invert(d3::Point<S> p, f64& u, int scan = 64) const;
};

template <Scalar S>
class NurbsSurface {
public:
    int degree_u{3}, degree_v{3};
    std::size_t count_u{0}, count_v{0};
    std::vector<ControlPoint3<S>> control;   // row-major, count_u * count_v
    KnotVector knots_u, knots_v;

    bool valid() const {
        return degree_u >= 1 && degree_v >= 1 &&
               control.size() == count_u * count_v &&
               count_u > static_cast<std::size_t>(degree_u) &&
               count_v > static_cast<std::size_t>(degree_v) &&
               knots_u.valid(degree_u, count_u) && knots_v.valid(degree_v, count_v);
    }

    const ControlPoint3<S>& at(std::size_t i, std::size_t j) const {
        return control[i * count_v + j];
    }

    d3::Point<S> evaluate(f64 u, f64 v) const;
    d3::Vec<S>   normal(f64 u, f64 v) const;
    void derivatives(f64 u, f64 v, d3::Vec<S>& du, d3::Vec<S>& dv) const;
    f64 invert(d3::Point<S> p, f64& u, f64& v, int scan = 24) const;
    f64 u_first() const noexcept { return knots_u.knots[static_cast<std::size_t>(degree_u)]; }
    f64 u_last() const noexcept { return knots_u.knots[count_u]; }
    f64 v_first() const noexcept { return knots_v.knots[static_cast<std::size_t>(degree_v)]; }
    f64 v_last() const noexcept { return knots_v.knots[count_v]; }
};

// Exact circle: nine control points, weights alternating 1 and cos(45°).
template <Scalar S>
NurbsCurve2<S> circle_nurbs(d2::Point<S> centre, f64 radius);

template <Scalar S>
NurbsCurve2<S> arc_nurbs(d2::Point<S> centre, f64 radius, f64 start_angle,
                         f64 end_angle);

template <Scalar S>
NurbsCurve2<S> ellipse_nurbs(d2::Point<S> centre, f64 rx, f64 ry,
                             f64 rotation = 0.0);

template <Scalar S>
NurbsCurve2<S> line_nurbs(d2::Point<S> a, d2::Point<S> b);

template <Scalar S>
NurbsCurve2<S> interpolate_points(const std::vector<d2::Point<S>>& points,
                                  int degree = 3);

template <Scalar S>
NurbsCurve2<S> approximate_points(const std::vector<d2::Point<S>>& points,
                                  std::size_t control_count, int degree = 3);

template <Scalar S>
std::vector<d2::Point<S>> simplify_polyline(const std::vector<d2::Point<S>>& points,
                                            f64 tolerance);

template <Scalar S>
struct FittedArc {
    d2::Point<S> start{}, end{}, centre{};
    f64  radius{0.0};
    bool is_line{true};
};

template <Scalar S>
std::vector<FittedArc<S>> fit_arcs(const std::vector<d2::Point<S>>& points,
                                   f64 tolerance);

}

#include "geometry/nurbs.inl"
