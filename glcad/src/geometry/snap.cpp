#include "geometry/snap.hpp"

#include <algorithm>
#include <cmath>

namespace gsnap {

constexpr double kPi = 3.14159265358979323846;

const char* kind_name(Kind k) noexcept {
    switch (k) {
        case Endpoint:             return "endpoint";
        case Midpoint:             return "midpoint";
        case Center:               return "center";
        case Node:                 return "node";
        case Quadrant:             return "quadrant";
        case Intersection:         return "intersection";
        case Insertion:            return "insertion";
        case Perpendicular:        return "perpendicular";
        case Tangent:              return "tangent";
        case Nearest:              return "nearest";
        case ApparentIntersection: return "apparent int";
        case Extension:            return "extension";
        case Parallel:             return "parallel";
        case GeometricCenter:      return "geo center";
        case Grid:                 return "grid";
        case Polar:                return "polar";
        default:                   return "-";
    }
}

int priority(Kind k) noexcept {
    switch (k) {
        case Endpoint:             return 0;
        case Node:                 return 0;
        case Insertion:            return 1;
        case Center:               return 2;
        case GeometricCenter:      return 2;
        case Quadrant:             return 3;
        case Intersection:         return 4;
        case ApparentIntersection: return 5;
        case Midpoint:             return 6;
        case Perpendicular:        return 7;
        case Tangent:              return 7;
        case Parallel:             return 8;
        case Extension:            return 9;
        case Nearest:              return 10;
        case Polar:                return 11;
        case Grid:                 return 12;
        default:                   return 99;
    }
}

const char* shape_kind_name(ShapeKind k) noexcept {
    switch (k) {
        case ShapeKind::Segment: return "segment";
        case ShapeKind::Circle:  return "circle";
        case ShapeKind::Arc:     return "arc";
        case ShapeKind::Point:   return "point";
        case ShapeKind::Block:   return "block";
        case ShapeKind::Ellipse: return "ellipse";
        case ShapeKind::Ray:     return "ray";
        case ShapeKind::Xline:   return "xline";
    }
    return "?";
}

u32 Engine::add(Shape s) {
    s.id = static_cast<u32>(shapes_.size()) + 1;
    shapes_.push_back(s);
    return s.id;
}

std::optional<Point> line_line_intersection(Point a0, Point a1, Point b0, Point b1,
                                            bool allow_extension) {
    const double x1 = double(a0.x), y1 = double(a0.y);
    const double x2 = double(a1.x), y2 = double(a1.y);
    const double x3 = double(b0.x), y3 = double(b0.y);
    const double x4 = double(b1.x), y4 = double(b1.y);

    const double den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::fabs(den) < 1e-9) return std::nullopt;

    const double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / den;
    const double u = ((x1 - x3) * (y1 - y2) - (y1 - y3) * (x1 - x2)) / den;

    if (!allow_extension && (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0))
        return std::nullopt;

    return Point{static_cast<i64>(std::llround(x1 + t * (x2 - x1))),
                 static_cast<i64>(std::llround(y1 + t * (y2 - y1)))};
}

Point closest_point_on_segment(Point a, Point b, double wx, double wy,
                               const gcm::WorkingVolume& vol) {
    double ax, ay, bx, by;
    gcm::to_world(vol, a, ax, ay);
    gcm::to_world(vol, b, bx, by);
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-24) return a;
    double t = ((wx - ax) * dx + (wy - ay) * dy) / len2;
    t = std::clamp(t, 0.0, 1.0);
    return gcm::to_lattice(vol, ax + dx * t, ay + dy * t);
}

std::vector<Point> circle_circle_intersection(Point ca, i64 ra, Point cb, i64 rb) {
    std::vector<Point> out;
    if (ra <= 0 || rb <= 0) return out;
    const double dx = double(cb.x - ca.x), dy = double(cb.y - ca.y);
    const double d = std::hypot(dx, dy);
    const double r1 = double(ra), r2 = double(rb);
    if (d < 1e-9) return out;
    if (d > r1 + r2 + 1e-9 || d < std::fabs(r1 - r2) - 1e-9) return out;

    const double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double h2 = r1 * r1 - a * a;
    const double h = h2 > 0.0 ? std::sqrt(h2) : 0.0;
    const double mx = double(ca.x) + a * dx / d;
    const double my = double(ca.y) + a * dy / d;
    const double ox = -dy / d * h, oy = dx / d * h;
    out.push_back({i64(std::llround(mx + ox)), i64(std::llround(my + oy))});
    if (h > 1e-9)
        out.push_back({i64(std::llround(mx - ox)), i64(std::llround(my - oy))});
    return out;
}

std::vector<Point> line_circle_intersection(Point a, Point b, Point c, i64 r) {
    std::vector<Point> out;
    const double dx = double(b.x - a.x), dy = double(b.y - a.y);
    const double fx = double(a.x - c.x), fy = double(a.y - c.y);
    const double A = dx * dx + dy * dy;
    if (A < 1e-9) return out;
    const double B = 2.0 * (fx * dx + fy * dy);
    const double C = fx * fx + fy * fy - double(r) * double(r);
    const double disc = B * B - 4.0 * A * C;
    if (disc < 0.0) return out;

    const double sq = std::sqrt(disc);
    for (double t : {(-B - sq) / (2.0 * A), (-B + sq) / (2.0 * A)}) {
        if (t < -1e-9 || t > 1.0 + 1e-9) continue;
        out.push_back({static_cast<i64>(std::llround(double(a.x) + t * dx)),
                       static_cast<i64>(std::llround(double(a.y) + t * dy))});
    }
    return out;
}

bool angle_within_arc(double angle, double start, double end) {
    auto norm = [](double v) {
        while (v < 0.0) v += 2.0 * kPi;
        while (v >= 2.0 * kPi) v -= 2.0 * kPi;
        return v;
    };
    const double a = norm(angle), s = norm(start), e = norm(end);
    if (s <= e) return a >= s - 1e-9 && a <= e + 1e-9;
    return a >= s - 1e-9 || a <= e + 1e-9;
}

namespace {

struct Best {
    Result r;
    double best_d{1e300};
    int    best_prio{999};

    void offer(Point p, Kind k, u32 sa, u32 sb, double d, double aperture) {
        if (d > aperture) return;
        const int prio = priority(k);
        if (prio < best_prio || (prio == best_prio && d < best_d)) {
            best_prio = prio;
            best_d = d;
            r = {true, p, k, sa, sb, d};
        }
    }
};

}

Result Engine::snap(const gcm::WorkingVolume& vol, double wx, double wy) const {
    return snap_from(vol, wx, wy, std::nullopt);
}

Result Engine::snap_from(const gcm::WorkingVolume& vol, double wx, double wy,
                         std::optional<Point> anchor) const {
    return snap_with(settings, vol, wx, wy, anchor);
}

Result Engine::snap_with(const Settings& settings, const gcm::WorkingVolume& vol,
                         double wx, double wy,
                         std::optional<Point> anchor) const {
    Best best;
    const double ap = settings.aperture_world();
    if (ap <= 0.0) return best.r;

    auto dist = [&](Point p) {
        double x, y;
        gcm::to_world(vol, p, x, y);
        return std::hypot(x - wx, y - wy);
    };
    auto offer = [&](Point p, Kind k, u32 sa, u32 sb = 0) {
        if (!settings.wants(k)) return;
        best.offer(p, k, sa, sb, dist(p), ap);
    };

    const Point cur = gcm::to_lattice(vol, wx, wy);
    const i64 margin = static_cast<i64>(ap * vol.scale() * 2.0) + 1;
    const bool want_ext = settings.wants(Extension) || settings.wants(ApparentIntersection);
    std::vector<u32> near_shapes;
    near_shapes.reserve(64);
    for (u32 idx = 0; idx < static_cast<u32>(shapes_.size()); ++idx) {
        const Shape& s = shapes_[idx];
        i64 lox = s.a.x, hix = s.a.x, loy = s.a.y, hiy = s.a.y;
        bool infinite = false;
        if (s.kind == ShapeKind::Segment) {
            lox = std::min(s.a.x, s.b.x); hix = std::max(s.a.x, s.b.x);
            loy = std::min(s.a.y, s.b.y); hiy = std::max(s.a.y, s.b.y);
        } else if (s.is_curve()) {
            lox = s.a.x - s.radius; hix = s.a.x + s.radius;
            loy = s.a.y - s.radius; hiy = s.a.y + s.radius;
        } else if (s.kind == ShapeKind::Ellipse) {
            const i64 r = std::llabs(s.b.x - s.a.x) + std::llabs(s.b.y - s.a.y) + s.radius;
            lox = s.a.x - r; hix = s.a.x + r;
            loy = s.a.y - r; hiy = s.a.y + r;
        } else if (s.is_infinite()) {
            infinite = true;
        }
        bool keep = infinite ||
                    (cur.x >= lox - margin && cur.x <= hix + margin &&
                     cur.y >= loy - margin && cur.y <= hiy + margin);
        if (!keep && want_ext && s.kind == ShapeKind::Segment) {
            const double dx = double(s.b.x - s.a.x), dy = double(s.b.y - s.a.y);
            const double len = std::hypot(dx, dy);
            if (len > 0.5) {
                const double d = std::fabs(dx * double(cur.y - s.a.y) - dy * double(cur.x - s.a.x)) / len;
                keep = d <= double(margin);
            }
        }
        if (keep) near_shapes.push_back(idx);
    }

    for (const u32 near_idx : near_shapes) {
        const Shape& s = shapes_[near_idx];
        switch (s.kind) {
            case ShapeKind::Point:
                offer(s.a, Node, s.id);
                break;

            case ShapeKind::Block:
                offer(s.a, Insertion, s.id);
                break;

            case ShapeKind::Segment: {
                offer(s.a, Endpoint, s.id);
                offer(s.b, Endpoint, s.id);
                offer(Point{(s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2}, Midpoint, s.id);
                offer(closest_point_on_segment(s.a, s.b, wx, wy, vol), Nearest, s.id);

                if (anchor && settings.wants(Perpendicular)) {
                    double ax, ay, bx, by, px, py;
                    gcm::to_world(vol, s.a, ax, ay);
                    gcm::to_world(vol, s.b, bx, by);
                    gcm::to_world(vol, *anchor, px, py);
                    const double dx = bx - ax, dy = by - ay;
                    const double len2 = dx * dx + dy * dy;
                    if (len2 > 1e-24) {
                        const double t = ((px - ax) * dx + (py - ay) * dy) / len2;
                        if (t >= -1e-9 && t <= 1.0 + 1e-9)
                            offer(gcm::to_lattice(vol, ax + dx * t, ay + dy * t),
                                  Perpendicular, s.id);
                    }
                }

                if (settings.wants(Extension)) {
                    double ax, ay, bx, by;
                    gcm::to_world(vol, s.a, ax, ay);
                    gcm::to_world(vol, s.b, bx, by);
                    const double dx = bx - ax, dy = by - ay;
                    const double len2 = dx * dx + dy * dy;
                    if (len2 > 1e-24) {
                        const double t = ((wx - ax) * dx + (wy - ay) * dy) / len2;
                        if (t < 0.0 || t > 1.0)
                            offer(gcm::to_lattice(vol, ax + dx * t, ay + dy * t),
                                  Extension, s.id);
                    }
                }
                break;
            }

            case ShapeKind::Ellipse: {
                offer(s.a, Center, s.id);

                double cx, cy, mx, my;
                gcm::to_world(vol, s.a, cx, cy);
                gcm::to_world(vol, s.b, mx, my);
                const double ax = mx - cx, ay = my - cy;
                const double major = std::hypot(ax, ay);
                if (major < 1e-12) break;
                const double ux = ax / major, uy = ay / major;
                const double minor = double(s.radius) * vol.inv_scale();

                for (int q = 0; q < 4; ++q) {
                    const double t = q * kPi * 0.5;
                    if(!s.ellipse_contains(t))continue;
                    const double lx = std::cos(t) * major, ly = std::sin(t) * minor;
                    offer(gcm::to_lattice(vol, cx + ux * lx - uy * ly,
                                          cy + uy * lx + ux * ly),
                          Quadrant, s.id);
                }

                const auto point_at=[&](double t){return gcm::to_lattice(vol,cx+ux*major*std::cos(t)-uy*minor*std::sin(t),cy+uy*major*std::cos(t)+ux*minor*std::sin(t));};
                if(s.ellipse_sweep()<2*kPi-1e-12) {
                    offer(point_at(s.start_angle),Endpoint,s.id);offer(point_at(s.end_angle),Endpoint,s.id);
                    offer(point_at(s.start_angle+s.ellipse_sweep()*.5),Midpoint,s.id);
                    offer(point_at(s.start_angle),Nearest,s.id);offer(point_at(s.end_angle),Nearest,s.id);
                }

                // Four Newton steps land within a lattice unit.
                {
                    const double dx = wx - cx, dy = wy - cy;
                    const double lx = dx * ux + dy * uy;
                    const double ly = -dx * uy + dy * ux;
                    double t = std::atan2(ly * major, lx * minor);
                    for (int it = 0; it < 8; ++it) {
                        const double ct = std::cos(t), st = std::sin(t);
                        const double px = major * ct, py = minor * st;
                        const double dpx = -major * st, dpy = minor * ct;
                        const double f = (lx - px) * dpx + (ly - py) * dpy;
                        const double df = -dpx * dpx - dpy * dpy -
                                          (lx - px) * major * ct -
                                          (ly - py) * minor * st;
                        if (std::fabs(df) < 1e-15) break;
                        t -= f / df;
                    }
                    if(!s.ellipse_contains(t))break;
                    const double px = major * std::cos(t), py = minor * std::sin(t);
                    offer(gcm::to_lattice(vol, cx + ux * px - uy * py,
                                          cy + uy * px + ux * py),
                          Nearest, s.id);
                }
                break;
            }

            case ShapeKind::Ray:
            case ShapeKind::Xline: {
                if (s.kind == ShapeKind::Ray) offer(s.a, Endpoint, s.id);

                double ax, ay, bx, by;
                gcm::to_world(vol, s.a, ax, ay);
                gcm::to_world(vol, s.b, bx, by);
                const double dx = bx - ax, dy = by - ay;
                const double len2 = dx * dx + dy * dy;
                if (len2 < 1e-24) break;

                double t = ((wx - ax) * dx + (wy - ay) * dy) / len2;
                if (s.kind == ShapeKind::Ray && t < 0.0) t = 0.0;
                offer(gcm::to_lattice(vol, ax + dx * t, ay + dy * t), Nearest, s.id);

                if (anchor && settings.wants(Perpendicular)) {
                    double px, py;
                    gcm::to_world(vol, *anchor, px, py);
                    double pt = ((px - ax) * dx + (py - ay) * dy) / len2;
                    if (s.kind == ShapeKind::Ray && pt < 0.0) pt = 0.0;
                    offer(gcm::to_lattice(vol, ax + dx * pt, ay + dy * pt),
                          Perpendicular, s.id);
                }
                break;
            }

            case ShapeKind::Circle:
            case ShapeKind::Arc: {
                offer(s.a, Center, s.id);

                for (int q = 0; q < 4; ++q) {
                    const double ang = q * kPi * 0.5;
                    if (s.kind == ShapeKind::Arc &&
                        !angle_within_arc(ang, s.start_angle, s.end_angle))
                        continue;
                    offer(Point{s.a.x + static_cast<i64>(std::llround(std::cos(ang) * double(s.radius))),
                                s.a.y + static_cast<i64>(std::llround(std::sin(ang) * double(s.radius)))},
                          Quadrant, s.id);
                }

                if (s.kind == ShapeKind::Arc) {
                    for (double ang : {s.start_angle, s.end_angle})
                        offer(Point{s.a.x + static_cast<i64>(std::llround(std::cos(ang) * double(s.radius))),
                                    s.a.y + static_cast<i64>(std::llround(std::sin(ang) * double(s.radius)))},
                              Endpoint, s.id);
                    const double mid = (s.start_angle + s.end_angle) * 0.5;
                    if (angle_within_arc(mid, s.start_angle, s.end_angle))
                        offer(Point{s.a.x + static_cast<i64>(std::llround(std::cos(mid) * double(s.radius))),
                                    s.a.y + static_cast<i64>(std::llround(std::sin(mid) * double(s.radius)))},
                              Midpoint, s.id);
                }

                {
                    double cx, cy;
                    gcm::to_world(vol, s.a, cx, cy);
                    const double dx = wx - cx, dy = wy - cy;
                    const double len = std::hypot(dx, dy);
                    if (len > 1e-12) {
                        const double rw = double(s.radius) * vol.inv_scale();
                        const double ang = std::atan2(dy, dx);
                        if (s.kind != ShapeKind::Arc ||
                            angle_within_arc(ang, s.start_angle, s.end_angle))
                            offer(gcm::to_lattice(vol, cx + dx / len * rw,
                                                  cy + dy / len * rw), Nearest, s.id);
                    }
                }

                if (anchor && settings.wants(Tangent)) {
                    double cx, cy, px, py;
                    gcm::to_world(vol, s.a, cx, cy);
                    gcm::to_world(vol, *anchor, px, py);
                    const double dx = px - cx, dy = py - cy;
                    const double d = std::hypot(dx, dy);
                    const double rw = double(s.radius) * vol.inv_scale();
                    if (d > rw + 1e-12) {
                        const double base = std::atan2(dy, dx);
                        const double off  = std::acos(std::clamp(rw / d, -1.0, 1.0));
                        for (double sgn : {1.0, -1.0}) {
                            const double ang = base + sgn * off;
                            if (s.kind == ShapeKind::Arc &&
                                !angle_within_arc(ang, s.start_angle, s.end_angle))
                                continue;
                            offer(gcm::to_lattice(vol, cx + std::cos(ang) * rw,
                                                  cy + std::sin(ang) * rw),
                                  Tangent, s.id);
                        }
                    }
                }
                break;
            }
        }
    }

    if (settings.wants(GeometricCenter)) {
        std::vector<u32> seen;
        for (const Shape& probe : shapes_) {
            const u32 id = probe.poly;
            if (id == 0) continue;
            bool done = false;
            for (u32 k : seen) if (k == id) { done = true; break; }
            if (done) continue;
            seen.push_back(id);

            std::vector<const Shape*> run;
            i64 lox = 0, hix = 0, loy = 0, hiy = 0;
            for (const Shape& s : shapes_)
                if (s.poly == id) {
                    if (run.empty()) {
                        lox = hix = s.a.x;
                        loy = hiy = s.a.y;
                    }
                    lox = std::min({lox, s.a.x, s.b.x});
                    hix = std::max({hix, s.a.x, s.b.x});
                    loy = std::min({loy, s.a.y, s.b.y});
                    hiy = std::max({hiy, s.a.y, s.b.y});
                    run.push_back(&s);
                }
            if (run.size() < 3) continue;
            if (cur.x < lox - margin || cur.x > hix + margin ||
                cur.y < loy - margin || cur.y > hiy + margin)
                continue;
            std::sort(run.begin(), run.end(), [](const Shape* a, const Shape* b) {
                return a->poly_index < b->poly_index;
            });
            if (run.back()->b.x != run.front()->a.x ||
                run.back()->b.y != run.front()->a.y)
                continue;

            // World units so the shoelace sum cannot overflow.
            double area2 = 0.0, cx = 0.0, cy = 0.0;
            for (const Shape* piece : run) {
                double x0, y0, x1, y1;
                gcm::to_world(vol, piece->a, x0, y0);
                gcm::to_world(vol, piece->b, x1, y1);
                const double cross = x0 * y1 - x1 * y0;
                area2 += cross;
                cx += (x0 + x1) * cross;
                cy += (y0 + y1) * cross;
            }
            if (std::fabs(area2) < 1e-12) continue;
            offer(gcm::to_lattice(vol, cx / (3.0 * area2), cy / (3.0 * area2)),
                  GeometricCenter, run.front()->id);
        }
    }

    if (settings.wants(Intersection) || settings.wants(ApparentIntersection)) {
        for (std::size_t i = 0; i < near_shapes.size(); ++i) {
            for (std::size_t j = i + 1; j < near_shapes.size(); ++j) {
                const Shape& p = shapes_[near_shapes[i]];
                const Shape& q = shapes_[near_shapes[j]];

                if (p.kind == ShapeKind::Segment && q.kind == ShapeKind::Segment) {
                    if (settings.wants(Intersection))
                        if (auto x = line_line_intersection(p.a, p.b, q.a, q.b, false))
                            offer(*x, Intersection, p.id, q.id);
                    if (settings.wants(ApparentIntersection))
                        if (auto x = line_line_intersection(p.a, p.b, q.a, q.b, true))
                            offer(*x, ApparentIntersection, p.id, q.id);
                } else if (p.kind == ShapeKind::Segment && q.is_curve()) {
                    for (Point x : line_circle_intersection(p.a, p.b, q.a, q.radius))
                        offer(x, Intersection, p.id, q.id);
                } else if (q.kind == ShapeKind::Segment && p.is_curve()) {
                    for (Point x : line_circle_intersection(q.a, q.b, p.a, p.radius))
                        offer(x, Intersection, q.id, p.id);
                } else if (p.is_curve() && q.is_curve()) {
                    for (Point x : circle_circle_intersection(p.a, p.radius, q.a, q.radius)) {
                        const auto on = [&](const Shape& s2) {
                            if (s2.kind != ShapeKind::Arc) return true;
                            return angle_within_arc(
                                std::atan2(double(x.y - s2.a.y), double(x.x - s2.a.x)),
                                s2.start_angle, s2.end_angle);
                        };
                        if (on(p) && on(q)) offer(x, Intersection, p.id, q.id);
                    }
                }
            }
        }
    }

    return best.r;
}

std::vector<Kind> Engine::applicable(const gcm::WorkingVolume& vol, double wx,
                                     double wy,
                                     std::optional<Point> anchor) const {
    static constexpr Kind kOrder[] = {
        Endpoint, Intersection, ApparentIntersection, Center, GeometricCenter,
        Midpoint, Quadrant, Node, Insertion, Perpendicular, Tangent, Parallel,
        Extension, Nearest,
    };

    std::vector<Kind> out;
    Settings probe = settings;
    probe.enabled = true;
    for (Kind k : kOrder) {
        if (!anchor && (k == Perpendicular || k == Tangent)) continue;
        probe.forced = static_cast<u32>(k);
        const Result r = snap_with(probe, vol, wx, wy, anchor);
        if (r.valid && r.kind == k) out.push_back(k);
    }
    return out;
}

void Engine::acquire(const Result& r, double now) {
    if (!r.valid) return;
    for (const auto& t : tracked_)
        if (t.point.x == r.point.x && t.point.y == r.point.y) return;
    tracked_.push_back({r.point, r.kind, now});
    if (tracked_.size() > 7) tracked_.erase(tracked_.begin());
}

std::vector<Guide> Engine::guides() const {
    std::vector<Guide> out;
    for (const auto& t : tracked_)
        for (double a : settings.track_angles)
            out.push_back({t.point, a, true});
    return out;
}

Result Engine::track(const gcm::WorkingVolume& vol, double wx, double wy) const {
    Result r;
    if (!settings.tracking || tracked_.empty()) return r;

    const double ap = settings.aperture_world();

    {
        double best_d = ap;
        for (std::size_t i = 0; i < tracked_.size(); ++i) {
            for (std::size_t j = i + 1; j < tracked_.size(); ++j) {
                double ax, ay, bx, by;
                gcm::to_world(vol, tracked_[i].point, ax, ay);
                gcm::to_world(vol, tracked_[j].point, bx, by);
                for (double a1 : settings.track_angles) {
                    for (double a2 : settings.track_angles) {
                        const double r1 = a1 * kPi / 180.0, r2 = a2 * kPi / 180.0;
                        const double d1x = std::cos(r1), d1y = std::sin(r1);
                        const double d2x = std::cos(r2), d2y = std::sin(r2);
                        const double den = d1x * d2y - d1y * d2x;
                        if (std::fabs(den) < 1e-9) continue;
                        const double t = ((bx - ax) * d2y - (by - ay) * d2x) / den;
                        const double px = ax + d1x * t, py = ay + d1y * t;
                        const double d = std::hypot(px - wx, py - wy);
                        if (d < best_d) {
                            best_d = d;
                            r = {true, gcm::to_lattice(vol, px, py), Intersection,
                                 0, 0, d};
                        }
                    }
                }
            }
        }
        if (r.valid) return r;
    }

    {
        double best_d = ap;
        for (const auto& t : tracked_) {
            double ox, oy;
            gcm::to_world(vol, t.point, ox, oy);
            for (double a : settings.track_angles) {
                const double rad = a * kPi / 180.0;
                const double ux = std::cos(rad), uy = std::sin(rad);
                const double proj = (wx - ox) * ux + (wy - oy) * uy;
                const double px = ox + ux * proj, py = oy + uy * proj;
                const double d = std::hypot(px - wx, py - wy);
                if (d < best_d) {
                    best_d = d;
                    r = {true, gcm::to_lattice(vol, px, py), Extension, 0, 0, d};
                }
            }
        }
    }

    return r;
}


void Engine::acquire_parallel(u32 shape_id) {
    // Shape ids are 1-based.
    if (shape_id == 0 || shape_id > shapes_.size()) return;
    if (shapes_[shape_id - 1].kind != ShapeKind::Segment) return;
    for (u32 id : parallel_)
        if (id == shape_id) return;
    parallel_.push_back(shape_id);
    if (parallel_.size() > 4) parallel_.erase(parallel_.begin());
}

Result Engine::snap_parallel(const gcm::WorkingVolume& vol, double wx, double wy,
                             Point anchor) const {
    Result r;
    if (!settings.wants(Parallel) || parallel_.empty()) return r;

    double ax, ay;
    gcm::to_world(vol, anchor, ax, ay);
    const double dx = wx - ax, dy = wy - ay;
    const double len = std::hypot(dx, dy);
    if (len < 1e-12) return r;

    const double aperture = settings.aperture_world();
    double best = aperture;

    for (u32 id : parallel_) {
        if (id == 0 || id > shapes_.size()) continue;
        const Shape& s = shapes_[id - 1];

        double sx0, sy0, sx1, sy1;
        gcm::to_world(vol, s.a, sx0, sy0);
        gcm::to_world(vol, s.b, sx1, sy1);
        const double rx = sx1 - sx0, ry = sy1 - sy0;
        const double rlen = std::hypot(rx, ry);
        if (rlen < 1e-12) continue;

        const double ux = rx / rlen, uy = ry / rlen;
        const double along = dx * ux + dy * uy;
        const double px = ax + ux * along, py = ay + uy * along;
        const double off = std::hypot(wx - px, wy - py);
        if (off >= best) continue;

        best = off;
        r.valid = true;
        r.kind = Parallel;
        r.point = gcm::to_lattice(vol, px, py);
        r.shape_a = id;
        r.distance = off;
    }
    return r;
}

}
