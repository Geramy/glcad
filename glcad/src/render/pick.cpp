#include "render/pick.hpp"

#include <algorithm>
#include <cmath>

namespace grender {
namespace {

constexpr std::size_t kLeafSize = 4;
constexpr std::size_t kMaxDepth = 64;

}

PickColor encode_pick(u32 id) noexcept {
    PickColor c;
    if (id > kMaxPickId) return c;      // alpha 0: nothing here
    c.r = static_cast<u8>((id >> 16) & 0xFF);
    c.g = static_cast<u8>((id >> 8) & 0xFF);
    c.b = static_cast<u8>(id & 0xFF);
    c.a = 0xFF;
    return c;
}

u32 decode_pick(PickColor c) noexcept {
    if (c.a == 0) return kNoPick;
    return (static_cast<u32>(c.r) << 16) | (static_cast<u32>(c.g) << 8) |
           static_cast<u32>(c.b);
}

void Aabb::add(f64 x, f64 y, f64 z) noexcept {
    if (empty) {
        lo[0] = hi[0] = x;
        lo[1] = hi[1] = y;
        lo[2] = hi[2] = z;
        empty = false;
        return;
    }
    lo[0] = std::min(lo[0], x); hi[0] = std::max(hi[0], x);
    lo[1] = std::min(lo[1], y); hi[1] = std::max(hi[1], y);
    lo[2] = std::min(lo[2], z); hi[2] = std::max(hi[2], z);
}

void Aabb::add(const Aabb& o) noexcept {
    if (o.empty) return;
    add(o.lo[0], o.lo[1], o.lo[2]);
    add(o.hi[0], o.hi[1], o.hi[2]);
}

f64 Aabb::surface_area() const noexcept {
    if (empty) return 0.0;
    const f64 dx = extent(0), dy = extent(1), dz = extent(2);
    return 2.0 * (dx * dy + dy * dz + dz * dx);
}

f64 Aabb::radius() const noexcept {
    if (empty) return 0.0;
    const f64 dx = extent(0), dy = extent(1), dz = extent(2);
    return 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool Aabb::intersects(const Aabb& o) const noexcept {
    if (empty || o.empty) return false;
    for (int i = 0; i < 3; ++i)
        if (hi[i] < o.lo[i] || lo[i] > o.hi[i]) return false;
    return true;
}

bool Aabb::contains(const Aabb& o) const noexcept {
    if (empty || o.empty) return false;
    for (int i = 0; i < 3; ++i)
        if (o.lo[i] < lo[i] || o.hi[i] > hi[i]) return false;
    return true;
}

f64 Aabb::distance_to(f64 x, f64 y, f64 z) const noexcept {
    if (empty) return 1e30;
    const f64 p[3] = {x, y, z};
    f64 d2 = 0.0;
    for (int i = 0; i < 3; ++i) {
        const f64 v = p[i] < lo[i] ? lo[i] - p[i] : (p[i] > hi[i] ? p[i] - hi[i] : 0.0);
        d2 += v * v;
    }
    return std::sqrt(d2);
}

void Ray::normalise() noexcept {
    const f64 n = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (n < 1e-300) return;
    dx /= n;
    dy /= n;
    dz /= n;
}

namespace {

bool slab(const Aabb& b, const Ray& r, f64& t_enter) {
    if (b.empty) return false;
    f64 t0 = r.tmin, t1 = r.tmax;
    const f64 o[3] = {r.ox, r.oy, r.oz};
    const f64 d[3] = {r.dx, r.dy, r.dz};

    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-300) {
            // Parallel slab: dividing by zero fails on axis-aligned rays.
            if (o[i] < b.lo[i] || o[i] > b.hi[i]) return false;
            continue;
        }
        const f64 inv = 1.0 / d[i];
        f64 a = (b.lo[i] - o[i]) * inv;
        f64 c = (b.hi[i] - o[i]) * inv;
        if (a > c) std::swap(a, c);
        t0 = std::max(t0, a);
        t1 = std::min(t1, c);
        if (t0 > t1) return false;
    }
    t_enter = t0;
    return true;
}

bool outside_frustum(const Aabb& b, const f64 planes[6][4]) {
    if (b.empty) return true;
    for (int p = 0; p < 6; ++p) {
        // Test the most-inside corner (min n.x): if even it is outside, so is the box.
        const f64 x = planes[p][0] > 0.0 ? b.lo[0] : b.hi[0];
        const f64 y = planes[p][1] > 0.0 ? b.lo[1] : b.hi[1];
        const f64 z = planes[p][2] > 0.0 ? b.lo[2] : b.hi[2];
        if (planes[p][0] * x + planes[p][1] * y + planes[p][2] * z + planes[p][3] > 0.0)
            return true;
    }
    return false;
}

}

void Bvh::clear() noexcept {
    items_.clear();
    nodes_.clear();
    root_bounds_ = Aabb{};
}

void Bvh::build(std::vector<Pickable> items) {
    items_ = std::move(items);
    nodes_.clear();
    root_bounds_ = Aabb{};
    if (items_.empty()) return;

    for (const Pickable& p : items_) root_bounds_.add(p.box);
    nodes_.reserve(items_.size() * 2);
    build_range(0, static_cast<u32>(items_.size()), 0);
}

u32 Bvh::build_range(u32 begin, u32 end, std::size_t depth) {
    const u32 index = static_cast<u32>(nodes_.size());
    nodes_.push_back(Node{});

    Aabb box;
    for (u32 i = begin; i < end; ++i) box.add(items_[i].box);
    nodes_[index].box = box;

    const u32 count = end - begin;
    if (count <= kLeafSize || depth >= kMaxDepth) {
        nodes_[index].left = begin;
        nodes_[index].count = count;
        return index;
    }

    // Median of the centres, not the box midpoint: one outlier would degenerate it.
    int axis = 0;
    f64 widest = box.extent(0);
    if (box.extent(1) > widest) { widest = box.extent(1); axis = 1; }
    if (box.extent(2) > widest) { axis = 2; }

    const u32 mid = begin + count / 2;
    std::nth_element(items_.begin() + begin, items_.begin() + mid,
                     items_.begin() + end,
                     [axis](const Pickable& a, const Pickable& b) {
                         return a.box.centre(axis) < b.box.centre(axis);
                     });

    // Identical centres: split at the median index anyway to stay finite.
    const u32 left = build_range(begin, mid, depth + 1);
    const u32 right = build_range(mid, end, depth + 1);
    nodes_[index].left = left;
    nodes_[index].right = right;
    nodes_[index].count = 0;
    return index;
}

std::size_t Bvh::depth() const noexcept {
    if (nodes_.empty()) return 0;
    // Iterative: a deep tree would overflow the stack.
    std::vector<std::pair<u32, std::size_t>> stack{{0, 1}};
    std::size_t deepest = 0;
    while (!stack.empty()) {
        const auto [n, d] = stack.back();
        stack.pop_back();
        deepest = std::max(deepest, d);
        if (nodes_[n].count != 0) continue;
        stack.push_back({nodes_[n].left, d + 1});
        stack.push_back({nodes_[n].right, d + 1});
    }
    return deepest;
}

u32 Bvh::raycast(const Ray& ray, f64* distance) const {
    if (nodes_.empty()) return kNoPick;

    Ray r = ray;
    r.normalise();

    u32 best = kNoPick;
    f64 best_t = r.tmax;

    std::vector<u32> stack{0};
    while (!stack.empty()) {
        const u32 n = stack.back();
        stack.pop_back();
        const Node& node = nodes_[n];

        f64 t_enter = 0.0;
        if (!slab(node.box, r, t_enter)) continue;
        if (t_enter > best_t) continue;

        if (node.count != 0) {
            for (u32 i = node.left; i < node.left + node.count; ++i) {
                f64 t = 0.0;
                if (!slab(items_[i].box, r, t)) continue;
                if (t < best_t) {
                    best_t = t;
                    best = items_[i].id;
                }
            }
            continue;
        }

        stack.push_back(node.left);
        stack.push_back(node.right);
    }

    if (best != kNoPick && distance) *distance = best_t;
    return best;
}

void Bvh::gather(u32 n, const Aabb& box, bool require_containment,
                 std::vector<u32>& out) const {
    if (nodes_.empty()) return;
    std::vector<u32> stack{n};
    while (!stack.empty()) {
        const u32 i = stack.back();
        stack.pop_back();
        const Node& node = nodes_[i];
        if (!box.intersects(node.box)) continue;

        if (node.count != 0) {
            for (u32 k = node.left; k < node.left + node.count; ++k) {
                const bool hit = require_containment ? box.contains(items_[k].box)
                                                     : box.intersects(items_[k].box);
                if (hit) out.push_back(items_[k].id);
            }
            continue;
        }

        stack.push_back(node.left);
        stack.push_back(node.right);
    }
}

std::vector<u32> Bvh::touching(const Aabb& box) const {
    std::vector<u32> out;
    if (!nodes_.empty()) gather(0, box, false, out);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<u32> Bvh::enclosed_by(const Aabb& box) const {
    std::vector<u32> out;
    if (!nodes_.empty()) gather(0, box, true, out);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<u32> Bvh::visible_in(const f64 planes[6][4]) const {
    std::vector<u32> out;
    if (nodes_.empty()) return out;

    std::vector<u32> stack{0};
    while (!stack.empty()) {
        const u32 i = stack.back();
        stack.pop_back();
        const Node& node = nodes_[i];
        if (outside_frustum(node.box, planes)) continue;

        if (node.count != 0) {
            for (u32 k = node.left; k < node.left + node.count; ++k)
                if (!outside_frustum(items_[k].box, planes))
                    out.push_back(items_[k].id);
            continue;
        }

        stack.push_back(node.left);
        stack.push_back(node.right);
    }
    std::sort(out.begin(), out.end());
    return out;
}

f64 projected_size(const Aabb& box, const f64 eye[3], f64 viewport_height,
                   f64 fov_y_radians) {
    if (box.empty) return 0.0;
    const f64 r = box.radius();
    if (r <= 0.0) return 0.0;

    const f64 d = box.distance_to(eye[0], eye[1], eye[2]);
    if (d <= r) return viewport_height;

    const f64 half_fov = fov_y_radians * 0.5;
    const f64 t = std::tan(half_fov);
    if (t <= 0.0) return viewport_height;

    return viewport_height * r / (d * t);
}

u32 choose_lod(const LodSettings& s, f64 pixels, u32 previous) {
    if (s.levels.empty()) return kCulled;

    const f64 slack = std::max(0.0, s.hysteresis);

    auto threshold = [&](std::size_t i, bool held) {
        const f64 t = s.levels[i].min_pixels;
        return held ? t * (1.0 - slack) : t;
    };

    for (std::size_t i = 0; i < s.levels.size(); ++i) {
        const bool held = previous == s.levels[i].detail;
        if (pixels >= threshold(i, held)) return s.levels[i].detail;
    }

    const bool was_visible = previous != kCulled;
    const f64 cull = was_visible ? s.cull_below_pixels * (1.0 - slack)
                                 : s.cull_below_pixels;
    if (pixels < cull) return kCulled;
    return s.levels.back().detail;
}

std::vector<u32> select_lod(const LodSettings& s, const std::vector<Pickable>& items,
                            const f64 eye[3], const std::vector<u32>& previous) {
    std::vector<u32> out;
    out.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const f64 px = projected_size(items[i].box, eye, s.viewport_height,
                                      s.fov_y_radians);
        const u32 was = i < previous.size() ? previous[i] : kCulled;
        out.push_back(choose_lod(s, px, was));
    }
    return out;
}

std::vector<StreamRequest> stream_order(const LodSettings& s,
                                        const std::vector<Pickable>& items,
                                        const f64 eye[3], std::size_t budget) {
    std::vector<StreamRequest> out;
    for (const Pickable& p : items) {
        const f64 px = projected_size(p.box, eye, s.viewport_height,
                                      s.fov_y_radians);
        const u32 level = choose_lod(s, px, kCulled);
        if (level == kCulled) continue;
        out.push_back({p.id, level, px});
    }
    std::sort(out.begin(), out.end(),
              [](const StreamRequest& a, const StreamRequest& b) {
                  if (a.priority != b.priority) return a.priority > b.priority;
                  return a.id < b.id;
              });
    if (budget && out.size() > budget) out.resize(budget);
    return out;
}

}
