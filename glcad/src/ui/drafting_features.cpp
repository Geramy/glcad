#include "ui/canvas.hpp"
#include "math/predicate.hpp"
#include "plancheck/plancheck.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace gcapp {
namespace {
using Point = gcm::Lattice2;
using Key = std::pair<gcm::i64, gcm::i64>;
Key key(Point p) { return {p.x, p.y}; }
bool same(Point a, Point b) { return key(a) == key(b); }
long double distance2(Point a, Point b) {
    const long double x = static_cast<long double>(a.x) - b.x,
                      y = static_cast<long double>(a.y) - b.y;
    return x * x + y * y;
}
gregion::Result joined_walls(const std::vector<gsnap::Shape>& shapes) {
    auto result=gregion::from_shapes(shapes);
    if(result.ok) return result;
    std::map<gcm::u32,std::vector<gsnap::Shape>> groups;
    for(const auto& shape:shapes) groups[shape.poly].push_back(shape);
    bool first=true;
    for(const auto& [id,boundary]:groups) {
        auto part=gregion::from_shapes(boundary);
        if(!part.ok) return part;
        if(first) {result=std::move(part);first=false;}
        else result=gregion::boolean_op(result.region,part.region,gregion::Operation::Union);
        if(!result.ok) return result;
    }
    return result;
}
struct DoorFrame {
    Point hinge, latch, leaf;
};
bool door_frame(const std::vector<gsnap::Shape> &shapes, std::size_t i, DoorFrame &frame) {
    if (i >= shapes.size())
        return false;
    const auto &arc = shapes[i];
    if (arc.kind != gsnap::ShapeKind::Arc || arc.radius <= 0 ||
        !std::isfinite(arc.start_angle) || !std::isfinite(arc.end_angle) ||
        (arc.layer != "A-DOOR" && !arc.attrs.user.count("door")))
        return false;
    const auto endpoint = [&](double angle) {
        return Point{arc.a.x + gcm::i64(std::llround(arc.radius * std::cos(angle))),
                     arc.a.y + gcm::i64(std::llround(arc.radius * std::sin(angle)))};
    };
    const Point ends[2] = {endpoint(arc.start_angle), endpoint(arc.end_angle)};
    int leaf = -1;
    const long double tolerance = std::max(4.0L, static_cast<long double>(arc.radius) * 1e-6L);
    for (const auto &s : shapes) {
        if (s.kind != gsnap::ShapeKind::Segment || s.layer != arc.layer ||
            (arc.poly && s.poly != arc.poly))
            continue;
        const auto other = same(s.a, arc.a) ? s.b : same(s.b, arc.a) ? s.a : Point{};
        if (!same(s.a, arc.a) && !same(s.b, arc.a))
            continue;
        for (int e = 0; e < 2; ++e)
            if (distance2(other, ends[e]) <= tolerance * tolerance)
                leaf = e;
    }
    if (leaf < 0)
        leaf = 1;
    frame = {arc.a, ends[1 - leaf], ends[leaf]};
    return true;
}

std::vector<gdoc::Loop> area_loops(std::vector<gdoc::Segment2> segments, long double gap) {
    if (segments.size() > 12000)
        return {};
    std::sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
        return std::min(a.a.x, a.b.x) < std::min(b.a.x, b.b.x);
    });
    std::vector<std::vector<Point>> splits(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i)
        splits[i] = {segments[i].a, segments[i].b};
    for (std::size_t i = 0; i < segments.size(); ++i)
        for (std::size_t j = i + 1; j < segments.size(); ++j) {
            const auto a = segments[i], b = segments[j];
            if (std::min(b.a.x, b.b.x) > std::max(a.a.x, a.b.x))
                break;
            if (std::max(a.a.x, a.b.x) < std::min(b.a.x, b.b.x) ||
                std::max(b.a.x, b.b.x) < std::min(a.a.x, a.b.x) ||
                std::max(a.a.y, a.b.y) < std::min(b.a.y, b.b.y) ||
                std::max(b.a.y, b.b.y) < std::min(a.a.y, a.b.y))
                continue;
            const long double rx = static_cast<long double>(a.b.x) - a.a.x,
                              ry = static_cast<long double>(a.b.y) - a.a.y;
            const long double sx = static_cast<long double>(b.b.x) - b.a.x,
                              sy = static_cast<long double>(b.b.y) - b.a.y;
            const long double qx = static_cast<long double>(b.a.x) - a.a.x,
                              qy = static_cast<long double>(b.a.y) - a.a.y;
            const auto det = rx * sy - ry * sx;
            if (det != 0) {
                const auto t = (qx * sy - qy * sx) / det, u = (qx * ry - qy * rx) / det;
                if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
                    const Point p{gcm::i64(std::llround(a.a.x + t * rx)),
                                  gcm::i64(std::llround(a.a.y + t * ry))};
                    splits[i].push_back(p);
                    splits[j].push_back(p);
                }
            } else if (qx * ry - qy * rx == 0) {
                for (auto p : {b.a, b.b})
                    if (p.x >= std::min(a.a.x, a.b.x) && p.x <= std::max(a.a.x, a.b.x) &&
                        p.y >= std::min(a.a.y, a.b.y) && p.y <= std::max(a.a.y, a.b.y))
                        splits[i].push_back(p);
                for (auto p : {a.a, a.b})
                    if (p.x >= std::min(b.a.x, b.b.x) && p.x <= std::max(b.a.x, b.b.x) &&
                        p.y >= std::min(b.a.y, b.b.y) && p.y <= std::max(b.a.y, b.b.y))
                        splits[j].push_back(p);
            }
        }
    std::map<Key, std::set<Key>> graph;
    const auto add = [&](Key a, Key b) {
        if (a != b) {
            graph[a].insert(b);
            graph[b].insert(a);
        }
    };
    for (auto &pts : splits) {
        std::sort(pts.begin(), pts.end(), [](Point a, Point b) { return key(a) < key(b); });
        for (std::size_t i = 1; i < pts.size(); ++i)
            add(key(pts[i - 1]), key(pts[i]));
    }
    if (gap > 0) {
        std::vector<Key> ends;
        for (const auto &[p, adj] : graph)
            if (adj.size() == 1)
                ends.push_back(p);
        std::vector<std::vector<std::size_t>> near(ends.size());
        for (std::size_t i = 0; i < ends.size(); ++i)
            for (std::size_t j = i + 1; j < ends.size(); ++j)
                if (distance2({ends[i].first, ends[i].second}, {ends[j].first, ends[j].second}) <=
                    gap * gap) {
                    near[i].push_back(j);
                    near[j].push_back(i);
                }
        for (std::size_t i = 0; i < ends.size(); ++i) {
            if (near[i].size() != 1 || near[near[i][0]].size() != 1)
                continue;
            const auto a = ends[i], b = ends[near[i][0]];
            bool crosses = false;
            for (const auto &[p, adj] : graph)
                for (auto q : adj)
                    if (p < q && gcm::predicate::Host::segments_properly_intersect(
                                     {a.first, a.second}, {b.first, b.second}, {p.first, p.second},
                                     {q.first, q.second}))
                        crosses = true;
            if (!crosses)
                add(a, b);
        }
    }
    std::vector<Key> dead;
    for (const auto &[p, adj] : graph)
        if (adj.size() < 2)
            dead.push_back(p);
    while (!dead.empty()) {
        auto p = dead.back();
        dead.pop_back();
        auto adj = graph[p];
        graph.erase(p);
        for (auto q : adj)
            if (graph.count(q)) {
                graph[q].erase(p);
                if (graph[q].size() < 2)
                    dead.push_back(q);
            }
    }
    std::map<Key, std::vector<Key>> ordered;
    for (const auto &[p, adj] : graph) {
        auto &list = ordered[p];
        list.assign(adj.begin(), adj.end());
        std::sort(list.begin(), list.end(), [&](Key a, Key b) {
            return std::atan2(static_cast<long double>(a.second) - p.second,
                              static_cast<long double>(a.first) - p.first) <
                   std::atan2(static_cast<long double>(b.second) - p.second,
                              static_cast<long double>(b.first) - p.first);
        });
    }
    std::set<std::pair<Key, Key>> used;
    std::vector<gdoc::Loop> loops;
    for (const auto &[a, adj] : ordered)
        for (auto b : adj) {
            if (used.count({a, b}))
                continue;
            Key p = a, q = b;
            gdoc::Loop loop;
            do {
                if (!used.insert({p, q}).second)
                    break;
                loop.push_back({p.first, p.second});
                const auto &next = ordered[q];
                auto at = std::find(next.begin(), next.end(), p);
                const auto r =
                    next[(std::size_t(at - next.begin()) + next.size() - 1) % next.size()];
                p = q;
                q = r;
            } while (p != a || q != b);
            if (p == a && q == b && loop.size() >= 3 && gdoc::loop_area_sign(loop) > 0)
                loops.push_back(std::move(loop));
        }
    return loops;
}
}

void Canvas::group_door(std::size_t first) {
    if (first >= shapes_.size())
        return;
    const auto poly = next_polyline_id();
    for (std::size_t i = first; i < shapes_.size(); ++i) {
        auto &s = shapes_[i];
        s.poly = poly;
        s.poly_index = gcm::u32(i - first);
        s.attrs.user["door"] = "swing";
    }
    ++edit_serial_;
}

namespace {
constexpr const char* opening_material = "opening_material";
bool wall_material_shape(const gsnap::Shape& s) {
    const auto type=s.attrs.user.find("sketch_object");
    return s.layer=="A-WALL" && s.kind==gsnap::ShapeKind::Segment &&
        (type==s.attrs.user.end() || type->second=="wall");
}
std::vector<gsnap::Shape> material_shapes(const std::vector<gsnap::Shape>& shapes) {
    std::vector<gsnap::Shape> walls;
    for(const auto& s:shapes)if(wall_material_shape(s))walls.push_back(s);
    return walls;
}
std::string save_opening_material(const gregion::Region& region, gcm::u32 group) {
    std::ostringstream out;out<<group<<' ';
    for(const auto& loop:region.loops)for(const auto& edge:loop) {
        const auto [a,b]=gregion::endpoints(edge);
        out<<a.x<<' '<<a.y<<' '<<b.x<<' '<<b.y<<' ';
    }
    return out.str();
}
gregion::Result read_opening_material(const std::string& text,gcm::u32& group) {
    std::istringstream in(text);in>>group;
    std::vector<gsnap::Shape> edges;gsnap::Shape edge;
    while(in>>edge.a.x>>edge.a.y>>edge.b.x>>edge.b.y)edges.push_back(edge);
    return gregion::from_shapes(edges);
}
}

void Canvas::track_door_opening(gcm::u32 door_key,const std::vector<gsnap::Shape>& uncut,bool automatic) {
    const auto index=index_of_key(door_key);
    if(index==npos || !automatic)return;
    auto& door=shapes_[index];door.attrs.user["opening_auto"]="1";
    const auto before=material_shapes(uncut),after=material_shapes(shapes_);
    if(before.empty())return;
    const auto base=gregion::from_shapes(before),cut=gregion::from_shapes(after);
    if(!base.ok || (!after.empty() && !cut.ok))return;
    const auto removed=after.empty()?base:gregion::boolean_op(base.region,cut.region,gregion::Operation::Subtract);
    if(removed.ok && !removed.region.empty())
        door.attrs.user[opening_material]=save_opening_material(removed.region,before.front().poly);
}

bool Canvas::update_door_openings(const std::vector<gsnap::Shape>& before) {
    const auto tracked=[](const auto& s){return s.kind==gsnap::ShapeKind::Arc && s.attrs.user.count("opening_auto");};
    std::vector<gsnap::Shape> old_doors,new_doors;
    for(const auto& s:before)if(tracked(s))old_doors.push_back(s);
    for(const auto& s:shapes_)if(tracked(s))new_doors.push_back(s);
    const auto equal=[](const auto& a,const auto& b) {
        return a.key==b.key && same(a.a,b.a) && a.radius==b.radius &&
            a.start_angle==b.start_angle && a.end_angle==b.end_angle;
    };
    std::map<gcm::u32,const gsnap::Shape*> old_by_key,new_by_key;
    for(const auto& door:old_doors)old_by_key[door.key]=&door;
    for(const auto& door:new_doors)new_by_key[door.key]=&door;
    std::vector<DraftingBounds> dirty;
    const auto changed=[&](const auto& doors,const auto& other) {
        for(const auto& door:doors) {
            const auto found=other.find(door.key);
            if(found==other.end()||!equal(door,*found->second))dirty.push_back(drafting_bounds(door).expanded(.501/vol_.inv_scale()));
        }
    };
    changed(old_doors,new_by_key);changed(new_doors,old_by_key);
    if(dirty.empty())return false;
    const auto all_walls=material_shapes(shapes_);
    const auto nearby=nearby_wall_contours(all_walls,dirty);
    std::vector<gsnap::Shape> walls;std::vector<DraftingBounds> wall_bounds;std::set<gcm::u32> wall_keys;
    for(std::size_t i=0;i<all_walls.size();++i)if(nearby[i]) {
        walls.push_back(all_walls[i]);wall_bounds.push_back(drafting_bounds(all_walls[i]));wall_keys.insert(all_walls[i].key);
    }
    if(walls.empty())return false;
    drafting_work_.opening_edges+=walls.size();
    const auto nearby_door=[&](const auto& door){
        const auto bounds=drafting_bounds(door).expanded(.501/vol_.inv_scale());
        return drafting_touches(bounds,dirty)&&drafting_touches(bounds,wall_bounds);
    };
    auto base=gregion::from_shapes(walls);
    if(!base.ok)return false;
    for(const auto& door:old_doors) {
        if(!nearby_door(door))continue;
        const auto saved=door.attrs.user.find(opening_material);
        if(saved==door.attrs.user.end())continue;
        gcm::u32 group=0;const auto patch=read_opening_material(saved->second,group);
        if(!patch.ok)continue;
        bool attached=false;
        for(const auto& loop:patch.region.loops)for(const auto& e:loop) {
            const auto [a,b]=gregion::endpoints(e);
            for(const auto& wall:walls)if(distance2(a,wall.a)<=16 || distance2(a,wall.b)<=16 ||
                distance2(b,wall.a)<=16 || distance2(b,wall.b)<=16)attached=true;
        }
        if(!attached)continue;
        auto joined=gregion::boolean_op(base.region,patch.region,gregion::Operation::Union);
        if(!joined.ok)return false;
        base=std::move(joined);
    }
    auto candidate=shapes_;
    auto material=base.region;
    std::vector<gsnap::Shape> full;
    for(const auto& loop:base.region.loops)for(const auto& e:loop) {
        auto s=e.shape;const auto [a,b]=gregion::endpoints(e);s.a=a;s.b=b;full.push_back(s);
    }
    for(std::size_t i=0;i<candidate.size();++i) {
        auto& door=candidate[i];if(!tracked(door)||!nearby_door(door))continue;
        ++drafting_work_.opening_doors;
        door.attrs.user.erase(opening_material);
        DoorFrame frame;if(!door_frame(candidate,i,frame))continue;
        double ax,ay,bx,by,tx,ty;
        gcm::to_world(vol_,frame.hinge,ax,ay);gcm::to_world(vol_,frame.latch,bx,by);
        gcm::to_world(vol_,frame.leaf,tx,ty);
        const double width=std::hypot(bx-ax,by-ay);if(width<1e-8)continue;
        const double ux=(bx-ax)/width,uy=(by-ay)/width;
        const double sign=((tx-ax)*(-uy)+(ty-ay)*ux)>=0?1:-1;
        struct Face {double offset;const gsnap::Shape* shape;};
        struct Support {double offset;const gsnap::Shape* shape;std::vector<std::pair<double,double>> spans;};
        std::vector<Support> supports;
        const double tolerance=std::max(8*vol_.inv_scale(),1e-6);
        for(const auto& edge:full) {
            double x,y,qx,qy;gcm::to_world(vol_,edge.a,x,y);gcm::to_world(vol_,edge.b,qx,qy);
            const double len=std::hypot(qx-x,qy-y);
            if(len<1e-8 || std::abs((qx-x)*uy-(qy-y)*ux)>len*1e-6)continue;
            double p=(x-ax)*ux+(y-ay)*uy,q=(qx-ax)*ux+(qy-ay)*uy;
            if(p>q)std::swap(p,q);
            if(q<-tolerance || p>width+tolerance)continue;
            const double offset=(x-ax)*(-uy)+(y-ay)*ux;
            if(std::abs(offset)>.5)continue;
            auto support=std::find_if(supports.begin(),supports.end(),[&](const auto& s){return std::abs(s.offset-offset)<=tolerance;});
            if(support==supports.end()) {supports.push_back({offset,&edge,{{p,q}}});}
            else support->spans.push_back({p,q});
        }
        std::vector<Face> faces;
        for(auto& support:supports) {
            std::sort(support.spans.begin(),support.spans.end());double covered=0;
            for(const auto& [p,q]:support.spans) {if(p>covered+tolerance)break;covered=std::max(covered,q);}
            if(covered>=width-tolerance)faces.push_back({support.offset,support.shape});
        }
        double best=.251,low=0,high=0;const gsnap::Shape* host=nullptr;
        for(const auto& a:faces)for(const auto& b:faces) {
            const double thickness=b.offset-a.offset;
            if(thickness<.05 || thickness>.5)continue;
            const double distance=std::abs(sign>0?b.offset:a.offset);
            if(distance<best) {best=distance;low=a.offset;high=b.offset;host=a.shape;}
        }
        if(!host)continue;
        const double shift=sign>0?high:low;
        const gcm::Lattice2 delta{gcm::i64(std::llround(-uy*shift/vol_.inv_scale())),gcm::i64(std::llround(ux*shift/vol_.inv_scale()))};
        for(auto& piece:candidate)if(door.poly?piece.poly==door.poly:piece.key==door.key) {
            piece.a.x+=delta.x;piece.a.y+=delta.y;piece.b.x+=delta.x;piece.b.y+=delta.y;
        }
        const auto point=[&](double along,double across) {return gcm::to_lattice(vol_,ax+ux*along-uy*across,ay+uy*along+ux*across);};
        const double eps=4*vol_.inv_scale();
        const gcm::Lattice2 points[]={point(0,low-eps),point(width,low-eps),point(width,high+eps),point(0,high+eps)};
        std::vector<gsnap::Shape> edges;
        for(int j=0;j<4;++j){auto s=*host;s.a=points[j];s.b=points[(j+1)%4];s.region=false;edges.push_back(s);}
        const auto cutter=gregion::from_shapes(edges);if(!cutter.ok)return false;
        const auto patch=gregion::boolean_op(base.region,cutter.region,gregion::Operation::Intersect);
        const auto cut=gregion::boolean_op(material,cutter.region,gregion::Operation::Subtract);
        if(!patch.ok || !cut.ok)return false;
        door.attrs.user[opening_material]=save_opening_material(patch.region,walls.front().poly);
        material=cut.region;
    }
    std::vector<gcm::u32> selected;
    for(auto i:selection_)if(i<shapes_.size())selected.push_back(shapes_[i].key);
    std::erase_if(candidate,[&](const auto& s){return wall_material_shape(s)&&wall_keys.contains(s.key);});
    auto wall_group=walls.front().poly;
    if(std::any_of(candidate.begin(),candidate.end(),[&](const auto& s){return wall_material_shape(s)&&s.poly==wall_group;}))wall_group=next_polyline_id();
    std::map<std::pair<Key,Key>,gcm::u32> existing_edges;
    const auto edge_key=[](gcm::Lattice2 a,gcm::Lattice2 b){auto ka=key(a),kb=key(b);if(kb<ka)std::swap(ka,kb);return std::pair{ka,kb};};
    for(const auto& old:walls)existing_edges[edge_key(old.a,old.b)]=old.key;
    gcm::u32 order=0;
    const auto appearance=[](const auto& s){const auto& a=s.attrs;return std::tie(a.color,a.linetype,a.lineweight,a.linetype_scale,a.transparency,a.visible,a.user,a.foreign_format,a.foreign_blob);};
    const auto joins=[&](const auto& a,const auto& b) {
        return a.kind==gsnap::ShapeKind::Segment&&b.kind==a.kind&&same(a.b,b.a)&&!same(a.a,b.b)&&
            appearance(a)==appearance(b)&&gcm::predicate::Host::collinear(a.a,a.b,b.b);
    };
    for(const auto& loop:material.loops) {
        std::vector<gsnap::Shape> boundary;
        for(const auto& edge:loop) {
            auto s=edge.shape;const auto [a,b]=gregion::endpoints(edge);s.a=a;s.b=b;
            s.region=false;s.region_loop=0;s.region_reversed=false;
            s.layer="A-WALL";s.poly=wall_group;s.key=0;
            if(!wall_material_shape(edge.shape))s.attrs=walls.front().attrs;
            if(!boundary.empty()&&joins(boundary.back(),s))boundary.back().b=s.b;
            else boundary.push_back(std::move(s));
        }
        if(boundary.size()>1&&joins(boundary.back(),boundary.front())) {
            boundary.front().a=boundary.back().a;boundary.pop_back();
        }
        for(auto& s:boundary) {
            s.poly_index=order++;
            if(const auto found=existing_edges.find(edge_key(s.a,s.b));found!=existing_edges.end())s.key=found->second;
            candidate.push_back(std::move(s));
        }
    }
    shapes_=std::move(candidate);assign_keys();selection_.clear();
    for(auto key:selected)if(const auto i=index_of_key(key);i!=npos)selection_.push_back(i);
    align_dirty_=true;
    return true;
}

namespace {
std::vector<std::size_t> selected_doors(const std::vector<gsnap::Shape>& shapes,
                                        const std::vector<std::size_t>& selection) {
    std::set<gcm::u32> groups;
    for (auto i : selection)
        if (i < shapes.size() && shapes[i].poly) groups.insert(shapes[i].poly);
    std::vector<std::size_t> result;
    DoorFrame frame;
    for (std::size_t i = 0; i < shapes.size(); ++i)
        if ((groups.count(shapes[i].poly) || std::find(selection.begin(), selection.end(), i) != selection.end()) &&
            door_frame(shapes, i, frame)) result.push_back(i);
    return result;
}
bool opposite_door_face(const std::vector<gsnap::Shape>& shapes, const DoorFrame& f,
                         double unit, double& shift_x, double& shift_y) {
    const double dx = double(f.latch.x) - f.hinge.x, dy = double(f.latch.y) - f.hinge.y;
    const double width = std::hypot(dx, dy), ux = dx / width, uy = dy / width;
    double nx = -uy, ny = ux;
    if (nx * (double(f.leaf.x) - f.hinge.x) + ny * (double(f.leaf.y) - f.hinge.y) < 0) {
        nx = -nx; ny = -ny;
    }
    const double tolerance = std::max(4., .001 / unit);
    std::vector<double> jambs[2];
    bool touches_wall = false;
    for (const auto& wall : shapes) {
        if (wall.layer != "A-WALL" || wall.kind != gsnap::ShapeKind::Segment) continue;
        for (int end = 0; end < 2; ++end) {
            const auto p = end ? f.latch : f.hinge;
            const double wx = double(wall.b.x) - wall.a.x, wy = double(wall.b.y) - wall.a.y;
            const double length2 = wx * wx + wy * wy;
            const double t = length2 ? std::clamp(((double(p.x) - wall.a.x) * wx +
                (double(p.y) - wall.a.y) * wy) / length2, 0., 1.) : 0.;
            if (std::hypot(double(p.x) - wall.a.x - t * wx,
                           double(p.y) - wall.a.y - t * wy) <= tolerance) touches_wall = true;
            for (auto ends : {std::pair{wall.a, wall.b}, std::pair{wall.b, wall.a}}) {
                if (distance2(p, ends.first) > tolerance * tolerance) continue;
                const double vx = double(ends.second.x) - p.x, vy = double(ends.second.y) - p.y;
                const double across = vx * nx + vy * ny;
                if (std::abs(vx * ux + vy * uy) <= tolerance && across < -tolerance &&
                    -across * unit <= .6) jambs[end].push_back(across);
            }
        }
    }
    std::vector<double> matches;
    for (double a : jambs[0]) for (double b : jambs[1])
        if (std::abs(a - b) <= tolerance &&
            std::none_of(matches.begin(), matches.end(), [&](double v) { return std::abs(v-a) <= tolerance; }))
            matches.push_back((a+b)*.5);
    if (matches.size() == 1) {
        shift_x = nx * matches[0]; shift_y = ny * matches[0]; return true;
    }
    shift_x = shift_y = 0;
    return !touches_wall;
}
}
bool Canvas::has_selected_door() const { return selected_door_count() != 0; }
std::size_t Canvas::selected_door_count() const { return selected_doors(shapes_, selection_).size(); }
bool Canvas::set_door_flow(std::string_view direction) {
    if (direction.empty()) { message_ = "Door flow: use forward, reverse, both or off"; return false; }
    return edit_selected_doors(direction);
}
bool Canvas::edit_selected_doors(std::string_view flow, bool reverse_swing, bool reverse_hinge) {
    if ((!flow.empty() && flow != "forward" && flow != "reverse" && flow != "both" && flow != "off") ||
        (flow.empty() && !reverse_swing && !reverse_hinge)) {
        message_ = "Door edit: choose forward, reverse, both or off, or reverse swing / hinge";
        return false;
    }
    const auto doors = selected_doors(shapes_, selection_);
    if (doors.empty()) { message_ = "Select one or more doors first"; return false; }
    auto candidate = shapes_;
    std::set<gcm::u32> edited_groups;
    for (auto index : doors) {
        const auto& arc = shapes_[index];
        DoorFrame frame;
        if (!door_frame(shapes_, index, frame)) return false;
        if (!flow.empty()) candidate[index].attrs.user["door_flow"] = std::string(flow);
        if (!reverse_swing && !reverse_hinge) continue;
        const auto pieces = polyline_pieces(index);
        if (!arc.poly || !edited_groups.insert(arc.poly).second || pieces.size() != 2) {
            message_ = "Door edit: swing and hinge reversal need a grouped leaf and swing arc; no doors changed";
            return false;
        }
        bool leaf_found = false;
        for (auto i : pieces) {
            const auto& piece = shapes_[i];
            leaf_found |= piece.kind == gsnap::ShapeKind::Segment &&
                ((same(piece.a, frame.hinge) && distance2(piece.b, frame.leaf) <= 16) ||
                 (same(piece.b, frame.hinge) && distance2(piece.a, frame.leaf) <= 16));
        }
        if (!leaf_found) { message_ = "Door edit: could not identify the door leaf; no doors changed"; return false; }
        double shift_x = 0, shift_y = 0;
        if (reverse_swing && !opposite_door_face(shapes_, frame, vol_.inv_scale(), shift_x, shift_y)) {
            message_ = "Door edit: cannot identify matching opposite wall faces at both jambs; no doors changed";
            return false;
        }
        const double dx = double(frame.latch.x) - frame.hinge.x, dy = double(frame.latch.y) - frame.hinge.y;
        const double width = std::hypot(dx, dy), ux = dx / width, uy = dy / width;
        const auto transform = [&](gcm::Lattice2 p) {
            double along = (double(p.x) - frame.hinge.x) * ux + (double(p.y) - frame.hinge.y) * uy;
            double across = -(double(p.x) - frame.hinge.x) * uy + (double(p.y) - frame.hinge.y) * ux;
            if (reverse_hinge) along = width - along;
            if (reverse_swing) across = -across;
            return gcm::Lattice2{gcm::i64(std::llround(frame.hinge.x + along * ux - across * uy + shift_x)),
                         gcm::i64(std::llround(frame.hinge.y + along * uy + across * ux + shift_y))};
        };
        for (auto i : pieces) {
            auto& piece = candidate[i];
            if (piece.kind == gsnap::ShapeKind::Arc) {
                auto angle = [&](double a) {
                    const double vx = std::cos(a), vy = std::sin(a);
                    double along = vx * ux + vy * uy, across = -vx * uy + vy * ux;
                    if (reverse_hinge) along = -along;
                    if (reverse_swing) across = -across;
                    return std::atan2(along * uy + across * ux, along * ux - across * uy);
                };
                piece.start_angle = angle(piece.start_angle); piece.end_angle = angle(piece.end_angle);
                if (reverse_swing != reverse_hinge) std::swap(piece.start_angle, piece.end_angle);
            } else piece.b = transform(piece.b);
            piece.a = transform(piece.a);
        }
    }
    if (reverse_swing || reverse_hinge) {
        std::vector<gsnap::Shape> check;
        for (const auto& shape : candidate) if (shape.layer == "A-WALL") check.push_back(shape);
        for (auto index : doors) check.push_back(candidate[index]);
        gplan::Plan plan; plan.shapes = &check; plan.metres_per_unit = vol_.inv_scale();
        if (!gplan::door_swing_collisions(plan).empty()) {
            message_ = "Door edit: a proposed swing intersects a wall; no doors changed";
            return false;
        }
    }
    mark();
    shapes_ = std::move(candidate);
    rebuild_grips();
    message_ = "Updated " + std::to_string(doors.size()) + (doors.size() == 1 ? " door" : " doors");
    return true;
}
std::vector<gsnap::Shape> Canvas::door_flow_shapes(std::size_t only) const {
    std::vector<gsnap::Shape> out;
    for (std::size_t i = 0; i < shapes_.size(); ++i) {
        const auto &s = shapes_[i];
        const auto it = s.attrs.user.find("door_flow");
        if (only != std::size_t(-1) && i != only) continue;
        DoorFrame f;
        if (it == s.attrs.user.end() || it->second == "off" || !shape_visible(s) ||
            !door_frame(shapes_, i, f))
            continue;
        if (it->second != "forward" && it->second != "reverse" && it->second != "both")
            continue;
        double hx, hy, lx, ly, tx, ty;
        gcm::to_world(vol_, f.hinge, hx, hy);
        gcm::to_world(vol_, f.latch, lx, ly);
        gcm::to_world(vol_, f.leaf, tx, ty);
        const auto width = std::hypot(lx - hx, ly - hy);
        if (width < 1e-12)
            continue;
        double nx = -(ly - hy) / width, ny = (lx - hx) / width;
        if (nx * (tx - hx) + ny * (ty - hy) < 0) {
            nx = -nx;
            ny = -ny;
        }
        if (it->second == "reverse") {
            nx = -nx;
            ny = -ny;
        }
        const double cx = (hx + lx) * .5, cy = (hy + ly) * .5;
        const double len = width * .16, head = width * .03;
        const auto line = [&](double ax, double ay, double bx, double by) {
            gsnap::Shape q;
            q.a = gcm::to_lattice(vol_, ax, ay);
            q.b = gcm::to_lattice(vol_, bx, by);
            q.layer = s.layer;
            q.attrs = s.attrs;
            q.attrs.user.clear();
            q.attrs.lineweight = gdoc::Lineweight::W025;
            out.push_back(q);
        };
        line(cx - nx * len * .5, cy - ny * len * .5, cx + nx * len * .5, cy + ny * len * .5);
        const auto tip = [&](double sign) {
            const double x = cx + sign * nx * len * .5, y = cy + sign * ny * len * .5;
            line(x, y, x - sign * nx * head - ny * head * .55,
                 y - sign * ny * head + nx * head * .55);
            line(x, y, x - sign * nx * head + ny * head * .55,
                 y - sign * ny * head - nx * head * .55);
        };
        tip(1);
        if (it->second == "both")
            tip(-1);
    }
    return out;
}

std::vector<gsnap::Shape> Canvas::background_boundary(const gdoc::AssociativeHatch &h, bool local) const {
    const auto window=drafting_bounds(h).expanded(h.gap_tolerance/vol_.inv_scale()+8);
    local=local&&window.valid;
    std::set<gcm::u32> keys(h.source_keys.begin(), h.source_keys.end());
    std::vector<gsnap::Shape> source;
    for (const auto &s : shapes_)
        if ((keys.empty() || keys.count(s.key)) && shape_visible(s) && (!local||window.overlaps(drafting_bounds(s))))
            source.push_back(s);
    std::vector<gsnap::Shape> boundary;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const auto &s = source[i];
        DoorFrame f;
        if (door_frame(source, i, f)) {
            gsnap::Shape threshold;
            threshold.a = f.hinge;
            threshold.b = f.latch;
            boundary.push_back(threshold);
        } else if (s.layer != "A-DOOR" && !s.attrs.user.count("door"))
            boundary.push_back(s);
    }
    auto segments = segments_of(boundary);
    const auto gap = h.gap_tolerance / vol_.inv_scale();
    const bool unchanged = background_graph_gap_ == gap &&
        background_graph_segments_.size() == segments.size() &&
        std::equal(segments.begin(), segments.end(), background_graph_segments_.begin(),
            [](const auto& a, const auto& b) { return same(a.a,b.a) && same(a.b,b.b); });
    if (!unchanged) {
        ++drafting_work_.background_traces;drafting_work_.background_segments+=segments.size();
        background_graph_loops_ = area_loops(segments, gap);
        background_graph_segments_ = std::move(segments);
        background_graph_gap_ = gap;
    }
    const auto& loops = background_graph_loops_;
    auto area = [](const gdoc::Loop &loop) {
        long double a = 0;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            auto p = loop[i], q = loop[(i + 1) % loop.size()];
            a += static_cast<long double>(p.x) * q.y - static_cast<long double>(q.x) * p.y;
        }
        return std::abs(a);
    };
    std::size_t best = loops.size();
    long double smallest = 1e100L;
    for (std::size_t i = 0; i < loops.size(); ++i)
        if (gdoc::point_in_loop(h.seed, loops[i]) && area(loops[i]) < smallest) {
            best = i;
            smallest = area(loops[i]);
        }
    if (best == loops.size())
        return local?background_boundary(h,false):std::vector<gsnap::Shape>{};
    std::vector<gdoc::Loop> region{loops[best]};
    for (std::size_t i = 0; i < loops.size(); ++i)
        if (i != best && area(loops[i]) < smallest) {
            bool touching = false;
            for (auto p : loops[i])
                for (auto q : loops[best])
                    if (same(p, q))
                        touching = true;
            bool nested = false;
            for (std::size_t j = 0; j < loops.size(); ++j)
                if (j != best && j != i && area(loops[j]) < smallest &&
                    area(loops[j]) > area(loops[i]) && gdoc::point_in_loop(loops[i][0], loops[j]))
                    nested = true;
            if (!touching && !nested && gdoc::point_in_loop(loops[i][0], loops[best]))
                region.push_back(loops[i]);
        }
    std::vector<gsnap::Shape> out;
    for (const auto &loop : region)
        for (std::size_t i = 0; i < loop.size(); ++i) {
            gsnap::Shape s;
            s.a = loop[i];
            s.b = loop[(i + 1) % loop.size()];
            out.push_back(s);
        }
    if(local&&!window.contains(drafting_bounds(out)))return background_boundary(h,false);
    return out;
}
bool Canvas::background_at_world(gcm::Lattice2 seed, gdoc::Color color, double gap, int opacity) {
    if (!std::isfinite(gap) || gap < 0 || gap > 1 || opacity < 0 || opacity > 100) {
        message_ = "Background: gap must be 0–1 m and opacity 0–100%";
        return false;
    }
    gdoc::AssociativeHatch h;
    h.area_background = true;
    h.style.name = "SOLID";
    h.seed = seed;
    h.seed_valid = true;
    h.layer = current_layer_;
    h.gap_tolerance = gap;
    h.attrs.color = color;
    h.attrs.transparency = 100 - opacity;
    for (auto i : selection_)
        if (i < shapes_.size())
            h.source_keys.push_back(shapes_[i].key);
    if(std::any_of(sel_refs_.begin(),sel_refs_.end(),[&](auto i) {
        return i<blocks_.references().size() && is_wall_section(blocks_.references()[i]);
    }))for(const auto& shape:shapes_)if(is_joined_wall(shape))h.source_keys.push_back(shape.key);
    h.boundary_geometry = background_boundary(h);
    if (h.boundary_geometry.empty()) {
        message_ = "Background: no closed area here; select its boundary or adjust the gap limit";
        return false;
    }
    auto segments = segments_of(h.boundary_geometry);
    h.boundary_shapes.resize(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i)
        h.boundary_shapes[i] = gcm::u32(i);
    if (!gdoc::update_hatch(h, segments, patterns_, vol_).regenerated) {
        message_ = "Background: could not fill this boundary";
        return false;
    }
    mark();
    for (auto &old : hatches_)
        if (old.area_background && old.valid &&
            gdoc::point_in_region(seed, old.cached.fill_loops)) {
            old = std::move(h);
            message_ = "Area background updated";
            return true;
        }
    hatches_.push_back(std::move(h));
    message_ = "Area background added";
    return true;
}
bool Canvas::remove_background_at(gcm::Lattice2 seed) {
    for (std::size_t i = 0; i < hatches_.size(); ++i)
        if (hatches_[i].area_background && hatches_[i].valid &&
            gdoc::point_in_region(seed, hatches_[i].cached.fill_loops)) {
            mark();
            hatches_.erase(hatches_.begin() + i);
            sel_hatches_.clear();
            message_ = "Area background removed";
            return true;
        }
    message_ = "No area background here";
    return false;
}
}

namespace gcapp {
std::vector<std::size_t> Canvas::wall_centerline_indices() const {
    const std::set<std::size_t> selected(selection_.begin(),selection_.end());
    std::set<gcm::u32> symbols;
    const auto protected_symbol=[](const auto& s) {
        return s.attrs.user.count("door") || s.attrs.user.count("window") ||
            s.layer=="A-DOOR" || s.layer=="A-GLAZ";
    };
    for(const auto& s:shapes_)if(s.poly && (protected_symbol(s) || s.kind!=gsnap::ShapeKind::Segment))symbols.insert(s.poly);
    std::vector<std::size_t> result;
    for(std::size_t i=0;i<shapes_.size();++i) {
        const auto& s=shapes_[i];
        if(s.kind!=gsnap::ShapeKind::Segment || s.region || !shape_visible(s) || same(s.a,s.b) ||
           s.attrs.user.count("sketch_object") || protected_symbol(s) || (s.poly && symbols.count(s.poly)) ||
           (!selected.empty() && !selected.count(i)))continue;
        if(selected.empty() && !s.layer.empty() && s.layer!="0" && s.layer!="A-WALL")continue;
        result.push_back(i);
    }
    return result;
}
std::vector<gdoc::Segment2> Canvas::wall_material_segments() const {
    std::set<gcm::u32> selected_groups;
    for(auto i:selection_)if(i<shapes_.size()) {
        const auto& shape=shapes_[i];const auto type=shape.attrs.user.find("sketch_object");
        if(type!=shape.attrs.user.end() && type->second=="wall")selected_groups.insert(shape.poly);
    }
    std::vector<gsnap::Shape> material;
    for(const auto& s:shapes_) {
        const auto type=s.attrs.user.find("sketch_object");
        if(type!=s.attrs.user.end() && type->second=="wall" &&
           (selection_.empty() || selected_groups.count(s.poly)))material.push_back(s);
    }
    return segments_of(material);
}
std::size_t Canvas::walls_from_lines(double thickness) {
    if (!std::isfinite(thickness) || thickness <= 0) return 0;
    const auto centerlines=wall_centerline_indices();
    std::map<Key,std::size_t> endpoints;
    std::vector<gsnap::Shape> existing;
    for(const auto& s:shapes_) {
        const auto type=s.attrs.user.find("sketch_object");
        if(type!=s.attrs.user.end() && type->second=="wall")existing.push_back(s);
    }
    for(auto i:centerlines){++endpoints[key(shapes_[i].a)];++endpoints[key(shapes_[i].b)];}
    if (centerlines.empty()) {
        message_ = "WALLS: select plain centerlines; existing wall objects are already joined";
        return 0;
    }
    std::vector<gregion::Region> regions;
    std::vector<std::vector<gsnap::Shape>> sections;
    if (!existing.empty()) {
        auto old = joined_walls(existing);
        if (!old.ok) { message_ = "Repair the existing wall boundary before joining"; return 0; }
        regions.push_back(std::move(old.region));
    }
    const auto connects_existing = [&](gcm::Lattice2 p) {
        if (existing.empty()) return false;
        if (regions.front().contains(p)) return true;
        for (const auto& edge : existing) {
            if (edge.kind != gsnap::ShapeKind::Segment) continue;
            const long double dx = (long double)edge.b.x - edge.a.x, dy = (long double)edge.b.y - edge.a.y;
            const long double ll = dx * dx + dy * dy;
            if (ll == 0) continue;
            const auto u = std::clamp((((long double)p.x - edge.a.x) * dx + ((long double)p.y - edge.a.y) * dy) / ll, 0.L, 1.L);
            if (std::hypot((long double)p.x - edge.a.x - u * dx, (long double)p.y - edge.a.y - u * dy) <= 2) return true;
        }
        return false;
    };
    for (auto i : centerlines) {
        const auto& line = shapes_[i];
        double ax, ay, bx, by;
        gcm::to_world(vol_, line.a, ax, ay); gcm::to_world(vol_, line.b, bx, by);
        const double length = std::hypot(bx - ax, by - ay);
        const double ux = (bx - ax) / length, uy = (by - ay) / length;
        const double nx = -uy * thickness * .5, ny = ux * thickness * .5;
        if (endpoints[key(line.a)] > 1 || connects_existing(line.a)) { ax -= ux * thickness * .5; ay -= uy * thickness * .5; }
        if (endpoints[key(line.b)] > 1 || connects_existing(line.b)) { bx += ux * thickness * .5; by += uy * thickness * .5; }
        const gcm::Lattice2 corners[] = {gcm::to_lattice(vol_, ax + nx, ay + ny), gcm::to_lattice(vol_, ax - nx, ay - ny),
            gcm::to_lattice(vol_, bx - nx, by - ny), gcm::to_lattice(vol_, bx + nx, by + ny)};
        std::vector<gsnap::Shape> boundary;
        for (int j = 0; j < 4; ++j) { gsnap::Shape s; s.a = corners[j]; s.b = corners[(j + 1) % 4]; boundary.push_back(s); }
        auto part = gregion::from_shapes(boundary);
        if (!part.ok) { message_ = "WALLS: thickness is too small for this drawing"; return 0; }
        for(auto& edge:boundary) {edge.layer="A-WALL";edge.attrs.user["sketch_object"]="wall";}
        sections.push_back(boundary);
        regions.push_back(std::move(part.region));
    }
    while (regions.size() > 1) {
        std::vector<gregion::Region> next;
        for (std::size_t i = 0; i < regions.size(); i += 2) {
            if (i + 1 == regions.size()) { next.push_back(std::move(regions[i])); continue; }
            auto joined = gregion::boolean_op(regions[i], regions[i + 1], gregion::Operation::Union);
            if (!joined.ok) { message_ = joined.error; return 0; }
            next.push_back(std::move(joined.region));
        }
        regions = std::move(next);
    }
    mark();
    for(auto i=centerlines.rbegin();i!=centerlines.rend();++i)shapes_.erase(shapes_.begin()+*i);
    adopt_wall_sections();
    for(const auto& section:sections)add_wall_section(section);
    sync_wall_sections();
    clear_selection();
    align_dirty_=true;refresh_hatches();
    message_ = std::to_string(centerlines.size()) + " centerline(s) converted to joined 2D walls";
    return centerlines.size();
}

bool Canvas::add_wall(gcm::Lattice2 a, gcm::Lattice2 b, double thickness) {
    double ax, ay, bx, by;
    gcm::to_world(vol_, a, ax, ay); gcm::to_world(vol_, b, bx, by);
    const double length = std::hypot(bx-ax, by-ay);
    if (!std::isfinite(thickness) || thickness <= 0 || length < 1e-9) return false;
    std::vector<gsnap::Shape> existing;
    for (const auto& shape:shapes_) {
        auto it=shape.attrs.user.find("sketch_object");
        if(it!=shape.attrs.user.end() && it->second=="wall") existing.push_back(shape);
    }
    gregion::Result old;
    if(!existing.empty()) {
        old=joined_walls(existing);
        if(!old.ok) {message_="Repair the existing wall boundary before joining";return false;}
        const auto connected=[&](gcm::Lattice2 point) {
            if(old.region.contains(point)) return true;
            for(const auto& edge:existing) {
                const long double dx=(long double)edge.b.x-edge.a.x,dy=(long double)edge.b.y-edge.a.y;
                const long double ll=dx*dx+dy*dy;
                if(ll==0) continue;
                const auto u=std::clamp((((long double)point.x-edge.a.x)*dx+((long double)point.y-edge.a.y)*dy)/ll,0.L,1.L);
                if(std::hypot((long double)point.x-edge.a.x-u*dx,(long double)point.y-edge.a.y-u*dy)<=2) return true;
            }
            return false;
        };
        const double ux=(bx-ax)/length,uy=(by-ay)/length;
        if(connected(a) && connected(b)) {
            const double nx=-uy*thickness*.5,ny=ux*thickness*.5;
            const gcm::Lattice2 corners[]={gcm::to_lattice(vol_,ax+nx,ay+ny),gcm::to_lattice(vol_,ax-nx,ay-ny),
                gcm::to_lattice(vol_,bx-nx,by-ny),gcm::to_lattice(vol_,bx+nx,by+ny)};
            std::vector<gsnap::Shape> boundary;
            for(int i=0;i<4;++i) {gsnap::Shape shape;shape.a=corners[i];shape.b=corners[(i+1)%4];boundary.push_back(shape);}
            auto proposed=gregion::from_shapes(boundary);
            if(proposed.ok) {
                auto combined=gregion::boolean_op(old.region,proposed.region,gregion::Operation::Union);
                if(combined.ok && std::abs(combined.region.area(vol_)-old.region.area(vol_))<=4*vol_.inv_scale()*vol_.inv_scale()) {
                    message_="Wall already exists";return true;
                }
            }
        }
        if(connected(a)) {ax-=ux*thickness*.5;ay-=uy*thickness*.5;}
        if(connected(b)) {bx+=ux*thickness*.5;by+=uy*thickness*.5;}
    }
    const double joined_length=std::hypot(bx-ax,by-ay);
    const double nx=-(by-ay)/joined_length*thickness*.5,ny=(bx-ax)/joined_length*thickness*.5;
    const gcm::Lattice2 corners[] = {gcm::to_lattice(vol_,ax+nx,ay+ny),
        gcm::to_lattice(vol_,ax-nx,ay-ny),gcm::to_lattice(vol_,bx-nx,by-ny),
        gcm::to_lattice(vol_,bx+nx,by+ny)};
    std::vector<gsnap::Shape> rectangle;
    for (int i=0;i<4;++i) {
        gsnap::Shape shape;shape.a=corners[i];shape.b=corners[(i+1)%4];
        shape.layer="A-WALL";shape.attrs.user["sketch_object"]="wall";
        rectangle.push_back(shape);
    }
    auto result=gregion::from_shapes(rectangle);
    if(!result.ok) return false;
    if(!existing.empty()) {
        result=gregion::boolean_op(old.region,result.region,gregion::Operation::Union);
        if(!result.ok) {message_=result.error;return false;}
    }
    mark();
    adopt_wall_sections();
    add_wall_section(rectangle);
    sync_wall_sections();
    clear_selection();
    align_dirty_=true;refresh_hatches();
    message_="Wall added; intersecting wall faces joined";
    return true;
}
}

namespace gcapp {
void Canvas::group_object(std::size_t first,const std::string& type) {
    if(first>=shapes_.size()) return;
    const auto poly=next_polyline_id();
    for(std::size_t i=first;i<shapes_.size();++i) {
        shapes_[i].poly=poly; shapes_[i].poly_index=gcm::u32(i-first);
        shapes_[i].attrs.user["sketch_object"]=type;
    }
    ++edit_serial_;
}
std::size_t Canvas::detect_wall_regions(bool infer_generic) {
    sync_wall_sections();
    const auto wall=[](const gsnap::Shape& s) {
        const auto found=s.attrs.user.find("sketch_object");
        return found!=s.attrs.user.end() && found->second=="wall";
    };
    const auto ends=[](const gsnap::Shape& s) {return gregion::endpoints({s,false});};
    std::map<gcm::u32,std::vector<const gsnap::Shape*>> groups;
    for(const auto& s:shapes_)if(wall(s) && s.poly)groups[s.poly].push_back(&s);
    std::set<gcm::u32> coherent;
    for(auto& [poly,members]:groups) {
        std::sort(members.begin(),members.end(),[](auto a,auto b){return a->poly_index<b->poly_index;});
        bool valid=true;gcm::u32 loop=0;gcm::Lattice2 start{},previous{};
        for(std::size_t n=0;n<members.size();++n) {
            const auto& s=*members[n];
            if(!s.region || s.poly_index!=n || s.region_loop>loop+1 || (!n && s.region_loop!=0)) {valid=false;break;}
            const auto [from,to]=gregion::endpoints({s,s.region_reversed});
            if(!n)start=from;
            else if(s.region_loop!=loop) {
                if(s.region_loop!=loop+1 || distance2(previous,start)>4) {valid=false;break;}
                start=from;
            } else if(distance2(previous,from)>4) {valid=false;break;}
            loop=s.region_loop;previous=to;
        }
        if(valid && distance2(previous,start)<=4)coherent.insert(poly);
    }
    std::map<Key,std::vector<std::size_t>> vertices;
    for(std::size_t i=0;i<shapes_.size();++i) {
        const auto& s=shapes_[i];const bool typed=wall(s);
        if(s.attrs.user.contains("wall_ungrouped") || (s.kind!=gsnap::ShapeKind::Segment && !(typed && s.kind==gsnap::ShapeKind::Arc)) ||
           (s.region && !typed) ||
           (s.attrs.user.count("sketch_object") && !typed) ||
           (!typed && s.layer!="A-WALL" && s.layer!="0" && !s.layer.empty()))continue;
        const auto [from,to]=ends(s);
        vertices[key(from)].push_back(i);vertices[key(to)].push_back(i);
    }
    struct OutlineCandidate {std::vector<std::size_t> members;gregion::Region region;double area,perimeter;};
    std::vector<OutlineCandidate> broad_outlines;
    std::set<std::size_t> seen;
    std::size_t count=0;bool cleared=false;
    for(const auto& [v,adj]:vertices) for(auto root:adj) {
        if(seen.count(root)) continue;
        std::vector<std::size_t> component{root}; seen.insert(root);
        bool closed=true;
        for(std::size_t n=0;n<component.size();++n) {
            const auto& s=shapes_[component[n]];
            const auto [from,to]=ends(s);
            for(auto p:{from,to}) {
                const auto& links=vertices[key(p)]; closed &= links.size()==2;
                for(auto j:links) if(seen.insert(j).second) component.push_back(j);
            }
        }
        if(!closed || component.size()<3) {
            for(auto i:component) {
                auto& s=shapes_[i];
                if(wall(s) && !coherent.count(s.poly) && s.region) {
                    s.region=false;s.region_loop=0;s.region_reversed=false;cleared=true;
                }
            }
            continue;
        }
        const bool typed=std::any_of(component.begin(),component.end(),[&](auto i){return wall(shapes_[i]);});
        if(typed && std::all_of(component.begin(),component.end(),[&](auto i){const auto& s=shapes_[i];return wall(s) && coherent.count(s.poly);}))continue;
        if(!typed && component.size()<4)continue;
        std::vector<gsnap::Shape> boundary;
        gcm::i64 minx=shapes_[root].a.x,maxx=minx,miny=shapes_[root].a.y,maxy=miny;
        bool wall_layer=true;
        for(auto i:component) {
            boundary.push_back(shapes_[i]); wall_layer &= shapes_[i].layer=="A-WALL";
            for(auto p:{shapes_[i].a,shapes_[i].b}) {
                minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);
            }
        }
        if(!typed && !wall_layer && !infer_generic)continue;
        auto region=gregion::from_shapes(boundary);
        if(!region.ok)continue;
        if(!typed) {
            double perimeter=0;
            for(const auto& edge:boundary)perimeter+=std::hypot(double(edge.b.x)-edge.a.x,double(edge.b.y)-edge.a.y)*vol_.inv_scale();
            const double area=std::abs(region.region.area(vol_));
            const double discriminant=perimeter*perimeter-16*area;
            if(perimeter<=0)continue;
            const double width=discriminant>=0?4*area/(perimeter+std::sqrt(discriminant)):perimeter;
            const double length=perimeter*.5-width;
            if(width<.05 || width>.5 || length<width*5) {
                broad_outlines.push_back({component,std::move(region.region),area,perimeter});
                continue;
            }
        }
        const auto poly=next_polyline_id(); gcm::u32 order=0;
        for(gcm::u32 l=0;l<region.region.loops.size();++l) for(const auto& edge:region.region.loops[l]) {
            auto i=std::find_if(component.begin(),component.end(),[&](auto j){return shapes_[j].key==edge.shape.key;});
            if(i==component.end()) continue;
            auto& s=shapes_[*i]; s.poly=poly;s.poly_index=order++;s.region=true;s.region_loop=l;s.region_reversed=edge.reversed;
            s.layer="A-WALL";s.attrs.user["sketch_object"]="wall";
        }
        ++count;
    }
    std::set<std::size_t> paired;
    for(std::size_t outer=0;outer<broad_outlines.size();++outer) {
        if(paired.count(outer))continue;
        std::size_t best=broad_outlines.size();double best_width=1e300;
        const auto& a=broad_outlines[outer];
        for(std::size_t inner=0;inner<broad_outlines.size();++inner) {
            const auto& b=broad_outlines[inner];
            if(inner==outer || paired.count(inner) || b.area>=a.area)continue;
            const double width=2*(a.area-b.area)/(a.perimeter+b.perimeter);
            if(width<.05 || width>.5 || (a.perimeter+b.perimeter)*.25<5*width || width>=best_width)continue;
            if(!std::all_of(b.members.begin(),b.members.end(),[&](auto j){const auto [from,to]=ends(shapes_[j]);return a.region.contains(from)&&a.region.contains(to);}))continue;
            best=inner;best_width=width;
        }
        if(best==broad_outlines.size())continue;
        auto members=a.members;members.insert(members.end(),broad_outlines[best].members.begin(),broad_outlines[best].members.end());
        std::vector<gsnap::Shape> boundary;for(auto i:members)boundary.push_back(shapes_[i]);
        const auto region=gregion::from_shapes(boundary);if(!region.ok)continue;
        const auto poly=next_polyline_id();gcm::u32 order=0;
        for(gcm::u32 loop=0;loop<region.region.loops.size();++loop)for(const auto& edge:region.region.loops[loop]) {
            const auto found=std::find_if(members.begin(),members.end(),[&](auto i){return shapes_[i].key==edge.shape.key;});
            if(found==members.end())continue;
            auto& shape=shapes_[*found];shape.poly=poly;shape.poly_index=order++;shape.region=true;
            shape.region_loop=loop;shape.region_reversed=edge.reversed;shape.layer="A-WALL";shape.attrs.user["sketch_object"]="wall";
        }
        paired.insert(outer);paired.insert(best);count+=2;
    }
    if(count) {
        std::vector<gsnap::Shape> walls;
        for(const auto& shape:shapes_) if(shape.attrs.user.count("sketch_object") &&
            shape.attrs.user.at("sketch_object")=="wall") walls.push_back(shape);
        auto joined=joined_walls(walls);
        if(joined.ok) {
            std::vector<gcm::u32> selected;
            for(auto index:selection_)if(index<shapes_.size())selected.push_back(shapes_[index].key);
            shapes_.erase(std::remove_if(shapes_.begin(),shapes_.end(),[](const auto& shape) {
                return shape.attrs.user.count("sketch_object") && shape.attrs.user.at("sketch_object")=="wall";
            }),shapes_.end());
            const auto poly=next_polyline_id();gcm::u32 order=0;
            for(gcm::u32 l=0;l<joined.region.loops.size();++l) for(const auto& edge:joined.region.loops[l]) {
                auto shape=edge.shape;shape.key=0;
                for(const auto& prior:walls) if(same(prior.a,shape.a)&&same(prior.b,shape.b)) {shape.key=prior.key;break;}
                shape.poly=poly;shape.poly_index=order++;shape.region=true;
                shape.region_loop=l;shape.region_reversed=edge.reversed;
                shape.attrs.user["sketch_object"]="wall";shape.layer="A-WALL";shapes_.push_back(shape);
            }
            assign_keys();selection_.clear();
            for(auto key:selected)if(const auto i=index_of_key(key);i!=npos)selection_.push_back(i);
        }
        ++edit_serial_; rebuild_grips();refresh_hatches();
    }
    if(cleared && !count) {++edit_serial_;rebuild_grips();refresh_hatches();}
    return count;
}
}
