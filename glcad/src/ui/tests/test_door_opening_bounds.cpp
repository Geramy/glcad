#include "test.hpp"
#include "ui/canvas.hpp"
#include "geometry/region.hpp"
#include "geometry/boolean.hpp"
#include "ui/shell.hpp"
#include "ui/ai_tools.hpp"
#include "exchange/dxf.hpp"
#include "exchange/dwg.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace gcapp;

TEST(wall_sections_move_by_interior_drag_and_coordinates_without_moving_neighbours) {
    for(bool document_undo:{false,true}) {
        Shell shell;shell.set_dpr(1);shell.set_viewport(1600,1000);shell.set_document_undo(document_undo);
        auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
        CHECK(c.add_wall(point(-3,0),point(1,0),.3));
        CHECK(c.add_wall(point(1,0),point(1,3),.3));
        CHECK(c.blocks().references().size()==2);shell.build_frame();
        const auto neighbour=c.blocks().references()[1];
        const auto target=shell.camera().target();const auto distance=shell.camera().distance();
        double x,y,to_x,to_y;
        CHECK(shell.camera().project(gview::Point3::from(-1,0,0),x,y));
        CHECK(shell.camera().project(gview::Point3::from(-1,-1,0),to_x,to_y));
        shell.pointer_move(x,y);shell.build_frame();shell.pointer_down(x,y);shell.build_frame();
        CHECK(c.selection_dragging());CHECK(c.selected_references()==std::vector<gcm::u32>{0});
        shell.pointer_move(to_x,to_y);shell.build_frame();shell.pointer_up(to_x,to_y);shell.build_frame();
        auto material=gregion::from_shapes(c.shapes());CHECK(material.ok);
        if(material.ok) {CHECK(material.region.contains(point(-1,-1)));CHECK(!material.region.contains(point(-1,0)));CHECK(material.region.contains(point(1,2)));}
        CHECK(c.blocks().references()[1].insertion.x==neighbour.insertion.x);
        CHECK(c.blocks().references()[1].insertion.y==neighbour.insertion.y);
        CHECK(shell.camera().target().x.to_f64()==target.x.to_f64());
        CHECK(shell.camera().target().y.to_f64()==target.y.to_f64());CHECK(shell.camera().distance()==distance);
        shell.command("UNDO");shell.build_frame();material=gregion::from_shapes(c.shapes());CHECK(material.ok);
        if(material.ok)CHECK(material.region.contains(point(-1,0)));
        shell.command("REDO");shell.build_frame();material=gregion::from_shapes(c.shapes());CHECK(material.ok);
        if(material.ok)CHECK(material.region.contains(point(-1,-1)));
        c.select_reference(0);CHECK(shell.set_selection_property("X","-2m"));CHECK(shell.set_selection_property("Y","-2m"));shell.build_frame();
        double px,py;gcm::to_world(c.volume(),c.blocks().references()[0].insertion,px,py);
        CHECK(std::abs(px+2)<1e-6);CHECK(std::abs(py+2)<1e-6);
        CHECK(c.blocks().references()[1].insertion.x==neighbour.insertion.x);
        CHECK(c.blocks().references()[1].insertion.y==neighbour.insertion.y);
    }
}

TEST(wall_sections_preserve_sources_through_merge_save_reopen_and_ungroup) {
    Shell shell;auto& c=shell.canvas();
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(point(0,0),point(4,0),.2));CHECK(c.add_wall(point(4,0),point(4,3),.2));
    c.merge_overlaps(.002,true,true);shell.build_frame();
    CHECK(c.blocks().references().size()==2);
    const auto path=(std::filesystem::temp_directory_path()/("glcad-wall-sections-"+std::to_string(getpid())+".glcad")).string();
    CHECK(shell.save_file(path));Shell reopened;CHECK(reopened.open_file(path));std::filesystem::remove(path);
    auto& restored=reopened.canvas();CHECK(restored.blocks().references().size()==2);
    CHECK(Canvas::is_wall_section(restored.blocks().references()[0]));
    restored.select_reference(0);CHECK(restored.explode_block(0));reopened.build_frame();
    CHECK(restored.blocks().references().size()==1);
    std::size_t lines=0;
    for(const auto& shape:restored.shapes())if(shape.attrs.user.contains("wall_ungrouped")) {
        ++lines;CHECK(shape.poly==0);CHECK(!shape.region);
    }
    CHECK(lines==4);CHECK(restored.selection().size()==4);
    restored.detect_wall_regions(true);
    CHECK(std::count_if(restored.shapes().begin(),restored.shapes().end(),[](const auto& shape){return shape.attrs.user.contains("wall_ungrouped") && !shape.poly;})==4);
}

TEST(wall_sections_legacy_joined_outline_can_be_selected_as_separate_straight_sections) {
    Canvas source;const auto point=[&](double x,double y){return gcm::to_lattice(source.volume(),x,y);};
    CHECK(source.add_wall(point(0,0),point(4,0),.2));CHECK(source.add_wall(point(4,0),point(4,3),.2));
    auto legacy=source.shapes();
    for(auto& shape:legacy) {shape.attrs.user.erase("wall_joined");shape.attrs.user.erase("wall_source_outline");shape.attrs.user.erase("wall_source_signature");}
    Canvas c;c.load_shapes(legacy);c.adopt_wall_sections();
    CHECK(c.blocks().references().size()>=2);
    double ax,ay,bx,by;c.view().screen_of(2,0,ax,ay);c.view().screen_of(4,2,bx,by);
    const auto horizontal=c.pick_wall_section(ax,ay),vertical=c.pick_wall_section(bx,by);
    CHECK(horizontal!=Canvas::npos32);CHECK(vertical!=Canvas::npos32);CHECK(horizontal!=vertical);
    const auto material=gregion::from_shapes(c.shapes());CHECK(material.ok);
    const auto original=gregion::from_shapes(legacy);CHECK(original.ok);
    if(material.ok && original.ok)CHECK(std::abs(material.region.area(c.volume())-original.region.area(c.volume()))<1e-6);
}

TEST(door_opening_preserves_closed_boundaries_at_wall_ends_and_for_thick_walls) {
    for(double thickness:{.1,.45})for(double from:{0.,.001,.01,1.}) {
        Canvas c;const auto& volume=c.volume();
        const auto point=[&](double x,double y){return gcm::to_lattice(volume,x,y);};
        CHECK(c.add_wall(point(0,0),point(4,0),thickness));
        const auto group=c.shapes().front().poly;
        const auto report=c.merge_overlaps(.002,false,true,Canvas::OpeningCut{point(from,0),point(from+1,0),thickness,group});
        CHECK(report.cut==2);
        auto boundary=gregion::from_shapes(c.shapes());
        CHECK_MSG(boundary.ok,"wall thickness=%g opening start=%g: %s",thickness,from,boundary.error.c_str());
        if(boundary.ok)CHECK(std::abs(boundary.region.area(volume)-3*thickness)<1e-6);
    }
}

namespace {
gai::Json opening_tool(Shell& shell,const char* name,gai::Json arguments) {
    gai::ToolCall call;call.function.name=name;call.function.arguments=arguments.dump();
    return gai::Json::parse(ai_tools::execute(shell,call));
}
gai::Json opening_door(Shell& shell,gai::Json arguments) {
    return opening_tool(shell,"draw_door",std::move(arguments));
}
std::vector<gsnap::Shape> opening_walls(const Canvas& canvas) {
    std::vector<gsnap::Shape> walls;
    for(const auto& shape:canvas.shapes())if(shape.layer=="A-WALL")walls.push_back(shape);
    return walls;
}
std::vector<gsnap::Shape> wall_material(const Canvas& canvas) {
    std::vector<gsnap::Shape> walls;
    for(const auto& shape:canvas.shapes()) {
        const auto type=shape.attrs.user.find("sketch_object");
        if(type!=shape.attrs.user.end() && type->second=="wall")walls.push_back(shape);
    }
    return walls;
}
}

TEST(wall_sections_leave_doors_stationary_and_restore_openings_after_moving_back) {
    for(bool document_undo:{false,true}) {
        Shell shell;shell.set_dpr(1);shell.set_viewport(1600,1000);shell.set_document_undo(document_undo);
        auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
        CHECK(c.add_wall(point(0,0),point(6,0),.2));
        CHECK(!opening_door(shell,{{"hinge","2,0"},{"latch","3,0"},{"framing","none"}}).contains("error"));
        shell.build_frame();
        const auto door=std::find_if(c.shapes().begin(),c.shapes().end(),[](const auto& s){return s.attrs.user.contains("opening_auto");});
        CHECK(door!=c.shapes().end());if(door==c.shapes().end())continue;
        const auto original=*door;const auto insertion=c.blocks().references()[0].insertion;
        const auto check=[&](double wall_y,bool cut) {
            const auto material=gregion::from_shapes(wall_material(c));CHECK(material.ok);
            if(material.ok) {CHECK(material.region.contains(point(1,wall_y)));CHECK(material.region.contains(point(2.5,wall_y))!=cut);}
            const auto index=c.index_of_key(original.key);CHECK(index!=Canvas::npos);
            if(index!=Canvas::npos) {CHECK(c.shapes()[index].a.x==original.a.x);CHECK(c.shapes()[index].a.y==original.a.y);}
        };
        c.select_reference(0);double x,y,to_x,to_y;
        CHECK(shell.camera().project(gview::Point3::from(1,0,0),x,y));
        CHECK(shell.camera().project(gview::Point3::from(1,-1,0),to_x,to_y));
        shell.pointer_move(x,y);shell.build_frame();shell.pointer_down(x,y);shell.pointer_move(to_x,to_y);shell.build_frame();
        check(-1,false);shell.key_special(1);shell.build_frame();check(0,true);
        CHECK(c.blocks().references()[0].insertion.x==insertion.x);CHECK(c.blocks().references()[0].insertion.y==insertion.y);
        c.select_reference(0);CHECK(shell.set_selection_property("Y","-2m"));shell.build_frame();
        check(-2.1,false);
        c.modify_references([&](auto& refs){refs[0].insertion=insertion;});shell.build_frame();check(0,true);
        grender::d2::DrawList selected,unselected;c.build_geometry(selected);c.clear_selection();c.build_geometry(unselected);
        CHECK(selected.lines().size()==unselected.lines().size());
        double dx,dy;gcm::to_world(c.volume(),original.a,dx,dy);
        CHECK(shell.camera().project(gview::Point3::from(dx,dy,0),x,y));
        shell.pointer_move(x,y);shell.build_frame();shell.pointer_down(x,y);shell.build_frame();
        CHECK(c.has_selected_door());CHECK(c.selected_references().empty());shell.key_special(1);shell.build_frame();
        c.select_reference(0);CHECK(c.explode_block(0));shell.build_frame();
        const auto loose=gregion::from_shapes(opening_walls(c));CHECK(loose.ok);
        if(loose.ok) {CHECK(loose.region.contains(point(1,0)));CHECK(!loose.region.contains(point(2.5,0)));}
        CHECK(c.blocks().references().empty());
    }
}

TEST(wall_regions_export_visible_boundaries_and_openings_without_hidden_source_blocks) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(point(0,0),point(8,0),.2));CHECK(c.add_wall(point(8,0),point(8,4),.2));
    CHECK(!opening_door(shell,{{"hinge","2,0"},{"latch","3,0"},{"framing","none"}}).contains("error"));
    CHECK(!opening_tool(shell,"draw_window",{{"from","5,0"},{"to","6,0"},{"framing","none"}}).contains("error"));
    shell.build_frame();
    const auto before=wall_material(c);const auto region=gregion::from_shapes(before);CHECK(region.ok);
    const auto count=c.shapes().size(),refs=c.blocks().references().size();
    const auto check=[&](const auto& entities,const auto& blocks) {
        std::vector<gsnap::Shape> walls;
        for(const auto& e:entities)if(e.layer=="A-WALL")walls.push_back(e.shape);
        CHECK(walls.size()==before.size());
        const auto back=gregion::from_shapes(walls);CHECK_MSG(back.ok,"%s",back.error.c_str());
        if(back.ok && region.ok) {
            CHECK(std::abs(back.region.area(c.volume())-region.region.area(c.volume()))<1e-6);
            CHECK(!back.region.contains(point(2.5,0)));CHECK(!back.region.contains(point(5.5,0)));
            CHECK(back.region.contains(point(4,0)));CHECK(back.region.contains(point(8,2)));
        }
        CHECK(blocks.references().empty());
        CHECK(std::none_of(blocks.all().begin(),blocks.all().end(),[](const auto& b){return b.name.starts_with("__glcad_wall_");}));
    };
    for(const auto ext:{"dxf","dwg"}) {
        const auto path=(std::filesystem::temp_directory_path()/("glcad-region-export-"+std::to_string(getpid())+"."+ext)).string();
        CHECK_MSG(shell.export_file(path),"%s",shell.log().empty()?"":shell.log().back().c_str());
        std::ifstream file(path,std::ios::binary);const std::string bytes(std::istreambuf_iterator<char>(file),{});CHECK(!bytes.empty());
        if(std::string(ext)=="dxf") {
            auto back=gdxf::read(bytes,c.volume());CHECK_MSG(back.ok,"%s",back.error.c_str());
            std::erase_if(back.document.entities,[](const auto& e){return e.kind=="HATCH";});
            check(back.document.entities,back.document.blocks);
        } else {
            const auto back=gdwg::read(reinterpret_cast<const gcm::u8*>(bytes.data()),bytes.size(),c.volume());
            CHECK_MSG(back.ok,"%s",back.error.c_str());check(back.entities,back.blocks);
        }
        if(const char* directory=std::getenv("GLCAD_REGION_EXPORT_ARTIFACT_DIR")) {
            std::filesystem::create_directories(directory);
            std::filesystem::copy_file(path,std::filesystem::path(directory)/(std::string("wall-openings.")+ext),std::filesystem::copy_options::overwrite_existing);
        }
        std::filesystem::remove(path);
        CHECK(c.shapes().size()==count);CHECK(c.blocks().references().size()==refs);
        CHECK(std::count_if(c.shapes().begin(),c.shapes().end(),[](const auto& s){return s.region;})==before.size());
    }
}

TEST(wall_features_window_after_door_keeps_material_closed_and_cleanup_repeatable) {
    for(double thickness:{.12,.45})for(bool framing:{false,true}) {
        Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
        CHECK(c.add_wall(point(0,0),point(8,0),thickness));
        const auto door=opening_door(shell,{{"hinge","1,0"},{"latch","2,0"},{"framing",framing?"wood":"none"}});
        CHECK_MSG(!door.contains("error"),"%s",door.dump().c_str());
        gai::ToolCall call;call.function.name="draw_window";
        call.function.arguments=gai::Json{{"from","4,0"},{"to","5,0"},{"framing",framing?"wood":"none"}}.dump();
        const auto window=gai::Json::parse(ai_tools::execute(shell,call));
        CHECK_MSG(!window.contains("error"),"%s",window.dump().c_str());
        for(int pass=0;pass<3;++pass) {
            const auto material=gregion::from_shapes(wall_material(c));
            CHECK_MSG(material.ok,"thickness=%g framing=%d pass=%d: %s",thickness,framing,pass,material.error.c_str());
            if(material.ok) {
                CHECK(std::abs(material.region.area(c.volume())-6*thickness)<1e-5);
                CHECK(!material.region.contains(point(1.5,0)));
                CHECK(!material.region.contains(point(4.5,0)));
            }
            c.merge_overlaps(.002,true,true);shell.build_frame();
        }
        CHECK(c.add_wall(point(8,0),point(8,3),thickness));
        shell.command("WALLS 200mm 2m");
        CHECK_MSG(!shell.modeller().bodies().empty(),"%s",shell.modeller().message().c_str());
        const auto material=gregion::from_shapes(wall_material(c));
        if(material.ok) {
            double volume=0;for(const auto& body:shell.modeller().bodies())volume+=gtopo::volume(body,c.volume());
            CHECK(std::abs(volume-2*material.region.area(c.volume()))<1e-5);
        }
    }
}

TEST(wall_features_new_multiroom_plan_extrudes_only_material_after_doors_and_windows) {
    for(double thickness:{.12,.45})for(double angle:{0.,.7}) {
        Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),
            x*std::cos(angle)-y*std::sin(angle),x*std::sin(angle)+y*std::cos(angle));};
        const auto text=[&](double x,double y) {
            char buf[128];std::snprintf(buf,sizeof(buf),"%.17g,%.17g",
                x*std::cos(angle)-y*std::sin(angle),x*std::sin(angle)+y*std::cos(angle));
            return std::string(buf);
        };
        for(const auto& ends:std::vector<std::array<double,4>>{{0,0,12,0},{12,0,12,8},
            {12,8,0,8},{0,8,0,0},{6,0,6,8},{0,4,12,4}}) {
            CHECK_MSG(c.add_wall(point(ends[0],ends[1]),point(ends[2],ends[3]),thickness),
                "thickness=%g angle=%g wall=(%g,%g)-(%g,%g): %s",thickness,angle,ends[0],ends[1],ends[2],ends[3],c.message().c_str());
            shell.build_frame();
        }
        const auto before=gregion::from_shapes(wall_material(c));CHECK(before.ok);
        for(const auto& ends:std::vector<std::array<double,4>>{{2,0,3,0},{6,1,6,2}}) {
            const auto result=opening_door(shell,{{"hinge",text(ends[0],ends[1])},{"latch",text(ends[2],ends[3])}});
            CHECK_MSG(!result.contains("error"),"%s",result.dump().c_str());
            CHECK(result.value("snapped_to_wall",false));shell.build_frame();
        }
        for(const auto& ends:std::vector<std::array<double,4>>{{12,5,12,6},{9,8,10,8}}) {
            const auto result=opening_tool(shell,"draw_window",{{"from",text(ends[0],ends[1])},{"to",text(ends[2],ends[3])}});
            CHECK_MSG(!result.contains("error"),"%s",result.dump().c_str());
            CHECK(result.value("snapped_to_wall",false));shell.build_frame();
        }
        for(int pass=0;pass<2;++pass) {
            const auto prior=gregion::from_shapes(wall_material(c));
            CHECK_MSG(prior.ok,"before cleanup thickness=%g angle=%g: %s",thickness,angle,prior.error.c_str());
            const auto cleanup=c.merge_overlaps(.002,false,true);CHECK(cleanup.cut==0);shell.build_frame();
            const auto cut=gregion::from_shapes(wall_material(c));
            CHECK_MSG(cut.ok,"after openings thickness=%g angle=%g: %s",thickness,angle,cut.error.c_str());
            c.merge_overlaps(.002,true,false);shell.build_frame();
            const auto material=gregion::from_shapes(wall_material(c));
            CHECK_MSG(material.ok,"thickness=%g angle=%g: %s",thickness,angle,material.error.c_str());
            if(!material.ok)continue;
            if(before.ok)CHECK(std::abs(material.region.area(c.volume())-(before.region.area(c.volume())-4*thickness))<1e-5);
            for(double x:{3.,9.})for(double y:{2.,6.})CHECK(!material.region.contains(point(x,y)));
            for(auto p:{point(2.5,0),point(6,1.5),point(12,5.5),point(9.5,8)})CHECK(!material.region.contains(p));
        }
        c.clear_selection();CHECK(c.wall_centerline_indices().empty());
        shell.command("WALLS 200mm 2m");
        CHECK_MSG(!shell.modeller().bodies().empty(),"%s",shell.modeller().message().c_str());
        const auto material=gregion::from_shapes(wall_material(c));
        if(material.ok) {
            double volume=0;
            for(const auto& body:shell.modeller().bodies()) {CHECK(body.analyse().closed);volume+=gtopo::volume(body,c.volume());}
            CHECK(std::abs(volume-2*material.region.area(c.volume()))<1e-5);
        }
    }
}

TEST(wall_features_standalone_openings_remain_standalone_during_cleanup) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(point(0,0),point(8,0),.45));
    const auto original=gregion::from_shapes(wall_material(c));CHECK(original.ok);
    const auto door=opening_door(shell,{{"hinge","1,0"},{"latch","2,0"},{"snap_to_wall",false}});
    CHECK(!door.contains("error"));CHECK(!door.value("snapped_to_wall",true));
    const auto window=opening_tool(shell,"draw_window",{{"from","4,0"},{"to","5,0"},{"snap_to_wall",false}});
    CHECK(!window.contains("error"));CHECK(!window.value("snapped_to_wall",true));
    for(int pass=0;pass<2;++pass) {
        const auto report=c.merge_overlaps(.002,true,true);CHECK(report.cut==0);shell.build_frame();
        const auto material=gregion::from_shapes(wall_material(c));CHECK(material.ok);
        if(material.ok && original.ok)CHECK(std::abs(material.region.area(c.volume())-original.region.area(c.volume()))<1e-7);
    }
    const auto count=c.shapes().size();
    const auto missing=opening_tool(shell,"draw_window",{{"from","0,10"},{"to","1,10"},{"snap_to_wall",true}});
    CHECK(missing.contains("error"));CHECK(c.shapes().size()==count);
}

TEST(door_default_host_detection_cuts_full_thickness_and_keeps_closed_styled_caps) {
    for(double thickness:{.12,.45})for(double angle:{0.,.7}) {
        Shell shell;shell.canvas().set_display_unit(gunit::Length::Metre);auto& c=shell.canvas();
        const auto point=[&](double x,double y) {
            return gcm::to_lattice(c.volume(),x*std::cos(angle)-y*std::sin(angle),x*std::sin(angle)+y*std::cos(angle));
        };
        const auto text=[&](double x,double y) {
            return std::to_string(x*std::cos(angle)-y*std::sin(angle))+","+
                std::to_string(x*std::sin(angle)+y*std::cos(angle));
        };
        CHECK(c.add_wall(point(0,0),point(5,0),thickness));
        const auto group=c.shapes().front().poly;
        c.modify_shapes([](auto& shapes) {for(auto& shape:shapes) {
            shape.attrs.user["wall_finish"]="custom";shape.attrs.color=gdoc::Color::rgb(31,63,95);
        }});
        const auto result=opening_door(shell,{{"hinge",text(2,.02)},{"latch",text(3,.02)},{"framing","none"}});
        CHECK_MSG(!result.contains("error"),"%s",result.dump().c_str());
        if(result.contains("error"))continue;
        CHECK(result["snapped_to_wall"]==true);CHECK(result["wall_faces_cut"]==2);
        const auto walls=opening_walls(c);CHECK(walls.size()==8);
        const auto boundary=gregion::from_shapes(walls);CHECK_MSG(boundary.ok,"%s",boundary.error.c_str());
        if(boundary.ok)CHECK(std::abs(boundary.region.area(c.volume())-4*thickness)<1e-5);
        for(const auto& wall:walls) {
            CHECK(wall.poly==group);CHECK(wall.attrs.user.count("wall_finish"));
            CHECK(wall.attrs.color==gdoc::Color::rgb(31,63,95));
        }
        for(const auto& shape:c.shapes())if(shape.layer=="A-DOOR" && shape.kind==gsnap::ShapeKind::Arc) {
            double x,y;gcm::to_world(c.volume(),shape.a,x,y);
            CHECK_MSG(std::abs(-x*std::sin(angle)+y*std::cos(angle)-thickness*.5)<1e-6,
                "thickness=%g angle=%g hinge=(%.12g,%.12g)",thickness,angle,x,y);
            CHECK_MSG(std::abs(double(shape.radius)*c.volume().inv_scale()-1)<1e-6,
                "thickness=%g angle=%g width=%.12g",thickness,angle,double(shape.radius)*c.volume().inv_scale());
        }
    }
}

TEST(door_default_host_detection_supports_manual_wall_faces) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    c.modify_shapes([&](auto& shapes) {
        const auto a=point(0,-.15),b=point(5,-.15),d=point(0,.15),e=point(5,.15);
        for(const auto& ends:{std::pair{a,b},std::pair{b,e},std::pair{e,d},std::pair{d,a}}) {
            gsnap::Shape wall;wall.kind=gsnap::ShapeKind::Segment;wall.layer="A-WALL";
            wall.a=ends.first;wall.b=ends.second;shapes.push_back(wall);
        }
    });
    const auto result=opening_door(shell,{{"hinge","2,0"},{"latch","3,0"},{"framing","none"}});
    CHECK_MSG(!result.contains("error"),"%s",result.dump().c_str());
    CHECK(result["snapped_to_wall"]==true);CHECK(result["wall_faces_cut"]==2);
    const auto boundary=gregion::from_shapes(opening_walls(c));CHECK(boundary.ok);
    if(boundary.ok)CHECK(std::abs(boundary.region.area(c.volume())-1.2)<1e-6);
}

TEST(door_explicit_standalone_skips_wall_cuts_and_required_host_rejects_missing_wall) {
    for(bool json_boolean:{false,true}) {
        Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        CHECK(c.add_wall(gcm::to_lattice(c.volume(),0,0),gcm::to_lattice(c.volume(),5,0),.3));
        const auto original=opening_walls(c);
        gai::Json args={{"hinge","2,0"},{"latch","3,0"},{"framing","none"}};
        args["snap_to_wall"]=json_boolean ? gai::Json(false) : gai::Json("false");
        const auto result=opening_door(shell,args);CHECK(!result.contains("error"));
        CHECK(result["snapped_to_wall"]==false);CHECK(result["wall_faces_cut"]==0);
        const auto walls=opening_walls(c);CHECK(walls.size()==original.size());
        for(std::size_t i=0;i<walls.size();++i) {
            CHECK(walls[i].a.x==original[i].a.x);CHECK(walls[i].a.y==original[i].a.y);
            CHECK(walls[i].b.x==original[i].b.x);CHECK(walls[i].b.y==original[i].b.y);
        }
        for(const auto& shape:c.shapes())if(shape.kind==gsnap::ShapeKind::Arc)CHECK(shape.a.y==0);
        const auto hosted=opening_door(shell,{{"hinge","3.5,0"},{"latch","4.5,0"},{"framing","none"}});
        CHECK(!hosted.contains("error"));CHECK(hosted["wall_faces_cut"]==2);
        const auto boundary=gregion::from_shapes(opening_walls(c));CHECK(boundary.ok);
        if(boundary.ok)CHECK(std::abs(boundary.region.area(c.volume())-1.2)<1e-6);
    }
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto required=opening_door(shell,{{"hinge","0,0"},{"latch","1,0"},{"snap_to_wall",true},{"framing","none"}});
    CHECK(required.contains("error"));CHECK(c.shapes().empty());
    const auto automatic=opening_door(shell,{{"hinge","0,0"},{"latch","1,0"},{"framing","none"}});
    CHECK(!automatic.contains("error"));CHECK(automatic["snapped_to_wall"]==false);
    CHECK(automatic["wall_faces_cut"]==0);CHECK(c.shapes().size()==2);
}

TEST(wall_region_repair_accepts_redrawn_plain_edges_without_reclassifying_rooms) {
    for(const char* replacement_layer:{"0","A-WALL"}) {
        Canvas c;const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
        CHECK(c.add_wall(point(0,0),point(4,0),.2));
        CHECK(c.add_wall(point(4,0),point(4,3),.2));
        const auto original=gregion::from_members(c.shapes());CHECK(original.ok);
        const auto missing=c.shapes().front();
        std::vector<gcm::u32> surviving_keys;
        for(std::size_t i=1;i<c.shapes().size();++i)surviving_keys.push_back(c.shapes()[i].key);
        c.modify_shapes([](auto& shapes){shapes.erase(shapes.begin());});
        const auto open_serial=c.edit_serial();
        CHECK(c.detect_wall_regions(false)==0);CHECK(c.edit_serial()>open_serial);
        CHECK(gregion::validate_members(c.shapes()).empty());
        const auto cleared_serial=c.edit_serial();
        CHECK(c.detect_wall_regions(false)==0);CHECK(c.edit_serial()==cleared_serial);
        c.modify_shapes([&](auto& shapes) {
            gsnap::Shape replacement;replacement.a=missing.a;replacement.b=missing.b;replacement.layer=replacement_layer;
            shapes.push_back(replacement);
            const gcm::Lattice2 room[]={point(10,10),point(13,10),point(13,13),point(10,13)};
            for(int i=0;i<4;++i){gsnap::Shape line;line.a=room[i];line.b=room[(i+1)%4];line.layer="0";shapes.push_back(line);}
        });
        CHECK(c.detect_wall_regions(false)>0);
        const auto walls=opening_walls(c);const auto repaired=gregion::from_members(walls);
        CHECK(repaired.ok);
        if(original.ok && repaired.ok)CHECK(std::abs(original.region.area(c.volume())-repaired.region.area(c.volume()))<1e-7);
        for(auto old_key:surviving_keys)CHECK(std::any_of(walls.begin(),walls.end(),[&](const auto& shape){return shape.key==old_key;}));
        std::size_t plain=0;
        for(const auto& shape:c.shapes())if(shape.layer=="0"){++plain;CHECK(!shape.region);CHECK(!shape.attrs.user.count("sketch_object"));}
        CHECK(plain==4);
        const auto serial=c.edit_serial();const auto unchanged=c.shapes();
        CHECK(c.detect_wall_regions()==0);CHECK(c.edit_serial()==serial);CHECK(c.shapes().size()==unchanged.size());
        for(std::size_t i=0;i<unchanged.size();++i) {
            CHECK(c.shapes()[i].key==unchanged[i].key);CHECK(c.shapes()[i].poly==unchanged[i].poly);
        }
    }
}

TEST(wall_region_repair_restores_door_split_membership_once) {
    Canvas c;const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(point(0,0),point(5,0),.2));
    const auto group=c.shapes().front().poly;
    CHECK(c.merge_overlaps(.002,false,true,Canvas::OpeningCut{point(2,0),point(3,0),.2,group}).cut==2);
    const auto cut=gregion::from_shapes(c.shapes());CHECK(cut.ok);
    if(cut.ok)CHECK(std::abs(cut.region.area(c.volume())-.8)<1e-6);
    c.modify_shapes([](auto& shapes){for(auto& shape:shapes){shape.region=false;shape.region_loop=0;shape.poly_index=0;}});
    CHECK(!gregion::from_members(c.shapes()).ok);
    CHECK(c.detect_wall_regions(false)>0);
    const auto repaired=gregion::from_members(c.shapes());CHECK(repaired.ok);
    if(repaired.ok){CHECK(repaired.region.loops.size()==2);CHECK(std::abs(repaired.region.area(c.volume())-cut.region.area(c.volume()))<1e-12);}
    const auto serial=c.edit_serial();const auto poly=c.shapes().front().poly;
    CHECK(c.detect_wall_regions(false)==0);CHECK(c.edit_serial()==serial);CHECK(c.shapes().front().poly==poly);
}

namespace {
void classification_rectangle(Canvas& c,double x0,double y0,double x1,double y1,const std::string& layer) {
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    const gcm::Lattice2 points[]={point(x0,y0),point(x1,y0),point(x1,y1),point(x0,y1)};
    c.modify_shapes([&](auto& shapes){for(int i=0;i<4;++i){gsnap::Shape s;s.a=points[i];s.b=points[(i+1)%4];s.layer=layer;shapes.push_back(s);}});
}
}

TEST(wall_classification_requires_material_not_just_a_wall_layer) {
    for(const char* layer:{"0","A-WALL"}) {
        Canvas c;classification_rectangle(c,0,0,6,4,layer);
        const auto serial=c.edit_serial();CHECK(c.detect_wall_regions()==0);CHECK(c.edit_serial()==serial);
        for(const auto& shape:c.shapes()){CHECK(!shape.region);CHECK(!shape.attrs.user.count("sketch_object"));}
        classification_rectangle(c,.2,.2,5.8,3.8,layer);
        CHECK(c.detect_wall_regions()==2);
        const auto material=gregion::from_members(c.shapes());CHECK(material.ok);
        if(material.ok){CHECK(material.region.loops.size()==2);CHECK(!material.region.contains(gcm::to_lattice(c.volume(),3,2)));CHECK(material.region.contains(gcm::to_lattice(c.volume(),.1,2)));}
        const auto recognized=c.edit_serial();CHECK(c.detect_wall_regions()==0);CHECK(c.edit_serial()==recognized);
    }
    Canvas rotated;const auto point=[&](double x,double y){return gcm::to_lattice(rotated.volume(),(x-y)*std::sqrt(.5),(x+y)*std::sqrt(.5));};
    const gcm::Lattice2 corners[]={point(0,0),point(4,0),point(4,.2),point(0,.2)};
    rotated.modify_shapes([&](auto& shapes){for(int i=0;i<4;++i){gsnap::Shape s;s.a=corners[i];s.b=corners[(i+1)%4];s.layer="0";shapes.push_back(s);}});
    CHECK(rotated.detect_wall_regions()==1);
}

TEST(walls_command_excludes_door_symbols_annotations_and_existing_cavities) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    classification_rectangle(c,0,0,6,4,"A-WALL");classification_rectangle(c,.2,.2,5.8,3.8,"A-WALL");
    CHECK(c.detect_wall_regions()==2);
    c.modify_shapes([&](auto& shapes) {
        for(const auto& layer:{std::string("A-DOOR"),std::string("A-GLAZ"),std::string("A-ANNO-DIMS")}) {
            gsnap::Shape shape;shape.a=gcm::to_lattice(c.volume(),10,1);shape.b=gcm::to_lattice(c.volume(),11,1);shape.layer=layer;shapes.push_back(shape);
        }
        gsnap::Shape leaf;leaf.a=gcm::to_lattice(c.volume(),12,1);leaf.b=gcm::to_lattice(c.volume(),13,1);leaf.layer="0";leaf.attrs.user["door"]="swing";shapes.push_back(leaf);
    });
    const auto original=c.shapes();shell.command("WALLS 200mm");
    CHECK(c.shapes().size()==original.size());
    for(std::size_t i=0;i<original.size();++i){CHECK(c.shapes()[i].key==original[i].key);CHECK(c.shapes()[i].a.x==original[i].a.x);CHECK(c.shapes()[i].b.y==original[i].b.y);}
    CHECK(c.wall_centerline_indices().empty());
    std::vector<gsnap::Shape> material;
    for(const auto& shape:c.shapes())if(shape.region)material.push_back(shape);
    const auto footprint=gregion::from_members(material);CHECK(footprint.ok);
    shell.command("WALLS 200mm 2m");
    CHECK(shell.modeller().bodies().size()==1);
    double volume=0;for(const auto& body:shell.modeller().bodies())volume+=gtopo::volume(body,c.volume());
    if(footprint.ok)CHECK(std::abs(volume-2*footprint.region.area(c.volume()))<1e-5);
}

TEST(walls_command_honors_selected_custom_layer_centerlines_in_2d_and_3d) {
    for(bool solid:{false,true}) {
        Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        c.modify_shapes([&](auto& shapes){
            gsnap::Shape center;center.a=gcm::to_lattice(c.volume(),0,0);center.b=gcm::to_lattice(c.volume(),4,0);center.layer="CUSTOM-WALL-AXES";shapes.push_back(center);
            gsnap::Shape door=center;door.a=gcm::to_lattice(c.volume(),10,0);door.b=gcm::to_lattice(c.volume(),11,0);door.layer="A-DOOR";shapes.push_back(door);
        });
        CHECK(c.wall_centerline_indices().empty());c.set_selection({0,1});CHECK(c.wall_centerline_indices().size()==1);
        shell.command(solid?"WALLS 200mm 2m":"WALLS 200mm");
        if(solid)CHECK(shell.modeller().bodies().size()==1);
        else {CHECK(std::any_of(c.shapes().begin(),c.shapes().end(),[](const auto& shape){return shape.region;}));CHECK(std::count_if(c.shapes().begin(),c.shapes().end(),[](const auto& shape){return shape.layer=="A-DOOR";})==1);}
    }
}

TEST(walls_3d_preserves_cut_door_opening_and_excludes_door_leaf) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(point(0,0),point(6,0),.2));
    CHECK(!opening_door(shell,{{"hinge","2,0"},{"latch","3,0"},{"framing","none"}}).contains("error"));
    c.clear_selection();
    CHECK(c.wall_centerline_indices().empty());
    const auto footprint=gregion::from_shapes(opening_walls(c));
    CHECK_MSG(footprint.ok,"%s",footprint.error.c_str());
    if(!footprint.ok)return;
    CHECK(!footprint.region.contains(point(2.5,0)));
    CHECK(footprint.region.contains(point(1,0)));
    shell.command("WALLS 200mm 2m");
    CHECK(!shell.modeller().bodies().empty());
    double volume=0;
    for(const auto& body:shell.modeller().bodies())volume+=gtopo::volume(body,c.volume());
    CHECK(std::abs(volume-2*footprint.region.area(c.volume()))<1e-5);
}

TEST(walls_3d_rejects_open_material_without_creating_partial_solids) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto point=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(point(0,0),point(4,0),.2));
    c.modify_shapes([&](auto& shapes) {
        gsnap::Shape broken;broken.a=point(10,0);broken.b=point(11,0);
        broken.layer="A-WALL";broken.attrs.user["sketch_object"]="wall";
        shapes.push_back(broken);
        gsnap::Shape centerline;centerline.a=point(20,0);centerline.b=point(21,0);
        shapes.push_back(centerline);
    });
    shell.build_frame();c.clear_selection();
    const auto before=c.shapes();
    shell.command("WALLS 200mm 2m");
    CHECK(shell.modeller().bodies().empty());
    CHECK(shell.modeller().message().find("wall boundary")!=std::string::npos);
    CHECK(c.shapes().size()==before.size());
    for(std::size_t i=0;i<before.size() && i<c.shapes().size();++i) {
        CHECK(c.shapes()[i].key==before[i].key);
        CHECK(c.shapes()[i].a.x==before[i].a.x && c.shapes()[i].a.y==before[i].a.y);
        CHECK(c.shapes()[i].b.x==before[i].b.x && c.shapes()[i].b.y==before[i].b.y);
    }
}

TEST(door_association_moves_live_repairs_old_opening_and_cancels_atomically) {
    for(double thickness:{.12,.45}) {
        Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
        CHECK(c.add_wall(p(0,0),p(8,0),thickness));
        CHECK(!opening_door(shell,{{"hinge","1,0"},{"latch","2,0"},{"framing","none"}}).contains("error"));
        std::size_t door=0;for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].kind==gsnap::ShapeKind::Arc)door=i;
        CHECK(c.shapes()[door].attrs.user.count("opening_material"));
        const auto group=c.shapes()[door].poly;std::vector<std::size_t> selected;
        for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].poly==group)selected.push_back(i);
        c.set_selection(selected);c.view().set_mapping(200,500,80);
        double x,y;c.view().screen_of(1,thickness*.5+.5,x,y);
        CHECK(c.begin_selection_drag(x,y));
        const auto check=[&](double old_x,double new_x) {
            const auto material=gregion::from_shapes(opening_walls(c));CHECK_MSG(material.ok,"%s",material.error.c_str());
            if(material.ok){CHECK(material.region.contains(p(old_x,0)));CHECK(!material.region.contains(p(new_x,0)));CHECK(std::abs(material.region.area(c.volume())-7*thickness)<1e-5);}
            CHECK(c.selection().size()==2);
        };
        c.update_selection_drag(x+160,y);check(1.5,3.5);
        c.update_selection_drag(x+240,y);check(3.5,4.5);
        c.cancel_selection_drag();check(4.5,1.5);
        CHECK(c.begin_selection_drag(x,y));c.update_selection_drag(x+160,y);CHECK(c.end_selection_drag());check(1.5,3.5);
        c.undo();check(3.5,1.5);c.redo();check(1.5,3.5);
        c.erase_selected();const auto healed=gregion::from_shapes(opening_walls(c));CHECK(healed.ok);
        if(healed.ok)CHECK(std::abs(healed.region.area(c.volume())-8*thickness)<1e-5);
    }
}

TEST(door_association_ungrouped_arc_does_not_shift_unrelated_loose_linework) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(p(0,0),p(8,0),.2));
    CHECK(!opening_door(shell,{{"hinge","1,0"},{"latch","2,0"},{"framing","none"}}).contains("error"));
    c.modify_shapes([&](auto& shapes) {
        for(auto& s:shapes)if(s.attrs.user.contains("door"))s.poly=0;
        gsnap::Shape line;line.a=p(20,5);line.b=p(21,5);shapes.push_back(line);
    });
    const auto line=c.shapes().back();
    std::size_t index=0;for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].attrs.user.contains("opening_auto"))index=i;
    const auto arc=c.shapes()[index];const double angle=(arc.start_angle+arc.end_angle)*.5;
    const gcm::Lattice2 hit{arc.a.x+gcm::i64(std::llround(arc.radius*std::cos(angle))),arc.a.y+gcm::i64(std::llround(arc.radius*std::sin(angle)))};
    double wx,wy,x,y;gcm::to_world(c.volume(),hit,wx,wy);c.view().set_mapping(200,500,80);c.view().screen_of(wx,wy,x,y);
    c.set_selection({index});c.set_tool(Tool::Select);CHECK(c.begin_selection_drag(x,y));c.update_selection_drag(x+160,y-4);
    const auto found=c.index_of_key(line.key);CHECK(found!=Canvas::npos);
    if(found!=Canvas::npos){CHECK(c.shapes()[found].a.x==line.a.x);CHECK(c.shapes()[found].a.y==line.a.y);}
    c.cancel_selection_drag();
}

TEST(door_association_attaches_standalone_auto_door_and_leaves_explicit_standalone_alone) {
    for(bool automatic:{true,false}) {
        Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
        CHECK(c.add_wall(p(0,0),p(8,0),.2));
        gai::Json args={{"hinge","1,2"},{"latch","2,2"},{"framing","none"}};
        if(!automatic)args["snap_to_wall"]=false;
        CHECK(!opening_door(shell,args).contains("error"));
        c.modify_shapes([&](auto& shapes){for(auto& s:shapes)if(s.layer=="A-DOOR") {const auto dy=p(0,-1.9).y-p(0,0).y;s.a.y+=dy;s.b.y+=dy;}});
        auto walls=gregion::from_shapes(opening_walls(c));CHECK(walls.ok);
        if(walls.ok)CHECK(walls.region.contains(p(1.5,0))!=automatic);
        c.modify_shapes([&](auto& shapes){for(auto& s:shapes)if(s.layer=="A-DOOR") {const auto dy=p(0,2).y-p(0,0).y;s.a.y+=dy;s.b.y+=dy;}});
        walls=gregion::from_shapes(opening_walls(c));CHECK(walls.ok);
        if(walls.ok)CHECK(std::abs(walls.region.area(c.volume())-1.6)<1e-5);
    }
}

TEST(door_association_keeps_other_openings_and_native_reopened_history) {
    Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(p(0,0),p(10,0),.2));
    CHECK(!opening_door(shell,{{"hinge","1,0"},{"latch","2,0"},{"framing","none"}}).contains("error"));
    CHECK(!opening_door(shell,{{"hinge","5,0"},{"latch","6,0"},{"framing","none"}}).contains("error"));
    const auto path=(std::filesystem::temp_directory_path()/("glcad-door-association-"+std::to_string(getpid())+".glcad")).string();
    CHECK(shell.save_file(path));shell.close_tab(0);
    const auto cwd=std::filesystem::current_path();Shell loaded;CHECK(loaded.open_file(path));std::filesystem::current_path(cwd);
    auto& restored=loaded.canvas();
    gcm::u32 group=0;
    for(const auto& s:restored.shapes())if(s.kind==gsnap::ShapeKind::Arc && s.a.x<p(3,0).x)group=s.poly;
    const auto dx=p(2,0).x-p(0,0).x;
    restored.modify_shapes([&](auto& shapes){for(auto& s:shapes)if(s.poly==group){s.a.x+=dx;s.b.x+=dx;}});
    loaded.sync_document();
    const auto verify=[&](double open,double filled) {
        const auto walls=gregion::from_shapes(opening_walls(restored));CHECK_MSG(walls.ok,"%s",walls.error.c_str());
        if(walls.ok){CHECK(walls.region.contains(p(filled,0)));CHECK(!walls.region.contains(p(open,0)));CHECK(!walls.region.contains(p(5.5,0)));CHECK(std::abs(walls.region.area(restored.volume())-1.6)<1e-5);}
    };
    verify(3.5,1.5);loaded.command("UNDO");verify(1.5,3.5);loaded.command("REDO");verify(3.5,1.5);
    loaded.close_tab(0);std::filesystem::remove(path);
}

TEST(door_association_rotated_overlapping_openings_keep_material_and_style) {
    for(double angle:{.7,1.5707963267948966}) {
        Shell shell;auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x*std::cos(angle)-y*std::sin(angle),x*std::sin(angle)+y*std::cos(angle));};
        const auto text=[&](double x){char out[128];std::snprintf(out,sizeof(out),"%.12f,%.12f",x*std::cos(angle),x*std::sin(angle));return std::string(out);};
        CHECK(c.add_wall(p(0,0),p(8,0),.2));
        c.modify_shapes([](auto& shapes){for(auto& shape:shapes)shape.attrs.color=gdoc::Color::rgb(20,60,90);});
        const auto first=opening_door(shell,{{"hinge",text(1)},{"latch",text(2)},{"framing","none"}});CHECK_MSG(!first.contains("error"),"%s",first.dump().c_str());
        gcm::u32 moving=0;for(const auto& s:c.shapes())if(s.kind==gsnap::ShapeKind::Arc)moving=s.poly;
        const auto second=opening_door(shell,{{"hinge",text(4)},{"latch",text(5)},{"framing","none"}});CHECK_MSG(!second.contains("error"),"%s",second.dump().c_str());
        for(const auto& shape:c.shapes())if(shape.kind==gsnap::ShapeKind::Arc)CHECK_MSG(shape.attrs.user.count("opening_material"),"angle=%g missing patch",angle);
        const auto shift=p(2.5,0),zero=p(0,0);
        c.modify_shapes([&](auto& shapes){for(auto& s:shapes)if(s.poly==moving){s.a.x+=shift.x-zero.x;s.a.y+=shift.y-zero.y;s.b.x+=shift.x-zero.x;s.b.y+=shift.y-zero.y;}});
        auto material=gregion::from_shapes(opening_walls(c));CHECK_MSG(material.ok,"%s",material.error.c_str());
        if(material.ok) {CHECK(material.region.contains(p(1.5,0)));CHECK(!material.region.contains(p(3.8,0)));CHECK(!material.region.contains(p(4.8,0)));CHECK(std::abs(material.region.area(c.volume())-1.3)<1e-5);}
        c.modify_shapes([&](auto& shapes){for(auto& s:shapes)if(s.poly==moving){s.a.x-=shift.x-zero.x;s.a.y-=shift.y-zero.y;s.b.x-=shift.x-zero.x;s.b.y-=shift.y-zero.y;}});
        material=gregion::from_shapes(opening_walls(c));CHECK(material.ok);
        if(material.ok){CHECK(!material.region.contains(p(1.5,0)));CHECK(!material.region.contains(p(4.5,0)));CHECK(material.region.contains(p(3.8,0)));CHECK(std::abs(material.region.area(c.volume())-1.2)<1e-5);}
        for(const auto& wall:opening_walls(c))CHECK(wall.attrs.color==gdoc::Color::rgb(20,60,90));
    }
}

TEST(door_association_shell_drag_is_one_undo_gesture_in_both_history_modes) {
    for(bool document_history:{false,true}) {
        Shell shell;shell.set_dpr(1);shell.set_viewport(1200,900);shell.set_document_undo(document_history);
        auto& c=shell.canvas();c.set_display_unit(gunit::Length::Metre);
        const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
        CHECK(c.add_wall(p(0,0),p(8,0),.2));shell.build_frame();
        CHECK(!opening_door(shell,{{"hinge","1,0"},{"latch","2,0"},{"framing","none"}}).contains("error"));
        shell.camera().set_target(gview::Point3::from(3,0,0));shell.fit_width(12);shell.build_frame();shell.build_frame();
        std::vector<std::size_t> selected;for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].layer=="A-DOOR")selected.push_back(i);
        c.set_selection(selected);const auto depth=shell.undo_depth();double x,y;c.view().screen_of(1,.6,x,y);const auto scale=c.view().scale();
        shell.pointer_move(x,y);shell.build_frame();shell.pointer_down(x,y);shell.build_frame();CHECK(c.selection_dragging());
        shell.pointer_move(x+scale,y);shell.build_frame();
        shell.pointer_move(x+2*scale,y);shell.build_frame();CHECK(shell.undo_depth()==depth);
        shell.pointer_up(x+2*scale,y);shell.build_frame();shell.build_frame();CHECK(shell.undo_depth()==depth+1);
        CHECK(c.begin_selection_drag(x+2*scale,y));c.update_selection_drag(x+3*scale,y);shell.build_frame();c.cancel_selection_drag();shell.build_frame();shell.build_frame();
        CHECK(shell.undo_depth()==depth+1);
        shell.command("UNDO");shell.build_frame();auto walls=gregion::from_shapes(opening_walls(c));CHECK(walls.ok);
        if(walls.ok){CHECK(!walls.region.contains(p(1.5,0)));CHECK(walls.region.contains(p(3.5,0)));}
        shell.command("REDO");shell.build_frame();walls=gregion::from_shapes(opening_walls(c));CHECK(walls.ok);
        if(walls.ok){CHECK(walls.region.contains(p(1.5,0)));CHECK(!walls.region.contains(p(3.5,0)));}
    }
}
