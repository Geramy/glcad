#pragma once

#include <string>
#include <vector>

#include "math/config.hpp"
#include "math/platform.hpp"

namespace gcvk {

using namespace gcm;

enum class Profile { AppleM, AppleA, DiscreteConsumer, DiscreteWorkstation, Unknown };

const char* profile_name(Profile p) noexcept;

struct DeviceCaps {
    std::string name;
    std::string driver;
    u32         api_version{0};
    std::string api_version_str;
    u32         vendor_id{0};
    bool        is_moltenvk{false};
    bool        is_unified_memory{false};

    bool shader_float64{false};
    bool shader_int64{false};
    bool atomic_int64_buffer{false};
    bool atomic_int64_shared{false};

    bool     subgroup_arithmetic{false};
    bool     subgroup_ballot{false};
    u32      subgroup_size{0};
    u32      max_workgroup_size{0};
    u64      device_local_bytes{0};

    Profile profile{Profile::Unknown};

    // Starting point only: consumer GPUs run f64 at 1/16-1/64 rate.
    bool prefers_df64() const noexcept;

    std::vector<std::string> trait_conflicts() const;
};

struct ProbeResult {
    bool                    ok{false};
    std::string             error;
    std::vector<DeviceCaps> devices;
    std::vector<std::string> instance_layers;
    bool                    validation_available{false};
};

ProbeResult probe();

}
