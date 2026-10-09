#include "render/scene.hpp"

#include <algorithm>

namespace grender {

Transform Transform::then(const Transform& after) const noexcept {
    Transform out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            f64 sum = 0;
            for (int k = 0; k < 4; ++k) sum += after.m[r * 4 + k] * m[k * 4 + c];
            out.m[r * 4 + c] = sum;
        }
    return out;
}

void Transform::apply(f64 x, f64 y, f64 z, f64& ox, f64& oy, f64& oz) const noexcept {
    ox = m[0] * x + m[1] * y + m[2] * z + m[3];
    oy = m[4] * x + m[5] * y + m[6] * z + m[7];
    oz = m[8] * x + m[9] * y + m[10] * z + m[11];
}

void Bounds::add(f64 x, f64 y, f64 z) noexcept {
    if (empty) {
        lo[0] = hi[0] = x; lo[1] = hi[1] = y; lo[2] = hi[2] = z;
        empty = false;
        return;
    }
    lo[0] = std::min(lo[0], x); hi[0] = std::max(hi[0], x);
    lo[1] = std::min(lo[1], y); hi[1] = std::max(hi[1], y);
    lo[2] = std::min(lo[2], z); hi[2] = std::max(hi[2], z);
}

void Bounds::add(const Bounds& b) noexcept {
    if (b.empty) return;
    add(b.lo[0], b.lo[1], b.lo[2]);
    add(b.hi[0], b.hi[1], b.hi[2]);
}

Bounds Bounds::transformed(const Transform& t) const noexcept {
    if (empty) return *this;
    // All eight corners: a rotated box's extent is not its transformed extent.
    Bounds out;
    for (int i = 0; i < 8; ++i) {
        const f64 x = (i & 1) ? hi[0] : lo[0];
        const f64 y = (i & 2) ? hi[1] : lo[1];
        const f64 z = (i & 4) ? hi[2] : lo[2];
        f64 ox, oy, oz;
        t.apply(x, y, z, ox, oy, oz);
        out.add(ox, oy, oz);
    }
    return out;
}

namespace {
bool overlaps(const Bounds& a, const Bounds& b) noexcept {
    if (a.empty || b.empty) return false;
    for (int k = 0; k < 3; ++k)
        if (a.hi[k] < b.lo[k] || a.lo[k] > b.hi[k]) return false;
    return true;
}
}

bool Scene::put(const DrawItem& item) {
    const auto it = index_.find(item.id);
    ++revision_;
    if (it != index_.end()) {
        items_[it->second] = item;
        return false;
    }
    index_.emplace(item.id, items_.size());
    items_.push_back(item);
    return true;
}

bool Scene::erase(gdoc::EntityId id) {
    const auto it = index_.find(id);
    if (it == index_.end()) return false;
    const std::size_t at = it->second;
    // Swap with the back: nothing may hold a DrawItem* across an erase.
    if (at + 1 != items_.size()) {
        items_[at] = items_.back();
        index_[items_[at].id] = at;
    }
    items_.pop_back();
    index_.erase(it);
    ++revision_;
    return true;
}

void Scene::clear() {
    items_.clear();
    index_.clear();
    ++revision_;
}

DrawItem* Scene::find(gdoc::EntityId id) {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &items_[it->second];
}

const DrawItem* Scene::find(gdoc::EntityId id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &items_[it->second];
}

bool Scene::set_transform(gdoc::EntityId id, const Transform& t) {
    DrawItem* item = find(id);
    if (!item) return false;
    item->transform = t;
    ++revision_;
    return true;
}

bool Scene::set_visible(gdoc::EntityId id, bool visible) {
    DrawItem* item = find(id);
    if (!item || item->visible == visible) return false;
    item->visible = visible;
    ++revision_;
    return true;
}

std::vector<const DrawItem*> Scene::visible_in(const Bounds& region) const {
    std::vector<const DrawItem*> out;
    for (const auto& item : items_) {
        if (!item.visible) continue;
        if (!item.bounds.empty && !overlaps(item.bounds.transformed(item.transform), region))
            continue;
        out.push_back(&item);
    }
    return out;
}

std::vector<const DrawItem*> Scene::sorted_translucent(f64 vx, f64 vy, f64 vz) const {
    std::vector<const DrawItem*> out;
    for (const auto& item : items_)
        if (item.visible && item.material.translucent) out.push_back(&item);
    const auto depth = [&](const DrawItem* d) {
        if (d->bounds.empty) return 0.0;
        const Bounds b = d->bounds.transformed(d->transform);
        const f64 cx = (b.lo[0] + b.hi[0]) * 0.5;
        const f64 cy = (b.lo[1] + b.hi[1]) * 0.5;
        const f64 cz = (b.lo[2] + b.hi[2]) * 0.5;
        return cx * vx + cy * vy + cz * vz;
    };
    std::stable_sort(out.begin(), out.end(),
                     [&](const DrawItem* a, const DrawItem* b) { return depth(a) > depth(b); });
    return out;
}

Bounds Scene::bounds() const {
    Bounds all;
    for (const auto& item : items_)
        if (item.visible) all.add(item.bounds.transformed(item.transform));
    return all;
}

}
