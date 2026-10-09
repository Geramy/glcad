#pragma once

#include <string>

#include "math/config.hpp"

#if defined(__APPLE__)
#  include <sys/sysctl.h>
#endif

namespace gcm {

enum class MatrixAccel { None, AMX, SME };

struct CpuInfo {
    std::string brand;
    MatrixAccel accel{MatrixAccel::None};

    bool sme{false};
    bool sme2{false};
    bool sme_f64f64{false};
    bool sme_i16i64{false};
    bool amx{false};

    u32 sme_vector_bytes{0};
    u32 perf_cores{0};
    u32 efficiency_cores{0};

    u32 f64_lanes() const noexcept { return sme_vector_bytes / 8; }

    bool has_native_f64_matrix() const noexcept {
        return (accel == MatrixAccel::SME && sme_f64f64) || accel == MatrixAccel::AMX;
    }

    const char* accel_name() const noexcept {
        switch (accel) {
            case MatrixAccel::AMX: return "AMX (undocumented; via Accelerate only)";
            case MatrixAccel::SME: return "SME (ARM Scalable Matrix Extension)";
            default:               return "none";
        }
    }
};

namespace detail {
#if defined(__APPLE__)
inline bool sysctl_flag(const char* name) noexcept {
    int    v   = 0;
    size_t len = sizeof(v);
    return sysctlbyname(name, &v, &len, nullptr, 0) == 0 && v != 0;
}
inline u32 sysctl_u32(const char* name) noexcept {
    u64    v   = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, nullptr, 0) != 0) return 0;
    return static_cast<u32>(v);
}
inline std::string sysctl_str(const char* name) {
    size_t len = 0;
    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0) return {};
    std::string s(len, '\0');
    if (sysctlbyname(name, s.data(), &len, nullptr, 0) != 0) return {};
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
#endif
}

inline CpuInfo probe_cpu() {
    CpuInfo c;
#if defined(__APPLE__)
    c.brand            = detail::sysctl_str("machdep.cpu.brand_string");
    c.sme              = detail::sysctl_flag("hw.optional.arm.FEAT_SME");
    c.sme2             = detail::sysctl_flag("hw.optional.arm.FEAT_SME2");
    c.sme_f64f64       = detail::sysctl_flag("hw.optional.arm.FEAT_SME_F64F64");
    c.sme_i16i64       = detail::sysctl_flag("hw.optional.arm.FEAT_SME_I16I64");
    c.sme_vector_bytes = detail::sysctl_u32("hw.optional.arm.sme_max_svl_b");
    c.perf_cores       = detail::sysctl_u32("hw.perflevel0.physicalcpu");
    c.efficiency_cores = detail::sysctl_u32("hw.perflevel1.physicalcpu");

    // AMX has no feature flag: Apple Silicon without SME (M1-M3) implies it.
    c.amx = !c.sme && !c.brand.empty();

    if (c.sme)      c.accel = MatrixAccel::SME;
    else if (c.amx) c.accel = MatrixAccel::AMX;
#endif
    return c;
}

}
