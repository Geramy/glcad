#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "engine/layer.hpp"
#include "geometry/snap.hpp"

namespace gplot {

using gcm::f64;
using gcm::i32;
using gcm::u8;
using Point = gcm::Lattice2;

struct PaperSize {
    std::string name;
    f64         width{0.297}, height{0.210};   // metres, landscape
    f64         margin_left{0.010}, margin_bottom{0.010};
    f64         margin_right{0.010}, margin_top{0.010};

    f64 printable_width() const noexcept {
        return width - margin_left - margin_right;
    }
    f64 printable_height() const noexcept {
        return height - margin_bottom - margin_top;
    }
};

const std::vector<PaperSize>& standard_papers();
const PaperSize* find_paper(std::string_view name);

PaperSize rotated(const PaperSize& p);

// `drawing_units` per `paper_units`: 1:50 is {50, 1}, 2:1 is {1, 2}.
enum class ScaleFamily : u8 { Metric, Imperial };

struct NamedScale {
    std::string name;
    f64         drawing{1.0};
    f64         paper{1.0};
    ScaleFamily family{ScaleFamily::Metric};

    // Drawing units per paper metre.
    f64 ratio() const noexcept { return paper != 0.0 ? drawing / paper : 1.0; }
    bool is_enlargement() const noexcept { return ratio() < 1.0; }
    bool is_full_size() const noexcept { return std::abs(ratio() - 1.0) < 1e-12; }
};

const std::vector<NamedScale>& standard_scales();
const NamedScale* find_scale(std::string_view name);

bool parse_scale(std::string_view text, NamedScale& out);

const NamedScale* nearest_standard_scale(f64 ratio,
                                         ScaleFamily family = ScaleFamily::Metric);

enum class Orientation : u8 { Portrait, Landscape };
enum class PlotArea : u8 { Layout, Extents, Display, Window, View };

enum class StyleMode : u8 { ColorDependent, Named };

enum class LinecapStyle : u8 { Butt, Square, Round, Diamond };
enum class LinejoinStyle : u8 { Miter, Bevel, Round, Diamond };

struct PlotStyle {
    std::string name{"Normal"};

    bool use_object_color{true};
    u8   r{0}, g{0}, b{0};

    bool          use_object_lineweight{true};
    gdoc::Lineweight lineweight{gdoc::Lineweight::Default};

    bool        use_object_linetype{true};
    std::string linetype{"Continuous"};

    f64  screening{100.0};        // percent
    bool plot{true};
    LinecapStyle  endcap{LinecapStyle::Butt};
    LinejoinStyle join{LinejoinStyle::Miter};
    f64  lineweight_scale{1.0};
    f64  width_mm{-1.0}; // exact custom pen weight, negative uses the ladder
    bool grayscale{false};
    int pattern_id{31}, fill_style{73};
    f64 pattern_size{0.5};
    bool adaptive_linetype{true};
    int source_end_style{4}, source_join_style{5};
};

class PlotStyleTable {
public:
    explicit PlotStyleTable(StyleMode mode = StyleMode::ColorDependent);

    std::string source_text;
    f64 pattern_scale{1};

    StyleMode mode() const noexcept { return mode_; }
    bool indexed_only() const noexcept { return indexed_only_; }
    void set_indexed_only(bool value) noexcept { indexed_only_=value; }

    PlotStyle&       by_index(i32 aci);
    const PlotStyle* by_index(i32 aci) const;

    PlotStyle&       by_name(std::string_view name);
    const PlotStyle* find(std::string_view name) const;
    void             add(PlotStyle s);

    PlotStyle resolve(i32 aci, std::string_view style_name) const;

    std::size_t size() const noexcept { return styles_.size(); }
    const std::vector<PlotStyle>& all() const noexcept { return styles_; }

    static PlotStyleTable monochrome();

private:
    StyleMode              mode_;
    bool indexed_only_{false};
    std::vector<PlotStyle> styles_;
};

struct PageSetup {
    std::string name{"Default"};
    PaperSize   paper;
    Orientation orientation{Orientation::Landscape};
    PlotArea    area{PlotArea::Layout};

    // Drawing units per paper metre.
    f64  scale_numerator{1.0};
    f64  scale_denominator{1.0};
    bool fit_to_paper{false};
    ScaleFamily scale_family{ScaleFamily::Metric};

    f64  offset_x{0.0}, offset_y{0.0};
    bool centred{true};

    bool plot_lineweights{true};
    bool scale_lineweights{false};
    bool plot_transparency{false};
    std::string style_table;

    Point window_lo{}, window_hi{};

    f64 scale() const noexcept {
        return scale_denominator != 0.0 ? scale_numerator / scale_denominator : 1.0;
    }

    bool set_scale(std::string_view name);
    void set_scale(const NamedScale& s);
    const std::string& scale_name() const noexcept { return scale_name_; }

    // Drawing units per paper metre -- the number the geometry is divided by.
    f64 drawing_per_paper() const noexcept {
        return scale_numerator != 0.0 ? scale_denominator / scale_numerator : 1.0;
    }

    f64 fitted_ratio(f64 model_width, f64 model_height) const;

    f64 effective_width() const noexcept {
        return orientation == Orientation::Landscape ? paper.printable_width()
                                                     : paper.printable_height();
    }
    f64 effective_height() const noexcept {
        return orientation == Orientation::Landscape ? paper.printable_height()
                                                     : paper.printable_width();
    }

    // Metres from the page's lower-left, including the margin.
    void to_paper(f64 mx, f64 my, f64 centre_x, f64 centre_y,
                  f64& px, f64& py) const;

    // Full page in metres, orientation applied (not the printable area).
    f64 page_width() const noexcept {
        return orientation == Orientation::Landscape ? paper.width : paper.height;
    }
    f64 page_height() const noexcept {
        return orientation == Orientation::Landscape ? paper.height : paper.width;
    }

    std::string scale_name_{"1:1"};
};

// Sheet point in metres; not lattice, which would round margins to zero.
struct PaperPoint {
    f64 x{0.0}, y{0.0};
};

struct Viewport {
    PaperPoint lo{}, hi{};          // the window on the sheet, in paper metres
    gcm::u32 source_space{0};
    f64    centre_x{0.0}, centre_y{0.0};
    f64    scale{1.0};              // drawing units per paper unit
    f64    rotation{0.0};

    bool locked{false};
    bool on{true};
    std::vector<std::string> frozen_layers;

    std::vector<PaperPoint> clip_polygon; // empty uses the rectangular frame

    bool layer_visible(std::string_view layer) const;

    void to_paper(f64 mx, f64 my, f64& px, f64& py) const;
    void to_model(f64 px, f64 py, f64& mx, f64& my) const;

    static f64 scale_from_ratio(f64 drawing, f64 paper) {
        return paper != 0.0 ? drawing / paper : 1.0;
    }
};

struct Sheet {
    std::string           name{"Layout1"};
    PageSetup             setup;
    std::vector<Viewport> viewports;
    gcm::u32              space{0};

    Viewport*       viewport(std::size_t i);
    const Viewport* viewport(std::size_t i) const;
};

class SheetSet {
public:
    Sheet&       add(Sheet s);
    Sheet*       find(std::string_view name);
    const Sheet* find(std::string_view name) const;
    bool         remove(std::string_view name);
    bool         reorder(std::string_view name, std::size_t to);

    std::size_t size() const noexcept { return sheets_.size(); }
    const std::vector<Sheet>& all() const noexcept { return sheets_; }

private:
    std::vector<Sheet> sheets_;
};

}
