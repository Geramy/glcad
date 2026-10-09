#pragma once

#include "math/config.hpp"
#include "math/scalar.hpp"

namespace gcm {

enum class Vendor { Apple, AMD, NVIDIA, Intel, CPU };

enum class Reduce {
    PrefixSum,
    Atomic64,        // faster where available; NOT usable for Authoritative work
    SubgroupBallot,
};

template <class P>
struct PlatformTraits;

struct AppleM {};
template <>
struct PlatformTraits<AppleM> {
    using PreferredScalar = DF64;
    static constexpr Vendor vendor                 = Vendor::Apple;
    static constexpr bool   has_native_f64         = false;
    static constexpr bool   has_int64              = true;
    static constexpr bool   has_atomic_i64         = false;
    static constexpr bool   has_subgroup_ops       = true;   // limited via MoltenVK
    static constexpr Reduce reduce_strategy        = Reduce::PrefixSum;
    static constexpr int    workgroup_size         = 256;
    static constexpr u32    max_items_per_dispatch = 1u << 20;  // watchdog bound
    static constexpr bool   unified_memory         = true;
};

struct AppleA {};
template <>
struct PlatformTraits<AppleA> {
    using PreferredScalar = DF64;
    static constexpr Vendor vendor                 = Vendor::Apple;
    static constexpr bool   has_native_f64         = false;
    static constexpr bool   has_int64              = true;    // apple3+
    static constexpr bool   has_atomic_i64         = false;
    static constexpr bool   has_subgroup_ops       = true;
    static constexpr Reduce reduce_strategy        = Reduce::PrefixSum;
    static constexpr int    workgroup_size         = 128;
    static constexpr u32    max_items_per_dispatch = 1u << 18;  // tighter thermals
    static constexpr bool   unified_memory         = true;
};

struct DiscreteConsumer {};
template <>
struct PlatformTraits<DiscreteConsumer> {
    using PreferredScalar = DF64;
    static constexpr Vendor vendor                 = Vendor::AMD;
    static constexpr bool   has_native_f64         = true;
    static constexpr bool   has_int64              = true;
    static constexpr bool   has_atomic_i64         = true;
    static constexpr bool   has_subgroup_ops       = true;
    static constexpr Reduce reduce_strategy        = Reduce::PrefixSum;
    static constexpr int    workgroup_size         = 256;
    static constexpr u32    max_items_per_dispatch = 1u << 24;
    static constexpr bool   unified_memory         = false;
};

struct DiscreteWorkstation {};
template <>
struct PlatformTraits<DiscreteWorkstation> {
    using PreferredScalar = F64;
    static constexpr Vendor vendor                 = Vendor::NVIDIA;
    static constexpr bool   has_native_f64         = true;
    static constexpr bool   has_int64              = true;
    static constexpr bool   has_atomic_i64         = true;
    static constexpr bool   has_subgroup_ops       = true;
    static constexpr Reduce reduce_strategy        = Reduce::PrefixSum;
    static constexpr int    workgroup_size         = 256;
    static constexpr u32    max_items_per_dispatch = 1u << 26;
    static constexpr bool   unified_memory         = false;
};

struct Cpu {};
template <>
struct PlatformTraits<Cpu> {
    using PreferredScalar = F64;
    static constexpr bool has_matrix_accel = true;   // queried at runtime
    static constexpr Vendor vendor                 = Vendor::CPU;
    static constexpr bool   has_native_f64         = true;
    static constexpr bool   has_int64              = true;
    static constexpr bool   has_atomic_i64         = true;
    static constexpr bool   has_subgroup_ops       = false;
    static constexpr Reduce reduce_strategy        = Reduce::PrefixSum;
    static constexpr int    workgroup_size         = 1;
    static constexpr u32    max_items_per_dispatch = 0xFFFFFFFFu;
    static constexpr bool   unified_memory         = true;
};

template <class P>
inline constexpr bool authoritative_reduce_ok =
    PlatformTraits<P>::reduce_strategy != Reduce::Atomic64;

static_assert(authoritative_reduce_ok<AppleM>);
static_assert(authoritative_reduce_ok<DiscreteConsumer>);
static_assert(authoritative_reduce_ok<Cpu>);

}
