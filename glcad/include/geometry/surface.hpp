#pragma once

#include <memory>
#include "geometry/nurbs.hpp"

#include <cmath>
#include <string>

#include "math/config.hpp"

namespace gtopo {

using gcm::f64;
using gcm::u32;

enum class SurfaceKind : gcm::u8 { Plane, Cylinder, Cone, Sphere, Torus, BSpline };

const char* surface_kind_name(SurfaceKind k) noexcept;

struct Surface {
    SurfaceKind kind{SurfaceKind::Plane};

    f64 ox{0}, oy{0}, oz{0};
    f64 ax{0}, ay{0}, az{1};
    f64 rx{1}, ry{0}, rz{0};

    f64 radius{0.0};         // cylinder, sphere; torus MAJOR radius
    f64 minor_radius{0.0};   // torus only
    f64 half_angle{0.0};     // cone, radians from the axis

    std::shared_ptr<const ggeo::NurbsSurface<gcm::F64>> nurbs;
    static Surface bspline(std::shared_ptr<const ggeo::NurbsSurface<gcm::F64>> n);

    static Surface plane(f64 ox, f64 oy, f64 oz, f64 nx, f64 ny, f64 nz);
    static Surface cylinder(f64 ox, f64 oy, f64 oz, f64 axx, f64 axy, f64 axz,
                            f64 r);
    static Surface sphere(f64 cx, f64 cy, f64 cz, f64 r);
    static Surface cone(f64 apex_x, f64 apex_y, f64 apex_z, f64 axx, f64 axy,
                        f64 axz, f64 half_angle_rad);
    static Surface torus(f64 cx, f64 cy, f64 cz, f64 axx, f64 axy, f64 axz,
                         f64 major, f64 minor);

    bool valid() const noexcept;
    void normalise();

    f64 implicit(f64 x, f64 y, f64 z) const noexcept;

    bool normal_at(f64 x, f64 y, f64 z, f64& nx, f64& ny, f64& nz) const noexcept;

    // u around the axis, v along it (across the tube for a torus).
    bool evaluate(f64 u, f64 v, f64& x, f64& y, f64& z) const noexcept;

    bool project(f64 x, f64 y, f64 z, f64& px, f64& py, f64& pz) const noexcept;

    f64 distance_to(f64 x, f64 y, f64 z) const noexcept;

    bool same_as(const Surface& o, f64 tol = 1e-9) const noexcept;
};

}
