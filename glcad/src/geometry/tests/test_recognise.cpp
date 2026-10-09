#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/recognise.hpp"

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

HalfedgeId edge_between(const Body& b, FaceId a, FaceId c) {
    for (u32 h = 0; h < b.halfedge_count(); ++h) {
        FaceId fa, fb;
        if (!faces_of_edge(b, h, fa, fb)) continue;
        if ((fa == a && fb == c) || (fa == c && fb == a)) return h;
    }
    return kInvalid;
}

std::size_t count_of(const std::vector<Feature>& fs, FeatureKind k) {
    std::size_t n = 0;
    for (const auto& f : fs)
        if (f.kind == k) ++n;
    return n;
}

}

TEST(recognise_a_box_falls_into_six_groups) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 3, 4);
    const auto groups = connected_face_groups(b, v);
    CHECK_MSG(groups.size() == 6, "%zu groups on a box", groups.size());
    for (const auto& g : groups) CHECK(g.size() == 1);
}

TEST(recognise_a_faceted_blend_groups_as_one_run) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge(b, e, 0.5, 8, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const auto tight = connected_face_groups(r.body, v, 0.02);
    const auto loose = connected_face_groups(r.body, v, 0.4);
    CHECK_MSG(loose.size() < tight.size(),
              "a looser tolerance found %zu groups against %zu", loose.size(),
              tight.size());

    std::size_t biggest = 0;
    for (const auto& g : loose) biggest = std::max(biggest, g.size());
    CHECK_MSG(biggest >= 8, "the blend's strips did not group (largest %zu)",
              biggest);
}

TEST(recognise_finds_a_fillet_by_its_exact_surface) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 6, 8, 10);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge_exact(b, e, 0.4, 6, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const auto blends = find_blends(r.body, v);
    CHECK_MSG(count_of(blends, FeatureKind::Fillet) > 0, "%zu blends found",
              blends.size());

    for (const auto& f : blends)
        if (f.kind == FeatureKind::Fillet) {
            CHECK_MSG(std::fabs(f.radius - 0.4) < 1e-6, "radius %.9f", f.radius);
            CHECK_MSG(f.certain(), "confidence %.2f", f.confidence);
            CHECK(!f.why.empty());
        }
}

TEST(recognise_finds_a_chamfer_as_a_narrow_planar_face) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = chamfer_edge(b, e, 0.5, 0.5, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    const auto blends = find_blends(r.body, v);
    CHECK_MSG(count_of(blends, FeatureKind::Chamfer) >= 1,
              "%zu blends, %zu chamfers", blends.size(),
              count_of(blends, FeatureKind::Chamfer));

    for (const auto& f : blends)
        if (f.kind == FeatureKind::Chamfer)
            CHECK_MSG(f.confidence < 0.9,
                      "a geometric guess claimed certainty (%.2f)", f.confidence);
}

TEST(recognise_does_not_call_a_whole_wall_a_blend) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    const auto blends = find_blends(b, v);
    CHECK_MSG(blends.empty(), "a plain box produced %zu blends", blends.size());
}

TEST(recognise_blend_search_can_be_switched_off) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = chamfer_edge(b, e, 0.5, 0.5, v);
    CHECK(r.ok);

    RecognitionSettings off;
    off.find_blends = false;
    CHECK(find_blends(r.body, v, off).empty());
}

TEST(recognise_a_blend_narrower_than_the_floor_is_ignored) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge_exact(b, e, 0.4, 6, v);
    CHECK(r.ok);

    RecognitionSettings picky;
    picky.min_fillet_radius = 1.0;
    CHECK_MSG(count_of(find_blends(r.body, v, picky), FeatureKind::Fillet) == 0,
              "a blend under the minimum radius was reported");
}

TEST(recognise_tells_a_hole_from_a_boss) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 6, 8, 10);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge_exact(b, e, 0.4, 6, v);
    CHECK(r.ok);

    const auto holes = find_holes(r.body, v);
    CHECK_MSG(!holes.empty(), "no cylindrical faces were classified");
    for (const auto& h : holes)
        CHECK_MSG(h.kind == FeatureKind::Hole || h.kind == FeatureKind::Boss,
                  "a cylinder was classified as %s",
                  feature_kind_name(h.kind));
}

TEST(recognise_reports_a_holes_radius_and_axis) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 6, 8, 10);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge_exact(b, e, 0.4, 6, v);
    CHECK(r.ok);

    for (const auto& h : find_holes(r.body, v)) {
        CHECK_MSG(h.radius > 0.0, "a %s reported no radius",
                  feature_kind_name(h.kind));
        const f64 len = std::sqrt(h.axis[0] * h.axis[0] + h.axis[1] * h.axis[1] +
                                  h.axis[2] * h.axis[2]);
        CHECK_MSG(std::fabs(len - 1.0) < 1e-9, "the axis is not a unit vector");
    }
}

TEST(recognise_finds_no_holes_in_a_plain_box) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    CHECK(find_holes(b, v).empty());
}

TEST(recognise_reports_how_much_of_the_part_it_understood) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 6, 8, 10);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge_exact(b, e, 0.4, 6, v);
    CHECK(r.ok);

    const RecognitionResult res = recognise(r.body, v);
    CHECK_MSG(res.ok, "%s", res.reason);
    CHECK(res.faces_total == r.body.face_count());
    CHECK_MSG(res.coverage() > 0.0 && res.coverage() <= 1.0,
              "coverage %.3f", res.coverage());
    CHECK(!res.features.empty());
}

TEST(recognise_a_face_has_exactly_one_owner) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 6, 8, 10);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = fillet_edge_exact(b, e, 0.4, 8, v);
    CHECK(r.ok);

    const RecognitionResult res = recognise(r.body, v);
    std::vector<int> owners(r.body.face_count(), 0);
    for (const auto& f : res.features) {
        CHECK_MSG(!f.primary_faces.empty(), "a %s owns nothing",
                  feature_kind_name(f.kind));
        for (FaceId id : f.primary_faces)
            if (id < owners.size()) ++owners[id];
    }

    for (std::size_t i = 0; i < owners.size(); ++i)
        CHECK_MSG(owners[i] <= 1, "face %zu has %d owners", i, owners[i]);

    for (const auto& f : res.features)
        for (FaceId id : f.primary_faces)
            CHECK(std::find(f.faces.begin(), f.faces.end(), id) != f.faces.end());
}

TEST(recognise_every_feature_says_why) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult r = chamfer_edge(b, e, 0.5, 0.5, v);
    CHECK(r.ok);

    for (const auto& f : recognise(r.body, v).features) {
        CHECK_MSG(!f.why.empty(), "a %s gave no evidence",
                  feature_kind_name(f.kind));
        CHECK_MSG(f.confidence > 0.0 && f.confidence <= 1.0,
                  "confidence %.3f", f.confidence);
        CHECK(!f.faces.empty());
    }
}

TEST(recognise_an_empty_body_is_refused) {
    const auto v = vol();
    const RecognitionResult r = recognise(Body{}, v);
    CHECK(!r.ok);
    CHECK(std::string(r.reason).size() > 0);
    CHECK(r.coverage() == 0.0);
}

TEST(recognise_every_kind_is_named) {
    for (int i = 0; i <= static_cast<int>(FeatureKind::PlanarFace); ++i)
        CHECK_MSG(
            std::string(feature_kind_name(static_cast<FeatureKind>(i))).size() > 0,
            "kind %d has no name", i);
}
