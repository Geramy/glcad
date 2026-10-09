#include "test.hpp"

#include <cmath>

#include "ui/canvas.hpp"
#include "ui/shell.hpp"
#include "geometry/extrude.hpp"

using namespace gcapp;

TEST(view_screen_y_grows_downward_world_y_grows_upward) {
    View v;
    v.set_viewport(1000, 800);

    double top_wx, top_wy, bottom_wx, bottom_wy;
    v.world_of(500, 100, top_wx, top_wy);
    v.world_of(500, 700, bottom_wx, bottom_wy);

    CHECK_MSG(top_wy > bottom_wy,
              "a point higher on screen must have a LARGER world y "
              "(top %.3f, bottom %.3f)", top_wy, bottom_wy);
}

TEST(view_world_and_screen_round_trip) {
    View v;
    v.set_viewport(1440, 900);
    v.pan(37.0, -21.0);
    v.zoom_at(700, 400, 1.7);

    for (double px = 0; px <= 1440; px += 137)
        for (double py = 0; py <= 900; py += 91) {
            double wx, wy, bx, by;
            v.world_of(px, py, wx, wy);
            v.screen_of(wx, wy, bx, by);
            CHECK(std::fabs(bx - px) < 1e-9);
            CHECK(std::fabs(by - py) < 1e-9);
        }
}

TEST(view_zoom_keeps_the_point_under_the_cursor_fixed) {
    View v;
    v.set_viewport(1440, 900);

    const double cx = 900, cy = 300;
    double before_x, before_y;
    v.world_of(cx, cy, before_x, before_y);

    for (double f : {1.1, 0.9091, 2.0, 0.5}) {
        v.zoom_at(cx, cy, f);
        double after_x, after_y;
        v.world_of(cx, cy, after_x, after_y);
        CHECK(std::fabs(after_x - before_x) < 1e-9);
        CHECK(std::fabs(after_y - before_y) < 1e-9);
    }
}

TEST(view_pan_moves_content_with_the_pointer) {
    View v;
    v.set_viewport(1000, 800);

    double wx0, wy0;
    v.world_of(500, 400, wx0, wy0);

    v.pan(100, 50);

    double wx1, wy1;
    v.world_of(500, 400, wx1, wy1);
    CHECK(wx1 < wx0);
    CHECK_MSG(wy1 > wy0, "dragging down must raise the world y at the centre");
}

TEST(view_zoom_is_clamped) {
    View v;
    v.set_viewport(1000, 800);
    for (int i = 0; i < 500; ++i) v.zoom_at(500, 400, 0.5);
    CHECK(v.scale() > 0.0);
    for (int i = 0; i < 500; ++i) v.zoom_at(500, 400, 2.0);
    CHECK(v.scale() < 1e9);
}

TEST(canvas_click_places_and_chains_segments) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);

    CHECK(c.segment_count() == 0);
    c.click(100, 100);
    CHECK(c.segment_count() == 0);
    c.click(300, 200);
    CHECK(c.segment_count() == 1);
    c.click(500, 150);
    CHECK(c.segment_count() == 2);

    c.undo();
    CHECK(c.segment_count() == 1);
    c.clear();
    CHECK(c.segment_count() == 0);
}

TEST(canvas_snaps_to_existing_endpoints) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);
    c.click(200, 200);
    c.click(400, 400);
    c.cancel();

    c.hover(402, 398);
    CHECK(c.snap().valid);
    CHECK(std::string(c.snap().kind) == "endpoint");
}

TEST(canvas_rect_emits_four_segments) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Rect);
    c.click(200, 200);
    c.click(500, 400);
    CHECK(c.segment_count() == 4);
    c.undo();
    CHECK(c.segment_count() == 0);
}


TEST(polar_locks_to_the_increment_and_reports_the_angle) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);
    c.polar().increment_deg = 45.0;

    c.click(500, 400);

    c.hover(700, 202);
    CHECK_MSG(c.track().active, "polar did not engage near 45 degrees");
    CHECK(std::fabs(c.track().angle_deg - 45.0) < 1e-9);
    CHECK(std::string(c.snap().kind) == "polar");
}

TEST(polar_releases_outside_the_tolerance) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);
    c.polar().increment_deg = 90.0;
    c.polar().tolerance_deg = 3.0;

    c.click(500, 400);
    c.hover(700, 300);
    CHECK(!c.track().active);
}

TEST(polar_covers_the_named_angles) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);
    c.polar().increment_deg = 45.0;
    c.click(500, 400);

    struct Case { double px, py, expect; };
    const Case cases[] = {
        {800, 400, 0.0},
        {500, 100, 90.0},
        {200, 400, 180.0},
        {500, 700, 270.0},
        {700, 200, 45.0},
        {300, 200, 135.0},
    };
    for (const auto& t : cases) {
        c.hover(t.px, t.py);
        CHECK_MSG(c.track().active, "no lock at expected %.0f deg", t.expect);
        CHECK_MSG(std::fabs(c.track().angle_deg - t.expect) < 1e-6,
                  "expected %.0f deg, got %.3f", t.expect, c.track().angle_deg);
    }
}

TEST(ortho_restricts_to_the_four_cardinals) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);
    c.toggle_ortho();
    CHECK(c.polar().ortho);

    c.click(500, 400);
    c.hover(700, 202);
    CHECK(!c.track().active);

    c.hover(800, 405);
    CHECK(c.track().active);
    CHECK(std::fabs(c.track().angle_deg) < 1e-9);
}

TEST(polar_only_engages_while_a_tool_is_armed) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);
    c.hover(700, 202);
    CHECK(!c.track().active);
}

TEST(polar_increment_cycles_through_standard_steps) {
    Canvas c;
    c.polar().increment_deg = 90.0;
    c.cycle_polar_increment();
    CHECK(std::fabs(c.polar().increment_deg - 45.0) < 1e-9);
    c.cycle_polar_increment();
    CHECK(std::fabs(c.polar().increment_deg - 30.0) < 1e-9);
}

TEST(object_snap_beats_polar) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);
    c.click(200, 600);
    c.click(400, 600);
    c.cancel();

    c.click(500, 400);
    c.hover(401, 599);
    CHECK(std::string(c.snap().kind) == "endpoint");
    CHECK(!c.track().active);
}

TEST(tools_circle_takes_centre_then_radius) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Circle);

    c.click(500, 400);
    CHECK_MSG(c.segment_count() == 0, "the first pick only sets the centre");
    c.click(600, 400);
    CHECK(c.segment_count() == 1);
    CHECK(c.shapes()[0].kind == gsnap::ShapeKind::Circle);
    CHECK(c.shapes()[0].radius > 0);
}

TEST(tools_arc_takes_three_picks) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Arc);

    c.click(500, 400);
    c.click(600, 400);
    CHECK(c.segment_count() == 0);
    c.click(500, 300);
    CHECK(c.segment_count() == 1);
    CHECK(c.shapes()[0].kind == gsnap::ShapeKind::Arc);
    CHECK(c.shapes()[0].radius > 0);
}

TEST(tools_polyline_accumulates_and_undoes_as_one) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Polyline);

    c.click(200, 200);
    c.click(300, 250);
    c.click(400, 200);
    c.click(500, 300);
    CHECK_MSG(c.segment_count() == 3, "expected 3 segments, got %zu",
              c.segment_count());

    c.undo();
    CHECK_MSG(c.segment_count() == 0, "undo must remove the whole polyline");
}

TEST(tools_line_undoes_one_segment_at_a_time) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Line);

    c.click(200, 200);
    c.click(300, 250);
    c.click(400, 200);
    CHECK(c.segment_count() == 2);

    c.undo();
    CHECK(c.segment_count() == 1);
    c.undo();
    CHECK(c.segment_count() == 0);
}

TEST(tools_escape_abandons_a_partial_construction) {
    Canvas c;
    c.view().set_viewport(1000, 800);

    c.set_tool(Tool::Arc);
    c.click(500, 400);
    c.click(600, 400);
    c.cancel();
    CHECK(c.segment_count() == 0);

    c.click(100, 100);
    c.click(200, 100);
    CHECK(c.segment_count() == 0);
    c.click(100, 200);
    CHECK(c.segment_count() == 1);
}

TEST(tools_curves_are_snappable_as_curves) {
    Canvas c;
    c.view().set_viewport(1000, 800);
    c.set_tool(Tool::Circle);
    c.click(500, 400);
    c.click(600, 400);
    c.cancel();

    c.snaps().settings.running = gsnap::Center;
    c.hover(502, 402);
    CHECK_MSG(c.snap().valid, "circle centre did not snap");
    CHECK(std::string(c.snap().kind) == "center");
}

TEST(tools_name_reported_in_the_status_line) {
    Canvas c;
    c.set_tool(Tool::Circle);
    CHECK(c.status().find("CIRCLE") != std::string::npos);
    c.set_tool(Tool::Polyline);
    CHECK(c.status().find("PLINE") != std::string::npos);
}

namespace {
Shell cursor_test_shell() {
    Shell s;s.set_dpr(1);s.set_viewport(1600,1000);
    s.canvas().set_display_unit(gunit::Length::Metre);
    s.camera().set_target(gview::Point3::from(2,2,0));s.fit_width(16);
    s.build_frame();return s;
}
gui::Vec2 cursor_at_plane(Shell& s,double u,double v) {
    double x,y,z,px,py;s.sketch_plane().to_world(u,v,x,y,z);
    CHECK(s.camera().project(gview::Point3::from(x,y,z),px,py));
    const gui::Vec2 p{float(px),float(py)};
    CHECK(s.canvas_bounds().contains(p));
    s.pointer_move(px,py);s.build_frame();s.build_frame();
    CHECK(!s.ui_wants_mouse());return p;
}
void stable_cursor(Shell& s,Shell::CursorKind expected) {
    for(int frame=0;frame<8;++frame) {
        CHECK_MSG(s.cursor()==expected,"cursor before frame %d: expected %d, got %d",frame,int(expected),int(s.cursor()));
        s.build_frame();
        CHECK_MSG(s.cursor()==expected,"cursor after frame %d: expected %d, got %d",frame,int(expected),int(s.cursor()));
    }
}
void cursor_rectangle(Shell& s) {
    s.canvas().set_tool(Tool::Line);
    for(const char* p:{"0,0","4,0","4,4","0,4","0,0"})s.canvas().type_point(p);
    s.canvas().cancel();s.canvas().set_tool(Tool::Select);s.build_frame();
}
}

TEST(cursor_line_selection_drawing_drag_and_empty_space_stay_stable) {
    auto s=cursor_test_shell();
    s.canvas().set_tool(Tool::Line);s.canvas().type_point("0,1");s.canvas().type_point("4,1");
    s.canvas().cancel();s.canvas().set_tool(Tool::Select);s.build_frame();
    cursor_at_plane(s,1.4,1);stable_cursor(s,Shell::CursorKind::Pick);
    s.canvas().set_selection({0});s.build_frame();
    const auto p=cursor_at_plane(s,1.4,1);stable_cursor(s,Shell::CursorKind::Move);
    s.pointer_down(p.x,p.y);s.pointer_move(p.x+20,p.y+15);s.build_frame();
    CHECK(s.canvas().selection_dragging());stable_cursor(s,Shell::CursorKind::Grab);
    s.pointer_up(p.x+20,p.y+15);s.build_frame();
    cursor_at_plane(s,-2,2);stable_cursor(s,Shell::CursorKind::Arrow);
    s.activate_tool(ToolId::Line);s.build_frame();stable_cursor(s,Shell::CursorKind::Cross);
}

TEST(cursor_dimension_selection_stays_stable) {
    auto s=cursor_test_shell();const auto& vol=s.canvas().volume();
    gdim::Dimension dim;dim.kind=gdim::Kind::Horizontal;
    dim.p1=gcm::to_lattice(vol,0,0);dim.p2=gcm::to_lattice(vol,4,0);dim.p3=gcm::to_lattice(vol,2,1);
    s.canvas().add_dimensions({dim});s.build_frame();
    cursor_at_plane(s,1,1);stable_cursor(s,Shell::CursorKind::Pick);
    s.canvas().set_selected_dimensions({0});s.build_frame();
    stable_cursor(s,Shell::CursorKind::Move);
}

TEST(cursor_closed_region_and_hatch_interior_stay_selectable) {
    auto s=cursor_test_shell();cursor_rectangle(s);
    cursor_at_plane(s,2,1);stable_cursor(s,Shell::CursorKind::Pick);
    CHECK(s.canvas().hatch_at_world(gcm::to_lattice(s.canvas().volume(),2,1),"ANSI31",0.5,45));
    s.build_frame();cursor_at_plane(s,2,1);stable_cursor(s,Shell::CursorKind::Pick);
    s.canvas().set_selected_hatches({0});s.build_frame();stable_cursor(s,Shell::CursorKind::Pick);
}

TEST(cursor_completed_frames_stay_selectable_during_coalesced_region_hover) {
    auto s=cursor_test_shell();cursor_rectangle(s);s.set_mode(Mode::Solid);s.build_frame();
    cursor_at_plane(s,1,2);stable_cursor(s,Shell::CursorKind::Pick);
    for(int frame=0;frame<12;++frame) {
        for(int event=0;event<5;++event) {
            double px,py;const double x=1+double(frame*5+event)/40;
            CHECK(s.camera().project(gview::Point3::from(x,2,0),px,py));
            s.pointer_move(px,py);
        }
        s.build_frame();CHECK(s.cursor()==Shell::CursorKind::Pick);
        s.build_frame();CHECK(s.cursor()==Shell::CursorKind::Pick);
    }
    cursor_at_plane(s,-2,2);stable_cursor(s,Shell::CursorKind::Arrow);
}

TEST(cursor_line_on_vertical_sketch_plane_uses_plane_coordinates) {
    auto s=cursor_test_shell();const auto& vol=s.canvas().volume();
    gtopo::Profile profile;
    for(auto [x,y]:{std::pair{10.0,10.0},std::pair{14.0,10.0},std::pair{14.0,14.0},std::pair{10.0,14.0}})
        profile.outer.push_back(gcm::to_lattice(vol,x,y));
    s.modeller().add_body(gtopo::extrude_z(profile,vol,3),"Cursor support");
    const auto& body=s.modeller().bodies().front();
    gcm::u32 side=body.face_count();
    for(gcm::u32 face=0;face<body.face_count();++face)
        if(std::abs(gtopo::plane_of_face(body,face,vol).nz)<0.1) {side=face;break;}
    CHECK(side<body.face_count());if(side>=body.face_count())return;
    CHECK(s.begin_sketch_on_face(0,side));s.fit_width(16);
    s.canvas().set_tool(Tool::Line);s.canvas().type_point("-2,0");s.canvas().type_point("2,0");
    s.canvas().cancel();s.canvas().set_tool(Tool::Select);s.build_frame();
    cursor_at_plane(s,-0.7,0);stable_cursor(s,Shell::CursorKind::Pick);
    s.canvas().set_selection({0});s.build_frame();stable_cursor(s,Shell::CursorKind::Move);
}
