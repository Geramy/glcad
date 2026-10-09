#include "test.hpp"
#include "ui/shell.hpp"
#include "ui/ai_tools.hpp"
#include "ui/mcp.hpp"
#include <cmath>

namespace {
using namespace gcapp;
Shell door_shell() {
    Shell shell; shell.set_dpr(1); shell.set_viewport(1200,800);
    shell.canvas().set_display_unit(gunit::Length::Metre);
    return shell;
}
gai::Json door_tool(Shell& shell, const char* name, gai::Json args) {
    gai::ToolCall call; call.function.name=name; call.function.arguments=args.dump();
    return gai::Json::parse(ai_tools::execute(shell,call));
}
void add_door(Shell& shell, double x) {
    CHECK(!door_tool(shell,"draw_door",{{"hinge",std::to_string(x)+",0"},
        {"latch",std::to_string(x+1)+",0"},{"framing","none"}}).contains("error"));
}
void door_click(Shell& shell, double x, double y) {
    double px,py; CHECK(shell.camera().project(gview::Point3::from(x,y,0),px,py));
    shell.pointer_move(px,py); shell.build_frame(); shell.pointer_down(px,py);
    shell.pointer_up(px,py); shell.build_frame();
}
}
TEST(door_modifier_click_toggles_whole_objects_and_preserves_context_selection) {
    for (bool command : {false,true}) {
        auto shell=door_shell(); auto& canvas=shell.canvas(); add_door(shell,0); add_door(shell,3);
        shell.camera().set_target(gview::Point3::from(2,1,0)); shell.fit_width(12); shell.build_frame();
        door_click(shell,0,.5); CHECK(canvas.selection().size()==2);
        shell.set_modifiers(false,!command,false,command);
        door_click(shell,3,.5); CHECK(canvas.selection().size()==4); CHECK(canvas.selected_door_count()==2);
        door_click(shell,0,.5); CHECK(canvas.selection().size()==2); CHECK(canvas.selected_door_count()==1);
        door_click(shell,0,.5); CHECK(canvas.selection().size()==4);
        shell.set_modifiers(false,false,false,false);
        double px,py; CHECK(shell.camera().project(gview::Point3::from(3,.5,0),px,py));
        shell.right_down(px,py); shell.build_frame(); CHECK(canvas.selection().size()==4);
        bool offered=false; for(const auto& node:shell.accessible_ui()) offered|=node.label=="Edit doors…";
        CHECK(offered);
        shell.command("DOORFLOW"); shell.build_frame();
        bool swing=false,hinge=false;
        for(const auto& node:shell.accessible_ui()) {
            swing|=node.label=="Reverse swing side"; hinge|=node.label=="Swap hinge / latch";
        }
        CHECK(swing && hinge);
    }
}
TEST(door_bulk_flow_swing_hinge_single_undo_and_atomic_failure) {
    auto shell=door_shell(); auto& canvas=shell.canvas(); add_door(shell,0); add_door(shell,3);
    canvas.select_all(); const auto original=canvas.shapes();
    CHECK(door_tool(shell,"edit_doors",{{"flow","both"},{"swing","reverse"},{"hinge","reverse"}})["edited_doors"]==2);
    CHECK(canvas.door_flow_shapes().size()==10);
    for(std::size_t i : {1u,3u}) {
        CHECK(canvas.shapes()[i].radius==original[i].radius);
        CHECK(canvas.shapes()[i].a.x>original[i].a.x);
    }
    CHECK(canvas.shapes()[0].b.y<canvas.shapes()[0].a.y);
    canvas.undo(); CHECK(canvas.door_flow_shapes().empty());
    CHECK(canvas.shapes()[0].b.y==original[0].b.y); CHECK(canvas.shapes()[3].a.x==original[3].a.x);
    canvas.redo(); CHECK(canvas.door_flow_shapes().size()==10);
    canvas.undo();
    canvas.modify_shapes([&](auto& shapes) {
        gsnap::Shape wall; wall.layer="A-WALL";
        wall.a=gcm::to_lattice(canvas.volume(),3.2,-.4); wall.b=gcm::to_lattice(canvas.volume(),3.7,-.4);
        shapes.push_back(wall);
    });
    canvas.select_all(); const auto serial=canvas.edit_serial();
    CHECK(door_tool(shell,"edit_doors",{{"flow","forward"},{"swing","reverse"}}).contains("error"));
    CHECK(canvas.edit_serial()==serial); CHECK(canvas.door_flow_shapes().empty());
    CHECK(canvas.shapes()[0].b.y==original[0].b.y);
    CHECK(door_tool(shell,"edit_doors",{{"swing","left"}}).contains("error"));
    CHECK(canvas.edit_serial()==serial);
}
TEST(door_hosted_flip_moves_to_opposite_jamb_face_without_changing_walls) {
    auto shell=door_shell(); auto& canvas=shell.canvas();
    CHECK(!door_tool(shell,"draw_wall",{{"from","0,0"},{"to","6,0"},{"thickness","0.2"}}).contains("error"));
    CHECK(!door_tool(shell,"draw_door",{{"hinge","2,0"},{"latch","3,0"},
        {"snap_to_wall","true"},{"framing","none"}}).contains("error"));
    const auto before=canvas.shapes(); canvas.select_all();
    CHECK_MSG(canvas.edit_selected_doors({},true,false),"%s",canvas.message().c_str());
    for(std::size_t i=0;i<before.size();++i) {
        if(before[i].layer=="A-WALL") {
            CHECK(canvas.shapes()[i].a.x==before[i].a.x); CHECK(canvas.shapes()[i].a.y==before[i].a.y);
            CHECK(canvas.shapes()[i].b.x==before[i].b.x); CHECK(canvas.shapes()[i].b.y==before[i].b.y);
        } else if(before[i].kind==gsnap::ShapeKind::Arc) {
            CHECK(canvas.shapes()[i].radius==before[i].radius);
            CHECK(std::abs(double(canvas.shapes()[i].a.y)*canvas.volume().inv_scale()+.1)<1e-6);
        }
    }
    CHECK(canvas.edit_selected_doors({},true,false));
    for(std::size_t i=0;i<before.size();++i) CHECK(canvas.shapes()[i].a.y==before[i].a.y);
}
TEST(door_mcp_bulk_edit_uses_same_validation_and_undo) {
    auto shell=door_shell(); add_door(shell,0); add_door(shell,3); shell.canvas().select_all();
    mcp::Server server(shell); using gai::Json;
    server.handle({{"jsonrpc","2.0"},{"id",1},{"method","initialize"},
        {"params",{{"protocolVersion","2025-11-25"}}}});
    server.handle({{"jsonrpc","2.0"},{"method","notifications/initialized"}});
    const auto result=server.handle({{"jsonrpc","2.0"},{"id",2},{"method","tools/call"},
        {"params",{{"name","edit_doors"},{"arguments",{{"flow","forward"},{"hinge","reverse"}}}}}});
    CHECK(result.has_value()); CHECK(!result->at("result")["isError"].get<bool>());
    CHECK(shell.canvas().door_flow_shapes().size()==6);
    shell.canvas().undo(); CHECK(shell.canvas().door_flow_shapes().empty());
}
TEST(door_additive_selection_keeps_text_dimensions_blocks_hatches_and_mechanical_edges) {
    auto shell=door_shell(); auto& c=shell.canvas(); add_door(shell,0);
    const auto p=[&](double x,double y) { return gcm::to_lattice(c.volume(),x,y); };
    c.add_text(p(3,3),"Note",.5);
    gdoc::BlockDef def; def.name="Selection block";
    gsnap::Shape line; line.a=p(5,0); line.b=p(5,2); def.geometry={line}; c.blocks().define(def);
    gdoc::BlockRef ref; ref.name=def.name; c.blocks().insert(ref);
    gdim::Dimension dim; dim.kind=gdim::Kind::Leader; dim.p1=p(7,0); dim.p2=p(7,2); dim.p3=p(8,2);
    dim.text_override="Leader"; c.add_dimensions({dim});
    c.modify_shapes([&](auto& shapes) {
        gsnap::Shape edge; edge.poly=200; edge.a=p(10,0); edge.b=p(12,0); shapes.push_back(edge);
        edge.poly_index=1; edge.a=p(12,0); edge.b=p(12,2); shapes.push_back(edge);
        edge.poly=0; edge.a=p(14,0); edge.b=p(16,0); shapes.push_back(edge);
        edge.a=p(16,0); edge.b=p(16,2); shapes.push_back(edge);
        edge.a=p(16,2); edge.b=p(14,2); shapes.push_back(edge);
        edge.a=p(14,2); edge.b=p(14,0); shapes.push_back(edge);
    });
    CHECK(c.hatch_at_world(p(15,1),"ANSI31")); c.clear_selection(); c.view().set_mapping(20,400,30);
    const auto toggle=[&](double x,double y) {
        double sx,sy; c.view().screen_of(x,y,sx,sy); c.select_edge_at(sx,sy);
    };
    toggle(0,.5); CHECK(c.selection().size()==2);
    double lx,ly,hx,hy; c.text_bounds(c.texts()[0],lx,ly,hx,hy);
    toggle((lx+hx)*.5,(ly+hy)*.5); CHECK(c.selected_texts().size()==1);
    toggle(5,1); CHECK(c.selected_references().size()==1);
    toggle(7,1); CHECK(c.selected_dimensions().size()==1);
    toggle(11,0); CHECK(c.selection().size()==3);
    toggle(15,1); CHECK(c.selected_hatches().size()==1);
    CHECK(c.selection_count()==6);
    toggle(5,1); CHECK(c.selected_references().empty()); CHECK(c.selection_count()==5);
    toggle(7,1); CHECK(c.selected_dimensions().empty());
    toggle((lx+hx)*.5,(ly+hy)*.5); CHECK(c.selected_texts().empty());
    toggle(15,1); CHECK(c.selected_hatches().empty());
    CHECK(c.selected_door_count()==1); CHECK(c.selection().size()==3);
}
