#pragma once

#include <string>
#include <vector>

#include "render/layout.hpp"

namespace gtext {

struct TextFrame {
    f64 ox{0}, oy{0}, oz{0};
    f64 ux{1}, uy{0}, uz{0};
    f64 vx{0}, vy{1}, vz{0};
    f64 nx{0}, ny{0}, nz{1};

    void to_world(f64 x, f64 y, f64& wx, f64& wy, f64& wz) const noexcept;

    static TextFrame on_plane(f64 px, f64 py, f64 pz,
                              f64 nx, f64 ny, f64 nz) noexcept;
    static TextFrame along(f64 px, f64 py, f64 pz, f64 nx, f64 ny, f64 nz,
                           f64 dx, f64 dy, f64 dz) noexcept;
};

enum class FacingMode : u8 {
    Flat,
    Billboard,
    Screen,
};

struct WorldTextStyle {
    TextStyle  text;
    FacingMode facing{FacingMode::Flat};
    f64        height{0.0025};      // world units per em

    bool flip_when_behind{true};
    bool hide_when_behind{false};
};

struct WorldGlyph {
    const Glyph* glyph{nullptr};
    // World units, counter-clockwise from lower-left.
    f64 x[4]{}, y[4]{}, z[4]{};
};

struct WorldText {
    std::vector<WorldGlyph> glyphs;
    bool visible{true};
    bool flipped{false};
    f64  width{0.0}, height{0.0};   // world units
};

WorldText place_in_world(const Font& f, std::string_view utf8,
                         const TextFrame& frame, const WorldTextStyle& style,
                         const f64 eye[3]);

enum class Direction : u8 { LeftToRight, RightToLeft };

enum class BidiClass : u8 {
    Neutral,
    LeftToRight,
    RightToLeft,
    ArabicLetter,
    EuropeanNumber,
    ArabicNumber,
};

BidiClass bidi_class(u32 codepoint) noexcept;

struct BidiRun {
    std::size_t begin{0}, end{0};    // byte offsets into the source
    u8          level{0};
    Direction   direction{Direction::LeftToRight};
};

std::vector<BidiRun> bidi_runs(std::string_view utf8,
                               Direction paragraph = Direction::LeftToRight);

Direction paragraph_direction(std::string_view utf8) noexcept;

enum class JoiningForm : u8 { Isolated, Initial, Medial, Final };

bool joins_right(u32 codepoint) noexcept;
bool joins_left(u32 codepoint) noexcept;

struct ShapedGlyph {
    u32         codepoint{0};
    u32         source{0};
    JoiningForm form{JoiningForm::Isolated};
};

std::vector<ShapedGlyph> shape_arabic(std::string_view utf8);

Layout layout_bidi(const Font& f, std::string_view utf8, const TextStyle& style,
                   Direction paragraph = Direction::LeftToRight);

}
