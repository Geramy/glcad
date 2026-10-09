#include "test.hpp"

#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/loft.hpp"
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
static Section section(const gcm::WorkingVolume& v, double half, double z) {
    Section s;
    s.profile.outer = rect(v, -half, -half, half, half);
    s.profile.normalize();
    s.frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    s.frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z);
    return s;
}

TEST(loft_two_equal_sections_is_a_prism) {
    const auto v = vol();
    const Body b = loft({section(v, 1, 0), section(v, 1, 4)}, v, true);
    const Topology t = b.analyse();

    CHECK_MSG(t.closed, "a capped loft must be closed");
    CHECK(t.manifold);
    CHECK(t.genus == 0);
    CHECK_MSG(std::fabs(std::fabs(volume(b, v)) - 16.0) < 0.1,
              "loft volume %.4f, expected 16", std::fabs(volume(b, v)));
}

TEST(loft_tapered_sections_form_a_frustum) {
    const auto v = vol();
    const Body b = loft({section(v, 2, 0), section(v, 1, 3)}, v, true);
    CHECK(b.analyse().closed);

    const double got = std::fabs(volume(b, v));
    CHECK_MSG(std::fabs(got - 28.0) < 0.2,
              "frustum volume %.4f, prismatoid formula predicts 28", got);
}

TEST(loft_through_several_sections) {
    const auto v = vol();
    const Body b = loft({section(v, 1, 0), section(v, 2, 2), section(v, 1, 4)}, v,
                        true);
    const Topology t = b.analyse();
    CHECK(t.closed);
    CHECK(t.manifold);

    const auto box = b.bounds(v);
    CHECK(std::fabs(box.hi.z.to_f64() - 4.0) < 1e-3);
    CHECK_MSG(std::fabs(box.hi.x.to_f64() - 2.0) < 1e-3,
              "the widest section should set the bounds");
}

TEST(loft_uncapped_is_open) {
    const auto v = vol();
    CHECK(!loft({section(v, 1, 0), section(v, 1, 4)}, v, false).analyse().closed);
    CHECK(loft({section(v, 1, 0), section(v, 1, 4)}, v, true).analyse().closed);
}

TEST(loft_with_a_hole_stays_genus_one) {
    const auto v = vol();
    std::vector<Section> s;
    for (double z : {0.0, 5.0}) {
        Section sec;
        sec.profile.outer = rect(v, -3, -3, 3, 3);
        sec.profile.holes.push_back(rect(v, -1, -1, 1, 1));
        sec.profile.normalize();
        sec.frame = ggeo::d3::Frame<gcm::F64>::world_xy();
        sec.frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z);
        s.push_back(sec);
    }
    const Topology t = loft(s, v, true).analyse();
    CHECK(t.closed);
    CHECK_MSG(t.genus == 1, "a lofted tube is genus 1; got %lld",
              (long long)t.genus);
}

TEST(loft_rejects_mismatched_vertex_counts) {
    const auto v = vol();
    Section a = section(v, 1, 0);
    Section b;
    b.profile.outer = {gcm::to_lattice(v, -1, -1), gcm::to_lattice(v, 1, -1),
                       gcm::to_lattice(v, 1, 1), gcm::to_lattice(v, 0, 2),
                       gcm::to_lattice(v, -1, 1)};
    b.profile.normalize();
    b.frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, 3);

    CHECK(check_sections({a, b}) == LoftError::VertexCountMismatch);
    CHECK(loft({a, b}, v, true).face_count() == 0);
    CHECK(std::string(loft_error_message(LoftError::VertexCountMismatch))
              .find("same vertex count") != std::string::npos);
}

TEST(loft_rejects_too_few_sections) {
    const auto v = vol();
    CHECK(check_sections({section(v, 1, 0)}) == LoftError::TooFewSections);
    CHECK(check_sections({}) == LoftError::TooFewSections);
}

TEST(offset_profile_insets_a_square) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    p.normalize();

    const Profile o = offset_profile(p, 2.0, v);
    CHECK(o.valid());

    double minx = 1e9, maxx = -1e9;
    for (const auto& q : o.outer) {
        double x, y;
        gcm::to_world(v, q, x, y);
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
    }
    CHECK_MSG(std::fabs(minx - 2.0) < 1e-3, "inset min x %.4f", minx);
    CHECK_MSG(std::fabs(maxx - 8.0) < 1e-3, "inset max x %.4f", maxx);
}

TEST(offset_profile_handles_concave_corners) {
    const auto v = vol();
    Profile p;
    p.outer = {gcm::to_lattice(v, 0, 0),  gcm::to_lattice(v, 10, 0),
               gcm::to_lattice(v, 10, 4), gcm::to_lattice(v, 4, 4),
               gcm::to_lattice(v, 4, 10), gcm::to_lattice(v, 0, 10)};
    p.normalize();

    const Profile o = offset_profile(p, 1.0, v);
    CHECK_MSG(o.valid(), "an L-shape should inset cleanly at 1 unit");
    CHECK(o.outer.size() == 6);
}

TEST(offset_profile_refuses_to_collapse) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 6, 6);
    p.normalize();
    CHECK_MSG(!offset_profile(p, 5.0, v).valid(),
              "an over-large inset must be refused");
}

TEST(offset_profile_grows_holes) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 20, 20);
    p.holes.push_back(rect(v, 8, 8, 12, 12));
    p.normalize();

    const Profile o = offset_profile(p, 1.0, v);
    CHECK(o.valid());
    CHECK(o.holes.size() == 1);

    double minx = 1e9, maxx = -1e9;
    for (const auto& q : o.holes[0]) {
        double x, y;
        gcm::to_world(v, q, x, y);
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
    }
    CHECK_MSG(std::fabs(maxx - minx - 6.0) < 1e-2,
              "hole is %.4f wide, expected 6", maxx - minx);
}

TEST(shell_open_top_is_a_box) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    p.normalize();

    const Body b = shell_extrusion(p, v, 5.0, 1.0, /*open_top=*/true);
    const Topology t = b.analyse();
    CHECK_MSG(t.closed, "a shelled box must still be a closed solid");

    const double got = std::fabs(volume(b, v));
    CHECK_MSG(std::fabs(got - 244.0) < 2.0,
              "shelled volume %.4f, expected about 244", got);
}

TEST(shell_closed_both_ends_is_hollow) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    p.normalize();

    const Body b = shell_extrusion(p, v, 6.0, 1.0, false, false);
    const Topology t = b.analyse();
    CHECK(t.closed);

    const double got = std::fabs(volume(b, v));
    CHECK_MSG(std::fabs(got - 344.0) < 3.0,
              "hollow volume %.4f, expected about 344", got);
}

TEST(shell_refuses_an_impossible_thickness) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 4, 4);
    p.normalize();
    CHECK(shell_extrusion(p, v, 5.0, 3.0, true).face_count() == 0);
    CHECK(shell_extrusion(p, v, 5.0, 0.0, true).face_count() == 0);
}

TEST(shell_result_triangulates) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 0, 0, 10, 10);
    p.normalize();
    const Body b = shell_extrusion(p, v, 5.0, 1.0, true);
    const TriMesh m = triangulate(b, v);
    CHECK(m.triangle_count() > 0);
    for (u32 i : m.indices) CHECK(i < b.vertex_count());
}
