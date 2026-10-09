#include "test.hpp"

#include <cmath>

#include "geometry/extrude.hpp"
#include "geometry/triangulate.hpp"

using namespace gtopo;
using gcm::Lattice2;

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

TEST(profile_signed_area_is_exact_and_signed) {
    const auto v = vol();
    const Loop2 ccw = rect(v, 0, 0, 10, 5);
    Loop2 cw = ccw;
    std::reverse(cw.begin(), cw.end());

    CHECK(signed_area_sign(ccw) == gcm::Sign::Positive);
    CHECK(signed_area_sign(cw) == gcm::Sign::Negative);
}

TEST(profile_normalize_forces_the_convention) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    std::reverse(p.outer.begin(), p.outer.end());
    p.holes.push_back(rect(v, 3, 3, 7, 7));

    CHECK(!p.valid());
    p.normalize();
    CHECK(p.valid());
    CHECK(signed_area_sign(p.outer) == gcm::Sign::Positive);
    CHECK(signed_area_sign(p.holes[0]) == gcm::Sign::Negative);
}

TEST(extrude_square_gives_a_closed_manifold_box) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);

    const Body b = extrude_z(p, v, 4.0);
    const Topology t = b.analyse();

    CHECK_MSG(t.vertices == 8, "expected 8 vertices, got %u", t.vertices);
    CHECK_MSG(t.edges == 12, "expected 12 edges, got %u", t.edges);
    CHECK_MSG(t.faces == 6, "expected 6 faces, got %u", t.faces);
    CHECK_MSG(t.loops == 6, "expected 6 loops, got %u", t.loops);
    CHECK_MSG(t.closed, "box is not closed -- some halfedge has no twin");
    CHECK_MSG(t.manifold, "box is not manifold");
    CHECK_MSG(t.shells == 1, "expected 1 shell, got %u", t.shells);

    CHECK_MSG(t.euler() == 2, "euler characteristic %lld, expected 2",
              (long long)t.euler());
    CHECK_MSG(t.genus == 0, "genus %lld, expected 0", (long long)t.genus);
}

TEST(extrude_with_a_hole_produces_genus_one) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    p.holes.push_back(rect(v, 3, 3, 7, 7));
    p.normalize();

    const Body b = extrude_z(p, v, 4.0);
    const Topology t = b.analyse();

    CHECK_MSG(t.vertices == 16, "expected 16 vertices, got %u", t.vertices);
    CHECK_MSG(t.edges == 24, "expected 24 edges, got %u", t.edges);
    CHECK_MSG(t.faces == 10, "expected 10 faces, got %u", t.faces);
    CHECK_MSG(t.loops == 12, "expected 12 loops, got %u", t.loops);
    CHECK_MSG(t.closed, "solid with hole is not closed");
    CHECK_MSG(t.manifold, "solid with hole is not manifold");
    CHECK_MSG(t.euler() == 0, "euler characteristic %lld, expected 0",
              (long long)t.euler());
    CHECK_MSG(t.genus == 1, "genus %lld, expected 1 (a through-hole)",
              (long long)t.genus);
}

TEST(extrude_two_holes_gives_genus_two) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 30, 10);
    p.holes.push_back(rect(v, 3, 3, 7, 7));
    p.holes.push_back(rect(v, 20, 3, 24, 7));
    p.normalize();

    const Topology t = extrude_z(p, v, 2.0).analyse();
    CHECK(t.closed);
    CHECK_MSG(t.genus == 2, "genus %lld, expected 2", (long long)t.genus);
}

TEST(extrude_caps_carry_the_holes_as_inner_loops) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    p.holes.push_back(rect(v, 3, 3, 7, 7));
    p.normalize();

    const Body b = extrude_z(p, v, 4.0);
    // Faces 0 and 1 are the caps; each must have two loops (outer + hole).
    CHECK(b.face_loop_count(0) == 2);
    CHECK(b.face_loop_count(1) == 2);
    for (u32 f = 2; f < b.face_count(); ++f) {
        CHECK(b.face_loop_count(f) == 1);
        CHECK(b.loop_size(b.face_loop_begin(f)) == 4);
    }
}

TEST(extrude_height_and_bounds) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, -5, -5, 5, 5);

    const Body b = extrude_z(p, v, 12.5);
    const auto box = b.bounds(v);
    CHECK(std::fabs(box.lo.z.to_f64()) < 1e-3);
    CHECK(std::fabs(box.hi.z.to_f64() - 12.5) < 1e-3);
    CHECK(std::fabs(box.size().x.to_f64() - 10.0) < 1e-3);
}

TEST(extrude_onto_an_arbitrary_frame) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 4, 4);

    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(100.0, 50.0, 20.0);

    const Body b = extrude(p, v, frame, 3.0);
    const Topology t = b.analyse();
    CHECK(t.closed);
    CHECK(t.genus == 0);

    const auto box = b.bounds(v);
    CHECK(std::fabs(box.lo.x.to_f64() - 100.0) < 1e-3);
    CHECK(std::fabs(box.lo.y.to_f64() - 50.0) < 1e-3);
    CHECK(std::fabs(box.hi.z.to_f64() - 23.0) < 1e-3);
}

TEST(triangulate_box_gives_two_triangles_per_face) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);

    const Body b = extrude_z(p, v, 4.0);
    const TriMesh m = triangulate(b, v);

    CHECK_MSG(m.triangle_count() == 12, "expected 12 triangles, got %zu",
              m.triangle_count());
    CHECK(m.normals.size() == m.triangle_count());
    for (u32 i : m.indices) CHECK(i < b.vertex_count());
}

TEST(triangulate_face_with_a_hole_bridges_correctly) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    p.holes.push_back(rect(v, 3, 3, 7, 7));
    p.normalize();

    const Body b = extrude_z(p, v, 4.0);
    const TriMesh m = triangulate(b, v);

    CHECK_MSG(m.triangle_count() == 32, "expected 32 triangles, got %zu",
              m.triangle_count());

    for (std::size_t i = 0; i < m.indices.size(); i += 3) {
        CHECK(m.indices[i] != m.indices[i + 1]);
        CHECK(m.indices[i + 1] != m.indices[i + 2]);
        CHECK(m.indices[i] != m.indices[i + 2]);
    }
}

TEST(triangulate_concave_polygon) {
    const auto v = vol();
    Profile p;
    p.outer = {gcm::to_lattice(v, 0, 0),  gcm::to_lattice(v, 10, 0),
               gcm::to_lattice(v, 10, 4), gcm::to_lattice(v, 4, 4),
               gcm::to_lattice(v, 4, 10), gcm::to_lattice(v, 0, 10)};
    p.normalize();

    const Body b = extrude_z(p, v, 2.0);
    const Topology t = b.analyse();
    CHECK(t.closed);
    CHECK(t.genus == 0);
    CHECK(t.vertices == 12);
    CHECK(t.faces == 8);

    const TriMesh m = triangulate(b, v);
    CHECK_MSG(m.triangle_count() == 20, "expected 20 triangles, got %zu",
              m.triangle_count());
}

TEST(triangulate_long_collinear_face_terminates_without_area) {
    std::vector<gcm::Lattice2> points;
    std::vector<u32> loop;
    for (u32 i = 0; i < 2048; ++i) {
        points.push_back({i, 2 * i});
        loop.push_back(i);
    }
    const auto triangles = triangulate_face({loop}, points);
    CHECK(triangles.size() % 3 == 0);
    for (u32 i : triangles) CHECK(i < points.size());
    for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
        const auto a = points[triangles[i]], b = points[triangles[i + 1]], c = points[triangles[i + 2]];
        CHECK((b.x - a.x) * (c.y - a.y) == (b.y - a.y) * (c.x - a.x));
    }
}

TEST(triangulate_dense_convex_profile_preserves_boundary_without_a_triangle_fan) {
    const auto v=vol();
    std::vector<Lattice2> points;
    std::vector<gcm::u32> loop;
    constexpr unsigned count=512;
    for(unsigned i=0;i<count;++i) {
        const double angle=2*std::acos(-1.0)*i/count;
        points.push_back(gcm::to_lattice(v,std::cos(angle),std::sin(angle)));
        loop.push_back(i);
    }
    const auto triangles=triangulate_face({loop},points);
    CHECK(triangles.size()==3*(count-2));
    std::vector<unsigned> valence(count),boundary(count);
    gcm::SInt<128> triangle_area,polygon_area;
    const auto cross=[](Lattice2 a,Lattice2 b) {
        return gcm::sub(gcm::mul_i64_i64<128>(a.x,b.y),gcm::mul_i64_i64<128>(a.y,b.x));
    };
    for(unsigned i=0;i<count;++i) polygon_area=gcm::add(polygon_area,cross(points[i],points[(i+1)%count]));
    for(std::size_t i=0;i+2<triangles.size();i+=3) {
        gcm::SInt<128> area;
        for(unsigned j=0;j<3;++j) {
            const auto a=triangles[i+j],b=triangles[i+(j+1)%3];
            CHECK(a<count && b<count);
            ++valence[a]; if(b==(a+1)%count)++boundary[a];
            area=gcm::add(area,cross(points[a],points[b]));
        }
        CHECK(area.sign()==gcm::Sign::Positive);
        triangle_area=gcm::add(triangle_area,area);
    }
    CHECK(gcm::sub(triangle_area,polygon_area).is_zero());
    CHECK(*std::max_element(valence.begin(),valence.end())<32);
    for(auto uses:boundary)CHECK(uses==1);
}
