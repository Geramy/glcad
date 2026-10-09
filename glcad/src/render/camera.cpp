#include "render/camera.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace gview {

constexpr f64 kPi = 3.14159265358979323846;

const char* view_name(StandardView v) noexcept {
    switch (v) {
        case StandardView::Front:  return "Front";
        case StandardView::Back:   return "Back";
        case StandardView::Left:   return "Left";
        case StandardView::Right:  return "Right";
        case StandardView::Top:    return "Top";
        case StandardView::Bottom: return "Bottom";
        case StandardView::Iso:    return "Isometric";
        case StandardView::IsoNE:  return "NE Isometric";
        case StandardView::IsoNW:  return "NW Isometric";
        case StandardView::IsoSE:  return "SE Isometric";
        case StandardView::IsoSW:  return "SW Isometric";
    }
    return "";
}

Mat4 Mat4::identity() noexcept {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const noexcept {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 4; ++i) {
            f64 s = 0.0;
            for (int k = 0; k < 4; ++k) s += m[k * 4 + i] * o.m[c * 4 + k];
            r.m[c * 4 + i] = s;
        }
    return r;
}

void Mat4::to_f32(float out[16]) const noexcept {
    for (int i = 0; i < 16; ++i) out[i] = static_cast<float>(m[i]);
}

Camera::Camera() = default;

void Camera::set_distance(f64 d) noexcept {
    if (!std::isfinite(d)) return;
    distance_ = std::clamp(d, 1e-6, 1e9);
}

void Camera::set_orbit(f64 az, f64 el) noexcept {
    azimuth_ = az;
    // Clamp shy of the poles: at exactly +-90 deg the up vector is undefined.
    const f64 limit = kPi * 0.5 - 1e-6;
    elevation_ = std::clamp(el, -limit, limit);
}

void Camera::toggle_projection() noexcept {
    proj_ = (proj_ == Projection::Perspective) ? Projection::Orthographic
                                               : Projection::Perspective;
}

void Camera::set_viewport(f64 w, f64 h) noexcept {
    vw_ = std::max(1.0, w);
    vh_ = std::max(1.0, h);
}

Mat4 Mat4::inverse() const noexcept {
    const f64* a = m;
    f64 inv[16];
    inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
    inv[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
    inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
    inv[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
    inv[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
    inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];
    const f64 det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if (std::fabs(det) < 1e-300) return identity();
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] / det;
    return r;
}

Vec3 Camera::forward() const noexcept {
    const f64 ce = std::cos(elevation_);
    return Vec3::from(-ce * std::cos(azimuth_), -ce * std::sin(azimuth_),
                      -std::sin(elevation_));
}

Point3 Camera::eye() const noexcept {
    const Vec3 f = forward();
    return Point3::from(target_.x.to_f64() - f.x.to_f64() * distance_,
                        target_.y.to_f64() - f.y.to_f64() * distance_,
                        target_.z.to_f64() - f.z.to_f64() * distance_);
}

Vec3 Camera::right() const noexcept {
    // Z is up.
    const Vec3 f = forward();
    const Vec3 wz = Vec3::unit_z();
    Vec3 r = ggeo::d3::cross(f, wz);
    const f64 len = ggeo::d3::length(r).to_f64();
    if (len < 1e-12) return Vec3::unit_x();
    return Vec3::from(r.x.to_f64() / len, r.y.to_f64() / len, r.z.to_f64() / len);
}

Vec3 Camera::up() const noexcept {
    return ggeo::d3::normalized(ggeo::d3::cross(right(), forward()));
}

Mat4 Camera::view_matrix() const noexcept {
    const Vec3 f = forward(), r = right(), u = up();
    const Point3 e = eye();

    Mat4 v = Mat4::identity();
    v.m[0] = r.x.to_f64();  v.m[4] = r.y.to_f64();  v.m[8]  = r.z.to_f64();
    v.m[1] = u.x.to_f64();  v.m[5] = u.y.to_f64();  v.m[9]  = u.z.to_f64();
    v.m[2] = -f.x.to_f64(); v.m[6] = -f.y.to_f64(); v.m[10] = -f.z.to_f64();

    const f64 ex = e.x.to_f64(), ey = e.y.to_f64(), ez = e.z.to_f64();
    v.m[12] = -(r.x.to_f64() * ex + r.y.to_f64() * ey + r.z.to_f64() * ez);
    v.m[13] = -(u.x.to_f64() * ex + u.y.to_f64() * ey + u.z.to_f64() * ez);
    v.m[14] =  (f.x.to_f64() * ex + f.y.to_f64() * ey + f.z.to_f64() * ez);
    return v;
}

f64 Camera::ortho_half_height() const noexcept {
    return distance_ * std::tan(fov_ * 0.5);
}

Mat4 Camera::projection_matrix() const noexcept {
    Mat4 p;
    // Vulkan clip space: Y down, depth 0..1.
    if (proj_ == Projection::Perspective) {
        const f64 t = 1.0 / std::tan(fov_ * 0.5);
        p.m[0]  = t / aspect();
        p.m[5]  = -t;                       // negated: Vulkan Y points down
        p.m[10] = far_ / (near_ - far_);
        p.m[11] = -1.0;
        p.m[14] = (near_ * far_) / (near_ - far_);
    } else {
        const f64 hh = ortho_half_height();
        const f64 hw = hh * aspect();
        // Parallel: depth starts far behind the eye so a zoomed plan is never clipped.
        const f64 n = -far_;
        p.m[0]  = 1.0 / hw;
        p.m[5]  = -1.0 / hh;
        p.m[10] = 1.0 / (n - far_);
        p.m[14] = n / (n - far_);
        p.m[15] = 1.0;
    }
    return p;
}

void Camera::orbit(f64 d_az, f64 d_el) noexcept {
    set_orbit(azimuth_ + d_az, elevation_ + d_el);
}

void Camera::orbit_about(const Point3& pivot, f64 d_az, f64 d_el) noexcept {
    const auto offset=target_-pivot;
    const auto r=right(),u=up(),f=forward();
    const auto dot=[](const Vec3& a,const Vec3& b) {return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto x=dot(offset,r),y=dot(offset,u),z=dot(offset,f);
    set_orbit(azimuth_+d_az,elevation_+d_el);
    target_=pivot+right()*x+up()*y+forward()*z;
}

void Camera::pan(f64 dx_px, f64 dy_px) noexcept { pan(dx_px,dy_px,target_); }

void Camera::pan(f64 dx_px, f64 dy_px, const Point3& depth_anchor) noexcept {
    f64 half_height=ortho_half_height();
    if(proj_==Projection::Perspective) {
        const auto delta=depth_anchor-eye();const auto f=forward();
        const double depth=(delta.x*f.x+delta.y*f.y+delta.z*f.z).to_f64();
        if(std::isfinite(depth) && depth>0) half_height*=depth/distance_;
    }
    if(!std::isfinite(dx_px) || !std::isfinite(dy_px)) return;
    const f64 world_per_px = (2.0 * half_height) / vh_;
    const Vec3 r = right(), u = up();
    const f64 sx = -dx_px * world_per_px, sy = dy_px * world_per_px;
    target_ = Point3::from(
        target_.x.to_f64() + r.x.to_f64() * sx + u.x.to_f64() * sy,
        target_.y.to_f64() + r.y.to_f64() * sx + u.y.to_f64() * sy,
        target_.z.to_f64() + r.z.to_f64() * sx + u.z.to_f64() * sy);
}

void Camera::dolly(f64 factor) noexcept { set_distance(distance_ * factor); }

void Camera::zoom_to(const ggeo::d3::AABB<F64>& box, f64 margin) noexcept {
    if (!box.valid()) return;
    target_ = box.centre();
    const Vec3 s = box.size();
    const f64 radius = 0.5 * std::sqrt(s.x.to_f64() * s.x.to_f64() +
                                       s.y.to_f64() * s.y.to_f64() +
                                       s.z.to_f64() * s.z.to_f64());
    const f64 need = (radius * margin) / std::max(1e-9, std::tan(fov_ * 0.5));
    set_distance(std::max(need, 1e-6));
}

void Camera::zoom_to_region(const ggeo::d3::AABB<F64>& box, f64 x, f64 y,
                            f64 width, f64 height, f64 margin) noexcept {
    if (!box.valid() || !(vw_>0 && vh_>0 && width>0 && height>0) ||
        !std::isfinite(x+y+width+height+margin)) return;
    margin=std::max(1.0,margin);
    const double cx=2*(x+width*.5)/vw_-1, cy=1-2*(y+height*.5)/vh_;
    const double hx=width/vw_/margin, hy=height/vh_/margin;
    const double tx=std::tan(fov_*.5)*aspect(), ty=std::tan(fov_*.5);
    const auto centre=box.centre();
    const auto r=right(), u=up(), f=forward();
    double distance=1e-6;
    for(int i=0;i<8;++i) {
        const auto point=Point3::from((i&1)?box.hi.x.to_f64():box.lo.x.to_f64(),
                                     (i&2)?box.hi.y.to_f64():box.lo.y.to_f64(),
                                     (i&4)?box.hi.z.to_f64():box.lo.z.to_f64());
        const auto delta=point-centre;
        const double dx=ggeo::d3::dot(delta,r).to_f64();
        const double dy=ggeo::d3::dot(delta,u).to_f64();
        const double depth=ggeo::d3::dot(delta,f).to_f64();
        const double dz=proj_==Projection::Perspective?depth:0;
        const double near_distance=proj_==Projection::Perspective?near_*2-depth:0;
        distance=std::max({distance,near_distance,
            (dx/tx-(cx+hx)*dz)/hx, ((cx-hx)*dz-dx/tx)/hx,
            (dy/ty-(cy+hy)*dz)/hy, ((cy-hy)*dz-dy/ty)/hy});
    }
    set_distance(distance);
    target_=centre+r*F64::from(-cx*distance_*tx)+u*F64::from(-cy*distance_*ty);
}

void Camera::look(StandardView v) noexcept {
    constexpr f64 h = kPi * 0.5;
    constexpr f64 iso_el = 0.6154797086703873;   // atan(1/sqrt(2))
    switch (v) {
        case StandardView::Front:  set_orbit(-h, 0.0); break;
        case StandardView::Back:   set_orbit(h, 0.0); break;
        case StandardView::Left:   set_orbit(kPi, 0.0); break;
        case StandardView::Right:  set_orbit(0.0, 0.0); break;
        case StandardView::Top:    set_orbit(-h, h); break;
        case StandardView::Bottom: set_orbit(-h, -h); break;
        case StandardView::Iso:
        case StandardView::IsoSE:  set_orbit(-kPi * 0.25, iso_el); break;
        case StandardView::IsoSW:  set_orbit(-kPi * 0.75, iso_el); break;
        case StandardView::IsoNE:  set_orbit(kPi * 0.25, iso_el); break;
        case StandardView::IsoNW:  set_orbit(kPi * 0.75, iso_el); break;
    }
}

void Camera::look_plan() noexcept {
    look(StandardView::Top);
    proj_ = Projection::Orthographic;
}

bool Camera::is_plan(f64 tol) const noexcept {
    return std::fabs(elevation_ - kPi * 0.5) <= tol;
}

void Camera::look_along_normal(f64 nx, f64 ny, f64 nz) noexcept {
    const f64 len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12) return;
    nx /= len; ny /= len; nz /= len;
    // forward = -n, so el = asin(n.z) and az = atan2(n.y, n.x).
    const f64 el = std::asin(std::clamp(nz, -1.0, 1.0));
    const bool vertical = std::fabs(nz) > 1.0 - 1e-9;
    const f64 az = vertical ? -kPi * 0.5 : std::atan2(ny, nx);
    set_orbit(az, el);
    proj_ = Projection::Orthographic;
}

bool Camera::looks_along(f64 nx, f64 ny, f64 nz, f64 tol) const noexcept {
    const Vec3 f = forward();
    const f64 len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12) return false;
    const f64 d = -(f.x.to_f64() * nx + f.y.to_f64() * ny + f.z.to_f64() * nz) / len;
    return d > 1.0 - tol;
}

bool Camera::plane_hit(f64 px, f64 py, const Point3& on, const Vec3& normal,
                       Point3& out) const noexcept {
    const ggeo::d3::Ray<F64> r = ray_through(px, py);
    const f64 nx = normal.x.to_f64(), ny = normal.y.to_f64(), nz = normal.z.to_f64();
    const f64 dx = r.direction.x.to_f64(), dy = r.direction.y.to_f64(), dz = r.direction.z.to_f64();
    const f64 denom = dx * nx + dy * ny + dz * nz;
    if (std::fabs(denom) < 1e-12) return false;
    const f64 ox = r.origin.x.to_f64(), oy = r.origin.y.to_f64(), oz = r.origin.z.to_f64();
    const f64 t = ((on.x.to_f64() - ox) * nx + (on.y.to_f64() - oy) * ny +
                   (on.z.to_f64() - oz) * nz) / denom;
    if (t < 0.0) return false;
    out = Point3::from(ox + dx * t, oy + dy * t, oz + dz * t);
    return true;
}

bool Camera::plane_hit(f64 px, f64 py, f64 z, f64& wx, f64& wy) const noexcept {
    const ggeo::d3::Ray<F64> r = ray_through(px, py);
    const f64 oz = r.origin.z.to_f64(), dz = r.direction.z.to_f64();
    if (std::fabs(dz) < 1e-12) return false;
    const f64 t = (z - oz) / dz;
    if (t < 0.0) return false;
    wx = r.origin.x.to_f64() + r.direction.x.to_f64() * t;
    wy = r.origin.y.to_f64() + r.direction.y.to_f64() * t;
    return true;
}

void Camera::zoom_at(f64 px, f64 py, f64 factor) noexcept { zoom_at(px,py,factor,target_); }

void Camera::zoom_at(f64 px, f64 py, f64 factor, const Point3& depth_anchor) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0 || !std::isfinite(px) || !std::isfinite(py)) return;
    // Anchor at the target's viewing depth, not the ground plane.
    const Point3 on = depth_anchor;
    const Vec3 normal = forward();
    Point3 anchor;
    const bool anchored=plane_hit(px,py,on,normal,anchor);
    const double previous=distance_;
    dolly(factor);
    if (!anchored) return;
    target_=anchor+(target_-anchor)*F64::from(distance_/previous);
}

StandardView Camera::nearest_standard() const noexcept {
    const StandardView all[] = {
        StandardView::Front, StandardView::Back, StandardView::Left,
        StandardView::Right, StandardView::Top, StandardView::Bottom,
        StandardView::IsoNE, StandardView::IsoNW,
        StandardView::IsoSE, StandardView::IsoSW,
    };
    const Vec3 f = forward();
    StandardView best = StandardView::Iso;
    f64 best_dot = -2.0;

    Camera probe = *this;
    for (StandardView v : all) {
        probe.look(v);
        const Vec3 pf = probe.forward();
        const f64 d = f.x.to_f64() * pf.x.to_f64() + f.y.to_f64() * pf.y.to_f64() +
                      f.z.to_f64() * pf.z.to_f64();
        if (d > best_dot) { best_dot = d; best = v; }
    }
    return best;
}

bool Camera::is_axis_aligned(f64 tol) const noexcept {
    const Vec3 f = forward();
    int nonzero = 0;
    for (f64 c : {f.x.to_f64(), f.y.to_f64(), f.z.to_f64()})
        if (std::fabs(c) > tol) ++nonzero;
    return nonzero == 1;
}

ggeo::d3::Ray<F64> Camera::ray_through(f64 px, f64 py) const noexcept {
    // Screen y grows downward.
    const f64 ndc_x = (px / vw_) * 2.0 - 1.0;
    const f64 ndc_y = 1.0 - (py / vh_) * 2.0;

    const Vec3 r = right(), u = up(), f = forward();
    const f64 hh = ortho_half_height();
    const f64 hw = hh * aspect();

    if (proj_ == Projection::Orthographic) {
        // The ray starts far behind the eye, matching the projection.
        const Point3 e = eye();
        const f64 back = far_ * 0.5;
        const Point3 o = Point3::from(
            e.x.to_f64() + r.x.to_f64() * ndc_x * hw + u.x.to_f64() * ndc_y * hh - f.x.to_f64() * back,
            e.y.to_f64() + r.y.to_f64() * ndc_x * hw + u.y.to_f64() * ndc_y * hh - f.y.to_f64() * back,
            e.z.to_f64() + r.z.to_f64() * ndc_x * hw + u.z.to_f64() * ndc_y * hh - f.z.to_f64() * back);
        return {o, f};
    }

    const f64 t = std::tan(fov_ * 0.5);
    const Vec3 dir = ggeo::d3::normalized(Vec3::from(
        f.x.to_f64() + r.x.to_f64() * ndc_x * t * aspect() + u.x.to_f64() * ndc_y * t,
        f.y.to_f64() + r.y.to_f64() * ndc_x * t * aspect() + u.y.to_f64() * ndc_y * t,
        f.z.to_f64() + r.z.to_f64() * ndc_x * t * aspect() + u.z.to_f64() * ndc_y * t));
    return {eye(), dir};
}

bool Camera::clip_segment(Point3& a,Point3& b) const noexcept {
    if(proj_==Projection::Orthographic) return true;
    const auto e=eye();const auto f=forward();
    const auto depth=[&](const Point3& p) {const auto d=p-e;return (d.x*f.x+d.y*f.y+d.z*f.z).to_f64();};
    const double za=depth(a),zb=depth(b);
    if(za<near_ && zb<near_) return false;
    if(za<near_) a=a+(b-a)*F64::from((near_-za)/(zb-za));
    else if(zb<near_) b=a+(b-a)*F64::from((near_-za)/(zb-za));
    return true;
}

bool Camera::project(Point3 world, f64& px, f64& py) const noexcept {
    return project(view_projection(), world.x.to_f64(), world.y.to_f64(), world.z.to_f64(), px, py);
}

bool Camera::project(const Mat4& vp, f64 x, f64 y, f64 z, f64& px, f64& py) const noexcept {
    const f64 cx = vp.m[0] * x + vp.m[4] * y + vp.m[8]  * z + vp.m[12];
    const f64 cy = vp.m[1] * x + vp.m[5] * y + vp.m[9]  * z + vp.m[13];
    const f64 cw = vp.m[3] * x + vp.m[7] * y + vp.m[11] * z + vp.m[15];
    if (cw <= 1e-12) return false;

    px = ((cx / cw) * 0.5 + 0.5) * vw_;
    py = ((cy / cw) * 0.5 + 0.5) * vh_;
    return true;
}

}
