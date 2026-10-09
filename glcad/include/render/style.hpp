#pragma once

#include <string>
#include <vector>

#include "render/camera.hpp"

namespace gview {

using gcm::f32;

enum class VisualStyle : u8 {
    Wireframe,
    HiddenLine,
    Shaded,
    ShadedEdges,
    XRay,
    Sketch,
    Section,
    Realistic,
};

const char* visual_style_name(VisualStyle s) noexcept;

struct StyleSettings {
    bool draw_faces{true};
    bool draw_edges{true};
    bool draw_silhouettes{false};
    bool hide_obscured_edges{false};
    bool dash_obscured_edges{false};
    bool two_sided{false};
    bool depth_write{true};

    f32  face_opacity{1.0f};
    f32  edge_width{1.0f};
    f32  ambient{0.25f};
    bool shadows{false};
    bool jitter_edges{false};

    std::string face_token{"geometry"};
    std::string edge_token{"geometry.endpoint"};
};

StyleSettings settings_for(VisualStyle s) noexcept;

bool is_silhouette(f64 n1x, f64 n1y, f64 n1z, f64 n2x, f64 n2y, f64 n2z,
                   f64 eye_x, f64 eye_y, f64 eye_z, f64 px, f64 py, f64 pz,
                   bool orthographic) noexcept;

enum class WheelVerb : u8 {
    None, Zoom, Orbit, Pan, Rewind, Center, Walk, Look, Up,
};

const char* wheel_verb_name(WheelVerb v) noexcept;

struct WheelLayout {
    f64 cx{0.0}, cy{0.0};
    f64 outer_radius{56.0};
    f64 inner_radius{18.0};
    bool mini{false};
};

class SteeringWheel {
public:
    void show(f64 x, f64 y, bool mini = false);
    void hide() noexcept { visible_ = false; }
    bool visible() const noexcept { return visible_; }
    WheelLayout layout() const noexcept { return layout_; }

    std::vector<WheelVerb> verbs() const;

    WheelVerb hit(f64 x, f64 y) const;

    WheelVerb drag(Camera& c, f64 dx, f64 dy);
    void      begin_drag(WheelVerb v) noexcept { held_ = v; }
    void      end_drag() noexcept { held_ = WheelVerb::None; }
    WheelVerb held() const noexcept { return held_; }

private:
    bool        visible_{false};
    WheelLayout layout_{};
    WheelVerb   held_{WheelVerb::None};
};

}
