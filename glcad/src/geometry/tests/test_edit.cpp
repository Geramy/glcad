#include "test.hpp"

#include <cmath>

#include "geometry/edit.hpp"

using namespace gedit;
constexpr f64 kPi = 3.14159265358979323846;

static Volume vol() {
    Volume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
static Point pt(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }
static void w(const Volume& v, Point p, f64& x, f64& y) { gcm::to_world(v, p, x, y); }
static bool at(const Volume& v, Point p, f64 x, f64 y, f64 tol = 1e-3) {
    f64 px, py;
    w(v, p, px, py);
    return std::fabs(px - x) < tol && std::fabs(py - y) < tol;
}
static Shape seg(const Volume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = pt(v, x0, y0);
    s.b = pt(v, x1, y1);
    return s;
}
static Shape circ(const Volume& v, f64 cx, f64 cy, f64 r) {
    Shape s;
    s.kind = gsnap::ShapeKind::Circle;
    s.a = pt(v, cx, cy);
    s.radius = static_cast<i64>(std::llround(r * v.scale()));
    return s;
}
static f64 length(const Volume& v, const Shape& s) {
    f64 ax, ay, bx, by;
    w(v, s.a, ax, ay);
    w(v, s.b, bx, by);
    return std::hypot(bx - ax, by - ay);
}

TEST(edit_translate_preserves_length) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 3, 4);
    const Shape t = translate(s, 1000, -2000);
    CHECK(std::fabs(length(v, t) - 5.0) < 1e-6);
}

TEST(edit_rotate_about_a_point) {
    const auto v = vol();
    const Shape s = seg(v, 1, 0, 2, 0);
    const Shape r = rotate(s, pt(v, 0, 0), kPi * 0.5, v);
    CHECK_MSG(at(v, r.a, 0, 1, 1e-3), "rotated start landed wrong");
    CHECK(at(v, r.b, 0, 2, 1e-3));
    CHECK(std::fabs(length(v, r) - 1.0) < 1e-6);
}

TEST(edit_scale_about_a_point) {
    const auto v = vol();
    const Shape s = seg(v, 1, 1, 2, 1);
    const Shape r = scale(s, pt(v, 0, 0), 3.0);
    CHECK(at(v, r.a, 3, 3, 1e-3));
    CHECK(std::fabs(length(v, r) - 3.0) < 1e-5);

    const Shape c = scale(circ(v, 2, 0, 1), pt(v, 0, 0), 2.0);
    CHECK(at(v, c.a, 4, 0, 1e-3));
    CHECK(std::fabs(f64(c.radius) * v.inv_scale() - 2.0) < 1e-3);
}

TEST(edit_mirror_across_an_axis) {
    const auto v = vol();
    const Shape m = mirror(seg(v, 1, 2, 3, 4), pt(v, 0, 0), pt(v, 0, 1), v);
    CHECK(at(v, m.a, -1, 2, 1e-3));
    CHECK(at(v, m.b, -3, 4, 1e-3));
    CHECK(std::fabs(length(v, m) - length(v, seg(v, 1, 2, 3, 4))) < 1e-5);
}

TEST(edit_mirror_reverses_an_arc_sweep) {
    const auto v = vol();
    Shape a = circ(v, 0, 0, 2);
    a.kind = gsnap::ShapeKind::Arc;
    a.start_angle = 0.0;
    a.end_angle = kPi * 0.5;

    const Shape m = mirror(a, pt(v, 0, 0), pt(v, 0, 1), v);
    CHECK_MSG(std::fabs(m.start_angle - kPi * 0.5) < 1e-6,
              "mirrored start %.4f", m.start_angle);
    CHECK(std::fabs(m.end_angle - kPi) < 1e-6);
}

TEST(edit_rectangular_array_counts_and_spacing) {
    const auto v = vol();
    const std::vector<Shape> in{seg(v, 0, 0, 1, 0)};
    const auto out = array_rectangular(in, 4, 3, 2.0, 5.0, v);
    CHECK_MSG(out.size() == 12, "expected 12 items, got %zu", out.size());

    CHECK(at(v, out[0].a, 0, 0, 1e-3));
    CHECK(at(v, out[1].a, 2, 0, 1e-3));
    CHECK(at(v, out[4].a, 0, 5, 1e-3));
}

TEST(edit_polar_array_full_circle_does_not_duplicate) {
    const auto v = vol();
    const std::vector<Shape> in{seg(v, 2, 0, 3, 0)};

    const auto out = array_polar(in, pt(v, 0, 0), 4, 2.0 * kPi, true, v);
    CHECK(out.size() == 4);
    CHECK(at(v, out[0].a, 2, 0, 1e-3));
    CHECK(at(v, out[1].a, 0, 2, 1e-3));
    CHECK(at(v, out[2].a, -2, 0, 1e-3));
    CHECK(at(v, out[3].a, 0, -2, 1e-3));
}

TEST(edit_polar_array_partial_sweep_includes_both_ends) {
    const auto v = vol();
    const std::vector<Shape> in{seg(v, 2, 0, 3, 0)};
    const auto out = array_polar(in, pt(v, 0, 0), 3, kPi * 0.5, true, v);
    CHECK(out.size() == 3);
    CHECK(at(v, out[0].a, 2, 0, 1e-3));
    CHECK(at(v, out[2].a, 0, 2, 1e-3));
}

TEST(edit_path_array_spaces_by_arc_length) {
    const auto v = vol();
    const std::vector<Shape> in{seg(v, 0, 0, 0.2, 0)};
    const std::vector<Point> path = {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10)};
    const auto out = array_path(in, path, 5, v);
    CHECK(out.size() == 5);
    CHECK(at(v, out[0].a, 0, 0, 1e-2));
    CHECK_MSG(at(v, out[2].a, 10, 0, 1e-2), "midpoint should land on the corner");
    CHECK(at(v, out[4].a, 10, 10, 1e-2));
}

TEST(edit_offset_a_line_to_the_picked_side) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);

    const auto up = offset(s, 2.0, pt(v, 5, 5), v);
    CHECK(up.has_value());
    CHECK_MSG(at(v, up->a, 0, 2, 1e-3), "offset went to the wrong side");

    const auto down = offset(s, 2.0, pt(v, 5, -5), v);
    CHECK(at(v, down->a, 0, -2, 1e-3));

    CHECK(std::fabs(length(v, *up) - 10.0) < 1e-5);
}

TEST(edit_offset_a_circle_grows_or_shrinks) {
    const auto v = vol();
    const Shape c = circ(v, 0, 0, 5);

    const auto out = offset(c, 2.0, pt(v, 9, 0), v);
    CHECK(std::fabs(f64(out->radius) * v.inv_scale() - 7.0) < 1e-3);

    const auto in = offset(c, 2.0, pt(v, 1, 0), v);
    CHECK(std::fabs(f64(in->radius) * v.inv_scale() - 3.0) < 1e-3);

    CHECK(!offset(c, 9.0, pt(v, 0, 0), v).has_value());
}

TEST(edit_trim_removes_the_picked_span) {
    const auto v = vol();
    const Shape target = seg(v, 0, 0, 10, 0);
    const std::vector<Shape> cutters = {seg(v, 3, -1, 3, 1), seg(v, 7, -1, 7, 1)};

    const auto r = trim(target, cutters, pt(v, 5, 0), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK_MSG(r.pieces.size() == 2, "expected 2 pieces, got %zu", r.pieces.size());
    CHECK(at(v, r.pieces[0].a, 0, 0, 1e-3));
    CHECK(at(v, r.pieces[0].b, 3, 0, 1e-3));
    CHECK(at(v, r.pieces[1].a, 7, 0, 1e-3));
    CHECK(at(v, r.pieces[1].b, 10, 0, 1e-3));
}

TEST(edit_trim_an_end_leaves_one_piece) {
    const auto v = vol();
    const auto r = trim(seg(v, 0, 0, 10, 0), {seg(v, 3, -1, 3, 1)}, pt(v, 1, 0), v);
    CHECK(r.changed);
    CHECK(r.pieces.size() == 1);
    CHECK(at(v, r.pieces[0].a, 3, 0, 1e-3));
    CHECK(at(v, r.pieces[0].b, 10, 0, 1e-3));
}

TEST(edit_trim_reports_when_nothing_crosses) {
    const auto v = vol();
    const auto r = trim(seg(v, 0, 0, 10, 0), {seg(v, 0, 5, 10, 5)}, pt(v, 5, 0), v);
    CHECK(!r.changed);
    CHECK_MSG(std::string(r.reason).find("crosses") != std::string::npos,
              "reason was: %s", r.reason);
}

TEST(edit_trim_against_a_circle) {
    const auto v = vol();
    const auto r = trim(seg(v, -10, 0, 10, 0), {circ(v, 0, 0, 4)}, pt(v, 0, 0), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK(r.pieces.size() == 2);
}

TEST(edit_extend_reaches_the_boundary) {
    const auto v = vol();
    const Shape target = seg(v, 0, 0, 5, 0);
    const std::vector<Shape> bounds = {seg(v, 9, -3, 9, 3)};

    const auto r = extend(target, bounds, pt(v, 4.5, 0), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK(r.pieces.size() == 1);
    CHECK_MSG(at(v, r.pieces[0].b, 9, 0, 1e-3), "extended end landed wrong");
    CHECK(at(v, r.pieces[0].a, 0, 0, 1e-3));
}

TEST(edit_extend_the_other_end) {
    const auto v = vol();
    const auto r = extend(seg(v, 0, 0, 5, 0), {seg(v, -4, -3, -4, 3)},
                          pt(v, 0.5, 0), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK(at(v, r.pieces[0].a, -4, 0, 1e-3));
    CHECK(at(v, r.pieces[0].b, 5, 0, 1e-3));
}

TEST(edit_extend_reports_when_there_is_nothing_to_meet) {
    const auto v = vol();
    const auto r = extend(seg(v, 0, 0, 5, 0), {seg(v, -9, -3, -9, 3)},
                          pt(v, 4.5, 0), v);
    CHECK(!r.changed);
    CHECK(std::string(r.reason).find("beyond") != std::string::npos);
}

TEST(edit_fillet_a_right_angle) {
    const auto v = vol();
    const Shape a = seg(v, 0, 0, 10, 0);
    const Shape b = seg(v, 0, 0, 0, 10);

    const auto r = fillet(a, b, 2.0, pt(v, 5, 0), pt(v, 0, 5), v);
    CHECK_MSG(r.ok, "%s", r.reason);

    CHECK_MSG(at(v, r.connector.a, 2, 2, 1e-2), "arc centre landed wrong");
    CHECK(std::fabs(f64(r.connector.radius) * v.inv_scale() - 2.0) < 1e-3);
    CHECK(r.connector.kind == gsnap::ShapeKind::Arc);

    CHECK(at(v, r.a.a, 2, 0, 1e-2));
    CHECK(at(v, r.b.a, 0, 2, 1e-2));

    const f64 sweep = std::fabs(r.connector.end_angle - r.connector.start_angle);
    CHECK_MSG(std::fabs(sweep - kPi * 0.5) < 1e-3, "arc swept %.4f rad", sweep);
}

TEST(edit_fillet_rejects_an_oversized_radius) {
    const auto v = vol();
    const auto r = fillet(seg(v, 0, 0, 3, 0), seg(v, 0, 0, 0, 3), 10.0,
                          pt(v, 2, 0), pt(v, 0, 2), v);
    CHECK(!r.ok);
    CHECK_MSG(std::string(r.reason).find("too large") != std::string::npos,
              "reason was: %s", r.reason);
}

TEST(edit_fillet_rejects_parallel_lines) {
    const auto v = vol();
    const auto r = fillet(seg(v, 0, 0, 10, 0), seg(v, 0, 5, 10, 5), 1.0,
                          pt(v, 5, 0), pt(v, 5, 5), v);
    CHECK(!r.ok);
    CHECK(std::string(r.reason).find("parallel") != std::string::npos);
}

TEST(edit_fillet_on_an_acute_corner) {
    const auto v = vol();
    const Shape a = seg(v, 0, 0, 10, 0);
    const Shape b = seg(v, 0, 0, 10, 10);
    const auto r = fillet(a, b, 1.0, pt(v, 5, 0), pt(v, 5, 5), v);
    CHECK_MSG(r.ok, "%s", r.reason);

    const f64 expect = 1.0 / std::tan(kPi / 8.0);
    f64 x, y;
    w(v, r.a.a, x, y);
    CHECK_MSG(std::fabs(x - expect) < 1e-2, "tangent at %.4f, expected %.4f",
              x, expect);
}

TEST(edit_chamfer_a_right_angle) {
    const auto v = vol();
    const auto r = chamfer(seg(v, 0, 0, 10, 0), seg(v, 0, 0, 0, 10), 2.0, 3.0,
                           pt(v, 5, 0), pt(v, 0, 5), v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK(r.connector.kind == gsnap::ShapeKind::Segment);
    CHECK(at(v, r.connector.a, 2, 0, 1e-2));
    CHECK(at(v, r.connector.b, 0, 3, 1e-2));
    CHECK(at(v, r.a.a, 2, 0, 1e-2));
    CHECK(at(v, r.b.a, 0, 3, 1e-2));
}

TEST(edit_chamfer_equal_distances) {
    const auto v = vol();
    const auto r = chamfer(seg(v, 0, 0, 10, 0), seg(v, 0, 0, 0, 10), 4.0, 4.0,
                           pt(v, 5, 0), pt(v, 0, 5), v);
    CHECK(r.ok);
    CHECK(std::fabs(length(v, r.connector) - 4.0 * std::sqrt(2.0)) < 1e-2);
}

static Shape arc_of(const Volume& v, f64 cx, f64 cy, f64 r, f64 a0, f64 a1) {
    Shape s;
    s.kind = gsnap::ShapeKind::Arc;
    s.a = pt(v, cx, cy);
    s.radius = static_cast<i64>(std::llround(r * v.scale()));
    s.start_angle = a0;
    s.end_angle = a1;
    return s;
}
static bool offers(const std::vector<GripOption>& m, GripOp op) {
    for (const auto& o : m) if (o.op == op) return true;
    return false;
}

TEST(grip_menu_never_disagrees_with_the_drag) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    const auto gs = grips_of(s, v);
    CHECK(!gs.empty());
    for (const Grip& g : gs) {
        const auto menu = grip_menu(s, g);
        CHECK_MSG(!menu.empty(), "%s offered nothing", grip_kind_name(g.kind));
        const auto dragged = drag_grip(s, g, pt(v, 3, 4), v);
        const auto chosen = apply_grip_op(s, g, menu[0].op, pt(v, 3, 4), v);
        CHECK(dragged.has_value() == chosen.has_value());
        if (dragged && chosen) {
            CHECK(dragged->a.x == chosen->a.x && dragged->a.y == chosen->a.y);
            CHECK(dragged->b.x == chosen->b.x && dragged->b.y == chosen->b.y);
        }
    }
}

TEST(grip_menu_is_per_shape_and_per_kind) {
    const auto v = vol();
    const Shape line = seg(v, 0, 0, 10, 0);
    const Shape circle = circ(v, 0, 0, 5);

    Grip end{pt(v, 0, 0), GripKind::Endpoint, 0, 0, false};
    Grip quad{pt(v, 5, 0), GripKind::Quadrant, 0, 0, false};

    CHECK(!offers(grip_menu(line, end), GripOp::Radius));
    CHECK(offers(grip_menu(line, end), GripOp::ConvertToArc));
    CHECK(offers(grip_menu(circle, quad), GripOp::Radius));
    CHECK(offers(grip_menu(circle, quad), GripOp::Diameter));
    CHECK(!offers(grip_menu(circle, quad), GripOp::ConvertToArc));

    Grip centre{pt(v, 0, 0), GripKind::Centre, 0, 0, false};
    CHECK(offers(grip_menu(circle, centre), GripOp::Radius));
    CHECK(!offers(grip_menu(line, centre), GripOp::Radius));
}

TEST(grip_op_not_offered_is_refused_not_approximated) {
    const auto v = vol();
    const Shape line = seg(v, 0, 0, 10, 0);
    Grip end{pt(v, 0, 0), GripKind::Endpoint, 0, 0, false};
    CHECK(!apply_grip_op(line, end, GripOp::Radius, pt(v, 1, 1), v).has_value());
    CHECK(!apply_grip_op(line, end, GripOp::Rotate, pt(v, 1, 1), v).has_value());
}

TEST(grip_lengthen_ignores_off_axis_drift) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    Grip end{pt(v, 10, 0), GripKind::Endpoint, 0, 1, false};

    const auto r = apply_grip_op(s, end, GripOp::Lengthen, pt(v, 14, 7), v);
    CHECK(r.has_value());
    CHECK_MSG(at(v, r->b, 14, 0, 1e-3), "the end left the line");
    CHECK(at(v, r->a, 0, 0, 1e-3));

    const auto st = apply_grip_op(s, end, GripOp::Stretch, pt(v, 14, 7), v);
    CHECK(st.has_value() && at(v, st->b, 14, 7, 1e-3));
}

TEST(grip_lengthen_shortens_too) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    Grip end{pt(v, 10, 0), GripKind::Endpoint, 0, 1, false};
    const auto r = apply_grip_op(s, end, GripOp::Lengthen, pt(v, 3, 0), v);
    CHECK(r.has_value());
    CHECK(std::fabs(length(v, *r) - 3.0) < 1e-3);
}

TEST(grip_lengthen_to_zero_is_refused) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    Grip end{pt(v, 10, 0), GripKind::Endpoint, 0, 1, false};
    CHECK(!apply_grip_op(s, end, GripOp::Lengthen, pt(v, 0, 0), v).has_value());
}

TEST(grip_convert_to_arc_passes_through_the_dragged_point) {
    const auto v = vol();
    const Shape s = seg(v, -5, 0, 5, 0);
    Grip mid{pt(v, 0, 0), GripKind::Midpoint, 0, 0, false};

    const auto r = apply_grip_op(s, mid, GripOp::ConvertToArc, pt(v, 0, 5), v);
    CHECK(r.has_value());
    CHECK(r->kind == gsnap::ShapeKind::Arc);
    f64 cx, cy;
    w(v, r->a, cx, cy);
    const f64 rad = f64(r->radius) / v.scale();
    CHECK_MSG(std::fabs(std::hypot(-5.0 - cx, 0.0 - cy) - rad) < 1e-2,
              "the first end is not on the arc");
    CHECK(std::fabs(std::hypot(5.0 - cx, 0.0 - cy) - rad) < 1e-2);
    CHECK_MSG(std::fabs(std::hypot(0.0 - cx, 5.0 - cy) - rad) < 1e-2,
              "the dragged point is not on the arc");

    auto ccw = [](f64 from, f64 to) {
        f64 d = to - from;
        while (d < 0.0) d += 2.0 * kPi;
        return d;
    };
    const f64 pa = std::atan2(5.0 - cy, 0.0 - cx);
    CHECK_MSG(ccw(r->start_angle, pa) <= ccw(r->start_angle, r->end_angle) + 1e-9,
              "the arc runs away from the point that defined it");
}

TEST(grip_convert_to_arc_the_other_way_round) {
    const auto v = vol();
    const Shape s = seg(v, -5, 0, 5, 0);
    Grip mid{pt(v, 0, 0), GripKind::Midpoint, 0, 0, false};
    const auto r = apply_grip_op(s, mid, GripOp::ConvertToArc, pt(v, 0, -5), v);
    CHECK(r.has_value());
    f64 cx, cy;
    w(v, r->a, cx, cy);
    auto ccw = [](f64 from, f64 to) {
        f64 d = to - from;
        while (d < 0.0) d += 2.0 * kPi;
        return d;
    };
    const f64 pa = std::atan2(-5.0 - cy, 0.0 - cx);
    CHECK(ccw(r->start_angle, pa) <= ccw(r->start_angle, r->end_angle) + 1e-9);
}

TEST(grip_convert_to_arc_refuses_collinear) {
    const auto v = vol();
    const Shape s = seg(v, -5, 0, 5, 0);
    Grip mid{pt(v, 0, 0), GripKind::Midpoint, 0, 0, false};
    CHECK(!apply_grip_op(s, mid, GripOp::ConvertToArc, pt(v, 2, 0), v).has_value());
}

TEST(grip_convert_to_line_is_the_chord) {
    const auto v = vol();
    const Shape a = arc_of(v, 0, 0, 5, 0.0, kPi * 0.5);
    Grip end{pt(v, 5, 0), GripKind::Endpoint, 0, 1, false};
    const auto r = apply_grip_op(a, end, GripOp::ConvertToLine, {}, v);
    CHECK(r.has_value());
    CHECK(r->kind == gsnap::ShapeKind::Segment);
    CHECK(at(v, r->a, 5, 0, 1e-2));
    CHECK(at(v, r->b, 0, 5, 1e-2));
    CHECK(std::fabs(length(v, *r) - 5.0 * std::sqrt(2.0)) < 1e-2);
}

TEST(grip_convert_to_line_refuses_a_full_circle_sweep) {
    const auto v = vol();
    const Shape a = arc_of(v, 0, 0, 5, 0.0, 2.0 * kPi);
    Grip end{pt(v, 5, 0), GripKind::Endpoint, 0, 1, false};
    CHECK(!apply_grip_op(a, end, GripOp::ConvertToLine, {}, v).has_value());
}

TEST(grip_diameter_is_half_of_radius) {
    const auto v = vol();
    const Shape c = circ(v, 0, 0, 5);
    Grip quad{pt(v, 5, 0), GripKind::Quadrant, 0, 0, false};

    const auto by_r = apply_grip_op(c, quad, GripOp::Radius, pt(v, 8, 0), v);
    const auto by_d = apply_grip_op(c, quad, GripOp::Diameter, pt(v, 8, 0), v);
    CHECK(by_r.has_value() && by_d.has_value());
    CHECK(std::fabs(f64(by_r->radius) / v.scale() - 8.0) < 1e-3);
    CHECK(std::fabs(f64(by_d->radius) / v.scale() - 4.0) < 1e-3);
}

TEST(grip_radius_of_zero_is_refused) {
    const auto v = vol();
    const Shape c = circ(v, 0, 0, 5);
    Grip quad{pt(v, 5, 0), GripKind::Quadrant, 0, 0, false};
    CHECK(!apply_grip_op(c, quad, GripOp::Radius, pt(v, 0, 0), v).has_value());
}

TEST(grip_rotate_preserves_the_sweep) {
    const auto v = vol();
    const Shape a = arc_of(v, 0, 0, 5, 0.0, kPi * 0.5);
    Grip end{pt(v, 5, 0), GripKind::Endpoint, 0, 1, false};

    const auto r = apply_grip_op(a, end, GripOp::Rotate, pt(v, 0, 5), v);
    CHECK(r.has_value());
    const f64 before = a.end_angle - a.start_angle;
    const f64 after = r->end_angle - r->start_angle;
    CHECK_MSG(std::fabs(after - before) < 1e-9, "the sweep changed by %g",
              after - before);
    CHECK(std::fabs(r->start_angle - kPi * 0.5) < 1e-9);
    CHECK(r->radius == a.radius);

    const auto d = drag_grip(a, end, pt(v, 0, 5), v);
    CHECK(d.has_value());
    CHECK(std::fabs((d->end_angle - d->start_angle) - before) > 1e-6);
}

TEST(grip_move_keeps_the_handle_under_the_cursor) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    Grip end{pt(v, 10, 0), GripKind::Endpoint, 0, 1, false};
    Grip mid{pt(v, 5, 0), GripKind::Midpoint, 0, 0, false};
    const auto r = apply_grip_op(s, mid, GripOp::Move, pt(v, 5, 9), v);
    CHECK(r.has_value());
    CHECK(at(v, r->a, 0, 9, 1e-3));
    CHECK(at(v, r->b, 10, 9, 1e-3));
    CHECK(std::fabs(length(v, *r) - 10.0) < 1e-6);
}

TEST(grip_add_vertex_returns_the_first_half) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    Grip end{pt(v, 0, 0), GripKind::Endpoint, 0, 0, false};
    const auto r = apply_grip_op(s, end, GripOp::AddVertex, {}, v);
    CHECK(r.has_value());
    CHECK(at(v, r->a, 0, 0, 1e-3));
    CHECK(at(v, r->b, 5, 0, 1e-3));
}

TEST(grip_menu_options_that_need_no_point_say_so) {
    const auto v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    Grip end{pt(v, 0, 0), GripKind::Endpoint, 0, 0, false};
    for (const auto& o : grip_menu(s, end)) {
        if (o.op == GripOp::AddVertex) CHECK(!o.needs_point);
        if (o.op == GripOp::Stretch)   CHECK(o.needs_point);
        CHECK(o.label[0] != '\0');
    }
}

TEST(elliptical_arcs_keep_their_axes_and_interval_through_edit_transforms) {
    const auto v=vol();Shape arc;arc.kind=gsnap::ShapeKind::Ellipse;
    arc.a=pt(v,1,2);arc.b=pt(v,5,2);arc.radius=std::llround(2*v.scale());arc.start_angle=.5;arc.end_angle=2.5;
    const auto rotated=rotate(arc,{},kPi/2,v);CHECK(at(v,rotated.a,-2,1));CHECK(at(v,rotated.b,-2,5));
    CHECK(rotated.start_angle==.5);CHECK(rotated.end_angle==2.5);
    const auto scaled=scale(arc,{},-2);CHECK(at(v,scaled.a,-2,-4));CHECK(at(v,scaled.b,-10,-4));CHECK(scaled.radius==2*arc.radius);
    CHECK(scaled.start_angle==.5);CHECK(scaled.end_angle==2.5);
    const auto reflected=mirror(arc,pt(v,0,0),pt(v,1,0),v);
    CHECK(at(v,reflected.a,1,-2));CHECK(at(v,reflected.b,5,-2));CHECK(reflected.start_angle==-2.5);CHECK(reflected.end_angle==-.5);
}
