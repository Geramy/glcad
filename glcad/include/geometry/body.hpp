#pragma once

#include <vector>

#include "math/fixed.hpp"
#include "geometry/d3.hpp"
#include "geometry/surface.hpp"

namespace gtopo {

using gcm::f64;
using gcm::i64;
using gcm::u32;

using VertexId   = u32;
using HalfedgeId = u32;
using LoopId     = u32;
using FaceId     = u32;

inline constexpr u32 kInvalid = 0xFFFFFFFFu;

struct Topology {
    u32  vertices{0};
    u32  edges{0};
    u32  faces{0};
    u32  loops{0};
    u32  shells{0};
    i64  genus{0};          // number of through-holes
    bool closed{false};
    bool manifold{false};

    // Euler-Poincare: V - E + 2F - L = 2(S - G)
    i64 euler() const noexcept {
        return static_cast<i64>(vertices) - static_cast<i64>(edges) +
               2 * static_cast<i64>(faces) - static_cast<i64>(loops);
    }
};

class Body {
public:
    std::vector<gcm::Lattice3> vertex_point;

    std::vector<u32> he_vertex;
    std::vector<u32> he_twin;     // opposite halfedge, or kInvalid on a boundary
    std::vector<u32> he_next;
    std::vector<u32> he_prev;
    std::vector<u32> he_loop;

    std::vector<u32> loop_he_offset;   // size loops + 1
    std::vector<u32> loop_face;

    // Empty (all faces planar) or exactly face_count() long.
    std::vector<Surface> face_surface;

    bool has_surfaces() const noexcept { return !face_surface.empty(); }
    const Surface* surface_of(FaceId f) const noexcept {
        return f < face_surface.size() ? &face_surface[f] : nullptr;
    }
    bool face_is_curved(FaceId f) const noexcept {
        const Surface* s = surface_of(f);
        return s && s->kind != SurfaceKind::Plane;
    }
    bool any_face_curved() const noexcept {
        for (u32 f = 0; f < face_surface.size(); ++f)
            if (face_is_curved(f)) return true;
        return false;
    }

    // Loop 0 of a face is the outer boundary; the rest are holes.
    std::vector<u32> face_loop_offset;  // size faces + 1

    u32 vertex_count() const noexcept { return static_cast<u32>(vertex_point.size()); }
    u32 halfedge_count() const noexcept { return static_cast<u32>(he_vertex.size()); }
    u32 loop_count() const noexcept {
        return loop_he_offset.empty() ? 0u : static_cast<u32>(loop_he_offset.size() - 1);
    }
    u32 face_count() const noexcept {
        return face_loop_offset.empty() ? 0u : static_cast<u32>(face_loop_offset.size() - 1);
    }

    u32 loop_begin(LoopId l) const noexcept { return loop_he_offset[l]; }
    u32 loop_end(LoopId l) const noexcept { return loop_he_offset[l + 1]; }
    u32 loop_size(LoopId l) const noexcept { return loop_end(l) - loop_begin(l); }

    u32 face_loop_begin(FaceId f) const noexcept { return face_loop_offset[f]; }
    u32 face_loop_end(FaceId f) const noexcept { return face_loop_offset[f + 1]; }
    u32 face_loop_count(FaceId f) const noexcept {
        return face_loop_end(f) - face_loop_begin(f);
    }

    Topology analyse() const;

    ggeo::d3::AABB<gcm::F64> bounds(const gcm::WorkingVolume& v) const;
};

class Builder {
public:
    VertexId add_vertex(gcm::Lattice3 p);

    void begin_face();
    void add_loop(const std::vector<VertexId>& loop);   // first = outer
    void end_face();

    Body build();

    u32 face_count() const noexcept { return static_cast<u32>(faces_.size()); }

private:
    struct Loop { std::vector<VertexId> v; };
    struct Face { std::vector<Loop> loops; };

    std::vector<gcm::Lattice3> points_;
    std::vector<Face>          faces_;
    Face                       current_{};
    bool                       in_face_{false};
};

}
