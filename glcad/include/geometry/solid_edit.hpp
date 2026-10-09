#pragma once

#include <string>
#include <vector>

#include "geometry/body.hpp"

namespace gtopo {

using Volume = gcm::WorkingVolume;
using Vec3   = ggeo::d3::Vec<gcm::F64>;

// A plane in world units: n . x = d, with n unit length.
struct Plane {
    f64 nx{0.0}, ny{0.0}, nz{1.0};
    f64 d{0.0};

    bool valid() const noexcept;
    f64  distance(f64 x, f64 y, f64 z) const noexcept {
        return nx * x + ny * y + nz * z - d;
    }
    Plane offset(f64 by) const noexcept { return {nx, ny, nz, d + by}; }
    Plane flipped() const noexcept { return {-nx, -ny, -nz, -d}; }
};

Plane face_plane(const Body& b, FaceId f, const Volume& v);

f64 face_planarity(const Body& b, FaceId f, const Volume& v);

std::vector<FaceId> faces_at_vertex(const Body& b, VertexId v);
bool faces_of_edge(const Body& b, HalfedgeId he, FaceId& a, FaceId& c);
bool edge_is_convex(const Body& b, HalfedgeId he, const Volume& v);

bool intersect_planes(const Plane& a, const Plane& b, const Plane& c,
                      f64& x, f64& y, f64& z);

struct EditResult {
    Body        body;
    bool        ok{false};
    std::string reason;
    f64         volume_before{0.0};
    f64         volume_after{0.0};
    f64         volume_delta() const noexcept { return volume_after - volume_before; }
};

EditResult move_face(const Body& b, FaceId face, Vec3 delta, const Volume& v);

EditResult offset_face(const Body& b, FaceId face, f64 distance, const Volume& v);

EditResult retarget_face(const Body& b, FaceId face, const Plane& target,
                         const Volume& v);

EditResult draft_face(const Body& b, FaceId face, const Plane& neutral,
                      f64 angle_deg, const Volume& v);

EditResult chamfer_edge(const Body& b, HalfedgeId edge, f64 setback_a,
                        f64 setback_b, const Volume& v);

std::vector<FaceId> circular_wall_faces(const Body& b, HalfedgeId edge, const Volume& v);
std::vector<HalfedgeId> circular_edge_ring(const Body& b, HalfedgeId edge, const Volume& v);
EditResult blend_circular_edge(const Body& b, HalfedgeId edge, f64 radial_setback,
                               f64 axial_setback, int segments, bool round, const Volume& v);

EditResult fillet_edge(const Body& b, HalfedgeId edge, f64 radius, int segments,
                       const Volume& v);

EditResult fillet_edge_exact(const Body& b, HalfedgeId edge, f64 radius,
                             int segments, const Volume& v);

struct ShellResult {
    Body        body;
    bool        ok{false};
    std::string reason;
    f64         thickness{0.0};
    std::size_t removed_faces{0};
};

ShellResult shell_body(const Body& b, f64 thickness,
                       const std::vector<FaceId>& open_faces, const Volume& v);

bool offset_surface(const Surface& s, f64 distance, Surface& out);

class IdMap {
public:
    void seed(const Body& b);

    u32  face_id(FaceId f) const;
    void set_face_id(FaceId f, u32 id);
    FaceId face_of(u32 id) const;      // kInvalid when the face is gone
    u32  allocate();
    u32 next_id() const noexcept { return next_; }
    void restore(std::vector<u32> ids, u32 next) { ids_ = std::move(ids); next_ = next; }

    std::size_t size() const noexcept { return ids_.size(); }
    const std::vector<u32>& all() const noexcept { return ids_; }

    void follow(const Body& before, const Body& after,
                const std::vector<u32>& survivors);

private:
    std::vector<u32> ids_;
    u32              next_{1};
};

}
