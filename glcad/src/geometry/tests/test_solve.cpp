#include "test.hpp"

#include <cmath>

#include "geometry/system.hpp"

using namespace gsolve;
constexpr f64 kPi = 3.14159265358979323846;

static bool near(f64 a, f64 b, f64 tol = 1e-6) { return std::fabs(a - b) < tol; }

static Constraint make(Kind k, std::vector<u32> e, f64 v = 0.0) {
    Constraint c;
    c.kind = k;
    c.entities = std::move(e);
    c.value = v;
    return c;
}

TEST(solve_coincident_pulls_points_together) {
    System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(5, 3);
    s.fix(a);
    s.add(make(Kind::Coincident, {a, b}));

    const auto r = s.solve();
    CHECK_MSG(r.ok(), "%s", r.message());

    f64 ax, ay, bx, by;
    s.point_at(a, ax, ay);
    s.point_at(b, bx, by);
    CHECK(near(ax, bx) && near(ay, by));
    CHECK_MSG(near(ax, 0.0) && near(ay, 0.0), "the fixed point moved");
}

TEST(solve_horizontal_and_vertical) {
    System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(4, 2);
    const u32 l = s.add_line(a, b);
    s.fix(a);
    s.add(make(Kind::Horizontal, {l}));

    CHECK(s.solve().ok());
    f64 ax, ay, bx, by;
    s.point_at(a, ax, ay);
    s.point_at(b, bx, by);
    CHECK_MSG(near(ay, by), "line is not horizontal: %.6f vs %.6f", ay, by);

    System t;
    const u32 c = t.add_point(0, 0);
    const u32 d = t.add_point(4, 2);
    const u32 m = t.add_line(c, d);
    t.fix(c);
    t.add(make(Kind::Vertical, {m}));
    CHECK(t.solve().ok());
    f64 cx, cy, dx, dy;
    t.point_at(c, cx, cy);
    t.point_at(d, dx, dy);
    CHECK(near(cx, dx));
}

TEST(solve_distance_sets_the_length) {
    System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(3, 0);
    s.fix(a);
    s.add(make(Kind::Distance, {a, b}, 10.0));

    CHECK(s.solve().ok());
    f64 ax, ay, bx, by;
    s.point_at(a, ax, ay);
    s.point_at(b, bx, by);
    CHECK_MSG(near(std::hypot(bx - ax, by - ay), 10.0, 1e-5),
              "length is %.6f", std::hypot(bx - ax, by - ay));
}

TEST(solve_perpendicular_lines) {
    System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(5, 0);
    const u32 c = s.add_point(0, 0);
    const u32 d = s.add_point(3, 3);
    const u32 l1 = s.add_line(a, b);
    const u32 l2 = s.add_line(c, d);
    s.fix(a); s.fix(b); s.fix(c);
    s.add(make(Kind::Perpendicular, {l1, l2}));

    CHECK(s.solve().ok());
    f64 ax, ay, bx, by, cx, cy, dx, dy;
    s.point_at(a, ax, ay); s.point_at(b, bx, by);
    s.point_at(c, cx, cy); s.point_at(d, dx, dy);
    const f64 dot = (bx - ax) * (dx - cx) + (by - ay) * (dy - cy);
    CHECK_MSG(near(dot, 0.0, 1e-5), "dot product %.8f", dot);
}

TEST(solve_parallel_lines) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    const u32 c = s.add_point(0, 3), d = s.add_point(4, 5);
    const u32 l1 = s.add_line(a, b), l2 = s.add_line(c, d);
    s.fix(a); s.fix(b); s.fix(c);
    s.add(make(Kind::Parallel, {l1, l2}));

    CHECK(s.solve().ok());
    f64 ax, ay, bx, by, cx, cy, dx, dy;
    s.point_at(a, ax, ay); s.point_at(b, bx, by);
    s.point_at(c, cx, cy); s.point_at(d, dx, dy);
    const f64 cross = (bx - ax) * (dy - cy) - (by - ay) * (dx - cx);
    CHECK(near(cross, 0.0, 1e-5));
}

TEST(solve_equal_lengths) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    const u32 c = s.add_point(0, 3), d = s.add_point(2, 3);
    const u32 l1 = s.add_line(a, b), l2 = s.add_line(c, d);
    s.fix(a); s.fix(b); s.fix(c);
    s.add(make(Kind::Equal, {l1, l2}));
    s.add(make(Kind::Horizontal, {l2}));

    CHECK(s.solve().ok());
    f64 cx, cy, dx, dy;
    s.point_at(c, cx, cy);
    s.point_at(d, dx, dy);
    CHECK_MSG(near(std::hypot(dx - cx, dy - cy), 5.0, 1e-5),
              "second line is %.6f long", std::hypot(dx - cx, dy - cy));
}

TEST(solve_radius_and_diameter) {
    System s;
    const u32 c = s.add_point(0, 0);
    const u32 circle = s.add_circle(c, 3.0);
    s.fix(c);
    s.add(make(Kind::Radius, {circle}, 7.5));
    CHECK(s.solve().ok());
    CHECK(near(s.radius_of(circle), 7.5, 1e-6));

    System t;
    const u32 c2 = t.add_point(0, 0);
    const u32 circ2 = t.add_circle(c2, 3.0);
    t.fix(c2);
    t.add(make(Kind::Diameter, {circ2}, 10.0));
    CHECK(t.solve().ok());
    CHECK(near(t.radius_of(circ2), 5.0, 1e-6));
}

TEST(solve_concentric_circles) {
    System s;
    const u32 c1 = s.add_point(0, 0), c2 = s.add_point(4, 5);
    const u32 a = s.add_circle(c1, 2.0), b = s.add_circle(c2, 3.0);
    s.fix(c1);
    s.add(make(Kind::Concentric, {a, b}));

    CHECK(s.solve().ok());
    f64 x1, y1, x2, y2;
    s.point_at(c1, x1, y1);
    s.point_at(c2, x2, y2);
    CHECK(near(x1, x2) && near(y1, y2));
}

TEST(solve_point_on_line) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(10, 0);
    const u32 p = s.add_point(5, 4);
    const u32 l = s.add_line(a, b);
    s.fix(a); s.fix(b);
    s.add(make(Kind::PointOnLine, {p, l}));

    CHECK(s.solve().ok());
    f64 px, py;
    s.point_at(p, px, py);
    CHECK_MSG(near(py, 0.0, 1e-5), "point is %.6f off the line", py);
}

TEST(solve_tangent_line_to_circle) {
    System s;
    const u32 cc = s.add_point(0, 0);
    const u32 circle = s.add_circle(cc, 2.0);
    const u32 a = s.add_point(-5, 3), b = s.add_point(5, 3);
    const u32 l = s.add_line(a, b);
    s.fix(cc);
    s.add(make(Kind::Radius, {circle}, 2.0));
    s.add(make(Kind::Horizontal, {l}));
    s.add(make(Kind::Tangent, {l, circle}));

    CHECK_MSG(s.solve().ok(), "%s", s.solve().message());
    f64 ax, ay;
    s.point_at(a, ax, ay);
    CHECK_MSG(near(std::fabs(ay), 2.0, 1e-4),
              "tangent line sits at y = %.6f, expected +-2", ay);
}

TEST(solve_angle_between_lines) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    const u32 c = s.add_point(0, 0), d = s.add_point(4, 1);
    const u32 l1 = s.add_line(a, b), l2 = s.add_line(c, d);
    s.fix(a); s.fix(b); s.fix(c);
    s.add(make(Kind::Angle, {l1, l2}, kPi / 3.0));

    CHECK(s.solve().ok());
    f64 cx, cy, dx, dy;
    s.point_at(c, cx, cy);
    s.point_at(d, dx, dy);
    const f64 ang = std::atan2(dy - cy, dx - cx);
    CHECK_MSG(near(ang, kPi / 3.0, 1e-4), "angle is %.6f rad", ang);
}

TEST(solve_symmetric_about_a_line) {
    System s;
    const u32 a1 = s.add_point(0, 0), a2 = s.add_point(0, 10);
    const u32 axis = s.add_line(a1, a2);
    const u32 p = s.add_point(-3, 4);
    const u32 q = s.add_point(5, 6);
    s.fix(a1); s.fix(a2); s.fix(p);
    s.add(make(Kind::Symmetric, {p, q, axis}));

    CHECK_MSG(s.solve().ok(), "%s", s.solve().message());
    f64 px, py, qx, qy;
    s.point_at(p, px, py);
    s.point_at(q, qx, qy);
    CHECK_MSG(near(qx, -px, 1e-4), "mirror x: %.6f vs %.6f", qx, -px);
    CHECK_MSG(near(qy, py, 1e-4), "mirror y: %.6f vs %.6f", qy, py);
}

TEST(solve_a_fully_constrained_rectangle) {
    System s;
    const u32 p0 = s.add_point(0, 0);
    const u32 p1 = s.add_point(6, 1);
    const u32 p2 = s.add_point(7, 4);
    const u32 p3 = s.add_point(1, 3);

    const u32 bottom = s.add_line(p0, p1);
    const u32 right  = s.add_line(p1, p2);
    const u32 top    = s.add_line(p2, p3);
    const u32 left   = s.add_line(p3, p0);

    s.fix(p0);
    s.add(make(Kind::Horizontal, {bottom}));
    s.add(make(Kind::Vertical, {right}));
    s.add(make(Kind::Horizontal, {top}));
    s.add(make(Kind::Vertical, {left}));
    s.add(make(Kind::Distance, {p0, p1}, 10.0));
    s.add(make(Kind::Distance, {p1, p2}, 4.0));

    const auto r = s.solve();
    CHECK_MSG(r.ok(), "%s", r.message());

    f64 x0, y0, x1, y1, x2, y2, x3, y3;
    s.point_at(p0, x0, y0);
    s.point_at(p1, x1, y1);
    s.point_at(p2, x2, y2);
    s.point_at(p3, x3, y3);

    CHECK(near(y0, y1, 1e-5));
    CHECK(near(x1, x2, 1e-5));
    CHECK(near(y2, y3, 1e-5));
    CHECK(near(x3, x0, 1e-5));
    CHECK_MSG(near(std::fabs(x1 - x0), 10.0, 1e-4), "width %.6f",
              std::fabs(x1 - x0));
    CHECK_MSG(near(std::fabs(y2 - y1), 4.0, 1e-4), "height %.6f",
              std::fabs(y2 - y1));

    const Dof d = s.analyse();
    CHECK_MSG(d.fully_constrained(), "%s, %d DOF remaining", d.describe(),
              d.remaining);
}

TEST(solve_dof_counts_down_as_constraints_are_added) {
    System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(5, 2);
    const u32 l = s.add_line(a, b);

    Dof d = s.analyse();
    CHECK(d.parameters == 4);
    CHECK_MSG(d.remaining == 4, "expected 4 DOF, got %d", d.remaining);
    CHECK(d.under_constrained());

    s.fix(a);
    d = s.analyse();
    CHECK_MSG(d.remaining == 2, "after pinning a point: %d DOF", d.remaining);

    s.add(make(Kind::Horizontal, {l}));
    s.solve();
    d = s.analyse();
    CHECK_MSG(d.remaining == 1, "after horizontal: %d DOF", d.remaining);

    s.add(make(Kind::Distance, {a, b}, 5.0));
    s.solve();
    d = s.analyse();
    CHECK_MSG(d.fully_constrained(), "%s with %d DOF", d.describe(), d.remaining);
}

TEST(solve_detects_redundant_constraints) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    const u32 l = s.add_line(a, b);
    s.fix(a);
    s.add(make(Kind::Horizontal, {l}));
    s.add(make(Kind::Horizontal, {l}));

    const Dof d = s.analyse();
    CHECK_MSG(d.redundant >= 1, "redundant constraint not detected (%d)",
              d.redundant);
    CHECK(d.over_constrained());
    CHECK(std::string(d.describe()) == "Over-constrained");
}

TEST(solve_would_conflict_warns_before_the_damage) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    const u32 l = s.add_line(a, b);
    s.fix(a);
    s.add(make(Kind::Horizontal, {l}));

    CHECK_MSG(s.would_conflict(make(Kind::Horizontal, {l})),
              "a duplicate constraint should be reported as conflicting");
    CHECK_MSG(!s.would_conflict(make(Kind::Distance, {a, b}, 5.0)),
              "an independent constraint was wrongly flagged");
}

TEST(solve_reports_overconstrained_rather_than_failing_silently) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    s.fix(a);
    s.add(make(Kind::Distance, {a, b}, 5.0));
    s.add(make(Kind::Distance, {a, b}, 9.0));

    const auto r = s.solve();
    CHECK(!r.ok());
    CHECK_MSG(r.status == Status::Overconstrained,
              "status was \"%s\"", r.message());
    CHECK_MSG(!r.conflicting.empty(), "no conflicting constraints named");
}

TEST(solve_already_satisfied_does_no_work) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    const u32 l = s.add_line(a, b);
    s.fix(a);
    s.add(make(Kind::Horizontal, {l}));

    const auto r = s.solve();
    CHECK(r.status == Status::AlreadySatisfied);
    CHECK(r.iterations == 0);
}

TEST(solve_reference_constraints_do_not_drive) {
    System s;
    const u32 a = s.add_point(0, 0), b = s.add_point(5, 0);
    s.fix(a);

    Constraint c = make(Kind::Distance, {a, b}, 99.0);
    c.driving = false;
    s.add(c);

    s.solve();
    f64 bx, by;
    s.point_at(b, bx, by);
    CHECK_MSG(near(bx, 5.0), "a reference constraint moved the geometry to %.4f",
              bx);
}

TEST(solve_constraint_names_are_readable) {
    CHECK(std::string(kind_name(Kind::Perpendicular)) == "perpendicular");
    CHECK(std::string(kind_name(Kind::Concentric)) == "concentric");
    CHECK(std::string(kind_name(Kind::Tangent)) == "tangent");
}
