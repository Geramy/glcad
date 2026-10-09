#pragma once
#include "geometry/snap.hpp"
#include <string>
#include <vector>

namespace gclip {
struct Boundary {
    std::vector<gcm::Lattice2> points;
    bool inverted{false};
};
struct Vertex { double x{}, y{}, u{}, v{}; };
struct MeshResult {
    bool ok{false};
    std::string error;
    std::vector<Vertex> vertices;
};
MeshResult triangles(const std::vector<Vertex>& input,
    const std::vector<Boundary>& boundaries, const gcm::WorkingVolume& volume);
bool valid_boundary(const std::vector<gcm::Lattice2>& boundary);
struct Result {
    bool ok{false};
    std::string error;
    std::vector<gsnap::Shape> shapes;
};
Result shapes(const std::vector<gsnap::Shape>& input,
              const std::vector<gcm::Lattice2>& boundary,bool inverted=false);
}
