#include "test.hpp"

#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/extrude.hpp"

using namespace gtopo;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

Loop2 rect(const gcm::WorkingVolume& v, double x0, double y0, double x1, double y1) {
    return {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
            gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
}

Body box(const gcm::WorkingVolume& v, double x0, double y0, double x1, double y1,
         double h, double z0 = 0.0) {
    Profile p;
    p.outer = rect(v, x0, y0, x1, y1);
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

HalfedgeId edge_between(const Body& b, FaceId a, FaceId c) {
    for (u32 h = 0; h < b.halfedge_count(); ++h) {
        FaceId fa, fb;
        if (!faces_of_edge(b, h, fa, fb)) continue;
        if ((fa == a && fb == c) || (fa == c && fb == a)) return h;
    }
    return kInvalid;
}

Vec3 vec(f64 x, f64 y, f64 z) {
    return {gcm::F64::from(x), gcm::F64::from(y), gcm::F64::from(z)};
}

}

TEST(edit_face_plane_is_outward_and_unit) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    CHECK(b.face_count() == 6);

    for (FaceId f = 0; f < b.face_count(); ++f) {
        const Plane p = face_plane(b, f, v);
        CHECK_MSG(p.valid(), "face %u has no usable plane", f);
        const f64 n = std::sqrt(p.nx * p.nx + p.ny * p.ny + p.nz * p.nz);
        CHECK(std::fabs(n - 1.0) < 1e-9);
        CHECK_MSG(face_planarity(b, f, v) < 1e-6,
                  "face %u is not planar (%.9f)", f, face_planarity(b, f, v));
    }

    const FaceId top = face_facing(b, v, 0, 0, 1);
    const Plane p = face_plane(b, top, v);
    CHECK_MSG(std::fabs(p.nz - 1.0) < 1e-9, "top normal is (%.3f,%.3f,%.3f)",
              p.nx, p.ny, p.nz);
    CHECK_MSG(std::fabs(p.d - 4.0) < 1e-6, "top plane at %.6f", p.d);
}

TEST(edit_three_planes_meet_at_a_point) {
    f64 x, y, z;
    CHECK(intersect_planes({1, 0, 0, 3}, {0, 1, 0, 5}, {0, 0, 1, 7}, x, y, z));
    CHECK(std::fabs(x - 3) < 1e-12 && std::fabs(y - 5) < 1e-12 &&
          std::fabs(z - 7) < 1e-12);

    CHECK(!intersect_planes({1, 0, 0, 0}, {1, 0, 0, 1}, {0, 0, 1, 0}, x, y, z));
    CHECK(!intersect_planes({1, 0, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}, x, y, z));
}

TEST(edit_vertex_reports_its_faces) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    for (VertexId i = 0; i < b.vertex_count(); ++i)
        CHECK_MSG(faces_at_vertex(b, i).size() == 3,
                  "corner %u touches %zu faces", i, faces_at_vertex(b, i).size());
}

TEST(edit_box_edges_are_all_convex) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    int checked = 0;
    for (u32 h = 0; h < b.halfedge_count(); ++h) {
        FaceId fa, fb;
        if (!faces_of_edge(b, h, fa, fb)) continue;
        CHECK_MSG(edge_is_convex(b, h, v), "edge %u read as concave", h);
        ++checked;
    }
    CHECK(checked == 24);
}

TEST(edit_offset_face_changes_volume_by_face_area_times_distance) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);

    const EditResult r = offset_face(b, top, 1.0, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK_MSG(std::fabs(r.volume_before - 24.0) < 1e-3, "%.6f", r.volume_before);
    CHECK_MSG(std::fabs(r.volume_after - 30.0) < 1e-3, "%.6f", r.volume_after);
    CHECK_MSG(std::fabs(r.volume_delta() - 6.0) < 1e-3, "delta %.6f",
              r.volume_delta());

    const Topology t = r.body.analyse();
    CHECK(t.closed && t.manifold);
    CHECK(t.genus == 0);
    CHECK(r.body.face_count() == 6);
}

TEST(edit_offset_inward_removes_material) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    const EditResult r = offset_face(b, top, -1.0, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK(std::fabs(r.volume_after - 18.0) < 1e-3);
}

TEST(edit_offset_through_the_far_side_is_refused) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    const EditResult r = offset_face(b, top, -6.0, v);
    CHECK_MSG(!r.ok, "an inverted solid was accepted");
    CHECK(!r.reason.empty());
}

TEST(edit_move_face_uses_only_the_normal_component) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);

    const EditResult r = move_face(b, top, vec(5.0, -3.0, 1.0), v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK_MSG(std::fabs(r.volume_after - 30.0) < 1e-3, "%.6f", r.volume_after);
}

TEST(edit_sliding_a_face_in_its_own_plane_is_refused) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    const EditResult r = move_face(b, top, vec(1.0, 1.0, 0.0), v);
    CHECK(!r.ok);
    CHECK(r.reason.find("own plane") != std::string::npos);
}

TEST(edit_offset_a_side_face_resizes_the_box) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId east = face_facing(b, v, 1, 0, 0);
    const EditResult r = offset_face(b, east, 1.0, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK_MSG(std::fabs(r.volume_after - 36.0) < 1e-3, "%.6f", r.volume_after);
}

TEST(edit_a_second_edit_composes_with_the_first) {
    const auto v = vol();
    Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    EditResult r = offset_face(b, top, 1.0, v);
    CHECK(r.ok);

    const FaceId east = face_facing(r.body, v, 1, 0, 0);
    const EditResult r2 = offset_face(r.body, east, 1.0, v);
    CHECK_MSG(r2.ok, "%s", r2.reason.c_str());
    CHECK_MSG(std::fabs(r2.volume_after - 45.0) < 1e-3, "%.6f", r2.volume_after);
}

TEST(edit_retarget_to_a_tilted_plane_keeps_the_solid_closed) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);

    Plane tilted{0.0, 0.24253562503633297, 0.9701425001453319, 0.0};
    tilted.d = tilted.ny * 0.0 + tilted.nz * 4.0;
    const EditResult r = retarget_face(b, top, tilted, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const Topology t = r.body.analyse();
    CHECK(t.closed && t.manifold);
    CHECK_MSG(face_planarity(r.body, top, v) < 1e-5,
              "the retargeted face is no longer planar");
}

TEST(edit_no_such_face_is_reported) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    CHECK(!offset_face(b, 99, 1.0, v).ok);
    CHECK(!move_face(b, 99, vec(0, 0, 1), v).ok);
    CHECK(!offset_face(b, 0, 0.0, v).ok);
}

TEST(edit_draft_tilts_a_wall_about_the_neutral_plane) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId east = face_facing(b, v, 1, 0, 0);

    const Plane neutral{0, 0, 1, 0};
    const EditResult r = draft_face(b, east, neutral, 5.0, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const Topology t = r.body.analyse();
    CHECK(t.closed && t.manifold);
    CHECK_MSG(std::fabs(r.volume_delta()) > 1e-4, "the draft changed nothing");

    CHECK(face_planarity(r.body, east, v) < 1e-5);
}

TEST(edit_draft_direction_follows_the_angle_sign) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId east = face_facing(b, v, 1, 0, 0);
    const Plane neutral{0, 0, 1, 0};

    const EditResult in = draft_face(b, east, neutral, 5.0, v);
    const EditResult out = draft_face(b, east, neutral, -5.0, v);
    CHECK(in.ok && out.ok);
    CHECK_MSG((in.volume_delta() > 0.0) != (out.volume_delta() > 0.0),
              "both draft directions changed the volume the same way");
}

TEST(edit_draft_about_a_parallel_plane_is_refused) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    const EditResult r = draft_face(b, top, Plane{0, 0, 1, 0}, 5.0, v);
    CHECK(!r.ok);
    CHECK(r.reason.find("parallel") != std::string::npos);
}

TEST(edit_chamfer_removes_a_prism_of_material) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    const FaceId east = face_facing(b, v, 1, 0, 0);
    const HalfedgeId e = edge_between(b, top, east);
    CHECK_MSG(e != kInvalid, "no edge between the top and east faces");

    const EditResult r = chamfer_edge(b, e, 0.5, 0.5, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const Topology t = r.body.analyse();
    CHECK_MSG(t.closed && t.manifold, "the chamfer left the solid open");
    CHECK(t.genus == 0);
    CHECK_MSG(r.body.face_count() == 7, "%u faces", r.body.face_count());

    CHECK_MSG(std::fabs(r.volume_delta() + 0.375) < 1e-3, "removed %.6f",
              -r.volume_delta());
}

TEST(edit_chamfer_setbacks_can_differ) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = chamfer_edge(b, e, 0.25, 1.0, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK_MSG(std::fabs(r.volume_delta() + 0.375) < 1e-3, "removed %.6f",
              -r.volume_delta());
}

TEST(edit_chamfer_rejects_bad_input) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    CHECK(!chamfer_edge(b, e, 0.0, 0.5, v).ok);
    CHECK(!chamfer_edge(b, e, -1.0, 0.5, v).ok);
    CHECK(!chamfer_edge(b, 9999, 0.5, 0.5, v).ok);
}

TEST(edit_chamfered_body_still_boolean_ready) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = chamfer_edge(b, e, 0.5, 0.5, v);
    CHECK(r.ok);
    CHECK_MSG(all_faces_planar(r.body), "the chamfer produced a non-planar face");

    const Body pin = box(v, 0.5, 0.5, 1.5, 1.5, 10.0, -1.0);
    const BoolResult cut = subtract(r.body, pin, v);
    CHECK_MSG(cut.ok(), "the chamfered body could not be cut");
}

TEST(edit_fillet_is_faceted_and_closes_the_solid) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));

    const EditResult r = fillet_edge(b, e, 0.5, 6, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const Topology t = r.body.analyse();
    CHECK_MSG(t.closed && t.manifold, "the fillet left the solid open");
    CHECK(t.genus == 0);
    CHECK_MSG(r.body.face_count() == 6 + 6, "%u faces", r.body.face_count());
    CHECK(all_faces_planar(r.body));
}

TEST(edit_fillet_removes_less_than_the_chamfer_it_replaces) {
    // Blend removes r^2(1 - pi/4) per unit length; chamfer removes r^2/2.
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));

    const EditResult f = fillet_edge(b, e, 0.5, 12, v);
    const EditResult c = chamfer_edge(b, e, 0.5, 0.5, v);
    CHECK_MSG(f.ok, "%s", f.reason.c_str());
    CHECK(c.ok);
    CHECK_MSG(-f.volume_delta() < -c.volume_delta(),
              "the fillet removed %.6f, the chamfer %.6f", -f.volume_delta(),
              -c.volume_delta());

    const f64 expect = 0.25 * (1.0 - 3.14159265358979323846 / 4.0) * 3.0;
    CHECK_MSG(std::fabs(-f.volume_delta() - expect) < expect * 0.1,
              "removed %.6f, analytic %.6f", -f.volume_delta(), expect);
}

TEST(edit_more_fillet_segments_converge_on_the_analytic_volume) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const f64 expect = 0.25 * (1.0 - 3.14159265358979323846 / 4.0) * 3.0;

    const EditResult coarse = fillet_edge(b, e, 0.5, 3, v);
    const EditResult fine = fillet_edge(b, e, 0.5, 24, v);
    CHECK(coarse.ok && fine.ok);
    CHECK_MSG(std::fabs(-fine.volume_delta() - expect) <
                  std::fabs(-coarse.volume_delta() - expect),
              "refining the blend did not improve it");
}

TEST(edit_fillet_rejects_bad_input) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    CHECK(!fillet_edge(b, e, 0.0, 4, v).ok);
    CHECK(!fillet_edge(b, e, -1.0, 4, v).ok);
    CHECK(!fillet_edge(b, 9999, 0.5, 4, v).ok);
}

TEST(edit_face_identities_survive_a_move) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    IdMap ids;
    ids.seed(b);
    CHECK(ids.size() == 6);

    const FaceId top = face_facing(b, v, 0, 0, 1);
    const u32 top_id = ids.face_id(top);
    CHECK(top_id != 0);

    const EditResult r = offset_face(b, top, 1.0, v);
    CHECK(r.ok);

    std::vector<u32> survivors(r.body.face_count());
    for (u32 i = 0; i < r.body.face_count(); ++i) survivors[i] = i;
    ids.follow(b, r.body, survivors);

    CHECK_MSG(ids.face_id(top) == top_id, "the top face lost its identity");
    CHECK(ids.face_of(top_id) == top);
}

TEST(edit_a_new_face_gets_a_new_identity) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    IdMap ids;
    ids.seed(b);

    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = chamfer_edge(b, e, 0.5, 0.5, v);
    CHECK(r.ok);

    std::vector<u32> survivors(r.body.face_count(), kInvalid);
    for (u32 i = 0; i < b.face_count(); ++i) survivors[i] = i;
    ids.follow(b, r.body, survivors);

    CHECK(ids.size() == r.body.face_count());
    const u32 fresh = ids.face_id(r.body.face_count() - 1);
    CHECK_MSG(fresh != 0, "the chamfer face got no identity");
    for (u32 i = 0; i + 1 < r.body.face_count(); ++i)
        CHECK_MSG(ids.face_id(i) != fresh, "the new face reused an old name");
}

TEST(edit_identity_of_a_deleted_face_resolves_to_nothing) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    IdMap ids;
    ids.seed(b);
    const u32 gone = ids.allocate();
    CHECK_MSG(ids.face_of(gone) == kInvalid,
              "an unassigned identity resolved to a face");
}

TEST(edit_exact_fillet_records_the_cylinder_it_really_is) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));

    const EditResult r = fillet_edge_exact(b, e, 0.5, 8, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK_MSG(r.body.has_surfaces(), "the blend carries no surface");
    CHECK(r.body.face_surface.size() == r.body.face_count());
    CHECK_MSG(r.body.any_face_curved(), "no face was recorded as curved");

    int cylindrical = 0;
    for (FaceId f = 0; f < r.body.face_count(); ++f)
        if (r.body.face_is_curved(f)) {
            ++cylindrical;
            const Surface* s = r.body.surface_of(f);
            CHECK(s->kind == SurfaceKind::Cylinder);
            CHECK_MSG(std::fabs(s->radius - 0.5) < 1e-9,
                      "the blend recorded radius %.9f", s->radius);
        }
    CHECK_MSG(cylindrical == 8, "%d faces carry the blend surface", cylindrical);
}

TEST(edit_exact_fillet_facets_lie_on_the_recorded_cylinder) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge_exact(b, e, 0.5, 12, v);
    CHECK(r.ok);

    for (FaceId f = 0; f < r.body.face_count(); ++f) {
        if (!r.body.face_is_curved(f)) continue;
        const Surface* s = r.body.surface_of(f);
        const u32 l = r.body.face_loop_begin(f);
        for (u32 h = r.body.loop_begin(l); h < r.body.loop_end(l); ++h) {
            f64 x, y, z;
            gcm::to_world(v, r.body.vertex_point[r.body.he_vertex[h]], x, y, z);
            CHECK_MSG(s->distance_to(x, y, z) < 1e-5,
                      "a blend vertex is %.9f off its own cylinder",
                      s->distance_to(x, y, z));
        }
    }
}

TEST(edit_a_planar_body_carries_no_surface_table) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    CHECK(!b.has_surfaces());
    CHECK(!b.any_face_curved());
    CHECK(b.surface_of(0) == nullptr);
}

TEST(edit_offset_surface_keeps_each_kind_parallel_to_itself) {
    Surface out;

    CHECK(offset_surface(Surface::plane(0, 0, 0, 0, 0, 1), 2.0, out));
    CHECK(out.kind == SurfaceKind::Plane);
    CHECK(std::fabs(out.oz - 2.0) < 1e-12);

    CHECK(offset_surface(Surface::cylinder(0, 0, 0, 0, 0, 1, 5.0), -1.0, out));
    CHECK_MSG(std::fabs(out.radius - 4.0) < 1e-12, "radius %.9f", out.radius);

    CHECK(offset_surface(Surface::sphere(0, 0, 0, 3.0), 1.5, out));
    CHECK(std::fabs(out.radius - 4.5) < 1e-12);

    CHECK(offset_surface(Surface::torus(0, 0, 0, 0, 0, 1, 5.0, 1.0), 0.25, out));
    CHECK(std::fabs(out.minor_radius - 1.25) < 1e-12);
}

TEST(edit_offset_cone_moves_its_apex_not_its_angle) {
    const Surface c = Surface::cone(0, 0, 0, 0, 0, 1, 0.5);
    Surface out;
    CHECK(offset_surface(c, 1.0, out));
    CHECK_MSG(std::fabs(out.half_angle - c.half_angle) < 1e-15,
              "the angle changed from %.9f to %.9f", c.half_angle, out.half_angle);
    CHECK_MSG(std::fabs(out.oz + 1.0 / std::sin(0.5)) < 1e-9,
              "the apex moved to %.9f", out.oz);

    f64 x, y, z;
    c.evaluate(0.9, 4.0, x, y, z);
    CHECK_MSG(std::fabs(out.distance_to(x, y, z) - 1.0) < 1e-6,
              "the offset cone is %.9f away, not 1", out.distance_to(x, y, z));
}

TEST(edit_offset_that_would_collapse_a_feature_is_refused) {
    Surface out;
    CHECK(!offset_surface(Surface::cylinder(0, 0, 0, 0, 0, 1, 1.0), -2.0, out));
    CHECK(!offset_surface(Surface::sphere(0, 0, 0, 1.0), -1.0, out));
}

TEST(edit_shell_hollows_a_closed_box) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const ShellResult r = shell_body(b, 0.5, {}, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const Topology t = r.body.analyse();
    CHECK_MSG(t.closed && t.manifold, "the shell did not close");
    CHECK_MSG(r.body.face_count() == 12, "%u faces", r.body.face_count());

    // 4^3 minus 3^3: the wall volume, exactly.
    const f64 vol_val = volume(r.body, v);
    CHECK_MSG(std::fabs(vol_val - (64.0 - 27.0)) < 1e-3,
              "wall volume %.6f, expected 37", vol_val);
}

TEST(edit_shell_with_an_open_face_makes_a_box_without_a_lid) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const FaceId top = face_facing(b, v, 0, 0, 1);

    const ShellResult r = shell_body(b, 0.5, {top}, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK(r.removed_faces == 1);

    const Topology t = r.body.analyse();
    CHECK_MSG(t.closed && t.manifold, "the open shell is not a closed solid");
    CHECK_MSG(r.body.face_count() == 14, "%u faces", r.body.face_count());

    CHECK(volume(r.body, v) < 37.0);
}

TEST(edit_shell_thickness_is_reported_and_positive) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const ShellResult a = shell_body(b, 0.5, {}, v);
    const ShellResult c = shell_body(b, -0.5, {}, v);
    CHECK(a.ok && c.ok);
    CHECK(std::fabs(a.thickness - 0.5) < 1e-12);
    CHECK_MSG(std::fabs(c.thickness - 0.5) < 1e-12,
              "a negative thickness should still report a positive wall");
}

TEST(edit_shell_refuses_a_wall_thicker_than_the_part) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const ShellResult r = shell_body(b, 5.0, {}, v);
    CHECK_MSG(!r.ok, "a wall thicker than the part was accepted");
    CHECK(!r.reason.empty());
}

TEST(edit_shell_rejects_bad_input) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    CHECK(!shell_body(b, 0.0, {}, v).ok);
    CHECK(!shell_body(Body{}, 1.0, {}, v).ok);
}

TEST(edit_shelled_body_is_still_boolean_ready) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const ShellResult r = shell_body(b, 0.5, {}, v);
    CHECK(r.ok);
    CHECK(all_faces_planar(r.body));

    const Body pin = box(v, 1, 1, 2, 2, 20, -5);
    CHECK_MSG(subtract(r.body, pin, v).ok(), "the shell could not be cut");
}

TEST(edit_fillet_works_on_a_boss_over_a_holed_face) {
    const auto v = vol();
    Body block;
    {
        Profile p;
        p.outer = {gcm::to_lattice(v, 0, 0), gcm::to_lattice(v, 4, 0),
                   gcm::to_lattice(v, 4, 4), gcm::to_lattice(v, 0, 4)};
        p.normalize();
        block = extrude(p, v, ggeo::d3::Frame<gcm::F64>::world_xy(), 2.0);
    }
    Body pocket_tool;
    {
        Profile p;
        p.outer = {gcm::to_lattice(v, 1, 1), gcm::to_lattice(v, 3, 1),
                   gcm::to_lattice(v, 3, 3), gcm::to_lattice(v, 1, 3)};
        p.normalize();
        auto fr = ggeo::d3::Frame<gcm::F64>::world_xy();
        fr.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, 1.5);
        pocket_tool = extrude(p, v, fr, 0.5);
    }
    const BoolResult cut = subtract(block, pocket_tool, v);
    CHECK_MSG(cut.ok(), "%s", cut.message());
    Body base = merge_coplanar_faces(cut.body, v);
    Body boss;
    {
        Profile p;
        p.outer = {gcm::to_lattice(v, 0.2, 0.2), gcm::to_lattice(v, 0.8, 0.2),
                   gcm::to_lattice(v, 0.8, 0.8), gcm::to_lattice(v, 0.2, 0.8)};
        p.normalize();
        auto fr = ggeo::d3::Frame<gcm::F64>::world_xy();
        fr.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, 2.0);
        boss = extrude(p, v, fr, 0.5);
    }
    const BoolResult joined = unite(base, boss, v);
    CHECK_MSG(joined.ok(), "%s", joined.message());
    Body solid = merge_coplanar_faces(joined.body, v);
    {
        const auto t = solid.analyse();
        CHECK_MSG(t.closed && t.manifold, "the boss union is not sound");
    }

    const gcm::Lattice3 e0 = gcm::to_lattice(v, 0.2, 0.2, 2.5);
    const gcm::Lattice3 e1 = gcm::to_lattice(v, 0.8, 0.2, 2.5);
    HalfedgeId edge = kInvalid;
    for (u32 h = 0; h < solid.halfedge_count(); ++h) {
        const gcm::Lattice3& a = solid.vertex_point[solid.he_vertex[h]];
        const u32 tw = solid.he_twin[h];
        if (tw == kInvalid) continue;
        const gcm::Lattice3& b2 = solid.vertex_point[solid.he_vertex[tw]];
        if (a.x == e0.x && a.y == e0.y && a.z == e0.z &&
            b2.x == e1.x && b2.y == e1.y && b2.z == e1.z) { edge = h; break; }
    }
    CHECK_MSG(edge != kInvalid, "the boss top edge was not found");
    if (edge == kInvalid) return;

    const EditResult f = fillet_edge_exact(solid, edge, 0.05, 12, v);
    CHECK_MSG(f.ok, "fillet: %s", f.reason.c_str());
    if (!f.ok) return;
    const auto t = f.body.analyse();
    CHECK(t.closed && t.manifold);
    CHECK_MSG(volume(f.body, v) < volume(solid, v), "the round removed nothing");
}

TEST(edit_fillet_works_after_a_deep_recut) {
    const auto v = vol();
    auto boxb = [&](double x0, double y0, double x1, double y1, double z0, double h) {
        Profile p;
        p.outer = {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
                   gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
        p.normalize();
        auto fr = ggeo::d3::Frame<gcm::F64>::world_xy();
        fr.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
        return extrude(p, v, fr, h);
    };
    Body solid = boxb(0, 0, 4, 4, 0, 2);
    for (const auto& tool : {boxb(1, 1, 3, 3, 1.5, 0.5), boxb(1, 1, 3, 3, 1.0, 1.0)}) {
        const BoolResult r = subtract(solid, tool, v);
        CHECK_MSG(r.ok(), "%s", r.message());
        solid = merge_coplanar_faces(r.body, v);
    }
    CHECK_MSG(std::fabs(volume(solid, v) - 28.0) < 1e-3, "base volume %.4f", volume(solid, v));
    const BoolResult joined = unite(solid, boxb(0.2, 0.2, 0.8, 0.8, 2.0, 0.5), v);
    CHECK_MSG(joined.ok(), "%s", joined.message());
    solid = merge_coplanar_faces(joined.body, v);
    {
        const auto t = solid.analyse();
        CHECK_MSG(t.closed && t.manifold, "boss union over the re-cut is not sound");
    }
    const gcm::Lattice3 e0 = gcm::to_lattice(v, 0.2, 0.2, 2.5);
    const gcm::Lattice3 e1 = gcm::to_lattice(v, 0.8, 0.2, 2.5);
    HalfedgeId edge = kInvalid;
    for (u32 h = 0; h < solid.halfedge_count(); ++h) {
        const gcm::Lattice3& a = solid.vertex_point[solid.he_vertex[h]];
        const u32 tw = solid.he_twin[h];
        if (tw == kInvalid) continue;
        const gcm::Lattice3& b2 = solid.vertex_point[solid.he_vertex[tw]];
        if (a.x == e0.x && a.y == e0.y && a.z == e0.z &&
            b2.x == e1.x && b2.y == e1.y && b2.z == e1.z) { edge = h; break; }
    }
    CHECK_MSG(edge != kInvalid, "the boss top edge was not found");
    if (edge == kInvalid) return;
    const EditResult f = fillet_edge_exact(solid, edge, 0.05, 12, v);
    CHECK_MSG(f.ok, "fillet: %s", f.reason.c_str());
    if (f.ok) {
        const auto t = f.body.analyse();
        CHECK(t.closed && t.manifold);
    }
}

TEST(edit_circular_pocket_rims_round_and_bevel_as_complete_edges) {
    const auto v=vol();
    for(int count:{48,179}) {
        Profile circle;
        for(int i=0;i<count;++i) {
            const double t=6.283185307179586*i/count;
            circle.outer.push_back(gcm::to_lattice(v,2+std::cos(t),2+std::sin(t)));
        }
        auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();
        frame.origin=ggeo::d3::Point<gcm::F64>::from(0,0,1);
        const auto cut=subtract(box(v,0,0,4,4,2),extrude(circle,v,frame,2),v);
        CHECK(cut.ok());
        const auto unrotated=merge_coplanar_faces(cut.body,v);
        for(bool rotated:{false,true}) {
        auto body=unrotated;
        if(rotated) for(auto& p:body.vertex_point) p={p.x,p.z,-p.y};
        CHECK(body.analyse().closed);
        for(bool floor:{false,true}) {
            HalfedgeId edge=kInvalid;
            for(u32 h=0;h<body.halfedge_count();++h) {
                const auto p=body.vertex_point[body.he_vertex[h]];
                double x,y,z;gcm::to_world(v,p,x,y,z);
                if(std::fabs((rotated?y:z)-(floor?1:2))<1e-6 && !circular_edge_ring(body,h,v).empty()) {edge=h;break;}
            }
            CHECK(edge!=kInvalid);if(edge==kInvalid) continue;
            CHECK(circular_edge_ring(body,edge,v).size()==std::size_t(count));
            for(bool round:{false,true}) {
                const auto result=round?fillet_edge_exact(body,edge,0.1,12,v):chamfer_edge(body,edge,0.1,0.1,v);
                CHECK_MSG(result.ok,"%s %s %d: %s",floor?"floor":"mouth",round?"round":"bevel",count,result.reason.c_str());
                if(!result.ok) continue;
                CHECK(result.body.analyse().closed && result.body.analyse().manifold);
                CHECK(all_faces_planar(result.body));
                CHECK((result.volume_after-result.volume_before)*(floor?1:-1)>0);
                CHECK(std::fabs(result.volume_after-result.volume_before)<0.1);
                for(u32 f=0;f<result.body.face_count();++f) {
                    const auto& surface=result.body.face_surface[f];
                    for(u32 l=result.body.face_loop_begin(f);l<result.body.face_loop_end(f);++l)
                        for(u32 h=result.body.loop_begin(l);h<result.body.loop_end(l);++h) {
                            double x,y,z;gcm::to_world(v,result.body.vertex_point[result.body.he_vertex[h]],x,y,z);
                            CHECK(surface.distance_to(x,y,z)<v.inv_scale()*8);
                        }
                }
                if(round) {
                    std::size_t torus=0;for(const auto& surface:result.body.face_surface) if(surface.kind==SurfaceKind::Torus) ++torus;
                    CHECK(torus==std::size_t(count*24));
                }
            }
            const auto too_large=fillet_edge(body,edge,2,12,v);
            CHECK(!too_large.ok);
            CHECK(body.analyse().closed);
        }
        }
    }
}

TEST(edit_nested_pockets_preserve_flat_faces_and_subdivided_circular_rims) {
    const auto v=vol();
    auto solid=box(v,-1,-1,1,1,2);
    double floor=solid.bounds(v).hi.z.to_f64();
    std::vector<double> levels{floor};
    const int counts[]={512,491,122};
    const double radii[]={std::sqrt(.61),std::sqrt(.32),std::sqrt(.02)};
    const double depths[]={.68681501875675377,.54654273060925151,3.1417613082027849};
    for(int pass=0;pass<3;++pass) {
        Profile circle;
        for(int i=0;i<counts[pass];++i) {
            const double t=6.283185307179586*i/counts[pass];
            circle.outer.push_back(gcm::to_lattice(v,radii[pass]*std::cos(t),radii[pass]*std::sin(t)));
        }
        auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();
        frame.origin=ggeo::d3::Point<gcm::F64>::from(0,0,floor-depths[pass]);
        const auto tool=extrude(circle,v,frame,depths[pass]);
        const auto cut=subtract(solid,tool,v);
        CHECK_MSG(cut.ok(),"nested cut %d: %s",pass,cut.message());if(!cut.ok())return;
        solid=merge_coplanar_faces(cut.body,v);
        floor=tool.bounds(v).lo.z.to_f64();if(pass<2)levels.push_back(floor);
        CHECK(solid.analyse().closed && solid.analyse().manifold);
        CHECK(all_faces_planar(solid));
        for(double level:levels) {
            unsigned faces=0;
            for(FaceId f=0;f<solid.face_count();++f) {
                bool on=true;
                for(auto l=solid.face_loop_begin(f);l<solid.face_loop_end(f);++l)
                    for(auto h=solid.loop_begin(l);h<solid.loop_end(l);++h) {
                        double x,y,z;gcm::to_world(v,solid.vertex_point[solid.he_vertex[h]],x,y,z);
                        if(std::fabs(z-level)>v.inv_scale()*.5)on=false;
                    }
                if(on)++faces;
            }
            CHECK_MSG(faces==1,"cut %d: floor %.12g split into %u faces",pass,level,faces);
        }
    }
    for(bool rotated:{false,true}) {
        auto body=solid;
        if(rotated)for(auto& p:body.vertex_point)p={p.z,p.x,p.y};
        unsigned rims=0,subdivided=0;
        for(FaceId f=0;f<body.face_count();++f) {
            const auto plane=face_plane(body,f,v);
            if(std::fabs(rotated?plane.nx:plane.nz)<.999999)continue;
            for(auto l=body.face_loop_begin(f);l<body.face_loop_end(f);++l) {
                const auto edge=body.loop_begin(l),count=body.loop_end(l)-edge;
                if(count<12)continue;
                ++rims;
                if(count>512)++subdivided;
                CHECK_MSG(circular_edge_ring(body,edge,v).size()==count,"missing rim %u/%u",f,l);
                if(l==body.face_loop_begin(f) && body.face_loop_end(f)-l>1) {
                    const auto oversized=blend_circular_edge(body,edge,.55,.02,6,true,v);
                    CHECK(!oversized.ok);
                    CHECK(oversized.reason=="The round or bevel reaches another boundary");
                }
                for(bool round:{false,true}) {
                    const auto result=blend_circular_edge(body,edge,.02,.02,6,round,v);
                    CHECK_MSG(result.ok,"rim %u/%u round %d: %s",f,l,round,result.reason.c_str());
                    if(!result.ok)continue;
                    CHECK(result.body.analyse().closed && result.body.analyse().manifold);
                    CHECK(all_faces_planar(result.body));
                    CHECK(std::fabs(result.volume_after-result.volume_before)<.003);
                    CHECK(std::fabs(result.volume_after-result.volume_before)>.00005);
                    CHECK(result.body.bounds(v).lo.x==body.bounds(v).lo.x);
                    CHECK(result.body.bounds(v).hi.z==body.bounds(v).hi.z);
                    std::size_t blends=0;
                    for(const auto& s:result.body.face_surface)
                        if(s.kind==(round?SurfaceKind::Torus:SurfaceKind::Cone))++blends;
                    CHECK(blends==count*(round?12:2));
                }
            }
        }
        CHECK(rims==6);CHECK(subdivided>=1);
    }
}

TEST(edit_plane_of_a_thin_lattice_triangle_has_no_spurious_tilt) {
    const auto v=vol();Builder builder;
    for(auto p:{gcm::Lattice3{2690239,-4024012,11313749},
                gcm::Lattice3{2690239,-3988827,11365019},
                gcm::Lattice3{2690239,-1388878,15153548}})builder.add_vertex(p);
    builder.begin_face();builder.add_loop({0,1,2});builder.end_face();
    const auto b=builder.build();const auto p=face_plane(b,0,v);
    CHECK(p.valid());CHECK(p.nx==1);CHECK(p.ny==0);CHECK(p.nz==0);
    CHECK(face_planarity(b,0,v)<v.inv_scale()*.01);
}

TEST(edit_concave_straight_round_and_bevel_add_material_in_both_edge_directions) {
    const auto v=vol();
    Profile p;
    for (auto xy : {std::pair{0.,0.}, {4.,0.}, {4.,1.}, {1.,1.}, {1.,4.}, {0.,4.}})
        p.outer.push_back(gcm::to_lattice(v,xy.first,xy.second));
    const Body body=extrude_z(p,v,3);
    HalfedgeId edge=kInvalid;
    for (HalfedgeId h=0;h<body.halfedge_count();++h) {
        double x,y,z,xx,yy,zz;
        gcm::to_world(v,body.vertex_point[body.he_vertex[h]],x,y,z);
        gcm::to_world(v,body.vertex_point[body.he_vertex[body.he_next[h]]],xx,yy,zz);
        if(std::abs(x-1)<1e-6 && std::abs(y-1)<1e-6 &&
           std::abs(xx-1)<1e-6 && std::abs(yy-1)<1e-6) {edge=h;break;}
    }
    CHECK(edge!=kInvalid);if(edge==kInvalid)return;
    const double radius=.25, length=3;
    for(auto h:{edge,body.he_twin[edge]}) {
        CHECK(!edge_is_convex(body,h,v));
        CHECK(!fillet_edge(body,h,3.1,16,v).ok);
        CHECK(!chamfer_edge(body,h,3.1,3.1,v).ok);
        for(bool round:{false,true}) {
            const auto result=round ? fillet_edge_exact(body,h,radius,32,v) :
                                      chamfer_edge(body,h,radius,radius,v);
            CHECK_MSG(result.ok,"%s",result.reason.c_str());if(!result.ok)continue;
            CHECK(result.body.analyse().closed);CHECK(result.body.analyse().manifold);
            const double expected=length*radius*radius*(round ? 1-3.141592653589793/4 : .5);
            CHECK_MSG(std::abs(result.volume_delta()-expected)<2e-4,
                      "inside blend added %g expected %g",result.volume_delta(),expected);
            for(FaceId f=0;f<result.body.face_count();++f) {
                CHECK(face_planarity(result.body,f,v)<1e-6);
                const auto* surf=result.body.surface_of(f);
                if(!round || !surf || surf->kind!=SurfaceKind::Cylinder)continue;
                for(u32 l=result.body.face_loop_begin(f);l<result.body.face_loop_end(f);++l)
                    for(u32 e=result.body.loop_begin(l);e<result.body.loop_end(l);++e) {
                        double x,y,z;gcm::to_world(v,result.body.vertex_point[result.body.he_vertex[e]],x,y,z);
                        CHECK(std::abs(std::hypot(x-1-radius,y-1-radius)-radius)<1e-6);
                        CHECK(std::abs(surf->ox-1-radius)<1e-6);
                        CHECK(std::abs(surf->oy-1-radius)<1e-6);
                    }
            }
        }
    }
}
