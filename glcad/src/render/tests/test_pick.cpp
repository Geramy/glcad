#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "render/pick.hpp"

using namespace grender;

namespace {

Aabb box_at(f64 x, f64 y, f64 z, f64 half = 0.5) {
    Aabb b;
    b.add(x - half, y - half, z - half);
    b.add(x + half, y + half, z + half);
    return b;
}

Pickable item(u32 id, f64 x, f64 y, f64 z, f64 half = 0.5) {
    return {box_at(x, y, z, half), id};
}

u32 brute_raycast(const std::vector<Pickable>& items, Ray r, f64* dist) {
    r.normalise();
    u32 best = kNoPick;
    f64 best_t = r.tmax;
    for (const Pickable& p : items) {
        f64 t0 = r.tmin, t1 = r.tmax;
        const f64 o[3] = {r.ox, r.oy, r.oz};
        const f64 d[3] = {r.dx, r.dy, r.dz};
        bool hit = true;
        for (int i = 0; i < 3 && hit; ++i) {
            if (std::fabs(d[i]) < 1e-300) {
                if (o[i] < p.box.lo[i] || o[i] > p.box.hi[i]) hit = false;
                continue;
            }
            const f64 inv = 1.0 / d[i];
            f64 a = (p.box.lo[i] - o[i]) * inv;
            f64 c = (p.box.hi[i] - o[i]) * inv;
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a);
            t1 = std::min(t1, c);
            if (t0 > t1) hit = false;
        }
        if (hit && t0 < best_t) { best_t = t0; best = p.id; }
    }
    if (best != kNoPick && dist) *dist = best_t;
    return best;
}

}

TEST(pick_id_round_trips_through_an_rgba8_pixel) {
    for (u32 id : {0u, 1u, 255u, 256u, 65535u, 65536u, 1000000u, kMaxPickId}) {
        const PickColor c = encode_pick(id);
        CHECK_MSG(decode_pick(c) == id, "id %u came back as %u", id,
                  decode_pick(c));
    }
}

TEST(pick_object_zero_is_distinguishable_from_the_background) {
    const PickColor zero = encode_pick(0);
    CHECK(zero.r == 0 && zero.g == 0 && zero.b == 0);
    CHECK_MSG(zero.a != 0, "object zero encoded as fully transparent");
    CHECK(decode_pick(zero) == 0);

    const PickColor background{0, 0, 0, 0};
    CHECK_MSG(decode_pick(background) == kNoPick,
              "an empty pixel decoded as object zero");
}

TEST(pick_an_id_too_large_for_the_buffer_is_refused_not_wrapped) {
    const PickColor c = encode_pick(kMaxPickId + 1);
    CHECK(c.a == 0);
    CHECK(decode_pick(c) == kNoPick);
}

TEST(aabb_growth_and_predicates) {
    Aabb a;
    CHECK(a.empty);
    CHECK(a.surface_area() == 0.0);
    CHECK(!a.intersects(box_at(0, 0, 0)));

    a.add(-1, -2, -3);
    a.add(1, 2, 3);
    CHECK(!a.empty);
    CHECK(std::fabs(a.extent(0) - 2.0) < 1e-12);
    CHECK(std::fabs(a.extent(1) - 4.0) < 1e-12);
    CHECK(std::fabs(a.centre(2)) < 1e-12);
    CHECK(std::fabs(a.surface_area() - (2 * (2 * 4 + 4 * 6 + 6 * 2))) < 1e-9);

    CHECK(a.contains(box_at(0, 0, 0, 0.5)));
    CHECK(!a.contains(box_at(0, 0, 0, 5.0)));
    CHECK(a.intersects(box_at(1.4, 0, 0, 0.5)));
    CHECK(!a.intersects(box_at(3, 0, 0, 0.5)));
}

TEST(aabb_distance_is_zero_inside_and_clamped_outside) {
    const Aabb b = box_at(0, 0, 0, 1.0);
    CHECK(std::fabs(b.distance_to(0, 0, 0)) < 1e-12);
    CHECK(std::fabs(b.distance_to(0.5, 0.5, 0.5)) < 1e-12);
    CHECK(std::fabs(b.distance_to(4, 0, 0) - 3.0) < 1e-12);
    CHECK(std::fabs(b.distance_to(4, 5, 1) - std::hypot(3.0, 4.0)) < 1e-9);
}

TEST(bvh_finds_the_nearest_object_along_a_ray) {
    std::vector<Pickable> items;
    for (u32 i = 0; i < 5; ++i) items.push_back(item(i, 0, 0, 2.0 * i));

    Bvh bvh;
    bvh.build(items);
    CHECK(bvh.size() == 5);

    Ray r;
    r.ox = 0; r.oy = 0; r.oz = -10;
    r.dz = 1;
    f64 d = 0.0;
    CHECK_MSG(bvh.raycast(r, &d) == 0, "hit %u, not the nearest",
              bvh.raycast(r));
    CHECK_MSG(std::fabs(d - 9.5) < 1e-9, "hit at %g", d);

    r.oz = 20;
    r.dz = -1;
    CHECK(bvh.raycast(r) == 4);
}

TEST(bvh_misses_report_no_pick) {
    std::vector<Pickable> items{item(7, 0, 0, 0)};
    Bvh bvh;
    bvh.build(items);

    Ray r;
    r.ox = 10; r.oy = 10; r.oz = -10;
    r.dz = 1;
    CHECK(bvh.raycast(r) == kNoPick);

    Bvh empty;
    CHECK(empty.raycast(r) == kNoPick);
    CHECK(empty.size() == 0);
    CHECK(empty.touching(box_at(0, 0, 0, 100)).empty());
}

TEST(bvh_handles_an_axis_aligned_ray) {
    std::vector<Pickable> items{item(1, 0, 0, 0, 1.0)};
    Bvh bvh;
    bvh.build(items);

    Ray along_x;
    along_x.ox = -10; along_x.oy = 0; along_x.oz = 0;
    along_x.dx = 1; along_x.dy = 0; along_x.dz = 0;
    CHECK(bvh.raycast(along_x) == 1);

    Ray parallel_miss = along_x;
    parallel_miss.oy = 5.0;
    CHECK(bvh.raycast(parallel_miss) == kNoPick);
}

TEST(bvh_agrees_with_the_brute_force_answer) {
    std::mt19937 rng(20260829u);
    std::uniform_real_distribution<f64> pos(-50.0, 50.0);
    std::uniform_real_distribution<f64> size(0.2, 3.0);

    std::vector<Pickable> items;
    for (u32 i = 0; i < 600; ++i)
        items.push_back(item(i, pos(rng), pos(rng), pos(rng), size(rng)));

    Bvh bvh;
    bvh.build(items);

    std::uniform_real_distribution<f64> dir(-1.0, 1.0);
    for (int k = 0; k < 400; ++k) {
        Ray r;
        r.ox = pos(rng); r.oy = pos(rng); r.oz = pos(rng);
        r.dx = dir(rng); r.dy = dir(rng); r.dz = dir(rng);
        if (std::fabs(r.dx) + std::fabs(r.dy) + std::fabs(r.dz) < 1e-6) continue;

        f64 want_d = 0.0, got_d = 0.0;
        const u32 want = brute_raycast(items, r, &want_d);
        const u32 got = bvh.raycast(r, &got_d);
        if (want == got) continue;
        CHECK_MSG(want != kNoPick && got != kNoPick &&
                      std::fabs(want_d - got_d) < 1e-9,
                  "ray %d: brute force found %u at %g, the bvh found %u at %g",
                  k, want, want_d, got, got_d);
    }
}

TEST(bvh_touching_and_enclosing_are_different_queries) {
    std::vector<Pickable> items{
        item(1, 0, 0, 0, 1.0),
        item(2, 4.5, 0, 0, 1.0),
        item(3, 20, 0, 0, 1.0),
    };
    Bvh bvh;
    bvh.build(items);

    Aabb query;
    query.add(-5, -5, -5);
    query.add(5, 5, 5);

    const auto crossing = bvh.touching(query);
    const auto window = bvh.enclosed_by(query);

    CHECK_MSG(crossing.size() == 2, "%zu touched", crossing.size());
    CHECK(crossing[0] == 1 && crossing[1] == 2);
    CHECK_MSG(window.size() == 1, "%zu enclosed", window.size());
    CHECK(window[0] == 1);
}

TEST(bvh_box_queries_agree_with_the_brute_force_answer) {
    std::mt19937 rng(7u);
    std::uniform_real_distribution<f64> pos(-30.0, 30.0);
    std::vector<Pickable> items;
    for (u32 i = 0; i < 300; ++i)
        items.push_back(item(i, pos(rng), pos(rng), pos(rng), 1.0));

    Bvh bvh;
    bvh.build(items);

    for (int k = 0; k < 60; ++k) {
        Aabb q;
        const f64 cx = pos(rng), cy = pos(rng), cz = pos(rng);
        const f64 h = 2.0 + std::fabs(pos(rng)) * 0.2;
        q.add(cx - h, cy - h, cz - h);
        q.add(cx + h, cy + h, cz + h);

        std::vector<u32> want_touch, want_hold;
        for (const Pickable& p : items) {
            if (q.intersects(p.box)) want_touch.push_back(p.id);
            if (q.contains(p.box)) want_hold.push_back(p.id);
        }
        std::sort(want_touch.begin(), want_touch.end());
        std::sort(want_hold.begin(), want_hold.end());
        CHECK(bvh.touching(q) == want_touch);
        CHECK(bvh.enclosed_by(q) == want_hold);
    }
}

TEST(bvh_does_not_degenerate_on_a_scene_with_an_outlier) {
    std::vector<Pickable> items;
    for (u32 i = 0; i < 256; ++i)
        items.push_back(item(i, static_cast<f64>(i % 16),
                             static_cast<f64>(i / 16), 0.0));
    items.push_back(item(999, 1e6, 1e6, 1e6));

    Bvh bvh;
    bvh.build(items);
    CHECK_MSG(bvh.depth() < 20, "the tree is %zu deep for %zu items",
              bvh.depth(), bvh.size());

    Ray r;
    r.ox = 1e6; r.oy = 1e6; r.oz = 1e6 - 100;
    r.dz = 1;
    CHECK(bvh.raycast(r) == 999);
}

TEST(bvh_survives_a_scene_of_identical_boxes) {
    std::vector<Pickable> items;
    for (u32 i = 0; i < 64; ++i) items.push_back(item(i, 0, 0, 0));

    Bvh bvh;
    bvh.build(items);
    CHECK(bvh.size() == 64);

    Aabb q;
    q.add(-1, -1, -1);
    q.add(1, 1, 1);
    CHECK_MSG(bvh.touching(q).size() == 64, "%zu of 64 identical boxes found",
              bvh.touching(q).size());
}

TEST(bvh_frustum_culling_keeps_what_is_half_on_screen) {
    std::vector<Pickable> items{
        item(1, 0, 0, 10, 1.0),
        item(2, 0, 0, -10, 1.0),
        item(3, 100, 0, 10, 1.0),
    };
    Bvh bvh;
    bvh.build(items);

    const f64 planes[6][4] = {
        {0, 0, -1, 1.0},     // near: z > 1
        {0, 0, 1, -100.0},   // far:  z < 100
        {-1, 0, 0, -20.0},   // x > -20
        {1, 0, 0, -20.0},    // x < 20
        {0, -1, 0, -20.0},
        {0, 1, 0, -20.0},
    };

    const auto seen = bvh.visible_in(planes);
    CHECK_MSG(seen.size() == 1, "%zu visible", seen.size());
    if (seen.size() == 1) CHECK(seen[0] == 1);

    Bvh edge;
    edge.build({item(4, -20.5, 0, 10, 1.0)});
    CHECK_MSG(edge.visible_in(planes).size() == 1,
              "a box half on screen was culled");
}

TEST(bvh_rebuild_replaces_rather_than_accumulates) {
    Bvh bvh;
    bvh.build({item(1, 0, 0, 0)});
    CHECK(bvh.size() == 1);
    bvh.build({item(2, 0, 0, 0), item(3, 5, 0, 0)});
    CHECK(bvh.size() == 2);
    Aabb q;
    q.add(-100, -100, -100);
    q.add(100, 100, 100);
    const auto all = bvh.touching(q);
    CHECK(all.size() == 2 && all[0] == 2 && all[1] == 3);

    bvh.clear();
    CHECK(bvh.size() == 0);
    CHECK(bvh.touching(q).empty());
}

TEST(lod_projected_size_is_independent_of_units) {
    const f64 eye_m[3] = {0, 0, 0};
    const f64 a = projected_size(box_at(0, 0, 10, 1.0), eye_m, 1080.0, 0.7854);

    const f64 b = projected_size(box_at(0, 0, 10000, 1000.0), eye_m, 1080.0, 0.7854);
    CHECK_MSG(std::fabs(a - b) < 1e-6, "%g pixels versus %g", a, b);
}

TEST(lod_projected_size_shrinks_with_distance_and_grows_with_the_viewport) {
    const f64 eye[3] = {0, 0, 0};
    const f64 near_px = projected_size(box_at(0, 0, 10, 1.0), eye, 1080.0, 0.7854);
    const f64 far_px = projected_size(box_at(0, 0, 100, 1.0), eye, 1080.0, 0.7854);
    CHECK(near_px > far_px);
    CHECK_MSG(std::fabs(near_px / far_px - 99.0 / 9.0) < 1e-6,
              "the ratio was %g", near_px / far_px);

    const f64 tall = projected_size(box_at(0, 0, 10, 1.0), eye, 2160.0, 0.7854);
    CHECK(std::fabs(tall - near_px * 2.0) < 1e-6);

    const f64 wide = projected_size(box_at(0, 0, 10, 1.0), eye, 1080.0, 1.5708);
    CHECK(wide < near_px);
}

TEST(lod_an_eye_inside_the_object_does_not_divide_by_zero) {
    const f64 eye[3] = {0, 0, 0};
    const f64 px = projected_size(box_at(0, 0, 0, 5.0), eye, 1080.0, 0.7854);
    CHECK(std::isfinite(px));
    CHECK(px >= 1080.0 - 1e-9);
}

TEST(lod_picks_the_level_the_ladder_says) {
    LodSettings s;
    CHECK(choose_lod(s, 500.0, kCulled) == 0);
    CHECK(choose_lod(s, 96.0, kCulled) == 0);
    CHECK(choose_lod(s, 50.0, kCulled) == 1);
    CHECK(choose_lod(s, 10.0, kCulled) == 2);
    CHECK(choose_lod(s, 3.0, kCulled) == 2);
    CHECK_MSG(choose_lod(s, 1.0, kCulled) == kCulled,
              "a one-pixel object was still drawn");
}

TEST(lod_hysteresis_stops_the_flicker_on_a_threshold) {
    LodSettings s;
    s.hysteresis = 0.2;

    CHECK_MSG(choose_lod(s, 90.0, 0) == 0, "level 0 dropped at 90 px");
    CHECK_MSG(choose_lod(s, 90.0, 1) == 1, "level 1 promoted at 90 px");
    CHECK(choose_lod(s, 96.0, 1) == 0);
    CHECK(choose_lod(s, 70.0, 0) == 1);

    CHECK(choose_lod(s, 1.9, 2) == 2);
    CHECK(choose_lod(s, 1.5, 2) == kCulled);
    CHECK(choose_lod(s, 1.9, kCulled) == kCulled);
}

TEST(lod_with_no_ladder_culls_rather_than_crashing) {
    LodSettings s;
    s.levels.clear();
    CHECK(choose_lod(s, 1000.0, kCulled) == kCulled);
}

TEST(lod_selects_a_level_for_every_item) {
    LodSettings s;
    std::vector<Pickable> items{
        item(0, 0, 0, 5, 1.0),
        item(1, 0, 0, 200, 1.0),
        item(2, 0, 0, 100000, 1.0),
    };
    const f64 eye[3] = {0, 0, 0};

    const auto first = select_lod(s, items, eye, {});
    CHECK(first.size() == 3);
    CHECK(first[0] == 0);
    CHECK(first[2] == kCulled);

    const auto second = select_lod(s, items, eye, first);
    CHECK(second == first);
}

TEST(stream_order_is_largest_on_screen_first) {
    LodSettings s;
    std::vector<Pickable> items{
        item(1, 0, 0, 200, 1.0),
        item(2, 0, 0, 20, 1.0),
        item(3, 0, 0, 60, 1.0),
    };
    const f64 eye[3] = {0, 0, 0};

    const auto q = stream_order(s, items, eye, 0);
    CHECK_MSG(q.size() == 3, "%zu requests", q.size());
    CHECK_MSG(q[0].id == 2, "the nearest part is not first");
    CHECK(q[1].id == 3);
    CHECK(q[2].id == 1);
    CHECK(q[0].priority > q[1].priority);
}

TEST(stream_order_does_not_request_what_will_not_be_drawn) {
    LodSettings s;
    std::vector<Pickable> items{
        item(1, 0, 0, 10, 1.0),
        item(2, 0, 0, 1000000, 1.0),
    };
    const f64 eye[3] = {0, 0, 0};
    const auto q = stream_order(s, items, eye, 0);
    CHECK_MSG(q.size() == 1, "%zu requests", q.size());
    CHECK(q[0].id == 1);
}

TEST(stream_order_respects_the_budget) {
    LodSettings s;
    std::vector<Pickable> items;
    for (u32 i = 0; i < 50; ++i)
        items.push_back(item(i, 0, 0, 10.0 + static_cast<f64>(i), 1.0));
    const f64 eye[3] = {0, 0, 0};

    const auto q = stream_order(s, items, eye, 5);
    CHECK(q.size() == 5);
    for (u32 i = 0; i < 5; ++i) CHECK(q[i].id == i);
}

TEST(stream_order_is_reproducible) {
    LodSettings s;
    std::vector<Pickable> items;
    for (u32 i = 0; i < 20; ++i) items.push_back(item(i, 0, 0, 30.0, 1.0));
    const f64 eye[3] = {0, 0, 0};

    const auto a = stream_order(s, items, eye, 0);
    const auto b = stream_order(s, items, eye, 0);
    CHECK(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i].id == b[i].id);
    for (u32 i = 0; i < a.size(); ++i) CHECK(a[i].id == i);
}

TEST(stream_order_carries_the_level_each_part_needs) {
    LodSettings s;
    std::vector<Pickable> items{
        item(1, 0, 0, 10, 1.0),
        item(2, 0, 0, 400, 1.0),
    };
    const f64 eye[3] = {0, 0, 0};
    const auto q = stream_order(s, items, eye, 0);
    CHECK(q.size() == 2);
    CHECK_MSG(q[0].detail < q[1].detail,
              "the near part was queued at a coarser level than the far one");
}
