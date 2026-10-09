#include "ui/icons.hpp"

#include <algorithm>
#include <cmath>

namespace gui {

namespace {

struct Pen {
    Context& c;
    Rect     box;
    Color    col;
    f32      w;

    Vec2 at(f32 x, f32 y) const {
        return {box.x + x * box.w, box.y + y * box.h};
    }
    void line(f32 x0, f32 y0, f32 x1, f32 y1) const {
        c.draw().line(at(x0, y0), at(x1, y1), w, col);
    }
    void rect(f32 x0, f32 y0, f32 x1, f32 y1, f32 radius = 0.0f) const {
        const Vec2 a = at(x0, y0), b = at(x1, y1);
        c.draw().border({a.x, a.y, b.x - a.x, b.y - a.y}, radius, w, col);
    }
    void fill(f32 x0, f32 y0, f32 x1, f32 y1, f32 radius = 0.0f) const {
        const Vec2 a = at(x0, y0), b = at(x1, y1);
        c.draw().round_rect({a.x, a.y, b.x - a.x, b.y - a.y}, radius, col);
    }
    void dot(f32 x, f32 y, f32 r = 0.07f) const {
        const Vec2 p = at(x, y);
        const f32 rr = r * box.w;
        c.draw().round_rect({p.x - rr, p.y - rr, rr * 2, rr * 2}, rr, col);
    }
    void arc(f32 cx, f32 cy, f32 r, f32 a0, f32 a1, int steps = 0) const {
        if (steps <= 0) {
            const f32 arc_px = std::fabs(a1 - a0) * r * box.w;
            steps = static_cast<int>(arc_px / 1.6f);
            steps = std::clamp(steps, 8, 96);
        }
        const f32 span = a1 - a0;
        Vec2 prev = at(cx + r * std::cos(a0), cy + r * std::sin(a0));
        for (int i = 1; i <= steps; ++i) {
            const f32 t = a0 + span * (static_cast<f32>(i) / steps);
            const Vec2 p = at(cx + r * std::cos(t), cy + r * std::sin(t));
            c.draw().line(prev, p, w, col);
            prev = p;
        }
    }
    void ellipse(f32 cx, f32 cy, f32 rx, f32 ry) const {
        constexpr int steps = 28;
        Vec2 previous = at(cx + rx, cy);
        for (int i = 1; i <= steps; ++i) {
            const f32 a = 6.28318530718f * static_cast<f32>(i) / steps;
            const Vec2 next = at(cx + rx * std::cos(a), cy + ry * std::sin(a));
            c.draw().line(previous, next, w, col);
            previous = next;
        }
    }

    void circle(f32 cx, f32 cy, f32 r) const {
        arc(cx, cy, r, 0.0f, 6.28318530718f);
    }
    void chevron(f32 cx, f32 cy, f32 s, int dir) const {
        switch (dir) {
            case 0: line(cx + s, cy - s, cx - s, cy); line(cx - s, cy, cx + s, cy + s); break;
            case 1: line(cx - s, cy - s, cx + s, cy); line(cx + s, cy, cx - s, cy + s); break;
            case 2: line(cx - s, cy + s, cx, cy - s); line(cx, cy - s, cx + s, cy + s); break;
            default: line(cx - s, cy - s, cx, cy + s); line(cx, cy + s, cx + s, cy - s); break;
        }
    }
};

}

void draw_icon(Context& c, Icon icon, const Rect& r, Color col, f32 weight) {
    if (c.icons_available()) {
        if (const u32 cp = icon_codepoint(icon); cp != 0) {
            const f32 size = std::min(r.w, r.h) * 0.6f;
            c.draw().text(r, icon_utf8(icon), size, col, TextAlign::Centre);
            (void)weight;
            return;
        }
    }

    const f32 side = std::min(r.w, r.h) * 0.5f;
    Rect box{r.x + (r.w - side) * 0.5f, r.y + (r.h - side) * 0.5f, side, side};
    const Pen p{c, box, col, weight};

    switch (icon) {
        case Icon::Line:      p.line(0.12f, 0.82f, 0.88f, 0.18f);
                              p.dot(0.12f, 0.82f, 0.09f);
                              p.dot(0.88f, 0.18f, 0.09f); break;
        case Icon::Polyline:  p.line(0.10f, 0.78f, 0.36f, 0.34f);
                              p.line(0.36f, 0.34f, 0.62f, 0.62f);
                              p.line(0.62f, 0.62f, 0.90f, 0.22f);
                              p.dot(0.10f, 0.78f, 0.08f);
                              p.dot(0.36f, 0.34f, 0.07f);
                              p.dot(0.62f, 0.62f, 0.07f);
                              p.dot(0.90f, 0.22f, 0.08f); break;
        case Icon::Rect:      p.rect(0.14f, 0.24f, 0.86f, 0.76f);
                              p.dot(0.14f, 0.24f, 0.08f);
                              p.dot(0.86f, 0.76f, 0.08f); break;
        case Icon::Circle:    p.circle(0.5f, 0.5f, 0.36f);
                              p.line(0.5f, 0.5f, 0.86f, 0.5f);
                              p.dot(0.5f, 0.5f, 0.07f); break;
        case Icon::Arc:       p.arc(0.5f, 0.74f, 0.42f, 3.34f, 6.09f);
                              p.dot(0.5f, 0.74f, 0.06f);
                              p.line(0.5f, 0.74f, 0.92f, 0.74f); break;
        case Icon::Ellipse:   p.ellipse(0.5f, 0.5f, 0.42f, 0.26f);
                              p.dot(0.5f, 0.5f, 0.06f); break;
        case Icon::Spline:    p.arc(0.32f, 0.72f, 0.30f, 4.71f, 6.28f);
                              p.arc(0.68f, 0.28f, 0.30f, 1.57f, 3.14f);
                              p.dot(0.10f, 0.72f, 0.07f);
                              p.dot(0.90f, 0.28f, 0.07f); break;
        case Icon::Point:     p.dot(0.5f, 0.5f, 0.11f);
                              p.line(0.5f, 0.10f, 0.5f, 0.30f);
                              p.line(0.5f, 0.70f, 0.5f, 0.90f);
                              p.line(0.10f, 0.5f, 0.30f, 0.5f);
                              p.line(0.70f, 0.5f, 0.90f, 0.5f); break;
        case Icon::Hatch:     p.rect(0.12f, 0.2f, 0.88f, 0.8f);
                              for (int i = 0; i < 3; ++i) {
                                  const f32 o = 0.2f + i * 0.22f;
                                  p.line(0.14f, o + 0.24f, o + 0.42f, 0.22f);
                              }
                              break;

        case Icon::Move:      p.line(0.5f, 0.12f, 0.5f, 0.88f);
                              p.line(0.12f, 0.5f, 0.88f, 0.5f);
                              p.chevron(0.5f, 0.22f, 0.1f, 2);
                              p.chevron(0.5f, 0.78f, 0.1f, 3);
                              p.chevron(0.22f, 0.5f, 0.1f, 0);
                              p.chevron(0.78f, 0.5f, 0.1f, 1); break;
        case Icon::Copy:      p.rect(0.1f, 0.3f, 0.62f, 0.9f);
                              p.rect(0.38f, 0.1f, 0.9f, 0.7f); break;
        case Icon::Rotate:    p.arc(0.5f, 0.5f, 0.36f, 0.6f, 5.5f);
                              p.line(0.78f, 0.28f, 0.9f, 0.16f);
                              p.line(0.78f, 0.28f, 0.72f, 0.14f); break;
        case Icon::Scale:     p.rect(0.1f, 0.5f, 0.5f, 0.9f);
                              p.rect(0.1f, 0.1f, 0.9f, 0.9f);
                              p.line(0.5f, 0.5f, 0.9f, 0.1f); break;
        case Icon::Mirror:    p.line(0.5f, 0.08f, 0.5f, 0.92f);
                              p.line(0.12f, 0.7f, 0.38f, 0.3f);
                              p.line(0.38f, 0.3f, 0.38f, 0.7f);
                              p.line(0.12f, 0.7f, 0.38f, 0.7f);
                              p.line(0.88f, 0.7f, 0.62f, 0.3f);
                              p.line(0.62f, 0.3f, 0.62f, 0.7f);
                              p.line(0.88f, 0.7f, 0.62f, 0.7f); break;
        case Icon::Offset:    p.rect(0.26f, 0.32f, 0.74f, 0.68f);
                              p.rect(0.1f, 0.18f, 0.9f, 0.82f); break;
        case Icon::Trim:      p.line(0.1f, 0.5f, 0.55f, 0.5f);
                              p.line(0.7f, 0.5f, 0.9f, 0.5f);
                              p.line(0.62f, 0.15f, 0.62f, 0.85f); break;
        case Icon::Extend:    p.line(0.1f, 0.5f, 0.62f, 0.5f);
                              p.chevron(0.55f, 0.5f, 0.1f, 1);
                              p.line(0.85f, 0.15f, 0.85f, 0.85f); break;
        case Icon::Fillet:    p.line(0.15f, 0.15f, 0.15f, 0.5f);
                              p.arc(0.5f, 0.5f, 0.35f, 3.14f, 4.71f);
                              p.line(0.5f, 0.85f, 0.85f, 0.85f); break;
        case Icon::Erase:     p.line(0.2f, 0.2f, 0.8f, 0.8f);
                              p.line(0.8f, 0.2f, 0.2f, 0.8f); break;
        case Icon::Array:     for (int y = 0; y < 2; ++y)
                                  for (int x = 0; x < 3; ++x)
                                      p.fill(0.08f + x * 0.32f, 0.2f + y * 0.34f,
                                             0.28f + x * 0.32f, 0.44f + y * 0.34f, 1.0f);
                              break;
        case Icon::Explode:   p.dot(0.5f, 0.5f, 0.08f);
                              for (int i = 0; i < 6; ++i) {
                                  const f32 a = i * 1.0472f;
                                  p.line(0.5f + 0.18f * std::cos(a),
                                         0.5f + 0.18f * std::sin(a),
                                         0.5f + 0.44f * std::cos(a),
                                         0.5f + 0.44f * std::sin(a));
                              }
                              break;

        case Icon::Text:      p.line(0.15f, 0.2f, 0.85f, 0.2f);
                              p.line(0.5f, 0.2f, 0.5f, 0.85f); break;
        case Icon::Dimension: p.line(0.1f, 0.35f, 0.9f, 0.35f);
                              p.line(0.1f, 0.2f, 0.1f, 0.5f);
                              p.line(0.9f, 0.2f, 0.9f, 0.5f);
                              p.line(0.32f, 0.62f, 0.32f, 0.88f);
                              p.line(0.5f, 0.62f, 0.68f, 0.88f);
                              p.line(0.68f, 0.62f, 0.5f, 0.88f); break;
        case Icon::Leader:    p.line(0.1f, 0.85f, 0.5f, 0.45f);
                              p.line(0.5f, 0.45f, 0.9f, 0.45f);
                              p.dot(0.1f, 0.85f); break;
        case Icon::Table:     p.rect(0.1f, 0.2f, 0.9f, 0.8f);
                              p.line(0.1f, 0.4f, 0.9f, 0.4f);
                              p.line(0.1f, 0.6f, 0.9f, 0.6f);
                              p.line(0.43f, 0.2f, 0.43f, 0.8f); break;

        case Icon::Extrude:   p.rect(0.1f, 0.45f, 0.6f, 0.9f);
                              p.line(0.1f, 0.45f, 0.4f, 0.15f);
                              p.line(0.6f, 0.45f, 0.9f, 0.15f);
                              p.line(0.4f, 0.15f, 0.9f, 0.15f);
                              p.line(0.6f, 0.9f, 0.9f, 0.6f);
                              p.line(0.9f, 0.15f, 0.9f, 0.6f); break;
        case Icon::Revolve:   p.line(0.18f, 0.1f, 0.18f, 0.9f);
                              p.arc(0.55f, 0.5f, 0.3f, -1.57f, 1.57f);
                              p.line(0.55f, 0.2f, 0.55f, 0.8f); break;
        case Icon::Sweep:     p.arc(0.5f, 0.9f, 0.55f, 3.34f, 5.08f);
                              p.rect(0.06f, 0.42f, 0.26f, 0.62f); break;
        case Icon::Loft:      p.rect(0.12f, 0.62f, 0.88f, 0.86f);
                              p.rect(0.3f, 0.14f, 0.7f, 0.34f);
                              p.line(0.12f, 0.62f, 0.3f, 0.34f);
                              p.line(0.88f, 0.62f, 0.7f, 0.34f); break;
        case Icon::Shell:     p.rect(0.1f, 0.2f, 0.9f, 0.8f);
                              p.rect(0.26f, 0.34f, 0.74f, 0.66f); break;
        case Icon::Union:     p.circle(0.37f, 0.5f, 0.28f);
                              p.circle(0.63f, 0.5f, 0.28f); break;
        case Icon::Subtract:  p.circle(0.37f, 0.5f, 0.28f);
                              p.arc(0.63f, 0.5f, 0.28f, -1.05f, 1.05f); break;
        case Icon::Intersect: p.arc(0.37f, 0.5f, 0.28f, -1.05f, 1.05f);
                              p.arc(0.63f, 0.5f, 0.28f, 2.09f, 4.19f); break;
        case Icon::Box:       p.rect(0.1f, 0.35f, 0.68f, 0.9f);
                              p.line(0.1f, 0.35f, 0.32f, 0.13f);
                              p.line(0.68f, 0.35f, 0.9f, 0.13f);
                              p.line(0.32f, 0.13f, 0.9f, 0.13f);
                              p.line(0.9f, 0.13f, 0.9f, 0.68f);
                              p.line(0.68f, 0.9f, 0.9f, 0.68f); break;

        case Icon::Pan:       p.chevron(0.5f, 0.16f, 0.11f, 2);
                              p.chevron(0.5f, 0.84f, 0.11f, 3);
                              p.chevron(0.16f, 0.5f, 0.11f, 0);
                              p.chevron(0.84f, 0.5f, 0.11f, 1); break;
        case Icon::Zoom:      p.circle(0.42f, 0.42f, 0.3f);
                              p.line(0.64f, 0.64f, 0.9f, 0.9f); break;
        case Icon::ZoomExtents: p.rect(0.1f, 0.2f, 0.9f, 0.8f);
                              p.line(0.32f, 0.42f, 0.32f, 0.58f);
                              p.line(0.68f, 0.42f, 0.68f, 0.58f);
                              p.line(0.32f, 0.5f, 0.68f, 0.5f); break;
        case Icon::Orbit:     p.circle(0.5f, 0.5f, 0.22f);
                              p.arc(0.5f, 0.5f, 0.44f, 0.0f, 6.283f); break;
        case Icon::Layers:    p.line(0.5f, 0.14f, 0.9f, 0.36f);
                              p.line(0.9f, 0.36f, 0.5f, 0.58f);
                              p.line(0.5f, 0.58f, 0.1f, 0.36f);
                              p.line(0.1f, 0.36f, 0.5f, 0.14f);
                              p.line(0.1f, 0.6f, 0.5f, 0.82f);
                              p.line(0.5f, 0.82f, 0.9f, 0.6f); break;
        case Icon::Grid:      for (int i = 1; i < 3; ++i) {
                                  p.line(0.1f + i * 0.267f, 0.1f, 0.1f + i * 0.267f, 0.9f);
                                  p.line(0.1f, 0.1f + i * 0.267f, 0.9f, 0.1f + i * 0.267f);
                              }
                              p.rect(0.1f, 0.1f, 0.9f, 0.9f); break;
        case Icon::Ortho:     p.line(0.15f, 0.85f, 0.15f, 0.2f);
                              p.line(0.15f, 0.85f, 0.85f, 0.85f); break;
        case Icon::Snap:      p.line(0.2f, 0.8f, 0.8f, 0.2f);
                              p.rect(0.42f, 0.42f, 0.58f, 0.58f); break;

        case Icon::ChevronLeft:  p.chevron(0.5f, 0.5f, 0.22f, 0); break;
        case Icon::ChevronRight: p.chevron(0.5f, 0.5f, 0.22f, 1); break;
        case Icon::ChevronUp:    p.chevron(0.5f, 0.5f, 0.22f, 2); break;
        case Icon::ChevronDown:  p.chevron(0.5f, 0.5f, 0.22f, 3); break;
        case Icon::Close:     p.line(0.25f, 0.25f, 0.75f, 0.75f);
                              p.line(0.75f, 0.25f, 0.25f, 0.75f); break;
        case Icon::Menu:      p.line(0.15f, 0.3f, 0.85f, 0.3f);
                              p.line(0.15f, 0.5f, 0.85f, 0.5f);
                              p.line(0.15f, 0.7f, 0.85f, 0.7f); break;
        case Icon::Plus:      p.line(0.5f, 0.2f, 0.5f, 0.8f);
                              p.line(0.2f, 0.5f, 0.8f, 0.5f); break;
        case Icon::Minus:     p.line(0.2f, 0.5f, 0.8f, 0.5f); break;
        case Icon::Lock:      p.rect(0.22f, 0.46f, 0.78f, 0.88f, 2.0f);
                              p.arc(0.5f, 0.46f, 0.19f, 3.14f, 6.283f); break;
        case Icon::Unlock:    p.rect(0.22f, 0.46f, 0.78f, 0.88f, 2.0f);
                              p.arc(0.66f, 0.46f, 0.19f, 3.14f, 5.0f); break;
        case Icon::Eye:       p.arc(0.5f, 0.78f, 0.44f, 3.6f, 5.82f);
                              p.arc(0.5f, 0.22f, 0.44f, 0.46f, 2.68f);
                              p.circle(0.5f, 0.5f, 0.13f); break;
        case Icon::EyeOff:    p.arc(0.5f, 0.78f, 0.44f, 3.6f, 5.82f);
                              p.arc(0.5f, 0.22f, 0.44f, 0.46f, 2.68f);
                              p.line(0.15f, 0.85f, 0.85f, 0.15f); break;
        case Icon::Snowflake: for (int i = 0; i < 3; ++i) {
                                  const f32 a = i * 1.0472f;
                                  p.line(0.5f - 0.38f * std::cos(a), 0.5f - 0.38f * std::sin(a),
                                         0.5f + 0.38f * std::cos(a), 0.5f + 0.38f * std::sin(a));
                              }
                              break;
        case Icon::Pin:       p.line(0.5f, 0.55f, 0.5f, 0.9f);
                              p.rect(0.3f, 0.12f, 0.7f, 0.55f, 2.0f); break;
        case Icon::Undo:      p.arc(0.5f, 0.55f, 0.32f, 3.14f, 5.9f);
                              p.line(0.18f, 0.55f, 0.18f, 0.28f);
                              p.line(0.18f, 0.28f, 0.42f, 0.4f); break;
        case Icon::Redo:      p.arc(0.5f, 0.55f, 0.32f, 3.52f, 6.28f);
                              p.line(0.82f, 0.55f, 0.82f, 0.28f);
                              p.line(0.82f, 0.28f, 0.58f, 0.4f); break;
        case Icon::Settings:  p.circle(0.5f, 0.5f, 0.18f);
                              for (int i = 0; i < 6; ++i) {
                                  const f32 a = i * 1.0472f;
                                  p.line(0.5f + 0.26f * std::cos(a), 0.5f + 0.26f * std::sin(a),
                                         0.5f + 0.44f * std::cos(a), 0.5f + 0.44f * std::sin(a));
                              }
                              break;
        case Icon::CheckPlan: p.line(0.04f, 0.08f, 0.68f, 0.08f);
                              p.line(0.68f, 0.08f, 0.68f, 0.13f);
                              p.line(0.68f, 0.68f, 0.68f, 0.96f);
                              p.line(0.68f, 0.96f, 0.04f, 0.96f);
                              p.line(0.04f, 0.96f, 0.04f, 0.08f);
                              p.line(0.17f, 0.24f, 0.36f, 0.24f);
                              p.line(0.17f, 0.24f, 0.17f, 0.80f);
                              p.line(0.17f, 0.80f, 0.54f, 0.80f);
                              p.line(0.17f, 0.51f, 0.34f, 0.51f);
                              p.line(0.34f, 0.51f, 0.34f, 0.80f);
                              p.line(0.54f, 0.68f, 0.54f, 0.80f);
                              p.circle(0.66f, 0.39f, 0.24f);
                              p.line(0.83f, 0.56f, 1.0f, 0.75f); break;
        case Icon::Search:    p.circle(0.42f, 0.42f, 0.28f);
                              p.line(0.63f, 0.63f, 0.88f, 0.88f); break;
        default: break;
    }
}

const char* icon_name(Icon i) noexcept {
    switch (i) {
        case Icon::Line: return "Line";
        case Icon::Rect: return "Rectangle";
        case Icon::Circle: return "Circle";
        case Icon::Arc: return "Arc";
        case Icon::Hatch: return "Hatch";
        case Icon::Dimension: return "Dimension";
        case Icon::Extrude: return "Extrude";
        case Icon::Layers: return "Layers";
        case Icon::CheckPlan: return "Check plan";
        default: return "";
    }
}

}
