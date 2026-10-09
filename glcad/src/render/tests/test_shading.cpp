#include "test.hpp"

#include <array>
#include <cmath>
#include <vector>

#include "render/shading.hpp"

using namespace grender;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

std::array<f64, 3> unit(f64 x, f64 y, f64 z) {
    const f64 n = std::sqrt(x * x + y * y + z * z);
    return {x / n, y / n, z / n};
}

f64 dot3(const std::array<f64, 3>& a, const std::array<f64, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

}

TEST(edges_a_sharp_corner_is_a_crease_and_a_smooth_one_is_not) {
    EdgeClassify s;
    s.crease_degrees = 30.0;
    const auto eye = unit(0, 0, 1);

    const auto flat_a = unit(0, 0, 1);
    const auto flat_b = unit(0.05, 0, 1);        // about 3 degrees
    CHECK(classify_edge(flat_a.data(), flat_b.data(), eye.data(), s) ==
          EdgeKind::None);

    const auto sharp = unit(1, 0, 0.2);          // well past 30 degrees
    CHECK(classify_edge(flat_a.data(), sharp.data(), eye.data(), s) ==
          EdgeKind::Crease);
}

TEST(edges_the_silhouette_test_comes_before_the_crease_test) {
    EdgeClassify s;
    s.crease_degrees = 30.0;
    const auto eye = unit(0, 0, 1);

    const auto toward = unit(0.02, 0, 1);
    const auto away = unit(0.02, 0, -1);
    CHECK_MSG(classify_edge(toward.data(), away.data(), eye.data(), s) ==
                  EdgeKind::Silhouette,
              "a smooth silhouette was classified as %s",
              edge_kind_name(classify_edge(toward.data(), away.data(),
                                           eye.data(), s)));

    s.draw_silhouettes = false;
    CHECK(classify_edge(toward.data(), away.data(), eye.data(), s) ==
          EdgeKind::Crease);
}

TEST(edges_a_boundary_is_drawn_whatever_its_orientation) {
    EdgeClassify s;
    const auto n = unit(0, 0, 1);
    const auto eye = unit(0, 0, 1);
    CHECK(classify_edge(n.data(), nullptr, eye.data(), s) == EdgeKind::Boundary);

    const auto away = unit(0, 0, -1);
    CHECK(classify_edge(away.data(), nullptr, eye.data(), s) ==
          EdgeKind::Boundary);

    s.draw_boundaries = false;
    CHECK(classify_edge(n.data(), nullptr, eye.data(), s) == EdgeKind::None);
}

TEST(edges_tangent_edges_are_off_by_default_and_can_be_asked_for) {
    EdgeClassify s;
    const auto a = unit(0, 0, 1);
    const auto b = unit(0.05, 0, 1);
    const auto eye = unit(0, 0, 1);
    CHECK(classify_edge(a.data(), b.data(), eye.data(), s) == EdgeKind::None);
    s.draw_tangents = true;
    CHECK(classify_edge(a.data(), b.data(), eye.data(), s) == EdgeKind::Tangent);
}

TEST(edge_kind_names_exist) {
    for (EdgeKind k : {EdgeKind::None, EdgeKind::Crease, EdgeKind::Silhouette,
                       EdgeKind::Boundary, EdgeKind::Tangent})
        CHECK(edge_kind_name(k)[0] != '\0');
}

TEST(ao_kernel_is_inside_the_hemisphere) {
    AoSettings s;
    s.samples = 64;
    const auto k = ao_kernel(s);
    CHECK_MSG(k.size() == 64, "%zu samples", k.size());

    for (const auto& v : k) {
        const f64 len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        CHECK_MSG(len <= 1.0 + 1e-9, "a sample is %g from the origin", len);
        CHECK_MSG(len > 1e-6, "a sample sits at the origin");
        CHECK_MSG(v[2] >= 0.0, "a sample went below the surface (z=%g)", v[2]);
    }
}

TEST(ao_kernel_has_no_directional_bias) {
    AoSettings s;
    s.samples = 512;
    const auto k = ao_kernel(s);

    f64 mx = 0.0, my = 0.0;
    for (const auto& v : k) { mx += v[0]; my += v[1]; }
    mx /= static_cast<f64>(k.size());
    my /= static_cast<f64>(k.size());
    CHECK_MSG(std::fabs(mx) < 0.05, "the kernel leans %g in x", mx);
    CHECK_MSG(std::fabs(my) < 0.05, "the kernel leans %g in y", my);
}

TEST(ao_kernel_packs_samples_toward_the_centre) {
    AoSettings s;
    s.samples = 64;
    const auto k = ao_kernel(s);

    std::size_t near = 0;
    for (const auto& v : k) {
        const f64 len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len < 0.5) ++near;
    }
    CHECK_MSG(near > k.size() / 2, "only %zu of %zu samples are in the near "
              "half", near, k.size());
}

TEST(ao_kernel_is_deterministic) {
    AoSettings s;
    s.samples = 32;
    const auto a = ao_kernel(s);
    const auto b = ao_kernel(s);
    CHECK(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK(a[i][0] == b[i][0] && a[i][1] == b[i][1] && a[i][2] == b[i][2]);

    AoSettings other = s;
    other.seed = 12345u;
    const auto c = ao_kernel(other);
    bool differs = false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i][0] != c[i][0]) differs = true;
    CHECK(differs);
}

TEST(ao_kernel_of_zero_samples_is_empty) {
    AoSettings s;
    s.samples = 0;
    CHECK(ao_kernel(s).empty());
}

TEST(ao_noise_stays_in_the_tangent_plane) {
    const auto n = ao_noise(4);
    CHECK(n.size() == 16);
    for (const auto& v : n) {
        const f64 len = std::hypot(v[0], v[1]);
        CHECK_MSG(std::fabs(len - 1.0) < 1e-9, "a noise vector is %g long", len);
    }
    CHECK(ao_noise(0).empty());

    CHECK(ao_noise(4)[3][0] == ao_noise(4)[3][0]);
}

TEST(ao_a_flat_surface_does_not_occlude_itself) {
    AoSettings s;
    s.samples = 8;
    std::vector<f64> expected(8, 10.0), sampled(8, 10.0);
    CHECK_MSG(std::fabs(ao_factor(sampled, expected, s) - 1.0) < 1e-9,
              "a flat surface came out %g occluded",
              1.0 - ao_factor(sampled, expected, s));

    for (f64& d : sampled) d = 10.0 - s.bias * 0.5;
    CHECK(ao_factor(sampled, expected, s) > 0.99);
}

TEST(ao_a_corner_is_occluded) {
    AoSettings s;
    s.samples = 8;
    s.power = 1.0;
    std::vector<f64> expected(8, 10.0);
    std::vector<f64> sampled(8, 10.0);
    for (int i = 0; i < 4; ++i) sampled[i] = 9.99;

    const f64 v = ao_factor(sampled, expected, s);
    CHECK_MSG(v < 0.7 && v > 0.3, "a half-occluded corner came out at %g", v);
}

TEST(ao_a_distant_foreground_object_does_not_cast_a_halo) {
    AoSettings s;
    s.samples = 8;
    s.radius = 0.05;
    s.power = 1.0;
    std::vector<f64> expected(8, 10.0);
    std::vector<f64> sampled(8, 10.0);
    for (int i = 0; i < 4; ++i) sampled[i] = 1.0;

    const f64 v = ao_factor(sampled, expected, s);
    CHECK_MSG(v > 0.97, "a distant object occluded by %g", 1.0 - v);
}

TEST(ao_factor_stays_in_range_and_handles_nothing) {
    AoSettings s;
    CHECK(ao_factor({}, {}, s) == 1.0);
    std::vector<f64> expected(4, 10.0), sampled(4, 0.0);
    const f64 v = ao_factor(sampled, expected, s);
    CHECK(v >= 0.0 && v <= 1.0);
}

TEST(outline_threshold_is_relative_to_depth) {
    OutlineSettings s;
    s.depth_relative = 0.02;
    s.compensate_slope = false;
    const f64 n[3] = {0, 0, 1};

    CHECK(is_outline(1.0, 1.03, n, n, 0.0, s));
    CHECK(is_outline(100.0, 103.0, n, n, 0.0, s));
    CHECK(!is_outline(1.0, 1.01, n, n, 0.0, s));
    CHECK_MSG(!is_outline(100.0, 101.0, n, n, 0.0, s),
              "a 1 percent step became an edge at range");
}

TEST(outline_does_not_rim_a_face_seen_edge_on) {
    OutlineSettings s;
    s.depth_relative = 0.02;
    s.compensate_slope = true;
    const f64 n[3] = {0, 0, 1};

    CHECK_MSG(!is_outline(10.0, 10.9, n, n, 1.0, s),
              "a steep face was rimmed");
    CHECK(is_outline(10.0, 10.9, n, n, 0.0, s));

    s.compensate_slope = false;
    CHECK(is_outline(10.0, 10.9, n, n, 1.0, s));
}

TEST(outline_finds_a_crease_by_its_normals) {
    OutlineSettings s;
    s.normal_degrees = 25.0;
    const f64 flat[3] = {0, 0, 1};
    const f64 tilted[3] = {0.7071, 0, 0.7071};      // 45 degrees
    const f64 barely[3] = {0.17, 0, 0.985};         // about 10 degrees

    CHECK(is_outline(10.0, 10.0, flat, tilted, 0.0, s));
    CHECK_MSG(!is_outline(10.0, 10.0, flat, barely, 0.0, s),
              "a 10 degree bend became an outline at a 25 degree threshold");
}

TEST(oit_weight_falls_off_with_depth) {
    OitSettings s;
    const f64 near = oit_weight(1.0, 0.5, s);
    const f64 middle = oit_weight(100.0, 0.5, s);
    const f64 far = oit_weight(900.0, 0.5, s);
    CHECK_MSG(near > middle && middle > far,
              "weights: near %g, middle %g, far %g", near, middle, far);
    CHECK(far > 0.0);
}

TEST(oit_weight_rises_with_alpha) {
    OitSettings s;
    CHECK(oit_weight(10.0, 0.9, s) > oit_weight(10.0, 0.1, s));
    CHECK(oit_weight(10.0, 0.0, s) == 0.0);
}

TEST(oit_weight_cannot_saturate_a_half_float_target) {
    OitSettings s;
    for (f64 d : {0.0, 0.001, 0.1, 1.0, 10.0, 1000.0, 1e6, -5.0})
        for (f64 a : {0.001, 0.5, 1.0, 2.0}) {
            const f64 w = oit_weight(d, a, s);
            CHECK_MSG(w <= s.max_weight + 1e-9, "weight %g at depth %g alpha %g",
                      w, d, a);
            CHECK(std::isfinite(w));
            CHECK(w >= 0.0);
        }
    CHECK(s.max_weight < 65504.0);
}

TEST(oit_result_does_not_depend_on_the_order_fragments_arrived_in) {
    OitSettings s;
    const Color background = Color::rgb(0.1f, 0.1f, 0.1f);
    std::vector<OitFragment> frags = {
        {Color::rgba(1.0f, 0.0f, 0.0f, 0.4f), 5.0},
        {Color::rgba(0.0f, 1.0f, 0.0f, 0.3f), 30.0},
        {Color::rgba(0.0f, 0.0f, 1.0f, 0.6f), 12.0},
        {Color::rgba(1.0f, 1.0f, 0.0f, 0.2f), 80.0},
    };

    OitAccum forward;
    for (const auto& f : frags) forward.add(f, s);
    OitAccum backward;
    for (auto it = frags.rbegin(); it != frags.rend(); ++it) backward.add(*it, s);
    OitAccum shuffled;
    for (int i : {2, 0, 3, 1}) shuffled.add(frags[static_cast<std::size_t>(i)], s);

    const Color a = oit_resolve(forward, background);
    const Color b = oit_resolve(backward, background);
    const Color c = oit_resolve(shuffled, background);
    CHECK_MSG(std::fabs(a.r - b.r) < 1e-6 && std::fabs(a.g - b.g) < 1e-6 &&
                  std::fabs(a.b - b.b) < 1e-6,
              "reversing the order changed the result");
    CHECK(std::fabs(a.r - c.r) < 1e-6 && std::fabs(a.b - c.b) < 1e-6);
}

TEST(oit_a_near_surface_dominates_a_far_one) {
    OitSettings s;
    const Color background = Color::rgb(0.0f, 0.0f, 0.0f);

    OitAccum acc;
    acc.add({Color::rgba(1.0f, 0.0f, 0.0f, 0.5f), 2.0}, s);
    acc.add({Color::rgba(0.0f, 0.0f, 1.0f, 0.5f), 500.0}, s);
    const Color out = oit_resolve(acc, background);
    CHECK_MSG(out.r > out.b, "the far surface won: r=%g b=%g", out.r, out.b);
}

TEST(oit_nothing_transparent_leaves_the_background) {
    OitSettings s;
    const Color background = Color::rgb(0.2f, 0.4f, 0.6f);
    OitAccum empty;
    const Color out = oit_resolve(empty, background);
    CHECK(std::fabs(out.r - background.r) < 1e-6);
    CHECK(std::fabs(out.g - background.g) < 1e-6);
    CHECK(std::fabs(out.b - background.b) < 1e-6);

    OitAccum clear;
    clear.add({Color::rgba(1, 1, 1, 0.0f), 10.0}, s);
    const Color still = oit_resolve(clear, background);
    CHECK(std::fabs(still.r - background.r) < 1e-6);
}

TEST(oit_an_opaque_fragment_hides_the_background) {
    OitSettings s;
    const Color background = Color::rgb(1.0f, 1.0f, 1.0f);
    OitAccum acc;
    acc.add({Color::rgba(0.0f, 0.0f, 0.0f, 1.0f), 10.0}, s);
    const Color out = oit_resolve(acc, background);
    CHECK_MSG(out.r < 0.01f, "an opaque black fragment let %g through", out.r);
    CHECK(std::fabs(out.a - 1.0f) < 1e-6);
}

TEST(oit_revealage_is_a_product_so_layers_compound) {
    OitSettings s;
    OitAccum acc;
    for (int i = 0; i < 4; ++i)
        acc.add({Color::rgba(1.0f, 1.0f, 1.0f, 0.5f), 10.0}, s);
    CHECK_MSG(std::fabs(acc.revealage - 0.0625) < 1e-9,
              "revealage came out %g", acc.revealage);
}

TEST(oit_the_result_stays_in_range) {
    OitSettings s;
    const Color background = Color::rgb(0.5f, 0.5f, 0.5f);
    OitAccum acc;
    for (int i = 0; i < 200; ++i)
        acc.add({Color::rgba(1.0f, 0.9f, 0.8f, 0.9f),
                 1.0 + static_cast<f64>(i)}, s);
    const Color out = oit_resolve(acc, background);
    for (f32 v : {out.r, out.g, out.b}) {
        CHECK_MSG(std::isfinite(v), "the resolve produced a non-finite channel");
        CHECK_MSG(v >= -1e-6f && v <= 1.0f + 1e-3f, "a channel came out at %g", v);
    }
}
