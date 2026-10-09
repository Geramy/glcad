#pragma once

#include <array>
#include <vector>

#include "render/vertex.hpp"

namespace grender {

using ggeo::f32;
using ggeo::f64;
using ggeo::u32;

enum class EdgeKind : ggeo::u8 {
    None,
    Crease,
    Silhouette,
    Boundary,
    Tangent,
};

const char* edge_kind_name(EdgeKind k) noexcept;

struct EdgeClassify {
    // Degrees; below this the faces are smooth and the edge is not drawn.
    f64  crease_degrees{30.0};
    bool draw_silhouettes{true};
    bool draw_boundaries{true};
    bool draw_tangents{false};
};

// Null `n1` = boundary edge; `eye_dir` points from the edge toward the eye.
EdgeKind classify_edge(const f64 n0[3], const f64* n1, const f64 eye_dir[3],
                       const EdgeClassify& s) noexcept;

struct AoSettings {
    std::size_t samples{16};
    f64         radius{0.05};      // world units
    f64         bias{0.0025};      // depth slack, world units
    f64         power{1.5};
    u32         seed{0x5EED1234u};
};

std::vector<std::array<f64, 3>> ao_kernel(const AoSettings& s);

std::vector<std::array<f64, 2>> ao_noise(u32 side, u32 seed = 0xA0A0A0A0u);

// Returns visibility: 1 fully open, 0 fully occluded.
f64 ao_factor(const std::vector<f64>& sample_depth,
              const std::vector<f64>& expected_depth, const AoSettings& s);

struct OutlineSettings {
    // Relative to the pixel's depth, not an absolute distance.
    f64 depth_relative{0.02};
    // Degrees.
    f64 normal_degrees{25.0};
    bool compensate_slope{true};
    f64  thickness{1.5};      // pixels
};

bool is_outline(f64 centre, f64 neighbour, const f64 nc[3], const f64 nn[3],
                f64 slope, const OutlineSettings& s) noexcept;

struct OitSettings {
    f64 near_plane{0.1};
    f64 far_plane{1000.0};
    // Half float tops out at 65504.
    f64 max_weight{3000.0};
};

f64 oit_weight(f64 depth, f64 alpha, const OitSettings& s) noexcept;

struct OitFragment {
    Color color{};
    f64   depth{0.0};
};

struct OitAccum {
    f64 r{0}, g{0}, b{0}, a{0};   // premultiplied sum
    f64 revealage{1.0};           // product of (1 - alpha)

    void add(const OitFragment& f, const OitSettings& s);
};

Color oit_resolve(const OitAccum& acc, Color background);

}
