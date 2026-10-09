#pragma once

#include <cstring>

#include "geometry/d2.hpp"
#include "geometry/d3.hpp"

namespace grender {

using ggeo::f32;
using ggeo::u32;
using ggeo::u64;
using ggeo::u8;

struct Color {
    f32 r{1}, g{1}, b{1}, a{1};

    static constexpr Color rgb(f32 r, f32 g, f32 b) noexcept { return {r, g, b, 1.0f}; }
    static constexpr Color rgba(f32 r, f32 g, f32 b, f32 a) noexcept { return {r, g, b, a}; }
};

enum class Role : u8 {
    Geometry,
    Preview,
    Construction,
    Endpoint,
    Snap,
    Selected,
    Highlight,
    Error,
    // Appended last so existing values keep their numbers.
    Annotation,
};

Color color_for(Role r) noexcept;

namespace d2 {
struct Vertex {
    f32 x{}, y{};
    f32 r{}, g{}, b{}, a{};

    static Vertex make(ggeo::f64 px, ggeo::f64 py, Color c) noexcept {
        return {static_cast<f32>(px), static_cast<f32>(py), c.r, c.g, c.b, c.a};
    }
};
static_assert(sizeof(Vertex) == 24);
}

namespace d3 {
struct Vertex {
    f32 x{}, y{}, z{};
    f32 nx{}, ny{}, nz{};
    f32 r{}, g{}, b{}, a{};

    static Vertex make(ggeo::f64 px, ggeo::f64 py, ggeo::f64 pz, Color c) noexcept {
        return {static_cast<f32>(px), static_cast<f32>(py), static_cast<f32>(pz),
                0.0f, 0.0f, 1.0f, c.r, c.g, c.b, c.a};
    }
};
static_assert(sizeof(Vertex) == 40);
}

struct Palette {
    Color of[9]{};

    bool operator==(const Palette& o) const noexcept {
        return std::memcmp(of, o.of, sizeof of) == 0;
    }

    Color get(Role r) const noexcept {
        const auto i = static_cast<std::size_t>(r);
        return i < 9 ? of[i] : Color::rgb(1, 1, 1);
    }
    void set(Role r, Color c) noexcept {
        const auto i = static_cast<std::size_t>(r);
        if (i < 9) of[i] = c;
    }

    static Palette builtin_dark() noexcept {
        Palette p;
        p.of[0] = Color::rgb(0.87f, 0.89f, 0.92f);          // Geometry
        p.of[1] = Color::rgba(0.35f, 0.75f, 1.0f, 0.85f);   // Preview
        p.of[2] = Color::rgba(0.62f, 0.65f, 0.72f, 0.8f);   // Construction
        p.of[3] = Color::rgb(0.45f, 0.50f, 0.58f);          // Endpoint
        p.of[4] = Color::rgb(1.0f, 0.78f, 0.28f);           // Snap
        p.of[5] = Color::rgb(0.20f, 0.68f, 1.0f);           // Selected
        p.of[6] = Color::rgb(1.0f, 1.0f, 1.0f);             // Highlight
        p.of[7] = Color::rgb(0.95f, 0.35f, 0.35f);          // Error
        p.of[8] = Color::rgb(0.87f, 0.89f, 0.92f);          // Annotation
        return p;
    }
};

const Palette& palette() noexcept;
void           set_palette(const Palette& p) noexcept;

u64            palette_revision() noexcept;

inline Color color_for(Role r) noexcept { return palette().get(r); }

}
