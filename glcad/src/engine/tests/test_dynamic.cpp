#include "test.hpp"

#include <cmath>
#include <string>

#include "engine/block.hpp"
#include "engine/underlay.hpp"

using namespace gdoc;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
Point2 pt(const gcm::WorkingVolume& v, f64 x, f64 y) {
    return gcm::to_lattice(v, x, y);
}
gsnap::Shape seg(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = pt(v, x0, y0);
    s.b = pt(v, x1, y1);
    return s;
}
f64 wx(const gcm::WorkingVolume& v, Point2 p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    (void)y;
    return x;
}
f64 wy(const gcm::WorkingVolume& v, Point2 p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    (void)x;
    return y;
}

BlockDef door(const gcm::WorkingVolume& v) {
    BlockDef d;
    d.name = "DOOR";
    d.geometry = {
        seg(v, 0, 0, 0, 2.1),        // 0: the hinge jamb, fixed
        seg(v, 0, 2.1, 0.9, 2.1),    // 1: the head, stretches
        seg(v, 0.9, 2.1, 0.9, 0),    // 2: the strike jamb, moves
    };
    return d;
}

}

TEST(dynamic_a_parameter_clamps_rather_than_refusing) {
    BlockTable t;
    DynamicBlock& d = t.make_dynamic("DOOR");
    BlockParameter p;
    p.name = "Width";
    p.minimum = 0.0;
    p.maximum = 0.5;
    d.parameters.push_back(p);

    CHECK(d.set_parameter("Width", 10.0));
    CHECK_MSG(std::fabs(d.parameter("Width")->value - 0.5) < 1e-12,
              "clamped to %.6f", d.parameter("Width")->value);
    CHECK(d.set_parameter("Width", -3.0));
    CHECK(std::fabs(d.parameter("Width")->value) < 1e-12);
    CHECK(!d.set_parameter("Nonexistent", 1.0));
}

TEST(dynamic_an_increment_admits_only_its_steps) {
    BlockParameter p;
    p.minimum = 0.0;
    p.maximum = 1.0;
    p.increment = 0.1;

    CHECK(p.valid_value(0.3));
    CHECK_MSG(!p.valid_value(0.35), "an off-step value was accepted");
    CHECK(std::fabs(p.clamp_value(0.34) - 0.3) < 1e-9);
    CHECK(std::fabs(p.clamp_value(0.36) - 0.4) < 1e-9);
}

TEST(dynamic_an_unconstrained_parameter_takes_anything) {
    BlockParameter p;      // minimum == maximum == 0
    CHECK(p.valid_value(-1e6));
    CHECK(p.valid_value(1e6));
    CHECK(std::fabs(p.clamp_value(42.0) - 42.0) < 1e-12);
}

TEST(dynamic_named_choices) {
    BlockTable t;
    DynamicBlock& d = t.make_dynamic("DOOR");
    BlockParameter p;
    p.name = "Size";
    p.kind = ParameterKind::Lookup;
    p.choices = {"762", "838", "900"};
    d.parameters.push_back(p);

    CHECK(d.set_choice("Size", "838"));
    CHECK(d.parameter("Size")->choice == 1);
    CHECK_MSG(!d.set_choice("Size", "1200"), "an unlisted choice was accepted");
    CHECK(!d.set_choice("Missing", "838"));
}

TEST(dynamic_stretch_moves_only_what_is_inside_its_window) {
    const auto v = vol();
    const BlockDef def = door(v);

    BlockTable t;
    DynamicBlock& d = t.make_dynamic("DOOR");
    BlockParameter p;
    p.name = "Width";
    p.kind = ParameterKind::Linear;
    d.parameters.push_back(p);

    BlockAction a;
    a.kind = ActionKind::Stretch;
    a.parameter = "Width";
    a.geometry = {1, 2};
    a.dx = 1.0;
    a.window_lo = pt(v, 0.5, -1.0);
    a.window_hi = pt(v, 2.0, 3.0);
    d.actions.push_back(a);

    d.set_parameter("Width", 0.3);
    const DynamicResult r = evaluate(def, d, v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK(r.geometry.size() == 3);

    CHECK_MSG(std::fabs(wx(v, r.geometry[0].a)) < 1e-6, "the hinge jamb moved");
    CHECK_MSG(std::fabs(wx(v, r.geometry[1].b) - 1.2) < 1e-4,
              "the head ended at %.6f", wx(v, r.geometry[1].b));
    CHECK_MSG(std::fabs(wx(v, r.geometry[1].a)) < 1e-6, "the head's hinge end moved");
}

TEST(dynamic_move_takes_the_whole_object) {
    const auto v = vol();
    BlockDef def;
    def.geometry = {seg(v, 0, 0, 1, 0)};

    BlockTable t;
    DynamicBlock& d = t.make_dynamic("B");
    BlockParameter p;
    p.name = "Offset";
    d.parameters.push_back(p);

    BlockAction a;
    a.kind = ActionKind::Move;
    a.parameter = "Offset";
    a.geometry = {0};
    a.dy = 1.0;
    d.actions.push_back(a);

    d.set_parameter("Offset", 2.5);
    const DynamicResult r = evaluate(def, d, v);
    CHECK(std::fabs(wy(v, r.geometry[0].a) - 2.5) < 1e-4);
    CHECK(std::fabs(wy(v, r.geometry[0].b) - 2.5) < 1e-4);
}

TEST(dynamic_flip_is_a_state_not_a_distance) {
    const auto v = vol();
    BlockDef def;
    def.geometry = {seg(v, 1, 0, 3, 0)};

    BlockTable t;
    DynamicBlock& d = t.make_dynamic("B");
    BlockParameter p;
    p.name = "Hand";
    p.kind = ParameterKind::Flip;
    p.base = pt(v, 0, 0);
    p.end = pt(v, 0, 1);          // mirror about the Y axis
    d.parameters.push_back(p);

    BlockAction a;
    a.kind = ActionKind::Flip;
    a.parameter = "Hand";
    a.geometry = {0};
    d.actions.push_back(a);

    d.set_parameter("Hand", 0.0);
    CHECK_MSG(std::fabs(wx(v, evaluate(def, d, v).geometry[0].a) - 1.0) < 1e-6,
              "an unflipped block moved");

    d.set_parameter("Hand", 1.0);
    CHECK_MSG(std::fabs(wx(v, evaluate(def, d, v).geometry[0].a) + 1.0) < 1e-4,
              "the flip did not mirror");
}

TEST(dynamic_rotate_turns_about_its_origin) {
    const auto v = vol();
    BlockDef def;
    def.geometry = {seg(v, 1, 0, 2, 0)};

    BlockTable t;
    DynamicBlock& d = t.make_dynamic("B");
    BlockParameter p;
    p.name = "Angle";
    p.kind = ParameterKind::Rotation;
    d.parameters.push_back(p);

    BlockAction a;
    a.kind = ActionKind::Rotate;
    a.parameter = "Angle";
    a.geometry = {0};
    a.origin = pt(v, 0, 0);
    d.actions.push_back(a);

    d.set_parameter("Angle", 3.14159265358979323846 * 0.5);
    const DynamicResult r = evaluate(def, d, v);
    CHECK_MSG(std::fabs(wx(v, r.geometry[0].a)) < 1e-4 &&
                  std::fabs(wy(v, r.geometry[0].a) - 1.0) < 1e-4,
              "the quarter turn landed at (%.4f, %.4f)", wx(v, r.geometry[0].a),
              wy(v, r.geometry[0].a));
}

TEST(dynamic_scale_grows_about_its_origin) {
    const auto v = vol();
    BlockDef def;
    def.geometry = {seg(v, 1, 0, 2, 0)};

    BlockTable t;
    DynamicBlock& d = t.make_dynamic("B");
    BlockParameter p;
    p.name = "Size";
    p.value = 1.0;
    d.parameters.push_back(p);

    BlockAction a;
    a.kind = ActionKind::Scale;
    a.parameter = "Size";
    a.geometry = {0};
    a.origin = pt(v, 0, 0);
    d.actions.push_back(a);

    d.set_parameter("Size", 3.0);
    const DynamicResult r = evaluate(def, d, v);
    CHECK(std::fabs(wx(v, r.geometry[0].a) - 3.0) < 1e-4);
    CHECK(std::fabs(wx(v, r.geometry[0].b) - 6.0) < 1e-4);
}

TEST(dynamic_visibility_shows_one_set_at_a_time) {
    const auto v = vol();
    BlockDef def;
    def.geometry = {seg(v, 0, 0, 1, 0), seg(v, 0, 1, 1, 1), seg(v, 0, 2, 1, 2)};

    BlockTable t;
    DynamicBlock& d = t.make_dynamic("B");
    d.visibility = {{"plain", {0}}, {"detailed", {0, 1, 2}}};

    CHECK(d.set_visibility("plain"));
    DynamicResult r = evaluate(def, d, v);
    CHECK_MSG(r.geometry.size() == 1, "%zu shapes shown", r.geometry.size());
    CHECK(r.hidden == 2);

    CHECK(d.set_visibility("detailed"));
    r = evaluate(def, d, v);
    CHECK(r.geometry.size() == 3);
    CHECK(r.hidden == 0);

    CHECK(!d.set_visibility("nonexistent"));
}

TEST(dynamic_evaluation_leaves_the_definition_alone) {
    const auto v = vol();
    const BlockDef def = door(v);
    const auto before = def.geometry;

    BlockTable t;
    DynamicBlock& d = t.make_dynamic("DOOR");
    BlockParameter p;
    p.name = "Width";
    d.parameters.push_back(p);
    BlockAction a;
    a.kind = ActionKind::Move;
    a.parameter = "Width";
    a.geometry = {0, 1, 2};
    a.dx = 1.0;
    d.actions.push_back(a);
    d.set_parameter("Width", 5.0);

    evaluate(def, d, v);
    for (std::size_t i = 0; i < before.size(); ++i)
        CHECK_MSG(def.geometry[i].a.x == before[i].a.x,
                  "the definition was modified");
}

TEST(dynamic_a_block_table_keeps_static_and_dynamic_apart) {
    BlockTable t;
    BlockDef plain;
    plain.name = "PLAIN";
    t.define(plain);

    CHECK(!t.is_dynamic("PLAIN"));
    CHECK(t.dynamic("PLAIN") == nullptr);
    CHECK(t.dynamic_count() == 0);

    t.make_dynamic("PLAIN");
    CHECK(t.is_dynamic("PLAIN"));
    CHECK(t.dynamic_count() == 1);
    t.make_dynamic("PLAIN");
    CHECK(t.dynamic_count() == 1);
}

TEST(dynamic_kind_names_are_all_defined) {
    for (int i = 0; i <= static_cast<int>(ParameterKind::Alignment); ++i)
        CHECK(std::string(parameter_kind_name(static_cast<ParameterKind>(i)))
                  .size() > 0);
    for (int i = 0; i <= static_cast<int>(ActionKind::Lookup); ++i)
        CHECK(std::string(action_kind_name(static_cast<ActionKind>(i))).size() > 0);
}

TEST(underlay_kind_comes_from_the_extension) {
    CHECK(underlay_kind_of("survey.pdf") == UnderlayKind::Pdf);
    CHECK(underlay_kind_of("plan.DWF") == UnderlayKind::Dwf);
    CHECK(underlay_kind_of("site.dgn") == UnderlayKind::Dgn);
    CHECK(underlay_kind_of("aerial.PNG") == UnderlayKind::Raster);
    CHECK(underlay_kind_of("notes.txt") == UnderlayKind::Unknown);
    CHECK(underlay_kind_of("noextension") == UnderlayKind::Unknown);
}

TEST(underlay_attach_names_it_after_the_file) {
    UnderlayTable t;
    Underlay u;
    u.path = "/drawings/received/SURVEY-2024.pdf";
    t.attach(u);

    CHECK(t.size() == 1);
    CHECK_MSG(t.find("SURVEY-2024.pdf") != nullptr, "named '%s'",
              t.all()[0].name.c_str());
    CHECK(t.all()[0].kind == UnderlayKind::Pdf);
}

TEST(underlay_defaults_do_not_plot_and_cannot_be_picked) {
    Underlay u;
    CHECK_MSG(!u.plottable, "an underlay would plot by default");
    CHECK_MSG(u.locked, "an underlay was selectable by default");
    CHECK_MSG(u.fade > 0, "an underlay at full strength is indistinguishable "
                          "from the drawing on top of it");
}

TEST(underlay_transform_round_trips) {
    const auto v = vol();
    UnderlayTable t;
    Underlay u;
    u.path = "a.pdf";
    u.insertion = pt(v, 10, 20);
    u.scale = 2.5;
    u.rotation = 0.7;
    t.attach(u);

    const Underlay* a = t.find("a.pdf");
    for (auto p : {std::make_pair(0.0, 0.0), std::make_pair(3.0, -4.0),
                   std::make_pair(-7.5, 2.25)}) {
        const Point2 source = pt(v, p.first, p.second);
        f64 dx, dy;
        a->to_drawing(source, v, dx, dy);
        Point2 back;
        a->to_underlay(dx, dy, v, back);
        CHECK_MSG(std::fabs(wx(v, back) - p.first) < 1e-4 &&
                      std::fabs(wy(v, back) - p.second) < 1e-4,
                  "(%.3f, %.3f) came back as (%.3f, %.3f)", p.first, p.second,
                  wx(v, back), wy(v, back));
    }
}

TEST(underlay_clipping_uses_the_same_rule_as_everything_else) {
    const auto v = vol();
    Underlay u;
    u.clip = {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)};
    CHECK(u.has_clip());
    CHECK(u.contains(pt(v, 5, 5)));
    CHECK(!u.contains(pt(v, 15, 5)));

    u.clip_inverted = true;
    CHECK(!u.contains(pt(v, 5, 5)));
    CHECK(u.contains(pt(v, 15, 5)));
}

TEST(underlay_without_a_clip_contains_everything) {
    const auto v = vol();
    Underlay u;
    CHECK(!u.has_clip());
    CHECK(u.contains(pt(v, 1e5, -1e5)));
}

TEST(underlay_calibration_sets_the_scale_and_leaves_the_origin) {
    const auto v = vol();
    UnderlayTable t;
    Underlay u;
    u.path = "survey.pdf";
    u.insertion = pt(v, 100, 200);
    t.attach(u);

    // Two points 4 units apart on the underlay are really 20 metres apart.
    CHECK(t.calibrate("survey.pdf", pt(v, 0, 0), pt(v, 4, 0), 20.0, v));
    const Underlay* a = t.find("survey.pdf");
    // Measured off the lattice: the scale is good to a part in ten million.
    CHECK_MSG(std::fabs(a->scale - 5.0) < 5.0 * 1e-7, "scale %.9f", a->scale);
    CHECK_MSG(a->insertion.x == pt(v, 100, 200).x, "the insertion point moved");

    CHECK(!t.calibrate("survey.pdf", pt(v, 0, 0), pt(v, 0, 0), 20.0, v));
    CHECK(!t.calibrate("survey.pdf", pt(v, 0, 0), pt(v, 4, 0), 0.0, v));
    CHECK(!t.calibrate("missing.pdf", pt(v, 0, 0), pt(v, 4, 0), 20.0, v));
}

TEST(underlay_draw_order_skips_the_hidden) {
    UnderlayTable t;
    Underlay a;
    a.path = "a.pdf";
    Underlay b;
    b.path = "b.pdf";
    b.visible = false;
    t.attach(a);
    t.attach(b);

    const auto order = t.draw_order();
    CHECK_MSG(order.size() == 1, "%zu underlays in the draw order", order.size());
    CHECK(order[0]->name == "a.pdf");
}

TEST(underlay_detach) {
    UnderlayTable t;
    Underlay u;
    u.path = "a.pdf";
    t.attach(u);
    CHECK(t.detach("a.pdf"));
    CHECK(t.size() == 0);
    CHECK(!t.detach("a.pdf"));
}
