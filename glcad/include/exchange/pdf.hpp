#pragma once
#include <memory>
#include "exchange/underlay_image.hpp"

#include <string>
#include <array>
#include <vector>
#include <optional>

#include "engine/transport.hpp"
#include "exchange/page.hpp"
#include "engine/linetype.hpp"

namespace gplot {

struct PdfColor {
    f64 r{0}, g{0}, b{0}, a{1};
    static PdfColor black() noexcept { return {0, 0, 0}; }
    static PdfColor rgb255(u8 r, u8 g, u8 b) noexcept {
        return {r / 255.0, g / 255.0, b / 255.0};
    }
};

// One drawn item in PAPER coordinates (metres from the page's lower-left).
struct PaperClip {
    PaperPoint lo, hi;
    std::vector<PaperPoint> polygon;
    bool valid() const;
    bool contains(PaperPoint p) const;
    std::vector<PaperPoint> boundary() const;
    std::vector<std::array<PaperPoint,3>> triangles() const;
    std::vector<std::array<PaperPoint,2>> segments(PaperPoint a,PaperPoint b) const;
    bool clip_segment(f64& x0, f64& y0, f64& x1, f64& y1) const;
};
struct ContentClip {
    PaperClip boundary;
    bool inverted{false};
    bool contains(PaperPoint p) const { return boundary.contains(p) != inverted; }
};
struct PdfPath {
    std::vector<std::pair<f64, f64>> points;
    bool     closed{false};
    bool     filled{false};
    PdfColor color{};
    f64      width_mm{0.25};
    std::vector<f64> dashes_mm;      // empty means solid
    std::string layer;
    std::optional<PaperClip> clip;
    LinecapStyle endcap{LinecapStyle::Butt};
    LinejoinStyle join{LinejoinStyle::Miter};
    int fill_style{73};
    f64 fill_size_mm{0.5};
    bool adaptive_dashes{false};
    std::vector<std::vector<std::pair<f64,f64>>> subpaths;
    bool even_odd{false};
};

struct PdfText {
    f64         x{0}, y{0};
    std::string text;
    f64         height_mm{2.5};
    f64         rotation{0.0};
    PdfColor    color{};
    std::string layer;
    std::optional<PaperClip> clip;
    f64 width_factor{1}, oblique{0}, y_factor{1};
    std::vector<ContentClip> clips;
    bool invisible{false};
};

struct PdfImage {
    std::shared_ptr<const RasterImage> raster;
    PaperPoint origin, x_axis, y_axis; // metres; axes are vectors spanning the page image
    std::optional<PaperClip> clip;
    std::vector<ContentClip> clips;
};

class PdfWriter {
public:
    // Page size in metres.
    void begin_page(f64 width, f64 height);
    void end_page();

    void add(const PdfPath& p);
    void add(const PdfText& t);
    void add(const PdfImage& image);
    const std::vector<PdfImage>& images_of_page(std::size_t i) const;

    void declare_layer(const std::string& name, bool visible = true);

    void set_title(std::string t) { title_ = std::move(t); }
    void set_creator(std::string c) { creator_ = std::move(c); }

    std::string build() const;
    bool        write(gio::Writer& w) const;
    bool        write_file(const std::string& path) const;
    bool        write_url(std::string_view url) const;

    std::size_t page_count() const noexcept { return pages_.size(); }
    std::size_t path_count() const noexcept;
    std::pair<f64, f64> page_size(std::size_t i) const noexcept;

    const std::vector<PdfPath>& paths_of_page(std::size_t i) const;
    const std::vector<PdfText>& texts_of_page(std::size_t i) const;

    const std::string& error() const noexcept { return error_; }

private:
    std::string error_;
    struct Page {
        f64 width{0.297}, height{0.210};
        std::vector<PdfPath> paths;
        std::vector<PdfText> texts;
        std::vector<PdfImage> images;
    };
    std::vector<Page>        pages_;
    std::vector<std::pair<std::string, bool>> layers_;
    std::string              title_{"GLCAD Drawing"};
    std::string              creator_{"GLCAD"};
    bool                     in_page_{false};
};

struct PlotJob {
    const Sheet*             sheet{nullptr};
    const PlotStyleTable*    styles{nullptr};
    const gdoc::LayerTable*  layers{nullptr};
    const gdoc::LinetypeTable* linetypes{nullptr};
    f64                      linetype_scale{1.0};
    bool                     paper_linetype_scale{false};
    gcm::WorkingVolume       volume{};

    // Model notes: drawing units (metres), rotation in radians.
    struct Note {
        f64         x{0}, y{0};
        std::string text;
        f64         height{0.0025};
        f64         rotation{0.0};
        std::string layer{"0"};
        f64 width_factor{1}, oblique{0}, y_factor{1};
        gdoc::Attributes attrs;
        std::vector<gclip::Boundary> clips;
        bool invisible{false};
    };
    struct Fill {
        std::vector<std::pair<f64,f64>> points;
        std::string layer;
        gdoc::Attributes attrs;
    };
    struct SpaceContent {
        gcm::u32                  space{0};
        std::vector<gsnap::Shape> shapes;
        std::vector<std::string>  shape_layers;
        std::vector<Note>         notes;
        // Drawn but excluded from the fit box.
        bool                      fit{true};
        std::vector<PdfImage> images;
        std::vector<Fill> fills;
    };
    std::vector<SpaceContent> content;

    // Already in paper metres at 1:1.
    std::vector<Fill> sheet_fills;
    std::vector<gsnap::Shape> sheet_shapes;
    std::vector<std::string>  sheet_layers;
    std::vector<Note>         sheet_notes;
    std::vector<PdfImage> sheet_images;
};

struct PlotResult {
    bool        ok{false};
    std::string message;
    std::size_t paths{0};
    std::size_t texts{0};
    std::size_t clipped{0};

    std::string scale_name{"1:1"};
    f64         scale_ratio{1.0};   // drawing units per paper metre
};

bool content_extents(const std::vector<PlotJob::SpaceContent>& content,
                     const gcm::WorkingVolume& v,
                     f64& lo_x, f64& lo_y, f64& hi_x, f64& hi_y);

PlotResult plot(const PlotJob& job, PdfWriter& out);

}
