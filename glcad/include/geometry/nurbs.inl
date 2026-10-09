// Implementation of ggeo/nurbs.hpp. Header-only, like the rest of ggeo.
#pragma once

#include <cmath>

namespace ggeo {

// --- knot vectors -----------------------------------------------------------
inline bool KnotVector::valid(int degree, std::size_t control_count) const {
    if (degree < 1 || control_count == 0) return false;
    // The count is not a convention: it is forced by the recursion. Getting it
    // wrong is the single most common NURBS bug, so it is checked rather than
    // assumed.
    if (knots.size() != control_count + static_cast<std::size_t>(degree) + 1)
        return false;
    for (std::size_t i = 1; i < knots.size(); ++i)
        if (knots[i] < knots[i - 1]) return false;
    return knots.front() < knots.back();
}

inline KnotVector KnotVector::clamped_uniform(int degree, std::size_t n) {
    KnotVector k;
    if (degree < 1 || n == 0) return k;
    const std::size_t total = n + static_cast<std::size_t>(degree) + 1;
    k.knots.reserve(total);

    // degree+1 zeros, then the interior, then degree+1 ones. The repeated end
    // knots are what clamp the curve to its first and last control points.
    const std::size_t interior = n > static_cast<std::size_t>(degree)
                                     ? n - static_cast<std::size_t>(degree) - 1
                                     : 0;
    for (int i = 0; i <= degree; ++i) k.knots.push_back(0.0);
    for (std::size_t i = 1; i <= interior; ++i)
        k.knots.push_back(static_cast<f64>(i) / static_cast<f64>(interior + 1));
    for (int i = 0; i <= degree; ++i) k.knots.push_back(1.0);
    return k;
}

inline KnotVector KnotVector::uniform(int degree, std::size_t n) {
    KnotVector k;
    if (degree < 1 || n == 0) return k;
    const std::size_t total = n + static_cast<std::size_t>(degree) + 1;
    k.knots.reserve(total);
    for (std::size_t i = 0; i < total; ++i)
        k.knots.push_back(static_cast<f64>(i) / static_cast<f64>(total - 1));
    return k;
}

inline int KnotVector::find_span(int degree, std::size_t n, f64 u) const {
    if (knots.empty()) return degree;
    const int lo = degree;
    const int hi = static_cast<int>(n);            // one past the last span
    if (u >= knots[static_cast<std::size_t>(hi)]) return hi - 1;
    if (u <= knots[static_cast<std::size_t>(lo)]) return lo;

    // Binary search, as the recursion needs a half-open span.
    int a = lo, b = hi, mid = (a + b) / 2;
    while (u < knots[static_cast<std::size_t>(mid)] ||
           u >= knots[static_cast<std::size_t>(mid) + 1]) {
        if (u < knots[static_cast<std::size_t>(mid)]) b = mid;
        else a = mid;
        mid = (a + b) / 2;
        if (a + 1 >= b) break;
    }
    return mid;
}

inline int KnotVector::multiplicity(f64 u, f64 tol) const {
    int n = 0;
    for (f64 k : knots)
        if (std::fabs(k - u) <= tol) ++n;
    return n;
}

// --- de Boor ----------------------------------------------------------------
namespace detail {

// One de Boor pass in homogeneous coordinates. Working homogeneously is what
// makes rational curves fall out of the same code as polynomial ones: divide
// by w at the very end, never in the middle.
template <int N>
struct Homog {
    f64 c[N + 1]{};      // coordinates, then the weight last
};

template <int N>
inline Homog<N> de_boor(int degree, const std::vector<f64>& knots,
                        const std::vector<Homog<N>>& cps, int span, f64 u) {
    std::vector<Homog<N>> d(static_cast<std::size_t>(degree) + 1);
    for (int j = 0; j <= degree; ++j) {
        const int idx = span - degree + j;
        if (idx >= 0 && static_cast<std::size_t>(idx) < cps.size())
            d[static_cast<std::size_t>(j)] = cps[static_cast<std::size_t>(idx)];
    }

    for (int r = 1; r <= degree; ++r)
        for (int j = degree; j >= r; --j) {
            const int i = span - degree + j;
            const f64 lo = knots[static_cast<std::size_t>(i)];
            const f64 hi = knots[static_cast<std::size_t>(i + degree - r + 1)];
            const f64 den = hi - lo;
            // A zero span means a repeated knot; the convex combination
            // degenerates to one of its endpoints rather than dividing by zero.
            const f64 a = den > 0.0 ? (u - lo) / den : 0.0;
            for (int k = 0; k <= N; ++k)
                d[static_cast<std::size_t>(j)].c[k] =
                    (1.0 - a) * d[static_cast<std::size_t>(j - 1)].c[k] +
                    a * d[static_cast<std::size_t>(j)].c[k];
        }
    return d[static_cast<std::size_t>(degree)];
}

} // namespace detail

// --- 2D curve ---------------------------------------------------------------
template <Scalar S>
bool NurbsCurve2<S>::rational() const {
    for (const auto& c : control)
        if (std::fabs(c.w.to_f64() - 1.0) > 1e-12) return true;
    return false;
}

template <Scalar S>
d2::Point<S> NurbsCurve2<S>::evaluate(f64 u) const {
    if (!valid()) return d2::Point<S>::from(0, 0);
    u = std::clamp(u, knots.first(), knots.last());

    std::vector<detail::Homog<2>> cps(control.size());
    for (std::size_t i = 0; i < control.size(); ++i) {
        const f64 w = control[i].w.to_f64();
        cps[i].c[0] = control[i].p.x.to_f64() * w;
        cps[i].c[1] = control[i].p.y.to_f64() * w;
        cps[i].c[2] = w;
    }

    const int span = knots.find_span(degree, control.size(), u);
    const auto h = detail::de_boor<2>(degree, knots.knots, cps, span, u);
    const f64 w = std::fabs(h.c[2]) > 1e-300 ? h.c[2] : 1.0;
    return d2::Point<S>::from(h.c[0] / w, h.c[1] / w);
}

template <Scalar S>
d2::Vec<S> NurbsCurve2<S>::derivative(f64 u) const {
    if (!valid()) return d2::Vec<S>::from(0, 0);
    // A central difference on the curve rather than the derivative basis. For
    // a rational curve the quotient rule needs the homogeneous derivative
    // anyway, and this is exact to the same order without a second code path
    // that could disagree with evaluate().
    const f64 span = knots.last() - knots.first();
    const f64 h = std::max(1e-7, span * 1e-6);
    const f64 a = std::clamp(u - h, knots.first(), knots.last());
    const f64 b = std::clamp(u + h, knots.first(), knots.last());
    if (b - a < 1e-300) return d2::Vec<S>::from(0, 0);

    const d2::Point<S> pa = evaluate(a), pb = evaluate(b);
    return d2::Vec<S>::from((pb.x.to_f64() - pa.x.to_f64()) / (b - a),
                            (pb.y.to_f64() - pa.y.to_f64()) / (b - a));
}

template <Scalar S>
std::vector<d2::Point<S>> NurbsCurve2<S>::tessellate(f64 tolerance,
                                                     int max_points) const {
    std::vector<d2::Point<S>> out;
    if (!valid()) return out;
    if (tolerance <= 0.0) tolerance = 1e-6;

    // Adaptive by chord height: subdivide a span only while its midpoint is
    // further than the tolerance from the chord. A fixed step is either too
    // coarse on a tight bend or wasteful everywhere else.
    const f64 u0 = knots.first(), u1 = knots.last();
    struct Seg { f64 a, b; };
    std::vector<Seg> stack{{u0, u1}};
    std::vector<f64> params{u0};

    int guard = 0;
    while (!stack.empty() && static_cast<int>(params.size()) < max_points &&
           guard++ < max_points * 4) {
        const Seg s = stack.back();
        stack.pop_back();
        const f64 mid = (s.a + s.b) * 0.5;

        const d2::Point<S> pa = evaluate(s.a);
        const d2::Point<S> pb = evaluate(s.b);
        const d2::Point<S> pm = evaluate(mid);

        const f64 ax = pa.x.to_f64(), ay = pa.y.to_f64();
        const f64 bx = pb.x.to_f64(), by = pb.y.to_f64();
        const f64 mx = pm.x.to_f64(), my = pm.y.to_f64();
        const f64 len = std::hypot(bx - ax, by - ay);
        const f64 sag = len > 1e-300
                            ? std::fabs((bx - ax) * (my - ay) - (by - ay) * (mx - ax)) / len
                            : std::hypot(mx - ax, my - ay);

        if (sag > tolerance && s.b - s.a > (u1 - u0) * 1e-6) {
            stack.push_back({mid, s.b});
            stack.push_back({s.a, mid});
        } else {
            params.push_back(s.b);
        }
    }

    std::sort(params.begin(), params.end());
    params.erase(std::unique(params.begin(), params.end()), params.end());
    out.reserve(params.size());
    for (f64 u : params) out.push_back(evaluate(u));
    return out;
}

// --- 3D curve ---------------------------------------------------------------
template <Scalar S>
bool NurbsCurve3<S>::rational() const {
    for (const auto& c : control)
        if (std::fabs(c.w.to_f64() - 1.0) > 1e-12) return true;
    return false;
}

template <Scalar S>
d3::Point<S> NurbsCurve3<S>::evaluate(f64 u) const {
    if (!valid()) return d3::Point<S>::from(0, 0, 0);
    u = std::clamp(u, knots.first(), knots.last());

    std::vector<detail::Homog<3>> cps(control.size());
    for (std::size_t i = 0; i < control.size(); ++i) {
        const f64 w = control[i].w.to_f64();
        cps[i].c[0] = control[i].p.x.to_f64() * w;
        cps[i].c[1] = control[i].p.y.to_f64() * w;
        cps[i].c[2] = control[i].p.z.to_f64() * w;
        cps[i].c[3] = w;
    }

    const int span = knots.find_span(degree, control.size(), u);
    const auto h = detail::de_boor<3>(degree, knots.knots, cps, span, u);
    const f64 w = std::fabs(h.c[3]) > 1e-300 ? h.c[3] : 1.0;
    return d3::Point<S>::from(h.c[0] / w, h.c[1] / w, h.c[2] / w);
}

template <Scalar S>
d3::Vec<S> NurbsCurve3<S>::derivative(f64 u) const {
    if (!valid()) return d3::Vec<S>::from(0, 0, 0);
    const f64 span = knots.last() - knots.first();
    const f64 h = std::max(1e-7, span * 1e-6);
    const f64 a = std::clamp(u - h, knots.first(), knots.last());
    const f64 b = std::clamp(u + h, knots.first(), knots.last());
    if (b - a < 1e-300) return d3::Vec<S>::from(0, 0, 0);
    const d3::Point<S> pa = evaluate(a), pb = evaluate(b);
    return d3::Vec<S>::from((pb.x.to_f64() - pa.x.to_f64()) / (b - a),
                            (pb.y.to_f64() - pa.y.to_f64()) / (b - a),
                            (pb.z.to_f64() - pa.z.to_f64()) / (b - a));
}

template <Scalar S>
std::vector<d3::Point<S>> NurbsCurve3<S>::tessellate(f64 tolerance,
                                                     int max_points) const {
    std::vector<d3::Point<S>> out;
    if (!valid()) return out;
    const int n = std::clamp(max_points / 8, 8, max_points);
    out.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i)
        out.push_back(evaluate(knots.first() +
                               (knots.last() - knots.first()) * i / n));
    (void)tolerance;
    return out;
}

// --- surface ----------------------------------------------------------------
template <Scalar S>
d3::Point<S> NurbsSurface<S>::evaluate(f64 u, f64 v) const {
    if (!valid()) return d3::Point<S>::from(0, 0, 0);
    u = std::clamp(u, knots_u.first(), knots_u.last());
    v = std::clamp(v, knots_v.first(), knots_v.last());

    // Tensor product: run de Boor down each row in v, then across the results
    // in u. Doing it in one direction then the other is what makes a surface
    // reuse the curve machinery rather than duplicate it.
    std::vector<detail::Homog<3>> row(count_v);
    std::vector<detail::Homog<3>> col(count_u);

    for (std::size_t i = 0; i < count_u; ++i) {
        for (std::size_t j = 0; j < count_v; ++j) {
            const auto& cp = at(i, j);
            const f64 w = cp.w.to_f64();
            row[j].c[0] = cp.p.x.to_f64() * w;
            row[j].c[1] = cp.p.y.to_f64() * w;
            row[j].c[2] = cp.p.z.to_f64() * w;
            row[j].c[3] = w;
        }
        const int sv = knots_v.find_span(degree_v, count_v, v);
        col[i] = detail::de_boor<3>(degree_v, knots_v.knots, row, sv, v);
    }

    const int su = knots_u.find_span(degree_u, count_u, u);
    const auto h = detail::de_boor<3>(degree_u, knots_u.knots, col, su, u);
    const f64 w = std::fabs(h.c[3]) > 1e-300 ? h.c[3] : 1.0;
    return d3::Point<S>::from(h.c[0] / w, h.c[1] / w, h.c[2] / w);
}

template <Scalar S>
d3::Vec<S> NurbsSurface<S>::normal(f64 u, f64 v) const {
    const f64 hu = std::max(1e-7, (knots_u.last() - knots_u.first()) * 1e-5);
    const f64 hv = std::max(1e-7, (knots_v.last() - knots_v.first()) * 1e-5);

    const d3::Point<S> p = evaluate(u, v);
    const d3::Point<S> pu = evaluate(std::min(u + hu, knots_u.last()), v);
    const d3::Point<S> pv = evaluate(u, std::min(v + hv, knots_v.last()));

    const f64 ax = pu.x.to_f64() - p.x.to_f64();
    const f64 ay = pu.y.to_f64() - p.y.to_f64();
    const f64 az = pu.z.to_f64() - p.z.to_f64();
    const f64 bx = pv.x.to_f64() - p.x.to_f64();
    const f64 by = pv.y.to_f64() - p.y.to_f64();
    const f64 bz = pv.z.to_f64() - p.z.to_f64();

    const f64 nx = ay * bz - az * by;
    const f64 ny = az * bx - ax * bz;
    const f64 nz = ax * by - ay * bx;
    const f64 len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-300) return d3::Vec<S>::from(0, 0, 0);
    return d3::Vec<S>::from(nx / len, ny / len, nz / len);
}

// --- exact conics -----------------------------------------------------------
template <Scalar S>
NurbsCurve2<S> circle_nurbs(d2::Point<S> centre, f64 radius) {
    // The nine-point rational quadratic. The corner weights are cos(45), and
    // that number is what makes this EXACT rather than a good approximation --
    // a NURBS circle with weight 1 everywhere is a rounded square.
    NurbsCurve2<S> c;
    c.degree = 2;
    const f64 w = std::sqrt(2.0) / 2.0;
    const f64 cx = centre.x.to_f64(), cy = centre.y.to_f64();
    const f64 r = radius;

    const f64 pts[9][2] = {{cx + r, cy},     {cx + r, cy + r}, {cx, cy + r},
                           {cx - r, cy + r}, {cx - r, cy},     {cx - r, cy - r},
                           {cx, cy - r},     {cx + r, cy - r}, {cx + r, cy}};
    for (int i = 0; i < 9; ++i) {
        ControlPoint2<S> cp;
        cp.p = d2::Point<S>::from(pts[i][0], pts[i][1]);
        cp.w = S::from((i % 2 == 1) ? w : 1.0);
        c.control.push_back(cp);
    }

    c.knots.knots = {0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1};
    return c;
}

template <Scalar S>
NurbsCurve2<S> arc_nurbs(d2::Point<S> centre, f64 radius, f64 start_angle,
                         f64 end_angle) {
    NurbsCurve2<S> c;
    c.degree = 2;
    f64 sweep = end_angle - start_angle;
    while (sweep <= 0.0) sweep += 6.28318530717958647692;
    if (sweep > 6.28318530717958647692) sweep = 6.28318530717958647692;

    // One rational quadratic segment per 90 degrees or less: past that the
    // weight goes negative and the arc turns inside out.
    const int segments = std::max(1, static_cast<int>(std::ceil(sweep / 1.5707963267948966)));
    const f64 step = sweep / segments;
    const f64 w = std::cos(step * 0.5);

    const f64 cx = centre.x.to_f64(), cy = centre.y.to_f64();
    auto on = [&](f64 a) {
        return d2::Point<S>::from(cx + radius * std::cos(a),
                                  cy + radius * std::sin(a));
    };

    ControlPoint2<S> first;
    first.p = on(start_angle);
    first.w = S::from(1.0);
    c.control.push_back(first);

    for (int i = 0; i < segments; ++i) {
        const f64 a0 = start_angle + step * i;
        const f64 a1 = a0 + step;
        // The middle control point is where the two end tangents meet.
        const f64 mid = (a0 + a1) * 0.5;
        const f64 rm = radius / std::cos(step * 0.5);

        ControlPoint2<S> m, e;
        m.p = d2::Point<S>::from(cx + rm * std::cos(mid), cy + rm * std::sin(mid));
        m.w = S::from(w);
        e.p = on(a1);
        e.w = S::from(1.0);
        c.control.push_back(m);
        c.control.push_back(e);
    }

    c.knots.knots.clear();
    c.knots.knots.push_back(0.0);
    c.knots.knots.push_back(0.0);
    c.knots.knots.push_back(0.0);
    for (int i = 1; i < segments; ++i) {
        const f64 t = static_cast<f64>(i) / segments;
        c.knots.knots.push_back(t);
        c.knots.knots.push_back(t);
    }
    c.knots.knots.push_back(1.0);
    c.knots.knots.push_back(1.0);
    c.knots.knots.push_back(1.0);
    return c;
}

template <Scalar S>
NurbsCurve2<S> ellipse_nurbs(d2::Point<S> centre, f64 rx, f64 ry, f64 rotation) {
    // An ellipse is a scaled circle, and a NURBS is closed under affine maps.
    // That is the entire construction: no separate ellipse basis exists.
    NurbsCurve2<S> c = circle_nurbs<S>(d2::Point<S>::from(0, 0), 1.0);
    const f64 co = std::cos(rotation), si = std::sin(rotation);
    const f64 cx = centre.x.to_f64(), cy = centre.y.to_f64();

    for (auto& cp : c.control) {
        const f64 x = cp.p.x.to_f64() * rx;
        const f64 y = cp.p.y.to_f64() * ry;
        cp.p = d2::Point<S>::from(cx + x * co - y * si, cy + x * si + y * co);
    }
    return c;
}

template <Scalar S>
NurbsCurve2<S> line_nurbs(d2::Point<S> a, d2::Point<S> b) {
    NurbsCurve2<S> c;
    c.degree = 1;
    ControlPoint2<S> p0, p1;
    p0.p = a;
    p1.p = b;
    c.control = {p0, p1};
    c.knots.knots = {0, 0, 1, 1};
    return c;
}

// --- fitting ----------------------------------------------------------------
namespace detail {

// Chord-length parameterisation. Uniform parameterisation on unevenly spaced
// points produces a curve that bulges between the close ones, which is the
// classic "why does my fitted spline wobble" complaint.
inline std::vector<f64> chord_params(const std::vector<f64>& x,
                                     const std::vector<f64>& y) {
    std::vector<f64> t(x.size(), 0.0);
    f64 total = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        total += std::hypot(x[i] - x[i - 1], y[i] - y[i - 1]);
        t[i] = total;
    }
    if (total <= 0.0) {
        for (std::size_t i = 0; i < t.size(); ++i)
            t[i] = static_cast<f64>(i) / std::max<std::size_t>(1, t.size() - 1);
        return t;
    }
    for (f64& v : t) v /= total;
    return t;
}

// Solve a dense linear system by Gaussian elimination with partial pivoting.
// The fitting systems here are small and well conditioned; a banded solver
// would be faster and is not what limits anything yet.
inline bool solve_dense(std::vector<f64>& a, std::vector<f64>& b, std::size_t n) {
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r)
            if (std::fabs(a[r * n + col]) > std::fabs(a[pivot * n + col])) pivot = r;
        if (std::fabs(a[pivot * n + col]) < 1e-14) return false;

        if (pivot != col) {
            for (std::size_t k = 0; k < n; ++k)
                std::swap(a[col * n + k], a[pivot * n + k]);
            std::swap(b[col], b[pivot]);
        }
        for (std::size_t r = col + 1; r < n; ++r) {
            const f64 f = a[r * n + col] / a[col * n + col];
            if (f == 0.0) continue;
            for (std::size_t k = col; k < n; ++k) a[r * n + k] -= f * a[col * n + k];
            b[r] -= f * b[col];
        }
    }
    for (std::size_t i = n; i-- > 0;) {
        f64 sum = b[i];
        for (std::size_t k = i + 1; k < n; ++k) sum -= a[i * n + k] * b[k];
        b[i] = sum / a[i * n + i];
    }
    return true;
}

// The basis functions at u, as de Boor's recursion defines them.
inline std::vector<f64> basis(int degree, const std::vector<f64>& knots, int span,
                              f64 u) {
    std::vector<f64> N(static_cast<std::size_t>(degree) + 1, 0.0);
    std::vector<f64> left(static_cast<std::size_t>(degree) + 1, 0.0);
    std::vector<f64> right(static_cast<std::size_t>(degree) + 1, 0.0);
    N[0] = 1.0;
    for (int j = 1; j <= degree; ++j) {
        left[static_cast<std::size_t>(j)] = u - knots[static_cast<std::size_t>(span + 1 - j)];
        right[static_cast<std::size_t>(j)] = knots[static_cast<std::size_t>(span + j)] - u;
        f64 saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const f64 den = right[static_cast<std::size_t>(r + 1)] +
                            left[static_cast<std::size_t>(j - r)];
            const f64 temp = den > 0.0 ? N[static_cast<std::size_t>(r)] / den : 0.0;
            N[static_cast<std::size_t>(r)] =
                saved + right[static_cast<std::size_t>(r + 1)] * temp;
            saved = left[static_cast<std::size_t>(j - r)] * temp;
        }
        N[static_cast<std::size_t>(j)] = saved;
    }
    return N;
}

} // namespace detail

template <Scalar S>
NurbsCurve2<S> interpolate_points(const std::vector<d2::Point<S>>& points,
                                  int degree) {
    NurbsCurve2<S> c;
    if (points.size() < 2) return c;
    if (points.size() == 2) return line_nurbs<S>(points[0], points[1]);

    degree = std::clamp(degree, 1, static_cast<int>(points.size()) - 1);
    c.degree = degree;

    std::vector<f64> x(points.size()), y(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        x[i] = points[i].x.to_f64();
        y[i] = points[i].y.to_f64();
    }
    const std::vector<f64> t = detail::chord_params(x, y);
    const std::size_t n = points.size();

    // Averaging knot placement (Piegl & Tiller). It puts a knot where the data
    // actually is, which keeps the interpolation matrix well conditioned.
    c.knots.knots.assign(n + static_cast<std::size_t>(degree) + 1, 0.0);
    for (std::size_t i = 0; i <= static_cast<std::size_t>(degree); ++i)
        c.knots.knots[i] = 0.0;
    for (std::size_t j = 1; j + static_cast<std::size_t>(degree) < n; ++j) {
        f64 sum = 0.0;
        for (int i = 0; i < degree; ++i) sum += t[j + static_cast<std::size_t>(i)];
        c.knots.knots[j + static_cast<std::size_t>(degree)] = sum / degree;
    }
    for (std::size_t i = 0; i <= static_cast<std::size_t>(degree); ++i)
        c.knots.knots[n + i] = 1.0;

    // One row per data point: the curve must pass THROUGH them, which is what
    // a drafter means by "fit a spline to these".
    std::vector<f64> A(n * n, 0.0), bx(n), by(n);
    for (std::size_t i = 0; i < n; ++i) {
        const int span = c.knots.find_span(degree, n, t[i]);
        const std::vector<f64> N = detail::basis(degree, c.knots.knots, span, t[i]);
        for (int j = 0; j <= degree; ++j) {
            const int col = span - degree + j;
            if (col >= 0 && static_cast<std::size_t>(col) < n)
                A[i * n + static_cast<std::size_t>(col)] = N[static_cast<std::size_t>(j)];
        }
        bx[i] = x[i];
        by[i] = y[i];
    }

    std::vector<f64> Ax = A, Ay = A;
    if (!detail::solve_dense(Ax, bx, n) || !detail::solve_dense(Ay, by, n)) {
        // Singular: fall back to a polyline through the points rather than
        // returning a curve that goes somewhere else entirely.
        c.degree = 1;
        c.control.clear();
        for (const auto& p : points) {
            ControlPoint2<S> cp;
            cp.p = p;
            c.control.push_back(cp);
        }
        c.knots = KnotVector::clamped_uniform(1, c.control.size());
        return c;
    }

    c.control.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        c.control[i].p = d2::Point<S>::from(bx[i], by[i]);
        c.control[i].w = S::from(1.0);
    }
    return c;
}

template <Scalar S>
NurbsCurve2<S> approximate_points(const std::vector<d2::Point<S>>& points,
                                  std::size_t control_count, int degree) {
    if (points.size() < 2) return NurbsCurve2<S>{};
    control_count = std::clamp(control_count, static_cast<std::size_t>(degree) + 1,
                               points.size());
    if (control_count >= points.size())
        return interpolate_points<S>(points, degree);

    NurbsCurve2<S> c;
    c.degree = degree;
    c.knots = KnotVector::clamped_uniform(degree, control_count);

    std::vector<f64> x(points.size()), y(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        x[i] = points[i].x.to_f64();
        y[i] = points[i].y.to_f64();
    }
    const std::vector<f64> t = detail::chord_params(x, y);

    // Normal equations. The ends are pinned to the first and last data points,
    // because a fitted curve that misses the endpoints is never what was
    // wanted, however good the middle is.
    const std::size_t m = control_count;
    std::vector<f64> A(m * m, 0.0), bx(m, 0.0), by(m, 0.0);

    for (std::size_t i = 0; i < points.size(); ++i) {
        const int span = c.knots.find_span(degree, m, t[i]);
        const std::vector<f64> N = detail::basis(degree, c.knots.knots, span, t[i]);
        for (int a = 0; a <= degree; ++a) {
            const int ra = span - degree + a;
            if (ra < 0 || static_cast<std::size_t>(ra) >= m) continue;
            for (int b = 0; b <= degree; ++b) {
                const int rb = span - degree + b;
                if (rb < 0 || static_cast<std::size_t>(rb) >= m) continue;
                A[static_cast<std::size_t>(ra) * m + static_cast<std::size_t>(rb)] +=
                    N[static_cast<std::size_t>(a)] * N[static_cast<std::size_t>(b)];
            }
            bx[static_cast<std::size_t>(ra)] += N[static_cast<std::size_t>(a)] * x[i];
            by[static_cast<std::size_t>(ra)] += N[static_cast<std::size_t>(a)] * y[i];
        }
    }

    // Pin the ends by replacing their rows outright.
    for (std::size_t k = 0; k < m; ++k) {
        A[0 * m + k] = k == 0 ? 1.0 : 0.0;
        A[(m - 1) * m + k] = k == m - 1 ? 1.0 : 0.0;
    }
    bx[0] = x.front();
    by[0] = y.front();
    bx[m - 1] = x.back();
    by[m - 1] = y.back();

    std::vector<f64> Ax = A, Ay = A;
    if (!detail::solve_dense(Ax, bx, m) || !detail::solve_dense(Ay, by, m))
        return interpolate_points<S>(points, degree);

    c.control.resize(m);
    for (std::size_t i = 0; i < m; ++i) {
        c.control[i].p = d2::Point<S>::from(bx[i], by[i]);
        c.control[i].w = S::from(1.0);
    }
    return c;
}

template <Scalar S>
std::vector<d2::Point<S>> simplify_polyline(const std::vector<d2::Point<S>>& points,
                                            f64 tolerance) {
    if (points.size() < 3 || tolerance <= 0.0) return points;

    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;

    // Douglas-Peucker, iteratively: the recursive form blows the stack on the
    // hundred-thousand-point polylines that come out of a scan.
    std::vector<std::pair<std::size_t, std::size_t>> stack{{0, points.size() - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;

        const f64 ax = points[a].x.to_f64(), ay = points[a].y.to_f64();
        const f64 bx = points[b].x.to_f64(), by = points[b].y.to_f64();
        const f64 len = std::hypot(bx - ax, by - ay);

        f64 worst = 0.0;
        std::size_t worst_i = a;
        for (std::size_t i = a + 1; i < b; ++i) {
            const f64 px = points[i].x.to_f64(), py = points[i].y.to_f64();
            const f64 d = len > 1e-300
                              ? std::fabs((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / len
                              : std::hypot(px - ax, py - ay);
            if (d > worst) {
                worst = d;
                worst_i = i;
            }
        }

        if (worst > tolerance) {
            keep[worst_i] = true;
            stack.push_back({a, worst_i});
            stack.push_back({worst_i, b});
        }
    }

    std::vector<d2::Point<S>> out;
    for (std::size_t i = 0; i < points.size(); ++i)
        if (keep[i]) out.push_back(points[i]);
    return out;
}

template <Scalar S>
std::vector<FittedArc<S>> fit_arcs(const std::vector<d2::Point<S>>& points,
                                   f64 tolerance) {
    std::vector<FittedArc<S>> out;
    if (points.size() < 2) return out;
    if (tolerance <= 0.0) tolerance = 1e-6;

    std::size_t start = 0;
    while (start + 1 < points.size()) {
        // Grow a run of points that one arc (or line) fits within tolerance,
        // then commit it. Greedy rather than optimal: an optimal segmentation
        // is quadratic and nobody has ever noticed the difference on a
        // toolpath.
        std::size_t best_end = start + 1;
        FittedArc<S> best;
        best.start = points[start];
        best.end = points[start + 1];
        best.is_line = true;

        for (std::size_t end = start + 2; end < points.size(); ++end) {
            const f64 ax = points[start].x.to_f64(), ay = points[start].y.to_f64();
            const f64 bx = points[end].x.to_f64(), by = points[end].y.to_f64();
            const std::size_t mid = (start + end) / 2;
            const f64 mx = points[mid].x.to_f64(), my = points[mid].y.to_f64();

            // Circle through three points; a vanishing determinant means they
            // are collinear, which is a line rather than a failure.
            const f64 d = 2.0 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
            FittedArc<S> candidate;
            candidate.start = points[start];
            candidate.end = points[end];

            f64 worst = 0.0;
            if (std::fabs(d) < 1e-12) {
                candidate.is_line = true;
                const f64 len = std::hypot(bx - ax, by - ay);
                for (std::size_t i = start + 1; i < end; ++i) {
                    const f64 px = points[i].x.to_f64(), py = points[i].y.to_f64();
                    const f64 dist =
                        len > 1e-300
                            ? std::fabs((bx - ax) * (py - ay) - (by - ay) * (px - ax)) / len
                            : std::hypot(px - ax, py - ay);
                    worst = std::max(worst, dist);
                }
            } else {
                const f64 a2 = ax * ax + ay * ay;
                const f64 m2 = mx * mx + my * my;
                const f64 b2 = bx * bx + by * by;
                const f64 ux = (a2 * (my - by) + m2 * (by - ay) + b2 * (ay - my)) / d;
                const f64 uy = (a2 * (bx - mx) + m2 * (ax - bx) + b2 * (mx - ax)) / d;
                const f64 r = std::hypot(ax - ux, ay - uy);

                candidate.is_line = false;
                candidate.centre = d2::Point<S>::from(ux, uy);
                candidate.radius = r;
                for (std::size_t i = start + 1; i < end; ++i) {
                    const f64 px = points[i].x.to_f64(), py = points[i].y.to_f64();
                    worst = std::max(worst, std::fabs(std::hypot(px - ux, py - uy) - r));
                }
            }

            if (worst > tolerance) break;
            best = candidate;
            best_end = end;
        }

        out.push_back(best);
        start = best_end;
    }
    return out;
}

} // namespace ggeo

// --- inversion -----------------------------------------------------------------
namespace ggeo {

template <Scalar S>
f64 NurbsCurve3<S>::invert(d3::Point<S> p, f64& u, int scan) const {
    const f64 a = knots.knots[static_cast<std::size_t>(degree)];
    const f64 b = knots.knots[control.size()];
    auto dist = [&](f64 t) {
        const d3::Point<S> q = evaluate(t);
        const f64 dx = q.x.to_f64() - p.x.to_f64(), dy = q.y.to_f64() - p.y.to_f64(),
                  dz = q.z.to_f64() - p.z.to_f64();
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    f64 best = a, bd = dist(a);
    for (int i = 1; i <= scan; ++i) {
        const f64 t = a + (b - a) * i / scan;
        const f64 d = dist(t);
        if (d < bd) { bd = d; best = t; }
    }
    // Golden-section refinement around the best sample: robust where Newton
    // on a rational curve is not.
    f64 lo = std::max(a, best - (b - a) / scan), hi = std::min(b, best + (b - a) / scan);
    for (int it = 0; it < 60; ++it) {
        const f64 m1 = lo + (hi - lo) * 0.381966, m2 = hi - (hi - lo) * 0.381966;
        if (dist(m1) < dist(m2)) hi = m2; else lo = m1;
    }
    u = (lo + hi) * 0.5;
    return dist(u);
}

template <Scalar S>
void NurbsSurface<S>::derivatives(f64 u, f64 v, d3::Vec<S>& du, d3::Vec<S>& dv) const {
    const f64 hu = (u_last() - u_first()) * 1e-5, hv = (v_last() - v_first()) * 1e-5;
    const f64 u0 = std::max(u_first(), u - hu), u1 = std::min(u_last(), u + hu);
    const f64 v0 = std::max(v_first(), v - hv), v1 = std::min(v_last(), v + hv);
    const d3::Point<S> pu0 = evaluate(u0, v), pu1 = evaluate(u1, v);
    const d3::Point<S> pv0 = evaluate(u, v0), pv1 = evaluate(u, v1);
    du = d3::Vec<S>::from((pu1.x.to_f64() - pu0.x.to_f64()) / (u1 - u0),
                          (pu1.y.to_f64() - pu0.y.to_f64()) / (u1 - u0),
                          (pu1.z.to_f64() - pu0.z.to_f64()) / (u1 - u0));
    dv = d3::Vec<S>::from((pv1.x.to_f64() - pv0.x.to_f64()) / (v1 - v0),
                          (pv1.y.to_f64() - pv0.y.to_f64()) / (v1 - v0),
                          (pv1.z.to_f64() - pv0.z.to_f64()) / (v1 - v0));
}

template <Scalar S>
f64 NurbsSurface<S>::invert(d3::Point<S> p, f64& u, f64& v, int scan) const {
    const f64 ua = u_first(), ub = u_last(), va = v_first(), vb = v_last();
    const f64 px = p.x.to_f64(), py = p.y.to_f64(), pz = p.z.to_f64();
    auto dist2 = [&](f64 s, f64 t) {
        const d3::Point<S> q = evaluate(s, t);
        const f64 dx = q.x.to_f64() - px, dy = q.y.to_f64() - py, dz = q.z.to_f64() - pz;
        return dx * dx + dy * dy + dz * dz;
    };
    f64 bu = ua, bv = va, bd = dist2(ua, va);
    for (int i = 0; i <= scan; ++i)
        for (int j = 0; j <= scan; ++j) {
            const f64 s = ua + (ub - ua) * i / scan, t = va + (vb - va) * j / scan;
            const f64 d = dist2(s, t);
            if (d < bd) { bd = d; bu = s; bv = t; }
        }
    // Newton on the distance: solve (P - p) . Pu = 0, (P - p) . Pv = 0.
    for (int it = 0; it < 40; ++it) {
        const d3::Point<S> q = evaluate(bu, bv);
        d3::Vec<S> du, dv;
        derivatives(bu, bv, du, dv);
        const f64 rx = q.x.to_f64() - px, ry = q.y.to_f64() - py, rz = q.z.to_f64() - pz;
        const f64 fu = rx * du.x.to_f64() + ry * du.y.to_f64() + rz * du.z.to_f64();
        const f64 fv = rx * dv.x.to_f64() + ry * dv.y.to_f64() + rz * dv.z.to_f64();
        // Gauss-Newton with the first-fundamental-form matrix.
        const f64 E = du.x.to_f64() * du.x.to_f64() + du.y.to_f64() * du.y.to_f64() + du.z.to_f64() * du.z.to_f64();
        const f64 F = du.x.to_f64() * dv.x.to_f64() + du.y.to_f64() * dv.y.to_f64() + du.z.to_f64() * dv.z.to_f64();
        const f64 G = dv.x.to_f64() * dv.x.to_f64() + dv.y.to_f64() * dv.y.to_f64() + dv.z.to_f64() * dv.z.to_f64();
        const f64 det = E * G - F * F;
        if (std::fabs(det) < 1e-30) break;
        const f64 su = (G * fu - F * fv) / det, sv = (E * fv - F * fu) / det;
        const f64 nu = std::clamp(bu - su, ua, ub), nv = std::clamp(bv - sv, va, vb);
        const f64 nd = dist2(nu, nv);
        if (nd < bd) { bd = nd; bu = nu; bv = nv; }
        if (std::fabs(su) < 1e-13 * (ub - ua) && std::fabs(sv) < 1e-13 * (vb - va)) break;
        if (nd >= bd && it > 3) break;
    }
    u = bu; v = bv;
    return std::sqrt(bd);
}

} // namespace ggeo
