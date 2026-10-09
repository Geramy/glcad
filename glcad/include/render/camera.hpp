#pragma once

#include "geometry/d3.hpp"

namespace gview {

using gcm::F64;
using gcm::f64;
using gcm::i32;
using gcm::u8;
using Vec3  = ggeo::d3::Vec<F64>;
using Point3 = ggeo::d3::Point<F64>;

enum class Projection : u8 { Perspective, Orthographic };

enum class StandardView : u8 {
    Front, Back, Left, Right, Top, Bottom,
    Iso,
    IsoNE, IsoNW, IsoSE, IsoSW,
};

const char* view_name(StandardView v) noexcept;

// Column-major.
struct Mat4 {
    f64 m[16]{};
    static Mat4 identity() noexcept;
    Mat4 operator*(const Mat4& o) const noexcept;
    void to_f32(float out[16]) const noexcept;
    Mat4 inverse() const noexcept;
};

class Camera {
public:
    Camera();

    Point3 target() const noexcept { return target_; }
    void   set_target(Point3 p) noexcept { target_ = p; }
    f64    distance() const noexcept { return distance_; }
    void   set_distance(f64 d) noexcept;

    // Radians: azimuth around +Z, elevation from the XY plane.
    f64  azimuth() const noexcept { return azimuth_; }
    f64  elevation() const noexcept { return elevation_; }
    void set_orbit(f64 azimuth, f64 elevation) noexcept;

    Projection projection() const noexcept { return proj_; }
    void       set_projection(Projection p) noexcept { proj_ = p; }
    void       toggle_projection() noexcept;

    f64  fov() const noexcept { return fov_; }
    void set_fov(f64 radians) noexcept { fov_ = radians; }

    void set_viewport(f64 w, f64 h) noexcept;
    f64  aspect() const noexcept { return (vh_ > 0.0) ? vw_ / vh_ : 1.0; }

    Point3 eye() const noexcept;
    Vec3   forward() const noexcept;
    Vec3   right() const noexcept;
    Vec3   up() const noexcept;

    Mat4 view_matrix() const noexcept;
    Mat4 projection_matrix() const noexcept;
    Mat4 view_projection() const noexcept { return projection_matrix() * view_matrix(); }

    f64 ortho_half_height() const noexcept;

    void orbit(f64 d_azimuth, f64 d_elevation) noexcept;

    void orbit_about(const Point3& pivot, f64 d_azimuth, f64 d_elevation) noexcept;
    void pan(f64 dx_px, f64 dy_px) noexcept;
    void pan(f64 dx_px, f64 dy_px, const Point3& depth_anchor) noexcept;
    void dolly(f64 factor) noexcept;
    void zoom_to(const ggeo::d3::AABB<F64>& box, f64 margin = 1.15) noexcept;
    void zoom_to_region(const ggeo::d3::AABB<F64>& box, f64 x, f64 y,
                        f64 width, f64 height, f64 margin = 1.15) noexcept;

    void look(StandardView v) noexcept;

    void look_plan() noexcept;
    void look_along_normal(f64 nx, f64 ny, f64 nz) noexcept;
    bool looks_along(f64 nx, f64 ny, f64 nz, f64 tolerance = 1e-4) const noexcept;
    bool is_plan(f64 tolerance_rad = 1e-4) const noexcept;

    void zoom_at(f64 px, f64 py, f64 factor) noexcept;
    void zoom_at(f64 px, f64 py, f64 factor, const Point3& depth_anchor) noexcept;

    bool plane_hit(f64 px, f64 py, f64 z, f64& wx, f64& wy) const noexcept;
    bool plane_hit(f64 px, f64 py, const Point3& on, const Vec3& normal,
                   Point3& out) const noexcept;
    StandardView nearest_standard() const noexcept;

    bool is_axis_aligned(f64 tolerance_rad = 1e-6) const noexcept;

    ggeo::d3::Ray<F64> ray_through(f64 px, f64 py) const noexcept;

    bool clip_segment(Point3& a, Point3& b) const noexcept;
    bool project(Point3 world, f64& px, f64& py) const noexcept;
    bool project(const Mat4& vp, f64 x, f64 y, f64 z, f64& px, f64& py) const noexcept;

private:
    Point3     target_{};
    f64        distance_{10.0};
    f64        azimuth_{-0.7853981633974483};   // -45 deg
    f64        elevation_{0.6154797086703873};  // ~35.264 deg: true isometric
    Projection proj_{Projection::Perspective};
    // 38 deg: reads as a true lens, not a wide angle.
    f64        fov_{0.6632251157578453};
    f64        vw_{1440.0}, vh_{900.0};
    f64        near_{0.01}, far_{100000.0};
};

}
