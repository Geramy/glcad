#include "test.hpp"

#include <cmath>

#include "geometry/snap.hpp"

using namespace gsnap;

static gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
static Point pt(const gcm::WorkingVolume& v, double x, double y) {
    return gcm::to_lattice(v, x, y);
}
static void world(const gcm::WorkingVolume& v, Point p, double& x, double& y) {
    gcm::to_world(v, p, x, y);
}
static bool at(const gcm::WorkingVolume& v, Point p, double x, double y,
               double tol = 1e-3) {
    double px, py;
    world(v, p, px, py);
    return std::fabs(px - x) < tol && std::fabs(py - y) < tol;
}

static Engine engine_with_all() {
    Engine e;
    e.settings.running = Endpoint | Midpoint | Center | Node | Quadrant |
                         Intersection | Insertion | Perpendicular | Tangent |
                         Nearest | ApparentIntersection | Extension;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 12.0;
    return e;
}

static Shape seg(const gcm::WorkingVolume& v, double x0, double y0,
                 double x1, double y1) {
    Shape s;
    s.kind = ShapeKind::Segment;
    s.a = pt(v, x0, y0);
    s.b = pt(v, x1, y1);
    return s;
}

TEST(snap_kind_names_match_autocad) {
    CHECK(std::string(kind_name(Endpoint)) == "endpoint");
    CHECK(std::string(kind_name(Midpoint)) == "midpoint");
    CHECK(std::string(kind_name(Perpendicular)) == "perpendicular");
    CHECK(std::string(kind_name(Tangent)) == "tangent");
    CHECK(std::string(kind_name(Quadrant)) == "quadrant");
}

TEST(snap_osmode_bits_match_autocad) {
    CHECK(Endpoint == 1);
    CHECK(Midpoint == 2);
    CHECK(Center == 4);
    CHECK(Node == 8);
    CHECK(Quadrant == 16);
    CHECK(Intersection == 32);
    CHECK(Insertion == 64);
    CHECK(Perpendicular == 128);
    CHECK(Tangent == 256);
    CHECK(Nearest == 512);
}

TEST(snap_priority_order_is_the_documented_one) {
    CHECK(priority(Endpoint) < priority(Center));
    CHECK(priority(Center) < priority(Intersection));
    CHECK(priority(Intersection) < priority(Midpoint));
    CHECK(priority(Midpoint) < priority(Perpendicular));
    CHECK(priority(Perpendicular) < priority(Nearest));
    CHECK(priority(Nearest) < priority(Grid));
}

TEST(snap_endpoint_and_midpoint) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    auto r = e.snap(v, 0.05, 0.02);
    CHECK(r.valid);
    CHECK(r.kind == Endpoint);
    CHECK(at(v, r.point, 0, 0));

    r = e.snap(v, 9.98, 0.01);
    CHECK(r.kind == Endpoint);
    CHECK(at(v, r.point, 10, 0));

    r = e.snap(v, 5.02, 0.05);
    CHECK(r.kind == Midpoint);
    CHECK(at(v, r.point, 5, 0));
}

TEST(snap_nearest_falls_back_along_the_segment) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    const auto r = e.snap(v, 3.7, 0.05);
    CHECK(r.valid);
    CHECK_MSG(r.kind == Nearest, "expected nearest, got %s", r.name());
    CHECK(at(v, r.point, 3.7, 0.0, 0.01));
}

TEST(snap_endpoint_beats_nearest_even_when_further) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    const auto r = e.snap(v, 0.12, 0.02);
    CHECK_MSG(r.kind == Endpoint, "expected endpoint, got %s", r.name());
}

TEST(snap_intersection_of_two_segments) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 10));
    e.add(seg(v, 0, 10, 10, 0));

    const auto r = e.snap(v, 5.05, 4.96);
    CHECK(r.valid);
    CHECK_MSG(r.kind == Intersection, "expected intersection, got %s", r.name());
    CHECK(at(v, r.point, 5, 5, 0.01));
    CHECK(r.shape_a != 0 && r.shape_b != 0);
}

TEST(snap_apparent_intersection_needs_extensions) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 2, 2));
    e.add(seg(v, 0, 10, 2, 8));

    e.settings.running = Intersection;
    CHECK(!e.snap(v, 5.0, 5.0).valid);

    e.settings.running = ApparentIntersection;
    const auto r = e.snap(v, 5.02, 4.98);
    CHECK_MSG(r.valid && r.kind == ApparentIntersection,
              "apparent intersection not found");
    CHECK(at(v, r.point, 5, 5, 0.01));
}

TEST(snap_perpendicular_needs_an_anchor) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    e.settings.running = Perpendicular;
    const auto r = e.snap_from(v, 3.02, 0.05, pt(v, 3, 5));
    CHECK_MSG(r.valid && r.kind == Perpendicular, "perpendicular not found");
    CHECK(at(v, r.point, 3, 0, 0.01));

    CHECK(!e.snap(v, 3.02, 0.05).valid);
}

TEST(snap_extension_only_beyond_the_ends) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.settings.running = Extension;
    e.add(seg(v, 0, 0, 10, 0));

    auto r = e.snap(v, 13.0, 0.02);
    CHECK_MSG(r.valid && r.kind == Extension, "extension past the end not found");
    CHECK(at(v, r.point, 13, 0, 0.01));

    CHECK(!e.snap(v, 5.0, 0.02).valid);
}

static Shape circle(const gcm::WorkingVolume& v, double cx, double cy, double r) {
    Shape s;
    s.kind = ShapeKind::Circle;
    s.a = pt(v, cx, cy);
    s.radius = static_cast<i64>(std::llround(r * v.scale()));
    return s;
}

TEST(snap_circle_centre_and_quadrants) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(circle(v, 5, 5, 3));

    auto r = e.snap(v, 5.05, 4.98);
    CHECK_MSG(r.kind == Center, "expected center, got %s", r.name());
    CHECK(at(v, r.point, 5, 5, 0.01));

    struct { double x, y; } q[] = {{8, 5}, {5, 8}, {2, 5}, {5, 2}};
    for (const auto& p : q) {
        r = e.snap(v, p.x + 0.03, p.y + 0.02);
        CHECK_MSG(r.valid && r.kind == Quadrant,
                  "quadrant at (%.0f,%.0f) gave %s", p.x, p.y, r.name());
        CHECK(at(v, r.point, p.x, p.y, 0.01));
    }
}

TEST(snap_nearest_on_a_circle) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.settings.running = Nearest;
    e.add(circle(v, 0, 0, 5));

    const double k = 5.0 / std::sqrt(2.0);
    const auto r = e.snap(v, k + 0.05, k + 0.05);
    CHECK(r.valid && r.kind == Nearest);
    CHECK(at(v, r.point, k, k, 0.02));
}

TEST(snap_tangent_from_an_external_point) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.settings.running = Tangent;
    e.add(circle(v, 0, 0, 3));

    const auto r = e.snap_from(v, 1.82, 2.38, pt(v, 5, 0));
    CHECK_MSG(r.valid && r.kind == Tangent, "tangent not found");
    CHECK_MSG(at(v, r.point, 1.8, 2.4, 0.02), "tangent landed elsewhere");
}

TEST(snap_line_circle_intersection) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.settings.running = Intersection;
    e.add(seg(v, -10, 0, 10, 0));
    e.add(circle(v, 0, 0, 4));

    const auto r = e.snap(v, 4.03, 0.02);
    CHECK_MSG(r.valid && r.kind == Intersection, "line-circle intersection missed");
    CHECK(at(v, r.point, 4, 0, 0.02));
}

TEST(snap_arc_limits_quadrants_to_its_sweep) {
    const auto v = vol();
    Engine e = engine_with_all();
    Shape a = circle(v, 0, 0, 5);
    a.kind = ShapeKind::Arc;
    a.start_angle = 0.0;
    a.end_angle = 3.14159265358979323846 * 0.5;
    e.add(a);

    auto r = e.snap(v, 5.02, 0.02);
    CHECK(r.valid);

    e.settings.running = Quadrant;
    CHECK_MSG(!e.snap(v, -5.0, 0.0).valid,
              "a quadrant outside the arc's sweep must not snap");
}

TEST(snap_arc_endpoints_and_midpoint) {
    const auto v = vol();
    Engine e = engine_with_all();
    Shape a = circle(v, 0, 0, 10);
    a.kind = ShapeKind::Arc;
    a.start_angle = 0.0;
    a.end_angle = 3.14159265358979323846;
    e.add(a);

    auto r = e.snap(v, 10.02, 0.02);
    CHECK_MSG(r.kind == Endpoint, "arc start gave %s", r.name());

    e.settings.running = Midpoint;
    r = e.snap(v, 0.02, 10.02);
    CHECK_MSG(r.valid && r.kind == Midpoint, "arc midpoint gave %s", r.name());
}

TEST(snap_node_and_insertion) {
    const auto v = vol();
    Engine e = engine_with_all();
    Shape p; p.kind = ShapeKind::Point; p.a = pt(v, 3, 3);
    Shape b; b.kind = ShapeKind::Block; b.a = pt(v, 7, 7);
    e.add(p);
    e.add(b);

    CHECK(e.snap(v, 3.02, 3.02).kind == Node);
    CHECK(e.snap(v, 7.02, 7.02).kind == Insertion);
}

TEST(snap_running_mask_gates_each_kind) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    e.settings.running = Endpoint;
    CHECK(e.snap(v, 0.02, 0.02).kind == Endpoint);
    CHECK_MSG(!e.snap(v, 5.0, 0.0).valid, "midpoint fired while masked off");

    e.settings.running = Midpoint;
    CHECK(e.snap(v, 5.02, 0.02).kind == Midpoint);
    CHECK(!e.snap(v, 0.0, 0.0).valid);
}

TEST(snap_forced_override_beats_the_running_set) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.settings.running = Endpoint;
    e.add(seg(v, 0, 0, 10, 0));

    e.settings.forced = Midpoint;
    const auto r = e.snap(v, 5.02, 0.02);
    CHECK_MSG(r.valid && r.kind == Midpoint, "forced override did not apply");
    CHECK_MSG(!e.snap(v, 0.02, 0.02).valid,
              "a forced override must exclude everything else");
}

TEST(snap_disabled_engine_returns_nothing) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));
    e.settings.enabled = false;
    CHECK(!e.snap(v, 0.0, 0.0).valid);
}

TEST(snap_aperture_scales_with_zoom) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    e.settings.scale = 400.0;
    CHECK(!e.snap(v, 0.5, 0.5).valid);

    e.settings.scale = 4.0;
    CHECK_MSG(e.snap(v, 0.5, 0.5).valid, "aperture did not widen when zoomed out");
}

TEST(snap_tracking_acquires_and_aligns) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    const auto ep = e.snap(v, 10.0, 0.0);
    CHECK(ep.valid);
    e.acquire(ep, 0.0);
    CHECK(e.tracked().size() == 1);

    const auto t = e.track(v, 10.05, 7.0);
    CHECK_MSG(t.valid, "tracking produced no alignment");
    double x, y;
    world(v, t.point, x, y);
    CHECK_MSG(std::fabs(x - 10.0) < 0.02, "aligned to x = %.4f, expected 10", x);
    CHECK(std::fabs(y - 7.0) < 0.05);
}

TEST(snap_tracking_intersects_two_guides) {
    const auto v = vol();
    Engine e = engine_with_all();
    Shape p1; p1.kind = ShapeKind::Point; p1.a = pt(v, 10, 0);
    Shape p2; p2.kind = ShapeKind::Point; p2.a = pt(v, 0, 6);
    e.add(p1);
    e.add(p2);

    e.acquire(e.snap(v, 10.0, 0.0), 0.0);
    e.acquire(e.snap(v, 0.0, 6.0), 1.0);
    CHECK(e.tracked().size() == 2);

    const auto t = e.track(v, 10.05, 6.05);
    CHECK(t.valid);
    CHECK_MSG(at(v, t.point, 10, 6, 0.05), "guide intersection missed");
}

TEST(snap_tracking_does_not_duplicate_points) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));
    const auto ep = e.snap(v, 0.0, 0.0);
    e.acquire(ep, 0.0);
    e.acquire(ep, 1.0);
    e.acquire(ep, 2.0);
    CHECK(e.tracked().size() == 1);
}

TEST(snap_tracking_is_bounded) {
    const auto v = vol();
    Engine e = engine_with_all();
    for (int i = 0; i < 20; ++i) {
        Shape p; p.kind = ShapeKind::Point; p.a = pt(v, i, i);
        e.add(p);
        e.acquire(e.snap(v, i, i), i);
    }
    CHECK_MSG(e.tracked().size() <= 7, "tracked %zu points", e.tracked().size());
}

TEST(snap_guides_radiate_from_every_acquired_point) {
    const auto v = vol();
    Engine e = engine_with_all();
    Shape p; p.kind = ShapeKind::Point; p.a = pt(v, 1, 1);
    e.add(p);
    e.acquire(e.snap(v, 1, 1), 0.0);

    const auto g = e.guides();
    CHECK(g.size() == e.settings.track_angles.size());
    for (const auto& gd : g) CHECK(gd.from_tracking);

    e.release_all();
    CHECK(e.guides().empty());
}

static bool offers(const std::vector<Kind>& v, Kind k) {
    for (Kind x : v) if (x == k) return true;
    return false;
}

TEST(osnap_ring_offers_only_what_is_actually_there) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    const auto near_end = e.applicable(v, 0.02, 0.0);
    CHECK_MSG(offers(near_end, Endpoint), "no endpoint at an endpoint");
    CHECK_MSG(!offers(near_end, Center), "a segment offered a centre");
    CHECK_MSG(!offers(near_end, Quadrant), "a segment offered a quadrant");
    CHECK_MSG(!offers(near_end, Node), "a segment offered a node");
}

TEST(osnap_ring_offers_a_circles_own_snaps) {
    const auto v = vol();
    Engine e = engine_with_all();
    Shape c;
    c.kind = ShapeKind::Circle;
    c.a = pt(v, 0, 0);
    c.radius = static_cast<i64>(std::llround(5.0 * v.scale()));
    e.add(c);

    const auto at_quad = e.applicable(v, 5.0, 0.0);
    CHECK_MSG(offers(at_quad, Quadrant), "no quadrant at the quadrant");

    const auto at_centre = e.applicable(v, 0.0, 0.0);
    CHECK_MSG(offers(at_centre, Center), "no centre at the centre");
}

TEST(osnap_ring_ignores_the_running_settings) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.settings.running = Endpoint;
    e.add(seg(v, 0, 0, 10, 0));

    const auto mid = e.applicable(v, 5.0, 0.0);
    CHECK_MSG(offers(mid, Midpoint),
              "the ring hid the midpoint because it was switched off");
    const Result r = e.snap(v, 5.0, 0.0);
    CHECK(!r.valid || r.kind != Midpoint);
}

TEST(osnap_ring_is_ordered_by_priority) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));
    e.add(seg(v, 0, 0, 0, 10));

    const auto here = e.applicable(v, 0.0, 0.0);
    CHECK_MSG(!here.empty(), "a shared corner offered nothing");
    for (std::size_t i = 1; i < here.size(); ++i)
        CHECK_MSG(priority(here[i - 1]) <= priority(here[i]),
                  "%s came before %s", kind_name(here[i - 1]),
                  kind_name(here[i]));
}

TEST(osnap_ring_will_not_offer_perpendicular_without_an_anchor) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));

    CHECK(!offers(e.applicable(v, 5.0, 0.1), Perpendicular));
    const auto with_anchor = e.applicable(v, 5.0, 0.1, pt(v, 5.0, 4.0));
    CHECK_MSG(offers(with_anchor, Perpendicular),
              "an anchored ring still hid perpendicular");
}

TEST(osnap_ring_offers_nothing_over_empty_space) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));
    CHECK(e.applicable(v, 400.0, 400.0).empty());
}

TEST(osnap_ring_agrees_with_the_snap_it_summons) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, 0, 0, 10, 0));
    Shape c;
    c.kind = ShapeKind::Circle;
    c.a = pt(v, 5, 0);
    c.radius = static_cast<i64>(std::llround(2.0 * v.scale()));
    e.add(c);

    for (double x : {0.0, 3.0, 5.0, 7.0, 10.0}) {
        const auto offered = e.applicable(v, x, 0.0);
        for (Kind k : offered) {
            Engine probe = e;
            probe.settings.forced = static_cast<u32>(k);
            const Result r = probe.snap(v, x, 0.0);
            CHECK_MSG(r.valid && r.kind == k,
                      "at x=%.1f the ring offered %s and the engine gave %s",
                      x, kind_name(k), r.valid ? kind_name(r.kind) : "nothing");
        }
    }
}

TEST(osnap_ring_offers_an_intersection_where_two_lines_cross) {
    const auto v = vol();
    Engine e = engine_with_all();
    e.add(seg(v, -5, 0, 5, 0));
    e.add(seg(v, 0, -5, 0, 5));
    const auto here = e.applicable(v, 0.0, 0.0);
    CHECK_MSG(offers(here, Intersection), "no intersection where lines cross");
    CHECK(!offers(e.applicable(v, 4.0, 0.0), Intersection));
}

TEST(geometric_centre_is_the_centroid_of_a_closed_polyline) {
    const auto v = vol();
    Engine e;

    const double xs[7] = {0, 6, 6, 2, 2, 0, 0};
    const double ys[7] = {0, 0, 2, 2, 6, 6, 0};
    for (int i = 0; i < 6; ++i) {
        Shape s;
        s.kind = ShapeKind::Segment;
        s.a = pt(v, xs[i], ys[i]);
        s.b = pt(v, xs[i + 1], ys[i + 1]);
        s.poly = 1;
        s.poly_index = static_cast<gcm::u32>(i);
        e.add(s);
    }
    e.settings.enabled = true;
    e.settings.running = GeometricCenter;
    e.settings.scale = 80.0;

    const Result r = e.snap(v, 2.3, 2.3);
    CHECK_MSG(r.valid, "no geometric centre offered inside the polyline");
    CHECK(r.kind == GeometricCenter);
    CHECK_MSG(at(v, r.point, 2.2, 2.2, 1e-3), "the centroid is not at 2.2, 2.2");

    Engine open;
    for (int i = 0; i < 5; ++i) {
        Shape s;
        s.kind = ShapeKind::Segment;
        s.a = pt(v, xs[i], ys[i]);
        s.b = pt(v, xs[i + 1], ys[i + 1]);
        s.poly = 1;
        s.poly_index = static_cast<gcm::u32>(i);
        open.add(s);
    }
    open.settings.enabled = true;
    open.settings.running = GeometricCenter;
    open.settings.scale = 80.0;
    CHECK_MSG(!open.snap(v, 2.3, 2.3).valid,
              "an open polyline offered a geometric centre");
}

TEST(two_circles_crossing_offer_an_intersection) {
    const auto v = vol();
    Engine e;
    Shape a;
    a.kind = ShapeKind::Circle;
    a.a = pt(v, 0, 0);
    a.radius = gcm::i64(std::llround(5.0 * v.scale()));
    Shape b = a;
    b.a = pt(v, 8, 0);
    e.add(a);
    e.add(b);
    e.settings.enabled = true;
    e.settings.running = Intersection;
    e.settings.scale = 80.0;

    const Result r = e.snap(v, 4.0, 3.05);
    CHECK_MSG(r.valid, "no intersection between two crossing circles");
    CHECK(r.kind == Intersection);
    CHECK_MSG(at(v, r.point, 4.0, 3.0, 1e-6), "the crossing is not at 4, 3");

    Engine apart;
    Shape c = a;
    Shape d = a;
    d.a = pt(v, 40, 0);
    apart.add(c);
    apart.add(d);
    apart.settings.enabled = true;
    apart.settings.running = Intersection;
    apart.settings.scale = 80.0;
    CHECK_MSG(!apart.snap(v, 20.0, 0.0).valid,
              "two circles that miss offered an intersection");

    Engine same;
    same.add(a);
    same.add(a);
    same.settings.enabled = true;
    same.settings.running = Intersection;
    same.settings.scale = 80.0;
    CHECK_MSG(!same.snap(v, 5.0, 0.0).valid,
              "a circle intersected with itself offered a crossing");
}
