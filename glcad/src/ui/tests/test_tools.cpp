#include "test.hpp"

#include <cmath>

#include "ui/canvas.hpp"

using namespace gcapp;

namespace {

struct Rig {
    Canvas c;

    Rig() {
        c.view().set_viewport(1000.0, 1000.0);
    }

    void screen(double wx, double wy, double& px, double& py) const {
        c.view().screen_of(wx, wy, px, py);
    }
    void world(gcm::Lattice2 p, double& wx, double& wy) const {
        gcm::to_world(c.volume(), p, wx, wy);
    }
    void click_world(double wx, double wy, bool add = false) {
        double px, py;
        screen(wx, wy, px, py);
        c.click(px, py, add);
    }
    std::size_t pick_world(double wx, double wy) const {
        double px, py;
        screen(wx, wy, px, py);
        return c.pick(px, py);
    }

    void draw_square() {
        c.set_tool(Tool::Line);
        click_world(0, 0);
        click_world(4, 0);
        click_world(4, 4);
        click_world(0, 4);
        click_world(0, 0);
        c.cancel();
    }
};

double wx_of(const Canvas& c, gcm::Lattice2 p) {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    double x, y;
    gcm::to_world(v, p, x, y);
    (void)y;
    return x;
}
double wy_of(const Canvas& c, gcm::Lattice2 p) {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    double x, y;
    gcm::to_world(v, p, x, y);
    (void)x;
    return y;
}

}

TEST(canvas_picks_the_line_under_the_cursor) {
    Rig r;
    r.draw_square();
    CHECK(r.c.segment_count() == 4);

    const std::size_t i = r.pick_world(2.0, 0.0);
    CHECK_MSG(i != Canvas::npos, "nothing picked on the bottom edge");
    CHECK(wy_of(r.c, r.c.shapes()[i].a) < 0.001);
}

TEST(canvas_picks_nothing_in_empty_space) {
    Rig r;
    r.draw_square();
    CHECK(r.pick_world(50.0, 50.0) == Canvas::npos);
}

TEST(canvas_hidden_layers_cannot_be_picked_or_selected_by_area_fence_or_lasso) {
    Rig r;
    r.c.set_current_layer("Hidden"); r.draw_square();
    r.c.set_tool(Tool::Select); r.click_world(2, 0);
    CHECK(!r.c.selection().empty()); CHECK(!r.c.grips().empty());
    r.c.set_hidden_layers({"Hidden"});
    CHECK(r.c.selection().empty()); CHECK(r.c.grips().empty());
    CHECK(r.pick_world(2, 0) == Canvas::npos);
    r.click_world(2, 0); CHECK(r.c.selection().empty());
    double x0, y0, x1, y1;
    r.screen(-1, -1, x0, y0); r.screen(5, 5, x1, y1);
    CHECK(r.c.select_area(x0, y0, x1, y1, false) == 0);
    CHECK(r.c.select_area(x1, y1, x0, y0, false) == 0);
    CHECK(r.c.select_lasso({{x0,y0},{x1,y0},{x1,y1},{x0,y1}}, false, false) == 0);
    r.screen(2, -1, x0, y0); r.screen(2, 5, x1, y1);
    CHECK(r.c.select_fence({{x0,y0},{x1,y1}}, false) == 0);
    r.c.select_all(); CHECK(r.c.selection().empty());
    r.c.set_hidden_layers({});
    CHECK(r.pick_world(2, 0) != Canvas::npos);
    CHECK(r.c.select_fence({{x0,y0},{x1,y1}}, false) == 2);
}

TEST(canvas_hidden_dimensions_and_block_members_do_not_take_clicks) {
    Rig r;
    r.c.set_current_layer("Hidden");
    r.c.set_tool(Tool::DimLinear);
    r.click_world(0, 0); r.click_world(4, 0); r.click_world(2, 2);
    CHECK(r.c.dimensions().size() == 1);
    double px, py; r.screen(2, 2, px, py);
    CHECK(r.c.pick_dim(px, py) != Canvas::npos);
    r.c.set_hidden_layers({"Hidden"});
    CHECK(r.c.pick_dim(px, py) == Canvas::npos);
    grender::d2::DrawList drawing; r.c.build(drawing);
    CHECK(r.c.labels().empty());
    r.c.set_tool(Tool::Select); r.c.select_at(px, py, false);
    CHECK(r.c.selected_dimensions().empty());

    gdoc::BlockDef def; def.name = "Mixed";
    gsnap::Shape edge; edge.layer = "Hidden";
    edge.a = gcm::to_lattice(r.c.volume(), 10, 0); edge.b = gcm::to_lattice(r.c.volume(), 14, 0);
    def.geometry.push_back(edge);
    edge.layer = "Visible";
    edge.a = gcm::to_lattice(r.c.volume(), 10, 4); edge.b = gcm::to_lattice(r.c.volume(), 14, 4);
    def.geometry.push_back(edge); r.c.blocks().define(def);
    gdoc::BlockRef ref; ref.name = def.name; r.c.blocks().insert(ref);
    r.screen(12, 0, px, py); CHECK(r.c.pick_reference(px, py) == Canvas::npos32);
    r.screen(12, 4, px, py); CHECK(r.c.pick_reference(px, py) != Canvas::npos32);
    r.c.set_hidden_layers({});
    r.screen(12, 0, px, py); CHECK(r.c.pick_reference(px, py) != Canvas::npos32);
}

TEST(canvas_hiding_layers_invalidates_snaps_without_changing_dimension_ownership) {
    Rig r;
    r.c.set_current_layer("Hidden"); r.draw_square();
    r.c.set_current_layer("Visible"); r.c.set_tool(Tool::Line);
    r.click_world(10, 0); r.click_world(14, 0); r.c.cancel();
    const auto visible_key = r.c.shapes().back().key;
    double px, py; r.screen(4, 4, px, py);
    r.c.set_grid_snap(false); r.c.hover(px, py);
    CHECK(r.c.snap().valid);
    r.c.set_hidden_layers({"Hidden"}); r.c.hover(px, py);
    CHECK(!r.c.snap().valid);
    r.c.set_tool(Tool::DimLinear);
    r.click_world(10, 0); r.click_world(14, 0); r.click_world(12, 2);
    CHECK(r.c.dimensions().size() == 1);
    if (!r.c.dimensions().empty()) {
        CHECK(r.c.dimensions()[0].o1.shape == visible_key);
        CHECK(r.c.dimensions()[0].o2.shape == visible_key);
    }
    r.c.cancel(); r.c.set_hidden_layers({}); r.c.hover(px, py);
    CHECK(r.c.snap().valid);
}

TEST(canvas_picks_the_nearest_of_two_crossing_lines) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(10, 0);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(5, -5);
    r.click_world(5, 5);
    r.c.cancel();
    CHECK(r.c.segment_count() == 2);

    CHECK_MSG(r.pick_world(4.95, 0.0) == 0, "the horizontal line should win here");
    CHECK_MSG(r.pick_world(5.0, 0.05) == 1, "the vertical line should win here");
}

TEST(canvas_select_replaces_and_shift_toggles) {
    Rig r;
    r.draw_square();
    r.c.set_tool(Tool::Select);

    r.click_world(2.0, 0.0);
    CHECK(r.c.selection().size() == 1);

    r.click_world(4.0, 2.0);
    CHECK(r.c.selection().size() == 1);

    r.click_world(2.0, 0.0, true);
    CHECK(r.c.selection().size() == 2);

    r.click_world(2.0, 0.0, true);
    CHECK(r.c.selection().size() == 1);

    r.click_world(80.0, 80.0);
    CHECK(r.c.selection().empty());
}

TEST(canvas_select_all_and_erase) {
    Rig r;
    r.draw_square();
    r.c.select_all();
    CHECK(r.c.selection().size() == 4);
    r.c.erase_selected();
    CHECK(r.c.segment_count() == 0);
    CHECK(r.c.selection().empty());
}

TEST(canvas_undo_restores_edited_geometry_not_just_the_count) {
    Rig r;
    r.draw_square();
    const auto before = r.c.shapes();

    r.c.select_all();
    r.c.set_tool(Tool::Move);
    r.click_world(0, 0);
    r.click_world(3, 0);
    CHECK(r.c.segment_count() == 4);
    CHECK_MSG(std::fabs(wx_of(r.c, r.c.shapes()[0].a) - 3.0) < 0.01,
              "the move did not happen: x=%.4f sel=%zu msg='%s'",
              wx_of(r.c, r.c.shapes()[0].a), r.c.selection().size(),
              r.c.message().c_str());

    r.c.undo();
    CHECK(r.c.segment_count() == 4);
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(r.c.shapes()[i].a.x == before[i].a.x);
        CHECK(r.c.shapes()[i].a.y == before[i].a.y);
    }
}

TEST(canvas_redo_replays_what_undo_took_back) {
    Rig r;
    r.draw_square();
    r.c.select_all();
    r.c.set_tool(Tool::Move);
    r.click_world(0, 0);
    r.click_world(3, 0);

    const double moved = wx_of(r.c, r.c.shapes()[0].a);
    r.c.undo();
    CHECK(std::fabs(wx_of(r.c, r.c.shapes()[0].a) - moved) > 0.5);
    r.c.redo();
    CHECK(std::fabs(wx_of(r.c, r.c.shapes()[0].a) - moved) < 0.01);
}

TEST(canvas_move_needs_a_selection_first) {
    Rig r;
    r.draw_square();
    r.c.set_tool(Tool::Move);

    r.click_world(2.0, 0.0);
    CHECK(r.c.selection().size() == 1);
    CHECK(r.c.segment_count() == 4);
}

TEST(canvas_copy_leaves_the_original) {
    Rig r;
    r.draw_square();
    r.c.select_all();
    r.c.set_tool(Tool::Copy);
    r.click_world(0, 0);
    r.click_world(10, 0);
    CHECK_MSG(r.c.segment_count() == 8, "expected 8 segments, got %zu",
              r.c.segment_count());
}

TEST(canvas_mirror_keeps_the_source) {
    Rig r;
    r.draw_square();
    r.c.select_all();
    r.c.set_tool(Tool::Mirror);
    r.click_world(8, 0);
    r.click_world(8, 10);
    CHECK(r.c.segment_count() == 8);
}

TEST(canvas_rotate_turns_the_selection) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(4, 0);
    r.c.cancel();
    r.c.select_all();

    r.c.set_tool(Tool::Rotate);
    r.click_world(0, 0);
    r.click_world(0, 5);
    CHECK(r.c.segment_count() == 1);
    CHECK_MSG(std::fabs(wy_of(r.c, r.c.shapes()[0].b) - 4.0) < 0.01,
              "expected the far end at y=4, got %.4f",
              wy_of(r.c, r.c.shapes()[0].b));
    CHECK(std::fabs(wx_of(r.c, r.c.shapes()[0].b)) < 0.01);
}

TEST(canvas_rectangular_array_multiplies_the_selection) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(1, 0);
    r.c.cancel();
    r.c.select_all();

    r.c.array_columns() = 3;
    r.c.array_rows() = 2;
    r.c.set_tool(Tool::ArrayRect);
    r.click_world(0, 0);
    r.click_world(2, 2);
    CHECK_MSG(r.c.segment_count() == 6, "got %zu", r.c.segment_count());
}

TEST(canvas_polar_array_multiplies_the_selection) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(2, 0);
    r.click_world(3, 0);
    r.c.cancel();
    r.c.select_all();

    r.c.array_count() = 4;
    r.c.set_tool(Tool::ArrayPolar);
    r.click_world(0, 0);
    CHECK(r.c.segment_count() == 4);
}

TEST(canvas_offset_adds_a_parallel_copy) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(6, 0);
    r.c.cancel();

    r.c.offset_distance() = 1.0;
    r.c.set_tool(Tool::Offset);
    r.click_world(3.0, 0.0);
    r.click_world(3.0, 4.0);
    CHECK(r.c.segment_count() == 2);
    CHECK_MSG(wy_of(r.c, r.c.shapes()[1].a) > 0.5,
              "the copy landed on the wrong side (%.4f)",
              wy_of(r.c, r.c.shapes()[1].a));
}

TEST(canvas_trim_cuts_at_the_crossing) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(10, 0);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(5, -3);
    r.click_world(5, 3);
    r.c.cancel();

    r.c.set_tool(Tool::Trim);
    r.click_world(8.0, 0.0);
    CHECK_MSG(r.c.segment_count() == 2, "trim produced %zu shapes",
              r.c.segment_count());
}

TEST(canvas_fillet_replaces_the_corner_with_an_arc) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(6, 0);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(0, 6);
    r.c.cancel();

    r.c.corner_radius() = 1.0;
    r.c.set_tool(Tool::Fillet);
    r.click_world(4.0, 0.0);
    r.click_world(0.0, 4.0);

    CHECK_MSG(r.c.segment_count() == 3, "expected two lines plus an arc, got %zu",
              r.c.segment_count());
    bool has_arc = false;
    for (const auto& s : r.c.shapes())
        if (s.kind == gsnap::ShapeKind::Arc) has_arc = true;
    CHECK_MSG(has_arc, "no fillet arc was created");
}

TEST(canvas_chamfer_replaces_the_corner_with_a_line) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(6, 0);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(0, 6);
    r.c.cancel();

    r.c.chamfer_distance() = 1.0;
    r.c.set_tool(Tool::Chamfer);
    r.click_world(4.0, 0.0);
    r.click_world(0.0, 4.0);
    CHECK(r.c.segment_count() == 3);
    for (const auto& s : r.c.shapes())
        CHECK(s.kind == gsnap::ShapeKind::Segment);
}

TEST(canvas_linear_dimension_measures_what_was_picked) {
    Rig r;
    r.c.set_tool(Tool::DimLinear);
    r.click_world(0, 0);
    r.click_world(4, 0);
    r.click_world(2, 2);
    CHECK_MSG(r.c.dimensions().size() == 1, "no dimension was created");

    const gdim::Style* st = r.c.dim_styles().find("Standard");
    CHECK(st != nullptr);
    CHECK_MSG(r.c.message().find("4") != std::string::npos,
              "measured '%s'", r.c.message().c_str());
}

TEST(canvas_dimension_geometry_reaches_the_draw_list) {
    Rig r;
    r.c.set_tool(Tool::DimLinear);
    r.click_world(0, 0);
    r.click_world(4, 0);
    r.click_world(2, 2);

    grender::d2::DrawList dl;
    r.c.build(dl);
    CHECK_MSG(!dl.lines().empty(), "the dimension drew nothing");
    CHECK_MSG(r.c.labels().size() == 1, "the dimension produced no text label");
    CHECK(!r.c.labels()[0].text.empty());
    CHECK(r.c.labels()[0].height > 0.0);
}

TEST(canvas_radial_dimension_needs_a_curve) {
    Rig r;
    r.c.set_tool(Tool::Circle);
    r.click_world(0, 0);
    r.click_world(3, 0);
    CHECK(r.c.segment_count() == 1);

    r.c.set_tool(Tool::DimRadial);
    r.click_world(50.0, 50.0);
    CHECK(r.c.dimensions().empty());

    r.click_world(3.0, 0.0);
    r.click_world(5.0, 5.0);
    CHECK_MSG(r.c.dimensions().size() == 1, "%s", r.c.message().c_str());
    CHECK(r.c.dimensions()[0].kind == gdim::Kind::Radial);
}

TEST(canvas_angular_dimension_takes_a_vertex_and_two_rays) {
    Rig r;
    r.c.set_tool(Tool::DimAngular);
    r.click_world(0, 0);
    r.click_world(4, 0);
    r.click_world(0, 4);
    CHECK(r.c.dimensions().empty());
    r.click_world(2, 2);
    CHECK(r.c.dimensions().size() == 1);
    CHECK(r.c.dimensions()[0].kind == gdim::Kind::Angular);
}

TEST(canvas_dimensions_undo_with_everything_else) {
    Rig r;
    r.c.set_tool(Tool::DimLinear);
    r.click_world(0, 0);
    r.click_world(4, 0);
    r.click_world(2, 2);
    CHECK(r.c.dimensions().size() == 1);
    r.c.undo();
    CHECK_MSG(r.c.dimensions().empty(), "undo left the dimension behind");
}

TEST(canvas_horizontal_constraint_levels_a_line) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(5, 1);
    r.c.cancel();
    r.c.select_all();

    CHECK_MSG(r.c.apply_constraint(gsolve::Kind::Horizontal), "%s",
              r.c.message().c_str());
    const double y0 = wy_of(r.c, r.c.shapes()[0].a);
    const double y1 = wy_of(r.c, r.c.shapes()[0].b);
    CHECK_MSG(std::fabs(y1 - y0) < 1e-4, "still sloping: %.6f vs %.6f", y0, y1);
}

TEST(canvas_perpendicular_constraint_squares_a_corner) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(5, 0);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(1, 4);
    r.c.cancel();
    r.c.select_all();

    CHECK_MSG(r.c.apply_constraint(gsolve::Kind::Perpendicular), "%s",
              r.c.message().c_str());

    auto dir = [&](std::size_t i, double& dx, double& dy) {
        dx = wx_of(r.c, r.c.shapes()[i].b) - wx_of(r.c, r.c.shapes()[i].a);
        dy = wy_of(r.c, r.c.shapes()[i].b) - wy_of(r.c, r.c.shapes()[i].a);
    };
    double ax, ay, bx, by;
    dir(0, ax, ay);
    dir(1, bx, by);
    const double dot = (ax * bx + ay * by) /
                       (std::hypot(ax, ay) * std::hypot(bx, by));
    CHECK_MSG(std::fabs(dot) < 1e-3, "cos of the angle is %.6f", dot);
}

TEST(canvas_constraint_keeps_shared_endpoints_together) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(5, 1);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(5, 1);
    r.click_world(6, 5);
    r.c.cancel();
    r.c.select_all();

    r.c.apply_constraint(gsolve::Kind::Horizontal);

    const double jx0 = wx_of(r.c, r.c.shapes()[0].b);
    const double jy0 = wy_of(r.c, r.c.shapes()[0].b);
    const double jx1 = wx_of(r.c, r.c.shapes()[1].a);
    const double jy1 = wy_of(r.c, r.c.shapes()[1].a);
    CHECK_MSG(std::hypot(jx1 - jx0, jy1 - jy0) < 1e-4,
              "the corner came apart: (%.4f,%.4f) vs (%.4f,%.4f)",
              jx0, jy0, jx1, jy1);
}

TEST(canvas_constraint_without_a_selection_is_refused) {
    Rig r;
    r.draw_square();
    CHECK(!r.c.apply_constraint(gsolve::Kind::Horizontal));
    CHECK(!r.c.message().empty());
}

TEST(canvas_reports_degrees_of_freedom) {
    Rig r;
    r.draw_square();
    const gsolve::Dof d = r.c.dof();
    CHECK(d.parameters > 0);
}

TEST(canvas_tool_names_match_the_command_names) {
    CHECK(std::string(tool_name(Tool::Move)) == "MOVE");
    CHECK(std::string(tool_name(Tool::Fillet)) == "FILLET");
    CHECK(std::string(tool_name(Tool::DimRadial)) == "DIMRADIUS");
    CHECK(tool_uses_selection(Tool::Move));
    CHECK(!tool_uses_selection(Tool::Trim));
    CHECK(tool_is_dimension(Tool::DimAngular));
    CHECK(!tool_is_dimension(Tool::Line));
}

TEST(canvas_switching_tools_drops_a_half_finished_operation) {
    Rig r;
    r.draw_square();
    r.c.select_all();
    r.c.set_tool(Tool::Move);
    r.click_world(0, 0);

    r.c.set_tool(Tool::Line);
    r.click_world(9, 9);
    r.click_world(9, 12);
    CHECK_MSG(r.c.segment_count() == 5, "the abandoned move leaked (%zu)",
              r.c.segment_count());
}

TEST(canvas_typed_point_draws_the_same_line_a_pick_would) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    CHECK(r.c.type_point("0,0"));
    CHECK(r.c.type_point("@4,0"));
    CHECK(r.c.segment_count() == 1);

    CHECK_MSG(std::fabs(wx_of(r.c, r.c.shapes()[0].a)) < 1e-6, "%.6f",
              wx_of(r.c, r.c.shapes()[0].a));
    CHECK_MSG(std::fabs(wx_of(r.c, r.c.shapes()[0].b) - 4.0) < 1e-6, "%.6f",
              wx_of(r.c, r.c.shapes()[0].b));
    CHECK(std::fabs(wy_of(r.c, r.c.shapes()[0].b)) < 1e-6);
}

TEST(canvas_typed_polar_draws_the_wall_the_mouse_could_only_approximate) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    CHECK(r.c.type_point("@5<90"));
    CHECK(r.c.segment_count() == 1);
    CHECK_MSG(std::fabs(wy_of(r.c, r.c.shapes()[0].b) - 5.0) < 1e-6, "%.6f",
              wy_of(r.c, r.c.shapes()[0].b));
    CHECK(std::fabs(wx_of(r.c, r.c.shapes()[0].b)) < 1e-6);
}

TEST(canvas_a_snap_does_not_move_a_typed_coordinate) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.set_tool(Tool::Line);
    r.c.type_point("0.02,0.02");
    r.c.type_point("@3,0");

    const auto& s = r.c.shapes().back();
    CHECK_MSG(std::fabs(wy_of(r.c, s.a) - 0.02) < 1e-4,
              "the typed start was pulled to %.6f", wy_of(r.c, s.a));
}

TEST(canvas_typed_nonsense_is_refused_without_drawing) {
    Rig r;
    r.c.set_tool(Tool::Line);
    CHECK(!r.c.type_point("banana"));
    CHECK(r.c.segment_count() == 0);
    CHECK(!r.c.message().empty());
}

TEST(canvas_readout_is_absolute_when_idle_and_relative_once_armed) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);

    r.click_world(2, 3);
    const std::string armed = r.c.cursor_readout();
    CHECK_MSG(armed[0] == '@', "an armed tool should read out relative: '%s'",
              armed.c_str());

    r.c.cancel();
    r.c.set_tool(Tool::Select);
    r.click_world(50, 50);
    const std::string idle = r.c.cursor_readout();
    CHECK_MSG(idle[0] != '@', "an idle cursor should read out absolute: '%s'",
              idle.c_str());
}

TEST(canvas_window_selection_takes_only_what_is_wholly_inside) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(2, 0);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(1, 1);
    r.click_world(9, 1);
    r.c.cancel();
    CHECK(r.c.segment_count() == 2);

    double x0, y0, x1, y1;
    r.screen(-1, -1, x0, y0);
    r.screen(3, 3, x1, y1);
    CHECK(!r.c.crossing_direction(x0, x1));
    const std::size_t n = r.c.select_area(x0, y0, x1, y1, false);
    CHECK_MSG(n == 1, "window selection took %zu", n);
}

TEST(canvas_crossing_selection_also_takes_what_it_clips) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.click_world(2, 0);
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.click_world(1, 1);
    r.click_world(9, 1);
    r.c.cancel();

    double x0, y0, x1, y1;
    r.screen(3, 3, x0, y0);
    r.screen(-1, -1, x1, y1);
    CHECK_MSG(r.c.crossing_direction(x0, x1),
              "right-to-left did not read as crossing");
    const std::size_t n = r.c.select_area(x0, y0, x1, y1, false);
    CHECK_MSG(n == 2, "crossing selection took %zu", n);
}

TEST(canvas_area_selection_can_add_to_the_set) {
    Rig r;
    r.draw_square();
    double x0, y0, x1, y1;
    r.screen(-1, -1, x0, y0);
    r.screen(5, 1, x1, y1);
    r.c.select_area(x0, y0, x1, y1, false);
    const std::size_t first = r.c.selection().size();
    CHECK(first > 0);

    r.screen(-1, 3, x0, y0);
    r.screen(5, 5, x1, y1);
    r.c.select_area(x0, y0, x1, y1, true);
    CHECK_MSG(r.c.selection().size() > first, "the second window replaced");
}

TEST(canvas_area_selection_over_nothing_clears) {
    Rig r;
    r.draw_square();
    r.c.select_all();
    double x0, y0, x1, y1;
    r.screen(90, 90, x0, y0);
    r.screen(95, 95, x1, y1);
    r.c.select_area(x0, y0, x1, y1, false);
    CHECK(r.c.selection().empty());
}

TEST(canvas_suggests_the_constraints_the_sketch_nearly_satisfies) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("4,0.01");
    r.c.cancel();

    const auto suggested = r.c.suggest_constraints();
    bool horizontal = false;
    for (const auto& s : suggested)
        if (s.kind == gsolve::Kind::Horizontal) horizontal = true;
    CHECK_MSG(horizontal, "%zu suggestions, none horizontal", suggested.size());
}

TEST(canvas_auto_constrain_levels_a_nearly_horizontal_line) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("4,0.01");
    r.c.cancel();

    const std::size_t n = r.c.auto_constrain();
    CHECK_MSG(n > 0, "%s", r.c.message().c_str());

    const double y0 = wy_of(r.c, r.c.shapes()[0].a);
    const double y1 = wy_of(r.c, r.c.shapes()[0].b);
    CHECK_MSG(std::fabs(y1 - y0) < 1e-4, "still sloping: %.6f vs %.6f", y0, y1);
}

TEST(canvas_auto_constrain_undoes_as_one_step) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("4,0.01");
    r.c.cancel();
    const auto before = r.c.shapes();

    r.c.auto_constrain();
    r.c.undo();
    CHECK(r.c.shapes()[0].b.y == before[0].b.y);
}

TEST(canvas_auto_constrain_on_an_empty_drawing_says_so) {
    Rig r;
    CHECK(r.c.auto_constrain() == 0);
    CHECK(!r.c.message().empty());
}

TEST(canvas_inference_tolerance_is_configurable) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("4,0.35");
    r.c.cancel();

    r.c.inference().angle_tolerance_deg = 1.0;
    bool found = false;
    for (const auto& s : r.c.suggest_constraints())
        if (s.kind == gsolve::Kind::Horizontal) found = true;
    CHECK(!found);

    r.c.inference().angle_tolerance_deg = 10.0;
    for (const auto& s : r.c.suggest_constraints())
        if (s.kind == gsolve::Kind::Horizontal) found = true;
    CHECK(found);
}

TEST(canvas_grips_appear_only_for_the_selection) {
    Rig r;
    r.draw_square();
    CHECK(r.c.grips().empty());

    r.c.set_tool(Tool::Select);
    r.click_world(2.0, 0.0);
    CHECK_MSG(r.c.grips().size() == 3, "%zu grips for one line",
              r.c.grips().size());

    r.click_world(80, 80);
    CHECK(r.c.grips().empty());
}

TEST(canvas_dragging_an_endpoint_grip_stretches_the_line) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    r.c.cancel();

    r.c.set_tool(Tool::Select);
    r.click_world(5.0, 0.0);
    CHECK(r.c.selection().size() == 1);

    double px, py;
    r.screen(10.0, 0.0, px, py);
    CHECK_MSG(r.c.begin_grip_drag(px, py), "no grip under the endpoint");
    CHECK(r.c.grip_dragging());

    r.screen(14.0, 0.0, px, py);
    r.c.update_grip_drag(px, py);
    CHECK(r.c.end_grip_drag());
    CHECK(!r.c.grip_dragging());

    CHECK_MSG(std::fabs(wx_of(r.c, r.c.shapes()[0].b) - 14.0) < 0.2,
              "the end landed at %.4f", wx_of(r.c, r.c.shapes()[0].b));
    CHECK_MSG(std::fabs(wx_of(r.c, r.c.shapes()[0].a)) < 1e-6,
              "the far end moved");
}

TEST(canvas_a_grip_drag_undoes_as_one_step) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    r.c.cancel();
    const auto before = r.c.shapes();

    r.c.set_tool(Tool::Select);
    r.click_world(5.0, 0.0);
    double px, py;
    r.screen(10.0, 0.0, px, py);
    r.c.begin_grip_drag(px, py);
    r.screen(14.0, 3.0, px, py);
    r.c.update_grip_drag(px, py);
    r.c.end_grip_drag();

    r.c.undo();
    CHECK(r.c.shapes()[0].b.x == before[0].b.x);
    CHECK(r.c.shapes()[0].b.y == before[0].b.y);
}

TEST(canvas_cancelling_a_grip_drag_restores_the_object) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    r.c.cancel();
    const auto before = r.c.shapes();

    r.c.set_tool(Tool::Select);
    r.click_world(5.0, 0.0);
    double px, py;
    r.screen(10.0, 0.0, px, py);
    r.c.begin_grip_drag(px, py);
    r.screen(30.0, 30.0, px, py);
    r.c.update_grip_drag(px, py);
    r.c.cancel_grip_drag();

    CHECK(!r.c.grip_dragging());
    CHECK_MSG(r.c.shapes()[0].b.x == before[0].b.x,
              "cancelling the drag left the object moved");
}

TEST(canvas_a_grip_drag_over_empty_space_starts_nothing) {
    Rig r;
    r.draw_square();
    r.c.select_all();
    double px, py;
    r.screen(80, 80, px, py);
    CHECK(!r.c.begin_grip_drag(px, py));
    CHECK(!r.c.grip_dragging());
    CHECK(!r.c.end_grip_drag());
}

TEST(canvas_hatch_fills_the_region_around_a_pick) {
    Rig r;
    r.draw_square();
    double px, py;
    r.screen(2.0, 2.0, px, py);
    CHECK_MSG(r.c.hatch_at(px, py, "ANSI31"), "%s", r.c.message().c_str());
    CHECK(r.c.hatches().size() == 1);
    CHECK(r.c.hatches()[0].valid);
    CHECK_MSG(!r.c.hatches()[0].cached.lines.empty(), "the fill is empty");
}

TEST(canvas_hatch_outside_any_boundary_is_refused) {
    Rig r;
    r.draw_square();
    double px, py;
    r.screen(80.0, 80.0, px, py);
    CHECK(!r.c.hatch_at(px, py));
    CHECK(r.c.hatches().empty());
    CHECK(!r.c.message().empty());
}

TEST(canvas_hatch_reaches_the_draw_list) {
    Rig r;
    r.draw_square();
    double px, py;
    r.screen(2.0, 2.0, px, py);
    r.c.hatch_at(px, py, "ANSI31");

    grender::d2::DrawList before;
    Rig plain;
    plain.draw_square();
    plain.c.build(before);

    grender::d2::DrawList after;
    r.c.build(after);
    CHECK_MSG(after.lines().size() > before.lines().size(),
              "the hatch drew nothing: %zu vs %zu", after.lines().size(),
              before.lines().size());
}

TEST(canvas_hatch_follows_the_wall_it_filled) {
    Rig r;
    r.draw_square();
    double px, py;
    r.screen(2.0, 2.0, px, py);
    CHECK(r.c.hatch_at(px, py, "ANSI31"));
    const std::size_t before = r.c.hatches()[0].cached.lines.size();
    CHECK(before > 0);

    r.c.select_all();
    r.c.set_tool(Tool::Move);
    r.click_world(0, 0);
    r.click_world(0, 0);
    r.c.cancel();

    r.c.clear_selection();
    r.c.set_tool(Tool::Select);
    r.click_world(4.0, 2.0);
    r.c.set_tool(Tool::Move);
    r.click_world(4.0, 2.0);
    r.click_world(9.0, 2.0);

    r.c.refresh_hatches();
    const auto& h = r.c.hatches()[0];
    CHECK_MSG(h.orphaned || h.cached.lines.size() != before,
              "the hatch neither followed nor noticed the wall moving");
}

TEST(canvas_gradient_uses_the_same_boundary_machinery) {
    Rig r;
    r.draw_square();
    double px, py;
    r.screen(2.0, 2.0, px, py);
    CHECK_MSG(r.c.gradient_at_point(px, py), "%s", r.c.message().c_str());
    CHECK(r.c.hatches().size() == 1);
    CHECK(r.c.message().find("gradient") != std::string::npos);
}

TEST(canvas_hatch_pattern_table_ships_the_standard_set) {
    Rig r;
    CHECK(r.c.patterns().find("ANSI31") != nullptr);
    CHECK(r.c.patterns().find("SOLID") != nullptr);
    CHECK(r.c.patterns().find("NOT-REAL") == nullptr);
}

TEST(canvas_point_places_a_node) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Point);
    r.c.type_point("3,4");
    CHECK(r.c.segment_count() == 1);
    CHECK(r.c.shapes()[0].kind == gsnap::ShapeKind::Point);
    CHECK(std::fabs(wx_of(r.c, r.c.shapes()[0].a) - 3.0) < 1e-6);
}

TEST(canvas_ray_and_xline_are_infinite) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Ray);
    r.c.type_point("0,0");
    r.c.type_point("1,0");
    CHECK(r.c.segment_count() == 1);
    CHECK(r.c.shapes()[0].kind == gsnap::ShapeKind::Ray);
    CHECK(r.c.shapes()[0].is_infinite());
    CHECK(!r.c.shapes()[0].is_curve());

    r.c.cancel();
    r.c.set_tool(Tool::Xline);
    r.c.type_point("0,5");
    r.c.type_point("1,5");
    CHECK(r.c.shapes()[1].kind == gsnap::ShapeKind::Xline);
    CHECK(r.c.shapes()[1].is_infinite());
}

TEST(canvas_an_infinite_line_is_drawn_clipped_not_to_the_world_extent) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Xline);
    r.c.type_point("0,0");
    r.c.type_point("1,0");

    grender::d2::DrawList dl;
    r.c.build(dl);
    bool found = false;
    for (const auto& l : dl.lines()) {
        const double span = std::hypot(l.x1 - l.x0, l.y1 - l.y0);
        if (span > 1.0) {
            found = true;
            CHECK_MSG(span < 500.0, "the xline was drawn %.1f units long", span);
        }
    }
    CHECK(found);
}

TEST(canvas_ellipse_takes_a_centre_a_major_axis_and_a_minor) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Ellipse);
    r.c.type_point("0,0");
    r.c.type_point("4,0");
    CHECK(r.c.segment_count() == 0);
    r.c.type_point("0,2");

    CHECK_MSG(r.c.segment_count() == 1, "%zu shapes", r.c.segment_count());
    const auto& e = r.c.shapes()[0];
    CHECK(e.kind == gsnap::ShapeKind::Ellipse);
    CHECK(e.is_elliptical());
    CHECK(std::fabs(wx_of(r.c, e.b) - 4.0) < 1e-6);

    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    CHECK_MSG(std::fabs(double(e.radius) * v.inv_scale() - 2.0) < 1e-4,
              "minor radius %.6f", double(e.radius) * v.inv_scale());
}

TEST(canvas_ellipse_reaches_the_draw_list) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Ellipse);
    r.c.type_point("0,0");
    r.c.type_point("4,0");
    r.c.type_point("0,2");

    grender::d2::DrawList dl;
    r.c.build(dl);
    CHECK_MSG(dl.arcs().size() == 1, "an ellipse drew %zu arcs", dl.arcs().size());
    if (dl.arcs().size() == 1) {
        CHECK_MSG(std::fabs(dl.arcs()[0].major - 4.0) < 1e-6, "major %g", dl.arcs()[0].major);
        CHECK_MSG(std::fabs(dl.arcs()[0].minor - 2.0) < 1e-6, "minor %g", dl.arcs()[0].minor);
    }
}

TEST(canvas_spline_passes_through_its_points) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Spline);
    r.c.type_point("0,0");
    r.c.type_point("2,3");
    r.c.type_point("6,1");
    CHECK_MSG(r.c.splines().size() == 1, "%zu splines", r.c.splines().size());

    const auto& c = r.c.splines()[0];
    CHECK(c.valid());
    const auto start = c.start();
    CHECK_MSG(std::fabs(start.x.to_f64()) < 1e-4 &&
                  std::fabs(start.y.to_f64()) < 1e-4,
              "the spline starts at (%.4f, %.4f)", start.x.to_f64(),
              start.y.to_f64());
    const auto end = c.end();
    CHECK(std::fabs(end.x.to_f64() - 6.0) < 1e-4);
}

TEST(canvas_spline_refits_as_it_grows) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Spline);
    r.c.type_point("0,0");
    r.c.type_point("2,3");
    r.c.type_point("6,1");
    const std::size_t after_three = r.c.splines().size();

    r.c.type_point("9,4");
    CHECK_MSG(r.c.splines().size() == after_three,
              "a fourth point created a second spline");
    CHECK(std::fabs(r.c.splines()[0].end().x.to_f64() - 9.0) < 1e-4);
}

TEST(canvas_spline_reaches_the_draw_list) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);

    grender::d2::DrawList before;
    r.c.build(before);

    r.c.set_tool(Tool::Spline);
    r.c.type_point("0,0");
    r.c.type_point("2,3");
    r.c.type_point("6,1");

    grender::d2::DrawList after;
    r.c.build(after);
    CHECK_MSG(after.lines().size() > before.lines().size() + 4,
              "the spline drew %zu segments", after.lines().size());
}

TEST(canvas_cancelling_drops_a_half_drawn_spline) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Spline);
    r.c.type_point("0,0");
    r.c.type_point("2,3");
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.c.type_point("9,9");
    r.c.type_point("9,12");
    CHECK(r.c.segment_count() == 1);
}

TEST(canvas_the_new_entities_undo) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Point);
    r.c.type_point("1,1");
    CHECK(r.c.segment_count() == 1);
    r.c.undo();
    CHECK(r.c.segment_count() == 0);
}

TEST(canvas_new_entity_tool_names_match_the_commands) {
    CHECK(std::string(tool_name(Tool::Ellipse)) == "ELLIPSE");
    CHECK(std::string(tool_name(Tool::Point)) == "POINT");
    CHECK(std::string(tool_name(Tool::Ray)) == "RAY");
    CHECK(std::string(tool_name(Tool::Xline)) == "XLINE");
    CHECK(std::string(tool_name(Tool::Spline)) == "SPLINE");
}

TEST(canvas_fence_takes_everything_it_crosses) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    for (int i = 0; i < 5; ++i) {
        r.c.set_tool(Tool::Line);
        r.c.type_point(("0," + std::to_string(i)).c_str());
        r.c.type_point(("10," + std::to_string(i)).c_str());
        r.c.cancel();
    }
    CHECK(r.c.segment_count() == 5);

    double x0, y0, x1, y1;
    r.screen(5.0, -1.0, x0, y0);
    r.screen(5.0, 6.0, x1, y1);
    const std::size_t n = r.c.select_fence({{x0, y0}, {x1, y1}}, false);
    CHECK_MSG(n == 5, "the fence found %zu", n);
    CHECK(r.c.selection().size() == 5);
}

TEST(canvas_fence_ignores_what_it_misses) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("2,0");
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.c.type_point("50,50");
    r.c.type_point("52,50");
    r.c.cancel();

    double x0, y0, x1, y1;
    r.screen(1.0, -1.0, x0, y0);
    r.screen(1.0, 1.0, x1, y1);
    CHECK(r.c.select_fence({{x0, y0}, {x1, y1}}, false) == 1);
}

TEST(canvas_fence_crosses_curves_too) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Circle);
    r.c.type_point("0,0");
    r.c.type_point("3,0");
    CHECK(r.c.segment_count() == 1);

    double x0, y0, x1, y1;
    r.screen(-5.0, 0.0, x0, y0);
    r.screen(5.0, 0.0, x1, y1);
    CHECK_MSG(r.c.select_fence({{x0, y0}, {x1, y1}}, false) == 1,
              "the fence missed the circle");
}

TEST(canvas_fence_needs_two_points) {
    Rig r;
    r.draw_square();
    CHECK(r.c.select_fence({}, false) == 0);
    CHECK(!r.c.message().empty());
}

TEST(canvas_lasso_follows_the_shape_rather_than_the_bounding_box) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("1,1");
    r.c.type_point("2,1");
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.c.type_point("8,8");
    r.c.type_point("9,8");
    r.c.cancel();

    std::vector<std::pair<double, double>> lasso;
    for (auto p : {std::make_pair(0.0, 0.0), std::make_pair(10.0, 0.0),
                   std::make_pair(10.0, 4.0), std::make_pair(4.0, 4.0),
                   std::make_pair(4.0, 10.0), std::make_pair(0.0, 10.0)}) {
        double sx, sy;
        r.screen(p.first, p.second, sx, sy);
        lasso.push_back({sx, sy});
    }

    const std::size_t n = r.c.select_lasso(lasso, false, false);
    CHECK_MSG(n == 1, "the lasso took %zu, expected only the one inside", n);
}

TEST(canvas_crossing_lasso_takes_what_it_clips) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("2,2");
    r.c.type_point("20,2");
    r.c.cancel();

    std::vector<std::pair<double, double>> lasso;
    for (auto p : {std::make_pair(0.0, 0.0), std::make_pair(6.0, 0.0),
                   std::make_pair(6.0, 6.0), std::make_pair(0.0, 6.0)}) {
        double sx, sy;
        r.screen(p.first, p.second, sx, sy);
        lasso.push_back({sx, sy});
    }

    CHECK_MSG(r.c.select_lasso(lasso, false, false) == 0,
              "a window lasso took a clipped object");
    CHECK_MSG(r.c.select_lasso(lasso, true, false) == 1,
              "a crossing lasso missed a clipped object");
}

TEST(canvas_lasso_selects_a_bare_node) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Point);
    r.c.type_point("3,3");

    std::vector<std::pair<double, double>> lasso;
    for (auto p : {std::make_pair(0.0, 0.0), std::make_pair(6.0, 0.0),
                   std::make_pair(6.0, 6.0), std::make_pair(0.0, 6.0)}) {
        double sx, sy;
        r.screen(p.first, p.second, sx, sy);
        lasso.push_back({sx, sy});
    }
    CHECK(r.c.select_lasso(lasso, false, false) == 1);
}

TEST(canvas_lasso_needs_three_points) {
    Rig r;
    r.draw_square();
    CHECK(r.c.select_lasso({{0, 0}, {10, 10}}, false, false) == 0);
    CHECK(!r.c.message().empty());
}

TEST(canvas_filter_by_kind) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.set_tool(Tool::Circle);
    r.c.type_point("10,10");
    r.c.type_point("12,10");
    r.c.cancel();

    Canvas::Filter f;
    f.by_kind = true;
    f.kind = gsnap::ShapeKind::Circle;
    CHECK_MSG(r.c.select_filtered(f, false) == 1, "%s", r.c.message().c_str());

    f.kind = gsnap::ShapeKind::Segment;
    CHECK(r.c.select_filtered(f, false) == 4);
}

TEST(canvas_filter_by_length) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    for (double len : {1.0, 5.0, 20.0}) {
        r.c.set_tool(Tool::Line);
        r.c.type_point("0,0");
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g,0", len);
        r.c.type_point(buf);
        r.c.cancel();
    }

    Canvas::Filter f;
    f.by_length = true;
    f.min_length = 3.0;
    f.max_length = 10.0;
    CHECK_MSG(r.c.select_filtered(f, false) == 1, "%s", r.c.message().c_str());
}

TEST(canvas_a_length_filter_does_not_match_a_circle) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Circle);
    r.c.type_point("0,0");
    r.c.type_point("3,0");

    Canvas::Filter f;
    f.by_length = true;
    f.min_length = 0.0;
    f.max_length = 1e9;
    CHECK(r.c.select_filtered(f, false) == 0);
}

TEST(canvas_filter_by_radius) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    for (double rad : {1.0, 4.0}) {
        r.c.set_tool(Tool::Circle);
        r.c.type_point("0,0");
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g,0", rad);
        r.c.type_point(buf);
        r.c.cancel();
    }

    Canvas::Filter f;
    f.by_radius = true;
    f.min_radius = 2.0;
    CHECK_MSG(r.c.select_filtered(f, false) == 1, "%s", r.c.message().c_str());
}

TEST(canvas_an_empty_filter_selects_nothing) {
    Rig r;
    r.draw_square();
    Canvas::Filter f;
    CHECK(!f.any());
    CHECK(r.c.select_filtered(f, false) == 0);
    CHECK(r.c.selection().empty());
}

TEST(canvas_filters_can_be_combined) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("2,0");
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,5");
    r.c.type_point("30,5");
    r.c.cancel();

    Canvas::Filter f;
    f.by_kind = true;
    f.kind = gsnap::ShapeKind::Segment;
    f.by_length = true;
    f.max_length = 10.0;
    CHECK_MSG(r.c.select_filtered(f, false) == 1, "%s", r.c.message().c_str());
}

TEST(canvas_dynamic_input_shows_distance_and_angle_once_armed) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    r.c.hover(0, 0);
    double px, py;
    r.screen(3.0, 4.0, px, py);
    r.c.hover(px, py);

    const auto in = r.c.dynamic_input();
    CHECK_MSG(in.visible, "nothing shown while a tool was armed");
    CHECK_MSG(in.fields.size() == 2, "%zu fields", in.fields.size());
    CHECK(in.fields[0].label == "D");
    CHECK(in.fields[1].label == "A");
    CHECK_MSG(in.fields[0].value.find("5") != std::string::npos,
              "distance reads '%s'", in.fields[0].value.c_str());
}

TEST(canvas_dynamic_input_shows_a_radius_for_a_circle) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Circle);
    r.click_world(0, 0);
    double px, py;
    r.screen(3.0, 0.0, px, py);
    r.c.hover(px, py);

    const auto in = r.c.dynamic_input();
    CHECK(in.visible);
    CHECK_MSG(in.fields.size() == 1, "%zu fields", in.fields.size());
    CHECK(in.fields[0].label == "R");
}

TEST(canvas_typing_a_distance_draws_it) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");

    double px, py;
    r.screen(1.0, 0.0, px, py);
    r.c.hover(px, py);

    r.c.dynamic_type("7");
    CHECK(r.c.dynamic_buffer() == "7");
    CHECK_MSG(r.c.dynamic_commit(), "%s", r.c.message().c_str());
    CHECK(r.c.segment_count() == 1);
    CHECK_MSG(std::fabs(wx_of(r.c, r.c.shapes()[0].b) - 7.0) < 1e-4,
              "the wall came out %.4f long", wx_of(r.c, r.c.shapes()[0].b));
}

TEST(canvas_tab_moves_to_the_angle_field_and_typing_there_means_an_angle) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    double px, py;
    r.screen(5.0, 0.0, px, py);
    r.c.hover(px, py);

    r.c.dynamic_tab();
    CHECK(r.c.dynamic_field_index() == 1);
    r.c.dynamic_type("90");
    CHECK(r.c.dynamic_commit());

    const auto& s = r.c.shapes()[0];
    CHECK_MSG(std::fabs(wx_of(r.c, s.b)) < 1e-3, "x = %.4f", wx_of(r.c, s.b));
    CHECK_MSG(std::fabs(wy_of(r.c, s.b) - 5.0) < 1e-3, "y = %.4f", wy_of(r.c, s.b));
}

TEST(canvas_dynamic_backspace_and_cancel) {
    Rig r;
    r.c.set_tool(Tool::Line);
    r.c.dynamic_type("123");
    r.c.dynamic_backspace();
    CHECK(r.c.dynamic_buffer() == "12");
    r.c.dynamic_cancel();
    CHECK(r.c.dynamic_buffer().empty());
    CHECK(r.c.dynamic_field_index() == 0);
    CHECK(!r.c.dynamic_commit());
}

TEST(canvas_dynamic_input_refuses_nonsense) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.dynamic_type("banana");
    CHECK(!r.c.dynamic_commit());
    CHECK(!r.c.message().empty());
    CHECK(r.c.segment_count() == 0);
}

TEST(canvas_the_typed_value_replaces_what_is_shown) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.click_world(0, 0);
    double px, py;
    r.screen(3.0, 0.0, px, py);
    r.c.hover(px, py);

    r.c.dynamic_type("42");
    const auto in = r.c.dynamic_input();
    CHECK_MSG(in.fields[0].value == "42", "shown '%s'", in.fields[0].value.c_str());
}

TEST(canvas_multiline_draws_every_element_of_its_style) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    CHECK(r.c.multilines().size() > 0);

    std::string chosen;
    std::size_t elements = 0;
    for (const auto& st : r.c.multilines().all())
        if (st.elements.size() >= 2) {
            chosen = st.name;
            elements = st.elements.size();
            break;
        }
    CHECK_MSG(!chosen.empty(), "no multiline style has two elements");
    r.c.set_multiline_style(chosen);
    CHECK(r.c.multiline_style() == chosen);

    r.c.set_tool(Tool::Multiline);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    CHECK_MSG(r.c.segment_count() >= elements,
              "%zu segments for a %zu-element wall", r.c.segment_count(),
              elements);
}

TEST(canvas_multiline_grows_as_one_object) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    std::string chosen;
    for (const auto& st : r.c.multilines().all())
        if (st.elements.size() >= 2) { chosen = st.name; break; }
    r.c.set_multiline_style(chosen);

    r.c.set_tool(Tool::Multiline);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    const std::size_t after_two = r.c.segment_count();
    r.c.type_point("10,10");
    CHECK_MSG(r.c.segment_count() > after_two,
              "the third point added nothing (%zu)", r.c.segment_count());

    r.c.undo();
    CHECK_MSG(r.c.segment_count() == 0, "%zu segments survived undo",
              r.c.segment_count());
}

TEST(canvas_multiline_justification_moves_the_run) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    std::string chosen;
    for (const auto& st : r.c.multilines().all())
        if (st.elements.size() >= 2) { chosen = st.name; break; }
    r.c.set_multiline_style(chosen);

    auto draw = [&](gdoc::Justify j) {
        Rig fresh;
        fresh.c.set_display_unit(gunit::Length::Metre);
        fresh.c.set_multiline_style(chosen);
        fresh.c.set_multiline_justify(j);
        fresh.c.set_tool(Tool::Multiline);
        fresh.c.type_point("0,0");
        fresh.c.type_point("10,0");
        double lo = 1e300;
        for (const auto& s : fresh.c.shapes()) lo = std::min(lo, wy_of(fresh.c, s.a));
        return lo;
    };

    const double zero = draw(gdoc::Justify::Zero);
    const double top = draw(gdoc::Justify::Top);
    CHECK_MSG(std::fabs(zero - top) > 1e-6,
              "justification did not move the wall (%.6f)", zero - top);
}

TEST(canvas_multiline_scale_widens_the_wall) {
    auto width = [](double scale) {
        Rig r;
        r.c.set_display_unit(gunit::Length::Metre);
        std::string chosen;
        for (const auto& st : r.c.multilines().all())
            if (st.elements.size() >= 2) { chosen = st.name; break; }
        r.c.set_multiline_style(chosen);
        r.c.multiline_scale() = scale;
        r.c.set_tool(Tool::Multiline);
        r.c.type_point("0,0");
        r.c.type_point("10,0");

        double lo = 1e300, hi = -1e300;
        for (const auto& s : r.c.shapes()) {
            lo = std::min(lo, wy_of(r.c, s.a));
            hi = std::max(hi, wy_of(r.c, s.a));
        }
        return hi - lo;
    };
    CHECK_MSG(width(4.0) > width(1.0) * 3.0, "the scale did not widen the wall");
}

TEST(canvas_an_unknown_multiline_style_is_ignored) {
    Rig r;
    const std::string before = r.c.multiline_style();
    r.c.set_multiline_style("NOT A STYLE");
    CHECK(r.c.multiline_style() == before);
}

TEST(canvas_define_block_consumes_the_selection) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    CHECK(r.c.segment_count() == 4);
    r.c.select_all();

    double px, py;
    r.screen(0, 0, px, py);
    CHECK_MSG(r.c.define_block("DOOR", px, py), "%s", r.c.message().c_str());
    CHECK_MSG(r.c.segment_count() == 0, "%zu loose shapes survived",
              r.c.segment_count());
    CHECK(r.c.blocks().find("DOOR") != nullptr);
    CHECK(r.c.blocks().find("DOOR")->geometry.size() == 4);
    CHECK(r.c.selection().empty());
}

TEST(canvas_a_definition_is_stored_once_however_often_it_appears) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.select_all();
    double px, py;
    r.screen(0, 0, px, py);
    r.c.define_block("DOOR", px, py);

    for (int i = 1; i <= 5; ++i) {
        r.screen(i * 10.0, 0, px, py);
        CHECK(r.c.insert_block("DOOR", px, py));
    }
    CHECK_MSG(r.c.blocks().size() == 1, "%zu definitions", r.c.blocks().size());
    CHECK_MSG(r.c.blocks().references().size() == 6, "%zu insertions",
              r.c.blocks().references().size());
}

TEST(canvas_blocks_reach_the_draw_list_through_their_transform) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.select_all();
    double px, py;
    r.screen(0, 0, px, py);
    r.c.define_block("DOOR", px, py);

    grender::d2::DrawList one;
    r.c.build(one);
    const std::size_t with_one = one.lines().size();
    CHECK_MSG(with_one >= 4, "the defining insertion drew %zu lines", with_one);

    r.screen(50.0, 50.0, px, py);
    r.c.insert_block("DOOR", px, py);
    grender::d2::DrawList two;
    r.c.build(two);
    CHECK_MSG(two.lines().size() == with_one * 2,
              "%zu lines for two insertions of a %zu-line block",
              two.lines().size(), with_one);
}

TEST(canvas_inserting_an_unknown_block_is_refused) {
    Rig r;
    CHECK(!r.c.insert_block("NOPE", 0, 0));
    CHECK(!r.c.message().empty());
    CHECK(r.c.blocks().references().empty());
}

TEST(canvas_defining_a_block_with_nothing_selected_is_refused) {
    Rig r;
    r.draw_square();
    CHECK(!r.c.define_block("X", 0, 0));
    r.c.select_all();
    CHECK(!r.c.define_block("", 0, 0));
}

TEST(canvas_explode_scatters_the_geometry_and_removes_the_insertion) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.select_all();
    double px, py;
    r.screen(0, 0, px, py);
    r.c.define_block("DOOR", px, py);
    r.screen(20.0, 0, px, py);
    r.c.insert_block("DOOR", px, py);
    CHECK(r.c.blocks().references().size() == 2);

    CHECK_MSG(r.c.explode_block(1), "%s", r.c.message().c_str());
    CHECK_MSG(r.c.segment_count() == 4, "%zu shapes after exploding",
              r.c.segment_count());
    CHECK(r.c.blocks().references().size() == 1);
    CHECK(r.c.blocks().find("DOOR") != nullptr);

    CHECK(!r.c.explode_block(99));
}

TEST(canvas_exploded_geometry_lands_where_the_insertion_was) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("1,0");
    r.c.cancel();
    r.c.select_all();

    double px, py;
    r.screen(0, 0, px, py);
    r.c.define_block("BAR", px, py);
    r.screen(10.0, 5.0, px, py);
    r.c.insert_block("BAR", px, py);
    r.c.explode_block(1);

    bool found = false;
    for (const auto& s : r.c.shapes())
        if (std::fabs(wx_of(r.c, s.a) - 10.0) < 1e-4 &&
            std::fabs(wy_of(r.c, s.a) - 5.0) < 1e-4)
            found = true;
    CHECK_MSG(found, "the exploded geometry did not land at the insertion");
}

TEST(canvas_attributes_are_per_insertion) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.select_all();
    double px, py;
    r.screen(0, 0, px, py);
    r.c.define_block("DOOR", px, py);

    gdoc::BlockDef def = *r.c.blocks().find("DOOR");
    gdoc::AttributeDef tag;
    tag.tag = "DOOR_NO";
    tag.default_value = "D-00";
    def.attributes.push_back(tag);
    r.c.blocks().define(def);

    r.screen(10.0, 0, px, py);
    r.c.insert_block("DOOR", px, py);
    r.screen(20.0, 0, px, py);
    r.c.insert_block("DOOR", px, py);

    CHECK(r.c.set_attribute(1, "DOOR_NO", "D-14"));
    CHECK(r.c.set_attribute(2, "DOOR_NO", "D-15"));

    const auto a = r.c.attributes_of(1);
    const auto b = r.c.attributes_of(2);
    CHECK(a.size() == 1 && b.size() == 1);
    CHECK_MSG(a[0].second == "D-14", "insertion 1 says '%s'", a[0].second.c_str());
    CHECK_MSG(b[0].second == "D-15", "insertion 2 says '%s'", b[0].second.c_str());

    CHECK(!r.c.set_attribute(99, "DOOR_NO", "X"));
    CHECK(r.c.attributes_of(99).empty());
}

TEST(canvas_an_inserted_block_carries_its_attribute_defaults) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.select_all();
    double px, py;
    r.screen(0, 0, px, py);
    r.c.define_block("DOOR", px, py);

    gdoc::BlockDef def = *r.c.blocks().find("DOOR");
    gdoc::AttributeDef tag;
    tag.tag = "DOOR_NO";
    tag.default_value = "D-00";
    def.attributes.push_back(tag);
    r.c.blocks().define(def);

    r.screen(10.0, 0, px, py);
    r.c.insert_block("DOOR", px, py);
    const auto a = r.c.attributes_of(1);
    CHECK(a.size() == 1);
    CHECK(a[0].second == "D-00");
}

TEST(canvas_a_dynamic_block_evaluates_per_insertion) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("1,0");
    r.c.cancel();
    r.c.select_all();

    double px, py;
    r.screen(0, 0, px, py);
    r.c.define_block("BAR", px, py);

    gdoc::DynamicBlock& d = r.c.blocks().make_dynamic("BAR");
    gdoc::BlockParameter p;
    p.name = "Offset";
    d.parameters.push_back(p);
    gdoc::BlockAction a;
    a.kind = gdoc::ActionKind::Move;
    a.parameter = "Offset";
    a.geometry = {0};
    a.dy = 1.0;
    d.actions.push_back(a);

    grender::d2::DrawList before;
    r.c.build(before);
    d.set_parameter("Offset", 3.0);
    grender::d2::DrawList after;
    r.c.build(after);

    CHECK(before.lines().size() == after.lines().size());
    bool moved = false;
    for (std::size_t i = 0; i < before.lines().size(); ++i)
        if (std::fabs(after.lines()[i].y0 - before.lines()[i].y0) > 1e-6)
            moved = true;
    CHECK_MSG(moved, "the dynamic parameter did not reach the drawing");
}

TEST(canvas_blocks_share_nested_geometry_layer_inheritance_and_picking) {
    Rig r;
    gdoc::BlockDef inner; inner.name = "Inner";
    gsnap::Shape edge; edge.layer = "0";
    edge.a = gcm::to_lattice(r.c.volume(), 0, 0);
    edge.b = gcm::to_lattice(r.c.volume(), 4, 0);
    inner.geometry.push_back(edge); r.c.blocks().define(inner);
    gdoc::BlockDef outer; outer.name = "Outer"; outer.nested = {inner.name};
    r.c.blocks().define(outer);
    gdoc::BlockRef ref; ref.name = outer.name; ref.layer = "Visible";
    const auto index = r.c.blocks().insert(ref);
    r.c.set_hidden_layers({"0"});
    grender::d2::DrawList drawing; r.c.build(drawing);
    CHECK(drawing.lines().size() == 1);
    double x, y; r.screen(2, 0, x, y);
    CHECK(r.c.pick_reference(x, y) == index);
    const auto expanded = r.c.blocks().expand(ref, r.c.volume());
    CHECK(expanded.size() == 1);
    if (!expanded.empty()) CHECK(expanded[0].layer == "Visible");
    r.c.set_hidden_layers({"Visible"}); r.c.build(drawing);
    CHECK(drawing.lines().empty()); CHECK(r.c.pick_reference(x, y) == Canvas::npos32);
}

TEST(canvas_dynamic_blocks_share_render_pick_snap_and_explode_geometry) {
    Rig r;
    gdoc::BlockDef def; def.name = "Moving";
    gsnap::Shape edge; edge.a = gcm::to_lattice(r.c.volume(), 0, 0);
    edge.b = gcm::to_lattice(r.c.volume(), 4, 0); def.geometry.push_back(edge);
    r.c.blocks().define(def);
    gdoc::BlockRef ref; ref.name = def.name; const auto index = r.c.blocks().insert(ref);
    auto& dynamic = r.c.blocks().make_dynamic(def.name);
    gdoc::BlockParameter p; p.name = "Offset"; dynamic.parameters.push_back(p);
    gdoc::BlockAction action; action.kind = gdoc::ActionKind::Move;
    action.parameter = p.name; action.geometry = {0}; action.dy = 1;
    dynamic.actions.push_back(action);
    dynamic.visibility = {{"On", {0}}, {"Off", {}}};
    r.c.set_tool(Tool::Line); r.c.set_grid_snap(false);
    double x, y; r.screen(2, 0, x, y);
    CHECK(r.c.pick_reference(x, y) == index);
    r.screen(0, 0, x, y); r.c.hover(x, y); CHECK(r.c.snap().valid);
    CHECK(dynamic.set_parameter("Offset", 3));
    r.screen(2, 0, x, y); CHECK(r.c.pick_reference(x, y) == Canvas::npos32);
    r.screen(2, 3, x, y); CHECK(r.c.pick_reference(x, y) == index);
    r.screen(0, 3, x, y); r.c.hover(x, y); CHECK(r.c.snap().valid);
    grender::d2::DrawList drawing; r.c.build(drawing);
    CHECK(drawing.lines().size() == 1);
    if (!drawing.lines().empty()) CHECK(std::abs(drawing.lines()[0].y0 - 3) < 1e-5);
    const auto exploded = r.c.blocks().explode(index, r.c.volume());
    CHECK(exploded && exploded->size() == 1);
    if (exploded && !exploded->empty()) CHECK(std::abs(wy_of(r.c, exploded->front().a) - 3) < 1e-5);
    CHECK(dynamic.set_visibility("Off"));
    r.c.build(drawing); CHECK(drawing.lines().empty());
    CHECK(r.c.pick_reference(x, y) == Canvas::npos32);
    r.c.hover(x, y); CHECK(!r.c.snap().valid);
    CHECK(dynamic.set_visibility("On"));
    r.c.build(drawing); CHECK(drawing.lines().size() == 1);
    CHECK(r.c.pick_reference(x, y) == index);
}

TEST(canvas_underlays_default_to_not_plotting) {
    Rig r;
    gdoc::Underlay u;
    u.path = "survey.pdf";
    r.c.underlays().attach(u);
    CHECK(r.c.underlays().size() == 1);
    CHECK(!r.c.underlays().all()[0].plottable);
    CHECK(r.c.underlays().all()[0].kind == gdoc::UnderlayKind::Pdf);
}

static bool has_tool(const std::vector<Tool>& v, Tool t) {
    for (Tool x : v) if (x == t) return true;
    return false;
}

TEST(tool_ring_is_contextual_not_a_toolbar_in_a_circle) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    r.c.clear_selection();

    const auto drawing = r.c.tool_ring();
    CHECK_MSG(has_tool(drawing, Tool::Line), "the empty ring has no Line");
    CHECK_MSG(!has_tool(drawing, Tool::Move),
              "the empty ring offered Move with nothing selected");

    r.c.select_all();
    const auto editing = r.c.tool_ring();
    CHECK_MSG(has_tool(editing, Tool::Move), "the selected ring has no Move");
    CHECK(has_tool(editing, Tool::Rotate));
    CHECK_MSG(!has_tool(editing, Tool::Line),
              "the selected ring offered Line");
}

TEST(tool_ring_sectors_stay_wide_enough_to_hit) {
    Rig r;
    r.draw_square();
    CHECK_MSG(r.c.tool_ring().size() <= 8, "%zu items in the ring",
              r.c.tool_ring().size());
    r.c.select_all();
    CHECK(r.c.tool_ring().size() <= 8);
    CHECK(!r.c.tool_ring().empty());
}

TEST(tool_availability_refuses_what_cannot_run) {
    Rig r;
    CHECK_MSG(r.c.tool_available(Tool::Line), "Line was unavailable on an "
                                              "empty drawing");
    CHECK_MSG(!r.c.tool_available(Tool::Move),
              "Move was available with nothing selected");
    CHECK(!r.c.tool_available(Tool::Trim));
    CHECK(!r.c.tool_available(Tool::Fillet));
    CHECK(!r.c.tool_available(Tool::DimLinear));

    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    CHECK(r.c.tool_available(Tool::Trim));
    CHECK(r.c.tool_available(Tool::Fillet));
    CHECK(r.c.tool_available(Tool::DimLinear));
    CHECK(!r.c.tool_available(Tool::Move));

    r.c.select_all();
    CHECK(r.c.tool_available(Tool::Move));
    CHECK(r.c.tool_available(Tool::ArrayPolar));
}

TEST(tool_availability_knows_a_radius_needs_a_curve) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.draw_square();
    CHECK_MSG(!r.c.tool_available(Tool::DimRadial),
              "a drawing of straight lines offered a radius dimension");

    r.c.set_tool(Tool::Circle);
    r.c.type_point("20,20");
    r.c.type_point("23,20");
    r.c.cancel();
    CHECK(r.c.tool_available(Tool::DimRadial));
    CHECK(r.c.tool_available(Tool::DimDiameter));
}

static bool has_snap(const std::vector<const char*>& v, const char* k) {
    for (const char* x : v) if (std::string(x) == k) return true;
    return false;
}

TEST(osnap_ring_reports_what_is_under_the_cursor) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    r.c.cancel();

    double px, py;
    r.screen(0, 0, px, py);
    const auto at_end = r.c.osnap_ring(px, py);
    CHECK_MSG(has_snap(at_end, "endpoint"), "no endpoint at the endpoint");

    r.screen(5, 0, px, py);
    CHECK(has_snap(r.c.osnap_ring(px, py), "midpoint"));

    r.screen(400, 400, px, py);
    CHECK_MSG(r.c.osnap_ring(px, py).empty(),
              "empty space offered a snap");
}

TEST(osnap_ring_choice_lasts_one_pick_only) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    r.c.cancel();

    CHECK(r.c.force_snap("midpoint"));
    CHECK(r.c.snaps().settings.forced != 0);
    CHECK(!r.c.force_snap("not-a-snap"));

    r.c.set_tool(Tool::Line);
    double px, py;
    r.screen(5, 0, px, py);
    r.c.click(px, py);
    r.c.cancel();
    CHECK_MSG(r.c.snaps().settings.forced == 0,
              "the forced snap outlived its pick");
}

TEST(canvas_the_cursor_aligns_with_an_existing_endpoint) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("10,0");
    r.c.type_point("10,4");
    r.c.cancel();

    double px, py;
    r.screen(10.05, 9.0, px, py);
    r.c.hover(px, py);

    CHECK_MSG(r.c.alignment().active, "nothing aligned");
    CHECK_MSG(r.c.alignment().vertical, "the vertical alignment was missed");
    double cx, cy;
    r.world(r.c.cursor(), cx, cy);
    CHECK_MSG(std::fabs(cx - 10.0) < 1e-6,
              "the cursor sits at x=%.4f, not on the endpoint's x", cx);
}

TEST(canvas_alignment_snaps_both_axes_from_two_different_points) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("10,0");
    r.c.type_point("10,1");
    r.c.cancel();
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,7");
    r.c.type_point("1,7");
    r.c.cancel();

    double px, py;
    r.screen(10.04, 7.04, px, py);
    r.c.hover(px, py);

    CHECK(r.c.alignment().active);
    CHECK_MSG(r.c.alignment().vertical && r.c.alignment().horizontal,
              "only one axis aligned");
    double cx, cy;
    r.world(r.c.cursor(), cx, cy);
    CHECK_MSG(std::fabs(cx - 10.0) < 1e-6 && std::fabs(cy - 7.0) < 1e-6,
              "landed at %.4f,%.4f", cx, cy);
}

TEST(canvas_alignment_does_not_reach_across_the_whole_drawing) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("10,0");
    r.c.type_point("10,1");
    r.c.cancel();

    double px, py;
    r.screen(10.0, 5000.0, px, py);
    r.c.hover(px, py);
    CHECK_MSG(!r.c.alignment().vertical,
              "a point thousands of units away still pulled the cursor");
}

TEST(canvas_an_object_snap_beats_an_alignment) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    r.c.type_point("10,0");
    r.c.cancel();

    double px, py;
    r.screen(10.0, 0.0, px, py);
    r.c.hover(px, py);
    CHECK_MSG(std::string(r.c.snap().kind) == "endpoint",
              "got '%s' where the endpoint is", r.c.snap().kind);
    CHECK_MSG(!r.c.alignment().active,
              "an alignment fired on top of an object snap");
}

TEST(canvas_a_typed_point_is_never_pulled_by_an_alignment) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("10,0");
    r.c.type_point("10,1");
    r.c.cancel();

    r.c.set_tool(Tool::Line);
    r.c.type_point("10.004,9");
    double cx, cy;
    r.world(r.c.cursor(), cx, cy);
    CHECK_MSG(std::fabs(cx - 10.004) < 1e-6,
              "a typed 10.004 was pulled to %.6f", cx);
}

TEST(canvas_alignment_follows_the_drawing_as_it_changes) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    double px, py;

    r.screen(10.0, 9.0, px, py);
    r.c.hover(px, py);
    CHECK(!r.c.alignment().active);

    r.c.set_tool(Tool::Line);
    r.c.type_point("10,0");
    r.c.type_point("10,1");
    r.c.cancel();

    r.screen(10.03, 9.0, px, py);
    r.c.hover(px, py);
    CHECK_MSG(r.c.alignment().active,
              "the new geometry was not picked up as an alignment source");
}

TEST(canvas_the_second_point_lines_up_with_the_first) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("3,3");

    double px, py;
    r.screen(3.04, 9.0, px, py);
    r.c.hover(px, py);
    double cx, cy;
    r.world(r.c.cursor(), cx, cy);
    CHECK_MSG(std::fabs(cx - 3.0) < 1e-3,
              "the second point sits at x=%.4f, not under the first", cx);
}

TEST(select_mode_neither_snaps_nor_aligns) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("6,0");
    r.c.type_point("6,2");
    r.c.cancel();
    r.c.set_tool(Tool::Select);

    double px, py;
    r.screen(6.05, 8.0, px, py);
    r.c.hover(px, py);
    CHECK_MSG(!r.c.alignment().vertical, "select mode aligned the idle pointer");
    CHECK_MSG(!r.c.snap().valid, "select mode snapped the idle pointer");
    double cx, cy;
    r.world(r.c.cursor(), cx, cy);
    CHECK_MSG(std::fabs(cx - 6.05) < 1e-3, "the idle pointer was pulled to x=%.4f", cx);
}

namespace {

Canvas drag_fixture() {
    Canvas c;
    c.view().set_viewport(800, 600);
    c.set_display_unit(gunit::Length::Metre);
    c.set_tool(Tool::Line);
    c.type_point("0,0");
    c.type_point("10,0");
    c.cancel();
    c.set_tool(Tool::Line);
    c.type_point("0,5");
    c.type_point("10,5");
    c.cancel();
    c.set_tool(Tool::Select);
    return c;
}

}

TEST(canvas_a_drag_that_barely_moves_is_a_click_not_a_window) {
    Canvas c = drag_fixture();
    c.begin_band(100, 100);
    c.update_band(102, 101);
    CHECK(c.end_band(false) == Canvas::npos);
}

TEST(canvas_dragging_a_window_selects_what_is_inside_it) {
    Canvas c = drag_fixture();
    double x0, y0, x1, y1;
    c.view().screen_of(-1.0, -1.0, x0, y0);
    c.view().screen_of(11.0, 6.0, x1, y1);

    c.begin_band(x0, y0);
    c.update_band(x1, y1);
    const std::size_t n = c.end_band(false);
    CHECK_MSG(n == 2, "a window round both lines took %zu", n);
    CHECK(c.selection().size() == 2);
}

TEST(canvas_window_direction_decides_crossing) {
    Canvas c = drag_fixture();
    c.begin_band(100, 100);
    c.update_band(400, 300);
    CHECK_MSG(!c.band().crossing, "left-to-right must not be crossing");

    c.cancel_band();
    c.begin_band(400, 100);
    c.update_band(100, 300);
    CHECK_MSG(c.band().crossing, "right-to-left must be crossing");
}

TEST(canvas_a_cancelled_window_selects_nothing) {
    Canvas c = drag_fixture();
    c.begin_band(0, 0);
    c.update_band(799, 599);
    c.cancel_band();
    CHECK(!c.band().active);
    CHECK(c.selection().empty());
}

TEST(canvas_dragging_a_selection_moves_every_object_in_it) {
    Canvas c = drag_fixture();
    c.select_all();
    CHECK(c.selection().size() == 2);

    double sx, sy, tx, ty;
    c.view().screen_of(5.0, 0.0, sx, sy);
    c.view().screen_of(5.0, 3.0, tx, ty);

    CHECK_MSG(c.begin_selection_drag(sx, sy), "a press on a selected object must drag it");
    c.update_selection_drag(tx, ty);
    CHECK(c.end_selection_drag());

    const auto& shapes = c.shapes();
    CHECK(shapes.size() == 2);
    double ax, ay, bx, by;
    gcm::to_world(c.volume(), shapes[0].a, ax, ay);
    gcm::to_world(c.volume(), shapes[1].a, bx, by);
    CHECK_MSG(std::fabs(ay - 3.0) < 1e-6, "the first line ended at y=%.4f", ay);
    CHECK_MSG(std::fabs(by - 8.0) < 1e-6, "the second line ended at y=%.4f", by);
}

TEST(canvas_a_press_on_empty_space_does_not_drag_the_selection) {
    Canvas c = drag_fixture();
    c.select_all();
    double sx, sy;
    c.view().screen_of(200.0, 200.0, sx, sy);
    CHECK_MSG(!c.begin_selection_drag(sx, sy),
              "empty space must start a window, not move the selection");
}

TEST(canvas_a_press_on_an_unselected_object_does_not_drag) {
    Canvas c = drag_fixture();
    double sx, sy;
    c.view().screen_of(5.0, 0.0, sx, sy);
    c.select_at(sx, sy, false);
    CHECK(c.selection().size() == 1);

    double ox, oy;
    c.view().screen_of(5.0, 5.0, ox, oy);
    CHECK(!c.begin_selection_drag(ox, oy));
}

TEST(canvas_a_cancelled_move_puts_everything_back) {
    Canvas c = drag_fixture();
    c.select_all();
    const auto before = c.shapes();

    double sx, sy, tx, ty;
    c.view().screen_of(5.0, 0.0, sx, sy);
    c.view().screen_of(60.0, 40.0, tx, ty);
    CHECK(c.begin_selection_drag(sx, sy));
    c.update_selection_drag(tx, ty);
    c.cancel_selection_drag();

    const auto& after = c.shapes();
    CHECK(after.size() == before.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        CHECK(after[i].a.x == before[i].a.x);
        CHECK(after[i].a.y == before[i].a.y);
        CHECK(after[i].b.x == before[i].b.x);
        CHECK(after[i].b.y == before[i].b.y);
    }
}

TEST(canvas_a_move_that_returns_to_its_start_leaves_geometry_untouched) {
    Canvas c = drag_fixture();
    c.select_all();
    const auto before = c.shapes();

    double sx, sy;
    c.view().screen_of(5.0, 0.0, sx, sy);
    CHECK(c.begin_selection_drag(sx, sy));
    for (int i = 0; i < 64; ++i) {
        const double a = 6.28318530718 * i / 64.0;
        c.update_selection_drag(sx + 90.0 * std::cos(a), sy + 90.0 * std::sin(a));
    }
    c.update_selection_drag(sx, sy);
    CHECK(c.end_selection_drag());

    const auto& after = c.shapes();
    for (std::size_t i = 0; i < after.size(); ++i) {
        CHECK_MSG(after[i].a.x == before[i].a.x && after[i].a.y == before[i].a.y,
                  "shape %zu drifted after a round trip", i);
    }
}

TEST(c38_leader_grips_edit_each_definition_point_and_undo) {
    for(int point=1;point<=3;++point) {
        Rig r;r.c.set_tool(Tool::Select);r.c.snaps().settings.enabled=false;
        gdim::Dimension d;d.kind=gdim::Kind::Leader;d.p1=gcm::to_lattice(r.c.volume(),1,1);d.p2=gcm::to_lattice(r.c.volume(),2,2);d.p3=gcm::to_lattice(r.c.volume(),3,2);d.text_override="Anchor";
        r.c.add_dimensions({d});
        auto original=point==1?d.p1:point==2?d.p2:d.p3;
        double x,y,px,py,tx,ty;r.world(original,x,y);r.screen(x,y,px,py);r.screen(x+.4,y+.3,tx,ty);
        CHECK(r.c.select_and_drag(px,py));r.c.update_selection_drag(tx,ty);CHECK(r.c.end_selection_drag());
        const auto& edited=r.c.dimensions()[0];auto p=point==1?edited.p1:point==2?edited.p2:edited.p3;double ax,ay;r.world(p,ax,ay);
        CHECK(std::abs(ax-x-.4)<1e-5);CHECK(std::abs(ay-y-.3)<1e-5);
        CHECK(edited.text_override=="Anchor");r.c.undo();const auto& undone=r.c.dimensions()[0];auto restored=point==1?undone.p1:point==2?undone.p2:undone.p3;
        CHECK(restored.x==original.x&&restored.y==original.y);
    }
}

TEST(c30_array_counts_and_entity_identities_match_placements) {
    Rig r;r.draw_square();r.c.select_all();const auto initial=r.c.shapes().size();
    r.c.array_rows()=2;r.c.array_columns()=3;r.c.set_tool(Tool::ArrayRect);r.c.type_point("0,0");r.c.type_point("5,5");
    CHECK(r.c.shapes().size()==initial*6);
    std::vector<gcm::u32> keys;for(const auto& shape:r.c.shapes())keys.push_back(shape.key);
    std::sort(keys.begin(),keys.end());CHECK(keys.front()!=0);CHECK(std::adjacent_find(keys.begin(),keys.end())==keys.end());
    r.c.undo();CHECK(r.c.shapes().size()==initial);
}

TEST(canvas_transparency_inherits_independently_of_colour_and_releases_invisible_selection) {
    Rig r;r.c.set_current_layer("Faded");r.draw_square();r.c.set_tool(Tool::Select);
    r.c.set_layer_transparencies({{"Faded",60}});
    auto shape=r.c.shapes().front();
    CHECK(std::abs(r.c.color_of_shape(shape).a-.4f)<1e-6);
    shape.attrs.color=gdoc::Color::rgb(200,40,10);
    CHECK(std::abs(r.c.color_of_shape(shape).a-.4f)<1e-6);
    shape.attrs.transparency=0;CHECK(r.c.color_of_shape(shape).a==1);
    shape.attrs.transparency=95;CHECK(std::abs(r.c.color_of_shape(shape).a-.05f)<1e-6);
    r.click_world(2,0);CHECK(!r.c.selection().empty());
    r.c.set_layer_transparencies({{"Faded",100}});
    CHECK(r.c.selection().empty());CHECK(r.c.grips().empty());CHECK(r.pick_world(2,0)==Canvas::npos);
    r.c.select_all();CHECK(r.c.selection().empty());
    r.c.set_layer_transparencies({{"Faded",60}});CHECK(r.pick_world(2,0)!=Canvas::npos);
}

TEST(block_byblock_opacity_uses_the_same_layers_for_rendering_and_picking) {
    Rig r;gdoc::LayerTable layers;gdoc::Layer layer;layer.name="Insertion";layer.transparency=100;layers.add(layer);
    gdoc::BlockDef def;def.name="Symbol";gsnap::Shape shape;
    shape.a=gcm::to_lattice(r.c.volume(),0,0);shape.b=gcm::to_lattice(r.c.volume(),4,0);
    shape.layer="Member";shape.attrs.transparency=-2;def.geometry.push_back(shape);r.c.blocks().define(def);
    gdoc::BlockRef ref;ref.name=def.name;ref.layer=layer.name;r.c.blocks().insert(ref);
    r.c.set_appearance_layers(layers);r.c.set_tool(Tool::Select);r.click_world(2,0);
    CHECK(r.c.selected_references().empty());
    layer.transparency=50;layers.add(layer);r.c.set_appearance_layers(layers);r.click_world(2,0);
    CHECK(r.c.selected_references().size()==1);
    layer.transparency=100;layers.add(layer);r.c.set_appearance_layers(layers);
    CHECK(r.c.selected_references().empty());CHECK(r.c.grips().empty());
}

TEST(nested_nonuniform_arc_rendering_and_picking_share_the_visible_interval) {
    Rig r;const auto v=r.c.volume();const double pi=std::acos(-1.);
    gsnap::Shape arc;arc.kind=gsnap::ShapeKind::Arc;arc.a={};arc.radius=std::llround(2*v.scale());arc.start_angle=pi/4;arc.end_angle=3*pi/4;
    gdoc::BlockDef child;child.name="Arc";child.geometry={arc};r.c.blocks().define(child);
    gdoc::BlockDef parent;parent.name="Assembly";gdoc::BlockRef inner;inner.name=child.name;inner.scale_x=2;parent.nested_refs={inner};r.c.blocks().define(parent);
    gdoc::BlockRef ref;ref.name=parent.name;const auto id=r.c.blocks().insert(ref);
    grender::d2::DrawList drawing;r.c.build(drawing);CHECK(drawing.arcs().size()==1);
    if(!drawing.arcs().empty()){CHECK(std::abs(drawing.arcs()[0].a0-pi/4)<1e-8);CHECK(std::abs(drawing.arcs()[0].a1-3*pi/4)<1e-8);}
    double x,y;r.screen(0,2,x,y);CHECK(r.c.pick_reference(x,y)==id);
    r.screen(0,-2,x,y);CHECK(r.c.pick_reference(x,y)==Canvas::npos32);
    r.screen(4,0,x,y);CHECK(r.c.pick_reference(x,y)==Canvas::npos32);
}


TEST(select_all_uses_visible_reference_geometry) {
    Rig r;gdoc::BlockDef def;def.name="Reference";def.is_xref=true;gsnap::Shape edge;edge.b=gcm::to_lattice(r.c.volume(),2,0);def.geometry={edge};r.c.blocks().define(def);
    gdoc::Xref x;x.name=def.name;x.state=gdoc::XrefState::Loaded;x.clip={gcm::to_lattice(r.c.volume(),4,-1),gcm::to_lattice(r.c.volume(),5,1)};r.c.blocks().attach(x);
    gdoc::BlockRef ref;ref.name=def.name;r.c.blocks().insert(ref);
    r.c.select_all();CHECK(r.c.selected_references().empty());
    r.c.blocks().xref(x.name)->clip_enabled=false;r.c.select_all();CHECK(r.c.selected_references().size()==1);
    r.c.blocks().unload(x.name);r.c.select_all();CHECK(r.c.selected_references().empty());
}

TEST(block_labels_follow_nested_affine_placements_and_reference_selection) {
    Rig rig; auto& canvas=rig.c; const auto& v=canvas.volume();
    gdoc::BlockDef leaf; leaf.name="Label"; leaf.base=gcm::to_lattice(v,1,1);
    gdoc::BlockDef::Note note; note.pos=gcm::to_lattice(v,2,3); note.text="Owned"; note.height=.2; note.angle=.4;
    note.attrs.color=gdoc::Color::by_block(); leaf.notes.push_back(note); canvas.blocks().define(leaf);
    gdoc::BlockRef child; child.name="Label"; child.insertion=gcm::to_lattice(v,3,4);
    child.scale_x=-2; child.scale_y=3; child.rotation=.6; child.attrs.color=gdoc::Color::by_block();
    gdoc::BlockDef parent; parent.name="Parent"; parent.base=gcm::to_lattice(v,2,-1); parent.nested_refs.push_back(child);
    canvas.blocks().define(parent);
    gdoc::BlockRef ref; ref.name="Parent"; ref.insertion=gcm::to_lattice(v,10,20);
    ref.scale_x=4; ref.scale_y=.5; ref.rotation=-.3; ref.attrs.color=gdoc::Color::aci(1);
    canvas.blocks().insert(ref);
    const auto labels=canvas.reference_labels(ref); CHECK(labels.size()==1); if(labels.empty())return;
    grender::d2::DrawList dynamic;canvas.build_dynamic_only(dynamic);CHECK(canvas.labels().size()==1);
    const auto& label=labels[0]; CHECK(label.text=="Owned"); CHECK(label.color.r==1); CHECK(label.color.g==0);
    const auto place=[&](double x,double y,const gdoc::BlockRef& r,const gdoc::BlockDef& d) {
        double bx,by,ix,iy;gcm::to_world(v,d.base,bx,by);gcm::to_world(v,r.insertion,ix,iy);
        x=(x-bx)*r.scale_x;y=(y-by)*r.scale_y;
        return std::pair{ix+std::cos(r.rotation)*x-std::sin(r.rotation)*y,
                         iy+std::sin(r.rotation)*x+std::cos(r.rotation)*y};
    };
    double nx,ny;gcm::to_world(v,note.pos,nx,ny);
    for(auto q:std::vector<std::pair<double,double>>{{0,0},{.2,0},{0,.2},{.2,.2}}) {
        auto p=place(nx+std::cos(note.angle)*q.first-std::sin(note.angle)*q.second,
                     ny+std::sin(note.angle)*q.first+std::cos(note.angle)*q.second,child,leaf);
        p=place(p.first,p.second,ref,parent);
        const double x=label.width_factor*(q.first+std::tan(label.oblique*3.141592653589793/180)*q.second);
        const double y=label.y_factor*q.second;
        CHECK(std::abs(label.x+std::cos(label.angle)*x-std::sin(label.angle)*y-p.first)<1e-9);
        CHECK(std::abs(label.y+std::sin(label.angle)*x+std::cos(label.angle)*y-p.second)<1e-9);
    }
    double lx=0,ly=0,hx=0,hy=0;canvas.labels_bounds(labels,lx,ly,hx,hy);
    double px,py;canvas.view().screen_of((lx+hx)/2,(ly+hy)/2,px,py);
    CHECK(canvas.pick_reference(px,py)==0);
    canvas.select_at(px,py,false); CHECK(canvas.selected_references().size()==1); CHECK(canvas.selected_texts().empty());
    CHECK(canvas.texts().empty()); CHECK(!canvas.explode_block(0)); CHECK(canvas.blocks().references().size()==1);
    ref.attrs.visible=false; *canvas.blocks().reference_mut(0)=ref;
    CHECK(canvas.reference_labels(ref).empty());canvas.select_all();CHECK(canvas.selected_references().empty());
}

TEST(clipped_reference_labels_are_not_pickable_in_holes_or_outside_windows) {
    Rig rig;auto& canvas=rig.c;const auto& v=canvas.volume();
    gdoc::BlockDef def;def.name="ClippedNote";def.is_xref=true;
    gdoc::BlockDef::Note note;note.text="MMMM";note.height=2;note.halign=0;note.valign=0;def.notes={note};
    canvas.blocks().define(def);gdoc::Xref x;x.name=def.name;x.state=gdoc::XrefState::Loaded;
    x.clip={gcm::to_lattice(v,1,.25),gcm::to_lattice(v,3,1.75)};x.clip_inverted=true;canvas.blocks().attach(x);
    gdoc::BlockRef ref;ref.name=def.name;canvas.blocks().insert(ref);
    const auto pick=[&](double wx,double wy) {
        double px,py;rig.screen(wx,wy,px,py);canvas.select_at(px,py,false);
        return std::pair{canvas.pick_reference(px,py),canvas.selected_references().size()};
    };
    CHECK(pick(2,1).first==Canvas::npos32);CHECK(pick(2,1).second==0);
    CHECK(pick(.4,1).first==0);CHECK(pick(.4,1).second==1);
    canvas.blocks().xref(x.name)->clip_inverted=false;
    CHECK(pick(2,1).first==0);CHECK(pick(2,1).second==1);
    CHECK(pick(.4,1).first==Canvas::npos32);CHECK(pick(.4,1).second==0);
    auto* clip=canvas.blocks().xref(x.name);clip->clip={gcm::to_lattice(v,20,20),gcm::to_lattice(v,21,21)};
    canvas.select_all();CHECK(canvas.selected_references().empty());
    CHECK(canvas.select_last()==0);
}

TEST(canvas_editing_negative_distance_keeps_its_original_direction) {
    Rig r;
    r.c.set_display_unit(gunit::Length::Metre);
    r.c.set_tool(Tool::Line);
    r.c.type_point("0,0");
    double x,y; r.screen(1,0,x,y); r.c.hover(x,y);
    r.c.dynamic_set(0,"-1");
    r.c.dynamic_set(0,"-10");
    CHECK(r.c.segment_count()==0);
    CHECK(r.c.dynamic_commit());
    CHECK(r.c.segment_count()==1);
    CHECK(std::fabs(wx_of(r.c,r.c.shapes()[0].b)+10)<1e-4);
}
