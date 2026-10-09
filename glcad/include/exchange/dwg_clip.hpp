#pragma once
#include "geometry/clip.hpp"
namespace gdwg::detail {
inline bool valid_clip(const std::vector<gcm::Lattice2>& points){return gclip::valid_boundary(points);}
}
