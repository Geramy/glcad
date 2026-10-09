#include "geometry/surface.hpp"

#include <algorithm>

namespace gtopo {

namespace {

f64 len3(f64 x, f64 y, f64 z) { return std::sqrt(x * x + y * y + z * z); }

void cross(f64 ax, f64 ay, f64 az, f64 bx, f64 by, f64 bz, f64& cx, f64& cy,
           f64& cz) {
    cx = ay * bz - az * by;
    cy = az * bx - ax * bz;
    cz = ax * by - ay * bx;
}

}

const char* surface_kind_name(SurfaceKind k) noexcept {
    switch (k) {
        case SurfaceKind::Plane:    return "plane";
        case SurfaceKind::Cylinder: return "cylinder";
        case SurfaceKind::Cone:     return "cone";
        case SurfaceKind::Sphere:   return "sphere";
        case SurfaceKind::Torus:    return "torus";
        case SurfaceKind::BSpline:  return "b-spline";
    }
    return "?";
}

void Surface::normalise() {
    const f64 al = len3(ax, ay, az);
    if (al > 1e-15) {
        ax /= al;
        ay /= al;
        az /= al;
    }
    const f64 d = rx * ax + ry * ay + rz * az;
    rx -= ax * d;
    ry -= ay * d;
    rz -= az * d;
    f64 rl = len3(rx, ry, rz);
    if (rl < 1e-12) {
        if (std::fabs(ax) < 0.9) { rx = 1; ry = 0; rz = 0; }
        else { rx = 0; ry = 1; rz = 0; }
        const f64 d2 = rx * ax + ry * ay + rz * az;
        rx -= ax * d2;
        ry -= ay * d2;
        rz -= az * d2;
        rl = len3(rx, ry, rz);
    }
    if (rl > 1e-15) {
        rx /= rl;
        ry /= rl;
        rz /= rl;
    }
}

Surface Surface::bspline(std::shared_ptr<const ggeo::NurbsSurface<gcm::F64>> n) {
    Surface s;
    s.kind = SurfaceKind::BSpline;
    s.nurbs = std::move(n);
    return s;
}

Surface Surface::plane(f64 ox, f64 oy, f64 oz, f64 nx, f64 ny, f64 nz) {
    Surface s;
    s.kind = SurfaceKind::Plane;
    s.ox = ox; s.oy = oy; s.oz = oz;
    s.ax = nx; s.ay = ny; s.az = nz;
    s.normalise();
    return s;
}

Surface Surface::cylinder(f64 ox, f64 oy, f64 oz, f64 axx, f64 axy, f64 axz,
                          f64 r) {
    Surface s;
    s.kind = SurfaceKind::Cylinder;
    s.ox = ox; s.oy = oy; s.oz = oz;
    s.ax = axx; s.ay = axy; s.az = axz;
    s.radius = r;
    s.normalise();
    return s;
}

Surface Surface::sphere(f64 cx, f64 cy, f64 cz, f64 r) {
    Surface s;
    s.kind = SurfaceKind::Sphere;
    s.ox = cx; s.oy = cy; s.oz = cz;
    s.radius = r;
    s.normalise();
    return s;
}

Surface Surface::cone(f64 apex_x, f64 apex_y, f64 apex_z, f64 axx, f64 axy,
                      f64 axz, f64 half_angle_rad) {
    Surface s;
    s.kind = SurfaceKind::Cone;
    s.ox = apex_x; s.oy = apex_y; s.oz = apex_z;
    s.ax = axx; s.ay = axy; s.az = axz;
    s.half_angle = half_angle_rad;
    s.normalise();
    return s;
}

Surface Surface::torus(f64 cx, f64 cy, f64 cz, f64 axx, f64 axy, f64 axz,
                       f64 major, f64 minor) {
    Surface s;
    s.kind = SurfaceKind::Torus;
    s.ox = cx; s.oy = cy; s.oz = cz;
    s.ax = axx; s.ay = axy; s.az = axz;
    s.radius = major;
    s.minor_radius = minor;
    s.normalise();
    return s;
}

bool Surface::valid() const noexcept {
    if (kind == SurfaceKind::BSpline) return nurbs && nurbs->valid();
    if (std::fabs(len3(ax, ay, az) - 1.0) > 1e-6) return false;
    switch (kind) {
        case SurfaceKind::Plane:    return true;
        case SurfaceKind::Cylinder:
        case SurfaceKind::Sphere:   return radius > 0.0;
        case SurfaceKind::Cone:
            return half_angle > 1e-9 && half_angle < 1.5707963267948966 - 1e-9;
        case SurfaceKind::Torus:
            return radius > 0.0 && minor_radius > 0.0;
        case SurfaceKind::BSpline: break;
    }
    return false;
}

f64 Surface::implicit(f64 x, f64 y, f64 z) const noexcept {
    if (kind == SurfaceKind::BSpline) {
        if (!nurbs) return 0.0;
        f64 u, v;
        const f64 d = nurbs->invert(ggeo::d3::Point<gcm::F64>::from(x, y, z), u, v);
        const auto q = nurbs->evaluate(u, v);
        const auto n = nurbs->normal(u, v);
        const f64 side = (x - q.x.to_f64()) * n.x.to_f64() + (y - q.y.to_f64()) * n.y.to_f64() +
                         (z - q.z.to_f64()) * n.z.to_f64();
        return side < 0.0 ? -d : d;
    }
    const f64 dx = x - ox, dy = y - oy, dz = z - oz;

    switch (kind) {
        case SurfaceKind::Plane:
            return dx * ax + dy * ay + dz * az;

        case SurfaceKind::Sphere:
            return len3(dx, dy, dz) - radius;

        case SurfaceKind::Cylinder: {
            const f64 along = dx * ax + dy * ay + dz * az;
            const f64 px = dx - ax * along, py = dy - ay * along, pz = dz - az * along;
            return len3(px, py, pz) - radius;
        }

        case SurfaceKind::Cone: {
            const f64 along = dx * ax + dy * ay + dz * az;
            const f64 px = dx - ax * along, py = dy - ay * along, pz = dz - az * along;
            const f64 r = len3(px, py, pz);
            return (r * std::cos(half_angle) - along * std::sin(half_angle));
        }

        case SurfaceKind::Torus: {
            const f64 along = dx * ax + dy * ay + dz * az;
            const f64 px = dx - ax * along, py = dy - ay * along, pz = dz - az * along;
            const f64 r = len3(px, py, pz);
            return len3(r - radius, along, 0.0) - minor_radius;
        }
        case SurfaceKind::BSpline: break;
    }
    return 0.0;
}

bool Surface::normal_at(f64 x, f64 y, f64 z, f64& nx, f64& ny,
                        f64& nz) const noexcept {
    if (kind == SurfaceKind::BSpline) {
        if (!nurbs) return false;
        f64 u, v;
        nurbs->invert(ggeo::d3::Point<gcm::F64>::from(x, y, z), u, v);
        const auto n = nurbs->normal(u, v);
        nx = n.x.to_f64(); ny = n.y.to_f64(); nz = n.z.to_f64();
        const f64 l = len3(nx, ny, nz);
        if (l < 1e-300) return false;
        nx /= l; ny /= l; nz /= l;
        return true;
    }
    const f64 dx = x - ox, dy = y - oy, dz = z - oz;

    switch (kind) {
        case SurfaceKind::Plane:
            nx = ax; ny = ay; nz = az;
            return true;

        case SurfaceKind::Sphere: {
            const f64 l = len3(dx, dy, dz);
            if (l < 1e-15) return false;
            nx = dx / l; ny = dy / l; nz = dz / l;
            return true;
        }

        case SurfaceKind::Cylinder: {
            const f64 along = dx * ax + dy * ay + dz * az;
            f64 px = dx - ax * along, py = dy - ay * along, pz = dz - az * along;
            const f64 l = len3(px, py, pz);
            if (l < 1e-15) return false;
            nx = px / l; ny = py / l; nz = pz / l;
            return true;
        }

        case SurfaceKind::Cone: {
            const f64 along = dx * ax + dy * ay + dz * az;
            f64 px = dx - ax * along, py = dy - ay * along, pz = dz - az * along;
            const f64 l = len3(px, py, pz);
            if (l < 1e-15) return false;
            px /= l; py /= l; pz /= l;
            const f64 c = std::cos(half_angle), s = std::sin(half_angle);
            nx = px * c - ax * s;
            ny = py * c - ay * s;
            nz = pz * c - az * s;
            const f64 nl = len3(nx, ny, nz);
            if (nl < 1e-15) return false;
            nx /= nl; ny /= nl; nz /= nl;
            return true;
        }

        case SurfaceKind::Torus: {
            const f64 along = dx * ax + dy * ay + dz * az;
            f64 px = dx - ax * along, py = dy - ay * along, pz = dz - az * along;
            const f64 l = len3(px, py, pz);
            if (l < 1e-15) return false;
            const f64 sx = ox + px / l * radius;
            const f64 sy = oy + py / l * radius;
            const f64 sz = oz + pz / l * radius;
            nx = x - sx; ny = y - sy; nz = z - sz;
            const f64 nl = len3(nx, ny, nz);
            if (nl < 1e-15) return false;
            nx /= nl; ny /= nl; nz /= nl;
            return true;
        }
        case SurfaceKind::BSpline: break;
    }
    return false;
}

bool Surface::evaluate(f64 u, f64 v, f64& x, f64& y, f64& z) const noexcept {
    if (kind == SurfaceKind::BSpline) {
        if (!nurbs) return false;
        const auto q = nurbs->evaluate(u, v);
        x = q.x.to_f64(); y = q.y.to_f64(); z = q.z.to_f64();
        return true;
    }
    f64 sx, sy, sz;
    cross(ax, ay, az, rx, ry, rz, sx, sy, sz);
    const f64 cu = std::cos(u), su = std::sin(u);

    switch (kind) {
        case SurfaceKind::Plane:
            x = ox + rx * u + sx * v;
            y = oy + ry * u + sy * v;
            z = oz + rz * u + sz * v;
            return true;

        case SurfaceKind::Cylinder:
            x = ox + (rx * cu + sx * su) * radius + ax * v;
            y = oy + (ry * cu + sy * su) * radius + ay * v;
            z = oz + (rz * cu + sz * su) * radius + az * v;
            return true;

        case SurfaceKind::Sphere: {
            const f64 cv = std::cos(v), sv = std::sin(v);
            x = ox + ((rx * cu + sx * su) * cv + ax * sv) * radius;
            y = oy + ((ry * cu + sy * su) * cv + ay * sv) * radius;
            z = oz + ((rz * cu + sz * su) * cv + az * sv) * radius;
            return true;
        }

        case SurfaceKind::Cone: {
            const f64 r = v * std::tan(half_angle);
            x = ox + (rx * cu + sx * su) * r + ax * v;
            y = oy + (ry * cu + sy * su) * r + ay * v;
            z = oz + (rz * cu + sz * su) * r + az * v;
            return true;
        }

        case SurfaceKind::Torus: {
            const f64 cv = std::cos(v), sv = std::sin(v);
            const f64 rr = radius + minor_radius * cv;
            x = ox + (rx * cu + sx * su) * rr + ax * minor_radius * sv;
            y = oy + (ry * cu + sy * su) * rr + ay * minor_radius * sv;
            z = oz + (rz * cu + sz * su) * rr + az * minor_radius * sv;
            return true;
        }
        case SurfaceKind::BSpline: break;
    }
    return false;
}

bool Surface::project(f64 x, f64 y, f64 z, f64& px, f64& py,
                      f64& pz) const noexcept {
    if (kind == SurfaceKind::BSpline) {
        if (!nurbs) return false;
        f64 u, v;
        nurbs->invert(ggeo::d3::Point<gcm::F64>::from(x, y, z), u, v);
        const auto q = nurbs->evaluate(u, v);
        px = q.x.to_f64(); py = q.y.to_f64(); pz = q.z.to_f64();
        return true;
    }
    const f64 dx = x - ox, dy = y - oy, dz = z - oz;

    switch (kind) {
        case SurfaceKind::Plane: {
            const f64 d = dx * ax + dy * ay + dz * az;
            px = x - ax * d;
            py = y - ay * d;
            pz = z - az * d;
            return true;
        }

        case SurfaceKind::Sphere: {
            const f64 l = len3(dx, dy, dz);
            if (l < 1e-15) return false;
            px = ox + dx / l * radius;
            py = oy + dy / l * radius;
            pz = oz + dz / l * radius;
            return true;
        }

        case SurfaceKind::Cylinder: {
            const f64 along = dx * ax + dy * ay + dz * az;
            f64 rxp = dx - ax * along, ryp = dy - ay * along, rzp = dz - az * along;
            const f64 l = len3(rxp, ryp, rzp);
            if (l < 1e-15) return false;
            px = ox + ax * along + rxp / l * radius;
            py = oy + ay * along + ryp / l * radius;
            pz = oz + az * along + rzp / l * radius;
            return true;
        }

        case SurfaceKind::Cone:
        case SurfaceKind::Torus: {
            f64 cx = x, cy = y, cz = z;
            for (int i = 0; i < 24; ++i) {
                const f64 f = implicit(cx, cy, cz);
                if (std::fabs(f) < 1e-14) break;
                f64 nx, ny, nz;
                if (!normal_at(cx, cy, cz, nx, ny, nz)) return false;
                cx -= nx * f;
                cy -= ny * f;
                cz -= nz * f;
            }
            px = cx; py = cy; pz = cz;
            return std::fabs(implicit(px, py, pz)) < 1e-9;
        }
        case SurfaceKind::BSpline: break;
    }
    return false;
}

f64 Surface::distance_to(f64 x, f64 y, f64 z) const noexcept {
    return std::fabs(implicit(x, y, z));
}

bool Surface::same_as(const Surface& o, f64 tol) const noexcept {
    if (kind == SurfaceKind::BSpline || o.kind == SurfaceKind::BSpline)
        return kind == o.kind && nurbs == o.nurbs;
    if (kind != o.kind) return false;
    if (std::fabs(radius - o.radius) > tol) return false;
    if (std::fabs(minor_radius - o.minor_radius) > tol) return false;
    if (std::fabs(half_angle - o.half_angle) > tol) return false;

    if (kind == SurfaceKind::Plane) {
        const f64 d = std::fabs(ax * o.ax + ay * o.ay + az * o.az);
        return std::fabs(d - 1.0) < tol &&
               std::fabs(implicit(o.ox, o.oy, o.oz)) < tol;
    }
    if (kind == SurfaceKind::Sphere)
        return len3(ox - o.ox, oy - o.oy, oz - o.oz) < tol;

    const f64 d = std::fabs(ax * o.ax + ay * o.ay + az * o.az);
    if (std::fabs(d - 1.0) > tol) return false;

    const f64 vx = o.ox - ox, vy = o.oy - oy, vz = o.oz - oz;
    const f64 along = vx * ax + vy * ay + vz * az;
    return len3(vx - ax * along, vy - ay * along, vz - az * along) < tol;
}

}
