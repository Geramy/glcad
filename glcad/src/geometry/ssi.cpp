#include "geometry/ssi.hpp"

#include <algorithm>
#include <cmath>

namespace gtopo {

namespace {

constexpr f64 kPi = 3.14159265358979323846;

f64 len3(f64 x, f64 y, f64 z) { return std::sqrt(x * x + y * y + z * z); }

void cross(f64 ax, f64 ay, f64 az, f64 bx, f64 by, f64 bz, f64& cx, f64& cy,
           f64& cz) {
    cx = ay * bz - az * by;
    cy = az * bx - ax * bz;
    cz = ax * by - ay * bx;
}

f64 dot3(f64 ax, f64 ay, f64 az, f64 bx, f64 by, f64 bz) {
    return ax * bx + ay * by + az * bz;
}

void fill_circle(IntersectionCurve& c, int steps) {
    f64 ux, uy, uz;
    if (std::fabs(c.direction.x) < 0.9) {
        cross(c.direction.x, c.direction.y, c.direction.z, 1, 0, 0, ux, uy, uz);
    } else {
        cross(c.direction.x, c.direction.y, c.direction.z, 0, 1, 0, ux, uy, uz);
    }
    const f64 ul = len3(ux, uy, uz);
    if (ul < 1e-15) return;
    ux /= ul; uy /= ul; uz /= ul;

    f64 vx, vy, vz;
    cross(c.direction.x, c.direction.y, c.direction.z, ux, uy, uz, vx, vy, vz);

    c.polyline.clear();
    c.polyline.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const f64 t = 2.0 * kPi * i / steps;
        const f64 ca = std::cos(t) * c.radius, sa = std::sin(t) * c.radius;
        c.polyline.push_back({c.origin.x + ux * ca + vx * sa,
                              c.origin.y + uy * ca + vy * sa,
                              c.origin.z + uz * ca + vz * sa});
    }
    c.closed = true;
}

void fill_ellipse(IntersectionCurve& c, int steps) {
    c.polyline.clear();
    c.polyline.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const f64 t = 2.0 * kPi * i / steps;
        const f64 ca = std::cos(t), sa = std::sin(t);
        c.polyline.push_back(
            {c.origin.x + c.major.x * ca + c.minor_dir.x * sa,
             c.origin.y + c.major.y * ca + c.minor_dir.y * sa,
             c.origin.z + c.major.z * ca + c.minor_dir.z * sa});
    }
    c.closed = true;
}

void fill_line(IntersectionCurve& c, const SsiRegion& r) {
    f64 t0 = -1e30, t1 = 1e30;
    const f64 o[3] = {c.origin.x, c.origin.y, c.origin.z};
    const f64 d[3] = {c.direction.x, c.direction.y, c.direction.z};

    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-15) {
            if (o[a] < r.lo[a] || o[a] > r.hi[a]) {
                t0 = 1.0;
                t1 = 0.0;
            }
            continue;
        }
        f64 lo = (r.lo[a] - o[a]) / d[a];
        f64 hi = (r.hi[a] - o[a]) / d[a];
        if (lo > hi) std::swap(lo, hi);
        t0 = std::max(t0, lo);
        t1 = std::min(t1, hi);
    }

    c.polyline.clear();
    if (t0 > t1) return;
    c.polyline.push_back({o[0] + d[0] * t0, o[1] + d[1] * t0, o[2] + d[2] * t0});
    c.polyline.push_back({o[0] + d[0] * t1, o[1] + d[1] * t1, o[2] + d[2] * t1});
}

}

const char* curve_kind_name(CurveKind k) noexcept {
    switch (k) {
        case CurveKind::None:       return "none";
        case CurveKind::Point:      return "point";
        case CurveKind::Line:       return "line";
        case CurveKind::Circle:     return "circle";
        case CurveKind::Ellipse:    return "ellipse";
        case CurveKind::Marched:    return "marched";
        case CurveKind::Coincident: return "coincident";
    }
    return "?";
}

f64 IntersectionCurve::length() const {
    f64 total = 0.0;
    for (std::size_t i = 1; i < polyline.size(); ++i)
        total += len3(polyline[i].x - polyline[i - 1].x,
                      polyline[i].y - polyline[i - 1].y,
                      polyline[i].z - polyline[i - 1].z);
    return total;
}

bool SsiRegion::contains(f64 x, f64 y, f64 z, f64 slack) const noexcept {
    return x >= lo[0] - slack && x <= hi[0] + slack && y >= lo[1] - slack &&
           y <= hi[1] + slack && z >= lo[2] - slack && z <= hi[2] + slack;
}

f64 SsiRegion::diagonal() const noexcept {
    return len3(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
}

bool project_onto_both(const Surface& a, const Surface& b, f64 x, f64 y, f64 z,
                       f64& px, f64& py, f64& pz, f64 tolerance,
                       int max_iterations) {
    px = x;
    py = y;
    pz = z;

    for (int i = 0; i < max_iterations; ++i) {
        const f64 fa = a.implicit(px, py, pz);
        const f64 fb = b.implicit(px, py, pz);
        if (std::fabs(fa) < tolerance && std::fabs(fb) < tolerance) return true;

        f64 nax, nay, naz, nbx, nby, nbz;
        if (!a.normal_at(px, py, pz, nax, nay, naz)) return false;
        if (!b.normal_at(px, py, pz, nbx, nby, nbz)) return false;

        const f64 aa = dot3(nax, nay, naz, nax, nay, naz);
        const f64 ab = dot3(nax, nay, naz, nbx, nby, nbz);
        const f64 bb = dot3(nbx, nby, nbz, nbx, nby, nbz);
        const f64 det = aa * bb - ab * ab;
        if (std::fabs(det) < 1e-14) return false;

        const f64 ca = (fa * bb - fb * ab) / det;
        const f64 cb = (fb * aa - fa * ab) / det;

        px -= nax * ca + nbx * cb;
        py -= nay * ca + nby * cb;
        pz -= naz * ca + nbz * cb;
    }

    return std::fabs(a.implicit(px, py, pz)) < tolerance * 100.0 &&
           std::fabs(b.implicit(px, py, pz)) < tolerance * 100.0;
}

bool has_exact_form(const Surface& a, const Surface& b) noexcept {
    const SurfaceKind ka = a.kind, kb = b.kind;
    auto pair = [&](SurfaceKind p, SurfaceKind q) {
        return (ka == p && kb == q) || (ka == q && kb == p);
    };

    if (pair(SurfaceKind::Plane, SurfaceKind::Plane)) return true;
    if (pair(SurfaceKind::Plane, SurfaceKind::Sphere)) return true;
    if (pair(SurfaceKind::Sphere, SurfaceKind::Sphere)) return true;
    if (pair(SurfaceKind::Plane, SurfaceKind::Cylinder)) return true;
    if (pair(SurfaceKind::Cylinder, SurfaceKind::Cylinder)) {
        const f64 d = std::fabs(dot3(a.ax, a.ay, a.az, b.ax, b.ay, b.az));
        return std::fabs(d - 1.0) < 1e-9;
    }
    return false;
}

namespace {

SsiResult plane_plane(const Surface& a, const Surface& b, const SsiRegion& r) {
    SsiResult out;
    out.was_exact = true;

    f64 dx, dy, dz;
    cross(a.ax, a.ay, a.az, b.ax, b.ay, b.az, dx, dy, dz);
    const f64 dl = len3(dx, dy, dz);

    if (dl < 1e-12) {
        if (std::fabs(a.implicit(b.ox, b.oy, b.oz)) < 1e-12) {
            IntersectionCurve c;
            c.kind = CurveKind::Coincident;
            out.curves.push_back(c);
        }
        out.ok = true;
        out.reason = out.curves.empty() ? "the planes are parallel" : "coplanar";
        return out;
    }

    IntersectionCurve c;
    c.kind = CurveKind::Line;
    c.direction = {dx / dl, dy / dl, dz / dl};

    const f64 da = dot3(a.ax, a.ay, a.az, a.ox, a.oy, a.oz);
    const f64 db = dot3(b.ax, b.ay, b.az, b.ox, b.oy, b.oz);

    f64 c1x, c1y, c1z, c2x, c2y, c2z;
    cross(b.ax, b.ay, b.az, c.direction.x, c.direction.y, c.direction.z, c1x, c1y, c1z);
    cross(c.direction.x, c.direction.y, c.direction.z, a.ax, a.ay, a.az, c2x, c2y, c2z);
    const f64 denom = dl * dl;
    c.origin = {(da * c1x + db * c2x) / denom, (da * c1y + db * c2y) / denom,
                (da * c1z + db * c2z) / denom};

    fill_line(c, r);
    if (!c.polyline.empty()) out.curves.push_back(c);
    out.ok = true;
    return out;
}

SsiResult plane_sphere(const Surface& p, const Surface& s, const SsiRegion& r) {
    SsiResult out;
    out.was_exact = true;
    out.ok = true;

    const f64 d = p.implicit(s.ox, s.oy, s.oz);
    if (std::fabs(d) > s.radius + 1e-12) {
        out.reason = "the plane misses the sphere";
        return out;
    }

    IntersectionCurve c;
    c.origin = {s.ox - p.ax * d, s.oy - p.ay * d, s.oz - p.az * d};
    c.direction = {p.ax, p.ay, p.az};

    if (std::fabs(std::fabs(d) - s.radius) < 1e-12) {
        c.kind = CurveKind::Point;
        c.polyline.push_back(c.origin);
        out.curves.push_back(c);
        return out;
    }

    c.kind = CurveKind::Circle;
    c.radius = std::sqrt(s.radius * s.radius - d * d);
    fill_circle(c, 64);
    (void)r;
    out.curves.push_back(c);
    return out;
}

SsiResult sphere_sphere(const Surface& a, const Surface& b, const SsiRegion& r) {
    SsiResult out;
    out.was_exact = true;
    out.ok = true;

    const f64 dx = b.ox - a.ox, dy = b.oy - a.oy, dz = b.oz - a.oz;
    const f64 d = len3(dx, dy, dz);

    if (d < 1e-12) {
        if (std::fabs(a.radius - b.radius) < 1e-12) {
            IntersectionCurve c;
            c.kind = CurveKind::Coincident;
            out.curves.push_back(c);
            out.reason = "concentric and equal";
        } else {
            out.reason = "concentric spheres of different radius never meet";
        }
        return out;
    }

    if (d > a.radius + b.radius + 1e-12 ||
        d < std::fabs(a.radius - b.radius) - 1e-12) {
        out.reason = "the spheres do not reach each other";
        return out;
    }

    const f64 t = (d * d + a.radius * a.radius - b.radius * b.radius) / (2.0 * d);
    const f64 h2 = a.radius * a.radius - t * t;

    IntersectionCurve c;
    c.direction = {dx / d, dy / d, dz / d};
    c.origin = {a.ox + c.direction.x * t, a.oy + c.direction.y * t,
                a.oz + c.direction.z * t};

    if (h2 <= 1e-18) {
        c.kind = CurveKind::Point;
        c.polyline.push_back(c.origin);
        out.curves.push_back(c);
        return out;
    }

    c.kind = CurveKind::Circle;
    c.radius = std::sqrt(h2);
    fill_circle(c, 64);
    (void)r;
    out.curves.push_back(c);
    return out;
}

SsiResult plane_cylinder(const Surface& p, const Surface& cyl,
                         const SsiRegion& r) {
    SsiResult out;
    out.was_exact = true;
    out.ok = true;

    const f64 cosang = dot3(p.ax, p.ay, p.az, cyl.ax, cyl.ay, cyl.az);

    if (std::fabs(cosang) < 1e-9) {
        const f64 d = p.implicit(cyl.ox, cyl.oy, cyl.oz);
        if (std::fabs(d) > cyl.radius + 1e-12) {
            out.reason = "the plane misses the cylinder";
            return out;
        }
        f64 sx, sy, sz;
        cross(p.ax, p.ay, p.az, cyl.ax, cyl.ay, cyl.az, sx, sy, sz);
        const f64 sl = len3(sx, sy, sz);
        if (sl < 1e-15) {
            out.reason = "degenerate plane and cylinder";
            return out;
        }
        sx /= sl; sy /= sl; sz /= sl;

        const f64 half = std::sqrt(std::max(0.0, cyl.radius * cyl.radius - d * d));
        const f64 fx = cyl.ox - p.ax * d, fy = cyl.oy - p.ay * d,
                  fz = cyl.oz - p.az * d;

        for (int sign : {1, -1}) {
            if (half < 1e-12 && sign < 0) break;
            IntersectionCurve c;
            c.kind = CurveKind::Line;
            c.origin = {fx + sx * half * sign, fy + sy * half * sign,
                        fz + sz * half * sign};
            c.direction = {cyl.ax, cyl.ay, cyl.az};
            fill_line(c, r);
            if (!c.polyline.empty()) out.curves.push_back(c);
        }
        return out;
    }

    const f64 d = p.implicit(cyl.ox, cyl.oy, cyl.oz);
    const f64 t = -d / cosang;
    const f64 cx = cyl.ox + cyl.ax * t;
    const f64 cy = cyl.oy + cyl.ay * t;
    const f64 cz = cyl.oz + cyl.az * t;

    IntersectionCurve c;
    c.origin = {cx, cy, cz};
    c.direction = {p.ax, p.ay, p.az};

    if (std::fabs(std::fabs(cosang) - 1.0) < 1e-9) {
        c.kind = CurveKind::Circle;
        c.radius = cyl.radius;
        fill_circle(c, 64);
        out.curves.push_back(c);
        return out;
    }

    f64 mnx, mny, mnz;
    cross(p.ax, p.ay, p.az, cyl.ax, cyl.ay, cyl.az, mnx, mny, mnz);
    const f64 ml = len3(mnx, mny, mnz);
    mnx /= ml; mny /= ml; mnz /= ml;

    f64 mjx, mjy, mjz;
    cross(mnx, mny, mnz, p.ax, p.ay, p.az, mjx, mjy, mjz);
    const f64 jl = len3(mjx, mjy, mjz);
    mjx /= jl; mjy /= jl; mjz /= jl;

    c.kind = CurveKind::Ellipse;
    c.minor_radius = cyl.radius;
    c.major_radius = cyl.radius / std::fabs(cosang);
    c.major = {mjx * c.major_radius, mjy * c.major_radius, mjz * c.major_radius};
    c.minor_dir = {mnx * c.minor_radius, mny * c.minor_radius,
                   mnz * c.minor_radius};
    fill_ellipse(c, 96);
    out.curves.push_back(c);
    return out;
}

SsiResult cylinder_cylinder_parallel(const Surface& a, const Surface& b,
                                     const SsiRegion& r) {
    SsiResult out;
    out.was_exact = true;
    out.ok = true;

    const f64 vx = b.ox - a.ox, vy = b.oy - a.oy, vz = b.oz - a.oz;
    const f64 along = dot3(vx, vy, vz, a.ax, a.ay, a.az);
    const f64 px = vx - a.ax * along, py = vy - a.ay * along, pz = vz - a.az * along;
    const f64 d = len3(px, py, pz);

    if (d < 1e-12) {
        if (std::fabs(a.radius - b.radius) < 1e-12) {
            IntersectionCurve c;
            c.kind = CurveKind::Coincident;
            out.curves.push_back(c);
            out.reason = "coaxial and equal";
        } else {
            out.reason = "coaxial cylinders of different radius never meet";
        }
        return out;
    }

    if (d > a.radius + b.radius + 1e-12 ||
        d < std::fabs(a.radius - b.radius) - 1e-12) {
        out.reason = "the cylinders do not reach each other";
        return out;
    }

    const f64 ux = px / d, uy = py / d, uz = pz / d;
    const f64 t = (d * d + a.radius * a.radius - b.radius * b.radius) / (2.0 * d);
    const f64 h2 = a.radius * a.radius - t * t;

    f64 sx, sy, sz;
    cross(a.ax, a.ay, a.az, ux, uy, uz, sx, sy, sz);

    for (int sign : {1, -1}) {
        if (h2 <= 1e-18 && sign < 0) break;
        const f64 h = h2 > 0.0 ? std::sqrt(h2) : 0.0;
        IntersectionCurve c;
        c.kind = CurveKind::Line;
        c.origin = {a.ox + ux * t + sx * h * sign, a.oy + uy * t + sy * h * sign,
                    a.oz + uz * t + sz * h * sign};
        c.direction = {a.ax, a.ay, a.az};
        fill_line(c, r);
        if (!c.polyline.empty()) out.curves.push_back(c);
    }
    return out;
}

SsiResult march(const Surface& a, const Surface& b, const SsiRegion& r,
                f64 tolerance) {
    SsiResult out;
    out.was_exact = false;

    const f64 diag = r.diagonal();
    const f64 step = std::max(diag * 0.004, tolerance * 10.0);
    const f64 seed_tol = std::max(tolerance, 1e-12);

    std::vector<Pt3> seeds;
    const int n = 12;
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j)
            for (int k = 0; k <= n; ++k) {
                const f64 x = r.lo[0] + (r.hi[0] - r.lo[0]) * i / n;
                const f64 y = r.lo[1] + (r.hi[1] - r.lo[1]) * j / n;
                const f64 z = r.lo[2] + (r.hi[2] - r.lo[2]) * k / n;
                f64 px, py, pz;
                if (!project_onto_both(a, b, x, y, z, px, py, pz, seed_tol)) continue;
                if (!r.contains(px, py, pz, step)) continue;

                bool duplicate = false;
                for (const Pt3& s : seeds)
                    if (len3(s.x - px, s.y - py, s.z - pz) < step * 3.0)
                        duplicate = true;
                if (!duplicate) seeds.push_back({px, py, pz});
            }

    if (seeds.empty()) {
        out.ok = true;
        out.reason = "no intersection found in the region";
        return out;
    }

    std::vector<Pt3> claimed;
    auto already_walked = [&](const Pt3& p) {
        for (const Pt3& c : claimed)
            if (len3(c.x - p.x, c.y - p.y, c.z - p.z) < step * 1.5) return true;
        return false;
    };

    for (const Pt3& seed : seeds) {
        if (already_walked(seed)) continue;

        IntersectionCurve c;
        c.kind = CurveKind::Marched;
        c.tolerance = tolerance;

        for (int dir = 0; dir < 2; ++dir) {
            std::vector<Pt3> side;
            Pt3 p = seed;
            const int max_steps = static_cast<int>(diag / step) * 4 + 64;

            for (int s = 0; s < max_steps; ++s) {
                f64 nax, nay, naz, nbx, nby, nbz;
                if (!a.normal_at(p.x, p.y, p.z, nax, nay, naz)) break;
                if (!b.normal_at(p.x, p.y, p.z, nbx, nby, nbz)) break;

                f64 tx, ty, tz;
                cross(nax, nay, naz, nbx, nby, nbz, tx, ty, tz);
                const f64 tl = len3(tx, ty, tz);
                if (tl < 1e-12) break;
                const f64 sgn = dir == 0 ? 1.0 : -1.0;
                tx = tx / tl * step * sgn;
                ty = ty / tl * step * sgn;
                tz = tz / tl * step * sgn;

                f64 px, py, pz;
                if (!project_onto_both(a, b, p.x + tx, p.y + ty, p.z + tz, px, py,
                                       pz, tolerance))
                    break;
                p = {px, py, pz};
                if (!r.contains(p.x, p.y, p.z, step)) break;
                side.push_back(p);
                claimed.push_back(p);

                if (s > 8 &&
                    len3(p.x - seed.x, p.y - seed.y, p.z - seed.z) < step * 0.9) {
                    c.closed = true;
                    break;
                }
            }

            if (dir == 0) {
                c.polyline.push_back(seed);
                for (const Pt3& q : side) c.polyline.push_back(q);
                if (c.closed) break;
            } else {
                std::vector<Pt3> joined;
                for (auto it = side.rbegin(); it != side.rend(); ++it)
                    joined.push_back(*it);
                for (const Pt3& q : c.polyline) joined.push_back(q);
                c.polyline = std::move(joined);
            }
        }

        if (c.polyline.size() >= 2) out.curves.push_back(std::move(c));
    }

    out.ok = true;
    if (out.curves.empty()) out.reason = "the surfaces touch but do not cross";
    return out;
}

}

SsiResult intersect_surfaces(const Surface& a, const Surface& b,
                             const SsiRegion& region, f64 tolerance) {
    SsiResult out;
    if (!a.valid() || !b.valid()) {
        out.reason = "one of the surfaces is degenerate";
        return out;
    }

    if (a.same_as(b)) {
        IntersectionCurve c;
        c.kind = CurveKind::Coincident;
        out.curves.push_back(c);
        out.ok = true;
        out.was_exact = true;
        out.reason = "the same surface";
        return out;
    }

    const SurfaceKind ka = a.kind, kb = b.kind;
    auto is = [&](SurfaceKind p, SurfaceKind q) { return ka == p && kb == q; };

    if (is(SurfaceKind::Plane, SurfaceKind::Plane)) return plane_plane(a, b, region);
    if (is(SurfaceKind::Plane, SurfaceKind::Sphere)) return plane_sphere(a, b, region);
    if (is(SurfaceKind::Sphere, SurfaceKind::Plane)) return plane_sphere(b, a, region);
    if (is(SurfaceKind::Sphere, SurfaceKind::Sphere))
        return sphere_sphere(a, b, region);
    if (is(SurfaceKind::Plane, SurfaceKind::Cylinder))
        return plane_cylinder(a, b, region);
    if (is(SurfaceKind::Cylinder, SurfaceKind::Plane))
        return plane_cylinder(b, a, region);

    if (is(SurfaceKind::Cylinder, SurfaceKind::Cylinder)) {
        const f64 d = std::fabs(dot3(a.ax, a.ay, a.az, b.ax, b.ay, b.az));
        if (std::fabs(d - 1.0) < 1e-9)
            return cylinder_cylinder_parallel(a, b, region);
    }

    return march(a, b, region, tolerance);
}

}
