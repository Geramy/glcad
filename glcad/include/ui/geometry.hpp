#pragma once

#include <algorithm>

#include "math/config.hpp"

namespace gui {

using gcm::f64;
using gcm::f32;
using gcm::i32;
using gcm::u16;
using gcm::u32;
using gcm::u8;

struct Vec2 {
    f32 x{0}, y{0};
    friend Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend Vec2 operator*(Vec2 a, f32 s) noexcept { return {a.x * s, a.y * s}; }
};

struct Insets {
    f32 l{0}, t{0}, r{0}, b{0};
    static Insets all(f32 v) noexcept { return {v, v, v, v}; }
    static Insets xy(f32 x, f32 y) noexcept { return {x, y, x, y}; }
    f32 horizontal() const noexcept { return l + r; }
    f32 vertical() const noexcept { return t + b; }
};

struct Rect {
    f32 x{0}, y{0}, w{0}, h{0};

    f32  right() const noexcept { return x + w; }
    f32  bottom() const noexcept { return y + h; }
    Vec2 centre() const noexcept { return {x + w * 0.5f, y + h * 0.5f}; }
    Vec2 size() const noexcept { return {w, h}; }
    bool empty() const noexcept { return w <= 0.0f || h <= 0.0f; }

    bool contains(Vec2 p) const noexcept {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }
    bool intersects(const Rect& o) const noexcept {
        return !(o.x >= right() || o.right() <= x || o.y >= bottom() || o.bottom() <= y);
    }
    Rect intersect(const Rect& o) const noexcept {
        const f32 nx = std::max(x, o.x), ny = std::max(y, o.y);
        const f32 nr = std::min(right(), o.right()), nb = std::min(bottom(), o.bottom());
        return {nx, ny, std::max(0.0f, nr - nx), std::max(0.0f, nb - ny)};
    }
    Rect deflate(const Insets& i) const noexcept {
        return {x + i.l, y + i.t, std::max(0.0f, w - i.horizontal()),
                std::max(0.0f, h - i.vertical())};
    }
    Rect inflate(const Insets& i) const noexcept {
        return {x - i.l, y - i.t, w + i.horizontal(), h + i.vertical()};
    }
    Rect offset(Vec2 d) const noexcept { return {x + d.x, y + d.y, w, h}; }

    Rect cut_top(f32 amount) noexcept {
        const f32 a = std::min(amount, h);
        const Rect r{x, y, w, a};
        y += a; h -= a;
        return r;
    }
    Rect cut_bottom(f32 amount) noexcept {
        const f32 a = std::min(amount, h);
        h -= a;
        return {x, y + h, w, a};
    }
    Rect cut_left(f32 amount) noexcept {
        const f32 a = std::min(amount, w);
        const Rect r{x, y, a, h};
        x += a; w -= a;
        return r;
    }
    Rect cut_right(f32 amount) noexcept {
        const f32 a = std::min(amount, w);
        w -= a;
        return {x + w, y, a, h};
    }
};

enum class Axis : u8 { Horizontal, Vertical };
enum class Align : u8 { Start, Centre, End, Stretch };

}
