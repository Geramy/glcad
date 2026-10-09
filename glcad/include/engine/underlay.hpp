#pragma once

#include <string>
#include <vector>

#include "engine/hatch.hpp"

namespace gdoc {

enum class UnderlayKind : u8 { Pdf, Dwf, Dgn, Raster, Unknown };

const char* underlay_kind_name(UnderlayKind k) noexcept;
UnderlayKind underlay_kind_of(std::string_view path) noexcept;

struct Underlay {
    std::string  path;
    UnderlayKind kind{UnderlayKind::Unknown};
    std::string  sheet;
    std::string  name;

    Point2 insertion{};
    f64    scale{1.0};
    f64    rotation{0.0};

    i32  fade{50};
    i32  contrast{50};
    bool monochrome{false};
    bool visible{true};
    bool plottable{false};
    bool locked{true};

    std::vector<Point2> clip;
    bool                clip_inverted{false};

    Point2 extent_lo{}, extent_hi{};
    bool   found{false};

    f64 y_factor{1.0};
    bool clip_enabled{true};
    bool adjust_background{false};
    std::vector<Point2> clip_hole;
    std::string layer{"0"};
    // Metres per source coordinate (PDF underlays use inches).
    f64 source_unit{1.0};
    f64 contrast_neutral{50.0};
    bool source_unit_known{false};
    bool source_coordinates_pending{false};
    bool has_clip() const noexcept { return clip_enabled && clip.size() >= 3; }
    bool contains(Point2 p) const;         // in underlay coordinates
    void to_drawing(Point2 p, const gcm::WorkingVolume& v, f64& x, f64& y) const;
    void to_underlay(f64 x, f64 y, const gcm::WorkingVolume& v, Point2& out) const;
};

class UnderlayTable {
public:
    Underlay&       attach(Underlay u);
    const Underlay* find(std::string_view name) const;
    Underlay*       find(std::string_view name);
    bool            detach(std::string_view name);

    std::size_t size() const noexcept { return items_.size(); }
    const std::vector<Underlay>& all() const noexcept { return items_; }

    std::vector<const Underlay*> draw_order() const;

    bool calibrate(std::string_view name, Point2 a, Point2 b, f64 real_distance,
                   const gcm::WorkingVolume& v);

private:
    std::vector<Underlay> items_;
};

}
