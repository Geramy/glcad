#include "engine/underlay.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gdoc {

const char* underlay_kind_name(UnderlayKind k) noexcept {
    switch (k) {
        case UnderlayKind::Pdf:     return "PDF";
        case UnderlayKind::Dwf:     return "DWF";
        case UnderlayKind::Dgn:     return "DGN";
        case UnderlayKind::Raster:  return "raster";
        case UnderlayKind::Unknown: return "unknown";
    }
    return "unknown";
}

UnderlayKind underlay_kind_of(std::string_view path) noexcept {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos) return UnderlayKind::Unknown;

    std::string ext;
    for (std::size_t i = dot + 1; i < path.size(); ++i)
        ext.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(path[i]))));

    if (ext == "pdf") return UnderlayKind::Pdf;
    if (ext == "dwf" || ext == "dwfx") return UnderlayKind::Dwf;
    if (ext == "dgn") return UnderlayKind::Dgn;
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tif" ||
        ext == "tiff" || ext == "bmp" || ext == "gif" || ext == "webp")
        return UnderlayKind::Raster;
    return UnderlayKind::Unknown;
}

bool Underlay::contains(Point2 p) const {
    if (!has_clip()) return true;

    const auto inside=[&](const std::vector<Point2>& boundary) {
        bool result=false;
        for(std::size_t i=0,j=boundary.size()-1;i<boundary.size();j=i++) {
            const long double xi=boundary[i].x,yi=boundary[i].y,xj=boundary[j].x,yj=boundary[j].y;
            if((yi>p.y)!=(yj>p.y)&&p.x<(xj-xi)*(p.y-yi)/(yj-yi)+xi)result=!result;
        }
        return result;
    };
    const bool retained=inside(clip)!=clip_inverted;
    return retained&&(clip_hole.size()<3||!inside(clip_hole));
}

void Underlay::to_drawing(Point2 p, const gcm::WorkingVolume& v, f64& x,
                          f64& y) const {
    f64 ux, uy, ix, iy;
    gcm::to_world(v, p, ux, uy);
    gcm::to_world(v, insertion, ix, iy);

    const f64 c = std::cos(rotation), s = std::sin(rotation);
    const f64 sx = ux * scale, sy = uy * scale * y_factor;
    x = ix + sx * c - sy * s;
    y = iy + sx * s + sy * c;
}

void Underlay::to_underlay(f64 x, f64 y, const gcm::WorkingVolume& v,
                           Point2& out) const {
    f64 ix, iy;
    gcm::to_world(v, insertion, ix, iy);

    const f64 c = std::cos(-rotation), s = std::sin(-rotation);
    const f64 dx = x - ix, dy = y - iy;
    const f64 rx = dx * c - dy * s, ry = dx * s + dy * c;
    const f64 k = scale != 0.0 ? 1.0 / scale : 1.0;
    out = gcm::to_lattice(v, rx * k, ry * k / (y_factor != 0 ? y_factor : 1));
}

Underlay& UnderlayTable::attach(Underlay u) {
    if (u.kind == UnderlayKind::Unknown) u.kind = underlay_kind_of(u.path);
    if (u.name.empty()) {
        const std::size_t slash = u.path.find_last_of("/\\");
        u.name = slash == std::string::npos ? u.path : u.path.substr(slash + 1);
    }

    for (auto& e : items_)
        if (e.name == u.name) {
            e = std::move(u);
            return e;
        }
    items_.push_back(std::move(u));
    return items_.back();
}

const Underlay* UnderlayTable::find(std::string_view name) const {
    for (const auto& u : items_)
        if (u.name == name) return &u;
    return nullptr;
}

Underlay* UnderlayTable::find(std::string_view name) {
    for (auto& u : items_)
        if (u.name == name) return &u;
    return nullptr;
}

bool UnderlayTable::detach(std::string_view name) {
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [&](const Underlay& u) { return u.name == name; });
    if (it == items_.end()) return false;
    items_.erase(it);
    return true;
}

std::vector<const Underlay*> UnderlayTable::draw_order() const {
    std::vector<const Underlay*> out;
    for (const auto& u : items_)
        if (u.visible) out.push_back(&u);
    return out;
}

bool UnderlayTable::calibrate(std::string_view name, Point2 a, Point2 b,
                              f64 real_distance, const gcm::WorkingVolume& v) {
    Underlay* u = find(name);
    if (!u || real_distance <= 0.0) return false;

    f64 ax, ay, bx, by;
    gcm::to_world(v, a, ax, ay);
    gcm::to_world(v, b, bx, by);
    const f64 measured = std::hypot(bx - ax, (by - ay)*u->y_factor);
    if (measured < 1e-12) return false;

    u->scale = real_distance / measured;
    return true;
}

}
