#include "geometry/body.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>

namespace gtopo {

VertexId Builder::add_vertex(gcm::Lattice3 p) {
    points_.push_back(p);
    return static_cast<VertexId>(points_.size() - 1);
}

void Builder::begin_face() {
    current_ = Face{};
    in_face_ = true;
}

void Builder::add_loop(const std::vector<VertexId>& loop) {
    if (!in_face_ || loop.size() < 3) return;
    current_.loops.push_back(Loop{loop});
}

void Builder::end_face() {
    if (in_face_ && !current_.loops.empty()) faces_.push_back(std::move(current_));
    in_face_ = false;
}

Body Builder::build() {
    Body b;
    b.vertex_point = points_;

    b.face_loop_offset.push_back(0);
    b.loop_he_offset.push_back(0);

    for (u32 fi = 0; fi < faces_.size(); ++fi) {
        for (const auto& lp : faces_[fi].loops) {
            const u32 loop_id = static_cast<u32>(b.loop_he_offset.size() - 1);
            const u32 start   = static_cast<u32>(b.he_vertex.size());
            const u32 n       = static_cast<u32>(lp.v.size());

            for (u32 i = 0; i < n; ++i) {
                b.he_vertex.push_back(lp.v[i]);
                b.he_twin.push_back(kInvalid);
                b.he_next.push_back(start + (i + 1) % n);
                b.he_prev.push_back(start + (i + n - 1) % n);
                b.he_loop.push_back(loop_id);
            }
            b.loop_he_offset.push_back(static_cast<u32>(b.he_vertex.size()));
            b.loop_face.push_back(fi);
        }
        b.face_loop_offset.push_back(static_cast<u32>(b.loop_he_offset.size() - 1));
    }

    // Keep every copy of a directed edge: coincident doubled faces each need a twin.
    std::multimap<uint64_t, u32> dir;
    auto key = [](u32 a, u32 c) { return (static_cast<uint64_t>(a) << 32) | c; };

    for (u32 h = 0; h < b.he_vertex.size(); ++h) {
        const u32 a = b.he_vertex[h];
        const u32 c = b.he_vertex[b.he_next[h]];
        dir.emplace(key(a, c), h);
    }
    for (u32 h = 0; h < b.he_vertex.size(); ++h) {
        const u32 a = b.he_vertex[h];
        const u32 c = b.he_vertex[b.he_next[h]];
        const auto range = dir.equal_range(key(c, a));
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == h) continue;
            b.he_twin[h] = it->second;
            it = dir.erase(it);
            break;
        }
    }
    return b;
}

Topology Body::analyse() const {
    Topology t;
    t.vertices = vertex_count();
    t.faces    = face_count();
    t.loops    = loop_count();

    u32 twinned = 0, boundary = 0;
    bool closed = true;
    for (u32 h = 0; h < halfedge_count(); ++h) {
        if (he_twin[h] == kInvalid) { ++boundary; closed = false; }
        else                        ++twinned;
    }
    t.edges  = twinned / 2 + boundary;
    t.closed = closed;

    bool manifold = true;
    for (u32 h = 0; h < halfedge_count(); ++h) {
        const u32 tw = he_twin[h];
        if (tw != kInvalid && he_twin[tw] != h) { manifold = false; break; }
    }
    t.manifold = manifold;

    std::vector<u32> parent(t.faces);
    for (u32 i = 0; i < t.faces; ++i) parent[i] = i;
    std::function<u32(u32)> find = [&](u32 x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (u32 h = 0; h < halfedge_count(); ++h) {
        const u32 tw = he_twin[h];
        if (tw == kInvalid) continue;
        const u32 fa = find(loop_face[he_loop[h]]);
        const u32 fb = find(loop_face[he_loop[tw]]);
        if (fa != fb) parent[fa] = fb;
    }
    u32 shells = 0;
    for (u32 i = 0; i < t.faces; ++i) if (find(i) == i) ++shells;
    t.shells = shells ? shells : 1;

    // Euler-Poincare:  V - E + 2F - L = 2(S - G)
    t.genus = static_cast<i64>(t.shells) - t.euler() / 2;
    return t;
}

ggeo::d3::AABB<gcm::F64> Body::bounds(const gcm::WorkingVolume& v) const {
    auto box = ggeo::d3::AABB<gcm::F64>::empty();
    for (const auto& p : vertex_point) {
        f64 x, y, z;
        gcm::to_world(v, p, x, y, z);
        box.expand(ggeo::d3::Point<gcm::F64>::from(x, y, z));
    }
    return box;
}

}
