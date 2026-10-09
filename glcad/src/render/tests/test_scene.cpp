#include "test.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "render/render_graph.hpp"
#include "render/scene.hpp"

using namespace grender;

namespace {
gdoc::EntityId id_of(gcm::u64 n) { return gdoc::EntityId{0, n}; }

DrawItem box(gcm::u64 n, double x, double size = 1.0, bool translucent = false) {
    DrawItem d;
    d.id = id_of(n);
    d.kind = DrawKind::Mesh;
    d.bounds.add(x, 0, 0);
    d.bounds.add(x + size, size, size);
    d.material.translucent = translucent;
    return d;
}
}

TEST(scene_keeps_objects_addressable_by_id) {
    Scene s;
    CHECK(s.put(box(1, 0)));
    CHECK(s.put(box(2, 10)));
    CHECK(!s.put(box(1, 5)));
    CHECK_MSG(s.size() == 2, "%zu items", s.size());
    CHECK(s.find(id_of(1)) != nullptr);
    CHECK(s.find(id_of(99)) == nullptr);

    const auto rev = s.revision();
    CHECK(s.erase(id_of(1)));
    CHECK(!s.erase(id_of(1)));
    CHECK(s.size() == 1);
    CHECK(s.revision() != rev);
    CHECK_MSG(s.find(id_of(2)) != nullptr, "erase lost the surviving object");
}

TEST(scene_moves_one_object_without_touching_geometry) {
    Scene s;
    s.put(box(1, 0));
    const auto rev = s.revision();
    const auto before = s.find(id_of(1))->bounds;

    CHECK(s.set_transform(id_of(1), Transform::translation(5, 0, 0)));
    CHECK_MSG(s.revision() != rev, "moving an object did not register");

    const auto& item = *s.find(id_of(1));
    CHECK_MSG(item.bounds.lo[0] == before.lo[0], "geometry was modified by a move");
    const auto placed = item.bounds.transformed(item.transform);
    CHECK_MSG(placed.lo[0] == before.lo[0] + 5.0, "the placement did not apply");

    CHECK(!s.set_transform(id_of(42), Transform::identity()));
}

TEST(scene_culls_by_bounds_and_visibility) {
    Scene s;
    s.put(box(1, 0));
    s.put(box(2, 100));
    Bounds near;
    near.add(-1, -1, -1);
    near.add(2, 2, 2);

    auto hit = s.visible_in(near);
    CHECK_MSG(hit.size() == 1, "%zu items survived the cull", hit.size());
    if (!hit.empty()) CHECK(hit.front()->id == id_of(1));

    s.set_transform(id_of(2), Transform::translation(-100, 0, 0));
    CHECK_MSG(s.visible_in(near).size() == 2, "the cull ignored a transform");

    s.set_visible(id_of(1), false);
    CHECK_MSG(s.visible_in(near).size() == 1, "an invisible object was drawn");
}

TEST(scene_sorts_translucent_back_to_front) {
    Scene s;
    s.put(box(1, 0,  1.0, true));
    s.put(box(2, 50, 1.0, true));
    s.put(box(3, 25, 1.0, false));

    auto sorted = s.sorted_translucent(1, 0, 0);
    CHECK_MSG(sorted.size() == 2, "%zu translucent", sorted.size());
    if (sorted.size() == 2)
        CHECK_MSG(sorted[0]->id == id_of(2), "not farthest-first");
}

namespace {
Pass named(const char* n, std::vector<ResourceId> reads,
           std::vector<ResourceId> writes, bool presents = false) {
    Pass p;
    p.name = n;
    p.reads = std::move(reads);
    p.writes = std::move(writes);
    p.presents = presents;
    return p;
}
}

TEST(render_graph_orders_passes_by_what_they_read) {
    RenderGraph g;
    g.add(named("present", {"colour"}, {"screen"}, true));
    g.add(named("opaque",  {"depth"},  {"colour"}));
    g.add(named("depth prepass", {}, {"depth"}));

    const auto plan = g.compile();
    CHECK_MSG(plan.ok(), "compile failed: %s", plan.detail.c_str());
    CHECK_MSG(plan.order.size() == 3, "%zu passes ordered", plan.order.size());
    if (plan.order.size() == 3) {
        CHECK_MSG(g.passes()[plan.order[0]].name == "depth prepass",
                  "first was %s", g.passes()[plan.order[0]].name.c_str());
        CHECK(g.passes()[plan.order[1]].name == "opaque");
        CHECK(g.passes()[plan.order[2]].name == "present");
    }
}

TEST(render_graph_culls_what_nothing_consumes) {
    RenderGraph g;
    g.add(named("present", {"colour"}, {"screen"}, true));
    g.add(named("opaque", {}, {"colour"}));
    g.add(named("shadow map", {}, {"shadows"}));

    const auto plan = g.compile();
    CHECK(plan.ok());
    CHECK_MSG(plan.culled.size() == 1, "%zu culled", plan.culled.size());
    if (!plan.culled.empty())
        CHECK_MSG(plan.culled.front() == "shadow map", "culled the wrong pass");
    CHECK(plan.order.size() == 2);
}

TEST(render_graph_refuses_a_cycle_rather_than_guess) {
    RenderGraph g;
    g.add(named("a", {"b_out"}, {"a_out"}, true));
    g.add(named("b", {"a_out"}, {"b_out"}));

    const auto plan = g.compile();
    CHECK_MSG(!plan.ok(), "a cycle compiled");
    CHECK(plan.error == GraphError::Cycle);
    CHECK(plan.order.empty());
}

TEST(render_graph_names_a_resource_nothing_writes) {
    RenderGraph g;
    g.add(named("opaque", {"depth"}, {"colour"}, true));
    const auto plan = g.compile();
    CHECK(!plan.ok());
    CHECK(plan.error == GraphError::MissingProducer);
    CHECK_MSG(plan.detail.find("depth") != std::string::npos,
              "the error did not name the resource: %s", plan.detail.c_str());
}

TEST(render_graph_executes_in_the_compiled_order) {
    RenderGraph g;
    std::vector<std::string> ran;
    auto record = [&ran](const char* n) {
        return [&ran, n](PassContext&) { ran.push_back(n); };
    };
    Pass present = named("present", {"colour"}, {"screen"}, true);
    present.execute = record("present");
    Pass opaque = named("opaque", {"depth"}, {"colour"});
    opaque.execute = record("opaque");
    Pass prepass = named("depth prepass", {}, {"depth"});
    prepass.execute = record("depth prepass");
    g.add(present); g.add(opaque); g.add(prepass);

    Scene scene;
    PassContext ctx;
    ctx.scene = &scene;
    CHECK(g.execute(ctx));
    CHECK_MSG(ran.size() == 3, "%zu passes ran", ran.size());
    if (ran.size() == 3) {
        CHECK(ran[0] == "depth prepass");
        CHECK(ran[2] == "present");
    }

    RenderGraph bad;
    Pass p = named("solo", {"missing"}, {"screen"}, true);
    bool touched = false;
    p.execute = [&touched](PassContext&) { touched = true; };
    bad.add(p);
    CHECK(!bad.execute(ctx));
    CHECK_MSG(!touched, "a pass ran despite a failed compile");
}

TEST(the_frame_the_renderer_declares_compiles_to_the_order_it_needs) {
    RenderGraph g;
    const auto pass = [&](const char* name, std::vector<ResourceId> reads,
                          std::vector<ResourceId> writes, bool presents) {
        Pass p;
        p.name = name;
        p.reads = std::move(reads);
        p.writes = std::move(writes);
        p.presents = presents;
        g.add(std::move(p));
    };
    pass("ground",  {},                  {"colour", "depth"}, false);
    pass("images",  {"colour", "depth"}, {"colour"},          false);
    pass("solids",  {"colour", "depth"}, {"colour", "depth"}, false);
    pass("drawing", {"colour", "depth"}, {"colour", "depth"}, false);
    pass("ui",      {"colour"},          {"colour"},          true);

    const CompileResult plan = g.compile();
    CHECK_MSG(plan.ok(), "%s", plan.detail.c_str());
    CHECK_MSG(plan.order.size() == 5, "%zu passes survived", plan.order.size());
    CHECK_MSG(plan.culled.empty(), "culled %zu passes of a frame that needs them",
              plan.culled.size());

    CHECK_MSG(g.passes()[plan.order.front()].name == "ground",
              "first pass is \"%s\"", g.passes()[plan.order.front()].name.c_str());
    CHECK_MSG(g.passes()[plan.order.back()].name == "ui",
              "last pass is \"%s\"", g.passes()[plan.order.back()].name.c_str());

    std::vector<std::string> ran;
    for (const auto i : plan.order) ran.push_back(g.passes()[i].name);
    const auto at = [&](const char* n) {
        return std::size_t(std::find(ran.begin(), ran.end(), n) - ran.begin());
    };
    CHECK(at("ground") < at("images"));
    CHECK(at("ground") < at("solids"));
    CHECK(at("solids") < at("drawing"));
    CHECK(at("drawing") < at("ui"));
}

TEST(a_pass_nobody_looks_at_is_dropped_from_the_frame) {
    RenderGraph g;
    Pass shadows; shadows.name = "shadows"; shadows.writes = {"shadow_map"};
    g.add(std::move(shadows));
    Pass ground; ground.name = "ground"; ground.writes = {"colour"};
    g.add(std::move(ground));
    Pass ui; ui.name = "ui"; ui.reads = {"colour"}; ui.writes = {"colour"};
    ui.presents = true;
    g.add(std::move(ui));

    const CompileResult plan = g.compile();
    CHECK(plan.ok());
    CHECK_MSG(plan.order.size() == 2, "%zu passes ran", plan.order.size());
    CHECK_MSG(plan.culled.size() == 1 && plan.culled.front() == "shadows",
              "culled %zu passes", plan.culled.size());
}

TEST(passes_that_all_blend_into_one_target_are_a_chain_not_a_cycle) {
    RenderGraph g;
    std::vector<std::string> ran;
    Pass clear;
    clear.name = "clear";
    clear.writes = {"colour"};
    clear.execute = [&ran](PassContext&) { ran.push_back("clear"); };
    g.add(std::move(clear));
    for (const char* name : {"first", "second", "third"}) {
        Pass p;
        p.name = name;
        p.reads = {"colour"};
        p.writes = {"colour"};
        p.presents = std::string(name) == "third";
        p.execute = [&ran, name](PassContext&) { ran.push_back(name); };
        g.add(std::move(p));
    }

    PassContext ctx;
    CHECK_MSG(g.execute(ctx), "a read-modify-write chain would not compile");
    CHECK_MSG(ran.size() == 4, "%zu passes ran", ran.size());
    if (ran.size() == 4) {
        CHECK_MSG(ran[0] == "clear", "first to run was \"%s\"", ran[0].c_str());
        CHECK(ran[1] == "first");
        CHECK(ran[2] == "second");
        CHECK(ran[3] == "third");
    }
}

TEST(a_frame_compiled_once_replays_without_recompiling) {
    RenderGraph g;
    std::vector<std::string> ran;
    Pass clear;
    clear.name = "clear";
    clear.writes = {"colour"};
    clear.execute = [&ran](PassContext&) { ran.push_back("clear"); };
    g.add(std::move(clear));
    Pass ui;
    ui.name = "ui";
    ui.reads = {"colour"};
    ui.writes = {"colour"};
    ui.presents = true;
    ui.execute = [&ran](PassContext&) { ran.push_back("ui"); };
    g.add(std::move(ui));

    const CompileResult plan = g.compile();
    CHECK(plan.ok());
    PassContext ctx;
    for (int frame = 0; frame < 3; ++frame) CHECK(g.execute(ctx, plan));
    CHECK_MSG(ran.size() == 6, "%zu passes ran over 3 frames", ran.size());
    if (ran.size() == 6) { CHECK(ran[0] == "clear"); CHECK(ran[5] == "ui"); }

    RenderGraph bad;
    Pass solo;
    solo.name = "solo";
    solo.reads = {"missing"};
    solo.presents = true;
    solo.execute = [&ran](PassContext&) { ran.push_back("solo"); };
    bad.add(std::move(solo));
    const CompileResult broken = bad.compile();
    CHECK(!broken.ok());
    CHECK(!bad.execute(ctx, broken));
    CHECK_MSG(ran.size() == 6, "a failed plan still ran a pass");
}
