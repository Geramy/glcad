#pragma once

#include "math/config.hpp"
#include "math/scalar.hpp"
#include "math/soft_f64.hpp"

namespace gcs {

using namespace gcm;

enum class Lane : u8 {
    GpuCompute,
    CpuScalar,
    CpuMatrix,
    Count
};

const char* lane_name(Lane l) noexcept;

enum class Shape : u8 {
    DataParallel,
    Matrix,
    Irregular,
    Serial,
};

enum class Reject : u8 {
    None,
    Unavailable,
    WrongShape,
    NotBitReproducible,
    LockedElsewhere,
};

const char* reject_name(Reject r) noexcept;

struct LaneCaps {
    bool available{true};
    // CpuMatrix is never bit-identical: Accelerate varies by chip and OS build.
    bool can_be_bit_reproducible{false};
};

struct LaneLoad {
    f64 utilization{0.0};
    u32 queue_depth{0};
    f64 thermal_headroom{1.0};  // 1.0 cool, 0.0 fully throttled

    f64 penalty() const noexcept {
        const f64 busy = 1.0 / (1.0 - (utilization > 0.95 ? 0.95 : utilization));
        const f64 queued = 1.0 + 0.25 * static_cast<f64>(queue_depth);
        const f64 hot = (thermal_headroom < 0.05) ? 20.0 : 1.0 / thermal_headroom;
        return busy * queued * hot;
    }
};

struct LaneCost {
    f64 fixed_ns{0.0};
    f64 ns_per_item{1.0};

    f64 estimate_ns(u64 items, const LaneLoad& load) const noexcept {
        return (fixed_ns + static_cast<f64>(items) * ns_per_item) * load.penalty();
    }
};

struct WorkItem {
    Shape shape{Shape::DataParallel};
    Cls   result_class{Cls::Display};
    u64   items{1};

    bool requires_bit_identity{false};

    const char* label{""};
};

inline const char* lane_name(Lane l) noexcept {
    switch (l) {
        case Lane::GpuCompute: return "GpuCompute";
        case Lane::CpuScalar:  return "CpuScalar";
        case Lane::CpuMatrix:  return "CpuMatrix";
        default:               return "?";
    }
}

inline const char* reject_name(Reject r) noexcept {
    switch (r) {
        case Reject::Unavailable:        return "lane unavailable";
        case Reject::WrongShape:         return "workload shape not supported";
        case Reject::NotBitReproducible: return "cannot guarantee bit-identical results";
        case Reject::LockedElsewhere:    return "workload locked to another lane";
        default:                         return "eligible";
    }
}

constexpr bool shape_fits(Lane l, Shape s) noexcept {
    switch (s) {
        case Shape::DataParallel:
            return l == Lane::GpuCompute || l == Lane::CpuScalar;
        case Shape::Matrix:
            return l == Lane::CpuMatrix || l == Lane::CpuScalar;
        case Shape::Irregular:
        case Shape::Serial:
            return l == Lane::CpuScalar;
    }
    return false;
}

}
