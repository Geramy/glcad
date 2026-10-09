#pragma once

#include <string>
#include <vector>

#include "geometry/dimension.hpp"

namespace gdim {

struct AnnotationScale {
    std::string name{"1:1"};
    f64         paper{1.0};      // paper units
    f64         drawing{1.0};    // drawing units the paper unit represents

    // Drawing units per paper unit. 1:50 gives 50.
    f64 factor() const noexcept { return paper > 0.0 ? drawing / paper : 1.0; }
};

const std::vector<AnnotationScale>& standard_scales();
const AnnotationScale*              find_scale(std::string_view name);

class ScaleList {
public:
    ScaleList();
    const AnnotationScale* find(std::string_view name) const;
    AnnotationScale&       add(AnnotationScale s);
    bool                   remove(std::string_view name);
    std::size_t            size() const noexcept { return scales_.size(); }
    const std::vector<AnnotationScale>& all() const noexcept { return scales_; }

    void set_current(std::string_view name);
    const std::string& current() const noexcept { return current_; }
    f64  current_factor() const;

private:
    std::vector<AnnotationScale> scales_;
    std::string                  current_{"1:1"};
};

struct Annotative {
    bool                     enabled{false};
    std::vector<std::string> scales;

    bool appears_at(std::string_view scale) const noexcept;
    void add_scale(std::string_view scale);
    bool remove_scale(std::string_view scale);
};

f64 annotative_size(f64 paper_height, f64 scale_factor) noexcept;

struct Chain {
    Kind        kind{Kind::Linear};
    std::string style{"Standard"};

    Point              origin{};
    std::vector<Point> points;
    Point              location{};

    // DIMDLI; 0 takes it from the style.
    f64 spacing{0.0};

    bool horizontal{true};
};

struct ChainResult {
    std::vector<Dimension> dimensions;
    bool                   ok{false};
    const char*            reason{""};
};

ChainResult baseline(const Chain& c, const Style& s, const Volume& v);
ChainResult continued(const Chain& c, const Style& s, const Volume& v);

enum class LeaderType : u8 { Straight, Spline, None };
enum class LeaderContent : u8 { MText, Block, None };

struct MLeaderStyle {
    std::string name{"Standard"};

    LeaderType    leader{LeaderType::Straight};
    ArrowType     arrow{ArrowType::ClosedFilled};
    f64           arrow_size{2.5};
    int           max_points{2};

    bool landing{true};
    f64  landing_gap{0.5};
    f64  landing_length{2.0};

    f64         text_height{2.5};
    TextPos     text_pos{TextPos::Above};
    std::string text_style{"Standard"};

    LeaderContent content{LeaderContent::MText};
    std::string   block;

    f64  scale{1.0};
    bool annotative{false};

    f64 scaled(f64 x) const noexcept { return x * scale; }
};

class MLeaderStyleTable {
public:
    MLeaderStyleTable();
    const MLeaderStyle* find(std::string_view name) const;
    MLeaderStyle&       add(MLeaderStyle s);
    std::size_t         size() const noexcept { return styles_.size(); }
    const std::vector<MLeaderStyle>& all() const noexcept { return styles_; }
    void set_current(std::string_view name);
    const std::string& current() const noexcept { return current_; }

private:
    std::vector<MLeaderStyle> styles_;
    std::string               current_{"Standard"};
};

struct MLeader {
    std::string style{"Standard"};
    std::vector<std::vector<Point>> lines;
    Point       landing{};
    std::string text;
    std::string block;
    bool        text_left{false};
    Annotative  annotative{};
};

struct MLeaderGeometry {
    std::vector<gsnap::Shape> lines;
    std::vector<gsnap::Shape> arrows;
    Point       text_at{};
    f64         text_height{0.0};
    std::string text;
    bool        ok{false};
    const char* reason{""};
};

MLeaderGeometry build(const MLeader& m, const MLeaderStyle& s, const Volume& v);

enum class GeoSymbol : u8 {
    Straightness, Flatness, Circularity, Cylindricity,
    ProfileLine, ProfileSurface,
    Angularity, Perpendicularity, Parallelism,
    Position, Concentricity, Symmetry,
    CircularRunout, TotalRunout,
};

enum class Material : u8 { RFS, MMC, LMC };

const char* symbol_name(GeoSymbol s) noexcept;
const char* symbol_glyph(GeoSymbol s) noexcept;
const char* material_glyph(Material m) noexcept;

struct DatumRef {
    std::string letter;
    Material    material{Material::RFS};
    bool        empty() const noexcept { return letter.empty(); }
};

struct GeoSegment {
    GeoSymbol   symbol{GeoSymbol::Position};
    std::string tolerance{"0.1"};
    bool        diameter{false};
    Material    material{Material::RFS};
    DatumRef    primary, secondary, tertiary;
};

struct FeatureControlFrame {
    std::vector<GeoSegment> segments;
    Point                   at{};
    f64                     height{0.0025};   // metres, one row
    Annotative              annotative{};
};

struct FrameCell {
    std::string text;
    f64         x{0.0}, y{0.0};
    f64         width{0.0}, height{0.0};
    f64         text_height{0.0};
};

struct FrameGeometry {
    std::vector<gsnap::Shape> lines;
    std::vector<FrameCell>    cells;
    f64                       width{0.0}, height{0.0};
    bool                      ok{false};
    const char*               reason{""};
};

FrameGeometry build(const FeatureControlFrame& f, const Volume& v);

struct DatumFeature {
    std::string letter{"A"};
    Point       at{};
    f64         height{0.0025};
    f64         leader_length{0.005};
    bool        below{false};
};

FrameGeometry build(const DatumFeature& d, const Volume& v);

struct OrdinateRun {
    std::string style{"Standard"};
    Point       datum{};
    std::vector<Point> features;
    bool        along_x{true};
    f64         leader_length{0.0}; // 0 takes it from the style
    f64         base_offset{0.0};
};

ChainResult ordinate_run(const OrdinateRun& r, const Style& s, const Volume& v);

struct JoggedRadius {
    std::string style{"Standard"};
    Point centre{};            // the TRUE centre, used for the measurement
    i64   radius{0};
    Point origin{};
    Point jog{};
    Point centre_override{};   // where the centre is DRAWN
    f64   jog_angle_deg{45.0};
};

Geometry build(const JoggedRadius& j, const Style& s, const Volume& v);

enum class InspectShape : u8 { Round, Angular, None };

struct Inspection {
    Dimension    dimension{};
    std::string  label;
    std::string  rate;
    InspectShape shape{InspectShape::Round};
};

struct InspectionGeometry {
    Geometry                base{};
    std::vector<gsnap::Shape> frame;
    std::vector<FrameCell>  cells;
    bool                    ok{false};
    const char*             reason{""};
};

InspectionGeometry build(const Inspection& i, const Style& s, const Volume& v);

enum class FieldKind : u8 {
    SheetNumber, SheetCount, SheetName, SheetSetName,
    DrawingTitle, FileName, FilePath,
    Author, LastSavedBy, CreateDate, SaveDate, PlotDate,
    Scale, CurrentLayer, SystemVariable,
    ObjectProperty,
    Formula,
    Custom,
};

const char* field_name(FieldKind k) noexcept;

struct Field {
    FieldKind   kind{FieldKind::SheetNumber};
    std::string argument;
    std::string format;
    std::string cached;
};

class FieldContext {
public:
    void set(std::string_view key, std::string value);
    const std::string* get(std::string_view key) const;
    std::size_t size() const noexcept { return values_.size(); }

private:
    std::vector<std::pair<std::string, std::string>> values_;
};

std::vector<Field> parse_fields(std::string_view text);

std::string evaluate_fields(std::string_view text, const FieldContext& ctx);

}
