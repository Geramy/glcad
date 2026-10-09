#pragma once
#include <functional>

#include <string>
#include <vector>

#include "render/layout.hpp"

namespace gtext {

enum class Stack : u8 { None, Horizontal, Diagonal, Tolerance };
enum class RunAlign : u8 { Bottom, Centre, Top };

struct Format {
    std::string font;
    f32  height{1.0f};            // multiple of the object height
    f32  width{1.0f};
    f32  oblique{0.0f};           // degrees
    f32  tracking{1.0f};
    bool bold{false}, italic{false};
    bool underline{false}, overline{false}, strike{false};

    bool     has_color{false};
    i32      aci{7};
    bool     true_color{false};
    u8       r{255}, g{255}, b{255};

    RunAlign align{RunAlign::Bottom};

    bool operator==(const Format& o) const noexcept;
    bool operator!=(const Format& o) const noexcept { return !(*this == o); }
};

struct Run {
    std::string text;
    std::string denominator;
    Stack       stack{Stack::None};
    Format      format{};
    bool        paragraph_break{false};
    u32         paragraph{0};
};

struct Paragraph {
    f32 indent_first{0.0f};
    f32 indent_left{0.0f};
    f32 indent_right{0.0f};
    std::vector<f32> tabs;
    Align align{Align::Left};
};

struct MTextContent {
    std::vector<Run>       runs;
    std::vector<Paragraph> paragraphs;

    std::string plain() const;
    bool empty() const noexcept { return runs.empty(); }
};

MTextContent parse_mtext(std::string_view s);

std::string format_mtext(const MTextContent& c);

struct MTextStyle {
    f32   height{1.0f};
    f32   width{0.0f};            // column width; 0 disables wrapping
    f32   line_spacing{1.2f};
    Align align{Align::Left};
    VAlign valign{VAlign::Top};
    std::function<f32(std::string_view)> measure;
    std::function<f32(std::string_view,const Format&)> measure_run;
    bool  exact_spacing{false};
};

struct MTextPiece {
    std::string text;
    std::string denominator;
    Stack  stack{Stack::None};
    Format format{};
    f32 x{0.0f}, y{0.0f};
    f32 width{0.0f};
    f32 height{0.0f};
    u32 line{0};
};

struct MTextLayout {
    std::vector<MTextPiece> pieces;
    f32 width{0.0f}, height{0.0f};
    u32 line_count{0};
    bool empty() const noexcept { return pieces.empty(); }
};

MTextLayout layout_mtext(const Font& f, const MTextContent& c,
                         const MTextStyle& style);
MTextLayout layout_mtext(const Font& f, std::string_view s,
                         const MTextStyle& style);

}
