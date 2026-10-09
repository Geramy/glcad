#include "geometry/edit.hpp"
#include <cstdio>

#include <algorithm>
#include <cmath>

namespace gedit {

constexpr f64 kPi = 3.14159265358979323846;

namespace {

struct W { f64 x, y; };
W world(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    return {x, y};
}
Point lat(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }

}

Shape translate(const Shape& s, i64 dx, i64 dy) {
    Shape o = s;
    o.a.x += dx; o.a.y += dy;
    o.b.x += dx; o.b.y += dy;
    return o;
}

Shape rotate(const Shape& s, Point c, f64 r, const Volume& v) {
    const f64 ct = std::cos(r), st = std::sin(r);
    auto rot = [&](Point p) {
        const f64 dx = f64(p.x - c.x), dy = f64(p.y - c.y);
        return Point{c.x + i64(std::llround(dx * ct - dy * st)),
                     c.y + i64(std::llround(dx * st + dy * ct))};
    };
    Shape o = s;
    o.a = rot(s.a);
    if (s.is_linear()||s.is_elliptical()) o.b = rot(s.b);
    if (s.is_curve()) { o.start_angle += r; o.end_angle += r; }
    (void)v;
    return o;
}

Shape scale(const Shape& s, Point c, f64 f) {
    auto sc = [&](Point p) {
        return Point{c.x + i64(std::llround(f64(p.x - c.x) * f)),
                     c.y + i64(std::llround(f64(p.y - c.y) * f))};
    };
    Shape o = s;
    o.a = sc(s.a);
    if (s.is_linear()||s.is_elliptical()) o.b = sc(s.b);
    if(s.is_elliptical())o.radius=i64(std::llround(f64(s.radius)*std::abs(f)));
    if (s.is_curve()) {
        o.radius = i64(std::llround(f64(s.radius) * std::fabs(f)));
        if(f<0) {o.start_angle+=3.14159265358979323846;o.end_angle+=3.14159265358979323846;}
    }
    return o;
}

Shape mirror(const Shape& s, Point p0, Point p1, const Volume& v) {
    const W a = world(v, p0), b = world(v, p1);
    const f64 dx = b.x - a.x, dy = b.y - a.y;
    const f64 len2 = dx * dx + dy * dy;

    auto reflect = [&](Point p) {
        if (len2 < 1e-24) return p;
        const W q = world(v, p);
        const f64 t = ((q.x - a.x) * dx + (q.y - a.y) * dy) / len2;
        const f64 px = a.x + dx * t, py = a.y + dy * t;
        return lat(v, 2.0 * px - q.x, 2.0 * py - q.y);
    };

    Shape o = s;
    o.a = reflect(s.a);
    if (s.is_linear()||s.is_elliptical()) {
        o.b = reflect(s.b);
        if(s.is_elliptical()&&len2>=1e-24){o.start_angle=-s.end_angle;o.end_angle=-s.start_angle;if(o.region)o.region_reversed=!o.region_reversed;}
    } else if (s.is_curve()) {
        // Reflection reverses an arc's sweep: the mirrored start is the old end.
        const f64 axis = std::atan2(dy, dx);
        const f64 ns = 2.0 * axis - s.end_angle;
        const f64 ne = 2.0 * axis - s.start_angle;
        o.start_angle = ns;
        o.end_angle = ne;
        if (o.region) o.region_reversed = !o.region_reversed;
    }
    return o;
}

std::vector<Shape> translate_all(const std::vector<Shape>& in, i64 dx, i64 dy) {
    std::vector<Shape> out;
    out.reserve(in.size());
    for (const auto& s : in) out.push_back(translate(s, dx, dy));
    return out;
}

std::vector<Shape> mirror_all(const std::vector<Shape>& in, Point a, Point b,
                              const Volume& v) {
    std::vector<Shape> out;
    out.reserve(in.size());
    for (const auto& s : in) out.push_back(mirror(s, a, b, v));
    return out;
}

std::vector<Shape> array_rectangular(const std::vector<Shape>& in, int cols,
                                     int rows, f64 dx, f64 dy, const Volume& v) {
    std::vector<Shape> out;
    if (cols < 1 || rows < 1) return out;
    const i64 sx = i64(std::llround(dx * v.scale()));
    const i64 sy = i64(std::llround(dy * v.scale()));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            for (const auto& s : in)
                out.push_back(translate(s, sx * c, sy * r));
    return out;
}

std::vector<Shape> array_polar(const std::vector<Shape>& in, Point centre,
                               int count, f64 total, bool rotate_items,
                               const Volume& v) {
    std::vector<Shape> out;
    if (count < 1) return out;

    // A full circle repeats the first item at the end; a partial sweep does not.
    const bool full = std::fabs(std::fabs(total) - 2.0 * kPi) < 1e-9;
    const int divisor = full ? count : std::max(1, count - 1);

    for (int i = 0; i < count; ++i) {
        const f64 ang = total * (f64(i) / f64(divisor));
        for (const auto& s : in) {
            Shape r = rotate(s, centre, ang, v);
            if (!rotate_items && s.is_curve()) {
                r.start_angle = s.start_angle;
                r.end_angle = s.end_angle;
            }
            out.push_back(r);
        }
    }
    return out;
}

std::vector<Shape> array_path(const std::vector<Shape>& in,
                              const std::vector<Point>& path, int count,
                              const Volume& v) {
    std::vector<Shape> out;
    if (path.size() < 2 || count < 1) return out;

    std::vector<f64> cum{0.0};
    for (std::size_t i = 1; i < path.size(); ++i) {
        const W a = world(v, path[i - 1]), b = world(v, path[i]);
        cum.push_back(cum.back() + std::hypot(b.x - a.x, b.y - a.y));
    }
    const f64 total = cum.back();
    if (total < 1e-12) return out;

    for (int k = 0; k < count; ++k) {
        const f64 want = total * (count == 1 ? 0.0 : f64(k) / f64(count - 1));
        std::size_t seg = 1;
        while (seg + 1 < cum.size() && cum[seg] < want) ++seg;
        const f64 t = (cum[seg] - cum[seg - 1]) > 1e-12
                          ? (want - cum[seg - 1]) / (cum[seg] - cum[seg - 1])
                          : 0.0;
        const W a = world(v, path[seg - 1]), b = world(v, path[seg]);
        const W at{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        const W origin = world(v, path[0]);
        const i64 dx = i64(std::llround((at.x - origin.x) * v.scale()));
        const i64 dy = i64(std::llround((at.y - origin.y) * v.scale()));
        for (const auto& s : in) out.push_back(translate(s, dx, dy));
    }
    return out;
}


Window make_window(Point a, Point b) {
    Window w;
    w.lo = {std::min(a.x, b.x), std::min(a.y, b.y)};
    w.hi = {std::max(a.x, b.x), std::max(a.y, b.y)};
    return w;
}

bool Window::contains(Point p) const noexcept {
    return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
}

bool Window::contains(const Shape& s) const {
    if (s.kind == gsnap::ShapeKind::Segment)
        return contains(s.a) && contains(s.b);
    if (s.is_curve()) {
        return s.a.x - s.radius >= lo.x && s.a.x + s.radius <= hi.x &&
               s.a.y - s.radius >= lo.y && s.a.y + s.radius <= hi.y;
    }
    return contains(s.a);
}

bool Window::crosses(const Shape& s) const {
    if (contains(s)) return false;
    if (s.kind == gsnap::ShapeKind::Segment)
        return contains(s.a) || contains(s.b);
    if (s.is_curve()) {
        return !(s.a.x + s.radius < lo.x || s.a.x - s.radius > hi.x ||
                 s.a.y + s.radius < lo.y || s.a.y - s.radius > hi.y);
    }
    return contains(s.a);
}

StretchResult stretch(const std::vector<Shape>& in, const Window& w, i64 dx,
                      i64 dy) {
    StretchResult r;
    r.shapes.reserve(in.size());

    for (const Shape& s : in) {
        if (w.contains(s)) {
            r.shapes.push_back(translate(s, dx, dy));
            ++r.moved;
            continue;
        }
        if (!w.crosses(s)) {
            r.shapes.push_back(s);
            ++r.untouched;
            continue;
        }

        if (s.kind == gsnap::ShapeKind::Segment) {
            Shape o = s;
            bool changed = false;
            if (w.contains(s.a)) { o.a.x += dx; o.a.y += dy; changed = true; }
            if (w.contains(s.b)) { o.b.x += dx; o.b.y += dy; changed = true; }
            r.shapes.push_back(o);
            if (changed) ++r.stretched;
            else ++r.untouched;
            continue;
        }

        if (w.contains(s.a)) {
            r.shapes.push_back(translate(s, dx, dy));
            ++r.moved;
        } else {
            r.shapes.push_back(s);
            ++r.untouched;
        }
    }
    return r;
}

f64 length_of(const Shape& s, const Volume& v) {
    if (s.kind == gsnap::ShapeKind::Segment) {
        const W a = world(v, s.a), b = world(v, s.b);
        return std::hypot(b.x - a.x, b.y - a.y);
    }
    const f64 r = static_cast<f64>(s.radius) * v.inv_scale();
    if (s.kind == gsnap::ShapeKind::Circle) return 2.0 * kPi * r;
    if (s.kind == gsnap::ShapeKind::Arc) {
        f64 sweep = s.end_angle - s.start_angle;
        while (sweep < 0.0) sweep += 2.0 * kPi;
        return sweep * r;
    }
    return 0.0;
}

std::optional<Shape> lengthen(const Shape& s, LengthenMode mode, f64 value,
                              Point pick, const Volume& v) {
    const f64 current = length_of(s, v);
    if (current <= 0.0) return std::nullopt;

    f64 target = current;
    switch (mode) {
        case LengthenMode::Total:   target = value; break;
        case LengthenMode::Delta:   target = current + value; break;
        case LengthenMode::Percent: target = current * value * 0.01; break;
    }
    if (target <= 1e-12) return std::nullopt;

    if (s.kind == gsnap::ShapeKind::Segment) {
        const W a = world(v, s.a), b = world(v, s.b), p = world(v, pick);
        const f64 da = std::hypot(p.x - a.x, p.y - a.y);
        const f64 db = std::hypot(p.x - b.x, p.y - b.y);

        Shape o = s;
        if (db <= da) {
            const f64 ux = (b.x - a.x) / current, uy = (b.y - a.y) / current;
            o.b = lat(v, a.x + ux * target, a.y + uy * target);
        } else {
            const f64 ux = (a.x - b.x) / current, uy = (a.y - b.y) / current;
            o.a = lat(v, b.x + ux * target, b.y + uy * target);
        }
        return o;
    }

    if (s.kind == gsnap::ShapeKind::Arc) {
        const f64 r = static_cast<f64>(s.radius) * v.inv_scale();
        if (r <= 0.0) return std::nullopt;
        const f64 sweep = target / r;
        if (sweep >= 2.0 * kPi) return std::nullopt;

        const W c = world(v, s.a), p = world(v, pick);
        const f64 pa = std::atan2(p.y - c.y, p.x - c.x);
        auto delta = [&](f64 a) {
            f64 d = std::fabs(pa - a);
            while (d > kPi) d = 2.0 * kPi - d;
            return d;
        };

        Shape o = s;
        if (delta(s.end_angle) <= delta(s.start_angle))
            o.end_angle = s.start_angle + sweep;
        else
            o.start_angle = s.end_angle - sweep;
        return o;
    }

    return std::nullopt;
}

namespace {

bool param_of(const Shape& s, Point p, const Volume& v, f64& t) {
    if (s.kind == gsnap::ShapeKind::Segment) {
        t = parameter_on_segment(s, p);
        return t > -0.001 && t < 1.001;
    }
    if (!s.is_curve()) return false;
    const W c = world(v, s.a), w = world(v, p);
    t = std::atan2(w.y - c.y, w.x - c.x);
    return true;
}

f64 wrap_positive(f64 a) {
    while (a < 0.0) a += 2.0 * kPi;
    while (a >= 2.0 * kPi) a -= 2.0 * kPi;
    return a;
}

}

BreakResult break_between(const Shape& s, Point p0, Point p1, const Volume& v) {
    BreakResult r;

    if (s.kind == gsnap::ShapeKind::Segment) {
        f64 t0, t1;
        if (!param_of(s, p0, v, t0) || !param_of(s, p1, v, t1)) {
            r.reason = "both break points must lie on the object";
            return r;
        }
        if (t0 > t1) std::swap(t0, t1);
        if (t1 - t0 < 1e-12) {
            r.reason = "the two break points are the same";
            return r;
        }

        const W a = world(v, s.a), b = world(v, s.b);
        auto at = [&](f64 t) {
            return lat(v, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        };
        if (t0 > 1e-9) {
            Shape first = s;
            first.b = at(t0);
            r.pieces.push_back(first);
        }
        if (t1 < 1.0 - 1e-9) {
            Shape second = s;
            second.a = at(t1);
            r.pieces.push_back(second);
        }
        r.changed = true;
        return r;
    }

    if (s.kind == gsnap::ShapeKind::Circle) {
        f64 a0, a1;
        param_of(s, p0, v, a0);
        param_of(s, p1, v, a1);
        Shape arc = s;
        arc.kind = gsnap::ShapeKind::Arc;
        arc.start_angle = wrap_positive(a1);
        arc.end_angle = wrap_positive(a0);
        if (arc.end_angle <= arc.start_angle) arc.end_angle += 2.0 * kPi;
        if (arc.end_angle - arc.start_angle < 1e-9) {
            r.reason = "the two break points are the same";
            return r;
        }
        r.pieces.push_back(arc);
        r.changed = true;
        return r;
    }

    if (s.kind == gsnap::ShapeKind::Arc) {
        f64 a0, a1;
        param_of(s, p0, v, a0);
        param_of(s, p1, v, a1);
        const f64 start = s.start_angle;
        f64 end = s.end_angle;
        while (end < start) end += 2.0 * kPi;

        auto rel = [&](f64 a) {
            f64 d = a - start;
            while (d < 0.0) d += 2.0 * kPi;
            return d;
        };
        f64 r0 = rel(a0), r1 = rel(a1);
        if (r0 > r1) std::swap(r0, r1);
        const f64 span = end - start;
        if (r0 > span || r1 > span) {
            r.reason = "both break points must lie on the arc";
            return r;
        }

        if (r0 > 1e-9) {
            Shape first = s;
            first.end_angle = start + r0;
            r.pieces.push_back(first);
        }
        if (span - r1 > 1e-9) {
            Shape second = s;
            second.start_angle = start + r1;
            second.end_angle = end;
            r.pieces.push_back(second);
        }
        r.changed = true;
        return r;
    }

    r.reason = "that object cannot be broken";
    return r;
}

BreakResult break_at(const Shape& s, Point p, const Volume& v) {
    BreakResult r = break_between(s, p, p, v);
    if (r.changed) return r;

    if (s.kind == gsnap::ShapeKind::Segment) {
        f64 t;
        if (!param_of(s, p, v, t) || t < 1e-9 || t > 1.0 - 1e-9) {
            r.reason = "the break point must lie on the object, away from its ends";
            return r;
        }
        Shape first = s, second = s;
        first.b = p;
        second.a = p;
        r.pieces = {first, second};
        r.changed = true;
        r.reason = "";
        return r;
    }

    if (s.kind == gsnap::ShapeKind::Arc) {
        f64 a;
        param_of(s, p, v, a);
        const f64 start = s.start_angle;
        f64 end = s.end_angle;
        while (end < start) end += 2.0 * kPi;
        f64 rel = a - start;
        while (rel < 0.0) rel += 2.0 * kPi;
        if (rel < 1e-9 || rel > end - start - 1e-9) {
            r.reason = "the break point must lie on the arc, away from its ends";
            return r;
        }
        Shape first = s, second = s;
        first.end_angle = start + rel;
        second.start_angle = start + rel;
        second.end_angle = end;
        r.pieces = {first, second};
        r.changed = true;
        r.reason = "";
        return r;
    }

    if (s.kind == gsnap::ShapeKind::Circle) {
        f64 a;
        param_of(s, p, v, a);
        Shape arc = s;
        arc.kind = gsnap::ShapeKind::Arc;
        arc.start_angle = wrap_positive(a);
        arc.end_angle = arc.start_angle + 2.0 * kPi;
        r.pieces = {arc};
        r.changed = true;
        r.reason = "";
        return r;
    }

    return r;
}

namespace {

bool same_point(const Volume& v, Point a, Point b, f64 tol_world) {
    const W wa = world(v, a), wb = world(v, b);
    return std::hypot(wa.x - wb.x, wa.y - wb.y) <= tol_world;
}

bool collinear_segments(const Shape& a, const Shape& b, const Volume& v, f64 tol) {
    const W a0 = world(v, a.a), a1 = world(v, a.b);
    const W b0 = world(v, b.a), b1 = world(v, b.b);
    const f64 ax = a1.x - a0.x, ay = a1.y - a0.y;
    const f64 len = std::hypot(ax, ay);
    if (len < 1e-12) return false;

    auto off = [&](W p) {
        return std::fabs((p.x - a0.x) * ay - (p.y - a0.y) * ax) / len;
    };
    return off(b0) <= tol && off(b1) <= tol;
}

}

JoinResult join(const std::vector<Shape>& in, const Volume& v, f64 tol) {
    JoinResult r;
    std::vector<Shape> work = in;
    std::vector<bool> used(work.size(), false);

    for (std::size_t i = 0; i < work.size(); ++i) {
        if (used[i]) continue;
        Shape cur = work[i];
        bool grew = true;

        while (grew) {
            grew = false;
            for (std::size_t j = 0; j < work.size(); ++j) {
                if (used[j] || j == i) continue;
                const Shape& other = work[j];
                if (other.kind != cur.kind) continue;

                if (cur.kind == gsnap::ShapeKind::Segment) {
                    if (!collinear_segments(cur, other, v, tol)) continue;

                    if (same_point(v, cur.b, other.a, tol)) cur.b = other.b;
                    else if (same_point(v, cur.b, other.b, tol)) cur.b = other.a;
                    else if (same_point(v, cur.a, other.a, tol)) cur.a = other.b;
                    else if (same_point(v, cur.a, other.b, tol)) cur.a = other.a;
                    else continue;

                    used[j] = true;
                    ++r.joined;
                    grew = true;
                    continue;
                }

                if (cur.kind == gsnap::ShapeKind::Arc) {
                    if (cur.radius != other.radius) continue;
                    if (!same_point(v, cur.a, other.a, tol)) continue;

                    const f64 gap_end = std::fabs(wrap_positive(other.start_angle) -
                                                  wrap_positive(cur.end_angle));
                    const f64 gap_start = std::fabs(wrap_positive(cur.start_angle) -
                                                    wrap_positive(other.end_angle));
                    const f64 eps = 1e-6;
                    if (gap_end < eps || std::fabs(gap_end - 2.0 * kPi) < eps) {
                        f64 end = other.end_angle;
                        while (end < cur.start_angle) end += 2.0 * kPi;
                        cur.end_angle = end;
                    } else if (gap_start < eps ||
                               std::fabs(gap_start - 2.0 * kPi) < eps) {
                        cur.start_angle = other.start_angle;
                        while (cur.end_angle < cur.start_angle)
                            cur.end_angle += 2.0 * kPi;
                    } else {
                        continue;
                    }

                    used[j] = true;
                    ++r.joined;
                    grew = true;
                }
            }
        }

        used[i] = true;
        r.shapes.push_back(cur);
    }

    if (r.joined == 0) r.reason = "nothing was contiguous enough to join";
    return r;
}

std::vector<Shape> align(const std::vector<Shape>& in, Point src_a, Point src_b,
                         Point dst_a, Point dst_b, bool scale_to_fit,
                         const Volume& v) {
    const W sa = world(v, src_a), sb = world(v, src_b);
    const W da = world(v, dst_a), db = world(v, dst_b);

    const f64 slen = std::hypot(sb.x - sa.x, sb.y - sa.y);
    const f64 dlen = std::hypot(db.x - da.x, db.y - da.y);
    if (slen < 1e-12 || dlen < 1e-12) return in;

    const f64 angle = std::atan2(db.y - da.y, db.x - da.x) -
                      std::atan2(sb.y - sa.y, sb.x - sa.x);
    const f64 factor = scale_to_fit ? dlen / slen : 1.0;

    std::vector<Shape> out;
    out.reserve(in.size());
    for (const Shape& s : in) {
        Shape o = rotate(s, src_a, angle, v);
        if (scale_to_fit) o = scale(o, src_a, factor);
        o = translate(o, dst_a.x - src_a.x, dst_a.y - src_a.y);
        out.push_back(o);
    }
    return out;
}

std::vector<Point> intersections(const Shape& a, const Shape& b) {
    std::vector<Point> out;
    if (a.kind == gsnap::ShapeKind::Segment && b.kind == gsnap::ShapeKind::Segment) {
        if (auto x = gsnap::line_line_intersection(a.a, a.b, b.a, b.b, false))
            out.push_back(*x);
    } else if (a.kind == gsnap::ShapeKind::Segment && b.is_curve()) {
        out = gsnap::line_circle_intersection(a.a, a.b, b.a, b.radius);
    } else if (b.kind == gsnap::ShapeKind::Segment && a.is_curve()) {
        out = gsnap::line_circle_intersection(b.a, b.b, a.a, a.radius);
    }
    return out;
}

f64 parameter_on_segment(const Shape& s, Point p) {
    const f64 dx = f64(s.b.x - s.a.x), dy = f64(s.b.y - s.a.y);
    const f64 len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) return 0.0;
    return (f64(p.x - s.a.x) * dx + f64(p.y - s.a.y) * dy) / len2;
}

bool point_on_shape(const Shape& s, Point p, const Volume& v, f64 tol) {
    if (s.kind == gsnap::ShapeKind::Segment) {
        const Point c = gsnap::closest_point_on_segment(s.a, s.b, world(v, p).x,
                                                        world(v, p).y, v);
        const W q = world(v, c), r = world(v, p);
        return std::hypot(q.x - r.x, q.y - r.y) <= tol;
    }
    if (s.is_curve()) {
        const W c = world(v, s.a), q = world(v, p);
        const f64 rw = f64(s.radius) * v.inv_scale();
        return std::fabs(std::hypot(q.x - c.x, q.y - c.y) - rw) <= tol;
    }
    return false;
}

std::optional<Shape> offset(const Shape& s, f64 distance, Point through,
                            const Volume& v) {
    if (distance <= 0.0) return std::nullopt;
    const i64 d = i64(std::llround(distance * v.scale()));

    if (s.kind == gsnap::ShapeKind::Segment) {
        const W a = world(v, s.a), b = world(v, s.b), t = world(v, through);
        const f64 dx = b.x - a.x, dy = b.y - a.y;
        const f64 len = std::hypot(dx, dy);
        if (len < 1e-12) return std::nullopt;

        const f64 cross = dx * (t.y - a.y) - dy * (t.x - a.x);
        const f64 sign = cross >= 0.0 ? 1.0 : -1.0;
        const f64 nx = -dy / len * distance * sign;
        const f64 ny = dx / len * distance * sign;

        Shape o = s;
        o.a = lat(v, a.x + nx, a.y + ny);
        o.b = lat(v, b.x + nx, b.y + ny);
        return o;
    }

    if (s.is_curve()) {
        const W c = world(v, s.a), t = world(v, through);
        const f64 rw = f64(s.radius) * v.inv_scale();
        const f64 dist_to_pick = std::hypot(t.x - c.x, t.y - c.y);

        Shape o = s;
        o.radius = dist_to_pick > rw ? s.radius + d : s.radius - d;
        if (o.radius <= 0) return std::nullopt;
        return o;
    }
    return std::nullopt;
}

TrimResult trim(const Shape& target, const std::vector<Shape>& cutters,
                Point pick, const Volume& v) {
    TrimResult r;
    if (target.kind != gsnap::ShapeKind::Segment) {
        r.reason = "trim currently handles line segments";
        return r;
    }

    std::vector<f64> cuts;
    for (const auto& c : cutters) {
        if (&c == &target) continue;
        for (Point x : intersections(target, c)) {
            const f64 t = parameter_on_segment(target, x);
            if (t > 1e-9 && t < 1.0 - 1e-9) cuts.push_back(t);
        }
    }
    if (cuts.empty()) {
        r.reason = "nothing crosses this object";
        return r;
    }
    std::sort(cuts.begin(), cuts.end());

    const f64 tp = std::clamp(parameter_on_segment(target, pick), 0.0, 1.0);
    f64 lo = 0.0, hi = 1.0;
    for (f64 c : cuts) {
        if (c <= tp) lo = c;
        else { hi = c; break; }
    }

    auto at = [&](f64 t) {
        return Point{target.a.x + i64(std::llround(f64(target.b.x - target.a.x) * t)),
                     target.a.y + i64(std::llround(f64(target.b.y - target.a.y) * t))};
    };

    if (lo > 1e-9) {
        Shape piece = target;
        piece.b = at(lo);
        r.pieces.push_back(piece);
    }
    if (hi < 1.0 - 1e-9) {
        Shape piece = target;
        piece.a = at(hi);
        r.pieces.push_back(piece);
    }
    r.changed = true;
    return r;
}

TrimResult extend(const Shape& target, const std::vector<Shape>& boundaries,
                  Point pick, const Volume& v) {
    TrimResult r;
    if (target.kind != gsnap::ShapeKind::Segment) {
        r.reason = "extend currently handles line segments";
        return r;
    }

    const f64 tp = parameter_on_segment(target, pick);
    const bool from_b = tp > 0.5;

    f64 best = from_b ? 1e300 : -1e300;
    bool found = false;
    for (const auto& b : boundaries) {
        if (&b == &target) continue;
        std::optional<Point> x;
        if (b.kind == gsnap::ShapeKind::Segment)
            x = gsnap::line_line_intersection(target.a, target.b, b.a, b.b, true);
        if (!x) {
            for (Point p : intersections(target, b)) x = p;
        }
        if (!x) continue;

        const f64 t = parameter_on_segment(target, *x);
        if (from_b && t > 1.0 + 1e-9 && t < best) { best = t; found = true; }
        if (!from_b && t < -1e-9 && t > best) { best = t; found = true; }
    }
    if (!found) {
        r.reason = "no boundary lies beyond that end";
        return r;
    }

    auto at = [&](f64 t) {
        return Point{target.a.x + i64(std::llround(f64(target.b.x - target.a.x) * t)),
                     target.a.y + i64(std::llround(f64(target.b.y - target.a.y) * t))};
    };
    Shape o = target;
    if (from_b) o.b = at(best);
    else o.a = at(best);
    r.pieces.push_back(o);
    r.changed = true;
    (void)v;
    return r;
}

namespace {

struct Corner {
    bool ok{false};
    W    at{};
    W    ua{}, ub{};
    f64  angle{0.0};
};

Corner corner_of(const Shape& a, const Shape& b, Point pick_a, Point pick_b,
                 const Volume& v) {
    Corner c;
    const auto x = gsnap::line_line_intersection(a.a, a.b, b.a, b.b, true);
    if (!x) return c;

    c.at = world(v, *x);
    const W pa = world(v, pick_a), pb = world(v, pick_b);

    auto unit = [&](W p) {
        const f64 dx = p.x - c.at.x, dy = p.y - c.at.y;
        const f64 len = std::hypot(dx, dy);
        return len < 1e-12 ? W{0, 0} : W{dx / len, dy / len};
    };
    c.ua = unit(pa);
    c.ub = unit(pb);
    if ((c.ua.x == 0 && c.ua.y == 0) || (c.ub.x == 0 && c.ub.y == 0)) return c;

    const f64 dot = std::clamp(c.ua.x * c.ub.x + c.ua.y * c.ub.y, -1.0, 1.0);
    c.angle = std::acos(dot);
    if (c.angle < 1e-6 || c.angle > kPi - 1e-6) return c;
    c.ok = true;
    return c;
}

Shape trim_to(const Shape& s, W corner, W p, const Volume& v) {
    Shape o = s;
    const W a = world(v, s.a), b = world(v, s.b);
    const f64 da = std::hypot(a.x - corner.x, a.y - corner.y);
    const f64 db = std::hypot(b.x - corner.x, b.y - corner.y);
    if (da <= db) o.a = lat(v, p.x, p.y);
    else o.b = lat(v, p.x, p.y);
    return o;
}

}

CornerResult fillet(const Shape& a, const Shape& b, f64 radius, Point pick_a,
                    Point pick_b, const Volume& v) {
    CornerResult r;
    if (a.kind != gsnap::ShapeKind::Segment || b.kind != gsnap::ShapeKind::Segment) {
        r.reason = "fillet currently handles two line segments";
        return r;
    }
    if (radius <= 0.0) { r.reason = "radius must be positive"; return r; }

    const Corner c = corner_of(a, b, pick_a, pick_b, v);
    if (!c.ok) { r.reason = "the lines are parallel or collinear"; return r; }

    // Tangent distance from the corner: r / tan(theta/2).
    const f64 half = c.angle * 0.5;
    const f64 tdist = radius / std::tan(half);

    auto length_from_corner = [&](const Shape& s) {
        const W p = world(v, s.a), q = world(v, s.b);
        return std::max(std::hypot(p.x - c.at.x, p.y - c.at.y),
                        std::hypot(q.x - c.at.x, q.y - c.at.y));
    };
    if (tdist > length_from_corner(a) + 1e-9 ||
        tdist > length_from_corner(b) + 1e-9) {
        r.reason = "radius too large for these segments";
        return r;
    }

    const W ta{c.at.x + c.ua.x * tdist, c.at.y + c.ua.y * tdist};
    const W tb{c.at.x + c.ub.x * tdist, c.at.y + c.ub.y * tdist};

    // Arc centre: along the angle bisector, r / sin(theta/2) from the corner.
    f64 bx = c.ua.x + c.ub.x, by = c.ua.y + c.ub.y;
    const f64 blen = std::hypot(bx, by);
    if (blen < 1e-12) { r.reason = "degenerate corner"; return r; }
    bx /= blen; by /= blen;
    const f64 cdist = radius / std::sin(half);
    const W centre{c.at.x + bx * cdist, c.at.y + by * cdist};

    Shape arc;
    arc.kind = gsnap::ShapeKind::Arc;
    arc.a = lat(v, centre.x, centre.y);
    arc.radius = i64(std::llround(radius * v.scale()));
    arc.start_angle = std::atan2(ta.y - centre.y, ta.x - centre.x);
    arc.end_angle = std::atan2(tb.y - centre.y, tb.x - centre.x);

    // Swap endpoints rather than store a negative sweep: arcs are read CCW.
    f64 sweep = arc.end_angle - arc.start_angle;
    while (sweep <= -kPi) sweep += 2.0 * kPi;
    while (sweep > kPi) sweep -= 2.0 * kPi;
    if (sweep < 0.0) {
        std::swap(arc.start_angle, arc.end_angle);
        sweep = -sweep;
    }
    arc.end_angle = arc.start_angle + sweep;

    r.a = trim_to(a, c.at, ta, v);
    r.b = trim_to(b, c.at, tb, v);
    r.connector = arc;
    r.ok = true;
    return r;
}

CornerResult chamfer(const Shape& a, const Shape& b, f64 da, f64 db,
                     Point pick_a, Point pick_b, const Volume& v) {
    CornerResult r;
    if (a.kind != gsnap::ShapeKind::Segment || b.kind != gsnap::ShapeKind::Segment) {
        r.reason = "chamfer currently handles two line segments";
        return r;
    }
    if (da <= 0.0 || db <= 0.0) { r.reason = "distances must be positive"; return r; }

    const Corner c = corner_of(a, b, pick_a, pick_b, v);
    if (!c.ok) { r.reason = "the lines are parallel or collinear"; return r; }

    const W ta{c.at.x + c.ua.x * da, c.at.y + c.ua.y * da};
    const W tb{c.at.x + c.ub.x * db, c.at.y + c.ub.y * db};

    Shape link;
    link.kind = gsnap::ShapeKind::Segment;
    link.a = lat(v, ta.x, ta.y);
    link.b = lat(v, tb.x, tb.y);

    r.a = trim_to(a, c.at, ta, v);
    r.b = trim_to(b, c.at, tb, v);
    r.connector = link;
    r.ok = true;
    return r;
}


namespace {

struct Circle { W c; f64 r; };

std::vector<Circle> tangent_circles(const Shape& a, const Shape& b, f64 r,
                                    const Volume& v) {
    std::vector<Circle> out;

    // Slack is a few lattice units, not an absolute epsilon.
    const f64 tol = std::max(1e-12, v.inv_scale() * 8.0);

    auto line_offsets = [&](const Shape& s, std::vector<std::pair<W, W>>& lines) {
        const W p0 = world(v, s.a), p1 = world(v, s.b);
        const f64 dx = p1.x - p0.x, dy = p1.y - p0.y;
        const f64 len = std::hypot(dx, dy);
        if (len < 1e-12) return;
        const W u{dx / len, dy / len};
        const W n{-u.y, u.x};
        for (int sign : {1, -1})
            lines.push_back({{p0.x + n.x * r * sign, p0.y + n.y * r * sign}, u});
    };

    const bool a_line = a.kind == gsnap::ShapeKind::Segment;
    const bool b_line = b.kind == gsnap::ShapeKind::Segment;

    if (a_line && b_line) {
        std::vector<std::pair<W, W>> la, lb;
        line_offsets(a, la);
        line_offsets(b, lb);
        for (const auto& [pa, ua] : la)
            for (const auto& [pb, ub] : lb) {
                const f64 den = ua.x * ub.y - ua.y * ub.x;
                if (std::fabs(den) < 1e-12) continue;
                const f64 t = ((pb.x - pa.x) * ub.y - (pb.y - pa.y) * ub.x) / den;
                out.push_back({{pa.x + ua.x * t, pa.y + ua.y * t}, r});
            }
        return out;
    }

    auto curve_circles = [&](const Shape& s, std::vector<Circle>& cs) {
        const W c = world(v, s.a);
        const f64 R = static_cast<f64>(s.radius) * v.inv_scale();
        cs.push_back({c, R + r});
        if (R > r + 1e-12) cs.push_back({c, R - r});
    };

    if (a_line != b_line) {
        const Shape& line = a_line ? a : b;
        const Shape& curve = a_line ? b : a;
        std::vector<std::pair<W, W>> lines;
        std::vector<Circle> circles;
        line_offsets(line, lines);
        curve_circles(curve, circles);

        for (const auto& [p, u] : lines)
            for (const Circle& cc : circles) {
                const f64 fx = p.x - cc.c.x, fy = p.y - cc.c.y;
                const f64 bq = 2.0 * (fx * u.x + fy * u.y);
                const f64 cq = fx * fx + fy * fy - cc.r * cc.r;
                // Lattice-unit slack: a fixed 1e-9 rejects exact tangencies.
                const f64 disc = bq * bq - 4.0 * cq;
                if (disc < -tol) continue;
                const f64 sq = std::sqrt(std::max(0.0, disc));
                for (f64 t : {(-bq - sq) * 0.5, (-bq + sq) * 0.5})
                    out.push_back({{p.x + u.x * t, p.y + u.y * t}, r});
            }
        return out;
    }

    std::vector<Circle> ca, cb;
    curve_circles(a, ca);
    curve_circles(b, cb);
    for (const Circle& x : ca)
        for (const Circle& y : cb) {
            const f64 dx = y.c.x - x.c.x, dy = y.c.y - x.c.y;
            const f64 d = std::hypot(dx, dy);
            if (d < 1e-12 || d > x.r + y.r + tol ||
                d < std::fabs(x.r - y.r) - tol)
                continue;
            const f64 t = (d * d + x.r * x.r - y.r * y.r) / (2.0 * d);
            const f64 h = std::sqrt(std::max(0.0, x.r * x.r - t * t));
            const f64 ux = dx / d, uy = dy / d;
            const f64 mx = x.c.x + ux * t, my = x.c.y + uy * t;
            out.push_back({{mx - uy * h, my + ux * h}, r});
            out.push_back({{mx + uy * h, my - ux * h}, r});
        }
    return out;
}

bool tangent_point(const Shape& s, W centre, const Volume& v, W& out) {
    if (s.kind == gsnap::ShapeKind::Segment) {
        const W p0 = world(v, s.a), p1 = world(v, s.b);
        const f64 dx = p1.x - p0.x, dy = p1.y - p0.y;
        const f64 len2 = dx * dx + dy * dy;
        if (len2 < 1e-24) return false;
        // Unclamped: a fillet legitimately extends a line to reach its corner.
        const f64 t = ((centre.x - p0.x) * dx + (centre.y - p0.y) * dy) / len2;
        out = {p0.x + dx * t, p0.y + dy * t};
        return true;
    }
    if (!s.is_curve()) return false;
    const W c = world(v, s.a);
    const f64 R = static_cast<f64>(s.radius) * v.inv_scale();
    const f64 dx = centre.x - c.x, dy = centre.y - c.y;
    const f64 d = std::hypot(dx, dy);
    if (d < 1e-12) return false;
    out = {c.x + dx / d * R, c.y + dy / d * R};
    return true;
}

Shape trim_toward(const Shape& s, W p, W pick, const Volume& v) {
    Shape o = s;
    if (s.kind == gsnap::ShapeKind::Segment) {
        const W a = world(v, s.a), b = world(v, s.b);
        const f64 da = std::hypot(pick.x - a.x, pick.y - a.y);
        const f64 db = std::hypot(pick.x - b.x, pick.y - b.y);
        if (da <= db) o.b = lat(v, p.x, p.y);
        else o.a = lat(v, p.x, p.y);
        return o;
    }
    if (s.is_curve()) {
        const W c = world(v, s.a);
        const f64 cut = std::atan2(p.y - c.y, p.x - c.x);
        const f64 keep = std::atan2(pick.y - c.y, pick.x - c.x);

        if (s.kind == gsnap::ShapeKind::Circle) {
            o.kind = gsnap::ShapeKind::Arc;
            o.start_angle = cut;
            o.end_angle = cut + 2.0 * kPi;
            return o;
        }

        auto rel = [&](f64 ang) {
            f64 d = ang - s.start_angle;
            while (d < 0.0) d += 2.0 * kPi;
            return d;
        };
        if (rel(keep) < rel(cut)) o.end_angle = s.start_angle + rel(cut);
        else o.start_angle = s.start_angle + rel(cut);
        return o;
    }
    return o;
}

}

CornerResult fillet_any(const Shape& a, const Shape& b, f64 radius, Point pick_a,
                        Point pick_b, const Volume& v) {
    CornerResult r;
    if (a.kind == gsnap::ShapeKind::Segment && b.kind == gsnap::ShapeKind::Segment)
        return fillet(a, b, radius, pick_a, pick_b, v);

    if (radius <= 0.0) {
        r.reason = "a fillet needs a positive radius";
        return r;
    }
    if ((!a.is_curve() && a.kind != gsnap::ShapeKind::Segment) ||
        (!b.is_curve() && b.kind != gsnap::ShapeKind::Segment)) {
        r.reason = "fillet handles lines, arcs and circles";
        return r;
    }

    const W pa = world(v, pick_a), pb = world(v, pick_b);
    const std::vector<Circle> candidates = tangent_circles(a, b, radius, v);
    if (candidates.empty()) {
        r.reason = "no circle of that radius is tangent to both";
        return r;
    }

    f64 best = 1e300;
    Circle chosen{};
    W ta{}, tb{};
    for (const Circle& c : candidates) {
        W qa, qb;
        if (!tangent_point(a, c.c, v, qa)) continue;
        if (!tangent_point(b, c.c, v, qb)) continue;
        const f64 score = std::hypot(qa.x - pa.x, qa.y - pa.y) +
                          std::hypot(qb.x - pb.x, qb.y - pb.y);
        if (score < best) {
            best = score;
            chosen = c;
            ta = qa;
            tb = qb;
        }
    }
    if (best > 1e299) {
        r.reason = "could not find the tangent points";
        return r;
    }

    r.a = trim_toward(a, ta, pa, v);
    r.b = trim_toward(b, tb, pb, v);

    gsnap::Shape arc;
    arc.kind = gsnap::ShapeKind::Arc;
    arc.a = lat(v, chosen.c.x, chosen.c.y);
    arc.radius = static_cast<i64>(std::llround(radius * v.scale()));
    f64 s0 = std::atan2(ta.y - chosen.c.y, ta.x - chosen.c.x);
    f64 s1 = std::atan2(tb.y - chosen.c.y, tb.x - chosen.c.x);
    f64 sweep = s1 - s0;
    while (sweep <= -kPi) sweep += 2.0 * kPi;
    while (sweep > kPi) sweep -= 2.0 * kPi;
    if (sweep < 0.0) {
        std::swap(s0, s1);
        sweep = -sweep;
    }
    arc.start_angle = s0;
    arc.end_angle = s0 + sweep;
    r.connector = arc;

    r.ok = true;
    return r;
}

CornerResult chamfer_any(const Shape& a, const Shape& b, f64 dist_a, f64 dist_b,
                         Point pick_a, Point pick_b, const Volume& v) {
    if (a.kind == gsnap::ShapeKind::Segment && b.kind == gsnap::ShapeKind::Segment)
        return chamfer(a, b, dist_a, dist_b, pick_a, pick_b, v);

    CornerResult r;
    if (dist_a <= 0.0 || dist_b <= 0.0) {
        r.reason = "a chamfer needs positive distances";
        return r;
    }

    const std::vector<Point> hits = intersections(a, b);
    if (hits.empty()) {
        r.reason = "those objects do not meet, so there is no corner to cut";
        return r;
    }

    const W pa = world(v, pick_a), pb = world(v, pick_b);
    W corner = world(v, hits[0]);
    f64 best = 1e300;
    for (const Point& h : hits) {
        const W w = world(v, h);
        const f64 d = std::hypot(w.x - pa.x, w.y - pa.y) +
                      std::hypot(w.x - pb.x, w.y - pb.y);
        if (d < best) {
            best = d;
            corner = w;
        }
    }

    auto step_along = [&](const Shape& s, f64 dist, W pick, W& out) {
        if (s.kind == gsnap::ShapeKind::Segment) {
            const W p0 = world(v, s.a), p1 = world(v, s.b);
            f64 dx = p1.x - p0.x, dy = p1.y - p0.y;
            const f64 len = std::hypot(dx, dy);
            if (len < 1e-12) return false;
            dx /= len;
            dy /= len;
            if ((pick.x - corner.x) * dx + (pick.y - corner.y) * dy < 0.0) {
                dx = -dx;
                dy = -dy;
            }
            out = {corner.x + dx * dist, corner.y + dy * dist};
            return true;
        }
        if (!s.is_curve()) return false;
        const W c = world(v, s.a);
        const f64 R = static_cast<f64>(s.radius) * v.inv_scale();
        if (R < 1e-12) return false;
        // Chamfer distance on a curve is arc length, not chord.
        const f64 a0 = std::atan2(corner.y - c.y, corner.x - c.x);
        const f64 ap = std::atan2(pick.y - c.y, pick.x - c.x);
        f64 dir = ap - a0;
        while (dir <= -kPi) dir += 2.0 * kPi;
        while (dir > kPi) dir -= 2.0 * kPi;
        const f64 t = a0 + (dir >= 0.0 ? 1.0 : -1.0) * (dist / R);
        out = {c.x + R * std::cos(t), c.y + R * std::sin(t)};
        return true;
    };

    W qa, qb;
    if (!step_along(a, dist_a, pa, qa) || !step_along(b, dist_b, pb, qb)) {
        r.reason = "could not step along one of the objects";
        return r;
    }

    r.a = trim_toward(a, qa, pa, v);
    r.b = trim_toward(b, qb, pb, v);

    gsnap::Shape link;
    link.kind = gsnap::ShapeKind::Segment;
    link.a = lat(v, qa.x, qa.y);
    link.b = lat(v, qb.x, qb.y);
    r.connector = link;
    r.ok = true;
    return r;
}

namespace {

PolylineCornerResult run_corners(const std::vector<Shape>& run, f64 size,
                                 bool round, const Volume& v) {
    PolylineCornerResult out;
    if (run.size() < 2) {
        out.shapes = run;
        out.reason = "a run needs at least two segments to have a corner";
        return out;
    }
    if (size <= 0.0) {
        out.shapes = run;
        out.reason = "the radius or distance must be positive";
        return out;
    }

    std::vector<Shape> work = run;
    std::vector<Shape> links;

    for (std::size_t i = 0; i + 1 < work.size(); ++i) {
        const Shape& a = work[i];
        const Shape& b = work[i + 1];
        if (a.kind != gsnap::ShapeKind::Segment ||
            b.kind != gsnap::ShapeKind::Segment) {
            ++out.skipped;
            continue;
        }

        const W wa0 = world(v, a.a);
        const W wb1 = world(v, b.b);
        const Point pick_a = lat(v, wa0.x, wa0.y);
        const Point pick_b = lat(v, wb1.x, wb1.y);

        const CornerResult c =
            round ? fillet(a, b, size, pick_a, pick_b, v)
                  : chamfer(a, b, size, size, pick_a, pick_b, v);
        if (!c.ok) {
            ++out.skipped;
            continue;
        }

        work[i] = c.a;
        work[i + 1] = c.b;
        links.push_back(c.connector);
        ++out.rounded;
    }

    out.shapes.reserve(work.size() + links.size());
    std::size_t link = 0;
    for (std::size_t i = 0; i < work.size(); ++i) {
        out.shapes.push_back(work[i]);
        if (i + 1 < work.size() && link < links.size())
            out.shapes.push_back(links[link++]);
    }
    return out;
}

}

PolylineCornerResult fillet_polyline(const std::vector<Shape>& run, f64 radius,
                                     const Volume& v) {
    return run_corners(run, radius, true, v);
}

PolylineCornerResult chamfer_polyline(const std::vector<Shape>& run, f64 distance,
                                      const Volume& v) {
    return run_corners(run, distance, false, v);
}

const char* grip_kind_name(GripKind k) noexcept {
    switch (k) {
        case GripKind::Endpoint: return "endpoint";
        case GripKind::Midpoint: return "midpoint";
        case GripKind::Centre:   return "centre";
        case GripKind::Quadrant: return "quadrant";
        case GripKind::Vertex:   return "vertex";
        case GripKind::Stretch:  return "stretch";
    }
    return "?";
}

std::vector<Grip> grips_of(const Shape& s, const Volume& v) {
    std::vector<Grip> out;

    switch (s.kind) {
        case gsnap::ShapeKind::Segment:
            out.push_back({s.a, GripKind::Endpoint, s.id, 0, false});
            out.push_back({Point{(s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2},
                           GripKind::Midpoint, s.id, 1, false});
            out.push_back({s.b, GripKind::Endpoint, s.id, 2, false});
            break;

        case gsnap::ShapeKind::Circle: {
            out.push_back({s.a, GripKind::Centre, s.id, 0, false});
            const i64 r = s.radius;
            out.push_back({{s.a.x + r, s.a.y}, GripKind::Quadrant, s.id, 1, false});
            out.push_back({{s.a.x, s.a.y + r}, GripKind::Quadrant, s.id, 2, false});
            out.push_back({{s.a.x - r, s.a.y}, GripKind::Quadrant, s.id, 3, false});
            out.push_back({{s.a.x, s.a.y - r}, GripKind::Quadrant, s.id, 4, false});
            break;
        }

        case gsnap::ShapeKind::Arc: {
            const W c = world(v, s.a);
            const f64 r = static_cast<f64>(s.radius) * v.inv_scale();
            out.push_back({s.a, GripKind::Centre, s.id, 0, false});
            out.push_back({lat(v, c.x + r * std::cos(s.start_angle),
                               c.y + r * std::sin(s.start_angle)),
                           GripKind::Endpoint, s.id, 1, false});
            f64 mid = (s.start_angle + s.end_angle) * 0.5;
            if (s.end_angle < s.start_angle) mid += kPi;
            out.push_back({lat(v, c.x + r * std::cos(mid), c.y + r * std::sin(mid)),
                           GripKind::Quadrant, s.id, 2, false});
            out.push_back({lat(v, c.x + r * std::cos(s.end_angle),
                               c.y + r * std::sin(s.end_angle)),
                           GripKind::Endpoint, s.id, 3, false});
            break;
        }

        default:
            out.push_back({s.a, GripKind::Vertex, s.id, 0, false});
            break;
    }
    return out;
}

std::vector<Grip> grips_of(const std::vector<Shape>& shapes, const Volume& v) {
    std::vector<Grip> out;
    for (u32 i = 0; i < shapes.size(); ++i) {
        std::vector<Grip> g = grips_of(shapes[i], v);
        for (Grip& one : g) {
            one.shape = i;
            out.push_back(one);
        }
    }
    return out;
}

Grip pick_grip(const std::vector<Grip>& grips, Point at, const Volume& v,
               f64 aperture_world) {
    Grip none;
    none.shape = kNoGrip;

    const W p = world(v, at);
    f64 best = aperture_world;
    for (const Grip& g : grips) {
        const W q = world(v, g.at);
        const f64 d = std::hypot(q.x - p.x, q.y - p.y);
        if (d >= best) continue;
        best = d;
        none = g;
        none.hot = true;
    }
    return none;
}

std::optional<Shape> drag_grip(const Shape& s, const Grip& g, Point to,
                               const Volume& v) {
    Shape o = s;

    switch (g.kind) {
        case GripKind::Endpoint:
            if (s.kind == gsnap::ShapeKind::Segment) {
                if (g.index == 0) o.a = to;
                else o.b = to;
                return o;
            }
            if (s.kind == gsnap::ShapeKind::Arc) {
                const W c = world(v, s.a), p = world(v, to);
                const f64 ang = std::atan2(p.y - c.y, p.x - c.x);
                if (g.index == 1) o.start_angle = ang;
                else o.end_angle = ang;
                return o;
            }
            return std::nullopt;

        case GripKind::Midpoint: {
            const i64 dx = to.x - (s.a.x + s.b.x) / 2;
            const i64 dy = to.y - (s.a.y + s.b.y) / 2;
            return translate(s, dx, dy);
        }

        case GripKind::Centre: {
            const i64 dx = to.x - s.a.x, dy = to.y - s.a.y;
            return translate(s, dx, dy);
        }

        case GripKind::Quadrant: {
            if (!s.is_curve()) return std::nullopt;
            const W c = world(v, s.a), p = world(v, to);
            const f64 r = std::hypot(p.x - c.x, p.y - c.y);
            if (r < 1e-12) return std::nullopt;
            o.radius = static_cast<i64>(std::llround(r * v.scale()));
            return o;
        }

        case GripKind::Vertex:
        case GripKind::Stretch:
            o.a = to;
            return o;
    }
    return std::nullopt;
}


const char* grip_op_name(GripOp o) noexcept {
    switch (o) {
        case GripOp::Stretch:       return "Stretch";
        case GripOp::Move:          return "Move";
        case GripOp::Lengthen:      return "Lengthen";
        case GripOp::ConvertToArc:  return "Convert to Arc";
        case GripOp::ConvertToLine: return "Convert to Line";
        case GripOp::Radius:        return "Radius";
        case GripOp::Diameter:      return "Diameter";
        case GripOp::Rotate:        return "Rotate";
        case GripOp::AddVertex:     return "Add Vertex";
        case GripOp::RemoveVertex:  return "Remove Vertex";
    }
    return "";
}

std::vector<GripOption> grip_menu(const Shape& s, const Grip& g) {
    std::vector<GripOption> out;
    auto add = [&](GripOp op, bool needs_point = true) {
        out.push_back({op, grip_op_name(op), needs_point});
    };

    switch (g.kind) {
        case GripKind::Endpoint:
            if (s.kind == gsnap::ShapeKind::Segment) {
                add(GripOp::Stretch);
                add(GripOp::Lengthen);
                add(GripOp::ConvertToArc);
                add(GripOp::AddVertex, false);
            } else if (s.kind == gsnap::ShapeKind::Arc) {
                add(GripOp::Stretch);
                add(GripOp::Rotate);
                add(GripOp::ConvertToLine, false);
            } else {
                add(GripOp::Stretch);
            }
            break;

        case GripKind::Midpoint:
            add(GripOp::Move);
            if (s.kind == gsnap::ShapeKind::Segment) add(GripOp::ConvertToArc);
            break;

        case GripKind::Centre:
            add(GripOp::Move);
            if (s.is_curve()) {
                add(GripOp::Radius);
                add(GripOp::Diameter);
            }
            break;

        case GripKind::Quadrant:
            add(GripOp::Radius);
            add(GripOp::Diameter);
            add(GripOp::Move);
            break;

        case GripKind::Vertex:
            add(GripOp::Stretch);
            add(GripOp::RemoveVertex, false);
            break;

        case GripKind::Stretch:
            add(GripOp::Stretch);
            break;
    }
    return out;
}

std::optional<Shape> apply_grip_op(const Shape& s, const Grip& g, GripOp op,
                                   Point to, const Volume& v) {
    bool offered = false;
    for (const GripOption& o : grip_menu(s, g))
        if (o.op == op) offered = true;
    if (!offered) return std::nullopt;

    Shape o = s;
    switch (op) {
        case GripOp::Stretch:
            return drag_grip(s, g, to, v);

        case GripOp::Move: {
            return translate(s, to.x - g.at.x, to.y - g.at.y);
        }

        case GripOp::Lengthen: {
            if (!s.is_linear()) return std::nullopt;
            const W a = world(v, s.a), b = world(v, s.b), p = world(v, to);
            const f64 dx = b.x - a.x, dy = b.y - a.y;
            const f64 len = std::hypot(dx, dy);
            if (len < 1e-12) return std::nullopt;
            const f64 ux = dx / len, uy = dy / len;
            const W anchor = (g.index == 0) ? b : a;
            const f64 t = (p.x - anchor.x) * ux + (p.y - anchor.y) * uy;
            const Point moved = lat(v, anchor.x + ux * t, anchor.y + uy * t);
            if (g.index == 0) o.a = moved; else o.b = moved;
            if (o.a.x == o.b.x && o.a.y == o.b.y) return std::nullopt;
            return o;
        }

        case GripOp::ConvertToArc: {
            const W a = world(v, s.a), b = world(v, s.b), p = world(v, to);
            const f64 d = 2.0 * (a.x * (b.y - p.y) + b.x * (p.y - a.y) +
                                 p.x * (a.y - b.y));
            if (std::abs(d) < 1e-12) return std::nullopt;
            const f64 aa = a.x * a.x + a.y * a.y;
            const f64 bb = b.x * b.x + b.y * b.y;
            const f64 pp = p.x * p.x + p.y * p.y;
            const f64 cx = (aa * (b.y - p.y) + bb * (p.y - a.y) + pp * (a.y - b.y)) / d;
            const f64 cy = (aa * (p.x - b.x) + bb * (a.x - p.x) + pp * (b.x - a.x)) / d;
            const f64 r = std::hypot(a.x - cx, a.y - cy);
            if (r < 1e-12) return std::nullopt;

            Shape arc;
            arc.kind = gsnap::ShapeKind::Arc;
            arc.id = s.id;
            arc.a = lat(v, cx, cy);
            arc.b = arc.a;
            arc.radius = static_cast<i64>(std::llround(r * v.scale()));
            f64 sa = std::atan2(a.y - cy, a.x - cx);
            f64 ea = std::atan2(b.y - cy, b.x - cx);
            const f64 pa = std::atan2(p.y - cy, p.x - cx);
            auto ccw = [](f64 from, f64 to_) {
                f64 d2 = to_ - from;
                while (d2 < 0.0) d2 += 6.283185307179586;
                return d2;
            };
            if (ccw(sa, pa) > ccw(sa, ea)) std::swap(sa, ea);
            arc.start_angle = sa;
            arc.end_angle = ea;
            return arc;
        }

        case GripOp::ConvertToLine: {
            if (s.kind != gsnap::ShapeKind::Arc) return std::nullopt;
            const W c = world(v, s.a);
            const f64 r = f64(s.radius) / v.scale();
            Shape seg;
            seg.kind = gsnap::ShapeKind::Segment;
            seg.id = s.id;
            seg.a = lat(v, c.x + r * std::cos(s.start_angle),
                        c.y + r * std::sin(s.start_angle));
            seg.b = lat(v, c.x + r * std::cos(s.end_angle),
                        c.y + r * std::sin(s.end_angle));
            if (seg.a.x == seg.b.x && seg.a.y == seg.b.y) return std::nullopt;
            return seg;
        }

        case GripOp::Radius:
        case GripOp::Diameter: {
            if (!s.is_curve()) return std::nullopt;
            const W c = world(v, s.a), p = world(v, to);
            f64 r = std::hypot(p.x - c.x, p.y - c.y);
            if (op == GripOp::Diameter) r *= 0.5;
            const i64 lattice_r = static_cast<i64>(std::llround(r * v.scale()));
            if (lattice_r <= 0) return std::nullopt;
            o.radius = lattice_r;
            return o;
        }

        case GripOp::Rotate: {
            if (s.kind != gsnap::ShapeKind::Arc) return std::nullopt;
            const W c = world(v, s.a), p = world(v, to);
            const f64 want = std::atan2(p.y - c.y, p.x - c.x);
            const f64 have = (g.index == 1) ? s.start_angle : s.end_angle;
            const f64 turn = want - have;
            o.start_angle = s.start_angle + turn;
            o.end_angle = s.end_angle + turn;
            return o;
        }

        case GripOp::AddVertex: {
            if (s.kind != gsnap::ShapeKind::Segment) return std::nullopt;
            const Point mid{(s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2};
            if ((mid.x == s.a.x && mid.y == s.a.y) ||
                (mid.x == s.b.x && mid.y == s.b.y))
                return std::nullopt;
            o.b = mid;
            return o;
        }

        case GripOp::RemoveVertex:
            return std::nullopt;
    }
    return std::nullopt;
}

}
