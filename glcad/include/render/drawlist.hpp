#pragma once
#include <algorithm>

#include <cmath>
#include <vector>

#include "render/vertex.hpp"

namespace grender {

namespace d2 {

// color.a < 0: no override; the role decides.
inline constexpr Color kByRole{0.0f, 0.0f, 0.0f, -1.0f};

struct LineItem {
    ggeo::f64 x0, y0, x1, y1;
    Role      role;
    Color     color{kByRole};
    float     width_px{1.0f};
};
struct PointItem {
    ggeo::f64 x, y;
    Role      role;
    Color     color{kByRole};
    float     width_px{1.0f};
};
struct TriangleItem {
    ggeo::f64 xy[6];
    Color color;
};
// p(t) = C + U*(major*cos t) + V*(minor*sin t), U = (ux, uy), V = (-uy, ux)
struct ArcItem {
    ggeo::f64 cx, cy;
    ggeo::f64 ux, uy;
    ggeo::f64 major, minor;
    ggeo::f64 a0, a1;
    Role      role;
    Color     color{kByRole};
    float     width_px{1.0f};
};

class DrawList {
public:
    void clear() noexcept { lines_.clear(); points_.clear(); arcs_.clear(); triangles_.clear(); }
    void append(const DrawList& o) {
        lines_.insert(lines_.end(), o.lines_.begin(), o.lines_.end());
        points_.insert(points_.end(), o.points_.begin(), o.points_.end());
        arcs_.insert(arcs_.end(), o.arcs_.begin(), o.arcs_.end());
        triangles_.insert(triangles_.end(), o.triangles_.begin(), o.triangles_.end());
    }

    void line(ggeo::f64 x0, ggeo::f64 y0, ggeo::f64 x1, ggeo::f64 y1, Role r,
              Color c = kByRole, float width = 1.0f) {
        lines_.push_back({x0, y0, x1, y1, r, c, width});
    }
    void point(ggeo::f64 x, ggeo::f64 y, Role r, Color c = kByRole) {
        points_.push_back({x, y, r, c});
    }
    void arc(ggeo::f64 cx, ggeo::f64 cy, ggeo::f64 ux, ggeo::f64 uy,
             ggeo::f64 major, ggeo::f64 minor, ggeo::f64 a0, ggeo::f64 a1, Role r,
             Color c = kByRole, float width = 1.0f) {
        arcs_.push_back({cx, cy, ux, uy, major, minor, a0, a1, r, c, width});
    }

    template <ggeo::Scalar S>
    void segment(const ggeo::d2::Segment<S>& s, Role r) {
        line(s.a.x.to_f64(), s.a.y.to_f64(), s.b.x.to_f64(), s.b.y.to_f64(), r);
    }

    const std::vector<LineItem>&  lines() const noexcept { return lines_; }
    const std::vector<PointItem>& points() const noexcept { return points_; }
    const std::vector<ArcItem>&   arcs() const noexcept { return arcs_; }
    const std::vector<TriangleItem>& triangles() const noexcept { return triangles_; }
    void triangle(const TriangleItem& t) { triangles_.push_back(t); }

    void flatten_arcs(ggeo::f64 px_per_unit) {
        for (const auto& a : arcs_) {
            int steps = static_cast<int>((a.a1 - a.a0) * a.major * px_per_unit / 3.0);
            emit_arc(a, std::clamp(steps, 12, 512));
        }
        arcs_.clear();
    }

    void flatten_arcs_chordal(ggeo::f64 tolerance, ggeo::f64 relative = 5e-5, int min_full = 24, int max_full = 1024) {
        constexpr ggeo::f64 tau = 6.283185307179586;
        for (const auto& a : arcs_) {
            const ggeo::f64 r = std::max(std::fabs(a.major), std::fabs(a.minor));
            const ggeo::f64 tol = std::max(tolerance, r * relative);
            int full = max_full;
            if (r > tol && tol > 0) {
                const ggeo::f64 step = 2.0 * std::acos(1.0 - tol / r);
                if (step > 0) full = static_cast<int>(std::min<ggeo::f64>(std::ceil(tau / step), max_full));
            } else full = min_full;
            full = std::clamp(full, min_full, max_full);
            const ggeo::f64 share = std::fabs(a.a1 - a.a0) / tau;
            emit_arc(a, std::max(1, static_cast<int>(std::ceil(full * share - 1e-9))));
        }
        arcs_.clear();
    }

    void pack(std::vector<Vertex>& line_verts, std::vector<Vertex>& point_verts) const {
        line_verts.clear();
        point_verts.clear();
        line_verts.reserve(lines_.size() * 2);
        point_verts.reserve(points_.size());
        for (const auto& l : lines_) {
            const Color c = l.color.a < 0.0f ? color_for(l.role) : l.color;
            line_verts.push_back(Vertex::make(l.x0, l.y0, c));
            line_verts.push_back(Vertex::make(l.x1, l.y1, c));
        }
        for (const auto& p : points_)
            point_verts.push_back(Vertex::make(
                p.x, p.y, p.color.a < 0.0f ? color_for(p.role) : p.color));
    }

private:
    void emit_arc(const ArcItem& a, int steps) {
        const ggeo::f64 sweep = a.a1 - a.a0;
        ggeo::f64 px0 = 0, py0 = 0;
        for (int i = 0; i <= steps; ++i) {
            const ggeo::f64 t = a.a0 + sweep * (ggeo::f64(i) / steps);
            const ggeo::f64 lx = a.major * std::cos(t), ly = a.minor * std::sin(t);
            const ggeo::f64 x = a.cx + a.ux * lx - a.uy * ly;
            const ggeo::f64 y = a.cy + a.uy * lx + a.ux * ly;
            if (i) lines_.push_back({px0, py0, x, y, a.role, a.color, a.width_px});
            px0 = x; py0 = y;
        }
    }

    std::vector<TriangleItem> triangles_;
    std::vector<LineItem>  lines_;
    std::vector<PointItem> points_;
    std::vector<ArcItem>   arcs_;
};

}
}
