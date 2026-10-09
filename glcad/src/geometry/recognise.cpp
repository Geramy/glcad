#include "geometry/recognise.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "geometry/measure.hpp"

namespace gtopo {

namespace {

struct P3 { f64 x, y, z; };

P3 world(const Volume& v, gcm::Lattice3 p) {
    f64 x, y, z;
    gcm::to_world(v, p, x, y, z);
    return {x, y, z};
}

f64 norm3(f64 x, f64 y, f64 z) { return std::sqrt(x * x + y * y + z * z); }

std::vector<FaceId> neighbours(const Body& b, FaceId f) {
    std::vector<FaceId> out;
    for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li)
        for (u32 h = b.loop_begin(li); h < b.loop_end(li); ++h) {
            const u32 twin = b.he_twin[h];
            if (twin == kInvalid || twin >= b.halfedge_count()) continue;
            const FaceId other = b.loop_face[b.he_loop[twin]];
            if (other != f &&
                std::find(out.begin(), out.end(), other) == out.end())
                out.push_back(other);
        }
    return out;
}

f64 face_area_of(const Body& b, FaceId f, const Volume& v) {
    return measure_face_area(b, f, v).value;
}

}

const char* feature_kind_name(FeatureKind k) noexcept {
    switch (k) {
        case FeatureKind::Unknown:    return "unknown";
        case FeatureKind::Hole:       return "hole";
        case FeatureKind::Pocket:     return "pocket";
        case FeatureKind::Boss:       return "boss";
        case FeatureKind::Fillet:     return "fillet";
        case FeatureKind::Chamfer:    return "chamfer";
        case FeatureKind::Slot:       return "slot";
        case FeatureKind::Rib:        return "rib";
        case FeatureKind::Through:    return "through hole";
        case FeatureKind::PlanarFace: return "planar face";
    }
    return "?";
}

std::vector<std::vector<FaceId>> connected_face_groups(const Body& b,
                                                       const Volume& v,
                                                       f64 angle_tolerance) {
    std::vector<std::vector<FaceId>> out;
    std::vector<bool> seen(b.face_count(), false);

    for (FaceId start = 0; start < b.face_count(); ++start) {
        if (seen[start]) continue;

        std::vector<FaceId> group{start};
        seen[start] = true;
        std::vector<FaceId> queue{start};

        while (!queue.empty()) {
            const FaceId f = queue.back();
            queue.pop_back();
            const Plane pf = face_plane(b, f, v);

            for (FaceId n : neighbours(b, f)) {
                if (seen[n]) continue;
                const Plane pn = face_plane(b, n, v);
                if (!pf.valid() || !pn.valid()) continue;

                const f64 d = pf.nx * pn.nx + pf.ny * pn.ny + pf.nz * pn.nz;
                if (std::acos(std::clamp(d, -1.0, 1.0)) > angle_tolerance) continue;

                seen[n] = true;
                group.push_back(n);
                queue.push_back(n);
            }
        }
        out.push_back(std::move(group));
    }
    return out;
}

std::vector<Feature> find_holes(const Body& b, const Volume& v,
                                const RecognitionSettings& s) {
    std::vector<Feature> out;
    if (!s.find_holes) return out;

    for (FaceId f = 0; f < b.face_count(); ++f) {
        const Surface* surf = b.surface_of(f);
        if (!surf || surf->kind != SurfaceKind::Cylinder) continue;
        if (surf->radius < s.min_fillet_radius) continue;

        const Plane p = face_plane(b, f, v);
        f64 cx = 0, cy = 0, cz = 0;
        int n = 0;
        for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li)
            for (u32 h = b.loop_begin(li); h < b.loop_end(li); ++h) {
                const P3 w = world(v, b.vertex_point[b.he_vertex[h]]);
                cx += w.x;
                cy += w.y;
                cz += w.z;
                ++n;
            }
        if (n == 0) continue;
        cx /= n;
        cy /= n;
        cz /= n;

        const f64 to_axis_x = surf->ox - cx, to_axis_y = surf->oy - cy,
                  to_axis_z = surf->oz - cz;
        const f64 along = to_axis_x * surf->ax + to_axis_y * surf->ay +
                          to_axis_z * surf->az;
        const f64 rx = to_axis_x - surf->ax * along;
        const f64 ry = to_axis_y - surf->ay * along;
        const f64 rz = to_axis_z - surf->az * along;
        const f64 rl = norm3(rx, ry, rz);
        if (rl < 1e-12) continue;

        const bool concave = (p.nx * rx + p.ny * ry + p.nz * rz) / rl > 0.0;

        Feature h;
        h.kind = concave ? FeatureKind::Hole : FeatureKind::Boss;
        h.faces = {f};
        h.radius = surf->radius;
        h.axis[0] = surf->ax;
        h.axis[1] = surf->ay;
        h.axis[2] = surf->az;
        h.origin[0] = cx;
        h.origin[1] = cy;
        h.origin[2] = cz;
        h.confidence = 0.95;
        h.why = "the face carries an exact cylindrical surface";
        if (!s.find_bosses && h.kind == FeatureKind::Boss) continue;
        out.push_back(std::move(h));
    }

    return out;
}

std::vector<Feature> find_blends(const Body& b, const Volume& v,
                                 const RecognitionSettings& s) {
    std::vector<Feature> out;
    if (!s.find_blends) return out;

    for (FaceId f = 0; f < b.face_count(); ++f) {
        const std::vector<FaceId> nb = neighbours(b, f);
        if (nb.size() < 2) continue;

        const f64 area = face_area_of(b, f, v);
        if (area <= 0.0) continue;

        f64 big1 = 0.0, big2 = 0.0;
        FaceId n1 = kInvalid, n2 = kInvalid;
        for (FaceId n : nb) {
            const f64 a = face_area_of(b, n, v);
            if (a > big1) {
                big2 = big1;
                n2 = n1;
                big1 = a;
                n1 = n;
            } else if (a > big2) {
                big2 = a;
                n2 = n;
            }
        }
        if (n1 == kInvalid || n2 == kInvalid) continue;
        if (area > big1 * 0.5 || area > big2 * 0.5) continue;

        const Surface* surf = b.surface_of(f);
        Feature blend;
        blend.faces = {f};

        if (surf && surf->kind == SurfaceKind::Cylinder) {
            if (surf->radius < s.min_fillet_radius ||
                surf->radius > s.max_fillet_radius)
                continue;
            blend.kind = FeatureKind::Fillet;
            blend.radius = surf->radius;
            blend.confidence = 0.95;
            blend.why = "a narrow cylindrical face between two larger ones";
        } else {
            const Plane pf = face_plane(b, f, v);
            const Plane p1 = face_plane(b, n1, v);
            const Plane p2 = face_plane(b, n2, v);
            if (!pf.valid() || !p1.valid() || !p2.valid()) continue;

            const f64 a1 = std::acos(std::clamp(
                pf.nx * p1.nx + pf.ny * p1.ny + pf.nz * p1.nz, -1.0, 1.0));
            const f64 a2 = std::acos(std::clamp(
                pf.nx * p2.nx + pf.ny * p2.ny + pf.nz * p2.nz, -1.0, 1.0));
            if (a1 < s.chamfer_angle_tolerance || a2 < s.chamfer_angle_tolerance)
                continue;

            blend.kind = FeatureKind::Chamfer;
            blend.width = area;
            blend.confidence = 0.7;
            blend.why = "a narrow planar face at an angle to both neighbours";
        }

        out.push_back(std::move(blend));
    }

    return out;
}

std::vector<Feature> find_pockets(const Body& b, const Volume& v,
                                  const RecognitionSettings& s) {
    std::vector<Feature> out;
    if (!s.find_pockets) return out;

    for (FaceId floor = 0; floor < b.face_count(); ++floor) {
        const Plane pf = face_plane(b, floor, v);
        if (!pf.valid()) continue;
        if (b.face_is_curved(floor)) continue;

        const std::vector<FaceId> walls = neighbours(b, floor);
        if (walls.size() < 3) continue;

        bool all_concave = true;
        f64 depth = 0.0;
        for (FaceId w : walls) {
            const Plane pw = face_plane(b, w, v);
            if (!pw.valid()) {
                all_concave = false;
                break;
            }
            f64 cx = 0, cy = 0, cz = 0;
            int n = 0;
            for (u32 li = b.face_loop_begin(floor); li < b.face_loop_end(floor);
                 ++li)
                for (u32 h = b.loop_begin(li); h < b.loop_end(li); ++h) {
                    const P3 q = world(v, b.vertex_point[b.he_vertex[h]]);
                    cx += q.x;
                    cy += q.y;
                    cz += q.z;
                    ++n;
                }
            if (n == 0) {
                all_concave = false;
                break;
            }
            if (pw.distance(cx / n, cy / n, cz / n) > 1e-9) {
                all_concave = false;
                break;
            }
            depth = std::max(depth, face_area_of(b, w, v));
        }
        if (!all_concave) continue;

        Feature p;
        p.kind = FeatureKind::Pocket;
        p.faces = walls;
        p.faces.insert(p.faces.begin(), floor);
        p.axis[0] = pf.nx;
        p.axis[1] = pf.ny;
        p.axis[2] = pf.nz;
        p.origin[0] = pf.nx * pf.d;
        p.origin[1] = pf.ny * pf.d;
        p.origin[2] = pf.nz * pf.d;
        p.length = face_area_of(b, floor, v);
        p.depth = depth;
        p.confidence = 0.6;
        p.why = "a planar floor enclosed by inward-facing walls";
        out.push_back(std::move(p));
    }

    return out;
}

RecognitionResult recognise(const Body& b, const Volume& v,
                            const RecognitionSettings& s) {
    RecognitionResult out;
    out.faces_total = b.face_count();
    if (b.face_count() == 0) {
        out.reason = "nothing to recognise";
        return out;
    }

    // Order matters: blends must claim their faces before pockets do.
    std::vector<Feature> candidates;
    for (Feature& f : find_blends(b, v, s)) candidates.push_back(std::move(f));
    for (Feature& f : find_holes(b, v, s)) candidates.push_back(std::move(f));
    for (Feature& f : find_pockets(b, v, s)) candidates.push_back(std::move(f));

    std::vector<bool> claimed(b.face_count(), false);
    for (Feature& f : candidates) {
        f.primary_faces.clear();
        for (FaceId id : f.faces)
            if (id < claimed.size() && !claimed[id]) f.primary_faces.push_back(id);
        if (f.primary_faces.empty()) continue;

        for (FaceId id : f.primary_faces) claimed[id] = true;
        out.features.push_back(std::move(f));
    }

    for (std::size_t i = 0; i < claimed.size(); ++i)
        if (claimed[i]) ++out.faces_explained;

    out.ok = true;
    return out;
}

}
