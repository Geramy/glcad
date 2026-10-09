#pragma once

#include "engine/lane.hpp"

namespace gcs {

struct MemoryHierarchy {
    u64 fast_bytes{32 * 1024};
    u64 mid_bytes{4 * 1024 * 1024};
    u32 vector_lanes{4};
    u32 max_workgroup{1};
};

// GPU values are what gcvk_probe reports on Apple M-series.
inline MemoryHierarchy hierarchy_for(Lane l) noexcept {
    switch (l) {
        case Lane::GpuCompute:
            return {32 * 1024, 8ull * 1024 * 1024, 32, 1024};
        case Lane::CpuScalar:
            return {128 * 1024, 16ull * 1024 * 1024, 4, 1};   // M-series P-core
        case Lane::CpuMatrix:
            return {128 * 1024, 16ull * 1024 * 1024, 8, 1};   // SME: 8 fp64 lanes
        default:
            return {};
    }
}

struct Extent2 {
    u64 nx{1}, ny{1};
    u64 area() const noexcept { return nx * ny; }
};

struct Tile2 {
    u32 tx{1}, ty{1};
    u64 area() const noexcept { return u64(tx) * ty; }
};

struct TileRequest {
    Extent2 extent{};
    u32     bytes_per_element{4};
    // Live tiles: elementwise 1, blocked GEMM 3, stencil 1 plus a halo.
    u32     tiles_resident{1};
};

struct TilePlan {
    Tile2 tile{};
    u64   working_set_bytes{0};
    u64   tile_count{0};
    bool  fits{true};
    bool  subgroup_aligned{true};
};

namespace detail {
constexpr u32 round_down_multiple(u32 v, u32 m) noexcept {
    return (m == 0 || v < m) ? v : (v / m) * m;
}
inline u32 isqrt_u64(u64 v) noexcept {
    u32 r = 0;
    while ((u64(r) + 1) * (u64(r) + 1) <= v) ++r;
    return r;
}
}

inline TilePlan plan_tile(const TileRequest& req, const MemoryHierarchy& h) noexcept {
    TilePlan p;

    const u32 bpe      = req.bytes_per_element ? req.bytes_per_element : 1;
    const u32 resident = req.tiles_resident ? req.tiles_resident : 1;

    u64 budget_elems = h.fast_bytes / (u64(bpe) * resident);
    if (budget_elems == 0) budget_elems = 1;

    if (h.max_workgroup > 1 && budget_elems > h.max_workgroup)
        budget_elems = h.max_workgroup;

    u32 tx = detail::isqrt_u64(budget_elems);
    if (tx == 0) tx = 1;

    // Alignment must never grow the tile past the memory budget.
    if (req.extent.nx >= h.vector_lanes && budget_elems >= h.vector_lanes) {
        const u32 aligned = detail::round_down_multiple(tx, h.vector_lanes);
        if (aligned >= h.vector_lanes) tx = aligned;
        else                           tx = h.vector_lanes;
    }
    if (u64(tx) > req.extent.nx) tx = static_cast<u32>(req.extent.nx);
    if (tx == 0) tx = 1;

    u64 ty = budget_elems / tx;
    if (ty == 0) ty = 1;
    if (ty > req.extent.ny) ty = req.extent.ny;
    if (ty == 0) ty = 1;

    p.tile.tx = tx;
    p.tile.ty = static_cast<u32>(ty);

    // A workgroup cap can still be violated after the alignment step; trim ty.
    if (h.max_workgroup > 1 && p.tile.area() > h.max_workgroup) {
        p.tile.ty = static_cast<u32>(h.max_workgroup / p.tile.tx);
        if (p.tile.ty == 0) p.tile.ty = 1;
    }

    p.working_set_bytes = p.tile.area() * bpe * resident;
    p.fits              = p.working_set_bytes <= h.fast_bytes;
    p.subgroup_aligned  = (p.tile.tx % h.vector_lanes == 0) ||
                          (u64(p.tile.tx) == req.extent.nx);

    const u64 nx_tiles = (req.extent.nx + p.tile.tx - 1) / p.tile.tx;
    const u64 ny_tiles = (req.extent.ny + p.tile.ty - 1) / p.tile.ty;
    p.tile_count       = nx_tiles * ny_tiles;
    return p;
}

inline TilePlan plan_tile(const TileRequest& req, Lane l) noexcept {
    return plan_tile(req, hierarchy_for(l));
}

}
