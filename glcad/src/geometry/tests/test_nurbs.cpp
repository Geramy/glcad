#include "test.hpp"

#include <cmath>

#include "geometry/nurbs.hpp"

using namespace ggeo;
using S = gcm::F64;
using P = d2::Point<S>;

namespace {
constexpr double kPi = 3.14159265358979323846;
P pt(double x, double y) { return P::from(x, y); }
double dist(P a, P b) {
    return std::hypot(a.x.to_f64() - b.x.to_f64(), a.y.to_f64() - b.y.to_f64());
}
}

TEST(nurbs_knot_count_is_forced_by_the_recursion) {
    const KnotVector k = KnotVector::clamped_uniform(3, 6);
    CHECK_MSG(k.size() == 6 + 3 + 1, "%zu knots for 6 points of degree 3",
              k.size());
    CHECK(k.valid(3, 6));
    CHECK_MSG(!k.valid(3, 7), "a wrong control count was accepted");
    CHECK(!k.valid(2, 6));
}

TEST(nurbs_clamped_knots_repeat_at_both_ends) {
    const KnotVector k = KnotVector::clamped_uniform(3, 6);
    CHECK(k.multiplicity(0.0) == 4);
    CHECK(k.multiplicity(1.0) == 4);
    CHECK(k.first() == 0.0 && k.last() == 1.0);
}

TEST(nurbs_knots_are_non_decreasing) {
    const KnotVector k = KnotVector::clamped_uniform(3, 8);
    for (std::size_t i = 1; i < k.size(); ++i)
        CHECK(k.knots[i] >= k.knots[i - 1]);

    KnotVector bad;
    bad.knots = {0, 0, 0, 0.5, 0.2, 1, 1, 1};
    CHECK_MSG(!bad.valid(2, 5), "a decreasing knot vector was accepted");
}

TEST(nurbs_span_lookup_stays_in_range) {
    const KnotVector k = KnotVector::clamped_uniform(3, 7);
    for (int i = 0; i <= 100; ++i) {
        const double u = i / 100.0;
        const int span = k.find_span(3, 7, u);
        CHECK_MSG(span >= 3 && span < 7, "span %d at u=%.3f", span, u);
    }
}

TEST(nurbs_clamped_curve_starts_and_ends_at_its_control_points) {
    NurbsCurve2<S> c;
    c.degree = 3;
    for (auto p : {pt(0, 0), pt(1, 3), pt(4, 3), pt(5, 0)}) {
        ControlPoint2<S> cp;
        cp.p = p;
        c.control.push_back(cp);
    }
    c.knots = KnotVector::clamped_uniform(3, 4);
    CHECK(c.valid());

    CHECK_MSG(dist(c.start(), pt(0, 0)) < 1e-9, "start is off by %.9f",
              dist(c.start(), pt(0, 0)));
    CHECK(dist(c.end(), pt(5, 0)) < 1e-9);
}

TEST(nurbs_curve_stays_inside_its_control_hull) {
    NurbsCurve2<S> c;
    c.degree = 3;
    for (auto p : {pt(0, 0), pt(1, 10), pt(4, 10), pt(5, 0)}) {
        ControlPoint2<S> cp;
        cp.p = p;
        c.control.push_back(cp);
    }
    c.knots = KnotVector::clamped_uniform(3, 4);

    for (int i = 0; i <= 100; ++i) {
        const P p = c.evaluate(i / 100.0);
        CHECK_MSG(p.x.to_f64() >= -1e-9 && p.x.to_f64() <= 5.0 + 1e-9,
                  "x left the hull: %.6f", p.x.to_f64());
        CHECK(p.y.to_f64() >= -1e-9 && p.y.to_f64() <= 10.0 + 1e-9);
    }
}

TEST(nurbs_degree_one_is_a_polyline) {
    NurbsCurve2<S> c;
    c.degree = 1;
    for (auto p : {pt(0, 0), pt(2, 0), pt(2, 2)}) {
        ControlPoint2<S> cp;
        cp.p = p;
        c.control.push_back(cp);
    }
    c.knots = KnotVector::clamped_uniform(1, 3);
    CHECK(c.valid());
    CHECK_MSG(dist(c.evaluate(0.5), pt(2, 0)) < 1e-9, "%.9f",
              dist(c.evaluate(0.5), pt(2, 0)));
}

TEST(nurbs_evaluation_of_an_invalid_curve_is_harmless) {
    NurbsCurve2<S> c;
    CHECK(!c.valid());
    c.evaluate(0.5);
    c.derivative(0.5);
    CHECK(c.tessellate(0.01).empty());
}

TEST(nurbs_derivative_points_along_the_curve) {
    NurbsCurve2<S> c = line_nurbs<S>(pt(0, 0), pt(10, 0));
    const d2::Vec<S> d = c.derivative(0.5);
    CHECK_MSG(std::fabs(d.y.to_f64()) < 1e-6, "a straight line has a y slope of %.9f",
              d.y.to_f64());
    CHECK(d.x.to_f64() > 0.0);
}

TEST(nurbs_circle_is_exact_not_approximate) {
    const NurbsCurve2<S> c = circle_nurbs<S>(pt(3, -2), 5.0);
    CHECK(c.valid());
    CHECK_MSG(c.rational(), "the circle came out non-rational");

    for (int i = 0; i <= 200; ++i) {
        const P p = c.evaluate(i / 200.0);
        const double r = std::hypot(p.x.to_f64() - 3.0, p.y.to_f64() + 2.0);
        CHECK_MSG(std::fabs(r - 5.0) < 1e-9, "radius %.12f at u=%.3f", r,
                  i / 200.0);
    }
}

TEST(nurbs_circle_closes_on_itself) {
    const NurbsCurve2<S> c = circle_nurbs<S>(pt(0, 0), 2.0);
    CHECK(dist(c.start(), c.end()) < 1e-9);
}

TEST(nurbs_arc_is_exact_over_its_sweep) {
    for (double sweep : {kPi * 0.25, kPi * 0.5, kPi, kPi * 1.5, kPi * 1.9}) {
        const NurbsCurve2<S> c = arc_nurbs<S>(pt(1, 1), 3.0, 0.3, 0.3 + sweep);
        CHECK_MSG(c.valid(), "an arc of %.3f rad is not a valid curve", sweep);
        for (int i = 0; i <= 60; ++i) {
            const P p = c.evaluate(i / 60.0);
            const double r = std::hypot(p.x.to_f64() - 1.0, p.y.to_f64() - 1.0);
            CHECK_MSG(std::fabs(r - 3.0) < 1e-8, "sweep %.3f: radius %.12f", sweep, r);
        }
    }
}

TEST(nurbs_arc_endpoints_match_its_angles) {
    const NurbsCurve2<S> c = arc_nurbs<S>(pt(0, 0), 2.0, 0.0, kPi * 0.5);
    CHECK_MSG(dist(c.start(), pt(2, 0)) < 1e-9, "start off by %.9f",
              dist(c.start(), pt(2, 0)));
    CHECK_MSG(dist(c.end(), pt(0, 2)) < 1e-9, "end off by %.9f",
              dist(c.end(), pt(0, 2)));
}

TEST(nurbs_ellipse_is_a_scaled_circle) {
    const NurbsCurve2<S> c = ellipse_nurbs<S>(pt(0, 0), 4.0, 2.0);
    CHECK(c.valid());
    for (int i = 0; i <= 100; ++i) {
        const P p = c.evaluate(i / 100.0);
        const double x = p.x.to_f64() / 4.0, y = p.y.to_f64() / 2.0;
        CHECK_MSG(std::fabs(x * x + y * y - 1.0) < 1e-9,
                  "off the ellipse by %.12f", x * x + y * y - 1.0);
    }
}

TEST(nurbs_rotated_ellipse) {
    const NurbsCurve2<S> c = ellipse_nurbs<S>(pt(0, 0), 4.0, 2.0, kPi * 0.25);
    const double co = std::cos(-kPi * 0.25), si = std::sin(-kPi * 0.25);
    for (int i = 0; i <= 50; ++i) {
        const P p = c.evaluate(i / 50.0);
        const double px = p.x.to_f64(), py = p.y.to_f64();
        const double x = (px * co - py * si) / 4.0;
        const double y = (px * si + py * co) / 2.0;
        CHECK(std::fabs(x * x + y * y - 1.0) < 1e-9);
    }
}

TEST(nurbs_line_needs_no_special_case) {
    const NurbsCurve2<S> c = line_nurbs<S>(pt(-1, -1), pt(3, 7));
    CHECK(c.valid());
    CHECK(!c.rational());
    CHECK(dist(c.evaluate(0.5), pt(1, 3)) < 1e-9);
}

TEST(nurbs_tessellation_respects_its_tolerance) {
    const NurbsCurve2<S> c = circle_nurbs<S>(pt(0, 0), 10.0);
    const auto coarse = c.tessellate(0.5);
    const auto fine = c.tessellate(0.001);
    CHECK_MSG(fine.size() > coarse.size(), "%zu vs %zu points", fine.size(),
              coarse.size());

    for (const auto& p : fine)
        CHECK(std::fabs(std::hypot(p.x.to_f64(), p.y.to_f64()) - 10.0) < 1e-9);

    for (std::size_t i = 1; i < fine.size(); ++i) {
        const double chord = dist(fine[i - 1], fine[i]);
        const double sag = 10.0 - std::sqrt(std::max(0.0, 100.0 - chord * chord / 4.0));
        CHECK_MSG(sag < 0.01, "sag %.9f exceeded the tolerance", sag);
    }
}

TEST(nurbs_tessellation_is_bounded) {
    const NurbsCurve2<S> c = circle_nurbs<S>(pt(0, 0), 1000.0);
    const auto pts = c.tessellate(1e-12, 200);
    CHECK_MSG(pts.size() <= 200, "%zu points ignored the cap", pts.size());
}

TEST(nurbs_flat_surface_evaluates_to_its_plane) {
    NurbsSurface<S> s;
    s.degree_u = 1;
    s.degree_v = 1;
    s.count_u = 2;
    s.count_v = 2;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            ControlPoint3<S> cp;
            cp.p = d3::Point<S>::from(i * 4.0, j * 6.0, 2.0);
            s.control.push_back(cp);
        }
    s.knots_u = KnotVector::clamped_uniform(1, 2);
    s.knots_v = KnotVector::clamped_uniform(1, 2);
    CHECK(s.valid());

    for (int i = 0; i <= 10; ++i)
        for (int j = 0; j <= 10; ++j) {
            const d3::Point<S> p = s.evaluate(i / 10.0, j / 10.0);
            CHECK_MSG(std::fabs(p.z.to_f64() - 2.0) < 1e-9, "z = %.9f",
                      p.z.to_f64());
            CHECK(std::fabs(p.x.to_f64() - i * 0.4) < 1e-9);
            CHECK(std::fabs(p.y.to_f64() - j * 0.6) < 1e-9);
        }
}

TEST(nurbs_surface_normal_is_unit_and_perpendicular) {
    NurbsSurface<S> s;
    s.degree_u = 1;
    s.degree_v = 1;
    s.count_u = 2;
    s.count_v = 2;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            ControlPoint3<S> cp;
            cp.p = d3::Point<S>::from(i * 4.0, j * 6.0, 0.0);
            s.control.push_back(cp);
        }
    s.knots_u = KnotVector::clamped_uniform(1, 2);
    s.knots_v = KnotVector::clamped_uniform(1, 2);

    const d3::Vec<S> n = s.normal(0.5, 0.5);
    const double len = std::sqrt(n.x.to_f64() * n.x.to_f64() +
                                 n.y.to_f64() * n.y.to_f64() +
                                 n.z.to_f64() * n.z.to_f64());
    CHECK_MSG(std::fabs(len - 1.0) < 1e-6, "normal length %.9f", len);
    CHECK_MSG(std::fabs(std::fabs(n.z.to_f64()) - 1.0) < 1e-6,
              "a flat XY surface's normal is (%.4f,%.4f,%.4f)", n.x.to_f64(),
              n.y.to_f64(), n.z.to_f64());
}

TEST(nurbs_invalid_surface_is_harmless) {
    NurbsSurface<S> s;
    CHECK(!s.valid());
    s.evaluate(0.5, 0.5);
    s.normal(0.5, 0.5);
    CHECK(true);
}

TEST(nurbs_interpolation_passes_through_every_point) {
    const std::vector<P> pts = {pt(0, 0),  pt(1, 2), pt(3, 3),
                                pt(6, 1),  pt(8, 4), pt(10, 0)};
    const NurbsCurve2<S> c = interpolate_points<S>(pts, 3);
    CHECK_MSG(c.valid(), "the fitted curve is not valid");

    double total = 0.0;
    std::vector<double> t{0.0};
    for (std::size_t i = 1; i < pts.size(); ++i) {
        total += dist(pts[i - 1], pts[i]);
        t.push_back(total);
    }
    for (double& v : t) v /= total;

    for (std::size_t i = 0; i < pts.size(); ++i) {
        const double d = dist(c.evaluate(t[i]), pts[i]);
        CHECK_MSG(d < 1e-6, "point %zu missed by %.9f", i, d);
    }
}

TEST(nurbs_interpolation_of_two_points_is_a_line) {
    const NurbsCurve2<S> c = interpolate_points<S>({pt(0, 0), pt(4, 4)}, 3);
    CHECK(c.degree == 1);
    CHECK(dist(c.evaluate(0.5), pt(2, 2)) < 1e-9);
}

TEST(nurbs_interpolation_of_nothing_is_nothing) {
    CHECK(!interpolate_points<S>({}, 3).valid());
    CHECK(!interpolate_points<S>({pt(1, 1)}, 3).valid());
}

TEST(nurbs_approximation_uses_fewer_control_points) {
    std::vector<P> pts;
    for (int i = 0; i <= 60; ++i) {
        const double t = i / 60.0 * kPi;
        pts.push_back(pt(t, std::sin(t)));
    }

    const NurbsCurve2<S> c = approximate_points<S>(pts, 8, 3);
    CHECK_MSG(c.valid(), "the approximation is not a valid curve");
    CHECK_MSG(c.control.size() == 8, "%zu control points", c.control.size());

    CHECK_MSG(dist(c.start(), pts.front()) < 1e-9, "start off by %.9f",
              dist(c.start(), pts.front()));
    CHECK(dist(c.end(), pts.back()) < 1e-9);

    double worst = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const P p = c.evaluate(i / 100.0);
        worst = std::max(worst, std::fabs(p.y.to_f64() - std::sin(p.x.to_f64())));
    }
    CHECK_MSG(worst < 0.02, "the approximation strays by %.6f", worst);
}

TEST(nurbs_approximation_with_a_full_budget_interpolates) {
    const std::vector<P> pts = {pt(0, 0), pt(1, 2), pt(3, 3), pt(6, 1)};
    const NurbsCurve2<S> c = approximate_points<S>(pts, 99, 3);
    CHECK(c.valid());
    CHECK(dist(c.start(), pts.front()) < 1e-6);
}

TEST(nurbs_simplify_drops_the_redundant_vertices) {
    std::vector<P> pts;
    for (int i = 0; i <= 100; ++i) pts.push_back(pt(i * 0.1, 0.0));
    pts[50] = pt(5.0, 1.0);

    const auto out = simplify_polyline<S>(pts, 0.01);
    CHECK_MSG(out.size() == 5, "%zu points survived", out.size());
    CHECK(dist(out.front(), pts.front()) < 1e-12);
    CHECK(dist(out.back(), pts.back()) < 1e-12);

    bool has_peak = false;
    for (const auto& p : out)
        if (std::fabs(p.y.to_f64() - 1.0) < 1e-12) has_peak = true;
    CHECK_MSG(has_peak, "the one real corner was dropped");
}

TEST(nurbs_simplify_keeps_what_matters) {
    std::vector<P> pts;
    for (int i = 0; i <= 40; ++i) {
        const double t = i / 40.0 * kPi * 2.0;
        pts.push_back(pt(std::cos(t) * 10.0, std::sin(t) * 10.0));
    }
    const auto out = simplify_polyline<S>(pts, 0.5);
    CHECK_MSG(out.size() > 8 && out.size() < pts.size(),
              "%zu of %zu points kept", out.size(), pts.size());

    for (const auto& p : out)
        CHECK(std::fabs(std::hypot(p.x.to_f64(), p.y.to_f64()) - 10.0) < 1e-9);
}

TEST(nurbs_simplify_of_a_short_run_is_a_no_op) {
    const std::vector<P> pts = {pt(0, 0), pt(1, 1)};
    CHECK(simplify_polyline<S>(pts, 1.0).size() == 2);
    CHECK(simplify_polyline<S>(pts, 0.0).size() == 2);
}

TEST(nurbs_fit_arcs_recognises_a_straight_run) {
    std::vector<P> pts;
    for (int i = 0; i <= 20; ++i) pts.push_back(pt(i * 0.5, 0.0));
    const auto arcs = fit_arcs<S>(pts, 1e-6);
    CHECK_MSG(arcs.size() == 1, "a straight line became %zu pieces", arcs.size());
    CHECK(arcs[0].is_line);
}

TEST(nurbs_fit_arcs_recognises_a_circular_run) {
    std::vector<P> pts;
    for (int i = 0; i <= 40; ++i) {
        const double t = i / 40.0 * kPi * 0.5;
        pts.push_back(pt(std::cos(t) * 5.0, std::sin(t) * 5.0));
    }
    const auto arcs = fit_arcs<S>(pts, 1e-6);
    CHECK_MSG(arcs.size() <= 3, "a quarter circle became %zu pieces", arcs.size());
    bool found_arc = false;
    for (const auto& a : arcs)
        if (!a.is_line) {
            found_arc = true;
            CHECK_MSG(std::fabs(a.radius - 5.0) < 1e-4, "radius %.6f", a.radius);
            CHECK(std::hypot(a.centre.x.to_f64(), a.centre.y.to_f64()) < 1e-4);
        }
    CHECK(found_arc);
}

TEST(nurbs_fit_arcs_covers_the_whole_polyline) {
    std::vector<P> pts;
    for (int i = 0; i <= 30; ++i) pts.push_back(pt(i * 0.3, i < 15 ? 0.0 : (i - 15) * 0.3));
    const auto arcs = fit_arcs<S>(pts, 1e-6);
    CHECK(!arcs.empty());
    CHECK(dist(arcs.front().start, pts.front()) < 1e-9);
    CHECK_MSG(dist(arcs.back().end, pts.back()) < 1e-9,
              "the fit stopped short of the end");
}

TEST(nurbs_fit_arcs_of_a_short_run) {
    CHECK(fit_arcs<S>({}, 0.1).empty());
    CHECK(fit_arcs<S>({pt(0, 0)}, 0.1).empty());
    CHECK(fit_arcs<S>({pt(0, 0), pt(1, 1)}, 0.1).size() == 1);
}
