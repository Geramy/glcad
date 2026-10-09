#pragma once

#include <string>
#include <vector>

#include "geometry/snap.hpp"

namespace gplan {

struct Finding {
    enum class Level : gcm::u8 { Error, Warning, Note };
    Level       level{Level::Warning};
    std::string rule;
    std::string text;
    bool located{false};
    gcm::Lattice2 location{};
    std::vector<gcm::u32> shape_keys;
};

struct Rules {
    double entrance_min_m{0.864};      // a 36in leaf, less trim allowance
    double egress_door_min_m{0.711};   // 32in clear less leaf allowance (R311.2)
    double jacks_double_over_m{1.83};
    int    layer0_slack{12};
    std::string source{"US residential baseline (IRC)"};
};

Rules load_rules(const std::string& path);
bool  save_rules(const std::string& path, const Rules& rules);

struct Plan {
    const std::vector<gsnap::Shape>* shapes{nullptr};
    double      metres_per_unit{1.0};
    std::size_t dimensions{0};
    std::size_t labels{0};
    std::size_t hatches{0};
    std::size_t orphan_hatches{0};
    bool architectural{true};
};

std::vector<Finding> check(const Plan& plan, const Rules& rules);
std::vector<Finding> door_swing_collisions(const Plan& plan);

}
