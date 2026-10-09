#pragma once

#include "geometry/d3.hpp"
#include "geometry/body.hpp"
#include "geometry/profile.hpp"

namespace gtopo {

Body extrude(const Profile& p, const gcm::WorkingVolume& vol,
             const ggeo::d3::Frame<gcm::F64>& frame, f64 height);

Body extrude_z(const Profile& p, const gcm::WorkingVolume& vol, f64 height);

}
