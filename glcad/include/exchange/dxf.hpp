#pragma once

#include <string>
#include <vector>

#include "engine/block.hpp"
#include "engine/linetype.hpp"
#include "engine/hatch.hpp"
#include "render/registry.hpp"
#include "engine/layer.hpp"
#include "geometry/snap.hpp"
#include "geometry/dimension.hpp"
#include "geometry/nurbs.hpp"
#include "exchange/page.hpp"

namespace gdxf {

using gcm::f64;
using gcm::i32;
using gcm::u32;
using gcm::u8;

struct Group {
    i32         code{0};
    std::string value;
    std::size_t line{0};

    bool        as_int(i32& out) const;
    bool        as_real(f64& out) const;
    const std::string& text() const noexcept { return value; }
};

struct Tape {
    std::vector<Group> groups;
    bool               ok{false};
    std::string        error;
    std::size_t        line{0};
    std::size_t        junk_lines{0};

    std::size_t size() const noexcept { return groups.size(); }
};

Tape lex(std::string_view text);

enum class Units : i32 {
    Unspecified = 0, Inches = 1, Feet = 2, Miles = 3, Millimetres = 4,
    Centimetres = 5, Metres = 6, Kilometres = 7, Microinches = 8, Mils = 9,
    Yards = 10, Angstroms = 11, Nanometres = 12, Microns = 13,
    Decimetres = 14, Decametres = 15, Hectometres = 16, Gigametres = 17,
};

const char* units_name(Units u) noexcept;
f64         metres_per(Units u) noexcept;   // 0 for Unspecified

struct Entity {
    gsnap::Shape shape;
    std::string  layer{"0"};
    std::string  kind;
};

struct Text {
    gcm::Lattice2 pos{};
    std::string   text;
    f64           height{0.0};   // metres
    f64           angle{0.0};    // radians
    std::string   layer{"0"};
    // halign 0 left, 1 centre, 2 right; valign 0 baseline, 1 bottom, 2 middle, 3 top
    i32           halign{0};
    i32           valign{0};
    // Column width in metres; nonzero writes MTEXT, zero writes TEXT.
    f64           width{0.0};
    std::string   style{"Standard"};
    gdoc::Attributes attrs;
};

struct HatchRec {
    std::string   pattern{"ANSI31"};
    bool          solid{false};
    f64           scale{1.0};
    f64           angle{0.0};      // degrees
    gcm::Lattice2 seed{};
    std::string   layer{"0"};
    std::vector<gsnap::Shape> boundary;
    std::vector<gdoc::PatternLine> pattern_lines;
    gdoc::Attributes attrs;
};

struct Layout {
    std::string name{"Layout1"};
    std::string paper_name{"A3"};
    double width{.420}, height{.297}; // metres, with orientation applied
    double margin_left{.01}, margin_bottom{.01}, margin_right{.01}, margin_top{.01};
    std::string plot_style_table;
    std::vector<Entity> entities;
    std::vector<Text> texts;
    std::vector<gdim::Dimension> dimensions;
    std::vector<ggeo::NurbsCurve2<gcm::F64>> splines;
    std::vector<HatchRec> hatches;
    std::vector<gdoc::BlockRef> references;
    std::vector<gplot::Viewport> viewports;
    std::vector<gdoc::Underlay> underlays;
};

struct Document {
    gdoc::LayerTable          layers;
    gdoc::LinetypeTable       linetypes;
    std::vector<gtext::TextStyleDef> text_styles;
    gdoc::BlockTable          blocks;
    std::vector<Entity>       entities;
    std::vector<Text>         texts;
    std::vector<gdim::Dimension>               dimensions;
    bool         has_dim_style{false};
    gdim::Style  dim_style{};
    std::vector<gdim::Style> dim_styles;
    std::vector<ggeo::NurbsCurve2<gcm::F64>>   splines;
    std::vector<HatchRec>                      hatches;
    std::string plot_style_table;
    std::vector<gdoc::Underlay> underlays;
    std::vector<Layout> layouts;
    int active_layout{-1}; // Model, otherwise an index into layouts
    Units                     units{Units::Metres};
    std::string               dxf_version{"AC1015"};   // AutoCAD 2000

    std::size_t size() const noexcept { return entities.size(); }
};

struct WriteOptions {
    Units       units{Units::Millimetres};
    std::string version{"AC1015"};
    bool        write_blocks{true};
    bool        explode_arcs{false};
    f64         chord_tolerance{1e-4};
};

// Empty on unsupported content; callers must not overwrite a file with it.
std::string write(const Document& d, const gcm::WorkingVolume& v,
                  const WriteOptions& o = {});

struct ReadResult {
    Document    document;
    bool        ok{false};
    std::string error;

    std::vector<std::string> skipped;
    std::size_t              unknown_entities{0};
    std::size_t              mirrored_entities{0};
    std::size_t              junk_lines{0};
    std::vector<std::pair<std::string, std::size_t>> unknown_kinds;
};

ReadResult read(std::string_view text, const gcm::WorkingVolume& v);

// Bulge = tan(included angle / 4); positive is counter-clockwise.
struct Bulge {
    f64  cx{0}, cy{0};
    f64  radius{0};
    f64  start_angle{0}, end_angle{0};    // radians, counter-clockwise
    bool is_arc{false};                   // false: the two points are a line
};

Bulge arc_from_bulge(f64 x0, f64 y0, f64 x1, f64 y1, f64 bulge);
f64   bulge_from_arc(f64 start_angle, f64 end_angle);

}
