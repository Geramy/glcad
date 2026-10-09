#include "test.hpp"

#include "engine/tile.hpp"

using namespace gcs;

static TileRequest grid(u64 nx, u64 ny, u32 bpe = 8, u32 resident = 1) {
    TileRequest r;
    r.extent = {nx, ny};
    r.bytes_per_element = bpe;
    r.tiles_resident = resident;
    return r;
}

TEST(tile_working_set_fits_the_fast_memory) {
    for (Lane l : {Lane::GpuCompute, Lane::CpuScalar, Lane::CpuMatrix}) {
        const auto h = hierarchy_for(l);
        for (u32 bpe : {4u, 8u, 16u, 64u}) {
            for (u32 res : {1u, 3u}) {
                const auto p = plan_tile(grid(100000, 100000, bpe, res), l);
                CHECK_MSG(p.working_set_bytes <= h.fast_bytes,
                          "%s: working set %llu > fast %llu",
                          lane_name(l), (unsigned long long)p.working_set_bytes,
                          (unsigned long long)h.fast_bytes);
                CHECK(p.fits);
            }
        }
    }
}

TEST(tile_respects_gpu_workgroup_limit) {
    // Apple M-series reports max 1024 invocations per workgroup (gcvk_probe).
    const auto h = hierarchy_for(Lane::GpuCompute);
    for (u32 bpe : {4u, 8u, 16u}) {
        const auto p = plan_tile(grid(1000000, 1000000, bpe), Lane::GpuCompute);
        CHECK_MSG(p.tile.area() <= h.max_workgroup,
                  "tile %ux%u = %llu invocations exceeds %u",
                  p.tile.tx, p.tile.ty,
                  (unsigned long long)p.tile.area(), h.max_workgroup);
    }
}

TEST(tile_aligns_to_the_subgroup_width) {
    // Apple reports subgroup size 32.
    const auto p = plan_tile(grid(100000, 100000, 8), Lane::GpuCompute);
    CHECK_MSG(p.subgroup_aligned, "tile row %u is not subgroup-aligned", p.tile.tx);
    CHECK(p.tile.tx % 32 == 0);
}

TEST(tile_never_exceeds_the_domain) {
    for (Lane l : {Lane::GpuCompute, Lane::CpuScalar}) {
        for (auto e : {Extent2{1, 1}, Extent2{7, 3}, Extent2{31, 1000},
                       Extent2{5000, 2}}) {
            TileRequest r;
            r.extent = e;
            r.bytes_per_element = 8;
            const auto p = plan_tile(r, l);
            CHECK(u64(p.tile.tx) <= e.nx);
            CHECK(u64(p.tile.ty) <= e.ny);
            CHECK(p.tile.tx >= 1);
            CHECK(p.tile.ty >= 1);
        }
    }
}

TEST(tile_small_domain_is_a_single_tile) {
    const auto p = plan_tile(grid(8, 8, 8), Lane::CpuScalar);
    CHECK(p.tile.tx == 8);
    CHECK(p.tile.ty == 8);
    CHECK(p.tile_count == 1);
}

TEST(tile_differs_by_lane_for_the_same_request) {
    const auto req = grid(100000, 100000, 8);
    const auto gpu = plan_tile(req, Lane::GpuCompute);
    const auto cpu = plan_tile(req, Lane::CpuScalar);

    CHECK_MSG(gpu.tile.area() != cpu.tile.area(),
              "gpu %ux%u and cpu %ux%u tiles are identical; the hierarchy is "
              "not being consulted",
              gpu.tile.tx, gpu.tile.ty, cpu.tile.tx, cpu.tile.ty);

    // The GPU's 1024-invocation cap binds before its 32 KB threadgroup memory.
    CHECK(cpu.tile.area() > gpu.tile.area());
}

TEST(tile_resident_count_shrinks_the_tile) {
    const auto one   = plan_tile(grid(100000, 100000, 8, 1), Lane::CpuScalar);
    const auto three = plan_tile(grid(100000, 100000, 8, 3), Lane::CpuScalar);
    CHECK(three.tile.area() < one.tile.area());
    CHECK(three.working_set_bytes <= hierarchy_for(Lane::CpuScalar).fast_bytes);
}

TEST(tile_larger_elements_give_smaller_tiles) {
    const auto small = plan_tile(grid(100000, 100000, 4), Lane::CpuScalar);
    const auto big   = plan_tile(grid(100000, 100000, 64), Lane::CpuScalar);
    CHECK(big.tile.area() < small.tile.area());
}

TEST(tile_count_covers_the_whole_domain) {
    const auto req = grid(1000, 700, 8);
    const auto p   = plan_tile(req, Lane::CpuScalar);
    const u64 nx = (1000 + p.tile.tx - 1) / p.tile.tx;
    const u64 ny = (700 + p.tile.ty - 1) / p.tile.ty;
    CHECK(p.tile_count == nx * ny);
    CHECK(nx * p.tile.tx >= 1000);
    CHECK(ny * p.tile.ty >= 700);
}

TEST(tile_survives_absurd_element_sizes) {
    const auto p = plan_tile(grid(1000, 1000, 1u << 20), Lane::GpuCompute);
    CHECK(p.tile.tx == 1);
    CHECK(p.tile.ty == 1);
    CHECK(!p.fits);
}
