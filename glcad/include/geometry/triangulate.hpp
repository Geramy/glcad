#pragma once

#include <vector>

#include "geometry/body.hpp"

namespace gtopo {

struct TriMesh {
    std::vector<u32>                    indices;    // 3 per triangle, into Body vertices
    std::vector<ggeo::d3::Vec<gcm::F64>> normals;   // one per triangle
    std::vector<u32>                    face_of;    // source face, one per triangle

    std::size_t triangle_count() const noexcept { return indices.size() / 3; }
};

TriMesh triangulate(const Body& b, const gcm::WorkingVolume& vol);

std::vector<u32> triangulate_face(const std::vector<std::vector<u32>>& loops,
                                  const std::vector<gcm::Lattice2>& pts);

}
