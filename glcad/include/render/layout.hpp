#pragma once

#include "render/font.hpp"

namespace gtext {

enum class Align : u8 { Left, Centre, Right };
enum class VAlign : u8 { Baseline, Top, Middle, Bottom };

struct TextStyle {
    f32    size{1.0f};
    f32    line_spacing{1.2f};
    f32    letter_spacing{0.0f};
    Align  align{Align::Left};
    VAlign valign{VAlign::Baseline};
    f32    wrap_width{0.0f};      // 0 = no wrapping
    bool   kerning{true};
};

struct PlacedGlyph {
    const Glyph* glyph{nullptr};
    f32 x{0.0f}, y{0.0f};
    f32 w{0.0f}, h{0.0f};
    u32 line{0};
};

struct Layout {
    std::vector<PlacedGlyph> glyphs;
    f32 width{0.0f};
    f32 height{0.0f};
    f32 min_x{0.0f}, min_y{0.0f}, max_x{0.0f}, max_y{0.0f};
    u32 line_count{0};

    bool empty() const noexcept { return glyphs.empty(); }
};

Layout layout_text(const Font& f, std::string_view utf8, const TextStyle& style);

void measure_text(const Font& f, std::string_view utf8, const TextStyle& style,
                  f32& w, f32& h);

}
