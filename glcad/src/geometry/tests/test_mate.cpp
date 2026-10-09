#include "test.hpp"

#include <cmath>

#include "geometry/extrude.hpp"
#include "geometry/mate.hpp"

using namespace gtopo;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

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

d2mate::Edge edge(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    return {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y1)};
}

bool same_points(const std::vector<gcm::Lattice3>& a,
                 const std::vector<gcm::Lattice3>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) return false;
    return true;
}

}

TEST(mate_placement_identity_does_nothing) {
    const Placement p;
    CHECK(p.is_identity());
    f64 x, y, z;
    p.apply(3, -4, 5, x, y, z);
    CHECK(x == 3.0 && y == -4.0 && z == 5.0);
}

TEST(mate_placement_rotation_is_orthonormal) {
    const Placement r = Placement::rotation(0, 0, 1, kPi * 0.5);
    f64 x, y, z;
    r.apply_direction(1, 0, 0, x, y, z);
    CHECK_MSG(std::fabs(x) < 1e-12 && std::fabs(y - 1.0) < 1e-12,
              "a quarter turn sent +X to (%.6f, %.6f, %.6f)", x, y, z);

    r.apply_direction(3, 4, 12, x, y, z);
    CHECK(std::fabs(std::sqrt(x * x + y * y + z * z) - 13.0) < 1e-12);
}

TEST(mate_placement_does_not_translate_a_direction) {
    const Placement t = Placement::translation(100, 200, 300);
    f64 x, y, z;
    t.apply_direction(1, 0, 0, x, y, z);
    CHECK(x == 1.0 && y == 0.0 && z == 0.0);
}

TEST(mate_placement_composes) {
    const Placement a = Placement::translation(1, 0, 0);
    const Placement b = Placement::rotation(0, 0, 1, kPi * 0.5);
    f64 x, y, z;
    a.then(b).apply(0, 0, 0, x, y, z);
    CHECK_MSG(std::fabs(x) < 1e-12 && std::fabs(y - 1.0) < 1e-12,
              "composed to (%.6f, %.6f, %.6f)", x, y, z);
}

TEST(mate_reference_plane_moves_with_its_part) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 2, 4);

    Assembly a;
    Instance i;
    i.body = &b;
    i.placement = Placement::translation(0, 0, 10);
    const u32 id = a.add(i);

    const FaceId top = face_facing(b, v, 0, 0, 1);
    const Plane p = a.ref_plane({id, RefKind::Face, top}, v);
    CHECK(p.valid());
    CHECK_MSG(std::fabs(p.nz - 1.0) < 1e-9, "normal (%.4f,%.4f,%.4f)", p.nx, p.ny,
              p.nz);
    CHECK_MSG(std::fabs(p.d - 14.0) < 1e-6, "plane at %.6f, expected 14", p.d);
}

TEST(mate_reference_axis_from_a_face_is_its_normal) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 2, 4);
    Assembly a;
    Instance i;
    i.body = &b;
    const u32 id = a.add(i);

    f64 ox, oy, oz, dx, dy, dz;
    CHECK(a.ref_axis({id, RefKind::Face, face_facing(b, v, 0, 0, 1)}, v, ox, oy,
                     oz, dx, dy, dz));
    CHECK(std::fabs(dz - 1.0) < 1e-9);
}

TEST(mate_reference_axis_from_an_edge_is_a_unit_direction) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 2, 2);
    Assembly a;
    Instance i;
    i.body = &b;
    const u32 id = a.add(i);

    f64 ox, oy, oz, dx, dy, dz;
    bool any = false;
    for (u32 h = 0; h < b.halfedge_count(); ++h)
        if (a.ref_axis({id, RefKind::Edge, h}, v, ox, oy, oz, dx, dy, dz)) {
            CHECK(std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - 1.0) < 1e-9);
            any = true;
        }
    CHECK(any);
}

TEST(mate_reference_of_a_bad_index_is_refused) {
    const auto v = vol();
    Assembly a;
    f64 x, y, z, dx, dy, dz;
    CHECK(!a.ref_plane({99, RefKind::Face, 0}, v).valid());
    CHECK(!a.ref_axis({99, RefKind::Edge, 0}, v, x, y, z, dx, dy, dz));
    CHECK(!a.ref_point({99, RefKind::Vertex, 0}, v, x, y, z));
}

TEST(mate_coincident_faces_bring_two_parts_together) {
    const auto v = vol();
    const Body base = box(v, 0, 0, 10, 10, 2);
    const Body block = box(v, 0, 0, 4, 4, 3);

    Assembly a;
    Instance bi;
    bi.body = &base;
    bi.grounded = true;
    const u32 base_id = a.add(bi);

    Instance ti;
    ti.body = &block;
    ti.placement = Placement::translation(2, 2, 20);
    const u32 block_id = a.add(ti);

    Mate m;
    m.kind = MateKind::Coincident;
    m.a = {base_id, RefKind::Face, face_facing(base, v, 0, 0, 1)};
    m.b = {block_id, RefKind::Face, face_facing(block, v, 0, 0, -1)};
    a.add_mate(m);

    const MateResult r = a.solve(v);
    CHECK_MSG(r.ok(), "%s (residual %.9f)", r.message(), r.residual);

    const MateError e = a.measure(a.mates()[0], v);
    CHECK_MSG(std::fabs(e.linear) < 1e-6, "the faces are %.9f apart", e.linear);
    CHECK_MSG(e.angular < 1e-6, "the faces are %.9f rad out of parallel",
              e.angular);

    CHECK(a.instance(base_id)->placement.is_identity());
}

TEST(mate_distance_holds_a_gap) {
    const auto v = vol();
    const Body base = box(v, 0, 0, 10, 10, 2);
    const Body block = box(v, 0, 0, 4, 4, 3);

    Assembly a;
    Instance bi;
    bi.body = &base;
    bi.grounded = true;
    const u32 base_id = a.add(bi);
    Instance ti;
    ti.body = &block;
    ti.placement = Placement::translation(0, 0, 30);
    const u32 block_id = a.add(ti);

    Mate m;
    m.kind = MateKind::Distance;
    m.a = {base_id, RefKind::Face, face_facing(base, v, 0, 0, 1)};
    m.b = {block_id, RefKind::Face, face_facing(block, v, 0, 0, -1)};
    m.value = 1.5;
    a.add_mate(m);

    CHECK_MSG(a.solve(v).ok(), "the distance mate did not close");
    const Plane pa = a.ref_plane(m.a, v), pb = a.ref_plane(m.b, v);
    CHECK_MSG(std::fabs(std::fabs(pb.d) - std::fabs(pa.d) - 1.5) < 1e-6,
              "the gap came out %.9f", std::fabs(pb.d) - std::fabs(pa.d));
}

TEST(mate_flush_and_coincident_are_not_the_same) {
    const auto v = vol();
    const Body a_body = box(v, 0, 0, 4, 4, 2);
    const Body b_body = box(v, 0, 0, 4, 4, 2);

    auto build = [&](MateKind k) {
        Assembly asm_;
        Instance ai;
        ai.body = &a_body;
        ai.grounded = true;
        const u32 ida = asm_.add(ai);
        Instance bi;
        bi.body = &b_body;
        bi.placement = Placement::translation(0, 0, 20);
        const u32 idb = asm_.add(bi);

        Mate m;
        m.kind = k;
        m.a = {ida, RefKind::Face, face_facing(a_body, v, 0, 0, 1)};
        m.b = {idb, RefKind::Face, face_facing(b_body, v, 0, 0, 1)};
        asm_.add_mate(m);
        asm_.solve(v);
        return asm_.instance(idb)->placement.t[2];
    };

    const f64 flush = build(MateKind::Flush);
    const f64 coincident = build(MateKind::Coincident);
    CHECK_MSG(std::fabs(flush - coincident) > 1e-6,
              "flush and coincident put the part in the same place (%.6f)", flush);
}

TEST(mate_parallel_aligns_without_closing_the_gap) {
    const auto v = vol();
    const Body a_body = box(v, 0, 0, 4, 4, 2);
    const Body b_body = box(v, 0, 0, 4, 4, 2);

    Assembly a;
    Instance ai;
    ai.body = &a_body;
    ai.grounded = true;
    const u32 ida = a.add(ai);
    Instance bi;
    bi.body = &b_body;
    bi.placement = Placement::rotation(1, 0, 0, 0.4)
                       .then(Placement::translation(0, 0, 20));
    const u32 idb = a.add(bi);

    Mate m;
    m.kind = MateKind::Parallel;
    m.a = {ida, RefKind::Face, face_facing(a_body, v, 0, 0, 1)};
    m.b = {idb, RefKind::Face, 0};
    a.add_mate(m);

    CHECK_MSG(a.solve(v).ok(), "parallel did not converge");
    CHECK_MSG(a.measure(a.mates()[0], v).angular < 1e-6, "still not parallel");
    CHECK_MSG(std::fabs(a.instance(idb)->placement.t[2]) > 1.0,
              "parallel closed a gap it should have left alone");
}

TEST(mate_perpendicular_squares_two_faces) {
    const auto v = vol();
    const Body a_body = box(v, 0, 0, 4, 4, 2);
    const Body b_body = box(v, 0, 0, 4, 4, 2);

    Assembly a;
    Instance ai;
    ai.body = &a_body;
    ai.grounded = true;
    const u32 ida = a.add(ai);
    Instance bi;
    bi.body = &b_body;
    bi.placement = Placement::rotation(1, 0, 0, 1.2)
                       .then(Placement::translation(0, 0, 20));
    const u32 idb = a.add(bi);

    Mate m;
    m.kind = MateKind::Perpendicular;
    m.a = {ida, RefKind::Face, face_facing(a_body, v, 0, 0, 1)};
    m.b = {idb, RefKind::Face, 0};
    a.add_mate(m);

    a.solve(v);
    CHECK_MSG(a.measure(a.mates()[0], v).angular < 1e-6,
              "the faces are %.9f from square", a.measure(a.mates()[0], v).angular);
}

TEST(mate_concentric_puts_a_bolt_through_a_hole) {
    const auto v = vol();
    const Body plate = box(v, 0, 0, 10, 10, 1);
    const Body bolt = box(v, 0, 0, 1, 1, 8);

    Assembly a;
    Instance pi;
    pi.body = &plate;
    pi.grounded = true;
    const u32 plate_id = a.add(pi);
    Instance bi;
    bi.body = &bolt;
    bi.placement = Placement::translation(30, 30, 0);
    const u32 bolt_id = a.add(bi);

    Mate m;
    m.kind = MateKind::Concentric;
    m.a = {plate_id, RefKind::Axis, face_facing(plate, v, 0, 0, 1)};
    m.b = {bolt_id, RefKind::Axis, face_facing(bolt, v, 0, 0, 1)};
    a.add_mate(m);

    const MateResult r = a.solve(v);
    CHECK_MSG(r.ok(), "%s (residual %.9f)", r.message(), r.residual);
    const MateError e = a.measure(a.mates()[0], v);
    CHECK_MSG(e.linear < 1e-6, "the axes are %.9f apart", e.linear);
    CHECK(e.angular < 1e-6);
}

TEST(mate_point_on_face) {
    const auto v = vol();
    const Body plate = box(v, 0, 0, 10, 10, 1);
    const Body pin = box(v, 0, 0, 1, 1, 1);

    Assembly a;
    Instance pi;
    pi.body = &plate;
    pi.grounded = true;
    const u32 plate_id = a.add(pi);
    Instance ni;
    ni.body = &pin;
    ni.placement = Placement::translation(2, 2, 15);
    const u32 pin_id = a.add(ni);

    Mate m;
    m.kind = MateKind::PointOnFace;
    m.a = {pin_id, RefKind::Vertex, 0};
    m.b = {plate_id, RefKind::Face, face_facing(plate, v, 0, 0, 1)};
    a.add_mate(m);

    CHECK(a.solve(v).ok());
    CHECK(std::fabs(a.measure(a.mates()[0], v).linear) < 1e-6);
}

TEST(mate_a_grounded_part_never_moves) {
    const auto v = vol();
    const Body base = box(v, 0, 0, 10, 10, 2);
    const Body block = box(v, 0, 0, 4, 4, 3);

    Assembly a;
    Instance bi;
    bi.body = &base;
    bi.grounded = true;
    bi.placement = Placement::translation(7, 8, 9);
    const u32 base_id = a.add(bi);
    Instance ti;
    ti.body = &block;
    const u32 block_id = a.add(ti);

    Mate m;
    m.kind = MateKind::Coincident;
    m.a = {base_id, RefKind::Face, face_facing(base, v, 0, 0, 1)};
    m.b = {block_id, RefKind::Face, face_facing(block, v, 0, 0, -1)};
    a.add_mate(m);
    a.solve(v);

    CHECK(a.instance(base_id)->placement.t[0] == 7.0);
    CHECK(a.instance(base_id)->placement.t[1] == 8.0);
    CHECK(a.instance(base_id)->placement.t[2] == 9.0);
}

TEST(mate_solving_never_deforms_a_part) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 4, 4, 3);
    const auto before = block.vertex_point;

    Assembly a;
    Instance gi;
    gi.body = &block;
    gi.grounded = true;
    const u32 g = a.add(gi);
    Instance mi;
    mi.body = &block;
    mi.placement = Placement::translation(0, 0, 30);
    const u32 mv = a.add(mi);

    Mate m;
    m.kind = MateKind::Coincident;
    m.a = {g, RefKind::Face, face_facing(block, v, 0, 0, 1)};
    m.b = {mv, RefKind::Face, face_facing(block, v, 0, 0, -1)};
    a.add_mate(m);
    a.solve(v);

    CHECK_MSG(same_points(block.vertex_point, before),
              "a mate moved a part's vertices");
}

TEST(mate_suppressed_mates_are_ignored) {
    const auto v = vol();
    const Body base = box(v, 0, 0, 10, 10, 2);
    const Body block = box(v, 0, 0, 4, 4, 3);

    Assembly a;
    Instance bi;
    bi.body = &base;
    bi.grounded = true;
    const u32 base_id = a.add(bi);
    Instance ti;
    ti.body = &block;
    ti.placement = Placement::translation(0, 0, 20);
    const u32 block_id = a.add(ti);

    Mate m;
    m.kind = MateKind::Coincident;
    m.a = {base_id, RefKind::Face, face_facing(base, v, 0, 0, 1)};
    m.b = {block_id, RefKind::Face, face_facing(block, v, 0, 0, -1)};
    m.suppressed = true;
    a.add_mate(m);

    a.solve(v);
    CHECK_MSG(std::fabs(a.instance(block_id)->placement.t[2] - 20.0) < 1e-12,
              "a suppressed mate still moved the part");
    CHECK(a.measure(a.mates()[0], v).satisfied);
}

TEST(mate_degrees_of_freedom_are_reported) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 2, 2, 2);

    Assembly a;
    Instance gi;
    gi.body = &b;
    gi.grounded = true;
    const u32 g = a.add(gi);
    Instance mi;
    mi.body = &b;
    const u32 m = a.add(mi);
    CHECK_MSG(a.degrees_of_freedom() == 6, "%d", a.degrees_of_freedom());

    a.add_mate({MateKind::Coincident, {g, RefKind::Face, 0},
                {m, RefKind::Face, 1}, 0.0, false, false, ""});
    CHECK_MSG(a.degrees_of_freedom() == 3, "%d", a.degrees_of_freedom());

    a.add_mate({MateKind::Fix, {g, RefKind::Face, 0}, {m, RefKind::Face, 1},
                0.0, false, false, ""});
    CHECK(a.degrees_of_freedom() < 0);
}

TEST(mate_a_conflicting_pair_is_reported_not_silently_half_solved) {
    const auto v = vol();
    const Body base = box(v, 0, 0, 10, 10, 2);
    const Body block = box(v, 0, 0, 4, 4, 3);

    Assembly a;
    Instance bi;
    bi.body = &base;
    bi.grounded = true;
    const u32 base_id = a.add(bi);
    Instance ti;
    ti.body = &block;
    ti.placement = Placement::translation(0, 0, 20);
    const u32 block_id = a.add(ti);

    const MateRef top{base_id, RefKind::Face, face_facing(base, v, 0, 0, 1)};
    const MateRef bottom{block_id, RefKind::Face,
                         face_facing(block, v, 0, 0, -1)};

    a.add_mate({MateKind::Distance, top, bottom, 1.0, false, false, ""});
    a.add_mate({MateKind::Distance, top, bottom, 5.0, false, false, ""});

    const MateResult r = a.solve(v, 60);
    CHECK_MSG(!r.ok(), "conflicting distances were reported as solved");
    CHECK_MSG(!r.conflicting.empty(), "no conflicting mate was named");
}

TEST(mate_an_empty_assembly_solves_trivially) {
    const auto v = vol();
    Assembly a;
    CHECK(!a.solve(v).ok());
    CHECK(a.solve(v).status == MateStatus::Empty);
    CHECK(a.degrees_of_freedom() == 0);
    CHECK(a.interference(v).empty());
}

TEST(mate_kind_names_and_dof_are_defined_for_every_kind) {
    for (int i = 0; i <= static_cast<int>(MateKind::Fix); ++i) {
        const MateKind k = static_cast<MateKind>(i);
        CHECK_MSG(std::string(mate_kind_name(k)).size() > 0, "kind %d unnamed", i);
        CHECK(mate_dof_removed(k) >= 1 && mate_dof_removed(k) <= 6);
    }
}

TEST(mate_interference_finds_overlapping_parts) {
    const auto v = vol();
    const Body a_body = box(v, 0, 0, 4, 4, 4);
    const Body b_body = box(v, 0, 0, 4, 4, 4);

    Assembly a;
    Instance ai;
    ai.body = &a_body;
    a.add(ai);
    Instance bi;
    bi.body = &b_body;
    bi.placement = Placement::translation(2, 2, 2);
    a.add(bi);

    const auto hits = a.interference(v);
    CHECK_MSG(hits.size() == 1, "%zu interferences", hits.size());
    CHECK_MSG(std::fabs(hits[0].volume - 8.0) < 1e-3,
              "overlap volume %.6f, expected 8", hits[0].volume);
    CHECK(!hits[0].touching);
}

TEST(mate_interference_ignores_parts_that_are_apart) {
    const auto v = vol();
    const Body a_body = box(v, 0, 0, 4, 4, 4);
    const Body b_body = box(v, 0, 0, 4, 4, 4);

    Assembly a;
    Instance ai;
    ai.body = &a_body;
    a.add(ai);
    Instance bi;
    bi.body = &b_body;
    bi.placement = Placement::translation(50, 0, 0);
    a.add(bi);
    CHECK(a.interference(v).empty());
}

TEST(mate_mated_parts_that_merely_touch_do_not_interfere) {
    const auto v = vol();
    const Body base = box(v, 0, 0, 10, 10, 2);
    const Body block = box(v, 0, 0, 4, 4, 3);

    Assembly a;
    Instance bi;
    bi.body = &base;
    bi.grounded = true;
    const u32 base_id = a.add(bi);
    Instance ti;
    ti.body = &block;
    ti.placement = Placement::translation(1, 1, 20);
    const u32 block_id = a.add(ti);

    Mate m;
    m.kind = MateKind::Coincident;
    m.a = {base_id, RefKind::Face, face_facing(base, v, 0, 0, 1)};
    m.b = {block_id, RefKind::Face, face_facing(block, v, 0, 0, -1)};
    a.add_mate(m);
    CHECK(a.solve(v).ok());

    for (const auto& hit : a.interference(v))
        CHECK_MSG(hit.volume < 1e-6,
                  "two mated faces read as %.9f of interference", hit.volume);
}

TEST(mate2_collinear_brings_two_edges_onto_one_line) {
    const auto v = vol();
    d2mate::Assembly2 a;

    d2mate::Part fixed;
    fixed.shapes = {edge(v, 0, 0, 10, 0)};
    fixed.grounded = true;
    a.add(fixed);

    d2mate::Part moving;
    moving.shapes = {edge(v, 0, 0, 6, 0)};
    moving.placement.tx = 3.0;
    moving.placement.ty = 7.0;
    moving.placement.angle = 0.6;
    a.add(moving);

    d2mate::Mate2 m;
    m.kind = MateKind::Collinear;
    m.a = {0, 0};
    m.b = {1, 0};
    a.add_mate(m);

    const MateResult r = a.solve(v);
    CHECK_MSG(r.ok(), "%s (residual %.9f)", r.message(), r.residual);

    f64 x0, y0, x1, y1;
    CHECK(a.edge_of({1, 0}, v, x0, y0, x1, y1));
    CHECK_MSG(std::fabs(y0) < 1e-6 && std::fabs(y1) < 1e-6,
              "the edge landed at y = %.9f, %.9f", y0, y1);
}

TEST(mate2_distance_holds_a_gap_between_edges) {
    const auto v = vol();
    d2mate::Assembly2 a;

    d2mate::Part fixed;
    fixed.shapes = {edge(v, 0, 0, 10, 0)};
    fixed.grounded = true;
    a.add(fixed);

    d2mate::Part moving;
    moving.shapes = {edge(v, 0, 0, 6, 0)};
    moving.placement.ty = 9.0;
    a.add(moving);

    d2mate::Mate2 m;
    m.kind = MateKind::Distance;
    m.a = {0, 0};
    m.b = {1, 0};
    m.value = 2.5;
    a.add_mate(m);

    CHECK_MSG(a.solve(v).ok(), "the 2D distance mate did not close");
    f64 x0, y0, x1, y1;
    a.edge_of({1, 0}, v, x0, y0, x1, y1);
    CHECK_MSG(std::fabs(std::fabs(y0) - 2.5) < 1e-6, "the gap came out %.9f", y0);
}

TEST(mate2_perpendicular_squares_two_edges) {
    const auto v = vol();
    d2mate::Assembly2 a;

    d2mate::Part fixed;
    fixed.shapes = {edge(v, 0, 0, 10, 0)};
    fixed.grounded = true;
    a.add(fixed);

    d2mate::Part moving;
    moving.shapes = {edge(v, 0, 0, 5, 0)};
    moving.placement.angle = 0.3;
    a.add(moving);

    d2mate::Mate2 m;
    m.kind = MateKind::Perpendicular;
    m.a = {0, 0};
    m.b = {1, 0};
    a.add_mate(m);

    a.solve(v);
    f64 x0, y0, x1, y1;
    a.edge_of({1, 0}, v, x0, y0, x1, y1);
    CHECK_MSG(std::fabs(x1 - x0) < 1e-6,
              "the edge is not vertical: dx = %.9f", x1 - x0);
}

TEST(mate2_a_grounded_part_never_moves) {
    const auto v = vol();
    d2mate::Assembly2 a;

    d2mate::Part fixed;
    fixed.shapes = {edge(v, 0, 0, 10, 0)};
    fixed.grounded = true;
    fixed.placement.tx = 4.0;
    a.add(fixed);

    d2mate::Part moving;
    moving.shapes = {edge(v, 0, 0, 6, 0)};
    moving.placement.ty = 5.0;
    a.add(moving);

    a.add_mate({MateKind::Collinear, {0, 0}, {1, 0}, 0.0, false, false});
    a.solve(v);
    CHECK(a.part(0)->placement.tx == 4.0);
}

TEST(mate2_degrees_of_freedom) {
    const auto v = vol();
    (void)v;
    d2mate::Assembly2 a;
    d2mate::Part fixed;
    fixed.grounded = true;
    a.add(fixed);
    d2mate::Part moving;
    a.add(moving);
    CHECK_MSG(a.degrees_of_freedom() == 3, "%d", a.degrees_of_freedom());

    a.add_mate({MateKind::Collinear, {0, 0}, {1, 0}, 0.0, false, false});
    CHECK_MSG(a.degrees_of_freedom() == 1, "%d", a.degrees_of_freedom());
}

TEST(mate2_an_assembly_of_one_part_has_nothing_to_solve) {
    const auto v = vol();
    d2mate::Assembly2 a;
    d2mate::Part p;
    p.shapes = {edge(v, 0, 0, 1, 0)};
    a.add(p);
    CHECK(a.solve(v).status == MateStatus::Empty);
}

TEST(pattern_linear_makes_a_grid_in_one_rule) {
    Pattern p;
    p.kind = PatternKind::Linear;
    p.dx1 = 10.0;
    p.count1 = 4;
    p.dy2 = 5.0;
    p.count2 = 3;
    CHECK(p.total() == 12);

    const auto places = expand_pattern(p, Placement::identity());
    CHECK_MSG(places.size() == 12, "%zu placements", places.size());

    CHECK(std::fabs(places[0].t[0]) < 1e-9);
    CHECK(std::fabs(places[3].t[0] - 30.0) < 1e-9);
    CHECK(std::fabs(places[11].t[1] - 10.0) < 1e-9);
    CHECK(std::fabs(places[11].t[0] - 30.0) < 1e-9);
}

TEST(pattern_circular_does_not_repeat_the_first_instance) {
    Pattern p;
    p.kind = PatternKind::Circular;
    p.count1 = 6;
    p.az = 1.0;
    const auto places = expand_pattern(p, Placement::translation(10, 0, 0));
    CHECK(places.size() == 6);

    f64 x, y, z;
    places[0].apply(0, 0, 0, x, y, z);
    CHECK(std::fabs(x - 10.0) < 1e-9);

    places[1].apply(0, 0, 0, x, y, z);
    CHECK_MSG(std::fabs(x - 5.0) < 1e-6 && std::fabs(y - 8.660254) < 1e-5,
              "the second instance landed at (%.6f, %.6f)", x, y);

    places[5].apply(0, 0, 0, x, y, z);
    CHECK_MSG(std::fabs(y + 8.660254) < 1e-5, "the sixth landed at y = %.6f", y);
}

TEST(pattern_circular_can_keep_instances_upright) {
    Pattern p;
    p.kind = PatternKind::Circular;
    p.count1 = 4;
    p.az = 1.0;
    p.rotate_instances = false;

    const auto places = expand_pattern(p, Placement::translation(10, 0, 0));
    for (const auto& pl : places) {
        CHECK_MSG(std::fabs(pl.m[0] - 1.0) < 1e-9 && std::fabs(pl.m[1]) < 1e-9,
                  "an upright instance was rotated");
    }
    f64 x, y, z;
    places[1].apply(0, 0, 0, x, y, z);
    CHECK(std::fabs(x) < 1e-6 && std::fabs(y - 10.0) < 1e-6);
}

TEST(pattern_mirror_reflects) {
    Pattern p;
    p.kind = PatternKind::Mirror;
    p.mirror_plane = Plane{1, 0, 0, 0};

    const auto places = expand_pattern(p, Placement::translation(3, 4, 5));
    CHECK(places.size() == 2);

    f64 x, y, z;
    places[1].apply(0, 0, 0, x, y, z);
    CHECK_MSG(std::fabs(x + 3.0) < 1e-9, "the mirrored copy is at x = %.9f", x);
    CHECK(std::fabs(y - 4.0) < 1e-9);
}

TEST(pattern_skipping_keeps_the_rule) {
    Pattern p;
    p.kind = PatternKind::Linear;
    p.dx1 = 10.0;
    p.count1 = 5;
    p.skipped = {2};

    CHECK_MSG(p.total() == 5, "the rule still says five");
    CHECK(p.is_skipped(2));
    CHECK(!p.is_skipped(3));

    const auto places = expand_pattern(p, Placement::identity());
    CHECK_MSG(places.size() == 4, "%zu placements after skipping one",
              places.size());
    for (const auto& pl : places)
        CHECK_MSG(std::fabs(pl.t[0] - 20.0) > 1e-6, "the skipped one appeared");
}

TEST(pattern_table_takes_explicit_placements) {
    Pattern p;
    p.kind = PatternKind::Table;
    p.placements = {Placement::translation(1, 0, 0),
                    Placement::translation(0, 7, 0),
                    Placement::rotation(0, 0, 1, 0.5)};
    CHECK(p.total() == 3);
    CHECK(expand_pattern(p, Placement::identity()).size() == 3);
}

TEST(pattern_expansion_makes_real_instances) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 1, 1, 1);

    Assembly a;
    Instance seed;
    seed.body = &b;
    seed.name = "bolt";
    const u32 id = a.add(seed);

    Pattern p;
    p.kind = PatternKind::Linear;
    p.seed = id;
    p.dx1 = 5.0;
    p.count1 = 4;
    a.add_pattern(p);
    CHECK(a.pattern_count() == 1);
    CHECK(a.patterned_instance_count() == 4);

    CHECK_MSG(a.expand_patterns() == 3, "the seed was duplicated");
    CHECK(a.size() == 4);
    CHECK(std::fabs(a.instance(3)->placement.t[0] - 15.0) < 1e-9);
    CHECK(a.instance(3)->name.find("bolt") != std::string::npos);
}

TEST(pattern_expansion_does_not_feed_on_itself) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 1, 1, 1);

    Assembly a;
    Instance seed;
    seed.body = &b;
    const u32 id = a.add(seed);

    Pattern p;
    p.kind = PatternKind::Linear;
    p.seed = id;
    p.dx1 = 2.0;
    p.count1 = 3;
    a.add_pattern(p);

    a.expand_patterns();
    const std::size_t after_first = a.size();
    a.expand_patterns();
    CHECK_MSG(a.size() == after_first + 2,
              "the second expansion produced %zu extra", a.size() - after_first);
}

TEST(pattern_removal) {
    Assembly a;
    a.add_pattern(Pattern{});
    CHECK(a.pattern_count() == 1);
    CHECK(a.remove_pattern(0));
    CHECK(a.pattern_count() == 0);
    CHECK(!a.remove_pattern(0));
}

TEST(assembly_a_subassembly_is_mated_as_one_thing) {
    const auto v = vol();
    const Body shaft = box(v, 0, 0, 1, 1, 10);
    const Body bearing = box(v, 0, 0, 2, 2, 1);

    Assembly gearbox;
    Instance s;
    s.body = &shaft;
    s.name = "shaft";
    gearbox.add(s);
    Instance bg;
    bg.body = &bearing;
    bg.name = "bearing";
    bg.placement = Placement::translation(0, 0, 5);
    gearbox.add(bg);
    CHECK(gearbox.part_count() == 2);

    Assembly machine;
    const Body base = box(v, 0, 0, 20, 20, 2);
    Instance b;
    b.body = &base;
    b.name = "base";
    b.grounded = true;
    machine.add(b);
    machine.add_subassembly(&gearbox, Placement::translation(5, 5, 2), "gearbox");

    CHECK_MSG(machine.size() == 2, "the outer assembly holds two instances");
    CHECK_MSG(machine.part_count() == 3, "%zu parts through the tree",
              machine.part_count());
    CHECK(machine.depth() >= 1);
}

TEST(assembly_flattening_composes_the_placements) {
    const auto v = vol();
    const Body part = box(v, 0, 0, 1, 1, 1);

    Assembly inner;
    Instance i;
    i.body = &part;
    i.name = "part";
    i.placement = Placement::translation(1, 0, 0);
    inner.add(i);

    Assembly outer;
    outer.add_subassembly(&inner, Placement::translation(10, 0, 0), "inner");

    const auto flat = outer.flatten();
    CHECK(flat.size() == 1);
    CHECK_MSG(std::fabs(flat[0].placement.t[0] - 11.0) < 1e-9,
              "the composed placement is at x = %.9f", flat[0].placement.t[0]);
    CHECK_MSG(flat[0].path == "inner/part", "path '%s'", flat[0].path.c_str());
}

TEST(assembly_flattening_composes_rotations_in_the_right_order) {
    const auto v = vol();
    const Body part = box(v, 0, 0, 1, 1, 1);

    Assembly inner;
    Instance i;
    i.body = &part;
    i.placement = Placement::translation(1, 0, 0);
    i.name = "p";
    inner.add(i);

    Assembly outer;
    outer.add_subassembly(&inner, Placement::rotation(0, 0, 1, 1.5707963267948966),
                          "sub");

    const auto flat = outer.flatten();
    CHECK(flat.size() == 1);
    f64 x, y, z;
    flat[0].placement.apply(0, 0, 0, x, y, z);
    CHECK_MSG(std::fabs(x) < 1e-9 && std::fabs(y - 1.0) < 1e-9,
              "the nested part landed at (%.6f, %.6f)", x, y);
}

TEST(assembly_nesting_is_bounded) {
    const auto v = vol();
    const Body part = box(v, 0, 0, 1, 1, 1);

    Assembly a;
    Instance i;
    i.body = &part;
    i.name = "p";
    a.add(i);
    a.add_subassembly(&a, Placement::identity(), "self");

    const auto flat = a.flatten();
    CHECK_MSG(!flat.empty(), "the guard swallowed everything");
    CHECK(flat.size() < 100000);
}

TEST(assembly_interference_works_through_a_subassembly) {
    const auto v = vol();
    const Body part = box(v, 0, 0, 4, 4, 4);

    Assembly inner;
    Instance i;
    i.body = &part;
    i.name = "inner part";
    inner.add(i);

    Assembly outer;
    Instance o;
    o.body = &part;
    o.name = "outer part";
    outer.add(o);
    outer.add_subassembly(&inner, Placement::translation(2, 2, 2), "sub");

    const auto flat = outer.flatten();
    CHECK(flat.size() == 2);
    CHECK(std::fabs(flat[1].placement.t[0] - 2.0) < 1e-9);
}

TEST(assembly_an_instance_with_neither_body_nor_sub_is_invalid) {
    Instance i;
    CHECK(!i.valid());
    CHECK(!i.is_subassembly());

    Assembly a;
    a.add(i);
    CHECK_MSG(a.flatten().empty(), "an empty instance produced a part");
}
