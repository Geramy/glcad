#include "test.hpp"

#include <cmath>

#include "geometry/ssi.hpp"

using namespace gtopo;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

SsiRegion region(f64 half = 20.0) {
    SsiRegion r;
    for (int i = 0; i < 3; ++i) {
        r.lo[i] = -half;
        r.hi[i] = half;
    }
    return r;
}

f64 len3(f64 x, f64 y, f64 z) { return std::sqrt(x * x + y * y + z * z); }

void check_on_both(const SsiResult& r, const Surface& a, const Surface& b,
                   f64 tol) {
    for (const auto& c : r.curves) {
        if (c.kind == CurveKind::Coincident) continue;
        for (const auto& p : c.polyline) {
            CHECK_MSG(a.distance_to(p.x, p.y, p.z) < tol,
                      "%s point is %.12f off surface A", curve_kind_name(c.kind),
                      a.distance_to(p.x, p.y, p.z));
            CHECK_MSG(b.distance_to(p.x, p.y, p.z) < tol,
                      "%s point is %.12f off surface B", curve_kind_name(c.kind),
                      b.distance_to(p.x, p.y, p.z));
        }
    }
}

}

TEST(surface_implicit_is_zero_on_the_surface) {
    const Surface p = Surface::plane(0, 0, 5, 0, 0, 1);
    CHECK(std::fabs(p.implicit(3, -7, 5)) < 1e-12);
    CHECK(std::fabs(p.implicit(0, 0, 8) - 3.0) < 1e-12);

    const Surface s = Surface::sphere(1, 2, 3, 4.0);
    CHECK(std::fabs(s.implicit(5, 2, 3)) < 1e-12);
    CHECK(std::fabs(s.implicit(1, 2, 3) + 4.0) < 1e-12);

    const Surface c = Surface::cylinder(0, 0, 0, 0, 0, 1, 2.0);
    CHECK(std::fabs(c.implicit(2, 0, 99)) < 1e-12);
    CHECK(std::fabs(c.implicit(0, 0, 0) + 2.0) < 1e-12);

    const Surface t = Surface::torus(0, 0, 0, 0, 0, 1, 5.0, 1.0);
    CHECK(std::fabs(t.implicit(6, 0, 0)) < 1e-12);
    CHECK(std::fabs(t.implicit(5, 0, 1)) < 1e-12);
}

TEST(surface_normals_are_unit_and_point_outward) {
    const Surface surfaces[] = {
        Surface::plane(0, 0, 0, 0, 0, 1),
        Surface::sphere(0, 0, 0, 3.0),
        Surface::cylinder(0, 0, 0, 0, 0, 1, 2.0),
        Surface::cone(0, 0, 0, 0, 0, 1, 0.5),
        Surface::torus(0, 0, 0, 0, 0, 1, 5.0, 1.0),
    };

    for (const Surface& s : surfaces) {
        f64 x, y, z;
        CHECK_MSG(s.evaluate(0.7, 2.0, x, y, z), "%s did not evaluate",
                  surface_kind_name(s.kind));
        CHECK_MSG(s.distance_to(x, y, z) < 1e-9,
                  "%s: evaluate() left the surface by %.12f",
                  surface_kind_name(s.kind), s.distance_to(x, y, z));

        f64 nx, ny, nz;
        CHECK(s.normal_at(x, y, z, nx, ny, nz));
        CHECK_MSG(std::fabs(len3(nx, ny, nz) - 1.0) < 1e-9,
                  "%s normal has length %.12f", surface_kind_name(s.kind),
                  len3(nx, ny, nz));

        const f64 h = 1e-4;
        const f64 f = s.implicit(x + nx * h, y + ny * h, z + nz * h);
        CHECK_MSG(std::fabs(f - h) < 1e-6, "%s: gradient is not unit (%.9f)",
                  surface_kind_name(s.kind), f / h);
    }
}

TEST(surface_projection_lands_on_the_surface) {
    const Surface surfaces[] = {
        Surface::plane(1, 2, 3, 1, 1, 1),
        Surface::sphere(1, 2, 3, 4.0),
        Surface::cylinder(0, 0, 0, 0, 1, 0, 3.0),
        Surface::cone(0, 0, 0, 0, 0, 1, 0.6),
        Surface::torus(0, 0, 0, 1, 0, 0, 6.0, 2.0),
    };
    for (const Surface& s : surfaces) {
        f64 px, py, pz;
        CHECK_MSG(s.project(7.3, -2.1, 4.4, px, py, pz), "%s would not project",
                  surface_kind_name(s.kind));
        CHECK_MSG(s.distance_to(px, py, pz) < 1e-8,
                  "%s projected to %.12f off itself", surface_kind_name(s.kind),
                  s.distance_to(px, py, pz));
    }
}

TEST(surface_frames_are_orthonormal_after_construction) {
    Surface s;
    s.kind = SurfaceKind::Cylinder;
    s.ax = 0; s.ay = 0; s.az = 5;
    s.rx = 0; s.ry = 0; s.rz = 3;
    s.radius = 1.0;
    s.normalise();

    CHECK(std::fabs(len3(s.ax, s.ay, s.az) - 1.0) < 1e-12);
    CHECK(std::fabs(len3(s.rx, s.ry, s.rz) - 1.0) < 1e-12);
    CHECK_MSG(std::fabs(s.ax * s.rx + s.ay * s.ry + s.az * s.rz) < 1e-12,
              "the reference direction was not made perpendicular");
}

TEST(surface_equality_compares_geometry_not_numbers) {
    const Surface a = Surface::plane(0, 0, 0, 0, 0, 1);
    const Surface b = Surface::plane(9, -4, 0, 0, 0, 1);
    CHECK(a.same_as(b));
    CHECK(!a.same_as(Surface::plane(0, 0, 1, 0, 0, 1)));

    const Surface c = Surface::cylinder(0, 0, 0, 0, 0, 1, 2.0);
    const Surface d = Surface::cylinder(0, 0, 50, 0, 0, 1, 2.0);
    CHECK(c.same_as(d));
    CHECK(!c.same_as(Surface::cylinder(1, 0, 0, 0, 0, 1, 2.0)));
}

TEST(surface_validity_is_checked) {
    Surface s;
    s.kind = SurfaceKind::Sphere;
    s.radius = 0.0;
    CHECK(!s.valid());
    s.radius = -1.0;
    CHECK(!s.valid());

    Surface c = Surface::cone(0, 0, 0, 0, 0, 1, 0.0);
    CHECK_MSG(!c.valid(), "a zero-angle cone is a line, not a cone");
}

TEST(ssi_plane_plane_is_a_line) {
    const Surface a = Surface::plane(0, 0, 0, 0, 0, 1);
    const Surface b = Surface::plane(0, 0, 0, 1, 0, 0);
    const SsiResult r = intersect_surfaces(a, b, region());

    CHECK_MSG(r.ok && r.was_exact, "%s", r.reason.c_str());
    CHECK(r.size() == 1);
    CHECK(r.curves[0].kind == CurveKind::Line);
    CHECK(r.curves[0].exact());
    CHECK_MSG(std::fabs(std::fabs(r.curves[0].direction.y) - 1.0) < 1e-12,
              "direction (%.4f,%.4f,%.4f)", r.curves[0].direction.x,
              r.curves[0].direction.y, r.curves[0].direction.z);
    check_on_both(r, a, b, 1e-9);
}

TEST(ssi_parallel_planes_do_not_meet) {
    const Surface a = Surface::plane(0, 0, 0, 0, 0, 1);
    const Surface b = Surface::plane(0, 0, 5, 0, 0, 1);
    const SsiResult r = intersect_surfaces(a, b, region());
    CHECK(r.ok);
    CHECK(r.empty());
    CHECK(!r.reason.empty());
}

TEST(ssi_coincident_planes_are_reported_as_such) {
    const Surface a = Surface::plane(0, 0, 0, 0, 0, 1);
    const Surface b = Surface::plane(3, 4, 0, 0, 0, 1);
    const SsiResult r = intersect_surfaces(a, b, region());
    CHECK(r.ok);
    CHECK(r.size() == 1);
    CHECK_MSG(r.curves[0].kind == CurveKind::Coincident, "got %s",
              curve_kind_name(r.curves[0].kind));
}

TEST(ssi_plane_sphere_is_a_circle) {
    const Surface p = Surface::plane(0, 0, 3, 0, 0, 1);
    const Surface s = Surface::sphere(0, 0, 0, 5.0);
    const SsiResult r = intersect_surfaces(p, s, region());

    CHECK(r.ok && r.was_exact);
    CHECK(r.size() == 1);
    CHECK(r.curves[0].kind == CurveKind::Circle);
    CHECK_MSG(std::fabs(r.curves[0].radius - 4.0) < 1e-12,
              "radius %.12f, expected 4 (3-4-5)", r.curves[0].radius);
    CHECK(r.curves[0].closed);
    check_on_both(r, p, s, 1e-9);
}

TEST(ssi_tangent_plane_and_sphere_give_a_point_not_a_circle) {
    const Surface p = Surface::plane(0, 0, 5, 0, 0, 1);
    const Surface s = Surface::sphere(0, 0, 0, 5.0);
    const SsiResult r = intersect_surfaces(p, s, region());
    CHECK(r.ok);
    CHECK(r.size() == 1);
    CHECK_MSG(r.curves[0].kind == CurveKind::Point, "got %s",
              curve_kind_name(r.curves[0].kind));
    CHECK(r.curves[0].polyline.size() == 1);
}

TEST(ssi_plane_misses_a_sphere) {
    const Surface p = Surface::plane(0, 0, 9, 0, 0, 1);
    const Surface s = Surface::sphere(0, 0, 0, 5.0);
    const SsiResult r = intersect_surfaces(p, s, region());
    CHECK(r.ok);
    CHECK(r.empty());
}

TEST(ssi_sphere_sphere_is_a_circle) {
    const Surface a = Surface::sphere(0, 0, 0, 5.0);
    const Surface b = Surface::sphere(6, 0, 0, 5.0);
    const SsiResult r = intersect_surfaces(a, b, region());

    CHECK(r.ok && r.was_exact);
    CHECK(r.size() == 1);
    CHECK(r.curves[0].kind == CurveKind::Circle);
    CHECK_MSG(std::fabs(r.curves[0].radius - 4.0) < 1e-9, "radius %.12f",
              r.curves[0].radius);
    CHECK(std::fabs(r.curves[0].origin.x - 3.0) < 1e-9);
    check_on_both(r, a, b, 1e-9);
}

TEST(ssi_spheres_that_do_not_reach_each_other) {
    const Surface a = Surface::sphere(0, 0, 0, 2.0);
    const Surface b = Surface::sphere(10, 0, 0, 2.0);
    CHECK(intersect_surfaces(a, b, region()).empty());

    const Surface c = Surface::sphere(0, 0, 0, 1.0);
    const Surface d = Surface::sphere(0.1, 0, 0, 8.0);
    CHECK(intersect_surfaces(c, d, region()).empty());
}

TEST(ssi_plane_perpendicular_to_a_cylinder_is_a_circle) {
    const Surface p = Surface::plane(0, 0, 4, 0, 0, 1);
    const Surface c = Surface::cylinder(0, 0, 0, 0, 0, 1, 3.0);
    const SsiResult r = intersect_surfaces(p, c, region());

    CHECK(r.ok && r.was_exact);
    CHECK(r.size() == 1);
    CHECK(r.curves[0].kind == CurveKind::Circle);
    CHECK(std::fabs(r.curves[0].radius - 3.0) < 1e-12);
    CHECK(std::fabs(r.curves[0].origin.z - 4.0) < 1e-12);
    check_on_both(r, p, c, 1e-9);
}

TEST(ssi_tilted_plane_and_cylinder_is_an_ellipse) {
    const f64 tilt = kPi / 6.0;
    const Surface p =
        Surface::plane(0, 0, 0, 0, std::sin(tilt), std::cos(tilt));
    const Surface c = Surface::cylinder(0, 0, 0, 0, 0, 1, 2.0);
    const SsiResult r = intersect_surfaces(p, c, region());

    CHECK(r.ok && r.was_exact);
    CHECK(r.size() == 1);
    CHECK_MSG(r.curves[0].kind == CurveKind::Ellipse, "got %s",
              curve_kind_name(r.curves[0].kind));
    CHECK(std::fabs(r.curves[0].minor_radius - 2.0) < 1e-12);
    CHECK_MSG(std::fabs(r.curves[0].major_radius - 2.0 / std::cos(tilt)) < 1e-12,
              "major %.9f", r.curves[0].major_radius);
    check_on_both(r, p, c, 1e-9);
}

TEST(ssi_plane_along_a_cylinder_gives_two_lines) {
    const Surface p = Surface::plane(1, 0, 0, 1, 0, 0);
    const Surface c = Surface::cylinder(0, 0, 0, 0, 0, 1, 3.0);
    const SsiResult r = intersect_surfaces(p, c, region());

    CHECK(r.ok && r.was_exact);
    CHECK_MSG(r.size() == 2, "%zu curves", r.size());
    for (const auto& curve : r.curves) {
        CHECK(curve.kind == CurveKind::Line);
        CHECK(std::fabs(std::fabs(curve.direction.z) - 1.0) < 1e-12);
    }
    check_on_both(r, p, c, 1e-9);
}

TEST(ssi_plane_tangent_along_a_cylinder_gives_one_line) {
    const Surface p = Surface::plane(3, 0, 0, 1, 0, 0);
    const Surface c = Surface::cylinder(0, 0, 0, 0, 0, 1, 3.0);
    const SsiResult r = intersect_surfaces(p, c, region());
    CHECK_MSG(r.size() == 1, "%zu curves for a tangent plane", r.size());
}

TEST(ssi_parallel_cylinders_meet_in_two_lines) {
    const Surface a = Surface::cylinder(0, 0, 0, 0, 0, 1, 3.0);
    const Surface b = Surface::cylinder(4, 0, 0, 0, 0, 1, 3.0);
    const SsiResult r = intersect_surfaces(a, b, region());

    CHECK(r.ok && r.was_exact);
    CHECK_MSG(r.size() == 2, "%zu curves", r.size());
    check_on_both(r, a, b, 1e-9);
}

TEST(ssi_coaxial_cylinders) {
    const Surface a = Surface::cylinder(0, 0, 0, 0, 0, 1, 3.0);
    const Surface b = Surface::cylinder(0, 0, 20, 0, 0, 1, 3.0);
    const SsiResult same = intersect_surfaces(a, b, region());
    CHECK(same.size() == 1);
    CHECK(same.curves[0].kind == CurveKind::Coincident);

    const Surface c = Surface::cylinder(0, 0, 0, 0, 0, 1, 5.0);
    CHECK_MSG(intersect_surfaces(a, c, region()).empty(),
              "coaxial cylinders of different radius should never meet");
}

TEST(ssi_perpendicular_cylinders_are_marched) {
    const Surface a = Surface::cylinder(0, 0, 0, 0, 0, 1, 3.0);
    const Surface b = Surface::cylinder(0, 0, 0, 1, 0, 0, 2.0);
    const SsiRegion reg = region(8.0);
    const SsiResult r = intersect_surfaces(a, b, reg, 1e-10);

    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK_MSG(!r.was_exact, "a quartic was claimed as exact");
    CHECK_MSG(!r.empty(), "the marcher found nothing: %s", r.reason.c_str());
    check_on_both(r, a, b, 1e-7);

    f64 total = 0.0;
    for (const auto& c : r.curves) total += c.length();
    CHECK_MSG(total > 4.0, "the marched curves total only %.4f", total);
}

TEST(ssi_marched_points_really_are_on_both_surfaces) {
    const Surface a = Surface::sphere(0, 0, 0, 5.0);
    const Surface b = Surface::cylinder(2, 0, 0, 1, 1, 0, 2.0);
    const SsiResult r = intersect_surfaces(a, b, region(10.0), 1e-11);
    CHECK(r.ok);
    CHECK(!r.empty());
    check_on_both(r, a, b, 1e-7);
}

TEST(ssi_marched_curve_carries_its_tolerance) {
    const Surface a = Surface::cylinder(0, 0, 0, 0, 0, 1, 3.0);
    const Surface b = Surface::cylinder(0, 0, 0, 1, 0, 0, 2.0);
    const SsiResult r = intersect_surfaces(a, b, region(8.0), 1e-9);
    for (const auto& c : r.curves) {
        CHECK(!c.exact());
        CHECK_MSG(c.tolerance > 0.0, "a marched curve reported no tolerance");
    }
}

TEST(ssi_surfaces_that_do_not_meet_report_nothing_not_a_failure) {
    const Surface a = Surface::sphere(0, 0, 0, 1.0);
    const Surface b = Surface::sphere(50, 0, 0, 1.0);
    const SsiResult r = intersect_surfaces(a, b, region(100.0));
    CHECK_MSG(r.ok, "no intersection was reported as an error");
    CHECK(r.empty());
}

TEST(ssi_degenerate_surfaces_are_refused) {
    Surface bad;
    bad.kind = SurfaceKind::Sphere;
    bad.radius = 0.0;
    const SsiResult r =
        intersect_surfaces(bad, Surface::plane(0, 0, 0, 0, 0, 1), region());
    CHECK(!r.ok);
    CHECK(!r.reason.empty());
}

TEST(ssi_reports_which_pairs_have_a_closed_form) {
    const Surface p = Surface::plane(0, 0, 0, 0, 0, 1);
    const Surface s = Surface::sphere(0, 0, 0, 1.0);
    const Surface c1 = Surface::cylinder(0, 0, 0, 0, 0, 1, 1.0);
    const Surface c2 = Surface::cylinder(0, 0, 0, 1, 0, 0, 1.0);
    const Surface t = Surface::torus(0, 0, 0, 0, 0, 1, 5.0, 1.0);

    CHECK(has_exact_form(p, p));
    CHECK(has_exact_form(p, s));
    CHECK(has_exact_form(s, s));
    CHECK(has_exact_form(p, c1));
    CHECK(has_exact_form(c1, c1));
    CHECK_MSG(!has_exact_form(c1, c2), "skew cylinders claimed a closed form");
    CHECK(!has_exact_form(t, p));
}

TEST(ssi_projection_onto_both_surfaces_converges) {
    const Surface a = Surface::plane(0, 0, 0, 0, 0, 1);
    const Surface b = Surface::sphere(0, 0, 0, 5.0);

    f64 px, py, pz;
    CHECK(project_onto_both(a, b, 4.0, 1.0, 2.0, px, py, pz));
    CHECK_MSG(a.distance_to(px, py, pz) < 1e-10, "%.12f",
              a.distance_to(px, py, pz));
    CHECK(b.distance_to(px, py, pz) < 1e-10);
    CHECK(std::fabs(len3(px, py, pz) - 5.0) < 1e-9);
}

TEST(ssi_projection_declines_when_the_normals_are_parallel) {
    const Surface a = Surface::plane(0, 0, 0, 0, 0, 1);
    const Surface b = Surface::plane(0, 0, 4, 0, 0, 1);
    f64 px, py, pz;
    CHECK(!project_onto_both(a, b, 1, 1, 1, px, py, pz));
}

TEST(ssi_every_curve_kind_is_named) {
    for (int i = 0; i <= static_cast<int>(CurveKind::Coincident); ++i)
        CHECK(std::string(curve_kind_name(static_cast<CurveKind>(i))).size() > 0);
}
