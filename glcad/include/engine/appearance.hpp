#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "math/config.hpp"

namespace gdoc {

using gcm::f64;
using gcm::i32;
using gcm::u32;
using gcm::u8;

// ACI 7 is white on a dark canvas and black on a light one.
struct Color {
    enum class Kind : u8 { ByLayer, ByBlock, Index, True };
    Kind kind{Kind::Index};
    i32  index{7};
    u8   r{255}, g{255}, b{255};

    static Color by_layer() noexcept { return {Kind::ByLayer, 0, 0, 0, 0}; }
    static Color by_block() noexcept { return {Kind::ByBlock, 0, 0, 0, 0}; }
    static Color aci(i32 i) noexcept { return {Kind::Index, i, 0, 0, 0}; }
    static Color rgb(u8 r, u8 g, u8 b) noexcept { return {Kind::True, 0, r, g, b}; }

    bool resolved() const noexcept { return kind == Kind::Index || kind == Kind::True; }
    bool operator==(const Color& o) const noexcept {
        return kind == o.kind && index == o.index && r == o.r && g == o.g && b == o.b;
    }
    bool operator!=(const Color& o) const noexcept { return !(*this == o); }
};

void aci_rgb(i32 index, u8& r, u8& g, u8& b) noexcept;

i32 aci_of_name(std::string_view name) noexcept;

enum class Lineweight : i32 {
    ByLayer = -3, ByBlock = -2, Default = -1,
    W000 = 0, W005 = 5, W009 = 9, W013 = 13, W015 = 15, W018 = 18,
    W020 = 20, W025 = 25, W030 = 30, W035 = 35, W040 = 40, W050 = 50,
    W053 = 53, W060 = 60, W070 = 70, W080 = 80, W090 = 90, W100 = 100,
    W106 = 106, W120 = 120, W140 = 140, W158 = 158, W200 = 200, W211 = 211,
};

f64 lineweight_mm(Lineweight w) noexcept;

Lineweight nearest_lineweight(f64 mm) noexcept;

struct Attributes {
    Color       color{Color::by_layer()};
    std::string linetype{"ByLayer"};
    Lineweight  lineweight{Lineweight::ByLayer};
    double      linetype_scale{1.0};
    int         transparency{-1};        // -1 = ByLayer, -2 = ByBlock
    bool        visible{true};

    std::unordered_map<std::string, std::string> user;

    std::string foreign_format;
    std::vector<gcm::u8> foreign_blob;

    bool has_foreign_data() const noexcept { return !foreign_blob.empty(); }

    bool all_by_layer() const noexcept {
        return color.kind == Color::Kind::ByLayer && linetype == "ByLayer" &&
               lineweight == Lineweight::ByLayer && linetype_scale == 1.0 &&
               transparency == -1 && visible && user.empty() && foreign_blob.empty();
    }
};

// ByBlock falls back like ByLayer at drawing level; explicit zero stays opaque.
inline int transparency_percent(const Attributes& attrs,int layer_transparency=0) noexcept {
    const int value=attrs.transparency<0?layer_transparency:attrs.transparency;
    return value<0?0:value>100?100:value;
}

}
