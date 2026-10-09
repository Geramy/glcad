#include "ui/canvas.hpp"
#include "geometry/triangulate.hpp"
#include "math/parallel.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include <map>

#include "engine/coordinate.hpp"
#include "geometry/edit.hpp"

namespace gcapp {

using grender::Role;

void View::pan(double dx, double dy) noexcept {
    ox_ -= dx / scale_;
    oy_ += dy / scale_;
}

void View::zoom_at(double px, double py, double factor) noexcept {
    double wx, wy;
    world_of(px, py, wx, wy);
    scale_ *= factor;
    if (scale_ < 1e-3) scale_ = 1e-3;
    if (scale_ > 1e7) scale_ = 1e7;
    double nx, ny;
    world_of(px, py, nx, ny);
    ox_ += wx - nx;
    oy_ += wy - ny;
}

void View::world_of(double px, double py, double& wx, double& wy) const noexcept {
    wx = ox_ + (px - vw_ * 0.5) / scale_;
    wy = oy_ - (py - vh_ * 0.5) / scale_;
}

void View::screen_of(double wx, double wy, double& px, double& py) const noexcept {
    px = (wx - ox_) * scale_ + vw_ * 0.5;
    py = (oy_ - wy) * scale_ + vh_ * 0.5;
}

Canvas::Canvas() {
    vol_.extent          = 500.0;   // 1 km working volume
    vol_.resolution_bits = 32;      // ~0.23 um lattice
}

void Canvas::set_tool(Tool t) noexcept {
    tool_    = t;
    pending_ = false;
    end_polyline();
    arc_pts_.clear();
    arc_pt_refs_.clear();
    picked_.clear();
    chain_.clear();
    spline_pts_.clear();
    mline_pts_.clear();
    mline_first_ = shapes_.size();
    message_.clear();
}

std::vector<double> Polar::angles() const {
    std::vector<double> a;
    if (ortho) {
        a = {0.0, 90.0, 180.0, 270.0};
    } else {
        const double inc = increment_deg > 0.0 ? increment_deg : 90.0;
        for (double v = 0.0; v < 360.0 - 1e-9; v += inc) a.push_back(v);
    }
    for (double e : extra) a.push_back(e);
    return a;
}

void Canvas::cycle_polar_increment() noexcept {
    static const double steps[] = {90.0, 45.0, 30.0, 22.5, 15.0, 10.0, 5.0};
    const int n = static_cast<int>(sizeof(steps) / sizeof(steps[0]));
    for (int i = 0; i < n; ++i)
        if (std::fabs(polar_.increment_deg - steps[i]) < 1e-9) {
            polar_.increment_deg = steps[(i + 1) % n];
            return;
        }
    polar_.increment_deg = 45.0;
}

const char* tool_name(Tool t) noexcept {
    switch (t) {
        case Tool::Line:        return "LINE";
        case Tool::Rect:        return "RECTANG";
        case Tool::Circle:      return "CIRCLE";
        case Tool::Arc:         return "ARC";
        case Tool::Polyline:    return "PLINE";
        case Tool::Ellipse:     return "ELLIPSE";
        case Tool::Point:       return "POINT";
        case Tool::Ray:         return "RAY";
        case Tool::Xline:       return "XLINE";
        case Tool::Spline:      return "SPLINE";
        case Tool::Multiline:   return "MLINE";
        case Tool::Move:        return "MOVE";
        case Tool::Copy:        return "COPY";
        case Tool::Rotate:      return "ROTATE";
        case Tool::Mirror:      return "MIRROR";
        case Tool::Offset:      return "OFFSET";
        case Tool::Trim:        return "TRIM";
        case Tool::Extend:      return "EXTEND";
        case Tool::Fillet:      return "FILLET";
        case Tool::Chamfer:     return "CHAMFER";
        case Tool::ArrayRect:   return "ARRAYRECT";
        case Tool::ArrayPolar:  return "ARRAYPOLAR";
        case Tool::DimLinear:   return "DIMLINEAR";
        case Tool::DimAligned:  return "DIMALIGNED";
        case Tool::DimAngular:  return "DIMANGULAR";
        case Tool::DimRadial:   return "DIMRADIUS";
        case Tool::DimDiameter: return "DIMDIAMETER";
        default:                return "SELECT";
    }
}

bool tool_uses_selection(Tool t) noexcept {
    switch (t) {
        case Tool::Move: case Tool::Copy: case Tool::Rotate:
        case Tool::Mirror: case Tool::ArrayRect: case Tool::ArrayPolar:
            return true;
        default:
            return false;
    }
}

bool tool_is_dimension(Tool t) noexcept {
    switch (t) {
        case Tool::DimLinear: case Tool::DimAligned: case Tool::DimAngular:
        case Tool::DimRadial: case Tool::DimDiameter:
            return true;
        default:
            return false;
    }
}

static double distance_to_axis(const gcm::WorkingVolume& v, Lattice centre,
                               Lattice major, Lattice p) {
    double cx, cy, mx, my, px, py;
    gcm::to_world(v, centre, cx, cy);
    gcm::to_world(v, major, mx, my);
    gcm::to_world(v, p, px, py);
    const double ax = mx - cx, ay = my - cy;
    const double len = std::hypot(ax, ay);
    if (len < 1e-12) return 0.0;
    return std::fabs((px - cx) * ay - (py - cy) * ax) / len;
}

void Canvas::add_text(Lattice pos, std::string text, double height, double angle,
                      int halign, int valign, double width, std::string style, std::string layer, gdoc::Attributes attrs) {
    if (text.empty()) return;
    mark();
    texts_.push_back({pos, std::move(text), height, angle, halign, valign, width,
                      std::move(style)});
    texts_.back().layer=layer.empty() ? current_layer_ : std::move(layer);
    texts_.back().attrs=std::move(attrs);
    ++texts_serial_;
}

void Canvas::load_shapes(std::vector<gsnap::Shape> shapes, bool clear_history) {
    sel_hatches_.clear();
    previous_selection_.clear(); cycle_candidates_.clear(); shape_selection_ids_.clear();
    cancel();
    selection_.clear();
    sel_refs_.clear();
    if (clear_history) { undo_stack_.clear(); redo_stack_.clear(); }
    shapes_ = std::move(shapes);
    note_polyline_ids();
    assign_keys();
    rebuild_grips();
    refresh_hatches();
}

void Canvas::mark() {
    remember_selection();
    align_dirty_ = true;
    ++edit_serial_;
    ++undo_mark_serial_;
    undo_stack_.push_back({shapes_, dims_, selection_, blocks_.references(), sel_refs_, texts_, sel_texts_, hatches_, splines_, constraints_, sel_dims_, sel_hatches_});
    redo_stack_.clear();
    if (undo_stack_.size() > 256) undo_stack_.erase(undo_stack_.begin());
}

void Canvas::add_segment(Lattice a, Lattice b) {
    if (a.x == b.x && a.y == b.y) return;
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = a;
    s.b = b;
    push_shape(s);
}

void Canvas::add_circle(Lattice centre, gcm::i64 radius) {
    if (radius <= 0) return;
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Circle;
    s.a = centre;
    s.radius = radius;
    push_shape(s);
}

void Canvas::add_arc(Lattice centre, gcm::i64 radius, double a0, double a1) {
    if (radius <= 0) return;
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Arc;
    s.a = centre;
    s.radius = radius;
    s.start_angle = a0;
    s.end_angle = a1;
    push_shape(s);
}

void Canvas::set_hidden_layers(std::vector<std::string> names) {
    if (names == hidden_layers_) return;
    hidden_layers_ = std::move(names);
    align_dirty_ = true;
    snap_engine_.release_all();
    snap_engine_.release_parallel();
    clear_hover();
    std::erase_if(selection_, [&](auto i) { return i >= shapes_.size() || layer_hidden(shapes_[i].layer); });
    std::erase_if(sel_dims_, [&](auto i) { return i >= dims_.size() || layer_hidden(dims_[i].layer); });
    std::erase_if(sel_texts_, [&](auto i) { return i >= texts_.size() || layer_hidden(texts_[i].layer); });
    std::erase_if(sel_refs_, [&](auto i) { return i >= blocks_.references().size() || layer_hidden(blocks_.references()[i].layer); });
    rebuild_grips();
}

void Canvas::set_layer_transparencies(std::map<std::string,int> values) {
    if(values==layer_transparencies_)return;
    layer_transparencies_=std::move(values);++style_serial_;align_dirty_=true;
    snap_engine_.release_all();snap_engine_.release_parallel();clear_hover();
    std::erase_if(selection_,[&](auto i){return i>=shapes_.size() || !shape_visible(shapes_[i]);});
    rebuild_grips();
}

void Canvas::set_appearance_layers(const gdoc::LayerTable& layers) {
    if(appearance_layers_.all()==layers.all())return;
    appearance_layers_=layers;++style_serial_;align_dirty_=true;
    snap_engine_.release_all();snap_engine_.release_parallel();clear_hover();
    std::erase_if(sel_refs_,[&](auto i) {
        if(i>=blocks_.references().size())return true;
        const auto geometry=blocks_.expand(blocks_.references()[i],vol_,8,&appearance_layers_);
        return reference_labels(blocks_.references()[i]).empty() &&
            std::none_of(geometry.begin(),geometry.end(),[&](const auto& shape){return shape_visible(shape);});
    });
    rebuild_grips();
}

void Canvas::set_underlay_snap_shapes(std::vector<gsnap::Shape> shapes) {
    const bool same = shapes.size() == underlay_snap_shapes_.size() &&
        std::equal(shapes.begin(), shapes.end(), underlay_snap_shapes_.begin(),
            [](const auto& a, const auto& b) {
                return a.kind == b.kind && a.a.x == b.a.x && a.a.y == b.a.y &&
                       a.b.x == b.b.x && a.b.y == b.b.y;
            });
    if (same) return;
    underlay_snap_shapes_ = std::move(shapes);
    ++underlay_snap_serial_;
    snap_engine_.release_all();
    snap_engine_.release_parallel();
    clear_hover();
}

void Canvas::sync_snap_engine() const {
    gcm::u64 key = edit_serial_ * 1000003ull + chain_.size() * 131ull + (pending_ ? 7ull : 0ull);
    if (pending_) key ^= static_cast<gcm::u64>(anchor_.x) * 0x9E3779B97F4A7C15ull ^ static_cast<gcm::u64>(anchor_.y);
    key ^= static_cast<gcm::u64>(blocks_.references().size()) * 0x2545F4914F6CDD1Dull;
    key ^= block_geometry_key();
    key ^= static_cast<gcm::u64>(splines_.size()) << 33;
    key ^= static_cast<gcm::u64>(hatches_.size()) << 47;
    key ^= underlay_snap_serial_ * 0xD6E8FEB86659FD93ull;
    key ^= style_serial_ * 0xA0761D6478BD642Full;
    key ^= static_cast<gcm::u64>(std::llround(spline_tolerance_ * 1e7)) << 5;
    for (const auto& layer : hidden_layers_)
        for (const unsigned char c : layer) key = (key * 131ull) ^ c;
    if (key != snap_sync_key_) {
        snap_sync_key_ = key;
        snap_engine_.clear();
        snap_shape_indices_.clear();
        for (std::size_t i = 0; i < shapes_.size(); ++i) {
            if (!shape_visible(shapes_[i])) continue;
            snap_engine_.add(shapes_[i]);
            snap_shape_indices_.push_back(i);
        }

        for (const auto& ref : blocks_.references()) {
            if (layer_hidden(ref.layer)) continue;
            for (const auto& sh : blocks_.expand(ref, vol_, 8, &appearance_layers_))
                if (shape_visible(sh)) snap_engine_.add(sh);
        }

        for (const auto& shape : underlay_snap_shapes_) snap_engine_.add(shape);

        for (const auto& c : splines_) {
            const auto pts = c.tessellate(spline_tolerance_);
            for (std::size_t i = 1; i < pts.size(); ++i) {
                gsnap::Shape seg;
                seg.kind = gsnap::ShapeKind::Segment;
                seg.a = gcm::to_lattice(vol_, pts[i - 1].x.to_f64(), pts[i - 1].y.to_f64());
                seg.b = gcm::to_lattice(vol_, pts[i].x.to_f64(), pts[i].y.to_f64());
                snap_engine_.add(seg);
            }
        }

        for (const auto& d : dims_) {
            if (layer_hidden(d.layer)) continue;
            const auto point = [&](gcm::Lattice2 at) {
                gsnap::Shape p2;
                p2.kind = gsnap::ShapeKind::Point;
                p2.a = at;
                p2.layer = d.layer;
                snap_engine_.add(p2);
            };
            if(d.kind==gdim::Kind::Leader)for(auto grip:d.leader_grips())point(grip);
            else {point(d.p1);point(d.p2);}
            if (d.kind == gdim::Kind::Radial || d.kind == gdim::Kind::Diameter ||
                d.kind == gdim::Kind::Angular)
                point(d.centre);
        }
        if (pending_) {
            gsnap::Shape p;
            p.kind = gsnap::ShapeKind::Point;
            p.a = anchor_;
            snap_engine_.add(p);
        }
        for (const auto& c : chain_) {
            gsnap::Shape p;
            p.kind = gsnap::ShapeKind::Point;
            p.a = c;
            snap_engine_.add(p);
        }
    }
    snap_engine_.settings.scale = view_.scale();

}

Snap Canvas::find_grid_snap(double wx, double wy) const {
    if (!grid_snap_) return Snap{};
    const double radius = 12.0 / view_.scale();
    const double target = 64.0 / view_.scale();
    const double dec    = grid_spacing_ > 0.0
                              ? grid_spacing_
                              : std::pow(10.0, std::floor(std::log10(target)));
    const double c=std::cos(grid_angle),s=std::sin(grid_angle);
    const double u=std::round((c*wx+s*wy)/dec)*dec,v=std::round((-s*wx+c*wy)/dec)*dec;
    const double gx=c*u-s*v,gy=s*u+c*v;
    if (std::hypot(gx - wx, gy - wy) < radius * 0.7)
        return {true, gcm::to_lattice(vol_, gx, gy), "grid"};
    return {};
}

void Canvas::rebuild_align_points() {
    align_points_.clear();
    align_dirty_ = false;

    for (const gsnap::Shape& s : shapes_) {
        if (layer_hidden(s.layer)) continue;
        switch (s.kind) {
            case gsnap::ShapeKind::Segment:
                align_points_.push_back(s.a);
                align_points_.push_back(s.b);
                align_points_.push_back(Lattice{(s.a.x + s.b.x) / 2,
                                                (s.a.y + s.b.y) / 2});
                break;
            case gsnap::ShapeKind::Circle:
            case gsnap::ShapeKind::Arc:
            case gsnap::ShapeKind::Ellipse:
                align_points_.push_back(s.a);
                break;
            case gsnap::ShapeKind::Point:
                align_points_.push_back(s.a);
                break;
            default:
                break;
        }
    }
    if (pending_) align_points_.push_back(anchor_);
}

bool Canvas::find_alignment(double wx, double wy, Lattice& out) {
    align_ = Alignment{};
    if (align_dirty_) rebuild_align_points();
    if (align_points_.empty()) return false;

    const double tolerance = 8.0 / view_.scale();
    const double reach = 400.0 / view_.scale();

    double best_dx = tolerance, best_dy = tolerance;
    bool found_x = false, found_y = false;
    Lattice src_x{}, src_y{};

    for (const Lattice& p : align_points_) {
        double px, py;
        gcm::to_world(vol_, p, px, py);

        const double dx = std::fabs(px - wx);
        const double dy = std::fabs(py - wy);
        if (dx < best_dx && dy <= reach) { best_dx = dx; found_x = true; src_x = p; }
        if (dy < best_dy && dx <= reach) { best_dy = dy; found_y = true; src_y = p; }
    }
    if (!found_x && !found_y) return false;

    double sx = wx, sy = wy;
    double ignore = 0.0;
    if (found_x) {
        gcm::to_world(vol_, src_x, sx, ignore);
        align_.vertical = true;
        align_.source_v = src_x;
    }
    if (found_y) {
        gcm::to_world(vol_, src_y, ignore, sy);
        align_.horizontal = true;
        align_.source_h = src_y;
    }
    align_.active = true;
    out = gcm::to_lattice(vol_, sx, sy);
    return true;
}

void Canvas::acquire_track() {
    if (snap_.valid)
        snap_engine_.acquire({true, snap_.point, gsnap::Endpoint, 0, 0, 0.0}, 0.0);
}

void Canvas::hover(double px, double py) {
    double wx, wy;
    view_.world_of(px, py, wx, wy);

    track_ = Track{};
    align_ = Alignment{};
    hover_shape_ = npos;

    if (typed_exact_) {
        snap_ = {true, cursor_, "typed"};
        return;
    }

    if (tool_ == Tool::Select && !pending_ && !dragging_selection_) {
        snap_ = Snap{};
        snap_key_ = 0;
        snap_anchor_ = gdim::Dimension::Anchor::None;
        cursor_ = gcm::to_lattice(vol_, wx, wy);
        return;
    }

    sync_snap_engine();
    const auto hit = snap_engine_.snap_from(
        vol_, wx, wy, pending_ ? std::optional<Lattice>(anchor_) : std::nullopt);
    if (hit.valid) {
        if (pending_ && snaps().settings.wants(gsnap::Parallel) &&
            (hit.kind == gsnap::Nearest || hit.kind == gsnap::Endpoint ||
             hit.kind == gsnap::Midpoint))
            snap_engine_.acquire_parallel(hit.shape_a);

        snap_key_ = 0;
        snap_anchor_ = gdim::Dimension::Anchor::None;
        if (hit.shape_a > 0 && hit.shape_a <= snap_shape_indices_.size()) {
            hover_shape_ = snap_shape_indices_[hit.shape_a - 1];
            const gsnap::Shape& src = shapes_[snap_shape_indices_[hit.shape_a - 1]];
            if (hit.kind == gsnap::Endpoint) {
                gcm::Lattice2 start{},end{};
                const bool has_start=gdim::Dimension::anchor_point(src,gdim::Dimension::Anchor::Start,start);
                const bool has_end=gdim::Dimension::anchor_point(src,gdim::Dimension::Anchor::End,end);
                const auto distance=[&](auto p){return std::hypot(double(hit.point.x)-p.x,double(hit.point.y)-p.y);};
                if(has_start || has_end) {
                    snap_anchor_=has_start && (!has_end || distance(start)<=distance(end))
                        ?gdim::Dimension::Anchor::Start:gdim::Dimension::Anchor::End;
                    snap_key_=src.key;
                }
            } else if (hit.kind == gsnap::Center && src.is_curve()) {
                snap_anchor_ = gdim::Dimension::Anchor::Centre;
                snap_key_ = src.key;
            }
        }
        snap_ = {true, hit.point, hit.name()};
        cursor_ = hit.point;
        return;
    }

    if (pending_) {
        const auto par = snap_engine_.snap_parallel(vol_, wx, wy, anchor_);
        if (par.valid) {
            snap_ = {true, par.point, par.name()};
            cursor_ = par.point;
            return;
        }
    }

    const auto tracked = snap_engine_.track(vol_, wx, wy);
    if (tracked.valid) {
        snap_ = {true, tracked.point, "tracking"};
        cursor_ = tracked.point;
        return;
    }

    if (pending_ && polar_.enabled) {
        double ax, ay;
        gcm::to_world(vol_, anchor_, ax, ay);
        const double dx = wx - ax, dy = wy - ay;
        const double len = std::hypot(dx, dy);

        if (len > 1e-12) {
            double raw = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            if (raw < 0.0) raw += 360.0;

            double best = 0.0, best_delta = 1e300;
            for (double a : polar_.angles()) {
                double d = std::fabs(raw - a);
                if (d > 180.0) d = 360.0 - d;
                if (d < best_delta) { best_delta = d; best = a; }
            }

            if (best_delta <= polar_.tolerance_deg) {
                const double rad = best * 3.14159265358979323846 / 180.0;
                const double ux = std::cos(rad), uy = std::sin(rad);
                double proj = dx * ux + dy * uy;
                if (proj < 0.0) proj = 0.0;

                const double target = 64.0 / view_.scale();
                const double dec = std::pow(10.0, std::floor(std::log10(target)));
                double use = proj;
                double best_t = 1e300;
                const auto consider = [&](double t) {
                    if (t >= 0.0 && std::fabs(t - proj) < std::fabs(best_t - proj)) best_t = t;
                };
                if (std::fabs(ux) > 1e-12)
                    consider((std::round((ax + ux * proj) / dec) * dec - ax) / ux);
                if (std::fabs(uy) > 1e-12)
                    consider((std::round((ay + uy * proj) / dec) * dec - ay) / uy);
                const double spacing = dec / std::max(std::fabs(ux), std::fabs(uy));
                if (best_t < 1e299 && std::fabs(best_t - proj) < spacing * 0.45) use = best_t;

                const double px_w = ax + ux * use;
                const double py_w = ay + uy * use;

                cursor_ = gcm::to_lattice(vol_, px_w, py_w);
                snap_   = {true, cursor_, "polar"};

                track_.active    = true;
                track_.angle_deg = best;
                track_.length    = use;
                const double guide = use + 40.0 / view_.scale();
                track_.gx = ax + ux * guide;
                track_.gy = ay + uy * guide;
                return;
            }
        }
    }

    Lattice aligned{};
    if (find_alignment(wx, wy, aligned)) {
        cursor_ = aligned;
        snap_ = {true, cursor_,
                 align_.vertical && align_.horizontal ? "align x+y"
                 : align_.vertical                    ? "align x"
                                                      : "align y"};
        return;
    }

    snap_ = find_grid_snap(wx, wy);
    cursor_ = snap_.valid ? snap_.point : gcm::to_lattice(vol_, wx, wy);
}

void Canvas::place(Lattice p) {
    if (!pending_) { anchor_ = p; pending_ = true; return; }
    mark();
    add_segment(anchor_, p);
    anchor_ = p;
}

static double angle_of(const gcm::WorkingVolume& v, Lattice centre, Lattice p) {
    double cx, cy, px, py;
    gcm::to_world(v, centre, cx, cy);
    gcm::to_world(v, p, px, py);
    return std::atan2(py - cy, px - cx);
}

static bool circumcircle(Lattice a, Lattice b, Lattice c, Lattice& centre, gcm::i64& radius) {
    const double ax = double(a.x), ay = double(a.y), bx = double(b.x), by = double(b.y), cx = double(c.x), cy = double(c.y);
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::fabs(d) < 1e-9) return false;
    const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    const double ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    const double uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    centre = {gcm::i64(std::llround(ux)), gcm::i64(std::llround(uy))};
    radius = gcm::i64(std::llround(std::hypot(ax - ux, ay - uy)));
    return radius > 0;
}

static gcm::i64 radius_of(Lattice centre, Lattice p) {
    const double dx = double(p.x - centre.x), dy = double(p.y - centre.y);
    return static_cast<gcm::i64>(std::llround(std::hypot(dx, dy)));
}

std::size_t Canvas::pick(double px, double py) const {
    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const Lattice at = gcm::to_lattice(vol_, wx, wy);
    const double tol = pickbox_pixels / view_.scale();      // world units

    std::size_t best = npos;
    double best_d = 1e300;
    for (std::size_t i = 0; i < shapes_.size(); ++i) {
        if (!shape_visible(shapes_[i]) || is_joined_wall(shapes_[i])) continue;
        if (!gedit::point_on_shape(shapes_[i], at, vol_, tol)) continue;
        double d = 0.0;
        if (shapes_[i].kind == gsnap::ShapeKind::Segment) {
            double ax, ay, bx, by;
            gcm::to_world(vol_, shapes_[i].a, ax, ay);
            gcm::to_world(vol_, shapes_[i].b, bx, by);
            const double ex = bx - ax, ey = by - ay;
            const double len2 = ex * ex + ey * ey;
            double t = len2 > 0.0 ? ((wx - ax) * ex + (wy - ay) * ey) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            d = std::hypot(wx - (ax + ex * t), wy - (ay + ey * t));
        } else {
            double cx, cy;
            gcm::to_world(vol_, shapes_[i].a, cx, cy);
            const double r = double(shapes_[i].radius) * vol_.inv_scale();
            d = std::fabs(std::hypot(wx - cx, wy - cy) - r);
        }
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

bool Canvas::is_selected(std::size_t i) const noexcept {
    return std::find(selection_.begin(), selection_.end(), i) != selection_.end();
}

std::vector<std::array<double,4>> Canvas::label_strokes(const Label& lab, const gcm::WorkingVolume& volume) {
    std::vector<std::array<double,4>> result;
    if(!lab.stroke_font) return result;
    const auto strokes=lab.stroke_font->stroke_text(lab.text);
    const double w=lab.stroke_font->text_width(lab.text)*lab.height;
    const double ox=lab.halign==1?-w*.5:lab.halign==2?-w:0;
    const double oy=lab.valign==2?-lab.height*.5:lab.valign==3?-lab.height:0;
    const double c=std::cos(lab.angle),s=std::sin(lab.angle),shear=std::tan(lab.oblique*3.141592653589793/180);
    auto point=[&](gtext::StrokePoint p) {
        double x=p.x*lab.height+ox,y=p.y*lab.height+oy;
        x=lab.width_factor*(x+shear*y);y*=lab.y_factor;
        return std::pair{lab.x+c*x-s*y,lab.y+s*x+c*y};
    };
    for(const auto& line:strokes) for(std::size_t i=1;i<line.size();++i) {
        auto a=point(line[i-1]),b=point(line[i]); result.push_back({a.first,a.second,b.first,b.second});
    }
    if(!lab.clips.empty()) {
        std::vector<gsnap::Shape> shapes;
        for(const auto& line:result) {
            gsnap::Shape sh;sh.a=gcm::to_lattice(volume,line[0],line[1]);sh.b=gcm::to_lattice(volume,line[2],line[3]);shapes.push_back(sh);
        }
        for(const auto& clip:lab.clips) {
            auto clipped=gclip::shapes(shapes,clip.points,clip.inverted);
            if(!clipped.ok)return {};
            shapes=std::move(clipped.shapes);
        }
        result.clear();
        for(const auto& sh:shapes) {std::array<double,4> line;gcm::to_world(volume,sh.a,line[0],line[1]);gcm::to_world(volume,sh.b,line[2],line[3]);result.push_back(line);}
    }
    return result;
}

std::vector<Canvas::Label> Canvas::note_labels(const TextItem& t, double annotation_scale) const {
    double x,y; gcm::to_world(vol_,t.pos,x,y);
    std::vector<Label> result; emit_note(t,x,y,result,annotation_scale); return result;
}

std::vector<Canvas::Label> Canvas::reference_labels(const gdoc::BlockRef& ref, double annotation_scale) const {
    std::vector<Label> result;
    if (layer_hidden(ref.layer)) return result;
    for (const auto& instance:blocks_.note_instances(ref,vol_,8,&appearance_layers_)) {
        const auto& note=instance.note;
        gsnap::Shape appearance; appearance.layer=note.layer; appearance.attrs=note.attrs;
        if (!shape_visible(appearance)) continue;
        TextItem text{note.pos,note.text,note.height,note.angle,note.halign,note.valign,note.width,note.style};
        text.attrs=note.attrs;
        const auto& m=instance.transform;
        for (auto label:note_labels(text,annotation_scale)) {
            const auto x=label.x,y=label.y;
            label.x=m[0]*x+m[1]*y+m[2]; label.y=m[3]*x+m[4]*y+m[5];
            const auto c=std::cos(label.angle),s=std::sin(label.angle);
            const auto shear=std::tan(label.oblique*3.141592653589793/180);
            const auto ax=c*label.width_factor,ay=s*label.width_factor;
            const auto bx=ax*shear-s*label.y_factor,by=ay*shear+c*label.y_factor;
            const auto ux=m[0]*ax+m[1]*ay,uy=m[3]*ax+m[4]*ay;
            const auto vx=m[0]*bx+m[1]*by,vy=m[3]*bx+m[4]*by;
            const auto width=std::hypot(ux,uy);
            if (!(width>1e-15) || !std::isfinite(width)) continue;
            label.angle=std::atan2(uy,ux); label.width_factor=width;
            label.y_factor=(ux*vy-uy*vx)/width;
            label.oblique=std::atan((ux*vx+uy*vy)/(width*width))*180/3.141592653589793;
            label.layer=note.layer; label.attrs=note.attrs;label.clips=instance.clips;
            if(label.color.a<0) label.color=color_of_shape(appearance);
            else {
                label.color.a*=1-float(transparency_of_shape(appearance))/100;
                label.attrs.color=gdoc::Color::rgb(gcm::u8(std::lround(label.color.r*255)),
                    gcm::u8(std::lround(label.color.g*255)),gcm::u8(std::lround(label.color.b*255)));
            }
            result.push_back(std::move(label));
        }
    }
    return result;
}

void Canvas::text_bounds(const TextItem& t, double& lx, double& ly, double& hx, double& hy) const {
    gcm::to_world(vol_,t.pos,lx,ly); hx=lx; hy=ly;
    labels_bounds(note_labels(t),lx,ly,hx,hy);
}

namespace {
const std::vector<std::array<gtext::OutlinePoint,3>>& glyph_fill_mesh(
    const gtext::Font& font, gcm::u32 code, const gcm::WorkingVolume& volume) {
    const auto& glyph=font.outline(code);
    const std::array<double,3> key{volume.cx,volume.cy,volume.extent};
    if(glyph.triangulation_bits==volume.resolution_bits && glyph.triangulation_volume==key)
        return glyph.triangles;
    glyph.triangles.clear();
    const auto& contours=glyph.contours;
    std::vector<double> areas(contours.size());
    const auto inside=[](const auto& loop,gtext::OutlinePoint p) {
        bool yes=false;
        for(std::size_t i=0,j=loop.size()-1;i<loop.size();j=i++) {
            const auto a=loop[i],b=loop[j];
            if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)yes=!yes;
        }
        return yes;
    };
    for(std::size_t i=0;i<contours.size();++i) {
        const auto& loop=contours[i];
        for(std::size_t j=0;j<loop.size();++j){const auto a=loop[j],b=loop[(j+1)%loop.size()];areas[i]+=a.x*b.y-a.y*b.x;}
    }
    std::vector<int> parent(contours.size(),-1);
    for(std::size_t i=0;i<contours.size();++i)if(contours[i].size()>=3)
        for(std::size_t j=0;j<contours.size();++j)if(i!=j && contours[j].size()>=3 && std::abs(areas[j])>std::abs(areas[i]) &&
            inside(contours[j],contours[i][0]) && (parent[i]<0 || std::abs(areas[j])<std::abs(areas[std::size_t(parent[i])])))parent[i]=int(j);
    for(std::size_t outer=0;outer<contours.size();++outer) {
        int depth=0;for(int p=parent[outer];p>=0;p=parent[std::size_t(p)])++depth;
        if(depth%2 || contours[outer].size()<3)continue;
        std::vector<gcm::Lattice2> points;std::vector<std::vector<gcm::u32>> loops;
        const auto add=[&](std::size_t index,bool hole) {
            std::vector<gcm::u32> loop;
            for(auto p:contours[index]){loop.push_back(gcm::u32(points.size()));points.push_back(gcm::to_lattice(volume,p.x,p.y));}
            if((areas[index]>0)==hole)std::reverse(loop.begin(),loop.end());
            loops.push_back(std::move(loop));
        };
        add(outer,false);
        for(std::size_t hole=0;hole<contours.size();++hole)if(parent[hole]==int(outer))add(hole,true);
        const auto indices=gtopo::triangulate_face(loops,points);
        for(std::size_t t=0;t+2<indices.size();t+=3) {
            std::array<gtext::OutlinePoint,3> triangle;
            for(int k=0;k<3;++k)gcm::to_world(volume,points[indices[t+k]],triangle[k].x,triangle[k].y);
            glyph.triangles.push_back(triangle);
        }
    }
    glyph.triangulation_volume=key;
    glyph.triangulation_bits=volume.resolution_bits;
    return glyph.triangles;
}
}

std::vector<grender::d2::TriangleItem> Canvas::label_fills(const Label& lab,const gcm::WorkingVolume& volume) {
    std::vector<grender::d2::TriangleItem> result;
    const double c=std::cos(lab.angle),s=std::sin(lab.angle),shear=std::tan(lab.oblique*3.141592653589793/180);
    const auto world=[&](double x,double y) {
        x=lab.width_factor*(x+shear*y);y*=lab.y_factor;
        return std::pair{lab.x+c*x-s*y,lab.y+s*x+c*y};
    };
    const auto triangle=[&](double ax,double ay,double bx,double by,double cx,double cy,grender::Color color) {
        const auto a=world(ax,ay),b=world(bx,by),d=world(cx,cy);
        result.push_back({{a.first,a.second,b.first,b.second,d.first,d.second},color});
    };
    const auto box=[&](double x,double y,double width,double height,grender::Color color) {
        triangle(x,y,x+width,y,x+width,y+height,color);triangle(x,y,x+width,y+height,x,y+height,color);
    };
    if(lab.box_width>0 && lab.box_height>0)box(0,0,lab.box_width,lab.box_height,lab.background);
    const auto* font=lab.outline_font;
    const double advance=lab.text_advance>0?lab.text_advance:font?font->measure(lab.text)*lab.height:gtext::decode_utf8(lab.text).size()*.62*lab.height;
    const double ox=lab.halign==1?-advance*.5:lab.halign==2?-advance:0;
    const double oy=lab.valign==2?-lab.height*.5:lab.valign==3?-lab.height:0;
    if(lab.highlight.a>0 && !lab.text.empty())box(ox,oy-lab.height*.2,advance,lab.height,lab.highlight);
    if(font && !lab.text.empty()) {
        double pen=ox;gcm::u32 previous=0;
        for(auto code:gtext::decode_utf8(lab.text)) {
            if(previous)pen+=font->kerning(previous,code)*lab.height;
            for(const auto& glyph_triangle:glyph_fill_mesh(*font,code,volume))
                for(int pass=0;pass<(lab.bold?2:1);++pass) {
                    double x[3],y[3];
                    for(int k=0;k<3;++k) {
                        x[k]=pen+(glyph_triangle[k].x+pass*.022)*lab.height;
                        y[k]=oy+glyph_triangle[k].y*lab.height;
                    }
                    triangle(x[0],y[0],x[1],y[1],x[2],y[2],lab.color);
                }
            if(const auto* glyph=font->glyph(code))pen+=(glyph->advance+(lab.tracking-1)*.1)*lab.height;
            previous=code;
        }
    }
    if(!lab.clips.empty()) {
        std::vector<grender::d2::TriangleItem> clipped;
        for(const auto& tri:result) {
            std::vector<gclip::Vertex> vertices;
            for(int k=0;k<3;++k)vertices.push_back({tri.xy[2*k],tri.xy[2*k+1],0,0});
            const auto cut=gclip::triangles(vertices,lab.clips,volume);
            for(std::size_t j=0;j+2<cut.vertices.size();j+=3){grender::d2::TriangleItem item{};item.color=tri.color;for(int k=0;k<3;++k){item.xy[k*2]=cut.vertices[j+k].x;item.xy[k*2+1]=cut.vertices[j+k].y;}clipped.push_back(item);}
        }
        return clipped;
    }
    return result;
}

std::vector<gclip::Vertex> Canvas::label_mesh(const Label& lab,const gtext::Font* font,const gcm::WorkingVolume& volume) {
    if(lab.outline_font || lab.box_width>0) {
        std::vector<gclip::Vertex> result;
        for(const auto& triangle:label_fills(lab,volume))for(int k=0;k<3;++k)result.push_back({triangle.xy[k*2],triangle.xy[k*2+1],0,0});
        return result;
    }
    if(lab.stroke_font||lab.text.empty()||lab.height<=0)return {};
    gtext::TextStyle st;st.size=float(lab.height);st.letter_spacing=float((lab.tracking-1)*.1);
    const auto layout=font&&font->loaded()?gtext::layout_text(*font,lab.text,st):gtext::Layout{};
    const bool fallback=!font||!font->loaded();
    const double w=fallback?lab.height*.62*lab.text.size():layout.width;
    const double ox=lab.halign==1?-w*.5:lab.halign==2?-w:0;
    const double oy=lab.valign==2?-lab.height*.5:lab.valign==3?-lab.height:0;
    const double c=std::cos(lab.angle),s=std::sin(lab.angle),shear=std::tan(lab.oblique*3.141592653589793/180);
    std::vector<gclip::Vertex> mesh;
    const auto quad=[&](double x,double y,double width,double height,double u0,double v0,double u1,double v1) {
        std::array<gclip::Vertex,4> corners{{{x,y+height,u0,v0},{x+width,y+height,u1,v0},{x+width,y,u1,v1},{x,y,u0,v1}}};
        for(auto& p:corners) {
            double px=p.x+ox,py=p.y+oy;px=lab.width_factor*(px+shear*py);py*=lab.y_factor;
            p.x=lab.x+c*px-s*py;p.y=lab.y+s*px+c*py;
        }
        for(int k:{0,1,2,0,2,3})mesh.push_back(corners[k]);
    };
    if(fallback)quad(0,0,w,lab.height,0,0,1,1);
    else for(const auto& g:layout.glyphs)quad(g.x,g.y,g.w,g.h,
        double(g.glyph->ax)/font->atlas_width(),double(g.glyph->ay)/font->atlas_height(),
        double(g.glyph->ax+g.glyph->aw)/font->atlas_width(),double(g.glyph->ay+g.glyph->ah)/font->atlas_height());
    return gclip::triangles(mesh,lab.clips,volume).vertices;
}

bool Canvas::labels_bounds(const std::vector<Label>& labels, double& lx, double& ly, double& hx, double& hy) const {
    bool first=true;
    const auto include=[&](double x,double y) {
        if(first){lx=hx=x;ly=hy=y;first=false;}
        else {lx=std::min(lx,x);hx=std::max(hx,x);ly=std::min(ly,y);hy=std::max(hy,y);}
    };
    for(const auto& lab:labels) {
        if(lab.stroke_font)for(auto line:label_strokes(lab,vol_))for(int k:{0,2})include(line[k],line[k+1]);
        else for(const auto& p:label_mesh(lab,text_font_,vol_))include(p.x,p.y);
    }
    return !first;
}

void Canvas::add_centre_mark(Lattice centre, double r) {
    mark();
    const double arm = std::max(r * 0.12, 4.0 / view_.scale());
    const gcm::i64 a = gcm::i64(std::llround(arm * vol_.scale()));
    add_segment({centre.x - a, centre.y}, {centre.x + a, centre.y});
    add_segment({centre.x, centre.y - a}, {centre.x, centre.y + a});
    message_ = "centre mark placed";
}

void Canvas::select_reference(gcm::u32 index) {
    remember_selection();
    sel_dims_.clear(); sel_hatches_.clear();
    if (index >= blocks_.references().size()) return;
    selection_.clear();
    sel_texts_.clear();
    sel_refs_.assign(1, index);
    const auto& ref = blocks_.references()[index];
    message_ = "block '" + ref.name + "' on layer " + ref.layer;
    rebuild_grips();
}

std::size_t Canvas::pick_dim(double px, double py) const {
    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const double slop = pickbox_pixels / view_.scale();
    for (std::size_t n = dims_.size(); n > 0; --n) {
        const auto& d = dims_[n - 1];
        if (layer_hidden(d.layer)) continue;
        const gdim::Style* st = dim_styles_.find(d.style);
        if (!st) continue;
        const gdim::Geometry g = gdim::build(d, annotative(*st), vol_);
        if (!g.ok) continue;
        if(!g.text.empty()) {
            TextItem note{g.text_at,g.text,g.text_height,g.text_angle,g.halign,g.valign,0,"__dimension__"};
            double lx,ly,hx,hy;text_bounds(note,lx,ly,hx,hy);
            if(wx>=lx-slop&&wx<=hx+slop&&wy>=ly-slop&&wy<=hy+slop) return n-1;
        }
        for (const auto& sh : g.lines) {
            if (sh.kind != gsnap::ShapeKind::Segment) continue;
            double ax, ay, bx, by;
            gcm::to_world(vol_, sh.a, ax, ay);
            gcm::to_world(vol_, sh.b, bx, by);
            const double dx = bx - ax, dy = by - ay;
            const double len2 = dx * dx + dy * dy;
            const double t = len2 < 1e-18 ? 0.0
                             : std::clamp(((wx - ax) * dx + (wy - ay) * dy) / len2, 0.0, 1.0);
            if (std::hypot(wx - (ax + dx * t), wy - (ay + dy * t)) < slop) return n - 1;
        }
    }
    return npos;
}

std::size_t Canvas::pick_text(double px, double py) const {
    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const double slop = 4.0 / view_.scale();
    for (std::size_t n = texts_.size(); n > 0; --n) {
        if (layer_hidden(texts_[n-1].layer)) continue;
        double lo_x, lo_y, hi_x, hi_y;
        auto resolved=texts_[n-1]; resolved.text=resolved_text(resolved);
        text_bounds(resolved, lo_x, lo_y, hi_x, hi_y);
        if (wx >= lo_x - slop && wx <= hi_x + slop && wy >= lo_y - slop && wy <= hi_y + slop)
            return n - 1;
    }
    return npos;
}

void Canvas::select_at(double px, double py, bool add) {
    select_hit(px,py,add,false);
}

gcm::u32 Canvas::pick_reference(double px, double py) const {
    if(const auto wall=pick_wall_section(px,py);wall!=npos32)return wall;
    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const double tol = pickbox_pixels / view_.scale();
    const auto& refs = blocks_.references();
    {
        gcm::u64 key = block_geometry_key() ^ (texts_serial_*0xD6E8FEB86659FD93ull);
        for (const auto& rf : refs) {
            key = (key * 131ull) ^ static_cast<gcm::u64>(rf.insertion.x);
            key = (key * 131ull) ^ static_cast<gcm::u64>(rf.insertion.y);
            key ^= static_cast<gcm::u64>(
                std::llround(rf.rotation * 1e6 + rf.scale_x * 7.0 + rf.scale_y * 13.0));
        }
        if (key != ref_bounds_key_ || ref_bounds_.size() != refs.size()) {
            ref_bounds_key_ = key;
            ref_bounds_.assign(refs.size(), {1e300, 1e300, -1e300, -1e300});
            for (std::size_t k = 0; k < refs.size(); ++k) {
                auto& b = ref_bounds_[k];
                const auto labels=reference_labels(refs[k]);
                if(!labels.empty()) labels_bounds(labels,b[0],b[1],b[2],b[3]);
                for (const auto& sh : blocks_.expand(refs[k], vol_, 8, &appearance_layers_)) {
                    double ax, ay;
                    gcm::to_world(vol_, sh.a, ax, ay);
                    double r = sh.is_curve() ? double(sh.radius) * vol_.inv_scale() : 0.0;
                    if(sh.is_elliptical())r=std::max(std::hypot(double(sh.b.x)-sh.a.x,double(sh.b.y)-sh.a.y),double(sh.radius))*vol_.inv_scale();
                    if(sh.is_infinite()){b={-1e300,-1e300,1e300,1e300};continue;}
                    b[0] = std::min(b[0], ax - r); b[1] = std::min(b[1], ay - r);
                    b[2] = std::max(b[2], ax + r); b[3] = std::max(b[3], ay + r);
                    if (sh.kind == gsnap::ShapeKind::Segment) {
                        double bx, by;
                        gcm::to_world(vol_, sh.b, bx, by);
                        b[0] = std::min(b[0], bx); b[1] = std::min(b[1], by);
                        b[2] = std::max(b[2], bx); b[3] = std::max(b[3], by);
                    }
                }
            }
        }
    }
    gcm::u32 best = npos32;
    double best_d = tol;
    for (gcm::u32 k = 0; k < static_cast<gcm::u32>(refs.size()); ++k) {
        if (layer_hidden(refs[k].layer)) continue;
        const auto& bb = ref_bounds_[k];
        if (wx < bb[0] - tol || wx > bb[2] + tol || wy < bb[1] - tol || wy > bb[3] + tol)
            continue;
        for (const auto& label:reference_labels(refs[k])) {
            auto lines=label_strokes(label,vol_);
            const auto mesh=label_mesh(label,text_font_,vol_);
            for(std::size_t j=0;j<mesh.size();j+=3) {
                bool positive=false,negative=false;
                for(int n=0;n<3;++n) {
                    const auto a=mesh[j+n],b=mesh[j+(n+1)%3];
                    const auto cross=(b.x-a.x)*(wy-a.y)-(b.y-a.y)*(wx-a.x);
                    positive|=cross>0;negative|=cross<0;
                    lines.push_back({a.x,a.y,b.x,b.y});
                }
                if(!(positive&&negative)){best_d=0;best=k;}
            }
            for(const auto& line:lines) {
                const double dx=line[2]-line[0],dy=line[3]-line[1],length=dx*dx+dy*dy;
                const double t=length>1e-24?std::clamp(((wx-line[0])*dx+(wy-line[1])*dy)/length,0.,1.):0.;
                const auto d=std::hypot(wx-line[0]-dx*t,wy-line[1]-dy*t);
                if(d<best_d){best_d=d;best=k;}
            }
        }
        for (const auto& sh : blocks_.expand(refs[k], vol_, 8, &appearance_layers_)) {
            if (!shape_visible(sh)) continue;
            double d=1e300;
            for(const auto& line:selection_lines(sh)) {
                const double dx=line[2]-line[0],dy=line[3]-line[1],length=dx*dx+dy*dy;
                const double t=length>1e-24?std::clamp(((wx-line[0])*dx+(wy-line[1])*dy)/length,0.,1.):0.;
                d=std::min(d,std::hypot(wx-line[0]-dx*t,wy-line[1]-dy*t));
            }
            if (d < best_d) { best_d = d; best = k; }
        }
    }
    return best;
}

void Canvas::select_all() {
    remember_selection();
    selection_.clear();sel_texts_.clear();sel_refs_.clear();sel_dims_.clear();sel_hatches_.clear();
    for(const auto& object:selection_objects())include_selection(object);
    for(std::size_t i=0;i<hatches_.size();++i)if(!layer_hidden(hatches_[i].layer))sel_hatches_.push_back(i);
    rebuild_grips();
}

void Canvas::erase_selected() {
    if (selection_.empty() && sel_refs_.empty() && sel_texts_.empty() && sel_dims_.empty() && sel_hatches_.empty()) {
        message_ = "nothing selected";
        return;
    }
    mark();
    std::size_t erased = 0;
    std::sort(sel_hatches_.begin(),sel_hatches_.end(),std::greater<std::size_t>());
    for(auto i:sel_hatches_) if(i<hatches_.size()) {hatches_.erase(hatches_.begin()+i);++erased;}
    sel_hatches_.clear();
    if (!sel_dims_.empty()) {
        std::sort(sel_dims_.begin(),sel_dims_.end(),std::greater<std::size_t>());
        for (auto i:sel_dims_) if (i<dims_.size()) { dims_.erase(dims_.begin()+i); ++erased; }
        sel_dims_.clear(); sel_hatches_.clear();
    }
    if (!sel_refs_.empty()) {
        std::vector<gcm::u32> refs = sel_refs_;
        std::sort(refs.begin(), refs.end(), std::greater<gcm::u32>());
        for (gcm::u32 rr : refs) if (blocks_.erase_reference(rr)) ++erased;
        sel_refs_.clear();
    }
    if (!sel_texts_.empty()) {
        std::vector<std::size_t> order = sel_texts_;
        std::sort(order.begin(), order.end(), std::greater<std::size_t>());
        for (std::size_t ti : order)
            if (ti < texts_.size()) { texts_.erase(texts_.begin() + static_cast<long>(ti)); ++erased; }
        sel_texts_.clear();
        ++texts_serial_;
    }
    std::size_t dims_gone = 0;
    if (!selection_.empty()) {
        const auto near_shape = [&](const gdim::Point& p, const gsnap::Shape& sh) {
            const gcm::f64 px_ = gcm::f64(p.x), py_ = gcm::f64(p.y);
            const gcm::f64 ax = gcm::f64(sh.a.x), ay = gcm::f64(sh.a.y);
            const gcm::f64 eps = 4.0;        // lattice units
            if (sh.kind == gsnap::ShapeKind::Circle || sh.kind == gsnap::ShapeKind::Arc) {
                const gcm::f64 r = gcm::f64(sh.radius);
                const gcm::f64 d = std::hypot(px_ - ax, py_ - ay);
                return std::fabs(d - r) <= eps || d <= eps;
            }
            const gcm::f64 bx = gcm::f64(sh.b.x), by = gcm::f64(sh.b.y);
            const gcm::f64 dx = bx - ax, dy = by - ay;
            const gcm::f64 len2 = dx * dx + dy * dy;
            if (len2 < 1.0)
                return std::hypot(px_ - ax, py_ - ay) <= eps;
            const gcm::f64 t = std::clamp(((px_ - ax) * dx + (py_ - ay) * dy) / len2, 0.0, 1.0);
            return std::hypot(px_ - (ax + dx * t), py_ - (ay + dy * t)) <= eps;
        };
        const auto anchored_to_selection = [&](const gdim::Dimension& d) {
            for (std::size_t i : selection_) {
                if (i >= shapes_.size()) continue;
                const gsnap::Shape& sh = shapes_[i];
                if (near_shape(d.p1, sh) || near_shape(d.p2, sh) ||
                    near_shape(d.centre, sh))
                    return true;
            }
            return false;
        };
        for (std::size_t di = dims_.size(); di-- > 0;)
            if (anchored_to_selection(dims_[di])) {
                dims_.erase(dims_.begin() + static_cast<long>(di));
                ++dims_gone;
            }

        std::vector<std::size_t> order = selection_;
        std::sort(order.begin(), order.end(), std::greater<std::size_t>());
        for (std::size_t i : order)
            if (i < shapes_.size()) { shapes_.erase(shapes_.begin() + static_cast<long>(i)); ++erased; }
        selection_.clear();
    }
    std::vector<gcm::u64> before;
    before.reserve(hatches_.size());
    for (const auto& h : hatches_) before.push_back(h.boundary_hash);
    const bool repaired_openings=update_door_openings(undo_stack_.back().shapes);
    sync_wall_sections();
    refresh_hatches();
    std::size_t hatches_gone = 0;
    for (std::size_t hi = hatches_.size(); hi-- > 0;) {
        const bool died = hatches_[hi].orphaned || !hatches_[hi].valid;
        const bool moved = hi < before.size() &&
                           hatches_[hi].boundary_hash != before[hi];
        const bool wall_fill=repaired_openings && (hatches_[hi].attrs.user.count("automatic_wall_core") || hatches_[hi].attrs.user.count("wall_style_association"));
        if (died || (moved && !wall_fill)) {
            hatches_.erase(hatches_.begin() + long(hi));
            ++hatches_gone;
        }
    }
    char buf[128];
    if (dims_gone || hatches_gone)
        std::snprintf(buf, sizeof(buf),
                      "erased %zu (and %zu dimension%s, %zu hatch%s on them)",
                      erased, dims_gone, dims_gone == 1 ? "" : "s",
                      hatches_gone, hatches_gone == 1 ? "" : "es");
    else
        std::snprintf(buf, sizeof(buf), "erased %zu", erased);
    message_ = buf;
}



void Canvas::rebuild_multiline() {
    const gdoc::MultilineStyle* style = multilines_.find(mline_style_);
    if (!style || style->elements.empty() || mline_pts_.size() < 2) return;

    if (shapes_.size() > mline_first_)
        shapes_.resize(mline_first_);
    else
        mline_first_ = shapes_.size();

    double shift = 0.0;
    if (mline_justify_ == gdoc::Justify::Top) shift = -style->max_offset();
    else if (mline_justify_ == gdoc::Justify::Bottom) shift = -style->min_offset();

    for (const gdoc::MultilineElement& el : style->elements) {
        const double offset = (el.offset + shift) * mline_scale_;

        for (std::size_t i = 1; i < mline_pts_.size(); ++i) {
            double ax, ay, bx, by;
            gcm::to_world(vol_, mline_pts_[i - 1], ax, ay);
            gcm::to_world(vol_, mline_pts_[i], bx, by);
            const double dx = bx - ax, dy = by - ay;
            const double len = std::hypot(dx, dy);
            if (len < 1e-12) continue;
            const double nx = -dy / len, ny = dx / len;

            add_segment(gcm::to_lattice(vol_, ax + nx * offset, ay + ny * offset),
                        gcm::to_lattice(vol_, bx + nx * offset, by + ny * offset));
        }
    }

    if (style->start_cap == gdoc::EndCap::Line ||
        style->end_cap == gdoc::EndCap::Line) {
        const double lo = (style->min_offset() + shift) * mline_scale_;
        const double hi = (style->max_offset() + shift) * mline_scale_;

        auto cap = [&](Lattice from, Lattice to) {
            double ax, ay, bx, by;
            gcm::to_world(vol_, from, ax, ay);
            gcm::to_world(vol_, to, bx, by);
            const double dx = bx - ax, dy = by - ay;
            const double len = std::hypot(dx, dy);
            if (len < 1e-12) return;
            const double nx = -dy / len, ny = dx / len;
            add_segment(gcm::to_lattice(vol_, ax + nx * lo, ay + ny * lo),
                        gcm::to_lattice(vol_, ax + nx * hi, ay + ny * hi));
        };

        if (style->start_cap == gdoc::EndCap::Line)
            cap(mline_pts_.front(), mline_pts_[1]);
        if (style->end_cap == gdoc::EndCap::Line)
            cap(mline_pts_.back(), mline_pts_[mline_pts_.size() - 2]);
    }
}


void Canvas::rebuild_spline() {
    if (spline_pts_.size() < 2) return;

    std::vector<ggeo::d2::Point<Scalar>> pts;
    pts.reserve(spline_pts_.size());
    for (Lattice p : spline_pts_) {
        double x, y;
        gcm::to_world(vol_, p, x, y);
        pts.push_back(ggeo::d2::Point<Scalar>::from(x, y));
    }

    auto curve = ggeo::interpolate_points<Scalar>(pts, 3);
    if (!curve.valid()) return;
    if (splines_.empty() || spline_pts_.size() == 3) splines_.push_back(curve);
    else splines_.back() = curve;
}



bool Canvas::tool_available(Tool t) const {
    const std::size_t n = selection_.size()+sel_refs_.size()+sel_texts_.size()+sel_dims_.size();

    switch (t) {
        case Tool::Select: case Tool::Line: case Tool::Rect: case Tool::Circle:
        case Tool::Arc: case Tool::Polyline: case Tool::Ellipse:
        case Tool::Point: case Tool::Ray: case Tool::Xline:
        case Tool::Spline: case Tool::Multiline:
            return true;

        case Tool::Move: case Tool::Copy: case Tool::Rotate:
        case Tool::Mirror: case Tool::ArrayRect: case Tool::ArrayPolar:
            return n > 0;

        case Tool::Offset:
            return !shapes_.empty();
        case Tool::Trim: case Tool::Extend:
            return shapes_.size() >= 2;

        case Tool::Fillet: case Tool::Chamfer:
            return shapes_.size() >= 2;

        case Tool::DimLinear: case Tool::DimAligned:
            return !shapes_.empty();
        case Tool::DimAngular:
            return shapes_.size() >= 2;
        case Tool::DimRadial: case Tool::DimDiameter: {
            for (const gsnap::Shape& sh : shapes_)
                if (sh.is_curve()) return true;
            return false;
        }
    }
    return false;
}

std::vector<Tool> Canvas::tool_ring() const {
    static const Tool kDrawing[] = {
        Tool::Line, Tool::Polyline, Tool::Rect, Tool::Circle,
        Tool::Arc, Tool::Ellipse, Tool::Spline, Tool::DimLinear,
    };
    static const Tool kEditing[] = {
        Tool::Move, Tool::Copy, Tool::Rotate, Tool::Mirror,
        Tool::Offset, Tool::ArrayRect, Tool::Fillet, Tool::Trim,
    };

    std::vector<Tool> out;
    if (selection_.empty())
        for (Tool t : kDrawing) out.push_back(t);
    else
        for (Tool t : kEditing) out.push_back(t);
    return out;
}

std::vector<const char*> Canvas::osnap_ring(double px, double py) const {
    sync_snap_engine();

    double wx, wy;
    view_.world_of(px, py, wx, wy);

    std::optional<gsnap::Point> anchor;
    if (pending_) anchor = anchor_;

    std::vector<const char*> out;
    for (gsnap::Kind k : snap_engine_.applicable(vol_, wx, wy, anchor))
        out.push_back(gsnap::kind_name(k));
    return out;
}

bool Canvas::force_snap(std::string_view kind) {
    static constexpr gsnap::Kind kAll[] = {
        gsnap::Endpoint, gsnap::Midpoint, gsnap::Center, gsnap::Node,
        gsnap::Quadrant, gsnap::Intersection, gsnap::Insertion,
        gsnap::Perpendicular, gsnap::Tangent, gsnap::Nearest,
        gsnap::ApparentIntersection, gsnap::Extension, gsnap::Parallel,
        gsnap::GeometricCenter,
    };
    for (gsnap::Kind k : kAll)
        if (kind == gsnap::kind_name(k)) {
            snap_engine_.settings.forced = static_cast<gcm::u32>(k);
            message_ = std::string("snap: ") + gsnap::kind_name(k);
            return true;
        }
    return false;
}

bool Canvas::define_block(std::string_view name, double px, double py) {
    if (selection_.empty()) {
        message_ = "select the geometry first";
        return false;
    }
    if (name.empty()) {
        message_ = "a block needs a name";
        return false;
    }

    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const Lattice base = gcm::to_lattice(vol_, wx, wy);

    gdoc::BlockDef def;
    def.name = std::string(name);
    def.base = {};

    for (std::size_t i : selection_) {
        if (i >= shapes_.size()) continue;
        gsnap::Shape s = shapes_[i];
        s.a.x -= base.x;
        s.a.y -= base.y;
        s.b.x -= base.x;
        s.b.y -= base.y;
        def.geometry.push_back(s);
    }

    mark();
    blocks_.define(def);

    std::vector<std::size_t> order = selection_;
    std::sort(order.begin(), order.end(), std::greater<std::size_t>());
    for (std::size_t i : order)
        if (i < shapes_.size()) shapes_.erase(shapes_.begin() + static_cast<long>(i));
    selection_.clear();
    rebuild_grips();

    gdoc::BlockRef r;
    r.name = def.name;
    r.insertion = base;
    r.layer=current_layer_;r.attrs=current_attributes;
    blocks_.insert(r);

    message_ = "defined block " + def.name;
    return true;
}

bool Canvas::insert_block(std::string_view name, double px, double py,
                          double scale, double rotation) {
    if (!blocks_.find(name)) {
        message_ = "no such block";
        return false;
    }
    double wx, wy;
    view_.world_of(px, py, wx, wy);

    mark();
    gdoc::BlockRef r;
    r.name = std::string(name);
    r.insertion = gcm::to_lattice(vol_, wx, wy);
    r.scale_x = r.scale_y = scale;
    r.rotation = rotation;
    r.layer=current_layer_;r.attrs=current_attributes;

    if (const gdoc::BlockDef* def = blocks_.find(name))
        for (const gdoc::AttributeDef& a : def->attributes)
            r.attributes.push_back({a.tag, a.default_value});

    blocks_.insert(r);
    message_ = "inserted " + std::string(name);
    return true;
}

bool Canvas::explode_block(std::size_t reference) {
    if (reference >= blocks_.references().size()) {
        message_ = "no such insertion";
        return false;
    }
    const gdoc::BlockRef r = blocks_.references()[reference];
    const gdoc::BlockDef* def = blocks_.find(r.name);
    if (!def) {
        message_ = "the definition is missing";
        return false;
    }
    if (!def->allow_exploding || def->is_xref || std::as_const(blocks_).xref(r.name)) {
        message_ = "that block cannot be exploded";
        return false;
    }

    if (!blocks_.note_instances(r,vol_).empty()) {
        message_="cannot explode a block containing text yet"; return false;
    }
    auto expanded = blocks_.expand(r, vol_, 8, &appearance_layers_);
    if(std::abs(std::abs(r.scale_x)-std::abs(r.scale_y))>1e-12 &&
       std::any_of(expanded.begin(),expanded.end(),[](const auto& sh){return sh.region&&sh.is_curve();})) {
        message_="cannot explode nonuniformly scaled curved regions yet";return false;
    }
    if (const auto error=gregion::validate_members(expanded); !error.empty()) {
        message_="cannot explode block: "+error;return false;
    }
    if(is_wall_section(r)) {
        std::vector<gsnap::Shape> walls;
        for(const auto& shape:shapes_)if(is_joined_wall(shape))walls.push_back(shape);
        const auto source=gregion::from_shapes(expanded),material=gregion::from_shapes(walls);
        if(source.ok && material.ok) {
            const auto visible=gregion::boolean_op(source.region,material.region,gregion::Operation::Intersect);
            if(!visible.ok) {message_="Cannot ungroup wall section: "+visible.error;return false;}
            if(std::abs(visible.region.area(vol_)-source.region.area(vol_))>4*vol_.inv_scale()*vol_.inv_scale()) {
                expanded.clear();
                for(const auto& loop:visible.region.loops)for(const auto& edge:loop)expanded.push_back(edge.shape);
            }
        }
    }
    mark();
    clear_selection();
    std::map<gcm::u32,gcm::u32> compounds;
    for (auto out : expanded) {
        out.key=out.id=0;
        if(is_wall_section(r)) {
            out.poly=out.poly_index=0;out.region=false;out.region_loop=0;out.region_reversed=false;
            out.attrs.user.erase("sketch_object");out.attrs.user.erase("wall_joined");
            out.attrs.user.erase("wall_source_signature");out.attrs.user.erase("wall_source_outline");
            out.attrs.user["wall_ungrouped"]="1";
        }
        if (out.poly) {
            auto [it,inserted]=compounds.try_emplace(out.poly,0);
            if (inserted) it->second=next_polyline_id();
            out.poly=it->second;
        }
        selection_.push_back(shapes_.size());push_shape(out);
    }
    blocks_.erase_reference(static_cast<gcm::u32>(reference));
    sync_wall_sections();
    rebuild_grips();
    message_ = is_wall_section(r) ? "Wall section ungrouped into drawing lines" : "exploded " + r.name;
    return true;
}

bool Canvas::set_attribute(std::size_t reference, std::string_view tag,
                           std::string_view value) {
    gdoc::BlockRef* r = blocks_.reference(static_cast<gcm::u32>(reference));
    if (!r) {
        message_ = "no such insertion";
        return false;
    }
    r->set_attribute(tag, value);
    return true;
}

std::vector<std::pair<std::string, std::string>> Canvas::attributes_of(
    std::size_t reference) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (reference >= blocks_.references().size()) return out;
    for (const gdoc::AttributeValue& a : blocks_.references()[reference].attributes)
        out.push_back({a.tag, a.value});
    return out;
}

void Canvas::emit_blocks(grender::d2::DrawList& out) const {
    const auto& refs = blocks_.references();
    std::vector<grender::d2::DrawList> parts(gcm::worker_count());
    gcm::parallel_for(refs.size(), 32, [&](std::size_t lo, std::size_t hi, unsigned t) {
        for (std::size_t k = lo; k < hi; ++k) {
            if (layer_hidden(refs[k].layer)) continue;
            const bool sel = std::find(sel_refs_.begin(), sel_refs_.end(), gcm::u32(k)) != sel_refs_.end();
            emit_block(parts[t], refs[k], sel ? Role::Selected : Role::Geometry);
        }
    });
    for (const auto& part : parts) out.append(part);
}

void Canvas::emit_block(grender::d2::DrawList& out, const gdoc::BlockRef& r, grender::Role role) const {
    if(is_wall_section(r) && (role==Role::Geometry || solid_only_))return;
    for (const auto& shape : blocks_.expand(r, vol_, 8, &appearance_layers_)) {
        if (!shape_visible(shape)) continue;
        emit_shape(out, shape, role);
    }
}
Canvas::DynamicInput Canvas::dynamic_input() const {
    DynamicInput out;
    if(!dynamic_input_enabled) return out;
    if (!pending_ && tool_ != Tool::Select) {
        out.visible = tool_ != Tool::Select;
        view_.screen_of(0, 0, out.x, out.y);
        double cx, cy;
        gcm::to_world(vol_, cursor_, cx, cy);
        view_.screen_of(cx, cy, out.x, out.y);
        out.x += 18.0;
        out.y += 18.0;
        out.fields.push_back({"X", gcmd::format_coordinate(cx, cy, unit_), true,
                              dyn_field_ == 0});
        return out;
    }
    if (!pending_) return out;

    double ax, ay, cx, cy;
    gcm::to_world(vol_, anchor_, ax, ay);
    gcm::to_world(vol_, cursor_, cx, cy);
    view_.screen_of(cx, cy, out.x, out.y);
    out.x += 18.0;
    out.y += 18.0;
    out.visible = true;

    const double dx = cx - ax, dy = cy - ay;

    if (tool_ == Tool::Circle) {
        out.fields.push_back(
            {"R", gunit::format_length(std::hypot(dx, dy),
                                       gunit::Format{unit_}), true, dyn_field_ == 0});
        return out;
    }

    out.fields.push_back({"D",
                          gunit::format_length(std::hypot(dx, dy),
                                               gunit::Format{unit_}),
                          true, dyn_field_ == 0});

    double deg = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
    while (deg < 0.0) deg += 360.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g\xc2\xb0", deg);
    out.fields.push_back({"A", buf, true, dyn_field_ == 1});

    if (!dyn_buffer_.empty() && dyn_field_ >= 0 &&
        dyn_field_ < static_cast<int>(out.fields.size()))
        out.fields[static_cast<std::size_t>(dyn_field_)].value = dyn_buffer_;

    return out;
}

void Canvas::dynamic_set(int field, std::string text) {
    if (!dyn_reference_valid_ || field!=dyn_field_) { dyn_reference_=cursor_; dyn_reference_valid_=true; }
    dyn_field_=field;
    dyn_buffer_=std::move(text);
    if (dyn_buffer_.empty()) { dyn_reference_valid_=false; return; }
    if (!pending_) return;
    double ax,ay,cx,cy;
    gcm::to_world(vol_,anchor_,ax,ay); gcm::to_world(vol_,dyn_reference_,cx,cy);
    double angle=std::atan2(cy-ay,cx-ax),distance=std::hypot(cx-ax,cy-ay);
    if (field==1) {
        const auto parsed=gunit::parse_angle(dyn_buffer_,gunit::Angle::Degree);
        if (!parsed || !std::isfinite(*parsed)) return;
        angle=*parsed;
    } else {
        const auto parsed=gunit::parse_length(dyn_buffer_,unit_);
        if (!parsed || !std::isfinite(*parsed)) return;
        distance=*parsed;
    }
    cursor_=gcm::to_lattice(vol_,ax+distance*std::cos(angle),ay+distance*std::sin(angle));
}

void Canvas::dynamic_type(std::string_view text) { dynamic_set(dyn_field_,dyn_buffer_+std::string(text)); }

void Canvas::dynamic_backspace() {
    if (!dyn_buffer_.empty()) dynamic_set(dyn_field_,dyn_buffer_.substr(0,dyn_buffer_.size()-1));
}

void Canvas::dynamic_tab() {
    const DynamicInput in = dynamic_input();
    if (in.fields.size() < 2) return;
    dyn_field_ = (dyn_field_ + 1) % static_cast<int>(in.fields.size());
    dyn_reference_valid_=false;
}

bool Canvas::dynamic_commit() {
    if (dyn_buffer_.empty()) return false;
    if (!pending_) {
        const auto point=dyn_buffer_;
        dynamic_cancel();
        return type_point(point);
    }

    double ax = 0.0, ay = 0.0;
    if (pending_) gcm::to_world(vol_, anchor_, ax, ay);
    else if (has_last_point_) gcm::to_world(vol_, last_point_, ax, ay);

    double cx, cy;
    gcm::to_world(vol_, cursor_, cx, cy);
    const double dx = cx - ax, dy = cy - ay;
    double current_angle = std::atan2(dy, dx);
    if (dyn_reference_valid_) {
        double rx,ry; gcm::to_world(vol_,dyn_reference_,rx,ry);
        current_angle=std::atan2(ry-ay,rx-ax);
    }
    const double current_dist = std::hypot(dx, dy);

    std::string typed;
    if (dyn_field_ == 1) {
        const auto a = gunit::parse_angle(dyn_buffer_, gunit::Angle::Degree);
        if (!a || !std::isfinite(*a)) {
            message_ = "cannot read that angle";
            return false;
        }
        typed = gcmd::format_polar(current_dist, *a, unit_);
    } else {
        const auto d = gunit::parse_length(dyn_buffer_, unit_);
        if (!d || !std::isfinite(*d)) {
            message_ = "cannot read that distance";
            return false;
        }
        typed = gcmd::format_polar(*d, current_angle, unit_);
    }

    dyn_buffer_.clear();
    dyn_field_ = 0;
    dyn_reference_valid_=false;
    return type_point(typed);
}

void Canvas::dynamic_cancel() {
    dyn_buffer_.clear();
    dyn_field_ = 0;
    dyn_reference_valid_=false;
}

void Canvas::set_multiline_style(std::string_view name) {
    if (multilines_.find(name)) mline_style_ = std::string(name);
}

std::size_t Canvas::select_area(double px0, double py0, double px1, double py1, bool add) {
    return select_polygon({{px0,py0},{px1,py0},{px1,py1},{px0,py1}},
                          false,crossing_direction(px0,px1),add);
}

std::size_t Canvas::select_fence(const std::vector<std::pair<double,double>>& pts, bool add) {
    return select_polygon(pts,true,true,add);
}
std::size_t Canvas::select_lasso(const std::vector<std::pair<double,double>>& pts,
                               bool crossing, bool add) {
    return select_polygon(pts,false,crossing,add);
}

bool Canvas::Filter::matches(const gsnap::Shape& s, const gcm::WorkingVolume& v,
                             std::string_view layer_of) const {
    if (by_kind && s.kind != kind) return false;
    if (by_layer && layer != layer_of) return false;

    if (by_length) {
        if (!s.is_linear() || s.is_infinite()) return false;
        double ax, ay, bx, by;
        gcm::to_world(v, s.a, ax, ay);
        gcm::to_world(v, s.b, bx, by);
        const double len = std::hypot(bx - ax, by - ay);
        if (len < min_length || len > max_length) return false;
    }

    if (by_radius) {
        if (!s.is_curve() && !s.is_elliptical()) return false;
        const double r = static_cast<double>(s.radius) * v.inv_scale();
        if (r < min_radius || r > max_radius) return false;
    }
    return true;
}

std::vector<std::size_t> Canvas::find(const Filter& f) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < shapes_.size(); ++i)
        if (shape_visible(shapes_[i]) && f.matches(shapes_[i], vol_, shapes_[i].layer))
            out.push_back(i);
    return out;
}

std::size_t Canvas::select_filtered(const Filter& f, bool add) {
    if (!f.any()) {
        message_ = "the filter selects nothing";
        return 0;
    }
    remember_selection();
    if (!add) {selection_.clear();sel_texts_.clear();sel_refs_.clear();sel_dims_.clear();sel_hatches_.clear();}

    const std::vector<std::size_t> found = find(f);
    for (std::size_t i : found)
        if (!is_selected(i)) selection_.push_back(i);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "filter: %zu found", found.size());
    message_ = buf;
    rebuild_grips();
    return found.size();
}

bool Canvas::type_point(std::string_view text) {
    const auto c = gcmd::parse_coordinate(text, unit_);
    if (!c || !c->valid()) {
        message_ = "cannot read that coordinate";
        return false;
    }

    double dir = 0.0;
    if (track_.active) dir = track_.angle_deg * 3.14159265358979323846 / 180.0;

    double lx = 0.0, ly = 0.0;
    if (pending_) gcm::to_world(vol_, anchor_, lx, ly);
    else if (has_last_point_) gcm::to_world(vol_, last_point_, lx, ly);

    double x, y, z;
    if (ucs_active() && !c->world_override) {
        double ulx = 0.0, uly = 0.0;
        world_to_ucs(lx, ly, ulx, uly);
        double ux, uy, uz;
        c->resolve(ulx, uly, 0.0, dir - ucs_angle_, ux, uy, uz);
        ucs_to_world(ux, uy, x, y);
        z = uz;
    } else {
        c->resolve(lx, ly, 0.0, dir, x, y, z);
    }

    double px, py;
    view_.screen_of(x, y, px, py);
    const Lattice exact = gcm::to_lattice(vol_, x, y);
    cursor_ = exact;

    typed_exact_ = true;
    click(px, py, false);
    typed_exact_ = false;

    last_point_ = exact;
    has_last_point_ = true;
    return true;
}

std::string Canvas::cursor_readout() const {
    double cx, cy;
    gcm::to_world(vol_, cursor_, cx, cy);
    if (ucs_active()) {
        double ux, uy;
        world_to_ucs(cx, cy, ux, uy);
        cx = ux;
        cy = uy;
    }

    if (!pending_) return gcmd::format_coordinate(cx, cy, unit_);

    double ax, ay;
    gcm::to_world(vol_, anchor_, ax, ay);
    const double dx = cx - ax, dy = cy - ay;

    if (track_.active)
        return gcmd::format_polar(std::hypot(dx, dy),
                                  std::atan2(dy, dx), unit_);
    return gcmd::format_relative(dx, dy, unit_);
}

std::size_t Canvas::picks_needed() const noexcept {
    switch (tool_) {
        case Tool::Move: case Tool::Copy: case Tool::Rotate:
        case Tool::Mirror: case Tool::ArrayRect:
            return 2;
        case Tool::ArrayPolar:
            return 1;
        case Tool::DimLinear: case Tool::DimAligned:
            return 3;
        case Tool::DimAngular:
            return 4;
        case Tool::DimRadial: case Tool::DimDiameter:
            return 2;
        default:
            return 0;
    }
}

void Canvas::run_edit_tool() {
    const Lattice p0 = arc_pts_.size() > 0 ? arc_pts_[0] : Lattice{};
    const Lattice p1 = arc_pts_.size() > 1 ? arc_pts_[1] : Lattice{};

    double x0, y0, x1, y1;
    gcm::to_world(vol_, p0, x0, y0);
    gcm::to_world(vol_, p1, x1, y1);

    std::vector<gsnap::Shape> picked;
    picked.reserve(selection_.size());
    for (std::size_t i : selection_)
        if (i < shapes_.size()) picked.push_back(shapes_[i]);

    if (picked.empty() && sel_refs_.empty() && sel_texts_.empty() && sel_dims_.empty()) { message_ = "nothing selected"; cancel(); return; }

    const auto items=selection_.size()+sel_dims_.size()+sel_refs_.size()+sel_texts_.size();
    const auto copies=tool_==Tool::ArrayRect?std::size_t(std::max(1,array_rows_))*std::size_t(std::max(1,array_columns_)):tool_==Tool::ArrayPolar?std::size_t(std::max(1,array_count_)):1;
    if(copies>10000 || copies*items>200000){message_="array exceeds 10,000 placements or 200,000 entities";cancel();return;}
    mark();
    const auto move_refs = [&](gcm::i64 dx2, gcm::i64 dy2) {
        for (gcm::u32 k : sel_refs_)
            if (gdoc::BlockRef* ref = blocks_.reference_mut(k)) {
                ref->insertion.x += dx2;
                ref->insertion.y += dy2;
            }
    };
    const auto move_texts = [&](gcm::i64 dx2, gcm::i64 dy2) {
        for (std::size_t ti : sel_texts_)
            if (ti < texts_.size()) { texts_[ti].pos.x += dx2; texts_[ti].pos.y += dy2; }
        if (!sel_texts_.empty()) ++texts_serial_;
    };
    const auto rotate_texts = [&](Lattice about, double a) {
        const double c = std::cos(a), sn = std::sin(a);
        for (std::size_t ti : sel_texts_)
            if (ti < texts_.size()) {
                const double rx = double(texts_[ti].pos.x - about.x);
                const double ry = double(texts_[ti].pos.y - about.y);
                texts_[ti].pos.x = about.x + gcm::i64(std::llround(rx * c - ry * sn));
                texts_[ti].pos.y = about.y + gcm::i64(std::llround(rx * sn + ry * c));
                texts_[ti].angle += a;
            }
        if (!sel_texts_.empty()) ++texts_serial_;
    };
    const auto rotate_refs = [&](Lattice about, double a) {
        const double c = std::cos(a), sn = std::sin(a);
        for (gcm::u32 k : sel_refs_)
            if (gdoc::BlockRef* ref = blocks_.reference_mut(k)) {
                const double rx = double(ref->insertion.x - about.x);
                const double ry = double(ref->insertion.y - about.y);
                ref->insertion.x = about.x + gcm::i64(std::llround(rx * c - ry * sn));
                ref->insertion.y = about.y + gcm::i64(std::llround(rx * sn + ry * c));
                ref->rotation += a;
            }
    };
    auto replace_selection = [&](const std::vector<gsnap::Shape>& out) {
        for (std::size_t k = 0; k < selection_.size() && k < out.size(); ++k)
            shapes_[selection_[k]] = out[k];
    };
    auto append = [&](const std::vector<gsnap::Shape>& out) {
        const std::size_t base=shapes_.size();
        shapes_.insert(shapes_.end(),out.begin(),out.end());selection_.clear();
        std::unordered_map<gcm::u32,gcm::u32> polylines;
        for(std::size_t k=base;k<shapes_.size();++k){
            if(!picked.empty()&&(k-base)%picked.size()==0)polylines.clear();
            auto& shape=shapes_[k];shape.key=++last_key_;
            if(shape.poly){auto [it,inserted]=polylines.try_emplace(shape.poly,0);if(inserted)it->second=++last_poly_;shape.poly=it->second;}
            selection_.push_back(k);
        }
    };
    const auto append_array=[&](std::vector<gsnap::Shape> out) {
        out.erase(out.begin(),out.begin()+std::min(picked.size(),out.size()));append(out);
    };

    const auto selected_dimensions=sel_dims_;bool copied_dimensions=false;
    const auto transform_dimensions=[&](const auto& transform,bool copy) {
        const auto& selected=selected_dimensions;if(copy&&!copied_dimensions){sel_dims_.clear();copied_dimensions=true;}
        for(auto i:selected) if(i<dims_.size()) {
            auto d=dims_[i];d.transform_points([&](auto& point){point=transform(point);});
            d.o1={};d.o2={};
            if(copy) {sel_dims_.push_back(dims_.size());dims_.push_back(std::move(d));}else dims_[i]=std::move(d);
        }
    };
    if(tool_==Tool::Move || tool_==Tool::Copy)
        transform_dimensions([&](Lattice p){return Lattice{p.x+p1.x-p0.x,p.y+p1.y-p0.y};},tool_==Tool::Copy);
    if(tool_==Tool::Rotate || tool_==Tool::Mirror) {
        const double angle=std::atan2(y1-y0,x1-x0);
        transform_dimensions([&](Lattice p){gsnap::Shape sh;sh.a=sh.b=p;return tool_==Tool::Rotate?gedit::rotate(sh,p0,angle,vol_).a:gedit::mirror(sh,p0,p1,vol_).a;},tool_==Tool::Mirror);
    }
    switch (tool_) {
        case Tool::Move: {
            replace_selection(gedit::translate_all(picked, p1.x - p0.x, p1.y - p0.y));
            move_refs(p1.x - p0.x, p1.y - p0.y);
            move_texts(p1.x - p0.x, p1.y - p0.y);
            message_ = "moved";
            break;
        }
        case Tool::Copy: {
            append(gedit::translate_all(picked, p1.x - p0.x, p1.y - p0.y));
            for (gcm::u32 k : sel_refs_)
                if (const gdoc::BlockRef* ref = blocks_.reference_mut(k)) {
                    gdoc::BlockRef copy = *ref;
                    copy.insertion.x += p1.x - p0.x;
                    copy.insertion.y += p1.y - p0.y;
                    blocks_.insert(copy);
                }
            for (std::size_t ti : sel_texts_)
                if (ti < texts_.size()) {
                    TextItem copy = texts_[ti];
                    copy.pos.x += p1.x - p0.x;
                    copy.pos.y += p1.y - p0.y;
                    texts_.push_back(std::move(copy));
                    ++texts_serial_;
                }
            message_ = "copied";
            break;
        }
        case Tool::Rotate: {
            const double a = std::atan2(y1 - y0, x1 - x0);
            std::vector<gsnap::Shape> out;
            out.reserve(picked.size());
            for (const auto& sh : picked) out.push_back(gedit::rotate(sh, p0, a, vol_));
            replace_selection(out);
            rotate_refs(p0, a);
            rotate_texts(p0, a);
            char buf[48];
            std::snprintf(buf, sizeof(buf), "rotated %.2f\xc2\xb0",
                          a * 180.0 / 3.14159265358979323846);
            message_ = buf;
            break;
        }
        case Tool::Mirror: {
            auto mirrored=gedit::mirror_all(picked,p0,p1,vol_);
            std::map<gcm::u32,gcm::u32> cloud_last;
            for(const auto& sh:mirrored) if(sh.attrs.user.contains("revision_cloud"))
                cloud_last[sh.poly]=std::max(cloud_last[sh.poly],sh.poly_index);
            for(auto& sh:mirrored) if(cloud_last.contains(sh.poly)) sh.poly_index=cloud_last[sh.poly]-sh.poly_index;
            append(mirrored);
            {
                const double theta = std::atan2(double(p1.y - p0.y), double(p1.x - p0.x));
                const double c2 = std::cos(2.0 * theta), s2 = std::sin(2.0 * theta);
                for (gcm::u32 k : sel_refs_)
                    if (const gdoc::BlockRef* ref = blocks_.reference_mut(k)) {
                        gdoc::BlockRef copy = *ref;
                        const double rx = double(ref->insertion.x - p0.x);
                        const double ry = double(ref->insertion.y - p0.y);
                        copy.insertion.x = p0.x + gcm::i64(std::llround(rx * c2 + ry * s2));
                        copy.insertion.y = p0.y + gcm::i64(std::llround(rx * s2 - ry * c2));
                        copy.scale_x = -copy.scale_x;
                        copy.rotation = 2.0 * theta - copy.rotation;
                        blocks_.insert(copy);
                    }
            }
            {
                const double theta2 = std::atan2(double(p1.y - p0.y), double(p1.x - p0.x));
                const double c2 = std::cos(2.0 * theta2), s2 = std::sin(2.0 * theta2);
                for (std::size_t ti : sel_texts_)
                    if (ti < texts_.size()) {
                        TextItem copy = texts_[ti];
                        const double rx = double(copy.pos.x - p0.x);
                        const double ry = double(copy.pos.y - p0.y);
                        copy.pos.x = p0.x + gcm::i64(std::llround(rx * c2 + ry * s2));
                        copy.pos.y = p0.y + gcm::i64(std::llround(rx * s2 - ry * c2));
                        copy.angle = 2.0 * theta2 - copy.angle;
                        texts_.push_back(std::move(copy));
                        ++texts_serial_;
                    }
            }
            message_ = "mirrored";
            break;
        }
        case Tool::ArrayRect: {
            for(int row=0;row<array_rows_;++row)for(int col=0;col<array_columns_;++col)if(row||col)
                transform_dimensions([&](Lattice p){return Lattice{p.x+(p1.x-p0.x)*col,p.y+(p1.y-p0.y)*row};},true);
            append_array(gedit::array_rectangular(picked, array_columns_, array_rows_,
                                            x1 - x0, y1 - y0, vol_));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "array %dx%d", array_columns_, array_rows_);
            message_ = buf;
            break;
        }
        case Tool::ArrayPolar: {
            const bool full=std::abs(std::abs(array_angle_)-2*3.141592653589793)<1e-9;
            for(int i=1;i<array_count_;++i)transform_dimensions([&](Lattice p){gsnap::Shape sh;sh.a=sh.b=p;return gedit::rotate(sh,p0,array_angle_*i/(full?array_count_:std::max(1,array_count_-1)),vol_).a;},true);
            append_array(gedit::array_polar(picked, p0, array_count_, array_angle_, true,
                                      vol_));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "polar array x%d", array_count_);
            message_ = buf;
            break;
        }
        default:
            break;
    }
    gregion::reconcile_joints(shapes_);
    sync_wall_sections();
    if(update_door_openings(undo_stack_.back().shapes))refresh_hatches();
    cancel();
}

static bool segment_gap(double p1x, double p1y, double q1x, double q1y,
                        double p2x, double p2y, double q2x, double q2y,
                        double& ax, double& ay, double& bx, double& by) {
    const double d1x = q1x - p1x, d1y = q1y - p1y;
    const double d2x = q2x - p2x, d2y = q2y - p2y;
    const double l1 = std::hypot(d1x, d1y), l2 = std::hypot(d2x, d2y);
    if (l1 < 1e-12 || l2 < 1e-12) return false;
    const double determinant=d1x*d2y-d1y*d2x;
    const double cross = determinant / (l1 * l2);
    if (std::fabs(cross)>1e-12) {
        const double t=((p2x-p1x)*d2y-(p2y-p1y)*d2x)/determinant;
        const double u=((p2x-p1x)*d1y-(p2y-p1y)*d1x)/determinant;
        if(t>=0&&t<=1&&u>=0&&u<=1) {
            ax=bx=p1x+t*d1x;ay=by=p1y+t*d1y;return false;
        }
    }
    if (std::fabs(cross) < 1e-3) {
        const double ta = ((p2x - p1x) * d1x + (p2y - p1y) * d1y) / (l1 * l1);
        const double tb = ((q2x - p1x) * d1x + (q2y - p1y) * d1y) / (l1 * l1);
        const double lo = std::max(0.0, std::min(ta, tb));
        const double hi = std::min(1.0, std::max(ta, tb));
        const double tm = lo <= hi ? (lo + hi) * 0.5
                                   : std::clamp((ta + tb) * 0.5, 0.0, 1.0);
        ax = p1x + d1x * tm; ay = p1y + d1y * tm;
        double u = ((ax - p2x) * d2x + (ay - p2y) * d2y) / (l2 * l2);
        u = std::clamp(u, 0.0, 1.0);
        bx = p2x + d2x * u; by = p2y + d2y * u;
        return std::hypot(bx - ax, by - ay) > 1e-12;
    }
    double best = 1e300;
    for (int pass = 0; pass < 2; ++pass) {
        const double* sp = pass == 0 ? &p1x : &p2x;
        (void)sp;
        const double ex[2] = {pass == 0 ? p2x : p1x, pass == 0 ? q2x : q1x};
        const double ey[2] = {pass == 0 ? p2y : p1y, pass == 0 ? q2y : q1y};
        const double ox = pass == 0 ? p1x : p2x, oy = pass == 0 ? p1y : p2y;
        const double dx = pass == 0 ? d1x : d2x, dy = pass == 0 ? d1y : d2y;
        const double ll = pass == 0 ? l1 : l2;
        for (int e = 0; e < 2; ++e) {
            double t = ((ex[e] - ox) * dx + (ey[e] - oy) * dy) / (ll * ll);
            t = std::clamp(t, 0.0, 1.0);
            const double fx = ox + dx * t, fy = oy + dy * t;
            const double d = std::hypot(ex[e] - fx, ey[e] - fy);
            if (d < best) {
                best = d;
                if (pass == 0) { ax = fx; ay = fy; bx = ex[e]; by = ey[e]; }
                else           { ax = ex[e]; ay = ey[e]; bx = fx; by = fy; }
            }
        }
    }
    return best > 1e-9;
}

bool Canvas::corner_selection(Tool tool) {
    if ((tool!=Tool::Fillet && tool!=Tool::Chamfer) || selection_.size()!=2) return false;
    auto i=selection_[0],j=selection_[1];
    if(i>=shapes_.size() || j>=shapes_.size()) return false;
    const auto a=shapes_[i],b=shapes_[j];
    const auto midpoint=[](const gsnap::Shape& s) {return Lattice{s.a.x+(s.b.x-s.a.x)/2,s.a.y+(s.b.y-s.a.y)/2};};
    const auto pa=midpoint(a),pb=midpoint(b);
    const auto result=tool==Tool::Fillet ? gedit::fillet_any(a,b,corner_radius_,pa,pb,vol_)
        : gedit::chamfer_any(a,b,chamfer_distance_,chamfer_second_,pa,pb,vol_);
    if(!result.ok) {message_=result.reason; return true;}
    if(is_region(i) || is_region(j)) {
        if(!is_region(i) || !is_region(j) || a.poly!=b.poly) {
            message_="Choose two edges of the same closed object";return true;
        }
        std::vector<gsnap::Shape> boundary;std::vector<std::size_t> replace;
        for(std::size_t k=0;k<shapes_.size();++k) if(shapes_[k].poly==a.poly) {
            replace.push_back(k);boundary.push_back(k==i?result.a:k==j?result.b:shapes_[k]);
        }
        boundary.push_back(result.connector);
        const auto region=gregion::from_shapes(boundary);
        if(!region.ok) {message_=region.error;return true;}
        store_region(region.region,replace,a.attrs,a.layer,a.poly);
    } else {
        mark(); shapes_[i]=result.a;shapes_[j]=result.b;
        auto connector=result.connector;
        if(a.poly && a.poly==b.poly) connector.poly=a.poly;
        push_shape(connector);
    }
    refresh_hatches();rebuild_grips();
    message_=tool==Tool::Fillet?"Selected edges rounded":"Selected edges chamfered";
    return true;
}

bool Canvas::dimension_selection() {
    if (selection_.size() != 2) return false;
    const auto i = selection_[0], j = selection_[1];
    if (i >= shapes_.size() || j >= shapes_.size() ||
        shapes_[i].kind != gsnap::ShapeKind::Segment ||
        shapes_[j].kind != gsnap::ShapeKind::Segment) return false;
    double ax, ay, bx, by, cx, cy, dx, dy, px, py, qx, qy;
    gcm::to_world(vol_, shapes_[i].a, ax, ay);
    gcm::to_world(vol_, shapes_[i].b, bx, by);
    gcm::to_world(vol_, shapes_[j].a, cx, cy);
    gcm::to_world(vol_, shapes_[j].b, dx, dy);
    const double det = (bx-ax)*(dy-cy)-(by-ay)*(dx-cx);
    if (std::abs(det) > 1e-15) {
        const double t = ((cx-ax)*(dy-cy)-(cy-ay)*(dx-cx))/det;
        const double u = ((cx-ax)*(by-ay)-(cy-ay)*(bx-ax))/det;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
            message_ = "Selected edges intersect: distance is zero";
            return true;
        }
    }
    if (!segment_gap(ax,ay,bx,by,cx,cy,dx,dy,px,py,qx,qy)) {
        message_ = "Selected edges touch: distance is zero";
        return true;
    }
    gdim::Dimension d;
    d.style = dim_styles_.current(); d.layer = current_layer_;
    d.kind = gdim::Kind::Aligned;
    d.p1 = gcm::to_lattice(vol_,px,py); d.p2 = gcm::to_lattice(vol_,qx,qy);
    d.p3 = gcm::to_lattice(vol_,(px+qx)*.5,(py+qy)*.5);
    assign_keys();
    d.o1={shapes_[i].key,gdim::Dimension::Anchor::Edge};
    d.o2={shapes_[j].key,gdim::Dimension::Anchor::Edge};
    mark(); dims_.push_back(d);
    set_tool(Tool::Select);
    message_ = "Distance dimension added between selected edges";
    return true;
}

void Canvas::refresh_dimensions() {
    if (dims_.empty()) return;
    for (gdim::Dimension& d : dims_) {
        if (!d.associative()) continue;
        if (d.o1.at==gdim::Dimension::Anchor::Edge && d.o2.at==gdim::Dimension::Anchor::Edge) {
            const auto i=index_of_key(d.o1.shape),j=index_of_key(d.o2.shape);
            d.orphaned=i==npos || j==npos || shapes_[i].kind!=gsnap::ShapeKind::Segment || shapes_[j].kind!=gsnap::ShapeKind::Segment;
            if (d.orphaned) continue;
            double ax,ay,bx,by,cx,cy,dx,dy,px,py,qx,qy;
            gcm::to_world(vol_,shapes_[i].a,ax,ay);gcm::to_world(vol_,shapes_[i].b,bx,by);
            gcm::to_world(vol_,shapes_[j].a,cx,cy);gcm::to_world(vol_,shapes_[j].b,dx,dy);
            if (std::hypot(bx-ax,by-ay)<1e-12 || std::hypot(dx-cx,dy-cy)<1e-12) {d.orphaned=true;continue;}
            segment_gap(ax,ay,bx,by,cx,cy,dx,dy,px,py,qx,qy);
            const double det=(bx-ax)*(dy-cy)-(by-ay)*(dx-cx);
            if (std::abs(det)>1e-15) {
                const double t=((cx-ax)*(dy-cy)-(cy-ay)*(dx-cx))/det;
                const double u=((cx-ax)*(by-ay)-(cy-ay)*(bx-ax))/det;
                if(t>=0&&t<=1&&u>=0&&u<=1) {px=qx=ax+t*(bx-ax);py=qy=ay+t*(by-ay);}
            }
            d.p1=gcm::to_lattice(vol_,px,py);d.p2=gcm::to_lattice(vol_,qx,qy);
            continue;
        }
        if(d.kind==gdim::Kind::Radial || d.kind==gdim::Kind::Diameter) {
            const auto index=index_of_key(d.o1.shape);
            d.orphaned=!d.o1.bound() || d.o1.at!=gdim::Dimension::Anchor::Centre || d.o2.bound() ||
                index==npos || !shapes_[index].is_curve() || shapes_[index].radius<=0;
            if(!d.orphaned) {
                d.centre=shapes_[index].a;d.radius=shapes_[index].radius;
                const auto points=d.radial_points();d.p1=points.first;d.p2=points.second;
            }
            continue;
        }
        bool lost = false;
        const auto resolve = [&](const gdim::Dimension::Ref& r, gcm::Lattice2& out) {
            if (!r.bound()) return;
            const std::size_t i = index_of_key(r.shape);
            if (i == npos) { lost = true; return; }
            const gsnap::Shape& s = shapes_[i];
            if(!gdim::Dimension::anchor_point(s,r.at,out)) {lost=true;return;}
        };
        const auto old1=d.p1,old2=d.p2;
        resolve(d.o1, d.p1);
        resolve(d.o2, d.p2);
        if(d.kind==gdim::Kind::Leader)for(auto& path:d.leader_paths)for(auto& point:path.vertices) {
            if(point.x==old1.x&&point.y==old1.y)point=d.p1;
            else if(point.x==old2.x&&point.y==old2.y)point=d.p2;
        }
        d.orphaned = lost;
    }
}

void Canvas::run_dimension_tool() {
    gdim::Dimension d;
    d.style = dim_styles_.current();
    d.layer = current_layer_;

    const auto ref_at = [&](std::size_t i) {
        gdim::Dimension::Ref r;
        if (i < arc_pt_refs_.size()) {
            r.shape = arc_pt_refs_[i].first;
            r.at    = arc_pt_refs_[i].second;
        }
        return r;
    };

    switch (tool_) {
        case Tool::DimLinear:
            d.kind = gdim::Kind::Linear;
            d.p1 = arc_pts_[0];
            d.p2 = arc_pts_[1];
            d.p3 = arc_pts_[2];
            d.o1 = ref_at(0);
            d.o2 = ref_at(1);
            break;
        case Tool::DimAligned:
            d.kind = gdim::Kind::Aligned;
            d.p1 = arc_pts_[0];
            d.p2 = arc_pts_[1];
            d.p3 = arc_pts_[2];
            d.o1 = ref_at(0);
            d.o2 = ref_at(1);
            break;
        case Tool::DimAngular:
            d.kind = gdim::Kind::Angular;
            d.centre = arc_pts_[0];
            d.p1 = arc_pts_[1];
            d.p2 = arc_pts_[2];
            d.p3 = arc_pts_[3];
            break;
        case Tool::DimRadial:
        case Tool::DimDiameter: {
            if (picked_.empty() || picked_[0] >= shapes_.size()) {
                message_ = "pick a circle or arc";
                cancel();
                return;
            }
            const gsnap::Shape& c = shapes_[picked_[0]];
            d.kind = tool_ == Tool::DimRadial ? gdim::Kind::Radial
                                              : gdim::Kind::Diameter;
            d.centre = c.a;
            d.radius = c.radius;
            d.p3 = arc_pts_.back();
            d.o1.shape = c.key;
            d.o1.at = gdim::Dimension::Anchor::Centre;
            break;
        }
        default:
            cancel();
            return;
    }

    mark();
    dims_.push_back(d);
    const gdim::Style* st = dim_styles_.find(d.style);
    message_ = st ? gdim::format(d, annotative(*st), vol_) : "dimension added";
    cancel();
}

void Canvas::click(double px, double py, bool additive) {
    hover(px, py);
    if(blip_mode && tool_!=Tool::Select) {if(blips.size()>=256) blips.erase(blips.begin());blips.push_back(cursor_);}
    if(selection_add_mode>0 && !additive) additive=true;

    struct ClearForced {
        gsnap::Engine& e;
        ~ClearForced() { e.settings.forced = gsnap::None; }
    } clear_forced{snap_engine_};

    if (tool_ == Tool::Select) {
        select_at(px, py, additive);
        return;
    }

    if (tool_ == Tool::Trim || tool_ == Tool::Extend) {
        const std::size_t i = pick(px, py);
        if (i == npos) { message_ = "nothing under the cursor"; return; }
        if(is_region(i)) {message_="use region booleans or EXPLODE before editing boundary pieces";return;}

        std::vector<gsnap::Shape> others;
        for (std::size_t k = 0; k < shapes_.size(); ++k)
            if (k != i) others.push_back(shapes_[k]);

        const gedit::TrimResult r = tool_ == Tool::Trim
            ? gedit::trim(shapes_[i], others, cursor_, vol_)
            : gedit::extend(shapes_[i], others, cursor_, vol_);
        if (!r.changed) { message_ = r.reason[0] ? r.reason : "no change"; return; }

        mark();
        shapes_.erase(shapes_.begin() + static_cast<long>(i));
        shapes_.insert(shapes_.end(), r.pieces.begin(), r.pieces.end());
        selection_.clear();
        refresh_hatches();
        message_ = tool_ == Tool::Trim ? "trimmed" : "extended";
        return;
    }

    if (tool_ == Tool::Offset) {
        if (picked_.empty()) {
            const std::size_t i = pick(px, py);
            if (i == npos) { message_ = "pick the object to offset"; return; }
            if(is_region(i)) {message_="EXPLODE a region before offsetting its boundary";return;}
            picked_.push_back(i);
            message_ = "pick the side";
            return;
        }
        const auto r = gedit::offset(shapes_[picked_[0]], offset_distance_, cursor_,
                                     vol_);
        if (!r) { message_ = "offset collapses the object"; cancel(); return; }
        mark();
        push_shape(*r);
        message_ = "offset";
        cancel();
        return;
    }

    if (tool_ == Tool::Fillet || tool_ == Tool::Chamfer) {
        const std::size_t i = pick(px, py);
        if (i == npos) { message_ = "pick two lines"; return; }
        if(is_region(i)) {message_="EXPLODE a region before rounding its boundary";return;}
        if (picked_.empty()) {
            picked_.push_back(i);
            arc_pts_.push_back(cursor_);
            message_ = "pick the second line";
            return;
        }
        if (i == picked_[0]) { message_ = "pick a different line"; return; }

        const gsnap::Shape a = shapes_[picked_[0]], b = shapes_[i];
        const bool curved = a.is_curve() || b.is_curve();
        const gedit::CornerResult r =
            tool_ == Tool::Fillet
                ? (curved ? gedit::fillet_any(a, b, corner_radius_, arc_pts_[0], cursor_, vol_)
                          : gedit::fillet(a, b, corner_radius_, arc_pts_[0], cursor_, vol_))
                : (curved ? gedit::chamfer_any(a, b, chamfer_distance_, chamfer_second_,
                                               arc_pts_[0], cursor_, vol_)
                          : gedit::chamfer(a, b, chamfer_distance_, chamfer_second_,
                                           arc_pts_[0], cursor_, vol_));
        if (!r.ok) { message_ = r.reason[0] ? r.reason : "cannot fit"; cancel(); return; }

        mark();
        shapes_[picked_[0]] = r.a;
        shapes_[i] = r.b;
        push_shape(r.connector);
        message_ = tool_ == Tool::Fillet ? "filleted" : "chamfered";
        cancel();
        return;
    }

    if (tool_ == Tool::DimRadial || tool_ == Tool::DimDiameter) {
        if (picked_.empty()) {
            const std::size_t i = pick(px, py);
            if (i == npos || !shapes_[i].is_curve()) {
                message_ = "pick a circle or arc";
                return;
            }
            picked_.push_back(i);
            arc_pts_.clear();
            arc_pt_refs_.clear();
            message_ = "place the leader";
            return;
        }
        arc_pts_.push_back(cursor_);
        run_dimension_tool();
        return;
    }

    if (tool_ == Tool::DimLinear || tool_ == Tool::DimAligned) {
        const std::size_t i = pick(px, py);
        bool interior = false;
        double ax = 0, ay = 0, bx = 0, by = 0;
        if (i != npos && shapes_[i].kind == gsnap::ShapeKind::Segment) {
            gcm::to_world(vol_, shapes_[i].a, ax, ay);
            gcm::to_world(vol_, shapes_[i].b, bx, by);
            double wx, wy;
            view_.world_of(px, py, wx, wy);
            const double tol = 12.0 / view_.scale();
            interior = std::hypot(wx - ax, wy - ay) > tol &&
                       std::hypot(wx - bx, wy - by) > tol;
        }
        if (arc_pts_.empty() && picked_.empty() && !interior &&
            pick_dim(px, py) != npos) {
            set_tool(Tool::Select);
            select_at(px, py, false);
            message_ = "drag to adjust its distance from the measured points";
            return;
        }
        if (arc_pts_.empty() && picked_.empty() && interior) {
            picked_.push_back(i);
            message_ = "pick the second line, or a point";
            return;
        }
        if (arc_pts_.empty() && picked_.size() == 1 && picked_[0] < shapes_.size() &&
            shapes_[picked_[0]].kind == gsnap::ShapeKind::Segment) {
            double p1x, p1y, q1x, q1y, gax, gay, gbx, gby;
            gcm::to_world(vol_, shapes_[picked_[0]].a, p1x, p1y);
            gcm::to_world(vol_, shapes_[picked_[0]].b, q1x, q1y);
            bool have = false;
            if (interior && i != picked_[0]) {
                have = segment_gap(p1x, p1y, q1x, q1y, ax, ay, bx, by,
                                   gax, gay, gbx, gby);
                if (!have) { message_ = "the lines cross -- no gap to dimension"; return; }
            } else {
                double cx, cy;
                gcm::to_world(vol_, cursor_, cx, cy);
                const double dx = q1x - p1x, dy = q1y - p1y;
                const double ll = dx * dx + dy * dy;
                double t = ll > 1e-24 ? ((cx - p1x) * dx + (cy - p1y) * dy) / ll : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                gax = p1x + dx * t; gay = p1y + dy * t;
                gbx = cx; gby = cy;
                have = std::hypot(gbx - gax, gby - gay) > 1e-12;
                if (!have) { message_ = "that point is on the line"; return; }
            }
            gdim::Dimension d;
            d.style = dim_styles_.current();
            d.layer = current_layer_;
            d.kind = gdim::Kind::Aligned;
            d.p1 = gcm::to_lattice(vol_, gax, gay);
            d.p2 = gcm::to_lattice(vol_, gbx, gby);
            d.p3 = gcm::to_lattice(vol_, (gax + gbx) * 0.5, (gay + gby) * 0.5);
            if (interior && i!=picked_[0]) {
                assign_keys();
                d.o1={shapes_[picked_[0]].key,gdim::Dimension::Anchor::Edge};
                d.o2={shapes_[i].key,gdim::Dimension::Anchor::Edge};
            }
            mark();
            dims_.push_back(d);
            const gdim::Style* st = dim_styles_.find(d.style);
            message_ = st ? gdim::format(d, annotative(*st), vol_) : "dimension added";
            cancel();
            set_tool(Tool::Select);
            return;
        }
    }

    if (tool_is_dimension(tool_)) {
        arc_pts_.push_back(cursor_);
        arc_pt_refs_.push_back({snap_key_, snap_anchor_});
        pending_ = true;
        anchor_ = arc_pts_[0];
        if (arc_pts_.size() >= picks_needed()) run_dimension_tool();
        return;
    }

    if (tool_uses_selection(tool_)) {
        if (selection_.empty() && sel_refs_.empty() && sel_texts_.empty() && sel_dims_.empty()) {
            select_at(px, py, additive);
            message_ = (selection_.empty() && sel_refs_.empty() && sel_texts_.empty())
                           ? "nothing under the cursor"
                           : "pick the base point";
            return;
        }
        arc_pts_.push_back(cursor_);
        pending_ = true;
        anchor_ = arc_pts_[0];
        if (arc_pts_.size() >= picks_needed()) run_edit_tool();
        return;
    }

    switch (tool_) {
        case Tool::Line:
            place(cursor_);
            break;

        case Tool::Polyline:
            if (chain_.empty()) { mark(); begin_polyline(); }
            chain_.push_back(cursor_);
            if (chain_.size() >= 2)
                add_segment(chain_[chain_.size() - 2], chain_.back());
            pending_ = true;
            anchor_ = cursor_;
            break;

        case Tool::Rect: {
            const auto four = [&](Lattice a, Lattice b, Lattice c, Lattice d) {
                mark();
                begin_polyline();
                add_segment(a, b);
                add_segment(b, c);
                add_segment(c, d);
                add_segment(d, a);
                end_polyline();
            };
            if (rect_mode_ == 2) {
                arc_pts_.push_back(cursor_);
                pending_ = true;
                anchor_ = arc_pts_[0];
                if (arc_pts_.size() == 3) {
                    const Lattice p = arc_pts_[0], q = arc_pts_[1], r = arc_pts_[2];
                    const double ex = double(q.x - p.x), ey = double(q.y - p.y), l = std::hypot(ex, ey);
                    if (l > 0.5) {
                        const double nx = -ey / l, ny = ex / l;
                        const double w = (double(r.x - p.x)) * nx + (double(r.y - p.y)) * ny;
                        const Lattice c{q.x + gcm::i64(std::llround(nx * w)), q.y + gcm::i64(std::llround(ny * w))};
                        const Lattice d{p.x + gcm::i64(std::llround(nx * w)), p.y + gcm::i64(std::llround(ny * w))};
                        four(p, q, c, d);
                    }
                    arc_pts_.clear();
                    arc_pt_refs_.clear();
                    pending_ = false;
                }
                break;
            }
            if (!pending_) { anchor_ = cursor_; pending_ = true; }
            else {
                Lattice a = anchor_, c = cursor_;
                if (rect_mode_ == 1) {
                    a = {2 * anchor_.x - cursor_.x, 2 * anchor_.y - cursor_.y};
                }
                const Lattice b{c.x, a.y}, d{a.x, c.y};
                four(a, b, c, d);
                pending_ = false;
            }
            break;
        }

        case Tool::Circle:
            if (circle_mode_ == 2) {
                arc_pts_.push_back(cursor_);
                pending_ = true;
                anchor_ = arc_pts_[0];
                if (arc_pts_.size() == 3) {
                    Lattice c;
                    gcm::i64 r;
                    if (circumcircle(arc_pts_[0], arc_pts_[1], arc_pts_[2], c, r)) { mark(); add_circle(c, r); }
                    arc_pts_.clear();
                    arc_pt_refs_.clear();
                    pending_ = false;
                }
                break;
            }
            if (!pending_) { anchor_ = cursor_; pending_ = true; }
            else {
                mark();
                if (circle_mode_ == 1) {
                    const Lattice c{(anchor_.x + cursor_.x) / 2, (anchor_.y + cursor_.y) / 2};
                    add_circle(c, radius_of(anchor_, cursor_) / 2);
                } else {
                    add_circle(anchor_, radius_of(anchor_, cursor_));
                }
                pending_ = false;
            }
            break;

        case Tool::Point: {
            mark();
            gsnap::Shape p;
            p.kind = gsnap::ShapeKind::Point;
            p.a = cursor_;
            push_shape(p);
            break;
        }

        case Tool::Ray:
        case Tool::Xline:
            if (!pending_) { anchor_ = cursor_; pending_ = true; }
            else {
                mark();
                gsnap::Shape s;
                s.kind = tool_ == Tool::Ray ? gsnap::ShapeKind::Ray
                                            : gsnap::ShapeKind::Xline;
                s.a = anchor_;
                s.b = cursor_;
                if (s.a.x != s.b.x || s.a.y != s.b.y) push_shape(s);
                anchor_ = anchor_;
            }
            break;

        case Tool::Ellipse:
            arc_pts_.push_back(cursor_);
            pending_ = true;
            anchor_ = arc_pts_[0];
            if (arc_pts_.size() == 3) {
                const Lattice c = arc_pts_[0];
                const double minor = distance_to_axis(vol_, c, arc_pts_[1],
                                                      arc_pts_[2]);
                mark();
                gsnap::Shape e;
                e.kind = gsnap::ShapeKind::Ellipse;
                e.a = c;
                e.b = arc_pts_[1];
                e.radius = static_cast<gcm::i64>(std::llround(minor * vol_.scale()));
                if (e.radius > 0) push_shape(e);
                arc_pts_.clear();
                arc_pt_refs_.clear();
                pending_ = false;
            }
            break;

        case Tool::Multiline: {
            if (mline_pts_.empty()) mark();
            mline_pts_.push_back(cursor_);
            pending_ = true;
            anchor_ = cursor_;
            if (mline_pts_.size() >= 2) rebuild_multiline();
            break;
        }

        case Tool::Spline:
            if (spline_pts_.empty()) mark();
            spline_pts_.push_back(cursor_);
            pending_ = true;
            anchor_ = cursor_;
            if (spline_pts_.size() >= 3) rebuild_spline();
            break;

        case Tool::Arc:
            arc_pts_.push_back(cursor_);
            pending_ = true;
            anchor_ = arc_pts_[0];
            if (arc_pts_.size() == 3) {
                if (arc_mode_ == 1) {
                    Lattice c;
                    gcm::i64 r;
                    if (circumcircle(arc_pts_[0], arc_pts_[1], arc_pts_[2], c, r)) {
                        double a0 = angle_of(vol_, c, arc_pts_[0]);
                        double am = angle_of(vol_, c, arc_pts_[1]);
                        double a1 = angle_of(vol_, c, arc_pts_[2]);
                        const auto ccw_between = [](double s, double e, double m) {
                            const double tau = 2.0 * 3.14159265358979323846;
                            double se = std::fmod(e - s + tau, tau), sm = std::fmod(m - s + tau, tau);
                            return sm <= se;
                        };
                        if (!ccw_between(a0, a1, am)) std::swap(a0, a1);
                        mark();
                        add_arc(c, r, a0, a1);
                    }
                } else if (arc_mode_ == 2) {
                    const Lattice c = arc_pts_[1];
                    mark();
                    add_arc(c, radius_of(c, arc_pts_[0]), angle_of(vol_, c, arc_pts_[0]), angle_of(vol_, c, arc_pts_[2]));
                } else {
                    const Lattice c = arc_pts_[0];
                    mark();
                    add_arc(c, radius_of(c, arc_pts_[1]),
                            angle_of(vol_, c, arc_pts_[1]),
                            angle_of(vol_, c, arc_pts_[2]));
                }
                arc_pts_.clear();
                arc_pt_refs_.clear();
                pending_ = false;
            }
            break;

        default:
            break;
    }
}

std::vector<std::string> Canvas::tool_modes(Tool t) {
    switch (t) {
        case Tool::Rect:   return {"Two corners", "From centre", "Edge and width"};
        case Tool::Circle: return {"Centre, radius", "Two points (diameter)", "Three points"};
        case Tool::Arc:    return {"Centre, start, end", "Three points", "Start, centre, end"};
        default:           return {};
    }
}

int Canvas::tool_mode(Tool t) const noexcept {
    switch (t) {
        case Tool::Rect:   return rect_mode_;
        case Tool::Circle: return circle_mode_;
        case Tool::Arc:    return arc_mode_;
        default:           return 0;
    }
}

void Canvas::set_tool_mode(Tool t, int mode) {
    const int n = static_cast<int>(tool_modes(t).size());
    if (n == 0) return;
    mode = std::clamp(mode, 0, n - 1);
    switch (t) {
        case Tool::Rect:   rect_mode_ = mode; break;
        case Tool::Circle: circle_mode_ = mode; break;
        case Tool::Arc:    arc_mode_ = mode; break;
        default: break;
    }
    cancel();
}

void Canvas::cancel() noexcept {
    pending_ = false;
    end_polyline();
    chain_.clear();
    arc_pts_.clear();
    arc_pt_refs_.clear();
    picked_.clear();
    spline_pts_.clear();
    mline_pts_.clear();
    mline_first_ = shapes_.size();
}

void Canvas::undo() {
    if (undo_stack_.empty()) return;
    ++edit_serial_;
    redo_stack_.push_back({shapes_, dims_, selection_, blocks_.references(), sel_refs_, texts_, sel_texts_, hatches_, splines_, constraints_, sel_dims_, sel_hatches_});
    const Snapshot s = undo_stack_.back();
    undo_stack_.pop_back();
    shapes_ = s.shapes;
    dims_ = s.dims;
    selection_ = s.selection;
    blocks_.set_references(s.refs);
    sel_refs_ = s.sel_refs;
    texts_ = s.texts;
    sel_texts_ = s.sel_texts; sel_dims_=s.sel_dims; sel_hatches_=s.sel_hatches;
    hatches_ = s.hatches;
    splines_ = s.splines;
    constraints_ = s.constraints;
    ++texts_serial_;
    cancel();
    refresh_symbol_legends(true);
}

void Canvas::redo() {
    if (redo_stack_.empty()) return;
    ++edit_serial_;
    undo_stack_.push_back({shapes_, dims_, selection_, blocks_.references(), sel_refs_, texts_, sel_texts_, hatches_, splines_, constraints_, sel_dims_, sel_hatches_});
    const Snapshot s = redo_stack_.back();
    redo_stack_.pop_back();
    shapes_ = s.shapes;
    dims_ = s.dims;
    selection_ = s.selection;
    blocks_.set_references(s.refs);
    sel_refs_ = s.sel_refs;
    texts_ = s.texts;
    sel_texts_ = s.sel_texts; sel_dims_=s.sel_dims; sel_hatches_=s.sel_hatches;
    hatches_ = s.hatches;
    splines_ = s.splines;
    constraints_ = s.constraints;
    ++texts_serial_;
    cancel();
    refresh_symbol_legends(true);
}

void Canvas::clear() {
    mark();
    hatches_.clear();
    shapes_.clear();
    splines_.clear();
    dims_.clear();
    sel_dims_.clear(); sel_hatches_.clear();
    selection_.clear();
    cancel();
}



void Canvas::rebuild_grips() {
    grips_.clear();
    if (selection_.empty()) return;

    for (std::size_t i : selection_) {
        if (i >= shapes_.size()) continue;
        if (!shape_visible(shapes_[i]) || is_region(i)) continue;
        if(shapes_[i].poly && shapes_[i].attrs.user.count("door"))continue;
        std::vector<gedit::Grip> g = gedit::grips_of(shapes_[i], vol_);
        for (gedit::Grip& one : g) {
            if(is_revision_cloud(i) && one.kind==gedit::GripKind::Centre) continue;
            one.shape = static_cast<gcm::u32>(i);
            grips_.push_back(one);
        }
    }
}

void Canvas::begin_band(double px, double py) {
    band_ = Band{};
    band_.active = true;
    band_.x0 = band_.x1 = px;
    band_.y0 = band_.y1 = py;
}

void Canvas::update_band(double px, double py) {
    if (!band_.active) return;
    band_.x1 = px;
    band_.y1 = py;
    band_.crossing = crossing_direction(band_.x0, band_.x1);
}

std::size_t Canvas::end_band(bool add) {
    if (!band_.active) return npos;
    const double dx = band_.x1 - band_.x0;
    const double dy = band_.y1 - band_.y0;
    const Band b = band_;
    band_ = Band{};

    constexpr double kMinDrag = 4.0;
    if (std::fabs(dx) < kMinDrag && std::fabs(dy) < kMinDrag) return npos;

    const std::size_t n = select_area(b.x0, b.y0, b.x1, b.y1, add);
    message_ = n == 0 ? "nothing in the window"
                      : std::to_string(n) + (n == 1 ? " object selected"
                                                    : " objects selected");
    return n;
}

bool Canvas::select_and_drag(double px, double py) {
    if (tool_ != Tool::Select || dragging_selection_) return false;
    if (!select_hit(px,py,false,false)) return false;
    if (!sel_dims_.empty()) {
        const auto dh=sel_dims_.front();
        dragging_dim_=int(dh);drag_dim_orig_=dims_[dh];dragging_dim_point_=3;
        if(dims_[dh].kind==gdim::Kind::Leader) {
            double best=pickbox_pixels;
            const auto grips=dims_[dh].leader_grips();
            dragging_dim_point_=int(grips.size());
            for(std::size_t i=0;i<grips.size();++i) {
                const auto p=grips[i];
                double x,y,sx,sy;gcm::to_world(vol_,p,x,y);view_.screen_of(x,y,sx,sy);
                const double distance=std::hypot(px-sx,py-sy);
                if(distance<=best) {best=distance;dragging_dim_point_=int(i+1);}
            }
        }
        drag_engaged_=false;drag_only_doors_=false;drag_last_delta_.reset();drag_px_=px;drag_py_=py;
        view_.world_of(px,py,drag_from_x_,drag_from_y_);
        dragging_selection_=true;
        message_=dims_[dh].kind==gdim::Kind::Leader ? "leader: drag point, Esc puts it back"
            : "dimension: drag closer or farther, measured points stay attached";
        return true;
    }
    return begin_selection_drag(px,py);
}

void Canvas::rescale_document(double factor) {
    if (factor <= 0.0 || std::fabs(factor - 1.0) < 1e-12) return;
    mark();
    const auto sc = [&](gcm::i64& v) { v = gcm::i64(std::llround(double(v) * factor)); };
    const auto scale_shape = [&](gsnap::Shape& sh) {
        sc(sh.a.x); sc(sh.a.y); sc(sh.b.x); sc(sh.b.y);
        sh.radius = gcm::i64(std::llround(double(sh.radius) * factor));
    };
    const auto scale_underlay=[&](gdoc::Underlay& u){sc(u.insertion.x);sc(u.insertion.y);u.scale*=factor;};
    auto underlays=underlays_.all();underlays_=gdoc::UnderlayTable{};
    for(auto& u:underlays){scale_underlay(u);underlays_.attach(std::move(u));}
    for (auto& sh : shapes_) scale_shape(sh);
    for (auto& t : texts_) { sc(t.pos.x); sc(t.pos.y); t.height *= factor; }
    ++texts_serial_;
    for (auto& d : dims_) {
        d.transform_points([&](auto& point){sc(point.x);sc(point.y);});
        d.radius = gcm::i64(std::llround(double(d.radius) * factor));
    }
    if (gdim::Style* ds = dim_styles_.find_mut("Standard")) {
        ds->dimtxt *= factor;
        ds->dimasz *= factor;
        ds->dimexe *= factor;
        ds->dimexo *= factor;
        ds->dimgap *= factor;
    }
    for (gdoc::BlockDef def : blocks_.all()) {
        for (auto& sh : def.geometry) scale_shape(sh);
        for(auto& u:def.underlays)scale_underlay(u);
        sc(def.base.x); sc(def.base.y);
        for(auto& ref:def.nested_refs){sc(ref.insertion.x);sc(ref.insertion.y);}
        for (auto& n : def.notes) { sc(n.pos.x); sc(n.pos.y); n.height *= factor; n.width *= factor; }
        blocks_.define(def);
    }
    for (gcm::u32 i = 0; i < gcm::u32(blocks_.references().size()); ++i)
        if (gdoc::BlockRef* ref = blocks_.reference_mut(i)) { sc(ref->insertion.x); sc(ref->insertion.y); }
    for (auto x : blocks_.xrefs()) {
        sc(x.insertion.x); sc(x.insertion.y);
        for (auto& p : x.clip) { sc(p.x); sc(p.y); }
        blocks_.attach(std::move(x));
    }
    refresh_hatches();
    rebuild_grips();
    message_ = "document rescaled";
}

void Canvas::scale_selected_text(double factor) {
    if (sel_texts_.empty() || factor <= 0.0) { message_ = "no text selected"; return; }
    mark();
    for (std::size_t ti : sel_texts_)
        if (ti < texts_.size()) texts_[ti].height *= factor;
    ++texts_serial_;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "text height %s", factor > 1.0 ? "up" : "down");
    message_ = buf;
}

bool Canvas::begin_selection_drag(double px, double py) {
    if (selection_.empty() && sel_refs_.empty() && sel_texts_.empty()) return false;
    if (!selected_at(px,py)) return false;

    view_.world_of(px, py, drag_from_x_, drag_from_y_);
    drag_original_.clear();
    drag_original_.reserve(selection_.size());
    for (std::size_t i : selection_)
        if (i < shapes_.size()) drag_original_.push_back(shapes_[i]);
    drag_refs_original_.clear();
    for (gcm::u32 k : sel_refs_)
        if (const gdoc::BlockRef* ref = blocks_.reference_mut(k)) drag_refs_original_.push_back(*ref);
    drag_texts_original_.clear();
    for (std::size_t ti : sel_texts_)
        if (ti < texts_.size()) drag_texts_original_.push_back(texts_[ti]);

    drag_engaged_ = false;
    drag_px_ = px;
    drag_py_ = py;
    drag_last_delta_.reset();
    drag_only_doors_=!selection_.empty()&&sel_refs_.empty()&&sel_texts_.empty()&&
        std::all_of(selection_.begin(),selection_.end(),[&](auto i){return i<shapes_.size()&&
            (shapes_[i].layer=="A-DOOR"||shapes_[i].attrs.user.contains("door"));});
    dragging_selection_ = true;
    message_ = std::to_string(selection_.size()) +
               (selection_.size() == 1 ? " object" : " objects") + ": drag to move";
    return true;
}

void Canvas::update_selection_drag(double px, double py) {
    if (!dragging_selection_) return;
    hover(px, py);
    double wx, wy;
    view_.world_of(px, py, wx, wy);

    const gcm::Lattice2 from = gcm::to_lattice(vol_, drag_from_x_, drag_from_y_);
    const gcm::Lattice2 to   = gcm::to_lattice(vol_, wx, wy);
    const gcm::Lattice2 d{to.x - from.x, to.y - from.y};
    if(drag_last_delta_&&drag_last_delta_->x==d.x&&drag_last_delta_->y==d.y)return;

    if (!drag_engaged_) {
        if (std::hypot(px - drag_px_, py - drag_py_) < 4.0) return;
        mark();
        drag_engaged_ = true;
    }

    drag_last_delta_=d;
    std::vector<gsnap::Shape> before_openings;
    for(const auto& s:shapes_)if(s.kind==gsnap::ShapeKind::Arc&&s.attrs.user.contains("opening_auto"))before_openings.push_back(s);
    std::vector<DraftingBounds> dirty;
    const auto include_doors=[&] {for(auto i:selection_)if(i<shapes_.size())dirty.push_back(drafting_bounds(shapes_[i]).expanded(.501/vol_.inv_scale()));};
    if(drag_only_doors_)include_doors();
    for (std::size_t k = 0; k < selection_.size() && k < drag_original_.size(); ++k) {
        const std::size_t i = selection_[k];
        if (i >= shapes_.size()) continue;
        gsnap::Shape moved = drag_original_[k];
        moved.a.x += d.x; moved.a.y += d.y;
        moved.b.x += d.x; moved.b.y += d.y;
        shapes_[i] = moved;
    }
    for (std::size_t k = 0; k < sel_refs_.size() && k < drag_refs_original_.size(); ++k)
        if (gdoc::BlockRef* ref = blocks_.reference_mut(sel_refs_[k])) {
            *ref = drag_refs_original_[k];
            ref->insertion.x += d.x;
            ref->insertion.y += d.y;
        }
    for (std::size_t k = 0; k < sel_texts_.size() && k < drag_texts_original_.size(); ++k)
        if (sel_texts_[k] < texts_.size()) {
            TextItem moved = drag_texts_original_[k];
            moved.pos.x += d.x;
            moved.pos.y += d.y;
            texts_[sel_texts_[k]] = std::move(moved);
            ++texts_serial_;
        }
    if (dragging_dim_ >= 0 && dragging_dim_ < int(dims_.size())) {
        gdim::Dimension moved = drag_dim_orig_;
        if(moved.kind==gdim::Kind::Leader) {
            const auto points=moved.leader_grips();const auto index=std::size_t(dragging_dim_point_-1);
            if(index<points.size())moved.move_leader_grip(index,{points[index].x+d.x,points[index].y+d.y});
        } else {
            double nx=0, ny=0;
            if (moved.kind==gdim::Kind::Aligned) {
                const double dx=double(moved.p2.x)-moved.p1.x,dy=double(moved.p2.y)-moved.p1.y;
                const double length=std::hypot(dx,dy);
                if (length>0) {nx=-dy/length;ny=dx/length;}
            } else if (moved.kind==gdim::Kind::Horizontal || moved.kind==gdim::Kind::Vertical || moved.kind==gdim::Kind::Linear) {
                const bool horizontal=moved.kind==gdim::Kind::Horizontal ||
                    (moved.kind==gdim::Kind::Linear && gdim::linear_is_horizontal(moved,vol_));
                nx=horizontal?0:1;ny=horizontal?1:0;
                if (moved.kind==gdim::Kind::Linear)
                    moved.kind=horizontal?gdim::Kind::Horizontal:gdim::Kind::Vertical;
            } else if (moved.kind==gdim::Kind::Ordinate) {
                moved.p2.x+=d.x;moved.p2.y+=d.y;
            } else {
                moved.p3.x+=d.x;moved.p3.y+=d.y;
            }
            const double offset=double(d.x)*nx+double(d.y)*ny;
            const gcm::Lattice2 shift{gcm::i64(std::llround(offset*nx)),gcm::i64(std::llround(offset*ny))};
            moved.p3.x+=shift.x;moved.p3.y+=shift.y;
            moved.extension_offset.x+=shift.x;moved.extension_offset.y+=shift.y;
        }
        dims_[std::size_t(dragging_dim_)] = std::move(moved);
    }
    ++edit_serial_;
    if(!drag_only_doors_) {gregion::reconcile_joints(shapes_);sync_wall_sections();}
    const bool openings_changed=update_door_openings(before_openings);
    if(drag_only_doors_) {include_doors();refresh_hatches(dirty);}
    else if(openings_changed)refresh_hatches();
    rebuild_grips();
}

bool Canvas::end_selection_drag() {
    if (!dragging_selection_) return false;
    dragging_selection_ = false;
    drag_engaged_ = false;
    dragging_dim_ = -1;
    drag_original_.clear();
    drag_texts_original_.clear();
    rebuild_grips();
    if(!drag_only_doors_)refresh_hatches();
    drag_only_doors_=false;drag_last_delta_.reset();
    return true;
}

void Canvas::cancel_selection_drag() {
    if (!dragging_selection_) return;
    for (std::size_t k = 0; k < sel_refs_.size() && k < drag_refs_original_.size(); ++k)
        if (gdoc::BlockRef* ref = blocks_.reference_mut(sel_refs_[k])) *ref = drag_refs_original_[k];
    for (std::size_t k = 0; k < sel_texts_.size() && k < drag_texts_original_.size(); ++k)
        if (sel_texts_[k] < texts_.size()) { texts_[sel_texts_[k]] = drag_texts_original_[k]; ++texts_serial_; }
    if (dragging_dim_ >= 0 && dragging_dim_ < int(dims_.size()))
        dims_[std::size_t(dragging_dim_)] = drag_dim_orig_;
    dragging_dim_ = -1;
    for (std::size_t k = 0; k < selection_.size() && k < drag_original_.size(); ++k) {
        const std::size_t i = selection_[k];
        if (i < shapes_.size()) shapes_[i] = drag_original_[k];
    }
    if(drag_engaged_ && !undo_stack_.empty()) {
        const auto& original=undo_stack_.back();
        shapes_=original.shapes;hatches_=original.hatches;selection_=original.selection;
        undo_stack_.pop_back();--undo_mark_serial_;++edit_serial_;
    }
    dragging_selection_ = false;
    drag_engaged_ = false;
    drag_original_.clear();
    drag_texts_original_.clear();
    drag_only_doors_=false;drag_last_delta_.reset();
    rebuild_grips();
    refresh_hatches();
}

std::vector<gedit::GripOption> Canvas::grip_options(double px, double py) const {
    const_cast<Canvas*>(this)->rebuild_grips();
    if (grips_.empty()) return {};
    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const gedit::Grip hit = gedit::pick_grip(
        grips_, gcm::to_lattice(vol_, wx, wy), vol_, 10.0 / view_.scale());
    if (hit.shape == gedit::kNoGrip || hit.shape >= shapes_.size()) return {};
    if(is_revision_cloud(hit.shape)) return {{gedit::GripOp::Stretch,"Stretch",true}};
    return gedit::grip_menu(shapes_[hit.shape], hit);
}

bool Canvas::apply_grip_option(double px, double py, gedit::GripOp op, Lattice to) {
    rebuild_grips();
    if (grips_.empty()) return false;
    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const gedit::Grip hit = gedit::pick_grip(
        grips_, gcm::to_lattice(vol_, wx, wy), vol_, 10.0 / view_.scale());
    if (hit.shape == gedit::kNoGrip || hit.shape >= shapes_.size()) {
        message_ = "no grip there";
        return false;
    }
    if(is_revision_cloud(hit.shape)) {
        if(op!=gedit::GripOp::Stretch) {message_="cloud grips stretch vertices or scallops";return false;}
        mark();drag_cloud_grip(hit,to);rebuild_grips();refresh_hatches();return true;
    }
    const auto made = gedit::apply_grip_op(shapes_[hit.shape], hit, op, to, vol_);
    if (!made) {
        message_ = std::string(gedit::grip_op_name(op)) + ": cannot";
        return false;
    }
    mark();
    gsnap::Shape out = *made;
    out.layer = shapes_[hit.shape].layer;
    out.attrs = shapes_[hit.shape].attrs;
    out.poly = shapes_[hit.shape].poly;
    out.poly_index = shapes_[hit.shape].poly_index;
    out.key = shapes_[hit.shape].key;
    shapes_[hit.shape] = std::move(out);
    rebuild_grips();
    message_ = gedit::grip_op_name(op);
    return true;
}

bool Canvas::begin_grip_drag(double px, double py) {
    rebuild_grips();
    if (grips_.empty()) return false;

    double wx, wy;
    view_.world_of(px, py, wx, wy);
    const gedit::Grip hit = gedit::pick_grip(
        grips_, gcm::to_lattice(vol_, wx, wy), vol_, 10.0 / view_.scale());
    if (hit.shape == gedit::kNoGrip) return false;

    mark();
    hot_grip_ = hit;
    dragging_grip_ = true;
    message_ = std::string("grip: ") + gedit::grip_kind_name(hit.kind);
    return true;
}

void Canvas::update_grip_drag(double px, double py) {
    if (!dragging_grip_) return;
    hover(px, py);
    if (hot_grip_.shape >= shapes_.size()) return;

    if(drag_cloud_grip(hot_grip_,cursor_)) return;
    const auto out = gedit::drag_grip(shapes_[hot_grip_.shape], hot_grip_, cursor_,
                                      vol_);
    if (out) shapes_[hot_grip_.shape] = *out;
}

bool Canvas::end_grip_drag() {
    if (!dragging_grip_) return false;
    dragging_grip_ = false;
    rebuild_grips();
    refresh_hatches();
    return true;
}

void Canvas::cancel_grip_drag() {
    if (!dragging_grip_) return;
    dragging_grip_ = false;
    undo();
}

std::vector<gsnap::Shape> Canvas::boundary_at_world(gcm::Lattice2 seed) const {
    std::vector<gsnap::Shape> out;
    const gdoc::BoundaryResult loops = gdoc::trace_region(boundary_segments(), seed);
    if (!loops.ok()) return out;
    for (const auto& loop : loops.loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            gsnap::Shape s;
            s.kind = gsnap::ShapeKind::Segment;
            s.a = loop[i];
            s.b = loop[(i + 1) % loop.size()];
            if (s.a.x != s.b.x || s.a.y != s.b.y) out.push_back(s);
        }
    }
    return out;
}

std::vector<gdoc::Segment2> Canvas::boundary_segments() const {
    std::vector<gdoc::Segment2> out;
    grender::d2::DrawList tmp;
    build_geometry(tmp);
    tmp.flatten_arcs_chordal(kModelChordTolerance);
    for (const auto& l : tmp.lines()) {
        if (l.role != Role::Geometry && l.role != Role::Selected) continue;
        out.push_back({gcm::to_lattice(vol_, l.x0, l.y0),
                       gcm::to_lattice(vol_, l.x1, l.y1)});
    }
    return out;
}

std::vector<gdoc::Segment2> Canvas::segments_of(const std::vector<gsnap::Shape>& shapes) const {
    std::vector<gdoc::Segment2> out;
    grender::d2::DrawList tmp;
    for (const auto& s : shapes) emit_shape(tmp, s, Role::Geometry);
    tmp.flatten_arcs_chordal(kModelChordTolerance);
    for (const auto& l : tmp.lines()) {
        if (l.role != Role::Geometry && l.role != Role::Selected) continue;
        out.push_back({gcm::to_lattice(vol_, l.x0, l.y0), gcm::to_lattice(vol_, l.x1, l.y1)});
    }
    return out;
}

std::vector<gsnap::Shape> Canvas::hatch_boundary(const gdoc::AssociativeHatch& hatch) const {
    if (hatch.orphaned) return {};
    if (hatch.area_background) return background_boundary(hatch);
    if (!hatch.style.associative && !hatch.boundary_geometry.empty())
        return hatch.boundary_geometry;

    const auto segments=boundary_segments();
    const auto region=hatch.seed_valid ? gdoc::trace_region(segments,hatch.seed)
                                      : gdoc::extract_loops(segments);
    if (!region.ok()) return {};
    using Edge=std::array<gcm::i64,4>;
    const auto key=[](gdoc::Segment2 edge) -> Edge {
        if (edge.b.x<edge.a.x || (edge.b.x==edge.a.x && edge.b.y<edge.a.y))
            std::swap(edge.a,edge.b);
        return {edge.a.x,edge.a.y,edge.b.x,edge.b.y};
    };
    std::map<Edge,gdoc::Segment2> remaining;
    for (const auto& loop:region.loops)
        for (std::size_t i=0;i<loop.size();++i) {
            const gdoc::Segment2 edge{loop[i],loop[(i+1)%loop.size()]};
            if (edge.a.x!=edge.b.x || edge.a.y!=edge.b.y) remaining.emplace(key(edge),edge);
        }

    std::vector<gsnap::Shape> boundary;
    for (const auto& shape:shapes_) {
        if (shape.kind!=gsnap::ShapeKind::Segment && shape.kind!=gsnap::ShapeKind::Arc &&
            shape.kind!=gsnap::ShapeKind::Circle) continue;
        const auto candidate=segments_of({shape});
        if (candidate.empty() || !std::all_of(candidate.begin(),candidate.end(),
            [&](const auto& edge) { return remaining.count(key(edge))!=0; })) continue;
        boundary.push_back(shape);
        for (const auto& edge:candidate) remaining.erase(key(edge));
    }
    for (const auto& entry:remaining) {
        gsnap::Shape shape;
        shape.kind=gsnap::ShapeKind::Segment;
        shape.a=entry.second.a; shape.b=entry.second.b;
        boundary.push_back(shape);
    }
    return boundary;
}

bool Canvas::add_hatch(const std::vector<gsnap::Shape>& boundary, gcm::Lattice2 seed,
                       std::string_view pattern, double scale, double angle_deg, std::string layer, gdoc::Attributes attrs) {
    const std::vector<gdoc::Segment2> segs = segments_of(boundary);
    const gdoc::BoundaryResult loops = gdoc::extract_loops(segs);
    if (!loops.ok() || loops.loops.empty()) {
        message_ = "no closed boundary around that point";
        return false;
    }
    gdoc::AssociativeHatch h;
    h.style = hatch_style_;
    h.style.name = std::string(pattern);
    h.style.scale = scale;
    h.style.angle = angle_deg;
    h.style.associative = false;
    h.layer=layer.empty() ? current_layer_ : std::move(layer);
    h.attrs=std::move(attrs);
    h.seed = seed;
    h.seed_valid = false;
    h.boundary_geometry = boundary;
    h.boundary_shapes.resize(segs.size());
    for (gcm::u32 i = 0; i < segs.size(); ++i) h.boundary_shapes[i] = i;
    const gdoc::HatchUpdate u = gdoc::update_hatch(h, segs, patterns_, vol_);
    if (!u.regenerated) { message_ = u.reason; return false; }
    hatches_.push_back(std::move(h));
    return true;
}

bool Canvas::hatch_at(double px, double py, std::string_view pattern) {
    double wx, wy;
    view_.world_of(px, py, wx, wy);
    return hatch_at_world(gcm::to_lattice(vol_, wx, wy), pattern, hatch_style_.scale, hatch_style_.angle);
}

bool Canvas::hatch_at_world(gcm::Lattice2 seed, std::string_view pattern, double scale, double angle_deg) {

    const std::vector<gdoc::Segment2> segs = boundary_segments();
    const gdoc::BoundaryResult loops = gdoc::trace_region(segs, seed);
    if (!loops.ok() || loops.loops.empty()) {
        message_ = "no closed boundary around that point";
        return false;
    }

    gdoc::AssociativeHatch h;
    h.style = hatch_style_;
    h.style.name = std::string(pattern);
    h.style.scale = scale;
    h.style.angle = angle_deg;
    h.style.associative = true;
    h.layer=current_layer_;
    h.seed = seed;
    h.seed_valid = true;
    h.boundary_shapes.resize(segs.size());
    for (gcm::u32 i = 0; i < segs.size(); ++i) h.boundary_shapes[i] = i;

    const gdoc::HatchUpdate u = gdoc::update_hatch(h, segs, patterns_, vol_);
    if (!u.regenerated) {
        message_ = u.reason;
        return false;
    }

    mark();
    hatches_.push_back(std::move(h));
    message_ = std::string("hatched with ") + std::string(pattern);
    return true;
}

bool Canvas::ensure_wall_hatch(gcm::Lattice2 seed, double scale, double angle_deg) {
    const auto boundaries=gdoc::extract_loops(boundary_segments());
    const auto region=gdoc::region_containing(boundaries,seed);
    if(!region.ok() || region.loops.empty()) {
        message_="no closed wall core around that point";
        return false;
    }
    const auto belongs=[&](const gdoc::AssociativeHatch& hatch) {
        const auto tag=hatch.attrs.user.find("automatic_wall_core");
        if(tag==hatch.attrs.user.end() || tag->second!="1" ||
            !hatch.style.associative || !hatch.seed_valid || hatch.area_background) return false;
        const auto other=gdoc::region_containing(boundaries,hatch.seed);
        if(!other.ok() || other.loops.empty())return false;
        const auto& a=region.loops.front();const auto& b=other.loops.front();
        return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),
            [](auto p,auto q){return p.x==q.x && p.y==q.y;});
    };
    const auto matches=std::count_if(hatches_.begin(),hatches_.end(),belongs);
    if(matches==1) return true;
    if(matches>1) {
        mark();
        bool kept=false;
        std::erase_if(hatches_,[&](const auto& hatch) {
            if(!belongs(hatch)) return false;
            if(!kept) {kept=true;return false;}
            return true;
        });
        sel_hatches_.clear();
        message_="Joined wall core retains its existing hatch style";
        return true;
    }
    if(!hatch_at_world(seed,"ANSI31",scale,angle_deg)) return false;
    hatches_.back().attrs.user["automatic_wall_core"]="1";
    refresh_wall_hatches();
    return true;
}

std::size_t Canvas::refresh_wall_hatches(const std::vector<DraftingBounds>& dirty) {
    const auto automatic=[](const auto& h) {
        const auto tag=h.attrs.user.find("automatic_wall_core");
        return tag!=h.attrs.user.end() && tag->second=="1" && h.style.associative && !h.area_background;
    };
    const auto tracked=[&](const auto& h) {
        return automatic(h) || (h.attrs.user.count("wall_style_association") && h.style.associative && !h.area_background);
    };
    const auto affected=[&](const auto& h){return drafting_hatch_affected(h,dirty,8);};
    if(std::none_of(hatches_.begin(),hatches_.end(),[&](const auto& h){
        return affected(h)&&(tracked(h) || (h.layer=="A-WALL" && h.style.associative && !h.area_background));
    }))return 0;
    std::vector<gsnap::Shape> walls;
    for(const auto& s:shapes_) {
        const auto type=s.attrs.user.find("sketch_object");
        if(type!=s.attrs.user.end() && type->second=="wall")walls.push_back(s);
    }
    if(!dirty.empty()) {
        auto queries=dirty;
        for(const auto& h:hatches_)if(tracked(h)&&affected(h))queries.push_back(drafting_bounds(h).expanded(8));
        const auto nearby=nearby_wall_contours(walls,queries);
        std::vector<gsnap::Shape> local;
        for(std::size_t i=0;i<walls.size();++i)if(nearby[i])local.push_back(std::move(walls[i]));
        walls=std::move(local);
    }
    const auto all=gdoc::extract_loops(segments_of(walls));
    struct Core {gdoc::BoundaryResult loops;gcm::Lattice2 seed;std::vector<gsnap::Shape> boundary;};
    std::vector<Core> cores;
    const auto probes=[&](const auto& loops) {
        std::vector<gcm::Lattice2> points;
        for(const auto& loop:loops) {
            const double side=gdoc::loop_area_sign(loop)>0?1:-1;
            for(std::size_t i=0;i<loop.size();++i) {
                const auto a=loop[i],b=loop[(i+1)%loop.size()];
                const double dx=double(b.x)-a.x,dy=double(b.y)-a.y,len=std::hypot(dx,dy);
                if(len<1)continue;
                const gcm::Lattice2 p{a.x+(b.x-a.x)/2+gcm::i64(std::llround(-dy/len*4*side)),
                    a.y+(b.y-a.y)/2+gcm::i64(std::llround(dx/len*4*side))};
                if(gdoc::point_in_region(p,loops))points.push_back(p);
            }
        }
        return points;
    };
    if(all.ok())for(const auto& loop:all.loops) {
        const auto seeds=probes(std::vector<gdoc::Loop>{loop});
        const auto seed=std::find_if(seeds.begin(),seeds.end(),[&](auto p){return gdoc::point_in_region(p,all.loops);});
        if(seed==seeds.end())continue;
        Core core;core.seed=*seed;core.loops=gdoc::region_containing(all,*seed);
        if(!core.loops.ok())continue;
        const auto nested=core.loops.loops;
        core.loops.loops.resize(1);
        for(std::size_t i=1;i<nested.size();++i) {
            bool deeper=false;
            for(std::size_t j=1;j<nested.size();++j)
                if(i!=j && !nested[i].empty() && gdoc::point_in_loop(nested[i].front(),nested[j])) {deeper=true;break;}
            if(!deeper)core.loops.loops.push_back(nested[i]);
        }
        for(const auto& boundary:core.loops.loops)for(std::size_t i=0;i<boundary.size();++i) {
            gsnap::Shape edge;edge.a=boundary[i];edge.b=boundary[(i+1)%boundary.size()];
            core.boundary.push_back(std::move(edge));
        }
        cores.push_back(std::move(core));
    }
    std::size_t changed=0;
    for(auto& h:hatches_)if(affected(h)&&!tracked(h) && h.layer=="A-WALL" && h.style.associative && !h.area_background && h.seed_valid) {
        if(std::any_of(cores.begin(),cores.end(),[&](const auto& core){return gdoc::point_in_region(h.seed,core.loops.loops);})) {
            h.attrs.user["wall_style_association"]="1";++changed;
        }
    }
    std::vector<std::vector<gdoc::AssociativeHatch>> replacements(hatches_.size());
    std::vector<bool> covered(hatches_.size());
    for(const auto& core:cores) {
        const auto samples=probes(core.loops.loops);
        std::size_t auto_owner=hatches_.size();
        std::vector<std::size_t> owners;
        for(std::size_t i=0;i<hatches_.size();++i) {
            const auto& h=hatches_[i];if(!tracked(h)||!affected(h))continue;
            bool match=h.seed_valid && gdoc::point_in_region(h.seed,core.loops.loops);
            if(!match && !h.boundary_geometry.empty()) {
                const auto prior=gdoc::extract_loops(segments_of(h.boundary_geometry));
                if(prior.ok())match=std::any_of(samples.begin(),samples.end(),[&](auto p){return gdoc::point_in_region(p,prior.loops);});
            }
            if(match) {
                covered[i]=true;
                if(!automatic(h))owners.push_back(i);
                else if(auto_owner==hatches_.size())auto_owner=i;
            }
        }
        if(auto_owner!=hatches_.size())owners.push_back(auto_owner);
        for(const auto owner:owners) {
            auto h=hatches_[owner];
            if(!h.seed_valid || !gdoc::point_in_region(h.seed,core.loops.loops))h.seed=core.seed;
            h.seed_valid=true;h.boundary_geometry=core.boundary;
            const auto boundary=segments_of(h.boundary_geometry);
            h.boundary_shapes.resize(boundary.size());
            for(std::size_t i=0;i<boundary.size();++i)h.boundary_shapes[i]=gcm::u32(i);
            if(gdoc::update_hatch(h,boundary,patterns_,vol_).regenerated)++changed;
            replacements[owner].push_back(std::move(h));
        }
    }
    std::vector<gdoc::AssociativeHatch> next;
    std::vector<std::size_t> selection;
    for(std::size_t i=0;i<hatches_.size();++i) {
        const bool selected=std::find(sel_hatches_.begin(),sel_hatches_.end(),i)!=sel_hatches_.end();
        const auto append=[&](auto h){if(selected)selection.push_back(next.size());next.push_back(std::move(h));};
        if(!tracked(hatches_[i])||!affected(hatches_[i]))append(std::move(hatches_[i]));
        else if(covered[i])for(auto& h:replacements[i])append(std::move(h));
        else {
            auto h=std::move(hatches_[i]);
            if(h.valid || !h.orphaned)++changed;
            h.valid=false;h.orphaned=true;append(std::move(h));
        }
    }
    if(next.size()!=hatches_.size())++changed;
    hatches_=std::move(next);sel_hatches_=std::move(selection);
    return changed;
}

bool Canvas::gradient_at_point(double px, double py) {
    if (!hatch_at(px, py, "SOLID")) return false;
    message_ = std::string("gradient: ") + gdoc::gradient_kind_name(gradient_.kind);
    return true;
}

Canvas::CleanupReport Canvas::merge_overlaps(double tol, bool dedupe, bool openings, std::optional<OpeningCut> opening) {
    CleanupReport rep;
    const auto wall_face=[](const gsnap::Shape& shape) {
        const auto type=shape.attrs.user.find("sketch_object");
        return shape.kind==gsnap::ShapeKind::Segment && shape.layer=="A-WALL" &&
            (type==shape.attrs.user.end() || type->second=="wall");
    };
    const auto world = [&](const gsnap::Point& p, double& x, double& y) {
        gcm::to_world(vol_, p, x, y);
    };
    modify_shapes([&](std::vector<gsnap::Shape>& shapes) {
        if (openings) {
            struct Axis { double ax, ay, ux, uy, len; };
            std::vector<Axis> axes;
            const auto covered = [&](double ax, double ay, double bx, double by,
                                     double len) {
                for (const auto& e : axes) {
                    if (std::fabs((ax - e.ax) * e.uy - (ay - e.ay) * e.ux) > tol * 4.0)
                        continue;
                    const double s0 = (ax - e.ax) * e.ux + (ay - e.ay) * e.uy;
                    const double s1 = (bx - e.ax) * e.ux + (by - e.ay) * e.uy;
                    if (std::min(s0, s1) > e.len - tol || std::max(s0, s1) < tol)
                        continue;
                    return true;
                }
                return false;
            };
            const auto add_axis = [&](double ax, double ay, double bx, double by) {
                const double len = std::hypot(bx - ax, by - ay);
                if (len < 0.35 || covered(ax, ay, bx, by, len)) return;
                axes.push_back({ax, ay, (bx - ax) / len, (by - ay) / len, len});
            };
            const auto wall_support = [&](double ax, double ay, double ux,
                                          double uy, double len) {
                int n = 0;
                for (const auto& s : shapes) {
                    if (!wall_face(s))
                        continue;
                    double x0, y0, x1, y1;
                    world(s.a, x0, y0);
                    world(s.b, x1, y1);
                    const double sl = std::hypot(x1 - x0, y1 - y0);
                    if (sl < 1e-9) continue;
                    if (std::fabs(((x1 - x0) * uy - (y1 - y0) * ux) / sl) > 0.05)
                        continue;
                    const double mx = (x0 + x1) * 0.5 - ax, my = (y0 + y1) * 0.5 - ay;
                    if (std::fabs(mx * uy - my * ux) > 0.25) continue;
                    const double mid = mx * ux + my * uy;
                    if (mid < -0.25 || mid > len + 0.25) continue;
                    ++n;
                }
                return n;
            };
            if(opening) {
                double ax,ay,bx,by;world(opening->from,ax,ay);world(opening->to,bx,by);add_axis(ax,ay,bx,by);
            }
            if(!opening) for (const auto& s : shapes) {
                const auto host=s.attrs.user.find("opening_host");
                if(host!=s.attrs.user.end() && host->second=="none")continue;
                const auto type=s.attrs.user.find("sketch_object");
                if(s.poly && type!=s.attrs.user.end() && (type->second=="window" || type->second=="rollup")) {
                    if(s.poly_index!=0)continue;
                    const auto other=std::find_if(shapes.begin(),shapes.end(),[&](const auto& edge) {
                        return edge.poly==s.poly && edge.poly_index==1 && edge.kind==gsnap::ShapeKind::Segment;
                    });
                    if(other==shapes.end())continue;
                    double ax,ay,bx,by,cx,cy,dx,dy;
                    world(s.a,ax,ay);world(s.b,bx,by);world(other->a,cx,cy);world(other->b,dx,dy);
                    add_axis((ax+cx)*.5,(ay+cy)*.5,(bx+dx)*.5,(by+dy)*.5);
                    continue;
                }
                if (s.layer == "A-DOOR" && s.kind == gsnap::ShapeKind::Arc) {
                    double cx, cy;
                    world(s.a, cx, cy);
                    const double r = double(s.radius) * vol_.inv_scale();
                    const auto try_angle = [&](double ang, double& bx, double& by,
                                               int& sup) {
                        bx = cx + std::cos(ang) * r;
                        by = cy + std::sin(ang) * r;
                        const double len = std::hypot(bx - cx, by - cy);
                        sup = len < 1e-9 ? 0
                                         : wall_support(cx, cy, (bx - cx) / len,
                                                        (by - cy) / len, len);
                    };
                    double b0x, b0y, b1x, b1y;
                    int sup0 = 0, sup1 = 0;
                    try_angle(s.start_angle, b0x, b0y, sup0);
                    try_angle(s.end_angle, b1x, b1y, sup1);
                    bool identified=false;
                    for(const auto& leaf:shapes) {
                        if(leaf.layer!="A-DOOR" || leaf.kind!=gsnap::ShapeKind::Segment || (s.poly && leaf.poly!=s.poly)) continue;
                        double ax,ay,bx,by;world(leaf.a,ax,ay);world(leaf.b,bx,by);
                        if(std::hypot(bx-cx,by-cy)<tol) {std::swap(ax,bx);std::swap(ay,by);}
                        if(std::hypot(ax-cx,ay-cy)>tol) continue;
                        if(std::hypot(bx-b0x,by-b0y)<tol) {add_axis(cx,cy,b1x,b1y);identified=true;break;}
                        if(std::hypot(bx-b1x,by-b1y)<tol) {add_axis(cx,cy,b0x,b0y);identified=true;break;}
                    }
                    if(identified) continue;
                    if (sup0 == 0 && sup1 == 0) continue;
                    if (sup0 >= sup1) add_axis(cx, cy, b0x, b0y);
                    else add_axis(cx, cy, b1x, b1y);
                } else if ((s.layer == "A-DOOR" || s.layer == "A-GLAZ") &&
                           s.kind == gsnap::ShapeKind::Segment) {
                    double ax, ay, bx, by;
                    world(s.a, ax, ay);
                    world(s.b, bx, by);
                    const double len = std::hypot(bx - ax, by - ay);
                    bool swing_leaf=false;
                    if(s.layer=="A-DOOR") for(const auto& arc:shapes) {
                        if(arc.layer!="A-DOOR" || arc.kind!=gsnap::ShapeKind::Arc) continue;
                        double cx,cy;world(arc.a,cx,cy);
                        const double radius=arc.radius*vol_.inv_scale();
                        if(std::abs(radius-len)<=tol && (std::hypot(ax-cx,ay-cy)<=tol || std::hypot(bx-cx,by-cy)<=tol)) swing_leaf=true;
                    }
                    if(swing_leaf) continue;
                    if (len < 0.35) continue;
                    if (wall_support(ax, ay, (bx - ax) / len, (by - ay) / len,
                                     len) == 0)
                        continue;
                    add_axis(ax, ay, bx, by);
                }
            }
            for (const auto& e : axes) {
                std::vector<std::pair<double, double>> lo_pts, hi_pts;
                const double c0 = 0, c1 = e.len;
                if(opening) {
                    shapes.erase(std::remove_if(shapes.begin(),shapes.end(),[&](const auto& s) {
                        if(!wall_face(s) ||
                           (opening->wall_group && s.poly!=opening->wall_group))return false;
                        double ax,ay,bx,by;world(s.a,ax,ay);world(s.b,bx,by);
                        const double p=(ax-e.ax)*e.ux+(ay-e.ay)*e.uy,q=(bx-e.ax)*e.ux+(by-e.ay)*e.uy;
                        const double da=(ax-e.ax)*e.uy-(ay-e.ay)*e.ux,db=(bx-e.ax)*e.uy-(by-e.ay)*e.ux;
                        const double half=opening->thickness*.5;
                        const double end_tolerance=std::max(2*vol_.inv_scale(),1e-9);
                        return std::abs(p-q)<tol && p>=-end_tolerance && p<=e.len+end_tolerance &&
                            da*db<0 && std::abs(std::abs(da)-half)<tol && std::abs(std::abs(db)-half)<tol;
                    }),shapes.end());
                }
                for (std::size_t i = 0; i < shapes.size(); ++i) {
                    gsnap::Shape& s = shapes[i];
                    if (!wall_face(s))
                        continue;
                    if(opening && opening->wall_group && s.poly!=opening->wall_group)continue;
                    double x0, y0, x1, y1;
                    world(s.a, x0, y0);
                    world(s.b, x1, y1);
                    const double sl = std::hypot(x1 - x0, y1 - y0);
                    if (sl < 1e-9) continue;
                    if (std::fabs(((x1 - x0) * e.uy - (y1 - y0) * e.ux) / sl) > 0.05)
                        continue;
                    const double d0 = (x0 - e.ax) * e.uy - (y0 - e.ay) * e.ux;
                    const double reach=opening?opening->thickness*.5+tol:.25;
                    if (std::fabs(d0) > reach) continue;
                    double p0 = (x0 - e.ax) * e.ux + (y0 - e.ay) * e.uy;
                    double p1 = (x1 - e.ax) * e.ux + (y1 - e.ay) * e.uy;
                    bool flipped = p1 < p0;
                    if (flipped) { std::swap(p0, p1); std::swap(x0, x1); std::swap(y0, y1); }
                    const double ov0 = std::max(p0, c0), ov1 = std::min(p1, c1);
                    if (ov1 - ov0 < tol * 2.0) continue;
                    const auto at = [&](double p, double& px, double& py) {
                        const double t2 = (p - p0) / (p1 - p0);
                        px = x0 + (x1 - x0) * t2;
                        py = y0 + (y1 - y0) * t2;
                    };
                    double cx0, cy0, cx1, cy1;
                    at(ov0, cx0, cy0);
                    at(ov1, cx1, cy1);
                    const double remnant=opening?std::max(2*vol_.inv_scale(),1e-9):.02;
                    const bool keep_lo = ov0 - p0 > remnant;
                    const bool keep_hi = p1 - ov1 > remnant;
                    if ((!opening || keep_lo) && ov0 - c0 < tol * 4.0) lo_pts.push_back({cx0, cy0});
                    if ((!opening || keep_hi) && c1 - ov1 < tol * 4.0) hi_pts.push_back({cx1, cy1});
                    ++rep.cut;
                    if (keep_lo) {
                        gsnap::Shape tail = s;
                        s.a = gcm::to_lattice(vol_, x0, y0);
                        s.b = gcm::to_lattice(vol_, cx0, cy0);
                        if (keep_hi) {
                            tail.key = 0;
                            tail.a = gcm::to_lattice(vol_, cx1, cy1);
                            tail.b = gcm::to_lattice(vol_, x1, y1);
                            shapes.push_back(std::move(tail));
                        }
                    } else if (keep_hi) {
                        s.a = gcm::to_lattice(vol_, cx1, cy1);
                        s.b = gcm::to_lattice(vol_, x1, y1);
                    } else {
                        shapes.erase(shapes.begin() + long(i));
                        --i;
                        ++rep.removed;
                    }
                }
                for (auto* side : {&lo_pts, &hi_pts}) {
                    if (side->size() != 2) continue;
                    const auto& p = (*side)[0];
                    const auto& q = (*side)[1];
                    const double d = std::hypot(q.first - p.first, q.second - p.second);
                    if (opening ? (d<=2*vol_.inv_scale() || d>opening->thickness+2*tol) : (d<.02 || d>.4)) continue;
                    gsnap::Shape cap;
                    cap.kind = gsnap::ShapeKind::Segment;
                    cap.layer = "A-WALL";
                    cap.a = gcm::to_lattice(vol_, p.first, p.second);
                    cap.b = gcm::to_lattice(vol_, q.first, q.second);
                    for(const auto& wall:shapes) {
                        const auto same=[](auto a,auto b){return a.x==b.x && a.y==b.y;};
                        if(wall_face(wall) &&
                           (same(wall.a,cap.a)||same(wall.b,cap.a)||same(wall.a,cap.b)||same(wall.b,cap.b))) {
                            cap.attrs=wall.attrs;cap.poly=wall.poly;break;
                        }
                    }
                    shapes.push_back(std::move(cap));
                    ++rep.caps;
                }
            }
        }
        if(rep.cut) for(auto& shape:shapes) {
            if(shape.layer=="A-WALL" && shape.region) {
                shape.region=false;shape.region_loop=0;shape.region_reversed=false;
            }
        }
        if (dedupe) {
            for (int pass = 0; pass < 6; ++pass) {
                bool changed = false;
                for (std::size_t i = 0; i < shapes.size(); ++i) {
                    gsnap::Shape& si = shapes[i];
                    if (si.kind != gsnap::ShapeKind::Segment) continue;
                    double ax, ay, bx, by;
                    world(si.a, ax, ay);
                    world(si.b, bx, by);
                    double len = std::hypot(bx - ax, by - ay);
                    if (len < 1e-9) continue;
                    double ux = (bx - ax) / len, uy = (by - ay) / len;
                    for (std::size_t j = i + 1; j < shapes.size(); ++j) {
                        const gsnap::Shape& sj = shapes[j];
                        if (sj.kind != gsnap::ShapeKind::Segment ||
                            sj.layer != si.layer)
                            continue;
                        const auto ti=si.attrs.user.find("sketch_object");
                        const auto tj=sj.attrs.user.find("sketch_object");
                        if((ti==si.attrs.user.end())!=(tj==sj.attrs.user.end()))continue;
                        if(ti!=si.attrs.user.end() && (ti->second!=tj->second || si.poly!=sj.poly))continue;
                        double cx, cy, dx, dy;
                        world(sj.a, cx, cy);
                        world(sj.b, dx, dy);
                        if (std::fabs((cx - ax) * uy - (cy - ay) * ux) > tol ||
                            std::fabs((dx - ax) * uy - (dy - ay) * ux) > tol)
                            continue;
                        double s0 = (cx - ax) * ux + (cy - ay) * uy;
                        double s1 = (dx - ax) * ux + (dy - ay) * uy;
                        auto low=sj.a,high=sj.b;
                        if (s1 < s0) {std::swap(s0, s1);std::swap(low,high);}
                        if (s1 < -tol || s0 > len + tol) continue;
                        if(s0<0)si.a=low;
                        if(s1>len)si.b=high;
                        shapes.erase(shapes.begin() + long(j));
                        ++rep.merged;
                        changed = true;
                        world(si.a, ax, ay);
                        world(si.b, bx, by);
                        len = std::hypot(bx - ax, by - ay);
                        ux=(bx-ax)/len;uy=(by-ay)/len;
                        --j;
                    }
                }
                if (!changed) break;
            }
        }
    });
    refresh_hatches();
    return rep;
}

std::size_t Canvas::refresh_hatches(const std::vector<DraftingBounds>& dirty) {
    if (hatches_.empty()) return 0;
    std::optional<std::vector<gdoc::Segment2>> segs;

    std::size_t changed = refresh_wall_hatches(dirty);
    for (auto& h : hatches_) {
        if((h.attrs.user.count("automatic_wall_core") || h.attrs.user.count("wall_style_association")) && h.style.associative && !h.area_background)continue;
        if(!drafting_hatch_affected(h,dirty,h.gap_tolerance/vol_.inv_scale()+8))continue;
        ++drafting_work_.hatch_updates;
        if (h.area_background) {
            const auto boundary=background_boundary(h,!dirty.empty());
            if(boundary.empty()) {if(h.valid || !h.orphaned)++changed;h.valid=false;h.orphaned=true;continue;}
            const auto own=segments_of(boundary);
            h.boundary_shapes.resize(own.size());
            for(std::size_t i=0;i<own.size();++i)h.boundary_shapes[i]=gcm::u32(i);
            if(gdoc::update_hatch(h,own,patterns_,vol_).regenerated)++changed;
            h.boundary_geometry=boundary;
            continue;
        }
        if (!h.style.associative) {
            if (!h.valid && !h.boundary_geometry.empty()) {
                const auto own_segments=segments_of(h.boundary_geometry);
                h.boundary_shapes.resize(own_segments.size());
                for (gcm::u32 i=0;i<own_segments.size();++i) h.boundary_shapes[i]=i;
                if (gdoc::update_hatch(h,own_segments,patterns_,vol_).regenerated) ++changed;
            }
            continue;
        }
        if(!segs)segs=boundary_segments();
        h.boundary_shapes.resize(segs->size());
        for (gcm::u32 i = 0; i < segs->size(); ++i) h.boundary_shapes[i] = i;
        if (gdoc::update_hatch(h, *segs, patterns_, vol_).regenerated) ++changed;
    }
    if(changed) static_key_=~gcm::u64{0};
    return changed;
}

void Canvas::emit_hatches(grender::d2::DrawList& out) const {
    for (const auto& h : hatches_) {
        if (!h.valid || h.orphaned || layer_hidden(h.layer)) continue;
        // Construction role so hatch lines never join a region trace.
        grender::Color col = grender::color_for(grender::Role::Geometry);
        if (!h.layer.empty()) {
            if (const auto c=color_of_layer(h.layer); c.a>=0.0f) col=c;
        } else if (!h.boundary_shapes.empty() && h.boundary_shapes.front() < shapes_.size())
            if (const auto c = color_of_layer(shapes_[h.boundary_shapes.front()].layer);
                c.a >= 0.0f)
                col = c;
        gsnap::Shape appearance;appearance.layer=h.layer;appearance.attrs=h.attrs;
        if(h.attrs.color.kind!=gdoc::Color::Kind::ByLayer || h.attrs.transparency>=0)
            col=color_of_shape(appearance);
        if(col.a<0)col=grender::color_for(Role::Geometry);
        if(h.cached.solid) {
            std::vector<gcm::Lattice2> points;
            std::vector<std::vector<gcm::u32>> loops;
            for(const auto& loop:h.cached.fill_loops) {
                loops.emplace_back();
                for(auto p:loop){loops.back().push_back(gcm::u32(points.size()));points.push_back(p);}
            }
            const auto indices=gtopo::triangulate_face(loops,points);
            for(std::size_t i=0;i+2<indices.size();i+=3) {
                grender::d2::TriangleItem t{};t.color=col;
                for(int k=0;k<3;++k)gcm::to_world(vol_,points[indices[i+k]],t.xy[2*k],t.xy[2*k+1]);
                out.triangle(t);
            }
        }
        for (const auto& s : h.cached.lines) {
            double x0, y0, x1, y1;
            gcm::to_world(vol_, s.a, x0, y0);
            gcm::to_world(vol_, s.b, x1, y1);
            out.line(x0, y0, x1, y1, Role::Construction, col);
        }
    }
}

gsolve::System Canvas::build_system(std::vector<gcm::u32>& entity_of_shape) const {
    gsolve::System sys;
    entity_of_shape.assign(shapes_.size(), gsolve::kInvalid);

    std::map<std::pair<gcm::i64, gcm::i64>, gcm::u32> points;
    auto point_for = [&](Lattice l) {
        const auto key = std::make_pair(l.x, l.y);
        const auto it = points.find(key);
        if (it != points.end()) return it->second;
        double x, y;
        gcm::to_world(vol_, l, x, y);
        const gcm::u32 id = sys.add_point(x, y);
        points.emplace(key, id);
        return id;
    };

    std::vector<std::vector<gcm::u32>> pts_of_shape(shapes_.size());
    for (std::size_t i = 0; i < shapes_.size(); ++i) {
        const gsnap::Shape& sh = shapes_[i];
        if (sh.kind == gsnap::ShapeKind::Segment) {
            const gcm::u32 a = point_for(sh.a), b = point_for(sh.b);
            pts_of_shape[i] = {a, b};
            entity_of_shape[i] = sys.add_line(a, b);
        } else if (sh.is_curve()) {
            const gcm::u32 c = point_for(sh.a);
            pts_of_shape[i] = {c};
            const double r = double(sh.radius) * vol_.inv_scale();
            entity_of_shape[i] = sh.kind == gsnap::ShapeKind::Circle
                                     ? sys.add_circle(c, r)
                                     : sys.add_arc(c, r, sh.start_angle, sh.end_angle);
        }
    }

    std::vector<bool> free_point(sys.entity_count(), false);
    for (std::size_t i : selection_)
        if (i < pts_of_shape.size())
            for (gcm::u32 pid : pts_of_shape[i])
                if (pid < free_point.size()) free_point[pid] = true;

    for (const auto& kv : points)
        if (!free_point[kv.second]) sys.fix(kv.second);

    for (const StoredConstraint& sc : constraints_) {
        const std::size_t ia = index_of_key(sc.a);
        if (ia == npos || ia >= entity_of_shape.size()) continue;
        if (entity_of_shape[ia] == gsolve::kInvalid) continue;
        gsolve::Constraint c;
        c.kind = sc.kind;
        c.value = sc.value;
        c.driving = sc.driving;
        c.entities.push_back(entity_of_shape[ia]);
        if (sc.b != 0) {
            const std::size_t ib = index_of_key(sc.b);
            if (ib == npos || ib >= entity_of_shape.size()) continue;
            if (entity_of_shape[ib] == gsolve::kInvalid) continue;
            c.entities.push_back(entity_of_shape[ib]);
        }
        sys.add(c);
    }

    return sys;
}

bool Canvas::resolve_constraints() {
    if (constraints_.empty()) return false;

    const std::vector<std::size_t> saved = selection_;
    selection_.clear();
    for (std::size_t i = 0; i < shapes_.size(); ++i) selection_.push_back(i);

    std::vector<gcm::u32> entity_of_shape;
    gsolve::System sys = build_system(entity_of_shape);
    const gsolve::SolveResult r = sys.solve();
    selection_ = saved;
    if (!r.ok()) {
        message_ = r.message();
        return false;
    }
    write_back(sys, entity_of_shape);
    return true;
}

std::size_t Canvas::prune_constraints() {
    const std::size_t before = constraints_.size();
    std::vector<StoredConstraint> kept;
    kept.reserve(before);
    for (const StoredConstraint& c : constraints_) {
        if (index_of_key(c.a) == npos) continue;
        if (c.b != 0 && index_of_key(c.b) == npos) continue;
        kept.push_back(c);
    }
    constraints_ = std::move(kept);
    return before - constraints_.size();
}

void Canvas::write_back(const gsolve::System& sys,
                        const std::vector<gcm::u32>& entity_of_shape) {
    for (std::size_t i = 0; i < shapes_.size(); ++i) {
        const gcm::u32 e = entity_of_shape[i];
        if (e == gsolve::kInvalid) continue;
        gsnap::Shape& sh = shapes_[i];

        if (sh.kind == gsnap::ShapeKind::Segment) {
            const gsolve::Entity& ent = sys.entities()[e];
            double x, y;
            sys.point_at(ent.p0, x, y);
            sh.a = gcm::to_lattice(vol_, x, y);
            sys.point_at(ent.p1, x, y);
            sh.b = gcm::to_lattice(vol_, x, y);
        } else if (sh.is_curve()) {
            const gsolve::Entity& ent = sys.entities()[e];
            double x, y;
            sys.point_at(ent.p0, x, y);
            sh.a = gcm::to_lattice(vol_, x, y);
            sh.radius = static_cast<gcm::i64>(
                std::llround(sys.radius_of(e) * vol_.scale()));
        }
    }
}

bool Canvas::apply_constraint(gsolve::Kind k, double value) {
    if (selection_.empty()) { message_ = "select the geometry first"; return false; }

    std::size_t arity = 2;
    switch (k) {
        case gsolve::Kind::Horizontal:
        case gsolve::Kind::Vertical:
        case gsolve::Kind::FixPoint:
        case gsolve::Kind::Radius:
        case gsolve::Kind::Diameter:
            arity = 1;
            break;
        default:
            arity = 2;
            break;
    }

    std::vector<gcm::u32> entity_of_shape;
    gsolve::System sys = build_system(entity_of_shape);

    std::vector<gcm::u32> args;
    for (std::size_t i : selection_) {
        if (i >= entity_of_shape.size()) continue;
        if (entity_of_shape[i] == gsolve::kInvalid) continue;
        args.push_back(entity_of_shape[i]);
    }

    if (args.size() < arity) {
        message_ = "select more geometry";
        return false;
    }

    std::vector<std::vector<gcm::u32>> groups;
    if (arity == 1)
        for (gcm::u32 e : args) groups.push_back({e});
    else
        groups.push_back({args[0], args[1]});

    for (const auto& g : groups) {
        gsolve::Constraint c;
        c.kind = k;
        c.entities = g;
        c.value = value;
        sys.add(c);
    }

    const gsolve::SolveResult r = sys.solve();
    if (!r.ok()) {
        message_ = r.message();
        return false;
    }

    mark();
    write_back(sys, entity_of_shape);

    for (const auto& g : groups) {
        StoredConstraint sc;
        sc.kind = k;
        sc.value = value;
        for (std::size_t i = 0; i < entity_of_shape.size() && i < shapes_.size(); ++i) {
            if (entity_of_shape[i] == gsolve::kInvalid) continue;
            if (entity_of_shape[i] == g[0]) sc.a = shapes_[i].key;
            if (g.size() > 1 && entity_of_shape[i] == g[1]) sc.b = shapes_[i].key;
        }
        if (sc.a != 0 && (g.size() == 1 || sc.b != 0)) constraints_.push_back(sc);
    }

    const gsolve::Dof d = sys.analyse();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s applied \xe2\x80\x94 %s",
                  gsolve::kind_name(k), d.describe());
    message_ = buf;
    return true;
}

gsolve::Dof Canvas::dof() const {
    std::vector<gcm::u32> entity_of_shape;
    return build_system(entity_of_shape).analyse();
}

std::vector<gsolve::Constraint> Canvas::suggest_constraints() const {
    std::vector<gcm::u32> entity_of_shape;
    const gsolve::System sys = build_system(entity_of_shape);
    return gsolve::infer_constraints(sys, inference_);
}

std::size_t Canvas::auto_constrain() {
    if (shapes_.empty()) {
        message_ = "nothing to constrain";
        return 0;
    }

    const std::vector<std::size_t> saved = selection_;
    select_all();

    std::vector<gcm::u32> entity_of_shape;
    gsolve::System sys = build_system(entity_of_shape);
    const std::size_t added = gsolve::auto_constrain(sys, inference_);

    if (added == 0) {
        selection_ = saved;
        message_ = "nothing to infer";
        return 0;
    }

    const gsolve::SolveResult r = sys.solve_clusters();
    if (!r.ok()) {
        selection_ = saved;
        message_ = r.message();
        return 0;
    }

    mark();
    write_back(sys, entity_of_shape);
    selection_ = saved;

    const gsolve::Dof d = sys.analyse();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "auto-constrain: %zu applied \xe2\x80\x94 %s",
                  added, d.describe());
    message_ = buf;
    return added;
}

gdim::Style Canvas::styled_for(const gdim::Style& st, double feature_scale) const {
    gdim::Style out = st;
    out.dimscale = st.dimscale * feature_scale;
    if (out.suffix.empty()) {
        out.unit = unit_;
        if (unit_ == gunit::Length::Foot) {
            out.format = gunit::Style::Architectural;
        } else if (unit_ == gunit::Length::Inch) {
            out.suffix = "\"";
        } else {
            out.suffix = std::string(" ") + gunit::suffix(unit_);
        }
    }
    return out;
}

double Canvas::auto_annotation_factor() const noexcept {
    if (auto_annotation_serial_ == edit_serial_) return auto_annotation_;
    auto_annotation_serial_ = edit_serial_;
    double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
    const auto span = [&](Lattice p) {
        double x, y;
        gcm::to_world(vol_, p, x, y);
        lo_x = std::min(lo_x, x); hi_x = std::max(hi_x, x);
        lo_y = std::min(lo_y, y); hi_y = std::max(hi_y, y);
    };
    for (const auto& sh : shapes_) { span(sh.a); if (!sh.is_curve()) span(sh.b); }
    for (const auto& r : blocks_.references()) span(r.insertion);
    for (const auto& t : texts_) span(t.pos);
    auto_annotation_ = 1.0;
    if (lo_x <= hi_x && lo_y <= hi_y) {
        const double want = std::max((hi_x - lo_x) / 0.38, (hi_y - lo_y) / 0.26);
        for (const double step : {1.0, 2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 75.0,
                                  100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0})
            if ((auto_annotation_ = step) >= want) break;
    }
    return auto_annotation_;
}

double Canvas::effective_annotation_factor() const noexcept {
    if (annotative_fixed_ > 0) return annotative_fixed_;
    if (annotation_scale_name_ != "1:1") return annotation_factor_;
    return auto_annotation_factor();
}

double Canvas::annotation_factor_for(const gdim::Style& st) const noexcept {
    if (annotative_fixed_ > 0) return annotative_fixed_;
    if (annotation_scale_name_ != "1:1") return annotation_factor_;
    constexpr double kPaperTextMaxMm = 10.0;
    return st.dimtxt * st.dimscale <= kPaperTextMaxMm ? auto_annotation_factor() : 1.0;
}

gdim::Style Canvas::annotative(const gdim::Style& st) const {
    return styled_for(st,annotation_factor_for(st));
}

std::string Canvas::text_property(const TextItem& text, const std::string& key) const {
    if(key=="BACKGROUND" || key=="HIGHLIGHT") {
        const auto found=text.attrs.user.find(key=="BACKGROUND" ? "text_background" : "text_highlight");
        return found==text.attrs.user.end() ? "NONE" : found->second;
    }
    const auto parsed=gtext::parse_mtext(text.text);
    const auto value=[&](const gtext::Format& format)->std::string {
        if(key=="FONT") {
            if(!format.font.empty())return format.font;
            const auto* style=text_styles_.find(text.style.empty()?text_styles_.current():text.style);
            return style && !style->font.empty() ? style->font : "Default";
        }
        if(key=="BOLD")return format.bold?"1":"0";
        if(key=="ITALIC")return format.italic?"1":"0";
        if(key=="COLOR") {
            if(!format.has_color || (!format.true_color && format.aci==256))return "BYLAYER";
            auto r=format.r,g=format.g,b=format.b;
            if(!format.true_color)gdoc::aci_rgb(format.aci,r,g,b);
            char hex[8];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",r,g,b);return hex;
        }
        return {};
    };
    const auto first=value(parsed.runs.empty()?gtext::Format{}:parsed.runs.front().format);
    for(const auto& run:parsed.runs)if(value(run.format)!=first)return "Mixed";
    return first;
}

void Canvas::emit_note(const TextItem& original, double x, double y, std::vector<Label>& labels, double annotation_scale) const {
    if (layer_hidden(original.layer)) return;
    auto t=original; t.text=resolved_text(original);
    if (t.text.empty()) return;
    const auto* def=text_styles_.find(t.style.empty() ? text_styles_.current() : t.style);
    if(def && def->annotative) {const double scale=annotation_scale>0?annotation_scale:effective_annotation_factor();t.height*=scale;t.width*=scale;}
    std::shared_ptr<gtext::StrokeFont> stroke;
    if(def && !def->font.empty()) {
        const auto& path=def->font;
        auto found=stroke_fonts_.find(path);
        if(found==stroke_fonts_.end()) {
            auto font=std::make_shared<gtext::StrokeFont>();
            if(!font->load_file(path)) font.reset();
            found=stroke_fonts_.emplace(path,std::move(font)).first;
        }
        stroke=found->second;
    }
    const auto font_for=[&](const gtext::Format& format)->std::shared_ptr<gtext::Font> {
        const auto requested=!format.font.empty()?format.font:def?def->font:std::string{};
        if(requested.empty() || (text_font_ && requested==text_font_->family()))return {};
        auto found=note_fonts_.find(requested);
        if(found==note_fonts_.end()) {
            std::string path=requested;
            if(font_registry_)if(const auto* entry=font_registry_->find(requested))path=entry->path;
            auto font=std::make_shared<gtext::Font>();
            if(!font->load_file(path) || !font->covers_ascii())font.reset();
            found=note_fonts_.emplace(requested,std::move(font)).first;
        }
        return found->second;
    };
    const auto fill_color=[&](const char* key) {
        grender::Color color{0,0,0,0};const auto it=t.attrs.user.find(key);
        if(it!=t.attrs.user.end() && it->second.size()==7 && it->second[0]=='#') {
            unsigned rgb=0;if(std::sscanf(it->second.c_str()+1,"%x",&rgb)==1)color={float((rgb>>16)&255)/255,float((rgb>>8)&255)/255,float(rgb&255)/255,1};
        }
        return color;
    };
    const auto highlight=fill_color("text_highlight"),background=fill_color("text_background");
    const auto first_label=labels.size();
    const double wf=def ? def->width_factor : 1, oblique=def ? def->oblique : 0;
    const double xf=def && def->backwards ? -1 : 1, yf=def && def->upside_down ? -1 : 1;

    if (!text_font_ && !stroke) {
        const auto first_label=labels.size();
        const gtext::MTextContent parsed = gtext::parse_mtext(t.text);
        std::vector<std::string> lines(1);
        for (const auto& run : parsed.runs) {
            while (run.paragraph >= lines.size()) lines.emplace_back();
            lines[run.paragraph] += run.text;
            if (run.stack != gtext::Stack::None)
                lines[run.paragraph] += "/" + run.denominator;
        }
        const double step = t.height * 1.2;
        double top = y;
        if (t.valign == 2)      top = y + step * double(lines.size() - 1) * 0.5;
        else if (t.valign <= 1) top = y + step * double(lines.size() - 1);
        for (std::size_t i = 0; i < lines.size(); ++i)
            labels.push_back({x, top - step * double(i), t.height, t.angle,
                               lines[i], t.halign, t.valign});
        for(std::size_t j=first_label;j<labels.size();++j) {auto& lab=labels[j]; lab.width_factor=wf*xf; lab.oblique=oblique; lab.y_factor=yf; lab.layer=t.layer.empty()?"0":t.layer; lab.color=color_of_layer(lab.layer); }
        return;
    }

    gtext::MTextStyle style;
    style.height = float(t.height);
    if(stroke) style.measure=[stroke](std::string_view text){return stroke->text_width(text);};
    style.width = float(t.width);       // zero disables wrapping, as it should
    style.align = t.halign == 0   ? gtext::Align::Left
                  : t.halign == 2 ? gtext::Align::Right
                                  : gtext::Align::Centre;
    style.valign = t.valign == 3   ? gtext::VAlign::Top
                   : t.valign <= 1 ? gtext::VAlign::Bottom
                                   : gtext::VAlign::Middle;

    auto parsed=gtext::parse_mtext(t.text);
    if(!original.field.empty()) {
        const auto source=gtext::parse_mtext(original.text);
        if(!source.runs.empty())for(auto& run:parsed.runs)run.format=source.runs.front().format;
    }
    for(auto& run:parsed.runs) { run.format.width*=float(wf); run.format.oblique+=float(oblique); }
    if(!stroke)style.measure_run=[&](std::string_view text,const gtext::Format& format) {
        const auto owned=font_for(format);const auto* face=owned?owned.get():text_font_;
        return face?face->measure(text)+float(gtext::decode_utf8(text).size())*(format.tracking-1)*.1f:float(text.size())*.62f;
    };
    const gtext::Font empty_font;
    const gtext::MTextLayout laid = gtext::layout_mtext(text_font_ ? *text_font_ : empty_font, parsed, style);
    if (laid.empty()) {
        labels.push_back({x, y, t.height, t.angle, t.text, t.halign, t.valign});
        labels.back().layer=t.layer.empty()?"0":t.layer;
        labels.back().color=color_of_layer(labels.back().layer);
        return;
    }

    double ox = 0, oy = 0;
    if (t.halign == 1)      ox = - laid.width * 0.5;
    else if (t.halign == 2) ox = - laid.width;
    if (t.valign == 2)      oy = + laid.height * 0.5;
    else if (t.valign <= 1) oy = + laid.height;

    const double ca = std::cos(t.angle), sa = std::sin(t.angle);
    for (const auto& piece : laid.pieces) {
        if (piece.text.empty() && piece.denominator.empty()) continue;
        const auto place = [&](double lx, double ly, const std::string& text,
                               double height) {
            lx=(ox+lx)*xf; ly=(oy+ly)*yf;
            const double wx = x + lx * ca - ly * sa;
            const double wy = y + lx * sa + ly * ca;
            Label lab{wx, wy, height, t.angle, text, 0, 0};
            lab.layer=t.layer.empty()?"0":t.layer;
            lab.color=color_of_layer(lab.layer);
            lab.stroke_font=stroke;
            if(!stroke) lab.owned_font=font_for(piece.format);
            if(!stroke && (lab.owned_font || piece.format.bold || piece.format.italic))lab.outline_font=lab.owned_font?lab.owned_font.get():text_font_;
            lab.bold=piece.format.bold;lab.highlight=highlight;lab.attrs=t.attrs;
            const auto* measure_face=lab.owned_font?lab.owned_font.get():text_font_;
            lab.text_advance=measure_face?(measure_face->measure(text)+gtext::decode_utf8(text).size()*(piece.format.tracking-1)*.1)*height:0;
            lab.width_factor=piece.format.width*xf; lab.oblique=piece.format.oblique+(piece.format.italic?12:0);
            lab.y_factor=yf; lab.tracking=piece.format.tracking;
            if (piece.format.has_color) {
                if (piece.format.true_color) {
                    lab.color = grender::Color{float(piece.format.r) / 255.0f,
                                               float(piece.format.g) / 255.0f,
                                               float(piece.format.b) / 255.0f, 1.0f};
                } else if (piece.format.aci != 7 && piece.format.aci != 256) {
                    gcm::u8 r = 0, g = 0, b = 0;
                    gdoc::aci_rgb(piece.format.aci, r, g, b);
                    lab.color = grender::Color{float(r) / 255.0f, float(g) / 255.0f,
                                               float(b) / 255.0f, 1.0f};
                }
            }
            if(lab.color.a>=0)lab.attrs.color=gdoc::Color::rgb(gcm::u8(std::lround(lab.color.r*255)),gcm::u8(std::lround(lab.color.g*255)),gcm::u8(std::lround(lab.color.b*255)));
            labels.push_back(std::move(lab));
        };
        if (piece.stack == gtext::Stack::None) {
            place(piece.x, -double(piece.y), piece.text, piece.height);
        } else {
            const double h = piece.height * 0.6;
            place(piece.x, -double(piece.y) + h * 0.7, piece.text, h);
            place(piece.x, -double(piece.y) - h * 0.5, piece.denominator, h);
        }
    }
    if(background.a>0 && labels.size()>first_label) {
        double lo_x=1e300,lo_y=1e300,hi_x=-1e300,hi_y=-1e300;
        const auto include=[&](double wx,double wy) {const double dx=wx-x,dy=wy-y;const double lx=ca*dx+sa*dy,ly=-sa*dx+ca*dy;lo_x=std::min(lo_x,lx);lo_y=std::min(lo_y,ly);hi_x=std::max(hi_x,lx);hi_y=std::max(hi_y,ly);};
        for(std::size_t i=first_label;i<labels.size();++i) {
            for(const auto& p:label_mesh(labels[i],text_font_,vol_))include(p.x,p.y);
            for(const auto& line:label_strokes(labels[i],vol_)){include(line[0],line[1]);include(line[2],line[3]);}
        }
        if(lo_x<=hi_x && lo_y<=hi_y) {
            const double padding=t.height*.1;lo_x-=padding;lo_y-=padding;hi_x+=padding;hi_y+=padding;
            Label fill{x+ca*lo_x-sa*lo_y,y+sa*lo_x+ca*lo_y,t.height,t.angle,"",0,0};
            fill.box_width=hi_x-lo_x;fill.box_height=hi_y-lo_y;fill.background=background;fill.layer=t.layer;fill.attrs=t.attrs;
            labels.insert(labels.begin()+std::ptrdiff_t(first_label),std::move(fill));
        }
    }
}

void Canvas::emit_dimensions(grender::d2::DrawList& out) const {
    for (const auto& d : dims_) {
        if (layer_hidden(d.layer)) continue;
        const gdim::Style* st = dim_styles_.find(d.style);
        if (!st) continue;
        const gdim::Style ann = annotative(*st);
        const gdim::Geometry g = gdim::build(d, ann, vol_);
        if (!g.ok) continue;

        const auto index=std::size_t(&d-dims_.data());
        const Role role = d.orphaned ? Role::Error : std::find(sel_dims_.begin(),sel_dims_.end(),index)!=sel_dims_.end() ? Role::Selected : Role::Annotation;
        for (auto sh : g.lines) { sh.layer=d.layer; emit_shape(out, sh, role); }
        for (auto sh : g.arrows) { sh.layer=d.layer; emit_shape(out, sh, role); }

        double tx, ty;
        gcm::to_world(vol_, g.text_at, tx, ty);
        TextItem note{g.text_at,g.text,g.text_height,g.text_angle,g.halign,g.valign,0,"__dimension__"};
        emit_note(note,tx,ty,labels_);
    }
}

void Canvas::emit_shape(grender::d2::DrawList& out, const gsnap::Shape& sh,
                        Role role) const {
    if(!shape_visible(sh)) return;
    const float width=stroke_width(sh);
    const grender::Color col = role == Role::Geometry
                                   ? color_of_shape(sh)
                                   : grender::d2::kByRole;
    if (sh.kind == gsnap::ShapeKind::Point) {
        double x, y;
        gcm::to_world(vol_, sh.a, x, y);
        const double r = point_size_ > 0.0        ? point_size_ * 0.5
                       : point_size_ < 0.0        ? -point_size_ * 0.5 *
                                                        (view_.vh() / std::max(view_.scale(), 1e-9))
                                                  : 4.0 / view_.scale();
        const int mark = point_mode_ % 32;
        const int surround = point_mode_ / 32;

        if (mark == 0) out.point(x, y, role, col);
        if (mark == 2 || mark == 3 || mark == 4) {
            if (mark == 2) {
                out.line(x - r, y, x + r, y, role, col, width);
                out.line(x, y - r, x, y + r, role, col, width);
            } else if (mark == 3) {
                out.line(x - r, y - r, x + r, y + r, role, col, width);
                out.line(x - r, y + r, x + r, y - r, role, col, width);
            } else {
                out.line(x, y, x, y + r, role, col, width);
            }
        }
        if (surround == 1 || surround == 3) {
            out.arc(x, y, 1.0, 0.0, r, r, 0.0, 6.28318530717958647692, role, col, width);
        }
        if (surround == 2 || surround == 3) {
            out.line(x - r, y - r, x + r, y - r, role, col, width);
            out.line(x + r, y - r, x + r, y + r, role, col, width);
            out.line(x + r, y + r, x - r, y + r, role, col, width);
            out.line(x - r, y + r, x - r, y - r, role, col, width);
        }
        return;
    }

    if (sh.is_infinite()) {
        double ax, ay, bx, by;
        gcm::to_world(vol_, sh.a, ax, ay);
        gcm::to_world(vol_, sh.b, bx, by);
        const double dx = bx - ax, dy = by - ay;
        const double len = std::hypot(dx, dy);
        if (len < 1e-12) return;

        const double reach = (view_.vw() + view_.vh()) / std::min(view_.scale(), tess_scale_) * 2.0;
        const double ux = dx / len, uy = dy / len;
        const double back = sh.kind == gsnap::ShapeKind::Ray ? 0.0 : reach;
        out.line(ax - ux * back, ay - uy * back, ax + ux * reach, ay + uy * reach,
                 role, col, width);
        return;
    }

    if (sh.kind == gsnap::ShapeKind::Ellipse) {
        double cx, cy, mx, my;
        gcm::to_world(vol_, sh.a, cx, cy);
        gcm::to_world(vol_, sh.b, mx, my);
        const double ax = mx - cx, ay = my - cy;
        const double major = std::hypot(ax, ay);
        if (major < 1e-12) return;
        const double ux = ax / major, uy = ay / major;
        const double minor = double(sh.radius) * vol_.inv_scale();

        out.arc(cx, cy, ux, uy, major, minor, sh.start_angle, sh.start_angle+sh.ellipse_sweep(), role, col, width);
        return;
    }

    if (sh.kind == gsnap::ShapeKind::Segment) {
        double ax, ay, bx, by;
        gcm::to_world(vol_, sh.a, ax, ay);
        gcm::to_world(vol_, sh.b, bx, by);
        const gdoc::Linetype* lt = linetype_of(sh);
        if (!lt) { out.line(ax, ay, bx, by, role, col, width); return; }
        const double len = std::hypot(bx - ax, by - ay);
        if (len < 1e-12) return;
        const double ux = (bx - ax) / len, uy = (by - ay) / len;
        const double dot = lt->cycle_length() * linetype_scale_ * 0.02;
        for (const auto& [t0, t1] : lt->spans(len, linetype_scale_, dot))
            out.line(ax + ux * t0, ay + uy * t0, ax + ux * t1, ay + uy * t1, role, col, width);
        return;
    }
    if (!sh.is_curve()) return;

    double cx, cy;
    gcm::to_world(vol_, sh.a, cx, cy);
    const double r = double(sh.radius) * vol_.inv_scale();

    double a0 = 0.0, a1 = 6.28318530717958647692;
    if (sh.kind == gsnap::ShapeKind::Arc) {
        a0 = sh.start_angle;
        a1 = sh.end_angle;
        while (a1 < a0) a1 += 6.28318530717958647692;
    }

    if (const gdoc::Linetype* lt = linetype_of(sh); lt && r > 1e-12) {
        const double sweep = a1 - a0;
        const double dot = lt->cycle_length() * linetype_scale_ * 0.02;
        for (const auto& [t0, t1] : lt->spans(sweep * r, linetype_scale_, dot))
            out.arc(cx, cy, 1.0, 0.0, r, r, a0 + t0 / r, a0 + t1 / r, role, col, width);
        return;
    }
    out.arc(cx, cy, 1.0, 0.0, r, r, a0, a1, role, col, width);
}

gcm::u64 Canvas::block_geometry_key() const noexcept {
    auto key = blocks_.revision() ^ (style_serial_*0x9E3779B97F4A7C15ull);
    for (const auto& dynamic : blocks_.dynamics()) {
        key = (key * 131ull) ^ dynamic.visible_set;
        for (const auto& parameter : dynamic.parameters) {
            key = (key * 131ull) ^ std::bit_cast<gcm::u64>(parameter.value);
            key = (key * 131ull) ^ parameter.choice;
        }
    }
    return key;
}

gcm::u64 Canvas::static_key() const noexcept {
    gcm::u64 k = edit_serial_ * 1000003ull;
    k ^= grender::palette_revision() * 0x9E3779B97F4A7C15ull;
    if(text_font_) k ^= gcm::u64(reinterpret_cast<std::uintptr_t>(text_font_->source_bytes().get()));
    for(const auto& [name,font]:note_fonts_)
        if(font) k=(k*131ull)^gcm::u64(reinterpret_cast<std::uintptr_t>(font->source_bytes().get()));
    k ^= block_geometry_key();
    for (const auto& r : blocks_.references()) {
        k = (k * 131ull) ^ static_cast<gcm::u64>(r.insertion.x) ^ (static_cast<gcm::u64>(r.insertion.y) << 21);
        k ^= static_cast<gcm::u64>(std::llround(r.rotation * 1e6 + r.scale_x * 7.0 + r.scale_y * 13.0));
        for (const auto& a : r.attributes) for (char c : a.value) k = (k * 131ull) ^ static_cast<unsigned char>(c);
    }
    for (const auto& d : blocks_.all()) {
        k = (k * 17ull) ^ static_cast<gcm::u64>(d.geometry.size());
        if (const gdoc::DynamicBlock* dyn = blocks_.dynamic(d.name))
            for (const auto& pm : dyn->parameters) k ^= static_cast<gcm::u64>(std::llround(pm.value * 1e6)) * 0x2545F4914F6CDD1Dull;
    }
    for (std::size_t i : sel_dims_) k=(k*41ull)^(i+1);
    for (std::size_t i : sel_hatches_) k=(k*43ull)^(i+1);
    for (std::size_t i : selection_) k = (k * 31ull) ^ (i + 1);
    for (gcm::u32 rsel : sel_refs_) k = (k * 37ull) ^ (rsel + 11u);
    for (const auto& h : hidden_layers_) for (char c : h) k = (k * 131ull) ^ static_cast<unsigned char>(c);
    k ^= static_cast<gcm::u64>(hatches_.size()) << 40;
    k ^= (static_cast<gcm::u64>(texts_.size()) + texts_serial_ * 7919ull) * 0x9E3779B97F4A7C15ull;
    k ^= static_cast<gcm::u64>(dims_.size()) << 48;
    k ^= static_cast<gcm::u64>(std::llround(spline_tolerance_ * 1e7)) << 8;
    k ^= std::bit_cast<gcm::u64>(effective_annotation_factor());
    if (!dims_.empty()) {
        k ^= (static_cast<gcm::u64>(unit_) + 1) << 59;
    }
    for (const auto& dd : dims_) {
        k=(k*131ull)^gcm::u64(dd.leader_text_absolute);
        k=(k*131ull)^gcm::u64(dd.leader_text_halign+1);k=(k*131ull)^gcm::u64(dd.leader_text_valign+1);
        for(auto value:{dd.leader_text_height,dd.leader_text_angle,dd.leader_arrow_size})k=(k*131ull)^std::bit_cast<gcm::u64>(value);
        for(const auto& path:dd.leader_paths) {
            k=(k*131ull)^gcm::u64(path.arrow);
            for(auto point:path.vertices)k=(k*131ull)^gcm::u64(point.x)^(gcm::u64(point.y)<<17);
        }
        k ^= static_cast<gcm::u64>(dd.p3.x * 31 + dd.p3.y * 7 + dd.centre.x);
        k = (k * 131ull) ^ static_cast<gcm::u64>(dd.p1.x) ^
            (static_cast<gcm::u64>(dd.p1.y) << 17) ^
            (static_cast<gcm::u64>(dd.p2.x) << 5) ^
            (static_cast<gcm::u64>(dd.p2.y) << 29) ^ (dd.orphaned ? 0x5bf03635ull : 0ull);
    }
    k ^= static_cast<gcm::u64>(splines_.size()) << 56;
    k ^= static_cast<gcm::u64>(blocks_.references().size()) << 20;
    k = (k * 1000003ull) ^ (style_serial_ * 0x9E3779B97F4A7C15ull);
    k ^= static_cast<gcm::u64>(std::llround(linetype_scale_ * 1e6)) << 12;
    return k ? k : 1;
}

void Canvas::build(grender::d2::DrawList& out) const {
    grender::d2::DrawList dyn;
    build_split(out, dyn);
    out.append(dyn);
}

void Canvas::build_geometry(grender::d2::DrawList& out) const {
    out.clear();
    solid_only_ = true;
    for (std::size_t i = 0; i < shapes_.size(); ++i)
        if (shape_visible(shapes_[i]))
            emit_shape(out, shapes_[i], highlight_selection && is_selected(i) ? Role::Selected : Role::Geometry);
    emit_blocks(out);
    for (const auto& c : splines_) {
        const auto pts = c.tessellate(spline_tolerance_);
        for (std::size_t i = 1; i < pts.size(); ++i)
            out.line(pts[i - 1].x.to_f64(), pts[i - 1].y.to_f64(),
                     pts[i].x.to_f64(), pts[i].y.to_f64(), Role::Geometry);
    }
    solid_only_ = false;
}

void Canvas::build_split(grender::d2::DrawList& out, grender::d2::DrawList& dynamic_out) const {
    build_dynamic_only(dynamic_out);
    out.clear();
    out.append(static_cache_);
}

void Canvas::build_dynamic_only(grender::d2::DrawList& dynamic_out) const {
    dynamic_out.clear();
    const gcm::u64 key = static_key();
    if (key != static_key_) {
        static_cache_.clear();
        labels_.clear();
        tess_scale_ = std::exp2(std::floor(std::log2(std::max(view_.scale(), 1e-12))) + 1.0);
        const std::size_t n = shapes_.size();
        {
            std::vector<grender::d2::DrawList> parts(gcm::worker_count());
            gcm::parallel_for(n, 500, [&](std::size_t lo, std::size_t hi, unsigned t) {
                for (std::size_t i = lo; i < hi; ++i) {
                    if (shapes_[i].is_infinite() || shapes_[i].kind == gsnap::ShapeKind::Point) continue;
                    if (shape_visible(shapes_[i]))
                        emit_shape(parts[t], shapes_[i], highlight_selection && is_selected(i) ? Role::Selected : Role::Geometry);
                }
            });
            for (const auto& part : parts) static_cache_.append(part);
        }

        emit_hatches(static_cache_);
        if(highlight_selection)for(auto i:sel_hatches_)if(i<hatches_.size() && !layer_hidden(hatches_[i].layer))
            for(const auto& shape:hatch_boundary(hatches_[i]))emit_shape(static_cache_,shape,Role::Selected);
        for(const auto& arrow:door_flow_shapes()) {
            double ax,ay,bx,by;gcm::to_world(vol_,arrow.a,ax,ay);gcm::to_world(vol_,arrow.b,bx,by);
            static_cache_.line(ax,ay,bx,by,Role::Construction,color_of_shape(arrow),2.f);
        }
        emit_blocks(static_cache_);
        emit_dimensions(static_cache_);
        for (std::size_t i=0;i<blocks_.references().size();++i) {
            const bool selected=highlight_selection && std::find(sel_refs_.begin(),sel_refs_.end(),gcm::u32(i))!=sel_refs_.end();
            for (auto label:reference_labels(blocks_.references()[i])) {
                if(selected)label.color=grender::color_for(Role::Selected);
                labels_.push_back(std::move(label));
            }
        }
        for (const auto& t : texts_) {
            double x, y;
            gcm::to_world(vol_, t.pos, x, y);
            emit_note(t, x, y, labels_);
        }
        const auto same_color=[](grender::Color a,grender::Color b) {
            return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
        };
        const auto same_label=[&](const Label& a,const Label& b) {
            if(a.x!=b.x || a.y!=b.y || a.height!=b.height || a.angle!=b.angle || a.text!=b.text ||
               a.halign!=b.halign || a.valign!=b.valign || a.width_factor!=b.width_factor ||
               a.oblique!=b.oblique || a.y_factor!=b.y_factor || a.tracking!=b.tracking ||
               a.bold!=b.bold || a.box_width!=b.box_width || a.box_height!=b.box_height ||
               a.text_advance!=b.text_advance || a.stroke_font!=b.stroke_font ||
               a.outline_font!=b.outline_font || !same_color(a.color,b.color) ||
               !same_color(a.background,b.background) || !same_color(a.highlight,b.highlight) ||
               a.clips.size()!=b.clips.size()) return false;
            if(a.stroke_font) return false;
            for(std::size_t i=0;i<a.clips.size();++i) {
                const auto& x=a.clips[i];const auto& y=b.clips[i];
                if(x.inverted!=y.inverted || x.points.size()!=y.points.size())return false;
                for(std::size_t j=0;j<x.points.size();++j)
                    if(x.points[j].x!=y.points[j].x || x.points[j].y!=y.points[j].y)return false;
            }
            return true;
        };
        std::vector<std::shared_ptr<const std::vector<gcm::u8>>> font_sources;
        font_sources.reserve(labels_.size());
        for(const auto& label:labels_) {
            const auto* font=label.outline_font ? label.outline_font : text_font_;
            font_sources.push_back(font ? font->source_bytes() : nullptr);
        }
        const std::array<double,3> label_volume{vol_.cx,vol_.cy,vol_.extent};
        const bool same_labels=label_cache_resolution_==vol_.resolution_bits &&
            label_cache_volume_==label_volume && label_font_sources_==font_sources &&
            labels_.size()==labels_cache_.size() &&
            std::equal(labels_.begin(),labels_.end(),labels_cache_.begin(),same_label);
        if(!same_labels) {
            label_geometry_cache_.clear();
            for(const auto& lab:labels_)for(auto line:label_strokes(lab,vol_))
                label_geometry_cache_.line(line[0],line[1],line[2],line[3],Role::Annotation,lab.color);
            for(const auto& lab:labels_)for(const auto& triangle:label_fills(lab,vol_))
                label_geometry_cache_.triangle(triangle);
            label_font_sources_=std::move(font_sources);
            label_cache_volume_=label_volume;
            label_cache_resolution_=vol_.resolution_bits;
            ++labels_revision_;
        }
        static_cache_.append(label_geometry_cache_);

        for (const auto& c : splines_) {
            const auto pts = c.tessellate(spline_tolerance_);
            for (std::size_t i = 1; i < pts.size(); ++i)
                static_cache_.line(pts[i - 1].x.to_f64(), pts[i - 1].y.to_f64(),
                                   pts[i].x.to_f64(), pts[i].y.to_f64(), Role::Geometry);
        }
        labels_cache_ = labels_;
        static_key_ = key;
    }
    labels_dyn_.clear();
    tess_scale_ = std::exp2(std::floor(std::log2(std::max(view_.scale(), 1e-12))) + 1.0);
    for (std::size_t i = 0; i < shapes_.size(); ++i)
        if ((shapes_[i].is_infinite() || shapes_[i].kind == gsnap::ShapeKind::Point) && !layer_hidden(shapes_[i].layer))
            emit_shape(dynamic_out, shapes_[i], highlight_selection && is_selected(i) ? Role::Selected : Role::Geometry);
    for (std::size_t ti : sel_texts_) {
        if (ti >= texts_.size() || layer_hidden(texts_[ti].layer)) continue;
        double lo_x, lo_y, hi_x, hi_y;
        auto resolved=texts_[ti]; resolved.text=resolved_text(resolved);
        text_bounds(resolved, lo_x, lo_y, hi_x, hi_y);
        const double m = 2.0 / view_.scale();
        lo_x -= m; lo_y -= m; hi_x += m; hi_y += m;
        const double xs[5] = {lo_x, hi_x, hi_x, lo_x, lo_x};
        const double ys[5] = {lo_y, lo_y, hi_y, hi_y, lo_y};
        for (int k = 0; k < 4; ++k) {
            gsnap::Shape e;
            e.kind = gsnap::ShapeKind::Segment;
            e.a = gcm::to_lattice(vol_, xs[k], ys[k]);
            e.b = gcm::to_lattice(vol_, xs[k + 1], ys[k + 1]);
            emit_shape(dynamic_out, e, Role::Selected);
        }
    }
    build_dynamic(dynamic_out);
}

void Canvas::build_dynamic(grender::d2::DrawList& out) const {

    if (pending_) {
        switch (tool_) {
            case Tool::Rect: {
                if (rect_mode_ == 2) {
                    if (arc_pts_.size() == 1) {
                        double ax, ay, bx, by;
                        gcm::to_world(vol_, arc_pts_[0], ax, ay);
                        gcm::to_world(vol_, cursor_, bx, by);
                        out.line(ax, ay, bx, by, Role::Preview);
                    }
                    break;
                }
                Lattice a = anchor_;
                const Lattice c = cursor_;
                if (rect_mode_ == 1) a = {2 * anchor_.x - cursor_.x, 2 * anchor_.y - cursor_.y};
                const Lattice b{c.x, a.y}, d{a.x, c.y};
                auto line = [&](Lattice p, Lattice q) {
                    double px, py, qx, qy;
                    gcm::to_world(vol_, p, px, py);
                    gcm::to_world(vol_, q, qx, qy);
                    out.line(px, py, qx, qy, Role::Preview);
                };
                line(a, b); line(b, c); line(c, d); line(d, a);
                break;
            }
            case Tool::Circle: {
                gsnap::Shape ghost;
                ghost.kind = gsnap::ShapeKind::Circle;
                ghost.a = anchor_;
                const double dx = double(cursor_.x - anchor_.x);
                const double dy = double(cursor_.y - anchor_.y);
                ghost.radius = static_cast<gcm::i64>(std::llround(std::hypot(dx, dy)));
                if (circle_mode_ == 1) {
                    ghost.a = {(anchor_.x + cursor_.x) / 2, (anchor_.y + cursor_.y) / 2};
                    ghost.radius /= 2;
                }
                if (circle_mode_ == 2) {
                    if (arc_pts_.size() == 1) {
                        double ax, ay, bx, by;
                        gcm::to_world(vol_, arc_pts_[0], ax, ay);
                        gcm::to_world(vol_, cursor_, bx, by);
                        out.line(ax, ay, bx, by, Role::Construction);
                    } else if (arc_pts_.size() == 2) {
                        Lattice c; gcm::i64 r;
                        if (circumcircle(arc_pts_[0], arc_pts_[1], cursor_, c, r)) { ghost.a = c; ghost.radius = r; emit_shape(out, ghost, Role::Preview); }
                    }
                    break;
                }
                emit_shape(out, ghost, Role::Preview);
                break;
            }
            case Tool::Arc: {
                if (arc_pts_.size() == 1) {
                    double ax, ay, bx, by;
                    gcm::to_world(vol_, arc_pts_[0], ax, ay);
                    gcm::to_world(vol_, cursor_, bx, by);
                    out.line(ax, ay, bx, by, Role::Construction);
                } else if (arc_pts_.size() == 2) {
                    const Lattice c = arc_pts_[0];
                    const double dx = double(arc_pts_[1].x - c.x);
                    const double dy = double(arc_pts_[1].y - c.y);
                    gsnap::Shape ghost;
                    ghost.kind = gsnap::ShapeKind::Arc;
                    ghost.a = c;
                    ghost.radius = static_cast<gcm::i64>(std::llround(std::hypot(dx, dy)));
                    double cx, cy, px, py, qx, qy;
                    gcm::to_world(vol_, c, cx, cy);
                    gcm::to_world(vol_, arc_pts_[1], px, py);
                    gcm::to_world(vol_, cursor_, qx, qy);
                    ghost.start_angle = std::atan2(py - cy, px - cx);
                    ghost.end_angle = std::atan2(qy - cy, qx - cx);
                    emit_shape(out, ghost, Role::Preview);
                }
                break;
            }
            default: {
                double ax, ay, bx, by;
                gcm::to_world(vol_, anchor_, ax, ay);
                gcm::to_world(vol_, cursor_, bx, by);
                out.line(ax, ay, bx, by, Role::Preview);
                break;
            }
        }
    }

    for(auto p:blips) {double x,y;gcm::to_world(vol_,p,x,y);out.point(x,y,Role::Construction);}
    const auto mark_ends = [&](std::size_t i) {
        if (i >= shapes_.size()) return;
        const auto& sh = shapes_[i];
        if (!shape_visible(sh)) return;
        if (sh.poly && sh.attrs.user.contains("revision_cloud")) return;
        double x, y;
        if (sh.kind == gsnap::ShapeKind::Segment) {
            gcm::to_world(vol_, sh.a, x, y);
            out.point(x, y, Role::Endpoint);
            gcm::to_world(vol_, sh.b, x, y);
            out.point(x, y, Role::Endpoint);
        } else if (sh.is_curve()) {
            gcm::to_world(vol_, sh.a, x, y);
            out.point(x, y, Role::Endpoint);
        }
    };
    if (hover_shape_ != npos && !is_selected(hover_shape_)) mark_ends(hover_shape_);

    for(auto i:sel_dims_) if(i<dims_.size() && dims_[i].kind==gdim::Kind::Leader && !layer_hidden(dims_[i].layer))
        for(auto p:dims_[i].leader_grips()) {double x,y;gcm::to_world(vol_,p,x,y);out.point(x,y,Role::Selected);}
    for (const auto& g : grips_) {
        double x, y;
        gcm::to_world(vol_, g.at, x, y);
        out.point(x, y, g.hot ? Role::Highlight : Role::Selected);
    }

    if (snap_.valid) {
        double x, y;
        gcm::to_world(vol_, snap_.point, x, y);
        if (tool_ != Tool::Select && snap_marker) out.point(x, y, Role::Snap);
    }

    if (track_.active && pending_) {
        double ax, ay;
        gcm::to_world(vol_, anchor_, ax, ay);
        out.line(ax, ay, track_.gx, track_.gy, Role::Construction);
    }

    if (align_.active) {
        double cx, cy;
        gcm::to_world(vol_, cursor_, cx, cy);
        const double overshoot = 24.0 / view_.scale();

        if (align_.vertical) {
            double sx, sy;
            gcm::to_world(vol_, align_.source_v, sx, sy);
            const double beyond = cy > sy ? overshoot : -overshoot;
            out.line(sx, sy, cx, cy + beyond, Role::Construction);
            out.point(sx, sy, Role::Snap);
        }
        if (align_.horizontal) {
            double sx, sy;
            gcm::to_world(vol_, align_.source_h, sx, sy);
            const double beyond = cx > sx ? overshoot : -overshoot;
            out.line(sx, sy, cx + beyond, cy, Role::Construction);
            out.point(sx, sy, Role::Snap);
        }
    }
}

std::string Canvas::status() const {
    double cx, cy;
    gcm::to_world(vol_, cursor_, cx, cy);
    const char* tn = tool_name(tool_);

    char track[96] = "";
    if (track_.active)
        std::snprintf(track, sizeof(track), "   < %.0f\xc2\xb0  %.3f", 
                      track_.angle_deg, track_.length);

    const char* mode = polar_.ortho ? "ORTHO" : (polar_.enabled ? "POLAR" : "");
    char inc[24] = "";
    if (polar_.enabled && !polar_.ortho)
        std::snprintf(inc, sizeof(inc), " %.4g\xc2\xb0", polar_.increment_deg);

    char sel[64] = "";
    if (!selection_.empty())
        std::snprintf(sel, sizeof(sel), "   %zu selected", selection_count());

    char msg[160] = "";
    if (!message_.empty()) std::snprintf(msg, sizeof(msg), "   %s", message_.c_str());

    char buf[640];
    std::snprintf(buf, sizeof(buf),
                  "GLCAD  \xe2\x80\x94  %s   %.3f, %.3f%s   snap:%s   %s%s   "
                  "%zu segs  %zu dims%s   zoom %.0f%%%s",
                  tn, cx, cy, track, snap_.valid ? snap_.kind : "-", mode, inc,
                  shapes_.size(), dims_.size(), sel, view_.scale() * 2.5, msg);
    return buf;
}

}

namespace gcapp {
Canvas::SpaceState Canvas::capture_space() const {
    return {{shapes_, dims_, selection_, blocks_.references(), sel_refs_, texts_, sel_texts_,
             hatches_, splines_, constraints_, sel_dims_, sel_hatches_}, undo_stack_, redo_stack_, underlays_,
             previous_selection_, shape_selection_ids_, annotation_scale_name_, annotation_factor_};
}

void Canvas::restore_space(const SpaceState& state) {
    previous_selection_.clear(); cycle_candidates_.clear(); shape_selection_ids_.clear();
    cancel();
    const auto& s = state.content;
    shapes_ = s.shapes; dims_ = s.dims; selection_ = s.selection;
    blocks_.set_references(s.refs); sel_refs_ = s.sel_refs;
    texts_ = s.texts; sel_texts_ = s.sel_texts; sel_dims_=s.sel_dims; sel_hatches_=s.sel_hatches;
    hatches_ = s.hatches; splines_ = s.splines; constraints_ = s.constraints;
    underlays_ = state.underlays;
    set_underlay_snap_shapes({});
    previous_selection_=state.previous_selection;shape_selection_ids_=state.shape_selection_ids;
    set_annotation_scale(state.annotation_scale,state.annotation_factor);annotative_fixed_=0;
    undo_stack_ = state.undo; redo_stack_ = state.redo;
    ++texts_serial_; ++style_serial_;
    snap_sync_key_ = ~0ull;
    note_polyline_ids(); assign_keys(); rebuild_grips(); refresh_hatches();
}
}
