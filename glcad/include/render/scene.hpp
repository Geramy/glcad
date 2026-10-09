#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "engine/entity.hpp"
#include "render/vertex.hpp"

namespace grender {

using gcm::f64;
using gcm::u32;
using gcm::u64;

// Row-major, metres. Placement only; geometry is never baked through it.
struct Transform {
    f64 m[16]{1, 0, 0, 0,
              0, 1, 0, 0,
              0, 0, 1, 0,
              0, 0, 0, 1};

    static Transform identity() noexcept { return {}; }
    bool is_identity() const noexcept {
        const Transform i;
        for (int k = 0; k < 16; ++k)
            if (m[k] != i.m[k]) return false;
        return true;
    }
    static Transform translation(f64 x, f64 y, f64 z) noexcept {
        Transform t;
        t.m[3] = x; t.m[7] = y; t.m[11] = z;
        return t;
    }
    // Row-major multiply: `this` then `after`.
    Transform then(const Transform& after) const noexcept;
    void apply(f64 x, f64 y, f64 z, f64& ox, f64& oy, f64& oz) const noexcept;
};

struct Bounds {
    f64  lo[3]{0, 0, 0}, hi[3]{0, 0, 0};
    bool empty{true};

    void add(f64 x, f64 y, f64 z) noexcept;
    void add(const Bounds& b) noexcept;
    Bounds transformed(const Transform& t) const noexcept;
};

struct Material {
    Color colour{Color::rgb(1, 1, 1)};
    bool  translucent{false};
    f64   depth_bias{0.0};
};

enum class DrawKind : gcm::u8 { Mesh, Lines, Points, Text, Overlay };

struct DrawItem {
    gdoc::EntityId id{};
    DrawKind       kind{DrawKind::Mesh};
    Transform      transform{};
    Material       material{};
    Bounds         bounds{};
    bool           visible{true};
    std::size_t    first{0}, count{0};
    u32            space{gdoc::kNoSpace};
};

class Scene {
public:
    bool put(const DrawItem& item);
    bool erase(gdoc::EntityId id);
    void clear();

    DrawItem*       find(gdoc::EntityId id);
    const DrawItem* find(gdoc::EntityId id) const;

    bool set_transform(gdoc::EntityId id, const Transform& t);
    bool set_visible(gdoc::EntityId id, bool visible);

    const std::vector<DrawItem>& items() const noexcept { return items_; }
    std::size_t size() const noexcept { return items_.size(); }
    u64 revision() const noexcept { return revision_; }

    std::vector<const DrawItem*> visible_in(const Bounds& region) const;

    std::vector<const DrawItem*> sorted_translucent(f64 vx, f64 vy, f64 vz) const;

    Bounds bounds() const;

private:
    std::vector<DrawItem> items_;
    std::unordered_map<gdoc::EntityId, std::size_t, gdoc::EntityIdHash> index_;
    u64 revision_{1};
};

}
