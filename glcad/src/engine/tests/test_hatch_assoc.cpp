#include "test.hpp"

#include <cmath>

#include "engine/hatch.hpp"

using namespace gdoc;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
Point2 pt(const gcm::WorkingVolume& v, f64 x, f64 y) {
    return gcm::to_lattice(v, x, y);
}
Segment2 seg(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    return {pt(v, x0, y0), pt(v, x1, y1)};
}

std::vector<Segment2> square(const gcm::WorkingVolume& v, f64 s) {
    return {seg(v, 0, 0, s, 0), seg(v, s, 0, s, s), seg(v, s, s, 0, s),
            seg(v, 0, s, 0, 0)};
}

AssociativeHatch make(const gcm::WorkingVolume& v) {
    AssociativeHatch h;
    h.style.name = "ANSI31";
    h.style.scale = 1.0;
    h.style.associative = true;
    h.boundary_shapes = {0, 1, 2, 3};
    h.seed = pt(v, 2, 2);
    h.seed_valid = true;
    return h;
}

}

TEST(hatch_fingerprint_is_order_independent) {
    const auto v = vol();
    std::vector<Segment2> a = square(v, 4.0);
    std::vector<Segment2> b = {a[2], a[0], a[3], a[1]};
    CHECK_MSG(boundary_fingerprint(a) == boundary_fingerprint(b),
              "reordering the boundary changed its fingerprint");
}

TEST(hatch_fingerprint_is_direction_independent) {
    const auto v = vol();
    std::vector<Segment2> a = square(v, 4.0);
    std::vector<Segment2> b = a;
    std::swap(b[1].a, b[1].b);
    CHECK(boundary_fingerprint(a) == boundary_fingerprint(b));
}

TEST(hatch_fingerprint_notices_a_moved_vertex) {
    const auto v = vol();
    std::vector<Segment2> a = square(v, 4.0);
    std::vector<Segment2> b = a;
    b[0].b = pt(v, 4.001, 0);
    CHECK_MSG(boundary_fingerprint(a) != boundary_fingerprint(b),
              "a moved corner went unnoticed");
}

TEST(hatch_generates_on_first_update) {
    const auto v = vol();
    PatternTable pats;
    AssociativeHatch h = make(v);
    CHECK(!h.valid);

    const HatchUpdate u = update_hatch(h, square(v, 6.0), pats, v);
    CHECK_MSG(u.regenerated, "%s", u.reason);
    CHECK(h.valid);
    CHECK(!h.orphaned);
    CHECK_MSG(!h.cached.lines.empty(), "the hatch produced no geometry");
}

TEST(hatch_does_not_regenerate_when_nothing_moved) {
    const auto v = vol();
    PatternTable pats;
    AssociativeHatch h = make(v);
    const auto boundary = square(v, 6.0);

    update_hatch(h, boundary, pats, v);
    const std::size_t before = h.cached.lines.size();

    const HatchUpdate again = update_hatch(h, boundary, pats, v);
    CHECK_MSG(!again.regenerated, "it regenerated for nothing");
    CHECK(h.cached.lines.size() == before);
}

TEST(hatch_follows_its_boundary_when_the_wall_moves) {
    const auto v = vol();
    PatternTable pats;
    AssociativeHatch h = make(v);

    update_hatch(h, square(v, 4.0), pats, v);
    const std::size_t small = h.cached.lines.size();
    CHECK(small > 0);

    const HatchUpdate grown = update_hatch(h, square(v, 12.0), pats, v);
    CHECK_MSG(grown.regenerated, "%s", grown.reason);
    CHECK_MSG(h.cached.lines.size() > small,
              "a bigger room got %zu lines, was %zu", h.cached.lines.size(), small);
}

TEST(hatch_that_is_not_associative_stays_a_snapshot) {
    const auto v = vol();
    PatternTable pats;
    AssociativeHatch h = make(v);
    update_hatch(h, square(v, 4.0), pats, v);
    const std::size_t before = h.cached.lines.size();

    h.style.associative = false;
    const HatchUpdate u = update_hatch(h, square(v, 12.0), pats, v);
    CHECK_MSG(!u.regenerated, "a non-associative hatch followed its boundary");
    CHECK(h.cached.lines.size() == before);
}

TEST(hatch_is_orphaned_when_its_room_is_deleted) {
    const auto v = vol();
    PatternTable pats;
    AssociativeHatch h = make(v);
    update_hatch(h, square(v, 4.0), pats, v);
    CHECK(h.valid);

    const HatchUpdate u = update_hatch(h, {}, pats, v);
    CHECK_MSG(u.orphaned, "%s", u.reason);
    CHECK(h.orphaned);
    CHECK(!h.valid);
}

TEST(hatch_is_orphaned_when_the_boundary_opens) {
    const auto v = vol();
    PatternTable pats;
    AssociativeHatch h = make(v);
    update_hatch(h, square(v, 4.0), pats, v);

    auto broken = square(v, 4.0);
    broken[2].b = pt(v, -50, 50);
    const HatchUpdate u = update_hatch(h, broken, pats, v);
    CHECK_MSG(u.orphaned || !u.regenerated, "an open room hatched anyway: %s",
              u.reason);
}

TEST(hatch_with_an_unknown_pattern_reports_it) {
    const auto v = vol();
    PatternTable pats;
    AssociativeHatch h = make(v);
    h.style.name = "NOT-A-PATTERN";
    const HatchUpdate u = update_hatch(h, square(v, 4.0), pats, v);
    CHECK(!u.regenerated);
    CHECK(std::string(u.reason).find("pattern") != std::string::npos);
}

namespace {
std::vector<Loop> unit_region(const gcm::WorkingVolume& v, f64 s) {
    return {{pt(v, 0, 0), pt(v, s, 0), pt(v, s, s), pt(v, 0, s)}};
}
}

TEST(gradient_linear_runs_from_zero_to_one_across_the_region) {
    const auto v = vol();
    const auto region = unit_region(v, 10.0);
    Gradient g;
    g.kind = GradientKind::Linear;

    const f64 left = gradient_at(g, pt(v, 0, 5), region, v);
    const f64 mid = gradient_at(g, pt(v, 5, 5), region, v);
    const f64 right = gradient_at(g, pt(v, 10, 5), region, v);
    CHECK_MSG(left < 0.01, "left is %.6f", left);
    CHECK_MSG(std::fabs(mid - 0.5) < 0.01, "middle is %.6f", mid);
    CHECK_MSG(right > 0.99, "right is %.6f", right);
}

TEST(gradient_angle_rotates_the_sweep) {
    const auto v = vol();
    const auto region = unit_region(v, 10.0);
    Gradient g;
    g.kind = GradientKind::Linear;
    g.angle = 90.0;

    CHECK(gradient_at(g, pt(v, 5, 0), region, v) < 0.01);
    CHECK(gradient_at(g, pt(v, 5, 10), region, v) > 0.99);
    CHECK(std::fabs(gradient_at(g, pt(v, 0, 5), region, v) -
                    gradient_at(g, pt(v, 10, 5), region, v)) < 0.01);
}

TEST(gradient_cylindrical_is_bright_in_the_middle) {
    const auto v = vol();
    const auto region = unit_region(v, 10.0);
    Gradient g;
    g.kind = GradientKind::Cylindrical;

    const f64 mid = gradient_at(g, pt(v, 5, 5), region, v);
    const f64 edge = gradient_at(g, pt(v, 0, 5), region, v);
    CHECK_MSG(mid > 0.99, "the ridge is %.6f", mid);
    CHECK_MSG(edge < 0.01, "the edge is %.6f", edge);

    Gradient inv = g;
    inv.kind = GradientKind::InvertedCylindrical;
    CHECK(gradient_at(inv, pt(v, 5, 5), region, v) < 0.01);
}

TEST(gradient_spherical_falls_off_with_distance) {
    const auto v = vol();
    const auto region = unit_region(v, 10.0);
    Gradient g;
    g.kind = GradientKind::Spherical;
    CHECK(gradient_at(g, pt(v, 5, 5), region, v) > 0.99);
    CHECK(gradient_at(g, pt(v, 0, 0), region, v) <
          gradient_at(g, pt(v, 3, 4), region, v));
}

TEST(gradient_every_kind_stays_in_range) {
    const auto v = vol();
    const auto region = unit_region(v, 10.0);
    for (int i = 0; i <= static_cast<int>(GradientKind::InvertedCurved); ++i) {
        Gradient g;
        g.kind = static_cast<GradientKind>(i);
        CHECK_MSG(std::string(gradient_kind_name(g.kind)).size() > 0,
                  "kind %d unnamed", i);
        for (int x = -2; x <= 12; ++x)
            for (int y = -2; y <= 12; ++y) {
                const f64 t = gradient_at(g, pt(v, x, y), region, v);
                CHECK_MSG(t >= 0.0 && t <= 1.0, "%s gave %.6f at (%d,%d)",
                          gradient_kind_name(g.kind), t, x, y);
            }
    }
}

TEST(gradient_colour_interpolates_between_the_ends) {
    Gradient g;
    g.two_colour = true;
    g.from = Color::rgb(0, 0, 0);
    g.to = Color::rgb(255, 255, 255);

    CHECK(gradient_color(g, 0.0).r == 0);
    CHECK(gradient_color(g, 1.0).r == 255);
    const Color mid = gradient_color(g, 0.5);
    CHECK_MSG(mid.r > 120 && mid.r < 135, "midpoint is %d", int(mid.r));
    CHECK(mid.kind == Color::Kind::True);
}

TEST(gradient_single_colour_fades_toward_white) {
    Gradient g;
    g.two_colour = false;
    g.from = Color::rgb(0, 0, 200);
    g.tint = 1.0;

    const Color hot = gradient_color(g, 1.0);
    CHECK_MSG(hot.r == 255 && hot.g == 255, "a full tint should reach white");
    CHECK(gradient_color(g, 0.0).b == 200);
}

TEST(gradient_bands_span_the_range) {
    const auto v = vol();
    const auto region = unit_region(v, 10.0);
    Gradient g;
    g.from = Color::rgb(0, 0, 0);
    g.to = Color::rgb(255, 255, 255);

    const auto bands = gradient_bands(g, region, 16, v);
    CHECK(bands.size() == 16);
    CHECK(std::fabs(bands.front().t) < 1e-12);
    CHECK(std::fabs(bands.back().t - 1.0) < 1e-12);
    CHECK(bands.front().color.r < bands.back().color.r);
    for (const auto& b : bands) CHECK(!b.loops.empty());
}

TEST(gradient_bands_of_an_empty_region_are_empty) {
    const auto v = vol();
    Gradient g;
    CHECK(gradient_bands(g, {}, 8, v).empty());
    CHECK(gradient_bands(g, unit_region(v, 1.0), 0, v).empty());
    CHECK(gradient_at(g, gcm::to_lattice(v, 0, 0), {}, v) == 0.0);
}
