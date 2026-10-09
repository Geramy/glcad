#include "test.hpp"

#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/extrude.hpp"

using namespace gtopo;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

Loop2 rect(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    return {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
            gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
}

Body box(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1, f64 h,
         f64 z0 = 0.0) {
    Profile p;
    p.outer = rect(v, x0, y0, x1, y1);
    p.normalize();
    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
    return extrude(p, v, frame, h);
}

Body cylinder(const gcm::WorkingVolume& v, f64 cx, f64 cy, f64 r, f64 h,
              int sides, f64 z0 = 0.0) {
    Loop2 loop;
    for (int i = 0; i < sides; ++i) {
        const f64 a = 2.0 * kPi * static_cast<f64>(i) / sides;
        loop.push_back(gcm::to_lattice(v, cx + r * std::cos(a),
                                       cy + r * std::sin(a)));
    }
    Profile p;
    p.outer = loop;
    p.normalize();
    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
    Body b = extrude(p, v, frame, h);

    b.face_surface.assign(b.face_count(), Surface{});
    for (u32 f = 0; f < b.face_count(); ++f) {
        const Plane pl = face_plane(b, f, v);
        if (!pl.valid()) continue;
        if (std::fabs(pl.nz) < 1e-6)
            b.face_surface[f] = Surface::cylinder(cx, cy, 0, 0, 0, 1, r);
    }
    return b;
}

f64 chord_error(f64 r, int sides) {
    return r * (1.0 - std::cos(kPi / static_cast<f64>(sides)));
}

}

TEST(curved_boolean_keeps_the_cylinder_on_the_hole) {
    const auto v = vol();
    const Body block = box(v, -5, -5, 5, 5, 4);
    const Body pin = cylinder(v, 0, 0, 2.0, 6.0, 32, -1.0);
    CHECK(has_curved_faces(pin));
    CHECK(!has_curved_faces(block));

    CurvedBoolSettings s;
    s.chord_tolerance = chord_error(2.0, 32);
    const CurvedBoolResult r = boolean_curved(block, pin, BoolOp::Subtract, v, s);
    CHECK_MSG(r.ok(), "%s", r.message());
    CHECK_MSG(r.faceted == 32, "%zu curved input faces seen", r.faceted);
    CHECK_MSG(r.preserved > 0, "the hole came out with no cylinder on it");
    CHECK(r.body.has_surfaces());

    std::size_t cylindrical = 0;
    for (u32 f = 0; f < r.body.face_count(); ++f) {
        const Surface* sf = r.body.surface_of(f);
        if (!sf || sf->kind != SurfaceKind::Cylinder) continue;
        ++cylindrical;
        CHECK_MSG(std::fabs(sf->radius - 2.0) < 1e-9,
                  "a hole face records radius %g", sf->radius);
    }
    CHECK_MSG(cylindrical >= 8, "only %zu faces kept the cylinder", cylindrical);
}

TEST(curved_boolean_leaves_the_flat_faces_flat) {
    const auto v = vol();
    const Body block = box(v, -5, -5, 5, 5, 4);
    const Body pin = cylinder(v, 0, 0, 2.0, 6.0, 24, -1.0);

    const CurvedBoolResult r = boolean_curved(block, pin, BoolOp::Subtract, v);
    CHECK(r.ok());

    for (u32 f = 0; f < r.body.face_count(); ++f) {
        const Surface* sf = r.body.surface_of(f);
        if (!sf || sf->kind != SurfaceKind::Cylinder) continue;
        const Plane p = face_plane(r.body, f, v);
        CHECK_MSG(std::fabs(p.nz) < 1e-6,
                  "a horizontal face was given a cylinder (nz=%g)", p.nz);
    }
}

TEST(curved_boolean_refines_the_seam_onto_the_real_surfaces) {
    const auto v = vol();
    const Body block = box(v, -5, -5, 5, 5, 4);
    const int sides = 16;
    const Body pin = cylinder(v, 0, 0, 2.0, 6.0, sides, -1.0);

    CurvedBoolSettings s;
    s.chord_tolerance = chord_error(2.0, sides);
    s.refine_seams = true;
    const CurvedBoolResult refined = boolean_curved(block, pin, BoolOp::Subtract,
                                                    v, s);
    CHECK_MSG(refined.ok(), "%s", refined.message());

    CurvedBoolSettings raw = s;
    raw.refine_seams = false;
    const CurvedBoolResult unrefined =
        boolean_curved(block, pin, BoolOp::Subtract, v, raw);
    CHECK(unrefined.ok());

    const Surface cyl = Surface::cylinder(0, 0, 0, 0, 0, 1, 2.0);
    auto worst = [&](const Body& b) {
        f64 d = 0.0;
        for (u32 i = 0; i < b.vertex_count(); ++i) {
            f64 x, y, z;
            gcm::to_world(v, b.vertex_point[i], x, y, z);
            const f64 radial = std::hypot(x, y);
            if (std::fabs(radial - 2.0) > 0.2) continue;
            d = std::max(d, cyl.distance_to(x, y, z));
        }
        return d;
    };
    CHECK_MSG(worst(refined.body) <= worst(unrefined.body) + 1e-9,
              "refinement moved the seam further from the cylinder: %g vs %g",
              worst(refined.body), worst(unrefined.body));
}

TEST(curved_boolean_will_not_drag_a_vertex_onto_the_wrong_surface) {
    const auto v = vol();
    const Body block = box(v, -5, -5, 5, 5, 4);
    const Body pin = cylinder(v, 0, 0, 2.0, 6.0, 24, -1.0);

    CurvedBoolSettings s;
    s.chord_tolerance = chord_error(2.0, 24);
    const CurvedBoolResult r = boolean_curved(block, pin, BoolOp::Subtract, v, s);
    CHECK(r.ok());

    CurvedBoolSettings raw = s;
    raw.refine_seams = false;
    const CurvedBoolResult before =
        boolean_curved(block, pin, BoolOp::Subtract, v, raw);
    CHECK(before.body.vertex_count() == r.body.vertex_count());

    const f64 limit = s.chord_tolerance * 2.0 + 4.0 / v.scale();
    for (u32 i = 0; i < r.body.vertex_count(); ++i) {
        f64 ax, ay, az, bx, by, bz;
        gcm::to_world(v, before.body.vertex_point[i], ax, ay, az);
        gcm::to_world(v, r.body.vertex_point[i], bx, by, bz);
        const f64 moved = std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by) +
                                    (az - bz) * (az - bz));
        CHECK_MSG(moved <= limit, "a vertex moved %g, past the %g limit", moved,
                  limit);
    }
}

TEST(curved_boolean_volume_converges_on_the_analytic_answer) {
    const auto v = vol();
    const Body block = box(v, -5, -5, 5, 5, 4);
    const f64 exact = 10.0 * 10.0 * 4.0 - kPi * 2.0 * 2.0 * 4.0;

    std::vector<f64> errors;
    for (int sides : {8, 16, 32, 64}) {
        const Body pin = cylinder(v, 0, 0, 2.0, 6.0, sides, -1.0);
        CurvedBoolSettings s;
        s.chord_tolerance = chord_error(2.0, sides);
        const CurvedBoolResult r = boolean_curved(block, pin, BoolOp::Subtract,
                                                  v, s);
        CHECK_MSG(r.ok(), "%d sides: %s", sides, r.message());

        const f64 got = volume(r.body, v);
        CHECK_MSG(got > exact - 1e-6,
                  "%d sides gave %g, LESS than the analytic %g -- an inscribed "
                  "tool cannot remove more than the cylinder does", sides, got,
                  exact);
        errors.push_back(std::fabs(got - exact));
    }

    for (std::size_t i = 1; i < errors.size(); ++i) {
        CHECK_MSG(errors[i] < errors[i - 1],
                  "error grew from %g to %g", errors[i - 1], errors[i]);
        const f64 ratio = errors[i] / errors[i - 1];
        CHECK_MSG(ratio > 0.15 && ratio < 0.35,
                  "the error fell by a factor of %g, not about four",
                  1.0 / ratio);
    }
    CHECK_MSG(errors.back() / exact < 0.001,
              "the finest cut is %g percent off", 100.0 * errors.back() / exact);
}

TEST(curved_boolean_falls_back_cleanly_when_nothing_is_curved) {
    const auto v = vol();
    const Body a = box(v, 0, 0, 10, 10, 5);
    const Body b = box(v, 5, 5, 15, 15, 5);

    const CurvedBoolResult r = boolean_curved(a, b, BoolOp::Union, v);
    CHECK(r.ok());
    CHECK(r.faceted == 0);
    CHECK(r.preserved == 0);
    CHECK(r.refined == 0);
    CHECK_MSG(!r.body.has_surfaces(),
              "an all-planar result carries a surface table for nothing");

    const BoolResult plain = boolean_op(a, b, BoolOp::Union, v);
    CHECK(plain.ok());
    CHECK(std::fabs(volume(r.body, v) - volume(plain.body, v)) < 1e-9);
}

TEST(curved_boolean_reports_the_planar_failure_rather_than_hiding_it) {
    const auto v = vol();
    const Body solid = box(v, 0, 0, 10, 10, 5);
    Body open = solid;
    if (open.face_loop_offset.size() > 2) open.face_loop_offset.pop_back();

    const CurvedBoolResult r = boolean_curved(solid, open, BoolOp::Union, v);
    if (!r.ok()) {
        CHECK(r.message()[0] != '\0');
        CHECK(r.preserved == 0 && r.refined == 0);
    }
}

TEST(curved_boolean_can_be_told_not_to_preserve_or_refine) {
    const auto v = vol();
    const Body block = box(v, -5, -5, 5, 5, 4);
    const Body pin = cylinder(v, 0, 0, 2.0, 6.0, 16, -1.0);

    CurvedBoolSettings off;
    off.preserve_surfaces = false;
    off.refine_seams = false;
    const CurvedBoolResult r = boolean_curved(block, pin, BoolOp::Subtract, v, off);
    CHECK(r.ok());
    CHECK(r.preserved == 0);
    CHECK(r.refined == 0);
    CHECK_MSG(!r.body.has_surfaces(),
              "surfaces were preserved when the caller asked for none");
}

TEST(curved_boolean_uniting_two_cylinders_keeps_both) {
    const auto v = vol();
    const Body a = cylinder(v, -1.0, 0, 2.0, 4.0, 24);
    const Body b = cylinder(v, 1.0, 0, 2.0, 4.0, 24);

    CurvedBoolSettings s;
    s.chord_tolerance = chord_error(2.0, 24);
    const CurvedBoolResult r = boolean_curved(a, b, BoolOp::Union, v, s);
    CHECK_MSG(r.ok(), "%s", r.message());
    CHECK(r.faceted == 48);
    CHECK_MSG(r.preserved > 0, "neither cylinder survived the union");

    bool left = false, right = false;
    for (u32 f = 0; f < r.body.face_count(); ++f) {
        const Surface* sf = r.body.surface_of(f);
        if (!sf || sf->kind != SurfaceKind::Cylinder) continue;
        if (std::fabs(sf->ox + 1.0) < 1e-9) left = true;
        if (std::fabs(sf->ox - 1.0) < 1e-9) right = true;
    }
    CHECK_MSG(left && right, "the union kept only one of the two cylinders");
}

TEST(curved_boolean_intersecting_two_cylinders_is_a_solid) {
    const auto v = vol();
    const Body a = cylinder(v, 0, 0, 2.0, 6.0, 24, -1.0);
    const Body b = cylinder(v, 0, 0, 2.0, 6.0, 24, -1.0);

    const CurvedBoolResult r = boolean_curved(a, b, BoolOp::Intersect, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    CHECK_MSG(std::fabs(volume(r.body, v) - volume(a, v)) < 1e-6,
              "self-intersection changed the volume from %g to %g", volume(a, v),
              volume(r.body, v));
    CHECK(r.preserved > 0);
}

TEST(curved_boolean_is_deterministic) {
    const auto v = vol();
    const Body block = box(v, -5, -5, 5, 5, 4);
    const Body pin = cylinder(v, 0, 0, 2.0, 6.0, 20, -1.0);

    const CurvedBoolResult a = boolean_curved(block, pin, BoolOp::Subtract, v);
    const CurvedBoolResult b = boolean_curved(block, pin, BoolOp::Subtract, v);
    CHECK(a.body.vertex_count() == b.body.vertex_count());
    CHECK(a.preserved == b.preserved);
    CHECK(a.refined == b.refined);
    for (u32 i = 0; i < a.body.vertex_count(); ++i)
        CHECK(a.body.vertex_point[i].x == b.body.vertex_point[i].x);
}
