#include "test.hpp"
#include "ui/shell.hpp"
#include "ui/ai_tools.hpp"
#include "ui/mcp.hpp"
#include "ui/snapshot.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace gcapp;
namespace {
void box(Canvas& c,double ax,double ay,double bx,double by) {
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    c.modify_shapes([&](auto& shapes) {
        for(const auto& points:std::vector<std::pair<gcm::Lattice2,gcm::Lattice2>>{
            {p(ax,ay),p(bx,ay)},{p(bx,ay),p(bx,by)},{p(bx,by),p(ax,by)},{p(ax,by),p(ax,ay)}}) {
            gsnap::Shape edge;edge.a=points.first;edge.b=points.second;shapes.push_back(edge);
        }
    });
}
gai::Json hatch_tool(Shell& s,const char* name,gai::Json args) {
    gai::ToolCall call;call.function.name=name;call.function.arguments=args.dump();
    return gai::Json::parse(ai_tools::execute(s,call));
}
bool click_hatch_control(Shell& s,const std::string& label) {
    s.build_frame();for(const auto& item:s.accessible_ui())if(item.label==label) {
        const auto r=item.bounds;const float x=r.x+r.w/2,y=r.y+r.h/2;
        s.pointer_move(x,y);s.build_frame();s.pointer_down(x,y);s.build_frame();s.pointer_up(x,y);s.build_frame();return true;
    }
    return false;
}
}
TEST(hatch_style_shared_tools_validate_regenerate_undo_and_persist) {
    Shell s;s.set_viewport(1200,800);auto& c=s.canvas();c.set_display_unit(gunit::Length::Metre);
    box(c,0,0,5,4);CHECK(c.hatch_at_world(gcm::to_lattice(c.volume(),1,1),"ANSI31",.1));
    const auto geometry=c.shapes();
    auto listing=hatch_tool(s,"list_hatches",gai::Json::object());CHECK(listing["hatches"].size()==1);CHECK(!listing["patterns"].empty());
    auto update=[&](const char* property,const char* value){return hatch_tool(s,"set_hatch_style",{{"hatch_ids",{0}},{"property",property},{"value",value}});};
    CHECK(!update("PATTERN","NET").contains("error"));CHECK(c.hatches()[0].style.name=="NET");CHECK(c.hatches()[0].valid);
    CHECK(!update("SCALE",".25").contains("error"));CHECK(c.hatches()[0].style.scale==.25);
    CHECK(!update("ANGLE","30").contains("error"));CHECK(c.hatches()[0].style.angle==30);
    CHECK(!update("COLOR","#EA7428").contains("error"));CHECK(c.hatches()[0].attrs.color==gdoc::Color::rgb(234,116,40));
    CHECK(c.shapes().size()==geometry.size());CHECK(c.shapes()[0].a.x==geometry[0].a.x);CHECK(c.shapes()[0].a.y==geometry[0].a.y);
    const auto serial=c.edit_serial();
    for(auto entry:{std::pair{"SCALE","0"},std::pair{"SCALE","nan"},std::pair{"ANGLE","12oops"},std::pair{"COLOR","#gg0000"},std::pair{"PATTERN","missing"}})
        CHECK(update(entry.first,entry.second).contains("error"));
    CHECK(c.edit_serial()==serial);
    c.undo();CHECK(c.hatches()[0].attrs.color!=gdoc::Color::rgb(234,116,40));c.redo();CHECK(c.hatches()[0].attrs.color==gdoc::Color::rgb(234,116,40));
    const auto path=(std::filesystem::temp_directory_path()/("glcad-hatch-style-"+std::to_string(getpid())+".glcad")).string();
    CHECK(s.save_file(path));Shell loaded;CHECK(loaded.open_file(path));CHECK(loaded.canvas().hatches().size()==1);
    const auto& h=loaded.canvas().hatches()[0];CHECK(h.style.name=="NET");CHECK(h.style.scale==.25);CHECK(h.style.angle==30);CHECK(h.attrs.color==gdoc::Color::rgb(234,116,40));
    const auto dxf=path+".dxf";CHECK(s.export_file(dxf));Shell exchanged;CHECK(exchanged.open_file(dxf));
    CHECK(exchanged.canvas().hatches().size()==1);
    if(!exchanged.canvas().hatches().empty()) {
        const auto& h=exchanged.canvas().hatches()[0];CHECK(h.style.name=="NET");CHECK(h.style.scale==.25);CHECK(h.style.angle==30);CHECK(h.attrs.color==gdoc::Color::rgb(234,116,40));
    }
    std::filesystem::remove(dxf);
    std::filesystem::remove(path);
    mcp::Server server(s);using gai::Json;
    const auto request=[](int id,const char* method,Json params=Json::object()) {
        return Json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};
    };
    CHECK(server.handle(request(1,"initialize",{{"protocolVersion","2025-11-25"}}))->contains("result"));
    const auto listed=server.handle(request(2,"tools/call",{{"name","list_hatches"},{"arguments",Json::object()}}));
    CHECK(listed->at("result")["structuredContent"]["hatches"][0]["hatch_id"]==0);
    const auto changed=server.handle(request(3,"tools/call",{{"name","set_hatch_style"},
        {"arguments",{{"hatch_ids",{0}},{"property","COLOR"},{"value","3"}}}}));
    CHECK(!changed->at("result")["isError"].get<bool>());CHECK(c.hatches()[0].attrs.color==gdoc::Color::aci(3));

}
TEST(hatch_style_interior_selection_respects_islands_and_geometry_priority) {
    Canvas c;c.view().set_viewport(1000,700);box(c,0,0,6,4);box(c,2,1,3,2);
    CHECK(c.hatch_at_world(gcm::to_lattice(c.volume(),1,1),"ANSI31",.1));
    double x,y;c.view().screen_of(1,1,x,y);c.select_at(x,y,false);CHECK(c.selected_hatches().size()==1);CHECK(c.selection().empty());
    c.view().screen_of(2.5,1.5,x,y);c.select_at(x,y,false);CHECK(c.selected_hatches().empty());
    c.view().screen_of(0,2,x,y);c.select_at(x,y,false);CHECK(c.selected_hatches().empty());CHECK(!c.selection().empty());
    c.view().screen_of(1,1,x,y);c.select_at(x,y,false);CHECK(c.selection_count()==1);
    c.erase_selected();CHECK(c.hatches().empty());CHECK(c.shapes().size()==8);
    c.undo();CHECK(c.hatches().size()==1);CHECK(c.selected_hatches().size()==1);c.redo();CHECK(c.hatches().empty());
    c.undo();c.select_all();CHECK(c.selected_hatches().size()==1);c.clear();CHECK(c.hatches().empty());CHECK(c.selected_hatches().empty());c.undo();CHECK(c.hatches().size()==1);
}
TEST(hatch_style_bar_exposes_real_patterns_colors_scale_and_angle) {
    Shell s;s.set_dpr(1);s.set_viewport(1500,1000);std::string error;CHECK(s.load_font(&error));
    auto& c=s.canvas();c.set_display_unit(gunit::Length::Metre);box(c,-2,-1,2,1);
    CHECK(c.hatch_at_world(gcm::to_lattice(c.volume(),0,0),"ANSI31",.1));c.set_tool(Tool::Select);s.build_frame();
    double x,y;c.view().screen_of(.4,.3,x,y);s.pointer_move(x,y);s.build_frame();s.pointer_down(x,y);s.pointer_up(x,y);s.build_frame();
    CHECK(c.selected_hatches().size()==1);
    for(const char* label:{"Hatch pattern","Hatch color","Hatch scale","Hatch angle"}) {
        bool found=false;for(const auto& item:s.accessible_ui())found|=item.label==label;CHECK_MSG(found,"missing %s",label);
    }
    CHECK(click_hatch_control(s,"Hatch pattern"));CHECK(click_hatch_control(s,"NET"));CHECK(c.hatches()[0].style.name=="NET");
    CHECK(click_hatch_control(s,"Hatch color"));CHECK(click_hatch_control(s,"1  #FF0000"));CHECK(c.hatches()[0].attrs.color==gdoc::Color::aci(1));
    if(const char* dir=std::getenv("GLCAD_UI_REVIEW_DIR")) {
        std::filesystem::create_directories(dir);const auto png=snapshot_png(s,1500,1000);
        std::ofstream out(std::filesystem::path(dir)/"hatch-style-bar.png",std::ios::binary);out.write(reinterpret_cast<const char*>(png.data()),png.size());
    }
    s.set_viewport(500,800);s.build_frame();for(const auto& item:s.accessible_ui())if(item.label=="Hatch angle")CHECK(item.bounds.right()<=500);
}
