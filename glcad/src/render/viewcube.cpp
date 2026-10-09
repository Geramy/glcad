#include "render/viewcube.hpp"

#include <algorithm>
#include <cmath>

namespace gview {

constexpr f64 kPi = 3.14159265358979323846;

void CubePart::orientation(f64& az, f64& el) const noexcept {
    const f64 len = std::sqrt(static_cast<f64>(nx * nx + ny * ny + nz * nz));
    if (len < 1e-12) { az = 0.0; el = 0.0; return; }
    const f64 ux = nx / len, uy = ny / len, uz = nz / len;

    el = std::asin(std::clamp(uz, -1.0, 1.0));
    if (std::fabs(uz) > 0.999999) { az = -kPi * 0.5; return; }
    az = std::atan2(uy, ux);
}

const std::vector<CubePart>& ViewCube::parts() {
    static const std::vector<CubePart> p = [] {
        std::vector<CubePart> v;
        v.push_back({CubeRegion::Face,  1,  0,  0, "RIGHT"});
        v.push_back({CubeRegion::Face, -1,  0,  0, "LEFT"});
        v.push_back({CubeRegion::Face,  0,  1,  0, "BACK"});
        v.push_back({CubeRegion::Face,  0, -1,  0, "FRONT"});
        v.push_back({CubeRegion::Face,  0,  0,  1, "TOP"});
        v.push_back({CubeRegion::Face,  0,  0, -1, "BOTTOM"});
        for (int a : {-1, 1})
            for (int b : {-1, 1}) {
                v.push_back({CubeRegion::Edge, a, b, 0, ""});
                v.push_back({CubeRegion::Edge, a, 0, b, ""});
                v.push_back({CubeRegion::Edge, 0, a, b, ""});
            }
        for (int x : {-1, 1})
            for (int y : {-1, 1})
                for (int z : {-1, 1})
                    v.push_back({CubeRegion::Corner, x, y, z, ""});
        return v;
    }();
    return p;
}

bool ViewCube::facing_camera(const Camera& cam, const CubePart& part) {
    const Vec3 f = cam.forward();
    const f64 len = std::sqrt(static_cast<f64>(part.nx * part.nx +
                                               part.ny * part.ny +
                                               part.nz * part.nz));
    if (len < 1e-12) return false;
    const f64 d = (f.x.to_f64() * part.nx + f.y.to_f64() * part.ny +
                   f.z.to_f64() * part.nz) / len;
    return d < -1e-6;
}

bool ViewCube::part_screen_position(const Camera& cam, const CubeLayout& layout,
                                    const CubePart& part, f64& px, f64& py) {
    if (!facing_camera(cam, part)) return false;

    const Vec3 r = cam.right(), u = cam.up();
    const f64 sx = part.nx * r.x.to_f64() + part.ny * r.y.to_f64() + part.nz * r.z.to_f64();
    const f64 sy = part.nx * u.x.to_f64() + part.ny * u.y.to_f64() + part.nz * u.z.to_f64();

    const f64 half = layout.size * 0.5;
    const f64 scale = half * 0.62;          // leaves room for the cube's corners
    px = layout.x + half + sx * scale;
    py = layout.y + half - sy * scale;      // screen y grows downward
    return true;
}

CubePart ViewCube::pick(const Camera& cam, const CubeLayout& layout,
                        f64 px, f64 py) {
    CubePart best;
    if (!layout.contains(px, py)) return best;

    f64 best_d = 1e300;
    for (const auto& part : parts()) {
        f64 cx, cy;
        if (!part_screen_position(cam, layout, part, cx, cy)) continue;

        f64 radius = layout.size * 0.18;
        if (part.region == CubeRegion::Edge) radius = layout.size * 0.11;
        if (part.region == CubeRegion::Corner) radius = layout.size * 0.09;

        const f64 d = std::hypot(px - cx, py - cy);
        if (d <= radius && d < best_d) { best_d = d; best = part; }
    }
    return best;
}

void ViewCube::apply(Camera& cam, const CubePart& part) {
    if (!part.valid()) return;
    f64 az, el;
    part.orientation(az, el);
    cam.set_orbit(az, el);
}

std::string ViewCube::top_label(const Camera& cam) {
    const Vec3 u = cam.up();
    const CubePart* best = nullptr;
    f64 best_dot = -2.0;
    for (const auto& p : parts()) {
        if (p.region != CubeRegion::Face) continue;
        const f64 d = u.x.to_f64() * p.nx + u.y.to_f64() * p.ny + u.z.to_f64() * p.nz;
        if (d > best_dot) { best_dot = d; best = &p; }
    }
    return best ? best->label : std::string{};
}


bool ViewCube::begin_drag(const Camera& cam, const CubeLayout& layout, f64 px,
                          f64 py, Drag& out) {
    if (px < layout.x || px > layout.x + layout.size || py < layout.y ||
        py > layout.y + layout.size)
        return false;

    out = Drag{};
    out.active = true;
    out.start_x = px;
    out.start_y = py;
    out.start_azimuth = cam.azimuth();
    out.start_elevation = cam.elevation();
    return true;
}

void ViewCube::update_drag(Camera& cam, Drag& d, f64 px, f64 py) {
    if (!d.active) return;

    const f64 dx = px - d.start_x;
    const f64 dy = py - d.start_y;
    d.travelled = std::max(d.travelled, std::hypot(dx, dy));

    constexpr f64 kRadiansPerPixel = 0.008726646259971648;   // half a degree
    cam.set_orbit(d.start_azimuth - dx * kRadiansPerPixel,
                  d.start_elevation + dy * kRadiansPerPixel);
}

bool ViewCube::end_drag(Drag& d, f64 click_threshold_px) {
    const bool was_click = d.active && d.travelled <= click_threshold_px;
    d = Drag{};
    return was_click;
}

}
