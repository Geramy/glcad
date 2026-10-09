#include "test.hpp"

#include <cmath>

#include "geometry/edit.hpp"

using namespace gedit;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

Volume vol() {
    Volume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
Point pt(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }
f64 wx(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    (void)y;
    return x;
}
f64 wy(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    (void)x;
    return y;
}
Shape seg(const Volume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = pt(v, x0, y0);
    s.b = pt(v, x1, y1);
    return s;
}
Shape circle(const Volume& v, f64 cx, f64 cy, f64 r) {
    Shape s;
    s.kind = gsnap::ShapeKind::Circle;
    s.a = pt(v, cx, cy);
    s.radius = static_cast<i64>(std::llround(r * v.scale()));
    return s;
}
Shape arc(const Volume& v, f64 cx, f64 cy, f64 r, f64 a0, f64 a1) {
    Shape s = circle(v, cx, cy, r);
    s.kind = gsnap::ShapeKind::Arc;
    s.start_angle = a0;
    s.end_angle = a1;
    return s;
}

void check_tangency(const Volume& v, const CornerResult& r, const Shape& a,
                    const Shape& b, f64 radius) {
    CHECK(r.connector.kind == gsnap::ShapeKind::Arc);
    const f64 got = static_cast<f64>(r.connector.radius) * v.inv_scale();
    CHECK_MSG(std::fabs(got - radius) < 1e-6, "the arc came out r=%.9f", got);

    const f64 cx = wx(v, r.connector.a), cy = wy(v, r.connector.a);

    auto distance_to = [&](const Shape& s) {
        if (s.kind == gsnap::ShapeKind::Segment) {
            const f64 ax = wx(v, s.a), ay = wy(v, s.a);
            const f64 bx = wx(v, s.b), by = wy(v, s.b);
            const f64 dx = bx - ax, dy = by - ay;
            const f64 len = std::hypot(dx, dy);
            if (len < 1e-12) return 1e30;
            return std::fabs((dx) * (cy - ay) - (dy) * (cx - ax)) / len;
        }
        const f64 sx = wx(v, s.a), sy = wy(v, s.a);
        const f64 R = static_cast<f64>(s.radius) * v.inv_scale();
        return std::fabs(std::hypot(cx - sx, cy - sy) - R);
    };

    CHECK_MSG(std::fabs(distance_to(a) - radius) < 1e-5,
              "the arc centre is %.9f from A, not %.9f", distance_to(a), radius);
    CHECK_MSG(std::fabs(distance_to(b) - radius) < 1e-5,
              "the arc centre is %.9f from B, not %.9f", distance_to(b), radius);
}

}

TEST(corner_fillet_two_lines_still_works_through_the_general_entry) {
    const Volume v = vol();
    const Shape a = seg(v, 0, 0, 6, 0);
    const Shape b = seg(v, 0, 0, 0, 6);
    const CornerResult r = fillet_any(a, b, 1.0, pt(v, 4, 0), pt(v, 0, 4), v);
    CHECK_MSG(r.ok, "%s", r.reason);
    check_tangency(v, r, a, b, 1.0);
}

TEST(corner_fillet_a_line_and_a_circle) {
    const Volume v = vol();
    const Shape line = seg(v, -10, 0, 10, 0);
    const Shape c = circle(v, 0, 4, 3.0);

    const CornerResult r = fillet_any(line, c, 1.0, pt(v, 6, 0), pt(v, 3, 4), v);
    CHECK_MSG(r.ok, "%s", r.reason);
    check_tangency(v, r, line, c, 1.0);
    CHECK_MSG(r.b.kind == gsnap::ShapeKind::Arc, "the circle stayed a circle");
}

TEST(corner_fillet_two_circles) {
    const Volume v = vol();
    const Shape a = circle(v, 0, 0, 4.0);
    const Shape b = circle(v, 12, 0, 4.0);
    const CornerResult r = fillet_any(a, b, 2.0, pt(v, 4, 0), pt(v, 8, 0), v);
    CHECK_MSG(r.ok, "%s", r.reason);
    check_tangency(v, r, a, b, 2.0);
}

TEST(corner_fillet_two_arcs) {
    const Volume v = vol();
    const Shape a = arc(v, 0, 0, 5.0, -kPi * 0.4, kPi * 0.4);
    const Shape b = arc(v, 12, 0, 5.0, kPi * 0.6, kPi * 1.4);
    const CornerResult r = fillet_any(a, b, 1.5, pt(v, 5, 0), pt(v, 7, 0), v);
    CHECK_MSG(r.ok, "%s", r.reason);
    check_tangency(v, r, a, b, 1.5);
}

TEST(corner_fillet_arc_is_the_short_way_round) {
    const Volume v = vol();
    const Shape a = seg(v, 0, 0, 8, 0);
    const Shape b = seg(v, 0, 0, 0, 8);
    const CornerResult r = fillet_any(a, b, 2.0, pt(v, 6, 0), pt(v, 0, 6), v);
    CHECK(r.ok);
    const f64 sweep = r.connector.end_angle - r.connector.start_angle;
    CHECK_MSG(sweep > 0.0 && sweep <= kPi + 1e-9, "the arc sweeps %.6f rad", sweep);
}

TEST(corner_fillet_picks_choose_between_the_candidates) {
    const Volume v = vol();
    const Shape a = seg(v, -10, 0, 10, 0);
    const Shape b = seg(v, 0, -10, 0, 10);

    const CornerResult ne = fillet_any(a, b, 2.0, pt(v, 5, 0), pt(v, 0, 5), v);
    const CornerResult sw = fillet_any(a, b, 2.0, pt(v, -5, 0), pt(v, 0, -5), v);
    CHECK(ne.ok && sw.ok);
    CHECK_MSG(wx(v, ne.connector.a) > 0.0 && wy(v, ne.connector.a) > 0.0,
              "the NE pick landed at (%.3f, %.3f)", wx(v, ne.connector.a),
              wy(v, ne.connector.a));
    CHECK(wx(v, sw.connector.a) < 0.0 && wy(v, sw.connector.a) < 0.0);
}

TEST(corner_fillet_rejects_the_impossible) {
    const Volume v = vol();
    const Shape a = seg(v, 0, 0, 6, 0);
    const Shape b = circle(v, 0, 100, 1.0);
    CHECK(!fillet_any(a, b, 0.0, pt(v, 1, 0), pt(v, 0, 99), v).ok);
    CHECK(!fillet_any(a, b, -1.0, pt(v, 1, 0), pt(v, 0, 99), v).ok);

    Shape point;
    point.kind = gsnap::ShapeKind::Point;
    CHECK(!fillet_any(a, point, 1.0, pt(v, 1, 0), pt(v, 0, 0), v).ok);
}

TEST(corner_chamfer_a_line_and_an_arc) {
    const Volume v = vol();
    const Shape line = seg(v, 0, 0, 10, 0);
    const Shape a = arc(v, 10, 5, 5.0, -kPi * 0.5, kPi * 0.5);

    const CornerResult r =
        chamfer_any(line, a, 1.0, 1.0, pt(v, 2, 0), pt(v, 14, 5), v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK(r.connector.kind == gsnap::ShapeKind::Segment);

    const f64 len = std::hypot(wx(v, r.connector.b) - wx(v, r.connector.a),
                               wy(v, r.connector.b) - wy(v, r.connector.a));
    CHECK_MSG(len > 0.0 && len < 3.0, "the chamfer is %.6f long", len);
}

TEST(corner_chamfer_measures_along_the_arc_not_the_chord) {
    const Volume v = vol();
    const Shape line = seg(v, 0, 0, 10, 0);
    const Shape a = arc(v, 10, 4, 4.0, -kPi * 0.5, kPi * 0.5);

    const f64 dist = 3.0;
    const CornerResult r =
        chamfer_any(line, a, dist, dist, pt(v, 2, 0), pt(v, 14, 4), v);
    CHECK_MSG(r.ok, "%s", r.reason);

    const f64 cx = 10.0, cy = 4.0, R = 4.0;
    const f64 got_angle = std::fabs(std::atan2(wy(v, r.connector.b) - cy,
                                               wx(v, r.connector.b) - cx) -
                                    std::atan2(0.0 - cy, 10.0 - cx));
    CHECK_MSG(std::fabs(got_angle - dist / R) < 1e-4,
              "the arc-side step subtended %.6f rad, expected %.6f", got_angle,
              dist / R);
}

TEST(corner_chamfer_needs_the_objects_to_meet) {
    const Volume v = vol();
    const Shape a = seg(v, 0, 0, 1, 0);
    const Shape b = circle(v, 100, 100, 1.0);
    const CornerResult r =
        chamfer_any(a, b, 0.2, 0.2, pt(v, 0, 0), pt(v, 100, 99), v);
    CHECK(!r.ok);
    CHECK(std::string(r.reason).find("meet") != std::string::npos);
}

TEST(corner_chamfer_rejects_bad_distances) {
    const Volume v = vol();
    const Shape a = seg(v, 0, 0, 6, 0);
    const Shape b = circle(v, 3, 0, 2.0);
    CHECK(!chamfer_any(a, b, 0.0, 1.0, pt(v, 1, 0), pt(v, 3, 2), v).ok);
    CHECK(!chamfer_any(a, b, 1.0, -1.0, pt(v, 1, 0), pt(v, 3, 2), v).ok);
}

TEST(corner_fillet_polyline_rounds_every_corner_at_once) {
    const Volume v = vol();
    const std::vector<Shape> run = {seg(v, 0, 0, 10, 0), seg(v, 10, 0, 10, 10),
                                    seg(v, 10, 10, 0, 10)};

    const PolylineCornerResult r = fillet_polyline(run, 2.0, v);
    CHECK_MSG(r.rounded == 2, "%zu corners rounded", r.rounded);
    CHECK(r.skipped == 0);
    CHECK_MSG(r.shapes.size() == 5, "%zu shapes", r.shapes.size());
    CHECK(r.shapes[1].kind == gsnap::ShapeKind::Arc);
    CHECK(r.shapes[3].kind == gsnap::ShapeKind::Arc);
}

TEST(corner_chamfer_polyline_cuts_every_corner) {
    const Volume v = vol();
    const std::vector<Shape> run = {seg(v, 0, 0, 10, 0), seg(v, 10, 0, 10, 10),
                                    seg(v, 10, 10, 0, 10)};
    const PolylineCornerResult r = chamfer_polyline(run, 1.5, v);
    CHECK(r.rounded == 2);
    CHECK(r.shapes.size() == 5);
    for (const auto& s : r.shapes) CHECK(s.kind == gsnap::ShapeKind::Segment);
}

TEST(corner_a_corner_too_tight_is_skipped_not_fatal) {
    const Volume v = vol();
    const std::vector<Shape> run = {seg(v, 0, 0, 10, 0), seg(v, 10, 0, 10, 0.2),
                                    seg(v, 10, 0.2, 0, 0.2)};
    const PolylineCornerResult r = fillet_polyline(run, 5.0, v);
    CHECK_MSG(r.skipped > 0, "an impossible corner was accepted");
    CHECK(!r.shapes.empty());
}

TEST(corner_polyline_with_nothing_to_round) {
    const Volume v = vol();
    CHECK(fillet_polyline({}, 1.0, v).rounded == 0);
    CHECK(fillet_polyline({seg(v, 0, 0, 1, 0)}, 1.0, v).rounded == 0);
    CHECK(std::string(fillet_polyline({}, 1.0, v).reason).size() > 0);
    CHECK(fillet_polyline({seg(v, 0, 0, 1, 0), seg(v, 1, 0, 1, 1)}, 0.0, v)
              .rounded == 0);
}

TEST(grip_a_line_offers_two_ends_and_a_middle) {
    const Volume v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    const auto g = grips_of(s, v);
    CHECK_MSG(g.size() == 3, "%zu grips", g.size());
    CHECK(g[0].kind == GripKind::Endpoint);
    CHECK(g[1].kind == GripKind::Midpoint);
    CHECK(g[2].kind == GripKind::Endpoint);
    CHECK(std::fabs(wx(v, g[1].at) - 5.0) < 1e-6);
}

TEST(grip_a_circle_offers_a_centre_and_four_quadrants) {
    const Volume v = vol();
    const auto g = grips_of(circle(v, 1, 2, 3.0), v);
    CHECK_MSG(g.size() == 5, "%zu grips", g.size());
    CHECK(g[0].kind == GripKind::Centre);
    for (std::size_t i = 1; i < g.size(); ++i)
        CHECK(g[i].kind == GripKind::Quadrant);
}

TEST(grip_an_arc_offers_its_ends_its_middle_and_its_centre) {
    const Volume v = vol();
    const auto g = grips_of(arc(v, 0, 0, 4.0, 0.0, kPi * 0.5), v);
    CHECK(g.size() == 4);
    CHECK(g[0].kind == GripKind::Centre);
    CHECK(g[1].kind == GripKind::Endpoint);
    CHECK(g[3].kind == GripKind::Endpoint);
}

TEST(grip_picking_finds_the_nearest_within_the_aperture) {
    const Volume v = vol();
    const std::vector<Shape> shapes = {seg(v, 0, 0, 10, 0), seg(v, 0, 5, 10, 5)};
    const auto g = grips_of(shapes, v);
    CHECK(g.size() == 6);

    const Grip hit = pick_grip(g, pt(v, 0.1, 0.1), v, 0.5);
    CHECK_MSG(hit.shape != kNoGrip, "nothing was picked next to an endpoint");
    CHECK(hit.hot);
    CHECK(hit.shape == 0);

    const Grip miss = pick_grip(g, pt(v, 50, 50), v, 0.5);
    CHECK(miss.shape == kNoGrip);
}

TEST(grip_dragging_an_endpoint_stretches_the_line) {
    const Volume v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    const auto g = grips_of(s, v);

    const auto out = drag_grip(s, g[2], pt(v, 15, 3), v);
    CHECK(out.has_value());
    CHECK_MSG(std::fabs(wx(v, out->a)) < 1e-6, "the far end moved");
    CHECK(std::fabs(wx(v, out->b) - 15.0) < 1e-6);
    CHECK(std::fabs(wy(v, out->b) - 3.0) < 1e-6);
}

TEST(grip_dragging_a_midpoint_moves_the_whole_line) {
    const Volume v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    const auto g = grips_of(s, v);

    const auto out = drag_grip(s, g[1], pt(v, 5, 4), v);
    CHECK(out.has_value());
    CHECK_MSG(std::fabs(wy(v, out->a) - 4.0) < 1e-6 &&
                  std::fabs(wy(v, out->b) - 4.0) < 1e-6,
              "the line was stretched instead of moved");
    CHECK(std::fabs(length_of(*out, v) - 10.0) < 1e-6);
}

TEST(grip_dragging_a_quadrant_changes_only_the_radius) {
    const Volume v = vol();
    const Shape s = circle(v, 0, 0, 3.0);
    const auto g = grips_of(s, v);

    const auto out = drag_grip(s, g[1], pt(v, 7, 0), v);
    CHECK(out.has_value());
    CHECK_MSG(out->a.x == s.a.x && out->a.y == s.a.y, "the centre moved");
    CHECK_MSG(std::fabs(static_cast<f64>(out->radius) * v.inv_scale() - 7.0) < 1e-6,
              "radius %.6f", static_cast<f64>(out->radius) * v.inv_scale());
}

TEST(grip_dragging_a_centre_moves_the_curve) {
    const Volume v = vol();
    const Shape s = circle(v, 0, 0, 3.0);
    const auto g = grips_of(s, v);
    const auto out = drag_grip(s, g[0], pt(v, 8, 9), v);
    CHECK(out.has_value());
    CHECK(std::fabs(wx(v, out->a) - 8.0) < 1e-6);
    CHECK(out->radius == s.radius);
}

TEST(grip_dragging_an_arc_end_sweeps_it_without_changing_the_radius) {
    const Volume v = vol();
    const Shape s = arc(v, 0, 0, 4.0, 0.0, kPi * 0.5);
    const auto g = grips_of(s, v);

    const auto out = drag_grip(s, g[3], pt(v, -4, 0), v);
    CHECK(out.has_value());
    CHECK(out->radius == s.radius);
    CHECK_MSG(std::fabs(out->end_angle - kPi) < 1e-6, "end angle %.6f",
              out->end_angle);
}

TEST(grip_kind_names_are_all_defined) {
    for (GripKind k : {GripKind::Endpoint, GripKind::Midpoint, GripKind::Centre,
                       GripKind::Quadrant, GripKind::Vertex, GripKind::Stretch})
        CHECK(std::string(grip_kind_name(k)).size() > 0);
}

TEST(grip_indices_point_back_at_their_object) {
    const Volume v = vol();
    const std::vector<Shape> shapes = {seg(v, 0, 0, 1, 0), circle(v, 5, 5, 1.0)};
    const auto g = grips_of(shapes, v);
    for (const Grip& one : g)
        CHECK_MSG(one.shape < shapes.size(), "a grip named object %u", one.shape);

    std::size_t from_circle = 0;
    for (const Grip& one : g)
        if (one.shape == 1) ++from_circle;
    CHECK(from_circle == 5);
}

TEST(corner_a_filleted_corner_never_loops_around_the_outside) {
    const Volume v = vol();
    struct Case { f64 ax, ay, bx, by, pax, pay, pbx, pby; };
    const Case cases[] = {
        {8, 0, 0, 8, 6, 0, 0, 6},
        {-8, 0, 0, 8, -6, 0, 0, 6},
        {-8, 0, 0, -8, -6, 0, 0, -6},
        {8, 0, 0, -8, 6, 0, 0, -6},
    };

    for (const Case& c : cases) {
        const Shape a = seg(v, 0, 0, c.ax, c.ay);
        const Shape b = seg(v, 0, 0, c.bx, c.by);
        const CornerResult r =
            fillet(a, b, 2.0, pt(v, c.pax, c.pay), pt(v, c.pbx, c.pby), v);
        CHECK_MSG(r.ok, "%s", r.reason);

        const f64 sweep = r.connector.end_angle - r.connector.start_angle;
        CHECK_MSG(sweep > 0.0, "a negative sweep would be read as the major arc");
        CHECK_MSG(sweep <= kPi * 0.5 + 1e-6,
                  "a square corner filleted to a %.6f rad arc", sweep);

        const f64 cx = wx(v, r.connector.a), cy = wy(v, r.connector.a);
        const f64 rad = static_cast<f64>(r.connector.radius) * v.inv_scale();
        for (f64 t : {r.connector.start_angle, r.connector.end_angle}) {
            const f64 px = cx + rad * std::cos(t), py = cy + rad * std::sin(t);
            const f64 d = std::min(std::fabs(px), std::fabs(py));
            CHECK_MSG(d < 1e-6, "an arc end is %.9f off both lines", d);
        }
    }
}
