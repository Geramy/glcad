#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <string>

#include "geometry/boolean.hpp"
#include "math/predicate.hpp"
#include "face_area.hpp"

namespace gtopo {

namespace {

struct V { double x, y, z; };
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double len(V a) { return std::sqrt(dot(a, a)); }

Body remove_redundant_edge_vertices(const Body& in) {
    std::vector<std::vector<u32>> neighbours(in.vertex_count());
    for (u32 h=0;h<in.halfedge_count();++h) {
        const auto a=in.he_vertex[h], b=in.he_vertex[in.he_next[h]];
        auto& adjacent=neighbours[a];
        if (std::find(adjacent.begin(),adjacent.end(),b)==adjacent.end()) adjacent.push_back(b);
        auto& reverse=neighbours[b];
        if (std::find(reverse.begin(),reverse.end(),a)==reverse.end()) reverse.push_back(a);
    }
    std::vector<bool> remove(in.vertex_count(),false);
    bool any=false;
    for (u32 id=0;id<in.vertex_count();++id) {
        if (neighbours[id].size()!=2) continue;
        const auto& a=in.vertex_point[neighbours[id][0]];
        const auto& b=in.vertex_point[id];
        const auto& c=in.vertex_point[neighbours[id][1]];
        using P=gcm::predicate::Host;
        if (!P::collinear({a.x,a.y},{b.x,b.y},{c.x,c.y}) ||
            !P::collinear({a.x,a.z},{b.x,b.z},{c.x,c.z}) ||
            !P::collinear({a.y,a.z},{b.y,b.z},{c.y,c.z})) continue;
        if (b.x<std::min(a.x,c.x) || b.x>std::max(a.x,c.x) ||
            b.y<std::min(a.y,c.y) || b.y>std::max(a.y,c.y) ||
            b.z<std::min(a.z,c.z) || b.z>std::max(a.z,c.z)) continue;
        remove[id]=true; any=true;
    }
    if (!any) return in;
    Builder builder;
    std::vector<u32> remap(in.vertex_count(),kInvalid);
    for (u32 id=0;id<in.vertex_count();++id)
        if (!remove[id]) remap[id]=builder.add_vertex(in.vertex_point[id]);
    for (u32 f=0;f<in.face_count();++f) {
        builder.begin_face();
        for (u32 l=in.face_loop_begin(f);l<in.face_loop_end(f);++l) {
            std::vector<u32> loop;
            for (u32 h=in.loop_begin(l);h<in.loop_end(l);++h)
                if (!remove[in.he_vertex[h]]) loop.push_back(remap[in.he_vertex[h]]);
            if (loop.size()<3) return in;
            builder.add_loop(loop);
        }
        builder.end_face();
    }
    auto result=builder.build();
    result.face_surface=in.face_surface;
    const auto was=in.analyse(), now=result.analyse();
    if ((was.closed && !now.closed) || (was.manifold && !now.manifold)) return in;
    return result;
}

}

Body merge_coplanar_faces(const Body& in, const gcm::WorkingVolume& vol,
                         std::vector<FaceId>* face_remap) {
    const u32 nf = in.face_count();
    if (face_remap) {
        face_remap->resize(nf);
        for (u32 f = 0; f < nf; ++f) (*face_remap)[f] = f;
    }
    if (nf < 2) return in;

    std::vector<V> normal(nf, V{0, 0, 0});
    std::vector<std::array<gcm::Sign,3>> winding(nf);
    std::vector<bool> planar(nf, true);
    std::vector<std::array<gcm::Lattice3, 3>> exact_plane(nf);
    const auto world = [&](u32 vtx) {
        double x, y, z;
        gcm::to_world(vol, in.vertex_point[vtx], x, y, z);
        return V{x, y, z};
    };
    for (u32 f = 0; f < nf; ++f) {
        if (in.has_surfaces() && in.face_is_curved(f)) { planar[f] = false; continue; }
        const u32 l = in.face_loop_begin(f);
        const u32 start = in.loop_begin(l), end = in.loop_end(l);
        if (end-start<3) {planar[f]=false;continue;}
        const auto a = in.vertex_point[in.he_vertex[start]];
        const auto area=detail::loop_area(in,l);
        for (int k=0;k<3;++k) winding[f][k]=area[k].sign();
        const V n{detail::approximate_area(area[0]),detail::approximate_area(area[1]),detail::approximate_area(area[2])};
        if (len(n)==0) {planar[f]=false;continue;}
        normal[f]=n*(1.0/len(n));
        bool found = false;
        for (u32 h = start + 1; h + 1 < end && !found; ++h) {
            const auto b = in.vertex_point[in.he_vertex[h]];
            const auto c = in.vertex_point[in.he_vertex[h + 1]];
            using P = gcm::predicate::Host;
            if (P::collinear({a.x,a.y},{b.x,b.y},{c.x,c.y}) &&
                P::collinear({a.x,a.z},{b.x,b.z},{c.x,c.z}) &&
                P::collinear({a.y,a.z},{b.y,b.z},{c.y,c.z})) continue;
            exact_plane[f] = {a,b,c};
            found = true;
        }
        planar[f] = found;
        if (found) {
            const auto& p = exact_plane[f];
            for (u32 li = in.face_loop_begin(f); li < in.face_loop_end(f); ++li)
                for (u32 h = in.loop_begin(li); h < in.loop_end(li); ++h)
                    if (!gcm::predicate::coplanar(p[0],p[1],p[2],in.vertex_point[in.he_vertex[h]]))
                        planar[f] = false;
        }
    }

    std::vector<u32> group(nf);
    for (u32 f = 0; f < nf; ++f) group[f] = f;
    const auto find = [&](u32 f) { while (group[f] != f) { group[f] = group[group[f]]; f = group[f]; } return f; };
    std::vector<u32> face_of_loop(in.loop_count());
    for (u32 f = 0; f < nf; ++f)
        for (u32 l = in.face_loop_begin(f); l < in.face_loop_end(f); ++l) face_of_loop[l] = f;
    std::vector<u32> loop_of_he(in.halfedge_count());
    for (u32 l = 0; l < in.loop_count(); ++l)
        for (u32 h = in.loop_begin(l); h < in.loop_end(l); ++h) loop_of_he[h] = l;
    bool any = false;
    for (u32 h = 0; h < in.halfedge_count(); ++h) {
        const u32 tw = in.he_twin[h];
        if (tw == kInvalid || tw < h) continue;
        const u32 fa = face_of_loop[loop_of_he[h]], fb = face_of_loop[loop_of_he[tw]];
        if (fa == fb || !planar[fa] || !planar[fb]) continue;
        const auto& pa = exact_plane[fa];
        const auto& pb = exact_plane[fb];
        if (!std::all_of(pb.begin(), pb.end(), [&](const auto& p) {
            return gcm::predicate::coplanar(pa[0],pa[1],pa[2],p);
        })) continue;
        // Thin fragments make float planes ill-conditioned; use exact coplanarity.
        int axis=0;
        while (axis<2 && winding[fa][axis]==gcm::Sign::Zero) ++axis;
        if (winding[fa][axis]==gcm::Sign::Zero || winding[fa][axis]!=winding[fb][axis]) continue;
        const u32 ra = find(fa), rb = find(fb);
        if (ra != rb) { group[ra] = rb; any = true; }
    }
    if (!any) return remove_redundant_edge_vertices(in);

    Builder out;
    std::vector<VertexId> vid(in.vertex_count(), kInvalid);
    const auto id_of = [&](u32 v) {
        if (vid[v] == kInvalid) vid[v] = out.add_vertex(in.vertex_point[v]);
        return vid[v];
    };
    std::vector<Surface> surfaces;
    std::vector<FaceId> remap(nf, kInvalid);
    FaceId next_face = 0;
    std::vector<bool> done(nf, false);
    for (u32 f = 0; f < nf; ++f) {
        if (done[f]) continue;
        const u32 root = find(f);
        std::vector<u32> members;
        for (u32 g = 0; g < nf; ++g) if (find(g) == root) members.push_back(g);
        for (u32 g : members) done[g] = true;
        if (members.size() == 1) {
            remap[f] = next_face++;
            out.begin_face();
            for (u32 l = in.face_loop_begin(f); l < in.face_loop_end(f); ++l) {
                std::vector<VertexId> loop;
                for (u32 h = in.loop_begin(l); h < in.loop_end(l); ++h) loop.push_back(id_of(in.he_vertex[h]));
                out.add_loop(loop);
            }
            out.end_face();
            if (in.has_surfaces()) surfaces.push_back(in.face_surface[f]);
            continue;
        }
        std::vector<bool> in_group(nf, false);
        for (u32 g : members) in_group[g] = true;
        std::unordered_multimap<u32, u32> from;
        std::vector<u32> boundary;
        for (u32 g : members)
            for (u32 l = in.face_loop_begin(g); l < in.face_loop_end(g); ++l)
                for (u32 h = in.loop_begin(l); h < in.loop_end(l); ++h) {
                    const u32 tw = in.he_twin[h];
                    if (tw != kInvalid && in_group[face_of_loop[loop_of_he[tw]]]) continue;
                    boundary.push_back(h);
                    from.emplace(in.he_vertex[h], h);
                }
        std::vector<bool> used(in.halfedge_count(), false);
        std::vector<std::vector<u32>> loops;
        for (u32 start : boundary) {
            if (used[start]) continue;
            std::vector<u32> loop;
            u32 h = start;
            for (int guard = 0; guard < 100000; ++guard) {
                used[h] = true;
                loop.push_back(in.he_vertex[h]);
                const u32 next_v = in.he_vertex[in.he_next[h]];
                u32 next_h = kInvalid;
                auto range = from.equal_range(next_v);
                for (auto it = range.first; it != range.second; ++it)
                    if (!used[it->second]) { next_h = it->second; break; }
                if (next_h == kInvalid) break;
                if (next_h == start) break;
                h = next_h;
            }
            if (loop.size() >= 3) loops.push_back(std::move(loop));
        }
        // Keep boundary subdivision vertices: the neighbour may use both segments.
        if (loops.empty()) {
            for (u32 g : members) {
                remap[g] = next_face++;
                out.begin_face();
                for (u32 l = in.face_loop_begin(g); l < in.face_loop_end(g); ++l) {
                    std::vector<VertexId> loop;
                    for (u32 h = in.loop_begin(l); h < in.loop_end(l); ++h) loop.push_back(id_of(in.he_vertex[h]));
                    out.add_loop(loop);
                }
                out.end_face();
                if (in.has_surfaces()) surfaces.push_back(in.face_surface[g]);
            }
            continue;
        }
        const V n = normal[f];
        std::vector<std::pair<double, std::size_t>> order;
        for (std::size_t i = 0; i < loops.size(); ++i) {
            V a{0, 0, 0};
            const V p0 = world(loops[i][0]);
            for (std::size_t k = 1; k + 1 < loops[i].size(); ++k)
                a = a + cross(world(loops[i][k]) - p0, world(loops[i][k + 1]) - p0);
            order.push_back({-std::fabs(dot(a, n)), i});
        }
        std::sort(order.begin(), order.end());
        for (u32 g : members) remap[g] = next_face;
        ++next_face;
        out.begin_face();
        for (const auto& [neg, i] : order) {
            std::vector<VertexId> loop;
            for (u32 v : loops[i]) loop.push_back(id_of(v));
            out.add_loop(loop);
        }
        out.end_face();
        if (in.has_surfaces()) surfaces.push_back(in.face_surface[f]);
    }
    Body merged = out.build();
    if (in.has_surfaces() && surfaces.size() == merged.face_count()) merged.face_surface = std::move(surfaces);
    const Topology t = merged.analyse();
    const Topology t0 = in.analyse();
    if ((t0.closed && !t.closed) || (t0.manifold && !t.manifold)) return in;
    if (all_faces_planar(in) && !all_faces_planar(merged)) return remove_redundant_edge_vertices(in);
    if (std::fabs(volume(merged, vol) - volume(in, vol)) > 1e-9 * std::max(1.0, std::fabs(volume(in, vol)))) return in;
    if (face_remap) *face_remap = std::move(remap);
    return remove_redundant_edge_vertices(merged);
}

}
