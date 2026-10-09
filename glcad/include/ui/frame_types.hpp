#pragma once

#include "render/scene.hpp"

#include <cstdint>
#include <vector>
#include <memory>
#include "exchange/underlay_image.hpp"

#include "render/vertex.hpp"

namespace gcapp {
namespace gbool { struct PreviewState; }

using Vertex = grender::d2::Vertex;

// std140: field offsets must match the shaders' Frame block.
struct FrameUniforms {
    float view_proj[16];       // 0
    float inv_view_proj[16];   // 64: for unprojecting a pixel to a ray
    float eye[4];              // 128
    float viewport[4];         // 144: width, height, text px range, plan (1 or 0)
    float background[4];       // 160
    float grid_minor[4];       // 176
    float grid_major[4];       // 192
    float axis_x[4];           // 208
    float axis_y[4];           // 224
    // plane: x0, y0, x1, y1; plane_style: tint alpha, inner highlight width (px).
    float plane[4];            // 240
    float plane_style[4];      // 256
    float plane_origin[4];     // 272
    float plane_u[4];          // 288
    float plane_v[4];          // 304
    float plane_n[4];          // 320
    // section: nx, ny, nz, d; a zero normal means no section.
    float section[4];          // 336
};
static_assert(sizeof(FrameUniforms) == 352, "std140 layout the shaders assume");

struct LineVertex {
    float pos[3];
    float color[4];
    float width_px{1.0f};
};
static_assert(sizeof(LineVertex) == 32);

struct UiInstance {
    float rect[4];      // x, y, w, h  (logical px)
    float color[4];
    float params[4];    // radius, thickness, kind (0 fill, 1 border, 2 shadow), spread
    float clip[4];
};

struct TextVertex {
    float pos[2];
    float uv[2];
    float color[4];
    float clip[4];
};

struct ArcInstance {
    float centre[4];   // xyz, start angle
    float major[4];    // U * major radius, end angle
    float minor[4];    // V * minor radius, stroke width in pixels
    float color[4];
};

struct MeshVertex {
    float pos[3];
    float normal[3];
    float color[4];
};

// Draw order is per layer slice; never batch the frame by primitive kind.
struct UiLayers {
    enum class Kind { Surface, Triangle, Text };
    struct Run { Kind kind; std::size_t first, count; };
    std::vector<Run> runs;
    void append(Kind kind, std::size_t first, std::size_t count) {
        if (!count) return;
        if (!runs.empty() && runs.back().kind == kind &&
            runs.back().first + runs.back().count == first)
            runs.back().count += count;
        else runs.push_back({kind, first, count});
    }
    static constexpr int kCount = 6;
    std::size_t ui[kCount + 1]{};
    std::size_t tris[kCount + 1]{};
    std::size_t text[kCount + 1]{};
};

struct ImageDraw {
    std::shared_ptr<const gplot::RasterImage> image;
    std::vector<TextVertex> vertices;
};

struct FrameData {
    const std::vector<LineVertex>*  static_lines{nullptr};
    const std::vector<ArcInstance>* static_arcs{nullptr};
    std::uint64_t                   static_version{0};
    const std::vector<ArcInstance>* canvas_arcs{nullptr};
    const std::vector<LineVertex>* canvas_lines{nullptr};
    const std::vector<LineVertex>* canvas_points{nullptr};
    std::size_t sketch_lines{0};
    const std::vector<MeshVertex>* mesh{nullptr};
    std::uint64_t mesh_version{0};
    const std::vector<grender::DrawItem>* mesh_items{nullptr};
    std::size_t preview_first{0}, preview_count{0};
    bool        preview_cut{false};
    const gbool::PreviewState* preview_gpu{nullptr};
    const std::vector<UiInstance>* ui{nullptr};
    const std::vector<TextVertex>* text{nullptr};
    const std::vector<TextVertex>* tris{nullptr};
    const UiLayers* layers{nullptr};

    const std::vector<ImageDraw>* images{nullptr};
    FrameUniforms uniforms{};
};

}
