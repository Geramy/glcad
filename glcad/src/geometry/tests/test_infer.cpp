#include "test.hpp"

#include <cmath>

#include "geometry/system.hpp"

using namespace gsolve;

namespace {
constexpr f64 kPi = 3.14159265358979323846;

struct L { u32 a, b, line; };
L add_line(System& s, f64 x0, f64 y0, f64 x1, f64 y1) {
    const u32 a = s.add_point(x0, y0);
    const u32 b = s.add_point(x1, y1);
    return {a, b, s.add_line(a, b)};
}

bool has(const std::vector<Constraint>& v, Kind k) {
    for (const auto& c : v)
        if (c.kind == k) return true;
    return false;
}
std::size_t count(const std::vector<Constraint>& v, Kind k) {
    std::size_t n = 0;
    for (const auto& c : v)
        if (c.kind == k) ++n;
    return n;
}
}

TEST(solve_constraint_touches_only_its_own_parameters) {
    System s;
    const L a = add_line(s, 0, 0, 4, 0);
    const L b = add_line(s, 10, 10, 14, 10);
    (void)b;

    Constraint c;
    c.kind = Kind::Horizontal;
    c.entities = {a.line};
    const auto touched = s.touched_parameters(c);

    CHECK_MSG(touched.size() == 4, "a line's horizontal touches %zu parameters",
              touched.size());
    for (u32 p : touched) CHECK(p < 4);
}

TEST(solve_circle_constraint_touches_centre_and_radius) {
    System s;
    const u32 c0 = s.add_point(0, 0);
    const u32 circle = s.add_circle(c0, 2.0);

    Constraint c;
    c.kind = Kind::Radius;
    c.entities = {circle};
    c.value = 3.0;
    const auto touched = s.touched_parameters(c);
    CHECK_MSG(touched.size() == 3, "%zu parameters", touched.size());
}

TEST(solve_independent_geometry_falls_into_separate_clusters) {
    System s;
    const L a = add_line(s, 0, 0, 4, 0.3);
    const L b = add_line(s, 20, 20, 24, 20.3);

    s.add({Kind::Horizontal, {a.line}, 0.0, "", true});
    s.add({Kind::Horizontal, {b.line}, 0.0, "", true});

    const auto clusters = s.decompose();
    CHECK_MSG(clusters.size() == 2, "%zu clusters", clusters.size());
    for (const auto& c : clusters) CHECK(c.constraints.size() == 1);
}

TEST(solve_shared_geometry_is_one_cluster) {
    System s;
    const u32 corner = s.add_point(4, 0);
    const u32 a0 = s.add_point(0, 0);
    const u32 b1 = s.add_point(4, 5);
    const u32 la = s.add_line(a0, corner);
    const u32 lb = s.add_line(corner, b1);

    s.add({Kind::Horizontal, {la}, 0.0, "", true});
    s.add({Kind::Vertical, {lb}, 0.0, "", true});

    const auto clusters = s.decompose();
    CHECK_MSG(clusters.size() == 1, "a shared corner split into %zu clusters",
              clusters.size());
    CHECK(clusters[0].constraints.size() == 2);
}

TEST(solve_a_fixed_point_separates_clusters) {
    System s;
    const u32 mid = s.add_point(4, 0);
    const u32 a0 = s.add_point(0, 0.4);
    const u32 b1 = s.add_point(9, 0.4);
    const u32 la = s.add_line(a0, mid);
    const u32 lb = s.add_line(mid, b1);
    s.fix(mid);

    s.add({Kind::Horizontal, {la}, 0.0, "", true});
    s.add({Kind::Horizontal, {lb}, 0.0, "", true});

    const auto clusters = s.decompose();
    CHECK_MSG(clusters.size() == 2, "the pin did not separate them (%zu)",
              clusters.size());
}

TEST(solve_clusters_reaches_the_same_answer_as_solving_together) {
    System a, b;
    for (System* s : {&a, &b}) {
        const L p = add_line(*s, 0, 0, 4, 0.3);
        const L q = add_line(*s, 20, 20, 24, 20.3);
        s->fix(p.a);
        s->fix(q.a);
        s->add({Kind::Horizontal, {p.line}, 0.0, "", true});
        s->add({Kind::Horizontal, {q.line}, 0.0, "", true});
    }

    const SolveResult ra = a.solve();
    const SolveResult rb = b.solve_clusters();
    CHECK_MSG(ra.ok(), "%s", ra.message());
    CHECK_MSG(rb.ok(), "%s", rb.message());

    for (u32 e = 0; e < a.entity_count(); ++e) {
        if (a.entities()[e].kind != EntityKind::Point) continue;
        f64 ax, ay, bx, by;
        a.point_at(e, ax, ay);
        b.point_at(e, bx, by);
        CHECK_MSG(std::hypot(bx - ax, by - ay) < 1e-6,
                  "entity %u differs: (%.6f,%.6f) vs (%.6f,%.6f)", e, ax, ay, bx, by);
    }
}

TEST(solve_clusters_of_a_single_system_is_just_solve) {
    System s;
    const L a = add_line(s, 0, 0, 4, 0.3);
    s.fix(a.a);
    s.add({Kind::Horizontal, {a.line}, 0.0, "", true});
    const SolveResult r = s.solve_clusters();
    CHECK(r.ok());
    f64 x0, y0, x1, y1;
    s.point_at(a.a, x0, y0);
    s.point_at(a.b, x1, y1);
    CHECK(std::fabs(y1 - y0) < 1e-8);
}

TEST(solve_one_failing_cluster_does_not_condemn_the_others) {
    System s;
    const L good = add_line(s, 0, 0, 4, 0.3);
    const L bad = add_line(s, 20, 20, 24, 20.3);
    s.fix(good.a);
    s.fix(bad.a);
    s.fix(bad.b);

    s.add({Kind::Horizontal, {good.line}, 0.0, "", true});
    s.add({Kind::Horizontal, {bad.line}, 0.0, "", true});

    s.solve_clusters();
    f64 x0, y0, x1, y1;
    s.point_at(good.a, x0, y0);
    s.point_at(good.b, x1, y1);
    CHECK_MSG(std::fabs(y1 - y0) < 1e-6,
              "the solvable cluster was abandoned (%.6f vs %.6f)", y0, y1);
}

TEST(infer_finds_a_nearly_horizontal_line) {
    System s;
    add_line(s, 0, 0, 4, 0.01);
    const auto found = infer_constraints(s);
    CHECK_MSG(has(found, Kind::Horizontal), "a 0.14-degree line was not seen");
}

TEST(infer_prefers_horizontal_over_parallel) {
    System s;
    add_line(s, 0, 0, 4, 0.005);
    add_line(s, 0, 5, 4, 5.005);
    const auto found = infer_constraints(s);
    CHECK(count(found, Kind::Horizontal) == 2);
    CHECK_MSG(!has(found, Kind::Parallel),
              "both lines got horizontal AND parallel, which is redundant");
}

TEST(infer_finds_perpendicular_and_parallel) {
    System s;
    add_line(s, 0, 0, 4, 2);
    add_line(s, 0, 5, 4, 7.01);
    add_line(s, 9, 0, 7, 4);
    const auto found = infer_constraints(s);
    CHECK(has(found, Kind::Parallel));
    CHECK(has(found, Kind::Perpendicular));
}

TEST(infer_finds_coincident_endpoints) {
    System s;
    s.add_point(0.0, 0.0);
    s.add_point(1e-9, -1e-9);
    const auto found = infer_constraints(s);
    CHECK(has(found, Kind::Coincident));
}

TEST(infer_leaves_deliberately_separate_points_alone) {
    System s;
    s.add_point(0, 0);
    s.add_point(1.0, 0);
    const auto found = infer_constraints(s);
    CHECK_MSG(!has(found, Kind::Coincident), "a 1 m gap read as coincident");
}

TEST(infer_finds_equal_lengths_and_radii) {
    System s;
    add_line(s, 0, 0, 3, 4);
    add_line(s, 10, 0, 13, 4);
    const u32 c0 = s.add_point(0, 20);
    const u32 c1 = s.add_point(9, 20);
    s.add_circle(c0, 2.0);
    s.add_circle(c1, 2.0);

    const auto found = infer_constraints(s);
    CHECK_MSG(count(found, Kind::Equal) >= 2, "%zu equal constraints",
              count(found, Kind::Equal));
}

TEST(infer_finds_concentric_circles) {
    System s;
    const u32 c0 = s.add_point(1, 1);
    const u32 c1 = s.add_point(1, 1);
    s.add_circle(c0, 2.0);
    s.add_circle(c1, 5.0);
    const auto found = infer_constraints(s);
    CHECK(has(found, Kind::Concentric));
}

TEST(infer_finds_tangency) {
    System s;
    const u32 c = s.add_point(0, 0);
    s.add_circle(c, 2.0);
    add_line(s, -5, 2.0, 5, 2.0);
    const auto found = infer_constraints(s);
    CHECK_MSG(has(found, Kind::Tangent), "a grazing line was not seen as tangent");
}

TEST(infer_respects_its_switches) {
    System s;
    add_line(s, 0, 0, 4, 0.005);
    InferenceSettings off;
    off.horizontal = false;
    const auto found = infer_constraints(s, off);
    CHECK_MSG(!has(found, Kind::Horizontal), "a disabled rule still fired");
}

TEST(infer_respects_its_tolerances) {
    System s;
    add_line(s, 0, 0, 4, 0.35);
    InferenceSettings tight;
    tight.angle_tolerance_deg = 1.0;
    CHECK(!has(infer_constraints(s, tight), Kind::Horizontal));

    InferenceSettings loose;
    loose.angle_tolerance_deg = 10.0;
    CHECK(has(infer_constraints(s, loose), Kind::Horizontal));
}

TEST(infer_caps_its_suggestions) {
    System s;
    for (int i = 0; i < 30; ++i) add_line(s, 0, i, 4, i);
    InferenceSettings few;
    few.max_suggestions = 5;
    CHECK(infer_constraints(s, few).size() <= 5);
}

TEST(auto_constrain_applies_and_solves) {
    System s;
    const L a = add_line(s, 0, 0, 4, 0.01);
    s.fix(a.a);

    const std::size_t n = auto_constrain(s);
    CHECK_MSG(n > 0, "nothing was applied");
    CHECK(s.constraint_count() == n);

    const SolveResult r = s.solve();
    CHECK_MSG(r.ok(), "%s", r.message());
    f64 x0, y0, x1, y1;
    s.point_at(a.a, x0, y0);
    s.point_at(a.b, x1, y1);
    CHECK_MSG(std::fabs(y1 - y0) < 1e-6, "the line was not levelled");
}

TEST(auto_constrain_refuses_the_redundant) {
    System s;
    const L a = add_line(s, 0, 0, 4, 0.001);
    s.fix(a.a);
    s.add({Kind::Horizontal, {a.line}, 0.0, "", true});

    const std::size_t before = s.constraint_count();
    auto_constrain(s);
    const Dof d = s.analyse();
    CHECK_MSG(!d.over_constrained(),
              "auto-constrain over-constrained the sketch (%s)", d.describe());
    CHECK(s.constraint_count() >= before);
}

TEST(auto_constrain_on_an_empty_system_does_nothing) {
    System s;
    CHECK(auto_constrain(s) == 0);
    CHECK(s.decompose().empty());
}

TEST(jacobian_analytic_agrees_with_finite_differences) {
    System s;
    const u32 p0 = s.add_point(0.3, 0.7);
    const u32 p1 = s.add_point(4.1, 1.9);
    const u32 p2 = s.add_point(2.2, 5.3);
    const u32 p3 = s.add_point(6.9, 3.1);
    const u32 la = s.add_line(p0, p1);
    const u32 lb = s.add_line(p2, p3);
    const u32 cen = s.add_point(1.5, 2.5);
    const u32 circ = s.add_circle(cen, 1.75);
    const u32 cen2 = s.add_point(4.5, 0.5);
    const u32 circ2 = s.add_circle(cen2, 2.25);

    const std::vector<Constraint> tests = {
        {Kind::Coincident, {p0, p1}, 0.0, "", true},
        {Kind::Horizontal, {la}, 0.0, "", true},
        {Kind::Vertical, {lb}, 0.0, "", true},
        {Kind::Parallel, {la, lb}, 0.0, "", true},
        {Kind::Perpendicular, {la, lb}, 0.0, "", true},
        {Kind::Distance, {p0, p1}, 3.0, "", true},
        {Kind::HorizontalDist, {p0, p1}, 2.0, "", true},
        {Kind::VerticalDist, {p0, p1}, 2.0, "", true},
        {Kind::Radius, {circ}, 2.0, "", true},
        {Kind::Diameter, {circ}, 4.0, "", true},
        {Kind::PointOnCircle, {p0, circ}, 0.0, "", true},
        {Kind::Equal, {la, lb}, 0.0, "", true},
        {Kind::Equal, {circ, circ2}, 0.0, "", true},
        {Kind::Concentric, {circ, circ2}, 0.0, "", true},
        {Kind::FixPoint, {p0}, 0.0, "", true},
    };

    struct Owner { u32 entity; u32 component; bool is_point; };
    std::vector<Owner> owner(s.parameter_count(), {kInvalid, 0, false});
    for (u32 e = 0; e < s.entity_count(); ++e) {
        const Entity& en = s.entities()[e];
        if (en.base == kInvalid) continue;
        for (u32 k = 0; k < en.count; ++k)
            if (en.base + k < owner.size())
                owner[en.base + k] = {e, k, en.kind == EntityKind::Point};
    }

    for (const Constraint& c : tests) {
        std::vector<std::vector<std::pair<u32, f64>>> rows;
        CHECK_MSG(s.constraint_jacobian(c, rows), "%s has no analytic rows",
                  kind_name(c.kind));

        for (std::size_t i = 0; i < rows.size(); ++i)
            for (const auto& [param, analytic] : rows[i]) {
                CHECK(param < owner.size());
                if (!owner[param].is_point) continue;

                const f64 h = 1e-6;
                System up = s, down = s;
                f64 x, y;
                s.point_at(owner[param].entity, x, y);
                const bool is_x = owner[param].component == 0;
                up.set_point(owner[param].entity, is_x ? x + h : x,
                             is_x ? y : y + h);
                down.set_point(owner[param].entity, is_x ? x - h : x,
                               is_x ? y : y - h);
                up.add(c);
                down.add(c);

                const std::vector<f64> ru = up.residuals();
                const std::vector<f64> rd = down.residuals();
                if (i >= ru.size() || i >= rd.size()) continue;

                const f64 numeric = (ru[i] - rd[i]) / (2.0 * h);
                CHECK_MSG(std::fabs(numeric - analytic) < 1e-4,
                          "%s row %zu param %u: analytic %.9f, numeric %.9f",
                          kind_name(c.kind), i, param, analytic, numeric);
            }
    }

    std::vector<std::vector<std::pair<u32, f64>>> rows;
    CHECK(s.constraint_jacobian({Kind::Radius, {circ}, 2.0, "", true}, rows));
    CHECK(rows.size() == 1 && rows[0].size() == 1);
    CHECK(std::fabs(rows[0][0].second - 1.0) < 1e-12);

    CHECK(s.constraint_jacobian({Kind::Diameter, {circ}, 4.0, "", true}, rows));
    CHECK_MSG(std::fabs(rows[0][0].second - 2.0) < 1e-12,
              "a diameter's derivative should be twice a radius's");
}

TEST(jacobian_declines_the_kinds_with_no_closed_form) {
    System s;
    const u32 p0 = s.add_point(0, 0);
    const u32 p1 = s.add_point(4, 0);
    const u32 p2 = s.add_point(0, 3);
    const u32 la = s.add_line(p0, p1);
    const u32 lb = s.add_line(p1, p2);

    std::vector<std::vector<std::pair<u32, f64>>> rows;
    CHECK(!s.constraint_jacobian({Kind::PointOnLine, {p2, la}, 0.0, "", true}, rows));
    CHECK(!s.constraint_jacobian({Kind::Collinear, {la, lb}, 0.0, "", true}, rows));
    CHECK(!s.constraint_jacobian({Kind::Angle, {la, lb}, 1.0, "", true}, rows));
}

TEST(jacobian_declines_a_degenerate_distance) {
    System s;
    const u32 a = s.add_point(1, 1);
    const u32 b = s.add_point(1, 1);
    std::vector<std::vector<std::pair<u32, f64>>> rows;
    CHECK(!s.constraint_jacobian({Kind::Distance, {a, b}, 5.0, "", true}, rows));
}

TEST(solve_still_converges_on_a_larger_sketch) {
    System s;
    std::vector<u32> pts, lines;
    const int n = 30;
    for (int i = 0; i <= n; ++i)
        pts.push_back(s.add_point(i * 1.0, (i % 2) ? 0.05 : -0.05));
    for (int i = 0; i < n; ++i) lines.push_back(s.add_line(pts[i], pts[i + 1]));
    s.fix(pts[0]);

    for (u32 l : lines) s.add({Kind::Horizontal, {l}, 0.0, "", true});

    const SolveResult r = s.solve(80, 1e-9);
    CHECK_MSG(r.ok(), "%s after %d iterations", r.message(), r.iterations);

    f64 x0, y0;
    s.point_at(pts[0], x0, y0);
    for (u32 p : pts) {
        f64 x, y;
        s.point_at(p, x, y);
        CHECK_MSG(std::fabs(y - y0) < 1e-5, "point strayed to y=%.9f", y);
    }
}

TEST(jacobian_accumulates_a_shared_parameter) {
    System s;
    const u32 corner = s.add_point(0, 0);
    const u32 a1 = s.add_point(5, 0);
    const u32 b1 = s.add_point(1, 4);
    const u32 la = s.add_line(corner, a1);
    const u32 lb = s.add_line(corner, b1);

    s.fix(corner);
    s.add({Kind::Perpendicular, {la, lb}, 0.0, "", true});

    const SolveResult r = s.solve();
    CHECK_MSG(r.ok(), "%s", r.message());

    f64 cx, cy, ax, ay, bx, by;
    s.point_at(corner, cx, cy);
    s.point_at(a1, ax, ay);
    s.point_at(b1, bx, by);
    const f64 ux = ax - cx, uy = ay - cy;
    const f64 vx = bx - cx, vy = by - cy;
    const f64 dot = (ux * vx + uy * vy) / (std::hypot(ux, uy) * std::hypot(vx, vy));
    CHECK_MSG(std::fabs(dot) < 1e-4, "cos of the corner angle is %.9f", dot);
}

TEST(jacobian_shared_corner_survives_parallel_too) {
    System s;
    const u32 shared = s.add_point(0, 0);
    const u32 a1 = s.add_point(5, 0.4);
    const u32 b1 = s.add_point(-3, 1.0);
    const u32 la = s.add_line(shared, a1);
    const u32 lb = s.add_line(shared, b1);
    s.fix(shared);
    s.fix(a1);
    s.add({Kind::Parallel, {la, lb}, 0.0, "", true});

    const SolveResult r = s.solve();
    CHECK_MSG(r.ok(), "%s", r.message());

    f64 sx, sy, ax, ay, bx, by;
    s.point_at(shared, sx, sy);
    s.point_at(a1, ax, ay);
    s.point_at(b1, bx, by);
    const f64 cross = (ax - sx) * (by - sy) - (ay - sy) * (bx - sx);
    CHECK_MSG(std::fabs(cross) < 1e-5, "cross product is %.9f", cross);
}
