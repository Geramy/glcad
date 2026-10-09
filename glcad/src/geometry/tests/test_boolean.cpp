#include "test.hpp"

#include <cmath>
#include <array>

#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/triangulate.hpp"

using namespace gtopo;

static gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

static Loop2 rect(const gcm::WorkingVolume& v, double x0, double y0,
                  double x1, double y1) {
    return {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
            gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
}

static Body box(const gcm::WorkingVolume& v, double x0, double y0, double x1,
                double y1, double h, double z0 = 0.0) {
    Profile p;
    p.outer = rect(v, x0, y0, x1, y1);
    p.normalize();
    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
    return extrude(p, v, frame, h);
}

TEST(dbg_tmp_circle_bool) {
    const auto v = vol();
    const Body solid = box(v, 0, 0, 4, 4, 2);
    for (int steps : {36}) {
        Loop2 loop;
        for (int i = 0; i < steps; ++i) {
            const double t = 2.0 * 3.14159265358979323846 * (double(i) / steps);
            loop.push_back(gcm::to_lattice(v, 2.0 + std::cos(t), 2.0 + std::sin(t)));
        }
        Profile p;
        p.outer = loop;
        p.normalize();
        ggeo::d3::Frame<gcm::F64> frame = ggeo::d3::Frame<gcm::F64>::world_xy();
        frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, 2);
        const Body made = extrude(p, v, frame, 0.5);
        ggeo::d3::Frame<gcm::F64> back = frame;
        back.v = back.v * gcm::F64::from(-1.0);
        back.n = back.n * gcm::F64::from(-1.0);
        Profile pin;
        auto reproj = [&](const Loop2& in) {
            Loop2 out;
            for (const auto& q : in) {
                double x, y;
                gcm::to_world(v, q, x, y);
                out.push_back(gcm::to_lattice(v, x, -y));
            }
            return out;
        };
        pin.outer = reproj(p.outer);
        pin.normalize();
        const Body made_in = extrude(pin, v, back, 0.5);
        const auto u = unite(solid, made, v);
        const auto s = subtract(solid, made_in, v);
        std::fprintf(stderr, "DBG steps=%d prism=%.4f prism_in_signed=%.4f unite=%.4f cut=%.4f closed=%d\n",
                     steps, volume(made, v), volume(made_in, v),
                     u.ok() ? volume(u.body, v) : -1.0,
                     s.ok() ? volume(s.body, v) : -1.0,
                     s.ok() ? (int)s.body.analyse().closed : -1);
        if ((steps == 12 || steps == 36) && s.ok()) {
            int dang = 0;
            for (std::size_t h = 0; h < s.body.halfedge_count(); ++h) {
                if (s.body.he_twin[h] >= s.body.halfedge_count()) {
                    if (dang < 8) {
                        const auto& a = s.body.vertex_point[s.body.he_vertex[h]];
                        std::fprintf(stderr, "DBG dang: (%lld,%lld,%lld) face-loop\n", (long long)a.x, (long long)a.y, (long long)a.z);
                    }
                    ++dang;
                }
            }
            std::fprintf(stderr, "DBG dangling halfedges: %d\n", dang);
            const gcm::i64 ax = 25769804, ay = 17179869, az = 17179869;
            const gcm::i64 bx = 24618971, by = 12884902, bz = 17179869;
            for (std::size_t h = 0; h < s.body.halfedge_count(); ++h) {
                const auto& a = s.body.vertex_point[s.body.he_vertex[h]];
                const auto& c = s.body.vertex_point[s.body.he_vertex[s.body.he_next[h]]];
                if ((a.x == ax && a.y == ay && a.z == az && c.x == bx && c.y == by && c.z == bz) ||
                    (a.x == bx && a.y == by && a.z == bz && c.x == ax && c.y == ay && c.z == az)) {
                    const u32 L = s.body.he_loop[h];
                    const u32 he0 = s.body.loop_he_offset[L];
                    const u32 n = s.body.loop_he_offset[L + 1] - he0;
                    std::fprintf(stderr, "DBG edge %c%c face=%d n=%d: ", a.x == ax ? 'A' : 'B', c.x == bx ? 'B' : 'A',
                                 (int)s.body.loop_face[L], (int)n);
                    for (u32 i = 0; i < n; ++i) {
                        const auto& q = s.body.vertex_point[s.body.he_vertex[he0 + i]];
                        std::fprintf(stderr, "(%lld,%lld,%lld) ", (long long)q.x, (long long)q.y, (long long)q.z);
                    }
                    std::fprintf(stderr, "\n");
                }
            }
        }
    }
    CHECK(true);
}

TEST(bool_volume_is_exact_and_signed) {
    const auto v = vol();
    const Body solid = box(v, 0, 0, 2, 3, 4);
    const double vol_val = volume(solid, v);
    CHECK_MSG(std::fabs(vol_val - 24.0) < 1e-3, "volume %.6f, expected 24", vol_val);
    CHECK(volume_6x(solid).sign() == gcm::Sign::Positive);
}

TEST(bool_planarity_gate) {
    const auto v = vol();
    CHECK(all_faces_planar(box(v, 0, 0, 1, 1, 1)));

    Profile p;
    p.outer = {gcm::to_lattice(v, 0, 0),  gcm::to_lattice(v, 4, 0),
               gcm::to_lattice(v, 5, 2),  gcm::to_lattice(v, 3, 4),
               gcm::to_lattice(v, 0, 3)};
    p.normalize();
    CHECK(all_faces_planar(extrude_z(p, v, 2.0)));
}

TEST(bool_rejects_open_bodies) {
    const auto v = vol();
    Builder b;
    const auto a = b.add_vertex(gcm::to_lattice(v, 0, 0, 0));
    const auto c = b.add_vertex(gcm::to_lattice(v, 1, 0, 0));
    const auto d = b.add_vertex(gcm::to_lattice(v, 0, 1, 0));
    b.begin_face();
    b.add_loop({a, c, d});
    b.end_face();
    const Body open = b.build();

    const auto r = subtract(box(v, 0, 0, 1, 1, 1), open, v);
    CHECK(!r.ok());
    CHECK(r.error == BoolError::OpenBody);
    CHECK(std::string(r.message()).find("closed") != std::string::npos);
}

TEST(bool_subtract_cuts_a_through_hole) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 10, 10, 4);
    const Body pin = box(v, 4, 4, 6, 6, 8, -2.0);

    const auto r = subtract(block, pin, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    if (!r.ok()) return;

    const double got = std::fabs(volume(r.body, v));
    CHECK_MSG(std::fabs(got - 384.0) < 0.5,
              "volume after subtract %.4f, expected 384 (400 - 16)", got);

    const Topology t = r.body.analyse();
    CHECK_MSG(t.closed, "the cut solid is not closed");
}

TEST(bool_subtract_a_corner_notch) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 10, 10, 10);
    const Body corner = box(v, 8, 8, 12, 12, 12, -1.0);

    const auto r = subtract(block, corner, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    if (!r.ok()) return;

    const double got = std::fabs(volume(r.body, v));
    CHECK_MSG(std::fabs(got - 960.0) < 1.0,
              "volume %.4f, expected 960 (1000 - 40)", got);
}

TEST(bool_subtract_a_disjoint_solid_changes_nothing) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 10, 10, 4);
    const Body away = box(v, 100, 100, 110, 110, 4);

    const auto r = subtract(block, away, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    if (!r.ok()) return;
    CHECK_MSG(std::fabs(std::fabs(volume(r.body, v)) - 400.0) < 0.5,
              "a disjoint subtraction changed the volume to %.4f",
              std::fabs(volume(r.body, v)));
}

TEST(bool_union_of_disjoint_solids_adds_volume) {
    const auto v = vol();
    const Body a = box(v, 0, 0, 2, 2, 2);
    const Body b = box(v, 10, 10, 12, 12, 2);

    const auto r = unite(a, b, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    if (!r.ok()) return;
    CHECK_MSG(std::fabs(std::fabs(volume(r.body, v)) - 16.0) < 0.5,
              "union volume %.4f, expected 16", std::fabs(volume(r.body, v)));
}

TEST(bool_union_of_overlapping_solids_does_not_double_count) {
    const auto v = vol();
    const Body a = box(v, 0, 0, 10, 10, 2);
    const Body b = box(v, 5, 0, 15, 10, 2);

    const auto r = unite(a, b, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    if (!r.ok()) return;
    const double got = std::fabs(volume(r.body, v));
    CHECK_MSG(std::fabs(got - 300.0) < 1.0,
              "union volume %.4f, expected 300 (200 + 200 - 100)", got);
}

TEST(bool_intersect_keeps_only_the_common_volume) {
    const auto v = vol();
    const Body a = box(v, 0, 0, 10, 10, 2);
    const Body b = box(v, 5, 0, 15, 10, 2);

    const auto r = intersect(a, b, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    if (!r.ok()) return;
    const double got = std::fabs(volume(r.body, v));
    CHECK_MSG(std::fabs(got - 100.0) < 1.0,
              "intersection volume %.4f, expected 100", got);
}

TEST(bool_intersect_of_disjoint_solids_is_empty) {
    const auto v = vol();
    const auto r = intersect(box(v, 0, 0, 2, 2, 2), box(v, 50, 50, 52, 52, 2), v);
    if (r.ok()) {
        CHECK_MSG(std::fabs(volume(r.body, v)) < 0.5,
                  "disjoint intersection has volume %.4f", volume(r.body, v));
    } else {
        CHECK(r.error == BoolError::EmptyResult);
    }
}

TEST(bool_results_are_triangulable) {
    const auto v = vol();
    const auto r = subtract(box(v, 0, 0, 10, 10, 4), box(v, 4, 4, 6, 6, 8, -2.0), v);
    if (!r.ok()) return;

    const TriMesh m = triangulate(r.body, v);
    CHECK_MSG(m.triangle_count() > 0, "boolean result produced no triangles");
    for (u32 i : m.indices) CHECK(i < r.body.vertex_count());
}

TEST(bool_subtract_then_union_restores_volume) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 10, 10, 4);
    const Body tool  = box(v, 8, 8, 12, 12, 6, -1.0);

    const auto cut = subtract(block, tool, v);
    if (!cut.ok()) return;
    const double after_cut = std::fabs(volume(cut.body, v));
    CHECK(after_cut < 400.0);

    const auto back = unite(cut.body, box(v, 8, 8, 10, 10, 4), v);
    if (!back.ok()) return;
    CHECK_MSG(std::fabs(std::fabs(volume(back.body, v)) - 400.0) < 1.5,
              "after cut and restore volume is %.4f, expected 400",
              std::fabs(volume(back.body, v)));
}

TEST(bool_subtract_survives_a_face_with_a_hole_loop) {
    const auto v = vol();
    const Body block = box(v, 0, 0, 4, 4, 2);
    const Body tool1 = box(v, 1, 1, 3, 3, 0.5, 1.5);
    const BoolResult r1 = subtract(block, tool1, v);
    CHECK_MSG(r1.ok(), "%s", r1.message());
    const Body merged = merge_coplanar_faces(r1.body, v);
    CHECK_MSG(std::fabs(volume(merged, v) - 30.0) < 1e-3, "after cut 1: %.4f", volume(merged, v));
    bool has_hole = false;
    for (u32 f = 0; f < merged.face_count(); ++f)
        if (merged.face_loop_end(f) - merged.face_loop_begin(f) > 1) has_hole = true;
    CHECK_MSG(has_hole, "the merge left no hole loop; the repro is void");

    const Body tool2 = box(v, 1, 1, 3, 3, 0.25, 1.75);
    const BoolResult r2 = subtract(merged, tool2, v);
    if (r2.ok()) {
        const auto t2 = r2.body.analyse();
        CHECK_MSG(t2.closed && t2.manifold, "touching cut shredded the shell");
        CHECK_MSG(std::fabs(volume(r2.body, v) - 30.0) < 1e-3, "touching cut: %.4f", volume(r2.body, v));
    }

    const Body tool3 = box(v, 1, 1, 3, 3, 1.0, 1.0);
    const BoolResult r3 = subtract(merged, tool3, v);
    CHECK_MSG(r3.ok(), "deep re-cut failed: %s", r3.message());
    const auto t3 = r3.body.analyse();
    CHECK_MSG(t3.closed && t3.manifold, "deep re-cut shredded the shell");
    CHECK_MSG(std::fabs(volume(r3.body, v) - 28.0) < 1e-3, "deep re-cut: %.4f", volume(r3.body, v));

    const BoolResult ri = intersect(merged, tool2, v);
    if (ri.ok())
        CHECK_MSG(std::fabs(volume(ri.body, v)) < 1e-6, "phantom intersection: %.4f", volume(ri.body, v));
}

TEST(bool_union_of_stacked_boxes_leaves_no_membrane) {
    const auto v = vol();
    const Body plate = box(v, 0, 0, 4, 4, 1);
    const Body cube = box(v, 1, 1, 2, 2, 1, 1.0);
    const BoolResult r = unite(plate, cube, v);
    CHECK_MSG(r.ok(), "%s", r.message());
    const auto t = r.body.analyse();
    CHECK_MSG(t.closed && t.manifold, "the stacked union is not sound BEFORE any merge");
    CHECK_MSG(std::fabs(volume(r.body, v) - 17.0) < 1e-3, "volume %.4f", volume(r.body, v));
    int membranes = 0;
    for (u32 f = 0; f < r.body.face_count(); ++f) {
        bool all_z1 = true, inside_foot = true;
        const u32 l = r.body.face_loop_begin(f);
        for (u32 h = r.body.loop_begin(l); h < r.body.loop_end(l); ++h) {
            double x, y, z;
            gcm::to_world(v, r.body.vertex_point[r.body.he_vertex[h]], x, y, z);
            if (std::fabs(z - 1.0) > 1e-9) all_z1 = false;
            if (x < 1.0 - 1e-9 || x > 2.0 + 1e-9 || y < 1.0 - 1e-9 || y > 2.0 + 1e-9) inside_foot = false;
        }
        if (all_z1 && inside_foot) ++membranes;
    }
    CHECK_MSG(membranes == 0, "%d membrane face(s) under the stacked box", membranes);
}

TEST(bool_merge_keeps_boundary_vertices_shared_with_unmerged_faces) {
    const auto v = vol();
    Builder builder;
    for (const auto& p : std::vector<gcm::Lattice3>{
             gcm::to_lattice(v,0,0,0), gcm::to_lattice(v,2,0,0),
             gcm::to_lattice(v,2,2,0), gcm::to_lattice(v,0,2,0),
             gcm::to_lattice(v,0,0,2), gcm::to_lattice(v,2,0,2),
             gcm::to_lattice(v,2,2,2), gcm::to_lattice(v,0,2,2),
             gcm::to_lattice(v,1,0,2), gcm::to_lattice(v,1,2,2)}) builder.add_vertex(p);
    for (const auto& loop : std::vector<std::vector<VertexId>>{
             {0,3,2,1}, {4,8,9,7}, {8,5,6,9}, {0,1,5,8,4},
             {3,7,9,6,2}, {0,4,7,3}, {1,2,6,5}}) {
        builder.begin_face(); builder.add_loop(loop); builder.end_face();
    }
    const Body input = builder.build();
    CHECK(input.analyse().closed && input.analyse().manifold);
    const Body merged = merge_coplanar_faces(input,v);
    CHECK(merged.face_count() == 6);
    CHECK(merged.analyse().closed && merged.analyse().manifold);
    CHECK(std::fabs(volume(merged,v)-8.0) < 1e-5);
}

TEST(bool_boss_union_after_pocket_cuts_has_matching_edge_subdivisions) {
    const auto v = vol();
    Body base = box(v,0,0,4,4,2);
    for (double depth : {0.5,1.0}) {
        const auto cut = subtract(base,box(v,1,1,3,3,depth,2-depth),v);
        CHECK(cut.ok());
        if (!cut.ok()) return;
        base = merge_coplanar_faces(cut.body,v);
    }
    const auto joined = unite(base,box(v,.2,.2,.8,.8,.5,2),v);
    CHECK(joined.ok());
    if (!joined.ok()) return;
    CHECK(joined.body.analyse().closed && joined.body.analyse().manifold);
    CHECK(all_faces_planar(joined.body));
    const Body merged = merge_coplanar_faces(joined.body,v);
    CHECK(merged.analyse().closed && merged.analyse().manifold);
    CHECK(std::fabs(volume(merged,v)-28.18) < 1e-5);
}

TEST(bool_circular_side_cut_preserves_its_original_edge_planes) {
    for (int bits : {32,40}) {
        auto v=vol(); v.resolution_bits=bits;
        const Body base=box(v,-4,-2,0,2,3,-1.5);
        Profile profile;
        constexpr int sides=80;
        const double pi=std::acos(-1.0),depth=.1218;
        for (int i=0;i<sides;++i) {
            const double angle=2*pi*i/sides;
            profile.outer.push_back(gcm::to_lattice(v,std::cos(angle),std::sin(angle)));
        }
        profile.normalize();
        auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();
        frame.u=ggeo::d3::Vec<gcm::F64>::from(0,1,0);
        frame.v=ggeo::d3::Vec<gcm::F64>::from(0,0,-1);
        frame.n=ggeo::d3::Vec<gcm::F64>::from(-1,0,0);
        const auto result=subtract(base,extrude(profile,v,frame,depth),v);
        CHECK(result.ok());
        if (!result.ok()) continue;
        CHECK(result.body.analyse().closed && result.body.analyse().manifold);
        CHECK(all_faces_planar(result.body));
        const auto merged=merge_coplanar_faces(result.body,v);
        CHECK(merged.analyse().closed && merged.analyse().manifold);
        const double expected=48-depth*sides*.5*std::sin(2*pi/sides);
        CHECK(std::fabs(volume(merged,v)-expected)<1e-5);
    }
}

TEST(bool_circle_cut_join_cut_keeps_closed_seams) {
    const auto v=vol();
    for (int n : {36,128,334}) {
        Profile p;
        for (int i=0;i<n;++i) {
            const double a=6.283185307179586*i/n;
            p.outer.push_back(gcm::to_lattice(v,4+std::cos(a),2.5+std::sin(a)));
        }
        p.normalize();
        const auto cylinder=[&](double z,double h) {
            auto f=ggeo::d3::Frame<gcm::F64>::world_xy();
            f.origin=ggeo::d3::Point<gcm::F64>::from(0,0,z);
            return extrude(p,v,f,h);
        };
        auto cut=subtract(box(v,0,0,8,5,3),cylinder(2,1),v);
        CHECK(cut.ok()); CHECK(cut.body.analyse().closed);
        auto joined=unite(merge_coplanar_faces(cut.body,v),cylinder(2,2),v);
        CHECK(joined.ok()); CHECK_MSG(joined.body.analyse().closed,"n=%d faces=%u",n,joined.body.face_count());
        auto recut=subtract(merge_coplanar_faces(joined.body,v),cylinder(2.5,1.5),v);
        CHECK_MSG(recut.ok(),"n=%d: %s",n,recut.message());
        CHECK(recut.body.analyse().closed);
        CHECK(all_faces_planar(joined.body)); CHECK(all_faces_planar(recut.body));
        const double area=volume(cylinder(0,1),v);
        CHECK(std::fabs(volume(joined.body,v)-(120+area))<1e-4);
        CHECK(std::fabs(volume(recut.body,v)-(120-area*.5))<1e-4);
    }
}

TEST(bool_rectangular_cuts_in_circular_pocket_floor_and_box_corner) {
    const auto v=vol();
    const auto solid=box(v,-1,-1,1,1,2);
    const double top=solid.bounds(v).hi.z.to_f64();
    Profile circle;
    for(int i=0;i<179;++i) {
        const double t=6.283185307179586*i/179;
        circle.outer.push_back(gcm::to_lattice(v,0.14142133295536041*std::cos(t),0.14142133295536041*std::sin(t)));
    }
    auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin=ggeo::d3::Point<gcm::F64>::from(0,0,top-0.0254);
    const auto pocket=subtract(solid,extrude(circle,v,frame,0.1),v);
    CHECK(pocket.ok());
    const auto original=merge_coplanar_faces(pocket.body,v);
    CHECK(original.analyse().closed && original.analyse().manifold);
    double floor=top;
    for(const auto& p:original.vertex_point) {
        double x,y,z;gcm::to_world(v,p,x,y,z);
        if(z>top-0.026 && z<top-0.025) floor=std::min(floor,z);
    }
    CHECK(floor<top);
    for(bool corner:{false,true}) for(double depth:{0.0592839,0.85428,2.2422304}) {
        const double start=corner?top:floor;
        const auto tool=corner?box(v,0.9,-1.1,1.1,-0.9,depth,start-depth)
                              :box(v,-0.02,-0.02,0.02,0.02,depth,start-depth);
        const auto cut=subtract(original,tool,v);
        CHECK_MSG(cut.ok(),"corner=%d depth=%.7g: %s",corner,depth,cut.message());
        const auto result=merge_coplanar_faces(cut.body,v);
        CHECK_MSG(result.analyse().closed && result.analyse().manifold,"corner=%d depth=%.7g is not closed",corner,depth);
        CHECK(all_faces_planar(result));
        const double expected=(corner?0.01:0.0016)*std::min(depth,start);
        CHECK_MSG(std::fabs(volume(original,v)-volume(result,v)-expected)<5e-7,
                  "corner=%d depth=%.7g removed %.9g, expected %.9g",corner,depth,volume(original,v)-volume(result,v),expected);
    }
}

TEST(bool_merge_flat_faces_survive_an_unmergeable_rounded_seam) {
    const auto v=vol();
    auto source=box(v,0,0,2,2,2);
    Builder builder;
    for (auto p:source.vertex_point) {
        const auto corner=gcm::to_lattice(v,2,2,2);
        if (p.x==corner.x && p.y==corner.y && p.z==corner.z) ++p.z;
        builder.add_vertex(p);
    }
    for (u32 f=0;f<source.face_count();++f) {
        const u32 h=source.loop_begin(source.face_loop_begin(f));
        for (const auto tri: {std::array<u32,3>{h,h+1,h+2},std::array<u32,3>{h,h+2,h+3}}) {
            builder.begin_face(); builder.add_loop({source.he_vertex[tri[0]],source.he_vertex[tri[1]],source.he_vertex[tri[2]]}); builder.end_face();
        }
    }
    const auto input=builder.build();
    CHECK(input.analyse().closed && all_faces_planar(input));
    const auto merged=merge_coplanar_faces(input,v);
    CHECK(merged.face_count()==7);
    CHECK(merged.analyse().closed && merged.analyse().manifold);
    CHECK(all_faces_planar(merged));
    CHECK(volume_6x(input).l==volume_6x(merged).l);
    CHECK(merge_coplanar_faces(merged,v).face_count()==7);
}

TEST(bool_merge_thin_coplanar_fragments_uses_exact_winding) {
    const auto v=vol();
    for (int axis=0;axis<3;++axis) {
        Builder builder;
        for (auto p: {gcm::Lattice3{2690239,-4024012,11313749},
                      gcm::Lattice3{2690239,-3988827,11365019},
                      gcm::Lattice3{2690239,-1388878,15153548},
                      gcm::Lattice3{2690239,-4500000,15500000}}) {
            for(int k=0;k<axis;++k)p={p.z,p.x,p.y};
            builder.add_vertex(p);
        }
        builder.begin_face();builder.add_loop({0,1,2});builder.end_face();
        builder.begin_face();builder.add_loop({0,2,3});builder.end_face();
        const auto source=builder.build();
        const auto merged=merge_coplanar_faces(source,v);
        CHECK(merged.face_count()==1);
        CHECK(merged.halfedge_count()==4);
        CHECK(all_faces_planar(merged));
        CHECK(volume_6x(source).l==volume_6x(merged).l);
        CHECK(merge_coplanar_faces(merged,v).face_count()==1);
    }
}

TEST(bool_nested_circular_and_square_pockets_keep_complete_flat_faces) {
    const auto v=vol();
    auto solid=box(v,-1,-1,1,1,2);
    double floor=solid.bounds(v).hi.z.to_f64();
    std::vector<double> levels{floor};
    for (int pass=0;pass<3;++pass) {
        const double depth=pass==0?0.62888771004452693:pass==1?0.3770457115024326:0.39114777542902623;
        Profile profile;
        if (pass==2) profile.outer=rect(v,-.2,-.2,.2,.2);
        else {
            const int count=pass==0?415:473;
            const double radius=pass==0?std::sqrt(.5):.5;
            for (int i=0;i<count;++i) {
                const double t=6.283185307179586*i/count;
                profile.outer.push_back(gcm::to_lattice(v,radius*std::cos(t),radius*std::sin(t)));
            }
        }
        auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();
        frame.origin=ggeo::d3::Point<gcm::F64>::from(0,0,floor-depth);
        const auto tool=extrude(profile,v,frame,depth+.01);
        floor=tool.bounds(v).lo.z.to_f64();levels.push_back(floor);
        auto cut=subtract(solid,tool,v);CHECK_MSG(cut.ok(),"pass %d: %s",pass,cut.message());
        if (!cut.ok()) return;
        solid=merge_coplanar_faces(cut.body,v);
        CHECK(solid.analyse().closed && solid.analyse().manifold);CHECK(all_faces_planar(solid));
        for (double level:levels) {
            u32 count=0;
            for (u32 f=0;f<solid.face_count();++f) {
                bool on=true;
                for (u32 l=solid.face_loop_begin(f);l<solid.face_loop_end(f);++l)
                    for (u32 h=solid.loop_begin(l);h<solid.loop_end(l);++h) {
                        double x,y,z;gcm::to_world(v,solid.vertex_point[solid.he_vertex[h]],x,y,z);
                        if (std::fabs(z-level)>v.inv_scale()*.5) on=false;
                    }
                if (on) ++count;
            }
            CHECK_MSG(count==1,"pass %d level %.12g has %u separate faces",pass,level,count);
        }
    }
}
