#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "math/fixed.hpp"
#include "math/predicate.hpp"
#include "engine/appearance.hpp"

namespace gsnap {

using gcm::f64;
using gcm::i64;
using gcm::u32;
using gcm::u8;
using Point = gcm::Lattice2;

// Bit values match AutoCAD's OSMODE.
enum Kind : u32 {
    None                 = 0,
    Endpoint             = 1u << 0,
    Midpoint             = 1u << 1,
    Center               = 1u << 2,
    Node                 = 1u << 3,
    Quadrant             = 1u << 4,
    Intersection         = 1u << 5,
    Insertion            = 1u << 6,
    Perpendicular        = 1u << 7,
    Tangent              = 1u << 8,
    Nearest              = 1u << 9,
    ApparentIntersection = 1u << 11,
    Extension            = 1u << 12,
    Parallel             = 1u << 13,
    GeometricCenter      = 1u << 14,
    Grid                 = 1u << 20,
    Polar                = 1u << 21,
};

const char* kind_name(Kind k) noexcept;

// Lower number wins.
int priority(Kind k) noexcept;

enum class ShapeKind : u8 {
    Segment, Circle, Arc, Point, Block,
    // `b` is the major-axis endpoint (radius and rotation); `radius` is the minor.
    Ellipse,
    Ray, Xline,
};

const char* shape_kind_name(ShapeKind k) noexcept;

struct Shape {
    ShapeKind kind{ShapeKind::Segment};
    std::string layer;
    gdoc::Attributes attrs;
    Point     a{}, b{};
    i64       radius{0};       // lattice units
    f64       start_angle{0.0}, end_angle{0.0};   // arc, radians, CCW from +X
    // Corpus index, not a document identity: changes on every refill.
    u32       id{0};

    // Stable document handle, never reused; 0 = not yet assigned.
    u32       key{0};

    // 0 = the shape stands alone.
    u32       poly{0};
    u32       poly_index{0};

    bool region{false};
    u32 region_loop{0};
    bool region_reversed{false};

    bool in_polyline() const noexcept { return poly != 0; }

    bool is_curve() const noexcept {
        return kind == ShapeKind::Circle || kind == ShapeKind::Arc;
    }
    bool is_elliptical() const noexcept { return kind == ShapeKind::Ellipse; }
    double ellipse_sweep() const noexcept {
        constexpr double tau=6.28318530717958647692;
        const double delta=end_angle-start_angle;
        if(std::abs(delta)<1e-12||std::abs(delta)>=tau-1e-12)return tau;
        const double sweep=std::fmod(delta,tau);return sweep<0?sweep+tau:sweep;
    }
    bool ellipse_contains(double angle) const noexcept {
        constexpr double tau=6.28318530717958647692;
        double offset=std::fmod(angle-start_angle,tau);if(offset<0)offset+=tau;
        return offset<=ellipse_sweep()+1e-12;
    }
    bool is_infinite() const noexcept {
        return kind == ShapeKind::Ray || kind == ShapeKind::Xline;
    }
    bool is_linear() const noexcept {
        return kind == ShapeKind::Segment || is_infinite();
    }
};

struct Result {
    bool   valid{false};
    Point  point{};
    Kind   kind{None};
    u32    shape_a{0}, shape_b{0};
    double distance{0.0};            // world units from the cursor

    const char* name() const noexcept { return kind_name(kind); }
};

struct TrackPoint {
    Point  point{};
    Kind   from{None};
    double acquired_at{0.0};
};

struct Guide {
    Point  origin{};
    double angle_deg{0.0};
    bool   from_tracking{false};
};

struct Settings {
    u32    running{Endpoint | Midpoint | Center | Intersection};
    bool   enabled{true};
    u32    forced{None};
    double aperture_px{12.0};
    double scale{40.0};         // pixels per world unit
    bool   tracking{true};
    double track_tolerance_deg{1.5};
    std::vector<double> track_angles{0.0, 90.0, 180.0, 270.0};

    bool wants(Kind k) const noexcept {
        if (forced != None) return (forced & k) != 0;
        return enabled && (running & k) != 0;
    }
    double aperture_world() const noexcept {
        return scale > 0.0 ? aperture_px / scale : 0.0;
    }
};

class Engine {
public:
    Settings settings;

    void clear() { shapes_.clear(); }
    u32  add(Shape s);
    const std::vector<Shape>& shapes() const noexcept { return shapes_; }
    std::size_t size() const noexcept { return shapes_.size(); }

    Result snap(const gcm::WorkingVolume& vol, double wx, double wy) const;

    Result snap_from(const gcm::WorkingVolume& vol, double wx, double wy,
                     std::optional<Point> anchor) const;

    std::vector<Kind> applicable(const gcm::WorkingVolume& vol, double wx,
                                 double wy,
                                 std::optional<Point> anchor = std::nullopt) const;

    void acquire(const Result& r, double now);
    void release_all() { tracked_.clear(); }
    const std::vector<TrackPoint>& tracked() const noexcept { return tracked_; }

    std::vector<Guide> guides() const;
    Result track(const gcm::WorkingVolume& vol, double wx, double wy) const;

    void acquire_parallel(u32 shape_id);
    void release_parallel() { parallel_.clear(); }
    const std::vector<u32>& parallel_references() const noexcept { return parallel_; }

    Result snap_parallel(const gcm::WorkingVolume& vol, double wx, double wy,
                         Point anchor) const;

private:
    Result snap_with(const Settings& settings, const gcm::WorkingVolume& vol,
                     double wx, double wy, std::optional<Point> anchor) const;

    std::vector<Shape>      shapes_;
    std::vector<TrackPoint> tracked_;
    std::vector<u32>        parallel_;
};

std::optional<Point> line_line_intersection(Point a0, Point a1, Point b0, Point b1,
                                            bool allow_extension);
Point closest_point_on_segment(Point a, Point b, double wx, double wy,
                               const gcm::WorkingVolume& vol);
std::vector<Point> circle_circle_intersection(Point ca, i64 ra, Point cb, i64 rb);

std::vector<Point> line_circle_intersection(Point a, Point b, Point centre,
                                            i64 radius);
bool angle_within_arc(double angle, double start, double end);

}
