#include "test.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#include "exchange/mesh_export.hpp"
#include "exchange/step.hpp"
#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "ui/shell.hpp"
#include "ui/solids_file.hpp"

using namespace gcapp;

namespace {
Shell release_shell() {
    Shell s;
    s.set_dpr(1); s.set_viewport(1600, 1000);
    s.canvas().set_display_unit(gunit::Length::Metre);
    return s;
}

void rectangle(Shell& s, const std::array<const char*, 4>& points) {
    s.canvas().set_tool(Tool::Line);
    for (const auto* point : points) CHECK(s.canvas().type_point(point));
    CHECK(s.canvas().type_point(points[0]));
    s.canvas().cancel(); s.canvas().set_tool(Tool::Select);
}

void click_world(Shell& s, double x, double y, double z) {
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(x,y,z), px, py));
    s.build_frame(); s.pointer_move(px,py); s.build_frame();
    s.pointer_down(px,py); s.pointer_up(px,py); s.build_frame();
}

void click_control(Shell& s, const std::string& label, gui::Context::Role role) {
    s.build_frame();
    for (const auto& node : s.accessible_ui()) {
        if (node.label != label || node.role != role || node.disabled) continue;
        const auto& r = node.bounds;
        const double x=r.x+r.w*.5, y=r.y+r.h*.5;
        s.pointer_move(x,y); s.build_frame();
        s.pointer_down(x,y); s.build_frame(); s.pointer_up(x,y); s.build_frame();
        return;
    }
    CHECK_MSG(false, "missing control: %s (typed '%s', %s, %s)", label.c_str(),
              s.modeller().typed().c_str(),s.modeller().message().c_str(),s.modeller().value_error().c_str());
}

void extrude(Shell& s, double u, double v, const char* distance) {
    s.build_frame(); s.activate_tool(ToolId::Extrude); s.build_frame();
    double x,y,z; s.sketch_plane().to_world(u,v,x,y,z);
    click_world(s,x,y,z);
    for (const char* c=distance; *c; ++c) s.key_char(std::string(1,*c));
    s.build_frame();
    s.key_special(0); s.build_frame();
}

std::filesystem::path release_path(const char* name) {
    const char* configured = std::getenv("GLCAD_RELEASE_ARTIFACT_DIR");
    static const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto dir = configured ? std::filesystem::path(configured) :
        std::filesystem::temp_directory_path() / ("glcad-release-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir / name;
}

void check_solid(const Shell& s, double expected, double tolerance=1e-4) {
    CHECK(s.modeller().bodies().size() == 1);
    if (s.modeller().bodies().size() != 1) return;
    const auto& body=s.modeller().bodies()[0];
    const auto topology=body.analyse();
    CHECK(topology.closed); CHECK(topology.manifold); CHECK(topology.shells == 1);
    CHECK_MSG(std::fabs(gtopo::volume(body,s.canvas().volume())-expected)<tolerance,
              "volume %.12g, expected %.12g",gtopo::volume(body,s.canvas().volume()),expected);
}

void check_mesh(const std::vector<gmesh::Object>& objects, double expected_m3) {
    CHECK(!objects.empty());
    double volume=0;
    for (const auto& o : objects) {
        CHECK(o.triangle_count()>0);
        std::map<std::pair<gcm::u32,gcm::u32>,int> directed;
        for (std::size_t t=0;t+2<o.triangles.size();t+=3) {
            const double* a=&o.vertices[3*o.triangles[t]];
            const double* b=&o.vertices[3*o.triangles[t+1]];
            const double* c=&o.vertices[3*o.triangles[t+2]];
            volume+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+
                     a[2]*(b[0]*c[1]-b[1]*c[0]))/6;
            for (std::size_t k=0;k<3;++k) {
                const auto p=o.triangles[t+k], q=o.triangles[t+(k+1)%3];
                CHECK(p != q);
                ++directed[{p,q}];
            }
        }
        for (const auto& [edge,uses] : directed) {
            CHECK(uses==1);
            CHECK(directed.contains({edge.second,edge.first}));
        }
    }
    const double expected=expected_m3*1e9;
    CHECK_MSG(std::fabs(volume-expected)<1e-4*1e9,"mesh volume %.12g mm3 expected %.12g",volume,expected);
}

std::vector<gcm::u8> file_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void check_stl(const std::filesystem::path& path, double expected_volume) {
    const auto r=gmesh::read_stl(file_bytes(path));
    CHECK_MSG(r.ok,"%s",r.error.c_str());
    check_mesh(r.objects,expected_volume);
}

void check_3mf(const std::filesystem::path& path, double expected_volume) {
    const auto r=gmesh::read_3mf(file_bytes(path));
    CHECK_MSG(r.ok,"%s",r.error.c_str());
    CHECK(r.unit==gmesh::Unit::Millimetre);
    check_mesh(r.objects,expected_volume);
}
}

TEST(release_workflow_sketch_cut_face_edit_round_undo_reopen_and_export) {
    for (const bool circle : {false,true}) {
        Shell s=release_shell();
        rectangle(s,{"0,0","8,0","8,5","0,5"});
        extrude(s,4,2.5,"3"); check_solid(s,120);
        s.camera().look_plan(); s.build_frame();
        s.command("SKETCH"); s.build_frame(); click_world(s,4,2.5,3);
        CHECK(s.active_sketch()==1);
        if (circle) {
            s.canvas().set_tool(Tool::Circle);
            CHECK(s.canvas().type_point("0,0")); CHECK(s.canvas().type_point("1,0"));
            s.canvas().set_tool(Tool::Select);
        } else rectangle(s,{"-1,-1","1,-1","1,1","-1,1"});
        extrude(s,0,0,"-0.5");
        const double area=2*(120-gtopo::volume(s.modeller().bodies()[0],s.canvas().volume()));
        CHECK(circle ? (area>3.1 && area<3.15) : std::fabs(area-4)<1e-4);
        CHECK(s.modeller().history(0).size()==2);
        CHECK(!s.sketch_visible(1)); CHECK(s.sketch_parent(1)==0);
        double height=2.5;
        for (const double amount : {1.137,-1.813,0.426}) {
            s.camera().look_plan(); s.build_frame(); click_world(s,4.1,2.6,height);
            CHECK(s.modeller().target().valid && s.modeller().target().source<0);
            const auto solid=encode_solids(s.modeller().bodies());
            const auto history=encode_feature_history(s.modeller().histories());
            click_control(s,"Face offset",gui::Context::Role::Field);
            s.key_char(std::to_string(amount)); s.build_frame();
            CHECK(!s.modeller().gpu_preview().tool.empty());
            CHECK(encode_solids(s.modeller().bodies())==solid);
            CHECK(encode_feature_history(s.modeller().histories())==history);
            click_control(s,"OK",gui::Context::Role::Button);
            height+=amount;
            check_solid(s,120+area*(height-3));
            CHECK(s.modeller().history(0).size()==2);
        }
        const auto before_round=encode_solids(s.modeller().bodies());
        const auto before_history=encode_feature_history(s.modeller().histories());
        const double before_volume=gtopo::volume(s.modeller().bodies()[0],s.canvas().volume());
        s.camera().look(gview::StandardView::Iso); s.build_frame();
        s.activate_tool(ToolId::FilletEdge); s.build_frame(); click_world(s,4,0,3);
        click_control(s,"Round radius",gui::Context::Role::Field);
        s.key_char(".25"); s.build_frame(); click_control(s,"OK",gui::Context::Role::Button);
        CHECK(s.modeller().history(0).size()==3);
        CHECK(s.modeller().bodies()[0].has_surfaces());
        const double final_volume=gtopo::volume(s.modeller().bodies()[0],s.canvas().volume());
        CHECK(final_volume<before_volume); check_solid(s,final_volume);
        const auto final_solid=encode_solids(s.modeller().bodies());
        const auto final_history=encode_feature_history(s.modeller().histories());
        s.command("UNDO"); s.build_frame();
        CHECK(encode_solids(s.modeller().bodies())==before_round);
        CHECK(encode_feature_history(s.modeller().histories())==before_history);
        s.command("REDO"); s.build_frame();
        CHECK(encode_solids(s.modeller().bodies())==final_solid);
        CHECK(encode_feature_history(s.modeller().histories())==final_history);

        const auto native=release_path(circle ? "circle-part.glcad" : "square-part.glcad");
        const double exact_volume=before_volume-8*.25*.25*(1-std::acos(-1.0)/4);
        auto manifest=native; manifest.replace_extension("json");
        std::ofstream expected(manifest);
        expected << std::setprecision(17) << "{\"mesh_volume_m3\":" << final_volume
                 << ",\"exact_volume_m3\":" << exact_volume
                 << ",\"profile\":\"" << (circle ? "circle" : "square") << "\"}\n";
        expected.close();
        CHECK(s.save_file(native.string()));
        Shell reopened=release_shell(); CHECK(reopened.open_file(native.string())); reopened.build_frame();
        CHECK(encode_solids(reopened.modeller().bodies())==final_solid);
        CHECK(encode_feature_history(reopened.modeller().histories())==final_history);
        const auto serial=reopened.modeller().edit_serial();
        CHECK(!reopened.set_feature_value(0,0,6));
        CHECK(reopened.modeller().edit_serial()==serial);
        CHECK(encode_solids(reopened.modeller().bodies())==final_solid);
        CHECK(encode_feature_history(reopened.modeller().histories())==final_history);
        CHECK(!reopened.sketch_visible(1)); CHECK(reopened.sketch_parent(1)==0);
        CHECK(!reopened.set_feature_value(0,2,1000));
        CHECK(reopened.modeller().edit_serial()==serial);
        CHECK(encode_solids(reopened.modeller().bodies())==final_solid);
        CHECK(encode_feature_history(reopened.modeller().histories())==final_history);
        for (const char* extension : {"step","stl","3mf"}) {
            auto file=native; file.replace_extension(extension);
            CHECK(reopened.export_file(file.string()));
            if (std::string(extension)=="stl") check_stl(file,final_volume);
            else if (std::string(extension)=="3mf") check_3mf(file,final_volume);
            else {
                Shell imported=release_shell(); CHECK(imported.import_file(file.string(),false));
                check_solid(imported,exact_volume,1e-3);
            }
        }
        reopened.edit_sketch(1); reopened.build_frame();
        for (const char* extension : {"dxf","pdf"}) {
            auto file=native; file.replace_extension(extension);
            CHECK(reopened.export_file(file.string()));
            CHECK(std::filesystem::file_size(file)>100);
        }
    }
}

TEST(release_workflow_dimensioned_sheet_reopens_and_exports_at_known_scale) {
    Shell s=release_shell();
    std::string error; CHECK_MSG(s.load_font(&error),"%s",error.c_str());
    gdoc::Layer outline; outline.name="ReleaseOutline"; outline.lineweight=gdoc::Lineweight::W050;
    s.layers().add(outline); s.layers().set_current(outline.name); s.build_frame();
    rectangle(s,{"0,0","8,0","8,5","0,5"});
    s.canvas().set_tool(Tool::Circle);
    CHECK(s.canvas().type_point("4,2.5")); CHECK(s.canvas().type_point("5,2.5"));
    s.canvas().set_tool(Tool::Select); s.build_frame();
    gdim::Dimension dimension;
    dimension.kind=gdim::Kind::Aligned; dimension.layer=outline.name;
    const auto& v=s.canvas().volume();
    dimension.p1=gcm::to_lattice(v,0,0); dimension.p2=gcm::to_lattice(v,8,0);
    dimension.p3=gcm::to_lattice(v,4,-.5);
    dimension.o1={s.canvas().shapes()[0].key,gdim::Dimension::Anchor::Start};
    dimension.o2={s.canvas().shapes()[0].key,gdim::Dimension::Anchor::End};
    s.canvas().add_dimensions({dimension});
    s.canvas().add_text(gcm::to_lattice(v,4,5.5),"Release drawing",.15);
    gdoc::Layer revision; revision.name="Revision";
    s.layers().add(revision); s.layers().set_current(revision.name); s.build_frame();
    rectangle(s,{"10,0","11,0","11,1","10,1"});
    s.layers().set_current(outline.name); s.build_frame();
    CHECK(s.canvas().hatch_at_world(gcm::to_lattice(v,1,1),"ANSI31",.25,45));
    s.layers().set_current(revision.name); s.build_frame();
    s.canvas().set_tool(Tool::Line);
    CHECK(s.canvas().type_point("1,3")); CHECK(s.canvas().type_point("2,3"));
    s.canvas().cancel(); s.canvas().set_tool(Tool::Select);
    s.canvas().modify_shapes([&](auto& shapes) {
        for (auto& shape:shapes)
            if (shape.kind==gsnap::ShapeKind::Circle)
                shape.radius=gcm::to_lattice(v,1.25,0).x;
    });
    s.command("PLOTSCALE 1:50"); s.command("PAGESETUP A3 landscape");
    s.enter_drawing_mode(); s.build_frame();
    const auto native=release_path("dimensioned-sheet.glcad"); CHECK(s.save_file(native.string()));
    Shell reopened=release_shell(); CHECK(reopened.load_font(&error));
    CHECK(reopened.open_file(native.string())); reopened.build_frame();
    CHECK(reopened.canvas().dimensions().size()==1);
    CHECK(reopened.canvas().hatches().size()==1);
    double denominator=0,cx=0,cy=0; CHECK(reopened.plot_den(denominator,cx,cy));
    CHECK(denominator==50);
    for (const char* extension:{"dxf","pdf"}) {
        auto path=native; path.replace_extension(extension); CHECK(reopened.export_file(path.string()));
        if (std::string(extension)=="dxf") {
            Shell imported=release_shell(); CHECK(imported.import_file(path.string(),false));
            CHECK(imported.canvas().shapes().size()==10);
            CHECK(imported.canvas().hatches().size()==1);
            if (!imported.canvas().hatches().empty())
                CHECK(imported.canvas().hatches()[0].boundary_geometry.size()==5);
        }
    }
}

TEST(release_workflow_step_units_preserve_the_same_one_by_two_by_three_metre_solid) {
    const gcm::WorkingVolume volume{};
    gtopo::Profile profile;
    profile.outer={gcm::to_lattice(volume,0,0),gcm::to_lattice(volume,1,0),
                   gcm::to_lattice(volume,1,2),gcm::to_lattice(volume,0,2)};
    profile.normalize();
    const auto body=gtopo::extrude(profile,volume,ggeo::d3::Frame<gcm::F64>::world_xy(),3);
    CHECK(body.analyse().closed); CHECK(body.analyse().manifold);
    const std::array<std::pair<const char*,gstep::LengthUnit>,5> units{{
        {"metre",gstep::LengthUnit::Metre},{"millimetre",gstep::LengthUnit::Millimetre},
        {"centimetre",gstep::LengthUnit::Centimetre},{"inch",gstep::LengthUnit::Inch},
        {"foot",gstep::LengthUnit::Foot}}};
    for (const auto& [name,unit] : units) {
        gstep::WriteOptions options; options.unit=unit;
        const auto encoded=gstep::write(body,volume,options); CHECK(!encoded.empty());
        const auto path=release_path((std::string("units-")+name+".step").c_str());
        std::ofstream file(path); file << encoded; file.close(); CHECK(bool(file));
        const auto restored=gstep::read(encoded,volume); CHECK(restored.ok);
        CHECK(restored.bodies.size()==1);
        if (!restored.bodies.empty()) CHECK(std::fabs(gtopo::volume(restored.bodies[0],volume)-6)<1e-6);
    }
}

TEST(release_workflow_incoming_dxf_edit_native_save_export_and_reopen) {
    const auto fixture=std::filesystem::path(__FILE__).parent_path()/"fixtures/incoming/plate.dxf";
    Shell s=release_shell(); std::string error; CHECK(s.load_font(&error));
    CHECK(s.import_file(fixture.string(),false)); s.build_frame();
    CHECK_MSG(s.canvas().shapes().size()==5,"imported %zu shapes",s.canvas().shapes().size());
    CHECK(s.canvas().dimensions().size()==1);
    CHECK(s.canvas().hatches().size()==1);
    CHECK(s.canvas().texts().size()==1);
    CHECK(s.layers().find("IncomingOutline")!=nullptr);
    if (const auto* layer=s.layers().find("IncomingOutline"))
        CHECK(layer->lineweight==gdoc::Lineweight::W050);
    gdoc::Layer revision; revision.name="Revision"; revision.lineweight=gdoc::Lineweight::W035;
    s.layers().add(revision); s.layers().set_current(revision.name); s.build_frame();
    s.canvas().set_tool(Tool::Line);
    CHECK(s.canvas().type_point("1,1")); CHECK(s.canvas().type_point("2,1"));
    s.canvas().cancel(); s.canvas().set_tool(Tool::Select);
    CHECK(s.canvas().shapes().size()==6);
    s.command("PLOTSCALE 1:50"); s.command("PAGESETUP A3 landscape");
    const auto native=release_path("incoming-drawing.glcad"); CHECK(s.save_file(native.string()));
    Shell reopened=release_shell(); CHECK(reopened.load_font(&error));
    CHECK(reopened.open_file(native.string())); reopened.build_frame();
    CHECK(reopened.canvas().shapes().size()==6);
    CHECK(reopened.canvas().dimensions().size()==1);
    CHECK(reopened.canvas().hatches().size()==1);
    CHECK(reopened.canvas().texts().size()==1);
    if (!reopened.canvas().hatches().empty()) {
        const auto& hatch=reopened.canvas().hatches()[0];
        CHECK(hatch.valid && !hatch.orphaned);
        CHECK(hatch.cached.fill_loops.size()==2);
        CHECK(hatch.boundary_geometry.size()==5);
    }
    for (const char* extension:{"dxf","pdf"}) {
        auto path=native; path.replace_extension(extension);
        CHECK_MSG(reopened.export_file(path.string()),"%s",reopened.log().empty()?"export failed":reopened.log().back().c_str());
        if (std::string(extension)=="dxf") {
            Shell incoming=release_shell(); CHECK(incoming.import_file(path.string(),false));
            CHECK(incoming.canvas().shapes().size()==6);
            CHECK(incoming.canvas().dimensions().size()==1);
            CHECK(incoming.canvas().hatches().size()==1);
            CHECK(incoming.canvas().texts().size()==1);
        }
    }
}

TEST(release_workflow_incoming_step_face_edit_native_save_export_and_reopen) {
    const auto fixture=std::filesystem::path(__FILE__).parent_path()/"fixtures/incoming/plate.step";
    Shell s=release_shell(); CHECK(s.import_file(fixture.string(),false)); s.build_frame();
    const double area=40-std::acos(-1.0);
    check_solid(s,area*3,.003);
    s.camera().look_plan(); s.build_frame(); click_world(s,1,1,3);
    CHECK(s.modeller().target().valid);
    click_control(s,"Face offset",gui::Context::Role::Field);
    s.key_char(".25"); s.build_frame();
    CHECK(!s.modeller().gpu_preview().tool.empty());
    click_control(s,"OK",gui::Context::Role::Button);
    check_solid(s,area*3.25,.003);
    const auto saved=encode_solids(s.modeller().bodies());
    const auto native=release_path("incoming-part.glcad"); CHECK(s.save_file(native.string()));
    Shell reopened=release_shell(); CHECK(reopened.open_file(native.string())); reopened.build_frame();
    CHECK(encode_solids(reopened.modeller().bodies())==saved);
    check_solid(reopened,area*3.25,.003);
    for (const char* extension:{"step","stl","3mf"}) {
        auto path=native; path.replace_extension(extension); CHECK(reopened.export_file(path.string()));
        if (std::string(extension)=="step") {
            Shell incoming=release_shell(); CHECK(incoming.import_file(path.string(),false));
            check_solid(incoming,area*3.25,.005);
        } else if (!reopened.modeller().bodies().empty()) {
            const double v=gtopo::volume(reopened.modeller().bodies()[0],reopened.canvas().volume());
            if (std::string(extension)=="stl") check_stl(path,v);
            else check_3mf(path,v);
        }
    }
}
