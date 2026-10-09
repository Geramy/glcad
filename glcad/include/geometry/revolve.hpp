#pragma once

#include "geometry/d2.hpp"
#include "geometry/body.hpp"
#include "geometry/profile.hpp"

namespace gtopo {

Body revolve(const Profile& p, const gcm::WorkingVolume& vol,
             const ggeo::d3::Frame<gcm::F64>& frame,
             ggeo::d2::Point<gcm::F64> axis_point,
             ggeo::d2::Vec<gcm::F64> axis_dir,
             f64 angle, int segments = 32);

Body revolve_y(const Profile& p, const gcm::WorkingVolume& vol, f64 angle,
               int segments = 32);

Body sweep(const Profile& p, const gcm::WorkingVolume& vol,
           const std::vector<ggeo::d3::Point<gcm::F64>>& path, bool cap_ends = true);

bool axis_crosses_profile(const Profile& p, const gcm::WorkingVolume& vol,
                          ggeo::d2::Point<gcm::F64> axis_point,
                          ggeo::d2::Vec<gcm::F64> axis_dir);

}
