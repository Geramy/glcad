#include <algorithm>
#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/ssi.hpp"

namespace gtopo {
namespace {

constexpr f64 kEps = 1e-12;

struct W { f64 x, y, z; };

W world_of(const gcm::WorkingVolume& v, const gcm::Lattice3& p) {
    f64 x, y, z;
    gcm::to_world(v, p, x, y, z);
    return {x, y, z};
}

// Either orientation matches: subtraction reverses the tool's faces.
bool same_plane(const Plane& a, const Plane& b, f64 angle_tol, f64 offset_tol) {
    const f64 c = a.nx * b.nx + a.ny * b.ny + a.nz * b.nz;
    if (c >= 1.0 - angle_tol) return std::fabs(a.d - b.d) <= offset_tol;
    if (c <= -1.0 + angle_tol) return std::fabs(a.d + b.d) <= offset_tol;
    return false;
}

}

CurvedBoolResult boolean_curved(const Body& a, const Body& b, BoolOp op,
                                const gcm::WorkingVolume& vol,
                                const CurvedBoolSettings& s) {
    CurvedBoolResult out;
    out.chord_tolerance = s.chord_tolerance;

    for (u32 f = 0; f < a.face_count(); ++f) if (a.face_is_curved(f)) ++out.faceted;
    for (u32 f = 0; f < b.face_count(); ++f) if (b.face_is_curved(f)) ++out.faceted;

    const BoolResult planar = boolean_op(a, b, op, vol);
    out.error = planar.error;
    out.body = planar.body;
    if (!planar.ok()) return out;

    if (!s.preserve_surfaces && !s.refine_seams) return out;

    struct Known {
        Plane   plane;
        Surface surface;
    };
    std::vector<Known> known;
    std::vector<Surface> surfaces;

    auto collect = [&](const Body& body) {
        if (!body.has_surfaces()) return;
        for (u32 f = 0; f < body.face_count(); ++f) {
            if (!body.face_is_curved(f)) continue;
            const Plane p = face_plane(body, f, vol);
            if (!p.valid()) continue;
            const Surface* sf = body.surface_of(f);
            if (!sf) continue;
            known.push_back({p, *sf});

            bool seen = false;
            for (const Surface& existing : surfaces)
                if (existing.same_as(*sf, 1e-9)) seen = true;
            if (!seen) surfaces.push_back(*sf);
        }
    };
    collect(a);
    collect(b);

    if (known.empty()) return out;

    Body& result = out.body;

    if (s.preserve_surfaces) {
        result.face_surface.assign(result.face_count(), Surface{});
        const f64 offset_tol = std::max(s.chord_tolerance, 1.0 / vol.scale()) * 4.0;

        for (u32 f = 0; f < result.face_count(); ++f) {
            const Plane p = face_plane(result, f, vol);
            if (!p.valid()) continue;
            for (const Known& k : known) {
                if (!same_plane(p, k.plane, 1e-6, offset_tol)) continue;
                result.face_surface[f] = k.surface;
                ++out.preserved;
                break;
            }
        }
        if (out.preserved == 0) result.face_surface.clear();
    }

    if (s.refine_seams && surfaces.size() >= 1) {
        // Facetting chord height plus one lattice unit of slack.
        const f64 on_surface = std::max(s.chord_tolerance, 1.0 / vol.scale()) * 4.0;

        for (u32 i = 0; i < result.vertex_count(); ++i) {
            const W w = world_of(vol, result.vertex_point[i]);

            const Surface* first = nullptr;
            const Surface* second = nullptr;
            for (const Surface& sf : surfaces) {
                if (sf.distance_to(w.x, w.y, w.z) > on_surface) continue;
                if (!first) first = &sf;
                else if (!second && !first->same_as(sf, 1e-9)) second = &sf;
            }
            if (!first || !second) continue;

            f64 px, py, pz;
            if (!project_onto_both(*first, *second, w.x, w.y, w.z, px, py, pz))
                continue;

            const f64 moved = std::sqrt((px - w.x) * (px - w.x) +
                                        (py - w.y) * (py - w.y) +
                                        (pz - w.z) * (pz - w.z));
            if (moved > on_surface * 2.0) continue;

            const gcm::Lattice3 snapped = gcm::to_lattice(vol, px, py, pz);
            if (snapped.x == result.vertex_point[i].x &&
                snapped.y == result.vertex_point[i].y &&
                snapped.z == result.vertex_point[i].z)
                continue;
            result.vertex_point[i] = snapped;
            ++out.refined;
        }
    }

    return out;
}

bool has_curved_faces(const Body& b) noexcept {
    return b.any_face_curved();
}

const char* CurvedBoolResult::message() const noexcept {
    BoolResult r;
    r.error = error;
    return r.message();
}

}
