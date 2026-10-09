#include "ui/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace gcapp {
namespace {
using ScreenPoint = std::pair<double, double>;
using Line = std::array<double, 4>;
double cross(ScreenPoint a, ScreenPoint b) { return a.first * b.second - a.second * b.first; }
ScreenPoint minus(ScreenPoint a, ScreenPoint b) { return {a.first - b.first, a.second - b.second}; }
double distance(ScreenPoint p, ScreenPoint a, ScreenPoint b) {
    const auto d = minus(b, a), q = minus(p, a);
    const double length = d.first * d.first + d.second * d.second;
    const double t =
        length ? std::clamp((q.first * d.first + q.second * d.second) / length, 0.0, 1.0) : 0;
    return std::hypot(q.first - t * d.first, q.second - t * d.second);
}
bool inside(const std::vector<ScreenPoint> &polygon, ScreenPoint p, double eps) {
    bool result = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto a = polygon[j], b = polygon[i];
        if (distance(p, a, b) <= eps)
            return true;
        if ((a.second > p.second) != (b.second > p.second) &&
            p.first < (b.first - a.first) * (p.second - a.second) / (b.second - a.second) + a.first)
            result = !result;
    }
    return result;
}
std::vector<double> intersections(ScreenPoint a, ScreenPoint b, ScreenPoint c, ScreenPoint d,
                                  double eps) {
    const auto r = minus(b, a), s = minus(d, c), q = minus(c, a);
    const double det = cross(r, s), length = std::hypot(r.first, r.second);
    if (length <= eps)
        return distance(a, c, d) <= eps ? std::vector<double>{0} : std::vector<double>{};
    if (std::abs(det) > eps * std::max(length, std::hypot(s.first, s.second))) {
        const double t = cross(q, s) / det, u = cross(q, r) / det;
        if (t >= -eps / length && t <= 1 + eps / length && u >= 0 && u <= 1)
            return {std::clamp(t, 0.0, 1.0)};
        return {};
    }
    if (std::abs(cross(q, r)) > eps * length)
        return {};
    const double rr = length * length;
    const auto e = minus(d, a);
    const double t0 = (q.first * r.first + q.second * r.second) / rr;
    const double t1 = (e.first * r.first + e.second * r.second) / rr;
    const double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(1.0, std::max(t0, t1));
    return lo <= hi ? std::vector<double>{lo, hi} : std::vector<double>{};
}
bool in_box(ScreenPoint p, const Line &box, double slop = 0) {
    return p.first >= box[0] - slop && p.first <= box[2] + slop && p.second >= box[1] - slop &&
           p.second <= box[3] + slop;
}
}

std::vector<std::array<double,4>> Canvas::selection_lines(const gsnap::Shape& shape) const {
    std::vector<std::array<double,4>> lines;
    if(!shape_visible(shape))return lines;
    grender::d2::DrawList draw;emit_shape(draw,shape,grender::Role::Geometry);
    draw.flatten_arcs(std::max(1.,view_.scale()));
    for(const auto& line:draw.lines())lines.push_back({line.x0,line.y0,line.x1,line.y1});
    if(lines.empty()) {double x,y;gcm::to_world(vol_,shape.a,x,y);lines.push_back({x,y,x,y});}
    return lines;
}

std::vector<Canvas::SelectionObject> Canvas::selection_objects() const {
    std::vector<SelectionObject> objects;
    const auto shape = [&](SelectionObject &object, const gsnap::Shape &s) {
        const auto lines=selection_lines(s);object.lines.insert(object.lines.end(),lines.begin(),lines.end());
    };
    const auto text = [&](SelectionObject &object, TextItem t) {
        t.text = resolved_text(t);
        double lx, ly, hx, hy;
        text_bounds(t, lx, ly, hx, hy);
        object.filled_boxes.push_back({lx, ly, hx, hy});
        object.lines.insert(
            object.lines.end(),
            {{lx, ly, hx, ly}, {hx, ly, hx, hy}, {hx, hy, lx, hy}, {lx, hy, lx, ly}});
    };
    std::map<gcm::u32, std::size_t> polys;
    for (std::size_t i = 0; i < shapes_.size(); ++i) {
        const auto &s = shapes_[i];
        if (!shape_visible(s) || is_joined_wall(s))
            continue;
        auto found = polys.find(s.poly);
        if (s.poly && found != polys.end()) {
            shape(objects[found->second], s);
            continue;
        }
        if (s.poly)
            polys[s.poly] = objects.size();
        objects.push_back({SelectionKind::Shape, i, {}, {}});
        shape(objects.back(), s);
    }
    for (std::size_t i = 0; i < texts_.size(); ++i) {
        objects.push_back({SelectionKind::Text, i, {}, {}});
        text(objects.back(), texts_[i]);
    }
    const auto &refs = blocks_.references();
    for (std::size_t i = 0; i < refs.size(); ++i) {
        if (layer_hidden(refs[i].layer))
            continue;
        SelectionObject object{SelectionKind::Reference, i, {}, {}};
        if(is_wall_section(refs[i])) {
            auto region=gregion::from_shapes(blocks_.expand(refs[i],vol_,8,&appearance_layers_));
            if(region.ok)object.filled_regions.push_back(std::move(region.region));
        }
        for (const auto &s : blocks_.expand(refs[i], vol_, 8, &appearance_layers_))
            shape(object, s);
        for (const auto& label:reference_labels(refs[i])) {
            if(label.stroke_font) {
                auto strokes=label_strokes(label,vol_);object.lines.insert(object.lines.end(),strokes.begin(),strokes.end());
                continue;
            }
            const auto mesh=label_mesh(label,text_font_,vol_);
            for(std::size_t j=0;j<mesh.size();j+=3) {
                std::array<ScreenPoint,3> triangle;
                for(int k=0;k<3;++k)triangle[k]={mesh[j+k].x,mesh[j+k].y};
                object.filled_triangles.push_back(triangle);
                for(int k=0;k<3;++k) {
                    const auto a=triangle[k],b=triangle[(k+1)%3];
                    object.lines.push_back({a.first,a.second,b.first,b.second});
                }
            }
        }
        if (!object.lines.empty())
            objects.push_back(std::move(object));
    }
    for (std::size_t i = 0; i < dims_.size(); ++i) {
        const auto &d = dims_[i];
        if (layer_hidden(d.layer))
            continue;
        const auto *style = dim_styles_.find(d.style);
        if (!style)
            continue;
        const auto g = gdim::build(d, annotative(*style), vol_);
        if (!g.ok)
            continue;
        SelectionObject object{SelectionKind::Dimension, i, {}, {}};
        for (const auto &s : g.lines)
            shape(object, s);
        for (const auto &s : g.arrows)
            shape(object, s);
        if (!g.text.empty())
            text(object, {g.text_at, g.text, g.text_height, g.text_angle, g.halign, g.valign, 0,
                          "__dimension__"});
        if (!object.lines.empty())
            objects.push_back(std::move(object));
    }
    return objects;
}

std::size_t Canvas::selection_count() const {
    std::set<gcm::u32> polylines;
    std::size_t count = sel_refs_.size() + sel_texts_.size() + sel_dims_.size() + sel_hatches_.size();
    for (auto i : selection_)
        if (i < shapes_.size()) {
            const auto poly = shapes_[i].poly;
            if (!poly || polylines.insert(poly).second)
                ++count;
        }
    return count;
}

void Canvas::include_selection(const SelectionObject &object, bool toggle) {
    auto add = [&](auto &selected, auto index) {
        auto it = std::find(selected.begin(), selected.end(), index);
        if (it == selected.end())
            selected.push_back(index);
        else if (toggle)
            selected.erase(it);
    };
    switch (object.kind) {
    case SelectionKind::Shape: {
        const bool remove = toggle && is_selected(object.index);
        for (auto i : polyline_pieces(object.index)) {
            if (!shape_visible(shapes_[i]))
                continue;
            if (remove) {
                auto it = std::find(selection_.begin(), selection_.end(), i);
                if (it != selection_.end())
                    selection_.erase(it);
            } else if (!is_selected(i))
                selection_.push_back(i);
        }
        break;
    }
    case SelectionKind::Text:
        add(sel_texts_, object.index);
        break;
    case SelectionKind::Reference:
        add(sel_refs_, gcm::u32(object.index));
        break;
    case SelectionKind::Dimension:
        add(sel_dims_, object.index);
        break;
    }
}

std::size_t Canvas::select_polygon(const std::vector<ScreenPoint> &points, bool fence,
                                   bool crossing, bool add) {
    if (points.size() < (fence ? 2u : 3u)) {
        message_ = fence ? "a fence needs two points" : "a polygon needs three points";
        return 0;
    }
    std::vector<ScreenPoint> polygon;
    for (auto p : points) {
        if (!std::isfinite(p.first) || !std::isfinite(p.second)) {
            message_ = "selection points must be finite";
            return 0;
        }
        double x, y;
        view_.world_of(p.first, p.second, x, y);
        polygon.emplace_back(x, y);
    }
    const double eps = std::max(vol_.inv_scale(), 1e-10);
    if (!fence) {
        double area = 0;
        for (std::size_t i = 0; i < polygon.size(); ++i)
            area += cross(minus(polygon[i], polygon[0]),
                          minus(polygon[(i + 1) % polygon.size()], polygon[0]));
        if (std::abs(area) <= eps * eps) {
            message_ = "selection polygon has no area";
            return 0;
        }
    }
    const auto objects = selection_objects();
    remember_selection();
    if (!add) {
        selection_.clear();
        sel_texts_.clear();
        sel_refs_.clear();
        sel_dims_.clear(); sel_hatches_.clear();
    }
    std::size_t hits = 0;
    for (const auto &object : objects) {
        bool all = true, any = false;
        for (auto line : object.lines) {
            const ScreenPoint a{line[0], line[1]}, b{line[2], line[3]};
            std::vector<double> cuts{0, 1};
            bool touched = false;
            const std::size_t edges = fence ? polygon.size() - 1 : polygon.size();
            for (std::size_t k = 0; k < edges; ++k) {
                auto ts = intersections(a, b, polygon[k], polygon[(k + 1) % polygon.size()], eps);
                touched |= !ts.empty();
                cuts.insert(cuts.end(), ts.begin(), ts.end());
            }
            if (fence) {
                any |= touched;
                continue;
            }
            bool line_all = inside(polygon, a, eps) && inside(polygon, b, eps);
            bool line_any = inside(polygon, a, eps) || inside(polygon, b, eps) || touched;
            std::sort(cuts.begin(), cuts.end());
            for (std::size_t k = 1; k < cuts.size(); ++k) {
                double t = (cuts[k - 1] + cuts[k]) * .5;
                bool in = inside(
                    polygon,
                    {a.first + t * (b.first - a.first), a.second + t * (b.second - a.second)}, eps);
                line_all &= in;
                line_any |= in;
            }
            all &= line_all;
            any |= line_any;
        }
        for (const auto &box : object.filled_boxes)
            for (auto p : polygon)
                any |= in_box(p, box);
        for(const auto& triangle:object.filled_triangles)
            for(auto p:polygon)any|=inside({triangle.begin(),triangle.end()},p,eps);
        if ((fence || crossing) ? any : all) {
            include_selection(object);
            ++hits;
        }
    }
    message_ = std::to_string(hits) + " objects selected";
    rebuild_grips();
    return hits;
}

void Canvas::ensure_selection_ids() {
    assign_keys();
    for (const auto &[key, id] : shape_selection_ids_)
        next_selection_id_ = std::max(next_selection_id_, id);
    for (const auto &t : texts_)
        next_selection_id_ = std::max(next_selection_id_, t.selection_id);
    for (const auto &d : dims_)
        next_selection_id_ = std::max(next_selection_id_, d.selection_id);
    for (const auto &r : blocks_.references())
        next_selection_id_ = std::max(next_selection_id_, r.selection_id);
    std::set<gcm::u64> used;
    for (const auto &s : shapes_) {
        auto &id = shape_selection_ids_[s.key];
        if (!id)
            id = ++next_selection_id_;
        used.insert(id);
    }
    const auto assign = [&](auto &item) {
        if (!item.selection_id || !used.insert(item.selection_id).second) {
            item.selection_id = ++next_selection_id_;
            used.insert(item.selection_id);
            return true;
        }
        return false;
    };
    for (auto &t : texts_)
        assign(t);
    auto refs = blocks_.references();
    bool changed = false;
    for (auto &r : refs)
        changed |= assign(r);
    if (changed)
        blocks_.set_references(std::move(refs));
    for (auto &d : dims_)
        assign(d);
}
gcm::u64 Canvas::selection_id(SelectionKind kind, std::size_t index) const {
    switch (kind) {
    case SelectionKind::Shape: {
        if (index >= shapes_.size())
            return 0;
        auto found = shape_selection_ids_.find(shapes_[index].key);
        return found == shape_selection_ids_.end() ? 0 : found->second;
    }
    case SelectionKind::Text:
        return index < texts_.size() ? texts_[index].selection_id : 0;
    case SelectionKind::Reference:
        return index < blocks_.references().size() ? blocks_.references()[index].selection_id : 0;
    case SelectionKind::Dimension:
        return index < dims_.size() ? dims_[index].selection_id : 0;
    }
    return 0;
}
void Canvas::remember_selection() {
    ensure_selection_ids();
    std::vector<gcm::u64> ids;
    const auto save = [&](const auto &list, SelectionKind kind) {
        for (auto i : list)
            if (auto id = selection_id(kind, i))
                ids.push_back(id);
    };
    save(selection_, SelectionKind::Shape);
    save(sel_texts_, SelectionKind::Text);
    save(sel_refs_, SelectionKind::Reference);
    save(sel_dims_, SelectionKind::Dimension);
    if (!ids.empty())
        previous_selection_ = std::move(ids);
}
std::size_t Canvas::select_previous() {
    ensure_selection_ids();
    const auto previous = previous_selection_;
    const auto objects = selection_objects();
    remember_selection();
    selection_.clear();
    sel_texts_.clear();
    sel_refs_.clear();
    sel_dims_.clear(); sel_hatches_.clear();
    std::size_t hits = 0;
    for (const auto &object : objects) {
        bool selected = std::find(previous.begin(), previous.end(),
                                  selection_id(object.kind, object.index)) != previous.end();
        if (object.kind == SelectionKind::Shape)
            for (auto i : polyline_pieces(object.index))
                selected |= std::find(previous.begin(), previous.end(),
                                      selection_id(object.kind, i)) != previous.end();
        if (selected) {
            include_selection(object);
            ++hits;
        }
    }
    rebuild_grips();
    message_ = std::to_string(hits) + " previous objects selected";
    return hits;
}
std::size_t Canvas::select_last() {
    ensure_selection_ids();
    const auto objects = selection_objects();
    const SelectionObject *last = nullptr;
    gcm::u64 latest = 0;
    for (const auto &object : objects) {
        auto id = selection_id(object.kind, object.index);
        if (id > latest) {
            latest = id;
            last = &object;
        }
    }
    clear_selection();
    if (last)
        include_selection(*last);
    rebuild_grips();
    message_ = last ? "last object selected" : "no visible objects";
    return last ? 1 : 0;
}

bool Canvas::selected_at(double px, double py) const {
    double x, y;
    view_.world_of(px, py, x, y);
    const ScreenPoint p{x, y};
    const auto selected = [](const auto &list, auto i) {
        return std::find(list.begin(), list.end(), i) != list.end();
    };
    for (const auto &object : selection_objects()) {
        bool included = false;
        switch (object.kind) {
        case SelectionKind::Shape:
            included = is_selected(object.index);
            break;
        case SelectionKind::Text:
            included = selected(sel_texts_, object.index);
            break;
        case SelectionKind::Reference:
            included = selected(sel_refs_, gcm::u32(object.index));
            break;
        case SelectionKind::Dimension:
            continue;
        }
        if (!included)
            continue;
        for(const auto& region:object.filled_regions)
            if(region.contains(gcm::to_lattice(vol_,x,y)))return true;
        for (auto box : object.filled_boxes)
            if (in_box(p, box))
                return true;
        for(const auto& triangle:object.filled_triangles)
            if(inside({triangle.begin(),triangle.end()},p,vol_.inv_scale()))return true;
        for (auto line : object.lines)
            if (distance(p, {line[0], line[1]}, {line[2], line[3]}) <=
                pickbox_pixels / view_.scale())
                return true;
    }
    return false;
}

bool Canvas::context_selected_at(double px,double py) const {
    if(selected_at(px,py))return true;
    const auto dim=pick_dim(px,py);
    if(dim!=npos && std::find(sel_dims_.begin(),sel_dims_.end(),dim)!=sel_dims_.end())return true;
    if(pick(px,py)!=npos || pick_text(px,py)!=npos || dim!=npos || pick_reference(px,py)!=npos32)return false;
    const auto hatch=pick_hatch(px,py);
    return hatch!=npos && std::find(sel_hatches_.begin(),sel_hatches_.end(),hatch)!=sel_hatches_.end();
}

bool Canvas::select_context_at(double px,double py) {
    if(context_selected_at(px,py))return true;
    return select_hit(px,py,false,false)>0;
}

std::size_t Canvas::select_hit(double px, double py, bool add, bool cycle, bool edge) {
    ensure_selection_ids();
    double x, y;
    view_.world_of(px, py, x, y);
    const ScreenPoint p{x, y};
    const double aperture = pickbox_pixels / view_.scale();
    auto objects = selection_objects();
    std::vector<std::pair<double, std::size_t>> hits;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        double d = 1e300;
        for(const auto& region:objects[i].filled_regions)
            if(region.contains(gcm::to_lattice(vol_,x,y)))d=0;
        for (const auto &box : objects[i].filled_boxes)
            if (in_box(p, box))
                d = 0;
        for(const auto& triangle:objects[i].filled_triangles)
            if(inside({triangle.begin(),triangle.end()},p,vol_.inv_scale()))d=0;
        for (auto line : objects[i].lines)
            d = std::min(d, distance(p, {line[0], line[1]}, {line[2], line[3]}));
        if (d <= aperture)
            hits.emplace_back(d <= vol_.inv_scale() ? 0 : d, i);
    }
    std::stable_sort(hits.begin(), hits.end(), [](auto a, auto b) { return a.first < b.first; });
    std::vector<gcm::u64> ids;
    for (auto h : hits) {
        const auto &o = objects[h.second];
        ids.push_back(selection_id(o.kind, o.index));
    }
    if (cycle && ids == cycle_candidates_ &&
        std::hypot(px - cycle_x_, py - cycle_y_) <= pickbox_pixels && !ids.empty())
        cycle_position_ = (cycle_position_ + 1) % ids.size();
    else
        cycle_position_ = 0;
    cycle_candidates_ = ids;
    cycle_x_ = px;
    cycle_y_ = py;
    remember_selection();
    if (!add) {
        selection_.clear();
        sel_texts_.clear();
        sel_refs_.clear();
        sel_dims_.clear(); sel_hatches_.clear();
    }
    if (!hits.empty()) {
        const auto& object = objects[hits[cycle_position_].second];
        bool whole = !edge || object.kind != SelectionKind::Shape;
        if (!whole)
            for (auto i : polyline_pieces(object.index)) {
                const auto kind=shapes_[i].attrs.user.find("sketch_object");
                whole |= shapes_[i].attrs.user.count("door") ||
                         (kind!=shapes_[i].attrs.user.end() && kind->second!="wall") ||
                         shapes_[i].layer == "A-DOOR";
            }
        if (whole) include_selection(object, add);
        else {
            std::size_t closest = object.index;
            double best = 1e300;
            for (auto i : polyline_pieces(object.index))
                for (auto line : selection_lines(shapes_[i])) {
                    const double d = distance(p, {line[0], line[1]}, {line[2], line[3]});
                    if (d < best) { best = d; closest = i; }
                }
            const auto found = std::find(selection_.begin(), selection_.end(), closest);
            if (found == selection_.end()) selection_.push_back(closest);
            else if (add) selection_.erase(found);
        }
    }
    if(hits.empty()) {
        const auto hatch=pick_hatch(px,py);
        if(hatch!=npos) {
            const auto found=std::find(sel_hatches_.begin(),sel_hatches_.end(),hatch);
            if(add && found!=sel_hatches_.end())sel_hatches_.erase(found);
            else if(found==sel_hatches_.end())sel_hatches_.push_back(hatch);
            rebuild_grips();message_="hatch selected — use the Hatch style bar";return 1;
        }
    }
    rebuild_grips();
    message_ = hits.empty() ? "nothing selected"
                            : "object " + std::to_string(cycle_position_ + 1) + " of " +
                                  std::to_string(hits.size()) + " at cursor";
    return hits.size();
}
void Canvas::select_edge_at(double px,double py) {
    select_hit(px, py, true, false, true);
}
std::size_t Canvas::cycle_selection(double px, double py, bool add) {
    return select_hit(px, py, add, true);
}
}
