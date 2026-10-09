#include <cstdio>

#include "math/predicate.hpp"
#include "render/device_caps.hpp"

using namespace gcvk;

static void yn(const char* label, bool v) {
    std::printf("    %-28s %s\n", label, v ? "yes" : "\033[31mNO\033[0m");
}

int main() {
    std::printf("\n\033[1mGLCAD - Vulkan device probe\033[0m\n");
    std::printf("docs/08-gpu-compute.md  |  docs/10-numerics-and-platform-abstraction.md\n\n");

    const ProbeResult r = probe();
    if (!r.ok) {
        std::printf("  \033[31mprobe failed:\033[0m %s\n\n", r.error.c_str());
        return 1;
    }

    std::printf("  instance layers: %zu   validation: %s\n\n",
                r.instance_layers.size(),
                r.validation_available ? "available" : "\033[33mNOT INSTALLED\033[0m");

    if (r.devices.empty()) {
        std::printf("  no Vulkan devices found\n\n");
        return 1;
    }

    for (const auto& d : r.devices) {
        std::printf("  \033[1m%s\033[0m\n", d.name.c_str());
        std::printf("    driver %s   Vulkan %s   %s\n",
                    d.driver.c_str(), d.api_version_str.c_str(),
                    d.is_moltenvk ? "(MoltenVK -> Metal)" : "");
        std::printf("    device-local memory %.1f GB   %s\n\n",
                    static_cast<double>(d.device_local_bytes) / (1024.0 * 1024.0 * 1024.0),
                    d.is_unified_memory ? "unified" : "discrete");

        yn("shaderFloat64", d.shader_float64);
        yn("shaderInt64", d.shader_int64);
        yn("shaderBufferInt64Atomics", d.atomic_int64_buffer);
        yn("shaderSharedInt64Atomics", d.atomic_int64_shared);
        yn("subgroup arithmetic", d.subgroup_arithmetic);
        yn("subgroup ballot", d.subgroup_ballot);
        std::printf("    %-28s %u\n", "subgroup size", d.subgroup_size);
        std::printf("    %-28s %u\n", "max workgroup invocations", d.max_workgroup_size);

        std::printf("\n    matched profile   \033[1m%s\033[0m\n", profile_name(d.profile));
        std::printf("    scalar backend    \033[1m%s\033[0m  (%d mantissa bits)\n",
                    d.prefers_df64() ? "DF64" : "F64",
                    d.prefers_df64() ? gcm::DF64::mantissa_bits : gcm::F64::mantissa_bits);
        std::printf("    predicate filter  gcm::Predicates<%s>\n",
                    d.prefers_df64() ? "DF64" : "F64");
        std::printf("    max lattice bits  %d\n",
                    d.prefers_df64()
                        ? gcm::predicate::Predicates<gcm::DF64>::max_lattice_bits
                        : gcm::predicate::Predicates<gcm::F64>::max_lattice_bits);
        std::printf("    reduction         %s\n",
                    d.atomic_int64_buffer ? "prefix-sum (atomics available but "
                                            "non-deterministic; not used for "
                                            "Authoritative work)"
                                          : "prefix-sum (forced: no 64-bit atomics)");

        const auto conflicts = d.trait_conflicts();
        if (conflicts.empty()) {
            std::printf("\n    \033[32mruntime capabilities match the compile-time profile\033[0m\n");
        } else {
            std::printf("\n    \033[33mtrait conflicts:\033[0m\n");
            for (const auto& c : conflicts) std::printf("      - %s\n", c.c_str());
        }
        std::printf("\n");
    }
    return 0;
}
