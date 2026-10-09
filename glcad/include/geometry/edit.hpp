#pragma once

#include <optional>
#include <vector>

#include "geometry/snap.hpp"

namespace gedit {

using gcm::f64;
using gcm::i64;
using gcm::u32;
using Shape = gsnap::Shape;
using Point = gcm::Lattice2;
using Volume = gcm::WorkingVolume;

Shape translate(const Shape& s, i64 dx, i64 dy);
Shape rotate(const Shape& s, Point centre, f64 radians, const Volume& v);
Shape scale(const Shape& s, Point centre, f64 factor);
Shape mirror(const Shape& s, Point a, Point b, const Volume& v);

std::vector<Shape> translate_all(const std::vector<Shape>& in, i64 dx, i64 dy);
std::vector<Shape> mirror_all(const std::vector<Shape>& in, Point a, Point b,
                              const Volume& v);

std::vector<Shape> array_rectangular(const std::vector<Shape>& in, int columns,
                                     int rows, f64 dx, f64 dy, const Volume& v);
std::vector<Shape> array_polar(const std::vector<Shape>& in, Point centre,
                               int count, f64 total_angle, bool rotate_items,
                               const Volume& v);
std::vector<Shape> array_path(const std::vector<Shape>& in,
                              const std::vector<Point>& path, int count,
                              const Volume& v);

std::optional<Shape> offset(const Shape& s, f64 distance, Point through,
                            const Volume& v);

struct TrimResult {
    bool               changed{false};
    std::vector<Shape> pieces;      // what survives; empty means fully removed
    const char*        reason{""};
};

TrimResult trim(const Shape& target, const std::vector<Shape>& cutters,
                Point pick, const Volume& v);

TrimResult extend(const Shape& target, const std::vector<Shape>& boundaries,
                  Point pick, const Volume& v);

struct CornerResult {
    bool  ok{false};
    Shape a;
    Shape b;
    Shape connector;
    const char* reason{""};
};

CornerResult fillet(const Shape& a, const Shape& b, f64 radius, Point pick_a,
                    Point pick_b, const Volume& v);

CornerResult chamfer(const Shape& a, const Shape& b, f64 dist_a, f64 dist_b,
                     Point pick_a, Point pick_b, const Volume& v);

CornerResult fillet_any(const Shape& a, const Shape& b, f64 radius, Point pick_a,
                        Point pick_b, const Volume& v);
CornerResult chamfer_any(const Shape& a, const Shape& b, f64 dist_a, f64 dist_b,
                         Point pick_a, Point pick_b, const Volume& v);

struct PolylineCornerResult {
    std::vector<Shape> shapes;
    std::size_t        rounded{0};
    std::size_t        skipped{0};
    const char*        reason{""};
};

PolylineCornerResult fillet_polyline(const std::vector<Shape>& run, f64 radius,
                                     const Volume& v);
PolylineCornerResult chamfer_polyline(const std::vector<Shape>& run, f64 distance,
                                      const Volume& v);

enum class GripKind : u32 {
    Endpoint,
    Midpoint,
    Centre,
    Quadrant,
    Vertex,
    Stretch,
};

const char* grip_kind_name(GripKind k) noexcept;

struct Grip {
    Point    at{};
    GripKind kind{GripKind::Endpoint};
    u32      shape{0};
    u32      index{0};
    bool     hot{false};
};

std::vector<Grip> grips_of(const Shape& s, const Volume& v);
std::vector<Grip> grips_of(const std::vector<Shape>& shapes, const Volume& v);

inline constexpr u32 kNoGrip = 0xFFFFFFFFu;
Grip pick_grip(const std::vector<Grip>& grips, Point at, const Volume& v,
               f64 aperture_world);

std::optional<Shape> drag_grip(const Shape& s, const Grip& g, Point to,
                               const Volume& v);

enum class GripOp : u32 {
    Stretch,
    Move,
    Lengthen,
    ConvertToArc,
    ConvertToLine,
    Radius,
    Diameter,
    Rotate,
    AddVertex,
    RemoveVertex,
};

const char* grip_op_name(GripOp o) noexcept;

struct GripOption {
    GripOp      op{GripOp::Stretch};
    const char* label{""};
    bool        needs_point{true};
};

std::vector<GripOption> grip_menu(const Shape& s, const Grip& g);

std::optional<Shape> apply_grip_op(const Shape& s, const Grip& g, GripOp op,
                                   Point to, const Volume& v);

struct Window {
    Point lo{}, hi{};
    bool contains(Point p) const noexcept;
    bool crosses(const Shape& s) const;
    bool contains(const Shape& s) const;
};

Window make_window(Point a, Point b);

struct StretchResult {
    std::vector<Shape> shapes;
    std::size_t moved{0};
    std::size_t stretched{0};
    std::size_t untouched{0};
};

StretchResult stretch(const std::vector<Shape>& in, const Window& w, i64 dx,
                      i64 dy);

enum class LengthenMode : u32 { Total, Delta, Percent };

std::optional<Shape> lengthen(const Shape& s, LengthenMode mode, f64 value,
                              Point pick, const Volume& v);

f64 length_of(const Shape& s, const Volume& v);

struct BreakResult {
    std::vector<Shape> pieces;
    bool               changed{false};
    const char*        reason{""};
};

BreakResult break_between(const Shape& s, Point p0, Point p1, const Volume& v);
BreakResult break_at(const Shape& s, Point p, const Volume& v);

struct JoinResult {
    std::vector<Shape> shapes;
    std::size_t        joined{0};
    const char*        reason{""};
};

JoinResult join(const std::vector<Shape>& in, const Volume& v,
                f64 tolerance_world = 1e-9);

std::vector<Shape> align(const std::vector<Shape>& in, Point src_a, Point src_b,
                         Point dst_a, Point dst_b, bool scale_to_fit,
                         const Volume& v);

std::vector<Point> intersections(const Shape& a, const Shape& b);
bool point_on_shape(const Shape& s, Point p, const Volume& v, f64 tol_world);
f64  parameter_on_segment(const Shape& s, Point p);

}
