#pragma once

#include "geometry/body.hpp"
#include "geometry/profile.hpp"

namespace gtopo {

struct Section {
    Profile                   profile;
    ggeo::d3::Frame<gcm::F64> frame{ggeo::d3::Frame<gcm::F64>::world_xy()};
};

Body loft(const std::vector<Section>& sections, const gcm::WorkingVolume& vol,
          bool cap_ends = true);

enum class LoftError : gcm::u8 {
    None, TooFewSections, VertexCountMismatch, InvalidProfile,
};
LoftError check_sections(const std::vector<Section>& sections);
const char* loft_error_message(LoftError e) noexcept;

Profile offset_profile(const Profile& p, f64 distance,
                       const gcm::WorkingVolume& vol);

Body shell_extrusion(const Profile& p, const gcm::WorkingVolume& vol, f64 height,
                     f64 thickness, bool open_top = true, bool open_bottom = false);

}
