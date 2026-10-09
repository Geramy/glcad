#include "ui/canvas.hpp"

#include <algorithm>
#include <cmath>

namespace gcapp {
namespace {
constexpr double pi = 3.14159265358979323846;
using XY = std::pair<double, double>;
XY endpoint(const gsnap::Shape &s, bool end, const gcm::WorkingVolume &v) {
    double x, y;
    gcm::to_world(v, s.a, x, y);
    const auto angle = end ? s.end_angle : s.start_angle;
    const double r = s.radius * v.inv_scale();
    return {x + r * std::cos(angle), y + r * std::sin(angle)};
}
double bulge(const gsnap::Shape &s) {
    double sweep = s.end_angle - s.start_angle;
    while (sweep < 0)
        sweep += 2 * pi;
    return std::tan(sweep / 4);
}
bool arc_between(gsnap::Shape &arc, XY a, XY b, double bulge, const gcm::WorkingVolume &v) {
    const double dx = b.first - a.first, dy = b.second - a.second, len = std::hypot(dx, dy);
    if (len < v.inv_scale() * 4 || !std::isfinite(bulge) || bulge <= 1e-5 || bulge > 1)
        return false;
    const double h = len * (1 - bulge * bulge) / (4 * bulge);
    const double cx = (a.first + b.first) * .5 - dy / len * h;
    const double cy = (a.second + b.second) * .5 + dx / len * h;
    arc.kind = gsnap::ShapeKind::Arc;
    arc.a = arc.b = gcm::to_lattice(v, cx, cy);
    arc.radius = gcm::i64(std::llround(len * (1 + bulge * bulge) / (4 * bulge) * v.scale()));
    arc.start_angle = std::atan2(a.second - cy, a.first - cx);
    arc.end_angle = arc.start_angle + 4 * std::atan(bulge);
    return true;
}
}

bool Canvas::is_revision_cloud(std::size_t index) const {
    if (index >= shapes_.size() || !shapes_[index].poly)
        return false;
    const auto found = shapes_[index].attrs.user.find("revision_cloud");
    return found != shapes_[index].attrs.user.end() && found->second == "1";
}

bool Canvas::revision_cloud(Lattice a, Lattice b, double chord, gcm::u32 replace) {
    double x0, y0, x1, y1;
    gcm::to_world(vol_, a, x0, y0);
    gcm::to_world(vol_, b, x1, y1);
    if (x1 < x0)
        std::swap(x0, x1);
    if (y1 < y0)
        std::swap(y0, y1);
    const double w = x1 - x0, h = y1 - y0;
    if (w < vol_.inv_scale() * 8 || h < vol_.inv_scale() * 8 || !std::isfinite(chord) ||
        chord < 0) {
        message_ = "revision cloud: use distinct corners and a positive chord length";
        return false;
    }
    if (chord == 0)
        chord = std::min(w, h) / 6;
    const double nx = std::max(1.0, std::ceil(w / chord)), ny = std::max(1.0, std::ceil(h / chord));
    if (nx + ny > 2048) {
        message_ = "revision cloud: increase chord length (4096 arcs maximum)";
        return false;
    }
    gdoc::Attributes attrs = current_attributes;
    std::string layer = current_layer_;
    gcm::u32 first_key = 0;
    if (replace) {
        const auto found = std::find_if(shapes_.begin(), shapes_.end(),
                                        [&](const auto &s) { return s.poly == replace; });
        if (found == shapes_.end() || !is_revision_cloud(std::size_t(found - shapes_.begin()))) {
            message_ = "select one revision cloud to edit";
            return false;
        }
        attrs = found->attrs;
        layer = found->layer;
        first_key = found->key;
    }
    attrs.user["revision_cloud"] = "1";
    const XY corners[] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    std::vector<gsnap::Shape> arcs;
    for (int edge = 0; edge < 4; ++edge) {
        const auto p = corners[edge], q = corners[(edge + 1) % 4];
        const int count = int(edge % 2 ? ny : nx);
        for (int i = 0; i < count; ++i) {
            const auto point = [&](int n) {
                const double t = double(n) / count;
                return XY{p.first + (q.first - p.first) * t, p.second + (q.second - p.second) * t};
            };
            gsnap::Shape arc;
            arc.layer = layer;
            arc.attrs = attrs;
            arc.poly_index = gcm::u32(arcs.size());
            if (!arc_between(arc, point(i), point(i + 1), .5, vol_)) {
                message_ = "revision cloud is below drawing resolution";
                return false;
            }
            arcs.push_back(std::move(arc));
        }
    }
    mark();
    end_polyline();
    const auto poly = replace ? replace : next_polyline_id();
    shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(),
                                 [&](const auto &s) { return replace && s.poly == replace; }),
                  shapes_.end());
    selection_.clear();
    sel_refs_.clear();
    sel_texts_.clear();
    sel_dims_.clear(); sel_hatches_.clear();
    for (auto &arc : arcs) {
        arc.poly = poly;
        arc.key = arc.poly_index == 0 ? first_key : 0;
        selection_.push_back(shapes_.size());
        push_shape(std::move(arc));
    }
    rebuild_grips();
    refresh_hatches();
    message_ = replace ? "revision cloud updated" : "revision cloud placed";
    return true;
}

bool Canvas::drag_cloud_grip(const gedit::Grip &grip, Lattice to) {
    if (!is_revision_cloud(grip.shape))
        return false;
    const auto pieces = polyline_pieces(grip.shape);
    if (pieces.size() < 2)
        return false;
    auto found = std::find(pieces.begin(), pieces.end(), std::size_t(grip.shape));
    if (found == pieces.end())
        return false;
    const auto pos = std::size_t(found - pieces.begin());
    auto arc = shapes_[grip.shape];
    const auto a = endpoint(arc, false, vol_), b = endpoint(arc, true, vol_);
    double x, y;
    gcm::to_world(vol_, to, x, y);
    const XY p{x, y};
    if (grip.index == 2) {
        const double dx = b.first - a.first, dy = b.second - a.second, len = std::hypot(dx, dy);
        const double sagitta =
            ((x - (a.first + b.first) * .5) * dy - (y - (a.second + b.second) * .5) * dx) / len;
        if (arc_between(arc, a, b, 2 * sagitta / len, vol_))
            shapes_[grip.shape] = arc;
    } else {
        const bool start = grip.index == 1;
        const auto neighbor = pieces[(pos + (start ? pieces.size() - 1 : 1)) % pieces.size()];
        auto other = shapes_[neighbor];
        if (arc_between(arc, start ? p : a, start ? b : p, bulge(arc), vol_) &&
            arc_between(other, start ? endpoint(other, false, vol_) : p,
                        start ? p : endpoint(other, true, vol_), bulge(other), vol_)) {
            shapes_[grip.shape] = arc;
            shapes_[neighbor] = other;
        }
    }
    return true;
}
}
