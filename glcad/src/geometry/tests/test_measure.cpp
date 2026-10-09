#include "test.hpp"

#include <cmath>

#include <algorithm>

#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/measure.hpp"

using namespace gtopo;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

Body box(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1, f64 h,
         f64 z0 = 0.0) {
    Profile p;
    p.outer = {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
               gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
    p.normalize();
    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
    return extrude(p, v, frame, h);
}

FaceId face_facing(const Body& b, const gcm::WorkingVolume& v, f64 nx, f64 ny,
                   f64 nz) {
    FaceId best = kInvalid;
    f64 best_dot = -2.0;
    for (FaceId f = 0; f < b.face_count(); ++f) {
        const Plane p = face_plane(b, f, v);
        if (!p.valid()) continue;
        const f64 d = p.nx * nx + p.ny * ny + p.nz * nz;
        if (d > best_dot) { best_dot = d; best = f; }
    }
    return best;
}

}

TEST(measure_a_box_has_the_volume_and_area_it_should) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);

    const MassProperties m = mass_properties(b, v, 1.0);
    CHECK_MSG(m.ok, "%s", m.reason);
    CHECK_MSG(std::fabs(m.volume - 24.0) < 1e-6, "volume %.9f", m.volume);
    CHECK_MSG(std::fabs(m.area - 52.0) < 1e-6, "area %.9f", m.area);
    CHECK(std::fabs(m.mass - 24.0) < 1e-6);
}

TEST(measure_density_is_the_callers_not_a_silent_default) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 1, 1, 1);
    for (f64 d : {7850.0, 2700.0, 0.5}) {
        const f64 got = mass_properties(b, v, d).mass;
        CHECK_MSG(std::fabs(got - d) < d * 1e-6, "density %.1f gave mass %.6f", d,
                  got);
    }
}

TEST(measure_centroid_of_a_box_is_its_middle) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 4, 6);
    const MassProperties m = mass_properties(b, v);
    CHECK(m.ok);
    CHECK_MSG(std::fabs(m.centroid[0] - 1.0) < 1e-6, "x %.9f", m.centroid[0]);
    CHECK_MSG(std::fabs(m.centroid[1] - 2.0) < 1e-6, "y %.9f", m.centroid[1]);
    CHECK_MSG(std::fabs(m.centroid[2] - 3.0) < 1e-6, "z %.9f", m.centroid[2]);
}

TEST(measure_centroid_of_an_offset_box_moves_with_it) {
    const auto v = vol();
    const Body b = box(v, 10, 20, 12, 24, 6, 5);
    const MassProperties m = mass_properties(b, v);
    CHECK(m.ok);
    CHECK(std::fabs(m.centroid[0] - 11.0) < 1e-6);
    CHECK(std::fabs(m.centroid[1] - 22.0) < 1e-6);
    CHECK_MSG(std::fabs(m.centroid[2] - 8.0) < 1e-6, "z %.9f", m.centroid[2]);
}

TEST(measure_inertia_of_a_box_matches_the_textbook) {
    const auto v = vol();
    const f64 a = 2.0, b_ = 4.0, c = 6.0;
    const Body b = box(v, 0, 0, a, b_, c);
    const f64 density = 3.0;

    const MassProperties m = mass_properties(b, v, density);
    CHECK(m.ok);
    const f64 mass = a * b_ * c * density;
    CHECK_MSG(std::fabs(m.mass - mass) < mass * 1e-6, "mass %.9f vs %.9f", m.mass,
              mass);

    const f64 ixx = mass * (b_ * b_ + c * c) / 12.0;
    const f64 iyy = mass * (a * a + c * c) / 12.0;
    const f64 izz = mass * (a * a + b_ * b_) / 12.0;
    CHECK_MSG(std::fabs(m.inertia[0] - ixx) < 1e-4, "Ixx %.6f, expected %.6f",
              m.inertia[0], ixx);
    CHECK_MSG(std::fabs(m.inertia[4] - iyy) < 1e-4, "Iyy %.6f, expected %.6f",
              m.inertia[4], iyy);
    CHECK_MSG(std::fabs(m.inertia[8] - izz) < 1e-4, "Izz %.6f, expected %.6f",
              m.inertia[8], izz);

    const f64 scale = std::max({m.inertia[0], m.inertia[4], m.inertia[8]});
    for (int k : {1, 2, 5})
        CHECK_MSG(std::fabs(m.inertia[k]) < scale * 1e-6,
                  "product of inertia %d is %.9f against a scale of %.3f", k,
                  m.inertia[k], scale);
}

TEST(measure_principal_moments_are_the_diagonal_for_an_aligned_box) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 4, 6);
    const MassProperties m = mass_properties(b, v, 1.0);
    CHECK(m.ok);

    f64 diag[3] = {m.inertia[0], m.inertia[4], m.inertia[8]};
    std::sort(diag, diag + 3);
    for (int i = 0; i < 3; ++i)
        CHECK_MSG(std::fabs(m.principal[i] - diag[i]) < 1e-4,
                  "principal %d is %.6f, diagonal %.6f", i, m.principal[i],
                  diag[i]);

    for (int i = 0; i < 3; ++i)
        CHECK(m.radius_of_gyration[i] > 0.0);
}

TEST(measure_bounding_box_is_reported) {
    const auto v = vol();
    const Body b = box(v, 1, 2, 5, 9, 3, 4);
    const MassProperties m = mass_properties(b, v);
    CHECK(m.ok);
    CHECK(std::fabs(m.lo[0] - 1.0) < 1e-6);
    CHECK(std::fabs(m.hi[1] - 9.0) < 1e-6);
    CHECK(std::fabs(m.lo[2] - 4.0) < 1e-6);
    CHECK(std::fabs(m.hi[2] - 7.0) < 1e-6);
}

TEST(measure_an_open_surface_has_no_mass_properties) {
    const auto v = vol();
    Body b;
    const MassProperties m = mass_properties(b, v);
    CHECK(!m.ok);
    CHECK(std::string(m.reason).size() > 0);
}

TEST(measure_mass_properties_of_a_solid_with_a_hole) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 10, 10, 4);
    const Body pin = box(v, 4, 4, 6, 6, 10, -3);
    const BoolResult cut = subtract(block, pin, v);
    CHECK_MSG(cut.ok(), "the hole could not be cut");

    const MassProperties m = mass_properties(cut.body, v, 1.0);
    CHECK_MSG(m.ok, "%s", m.reason);
    CHECK_MSG(std::fabs(m.volume - 384.0) < 1e-3, "volume %.6f", m.volume);
    CHECK(std::fabs(m.centroid[0] - 5.0) < 1e-4);
    CHECK(std::fabs(m.centroid[1] - 5.0) < 1e-4);
}

TEST(measure_section_through_a_box_is_a_rectangle) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    const SectionResult r = section(b, Plane{0, 0, 1, 4.0}, v);

    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK_MSG(r.loops.size() == 1, "%zu loops", r.loops.size());
    CHECK_MSG(std::fabs(r.cut_area - 24.0) < 1e-4, "cut area %.6f", r.cut_area);
    CHECK(r.loops[0].outer);
    CHECK(r.loops[0].points.size() >= 12);
}

TEST(measure_section_of_a_tube_is_a_ring_not_two_discs) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 10, 10, 6);
    const Body bore = box(v, 3, 3, 7, 7, 12, -3);
    const BoolResult tube = subtract(block, bore, v);
    CHECK(tube.ok());

    const SectionResult r = section(tube.body, Plane{0, 0, 1, 3.0}, v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK_MSG(r.loops.size() == 2, "%zu loops through a tube", r.loops.size());
    CHECK_MSG(std::fabs(r.cut_area - 84.0) < 1e-3, "cut area %.6f", r.cut_area);

    int outers = 0;
    for (const auto& l : r.loops)
        if (l.outer) ++outers;
    CHECK_MSG(outers == 1, "%d loops claimed to be outer", outers);
}

TEST(measure_section_produces_real_geometry_not_pixels) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const SectionResult r = section(b, Plane{0, 1, 0, 2.0}, v);
    CHECK(r.ok);
    CHECK_MSG(!r.segments.empty(), "no cut segments were produced");
    CHECK(r.segments.size() % 6 == 0);

    for (const auto& l : r.loops)
        for (std::size_t i = 0; i + 2 < l.points.size(); i += 3)
            CHECK_MSG(std::fabs(l.points[i + 1] - 2.0) < 1e-6,
                      "a cut point sits at y = %.9f", l.points[i + 1]);
}

TEST(measure_a_plane_that_misses_the_solid_says_so) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const SectionResult r = section(b, Plane{0, 0, 1, 50.0}, v);
    CHECK_MSG(r.ok, "a miss was reported as an error");
    CHECK(r.loops.empty());
    CHECK(std::string(r.reason).size() > 0);
}

TEST(measure_section_refuses_a_degenerate_plane) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    CHECK(!section(b, Plane{0, 0, 0, 0}, v).ok);
    CHECK(!section(Body{}, Plane{0, 0, 1, 0}, v).ok);
}

TEST(measure_edge_length) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 3, 4, 5);
    bool found_three = false, found_five = false;
    for (u32 h = 0; h < b.halfedge_count(); ++h) {
        const Measurement m = measure_edge_length(b, h, v);
        CHECK(m.ok);
        CHECK(m.units == "m");
        if (std::fabs(m.value - 3.0) < 1e-6) found_three = true;
        if (std::fabs(m.value - 5.0) < 1e-6) found_five = true;
    }
    CHECK(found_three && found_five);
    CHECK(!measure_edge_length(b, 9999, v).ok);
}

TEST(measure_face_area_and_perimeter) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 3, 4, 5);
    const FaceId top = face_facing(b, v, 0, 0, 1);

    const Measurement a = measure_face_area(b, top, v);
    CHECK_MSG(a.ok, "%s", a.reason);
    CHECK(a.units == "m^2");
    CHECK_MSG(std::fabs(a.value - 12.0) < 1e-6, "area %.9f", a.value);

    const Measurement p = measure_face_perimeter(b, top, v);
    CHECK(p.ok);
    CHECK(p.units == "m");
    CHECK_MSG(std::fabs(p.value - 14.0) < 1e-6, "perimeter %.9f", p.value);
}

TEST(measure_a_face_with_a_hole_reports_the_material_not_the_outline) {
    const auto v = vol();
    Builder bld;
    auto add = [&](f64 x, f64 y, f64 z) {
        return bld.add_vertex(gcm::to_lattice(v, x, y, z));
    };
    const VertexId o0 = add(0, 0, 0), o1 = add(10, 0, 0), o2 = add(10, 10, 0),
                   o3 = add(0, 10, 0);
    const VertexId i0 = add(4, 4, 0), i1 = add(6, 4, 0), i2 = add(6, 6, 0),
                   i3 = add(4, 6, 0);

    bld.begin_face();
    bld.add_loop({o0, o1, o2, o3});
    bld.add_loop({i0, i3, i2, i1});
    bld.end_face();
    const Body b = bld.build();

    const Measurement a = measure_face_area(b, 0, v);
    CHECK_MSG(a.ok, "%s", a.reason);
    CHECK_MSG(std::fabs(a.value - 96.0) < 1e-3,
              "the holed face measured %.6f, expected 100 - 4", a.value);
}

TEST(measure_the_top_of_a_cut_block_still_totals_the_right_area) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 10, 10, 4);
    const Body pin = box(v, 4, 4, 6, 6, 10, -3);
    const BoolResult cut = subtract(block, pin, v);
    CHECK(cut.ok());

    f64 total = 0.0;
    for (FaceId f = 0; f < cut.body.face_count(); ++f) {
        const Plane p = face_plane(cut.body, f, v);
        if (!p.valid() || p.nz < 0.999 || std::fabs(p.d - 4.0) > 1e-6) continue;
        total += measure_face_area(cut.body, f, v).value;
    }
    CHECK_MSG(std::fabs(total - 96.0) < 1e-3,
              "the top totalled %.6f, expected 100 - 4", total);
}

TEST(measure_angle_between_faces_is_the_dihedral) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const Measurement m = measure_angle_between_faces(
        b, face_facing(b, v, 0, 0, 1), face_facing(b, v, 1, 0, 0), v);
    CHECK(m.ok);
    CHECK(m.units == "rad");
    CHECK_MSG(std::fabs(m.value - 3.14159265358979323846 * 0.5) < 1e-6,
              "the corner measured %.6f rad", m.value);
}

TEST(measure_units_are_named_so_an_area_cannot_be_printed_as_a_length) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 2, 2);
    CHECK(measure_face_area(b, 0, v).units == "m^2");
    CHECK(measure_edge_length(b, 0, v).units == "m");
    CHECK(measure_angle_between_faces(b, 0, 1, v).units == "rad");
}

TEST(measure_distance_between_two_vertices) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 3, 4, 0.0001);
    const Measurement m = measure_distance(b, 0, 2, v);
    CHECK(m.ok);
    CHECK(m.value > 0.0);
    CHECK(!measure_distance(b, 0, 9999, v).ok);
}

TEST(measure_clearance_between_two_bodies) {
    const auto v = vol();
    const Body a = box(v, 0, 0, 2, 2, 2);
    const Body b = box(v, 5, 0, 7, 2, 2);

    const Measurement m = measure_clearance(a, b, v);
    CHECK_MSG(m.ok, "%s", m.reason);
    CHECK_MSG(std::fabs(m.value - 3.0) < 1e-6, "clearance %.9f", m.value);
    CHECK(std::string(m.reason).find("closest approach") != std::string::npos);
}

TEST(measure_clearance_of_an_empty_body_is_refused) {
    const auto v = vol();
    const Body a = box(v, 0, 0, 2, 2, 2);
    CHECK(!measure_clearance(a, Body{}, v).ok);
}
