#include "render/style.hpp"

#include <cmath>

namespace gview {

const char* visual_style_name(VisualStyle s) noexcept {
    switch (s) {
        case VisualStyle::Wireframe:   return "Wireframe";
        case VisualStyle::HiddenLine:  return "Hidden Line";
        case VisualStyle::Shaded:      return "Shaded";
        case VisualStyle::ShadedEdges: return "Shaded with Edges";
        case VisualStyle::XRay:        return "X-Ray";
        case VisualStyle::Sketch:      return "Sketch";
        case VisualStyle::Section:     return "Section";
        case VisualStyle::Realistic:   return "Realistic";
    }
    return "?";
}

StyleSettings settings_for(VisualStyle s) noexcept {
    StyleSettings t;

    switch (s) {
        case VisualStyle::Wireframe:
            t.draw_faces = false;
            t.depth_write = false;
            t.edge_token = "geometry";
            break;

        case VisualStyle::HiddenLine:
            // Faces in the background colour, so they occlude the edges behind them.
            t.draw_faces = true;
            t.face_token = "canvas.background";
            t.hide_obscured_edges = true;
            t.draw_silhouettes = true;
            t.edge_token = "geometry";
            break;

        case VisualStyle::Shaded:
            t.draw_edges = false;
            break;

        case VisualStyle::ShadedEdges:
            t.draw_silhouettes = true;
            break;

        case VisualStyle::XRay:
            // No depth write, or the translucent surface hides what is behind it.
            t.face_opacity = 0.35f;
            t.depth_write = false;
            t.two_sided = true;
            t.draw_silhouettes = true;
            break;

        case VisualStyle::Sketch:
            t.jitter_edges = true;
            t.draw_silhouettes = true;
            t.edge_width = 1.6f;
            t.face_token = "ui.surface";
            break;

        case VisualStyle::Section:
            // Two-sided: looking into a section shows back faces.
            t.two_sided = true;
            t.draw_silhouettes = true;
            break;

        case VisualStyle::Realistic:
            t.shadows = true;
            t.ambient = 0.1f;
            t.draw_edges = false;
            break;
    }
    return t;
}

bool is_silhouette(f64 n1x, f64 n1y, f64 n1z, f64 n2x, f64 n2y, f64 n2z,
                   f64 eye_x, f64 eye_y, f64 eye_z, f64 px, f64 py, f64 pz,
                   bool orthographic) noexcept {
    f64 vx = eye_x, vy = eye_y, vz = eye_z;
    if (!orthographic) {
        vx = eye_x - px;
        vy = eye_y - py;
        vz = eye_z - pz;
    }
    const f64 len = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (len < 1e-15) return false;
    vx /= len;
    vy /= len;
    vz /= len;

    const f64 d1 = n1x * vx + n1y * vy + n1z * vz;
    const f64 d2 = n2x * vx + n2y * vy + n2z * vz;
    return (d1 > 0.0) != (d2 > 0.0);
}

const char* wheel_verb_name(WheelVerb v) noexcept {
    switch (v) {
        case WheelVerb::None:   return "";
        case WheelVerb::Zoom:   return "Zoom";
        case WheelVerb::Orbit:  return "Orbit";
        case WheelVerb::Pan:    return "Pan";
        case WheelVerb::Rewind: return "Rewind";
        case WheelVerb::Center: return "Center";
        case WheelVerb::Walk:   return "Walk";
        case WheelVerb::Look:   return "Look";
        case WheelVerb::Up:     return "Up/Down";
    }
    return "";
}

void SteeringWheel::show(f64 x, f64 y, bool mini) {
    visible_ = true;
    layout_.cx = x;
    layout_.cy = y;
    layout_.mini = mini;
    layout_.outer_radius = mini ? 36.0 : 56.0;
    layout_.inner_radius = mini ? 10.0 : 18.0;
}

std::vector<WheelVerb> SteeringWheel::verbs() const {
    if (layout_.mini)
        return {WheelVerb::Zoom, WheelVerb::Orbit, WheelVerb::Pan,
                WheelVerb::Rewind};
    return {WheelVerb::Zoom,   WheelVerb::Rewind, WheelVerb::Pan,
            WheelVerb::Orbit,  WheelVerb::Center, WheelVerb::Walk,
            WheelVerb::Look,   WheelVerb::Up};
}

WheelVerb SteeringWheel::hit(f64 x, f64 y) const {
    if (!visible_) return WheelVerb::None;

    const f64 dx = x - layout_.cx, dy = y - layout_.cy;
    const f64 r = std::hypot(dx, dy);
    if (r < layout_.inner_radius || r > layout_.outer_radius)
        return WheelVerb::None;

    const std::vector<WheelVerb> v = verbs();
    if (v.empty()) return WheelVerb::None;

    constexpr f64 kTwoPi = 6.28318530717958647692;
    f64 angle = std::atan2(dx, -dy);
    if (angle < 0.0) angle += kTwoPi;

    const std::size_t index =
        static_cast<std::size_t>(angle / kTwoPi * static_cast<f64>(v.size()));
    return v[std::min(index, v.size() - 1)];
}

WheelVerb SteeringWheel::drag(Camera& c, f64 dx, f64 dy) {
    switch (held_) {
        case WheelVerb::Orbit:
            c.orbit(-dx * 0.008, dy * 0.008);
            break;
        case WheelVerb::Pan:
            c.pan(dx, dy);
            break;
        case WheelVerb::Zoom:
            c.dolly(dy > 0.0 ? 1.02 : 0.98);
            break;
        case WheelVerb::Up:
            c.set_target(Point3::from(c.target().x.to_f64(),
                                      c.target().y.to_f64(),
                                      c.target().z.to_f64() - dy * 0.01));
            break;
        case WheelVerb::Look:
            c.orbit(-dx * 0.004, dy * 0.004);
            break;
        default:
            break;
    }
    return held_;
}

}
