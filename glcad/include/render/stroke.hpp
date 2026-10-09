#pragma once

#include <map>
#include <string>
#include <vector>

#include "math/config.hpp"
#include "engine/stream.hpp"

namespace gtext {

using gcm::f32;
using gcm::f64;
using gcm::i32;
using gcm::u8;
using gcm::u16;
using gcm::u32;

struct StrokePoint {
    f32 x{0.0f}, y{0.0f};
};

struct StrokeGlyph {
    std::vector<std::vector<StrokePoint>> strokes;
    f32  advance{0.0f};
    f32  min_x{0.0f}, min_y{0.0f}, max_x{0.0f}, max_y{0.0f};
    bool empty() const noexcept { return strokes.empty(); }
};

enum class ShxKind : u8 { Unknown, Shapes1_0, Shapes1_1, UniFont, BigFont };

struct StrokeFontInfo {
    std::string name;
    ShxKind     kind{ShxKind::Unknown};
    f32         above{0.0f};      // height above the baseline, font units
    f32         below{0.0f};
    u32         glyph_count{0};
    bool        unicode{false};
};

class StrokeFont {
public:
    bool load_shp(std::string_view source, std::string* error = nullptr);
    bool load_shx(gio::Reader& r, std::string* error = nullptr);
    bool load_file(const std::string& path, std::string* error = nullptr);

    bool loaded() const noexcept { return loaded_; }
    const StrokeFontInfo& info() const noexcept { return info_; }

    const StrokeGlyph* glyph(u32 codepoint) const;
    bool               has(u32 codepoint) const;
    std::size_t        size() const noexcept { return shapes_.size(); }

    std::vector<std::vector<StrokePoint>> stroke_text(std::string_view utf8,
                                                      f32 width_factor = 1.0f,
                                                      f32 oblique_deg = 0.0f) const;
    f32 text_width(std::string_view utf8, f32 width_factor = 1.0f) const;

    void clear();

private:
    struct Shape {
        std::vector<u8> code;
        std::string     name;
    };

    bool run(const std::vector<u8>& code, StrokeGlyph& out, f32& x, f32& y,
             bool& pen_down, f32 scale, int depth) const;

    std::map<u32, Shape>                shapes_;
    mutable std::map<u32, StrokeGlyph>  cache_;
    StrokeFontInfo                      info_{};
    bool                                loaded_{false};
};

// Direction 0 is east, counter-clockwise; diagonals sit on the octant grid.
void shx_direction(u8 dir, f32& dx, f32& dy) noexcept;

}
