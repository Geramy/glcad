#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "math/fixed.hpp"
#include "engine/layer.hpp"
#include "geometry/snap.hpp"

namespace gdoc {

using gcm::f64;
using gcm::i64;
using gcm::u32;
using Point2 = gcm::Lattice2;

struct Segment2 {
    Point2 a, b;
};

using Loop = std::vector<Point2>;

enum class BoundaryError { None, NotClosed, TooFewSegments, NoEnclosingLoop };

struct BoundaryResult {
    std::vector<Loop> loops;          // [0] outer, rest are islands
    BoundaryError     error{BoundaryError::None};
    Point2            gap_at{};
    bool              ok() const noexcept { return error == BoundaryError::None; }
    std::string       message(const gcm::WorkingVolume& v) const;
};

BoundaryResult extract_loops(const std::vector<Segment2>& segments);

BoundaryResult trace_region(const std::vector<Segment2>& segments, Point2 seed);

BoundaryResult region_containing(const BoundaryResult& loops, Point2 seed);

bool point_in_loop(Point2 p, const Loop& loop);
bool point_in_region(Point2 p, const std::vector<Loop>& loops);  // even-odd

struct PatternLine {
    f64              angle{0.0};
    f64              x{0.0}, y{0.0};
    f64              dx{0.0}, dy{1.0};
    std::vector<f64> dashes;
};

struct HatchPattern {
    std::string              name{"SOLID"};
    std::string              description;
    std::vector<PatternLine> lines;

    bool solid() const noexcept { return lines.empty(); }
};

class PatternTable {
public:
    PatternTable();

    const HatchPattern* find(std::string_view name) const;
    HatchPattern&       add(HatchPattern p);
    std::size_t         size() const noexcept { return pats_.size(); }
    const std::vector<HatchPattern>& all() const noexcept { return pats_; }

    std::size_t load_pat(std::string_view text);

private:
    std::vector<HatchPattern> pats_;
};

struct HatchStyle {
    std::string name{"ANSI31"};
    f64         scale{1.0};
    f64         angle{0.0};
    Point2      origin{0, 0};
    bool        associative{true};

    enum class Islands : gcm::u8 { Normal, Outer, Ignore } islands{Islands::Normal};
};

struct Hatch {
    std::vector<Segment2> lines;
    bool                  solid{false};
    std::vector<Loop>     fill_loops;
    const char*          error{nullptr};
};

Hatch generate_hatch(const std::vector<Loop>& loops, const HatchPattern& pattern,
                     const HatchStyle& style, const gcm::WorkingVolume& vol);

i64 loop_area_sign(const Loop& loop);

struct AssociativeHatch {
    HatchStyle style{};
    Attributes attrs{};
    bool area_background{false};
    double gap_tolerance{0.0};
    std::vector<gcm::u32> source_keys;

    // Indices into update_hatch's segment list, not the drawing's shape list.
    std::vector<gcm::u32> boundary_shapes;
    std::vector<gsnap::Shape> boundary_geometry;
    Point2                seed{};
    bool                  seed_valid{false};

    Hatch  cached{};
    gcm::u64 boundary_hash{0};
    bool   valid{false};

    bool orphaned{false};
    std::string layer;
};

gcm::u64 boundary_fingerprint(const std::vector<Segment2>& segments);

struct HatchUpdate {
    bool        regenerated{false};
    bool        orphaned{false};
    const char* reason{""};
};

HatchUpdate update_hatch(AssociativeHatch& h, const std::vector<Segment2>& segments,
                         const PatternTable& patterns,
                         const gcm::WorkingVolume& vol);

enum class GradientKind : gcm::u8 {
    Linear, Cylindrical, InvertedCylindrical,
    Spherical, InvertedSpherical, HemiSpherical, InvertedHemiSpherical,
    Curved, InvertedCurved,
};

const char* gradient_kind_name(GradientKind k) noexcept;

struct Gradient {
    GradientKind kind{GradientKind::Linear};
    Color        from{Color::aci(5)};
    Color        to{Color::aci(2)};
    bool         two_colour{true};
    f64          tint{0.5};
    f64          angle{0.0};
    bool         centred{true};
    Point2       origin{0, 0};
};

f64 gradient_at(const Gradient& g, Point2 p, const std::vector<Loop>& region,
                const gcm::WorkingVolume& vol);

Color gradient_color(const Gradient& g, f64 t);

struct GradientBand {
    std::vector<Loop> loops;
    Color             color;
    f64               t{0.0};
};

std::vector<GradientBand> gradient_bands(const Gradient& g,
                                         const std::vector<Loop>& region,
                                         int bands, const gcm::WorkingVolume& vol);

}
