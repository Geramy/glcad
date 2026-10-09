#pragma once

#include <string>
#include <vector>

#include "render/theme.hpp"
#include "ui/geometry.hpp"

namespace gui {

using Color = gtheme::Color;

enum class Prim : u8 { Rect, RoundRect, Border, Line, Text, Shadow, Icon, Clip, Unclip, Triangle };

enum class TextAlign : u8 { Left, Centre, Right };

struct DrawCmd {
    Prim  prim{Prim::Rect};
    Rect  rect{};
    Color color{};
    f32   radius{0};
    f32   thickness{1};
    Vec2  a{}, b{}, c{};
    std::string text;
    f32   font_size{13};
    TextAlign align{TextAlign::Left};
    u32   icon{0};
};

class DrawList {
public:
    void clear() noexcept { cmds_.clear(); clip_depth_ = 0; }

    void rect(const Rect& r, Color c) { push({Prim::Rect, r, c}); }
    void round_rect(const Rect& r, f32 radius, Color c) {
        DrawCmd d{Prim::RoundRect, r, c};
        d.radius = radius;
        push(std::move(d));
    }
    void border(const Rect& r, f32 radius, f32 thickness, Color c) {
        DrawCmd d{Prim::Border, r, c};
        d.radius = radius;
        d.thickness = thickness;
        push(std::move(d));
    }
    void shadow(const Rect& r, f32 radius, f32 spread, Color c) {
        DrawCmd d{Prim::Shadow, r, c};
        d.radius = radius;
        d.thickness = spread;
        push(std::move(d));
    }
    void line(Vec2 p0, Vec2 p1, f32 thickness, Color c) {
        DrawCmd d{Prim::Line, {}, c};
        d.a = p0; d.b = p1; d.thickness = thickness;
        push(std::move(d));
    }
    void text(const Rect& r, std::string s, f32 size, Color c,
              TextAlign al = TextAlign::Left) {
        DrawCmd d{Prim::Text, r, c};
        d.text = std::move(s);
        d.font_size = size;
        d.align = al;
        push(std::move(d));
    }
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color col) {
        DrawCmd d{Prim::Triangle, {}, col};
        d.a = a; d.b = b; d.c = c;
        push(std::move(d));
    }
    void polygon(const std::vector<Vec2>& pts, Color col) {
        for (std::size_t i = 1; i + 1 < pts.size(); ++i) triangle(pts[0], pts[i], pts[i + 1], col);
    }
    void icon(const Rect& r, u32 codepoint, Color c) {
        DrawCmd d{Prim::Icon, r, c};
        d.icon = codepoint;
        push(std::move(d));
    }

    void push_clip(const Rect& r) {
        DrawCmd d{Prim::Clip, r, {}};
        push(std::move(d));
        ++clip_depth_;
    }
    void pop_clip() {
        if (clip_depth_ == 0) return;
        push({Prim::Unclip, {}, {}});
        --clip_depth_;
    }

    const std::vector<DrawCmd>& commands() const noexcept { return cmds_; }
    void append(const DrawList& other) {
        cmds_.insert(cmds_.end(), other.cmds_.begin(), other.cmds_.end());
    }
    std::size_t size() const noexcept { return cmds_.size(); }
    u32 clip_depth() const noexcept { return clip_depth_; }

    std::size_t count(Prim p) const noexcept {
        std::size_t n = 0;
        for (const auto& c : cmds_) if (c.prim == p) ++n;
        return n;
    }

private:
    void push(DrawCmd d) { cmds_.push_back(std::move(d)); }
    std::vector<DrawCmd> cmds_;
    u32 clip_depth_{0};
};

}
