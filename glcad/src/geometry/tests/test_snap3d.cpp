#include "test.hpp"

#include <cmath>

#include "geometry/snap3d.hpp"
#include "geometry/extrude.hpp"

using namespace gsnap;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

gtopo::Body box(const gcm::WorkingVolume& v, double x0, double y0, double x1,
                double y1, double h) {
    gtopo::Profile p;
    p.outer = {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
               gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
    p.normalize();
    return gtopo::extrude_z(p, v, h);
}

void w3(const gcm::WorkingVolume& v, Point3 p, double& x, double& y, double& z) {
    gcm::to_world(v, p, x, y, z);
}

}

TEST(snap3d_finds_a_vertex) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 2, 3, 4);
    Engine3 e;
    e.settings.aperture_world = 0.3;
    e.add(&b);

    const Result3 r = e.snap_point(v, 2.05, 3.05, 4.02);
    CHECK_MSG(r.valid, "nothing snapped near a corner");
    CHECK(r.kind == Endpoint);
    CHECK(r.vertex != gtopo::kInvalid);

    double x, y, z;
    w3(v, r.point, x, y, z);
    CHECK_MSG(std::fabs(x - 2.0) < 1e-6 && std::fabs(y - 3.0) < 1e-6 &&
                  std::fabs(z - 4.0) < 1e-6,
              "snapped to (%.4f, %.4f, %.4f)", x, y, z);
}

TEST(snap3d_vertex_beats_edge_and_face) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 2, 2, 2);
    Engine3 e;
    e.settings.aperture_world = 5.0;
    e.add(&b);

    const Result3 r = e.snap_point(v, 0.01, 0.01, 0.01);
    CHECK(r.valid);
    CHECK_MSG(r.kind == Endpoint, "picked %s over the corner", r.name());
}

TEST(snap3d_finds_an_edge_midpoint) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 4, 4, 4);
    Engine3 e;
    e.settings.aperture_world = 0.3;
    e.settings.running = Midpoint;
    e.add(&b);

    const Result3 r = e.snap_point(v, 2.0, 0.02, 0.02);
    CHECK_MSG(r.valid, "no midpoint found");
    CHECK(r.kind == Midpoint);
    CHECK(r.edge != gtopo::kInvalid);
}

TEST(snap3d_finds_a_face_centre) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 4, 6, 2);
    Engine3 e;
    e.settings.aperture_world = 0.5;
    e.settings.running = Center;
    e.add(&b);

    const Result3 r = e.snap_point(v, 2.0, 3.0, 2.05);
    CHECK_MSG(r.valid, "no face centre found");
    CHECK(r.kind == Center);
    CHECK(r.face != gtopo::kInvalid);

    double x, y, z;
    w3(v, r.point, x, y, z);
    CHECK(std::fabs(x - 2.0) < 1e-5 && std::fabs(y - 3.0) < 1e-5);
    CHECK_MSG(std::fabs(z - 2.0) < 1e-5, "z = %.6f", z);
}

TEST(snap3d_respects_the_running_mask) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 2, 2, 2);
    Engine3 e;
    e.settings.aperture_world = 0.3;
    e.settings.running = Center;
    e.add(&b);
    const Result3 r = e.snap_point(v, 0.01, 0.01, 0.01);
    CHECK_MSG(!r.valid || r.kind != Endpoint, "a masked-out snap still fired");
}

TEST(snap3d_finds_nothing_far_away) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 2, 2, 2);
    Engine3 e;
    e.settings.aperture_world = 0.1;
    e.add(&b);
    CHECK(!e.snap_point(v, 50, 50, 50).valid);
}

TEST(snap3d_ray_reaches_out_to_what_it_passes_near) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 2, 2, 2);
    Engine3 e;
    e.settings.aperture_world = 0.3;
    e.add(&b);

    Ray3 ray;
    ray.ox = 2.05;
    ray.oy = 2.05;
    ray.oz = 10.0;
    ray.dx = 0.0;
    ray.dy = 0.0;
    ray.dz = -1.0;

    const Result3 r = e.snap(v, ray);
    CHECK_MSG(r.valid, "the ray found nothing");
    CHECK(r.kind == Endpoint);
}

TEST(snap3d_ignores_what_is_behind_the_eye) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 2, 2, 2);
    Engine3 e;
    e.settings.aperture_world = 0.5;
    e.add(&b);

    Ray3 ray;
    ray.ox = 1.0;
    ray.oy = 1.0;
    ray.oz = -5.0;
    ray.dz = -1.0;
    CHECK(!e.snap(v, ray).valid);
}

TEST(snap3d_face_normal_axis_is_perpendicular_to_the_face) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 2, 2, 4);
    Engine3 e;
    e.settings.aperture_world = 0.5;
    e.settings.running = Center;
    const gcm::u32 id = e.add(&b);

    const Result3 hit = e.snap_point(v, 1.0, 1.0, 4.02);
    CHECK(hit.valid && hit.face != gtopo::kInvalid);

    const Engine3::Axis a = e.face_normal_axis(v, id, hit.face);
    CHECK_MSG(a.valid, "no axis from the face");
    CHECK_MSG(std::fabs(a.dz - 1.0) < 1e-6,
              "the top face's normal is (%.4f, %.4f, %.4f)", a.dx, a.dy, a.dz);
}

TEST(snap3d_edge_axis_runs_along_the_edge) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 4, 2, 2);
    Engine3 e;
    const gcm::u32 id = e.add(&b);

    bool found = false;
    for (gcm::u32 h = 0; h < b.halfedge_count() && !found; ++h) {
        const Engine3::Axis a = e.edge_axis(v, id, h);
        if (!a.valid) continue;
        const double len = std::sqrt(a.dx * a.dx + a.dy * a.dy + a.dz * a.dz);
        CHECK_MSG(std::fabs(len - 1.0) < 1e-9, "axis length %.9f", len);
        found = true;
    }
    CHECK(found);
}

TEST(snap3d_locking_to_an_axis_projects_onto_it) {
    Engine3::Axis a;
    a.valid = true;
    a.ox = 1.0; a.oy = 2.0; a.oz = 3.0;
    a.dx = 0.0; a.dy = 0.0; a.dz = 1.0;

    double x, y, z;
    CHECK(Engine3::lock_to_axis(a, 9.0, -4.0, 8.0, x, y, z));
    CHECK_MSG(std::fabs(x - 1.0) < 1e-12 && std::fabs(y - 2.0) < 1e-12,
              "the lock did not hold the axis (%.6f, %.6f)", x, y);
    CHECK(std::fabs(z - 8.0) < 1e-12);

    Engine3::Axis bad;
    CHECK(!Engine3::lock_to_axis(bad, 0, 0, 0, x, y, z));
}

TEST(snap3d_axis_of_a_bad_index_is_invalid) {
    const auto v = vol();
    Engine3 e;
    CHECK(!e.face_normal_axis(v, 99, 0).valid);
    CHECK(!e.edge_axis(v, 99, 0).valid);
}

TEST(snap_every_kind_has_its_own_glyph_shape) {
    const Kind kinds[] = {Endpoint, Midpoint, Center, GeometricCenter, Quadrant,
                          Intersection, ApparentIntersection, Perpendicular,
                          Tangent, Nearest, Parallel, Insertion, Node};
    std::vector<GlyphShape> seen;
    for (Kind k : kinds) {
        const GlyphShape g = glyph_for(k);
        for (GlyphShape prev : seen)
            CHECK_MSG(prev != g, "%s reuses the %s glyph", kind_name(k),
                      glyph_name(g));
        seen.push_back(g);
    }
    CHECK(seen.size() == 13);
}

TEST(snap_glyph_outlines_are_drawable) {
    const GlyphShape all[] = {
        GlyphShape::Square, GlyphShape::Triangle, GlyphShape::Circle,
        GlyphShape::CircleDot, GlyphShape::Diamond, GlyphShape::Cross,
        GlyphShape::DoubleCross, GlyphShape::Perpendicular,
        GlyphShape::TangentCircle, GlyphShape::Hourglass, GlyphShape::Parallel,
        GlyphShape::Ellipse, GlyphShape::Node, GlyphShape::Dashes};

    for (GlyphShape g : all) {
        const auto out = glyph_outline(g);
        CHECK_MSG(!out.empty(), "%s draws nothing", glyph_name(g));
        CHECK_MSG(out.size() % 4 == 0, "%s produced a half segment",
                  glyph_name(g));
        for (double c : out)
            CHECK_MSG(std::fabs(c) <= 0.5 + 1e-9, "%s strays to %.4f",
                      glyph_name(g), c);
    }
}

TEST(snap_unknown_kinds_fall_back_to_a_neutral_glyph) {
    CHECK(glyph_for(Grid) == GlyphShape::Dashes);
    CHECK(glyph_for(Extension) == GlyphShape::Dashes);
    CHECK(std::string(glyph_name(GlyphShape::Dashes)) == "dashes");
}

TEST(snap_parallel_locks_to_an_acquired_direction) {
    const auto v = vol();
    Engine e;
    e.settings.running = Parallel;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 400.0;

    Shape ref;
    ref.kind = ShapeKind::Segment;
    ref.a = gcm::to_lattice(v, 0, 0);
    ref.b = gcm::to_lattice(v, 3, 4);
    const gcm::u32 id = e.add(ref);
    e.acquire_parallel(id);
    CHECK(e.parallel_references().size() == 1);

    const Point anchor = gcm::to_lattice(v, 10, 10);
    const Result r = e.snap_parallel(v, 13.2, 13.9, anchor);
    CHECK_MSG(r.valid, "the parallel lock did not fire");
    CHECK(r.kind == Parallel);

    double x, y;
    gcm::to_world(v, r.point, x, y);
    const double cross = (x - 10.0) * 4.0 - (y - 10.0) * 3.0;
    CHECK_MSG(std::fabs(cross) < 1e-4, "off the parallel ray by %.9f", cross);
}

TEST(snap_parallel_does_nothing_without_a_reference) {
    const auto v = vol();
    Engine e;
    e.settings.running = Parallel;
    CHECK(!e.snap_parallel(v, 1, 1, gcm::to_lattice(v, 0, 0)).valid);
}

TEST(snap_parallel_ignores_curves_and_duplicates) {
    const auto v = vol();
    Engine e;
    Shape c;
    c.kind = ShapeKind::Circle;
    c.a = gcm::to_lattice(v, 0, 0);
    c.radius = 1000;
    e.acquire_parallel(e.add(c));
    CHECK_MSG(e.parallel_references().empty(), "a circle was acquired");

    Shape s;
    s.kind = ShapeKind::Segment;
    s.a = gcm::to_lattice(v, 0, 0);
    s.b = gcm::to_lattice(v, 1, 0);
    const gcm::u32 id = e.add(s);
    e.acquire_parallel(id);
    e.acquire_parallel(id);
    CHECK_MSG(e.parallel_references().size() == 1, "the same line twice");

    e.release_parallel();
    CHECK(e.parallel_references().empty());
}

TEST(snap_parallel_respects_the_running_mask) {
    const auto v = vol();
    Engine e;
    e.settings.running = Endpoint;
    Shape s;
    s.kind = ShapeKind::Segment;
    s.a = gcm::to_lattice(v, 0, 0);
    s.b = gcm::to_lattice(v, 1, 0);
    e.acquire_parallel(e.add(s));
    CHECK(!e.snap_parallel(v, 5, 0.01, gcm::to_lattice(v, 0, 5)).valid);
}

TEST(snap_ellipse_quadrants_are_the_axis_ends_not_the_compass) {
    const auto v = vol();
    Engine e;
    e.settings.running = Quadrant;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 20.0;

    Shape el;
    el.kind = ShapeKind::Ellipse;
    el.a = gcm::to_lattice(v, 0, 0);
    el.b = gcm::to_lattice(v, 3, 4);
    el.radius = static_cast<gcm::i64>(std::llround(2.0 * v.scale()));
    e.add(el);

    const Result r = e.snap(v, 3.0, 4.0);
    CHECK_MSG(r.valid, "no quadrant at the major axis end");
    CHECK(r.kind == Quadrant);

    const Result m = e.snap(v, -1.6, 1.2);
    CHECK_MSG(m.valid, "no quadrant at the minor axis end");
}

TEST(snap_ellipse_nearest_lands_on_the_curve) {
    const auto v = vol();
    Engine e;
    e.settings.running = Nearest;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 200.0;

    Shape el;
    el.kind = ShapeKind::Ellipse;
    el.a = gcm::to_lattice(v, 0, 0);
    el.b = gcm::to_lattice(v, 4, 0);
    el.radius = static_cast<gcm::i64>(std::llround(2.0 * v.scale()));
    e.add(el);

    const Result r = e.snap(v, 3.0, 3.0);
    CHECK(r.valid);
    double x, y;
    gcm::to_world(v, r.point, x, y);
    const double f = (x / 4.0) * (x / 4.0) + (y / 2.0) * (y / 2.0);
    CHECK_MSG(std::fabs(f - 1.0) < 1e-3, "the nearest point is off by %.6f", f - 1.0);
}

TEST(snap_ellipse_centre) {
    const auto v = vol();
    Engine e;
    e.settings.running = Center;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 20.0;

    Shape el;
    el.kind = ShapeKind::Ellipse;
    el.a = gcm::to_lattice(v, 5, 5);
    el.b = gcm::to_lattice(v, 9, 5);
    el.radius = static_cast<gcm::i64>(std::llround(2.0 * v.scale()));
    e.add(el);

    const Result r = e.snap(v, 5.05, 5.05);
    CHECK(r.valid);
    CHECK(r.kind == Center);
}

TEST(snap_a_ray_has_one_endpoint_and_an_xline_has_none) {
    const auto v = vol();
    Engine e;
    e.settings.running = Endpoint;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 20.0;

    Shape ray;
    ray.kind = ShapeKind::Ray;
    ray.a = gcm::to_lattice(v, 0, 0);
    ray.b = gcm::to_lattice(v, 1, 0);
    e.add(ray);

    CHECK_MSG(e.snap(v, 0.05, 0.0).valid, "the ray's start is not snappable");
    CHECK_MSG(!e.snap(v, 1.0, 0.0).valid,
              "the ray offered an endpoint where it merely passes through");

    Engine x;
    x.settings.running = Endpoint;
    x.settings.scale = 40.0;
    x.settings.aperture_px = 20.0;
    Shape xl;
    xl.kind = ShapeKind::Xline;
    xl.a = gcm::to_lattice(v, 0, 5);
    xl.b = gcm::to_lattice(v, 1, 5);
    x.add(xl);
    CHECK_MSG(!x.snap(v, 0.0, 5.0).valid, "an xline offered an endpoint");
}

TEST(snap_nearest_on_a_ray_does_not_run_behind_its_start) {
    const auto v = vol();
    Engine e;
    e.settings.running = Nearest;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 400.0;

    Shape ray;
    ray.kind = ShapeKind::Ray;
    ray.a = gcm::to_lattice(v, 0, 0);
    ray.b = gcm::to_lattice(v, 1, 0);
    e.add(ray);

    const Result r = e.snap(v, -5.0, 0.2);
    CHECK(r.valid);
    double x, y;
    gcm::to_world(v, r.point, x, y);
    CHECK_MSG(x >= -1e-6, "the ray extended backward to x = %.6f", x);
}

TEST(snap_nearest_on_an_xline_runs_both_ways) {
    const auto v = vol();
    Engine e;
    e.settings.running = Nearest;
    e.settings.scale = 40.0;
    e.settings.aperture_px = 400.0;

    Shape xl;
    xl.kind = ShapeKind::Xline;
    xl.a = gcm::to_lattice(v, 0, 0);
    xl.b = gcm::to_lattice(v, 1, 0);
    e.add(xl);

    const Result r = e.snap(v, -5.0, 0.2);
    CHECK(r.valid);
    double x, y;
    gcm::to_world(v, r.point, x, y);
    CHECK_MSG(std::fabs(x + 5.0) < 1e-4, "the xline stopped at x = %.6f", x);
}

TEST(snap_shape_kind_names_are_all_defined) {
    for (ShapeKind k : {ShapeKind::Segment, ShapeKind::Circle, ShapeKind::Arc,
                        ShapeKind::Point, ShapeKind::Block, ShapeKind::Ellipse,
                        ShapeKind::Ray, ShapeKind::Xline})
        CHECK(std::string(shape_kind_name(k)).size() > 0);
}

TEST(snap_shape_classifiers_agree_with_the_kinds) {
    Shape s;
    s.kind = ShapeKind::Ray;
    CHECK(s.is_infinite() && s.is_linear() && !s.is_curve());
    s.kind = ShapeKind::Segment;
    CHECK(!s.is_infinite() && s.is_linear());
    s.kind = ShapeKind::Ellipse;
    CHECK(s.is_elliptical() && !s.is_curve() && !s.is_linear());
    s.kind = ShapeKind::Circle;
    CHECK(s.is_curve() && !s.is_elliptical());
}

TEST(elliptical_arc_snaps_exclude_the_missing_part_of_the_ellipse) {
    const auto v=vol();Engine e;Shape arc;arc.kind=ShapeKind::Ellipse;
    arc.a=gcm::to_lattice(v,0,0);arc.b=gcm::to_lattice(v,4,0);arc.radius=std::llround(2*v.scale());
    arc.start_angle=0;arc.end_angle=std::acos(-1.)/2;e.add(arc);
    e.settings.running=Quadrant|Nearest|Endpoint;e.settings.scale=100;e.settings.aperture_px=10;
    CHECK(!e.snap(v,-4,0).valid);CHECK(!e.snap(v,0,-2).valid);
    CHECK(e.snap(v,4,0).valid);CHECK(e.snap(v,0,2).valid);
}
