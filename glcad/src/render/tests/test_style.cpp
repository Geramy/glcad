#include "test.hpp"

#include <cmath>
#include <string>

#include "render/style.hpp"

using namespace gview;

namespace {
constexpr f64 kPi = 3.14159265358979323846;
}

TEST(style_every_style_is_named_and_resolves) {
    for (int i = 0; i <= static_cast<int>(VisualStyle::Realistic); ++i) {
        const auto s = static_cast<VisualStyle>(i);
        CHECK_MSG(std::string(visual_style_name(s)).size() > 0, "style %d unnamed",
                  i);
        const StyleSettings t = settings_for(s);
        CHECK_MSG(t.draw_faces || t.draw_edges, "%s draws nothing at all",
                  visual_style_name(s));
        CHECK(!t.face_token.empty());
        CHECK(!t.edge_token.empty());
    }
}

TEST(style_wireframe_shows_what_is_behind) {
    const StyleSettings t = settings_for(VisualStyle::Wireframe);
    CHECK(!t.draw_faces);
    CHECK(t.draw_edges);
    CHECK_MSG(!t.depth_write, "wireframe wrote depth and would hide itself");
    CHECK(!t.hide_obscured_edges);
}

TEST(style_hidden_line_draws_faces_in_the_background_colour) {
    const StyleSettings t = settings_for(VisualStyle::HiddenLine);
    CHECK_MSG(t.draw_faces, "hidden line drew no faces");
    CHECK_MSG(t.face_token == "canvas.background", "faces use '%s'",
              t.face_token.c_str());
    CHECK(t.hide_obscured_edges);
    CHECK(t.draw_edges);
}

TEST(style_xray_does_not_write_depth) {
    const StyleSettings t = settings_for(VisualStyle::XRay);
    CHECK(t.face_opacity < 1.0f);
    CHECK_MSG(!t.depth_write, "x-ray wrote depth");
    CHECK(t.two_sided);
}

TEST(style_section_lights_both_sides) {
    CHECK(settings_for(VisualStyle::Section).two_sided);
}

TEST(style_shaded_and_shaded_with_edges_differ) {
    const StyleSettings plain = settings_for(VisualStyle::Shaded);
    const StyleSettings edged = settings_for(VisualStyle::ShadedEdges);
    CHECK(!plain.draw_edges);
    CHECK(edged.draw_edges);
    CHECK(plain.draw_faces && edged.draw_faces);
}

TEST(style_sketch_is_deliberately_loose) {
    const StyleSettings t = settings_for(VisualStyle::Sketch);
    CHECK(t.jitter_edges);
    CHECK(t.edge_width > 1.0f);
}

TEST(style_realistic_asks_for_shadows) {
    const StyleSettings t = settings_for(VisualStyle::Realistic);
    CHECK(t.shadows);
    CHECK(t.ambient < settings_for(VisualStyle::Shaded).ambient);
}

TEST(style_a_silhouette_is_where_the_facing_flips) {
    CHECK(is_silhouette(0, 0, 1, 0, 0, -1, 0, 0, 10, 0, 0, 0, true));
    CHECK(!is_silhouette(0, 0, 1, 0.1, 0, 1, 0, 0, 10, 0, 0, 0, true));
    CHECK(!is_silhouette(0, 0, -1, 0, 0.1, -1, 0, 0, 10, 0, 0, 0, true));
}

TEST(style_perspective_silhouettes_depend_on_where_the_edge_is) {
    const f64 eye_x = 0, eye_y = 0, eye_z = 5;

    const bool at_centre = is_silhouette(0.3, 0, 0.954, 0, 0, 1, eye_x, eye_y,
                                         eye_z, 0, 0, 0, false);
    const bool far_out = is_silhouette(0.3, 0, 0.954, 0, 0, 1, eye_x, eye_y,
                                       eye_z, 100, 0, 0, false);
    CHECK_MSG(!at_centre, "the edge was a silhouette at the centre already");
    CHECK_MSG(far_out, "the perspective silhouette did not move with the edge");

    CHECK(is_silhouette(0.3, 0, 0.954, 0, 0, 1, 0, 0, 1, 0, 0, 0, true) ==
          is_silhouette(0.3, 0, 0.954, 0, 0, 1, 0, 0, 1, 100, 0, 0, true));
}

TEST(style_a_degenerate_view_direction_is_not_a_silhouette) {
    CHECK(!is_silhouette(0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, false));
}

TEST(wheel_is_hidden_until_shown) {
    SteeringWheel w;
    CHECK(!w.visible());
    CHECK(w.hit(0, 0) == WheelVerb::None);

    w.show(100, 100);
    CHECK(w.visible());
    w.hide();
    CHECK(!w.visible());
}

TEST(wheel_the_hub_fires_nothing) {
    SteeringWheel w;
    w.show(100, 100);
    CHECK_MSG(w.hit(100, 100) == WheelVerb::None, "the hub fired a verb");
    CHECK(w.hit(102, 101) == WheelVerb::None);
}

TEST(wheel_outside_the_rim_fires_nothing) {
    SteeringWheel w;
    w.show(100, 100);
    CHECK(w.hit(500, 500) == WheelVerb::None);
    CHECK(w.hit(100, 100 - 200) == WheelVerb::None);
}

TEST(wheel_verbs_are_laid_out_clockwise_from_the_top) {
    SteeringWheel w;
    w.show(100, 100);
    const auto verbs = w.verbs();
    CHECK(verbs.size() == 8);

    const f64 r = (w.layout().outer_radius + w.layout().inner_radius) * 0.5;
    CHECK_MSG(w.hit(100, 100 - r) == verbs[0], "the top slot is %s",
              wheel_verb_name(w.hit(100, 100 - r)));

    CHECK(w.hit(100 + r, 100) == verbs[verbs.size() / 4]);
}

TEST(wheel_the_mini_wheel_drops_the_verbs_that_need_a_label) {
    SteeringWheel w;
    w.show(100, 100, true);
    CHECK(w.layout().mini);
    CHECK_MSG(w.verbs().size() == 4, "%zu verbs on the mini wheel",
              w.verbs().size());
    CHECK(w.layout().outer_radius < 56.0);
}

TEST(wheel_orbit_and_pan_move_the_camera) {
    Camera c;
    const f64 azimuth = c.azimuth();
    SteeringWheel w;
    w.show(100, 100);

    w.begin_drag(WheelVerb::Orbit);
    CHECK(w.held() == WheelVerb::Orbit);
    CHECK(w.drag(c, 40, 0) == WheelVerb::Orbit);
    CHECK_MSG(std::fabs(c.azimuth() - azimuth) > 1e-6, "orbit did nothing");
    w.end_drag();
    CHECK(w.held() == WheelVerb::None);
}

TEST(wheel_dragging_up_zooms_in) {
    Camera c;
    c.set_distance(10.0);
    SteeringWheel w;
    w.show(100, 100);
    w.begin_drag(WheelVerb::Zoom);

    w.drag(c, 0, 5);
    const f64 out = c.distance();
    c.set_distance(10.0);
    w.drag(c, 0, -5);
    CHECK_MSG(out != c.distance(), "zoom ignored the drag direction");
}

TEST(wheel_a_drag_with_nothing_held_does_nothing) {
    Camera c;
    const f64 before = c.azimuth();
    SteeringWheel w;
    w.show(100, 100);
    CHECK(w.drag(c, 100, 100) == WheelVerb::None);
    CHECK(std::fabs(c.azimuth() - before) < 1e-12);
}

TEST(wheel_verbs_are_all_named) {
    for (int i = 1; i <= static_cast<int>(WheelVerb::Up); ++i)
        CHECK_MSG(std::string(wheel_verb_name(static_cast<WheelVerb>(i))).size() > 0,
                  "verb %d unnamed", i);
    CHECK(std::string(wheel_verb_name(WheelVerb::None)).empty());
}
