#include "render/shading.hpp"

#include <algorithm>
#include <cmath>

namespace grender {
namespace {

constexpr f64 kPi = 3.14159265358979323846;

f64 dot3(const f64 a[3], const f64 b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Fixed generator: std engine seeding is not identical across stdlibs.
struct Rng {
    u32 state;
    explicit Rng(u32 seed) : state(seed ? seed : 1u) {}
    u32 next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    f64 unit() { return static_cast<f64>(next()) / 4294967296.0; }
    f64 signed_unit() { return unit() * 2.0 - 1.0; }
};

}

const char* edge_kind_name(EdgeKind k) noexcept {
    switch (k) {
        case EdgeKind::None:       return "none";
        case EdgeKind::Crease:     return "crease";
        case EdgeKind::Silhouette: return "silhouette";
        case EdgeKind::Boundary:   return "boundary";
        case EdgeKind::Tangent:    return "tangent";
    }
    return "";
}

EdgeKind classify_edge(const f64 n0[3], const f64* n1, const f64 eye_dir[3],
                       const EdgeClassify& s) noexcept {
    if (!n1) {
        return s.draw_boundaries ? EdgeKind::Boundary : EdgeKind::None;
    }

    // Silhouette first: a cylinder's outline is both smooth and a silhouette.
    if (s.draw_silhouettes) {
        const f64 a = dot3(n0, eye_dir);
        const f64 b = dot3(n1, eye_dir);
        if ((a > 0.0) != (b > 0.0)) return EdgeKind::Silhouette;
    }

    const f64 c = std::clamp(dot3(n0, n1), -1.0, 1.0);
    const f64 angle = std::acos(c) * 180.0 / kPi;
    if (angle >= s.crease_degrees) return EdgeKind::Crease;

    return s.draw_tangents ? EdgeKind::Tangent : EdgeKind::None;
}

std::vector<std::array<f64, 3>> ao_kernel(const AoSettings& s) {
    std::vector<std::array<f64, 3>> out;
    if (s.samples == 0) return out;
    out.reserve(s.samples);

    Rng rng(s.seed);
    std::size_t guard = 0;
    while (out.size() < s.samples && guard < s.samples * 64) {
        ++guard;
        // Rejection in the unit ball; normalising a cube would bias the diagonals.
        const f64 x = rng.signed_unit();
        const f64 y = rng.signed_unit();
        const f64 z = rng.unit();          // hemisphere: z >= 0
        const f64 len2 = x * x + y * y + z * z;
        if (len2 > 1.0 || len2 < 1e-12) continue;

        const f64 len = std::sqrt(len2);
        f64 v[3] = {x / len, y / len, z / len};

        const f64 t = static_cast<f64>(out.size()) /
                      static_cast<f64>(std::max<std::size_t>(1, s.samples));
        const f64 scale = 0.1 + 0.9 * t * t;
        for (int i = 0; i < 3; ++i) v[i] *= scale;
        out.push_back({v[0], v[1], v[2]});
    }
    return out;
}

std::vector<std::array<f64, 2>> ao_noise(u32 side, u32 seed) {
    std::vector<std::array<f64, 2>> out;
    if (side == 0) return out;
    Rng rng(seed);
    out.reserve(static_cast<std::size_t>(side) * side);
    for (u32 i = 0; i < side * side; ++i) {
        f64 x = rng.signed_unit(), y = rng.signed_unit();
        const f64 len = std::hypot(x, y);
        if (len < 1e-9) { x = 1.0; y = 0.0; }
        else            { x /= len; y /= len; }
        out.push_back({x, y});
    }
    return out;
}

f64 ao_factor(const std::vector<f64>& sample_depth,
              const std::vector<f64>& expected_depth, const AoSettings& s) {
    const std::size_t n = std::min(sample_depth.size(), expected_depth.size());
    if (n == 0) return 1.0;

    f64 occluded = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (sample_depth[i] >= expected_depth[i] - s.bias) continue;

        const f64 gap = std::fabs(expected_depth[i] - sample_depth[i]);
        const f64 range = s.radius > 0.0 ? std::clamp(s.radius / gap, 0.0, 1.0)
                                         : 1.0;
        occluded += range;
    }

    const f64 visibility = 1.0 - occluded / static_cast<f64>(n);
    return std::pow(std::clamp(visibility, 0.0, 1.0), std::max(0.0, s.power));
}

bool is_outline(f64 centre, f64 neighbour, const f64 nc[3], const f64 nn[3],
                f64 slope, const OutlineSettings& s) noexcept {
    const f64 scale = std::max(1e-6, std::fabs(centre));
    f64 threshold = s.depth_relative * scale;

    if (s.compensate_slope) threshold += std::fabs(slope);

    if (std::fabs(centre - neighbour) > threshold) return true;

    const f64 c = std::clamp(dot3(nc, nn), -1.0, 1.0);
    return std::acos(c) * 180.0 / kPi >= s.normal_degrees;
}

f64 oit_weight(f64 depth, f64 alpha, const OitSettings& s) noexcept {
    alpha = std::clamp(alpha, 0.0, 1.0);
    if (alpha <= 0.0) return 0.0;

    const f64 span = std::max(1e-9, s.far_plane - s.near_plane);
    const f64 z = std::clamp((depth - s.near_plane) / span, 0.0, 1.0);

    const f64 d = 1.0 - z;
    const f64 w = alpha * std::max(1e-2, d * d * d) * 1000.0;

    return std::clamp(w, 1e-4, s.max_weight);
}

void OitAccum::add(const OitFragment& f, const OitSettings& s) {
    const f64 alpha = std::clamp(static_cast<f64>(f.color.a), 0.0, 1.0);
    const f64 w = oit_weight(f.depth, alpha, s);

    // Premultiplied, or the result depends on fragment order.
    r += static_cast<f64>(f.color.r) * alpha * w;
    g += static_cast<f64>(f.color.g) * alpha * w;
    b += static_cast<f64>(f.color.b) * alpha * w;
    a += alpha * w;

    revealage *= (1.0 - alpha);
}

Color oit_resolve(const OitAccum& acc, Color background) {
    if (acc.a <= 1e-9) return background;

    const f64 inv = 1.0 / std::max(1e-9, acc.a);
    const f64 cr = acc.r * inv, cg = acc.g * inv, cb = acc.b * inv;
    const f64 reveal = std::clamp(acc.revealage, 0.0, 1.0);

    Color out;
    out.r = static_cast<f32>(cr * (1.0 - reveal) +
                             static_cast<f64>(background.r) * reveal);
    out.g = static_cast<f32>(cg * (1.0 - reveal) +
                             static_cast<f64>(background.g) * reveal);
    out.b = static_cast<f32>(cb * (1.0 - reveal) +
                             static_cast<f64>(background.b) * reveal);
    out.a = 1.0f;
    return out;
}

}
