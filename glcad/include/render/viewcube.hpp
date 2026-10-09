#pragma once

#include <string>
#include <vector>

#include "render/camera.hpp"

namespace gview {

enum class CubeRegion : u8 { None, Face, Edge, Corner };

// (0,0,1) is the top face; (1,1,0) the front-right edge; (1,1,1) a corner.
struct CubePart {
    CubeRegion  region{CubeRegion::None};
    i32         nx{0}, ny{0}, nz{0};
    std::string label;

    bool valid() const noexcept { return region != CubeRegion::None; }
    void orientation(f64& azimuth, f64& elevation) const noexcept;
};

struct CubeLayout {
    f64 x{0}, y{0};             // top-left, viewport pixels
    f64 size{110.0};
    f64 margin{16.0};

    static CubeLayout top_right(f64 viewport_w, f64 /*viewport_h*/,
                                f64 size = 110.0, f64 margin = 16.0) {
        return {viewport_w - size - margin, margin, size, margin};
    }
    bool contains(f64 px, f64 py) const noexcept {
        return px >= x && px < x + size && py >= y && py < y + size;
    }
};

class ViewCube {
public:
    static const std::vector<CubePart>& parts();

    static CubePart pick(const Camera& cam, const CubeLayout& layout,
                         f64 px, f64 py);

    static bool part_screen_position(const Camera& cam, const CubeLayout& layout,
                                     const CubePart& part, f64& px, f64& py);

    static bool facing_camera(const Camera& cam, const CubePart& part);

    static void apply(Camera& cam, const CubePart& part);

    struct Drag {
        bool active{false};
        f64  start_x{0.0}, start_y{0.0};
        f64  start_azimuth{0.0}, start_elevation{0.0};
        f64  travelled{0.0};      // pixels, so a click is told from a drag
    };

    static bool begin_drag(const Camera& cam, const CubeLayout& layout, f64 px,
                           f64 py, Drag& out);
    static void update_drag(Camera& cam, Drag& d, f64 px, f64 py);
    static bool end_drag(Drag& d, f64 click_threshold_px = 4.0);

    static std::string top_label(const Camera& cam);
};

}
