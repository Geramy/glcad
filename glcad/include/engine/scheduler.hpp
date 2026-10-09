#pragma once

#include <array>
#include <optional>

#include "engine/lane.hpp"

namespace gcs {

struct Decision {
    Lane lane{Lane::CpuScalar};
    f64  estimated_ns{0.0};
    bool locked{false};

    std::array<Reject, static_cast<size_t>(Lane::Count)> rejected{};
    std::array<f64, static_cast<size_t>(Lane::Count)>    estimates{};

    Reject reject_reason(Lane l) const noexcept {
        return rejected[static_cast<size_t>(l)];
    }
};

class Scheduler {
public:
    static constexpr size_t N = static_cast<size_t>(Lane::Count);

    Scheduler() {
        caps_[idx(Lane::CpuScalar)]  = {true, true};
        caps_[idx(Lane::GpuCompute)] = {true, true};
        caps_[idx(Lane::CpuMatrix)]  = {true, false};  // Accelerate: not reproducible

        // Placeholder costs; replaced by first-run calibration.
        cost_[idx(Lane::GpuCompute)] = {60000.0, 0.002};
        cost_[idx(Lane::CpuScalar)]  = {50.0, 1.0};
        cost_[idx(Lane::CpuMatrix)]  = {2000.0, 0.04};
    }

    void set_caps(Lane l, LaneCaps c) noexcept { caps_[idx(l)] = c; }
    void set_cost(Lane l, LaneCost c) noexcept { cost_[idx(l)] = c; }
    void set_load(Lane l, LaneLoad v) noexcept { load_[idx(l)] = v; }

    LaneLoad load(Lane l) const noexcept { return load_[idx(l)]; }
    LaneCost cost(Lane l) const noexcept { return cost_[idx(l)]; }

    void lock(Shape s, Lane l) noexcept { locks_[static_cast<size_t>(s)] = l; }
    void unlock(Shape s) noexcept { locks_[static_cast<size_t>(s)].reset(); }
    void lock_all(Lane l) noexcept {
        for (auto& v : locks_) v = l;
    }
    void unlock_all() noexcept {
        for (auto& v : locks_) v.reset();
    }
    std::optional<Lane> locked_lane(Shape s) const noexcept {
        return locks_[static_cast<size_t>(s)];
    }

    Decision schedule(const WorkItem& w) const noexcept {
        Decision d;
        for (size_t i = 0; i < N; ++i) {
            d.rejected[i]  = Reject::None;
            d.estimates[i] = -1.0;
        }

        const auto pin = locks_[static_cast<size_t>(w.shape)];

        f64  best     = 1e300;
        bool have_any = false;

        for (size_t i = 0; i < N; ++i) {
            const Lane l = static_cast<Lane>(i);

            if (pin && *pin != l) { d.rejected[i] = Reject::LockedElsewhere; continue; }
            if (!caps_[i].available) { d.rejected[i] = Reject::Unavailable; continue; }
            if (!shape_fits(l, w.shape)) { d.rejected[i] = Reject::WrongShape; continue; }

            // Bit-reproducible work may only run where reproducibility is guaranteed.
            if (w.requires_bit_identity && !caps_[i].can_be_bit_reproducible) {
                d.rejected[i] = Reject::NotBitReproducible;
                continue;
            }

            const f64 est = cost_[i].estimate_ns(w.items, load_[i]);
            d.estimates[i] = est;
            if (est < best) {
                best     = est;
                d.lane   = l;
                have_any = true;
            }
        }

        if (!have_any) {
            d.lane         = Lane::CpuScalar;
            d.estimated_ns = cost_[idx(Lane::CpuScalar)]
                                 .estimate_ns(w.items, load_[idx(Lane::CpuScalar)]);
        } else {
            d.estimated_ns = best;
        }
        d.locked = pin.has_value();
        return d;
    }

    // Migratable Authoritative work gets SoftF64 on the GPU, never DF64.
    enum class ScalarChoice : u8 { F32, DF64, SoftF64, F64 };

    static ScalarChoice scalar_for(const WorkItem& w, Lane l) noexcept {
        if (l != Lane::GpuCompute) return ScalarChoice::F64;
        if (w.requires_bit_identity) return ScalarChoice::SoftF64;
        if (w.result_class == Cls::Authoritative) return ScalarChoice::DF64;
        return ScalarChoice::F32;
    }

    static const char* scalar_name(ScalarChoice c) noexcept {
        switch (c) {
            case ScalarChoice::F32:     return "F32";
            case ScalarChoice::DF64:    return "DF64";
            case ScalarChoice::SoftF64: return "SoftF64";
            default:                    return "F64";
        }
    }

private:
    static constexpr size_t idx(Lane l) noexcept { return static_cast<size_t>(l); }

    std::array<LaneCaps, N> caps_{};
    std::array<LaneCost, N> cost_{};
    std::array<LaneLoad, N> load_{};
    std::array<std::optional<Lane>, 4> locks_{};
};

}
