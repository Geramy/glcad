#include "engine/units.hpp"
#include "ui/modeller.hpp"

#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/revolve.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/loft.hpp"
#include "geometry/mate.hpp"
#include "geometry/measure.hpp"

namespace gcapp {

using gview::Point3;
using gview::Vec3;
using gcm::F64;

namespace {

struct V3 { double x, y, z; };
V3 v3(const Point3& p) { return {p.x.to_f64(), p.y.to_f64(), p.z.to_f64()}; }
V3 v3(const Vec3& p)   { return {p.x.to_f64(), p.y.to_f64(), p.z.to_f64()}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double len(V3 a) { return std::sqrt(dot(a, a)); }
V3 unit(V3 a) { const double l = len(a); return l > 1e-300 ? a * (1.0 / l) : V3{0, 0, 1}; }
Point3 P(V3 a) { return Point3::from(a.x, a.y, a.z); }
Vec3   N(V3 a) { return Vec3::from(a.x, a.y, a.z); }

ggeo::d3::Frame<F64> frame_on(V3 origin, V3 n) {
    const V3 axis = std::fabs(n.z) < 0.9 ? V3{0, 0, 1}
                  : std::fabs(n.x) < 0.9 ? V3{1, 0, 0} : V3{0, 1, 0};
    const V3 u = unit(cross(axis, n));
    const V3 v = cross(n, u);
    ggeo::d3::Frame<F64> f;
    f.origin = P(origin);
    f.u = N(u); f.v = N(v); f.n = N(n);
    return f;
}

void to_plane(const ggeo::d3::Frame<F64>& f, V3 p, double& x, double& y) {
    const V3 d = p - v3(f.origin);
    x = dot(d, v3(f.u));
    y = dot(d, v3(f.v));
}

double ray_triangle(V3 o, V3 d, V3 a, V3 b, V3 c) {
    const V3 e1 = b - a, e2 = c - a;
    const V3 p = cross(d, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-14) return -1.0;
    const double inv = 1.0 / det;
    const V3 tv = o - a;
    const double u = dot(tv, p) * inv;
    if (u < -1e-9 || u > 1.0 + 1e-9) return -1.0;
    const V3 q = cross(tv, e1);
    const double v = dot(d, q) * inv;
    if (v < -1e-9 || u + v > 1.0 + 1e-9) return -1.0;
    return dot(e2, q) * inv;
}

}

Modeller::Modeller(const gcm::WorkingVolume& vol) : vol_(vol) {}

void Modeller::arm(Op op) {
    clear_preview();
    op_ = op;
    stage_ = op == Op::None ? Stage::Idle : Stage::PickTarget;
    hover_ = Target{};
    target_ = Target{};
    distance_ = 0.0;
    typed_.clear();
    axis_valid_ = false;
    edge_valid_ = false;
    selected_edges_.clear();
    edge_applied_ = false;
    path_valid_ = false;
    measure_first_ = false;
    if (op != Op::MeasureDistance) measure_valid_ = false;
    message_ = prompt();
}

void Modeller::cancel() {
    clear_preview();
    if (stage_ == Stage::Idle) return;
    if (stage_ == Stage::PickTarget) { arm(Op::None); return; }
    target_ = Target{};
    distance_ = 0.0;
    typed_.clear();
    axis_valid_ = false;
    edge_valid_ = false;
    stage_ = Stage::PickTarget;
    mesh_dirty_ = true;
    message_ = prompt();
}

const char* Modeller::prompt() const noexcept {
    switch (stage_) {
        case Stage::Idle:       return "";
        case Stage::PickTarget:
            switch (op_) {
                case Op::Extrude:   return "select a face or a closed sketch to extrude";
                case Op::Revolve:   return "select a closed sketch to revolve";
                case Op::Union:     return "select the first solid to join";
                case Op::Subtract:  return "select the solid to cut";
                case Op::Intersect: return "select the first solid to intersect";
                case Op::FilletEdge:  return "select an edge to fillet";
                case Op::ChamferEdge: return "select an edge to chamfer";
                case Op::Loft:        return "select the first profile to loft";
                case Op::Mate:        return "select a face of the solid that stays";
                case Op::Sweep:       return "select the profile to sweep";
                case Op::ShellBody:   return "select the face to open, then type the wall thickness";
                case Op::Split:       return "select the solid to split";
                case Op::Section:     return "select the solid to section";
                case Op::MeasureDistance: return "pick the first point";
                case Op::PatternBody: return "select a face: the pattern runs along its normal";
                case Op::MirrorBody:  return "select the solid to mirror";
                default:            return "";
            }
        case Stage::Placed:     return "drag the arrow, or type a distance";
        case Stage::Dragging:   return "release to extrude";
        case Stage::PickAxis:   return "pick a line of the sketch as the axis";
        case Stage::PickSecond:
            return op_ == Op::Subtract ? "select the solid to cut with"
                 : op_ == Op::Loft ? "select the second profile"
                 : op_ == Op::Mate ? "select the face that will move to meet it"
                 : op_ == Op::Sweep ? "select a sketch line as the path"
                 : op_ == Op::Split ? "select a face or a sketch as the cutting plane"
                 : op_ == Op::Section ? "select a face or a sketch as the cutting plane"
                 : op_ == Op::MeasureDistance ? "pick the second point"
                 : op_ == Op::MirrorBody ? "select a face or a sketch as the mirror plane"
                                         : "select the second solid";
        case Stage::TypeValue:
            return op_ == Op::FilletEdge ? "type the radius, then Enter"
                 : op_ == Op::ChamferEdge ? "type the setback, then Enter"
                 : op_ == Op::ShellBody ? "type the wall thickness, then Enter"
                 : op_ == Op::PatternBody ? "type count and spacing, e.g. 4 2.5, then Enter"
                                          : "type a value, then Enter";
    }
    return "";
}

const gtopo::TriMesh& Modeller::tri(std::size_t body) const {
    if (tri_cache_.size() != bodies_.size()) {
        tri_cache_.clear();
        tri_cache_.resize(bodies_.size());
    }
    if (tri_cache_[body].indices.empty() && bodies_[body].face_count() > 0)
        tri_cache_[body] = gtopo::triangulate(bodies_[body], vol_);
    return tri_cache_[body];
}

const Modeller::SelectionTopology& Modeller::selection_topology(std::size_t body) const {
    if (selection_cache_.size()!=bodies_.size()) selection_cache_.resize(bodies_.size());
    auto& cache=selection_cache_[body];
    if (!cache.group_of_face.empty()) return cache;
    const auto& b=bodies_[body];
    cache.group_of_face.assign(b.face_count(),gtopo::kInvalid);
    for (gcm::u32 f=0;f<b.face_count();++f) {
        if (!b.face_is_curved(f) || cache.group_of_face[f]!=gtopo::kInvalid) continue;
        const auto group=gcm::u32(cache.groups.size());
        std::vector<gcm::u32> faces{f}; cache.group_of_face[f]=group;
        for (std::size_t i=0;i<faces.size();++i)
            for (auto l=b.face_loop_begin(faces[i]);l<b.face_loop_end(faces[i]);++l)
                for (auto h=b.loop_begin(l);h<b.loop_end(l);++h) {
                    const auto twin=b.he_twin[h]; if (twin==gtopo::kInvalid) continue;
                    const auto next=b.loop_face[b.he_loop[twin]];
                    if (cache.group_of_face[next]==gtopo::kInvalid && b.face_is_curved(next) &&
                        b.face_surface[f].same_as(b.face_surface[next],1e-12)) {
                        cache.group_of_face[next]=group; faces.push_back(next);
                    }
                }
        std::sort(faces.begin(),faces.end()); cache.groups.push_back(std::move(faces));
    }
    for (gcm::u32 l=0;l<b.loop_count();++l) {
        if (b.loop_size(l)<12 || b.face_is_curved(b.loop_face[l])) continue;
        const auto h=b.loop_begin(l),twin=b.he_twin[h];
        if (twin==gtopo::kInvalid || cache.group_of_face[b.loop_face[b.he_loop[twin]]]!=gtopo::kInvalid) continue;
        auto faces=gtopo::circular_wall_faces(b,h,vol_);
        if (faces.empty() || std::any_of(faces.begin(),faces.end(),[&](auto f){return cache.group_of_face[f]!=gtopo::kInvalid;})) continue;
        const auto group=gcm::u32(cache.groups.size());
        for (auto f:faces) cache.group_of_face[f]=group;
        cache.groups.push_back(std::move(faces));
    }
    return cache;
}

bool Modeller::pick_face(const gview::Camera& cam, double px, double py,
                         std::size_t& body, gcm::u32& face, double& best) const {
    const auto ray = cam.ray_through(px, py);
    const V3 o = v3(ray.origin), d = v3(ray.direction);
    best = 1e300;
    bool hit = false;
    for (std::size_t bi = 0; bi < bodies_.size(); ++bi) {
        if (body_hidden(bi)) continue;
        const gtopo::TriMesh& m = const_cast<Modeller*>(this)->tri(bi);
        const gtopo::Body& b = bodies_[bi];
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            V3 p[3];
            for (int k = 0; k < 3; ++k) {
                double x, y, z;
                gcm::to_world(vol_, b.vertex_point[m.indices[i + k]], x, y, z);
                p[k] = {x, y, z};
            }
            const double t = ray_triangle(o, d, p[0], p[1], p[2]);
            if (t > 1e-9 && t < best) {
                best = t;
                body = bi;
                face = m.face_of[i / 3];
                hit = true;
            }
        }
    }
    return hit;
}

bool Modeller::face_is_planar(std::size_t body, gcm::u32 face) const {
    return body < bodies_.size() && face < bodies_[body].face_count() &&
           selection_topology(body).group_of_face[face] == gtopo::kInvalid;
}

bool Modeller::target_from_face(std::size_t bi, gcm::u32 fi, Target& out) const {
    const gtopo::Body& b = bodies_[bi];
    if (fi >= b.face_count()) return false;
    const auto& topology=selection_topology(bi);
    const auto group=topology.group_of_face[fi];
    if (group!=gtopo::kInvalid) {
        out=Target{};out.valid=true;out.on_body=true;out.body=bi;out.face=fi;
        out.surface_faces=topology.groups[group];
        const auto plane=gtopo::plane_of_face(b,fi,vol_);
        out.centroid=Point3::from(plane.ox,plane.oy,plane.oz);
        out.normal=Vec3::from(plane.nx,plane.ny,plane.nz);
        for (auto f:out.surface_faces) for(auto l=b.face_loop_begin(f);l<b.face_loop_end(f);++l)
            for(auto h=b.loop_begin(l);h<b.loop_end(l);++h) {
                const auto twin=b.he_twin[h];
                if(twin!=gtopo::kInvalid && topology.group_of_face[b.loop_face[b.he_loop[twin]]]==group) continue;
                std::vector<Point3> edge;
                for(auto e:{h,b.he_next[h]}) {double x,y,z;gcm::to_world(vol_,b.vertex_point[b.he_vertex[e]],x,y,z);edge.push_back(Point3::from(x,y,z));}
                out.outline.push_back(std::move(edge));
            }
        return true;
    }

    out = Target{};
    std::vector<std::vector<V3>> loops;
    for (gcm::u32 l = b.face_loop_begin(fi); l < b.face_loop_end(fi); ++l) {
        std::vector<V3> loop;
        for (gcm::u32 h = b.loop_begin(l); h < b.loop_end(l); ++h) {
            double x, y, z;
            gcm::to_world(vol_, b.vertex_point[b.he_vertex[h]], x, y, z);
            loop.push_back({x, y, z});
        }
        if (loop.size() >= 3) loops.push_back(std::move(loop));
    }
    if (loops.empty()) return false;

    const auto support=gtopo::plane_of_face(b,fi,vol_);
    const V3 n{support.nx,support.ny,support.nz};
    const V3 c{support.ox,support.oy,support.oz};

    out.valid = true;
    out.on_body = true;
    out.body = bi;
    out.face = fi;
    out.frame = frame_on(loops[0][0], n);
    out.centroid = P(c);
    out.normal = N(n);
    for (std::size_t i = 0; i < loops.size(); ++i) {
        gtopo::Loop2 l2;
        std::vector<Point3> l3;
        for (const V3& p : loops[i]) {
            double x, y;
            to_plane(out.frame, p, x, y);
            l2.push_back(gcm::to_lattice(vol_, x, y));
            l3.push_back(P(p));
        }
        if (i == 0) out.profile.outer = std::move(l2);
        else out.profile.holes.push_back(std::move(l2));
        out.outline.push_back(std::move(l3));
    }
    out.profile.normalize();
    return out.profile.valid();
}

bool Modeller::face_under(const Point3& p, const gtopo::SketchPlane& plane,
                          std::size_t& body, gcm::u32& face) const {
    const V3 q = v3(p);
    for (std::size_t bi = 0; bi < bodies_.size(); ++bi) {
        if (body_hidden(bi)) continue;
        const gtopo::Body& b = bodies_[bi];
        for (gcm::u32 fi = 0; fi < b.face_count(); ++fi) {
            if (b.face_is_curved(fi)) continue;
            const gcm::u32 l = b.face_loop_begin(fi);
            gdoc::Loop loop;
            bool flat = true;
            for (gcm::u32 h = b.loop_begin(l); h < b.loop_end(l); ++h) {
                double x, y, z;
                gcm::to_world(vol_, b.vertex_point[b.he_vertex[h]], x, y, z);
                if (std::fabs(plane.distance(x, y, z)) > 1e-6) { flat = false; break; }
                double u, v;
                plane.to_plane(x, y, z, u, v);
                loop.push_back(gcm::to_lattice(vol_, u, v));
            }
            if (!flat || loop.size() < 3) continue;
            double qu, qv;
            plane.to_plane(q.x, q.y, q.z, qu, qv);
            if (gdoc::point_in_loop(gcm::to_lattice(vol_, qu, qv), loop)) {
                body = bi;
                face = fi;
                return true;
            }
        }
    }
    return false;
}

const gdoc::BoundaryResult& Modeller::loops_of(int si) const {
    static const gdoc::BoundaryResult none;
    if (si < 0 || si >= static_cast<int>(sources_.size())) return none;
    if (loop_cache_.size() != sources_.size()) loop_cache_.resize(sources_.size());
    const SketchSource& src = sources_[si];
    LoopCache& c = loop_cache_[std::size_t(si)];
    if (!src.segments) return none;
    if (c.filled && c.segments == static_cast<const void*>(src.segments) &&
        c.version == src.version && c.count == src.segments->size())
        return c.loops;
    c.loops = gdoc::extract_loops(*src.segments);
    c.segments = static_cast<const void*>(src.segments);
    c.version = src.version;
    c.count = src.segments->size();
    c.filled = true;
    return c.loops;
}

bool Modeller::target_from_region(int si, double u, double v, Target& out) const {
    if (si < 0 || si >= static_cast<int>(sources_.size())) return false;
    const SketchSource& src = sources_[si];
    if (!src.segments || src.segments->empty()) return false;
    const gdoc::BoundaryResult r =
        gdoc::region_containing(loops_of(si), gcm::to_lattice(vol_, u, v));
    return target_from_boundary(si, r, out);
}

bool Modeller::target_from_boundary(int si, const gdoc::BoundaryResult& r, Target& out) const {
    const SketchSource& src = sources_[si];
    if (!r.ok() || r.loops.empty() || r.loops[0].size() < 3) return false;

    out = Target{};
    out.source = si;
    const auto world_of = [&](const gcm::Lattice2& p) {
        double x, y, wx, wy, wz;
        gcm::to_world(vol_, p, x, y);
        src.plane.to_world(x, y, wx, wy, wz);
        return V3{wx, wy, wz};
    };
    V3 c{0, 0, 0};
    for (const auto& p : r.loops[0]) c = c + world_of(p);
    c = c * (1.0 / double(r.loops[0].size()));

    V3 n{src.plane.nx, src.plane.ny, src.plane.nz};
    std::size_t bi; gcm::u32 fi;
    if (face_under(P(c), src.plane, bi, fi)) {
        Target face;
        if (target_from_face(bi, fi, face) && face.surface_faces.empty()) {
            n = v3(face.normal);
            c = c - n * dot(c - v3(face.frame.origin), n);
            out.on_body = true;
            out.body = bi;
            out.face = fi;
        }
    }
    out.frame = frame_on(c, n);
    out.centroid = P(c);
    out.normal = N(n);

    for (std::size_t i = 0; i < r.loops.size(); ++i) {
        gtopo::Loop2 l2;
        std::vector<Point3> l3;
        for (const auto& p : r.loops[i]) {
            const V3 w = world_of(p);
            double fu, fv;
            to_plane(out.frame, w, fu, fv);
            l2.push_back(gcm::to_lattice(vol_, fu, fv));
            l3.push_back(P(w));
        }
        if (i == 0) out.profile.outer = std::move(l2);
        else out.profile.holes.push_back(std::move(l2));
        out.outline.push_back(std::move(l3));
    }
    out.profile.normalize();
    // normalize() may reorder loops; the outline must stay index-aligned.
    out.outline.clear();
    const auto lift = [&](const gtopo::Loop2& loop) {
        std::vector<Point3> l3;
        for (const auto& q : loop) {
            double fu, fv;
            gcm::to_world(vol_, q, fu, fv);
            const V3 w = v3(out.frame.origin) + v3(out.frame.u) * fu + v3(out.frame.v) * fv;
            l3.push_back(P(w));
        }
        return l3;
    };
    out.outline.push_back(lift(out.profile.outer));
    for (const auto& h : out.profile.holes) out.outline.push_back(lift(h));
    out.valid = out.profile.valid();
    return out.valid;
}

bool Modeller::sketch_consumed(int source) const {
    if (source < 0 || std::size_t(source) >= sources_.size()) return false;
    const auto& src = sources_[source];
    if (!src.segments) return false;
    if (consumed_cache_.size() != sources_.size()) consumed_cache_.resize(sources_.size());
    auto& cached = consumed_cache_[source];
    const auto& p = src.plane;
    const std::array<double,12> plane{p.ox,p.oy,p.oz,p.ux,p.uy,p.uz,p.vx,p.vy,p.vz,p.nx,p.ny,p.nz};
    if (cached.filled && cached.segments == src.segments && cached.version == src.version &&
        cached.count == src.segments->size() && cached.plane == plane) return cached.consumed;
    const bool consumed = sketch_consumed_uncached(source);
    cached = {src.segments, src.version, src.segments->size(), plane, true, consumed};
    return consumed;
}

bool Modeller::sketch_consumed_uncached(int source) const {
    bool used=false;
    for (std::size_t b=0; b<bodies_.size(); ++b) {
        const auto& features=history(b);
        for (std::size_t f=0; f<std::min(features.size(),rolled_back_to(b)); ++f)
            used |= !features[f].suppressed && features[f].sketch==source;
    }
    if (!used) return false;
    const auto& loops=loops_of(source);
    if (!loops.ok()) return false;
    std::vector<gtopo::Loop2> consumed;
    for (std::size_t b=0; b<bodies_.size(); ++b) {
        const auto& features=history(b);
        for (std::size_t f=0; f<std::min(features.size(),rolled_back_to(b)); ++f) {
            const auto& feature=features[f];
            if (feature.suppressed || feature.sketch!=source || !feature.has_seed) continue;
            const auto region=gdoc::region_containing(loops,gcm::to_lattice(vol_,feature.seed_u,feature.seed_v));
            consumed.insert(consumed.end(),region.loops.begin(),region.loops.end());
        }
    }
    for (const auto& loop:loops.loops) {
        if (loop.size()<3) continue;
        bool found=false;
        for (const auto& used:consumed) {
            if (loop.size()!=used.size()) continue;
            bool same=true;
            for (std::size_t i=0; i<loop.size(); ++i)
                same &= loop[i].x==used[i].x && loop[i].y==used[i].y;
            found |= same;
        }
        if (!found) {
            gdoc::BoundaryResult region; region.loops.push_back(loop);
            Target target;
            if (target_from_boundary(source,region,target) && !region_used(target)) return false;
        }
    }
    return true;
}

bool Modeller::region_used(const Target& target) const {
    if (target.source < 0) return false;
    const auto world = [&](const gtopo::Loop2& loop, const ggeo::d3::Frame<F64>& frame) {
        std::vector<V3> result;
        for (const auto& p : loop) {
            double u, v;
            gcm::to_world(vol_, p, u, v);
            result.push_back(v3(frame.origin) + v3(frame.u)*u + v3(frame.v)*v);
        }
        return result;
    };
    const auto same_loop = [&](const gtopo::Loop2& a, const ggeo::d3::Frame<F64>& af,
                               const gtopo::Loop2& b, const ggeo::d3::Frame<F64>& bf) {
        if (a.size() != b.size() || a.empty()) return false;
        const auto aw = world(a, af), bw = world(b, bf);
        for (std::size_t start = 0; start < b.size(); ++start) {
            for (bool reverse : {false, true}) {
                bool same = true;
                for (std::size_t k = 0; k < a.size(); ++k)
                    if (len(aw[k] - bw[(start + (reverse ? b.size()-k : k))%b.size()]) > 1e-6) {
                        same = false; break;
                    }
                if (same) return true;
            }
        }
        return false;
    };
    for (std::size_t bi = 0; bi < bodies_.size(); ++bi) {
        const auto& features = history(bi);
        const auto end = std::min(features.size(), rolled_back_to(bi));
        for (std::size_t i = 0; i < end; ++i) {
            const auto& feature = features[i];
            if (feature.suppressed || feature.sketch != target.source) continue;
            if (feature.profile.outer.empty()) return true;
            if (feature.profile.holes.size() != target.profile.holes.size() ||
                !same_loop(feature.profile.outer, feature.frame, target.profile.outer, target.frame)) continue;
            bool holes = true;
            for (std::size_t h = 0; h < feature.profile.holes.size(); ++h)
                holes = holes && same_loop(feature.profile.holes[h], feature.frame, target.profile.holes[h], target.frame);
            if (holes) return true;
        }
    }
    return false;
}

bool Modeller::face_region_at(std::size_t body, gcm::u32 face,
                              const Point3& hit, Target& out) const {
    if (!target_from_face(body, face, out) || !out.surface_faces.empty()) return false;
    std::vector<gtopo::Loop2> exclusions;
    for (std::size_t si = 0; si < sources_.size(); ++si) {
        if (!sources_[si].visible) continue;
        const auto& plane = sources_[si].plane;
        if (std::fabs(plane.distance(hit.x.to_f64(), hit.y.to_f64(), hit.z.to_f64())) > 1e-6 ||
            std::fabs(dot(V3{plane.nx, plane.ny, plane.nz}, v3(out.normal))) < 1-1e-9) continue;
        const auto& loops = loops_of(static_cast<int>(si));
        if (!loops.ok()) continue;
        for (std::size_t li = 0; li < loops.loops.size(); ++li) {
            if (loops.loops[li].size() < 3) continue;
            gdoc::BoundaryResult region;
            region.loops.push_back(loops.loops[li]);
            for (std::size_t hi = 0; hi < loops.loops.size(); ++hi)
                if (hi != li && !loops.loops[hi].empty() &&
                    gdoc::point_in_loop(loops.loops[hi][0], loops.loops[li]))
                    region.loops.push_back(loops.loops[hi]);
            Target candidate;
            if (!target_from_boundary(static_cast<int>(si), region, candidate) ||
                !candidate.on_body || candidate.body != body || candidate.face != face ||
                region_used(candidate)) continue;
            gtopo::Loop2 hole;
            bool contained = true;
            for (const auto& point : candidate.outline.front()) {
                double u,v;
                to_plane(out.frame,v3(point),u,v);
                const auto q = gcm::to_lattice(vol_,u,v);
                if (!gdoc::point_in_loop(q,out.profile.outer)) { contained=false; break; }
                for (const auto& existing : out.profile.holes)
                    if (gdoc::point_in_loop(q,existing)) { contained=false; break; }
                hole.push_back(q);
            }
            if (contained) exclusions.push_back(std::move(hole));
        }
    }
    for (std::size_t i=0;i<exclusions.size();++i) {
        bool nested=false;
        for (std::size_t j=0;j<exclusions.size();++j) {
            if (i==j || !gdoc::point_in_loop(exclusions[i][0],exclusions[j])) continue;
            if (!gdoc::point_in_loop(exclusions[j][0],exclusions[i]) || j<i) { nested=true; break; }
        }
        if (!nested) out.profile.holes.push_back(exclusions[i]);
    }
    if (exclusions.empty()) return true;
    out.from_face = true;
    out.profile.normalize();
    out.outline.clear();
    const auto lift = [&](const gtopo::Loop2& loop) {
        std::vector<Point3> points;
        for (const auto& p : loop) {
            double u,v; gcm::to_world(vol_,p,u,v);
            points.push_back(P(v3(out.frame.origin)+v3(out.frame.u)*u+v3(out.frame.v)*v));
        }
        out.outline.push_back(std::move(points));
    };
    lift(out.profile.outer);
    for (const auto& hole : out.profile.holes) lift(hole);
    out.centroid = hit;
    return out.profile.valid();
}

bool Modeller::find_target(const gview::Camera& cam, double px, double py,
                           Target& out, bool prefer_result) const {
    std::size_t body = 0; gcm::u32 face = 0; double t_face = 1e300;
    const bool face_hit = pick_face(cam, px, py, body, face, t_face);

    const auto ray = cam.ray_through(px, py);
    const V3 o = v3(ray.origin), d = unit(v3(ray.direction));

    int best_src = -1;
    double best_t = 1e300, best_u = 0, best_v = 0;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        if (!sources_[i].visible) continue;
        const auto& pl = sources_[i].plane;
        Point3 hit;
        if (!cam.plane_hit(px, py, Point3::from(pl.ox, pl.oy, pl.oz),
                           Vec3::from(pl.nx, pl.ny, pl.nz), hit))
            continue;
        const double t = dot(v3(hit) - o, d);
        double u, v;
        pl.to_plane(hit.x.to_f64(), hit.y.to_f64(), hit.z.to_f64(), u, v);
        Target probe;
        if (!target_from_region(static_cast<int>(i), u, v, probe)) continue;
        if (prefer_result && face_hit && region_used(probe)) continue;
        if (t < best_t) { best_t = t; best_src = static_cast<int>(i); best_u = u; best_v = v; }
    }

    const auto face_target = [&] {
        const bool profile_op = op_ == Op::None || op_ == Op::Extrude;
        const bool found=(profile_op && face_region_at(body,face,P(o+d*t_face),out)) || target_from_face(body,face,out);
        if (found) {out.pick_point=P(o+d*t_face);out.has_pick_point=true;}
        return found;
    };
    if (face_hit && best_src >= 0) {
        if (t_face < best_t - 1e-6) {
            if (face_target()) return true;
        }
        if (target_from_region(best_src, best_u, best_v, out)) return true;
        return face_target();
    }
    if (face_hit) return face_target();
    if (best_src >= 0) return target_from_region(best_src, best_u, best_v, out);
    return false;
}

void Modeller::hover(const gview::Camera& cam, double px, double py) {
    switch (stage_) {
        case Stage::PickTarget:
            if (op_ == Op::FilletEdge || op_ == Op::ChamferEdge) {
                pick_edge(cam, px, py);
                break;
            }
            [[fallthrough]];
        case Stage::Placed: {
            Target t;
            const bool prefer_result=op_==Op::Extrude && stage_==Stage::Placed && target_.on_body && target_.source<0;
            hover_ = find_target(cam, px, py, t, prefer_result) ? t : Target{};
            break;
        }
        case Stage::PickAxis:
            pick_axis(cam, px, py);
            break;
        case Stage::PickSecond: {
            if (op_ == Op::Loft) {
                Target t;
                hover_ = (find_target(cam, px, py, t) && t.source >= 0) ? t : Target{};
                break;
            }
            if (op_ == Op::Sweep) {
                path_valid_ = pick_any_line(cam, px, py, path_source_, path_seg_);
                break;
            }
            if (op_ == Op::Split || op_ == Op::Section || op_ == Op::MirrorBody) {
                Target t;
                hover_ = (find_target(cam, px, py, t) && t.surface_faces.empty()) ? t : Target{};
                break;
            }
            if (op_ == Op::MeasureDistance) break;
            std::size_t body; gcm::u32 face; double t;
            hover_ = Target{};
            if (pick_face(cam, px, py, body, face, t) && body != first_body_) {
                Target ft;
                if (target_from_face(body, face, ft) && ft.surface_faces.empty()) hover_ = ft;
            }
            break;
        }
        default:
            break;
    }
}

bool Modeller::press(const gview::Camera& cam, double px, double py) {
    if (stage_ == Stage::Idle) return false;

    if (stage_ == Stage::PickAxis) {
        if (pick_axis(cam, px, py)) commit_revolve();
        return true;
    }
    if (stage_ == Stage::TypeValue) return true;
    if ((op_ == Op::FilletEdge || op_ == Op::ChamferEdge) && stage_ == Stage::PickTarget) {
        if (pick_edge(cam, px, py)) {
            stage_ = Stage::TypeValue;
            typed_.clear();
            message_ = prompt();
        }
        return true;
    }
    if (stage_ == Stage::PickSecond && op_ == Op::Loft) {
        Target t;
        if (find_target(cam, px, py, t) && t.source >= 0) commit_loft(t);
        return true;
    }
    if (op_ == Op::MeasureDistance) {
        gview::Point3 hp;
        if (!hit_point(cam, px, py, hp)) return true;
        if (!measure_first_) {
            measure_a_ = hp;
            measure_first_ = true;
            measure_valid_ = false;
            stage_ = Stage::PickSecond;
            message_ = prompt();
        } else {
            measure_b_ = hp;
            measure_first_ = false;
            measure_valid_ = true;
            const V3 d = v3(measure_b_) - v3(measure_a_);
            measure_value_ = len(d);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "distance %.4g  (dx %.4g, dy %.4g, dz %.4g)",
                          measure_value_, d.x, d.y, d.z);
            message_ = buf;
            stage_ = Stage::PickTarget;
        }
        return true;
    }
    if (stage_ == Stage::PickSecond && op_ == Op::Sweep) {
        if (pick_any_line(cam, px, py, path_source_, path_seg_)) commit_sweep(path_source_, path_seg_);
        return true;
    }
    if (stage_ == Stage::PickSecond && (op_ == Op::Split || op_ == Op::Section || op_ == Op::MirrorBody)) {
        Target t;
        if (!find_target(cam, px, py, t)) return true;
        if (!t.surface_faces.empty()) { message_ = "Select a planar face or a sketch as the plane"; return true; }
        if (op_ == Op::Split) commit_split(t);
        else if (op_ == Op::Section) commit_section(t);
        else commit_mirror(t);
        return true;
    }
    if (stage_ == Stage::PickSecond && op_ == Op::Mate) {
        std::size_t body; gcm::u32 face; double t;
        if (pick_face(cam, px, py, body, face, t) && body != first_body_) {
            Target ft;
            if (target_from_face(body, face, ft) && ft.surface_faces.empty()) commit_mate(ft);
        }
        return true;
    }
    if (stage_ == Stage::PickSecond) {
        std::size_t body; gcm::u32 face; double t;
        if (pick_face(cam, px, py, body, face, t) && body != first_body_) commit_combine(body);
        return true;
    }

    if (stage_ == Stage::Placed && arrow_hit(cam, px, py)) {
        grab_offset_ = axis_param(cam, px, py) - distance_;
        stage_ = Stage::Dragging;
        message_ = prompt();
        return true;
    }

    Target t;
    if (find_target(cam, px, py, t)) {
        switch (op_) {
            case Op::Revolve:
                if (t.on_body && t.source < 0) { message_ = "revolve needs a sketch region"; return true; }
                target_ = t;
                hover_ = Target{};
                axis_valid_ = false;
                stage_ = Stage::PickAxis;
                message_ = prompt();
                return true;
            case Op::Loft:
                if (t.source < 0) { message_ = "loft needs sketch profiles"; return true; }
                target_ = t;
                hover_ = Target{};
                stage_ = Stage::PickSecond;
                message_ = prompt();
                return true;
            case Op::Sweep:
                if (t.source < 0) { message_ = "sweep needs a sketch profile"; return true; }
                target_ = t;
                hover_ = Target{};
                stage_ = Stage::PickSecond;
                message_ = prompt();
                return true;
            case Op::ShellBody:
            case Op::PatternBody:
                if (!t.on_body || t.source >= 0) { message_ = "select a face of a solid"; return true; }
                if (op_ == Op::ShellBody && !t.surface_faces.empty()) { message_ = "Select a planar face to open the shell"; return true; }
                target_ = t;
                hover_ = Target{};
                typed_.clear();
                stage_ = Stage::TypeValue;
                message_ = prompt();
                return true;
            case Op::Split:
            case Op::Section:
            case Op::MirrorBody:
                if (!t.on_body || t.source >= 0) { message_ = "select a solid"; return true; }
                first_body_ = t.body;
                target_ = t;
                hover_ = Target{};
                stage_ = Stage::PickSecond;
                message_ = prompt();
                return true;
            case Op::Mate:
                if (!t.on_body || t.source >= 0) { message_ = "select a face of a solid"; return true; }
                if (!t.surface_faces.empty()) { message_ = "Select a planar face to mate"; return true; }
                first_body_ = t.body;
                target_ = t;
                hover_ = Target{};
                stage_ = Stage::PickSecond;
                message_ = prompt();
                return true;
            case Op::Union:
            case Op::Subtract:
            case Op::Intersect:
                if (!t.on_body) { message_ = "select a solid"; return true; }
                first_body_ = t.body;
                target_ = t;
                hover_ = Target{};
                stage_ = Stage::PickSecond;
                message_ = prompt();
                return true;
            default:
                if(!t.surface_faces.empty()) {message_="Select a planar face or a sketch for extrusion";return true;}
                target_ = t;
                hover_ = Target{};
                distance_ = 0.0;
                typed_.clear();
                stage_ = Stage::Placed;
                message_ = prompt();
                return true;
        }
    }
    return stage_ == Stage::Placed;
}

bool Modeller::pick_axis(const gview::Camera& cam, double px, double py) {
    axis_valid_ = false;
    if (target_.source < 0 || target_.source >= static_cast<int>(sources_.size())) return false;
    const SketchSource& src = sources_[target_.source];
    if (!src.segments) return false;
    double best = 14.0;
    for (const auto& seg : *src.segments) {
        double ax, ay, bx, by, wx, wy, wz;
        gcm::to_world(vol_, seg.a, ax, ay);
        gcm::to_world(vol_, seg.b, bx, by);
        src.plane.to_world(ax, ay, wx, wy, wz);
        const Point3 A = Point3::from(wx, wy, wz);
        src.plane.to_world(bx, by, wx, wy, wz);
        const Point3 B = Point3::from(wx, wy, wz);
        double sax, say, sbx, sby;
        if (!cam.project(A, sax, say) || !cam.project(B, sbx, sby)) continue;
        const double dx = sbx - sax, dy = sby - say, l2 = dx * dx + dy * dy;
        double u = l2 > 1e-9 ? ((px - sax) * dx + (py - say) * dy) / l2 : 0.0;
        u = std::clamp(u, 0.0, 1.0);
        const double dist = std::hypot(px - (sax + dx * u), py - (say + dy * u));
        if (dist < best) { best = dist; axis_a_ = A; axis_b_ = B; axis_valid_ = true; }
    }
    return axis_valid_;
}

void Modeller::commit_revolve() {
    if (!axis_valid_ || !target_.valid) return;
    double ax, ay, bx, by;
    to_plane(target_.frame, v3(axis_a_), ax, ay);
    to_plane(target_.frame, v3(axis_b_), bx, by);
    const double dx = bx - ax, dy = by - ay, l = std::hypot(dx, dy);
    if (l < 1e-9) { message_ = "the axis has no length"; return; }
    const auto axis_point = ggeo::d2::Point<F64>::from(ax, ay);
    const auto axis_dir = ggeo::d2::Vec<F64>::from(dx / l, dy / l);
    if (gtopo::axis_crosses_profile(target_.profile, vol_, axis_point, axis_dir)) {
        message_ = "the axis crosses the profile: pick a line on its edge or outside it";
        return;
    }
    gtopo::Body made = gtopo::revolve(target_.profile, vol_, target_.frame, axis_point,
                                      axis_dir, 2.0 * 3.14159265358979323846, 48);
    if (made.face_count() == 0) { message_ = "nothing to revolve"; return; }
    snapshot();
    bodies_.push_back(std::move(made));
    note(bodies_.size() - 1, "Revolve", target_.source);
    {
        Feature& f = history_.back().back();
        f.op = Op::Revolve; f.regenerable = true;
        f.profile = target_.profile; f.frame = target_.frame;
        f.axis_x = ax; f.axis_y = ay; f.axis_dx = dx / l; f.axis_dy = dy / l;
        if (target_.source >= 0 && target_.source < static_cast<int>(sources_.size())) {
            double u, v;
            sources_[target_.source].plane.to_plane(target_.centroid.x.to_f64(), target_.centroid.y.to_f64(), target_.centroid.z.to_f64(), u, v);
            f.has_seed = true; f.seed_u = u; f.seed_v = v;
        }
    }
    message_ = "revolved a full turn";
    target_ = Target{};
    axis_valid_ = false;
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

void Modeller::commit_combine(std::size_t second) {
    if (first_body_ >= bodies_.size() || second >= bodies_.size() || second == first_body_) return;
    const gtopo::BoolOp bop = op_ == Op::Union ? gtopo::BoolOp::Union
                            : op_ == Op::Subtract ? gtopo::BoolOp::Subtract
                                                  : gtopo::BoolOp::Intersect;
    const bool curved = gtopo::has_curved_faces(bodies_[first_body_]) ||
                        gtopo::has_curved_faces(bodies_[second]);
    gtopo::Body result;
    std::string kept;
    if (curved) {
        const gtopo::CurvedBoolResult cr =
            gtopo::boolean_curved(bodies_[first_body_], bodies_[second], bop, vol_);
        if (cr.ok()) {
            result = cr.body;
            if (cr.preserved)
                kept = ", " + std::to_string(cr.preserved) + " curved face(s) kept";
        }
    }
    if (result.face_count() == 0) {
        gtopo::BoolResult r = gtopo::boolean_op(bodies_[first_body_], bodies_[second], bop, vol_);
        if (!r.ok()) {
            message_ = op_ == Op::Union ? "could not join the solids"
                     : op_ == Op::Subtract ? "could not cut" : "the solids do not overlap";
            stage_ = Stage::PickTarget;
            target_ = Target{};
            return;
        }
        result = std::move(r.body);
    }
    snapshot();
    bodies_[first_body_] = gtopo::merge_coplanar_faces(result, vol_);
    combine_note_ = kept;
    {
        if (history_.size() < bodies_.size()) history_.resize(bodies_.size());
        const std::string verb = op_ == Op::Union ? "Union with" : op_ == Op::Subtract ? "Subtract" : "Intersect with";
        note(first_body_, verb + " Solid " + std::to_string(second + 1));
        for (const auto& f : history_[second]) history_[first_body_].push_back({"  " + f.label, f.sketch});
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(second));
        if (second < body_layer_.size()) body_layer_.erase(body_layer_.begin() + static_cast<std::ptrdiff_t>(second));
    }
    bodies_.erase(bodies_.begin() + static_cast<std::ptrdiff_t>(second));
    if (second < face_ids_.size()) face_ids_.erase(face_ids_.begin() + static_cast<std::ptrdiff_t>(second));
    if (second < body_key_.size()) body_key_.erase(body_key_.begin() + static_cast<std::ptrdiff_t>(second));
    if (second < body_material_.size()) body_material_.erase(body_material_.begin() + static_cast<std::ptrdiff_t>(second));
    if (second < rollback_.size()) rollback_.erase(rollback_.begin() + static_cast<std::ptrdiff_t>(second));
    message_ = (op_ == Op::Union ? "joined" : op_ == Op::Subtract ? "cut" : "intersected") +
               combine_note_;
    combine_note_.clear();
    target_ = Target{};
    hover_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

bool Modeller::pick_edge(const gview::Camera& cam, double px, double py) {
    edge_valid_ = false;
    double best = edge_pick_px_;
    std::size_t occ_body = 0; gcm::u32 occ_face = 0; double occ_t = 1e300;
    const bool occluded_by = pick_face(cam, px, py, occ_body, occ_face, occ_t);
    const auto ray = cam.ray_through(px, py);
    const V3 ro = v3(ray.origin), rd = unit(v3(ray.direction));
    const auto view_projection = cam.view_projection();
    const auto eye = v3(cam.eye()), forward = v3(cam.forward());
    for (std::size_t bi = 0; bi < bodies_.size(); ++bi) {
        if (body_hidden(bi)) continue;
        const gtopo::Body& b = bodies_[bi];
        const auto& topology=selection_topology(bi);
        for (gcm::u32 h = 0; h < b.halfedge_count(); ++h) {
            if (b.he_twin[h] != gtopo::kInvalid && b.he_twin[h] < h) continue;
            const auto twin=b.he_twin[h];
            if (twin!=gtopo::kInvalid) {
                const auto group=topology.group_of_face[b.loop_face[b.he_loop[h]]];
                if (group!=gtopo::kInvalid && group==topology.group_of_face[b.loop_face[b.he_loop[twin]]]) continue;
            }
            double ax, ay, az, bx, by, bz;
            gcm::to_world(vol_, b.vertex_point[b.he_vertex[h]], ax, ay, az);
            gcm::to_world(vol_, b.vertex_point[b.he_vertex[b.he_next[h]]], bx, by, bz);
            auto clipped_a=Point3::from(ax,ay,az),clipped_b=Point3::from(bx,by,bz);
            if(!cam.clip_segment(clipped_a,clipped_b)) continue;
            double sax, say, sbx, sby;
            if (!cam.project(view_projection,clipped_a.x.to_f64(),clipped_a.y.to_f64(),clipped_a.z.to_f64(),sax,say) ||
                !cam.project(view_projection,clipped_b.x.to_f64(),clipped_b.y.to_f64(),clipped_b.z.to_f64(),sbx,sby)) continue;
            const double dx = sbx - sax, dy = sby - say, l2 = dx * dx + dy * dy;
            double u = l2 > 1e-9 ? ((px - sax) * dx + (py - say) * dy) / l2 : 0.0;
            u = std::clamp(u, 0.0, 1.0);
            const double dist = std::hypot(px - (sax + dx * u), py - (say + dy * u));
            if (dist >= best) continue;
            double world_u=u;
            if(cam.projection()==gview::Projection::Perspective) {
                const double za=dot(v3(clipped_a)-eye,forward),zb=dot(v3(clipped_b)-eye,forward);
                world_u=(u/zb)/((1-u)/za+u/zb);
            }
            const auto picked=P(v3(clipped_a)+(v3(clipped_b)-v3(clipped_a))*world_u);
            if (occluded_by) {
                bool borders_shown = false;
                if (occ_body == bi) {
                    borders_shown = b.loop_face[b.he_loop[h]] == occ_face ||
                        (twin != gtopo::kInvalid && b.loop_face[b.he_loop[twin]] == occ_face);
                }
                if (!borders_shown) {
                    const V3 ep=v3(picked);
                    const double t_edge = dot(ep - ro, rd);
                    if (t_edge > occ_t + 1e-3 * std::max(1.0, occ_t)) continue;
                }
            }
            best = dist;
            edge_valid_ = true;
            edge_body_ = bi;
            edge_he_ = h;
            edge_a_ = Point3::from(ax, ay, az);
            edge_b_ = Point3::from(bx, by, bz);
            edge_pick_point_=picked;
        }
    }
    return edge_valid_;
}

void Modeller::commit_edge(double value, bool replace_applied) {
    if (!edge_valid_ || edge_body_ >= bodies_.size()) return;
    const bool replacing=replace_applied && !undo_.empty() && edge_body_<undo_.back().bodies.size();
    const auto& source=replacing ? undo_.back().bodies[edge_body_] : bodies_[edge_body_];
    const gtopo::EditResult r = op_ == Op::FilletEdge
        ? gtopo::fillet_edge_exact(source, edge_he_, value, 12, vol_)
        : gtopo::chamfer_edge(source, edge_he_, value, value, vol_);
    if (!r.ok) {
        message_ = r.reason.empty() ? "could not blend that edge" : r.reason;
        stage_ = Stage::PickTarget;
        edge_valid_ = false;
        return;
    }
    if (replacing) { const auto operation=op_; undo(); op_=operation; redo_.clear(); }
    snapshot();
    bodies_[edge_body_] = r.body;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %s", op_ == Op::FilletEdge ? "filleted" : "chamfered",
                  gunit::format_length(value, gunit::Format{display_unit_}).c_str());
    message_ = buf;
    std::snprintf(buf, sizeof(buf), "%s %.4g", op_ == Op::FilletEdge ? "Fillet" : "Chamfer", value);
    note(edge_body_, buf);
    {
        Feature& f = history_[edge_body_].back();
        f.op = op_; f.value = value; f.regenerable = true; f.on_body = true;
        f.anchor = P((v3(edge_a_) + v3(edge_b_)) * 0.5);
    }
    edge_valid_ = false;
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

void Modeller::begin_edge() {
    if (!edge_valid_) return;
    selected_edges_ = {{edge_body_, edge_he_, edge_a_, edge_b_, edge_pick_point_}};
    op_ = Op::FilletEdge;
    stage_ = Stage::TypeValue;
    typed_.clear();
    edge_applied_ = false;
    edge_value_ = 0.0;
    message_ = "Round or Bevel the edge; type R, Enter to apply";
    mesh_dirty_ = true;
}

bool Modeller::toggle_edge_at(const gview::Camera& cam, double px, double py) {
    const EdgeSelection previous{edge_body_,edge_he_,edge_a_,edge_b_,edge_pick_point_};
    const bool kept=edge_kept();
    if(kept && selected_edges_.empty()) selected_edges_.push_back(previous);
    if (!pick_edge(cam,px,py)) {
        if (kept) {
            edge_valid_=true; edge_body_=previous.body; edge_he_=previous.halfedge;
            edge_a_=previous.a; edge_b_=previous.b; edge_pick_point_=previous.pick;
        }
        return false;
    }
    const EdgeSelection hit{edge_body_,edge_he_,edge_a_,edge_b_,edge_pick_point_};
    if (edge_applied_) selected_edges_.clear();
    const auto& body=bodies_[hit.body];
    const auto ring=gtopo::circular_edge_ring(body,hit.halfedge,vol_);
    const auto canonical=[&](gcm::u32 h) {
        return body.he_twin[h]==gtopo::kInvalid ? h : std::min(h,body.he_twin[h]);
    };
    const auto same=[&](const EdgeSelection& edge) {
        if (edge.body!=hit.body) return false;
        if (canonical(edge.halfedge)==canonical(hit.halfedge)) return true;
        return std::any_of(ring.begin(),ring.end(),[&](gcm::u32 h) {return canonical(h)==canonical(edge.halfedge);});
    };
    const auto found=std::find_if(selected_edges_.begin(),selected_edges_.end(),same);
    if(found==selected_edges_.end()) selected_edges_.push_back(hit);
    else selected_edges_.erase(found);
    if(selected_edges_.empty()) {finish_edge();return true;}
    const auto& active=selected_edges_.back();
    edge_valid_=true; edge_body_=active.body; edge_he_=active.halfedge;
    edge_a_=active.a; edge_b_=active.b; edge_pick_point_=active.pick;
    op_=Op::FilletEdge; stage_=Stage::TypeValue; edge_applied_=false;
    typed_.clear(); clear_preview();
    message_=std::to_string(selected_edges_.size())+" edges selected; Round or Bevel applies to all";
    mesh_dirty_=true;
    return true;
}


bool Modeller::select_context_edge_at(const gview::Camera& cam,double px,double py,bool& preserved) {
    preserved=false;
    const EdgeSelection previous{edge_body_,edge_he_,edge_a_,edge_b_,edge_pick_point_};
    const bool was_valid=edge_valid_,was_kept=edge_kept();
    const auto restore=[&] {
        edge_valid_=was_valid;edge_body_=previous.body;edge_he_=previous.halfedge;
        edge_a_=previous.a;edge_b_=previous.b;edge_pick_point_=previous.pick;
    };
    if(!pick_edge(cam,px,py)) {restore();return false;}
    const EdgeSelection hit{edge_body_,edge_he_,edge_a_,edge_b_,edge_pick_point_};
    const auto& body=bodies_[hit.body];
    const auto ring=gtopo::circular_edge_ring(body,hit.halfedge,vol_);
    const auto canonical=[&](gcm::u32 h) {
        return body.he_twin[h]==gtopo::kInvalid?h:std::min(h,body.he_twin[h]);
    };
    const auto same=[&](const EdgeSelection& edge) {
        if(edge.body!=hit.body || edge.halfedge>=body.halfedge_count())return false;
        if(canonical(edge.halfedge)==canonical(hit.halfedge))return true;
        return std::any_of(ring.begin(),ring.end(),[&](gcm::u32 h){return canonical(h)==canonical(edge.halfedge);});
    };
    if(std::any_of(selected_edges_.begin(),selected_edges_.end(),same) ||
       (was_kept && selected_edges_.empty() && same(previous))) {
        restore();preserved=true;return true;
    }
    clear_preview();begin_edge();
    return true;
}

double Modeller::edge_length() const {
    if (!edge_valid_) return 0.0;
    const V3 d = v3(edge_b_) - v3(edge_a_);
    return len(d);
}

std::vector<std::pair<gview::Point3,gview::Point3>> Modeller::edge_highlight() const {
    if (!edge_valid_ || edge_body_>=bodies_.size()) return {};
    std::vector<std::pair<Point3,Point3>> result;
    const auto append=[&](const EdgeSelection& edge) {
        const auto& source=edge_applied_ && !undo_.empty() ? undo_.back().bodies : bodies_;
        if(edge.body>=source.size()) return;
        const auto& body=source[edge.body];
        if(edge.halfedge>=body.halfedge_count()) return;
        const auto ring=gtopo::circular_edge_ring(body,edge.halfedge,vol_);
        if(ring.empty()) {result.push_back({edge.a,edge.b});return;}
        const auto point=[&](gcm::u32 h) {
            double x,y,z;gcm::to_world(vol_,body.vertex_point[body.he_vertex[h]],x,y,z);
            return Point3::from(x,y,z);
        };
        for(const auto h:ring) result.push_back({point(h),point(body.he_next[h])});
    };
    if(selected_edges_.empty()) append({edge_body_,edge_he_,edge_a_,edge_b_,edge_pick_point_});
    else for(const auto& edge:selected_edges_) append(edge);
    return result;
}

bool Modeller::edge_midpoint(gview::Point3& out) const {
    if (!edge_valid_) return false;
    out = P((v3(edge_a_) + v3(edge_b_)) * 0.5);
    return true;
}

bool Modeller::edge_drafts(Op op, double value, bool replace_applied,
                           std::vector<BodyDraft>& out, std::string& error) const {
    out.clear();error.clear();
    const auto& source=replace_applied && !undo_.empty() ? undo_.back().bodies : bodies_;
    std::vector<BodyDraft> drafts;
    for(const auto& edge:selected_edges_) {
        if(edge.body>=source.size()) {error="a selected solid no longer exists; no edges changed";return false;}
        auto draft=std::find_if(drafts.begin(),drafts.end(),[&](const BodyDraft& d) {return d.body==edge.body;});
        if(draft==drafts.end()) {
            drafts.push_back({edge.body,source[edge.body]});
            draft=std::prev(drafts.end());
        }
        auto& body=draft->shape;
        const auto mid=(v3(edge.a)+v3(edge.b))*.5;
        gcm::u32 he=gtopo::kInvalid;
        const double tolerance=vol_.inv_scale()*8;
        for(gcm::u32 h=0;h<body.halfedge_count();++h) {
            double ax,ay,az,bx,by,bz;
            gcm::to_world(vol_,body.vertex_point[body.he_vertex[h]],ax,ay,az);
            gcm::to_world(vol_,body.vertex_point[body.he_vertex[body.he_next[h]]],bx,by,bz);
            const V3 a{ax,ay,az},b{bx,by,bz},d=b-a;
            const double l2=dot(d,d),u=l2>0 ? dot(mid-a,d)/l2 : -1;
            if(u>=0 && u<=1 && len(mid-(a+d*u))<=tolerance) {he=h;break;}
        }
        if(he==gtopo::kInvalid) {error="a selected edge no longer exists; no edges changed";return false;}
        auto result=op==Op::FilletEdge ? gtopo::fillet_edge_exact(body,he,value,12,vol_)
                                    : gtopo::chamfer_edge(body,he,value,value,vol_);
        if(!result.ok) {
            error=(result.reason.empty() ? "could not blend a selected edge" : result.reason)+"; no edges changed";
            return false;
        }
        body=std::move(result.body);
    }
    out=std::move(drafts);
    return !out.empty();
}

const gtopo::Body& Modeller::displayed_body(std::size_t body) const {
    if(value_preview_replaces_)
        for(const auto& draft:value_preview_) if(draft.body==body) return draft.shape;
    return bodies_[body];
}

bool Modeller::apply_edge(Op op, double value) {
    if (!edge_valid_ || !std::isfinite(value) || value <= 1e-9) return false;
    const std::size_t body = edge_body_;
    const gcm::u32 he = edge_he_;
    const Point3 a = edge_a_, b = edge_b_;
    const bool replace_applied=edge_applied_;
    const auto selection=selected_edges_;
    edge_valid_ = true;
    edge_body_ = body;
    edge_he_ = he;
    edge_a_ = a;
    edge_b_ = b;
    op_ = op;
    committed_ = false;
    if(selection.size()>1) {
        const bool replacing=replace_applied && !undo_.empty();
        std::vector<BodyDraft> drafts;
        if(edge_drafts(op,value,replacing,drafts,message_)) {
            if(replacing) {undo();redo_.clear();}
            snapshot();
            for(auto& draft:drafts) bodies_[draft.body]=std::move(draft.shape);
            for(const auto& edge:selection) {
                note(edge.body,op==Op::FilletEdge ? "Fillet" : "Chamfer");
                auto& feature=history_[edge.body].back();
                feature.op=op; feature.value=value; feature.regenerable=true; feature.on_body=true;
                feature.anchor=P((v3(edge.a)+v3(edge.b))*0.5);
            }
            committed_=true; invalidate_mesh();
            message_=std::to_string(selection.size())+" edges "+(op==Op::FilletEdge ? "rounded" : "beveled");
        }
    } else commit_edge(value,replace_applied);
    selected_edges_=selection;
    op_=op;
    if (!committed_) {
        edge_valid_ = true;
        edge_body_ = body; edge_he_ = he; edge_a_ = a; edge_b_ = b;
        stage_ = Stage::TypeValue;
        return false;
    }
    edge_valid_ = true;
    edge_body_ = body; edge_he_ = he; edge_a_ = a; edge_b_ = b;
    edge_applied_ = true;
    edge_value_ = value;
    stage_ = Stage::TypeValue;
    mesh_dirty_ = true;
    return true;
}

void Modeller::finish_edge() {
    edge_valid_ = false;
    edge_applied_ = false;
    typed_.clear();
    arm(Op::None);
    mesh_dirty_ = true;
}

void Modeller::commit_loft(const Target& second) {
    if (!target_.valid || !second.valid) return;
    std::vector<gtopo::Section> sections = {{target_.profile, target_.frame},
                                            {second.profile, second.frame}};
    const gtopo::LoftError err = gtopo::check_sections(sections);
    if (err != gtopo::LoftError::None) {
        message_ = gtopo::loft_error_message(err);
        stage_ = Stage::PickTarget;
        target_ = Target{};
        return;
    }
    gtopo::Body made = gtopo::loft(sections, vol_, true);
    if (made.face_count() == 0) { message_ = "nothing to loft"; stage_ = Stage::PickTarget; return; }
    const int loft_sketch = target_.source;
    snapshot();
    bodies_.push_back(std::move(made));
    note(bodies_.size() - 1, "Loft", loft_sketch);
    message_ = "lofted";
    target_ = Target{};
    hover_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

std::size_t Modeller::prune_mates() {
    const std::size_t before = mates_.size();
    std::vector<StoredMate> kept;
    kept.reserve(before);
    for (const StoredMate& m : mates_) {
        const std::size_t a = body_of_name(m.body_a);
        const std::size_t b = body_of_name(m.body_b);
        if (a == static_cast<std::size_t>(-1) || b == static_cast<std::size_t>(-1)) continue;
        if (face_of_name(a, m.face_a) == gtopo::kInvalid) continue;
        if (face_of_name(b, m.face_b) == gtopo::kInvalid) continue;
        kept.push_back(m);
    }
    mates_ = std::move(kept);
    return before - mates_.size();
}

std::size_t Modeller::resolve_mates() {
    if (mates_.empty()) return 0;
    prune_mates();
    if (mates_.empty()) return 0;

    gtopo::Assembly assembly;
    std::vector<gcm::u32> instance_of(bodies_.size(), gtopo::kInvalid);
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (bodies_[i].has_surfaces()) {
            continue;
        }
        gtopo::Instance in;
        in.body = &bodies_[i];
        in.grounded = i == 0;
        in.name = std::to_string(i);
        instance_of[i] = assembly.add(in);
    }

    std::size_t applied = 0;
    for (const StoredMate& m : mates_) {
        if (m.suppressed) continue;
        const std::size_t a = body_of_name(m.body_a);
        const std::size_t b = body_of_name(m.body_b);
        if (a >= instance_of.size() || b >= instance_of.size()) continue;
        if (instance_of[a] == gtopo::kInvalid || instance_of[b] == gtopo::kInvalid) continue;
        gtopo::Mate mate;
        mate.kind = m.kind;
        mate.a = {instance_of[a], gtopo::RefKind::Face, face_of_name(a, m.face_a)};
        mate.b = {instance_of[b], gtopo::RefKind::Face, face_of_name(b, m.face_b)};
        mate.value = m.value;
        assembly.add_mate(mate);
        ++applied;
    }
    if (applied == 0) return 0;

    const gtopo::MateResult r = assembly.solve(vol_);
    if (!r.ok()) {
        message_ = std::string("the assembly did not solve: ") + r.message();
        return 0;
    }
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (instance_of[i] == gtopo::kInvalid) continue;
        const gtopo::Instance* placed = assembly.instance(instance_of[i]);
        if (!placed) continue;
        for (auto& v : bodies_[i].vertex_point) {
            double x, y, z, ox, oy, oz;
            gcm::to_world(vol_, v, x, y, z);
            placed->placement.apply(x, y, z, ox, oy, oz);
            v = gcm::to_lattice(vol_, ox, oy, oz);
        }
    }
    invalidate_mesh();
    message_ = std::to_string(applied) + " mate(s) held -- " +
               std::to_string(assembly.degrees_of_freedom()) +
               " degrees of freedom left";
    return applied;
}

void Modeller::commit_mate(const Target& second) {
    if (!target_.valid || !target_.on_body || !second.on_body) return;
    if (!target_.surface_faces.empty() || !second.surface_faces.empty()) return;
    if (second.body == first_body_) { message_ = "pick a face on a different solid"; return; }
    const gtopo::Body& a = bodies_[first_body_];
    gtopo::Body& b = bodies_[second.body];
    if (b.has_surfaces()) {
        message_ = "cannot yet move a solid with curved faces";
        stage_ = Stage::PickTarget;
        target_ = Target{};
        return;
    }

    gtopo::Assembly assembly;
    gtopo::Instance ia; ia.body = &a; ia.grounded = true; ia.name = "a";
    gtopo::Instance ib; ib.body = &b; ib.name = "b";
    const gcm::u32 ida = assembly.add(ia);
    const gcm::u32 idb = assembly.add(ib);
    gtopo::Mate m;
    m.kind = mate_kind_;
    m.a = {ida, gtopo::RefKind::Face, target_.face};
    m.b = {idb, gtopo::RefKind::Face, second.face};
    m.value = mate_value_;
    assembly.add_mate(m);
    const gtopo::MateResult r = assembly.solve(vol_);
    if (!r.ok()) {
        message_ = std::string("mate failed: ") + r.message();
        stage_ = Stage::PickTarget;
        target_ = Target{};
        return;
    }
    const gtopo::Instance* placed = assembly.instance(idb);
    if (!placed) return;
    snapshot();
    for (auto& v : b.vertex_point) {
        double x, y, z, ox, oy, oz;
        gcm::to_world(vol_, v, x, y, z);
        placed->placement.apply(x, y, z, ox, oy, oz);
        v = gcm::to_lattice(vol_, ox, oy, oz);
    }
    {
        StoredMate m;
        m.kind = mate_kind_;
        m.body_a = body_name(first_body_);
        m.face_a = face_name(first_body_, target_.face);
        m.body_b = body_name(second.body);
        m.face_b = face_name(second.body, second.face);
        m.value = mate_value_;
        if (m.body_a && m.body_b && m.face_a && m.face_b) mates_.push_back(m);
    }
    note(second.body, std::string(gtopo::mate_kind_name(mate_kind_)) + " to Solid " +
                          std::to_string(first_body_ + 1));
    message_ = std::string("mated: ") + gtopo::mate_kind_name(mate_kind_) + " -- " +
               std::to_string(assembly.degrees_of_freedom()) + " degrees of freedom left";
    target_ = Target{};
    hover_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

gtopo::Body Modeller::transformed(const gtopo::Body& b, bool reflect,
                                  const std::function<void(double&, double&, double&)>& map) const {
    gtopo::Builder out;
    for (const auto& v : b.vertex_point) {
        double x, y, z;
        gcm::to_world(vol_, v, x, y, z);
        map(x, y, z);
        out.add_vertex(gcm::to_lattice(vol_, x, y, z));
    }
    for (gcm::u32 f = 0; f < b.face_count(); ++f) {
        out.begin_face();
        for (gcm::u32 l = b.face_loop_begin(f); l < b.face_loop_end(f); ++l) {
            std::vector<gtopo::VertexId> loop;
            for (gcm::u32 h = b.loop_begin(l); h < b.loop_end(l); ++h) loop.push_back(b.he_vertex[h]);
            if (reflect) std::reverse(loop.begin(), loop.end());
            out.add_loop(loop);
        }
        out.end_face();
    }
    return out.build();
}

bool Modeller::plane_of(const Target& t, double& ox, double& oy, double& oz,
                        double& nx, double& ny, double& nz) const {
    if (!t.valid || !t.surface_faces.empty()) return false;
    if (t.source >= 0 && t.source < static_cast<int>(sources_.size()) && !t.on_body) {
        const auto& pl = sources_[t.source].plane;
        ox = pl.ox; oy = pl.oy; oz = pl.oz; nx = pl.nx; ny = pl.ny; nz = pl.nz;
        return true;
    }
    const V3 c = v3(t.centroid), n = v3(t.normal);
    ox = c.x; oy = c.y; oz = c.z; nx = n.x; ny = n.y; nz = n.z;
    return true;
}

bool Modeller::pick_any_line(const gview::Camera& cam, double px, double py, int& source,
                             gdoc::Segment2& seg) const {
    double best = 14.0;
    bool found = false;
    for (std::size_t si = 0; si < sources_.size(); ++si) {
        const SketchSource& src = sources_[si];
        if (!src.visible || !src.segments) continue;
        for (const auto& sg : *src.segments) {
            double ax, ay, bx, by, wx, wy, wz;
            gcm::to_world(vol_, sg.a, ax, ay);
            gcm::to_world(vol_, sg.b, bx, by);
            src.plane.to_world(ax, ay, wx, wy, wz);
            const Point3 A = Point3::from(wx, wy, wz);
            src.plane.to_world(bx, by, wx, wy, wz);
            const Point3 B = Point3::from(wx, wy, wz);
            double sax, say, sbx, sby;
            if (!cam.project(A, sax, say) || !cam.project(B, sbx, sby)) continue;
            const double dx = sbx - sax, dy = sby - say, l2 = dx * dx + dy * dy;
            double u = l2 > 1e-9 ? ((px - sax) * dx + (py - say) * dy) / l2 : 0.0;
            u = std::clamp(u, 0.0, 1.0);
            const double dist = std::hypot(px - (sax + dx * u), py - (say + dy * u));
            if (dist < best) { best = dist; source = static_cast<int>(si); seg = sg; found = true; }
        }
    }
    return found;
}

bool Modeller::hit_point(const gview::Camera& cam, double px, double py, Point3& out) const {
    std::size_t body; gcm::u32 face; double t;
    if (pick_face(cam, px, py, body, face, t)) {
        const auto ray = cam.ray_through(px, py);
        out = P(v3(ray.origin) + v3(ray.direction) * t);
        return true;
    }
    double best = 1e300;
    bool found = false;
    for (const auto& src : sources_) {
        if (!src.visible) continue;
        Point3 hit;
        if (!cam.plane_hit(px, py, Point3::from(src.plane.ox, src.plane.oy, src.plane.oz),
                           Vec3::from(src.plane.nx, src.plane.ny, src.plane.nz), hit)) continue;
        const auto ray = cam.ray_through(px, py);
        const double d = len(v3(hit) - v3(ray.origin));
        if (d < best) { best = d; out = hit; found = true; }
    }
    return found;
}

void Modeller::commit_sweep(int source, const gdoc::Segment2& from) {
    if (!target_.valid || source < 0 || source >= static_cast<int>(sources_.size())) return;
    const SketchSource& src = sources_[source];
    if (!src.segments) return;
    std::vector<gdoc::Segment2> segs = *src.segments;
    std::vector<gcm::Lattice2> chain = {from.a, from.b};
    std::vector<bool> used(segs.size(), false);
    for (std::size_t i = 0; i < segs.size(); ++i)
        if ((segs[i].a.x == from.a.x && segs[i].a.y == from.a.y && segs[i].b.x == from.b.x && segs[i].b.y == from.b.y) ||
            (segs[i].a.x == from.b.x && segs[i].a.y == from.b.y && segs[i].b.x == from.a.x && segs[i].b.y == from.a.y))
            used[i] = true;
    const auto same = [](const gcm::Lattice2& p, const gcm::Lattice2& q) { return p.x == q.x && p.y == q.y; };
    for (int dir = 0; dir < 2; ++dir) {
        bool grew = true;
        while (grew && chain.size() < 4096) {
            grew = false;
            const gcm::Lattice2 end = dir == 0 ? chain.back() : chain.front();
            for (std::size_t i = 0; i < segs.size(); ++i) {
                if (used[i]) continue;
                gcm::Lattice2 next;
                if (same(segs[i].a, end)) next = segs[i].b;
                else if (same(segs[i].b, end)) next = segs[i].a;
                else continue;
                used[i] = true;
                if (dir == 0) chain.push_back(next); else chain.insert(chain.begin(), next);
                grew = true;
                break;
            }
            if (same(chain.front(), chain.back()) && chain.size() > 2) break;
        }
    }
    std::vector<Point3> path;
    for (const auto& p : chain) {
        double x, y, wx, wy, wz;
        gcm::to_world(vol_, p, x, y);
        src.plane.to_world(x, y, wx, wy, wz);
        path.push_back(Point3::from(wx, wy, wz));
    }
    gtopo::Body made = gtopo::sweep(target_.profile, vol_, path, true);
    if (made.face_count() == 0) { message_ = "nothing to sweep"; stage_ = Stage::PickTarget; return; }
    snapshot();
    bodies_.push_back(std::move(made));
    note(bodies_.size() - 1, "Sweep", target_.source);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "swept along %zu segments", path.size() - 1);
    message_ = buf;
    target_ = Target{};
    path_valid_ = false;
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

void Modeller::commit_shell(double thickness) {
    if (!target_.valid || !target_.on_body || target_.body >= bodies_.size()) return;
    const gtopo::ShellResult r = gtopo::shell_body(bodies_[target_.body], thickness,
                                                   {target_.face}, vol_);
    if (!r.ok) {
        message_ = r.reason.empty() ? "could not shell that solid" : r.reason;
        stage_ = Stage::PickTarget;
        target_ = Target{};
        return;
    }
    snapshot();
    bodies_[target_.body] = r.body;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "shelled to a %.4g wall", thickness);
    message_ = buf;
    std::snprintf(buf, sizeof(buf), "Shell %s", gunit::format_length(thickness, gunit::Format{display_unit_}).c_str());
    note(target_.body, buf);
    {
        Feature& f = history_[target_.body].back();
        f.op = Op::ShellBody; f.value = thickness; f.regenerable = true; f.on_body = true;
        f.anchor = target_.centroid;
    }
    target_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

void Modeller::commit_split(const Target& by) {
    double ox, oy, oz, nx, ny, nz;
    if (first_body_ >= bodies_.size() || !plane_of(by, ox, oy, oz, nx, ny, nz)) return;
    const auto bb = bodies_[first_body_].bounds(vol_);
    const double span = 4.0 * std::max({bb.hi.x.to_f64() - bb.lo.x.to_f64(),
                                        bb.hi.y.to_f64() - bb.lo.y.to_f64(),
                                        bb.hi.z.to_f64() - bb.lo.z.to_f64(), 1.0});
    const V3 c{(bb.lo.x.to_f64() + bb.hi.x.to_f64()) * 0.5, (bb.lo.y.to_f64() + bb.hi.y.to_f64()) * 0.5,
               (bb.lo.z.to_f64() + bb.hi.z.to_f64()) * 0.5};
    const V3 n = unit({nx, ny, nz});
    const V3 o = c - n * dot(c - V3{ox, oy, oz}, n);
    const auto frame = frame_on(o, n);
    gtopo::Profile square;
    for (auto [sx, sy] : {std::pair{-1.0, -1.0}, std::pair{1.0, -1.0}, std::pair{1.0, 1.0}, std::pair{-1.0, 1.0}})
        square.outer.push_back(gcm::to_lattice(vol_, sx * span, sy * span));
    square.normalize();
    const gtopo::Body half = gtopo::extrude(square, vol_, frame, span);
    gtopo::BoolResult keep = gtopo::boolean_op(bodies_[first_body_], half, gtopo::BoolOp::Intersect, vol_);
    gtopo::BoolResult rest = gtopo::boolean_op(bodies_[first_body_], half, gtopo::BoolOp::Subtract, vol_);
    if (!keep.ok() || !rest.ok() || keep.body.face_count() == 0 || rest.body.face_count() == 0) {
        message_ = "the plane does not pass through the solid";
        stage_ = Stage::PickTarget;
        target_ = Target{};
        return;
    }
    snapshot();
    bodies_[first_body_] = std::move(keep.body);
    bodies_.push_back(std::move(rest.body));
    note(first_body_, "Split");
    note(bodies_.size() - 1, "Split from Solid " + std::to_string(first_body_ + 1));
    message_ = "split into two solids";
    target_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

void Modeller::commit_section(const Target& by) {
    double ox, oy, oz, nx, ny, nz;
    if (first_body_ >= bodies_.size() || !plane_of(by, ox, oy, oz, nx, ny, nz)) return;
    const V3 n = unit({nx, ny, nz});
    gtopo::Plane pl;
    pl.nx = n.x; pl.ny = n.y; pl.nz = n.z;
    pl.d = n.x * ox + n.y * oy + n.z * oz;
    const gtopo::SectionResult r = gtopo::section(bodies_[first_body_], pl, vol_);
    if (!r.ok || r.segments.empty()) {
        message_ = r.reason && r.reason[0] ? r.reason : "the plane does not cut the solid";
        stage_ = Stage::PickTarget;
        target_ = Target{};
        return;
    }
    section_ = SectionOut{};
    section_.valid = true;
    section_.ox = ox; section_.oy = oy; section_.oz = oz;
    section_.nx = n.x; section_.ny = n.y; section_.nz = n.z;
    section_.segments = r.segments;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "section: %zu loops, area %.4g", r.loops.size(), r.cut_area);
    message_ = buf;
    target_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
}

void Modeller::commit_pattern(int count, double spacing) {
    if (!target_.valid || !target_.on_body || target_.body >= bodies_.size()) return;
    const gtopo::Body src = bodies_[target_.body];
    if (src.has_surfaces()) { message_ = "cannot yet pattern a solid with curved faces"; stage_ = Stage::PickTarget; return; }
    const V3 n = unit(v3(target_.normal));
    snapshot();
    const std::size_t from = target_.body;
    for (int i = 1; i < count; ++i) {
        const V3 d = n * (spacing * i);
        bodies_.push_back(transformed(src, false, [&](double& x, double& y, double& z) {
            x += d.x; y += d.y; z += d.z;
        }));
        note(bodies_.size() - 1, "Pattern of Solid " + std::to_string(from + 1));
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "patterned: %d solids %.4g apart", count, spacing);
    message_ = buf;
    target_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

void Modeller::commit_mirror(const Target& by) {
    double ox, oy, oz, nx, ny, nz;
    if (first_body_ >= bodies_.size() || !plane_of(by, ox, oy, oz, nx, ny, nz)) return;
    const gtopo::Body src = bodies_[first_body_];
    if (src.has_surfaces()) { message_ = "cannot yet mirror a solid with curved faces"; stage_ = Stage::PickTarget; return; }
    const V3 n = unit({nx, ny, nz});
    const V3 o{ox, oy, oz};
    snapshot();
    bodies_.push_back(transformed(src, true, [&](double& x, double& y, double& z) {
        const double d = dot(V3{x, y, z} - o, n);
        x -= 2.0 * d * n.x; y -= 2.0 * d * n.y; z -= 2.0 * d * n.z;
    }));
    note(bodies_.size() - 1, "Mirror of Solid " + std::to_string(first_body_ + 1));
    message_ = "mirrored";
    target_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

std::string Modeller::describe(std::size_t body) const {
    if (body >= bodies_.size()) return "no solid";
    volume_cache_.resize(bodies_.size());
    auto& volume = volume_cache_[body];
    if (!volume) volume = gtopo::volume(bodies_[body], vol_);
    char buf[128];
    const double cube = gunit::metres_per(display_unit_);
    std::snprintf(buf, sizeof(buf), "volume %.4g %s\xc2\xb3, %u faces",
                  *volume / (cube * cube * cube),
                  gunit::suffix(display_unit_), bodies_[body].face_count());
    return buf;
}

double Modeller::axis_param(const gview::Camera& cam, double px, double py) const {
    const auto ray = cam.ray_through(px, py);
    const V3 p1 = v3(target_.centroid), d1 = unit(v3(target_.normal));
    const V3 p2 = v3(ray.origin), d2 = unit(v3(ray.direction));
    const V3 r = p1 - p2;
    const double a = dot(d1, d1), b = dot(d1, d2), c = dot(d2, d2);
    const double d = dot(d1, r), e = dot(d2, r);
    const double denom = a * c - b * b;
    if (std::fabs(denom) < 2e-3) return distance_;
    return (b * e - c * d) / denom;
}

void Modeller::drag(const gview::Camera& cam, double px, double py) {
    if (stage_ != Stage::Dragging) return;
    distance_ = axis_param(cam, px, py) - grab_offset_;
    mesh_dirty_ = true;
}

bool Modeller::release() {
    if (stage_ != Stage::Dragging) return false;
    clear_preview();
    stage_ = Stage::Placed;
    if (std::fabs(distance_) < 1e-9) return false;
    commit(distance_);
    return committed_;
}

void Modeller::set_typed(std::string text) {
    if (stage_!=Stage::Placed && stage_!=Stage::Dragging && stage_!=Stage::TypeValue) return;
    if (text==typed_) return;
    const bool had_typed_preview=typed_preview_;
    clear_preview();
    typed_=std::move(text);
    if (typed_.empty()) { if (had_typed_preview) distance_=0; return; }
    if (op_==Op::PatternBody && stage_==Stage::TypeValue) {
        int count=0; double spacing=0;
        if (std::sscanf(typed_.c_str(),"%d %lf",&count,&spacing)!=2 || count<2 || !std::isfinite(spacing) || spacing<=0 ||
            !target_.valid || target_.body>=bodies_.size()) { value_error_="Enter count and spacing, e.g. 4 2.5"; return; }
        const auto& source=bodies_[target_.body];
        if (source.has_surfaces()) { value_error_="Cannot pattern curved faces yet"; return; }
        const V3 n=unit(v3(target_.normal));
        value_preview_replaces_=false;
        for (int i=1;i<count;++i) {
            const V3 d=n*(spacing*i);
            value_preview_.push_back({target_.body,transformed(source,false,[&](double& x,double& y,double& z) { x+=d.x; y+=d.y; z+=d.z; })});
        }
        return;
    }
    const auto parsed=gunit::parse_length(typed_,display_unit_);
    if (!parsed || !std::isfinite(*parsed) || std::fabs(*parsed)<=1e-9 ||
        (stage_==Stage::TypeValue && *parsed<=0)) { value_error_="Enter a valid nonzero size"; return; }
    if (stage_!=Stage::TypeValue) {
        distance_=*parsed;
        typed_preview_=true;
        return;
    }
    value_preview_replaces_=true;
    if (op_==Op::ShellBody && target_.valid && target_.body<bodies_.size()) {
        const auto draft=gtopo::shell_body(bodies_[target_.body],*parsed,{target_.face},vol_);
        if (!draft.ok) { value_error_=draft.reason; return; }
        value_preview_.push_back({target_.body,draft.body});
    } else if(edge_valid_ && selected_edges_.size()>1) {
        edge_drafts(op_,*parsed,edge_applied_,value_preview_,value_error_);
    } else if (edge_valid_ && edge_body_<bodies_.size()) {
        const auto& source=edge_applied_ && !undo_.empty() ? undo_.back().bodies[edge_body_] : bodies_[edge_body_];
        const auto draft=op_==Op::ChamferEdge
            ? gtopo::chamfer_edge(source,edge_he_,*parsed,*parsed,vol_)
            : gtopo::fillet_edge(source,edge_he_,*parsed,12,vol_);
        if (!draft.ok) { value_error_=draft.reason; return; }
        value_preview_.push_back({edge_body_,draft.body});
    }
}

void Modeller::type(char c) {
    if (c>=32 && c<127) set_typed(typed_+c);
}

void Modeller::backspace() {
    if (!typed_.empty()) set_typed(typed_.substr(0,typed_.size()-1));
}

bool Modeller::commit_typed() {
    if (stage_ == Stage::TypeValue) {
        if (typed_.empty()) return false;
        if (op_ == Op::PatternBody) {
            int count = 0; double spacing = 0.0;
            if (std::sscanf(typed_.c_str(), "%d %lf", &count, &spacing) != 2 || count < 2 || !std::isfinite(spacing) || spacing <= 0.0) {
                message_ = "count and spacing, e.g. 4 2.5";
                return false;
            }
            typed_.clear();
            commit_pattern(count, spacing);
            return committed_;
        }
        const auto parsed = gunit::parse_length(typed_, display_unit_);
        const double d = parsed.value_or(0.0);
        const bool bad = !parsed || !std::isfinite(d) || d <= 1e-9;
        if (bad) { message_ = "a positive value, please"; return false; }
        if (op_ == Op::ShellBody) {
            committed_=false;
            commit_shell(d);
        } else {
            const bool ok=apply_edge(op_ == Op::ChamferEdge ? Op::ChamferEdge : Op::FilletEdge, d);
            if (ok) typed_.clear();
            else { clear_preview(); value_error_=message_; }
            return ok;
        }
        if (committed_) typed_.clear();
        return committed_;
    }
    if (stage_ != Stage::Placed && stage_ != Stage::Dragging) return false;
    if (typed_.empty()) return false;
    const auto parsed = gunit::parse_length(typed_, display_unit_);
    const double d = parsed.value_or(0.0);
    if (!parsed || !std::isfinite(d) || std::fabs(d) < 1e-9) { message_="Enter a valid nonzero size"; return false; }
    stage_ = Stage::Placed;
    commit(d);
    return committed_;
}

void Modeller::place(Op op, const Target& t) {
    if (!t.valid || !t.surface_faces.empty()) return;
    op_ = op;
    target_ = t;
    hover_ = Target{};
    distance_ = 0.0;
    typed_.clear();
    stage_ = Stage::Placed;
    message_ = prompt();
    mesh_dirty_ = true;
}

void Modeller::preselect_body(Op op, std::size_t body) {
    if (body >= bodies_.size()) return;
    arm(op);
    first_body_ = body;
    Target t;
    if (target_from_face(body, 0, t)) target_ = t;
    stage_ = Stage::PickSecond;
    message_ = prompt();
}

gcm::u32 Modeller::add_material(Material m) {
    for (gcm::u32 i = 0; i < gcm::u32(materials_.size()); ++i)
        if (materials_[i].name == m.name) {
            materials_[i] = std::move(m);
            invalidate_mesh();
            return i;
        }
    materials_.push_back(std::move(m));
    invalidate_mesh();
    return gcm::u32(materials_.size()) - 1;
}

bool Modeller::set_material(std::size_t body, std::string_view name) {
    if (body >= bodies_.size()) return false;
    for (gcm::u32 i = 0; i < gcm::u32(materials_.size()); ++i)
        if (materials_[i].name == name) {
            if (body_material_.size() < bodies_.size()) body_material_.resize(bodies_.size(), 0);
            body_material_[body] = i;
            invalidate_mesh();
            return true;
        }
    return false;
}

void Modeller::add_body(gtopo::Body b, const char* what) {
    if (b.face_count() == 0) { message_ = "nothing to add"; return; }
    snapshot();
    gtopo::IdMap ids;
    ids.seed(b);
    bodies_.push_back(std::move(b));
    face_ids_.push_back(std::move(ids));
    body_material_.push_back(0);
    note(bodies_.size() - 1, what);
    message_ = what;
    invalidate_mesh();
}

std::vector<std::vector<gtopo::FaceId>> Modeller::merge_loaded_faces() {
    sync_face_ids();
    std::vector<std::vector<gtopo::FaceId>> maps(bodies_.size());
    bool changed = false;
    for (std::size_t bi = 0; bi < bodies_.size(); ++bi) {
        auto merged = gtopo::merge_coplanar_faces(bodies_[bi], vol_, &maps[bi]);
        if (merged.face_count() == bodies_[bi].face_count() &&
            merged.vertex_count() == bodies_[bi].vertex_count()) continue;
        const auto& old_ids = face_ids_[bi];
        std::vector<gcm::u32> ids(merged.face_count(), 0);
        for (gtopo::FaceId f = 0; f < maps[bi].size(); ++f)
            if (!ids[maps[bi][f]]) ids[maps[bi][f]] = old_ids.face_id(f);
        const auto rename = [&](gcm::u32 id) {
            const auto f = old_ids.face_of(id);
            return f == gtopo::kInvalid ? id : ids[maps[bi][f]];
        };
        for (auto& mate : mates_) {
            if (mate.body_a == body_key_[bi]) mate.face_a = rename(mate.face_a);
            if (mate.body_b == body_key_[bi]) mate.face_b = rename(mate.face_b);
        }
        const auto next = old_ids.next_id();
        face_ids_[bi].restore(std::move(ids), next);
        bodies_[bi] = std::move(merged);
        changed = true;
    }
    if (changed) { ++edit_serial_; invalidate_mesh(); }
    return maps;
}

void Modeller::set_bodies(std::vector<gtopo::Body> bodies) {
    arm(Op::None);
    ++edit_serial_;
    bodies_ = std::move(bodies);
    body_material_.assign(bodies_.size(), 0);
    face_ids_.clear();
    face_ids_.reserve(bodies_.size());
    for (const auto& b : bodies_) {
        gtopo::IdMap ids;
        ids.seed(b);
        face_ids_.push_back(std::move(ids));
    }
    history_.assign(bodies_.size(), {{"Loaded", -1}});
    body_layer_.assign(bodies_.size(), current_layer_);
    undo_.clear();
    redo_.clear();
    target_ = Target{};
    hover_ = Target{};
    if (stage_ != Stage::Idle) stage_ = Stage::PickTarget;
    invalidate_mesh();
}

namespace {
std::vector<gdoc::Segment2> split_at_junctions(const std::vector<gdoc::Segment2>& in,
                                               const gcm::WorkingVolume& vol) {
    struct S { double ax, ay, bx, by; };
    std::vector<S> segs;
    for (const auto& sg : in) {
        S s;
        gcm::to_world(vol, sg.a, s.ax, s.ay);
        gcm::to_world(vol, sg.b, s.bx, s.by);
        segs.push_back(s);
    }
    const double eps = 4.0 / vol.scale();
    std::vector<gdoc::Segment2> out;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const S& s = segs[i];
        const double dx = s.bx - s.ax, dy = s.by - s.ay, l2 = dx * dx + dy * dy;
        if (l2 < eps * eps) continue;
        std::vector<double> ts = {0.0, 1.0};
        for (std::size_t j = 0; j < segs.size(); ++j) {
            if (j == i) continue;
            const S& o = segs[j];
            for (auto [px, py] : {std::pair{o.ax, o.ay}, std::pair{o.bx, o.by}}) {
                const double t = ((px - s.ax) * dx + (py - s.ay) * dy) / l2;
                if (t <= 1e-9 || t >= 1.0 - 1e-9) continue;
                const double cx = s.ax + dx * t, cy = s.ay + dy * t;
                if (std::hypot(px - cx, py - cy) <= eps) ts.push_back(t);
            }
            const double ox = o.bx - o.ax, oy = o.by - o.ay;
            const double den = dx * oy - dy * ox;
            if (std::fabs(den) < 1e-12) continue;
            const double t = ((o.ax - s.ax) * oy - (o.ay - s.ay) * ox) / den;
            const double u = ((o.ax - s.ax) * dy - (o.ay - s.ay) * dx) / den;
            if (t > 1e-9 && t < 1.0 - 1e-9 && u > 1e-9 && u < 1.0 - 1e-9) ts.push_back(t);
        }
        std::sort(ts.begin(), ts.end());
        for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
            if (ts[k + 1] - ts[k] < 1e-9) continue;
            out.push_back({gcm::to_lattice(vol, s.ax + dx * ts[k], s.ay + dy * ts[k]),
                           gcm::to_lattice(vol, s.ax + dx * ts[k + 1], s.ay + dy * ts[k + 1])});
        }
    }
    return out;
}

std::vector<gtopo::Body> join_touching(std::vector<gtopo::Body> parts, const gcm::WorkingVolume& vol) {
    if (parts.size() > 512) return parts;
    if (parts.size() < 2) return parts;
    struct Box { double lo_x, lo_y, lo_z, hi_x, hi_y, hi_z; };
    const auto box_of = [&](const gtopo::Body& b) {
        const auto bb = b.bounds(vol);
        return Box{bb.lo.x.to_f64(), bb.lo.y.to_f64(), bb.lo.z.to_f64(),
                   bb.hi.x.to_f64(), bb.hi.y.to_f64(), bb.hi.z.to_f64()};
    };
    std::vector<Box> boxes;
    boxes.reserve(parts.size());
    for (const auto& b : parts) boxes.push_back(box_of(b));
    const auto touches = [](const Box& a, const Box& b) {
        const double m = 1e-9;
        return a.lo_x <= b.hi_x + m && b.lo_x <= a.hi_x + m &&
               a.lo_y <= b.hi_y + m && b.lo_y <= a.hi_y + m &&
               a.lo_z <= b.hi_z + m && b.lo_z <= a.hi_z + m;
    };
    bool merged = true;
    while (merged && parts.size() > 1) {
        merged = false;
        for (std::size_t i = 0; i < parts.size() && !merged; ++i)
            for (std::size_t j = i + 1; j < parts.size() && !merged; ++j) {
                if (!touches(boxes[i], boxes[j])) continue;
                const gtopo::BoolResult r = gtopo::unite(parts[i], parts[j], vol);
                if (!r.ok() || r.body.face_count() == 0) continue;
                const double vi = gtopo::volume(parts[i], vol), vj = gtopo::volume(parts[j], vol);
                const double vu = gtopo::volume(r.body, vol);
                const double eps = 1e-6 * std::max(1.0, vi + vj);
                if (vu > vi + vj - eps) {
                    if (r.body.face_count() >= parts[i].face_count() + parts[j].face_count()) continue;
                }
                parts[i] = r.body;
                boxes[i] = box_of(parts[i]);
                parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(j));
                boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(j));
                merged = true;
            }
    }
    return parts;
}
}

std::size_t Modeller::lift_all(int si, double height) {
    if (si < 0 || si >= static_cast<int>(sources_.size()) || std::fabs(height) < 1e-9) return 0;
    const SketchSource& src = sources_[si];
    if (!src.segments || src.segments->empty()) { message_ = "the sketch is empty"; return 0; }

    struct Found { double area; double cx, cy; };
    std::vector<Found> seen;
    std::vector<gtopo::Body> parts;
    const std::vector<gdoc::Segment2> split = split_at_junctions(*src.segments, vol_);
    std::vector<SketchSource> saved = sources_;
    sources_[si].segments = &split;
    struct Restore { std::vector<SketchSource>& s; std::vector<SketchSource> v; ~Restore() { s = v; } } restore{sources_, saved};
    const auto area_of = [&](const gtopo::Profile& p) {
        double a = 0.0;
        for (std::size_t i = 0; i < p.outer.size(); ++i) {
            double x0, y0, x1, y1;
            gcm::to_world(vol_, p.outer[i], x0, y0);
            gcm::to_world(vol_, p.outer[(i + 1) % p.outer.size()], x1, y1);
            a += x0 * y1 - x1 * y0;
        }
        return std::fabs(a) * 0.5;
    };
    for (const auto& seg : split) {
        double ax, ay, bx, by;
        gcm::to_world(vol_, seg.a, ax, ay);
        gcm::to_world(vol_, seg.b, bx, by);
        const double mx = (ax + bx) * 0.5, my = (ay + by) * 0.5;
        const double nx = -(by - ay), ny = bx - ax, nl = std::hypot(nx, ny);
        if (nl < 1e-12) continue;
        const double step = 1e-3 * std::max(1.0, nl);
        for (double side : {1.0, -1.0}) {
            Target t;
            if (!target_from_region(si, mx + nx / nl * step * side, my + ny / nl * step * side, t)) continue;
            const double area = area_of(t.profile);
            const double cx = t.centroid.x.to_f64(), cy = t.centroid.y.to_f64();
            bool dup = false;
            for (const auto& f : seen)
                if (std::fabs(f.area - area) < 1e-9 * std::max(1.0, area) &&
                    std::fabs(f.cx - cx) < 1e-6 && std::fabs(f.cy - cy) < 1e-6) { dup = true; break; }
            if (dup) continue;
            seen.push_back({area, cx, cy});
            target_ = t;
            gtopo::Body b = make_body(height);
            if (b.face_count() > 0) parts.push_back(std::move(b));
        }
    }
    target_ = Target{};
    if (parts.empty()) { message_ = "no closed regions in the sketch"; return 0; }
    parts = join_touching(std::move(parts), vol_);
    snapshot();
    const std::size_t n = parts.size();
    for (auto& b : parts) {
        bodies_.push_back(std::move(b));
        char lab[64];
        std::snprintf(lab, sizeof(lab), "Lift %.4g", height);
        note(bodies_.size() - 1, lab, si);
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "raised the plan by %.4g: %zu solid(s)", height, n);
    message_ = buf;
    committed_ = true;
    invalidate_mesh();
    return n;
}

std::size_t Modeller::walls(int si, double thickness, double height,
                            const std::vector<gdoc::Segment2>* centerlines,
                            const std::vector<gdoc::Segment2>* material) {
    if (si < 0 || si >= static_cast<int>(sources_.size()) || thickness <= 0.0 || std::fabs(height) < 1e-9) return 0;
    const SketchSource& src = sources_[si];
    const auto* lines=centerlines?centerlines:src.segments;
    if((!lines || lines->empty()) && (!material || material->empty())) {message_="Select plain wall centerlines or existing wall regions";return 0;}
    std::vector<gtopo::Body> parts;
    const gtopo::SketchPlane& pl = src.plane;
    ggeo::d3::Frame<F64> wall_frame;
    wall_frame.origin=Point3::from(pl.ox,pl.oy,pl.oz);
    wall_frame.u=Vec3::from(pl.ux,pl.uy,pl.uz);wall_frame.v=Vec3::from(pl.vx,pl.vy,pl.vz);wall_frame.n=Vec3::from(pl.nx,pl.ny,pl.nz);
    if(material && !material->empty()) {
        const auto loops=gdoc::extract_loops(*material);
        std::size_t boundary_edges=0;
        for(const auto& loop:loops.loops)boundary_edges+=loop.size();
        if(!loops.ok() || boundary_edges!=material->size()) {
            message_="Repair the open wall boundary before creating 3D walls; no solids created";
            return 0;
        }
        std::vector<int> parent(loops.loops.size(),-1);
        const auto area=[](const auto& loop) {
            long double result=0;for(std::size_t j=0;j<loop.size();++j){const auto a=loop[j],b=loop[(j+1)%loop.size()];result+=(long double)a.x*b.y-(long double)b.x*a.y;}return std::abs(result);
        };
        for(std::size_t i=0;i<loops.loops.size();++i)for(std::size_t j=0;j<loops.loops.size();++j)
            if(i!=j && area(loops.loops[j])>area(loops.loops[i]) && gdoc::point_in_loop(loops.loops[i].front(),loops.loops[j]) &&
               (parent[i]<0 || area(loops.loops[j])<area(loops.loops[std::size_t(parent[i])])))parent[i]=int(j);
        for(std::size_t i=0;i<loops.loops.size();++i) {
            int depth=0;for(int p=parent[i];p>=0;p=parent[std::size_t(p)])++depth;
            if(depth%2)continue;
            gtopo::Profile profile;profile.outer=loops.loops[i];
            for(std::size_t j=0;j<loops.loops.size();++j)if(parent[j]==int(i))profile.holes.push_back(loops.loops[j]);
            profile.normalize();auto body=gtopo::extrude(profile,vol_,wall_frame,height);
            if(body.face_count()>0)parts.push_back(std::move(body));
        }
    }
    const std::vector<gdoc::Segment2> empty;
    for (const auto& seg : lines?*lines:empty) {
        double ax, ay, bx, by;
        gcm::to_world(vol_, seg.a, ax, ay);
        gcm::to_world(vol_, seg.b, bx, by);
        const double dx = bx - ax, dy = by - ay, l = std::hypot(dx, dy);
        if (l < 1e-9) continue;
        const double ux = dx / l, uy = dy / l, nx = -uy * thickness * 0.5, ny = ux * thickness * 0.5;
        const double ex = ux * thickness * 0.5, ey = uy * thickness * 0.5;
        const double qs[4][2] = {{ax - ex + nx, ay - ey + ny}, {bx + ex + nx, by + ey + ny},
                                 {bx + ex - nx, by + ey - ny}, {ax - ex - nx, ay - ey - ny}};
        gtopo::Profile prof;
        for (const auto& q : qs) prof.outer.push_back(gcm::to_lattice(vol_, q[0], q[1]));
        prof.normalize();
        ggeo::d3::Frame<F64> frame;
        frame.origin = Point3::from(pl.ox, pl.oy, pl.oz);
        frame.u = Vec3::from(pl.ux, pl.uy, pl.uz);
        frame.v = Vec3::from(pl.vx, pl.vy, pl.vz);
        frame.n = Vec3::from(pl.nx, pl.ny, pl.nz);
        gtopo::Body b = gtopo::extrude(prof, vol_, frame, height);
        if (b.face_count() > 0) parts.push_back(std::move(b));
    }
    if (parts.empty()) { message_ = "no lines to make walls from"; return 0; }
    const bool too_many_to_join = parts.size() > 512;
    parts = join_touching(std::move(parts), vol_);
    snapshot();
    const std::size_t n = parts.size();
    for (auto& b : parts) {
        bodies_.push_back(std::move(b));
        char lab[64];
        std::snprintf(lab, sizeof(lab), "Walls %.4g x %.4g", thickness, height);
        note(bodies_.size() - 1, lab, si);
    }
    char buf[128];
    if (too_many_to_join)
        std::snprintf(buf, sizeof(buf),
                      "walls %s x %s: %zu solids, too many to weld together -- UNION pairs as needed",
                      gunit::format_length(thickness, gunit::Format{display_unit_}).c_str(), gunit::format_length(height, gunit::Format{display_unit_}).c_str(), n);
    else
        std::snprintf(buf, sizeof(buf), "walls %s thick, %s high: %zu solid(s)",
                      gunit::format_length(thickness, gunit::Format{display_unit_}).c_str(), gunit::format_length(height, gunit::Format{display_unit_}).c_str(), n);
    message_ = buf;
    committed_ = true;
    invalidate_mesh();
    return n;
}

bool Modeller::erase(std::size_t body) {
    return erase_many({body});
}

bool Modeller::erase_many(std::vector<std::size_t> indices) {
    if(indices.empty()) return false;
    std::sort(indices.begin(),indices.end());
    indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
    if(indices.back()>=bodies_.size()) return false;
    snapshot();
    for(auto it=indices.rbegin();it!=indices.rend();++it) {
        const auto body=*it;
    bodies_.erase(bodies_.begin() + static_cast<std::ptrdiff_t>(body));
    if (body < history_.size()) history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(body));
    if (body < body_layer_.size()) body_layer_.erase(body_layer_.begin() + static_cast<std::ptrdiff_t>(body));
    if (body < face_ids_.size()) face_ids_.erase(face_ids_.begin() + static_cast<std::ptrdiff_t>(body));
    if (body < body_key_.size()) body_key_.erase(body_key_.begin() + static_cast<std::ptrdiff_t>(body));
    if (body < body_material_.size()) body_material_.erase(body_material_.begin() + static_cast<std::ptrdiff_t>(body));
    if (body < rollback_.size()) rollback_.erase(rollback_.begin() + static_cast<std::ptrdiff_t>(body));
    }
    arm(Op::None);
    message_=std::to_string(indices.size())+" solids deleted";
    invalidate_mesh();
    return true;
}

void Modeller::fill(const Target& t, const float color[4],
                    std::vector<MeshVertex>& out) const {
    if (!t.valid) return;
    if (!t.surface_faces.empty() && t.body<bodies_.size()) {
        const auto& body=bodies_[t.body];const auto& mesh=const_cast<Modeller*>(this)->tri(t.body);
        for(std::size_t i=0;i+2<mesh.indices.size();i+=3) {
            if(!std::binary_search(t.surface_faces.begin(),t.surface_faces.end(),mesh.face_of[i/3])) continue;
            const auto normal=gtopo::plane_of_face(body,mesh.face_of[i/3],vol_);
            for(int k=0;k<3;++k) {
                double x,y,z;gcm::to_world(vol_,body.vertex_point[mesh.indices[i+k]],x,y,z);
                MeshVertex v{};v.pos[0]=float(x);v.pos[1]=float(y);v.pos[2]=float(z);
                v.normal[0]=float(normal.nx);v.normal[1]=float(normal.ny);v.normal[2]=float(normal.nz);
                std::copy(color,color+4,v.color);out.push_back(v);
            }
        }
        return;
    }
    if (t.outline.empty()) return;
    std::vector<gcm::Lattice2> pts;
    std::vector<std::vector<gcm::u32>> loops;
    std::vector<Point3> world;
    const auto add = [&](const gtopo::Loop2& l2, const std::vector<Point3>& l3) {
        std::vector<gcm::u32> idx;
        for (std::size_t i = 0; i < l2.size() && i < l3.size(); ++i) {
            idx.push_back(static_cast<gcm::u32>(pts.size()));
            pts.push_back(l2[i]);
            world.push_back(l3[i]);
        }
        loops.push_back(std::move(idx));
    };
    add(t.profile.outer, t.outline[0]);
    for (std::size_t h = 0; h < t.profile.holes.size() && h + 1 < t.outline.size(); ++h)
        add(t.profile.holes[h], t.outline[h + 1]);

    const std::vector<gcm::u32> tri = gtopo::triangulate_face(loops, pts);
    const V3 n = v3(t.normal);
    for (std::size_t i = 0; i + 2 < tri.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            const V3 p = v3(world[tri[i + k]]);
            MeshVertex v{};
            v.pos[0] = float(p.x); v.pos[1] = float(p.y); v.pos[2] = float(p.z);
            v.normal[0] = float(n.x); v.normal[1] = float(n.y); v.normal[2] = float(n.z);
            v.color[0] = color[0]; v.color[1] = color[1]; v.color[2] = color[2];
            v.color[3] = color[3];
            out.push_back(v);
        }
    }
}

bool Modeller::commit_distance(double distance) {
    if (!target_.valid || std::fabs(distance) < 1e-12) return false;
    commit(distance);
    return committed_;
}

double Modeller::through_distance() const {
    if (!target_.valid) return 0.0;
    const double nx = target_.normal.x.to_f64();
    const double ny = target_.normal.y.to_f64();
    const double nz = target_.normal.z.to_f64();
    const double start = target_.centroid.x.to_f64() * nx +
                         target_.centroid.y.to_f64() * ny +
                         target_.centroid.z.to_f64() * nz;
    double far = start;
    double span = 0.0;
    for (const auto& b : bodies_)
        for (const auto& p : b.vertex_point) {
            double x, y, z;
            gcm::to_world(vol_, p, x, y, z);
            const double t = x * nx + y * ny + z * nz;
            far = std::max(far, t);
            span = std::max(span, std::fabs(t - start));
        }
    return (far - start) + std::max(span * 1e-3, 1e-4);
}

gtopo::Body Modeller::make_body(double distance) const {
    return body_for(target_.profile, target_.frame, distance);
}

void Modeller::preview_triangles(const float color[4], std::vector<MeshVertex>& out) {
    preview_ = Preview{};
    if ((stage_ != Stage::Dragging && !typed_preview_) || !target_.valid || std::fabs(distance_) <= 1e-9) return;
    const auto& profile = target_.profile;
    if (profile.outer.size() < 3) return;
    const auto& frame = target_.frame;
    const V3 origin = v3(frame.origin), u = v3(frame.u), v = v3(frame.v);
    const V3 axis = v3(frame.n), offset = axis * distance_;
    const V3 top_normal = axis * (distance_ < 0 ? -1.0 : 1.0);
    std::vector<gcm::Lattice2> points;
    std::vector<std::vector<gcm::u32>> loops;
    std::vector<V3> world;
    const auto add_loop = [&](const gtopo::Loop2& loop) {
        std::vector<gcm::u32> indices;
        for (const auto& p : loop) {
            double x, y;
            gcm::to_world(vol_, p, x, y);
            indices.push_back(static_cast<gcm::u32>(points.size()));
            points.push_back(p);
            world.push_back(origin + u*x + v*y);
        }
        loops.push_back(std::move(indices));
    };
    add_loop(profile.outer);
    for (const auto& hole : profile.holes) add_loop(hole);
    const auto emit = [&](V3 a, V3 b, V3 c, V3 normal) {
        if (dot(cross(b-a, c-a), normal) < 0) std::swap(b, c);
        for (const auto p : {a, b, c}) {
            MeshVertex vertex{};
            vertex.pos[0] = float(p.x); vertex.pos[1] = float(p.y); vertex.pos[2] = float(p.z);
            vertex.normal[0] = float(normal.x); vertex.normal[1] = float(normal.y); vertex.normal[2] = float(normal.z);
            std::copy(color, color+4, vertex.color);
            out.push_back(vertex);
        }
    };
    const auto caps = gtopo::triangulate_face(loops, points);
    for (std::size_t i = 0; i+2 < caps.size(); i += 3) {
        const V3 a = world[caps[i]], b = world[caps[i+1]], c = world[caps[i+2]];
        emit(a, b, c, top_normal * -1.0);
        emit(a+offset, b+offset, c+offset, top_normal);
    }
    for (std::size_t li = 0; li < loops.size(); ++li) {
        const auto& loop = loops[li];
        double area = 0;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const auto a = world[loop[i]] - origin, b = world[loop[(i+1)%loop.size()]] - origin;
            area += dot(cross(a, b), axis);
        }
        const double sign = ((area > 0) == (li == 0)) ? 1.0 : -1.0;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const V3 a = world[loop[i]], b = world[loop[(i+1)%loop.size()]];
            V3 normal = cross(b-a, axis);
            const double length = std::sqrt(dot(normal, normal));
            if (length <= 1e-20) continue;
            normal = normal * (sign / length);
            emit(a, b, b+offset, normal);
            emit(a, b+offset, a+offset, normal);
        }
    }
    preview_.active = true;
    preview_.cut = distance_ < 0;
    preview_.on_body = target_.on_body && target_.body < bodies_.size();
}

gbool::PreviewState Modeller::gpu_preview() const {
    gbool::PreviewState s;
    const auto copy_body = [&](std::size_t body, std::vector<MeshVertex>& out,
                               std::size_t& first, std::size_t& count) {
        for (const auto& range : mesh_ranges_) {
            if (range.body != body || range.first+range.count > mesh_.size()) continue;
            first = range.first; count = range.count;
            out.assign(mesh_.begin()+first, mesh_.begin()+first+count);
            const auto& triangles = tri(body);
            for (std::size_t i = 0; i < out.size(); ++i) {
                const auto& n = triangles.normals[i/3];
                out[i].normal[0] = float(n.x.to_f64());
                out[i].normal[1] = float(n.y.to_f64());
                out[i].normal[2] = float(n.z.to_f64());
            }
            return;
        }
    };
    if (stage_ == Stage::PickSecond && hover_.valid && hover_.on_body &&
        hover_.body != first_body_ && first_body_ < bodies_.size() && hover_.body < bodies_.size() &&
        (op_ == Op::Union || op_ == Op::Subtract || op_ == Op::Intersect)) {
        s.operation = op_ == Op::Union ? gbool::Operation::Union :
                      op_ == Op::Subtract ? gbool::Operation::Subtract : gbool::Operation::Intersect;
        copy_body(first_body_, s.body, s.target_body_first, s.target_body_count);
        copy_body(hover_.body, s.tool, s.tool_body_first, s.tool_body_count);
        if (s.body.empty()) s.tool.clear();
        return s;
    }
    if ((stage_ != Stage::Dragging && !typed_preview_) || !target_.valid || !preview_.active ||
        preview_range_count_ == 0 || preview_range_first_ + preview_range_count_ > mesh_.size()) return s;
    s.first = preview_range_first_;
    s.count = preview_range_count_;
    s.body_first = s.first;
    s.operation = preview_.on_body && preview_.cut ? gbool::Operation::Subtract : gbool::Operation::Union;
    s.tool.assign(mesh_.begin() + s.first, mesh_.begin() + s.first + s.count);
    if (preview_.on_body && target_.body < bodies_.size()) {
        copy_body(target_.body, s.body, s.target_body_first, s.target_body_count);
        if (s.body.empty()) s.tool.clear();
    }
    return s;
}

gtopo::Body Modeller::body_for(const gtopo::Profile& profile, const ggeo::d3::Frame<F64>& frame, double distance) const {
    if (distance >= 0.0)
        return gtopo::extrude(profile, vol_, frame, distance);

    ggeo::d3::Frame<F64> back = frame;
    const V3 v = v3(frame.v), n = v3(frame.n);
    back.v = N(v * -1.0);
    back.n = N(n * -1.0);
    gtopo::Profile p;
    const auto reproject = [&](const gtopo::Loop2& in) {
        gtopo::Loop2 out;
        for (const auto& q : in) {
            double x, y;
            gcm::to_world(vol_, q, x, y);
            out.push_back(gcm::to_lattice(vol_, x, -y));
        }
        return out;
    };
    p.outer = reproject(profile.outer);
    for (const auto& h : profile.holes) p.holes.push_back(reproject(h));
    p.normalize();
    return gtopo::extrude(p, vol_, back, -distance);
}

Modeller::Metadata Modeller::metadata() const {
    sync_face_ids();
    Metadata m;
    m.materials = materials_; m.body_keys = body_key_; m.last_body_key = last_body_key_;
    m.mates = mates_;
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        m.body_layers.push_back(body_layer(i));
        m.body_material.push_back(material_of(i));
        m.face_ids.push_back(face_ids_[i].all());
        m.next_face_ids.push_back(face_ids_[i].next_id());
        m.rollback.push_back(rolled_back_to(i));
        std::vector<gcm::u8> flags;
        for (const auto& f : history(i)) flags.push_back(f.suppressed ? 1 : 0);
        m.suppressed.push_back(std::move(flags));
    }
    return m;
}

bool Modeller::load_metadata(const Metadata& m) {
    const auto n = bodies_.size();
    if (m.materials.empty() || m.body_layers.size() != n || m.body_material.size() != n || m.body_keys.size() != n ||
        m.face_ids.size() != n || m.next_face_ids.size() != n || m.suppressed.size() != n || m.rollback.size() != n)
        return false;
    std::vector<gtopo::IdMap> ids(n);
    std::vector<gcm::u32> keys = m.body_keys;
    std::sort(keys.begin(), keys.end());
    if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!m.body_keys[i] || m.body_keys[i] > m.last_body_key || m.body_material[i] >= m.materials.size() ||
            m.face_ids[i].size() != bodies_[i].face_count() || m.suppressed[i].size() != history(i).size()) return false;
        auto faces = m.face_ids[i]; std::sort(faces.begin(), faces.end());
        if (std::adjacent_find(faces.begin(), faces.end()) != faces.end()) return false;
        for (const auto id : faces) if (!id || id >= m.next_face_ids[i]) return false;
        auto next = m.next_face_ids[i];
        for (std::size_t j = 0; j < body_key_.size() && j < face_ids_.size(); ++j)
            if (body_key_[j] == m.body_keys[i]) next = std::max(next, face_ids_[j].next_id());
        ids[i].restore(m.face_ids[i], next);
    }
    body_layer_ = m.body_layers;
    materials_ = m.materials; body_material_ = m.body_material; body_key_ = m.body_keys;
    last_body_key_ = m.last_body_key;
    face_ids_ = std::move(ids); mates_ = m.mates; rollback_ = m.rollback;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < history_[i].size(); ++j) history_[i][j].suppressed = m.suppressed[i][j] != 0;
    invalidate_mesh();
    return true;
}

void Modeller::snapshot() {
    ++edit_serial_;
    history_.resize(bodies_.size());
    body_layer_.resize(bodies_.size(), current_layer_);
    undo_.push_back({bodies_, history_, body_layer_, metadata()});
    redo_.clear();
    if (undo_.size() > 64) undo_.erase(undo_.begin());
}

std::size_t Modeller::extrusion_cap_feature(const Target& target) const {
    if (!target.valid || !target.on_body || target.source >= 0 || target.from_face) return kAllFeatures;
    const auto& features = history(target.body);
    if (features.empty() || rolled_back_to(target.body) != kAllFeatures) return kAllFeatures;
    const auto& feature = features.back();
    if (feature.op != Op::Extrude || !feature.regenerable || feature.suppressed ||
        dot(v3(target.normal),v3(feature.frame.n)) < 1-1e-9 ||
        target.outline.size() != feature.profile.holes.size()+1) return kAllFeatures;
    const double tolerance = vol_.inv_scale()*4;
    const auto same_boundary = [&](const std::vector<Point3>& current, const gtopo::Loop2& original) {
        std::vector<V3> a,b;
        for (const auto& p : current) a.push_back(v3(p));
        for (const auto& p : original) {
            double u,v; gcm::to_world(vol_,p,u,v);
            b.push_back(v3(feature.frame.origin)+v3(feature.frame.u)*u+v3(feature.frame.v)*v+
                        v3(feature.frame.n)*feature.value);
        }
        const auto contains_boundary = [&](const std::vector<V3>& points, const std::vector<V3>& boundary) {
            for (const auto& p : points) {
                double best=1e300;
                for (std::size_t i=0;i<boundary.size();++i) {
                    const auto edge=boundary[(i+1)%boundary.size()]-boundary[i];
                    const auto length2=dot(edge,edge);
                    const double t=length2>0 ? std::clamp(dot(p-boundary[i],edge)/length2,0.0,1.0) : 0;
                    best=std::min(best,len(p-(boundary[i]+edge*t)));
                }
                if (best>tolerance) return false;
            }
            return !points.empty() && !boundary.empty();
        };
        return contains_boundary(a,b) && contains_boundary(b,a);
    };
    if (!same_boundary(target.outline[0],feature.profile.outer)) return kAllFeatures;
    for (std::size_t i=0;i<feature.profile.holes.size();++i)
        if (!same_boundary(target.outline[i+1],feature.profile.holes[i])) return kAllFeatures;
    return features.size()-1;
}

void Modeller::commit(double distance) {
    clear_preview();
    committed_ = false;
    const auto fail = [&](const std::string& reason) {
        message_ = reason;
        value_error_ = reason;
        stage_ = Stage::Placed;
    };
    const auto cap = extrusion_cap_feature(target_);
    if (cap != kAllFeatures) {
        auto features = history(target_.body);
        auto& feature = features[cap];
        feature.value += distance;
        char label[96];
        std::snprintf(label,sizeof(label),"%s %.4g",feature.on_body ?
            (feature.value < 0 ? "Cut" : "Join") : "Extrude",std::fabs(feature.value));
        feature.label=label;
        gtopo::Body result;
        std::string why;
        if (!replay(features,result,why)) { fail(why); return; }
        const auto topology=result.analyse();
        if (!topology.closed || !topology.manifold || !gtopo::all_faces_planar(result)) {
            fail("Could not rebuild a closed solid; change the distance and retry"); return;
        }
        snapshot();
        bodies_[target_.body]=std::move(result);
        history_[target_.body]=std::move(features);
        message_=label;
        typed_.clear(); distance_=0;
        target_=Target{}; stage_=Stage::PickTarget; committed_=true;
        invalidate_mesh();
        return;
    }
    gtopo::Body made = make_body(distance);
    if (made.face_count() == 0) { fail("nothing to extrude"); return; }
    const bool on_body = target_.on_body && target_.body < bodies_.size();
    gtopo::Body result;
    if (on_body) {
        const auto& original = bodies_[target_.body];
        auto boolean = distance >= 0.0 ? gtopo::unite(original, made, vol_)
                                       : gtopo::subtract(original, made, vol_);
        if (!boolean.ok()) { fail(boolean.message()); return; }
        result = gtopo::merge_coplanar_faces(boolean.body, vol_);
        const auto topology = result.analyse();
        if (!topology.closed || !topology.manifold || !gtopo::all_faces_planar(result)) {
            fail("Could not form a closed solid; change the distance and retry");
            return;
        }
        if (distance < 0 && gtopo::volume(original,vol_)-gtopo::volume(result,vol_) < 1e-9) {
            fail("nothing to cut there");
            return;
        }
    } else {
        result = std::move(made);
        const auto topology = result.analyse();
        if (!topology.closed || !topology.manifold || !gtopo::all_faces_planar(result)) {
            fail("Could not form a closed extrusion");
            return;
        }
    }

    snapshot();
    const std::size_t body = on_body ? target_.body : bodies_.size();
    if (on_body) bodies_[body] = std::move(result);
    else bodies_.push_back(std::move(result));
    char label[96];
    if (on_body) std::snprintf(label,sizeof(label),"%s %.4g",distance >= 0 ? "Join" : "Cut",std::fabs(distance));
    else std::snprintf(label,sizeof(label),"Extrude %s",gunit::format_length(std::fabs(distance),gunit::Format{display_unit_}).c_str());
    note(body,label,target_.source);
    Feature& feature = history_[body].back();
    feature.op = Op::Extrude;
    feature.value = distance;
    feature.regenerable = true;
    feature.on_body = on_body;
    feature.profile = target_.profile;
    feature.frame = target_.frame;
    if (target_.source >= 0 && target_.source < static_cast<int>(sources_.size())) {
        const auto& plane = sources_[target_.source].plane;
        plane.to_plane(target_.centroid.x.to_f64(),target_.centroid.y.to_f64(),target_.centroid.z.to_f64(),
                       feature.seed_u,feature.seed_v);
        feature.has_seed = true;
    }
    message_ = label;
    typed_.clear();
    distance_ = 0.0;
    target_ = Target{};
    stage_ = Stage::PickTarget;
    committed_ = true;
    invalidate_mesh();
}

bool Modeller::undo() {
    if (undo_.empty()) return false;
    arm(Op::None);
    ++edit_serial_;
    history_.resize(bodies_.size());
    body_layer_.resize(bodies_.size(), current_layer_);
    redo_.push_back({bodies_, history_, body_layer_, metadata()});
    bodies_ = std::move(undo_.back().bodies);
    history_ = std::move(undo_.back().history);
    body_layer_ = std::move(undo_.back().layers);
    load_metadata(undo_.back().metadata);
    undo_.pop_back();
    target_ = Target{};
    if (stage_ != Stage::Idle) stage_ = Stage::PickTarget;
    invalidate_mesh();
    return true;
}

bool Modeller::redo() {
    if (redo_.empty()) return false;
    arm(Op::None);
    ++edit_serial_;
    history_.resize(bodies_.size());
    body_layer_.resize(bodies_.size(), current_layer_);
    undo_.push_back({bodies_, history_, body_layer_, metadata()});
    bodies_ = std::move(redo_.back().bodies);
    history_ = std::move(redo_.back().history);
    body_layer_ = std::move(redo_.back().layers);
    load_metadata(redo_.back().metadata);
    redo_.pop_back();
    invalidate_mesh();
    return true;
}

void Modeller::clear() {
    if (bodies_.empty()) return;
    snapshot();
    bodies_.clear();
    history_.clear();
    body_layer_.clear();
    invalidate_mesh();
}

void Modeller::wireframe(std::vector<float>& out) const {
    out.clear();
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (body_hidden(i)) continue;
        const gtopo::Body& b = displayed_body(i);
        for (gtopo::u32 h = 0; h < b.halfedge_count(); ++h) {
            const gtopo::u32 tw = b.he_twin[h];
            if (tw != gtopo::kInvalid && tw < h) continue;
            double ax, ay, az, bx, by, bz;
            gcm::to_world(vol_, b.vertex_point[b.he_vertex[h]], ax, ay, az);
            gcm::to_world(vol_, b.vertex_point[b.he_vertex[b.he_next[h]]], bx, by, bz);
            out.push_back(float(ax)); out.push_back(float(ay)); out.push_back(float(az));
            out.push_back(float(bx)); out.push_back(float(by)); out.push_back(float(bz));
        }
    }
}

const std::vector<MeshVertex>& Modeller::mesh() {
    if (!mesh_dirty_) return mesh_;
    ++mesh_version_;
    mesh_dirty_ = false;
    preview_ = {};
    mesh_.clear();
    mesh_ranges_.clear();

    const double cos_smooth = std::cos(smooth_angle_deg_ * 3.14159265358979323846 / 180.0);
    const auto emit = [&](const gtopo::Body& b, const gtopo::TriMesh& m,
                          float r, float g, float bl, float a) {
        std::unordered_map<gtopo::u32, std::vector<std::size_t>> at_vertex;
        if (smooth_shading_) {
            for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
                for (int k = 0; k < 3; ++k) at_vertex[m.indices[i + k]].push_back(i / 3);
        }
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const auto& n = m.normals[i / 3];
            const double nx = n.x.to_f64(), ny = n.y.to_f64(), nz = n.z.to_f64();
            for (int k = 0; k < 3; ++k) {
                double x, y, z;
                gcm::to_world(vol_, b.vertex_point[m.indices[i + k]], x, y, z);
                MeshVertex v{};
                v.pos[0] = float(x); v.pos[1] = float(y); v.pos[2] = float(z);
                double sx = nx, sy = ny, sz = nz;
                if (smooth_shading_) {
                    sx = sy = sz = 0.0;
                    for (std::size_t t : at_vertex[m.indices[i + k]]) {
                        const auto& o = m.normals[t];
                        const double ox = o.x.to_f64(), oy = o.y.to_f64(), oz = o.z.to_f64();
                        if (nx * ox + ny * oy + nz * oz >= cos_smooth) { sx += ox; sy += oy; sz += oz; }
                    }
                    const double len = std::sqrt(sx * sx + sy * sy + sz * sz);
                    if (len > 1e-12) { sx /= len; sy /= len; sz /= len; }
                    else { sx = nx; sy = ny; sz = nz; }
                }
                v.normal[0] = float(sx);
                v.normal[1] = float(sy);
                v.normal[2] = float(sz);
                v.color[0] = r; v.color[1] = g; v.color[2] = bl; v.color[3] = a;
                mesh_.push_back(v);
            }
        }
    };
    const bool previewing = (stage_ == Stage::Dragging || typed_preview_) && std::fabs(distance_) > 1e-9;
    std::vector<MeshVertex> preview_tri;
    if (previewing) {
        float cr = 0.25f, cg = 0.55f, cb = 0.95f;
        if (target_.on_body && target_.body < bodies_.size()) {
            const auto& mat = materials_[std::min<std::size_t>(material_of(target_.body), materials_.size() - 1)];
            cr = mat.r; cg = mat.g; cb = mat.b;
            if (distance_ >= 0.0) {
                const float accent_r = 0.20f, accent_g = 0.45f, accent_b = 0.95f;
                cr = cr * 0.85f + accent_r * 0.15f;
                cg = cg * 0.85f + accent_g * 0.15f;
                cb = cb * 0.85f + accent_b * 0.15f;
            } else {
                cr *= 0.70f;
                cg *= 0.70f;
                cb *= 0.70f;
            }
        } else if (distance_ < 0.0) {
            cr = 0.95f; cg = 0.45f; cb = 0.30f;
        }
        const float color[4] = {cr, cg, cb, 1.0f};
        preview_triangles(color, preview_tri);
    }
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (body_hidden(i)) continue;
        const Material& mat = materials_[std::min<std::size_t>(material_of(i),
                                                              materials_.size() - 1)];
        const std::size_t first = mesh_.size();
        const auto& display=displayed_body(i);
        if(&display!=&bodies_[i]) emit(display,gtopo::triangulate(display,vol_),mat.r,mat.g,mat.b,mat.a);
        else emit(bodies_[i], tri(i), mat.r, mat.g, mat.b, mat.a);
        if (mesh_.size() > first)
            mesh_ranges_.push_back({i, body_name(i), first, mesh_.size() - first,
                                    mat.a < 1.0f});
    }

    if (!value_preview_replaces_) for (const auto& draft:value_preview_) {
        const std::size_t first=mesh_.size();
        emit(draft.shape,gtopo::triangulate(draft.shape,vol_),.35f,.55f,.85f,1.f);
        mesh_ranges_.push_back({bodies_.size(),0,first,mesh_.size()-first,false});
    }

    if (previewing && !preview_tri.empty()) {
        const std::size_t first = mesh_.size();
        for (const auto& p : preview_tri) mesh_.push_back(p);
        mesh_ranges_.push_back({bodies_.size(), 0, first,
                                mesh_.size() - first, false});
        preview_range_first_ = first;
        preview_range_count_ = preview_tri.size();
    } else {
        preview_range_first_ = 0;
        preview_range_count_ = 0;
    }
    return mesh_;
}

static double extent_of(const gtopo::Body& b, const gcm::WorkingVolume& vol) {
    if (b.vertex_point.empty()) return 0.0;
    gcm::Lattice3 lo = b.vertex_point.front(), hi = lo;
    for (const auto& p : b.vertex_point) {
        lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
        lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
        lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
    }
    return std::max({double(hi.x - lo.x), double(hi.y - lo.y), double(hi.z - lo.z)}) * vol.inv_scale();
}

bool Modeller::replay(const std::vector<Feature>& feats, gtopo::Body& out, std::string& why) const {
    gtopo::Body cur;
    bool have = false;
    for (std::size_t i = 0; i < feats.size(); ++i) {
        const Feature& f = feats[i];
        if (f.suppressed) continue;
        if (!f.regenerable) {
            why = "feature " + std::to_string(i + 1) + " (" + f.label + ") cannot be redone from its record";
            return false;
        }
        if (!std::isfinite(f.value)) { why = f.label + ": invalid feature size"; return false; }
        switch (f.op) {
            case Op::Extrude:
            case Op::Revolve: {
                if (f.on_body && !have) { why = f.label + ": no base solid yet"; return false; }
                if (f.op==Op::Extrude && f.on_body && std::fabs(f.value)<1e-9) break;
                if (f.op==Op::Extrude && std::fabs(f.value)<1e-9) {
                    why = f.label + ": the base extrusion needs a nonzero size"; return false;
                }
                gtopo::Profile profile = f.profile;
                const ggeo::d3::Frame<F64> frame = f.frame;
                if (f.has_seed && f.sketch >= 0 && f.sketch < static_cast<int>(sources_.size()) &&
                    sources_[f.sketch].version != 0) {
                    Target t;
                    if (!target_from_region(f.sketch, f.seed_u, f.seed_v, t) || t.outline.empty()) {
                        why = f.label + ": the sketch region it uses is missing or open";
                        return false;
                    }
                    {
                        gtopo::Profile p;
                        const auto project = [&](const std::vector<Point3>& loop) {
                            gtopo::Loop2 out;
                            for (const auto& w : loop) {
                                double fu, fv;
                                to_plane(frame, v3(w), fu, fv);
                                out.push_back(gcm::to_lattice(vol_, fu, fv));
                            }
                            return out;
                        };
                        p.outer = project(t.outline[0]);
                        for (std::size_t k = 1; k < t.outline.size(); ++k) p.holes.push_back(project(t.outline[k]));
                        p.normalize();
                        if (!p.valid()) { why = f.label + ": the sketch region is invalid"; return false; }
                        profile = std::move(p);
                    }
                }
                gtopo::Body made;
                if (f.op == Op::Extrude) {
                    made = body_for(profile, frame, f.value);
                } else {
                    made = gtopo::revolve(profile, vol_, frame, ggeo::d2::Point<F64>::from(f.axis_x, f.axis_y),
                                          ggeo::d2::Vec<F64>::from(f.axis_dx, f.axis_dy), 2.0 * 3.14159265358979323846, 48);
                }
                if (made.face_count() == 0) { why = f.label + ": nothing to make"; return false; }
                if (!have) { cur = std::move(made); have = true; break; }
                if (f.on_body) {
                    gtopo::BoolResult r = f.value >= 0.0 ? gtopo::unite(cur, made, vol_) : gtopo::subtract(cur, made, vol_);
                    if (!r.ok() || r.body.face_count() == 0) { why = f.label + ": the join or cut could not be formed"; return false; }
                    cur = gtopo::merge_coplanar_faces(r.body, vol_);
                } else {
                    gtopo::BoolResult r = gtopo::unite(cur, made, vol_);
                    if (!r.ok() || r.body.face_count() == 0) {
                        why = f.label + ": the join could not be formed"; return false;
                    }
                    cur = std::move(r.body);
                }
                break;
            }
            case Op::FilletEdge:
            case Op::ChamferEdge: {
                if (!have) { why = f.label + ": no solid yet"; return false; }
                const V3 want = v3(f.anchor);
                double best = 1e300;
                gcm::u32 best_he = gtopo::kInvalid;
                for (gcm::u32 h = 0; h < cur.halfedge_count(); ++h) {
                    if (cur.he_twin[h] != gtopo::kInvalid && cur.he_twin[h] < h) continue;
                    double ax, ay, az, bx, by, bz;
                    gcm::to_world(vol_, cur.vertex_point[cur.he_vertex[h]], ax, ay, az);
                    gcm::to_world(vol_, cur.vertex_point[cur.he_vertex[cur.he_next[h]]], bx, by, bz);
                    const V3 m{(ax + bx) * 0.5, (ay + by) * 0.5, (az + bz) * 0.5};
                    const double d = len(m - want);
                    if (d < best) { best = d; best_he = h; }
                }
                if (best_he == gtopo::kInvalid) { why = f.label + ": no edge to act on"; return false; }
                // Bounded search: unbounded would silently move the fillet to another edge.
                if (best > std::max(std::fabs(f.value), extent_of(cur,vol_) * 1e-3) + 1e-9) {
                    why = f.label + ": the edge it was applied to is gone (nearest is " +
                          std::to_string(best) + " away)";
                    return false;
                }
                const gtopo::EditResult r = f.op == Op::FilletEdge
                    ? gtopo::fillet_edge_exact(cur, best_he, f.value, 12, vol_)
                    : gtopo::chamfer_edge(cur, best_he, f.value, f.value, vol_);
                if (!r.ok) { why = f.label + ": " + (r.reason.empty() ? "could not blend that edge" : r.reason); return false; }
                cur = r.body;
                break;
            }
            case Op::ShellBody: {
                if (!have) { why = f.label + ": no solid yet"; return false; }
                const V3 want = v3(f.anchor);
                double best = 1e300;
                gcm::u32 best_face = 0;
                for (gcm::u32 fi = 0; fi < cur.face_count(); ++fi) {
                    V3 c{0, 0, 0};
                    int n = 0;
                    const gcm::u32 l = cur.face_loop_begin(fi);
                    for (gcm::u32 h = cur.loop_begin(l); h < cur.loop_end(l); ++h) {
                        double x, y, z;
                        gcm::to_world(vol_, cur.vertex_point[cur.he_vertex[h]], x, y, z);
                        c = c + V3{x, y, z};
                        ++n;
                    }
                    if (n == 0) continue;
                    c = c * (1.0 / n);
                    const double d = len(c - want);
                    if (d < best) { best = d; best_face = fi; }
                }
                if (best > std::max(std::fabs(f.value), extent_of(cur,vol_) * 1e-3) + 1e-9) {
                    why = f.label + ": the face it was applied to is gone";
                    return false;
                }
                const gtopo::ShellResult r = gtopo::shell_body(cur, f.value, {best_face}, vol_);
                if (!r.ok) { why = f.label + ": " + (r.reason.empty() ? "could not shell" : r.reason); return false; }
                cur = r.body;
                break;
            }
            default:
                why = f.label + ": not a feature that can be redone";
                return false;
        }
        if (have) {
            const auto topology = cur.analyse();
            if (!topology.closed || !topology.manifold ||
                (!cur.has_surfaces() && !gtopo::all_faces_planar(cur))) {
                why = f.label + ": could not rebuild a closed solid";
                return false;
            }
        }
    }
    if (!have) { why = "no features"; return false; }
    out = std::move(cur);
    return true;
}

bool Modeller::rebuild(std::size_t body, std::string& why) {
    if (body >= bodies_.size() || body >= history_.size()) { why = "no such solid"; return false; }
    gtopo::Body made;
    if (!replay(history_[body], made, why)) { rebuild_message_ = why; return false; }
    const auto hist = history_[body];
    snapshot();
    bodies_[body] = std::move(made);
    history_[body] = hist;
    target_ = Target{};
    hover_ = Target{};
    invalidate_mesh();
    rebuild_message_ = "rebuilt";
    return true;
}

bool Modeller::set_feature_value(std::size_t body, std::size_t feature, double value) {
    if (body >= bodies_.size() || body >= history_.size() || feature >= history_[body].size()) return false;
    if (!std::isfinite(value)) { rebuild_message_ = "Enter a finite feature size"; return false; }
    auto features = history_[body];
    Feature& f = features[feature];
    if (!f.regenerable) { rebuild_message_ = f.label + " cannot be changed: it is a record, not a recipe"; return false; }
    const double was = f.value;
    f.value = (f.op == Op::Extrude && was < 0.0) ? -std::fabs(value) : value;
    const std::size_t sp = f.label.find(' ');
    char buf[64];
    std::snprintf(buf, sizeof(buf), " %.4g", std::fabs(f.value));
    f.label = (sp == std::string::npos ? f.label : f.label.substr(0, sp)) + buf;
    gtopo::Body made;
    std::string why;
    if (!replay(features, made, why)) { rebuild_message_ = why; message_ = why; return false; }
    message_ = "rebuilt with " + f.label;
    snapshot();
    bodies_[body] = std::move(made);
    history_[body] = std::move(features);
    target_ = Target{};
    hover_ = Target{};
    invalidate_mesh();
    rebuild_message_ = "rebuilt";
    return true;
}

bool Modeller::remove_feature(std::size_t body, std::size_t feature) {
    if (body >= history_.size() || feature >= history_[body].size()) return false;
    std::vector<Feature> feats = history_[body];
    const std::string gone = feats[feature].label;
    feats.erase(feats.begin() + static_cast<std::ptrdiff_t>(feature));
    if (feats.empty() || feature == 0) {
        erase(body);
        message_ = "removed " + gone + ", and the solid with it";
        return true;
    }
    gtopo::Body made;
    std::string why;
    if (!replay(feats, made, why)) { rebuild_message_ = why; message_ = why; return false; }
    snapshot();
    bodies_[body] = std::move(made);
    history_[body] = feats;
    target_ = Target{};
    hover_ = Target{};
    invalidate_mesh();
    message_ = "removed " + gone;
    return true;
}

bool Modeller::rollback(std::size_t body, std::size_t through, std::string& why) {
    if (body >= bodies_.size() || body >= history_.size()) { why = "no such solid"; return false; }
    if (rollback_.size() < history_.size()) rollback_.resize(history_.size(), kAllFeatures);
    const std::size_t total = history_[body].size();
    const std::size_t want = std::min(through, total);

    std::vector<Feature> upto(history_[body].begin(),
                              history_[body].begin() + std::ptrdiff_t(want));
    gtopo::Body made;
    if (want > 0 && !replay(upto, made, why)) { rebuild_message_ = why; return false; }
    const auto hist = history_[body];
    snapshot();
    bodies_[body] = std::move(made);
    history_[body] = hist;
    rollback_[body] = want >= total ? kAllFeatures : want;
    target_ = Target{};
    hover_ = Target{};
    invalidate_mesh();
    rebuild_message_ = want >= total ? "rolled forward"
                                     : "rolled back to feature " + std::to_string(want);
    return true;
}

bool Modeller::suppress(std::size_t body, std::size_t feature, bool on, std::string& why) {
    if (body >= bodies_.size() || body >= history_.size() || feature >= history_[body].size()) {
        why = "no such feature";
        return false;
    }
    if (history_[body][feature].suppressed == on) return true;
    auto features = history_[body];
    features[feature].suppressed = on;
    bool any = false;
    for (const auto& f : features) any = any || !f.suppressed;
    gtopo::Body made;
    if (any && !replay(features, made, why)) { rebuild_message_ = why; return false; }
    snapshot();
    bodies_[body] = std::move(made);
    history_[body] = std::move(features);
    target_ = Target{};
    hover_ = Target{};
    invalidate_mesh();
    rebuild_message_ = history_[body][feature].label +
        (on ? (any ? " suppressed" : " suppressed -- nothing left to build") : " unsuppressed");
    return true;
}

bool Modeller::reorder(std::size_t body, std::size_t from, std::size_t to, std::string& why) {
    if (body >= bodies_.size() || body >= history_.size()) { why = "no such solid"; return false; }
    auto h = history_[body];
    if (from >= h.size() || to >= h.size()) { why = "no such feature"; return false; }
    if (from == to) return true;
    Feature moved = h[from];
    h.erase(h.begin() + std::ptrdiff_t(from));
    h.insert(h.begin() + std::ptrdiff_t(to), std::move(moved));
    gtopo::Body made;
    if (!replay(h, made, why)) { rebuild_message_ = why; return false; }
    snapshot();
    bodies_[body] = std::move(made);
    history_[body] = std::move(h);
    target_ = Target{};
    hover_ = Target{};
    invalidate_mesh();
    rebuild_message_ = "feature moved";
    return true;
}

std::size_t Modeller::regenerate_from_sketch(int sketch) {
    std::size_t n = 0;
    for (std::size_t b = 0; b < history_.size() && b < bodies_.size(); ++b) {
        bool uses = false;
        for (const auto& f : history_[b]) if (f.sketch == sketch && f.regenerable && f.has_seed) uses = true;
        if (!uses) continue;
        std::string why;
        if (rebuild(b, why)) ++n;
    }
    return n;
}

void Modeller::arrow(const gview::Camera& cam, Point3& base, Point3& tip) const {
    const V3 c = v3(target_.centroid), n = unit(v3(target_.normal));
    const V3 b = c + n * distance_;
    double px0, py0, px1, py1;
    double world_per_px = 0.01;
    if (cam.project(P(b), px0, py0) && cam.project(P(b + n), px1, py1)) {
        const double px_per_unit = std::hypot(px1 - px0, py1 - py0);
        if (px_per_unit > 1e-9) world_per_px = 1.0 / px_per_unit;
    }
    base = P(b);
    tip = P(b + n * (60.0 * world_per_px));
}

bool Modeller::arrow_hit(const gview::Camera& cam, double px, double py) const {
    Point3 b, t;
    arrow(cam, b, t);
    double bx, by, tx, ty;
    if (!cam.project(b, bx, by) || !cam.project(t, tx, ty)) return false;
    const double dx = tx - bx, dy = ty - by;
    const double l2 = dx * dx + dy * dy;
    double u = l2 > 1e-9 ? ((px - bx) * dx + (py - by) * dy) / l2 : 0.0;
    u = std::clamp(u, 0.0, 1.0);
    const double cx = bx + dx * u, cy = by + dy * u;
    return std::hypot(px - cx, py - cy) <= 16.0;
}

void Modeller::overlay(const gview::Camera& cam, const float highlight[4],
                       const float accent[4], std::vector<LineVertex>& lines) const {
    const auto seg = [&](const Point3& a, const Point3& b, const float* c) {
        lines.push_back({{float(a.x.to_f64()), float(a.y.to_f64()), float(a.z.to_f64())},
                         {c[0], c[1], c[2], c[3]}});
        lines.push_back({{float(b.x.to_f64()), float(b.y.to_f64()), float(b.z.to_f64())},
                         {c[0], c[1], c[2], c[3]}});
    };
    const auto outline = [&](const Target& t, const float* c) {
        for (const auto& loop : t.outline)
            for (std::size_t i = 0; i < loop.size(); ++i)
                seg(loop[i], loop[(i + 1) % loop.size()], c);
    };

    if (stage_ == Stage::PickTarget || stage_ == Stage::Placed || stage_ == Stage::PickSecond)
        if (hover_.valid) outline(hover_, highlight);

    if (stage_ == Stage::PickAxis) {
        outline(target_, accent);
        if (axis_valid_) {
            const V3 a = v3(axis_a_), b = v3(axis_b_);
            const V3 dir = unit(b - a);
            const double ext = len(b - a) * 1.5 + 1.0;
            seg(P(a - dir * ext), P(b + dir * ext), highlight);
            seg(axis_a_, axis_b_, accent);
        }
    }
    if (stage_ == Stage::PickSecond) outline(target_, accent);
    if (measure_valid_) {
        seg(measure_a_, measure_b_, accent);
    } else if (measure_first_) {
        const V3 a = v3(measure_a_);
        seg(P(a - V3{0.05, 0, 0}), P(a + V3{0.05, 0, 0}), accent);
        seg(P(a - V3{0, 0.05, 0}), P(a + V3{0, 0.05, 0}), accent);
    }
    if (path_valid_ && stage_ == Stage::PickSecond && op_ == Op::Sweep &&
        path_source_ >= 0 && path_source_ < static_cast<int>(sources_.size())) {
        const auto& pl = sources_[path_source_].plane;
        double ax, ay, bx, by, wx, wy, wz;
        gcm::to_world(vol_, path_seg_.a, ax, ay);
        gcm::to_world(vol_, path_seg_.b, bx, by);
        pl.to_world(ax, ay, wx, wy, wz);
        const Point3 A = Point3::from(wx, wy, wz);
        pl.to_world(bx, by, wx, wy, wz);
        seg(A, Point3::from(wx, wy, wz), accent);
    }

    if (stage_ == Stage::Placed || stage_ == Stage::Dragging) {
        outline(target_, accent);
    }
}

bool Modeller::gizmo_label(const gview::Camera& cam, double& px, double& py,
                           std::string& text) const {
    if (measure_valid_) {
        const V3 m = (v3(measure_a_) + v3(measure_b_)) * 0.5;
        if (!cam.project(P(m), px, py)) return false;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", measure_value_);
        text = buf;
        return true;
    }
    if (stage_ == Stage::TypeValue && target_.valid && !edge_valid_) {
        if (!cam.project(target_.centroid, px, py)) return false;
        text = typed_.empty() ? std::string(op_ == Op::ShellBody ? "thickness?" : "count spacing?") : typed_;
        return true;
    }
    if (stage_ == Stage::TypeValue && edge_valid_) {
        const V3 m = (v3(edge_a_) + v3(edge_b_)) * 0.5;
        if (!cam.project(P(m), px, py)) return false;
        text = typed_.empty() ? std::string(op_ == Op::FilletEdge ? "radius?" : "setback?") : typed_;
        return true;
    }
    if (stage_ != Stage::Placed && stage_ != Stage::Dragging) return false;
    Point3 b, t;
    arrow(cam, b, t);
    if (!cam.project(t, px, py)) return false;
    char buf[64];
    if (!typed_.empty()) std::snprintf(buf, sizeof(buf), "%s", typed_.c_str());
    else std::snprintf(buf, sizeof(buf), "%.3f", distance_);
    text = buf;
    return true;
}

}
