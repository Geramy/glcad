#include "test.hpp"

#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/revolve.hpp"
#include "geometry/triangulate.hpp"

using namespace gtopo;
constexpr double kPi = 3.14159265358979323846;

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

TEST(revolve_full_turn_is_closed_and_manifold) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 2, 0, 4, 3);
    p.normalize();

    const Body b = revolve_y(p, v, 2.0 * kPi, 24);
    const Topology t = b.analyse();

    CHECK_MSG(t.closed, "a full revolution must need no end caps and still close");
    CHECK(t.manifold);
    CHECK(t.shells == 1);
    CHECK_MSG(t.vertices == 4 * 24, "expected %d vertices, got %u", 4 * 24, t.vertices);
    CHECK_MSG(t.faces == 4 * 24, "expected %d faces, got %u", 4 * 24, t.faces);
}

TEST(revolve_volume_matches_pappus) {
    const auto v = vol();
    const double x0 = 2.0, x1 = 4.0, y0 = 0.0, y1 = 3.0;
    Profile p;
    p.outer = rect(v, x0, y0, x1, y1);
    p.normalize();

    const Body b = revolve_y(p, v, 2.0 * kPi, 256);

    const double area = (x1 - x0) * (y1 - y0);
    const double r_centroid = (x0 + x1) * 0.5;
    const double expected = 2.0 * kPi * r_centroid * area;

    const double got = std::fabs(volume(b, v));
    CHECK_MSG(std::fabs(got - expected) / expected < 0.005,
              "revolved volume %.4f, Pappus predicts %.4f", got, expected);
}

TEST(revolve_offset_profile_is_a_torus) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 3, -0.5, 4, 0.5);
    p.normalize();

    const Topology t = revolve_y(p, v, 2.0 * kPi, 32).analyse();
    CHECK(t.closed);
    CHECK_MSG(t.genus == 1, "a solid of revolution offset from its axis is a "
                            "torus; genus reported %lld", (long long)t.genus);
}

TEST(revolve_partial_turn_gets_end_caps) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 2, 0, 4, 3);
    p.normalize();

    const Body b = revolve_y(p, v, kPi * 0.5, 16);
    const Topology t = b.analyse();
    CHECK_MSG(t.closed, "a partial revolution must be capped to stay closed");
    CHECK(t.genus == 0);

    Profile q;
    q.outer = rect(v, 2, 0, 4, 3);
    q.normalize();
    const double full = std::fabs(volume(revolve_y(q, v, 2.0 * kPi, 64), v));
    const double quarter = std::fabs(volume(revolve_y(q, v, kPi * 0.5, 16), v));
    CHECK_MSG(std::fabs(quarter / full - 0.25) < 0.02,
              "quarter turn gave %.4f of the full volume", quarter / full);
}

TEST(revolve_rejects_an_axis_through_the_profile) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, -2, 0, 2, 3);
    p.normalize();

    CHECK(axis_crosses_profile(p, v, ggeo::d2::Point<gcm::F64>::from(0, 0),
                               ggeo::d2::Vec<gcm::F64>::from(0, 1)));
    CHECK_MSG(revolve_y(p, v, 2.0 * kPi, 16).face_count() == 0,
              "an axis through the profile must be rejected");
}

TEST(revolve_segment_count_controls_fidelity) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 2, 0, 4, 3);
    p.normalize();

    const double coarse = std::fabs(volume(revolve_y(p, v, 2.0 * kPi, 8), v));
    const double fine   = std::fabs(volume(revolve_y(p, v, 2.0 * kPi, 256), v));
    const double truth  = 2.0 * kPi * 3.0 * 6.0;

    CHECK(coarse < fine);
    CHECK(fine <= truth * 1.001);
    CHECK_MSG(std::fabs(fine - truth) < std::fabs(coarse - truth),
              "refinement did not improve the volume");
}

TEST(revolve_result_triangulates) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, 2, 0, 4, 3);
    p.normalize();
    const Body b = revolve_y(p, v, 2.0 * kPi, 16);
    const TriMesh m = triangulate(b, v);
    CHECK(m.triangle_count() == 16 * 4 * 2);
    for (u32 i : m.indices) CHECK(i < b.vertex_count());
}

TEST(sweep_along_a_straight_path_matches_extrude) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, -1, -1, 1, 1);
    p.normalize();

    std::vector<ggeo::d3::Point<gcm::F64>> path = {
        ggeo::d3::Point<gcm::F64>::from(0, 0, 0),
        ggeo::d3::Point<gcm::F64>::from(0, 0, 10)};

    const Body b = sweep(p, v, path, true);
    const Topology t = b.analyse();
    CHECK_MSG(t.closed, "a capped sweep must be closed");
    CHECK(t.genus == 0);
    CHECK_MSG(std::fabs(std::fabs(volume(b, v)) - 40.0) < 0.5,
              "swept volume %.4f, expected 40", std::fabs(volume(b, v)));
}

TEST(sweep_follows_a_bent_path) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, -0.5, -0.5, 0.5, 0.5);
    p.normalize();

    std::vector<ggeo::d3::Point<gcm::F64>> path = {
        ggeo::d3::Point<gcm::F64>::from(0, 0, 0),
        ggeo::d3::Point<gcm::F64>::from(0, 0, 5),
        ggeo::d3::Point<gcm::F64>::from(3, 0, 8),
        ggeo::d3::Point<gcm::F64>::from(8, 0, 8)};

    const Body b = sweep(p, v, path, true);
    const Topology t = b.analyse();
    CHECK(t.closed);
    CHECK(t.manifold);

    const auto box = b.bounds(v);
    CHECK(box.hi.x.to_f64() > 7.0);
    CHECK(box.hi.z.to_f64() > 7.5);
}

TEST(sweep_uncapped_is_open) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, -1, -1, 1, 1);
    p.normalize();
    std::vector<ggeo::d3::Point<gcm::F64>> path = {
        ggeo::d3::Point<gcm::F64>::from(0, 0, 0),
        ggeo::d3::Point<gcm::F64>::from(0, 0, 4)};

    CHECK(!sweep(p, v, path, false).analyse().closed);
    CHECK(sweep(p, v, path, true).analyse().closed);
}

TEST(sweep_with_a_hole_produces_a_tube) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, -2, -2, 2, 2);
    p.holes.push_back(rect(v, -1, -1, 1, 1));
    p.normalize();

    std::vector<ggeo::d3::Point<gcm::F64>> path = {
        ggeo::d3::Point<gcm::F64>::from(0, 0, 0),
        ggeo::d3::Point<gcm::F64>::from(0, 0, 6)};

    const Body b = sweep(p, v, path, true);
    const Topology t = b.analyse();
    CHECK(t.closed);
    CHECK_MSG(t.genus == 1, "a square tube is genus 1; got %lld",
              (long long)t.genus);

    CHECK_MSG(std::fabs(std::fabs(volume(b, v)) - 72.0) < 1.0,
              "tube volume %.4f, expected 72", std::fabs(volume(b, v)));
}

TEST(sweep_rejects_a_degenerate_path) {
    const auto v = vol();
    Profile p;
    p.outer = rect(v, -1, -1, 1, 1);
    p.normalize();
    std::vector<ggeo::d3::Point<gcm::F64>> one = {
        ggeo::d3::Point<gcm::F64>::from(0, 0, 0)};
    CHECK(sweep(p, v, one, true).face_count() == 0);
    CHECK(sweep(p, v, {}, true).face_count() == 0);
}
