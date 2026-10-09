#include "test.hpp"
#include "ui/shell.hpp"
#include "ui/mcp.hpp"
#include "ui/ai_tools.hpp"
#include "ui/snapshot.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {
using namespace gcapp;
Shell feature_shell() {
    Shell s;
    s.set_dpr(1);
    s.set_viewport(1200, 800);
    s.canvas().set_display_unit(gunit::Length::Metre);
    return s;
}
void segment(Canvas &canvas, double ax, double ay, double bx, double by) {
    gsnap::Shape line;
    line.a = gcm::to_lattice(canvas.volume(), ax, ay);
    line.b = gcm::to_lattice(canvas.volume(), bx, by);
    canvas.modify_shapes([&](auto &shapes) { shapes.push_back(line); });
}
void square(Canvas &c, double ax, double ay, double bx, double by, double gap = 0) {
    segment(c, ax + gap, ay, bx, ay);
    segment(c, bx, ay, bx, by);
    segment(c, bx, by, ax, by);
    segment(c, ax, by, ax, ay);
}
long double fill_area(const Canvas &c) {
    grender::d2::DrawList draw;
    c.build(draw);
    long double area = 0;
    for (const auto &t : draw.triangles())
        area += std::abs((t.xy[2] - t.xy[0]) * (t.xy[5] - t.xy[1]) -
                         (t.xy[4] - t.xy[0]) * (t.xy[3] - t.xy[1])) *
                .5;
    return area;
}
gai::Json tool(Shell &s, const char *name, gai::Json args) {
    gai::ToolCall call;
    call.function.name = name;
    call.function.arguments = args.dump();
    return gai::Json::parse(ai_tools::execute(s, call));
}
std::string feature_path(const char *suffix) {
    return (std::filesystem::temp_directory_path() /
            ("glcad-drafting-" + std::to_string(getpid()) + suffix))
        .string();
}
void check_dimension_translation(const gdim::Geometry& before,
                                 const gdim::Geometry& after,gcm::Lattice2 delta) {
    CHECK(before.ok&&after.ok);
    const auto point=[&](gcm::Lattice2 a,gcm::Lattice2 b) {
        CHECK(std::abs(b.x-a.x-delta.x)<=2);
        CHECK(std::abs(b.y-a.y-delta.y)<=2);
    };
    const auto strokes=[&](const auto& a,const auto& b) {
        CHECK(a.size()==b.size());
        for(std::size_t i=0;i<std::min(a.size(),b.size());++i) {
            point(a[i].a,b[i].a);point(a[i].b,b[i].b);
        }
    };
    strokes(before.lines,after.lines);strokes(before.arrows,after.arrows);
    point(before.text_at,after.text_at);
    CHECK(before.text==after.text);
    CHECK(before.text_angle==after.text_angle);
    CHECK(before.text_height==after.text_height);
}
}
TEST(drafting_text_glyph_cache_is_invalidated_between_tabs) {
    auto s=feature_shell();std::string error;CHECK(s.load_font(&error));
    const auto add=[&](const char* text) {
        auto& c=s.canvas();c.add_text(gcm::to_lattice(c.volume(),0,0),text,.25,0,1,2,0,"__cache_test__");
        s.build_frame();return s.ui_layers().text[1];
    };
    const auto short_count=add("A");CHECK(short_count>0);
    s.new_tab();const auto long_count=add("ABCDE");CHECK(long_count>short_count);
    s.switch_tab(0);s.build_frame();CHECK(s.ui_layers().text[1]==short_count);
    s.switch_tab(1);s.build_frame();CHECK(s.ui_layers().text[1]==long_count);
}
TEST(drafting_text_only_native_open_fits_visible_note_bounds) {
    auto s=feature_shell();s.set_viewport(1600,1000);
    std::string error;CHECK(s.load_font(&error));
    auto& c=s.canvas();c.add_text(gcm::to_lattice(c.volume(),0,0),"Door schedule",.35);
    c.set_selected_texts({0});
    CHECK(s.set_selection_property("BOLD","ON"));
    CHECK(s.set_selection_property("ITALIC","ON"));
    CHECK(s.set_selection_property("BACKGROUND","#FFFFFF"));
    CHECK(s.set_selection_property("HIGHLIGHT","#FFFF00"));
    const auto path=feature_path("-text-fit.glcad");CHECK(s.save_file(path));
    const auto cwd=std::filesystem::current_path();
    auto reopened=feature_shell();reopened.set_viewport(1600,1000);CHECK(reopened.load_font(&error));
    CHECK(reopened.open_file(path));std::filesystem::current_path(cwd);
    reopened.build_frame();
    const auto& loaded=static_cast<const Shell&>(reopened).canvas();CHECK(loaded.texts().size()==1);
    double lx=0,ly=0,hx=0,hy=0;
    CHECK(loaded.labels_bounds(loaded.note_labels(loaded.texts()[0]),lx,ly,hx,hy));
    const auto fit=reopened.canvas_fit_bounds();
    for(const auto corner:{std::pair{lx,ly},std::pair{lx,hy},std::pair{hx,ly},std::pair{hx,hy}}) {
        double x=0,y=0;
        CHECK(reopened.camera().project(gview::Point3::from(corner.first,corner.second,0),x,y));
        CHECK(fit.contains({float(x),float(y)}));
    }
    CHECK(!reopened.mesh().empty());
    double x=0,y=0;CHECK(reopened.camera().project(gview::Point3::from((lx+hx)*.5,hy,0),x,y));
    CHECK(y>fit.y && y<fit.bottom());
    std::filesystem::remove(path);
}
TEST(drafting_background_gap_islands_color_and_undo) {
    auto s = feature_shell();
    auto &c = s.canvas();
    square(c, 0, 0, 6, 4, .005);
    square(c, 2, 1, 3, 2);
    const auto before = c.shapes();
    const auto seed = gcm::to_lattice(c.volume(), 1, 1);
    CHECK(!c.background_at_world(seed, gdoc::Color::rgb(12, 80, 150), .001, 40));
    CHECK(c.hatches().empty());
    CHECK(c.background_at_world(seed, gdoc::Color::rgb(12, 80, 150), .01, 40));
    CHECK(c.shapes().size() == before.size());
    CHECK(c.shapes()[0].a.x == before[0].a.x);
    CHECK_MSG(std::abs(double(fill_area(c)) - 23) < 1e-5, "area %.9f, loops %zu",
              double(fill_area(c)), c.hatches()[0].cached.fill_loops.size());
    CHECK(c.hatches()[0].attrs.transparency == 60);
    CHECK(c.background_at_world(seed, gdoc::Color::rgb(180, 120, 50), .01, 80));
    CHECK(c.hatches().size() == 1);
    c.undo();
    CHECK(c.hatches()[0].attrs.color == gdoc::Color::rgb(12, 80, 150));
    c.redo();
    CHECK(c.hatches()[0].attrs.color == gdoc::Color::rgb(180, 120, 50));
    CHECK(c.remove_background_at(seed));
    CHECK(c.hatches().empty());
    c.undo();
    CHECK(c.hatches().size() == 1);
}
TEST(drafting_background_shared_wall_and_selected_boundary) {
    auto s = feature_shell();
    auto &c = s.canvas();
    square(c, 0, 0, 8, 4);
    segment(c, 4, 0, 4, 4);
    CHECK(c.background_at_world(gcm::to_lattice(c.volume(), 1, 1), gdoc::Color::rgb(30, 60, 90), 0,
                                35));
    CHECK(std::abs(double(fill_area(c)) - 16) < 1e-5);
    c.clear();
    square(c, 0, 0, 8, 4);
    segment(c, 4, 0, 4, 4);
    c.set_selection({0, 1, 2, 3});
    CHECK(c.background_at_world(gcm::to_lattice(c.volume(), 1, 1), gdoc::Color::rgb(30, 60, 90), 0,
                                35));
    CHECK(std::abs(double(fill_area(c)) - 32) < 1e-5);
    c.modify_shapes([](auto &shapes) { shapes.erase(shapes.begin()); });
    c.refresh_hatches();
    CHECK(c.hatches()[0].orphaned);
    CHECK(fill_area(c) == 0);
}
TEST(drafting_door_flow_and_room_background_survive_reopen_and_export) {
    auto s = feature_shell();
    auto &c = s.canvas();
    segment(c, 0, 0, 2, 0);
    segment(c, 3, 0, 5, 0);
    segment(c, 5, 0, 5, 4);
    segment(c, 5, 4, 0, 4);
    segment(c, 0, 4, 0, 0);
    auto made = tool(s, "draw_door", {{"hinge", "2,0"}, {"latch", "3,0"}, {"framing", "none"}});
    CHECK(!made.contains("error"));
    CHECK(c.shapes().size() == 7);
    c.select_all();
    CHECK(c.has_selected_door());
    s.command("DOORFLOW forward");
    CHECK(c.door_flow_shapes().size() == 3);
    const auto forward = c.door_flow_shapes()[0];
    CHECK(forward.b.y > forward.a.y);
    s.command("BACKGROUND 1,1 #123456 10mm 55");
    CHECK(c.hatches().size() == 1);
    CHECK(std::abs(double(fill_area(c)) - 20) < 1e-5);
    s.command("DOORFLOW reverse");
    CHECK(c.door_flow_shapes()[0].b.y < c.door_flow_shapes()[0].a.y);
    s.command("DOORFLOW both");
    CHECK(c.door_flow_shapes().size() == 5);
    s.build_frame();
    CHECK(s.mesh().size() >= 6);
    auto &m = static_cast<const Shell &>(s);
    bool unlit = false;
    for (const auto &vertex : m.mesh())
        if (vertex.normal[0] == 0 && vertex.normal[1] == 0 && vertex.normal[2] == 0)
            unlit = true;
    CHECK(unlit);
    const auto path = feature_path(".glcad");
    CHECK(s.save_file(path));
    auto reopened = feature_shell();
    CHECK(reopened.open_file(path));
    CHECK(reopened.canvas().hatches().size() == 1);
    CHECK(reopened.canvas().hatches()[0].attrs.color == gdoc::Color::rgb(18, 52, 86));
    CHECK(reopened.canvas().hatches()[0].attrs.transparency == 45);
    CHECK(reopened.canvas().door_flow_shapes().size() == 5);
    CHECK(std::abs(double(fill_area(reopened.canvas())) - 20) < 1e-5);
    const auto dxf = feature_path(".dxf");
    CHECK(reopened.export_file(dxf));
    auto imported = feature_shell();
    CHECK(imported.open_file(dxf));
    CHECK(imported.canvas().hatches().size() == 1);
    CHECK(imported.canvas().hatches()[0].attrs.color == gdoc::Color::rgb(18, 52, 86));
    CHECK(std::abs(imported.canvas().hatches()[0].attrs.transparency - 45) <= 1);
    gplot::PdfWriter pdf;
    CHECK(reopened.plot_sheet(pdf).ok);
    bool filled = false;
    for (const auto &p : pdf.paths_of_page(0))
        if (p.filled)
            filled = true;
    CHECK(filled);
    if (const auto *directory = std::getenv("GLCAD_UI_REVIEW_DIR")) {
        std::filesystem::create_directories(directory);
        std::string error;
        CHECK(reopened.load_font(&error));
        reopened.build_frame();
        reopened.camera().set_target(gview::Point3::from(2.5, 2, 0));
        reopened.fit_width(12);
        reopened.build_frame();
        auto png = snapshot_png(reopened, 1200, 800);
        std::filesystem::copy_file(path,
                                   std::filesystem::path(directory) / "area-and-door-flow.glcad",
                                   std::filesystem::copy_options::overwrite_existing);
        std::ofstream out(std::filesystem::path(directory) / "area-and-door-flow.png",
                          std::ios::binary);
        out.write(reinterpret_cast<const char *>(png.data()), png.size());
        CHECK(!png.empty());
    }
    std::filesystem::remove(path);
    std::filesystem::remove(dxf);
}
TEST(drafting_background_thick_walls_and_doorway) {
    auto s = feature_shell();
    auto &c = s.canvas();
    for (const auto &ends : {std::pair{"0,0", "5,0"}, std::pair{"5,0", "5,4"},
                             std::pair{"5,4", "0,4"}, std::pair{"0,4", "0,0"}})
        CHECK(
            !tool(s, "draw_wall", {{"from", ends.first}, {"to", ends.second}, {"thickness", "0.2"}})
                 .contains("error"));
    CHECK(!tool(s, "draw_door", {{"hinge", "2,0"}, {"latch", "3,0"}, {"wall_thickness", "0.2"}})
               .contains("error"));
    CHECK(!tool(s, "merge_overlaps", gai::Json::object()).contains("error"));
    c.select_all();
    CHECK(c.background_at_world(gcm::to_lattice(c.volume(), 1, 1), gdoc::Color::rgb(50, 120, 180),
                                .01, 40));
    CHECK_MSG(double(fill_area(c)) > 18 && double(fill_area(c)) < 19, "room area %.9f",
              double(fill_area(c)));
}
TEST(drafting_door_arrow_follows_move_and_edit_menu) {
    auto s = feature_shell();
    auto &c = s.canvas();
    CHECK(!tool(s, "draw_door", {{"hinge", "0,0"}, {"latch", "1,0"}, {"framing", "none"}})
               .contains("error"));
    c.select_all();
    CHECK(s.selection_summary().door);
    CHECK(c.set_door_flow("forward"));
    auto old = c.door_flow_shapes();
    CHECK(!tool(s, "move_selected", {{"dx", "2"}, {"dy", "3"}}).contains("error"));
    auto moved = c.door_flow_shapes();
    CHECK(moved.size() == 3);
    double ox, oy, nx, ny;
    gcm::to_world(c.volume(), old[0].a, ox, oy);
    gcm::to_world(c.volume(), moved[0].a, nx, ny);
    CHECK(std::abs(nx - ox - 2) < 1e-5);
    CHECK(std::abs(ny - oy - 3) < 1e-5);
    c.select_all();
    s.command("DOORFLOW");
    s.build_frame();
    bool edit = false;
    for (const auto &node : s.accessible_ui())
        if (node.label == "One way — toward swing")
            edit = true;
    CHECK_MSG(edit, "door %d, UI nodes %zu, log %s", c.has_selected_door(),
              s.accessible_ui().size(), s.log().empty() ? "" : s.log().back().c_str());
    gui::Rect button{};
    for (const auto &node : s.accessible_ui())
        if (node.label == "One way — away from swing")
            button = node.bounds;
    CHECK(button.w > 0);
    const double x = button.x + button.w * .5, y = button.y + button.h * .5;
    s.pointer_move(x, y);
    s.build_frame();
    s.pointer_down(x, y);
    s.build_frame();
    s.pointer_up(x, y);
    s.build_frame();
    CHECK(c.door_flow_shapes()[0].a.y > c.door_flow_shapes()[0].b.y);
}
TEST(drafting_background_shell_history_and_other_sketch_visibility) {
    auto s = feature_shell();
    square(s.canvas(), 0, 0, 4, 3);
    s.command("BACKGROUND 1,1 #74A9CF 10mm 40");
    s.command("BACKGROUND 1,1 #123456 10mm 60");
    s.command("UNDO");
    CHECK(s.canvas().hatches().size() == 1);
    CHECK(s.canvas().hatches()[0].attrs.color == gdoc::Color::rgb(116, 169, 207));
    CHECK(s.canvas().hatches()[0].attrs.transparency == 60);
    s.command("REDO");
    CHECK(s.canvas().hatches()[0].attrs.color == gdoc::Color::rgb(18, 52, 86));
    s.build_frame();
    const auto version = s.static_lines_version();
    const auto vertices = s.mesh().size();
    s.build_frame();
    CHECK(s.static_lines_version() == version);
    CHECK(s.mesh().size() == vertices);
    s.begin_sketch_on_ground();
    s.build_frame();
    CHECK(s.mesh().size() >= 6);
    s.edit_sketch(0);
    s.build_frame();
    CHECK(s.canvas().hatches().size() == 1);
    CHECK(std::abs(double(fill_area(s.canvas())) - 12) < 1e-5);
}
TEST(mcp_legacy_and_current_tools_validate_and_edit_real_document) {
    auto s = feature_shell();
    mcp::Server server(s);
    using gai::Json;
    auto request = [](int id, const char *method, Json params = Json::object()) {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    };
    CHECK(server.handle(request(1, "tools/list"))->contains("error"));
    CHECK(server.handle(request(2, "initialize", {{"protocolVersion", "2025-11-25"}}))
              ->at("result")["protocolVersion"] == "2025-11-25");
    CHECK(!server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
    auto listed = server.handle(request(3, "tools/list"));
    CHECK(listed->at("result")["tools"].size() > 20);
    auto bad = server.handle(request(
        4, "tools/call", {{"name", "draw_line"}, {"arguments", {{"from", 42}, {"to", "1,1"}}}}));
    CHECK(bad->contains("error"));
    CHECK(s.canvas().shapes().empty());
    auto drawn = server.handle(request(
        5, "tools/call", {{"name", "draw_line"}, {"arguments", {{"from", "0,0"}, {"to", "2,0"}}}}));
    CHECK(!drawn->at("result")["isError"].get<bool>());
    CHECK(s.canvas().shapes().size() == 1);
    CHECK(!server.handle(
        {{"jsonrpc", "2.0"}, {"method", "tools/call"}, {"params", {{"name", "erase_selected"}}}}));
    CHECK(s.canvas().shapes().size() == 1);
    const Json meta = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                       {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
    auto discover = server.handle(request(6, "server/discover", {{"_meta", meta}}));
    CHECK(discover->at("result")["resultType"] == "complete");
    auto missing =
        server.handle(request(7, "tools/call", {{"_meta", meta}, {"name", "document_summary"}}));
    CHECK(missing->contains("error"));
    auto summary = server.handle(request(
        8, "tools/call",
        {{"_meta", meta}, {"name", "document_summary"}, {"arguments", {{"document_id", "main"}}}}));
    CHECK(summary->at("result")["resultType"] == "complete");
    auto unknown = server.handle(request(9, "tools/call", {{"name", "does_not_exist"}}));
    CHECK(unknown->at("error")["code"] == -32602);
    auto undo = server.handle(
        request(10, "tools/call", {{"name", "run_command"}, {"arguments", {{"command", "UNDO"}}}}));
    CHECK(undo->contains("result"));
    CHECK(!undo->at("result")["structuredContent"]["recent_command_history"].empty());
    CHECK(s.canvas().shapes().empty());
}

TEST(drafting_selected_distance_and_delete_modifiers) {
    auto s=feature_shell();auto& c=s.canvas();
    segment(c,0,0,4,0);segment(c,0,2,4,2);
    c.set_selection({0,1});s.command("DIST");
    CHECK(c.dimensions().size()==1);
    double ax,ay,bx,by;
    gcm::to_world(c.volume(),c.dimensions()[0].p1,ax,ay);
    gcm::to_world(c.volume(),c.dimensions()[0].p2,bx,by);
    CHECK(std::abs(std::hypot(bx-ax,by-ay)-2)<1e-6);
    s.build_frame();c.set_selection({0,1});
    s.set_modifiers(false,false,false,true);s.key_special(2);
    CHECK(c.shapes().empty());
    s.command("UNDO");CHECK(c.shapes().size()==2);
    c.set_selection({0,1});s.set_modifiers(false,true,false,false);s.key_special(4);
    CHECK(c.shapes().empty());
}
TEST(drafting_wall_union_manual_detection_and_located_checks) {
    auto s=feature_shell();auto& c=s.canvas();const auto& v=c.volume();
    CHECK(c.add_wall(gcm::to_lattice(v,0,0),gcm::to_lattice(v,4,0),.2));
    CHECK(c.add_wall(gcm::to_lattice(v,4,0),gcm::to_lattice(v,4,3),.2));
    auto boundary=gregion::from_shapes(c.shapes());CHECK(boundary.ok);
    CHECK(std::abs(boundary.region.area(v)-1.4)<1e-5);
    for(const auto& finding:s.plan_check()) CHECK(finding.rule!="walls.crossing");
    c.clear();square(c,0,0,4,.2);
    CHECK(c.detect_wall_regions()==1);CHECK(c.is_region(0));
    CHECK(c.hatch_at_world(gcm::to_lattice(v,1,.1),"ANSI31"));
    CHECK(c.detect_wall_regions()==0);
    c.clear();segment(c,0,0,2,0);segment(c,2.005,0,2.005,2);
    c.modify_shapes([](auto& shapes){for(auto& sh:shapes)sh.layer="A-WALL";});
    bool located=false;for(const auto& finding:s.plan_check())
        if(finding.rule=="walls.join_gap") located=finding.located && finding.shape_keys.size()==2;
    CHECK(located);
}
TEST(drafting_background_tool_and_library_registry) {
    auto s=feature_shell();square(s.canvas(),0,0,6,4);
    auto result=tool(s,"area_background",{{"at","1,1"},{"color","#3388AA"},{"opacity","60"}});
    CHECK(!result.contains("error"));CHECK(s.canvas().hatches().size()==1);
    CHECK(s.canvas().hatches()[0].attrs.transparency==40);
    s.command("SKETCHLIBRARY");s.build_frame();
    CHECK(s.canvas().shapes().size()==4);
    CHECK(!tool(s,"draw_door",{{"hinge","1,0"},{"latch","2,0"},{"component","frame"}}).contains("error"));
    CHECK(s.canvas().shapes().back().attrs.user.at("sketch_object")=="frame");
}
TEST(drafting_command_click_multiselect_and_round) {
    auto s=feature_shell();auto& c=s.canvas();
    segment(c,0,0,4,0);segment(c,0,0,0,4);
    s.camera().set_target(gview::Point3::from(2,2,0));s.fit_width(12);s.build_frame();
    const auto click=[&](double x,double y) {
        double px,py;CHECK(s.camera().project(gview::Point3::from(x,y,0),px,py));
        s.pointer_move(px,py);s.build_frame();s.pointer_down(px,py);s.pointer_up(px,py);s.build_frame();
    };
    click(2,0);CHECK(c.selection().size()==1);
    s.set_modifiers(false,false,false,true);click(0,2);CHECK(c.selection().size()==2);
    s.set_modifiers(false,false,false,false);s.command("FILLET 0.2");
    CHECK(c.shapes().size()==3);
    CHECK(c.shapes().back().kind==gsnap::ShapeKind::Arc);
}
TEST(drafting_opening_snap_and_wall_extension_after_door) {
    auto s=feature_shell();auto& c=s.canvas();
    CHECK(!tool(s,"draw_wall",{{"from","0,0"},{"to","5,0"},{"thickness","0.2"}}).contains("error"));
    CHECK(!tool(s,"draw_door",{{"hinge","2,0.1"},{"latch","3,0.1"},{"snap_to_wall","true"},{"framing","none"}}).contains("error"));
    bool snapped=false;
    for(const auto& sh:c.shapes()) if(sh.kind==gsnap::ShapeKind::Arc) {
        double x,y;gcm::to_world(c.volume(),sh.a,x,y);snapped=std::abs(y-.1)<1e-6;
    }
    CHECK(snapped);
    CHECK(!tool(s,"draw_wall",{{"from","5,0"},{"to","5,3"},{"thickness","0.2"}}).contains("error"));
    CHECK(gregion::from_shapes([&]{std::vector<gsnap::Shape> walls;for(const auto& sh:c.shapes())if(sh.layer=="A-WALL")walls.push_back(sh);return walls;}()).ok);
}
TEST(drafting_dimension_drag_keeps_origins_and_moves_only_perpendicular) {
    for(auto kind:{gdim::Kind::Aligned,gdim::Kind::Horizontal,gdim::Kind::Vertical,gdim::Kind::Linear}) {
        auto s=feature_shell();auto& c=s.canvas();
        const double bx=kind==gdim::Kind::Vertical?0:4;
        const double by=kind==gdim::Kind::Aligned?3:kind==gdim::Kind::Vertical?4:0;
        const double length=std::hypot(bx,by),ux=bx/length,uy=by/length;
        const double nx=-uy,ny=ux;
        segment(c,0,0,-.5,-.5);segment(c,bx,by,bx+.5,by-.5);
        gdim::Dimension d;d.kind=kind;d.style=c.dim_styles().current();
        d.p1=gcm::to_lattice(c.volume(),0,0);d.p2=gcm::to_lattice(c.volume(),bx,by);
        d.p3=gcm::to_lattice(c.volume(),bx*.5+nx,by*.5+ny);
        d.o1={c.shapes()[0].key,gdim::Dimension::Anchor::Start};
        d.o2={c.shapes()[1].key,gdim::Dimension::Anchor::Start};
        c.add_dimensions({d});c.set_tool(Tool::Select);c.view().set_mapping(300,400,50);
        double x,y;c.view().screen_of(bx*.25+nx,by*.25+ny,x,y);
        CHECK(c.select_and_drag(x,y));c.update_selection_drag(x+50,y-100);CHECK(c.end_selection_drag());
        const auto moved=c.dimensions()[0];
        const auto style=*c.dim_styles().find(d.style);
        const auto original_geometry=gdim::build(d,style,c.volume());
        check_dimension_translation(original_geometry,gdim::build(moved,style,c.volume()),
                                    {moved.p3.x-d.p3.x,moved.p3.y-d.p3.y});
        CHECK(moved.p1.x==d.p1.x&&moved.p1.y==d.p1.y);
        CHECK(moved.p2.x==d.p2.x&&moved.p2.y==d.p2.y);
        CHECK(moved.o1.shape==d.o1.shape&&moved.o1.at==d.o1.at);
        CHECK(moved.o2.shape==d.o2.shape&&moved.o2.at==d.o2.at);
        const double mx=double(moved.p3.x-d.p3.x)*c.volume().inv_scale();
        const double my=double(moved.p3.y-d.p3.y)*c.volume().inv_scale();
        CHECK(std::abs(mx*ux+my*uy)<1e-6);
        CHECK(std::abs(mx*nx+my*ny-(nx+2*ny))<1e-6);
        CHECK(std::abs(gdim::measure(moved,c.volume())-gdim::measure(d,c.volume()))<1e-9);
        c.undo();CHECK(c.dimensions()[0].p3.x==d.p3.x&&c.dimensions()[0].p3.y==d.p3.y);
        CHECK(c.dimensions()[0].extension_offset.x==0&&c.dimensions()[0].extension_offset.y==0);
        c.redo();CHECK(c.dimensions()[0].p3.x==moved.p3.x&&c.dimensions()[0].p3.y==moved.p3.y);
        check_dimension_translation(original_geometry,gdim::build(c.dimensions()[0],style,c.volume()),
                                    {moved.p3.x-d.p3.x,moved.p3.y-d.p3.y});
        c.view().screen_of(bx*.25+nx+mx,by*.25+ny+my,x,y);
        CHECK(c.select_and_drag(x,y));
        c.update_selection_drag(x-250*nx,y+250*ny);CHECK(c.end_selection_drag());
        const auto crossed=c.dimensions()[0];
        CHECK(crossed.p1.x==d.p1.x&&crossed.p1.y==d.p1.y);
        CHECK(crossed.p2.x==d.p2.x&&crossed.p2.y==d.p2.y);
        check_dimension_translation(original_geometry,gdim::build(crossed,style,c.volume()),
                                    {crossed.p3.x-d.p3.x,crossed.p3.y-d.p3.y});
        c.view().screen_of(bx*.25+nx+mx-5*nx,by*.25+ny+my-5*ny,x,y);
        CHECK(c.select_and_drag(x,y));c.update_selection_drag(x+100*nx,y-100*ny);
        c.cancel_selection_drag();
        check_dimension_translation(gdim::build(crossed,style,c.volume()),
                                    gdim::build(c.dimensions()[0],style,c.volume()),{});
        c.modify_shapes([&](auto& shapes){shapes[1].a=gcm::to_lattice(c.volume(),bx+ux,by+uy);});
        c.refresh_dimensions();
        CHECK(std::abs(gdim::measure(c.dimensions()[0],c.volume())-(length+1))<1e-6);
        CHECK(c.dimensions()[0].p1.x==d.p1.x&&c.dimensions()[0].p1.y==d.p1.y);
        CHECK(!c.dimensions()[0].orphaned);
    }
}
TEST(drafting_selected_edge_dimension_stays_associative_after_drag_and_reload) {
    auto s=feature_shell();auto& c=s.canvas();
    segment(c,0,0,0,6);segment(c,4,0,4,6);
    c.select_all();CHECK(c.dimension_selection());
    const auto before=c.dimensions()[0];CHECK(before.associative());
    c.set_tool(Tool::Select);c.view().set_mapping(300,400,50);
    double x,y;c.view().screen_of(1,3,x,y);
    CHECK(c.select_and_drag(x,y));c.update_selection_drag(x+50,y-100);CHECK(c.end_selection_drag());
    const auto moved=c.dimensions()[0];
    CHECK(moved.p1.x==before.p1.x&&moved.p1.y==before.p1.y);
    CHECK(moved.p2.x==before.p2.x&&moved.p2.y==before.p2.y);
    CHECK(moved.p3.x==before.p3.x);CHECK(moved.p3.y>before.p3.y);
    c.modify_shapes([&](auto& shapes){shapes[1].a=gcm::to_lattice(c.volume(),6,0);shapes[1].b=gcm::to_lattice(c.volume(),6,6);});
    c.refresh_dimensions();
    CHECK(std::abs(gdim::measure(c.dimensions()[0],c.volume())-6)<1e-6);
    CHECK(!c.dimensions()[0].orphaned);
    const auto path=feature_path("-edge-dimension.glcad");CHECK(s.save_file(path));
    const auto cwd=std::filesystem::current_path();auto loaded=feature_shell();CHECK(loaded.open_file(path));std::filesystem::current_path(cwd);
    CHECK(loaded.canvas().dimensions().size()==1);
    if(!loaded.canvas().dimensions().empty()) {
        CHECK(loaded.canvas().dimensions()[0].extension_offset.x==moved.extension_offset.x);
        CHECK(loaded.canvas().dimensions()[0].extension_offset.y==moved.extension_offset.y);
        check_dimension_translation(gdim::build(c.dimensions()[0],*c.dim_styles().find(before.style),c.volume()),
            gdim::build(loaded.canvas().dimensions()[0],*loaded.canvas().dim_styles().find(before.style),loaded.canvas().volume()),{});
        CHECK(loaded.canvas().dimensions()[0].o1.at==gdim::Dimension::Anchor::Edge);
        CHECK(loaded.canvas().dimensions()[0].o2.at==gdim::Dimension::Anchor::Edge);
        CHECK(std::abs(gdim::measure(loaded.canvas().dimensions()[0],loaded.canvas().volume())-6)<1e-6);
    }
    std::filesystem::remove(path);
}
TEST(drafting_clicked_edge_dimension_keeps_references_and_adjusts_offset) {
    auto s=feature_shell();auto& c=s.canvas();
    segment(c,0,0,0,6);segment(c,4,0,4,6);
    c.view().set_mapping(300,400,50);c.set_tool(Tool::DimLinear);
    double x,y;c.view().screen_of(0,3,x,y);c.click(x,y);
    c.view().screen_of(4,3,x,y);c.click(x,y);
    CHECK(c.dimensions().size()==1);if(c.dimensions().empty())return;
    const auto d=c.dimensions()[0];
    CHECK(d.o1.at==gdim::Dimension::Anchor::Edge&&d.o2.at==gdim::Dimension::Anchor::Edge);
    c.view().screen_of(1,3,x,y);CHECK(c.select_and_drag(x,y));
    c.update_selection_drag(x+100,y-50);CHECK(c.end_selection_drag());
    CHECK(c.dimensions()[0].p1.x==d.p1.x&&c.dimensions()[0].p1.y==d.p1.y);
    CHECK(c.dimensions()[0].p2.x==d.p2.x&&c.dimensions()[0].p2.y==d.p2.y);
    CHECK(c.dimensions()[0].p3.x==d.p3.x&&c.dimensions()[0].p3.y>d.p3.y);
    if(const auto* directory=std::getenv("GLCAD_UI_REVIEW_DIR")) {
        std::filesystem::create_directories(directory);std::string error;CHECK(s.load_font(&error));
        if(auto* style=c.dim_styles().find_mut(c.dim_styles().current()))style->dimscale=75;
        s.build_frame();s.camera().set_target(gview::Point3::from(2,4,0));s.fit_width(16);s.build_frame();
        const auto png=snapshot_png(s,1200,800);CHECK(!png.empty());
        std::ofstream out(std::filesystem::path(directory)/"dimension-offset-drag.png",std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()),png.size());
    }
    c.modify_shapes([&](auto& shapes){shapes[1].a=gcm::to_lattice(c.volume(),5,0);shapes[1].b=gcm::to_lattice(c.volume(),5,6);});
    c.refresh_dimensions();CHECK(std::abs(gdim::measure(c.dimensions()[0],c.volume())-5)<1e-6);
}
TEST(drafting_closed_circle_supports_background_color) {
    auto s=feature_shell();auto& c=s.canvas();
    c.set_tool(Tool::Circle);CHECK(c.type_point("0,0"));CHECK(c.type_point("2,0"));c.cancel();
    CHECK(c.background_at_world(gcm::to_lattice(c.volume(),0,0),gdoc::Color::rgb(90,130,180),0,75));
    CHECK(std::abs(double(fill_area(c))-4*std::acos(-1.0))<.02);
}
TEST(mcp_background_and_located_wall_check) {
    auto s=feature_shell();square(s.canvas(),0,0,4,3);
    mcp::Server server(s);using gai::Json;
    auto request=[](int id,const char* method,Json params=Json::object()) {
        return Json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};
    };
    CHECK(server.handle(request(1,"initialize",{{"protocolVersion","2025-11-25"}}))->contains("result"));
    auto filled=server.handle(request(2,"tools/call",{{"name","area_background"},
        {"arguments",{{"at","1,1"},{"color","#224466"},{"opacity","65"}}}}));
    CHECK(!filled->at("result")["isError"].get<bool>());CHECK(s.canvas().hatches().size()==1);
    s.canvas().clear();segment(s.canvas(),0,0,2,0);segment(s.canvas(),2.005,0,2.005,2);
    s.canvas().modify_shapes([](auto& shapes){for(auto& sh:shapes)sh.layer="A-WALL";});
    auto checked=server.handle(request(3,"tools/call",{{"name","plan_check"}}));
    bool located=false;
    for(const auto& f:checked->at("result")["structuredContent"]["findings"])
        if(f["rule"]=="walls.join_gap") located=f.contains("location_m") && f["shape_keys"].size()==2;
    CHECK(located);
}
TEST(drafting_manual_wall_regions_join_and_support_edge_rounding) {
    auto s=feature_shell();auto& c=s.canvas();
    square(c,0,-.1,4,.1);square(c,3.9,0,4.1,3);
    c.modify_shapes([](auto& shapes){for(auto& sh:shapes)sh.layer="A-WALL";});
    CHECK(c.detect_wall_regions()==2);
    CHECK(gregion::from_shapes(c.shapes()).ok);
    CHECK(c.is_region(0));
    for(const auto& f:s.plan_check()) CHECK(f.rule!="walls.crossing");
    c.clear();square(c,0,0,4,.2);CHECK(c.detect_wall_regions()==1);
    c.view().set_mapping(300,400,500);
    for(std::size_t i:{0u,1u}) {
        const auto sh=c.shapes()[i];double ax,ay,bx,by,x,y;
        gcm::to_world(c.volume(),sh.a,ax,ay);gcm::to_world(c.volume(),sh.b,bx,by);
        c.view().screen_of((ax+bx)*.5,(ay+by)*.5,x,y);c.select_edge_at(x,y);
    }
    CHECK(c.selection().size()==2);s.command("FILLET 0.05");
    CHECK(gregion::validate_members(c.shapes()).empty());
    CHECK_MSG(std::any_of(c.shapes().begin(),c.shapes().end(),[](const auto& sh){return sh.kind==gsnap::ShapeKind::Arc;}),"%s",c.message().c_str());
}
TEST(drafting_right_click_background_applies_at_context_point) {
    auto s=feature_shell();auto& c=s.canvas();square(c,0,0,4,3);
    s.camera().set_target(gview::Point3::from(2,1.5,0));s.fit_width(12);s.build_frame();
    double x,y;CHECK(s.camera().project(gview::Point3::from(2,1.5,0),x,y));
    s.pointer_move(x,y);s.right_down(x,y);s.build_frame();
    gui::Rect action{};
    for(const auto& node:s.accessible_ui()) if(node.label=="Area background…") action=node.bounds;
    CHECK(action.w>0);
    if(action.w<=0) return;
    const auto action_point=action.centre();
    s.pointer_move(action_point.x,action_point.y);s.pointer_down(action_point.x,action_point.y);s.build_frame();s.pointer_up(action_point.x,action_point.y);s.build_frame();
    gui::Rect button{};
    for(const auto& node:s.accessible_ui()) if(node.label=="Pick area / update color") button=node.bounds;
    CHECK(button.w>0);
    if(button.w<=0) return;
    const auto p=button.centre();s.pointer_move(p.x,p.y);s.pointer_down(p.x,p.y);s.build_frame();s.pointer_up(p.x,p.y);s.build_frame();
    CHECK(c.hatches().size()==1);
    CHECK(std::abs(double(fill_area(c))-12)<1e-5);
}
TEST(drafting_repeated_wall_does_not_grow_or_duplicate_hatching) {
    auto s=feature_shell();auto& c=s.canvas();
    const gai::Json args={{"from","0,0"},{"to","4,0"},{"thickness","0.2"}};
    CHECK(!tool(s,"draw_wall",args).contains("error"));
    const auto count=c.shapes().size(),hatches=c.hatches().size();
    const auto before=gregion::from_shapes(c.shapes());CHECK(before.ok);
    CHECK(!tool(s,"draw_wall",args).contains("error"));
    CHECK(c.shapes().size()==count);CHECK(c.hatches().size()==hatches);
    const auto after=gregion::from_shapes(c.shapes());CHECK(after.ok);
    CHECK(std::abs(after.region.area(c.volume())-before.region.area(c.volume()))<1e-10);
}

TEST(drafting_discipline_is_independent_of_mode_and_persists_with_undo) {
    auto s = feature_shell();
    CHECK(s.drawing_discipline() == DrawingDiscipline::General);
    s.command("DISCIPLINE ARCHITECTURAL");
    CHECK(s.drawing_discipline() == DrawingDiscipline::Architectural);
    CHECK(s.mode() == Mode::Sketch);
    s.command("DISCIPLINE MECHANICAL");
    CHECK(s.drawing_discipline() == DrawingDiscipline::Mechanical);
    s.command("UNDO");
    CHECK(s.drawing_discipline() == DrawingDiscipline::Architectural);
    s.command("REDO");
    CHECK(s.drawing_discipline() == DrawingDiscipline::Mechanical);
    s.set_mode(Mode::Solid);
    s.command("DISCIPLINE ARCHITECTURAL");
    CHECK(s.mode() == Mode::Solid);
    CHECK(s.drawing_discipline() == DrawingDiscipline::Architectural);
    const auto path = (std::filesystem::temp_directory_path() / ("glcad-discipline-" + std::to_string(getpid()) + ".glcad")).string();
    CHECK(s.save_file(path));
    auto restored = feature_shell();
    CHECK(restored.open_file(path));
    CHECK(restored.drawing_discipline() == DrawingDiscipline::Architectural);
    CHECK(restored.project_type() == "architecture");
    restored.new_tab();
    CHECK(restored.drawing_discipline() == DrawingDiscipline::General);
    std::filesystem::remove(path);
}

TEST(drafting_mechanical_library_and_wall_inference_follow_discipline) {
    auto s = feature_shell();
    s.command("DISCIPLINE MECHANICAL");
    square(s.canvas(), 0, 0, 4, .2);
    s.sync_document();
    for (const auto& shape : s.canvas().shapes()) CHECK(!shape.region);
    s.command("SKETCHLIBRARY");
    s.build_frame();
    bool circle = false, wall = false, selector = false;
    for (const auto& node : s.accessible_ui()) {
        circle |= node.label == "Circle";
        wall |= node.label == "Wall";
        selector |= node.label.find("Drawing discipline") != std::string::npos;
    }
    CHECK(circle); CHECK(!wall); CHECK(selector);
    s.canvas().modify_shapes([](auto& shapes) { for (auto& shape : shapes) shape.layer = "A-WALL"; });
    s.sync_document();
    for (const auto& shape : s.canvas().shapes()) CHECK(shape.region);
}

TEST(drafting_assistant_discipline_context_and_semantic_tool_contract) {
    auto s=feature_shell();
    const auto mode=s.mode();
    CHECK(tool(s,"document_summary",gai::Json::object())["drawing_discipline"]=="general");
    auto purpose=tool(s,"set_drawing_discipline",{{"discipline","architectural"}});
    CHECK(!purpose.contains("error"));
    CHECK(s.drawing_discipline()==DrawingDiscipline::Architectural);
    CHECK(s.mode()==mode);
    CHECK(purpose["drafting_guidance"].get<std::string>().find("draw_wall")!=std::string::npos);
    CHECK(tool(s,"set_drawing_discipline",{{"discipline","invalid"}}).contains("error"));
    CHECK(s.drawing_discipline()==DrawingDiscipline::Architectural);
    CHECK(!tool(s,"draw_line",{{"from","0,0"},{"to","2,0"}}).contains("error"));
    CHECK(s.canvas().shapes().size()==1);
    CHECK(!s.canvas().shapes()[0].region);
    purpose=tool(s,"set_drawing_discipline",{{"discipline","mechanical"}});
    CHECK(s.drawing_discipline()==DrawingDiscipline::Mechanical);
    CHECK(purpose["drawing_discipline"]=="mechanical");
    CHECK(tool(s,"document_summary",gai::Json::object())["drawing_discipline"]=="mechanical");
    CHECK(!tool(s,"draw_rect",{{"corner_a","3,0"},{"corner_b","8,0.2"}}).contains("error"));
    s.build_frame();
    for(const auto& shape:s.canvas().shapes())CHECK(!shape.attrs.user.contains("sketch_object"));
    const auto definitions=ai_tools::registry();
    for(const auto* name:{"draw_line","draw_polyline","draw_rect"}) {
        const auto it=std::find_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.function.name==name;});
        CHECK(it!=definitions.end());if(it==definitions.end())continue;
        CHECK(it->function.description.find("mechanical")!=std::string::npos);
        CHECK(it->function.description.find("draw_wall")!=std::string::npos);
    }
}

TEST(mcp_discipline_context_and_perpendicular_wall_tools_share_joined_boundary) {
    auto s=feature_shell();mcp::Server server(s);using gai::Json;
    const auto request=[](int id,const char* method,Json params=Json::object()) {
        return Json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};
    };
    auto init=server.handle(request(1,"initialize",{{"protocolVersion","2025-11-25"}}));
    CHECK(init->at("result")["instructions"].get<std::string>().find("set_drawing_discipline")!=std::string::npos);
    auto set=server.handle(request(2,"tools/call",{{"name","set_drawing_discipline"},
        {"arguments",{{"discipline","architectural"}}}}));
    CHECK(!set->at("result")["isError"].get<bool>());
    auto info=server.handle(request(3,"tools/call",{{"name","document_info"}}));
    CHECK(info->at("result")["structuredContent"]["drawing_discipline"]=="architectural");
    CHECK(info->at("result")["structuredContent"]["drafting_guidance"].get<std::string>().find("draw_wall")!=std::string::npos);
    for(const auto* endpoint:{"0,5","5,0"}) {
        auto wall=server.handle(request(4,"tools/call",{{"name","draw_wall"},
            {"arguments",{{"from","0,0"},{"to",endpoint},{"thickness","0.2"}}}}));
        CHECK(!wall->at("result")["isError"].get<bool>());
    }
    CHECK(s.mode()==Mode::Sketch);CHECK(s.modeller().bodies().empty());
    const auto region=gregion::from_shapes(s.canvas().shapes());
    CHECK(region.ok);CHECK(region.region.loops.size()==1);
    CHECK(std::abs(region.region.area(s.canvas().volume())-2.0)<1e-5);
    const double outline[][4]={{-.1,-.1,5,-.1},{5,-.1,5,.1},{5,.1,.1,.1},
                              {.1,.1,.1,5},{.1,5,-.1,5},{-.1,5,-.1,-.1}};
    const auto on_outline=[&](double x,double y) {
        for(const auto& edge:outline) {
            const auto dx=edge[2]-edge[0],dy=edge[3]-edge[1];
            const auto t=std::clamp(((x-edge[0])*dx+(y-edge[1])*dy)/(dx*dx+dy*dy),0.0,1.0);
            if(std::hypot(x-edge[0]-t*dx,y-edge[1]-t*dy)<1e-6)return true;
        }
        return false;
    };
    for(const auto& shape:s.canvas().shapes()) {
        double ax,ay,bx,by;gcm::to_world(s.canvas().volume(),shape.a,ax,ay);gcm::to_world(s.canvas().volume(),shape.b,bx,by);
        CHECK(on_outline(ax,ay));CHECK(on_outline(bx,by));CHECK(on_outline((ax+bx)*.5,(ay+by)*.5));
    }
    for(const auto& shape:s.canvas().shapes()) {
        CHECK(shape.attrs.user.at("sketch_object")=="wall");
        CHECK(shape.region);CHECK(shape.poly==s.canvas().shapes()[0].poly);
    }
    for(const auto& finding:s.plan_check())CHECK(finding.rule!="walls.crossing");
    auto objects=server.handle(request(5,"tools/call",{{"name","list_objects"}}));
    for(const auto& object:objects->at("result")["structuredContent"]["objects"]) {
        CHECK(object["object_type"]=="wall");CHECK(object["region_boundary"]==true);
        CHECK(object.contains("compound_id"));
    }
    const Json meta={{"io.modelcontextprotocol/protocolVersion","2026-07-28"},
                     {"io.modelcontextprotocol/clientCapabilities",Json::object()}};
    auto discover=server.handle(request(6,"server/discover",{{"_meta",meta}}));
    CHECK(discover->at("result")["instructions"].get<std::string>().find("architectural")!=std::string::npos);
    CHECK(discover->at("result")["instructions"].get<std::string>().find("draw_wall")!=std::string::npos);
}

TEST(mcp_and_assistant_tool_activity_is_visible_in_command_history) {
    auto s=feature_shell();mcp::Server server(s);using gai::Json;
    const auto request=[](int id,const char* method,Json params=Json::object()) {
        return Json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};
    };
    const auto occurrences=[&](const std::string& text) {
        return std::count_if(s.log().begin(),s.log().end(),[&](const auto& line){return line.find(text)!=std::string::npos;});
    };
    server.handle(request(1,"initialize",{{"protocolVersion","2025-11-25"}}));
    auto drawn=server.handle(request(2,"tools/call",{{"name","draw_line"},
        {"arguments",{{"from","0,0"},{"to","2,0"},{"api_key","private-value"}}}}));
    CHECK(!drawn->at("result")["isError"].get<bool>());
    CHECK(occurrences("[MCP] draw_line {")==1);
    CHECK(occurrences("[MCP] draw_line: ok")==1);
    CHECK(occurrences("[Tool] draw_line")==0);
    CHECK(occurrences("private-value")==0);CHECK(occurrences("[redacted]")==1);
    auto failed=server.handle(request(3,"tools/call",{{"name","draw_line"},
        {"arguments",{{"from",42},{"to","1,1"}}}}));
    CHECK(failed->contains("error"));
    CHECK(occurrences("[MCP] draw_line: error: Arguments do not match")==1);
    server.handle(request(4,"tools/call",{{"name","document_info"}}));
    CHECK(occurrences("[MCP] document_info: ok")==1);
    auto command=server.handle(request(5,"tools/call",{{"name","run_command"},
        {"arguments",{{"command","ZOOM EXTENTS"}}}}));
    CHECK(!command->at("result")["isError"].get<bool>());
    CHECK(occurrences("[MCP] run_command {")==1);
    CHECK(occurrences("[MCP] run_command: ok")==1);
    gai::ToolCall circle;circle.function.name="draw_circle";
    circle.function.arguments=R"({"centre":"3,3","radius":"1","nested":{"password":"hidden-value"}})";
    s.ai_queue_tools({circle});s.build_frame();
    CHECK(occurrences("[Assistant] draw_circle {")==1);
    CHECK(occurrences("[Assistant] draw_circle: ok")==1);
    CHECK(occurrences("hidden-value")==0);
    CHECK(occurrences("[MCP] draw_line {")==2);
    gai::ToolCall bad;bad.function.name="draw_line";bad.function.arguments=R"({"from":"bad","to":"1,1"})";
    s.ai_queue_tools({bad});s.build_frame();
    CHECK(occurrences("[Assistant] draw_line: error:")==1);
    gai::ToolCall huge;huge.function.name="add_text";
    huge.function.arguments=Json{{"text",std::string(10000,'x')}}.dump();
    CHECK(ai_tools::activity_entry(huge,ai_tools::Origin::Assistant).size()<2000);
}

TEST(drafting_text_formatting_renders_persists_and_is_shared_with_tools) {
    auto s=feature_shell();std::string error;CHECK(s.load_font(&error));
    auto& c=s.canvas();c.add_text(gcm::to_lattice(c.volume(),1,1),"Wide Wi café",.3);
    c.set_selected_texts({0});
    CHECK(s.set_selection_property("COLOR","#123456"));
    CHECK(s.set_selection_property("BACKGROUND","#EEEEEE"));
    CHECK(s.set_selection_property("HIGHLIGHT","#FFFF00"));
    CHECK(s.set_selection_property("BOLD","ON"));
    CHECK(s.set_selection_property("ITALIC","ON"));
    CHECK(c.text_property(c.texts()[0],"BOLD")=="1");
    CHECK(c.text_property(c.texts()[0],"ITALIC")=="1");
    CHECK(c.text_property(c.texts()[0],"COLOR")=="#123456");
    auto labels=c.note_labels(c.texts()[0]);CHECK(labels.size()>=2);
    bool glyphs=false,backdrop=false,highlight=false;
    for(const auto& label:labels) {
        glyphs=glyphs || label.outline_font!=nullptr;backdrop=backdrop || label.box_width>0;
        highlight=highlight || label.highlight.a>0;
        CHECK(!Canvas::label_fills(label,c.volume()).empty());
    }
    CHECK(glyphs);CHECK(backdrop);CHECK(highlight);
    const auto cwd=std::filesystem::current_path();
    const auto path=feature_path("-text-format.glcad");CHECK(s.save_file(path));
    auto restored=feature_shell();CHECK(restored.open_file(path));std::filesystem::current_path(cwd);
    CHECK(restored.canvas().texts().size()==1);
    CHECK(restored.canvas().text_property(restored.canvas().texts()[0],"BACKGROUND")=="#EEEEEE");
    CHECK(restored.canvas().text_property(restored.canvas().texts()[0],"HIGHLIGHT")=="#FFFF00");
    CHECK(restored.canvas().text_property(restored.canvas().texts()[0],"COLOR")=="#123456");
    gai::ToolCall call;call.function.name="set_text_style";
    call.function.arguments=R"({"text_ids":[0],"property":"bold","value":"OFF"})";
    auto response=gai::Json::parse(ai_tools::execute(s,call,ai_tools::Origin::Assistant));
    CHECK(!response.contains("error"));CHECK(c.text_property(c.texts()[0],"BOLD")=="0");
    CHECK(s.set_selection_property("BOLD","OFF"));CHECK(s.set_selection_property("ITALIC","OFF"));
    labels=c.note_labels(c.texts()[0]);
    for(const auto& label:labels)if(!label.text.empty()) {CHECK(!label.bold);CHECK(std::abs(label.oblique)<1e-8);CHECK(label.text_advance>0);}
    gtext::FontRegistry installed;installed.scan_system();std::string alternate;
    for(const auto& face:installed.all()) {
        if(face.kind!=gtext::FontKind::TrueType && face.kind!=gtext::FontKind::OpenType)continue;
        gtext::Font candidate;if(candidate.load_file(face.path) && candidate.covers_ascii()){alternate=face.name;break;}
    }
    CHECK(!alternate.empty());
    if(!alternate.empty()) {
        CHECK(s.set_selection_property("FONT",alternate));
        CHECK(c.text_property(c.texts()[0],"FONT")==alternate);
        labels=c.note_labels(c.texts()[0]);bool owned=false;
        for(const auto& label:labels)if(!label.text.empty()){owned=owned || bool(label.owned_font);CHECK(label.outline_font!=nullptr);CHECK(!Canvas::label_fills(label,c.volume()).empty());}
        CHECK(owned);
    }
    gplot::PdfWriter pdf;CHECK(s.plot_sheet(pdf).ok);
    bool filled=false;for(const auto& item:pdf.paths_of_page(0))filled=filled||item.filled;CHECK(filled);
}

TEST(drafting_text_run_formatting_preserves_variations_and_reports_mixed) {
    auto s=feature_shell();auto& c=s.canvas();
    gtext::MTextContent content=gtext::parse_mtext("One");
    auto second=content.runs.front();second.text="Two";second.format.bold=true;second.format.italic=true;
    second.format.has_color=true;second.format.true_color=true;second.format.r=120;
    content.runs.push_back(second);second.text="Three";second.format.r=20;content.runs.push_back(second);
    second.text="Four";second.format.has_color=false;content.runs.push_back(second);
    const auto encoded=gtext::format_mtext(content);const auto decoded=gtext::parse_mtext(encoded);
    CHECK(decoded.runs.size()==4);CHECK(decoded.runs[1].format.bold);CHECK(decoded.runs[1].format.italic);
    CHECK(decoded.runs[1].format.r==120);CHECK(decoded.runs[2].format.r==20);
    CHECK(!decoded.runs[3].format.true_color && decoded.runs[3].format.aci==256);
    c.add_text({},encoded,.2);
    CHECK(c.text_property(c.texts()[0],"BOLD")=="Mixed");CHECK(c.text_property(c.texts()[0],"COLOR")=="Mixed");
}

TEST(drafting_automatic_wall_hatches_join_without_overriding_user_styles) {
    auto s=feature_shell();auto& c=s.canvas();
    const auto wall=[&](const char* from,const char* to,const char* type="exterior") {
        CHECK(!tool(s,"draw_wall",{{"from",from},{"to",to},{"type",type},{"thickness","0.2"}}).contains("error"));
    };
    wall("0,0","4,0");CHECK(c.hatches().size()==1);
    CHECK(c.hatches()[0].attrs.user.at("automatic_wall_core")=="1");
    CHECK(c.set_hatch_property({0},"PATTERN","ANSI32"));
    CHECK(c.set_hatch_property({0},"ANGLE","27"));
    CHECK(c.set_hatch_property({0},"COLOR","#123456"));
    wall("4,0","4,4","interior");
    CHECK(c.hatches().size()==1);CHECK(c.hatches()[0].style.name=="ANSI32");
    CHECK(c.hatches()[0].style.angle==27);
    wall("10,0","14,0","interior");
    CHECK(c.hatches().size()==2);CHECK(c.hatches()[1].style.angle==135);
    CHECK(c.hatch_at_world(gcm::to_lattice(c.volume(),2,0),"NET",1.7,17));
    CHECK(c.hatches().size()==3);CHECK(!c.hatches()[2].attrs.user.count("automatic_wall_core"));
    s.build_frame();wall("4,0","10,0");s.build_frame();
    CHECK(c.hatches().size()==2);
    CHECK(c.hatches()[0].style.name=="ANSI32");CHECK(c.hatches()[0].style.angle==27);
    CHECK(c.hatches()[0].attrs.color==gdoc::Color::rgb(0x12,0x34,0x56));
    CHECK(c.hatches()[1].style.name=="NET");CHECK(c.hatches()[1].style.scale==1.7);CHECK(c.hatches()[1].style.angle==17);
    s.command("UNDO");CHECK(c.hatches().size()==3);CHECK(c.hatches()[1].style.angle==135);
    s.command("REDO");CHECK(c.hatches().size()==2);CHECK(c.hatches()[0].style.angle==27);
    const auto path=feature_path("-automatic-wall-hatch.glcad");CHECK(s.save_file(path));
    const auto cwd=std::filesystem::current_path();auto reopened=feature_shell();CHECK(reopened.open_file(path));
    std::filesystem::current_path(cwd);
    CHECK(reopened.canvas().hatches().size()==2);
    CHECK(reopened.canvas().hatches()[0].attrs.user.at("automatic_wall_core")=="1");
    CHECK(!tool(reopened,"draw_wall",{{"from","4,4"},{"to","4,6"},{"type","interior"},{"thickness","0.2"}}).contains("error"));
    CHECK(reopened.canvas().hatches().size()==2);CHECK(reopened.canvas().hatches()[0].style.name=="ANSI32");
    CHECK(reopened.canvas().hatches()[0].style.angle==27);
    std::filesystem::remove(path);
}

TEST(drafting_automatic_wall_hatches_keep_nested_disconnected_cores_separate) {
    auto s=feature_shell();
    const auto wall=[&](const char* from,const char* to) {
        CHECK(!tool(s,"draw_wall",{{"from",from},{"to",to},{"thickness","0.2"}}).contains("error"));
    };
    wall("0,0","6,0");wall("6,0","6,6");wall("6,6","0,6");wall("0,6","0,0");
    CHECK(s.canvas().hatches().size()==1);
    wall("2,3","4,3");CHECK(s.canvas().hatches().size()==2);
    wall("6,0","8,0");CHECK(s.canvas().hatches().size()==2);
    const auto inside=gcm::to_lattice(s.canvas().volume(),3,3);
    const auto outer=gregion::from_shapes(s.canvas().hatches()[0].boundary_geometry);
    const auto inner=gregion::from_shapes(s.canvas().hatches()[1].boundary_geometry);
    CHECK(outer.ok);CHECK(inner.ok);
    if(outer.ok)CHECK(!outer.region.contains(inside));
    if(inner.ok)CHECK(inner.region.contains(inside));
}

TEST(drafting_hosted_doors_use_wall_face_and_reject_obstructed_sweeps_atomically) {
    for(const auto* swing:{"left","right"}) {
        auto s=feature_shell();auto& c=s.canvas();
        CHECK(!tool(s,"draw_wall",{{"from","0,0"},{"to","5,0"},{"thickness","0.2"}}).contains("error"));
        const auto placed=tool(s,"draw_door",{{"hinge","2,0"},{"latch","3,0"},{"snap_to_wall","true"},{"swing",swing},{"framing","none"}});
        CHECK(!placed.contains("error"));CHECK(placed["wall_faces_cut"].get<int>()==2);
        bool arc_seen=false;
        for(const auto& shape:c.shapes()) {
            if(shape.kind==gsnap::ShapeKind::Arc && shape.layer=="A-DOOR") {
                double x,y;gcm::to_world(c.volume(),shape.a,x,y);
                CHECK(std::abs(x-2)<1e-7);CHECK(std::abs(y-(std::string(swing)=="left"?.1:-.1))<1e-7);
                CHECK(std::abs(shape.radius*c.volume().inv_scale()-1)<1e-7);arc_seen=true;
            }
            if(shape.layer=="A-WALL" && shape.kind==gsnap::ShapeKind::Segment) {
                double ax,ay,bx,by;gcm::to_world(c.volume(),shape.a,ax,ay);gcm::to_world(c.volume(),shape.b,bx,by);
                if(std::abs(ay-by)<1e-7)CHECK(std::max(ax,bx)<=2+1e-7 || std::min(ax,bx)>=3-1e-7);
            }
        }
        CHECK(arc_seen);
    }
    auto s=feature_shell();auto& c=s.canvas();
    CHECK(!tool(s,"draw_wall",{{"from","0,0"},{"to","5,0"},{"thickness","0.2"}}).contains("error"));
    CHECK(!tool(s,"draw_wall",{{"from","2.8,0.6"},{"to","2.8,2"},{"thickness","0.2"}}).contains("error"));
    const auto count=c.shapes().size(),hatches=c.hatches().size();const auto serial=c.edit_serial();
    const auto blocked=tool(s,"draw_door",{{"hinge","2,0"},{"latch","3,0"},{"snap_to_wall","true"},{"framing","none"}});
    CHECK(blocked.contains("error"));CHECK(blocked.dump().find("intersects a wall")!=std::string::npos);
    CHECK(c.shapes().size()==count);CHECK(c.hatches().size()==hatches);CHECK(c.edit_serial()==serial);
    auto close_latch=feature_shell();
    CHECK(!tool(close_latch,"draw_wall",{{"from","-1,0"},{"to","2,0"},{"thickness","0.1"}}).contains("error"));
    CHECK(!tool(close_latch,"draw_wall",{{"from","0.82,0.06"},{"to","0.82,2"},{"thickness","0.1"}}).contains("error"));
    const auto near_count=close_latch.canvas().shapes().size();
    CHECK(tool(close_latch,"draw_door",{{"hinge","0,0"},{"latch","0.8,0"},{"snap_to_wall","true"},{"framing","none"}}).contains("error"));
    CHECK(close_latch.canvas().shapes().size()==near_count);
    auto empty=feature_shell();
    CHECK(tool(empty,"draw_door",{{"hinge","0,0"},{"latch","1,0"},{"snap_to_wall","true"},{"framing","none"}}).contains("error"));
    CHECK(empty.canvas().shapes().empty());
    const auto standalone=tool(empty,"draw_door",{{"hinge","0,0"},{"latch","1,0"},{"snap_to_wall","false"},{"framing","none"}});
    CHECK(!standalone.contains("error"));CHECK(standalone["wall_faces_cut"].get<int>()==0);
    CHECK(standalone["note"]=="No wall faces intersected this opening");
}

TEST(drafting_background_shared_graph_revalidates_geometry_visibility_sources_and_gap) {
    auto shell=feature_shell();auto& c=shell.canvas();
    square(c,0,0,4,4);square(c,6,0,10,4);
    c.modify_shapes([](auto& shapes) {for(auto& shape:shapes)shape.layer="Rooms";});
    gdoc::AssociativeHatch background;background.area_background=true;background.gap_tolerance=0;
    background.seed=gcm::to_lattice(c.volume(),1,1);
    CHECK(c.background_boundary(background).size()==4);
    background.seed=gcm::to_lattice(c.volume(),7,1);
    CHECK(c.background_boundary(background).size()==4);
    for(std::size_t i=0;i<4;++i)background.source_keys.push_back(c.shapes()[i].key);
    CHECK(c.background_boundary(background).empty());
    background.source_keys.clear();
    CHECK(c.background_boundary(background).size()==4);
    c.set_hidden_layers({"Rooms"});CHECK(c.background_boundary(background).empty());
    c.set_hidden_layers({});CHECK(c.background_boundary(background).size()==4);
    auto moved=c.shapes();const auto delta=gcm::to_lattice(c.volume(),20,0).x;
    for(auto& shape:moved) {shape.a.x+=delta;shape.b.x+=delta;}
    c.load_shapes(std::move(moved));CHECK(c.background_boundary(background).empty());
    background.seed=gcm::to_lattice(c.volume(),27,1);CHECK(c.background_boundary(background).size()==4);
    c.clear();square(c,0,0,4,4,.005);background.seed=gcm::to_lattice(c.volume(),1,1);
    background.gap_tolerance=.01;CHECK(!c.background_boundary(background).empty());
    background.gap_tolerance=0;CHECK(c.background_boundary(background).empty());
    background.gap_tolerance=.01;CHECK(!c.background_boundary(background).empty());
}

TEST(drafting_outline_glyph_cache_preserves_style_clipping_volume_and_reload) {
    auto shell=feature_shell();std::string error;CHECK(shell.load_font(&error));
    gtext::Font face;CHECK(face.load(*shell.font().source_bytes()));
    Canvas::Label label{0,0,1,0,"B",0,0};label.outline_font=&face;
    auto volume=shell.canvas().volume();
    const auto original=Canvas::label_fills(label,volume);CHECK(!original.empty());
    label.x=3;label.y=4;label.height=2;label.color={.25f,.5f,.75f,1};
    const auto moved=Canvas::label_fills(label,volume);CHECK(moved.size()==original.size());
    for(std::size_t i=0;i<std::min(moved.size(),original.size());++i) {
        CHECK(moved[i].color.r==.25f && moved[i].color.g==.5f && moved[i].color.b==.75f);
        for(int k=0;k<3;++k) {
            CHECK(std::abs(moved[i].xy[k*2]-(3+original[i].xy[k*2]*2))<1e-10);
            CHECK(std::abs(moved[i].xy[k*2+1]-(4+original[i].xy[k*2+1]*2))<1e-10);
        }
    }
    label.bold=true;CHECK(Canvas::label_fills(label,volume).size()==original.size()*2);
    label.bold=false;label.angle=.4;label.oblique=12;
    CHECK(Canvas::label_fills(label,volume).size()==original.size());
    label.clips={{{gcm::to_lattice(volume,100,100),gcm::to_lattice(volume,101,101)},false}};
    CHECK(Canvas::label_fills(label,volume).empty());label.clips.clear();
    volume.cx=.2;volume.cy=.3;volume.extent=1000;volume.resolution_bits=25;
    CHECK(!Canvas::label_fills(label,volume).empty());
    const auto requantized=Canvas::label_fills(label,shell.canvas().volume());
    CHECK(!requantized.empty());
    CHECK(face.load(*shell.font().source_bytes()));
    const auto reloaded=Canvas::label_fills(label,shell.canvas().volume());
    CHECK(reloaded.size()==requantized.size());
    for(std::size_t i=0;i<std::min(reloaded.size(),requantized.size());++i)
        for(int k=0;k<6;++k)CHECK(reloaded[i].xy[k]==requantized[i].xy[k]);
}

TEST(drafting_note_geometry_reuses_unchanged_labels_and_revalidates_edits) {
    auto shell=feature_shell();std::string error;CHECK(shell.load_font(&error));
    auto& c=shell.canvas();gtext::Font font;CHECK(font.load(*shell.font().source_bytes()));c.set_text_font(&font);
    c.add_text(gcm::to_lattice(c.volume(),0,0),"{\\floaded|b1|i0;BUILDING}",.3,0,0,0,0,"Standard","Notes");
    const auto draw=[&] {grender::d2::DrawList dynamic;c.build_dynamic_only(dynamic);};
    draw();const auto initial=c.static_draw().triangles();CHECK(!initial.empty());
    const auto original_revision=c.labels_revision();
    segment(c,3,3,4,3);draw();CHECK(c.labels_revision()==original_revision);
    CHECK(c.static_draw().triangles().size()==initial.size());
    for(std::size_t i=0;i<initial.size();++i)for(int k=0;k<6;++k)
        CHECK(c.static_draw().triangles()[i].xy[k]==initial[i].xy[k]);
    CHECK(c.set_text(0,"{\\floaded|b1|i0;B}"));draw();CHECK(c.labels_revision()!=original_revision);
    CHECK(c.static_draw().triangles().size()<initial.size());
    auto revision=c.labels_revision();
    c.set_layer_colors({{"Notes",{.2f,.4f,.8f,1}}});draw();CHECK(c.labels_revision()!=revision);
    CHECK(c.static_draw().triangles().front().color.r==.2f);
    c.set_hidden_layers({"Notes"});draw();CHECK(c.labels().empty());CHECK(c.static_draw().triangles().empty());
    c.set_hidden_layers({});draw();CHECK(!c.static_draw().triangles().empty());
    revision=c.labels_revision();CHECK(font.load(*shell.font().source_bytes()));draw();
    CHECK(c.labels_revision()!=revision);
    gtext::TextStyleTable styles;gtext::TextStyleDef style;style.name="Standard";style.width_factor=2;styles.add(style);
    c.set_text_styles(styles);revision=c.labels_revision();draw();CHECK(c.labels_revision()!=revision);
    const auto stretched=c.static_draw().triangles();CHECK(!stretched.empty());
    revision=c.labels_revision();segment(c,4,3,5,3);draw();CHECK(c.labels_revision()==revision);
    gdim::Dimension dimension;dimension.p1=gcm::to_lattice(c.volume(),0,0);dimension.p2=gcm::to_lattice(c.volume(),2,0);
    dimension.p3=gcm::to_lattice(c.volume(),1,1);c.add_dimensions({dimension});draw();CHECK(c.labels_revision()!=revision);
    revision=c.labels_revision();dimension.p2=gcm::to_lattice(c.volume(),3,0);c.load_dimensions({dimension});draw();
    CHECK(c.labels_revision()!=revision);
}

TEST(drafting_note_geometry_cache_revalidates_block_placement_and_clipping) {
    auto shell=feature_shell();std::string error;CHECK(shell.load_font(&error));auto& c=shell.canvas();
    gdoc::BlockDef block;block.name="Cached note";block.is_xref=true;
    gdoc::BlockDef::Note note;note.text="{\\floaded|b1|i0;BUILDING}";note.height=1;block.notes={note};
    c.blocks().define(block);gdoc::Xref xref;xref.name=block.name;xref.state=gdoc::XrefState::Loaded;c.blocks().attach(xref);
    gdoc::BlockRef ref;ref.name=block.name;c.blocks().insert(ref);
    const auto draw=[&] {grender::d2::DrawList dynamic;c.build_dynamic_only(dynamic);};
    draw();CHECK(!c.static_draw().triangles().empty());auto revision=c.labels_revision();
    segment(c,10,10,11,10);draw();CHECK(c.labels_revision()==revision);
    auto* placed=c.blocks().reference_mut(0);placed->insertion=gcm::to_lattice(c.volume(),4,5);placed->rotation=.3;
    draw();CHECK(c.labels_revision()!=revision);CHECK(!c.static_draw().triangles().empty());
    auto* clip=c.blocks().xref(block.name);clip->clip={gcm::to_lattice(c.volume(),100,100),gcm::to_lattice(c.volume(),101,101)};
    draw();CHECK(c.static_draw().triangles().empty());
    c.blocks().xref(block.name)->clip_enabled=false;draw();CHECK(!c.static_draw().triangles().empty());
    block.notes[0].text="{\\floaded|b1|i0;B}";c.blocks().define(block);revision=c.labels_revision();draw();
    CHECK(c.labels_revision()!=revision);
}

TEST(drafting_filled_note_mesh_stays_stable_for_line_edits_and_revalidates_tabs) {
    auto s=feature_shell();std::string error;CHECK(s.load_font(&error));
    auto& c=s.canvas();
    c.add_text(gcm::to_lattice(c.volume(),0,0),"{\\floaded|b1|i0;BUILDING}",.3,0,0,0,0,"Standard");
    s.build_frame();CHECK(!s.mesh().empty());const auto original=s.mesh();
    const auto revision=s.mesh_version();
    segment(c,3,3,4,3);s.build_frame();CHECK(s.mesh_version()==revision);
    CHECK(s.mesh().size()==original.size());
    s.new_tab();s.build_frame();CHECK(s.mesh().empty());
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(),5,6),"{\\floaded|b1|i0;BUILDING}",.3,0,0,0,0,"Standard");
    s.build_frame();CHECK(s.mesh().size()==original.size());
    CHECK(s.mesh().front().pos[0]!=original.front().pos[0]);
    const auto second=s.mesh();
    s.switch_tab(0);s.build_frame();CHECK(s.mesh().size()==original.size());
    for(std::size_t i=0;i<original.size();++i)for(int k=0;k<3;++k)
        CHECK(s.mesh()[i].pos[k]==original[i].pos[k]);
    CHECK(s.canvas().set_text(0,"{\\floaded|b1|i0;B}"));s.build_frame();
    CHECK(s.mesh().size()<original.size());
    s.switch_tab(1);s.build_frame();CHECK(s.mesh().size()==second.size());
    for(std::size_t i=0;i<second.size();++i)for(int k=0;k<3;++k)
        CHECK(s.mesh()[i].pos[k]==second[i].pos[k]);
    s.themes().set_follow_system(false);CHECK(s.themes().activate("GLCAD Light"));s.build_frame();
    const auto light=s.mesh().front();const auto light_version=s.mesh_version();
    CHECK(s.themes().activate("GLCAD Dark"));s.build_frame();CHECK(s.mesh_version()!=light_version);
    const auto dark=s.mesh().front();
    CHECK(light.color[0]!=dark.color[0] || light.color[1]!=dark.color[1] || light.color[2]!=dark.color[2]);
    const auto notes=s.canvas().texts();
    s.command("DATUM XZ");CHECK(s.active_sketch()==1);s.set_sketch_visibility(0,Shell::SketchVisibility::Hidden);
    s.canvas().load_texts(notes);s.build_frame();CHECK(s.mesh().size()==second.size());
    bool elevated=false;
    for(const auto& vertex:s.mesh()) {CHECK(std::abs(vertex.pos[1])<1e-6);elevated|=std::abs(vertex.pos[2])>1;}
    CHECK(elevated);
}

TEST(drafting_wall_hatch_style_survives_door_at_seed_and_boundary_repair) {
    auto s=feature_shell();auto& c=s.canvas();
    CHECK(!tool(s,"draw_wall",{{"from","0,0"},{"to","6,0"},{"thickness","0.2"}}).contains("error"));
    CHECK(c.set_hatch_property({0},"PATTERN","NET"));
    CHECK(c.set_hatch_property({0},"SCALE","0.5"));
    CHECK(c.set_hatch_property({0},"ANGLE","27"));
    CHECK(c.set_hatch_property({0},"COLOR","#123456"));
    s.build_frame();
    const auto style_check=[&] {
        CHECK(c.hatches().size()==2);
        for(const auto& h:c.hatches()) {
            CHECK(h.valid);CHECK(!h.orphaned);CHECK(!h.cached.lines.empty());
            CHECK(h.style.name=="NET");CHECK(h.style.angle==27);CHECK(h.style.scale==.5);
            CHECK(h.attrs.color==gdoc::Color::rgb(0x12,0x34,0x56));
        }
    };
    const auto result=tool(s,"draw_door",{{"hinge","2.5,0"},{"latch","3.5,0"},{"framing","none"}});
    CHECK_MSG(!result.contains("error"),"%s",result.dump().c_str());
    s.build_frame();style_check();
    s.command("UNDO");CHECK(c.hatches().size()==1);CHECK(c.hatches()[0].valid);
    s.command("REDO");s.build_frame();style_check();
    gsnap::Shape removed;
    c.modify_shapes([&](auto& shapes) {
        const auto edge=std::find_if(shapes.begin(),shapes.end(),[](const auto& shape){return shape.layer=="A-WALL";});
        CHECK(edge!=shapes.end());if(edge!=shapes.end()){removed=*edge;shapes.erase(edge);}
    });
    s.build_frame();
    CHECK(std::any_of(c.hatches().begin(),c.hatches().end(),[](const auto& h){return h.orphaned;}));
    const auto path=feature_path("-wall-style-repair.glcad");CHECK(s.save_file(path));
    const auto cwd=std::filesystem::current_path();auto reopened=feature_shell();CHECK(reopened.open_file(path));
    std::filesystem::current_path(cwd);
    removed.key=0;removed.poly=0;removed.poly_index=0;removed.region=false;
    removed.region_loop=0;removed.region_reversed=false;removed.attrs={};removed.layer="0";
    c.modify_shapes([&](auto& shapes){shapes.push_back(removed);});s.build_frame();style_check();
    reopened.canvas().modify_shapes([&](auto& shapes){shapes.push_back(removed);});reopened.build_frame();
    CHECK(reopened.canvas().hatches().size()==2);
    for(const auto& h:reopened.canvas().hatches()) {
        CHECK(h.valid);CHECK(h.style.name=="NET");CHECK(h.style.angle==27);
        CHECK(h.attrs.color==gdoc::Color::rgb(0x12,0x34,0x56));
    }
    std::filesystem::remove(path);
}

TEST(drafting_legacy_wall_hatch_keeps_explicit_style_across_door_cut) {
    auto s=feature_shell();auto& c=s.canvas();
    CHECK(!tool(s,"draw_wall",{{"from","0,0"},{"to","6,0"},{"thickness","0.2"}}).contains("error"));
    auto legacy=c.hatches();legacy[0].attrs.user.erase("automatic_wall_core");
    legacy[0].boundary_geometry.clear();legacy[0].style.name="NET";
    legacy[0].style.angle=17;legacy[0].attrs.color=gdoc::Color::rgb(10,30,50);
    legacy[0].valid=false;c.load_hatches(std::move(legacy));c.refresh_hatches();
    CHECK(!tool(s,"draw_door",{{"hinge","2.5,0"},{"latch","3.5,0"},{"framing","none"}}).contains("error"));
    s.build_frame();CHECK(c.hatches().size()==2);
    for(const auto& h:c.hatches()) {
        CHECK(h.valid);CHECK(!h.orphaned);CHECK(h.style.name=="NET");CHECK(h.style.angle==17);
        CHECK(h.attrs.color==gdoc::Color::rgb(10,30,50));
        CHECK(!h.attrs.user.count("automatic_wall_core"));
    }
}

TEST(drafting_context_selection_preserves_dimension_and_hatch_groups) {
    auto s=feature_shell();auto& c=s.canvas();
    for(int i=0;i<3;++i) {
        gdim::Dimension d;d.kind=gdim::Kind::Horizontal;
        d.p1=gcm::to_lattice(c.volume(),0,i*3);d.p2=gcm::to_lattice(c.volume(),4,i*3);
        d.p3=gcm::to_lattice(c.volume(),2,i*3+1);c.add_dimensions({d});
    }
    c.view().set_mapping(300,500,50);c.set_selected_dimensions({0,1});
    double x,y;c.view().screen_of(1,1,x,y);
    CHECK(c.select_context_at(x,y));CHECK(c.selected_dimensions()==std::vector<std::size_t>({0,1}));
    c.view().screen_of(1,7,x,y);
    CHECK(c.select_context_at(x,y));CHECK(c.selected_dimensions()==std::vector<std::size_t>({2}));
    c.clear();square(c,0,0,2,2);square(c,4,0,6,2);
    CHECK(c.background_at_world(gcm::to_lattice(c.volume(),1,1),gdoc::Color::rgb(90,130,180),0,75));
    CHECK(c.background_at_world(gcm::to_lattice(c.volume(),5,1),gdoc::Color::rgb(90,130,180),0,75));
    c.set_selected_hatches({0,1});c.view().screen_of(1,1,x,y);
    CHECK(c.select_context_at(x,y));CHECK(c.selected_hatches()==std::vector<std::size_t>({0,1}));
}

TEST(background_local_refresh_expands_into_adjacent_room_and_preserves_islands) {
    auto s=feature_shell();auto& c=s.canvas();
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    square(c,0,0,8,4);segment(c,4,0,4,4);const auto divider=c.shapes().back();
    square(c,6,1,7,2);square(c,20,0,24,4);
    CHECK(c.background_at_world(p(1,1),gdoc::Color::rgb(90,130,180),0,75));
    CHECK(c.background_at_world(p(21,1),gdoc::Color::rgb(90,130,180),0,75));
    const auto remote=c.hatches().back().boundary_hash;
    c.modify_shapes([&](auto& shapes){std::erase_if(shapes,[&](const auto& shape){return shape.key==divider.key;});});
    const auto work=c.drafting_work();c.refresh_hatches({drafting_bounds(divider)});
    CHECK(c.drafting_work().hatch_updates-work.hatch_updates==1);
    CHECK(c.hatches().back().boundary_hash==remote);CHECK(c.hatches().front().valid);
    CHECK(gdoc::point_in_region(p(7.5,2),c.hatches().front().cached.fill_loops));
    CHECK(!gdoc::point_in_region(p(6.5,1.5),c.hatches().front().cached.fill_loops));
    c.modify_shapes([&](auto& shapes){shapes.push_back(divider);});c.refresh_hatches({drafting_bounds(divider)});
    CHECK(gdoc::point_in_region(p(1,1),c.hatches().front().cached.fill_loops));
    CHECK(!gdoc::point_in_region(p(7.5,2),c.hatches().front().cached.fill_loops));
}

TEST(door_drag_only_retraces_the_room_it_enters_and_skips_duplicate_positions) {
    auto s=feature_shell();auto& c=s.canvas();
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    square(c,0,0,4,4);square(c,20,0,24,4);
    CHECK(c.background_at_world(p(2,2),gdoc::Color::rgb(90,130,180),0,75));
    CHECK(c.background_at_world(p(22,2),gdoc::Color::rgb(90,130,180),0,75));
    CHECK(!tool(s,"draw_door",{{"hinge","6,1"},{"latch","7,1"},{"framing","none"},{"snap_to_wall",false}}).contains("error"));
    std::vector<std::size_t> selected;
    for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].attrs.user.contains("door"))selected.push_back(i);
    c.set_selection(selected);c.set_tool(Tool::Select);c.view().set_mapping(200,500,80);
    double x,y;c.view().screen_of(6,1.5,x,y);CHECK(c.begin_selection_drag(x,y));
    auto work=c.drafting_work();c.update_selection_drag(x+80,y);
    CHECK(c.drafting_work().hatch_updates==work.hatch_updates);
    const auto serial=c.edit_serial();c.update_selection_drag(x+80,y);CHECK(c.edit_serial()==serial);
    work=c.drafting_work();c.update_selection_drag(x-320,y);
    CHECK(c.drafting_work().hatch_updates-work.hatch_updates==1);
    CHECK(c.drafting_work().background_segments-work.background_segments<20);
    for(const auto& h:c.hatches())CHECK(h.valid&&!h.orphaned);
    c.cancel_selection_drag();for(const auto& h:c.hatches())CHECK(h.valid&&!h.orphaned);
}

TEST(door_drag_keeps_distant_openings_in_a_connected_building) {
    auto s=feature_shell();auto& c=s.canvas();
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(p(0,0),p(28,0),.2));
    CHECK(c.add_wall(p(0,4),p(28,4),.2));
    for(int i=0;i<=7;++i)CHECK(c.add_wall(p(i*4,0),p(i*4,4),.2));
    for(int i=0;i<7;++i)CHECK(!tool(s,"draw_door",{{"hinge",std::to_string(1+i*4)+",0"},
        {"latch",std::to_string(2+i*4)+",0"},{"framing","none"}}).contains("error"));
    std::vector<gsnap::Shape> doors;
    for(const auto& shape:c.shapes())if(shape.kind==gsnap::ShapeKind::Arc&&shape.attrs.user.contains("opening_auto"))doors.push_back(shape);
    CHECK(doors.size()==7);if(doors.size()!=7)return;
    std::vector<std::size_t> selected;
    for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].poly==doors.front().poly)selected.push_back(i);
    c.set_selection(selected);c.set_tool(Tool::Select);c.view().set_mapping(200,500,80);
    double x,y;c.view().screen_of(1,.6,x,y);CHECK(c.begin_selection_drag(x,y));
    const auto work=c.drafting_work();
    for(int frame=0;frame<8;++frame)c.update_selection_drag(x+40+frame*3,y);
    CHECK(c.drafting_work().opening_doors-work.opening_doors==8);
    CHECK(c.drafting_work().opening_edges-work.opening_edges>8*20);
    std::vector<gsnap::Shape> walls;
    for(const auto& shape:c.shapes())if(shape.layer=="A-WALL")walls.push_back(shape);
    const auto material=gregion::from_shapes(walls);CHECK_MSG(material.ok,"%s",material.error.c_str());
    if(material.ok) {
        CHECK(material.region.contains(p(1.25,0)));
        CHECK(!material.region.contains(p(2.25,0)));
        for(int i=1;i<7;++i)CHECK(!material.region.contains(p(1.5+i*4,0)));
    }
    for(std::size_t i=1;i<doors.size();++i) {
        const auto at=c.index_of_key(doors[i].key);CHECK(at!=Canvas::npos);if(at==Canvas::npos)continue;
        const auto& after=c.shapes()[at];
        CHECK(after.a.x==doors[i].a.x&&after.a.y==doors[i].a.y);
        CHECK(after.attrs.user.at("opening_material")==doors[i].attrs.user.at("opening_material"));
    }
    c.cancel_selection_drag();walls.clear();
    for(const auto& shape:c.shapes())if(shape.layer=="A-WALL")walls.push_back(shape);
    const auto restored=gregion::from_shapes(walls);CHECK(restored.ok);
    if(restored.ok) {CHECK(!restored.region.contains(p(1.25,0)));CHECK(restored.region.contains(p(2.25,0)));}
}

TEST(background_orphan_retries_only_when_its_area_changes) {
    auto s=feature_shell();auto& c=s.canvas();
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    square(c,20,0,24,4);const auto edge=c.shapes().back();
    CHECK(c.background_at_world(p(22,2),gdoc::Color::rgb(90,130,180),0,75));
    c.modify_shapes([&](auto& shapes){std::erase_if(shapes,[&](const auto& shape){return shape.key==edge.key;});});
    c.refresh_hatches();CHECK(c.hatches().front().orphaned);
    gsnap::Shape remote;remote.a=p(0,0);remote.b=p(1,0);
    const auto work=c.drafting_work();c.refresh_hatches({drafting_bounds(remote)});
    CHECK(c.drafting_work().background_traces==work.background_traces);
    CHECK(c.drafting_work().hatch_updates==work.hatch_updates);
    c.modify_shapes([&](auto& shapes){shapes.push_back(edge);});
    c.refresh_hatches({drafting_bounds(edge)});
    CHECK(c.hatches().front().valid&&!c.hatches().front().orphaned);
    CHECK(gdoc::point_in_region(p(22,2),c.hatches().front().cached.fill_loops));
}

TEST(object_library_folders_preview_rows_drag_drop_and_cancel) {
    auto s=feature_shell();s.set_viewport(1200,900);auto& c=s.canvas();
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(point(0,0),point(8,0),.2));
    s.camera().set_target(gview::Point3::from(4,0,0));s.fit_width(12);s.build_frame();
    const auto row=[&](const char* name) {gui::Rect result{};for(const auto& item:s.accessible_ui())if(item.label==name)result=item.bounds;return result;};
    const auto click=[&](const char* name){const auto r=row(name);CHECK(r.w>0);const auto p=r.centre();s.pointer_move(p.x,p.y);s.build_frame();s.pointer_down(p.x,p.y);s.build_frame();s.pointer_up(p.x,p.y);s.build_frame();s.build_frame();};
    s.command("SKETCHLIBRARY");s.build_frame();CHECK(row("Architecture").w>0);CHECK(row("Swing door").w>0);
    click("Doors and windows");CHECK(row("Swing door").w==0);click("Doors and windows");CHECK(row("Swing door").w>0);
    const auto drag=[&](const char* label,double x,double y) {
        const auto from=row(label).centre();CHECK(row(label).w>0);
        s.pointer_move(from.x,from.y);s.build_frame();s.pointer_down(from.x,from.y);s.build_frame();
        s.pointer_move(x,y);s.build_frame();s.pointer_up(x,y);s.build_frame();
    };
    double x,y;CHECK(s.camera().project(gview::Point3::from(4,0,0),x,y));
    drag("Swing door",x,y);
    std::vector<gsnap::Shape> walls;int doors=0;
    for(const auto& shape:c.shapes()) {if(shape.layer=="A-WALL")walls.push_back(shape);if(shape.kind==gsnap::ShapeKind::Arc && shape.layer=="A-DOOR")++doors;}
    CHECK_MSG(doors==1,"%s",s.log().empty()?"":s.log().back().c_str());
    auto material=gregion::from_shapes(walls);CHECK(material.ok);if(material.ok)CHECK(!material.region.contains(point(4.45,0)));
    const auto size=c.shapes().size();drag("Swing door",-20,-20);CHECK(c.shapes().size()==size);
    const auto from=row("Swing door").centre();
    s.pointer_move(from.x,from.y);s.build_frame();s.pointer_down(from.x,from.y);s.build_frame();
    s.pointer_move(x,y);s.build_frame();s.key_special(1);s.build_frame();s.pointer_up(x,y);s.build_frame();
    CHECK(c.shapes().size()==size);CHECK(c.tool()==Tool::Select);
    s.command("SKETCHLIBRARY");s.build_frame();
    s.command("UNDO");s.build_frame();CHECK(c.shapes().size()==4);
    s.command("REDO");s.build_frame();CHECK(c.shapes().size()==size);
    click("Architecture");CHECK(row("Swing door").w==0);CHECK(row("Symbols").w>0);
    click("Symbols");CHECK(row("North arrow").w>0);
    const auto refs=c.blocks().references().size();
    drag("North arrow",x+100,y-100);CHECK(c.blocks().references().size()==refs+1);
    if(!c.blocks().references().empty())CHECK(c.blocks().references().back().name=="__glcad_symbol_north");
}
