#pragma once

#include <string>
#include <vector>

#include "geometry/surface.hpp"

namespace gtopo {

enum class CurveKind : gcm::u8 {
    None,
    Point,
    Line,
    Circle,
    Ellipse,
    Marched,
    Coincident,
};

const char* curve_kind_name(CurveKind k) noexcept;

struct Pt3 { f64 x{0}, y{0}, z{0}; };

struct IntersectionCurve {
    CurveKind kind{CurveKind::None};

    Pt3 origin{};                 // a point on a line, or a conic's centre
    Pt3 direction{};              // a line's direction, or a conic's normal
    Pt3 major{}, minor_dir{};     // ellipse axes
    f64 radius{0.0};              // circle
    f64 major_radius{0.0}, minor_radius{0.0};

    std::vector<Pt3> polyline;
    bool closed{false};
    f64  tolerance{0.0};          // 0 for the exact kinds

    bool exact() const noexcept {
        return kind != CurveKind::Marched && kind != CurveKind::None;
    }
    f64 length() const;
};

struct SsiRegion {
    f64 lo[3]{-1e3, -1e3, -1e3};
    f64 hi[3]{1e3, 1e3, 1e3};
    bool contains(f64 x, f64 y, f64 z, f64 slack = 0.0) const noexcept;
    f64  diagonal() const noexcept;
};

struct SsiResult {
    std::vector<IntersectionCurve> curves;
    bool        ok{false};
    bool        was_exact{false};
    std::string reason;

    std::size_t size() const noexcept { return curves.size(); }
    bool empty() const noexcept { return curves.empty(); }
};

// `tolerance` bounds marched-point distance from either surface.
SsiResult intersect_surfaces(const Surface& a, const Surface& b,
                             const SsiRegion& region, f64 tolerance = 1e-9);

bool has_exact_form(const Surface& a, const Surface& b) noexcept;

bool project_onto_both(const Surface& a, const Surface& b, f64 x, f64 y, f64 z,
                       f64& px, f64& py, f64& pz, f64 tolerance = 1e-12,
                       int max_iterations = 40);

}
