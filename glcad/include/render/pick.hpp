#pragma once

#include <cstdint>
#include <vector>

#include "render/vertex.hpp"

namespace grender {

using ggeo::f64;
using ggeo::u32;
using ggeo::u8;

// 24 bits of id + 8 of coverage, so background differs from object 0.
inline constexpr u32 kNoPick = 0xFFFFFFFFu;
inline constexpr u32 kMaxPickId = 0x00FFFFFFu;

struct PickColor {
    u8 r{0}, g{0}, b{0}, a{0};
};

PickColor encode_pick(u32 id) noexcept;
u32       decode_pick(PickColor c) noexcept;

struct Aabb {
    f64 lo[3]{0, 0, 0};
    f64 hi[3]{0, 0, 0};
    bool empty{true};

    void add(f64 x, f64 y, f64 z) noexcept;
    void add(const Aabb& o) noexcept;
    f64  centre(int axis) const noexcept {
        return (lo[axis] + hi[axis]) * 0.5;
    }
    f64  extent(int axis) const noexcept { return hi[axis] - lo[axis]; }
    f64  surface_area() const noexcept;
    f64  radius() const noexcept;
    bool intersects(const Aabb& o) const noexcept;
    bool contains(const Aabb& o) const noexcept;
    f64  distance_to(f64 x, f64 y, f64 z) const noexcept;
};

struct Ray {
    f64 ox{0}, oy{0}, oz{0};
    f64 dx{0}, dy{0}, dz{1};
    f64 tmin{0.0}, tmax{1e30};

    void normalise() noexcept;
};

struct Pickable {
    Aabb box;
    u32  id{0};
};

class Bvh {
public:
    void build(std::vector<Pickable> items);
    void clear() noexcept;

    std::size_t size() const noexcept { return items_.size(); }
    std::size_t node_count() const noexcept { return nodes_.size(); }
    std::size_t depth() const noexcept;
    const Aabb& bounds() const noexcept { return root_bounds_; }

    u32 raycast(const Ray& r, f64* distance = nullptr) const;

    std::vector<u32> touching(const Aabb& box) const;
    std::vector<u32> enclosed_by(const Aabb& box) const;

    // Six planes nx,ny,nz,d with the interior on the negative side.
    std::vector<u32> visible_in(const f64 planes[6][4]) const;

private:
    struct Node {
        Aabb box;
        u32  left{0};      // child index, or the first item for a leaf
        u32  right{0};
        u32  count{0};     // 0 for an interior node
    };

    u32  build_range(u32 begin, u32 end, std::size_t depth);
    void gather(u32 node, const Aabb& box, bool require_containment,
                std::vector<u32>& out) const;

    std::vector<Pickable> items_;
    std::vector<Node>     nodes_;
    Aabb                  root_bounds_;
};

f64 projected_size(const Aabb& box, const f64 eye[3], f64 viewport_height,
                   f64 fov_y_radians);

struct LodLevel {
    u32 detail{0};          // 0 is the finest
    f64 min_pixels{0.0};
};

struct LodSettings {
    std::vector<LodLevel> levels{{0, 96.0}, {1, 24.0}, {2, 6.0}};
    f64 cull_below_pixels{2.0};

    // Hysteresis fraction; stops flicker on a threshold.
    f64 hysteresis{0.15};

    f64 viewport_height{1080.0};
    f64 fov_y_radians{0.7853981633974483};
};

inline constexpr u32 kCulled = 0xFFFFFFFFu;

u32 choose_lod(const LodSettings& s, f64 pixels, u32 previous);

std::vector<u32> select_lod(const LodSettings& s, const std::vector<Pickable>& items,
                            const f64 eye[3], const std::vector<u32>& previous);

struct StreamRequest {
    u32 id{0};
    u32 detail{0};
    f64 priority{0.0};      // larger is more urgent
};

std::vector<StreamRequest> stream_order(const LodSettings& s,
                                        const std::vector<Pickable>& items,
                                        const f64 eye[3],
                                        std::size_t budget);

}
