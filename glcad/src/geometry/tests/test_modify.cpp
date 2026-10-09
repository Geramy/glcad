#include "test.hpp"

#include <cmath>

#include "geometry/edit.hpp"

using namespace gedit;

namespace {

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
Shape arc(const Volume& v, f64 cx, f64 cy, f64 r, f64 a0, f64 a1) {
    Shape s;
    s.kind = gsnap::ShapeKind::Arc;
    s.a = pt(v, cx, cy);
    s.radius = static_cast<i64>(std::llround(r * v.scale()));
    s.start_angle = a0;
    s.end_angle = a1;
    return s;
}
Shape circle(const Volume& v, f64 cx, f64 cy, f64 r) {
    Shape s;
    s.kind = gsnap::ShapeKind::Circle;
    s.a = pt(v, cx, cy);
    s.radius = static_cast<i64>(std::llround(r * v.scale()));
    return s;
}

constexpr f64 kPi = 3.14159265358979323846;

}

TEST(modify_stretch_moves_what_is_enclosed_and_stretches_what_is_clipped) {
    const Volume v = vol();
    const std::vector<Shape> in = {
        seg(v, 0, 0, 10, 0),
        seg(v, 6, 1, 9, 1),
        seg(v, -5, 5, -1, 5),
    };
    const Window w = make_window(pt(v, 5, -2), pt(v, 12, 3));

    const StretchResult r = stretch(in, w, static_cast<i64>(2.0 * v.scale()), 0);
    CHECK(r.shapes.size() == 3);
    CHECK_MSG(r.stretched == 1, "%zu stretched", r.stretched);
    CHECK_MSG(r.moved == 1, "%zu moved", r.moved);
    CHECK_MSG(r.untouched == 1, "%zu untouched", r.untouched);

    CHECK(std::fabs(wx(v, r.shapes[0].a) - 0.0) < 1e-6);
    CHECK_MSG(std::fabs(wx(v, r.shapes[0].b) - 12.0) < 1e-6, "%.6f",
              wx(v, r.shapes[0].b));

    CHECK(std::fabs(wx(v, r.shapes[1].a) - 8.0) < 1e-6);
    CHECK(std::fabs(wx(v, r.shapes[1].b) - 11.0) < 1e-6);

    CHECK(std::fabs(wx(v, r.shapes[2].a) + 5.0) < 1e-6);
}

TEST(modify_stretch_leaves_a_circle_a_circle) {
    const Volume v = vol();
    const std::vector<Shape> in = {circle(v, 0, 0, 3)};
    const Window w = make_window(pt(v, -1, -1), pt(v, 1, 1));
    const StretchResult r = stretch(in, w, static_cast<i64>(5.0 * v.scale()), 0);
    CHECK(r.shapes[0].kind == gsnap::ShapeKind::Circle);
    CHECK(r.shapes[0].radius == in[0].radius);
    CHECK_MSG(std::fabs(wx(v, r.shapes[0].a) - 5.0) < 1e-6,
              "the centre was caught, so it should have moved");
}

TEST(modify_window_containment_uses_the_whole_extent) {
    const Volume v = vol();
    const Window w = make_window(pt(v, -2, -2), pt(v, 2, 2));
    CHECK(w.contains(circle(v, 0, 0, 1)));
    CHECK_MSG(!w.contains(circle(v, 0, 0, 5)),
              "a circle bigger than the window read as contained");
    CHECK(w.crosses(circle(v, 0, 0, 5)));
    CHECK(!w.crosses(seg(v, 10, 10, 20, 20)));
}

TEST(modify_length_of_every_kind) {
    const Volume v = vol();
    CHECK(std::fabs(length_of(seg(v, 0, 0, 3, 4), v) - 5.0) < 1e-6);
    CHECK(std::fabs(length_of(circle(v, 0, 0, 2), v) - 2.0 * kPi * 2.0) < 1e-4);
    CHECK(std::fabs(length_of(arc(v, 0, 0, 2, 0, kPi), v) - kPi * 2.0) < 1e-4);
}

TEST(modify_lengthen_moves_the_end_nearest_the_pick) {
    const Volume v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);

    const auto right = lengthen(s, LengthenMode::Total, 15.0, pt(v, 9.5, 0), v);
    CHECK(right.has_value());
    CHECK_MSG(std::fabs(wx(v, right->a)) < 1e-6, "the anchor end moved");
    CHECK_MSG(std::fabs(wx(v, right->b) - 15.0) < 1e-6, "%.6f", wx(v, right->b));

    const auto left = lengthen(s, LengthenMode::Total, 15.0, pt(v, 0.5, 0), v);
    CHECK(left.has_value());
    CHECK_MSG(std::fabs(wx(v, left->b) - 10.0) < 1e-6, "the anchor end moved");
    CHECK_MSG(std::fabs(wx(v, left->a) + 5.0) < 1e-6, "%.6f", wx(v, left->a));
}

TEST(modify_lengthen_modes) {
    const Volume v = vol();
    const Shape s = seg(v, 0, 0, 10, 0);
    const Point p = pt(v, 10, 0);

    CHECK(std::fabs(length_of(*lengthen(s, LengthenMode::Total, 4.0, p, v), v) -
                    4.0) < 1e-6);
    CHECK(std::fabs(length_of(*lengthen(s, LengthenMode::Delta, 2.5, p, v), v) -
                    12.5) < 1e-6);
    CHECK(std::fabs(length_of(*lengthen(s, LengthenMode::Percent, 150.0, p, v), v) -
                    15.0) < 1e-6);
}

TEST(modify_lengthen_an_arc_changes_its_sweep) {
    const Volume v = vol();
    const Shape a = arc(v, 0, 0, 2, 0, kPi * 0.5);
    const f64 before = length_of(a, v);
    const auto out = lengthen(a, LengthenMode::Delta, 1.0, pt(v, 0, 2), v);
    CHECK(out.has_value());
    CHECK_MSG(std::fabs(length_of(*out, v) - (before + 1.0)) < 1e-4,
              "%.6f vs %.6f", length_of(*out, v), before + 1.0);
    CHECK(out->radius == a.radius);
}

TEST(modify_lengthen_refuses_the_impossible) {
    const Volume v = vol();
    CHECK(!lengthen(seg(v, 0, 0, 10, 0), LengthenMode::Total, -1.0,
                    pt(v, 10, 0), v).has_value());
    CHECK(!lengthen(seg(v, 0, 0, 10, 0), LengthenMode::Delta, -20.0,
                    pt(v, 10, 0), v).has_value());
    CHECK(!lengthen(circle(v, 0, 0, 2), LengthenMode::Total, 5.0, pt(v, 2, 0), v)
               .has_value());
    CHECK(!lengthen(arc(v, 0, 0, 2, 0, kPi), LengthenMode::Total, 100.0,
                    pt(v, 0, 2), v).has_value());
}

TEST(modify_break_removes_the_span_between_two_points) {
    const Volume v = vol();
    const BreakResult r = break_between(seg(v, 0, 0, 10, 0), pt(v, 3, 0),
                                        pt(v, 7, 0), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK(r.pieces.size() == 2);
    CHECK(std::fabs(wx(v, r.pieces[0].b) - 3.0) < 1e-6);
    CHECK(std::fabs(wx(v, r.pieces[1].a) - 7.0) < 1e-6);
    CHECK(std::fabs(length_of(r.pieces[0], v) - 3.0) < 1e-6);
    CHECK(std::fabs(length_of(r.pieces[1], v) - 3.0) < 1e-6);
}

TEST(modify_break_at_an_end_leaves_one_piece) {
    const Volume v = vol();
    const BreakResult r = break_between(seg(v, 0, 0, 10, 0), pt(v, 0, 0),
                                        pt(v, 4, 0), v);
    CHECK(r.changed);
    CHECK_MSG(r.pieces.size() == 1, "%zu pieces", r.pieces.size());
    CHECK(std::fabs(length_of(r.pieces[0], v) - 6.0) < 1e-6);
}

TEST(modify_breaking_a_circle_gives_an_arc) {
    const Volume v = vol();
    const BreakResult r = break_between(circle(v, 0, 0, 2), pt(v, 2, 0),
                                        pt(v, 0, 2), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK(r.pieces.size() == 1);
    CHECK_MSG(r.pieces[0].kind == gsnap::ShapeKind::Arc,
              "a broken circle is still a circle");
    CHECK_MSG(std::fabs(length_of(r.pieces[0], v) - 1.5 * kPi * 2.0) < 1e-3,
              "%.6f", length_of(r.pieces[0], v));
}

TEST(modify_break_an_arc_in_the_middle) {
    const Volume v = vol();
    const Shape a = arc(v, 0, 0, 2, 0, kPi);
    const BreakResult r = break_between(a, pt(v, 1.4142, 1.4142),
                                        pt(v, 0, 2), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK(r.pieces.size() == 2);
    CHECK(length_of(r.pieces[0], v) + length_of(r.pieces[1], v) <
          length_of(a, v) + 1e-6);
}

TEST(modify_break_at_a_point_splits_without_a_gap) {
    const Volume v = vol();
    const BreakResult r = break_at(seg(v, 0, 0, 10, 0), pt(v, 4, 0), v);
    CHECK_MSG(r.changed, "%s", r.reason);
    CHECK(r.pieces.size() == 2);
    CHECK(std::fabs(length_of(r.pieces[0], v) + length_of(r.pieces[1], v) - 10.0) <
          1e-6);
}

TEST(modify_break_rejects_points_off_the_object) {
    const Volume v = vol();
    const BreakResult r = break_between(seg(v, 0, 0, 10, 0), pt(v, 20, 0),
                                        pt(v, 30, 0), v);
    CHECK(!r.changed);
    CHECK(std::string(r.reason).size() > 0);

    CHECK(!break_at(seg(v, 0, 0, 10, 0), pt(v, 0, 0), v).changed);
}

TEST(modify_join_merges_collinear_touching_segments) {
    const Volume v = vol();
    const std::vector<Shape> in = {seg(v, 0, 0, 4, 0), seg(v, 4, 0, 10, 0)};
    const JoinResult r = join(in, v);
    CHECK_MSG(r.joined == 1, "%zu joined", r.joined);
    CHECK(r.shapes.size() == 1);
    CHECK_MSG(std::fabs(length_of(r.shapes[0], v) - 10.0) < 1e-6, "%.6f",
              length_of(r.shapes[0], v));
}

TEST(modify_join_handles_every_touching_orientation) {
    const Volume v = vol();
    const JoinResult r = join({seg(v, 0, 0, 4, 0), seg(v, 10, 0, 4, 0)}, v);
    CHECK(r.joined == 1);
    CHECK(std::fabs(length_of(r.shapes[0], v) - 10.0) < 1e-6);

    const JoinResult r2 = join({seg(v, 4, 0, 10, 0), seg(v, 4, 0, 0, 0)}, v);
    CHECK(r2.joined == 1);
    CHECK(std::fabs(length_of(r2.shapes[0], v) - 10.0) < 1e-6);
}

TEST(modify_join_chains_more_than_two) {
    const Volume v = vol();
    const JoinResult r = join({seg(v, 0, 0, 2, 0), seg(v, 2, 0, 5, 0),
                               seg(v, 5, 0, 9, 0)}, v);
    CHECK_MSG(r.shapes.size() == 1, "%zu shapes left", r.shapes.size());
    CHECK(std::fabs(length_of(r.shapes[0], v) - 9.0) < 1e-6);
}

TEST(modify_join_never_loses_geometry) {
    const Volume v = vol();
    const std::vector<Shape> in = {seg(v, 0, 0, 4, 0), seg(v, 4, 0, 10, 0),
                                   seg(v, 50, 50, 60, 60), circle(v, 0, 0, 3)};
    const JoinResult r = join(in, v);
    CHECK_MSG(r.shapes.size() == 3, "%zu shapes", r.shapes.size());
}

TEST(modify_join_refuses_non_collinear) {
    const Volume v = vol();
    const JoinResult r = join({seg(v, 0, 0, 4, 0), seg(v, 4, 0, 4, 6)}, v);
    CHECK(r.joined == 0);
    CHECK(r.shapes.size() == 2);
    CHECK(std::string(r.reason).size() > 0);
}

TEST(modify_join_merges_contiguous_arcs) {
    const Volume v = vol();
    const JoinResult r = join({arc(v, 0, 0, 2, 0, kPi * 0.5),
                               arc(v, 0, 0, 2, kPi * 0.5, kPi)}, v);
    CHECK_MSG(r.joined == 1, "%zu joined", r.joined);
    CHECK(r.shapes.size() == 1);
    CHECK_MSG(std::fabs(length_of(r.shapes[0], v) - kPi * 2.0) < 1e-3, "%.6f",
              length_of(r.shapes[0], v));
}

TEST(modify_join_refuses_arcs_of_different_radius) {
    const Volume v = vol();
    const JoinResult r = join({arc(v, 0, 0, 2, 0, kPi * 0.5),
                               arc(v, 0, 0, 3, kPi * 0.5, kPi)}, v);
    CHECK(r.joined == 0);
    CHECK(r.shapes.size() == 2);
}

TEST(modify_align_translates_and_rotates) {
    const Volume v = vol();
    const std::vector<Shape> in = {seg(v, 0, 0, 4, 0)};

    const auto out = align(in, pt(v, 0, 0), pt(v, 4, 0), pt(v, 10, 10),
                           pt(v, 10, 14), false, v);
    CHECK(out.size() == 1);
    CHECK_MSG(std::fabs(wx(v, out[0].a) - 10.0) < 1e-4 &&
                  std::fabs(wy(v, out[0].a) - 10.0) < 1e-4,
              "the first point landed at (%.4f, %.4f)", wx(v, out[0].a),
              wy(v, out[0].a));
    CHECK_MSG(std::fabs(wx(v, out[0].b) - 10.0) < 1e-4 &&
                  std::fabs(wy(v, out[0].b) - 14.0) < 1e-4,
              "the second point landed at (%.4f, %.4f)", wx(v, out[0].b),
              wy(v, out[0].b));
}

TEST(modify_align_can_scale_to_fit) {
    const Volume v = vol();
    const std::vector<Shape> in = {seg(v, 0, 0, 4, 0)};

    const auto scaled = align(in, pt(v, 0, 0), pt(v, 4, 0), pt(v, 0, 0),
                              pt(v, 8, 0), true, v);
    CHECK_MSG(std::fabs(length_of(scaled[0], v) - 8.0) < 1e-4, "%.6f",
              length_of(scaled[0], v));

    const auto unscaled = align(in, pt(v, 0, 0), pt(v, 4, 0), pt(v, 0, 0),
                                pt(v, 8, 0), false, v);
    CHECK(std::fabs(length_of(unscaled[0], v) - 4.0) < 1e-4);
}

TEST(modify_align_with_degenerate_input_returns_the_input) {
    const Volume v = vol();
    const std::vector<Shape> in = {seg(v, 0, 0, 4, 0)};
    const auto out = align(in, pt(v, 0, 0), pt(v, 0, 0), pt(v, 5, 5),
                           pt(v, 9, 9), false, v);
    CHECK(out.size() == 1);
    CHECK(out[0].a.x == in[0].a.x && out[0].b.y == in[0].b.y);
}
