#pragma once

#include <string>
#include <vector>

#include "geometry/snap.hpp"
#include "geometry/body.hpp"
#include "render/camera.hpp"

namespace gdraft {

using gcm::f64;
using gcm::u32;
using gcm::u64;
using gcm::u8;
using gview::StandardView;

inline constexpr u32 kNoView = 0xFFFFFFFFu;

enum class ProjectionAngle : u8 { First, Third };
const char* projection_angle_name(ProjectionAngle a) noexcept;

enum class ViewKind : u8 {
    Base,
    Projected,
    Isometric,
    Section,
    Detail,
};
const char* view_kind_name(ViewKind k) noexcept;

enum class Alignment : u8 { None, Horizontal, Vertical };

// Paper metres relative to the view's own centre.
struct Edge2 {
    f64 x0{}, y0{}, x1{}, y1{};
    u32 face_a{gtopo::kInvalid}, face_b{gtopo::kInvalid};
};

struct ViewContent {
    std::vector<Edge2> visible;
    std::vector<Edge2> hidden;
    std::vector<Edge2> silhouette;
    std::size_t degenerate{0};
    f64 width{0.0}, height{0.0};
};

enum class BreakStyle : u8 {
    Straight,
    Zigzag,
    Jagged,
};
const char* break_style_name(BreakStyle s) noexcept;

struct ViewBreak {
    // View paper coordinates along the break's axis.
    f64  from{0.0}, to{0.0};
    bool horizontal{true};   // the break runs vertically, removing width
    f64  gap{0.006};         // paper metres left between the two halves
    BreakStyle style{BreakStyle::Zigzag};

    f64 removed() const noexcept { return std::abs(to - from); }
    bool valid() const noexcept { return removed() > 0.0 && gap >= 0.0; }
};

struct ViewCrop {
    f64  lo_x{0.0}, lo_y{0.0}, hi_x{0.0}, hi_y{0.0};
    bool enabled{false};
    bool valid() const noexcept { return hi_x > lo_x && hi_y > lo_y; }
};

struct DrawingView {
    std::string  label;
    ViewKind     kind{ViewKind::Base};
    StandardView direction{StandardView::Front};

    // Paper metres from the sheet origin.
    f64 x{0.0}, y{0.0};

    // Model units per paper metre.
    f64  scale{1.0};
    bool scale_from_parent{true};

    u32       parent{kNoView};
    Alignment alignment{Alignment::None};

    bool show_hidden{true};
    bool show_silhouettes{true};
    bool show_tangent_edges{false};

    u32  source{0};
    u64  source_revision{0};

    ViewContent content;
    bool        generated{false};

    bool clipped{false};
    f64  clip_x{0.0}, clip_y{0.0}, clip_radius{0.0};   // paper metres

    std::vector<ViewBreak> breaks;
    ViewCrop               crop;

    f64 removed_length() const noexcept {
        f64 n = 0.0;
        for (const ViewBreak& b : breaks) n += b.removed();
        return n;
    }
};

std::vector<std::vector<std::pair<f64, f64>>> break_symbol(const ViewBreak& b,
                                                           const DrawingView& v);

struct SourceModel {
    u32                 id{0};
    gtopo::Body         body;
    gcm::WorkingVolume  volume;
    u64                 revision{1};

    void touch() noexcept { ++revision; }
};

enum class Layout : u8 {
    OneView,
    TwoView,
    ThreeView,
    FourView,
};
const char* layout_name(Layout l) noexcept;

std::vector<StandardView> layout_views(Layout l);

// Unit offset in (right, up); decides first vs third angle.
void projection_offset(StandardView from, StandardView to, ProjectionAngle angle,
                       f64& right, f64& up);

Alignment alignment_for(StandardView from, StandardView to);

struct SectionLine {
    // Paper metres relative to the parent view's centre.
    f64 x0{}, y0{}, x1{}, y1{};
    f64 look_x{0.0}, look_y{-1.0};
    std::string label{"A"};
};

struct CutFace {
    std::vector<std::pair<f64, f64>> loop;   // paper metres, view-relative
};

struct DetailCircle {
    f64 x{}, y{};                  // centre on the parent, view-relative
    f64 radius{0.010};             // paper metres on the PARENT
    std::string label{"A"};
};

class ViewSet {
public:
    explicit ViewSet(ProjectionAngle angle = ProjectionAngle::Third)
        : angle_(angle) {}

    ProjectionAngle angle() const noexcept { return angle_; }
    void set_angle(ProjectionAngle a);

    u32 add_base(const SourceModel& m, StandardView dir, f64 x, f64 y, f64 scale);

    u32 project(const SourceModel& m, u32 parent, StandardView dir,
                f64 gap = 0.020);

    std::vector<u32> add_layout(const SourceModel& m, Layout l, f64 x, f64 y,
                                f64 scale, f64 gap = 0.020);

    DrawingView*       view(u32 i);
    const DrawingView* view(u32 i) const;
    std::size_t size() const noexcept { return views_.size(); }
    const std::vector<DrawingView>& all() const noexcept { return views_; }

    std::vector<u32> stale(const SourceModel& m) const;
    bool             is_stale(u32 i, const SourceModel& m) const;
    std::size_t      regenerate(const SourceModel& m);

    bool move(u32 i, f64 to_x, f64 to_y);
    bool unalign(u32 i);

    bool add_break(u32 view, const ViewBreak& b);
    bool clear_breaks(u32 view);
    bool set_crop(u32 view, const ViewCrop& c);

    u32 add_section(const SourceModel& m, u32 parent, const SectionLine& line,
                    f64 gap = 0.020);

    u32 add_detail(const SourceModel& m, u32 parent, const DetailCircle& circle,
                   f64 scale, f64 gap = 0.020);

    const SectionLine*  section_line(u32 i) const;
    const DetailCircle* detail_circle(u32 i) const;
    const std::vector<CutFace>& cut_faces(u32 i) const;

private:
    void       lay_out_child(DrawingView& child, const DrawingView& parent,
                             f64 gap) const;
    void       relayout_children(u32 parent, f64 gap);

    void regenerate_one(DrawingView& v, const SourceModel& m);

    ProjectionAngle          angle_;
    std::vector<DrawingView> views_;
    f64                      gap_{0.020};

    std::vector<std::pair<u32, SectionLine>>  sections_;
    std::vector<std::pair<u32, DetailCircle>> details_;
    std::vector<std::pair<u32, std::vector<CutFace>>> cuts_;
};

ViewContent project_body(const gtopo::Body& b, const gcm::WorkingVolume& v,
                         StandardView dir, f64 scale, bool hidden_lines = true);

struct SheetLayers {
    std::string visible{"VIEW-VISIBLE"};
    std::string hidden{"VIEW-HIDDEN"};
    std::string silhouette{"VIEW-OUTLINE"};
    std::string cut{"VIEW-CUT"};
    std::string annotation{"VIEW-MARKS"};
};

void to_sheet_shapes(const ViewSet& s, const gcm::WorkingVolume& paper,
                     std::vector<gsnap::Shape>& shapes,
                     std::vector<std::string>& layers,
                     const SheetLayers& names = {});

// `toward` points from the paper to the eye.
void view_axes(StandardView v, f64 right[3], f64 up[3], f64 toward[3]) noexcept;

}
