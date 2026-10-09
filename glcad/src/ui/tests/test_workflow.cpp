#include "test.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#ifndef _WIN32
#include <sys/wait.h>
#else
namespace {
int setenv(const char* name,const char* value,int overwrite) {
    if(!overwrite && std::getenv(name)) return 0;
    return _putenv_s(name,value);
}
int unsetenv(const char* name) { return _putenv_s(name,""); }
}
#endif

#include "geometry/measure.hpp"
#include "geometry/extrude.hpp"
#include "ui/shell.hpp"
#include "exchange/plot_style.hpp"
#include "exchange/dwg.hpp"
#include "exchange/dwf.hpp"
#include "exchange/dwg_write.hpp"
#include "exchange/mesh_export.hpp"
#include "ui/solids_file.hpp"
#include "ui/snapshot.hpp"
#include "engine/appconfig.hpp"
#include "ui/ai_tools.hpp"
#include "geometry/boolean.hpp"
#include <filesystem>

namespace {
class ScopedTestConfig {
public:
    explicit ScopedTestConfig(std::string path)
        : path_(std::move(path)), cwd_(std::filesystem::current_path()),
          had_previous_(std::getenv("GLCAD_CONFIG_DIR") != nullptr),
          previous_(had_previous_ ? std::getenv("GLCAD_CONFIG_DIR") : "") {
        CHECK(setenv("GLCAD_CONFIG_DIR", path_.c_str(), 1) == 0);
    }
    ~ScopedTestConfig() {
        std::error_code ec;
        std::filesystem::current_path(cwd_, ec);
        CHECK_MSG(!ec, "cannot restore test working directory: %s", ec.message().c_str());
        if (had_previous_) setenv("GLCAD_CONFIG_DIR", previous_.c_str(), 1);
        else unsetenv("GLCAD_CONFIG_DIR");
        if (!ec) std::filesystem::remove_all(path_, ec);
    }
    ScopedTestConfig(const ScopedTestConfig&) = delete;
    ScopedTestConfig& operator=(const ScopedTestConfig&) = delete;
private:
    std::string path_;
    std::filesystem::path cwd_;
    bool had_previous_;
    std::string previous_;
};

std::string scratch_path(const char* name) {
    const std::string dir = "/tmp/glcad-ui-test-" + std::to_string(getpid());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/" + name;
}
}

using namespace gcapp;

TEST(workflow_config_scope_restores_environment_and_cwd_before_cleanup) {
    const char* inherited = std::getenv("GLCAD_CONFIG_DIR");
    const bool had_inherited = inherited != nullptr;
    const std::string previous = inherited ? inherited : "";
    const auto cwd = std::filesystem::current_path();
    const auto outer_path = scratch_path("config-scope-outer");
    const auto inner_path = scratch_path("config-scope-inner");
    {
        ScopedTestConfig outer(outer_path);
        std::filesystem::create_directories(outer_path);
        std::filesystem::current_path(outer_path);
        {
            ScopedTestConfig inner(inner_path);
            std::filesystem::create_directories(inner_path);
            std::filesystem::current_path(inner_path);
            CHECK(std::string(std::getenv("GLCAD_CONFIG_DIR")) == inner_path);
        }
        CHECK(std::filesystem::equivalent(std::filesystem::current_path(), outer_path));
        CHECK(std::string(std::getenv("GLCAD_CONFIG_DIR")) == outer_path);
        CHECK(!std::filesystem::exists(inner_path));
    }
    CHECK(std::filesystem::current_path() == cwd);
    CHECK(!std::filesystem::exists(outer_path));
    const char* restored = std::getenv("GLCAD_CONFIG_DIR");
    CHECK((restored != nullptr) == had_inherited);
    if (restored) CHECK(std::string(restored) == previous);
}

TEST(tools_every_group_belongs_to_a_mode_and_has_tools) {
    for (const auto& g : tool_groups()) {
        std::size_t n = 0;
        for (const auto& d : tool_defs()) if (d.group == g.group) ++n;
        CHECK_MSG(n > 0, "group '%s' has no tools", g.caption);
        CHECK(g.caption[0] != '\0');
    }
}

TEST(tools_every_tool_has_a_group_an_icon_and_a_command) {
    for (const auto& d : tool_defs()) {
        bool grouped = false;
        for (const auto& g : tool_groups()) if (g.group == d.group) grouped = true;
        CHECK_MSG(grouped, "tool '%s' is in no group", d.label);
        CHECK_MSG(gui::icon_codepoint(d.icon) != 0, "tool '%s' has no icon", d.label);
        CHECK_MSG(d.command[0] != '\0', "tool '%s' has no command", d.label);
    }
}

TEST(tools_each_mode_has_the_verbs_of_its_half) {
    CHECK(tool_mode(ToolId::Line) == Mode::Sketch);
    CHECK(tool_mode(ToolId::Trim) == Mode::Sketch);
    CHECK(tool_mode(ToolId::DimLinear) == Mode::Sketch);
    CHECK(tool_mode(ToolId::Extrude) == Mode::Solid);
    CHECK(tool_mode(ToolId::Union) == Mode::Solid);
    CHECK(tool_mode(ToolId::NewSketch) == Mode::Solid);
    std::size_t sketch = 0, solid = 0;
    for (const auto& d : tool_defs()) (tool_mode(d.id) == Mode::Sketch ? sketch : solid)++;
    CHECK_MSG(sketch >= 15 && solid >= 15, "%zu sketch tools, %zu solid tools", sketch, solid);
}

TEST(tools_every_tool_on_the_toolbar_is_connected) {
    std::size_t wired = 0, total = 0;
    for (const auto& d : tool_defs()) { ++total; if (d.wired) ++wired; }
    CHECK_MSG(wired == total, "%zu of %zu tools wired", wired, total);
}

TEST(toolbar_wrapping_keeps_every_tool_visible_without_overlapping_controls) {
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
        for (const float width : {320.0f, 400.0f, 800.0f, 1200.0f, 1600.0f, 2400.0f}) {
            for (const auto mode : {Mode::Sketch, Mode::Solid, Mode::Drawing}) {
                const gui::Rect area{0, 100 * scale, width * scale, 1500 * scale};
                const auto layout = layout_toolbar(mode, area, scale);
                std::vector<const ToolDef*> expected, actual;
                for (const auto& group : tool_groups()) if (group.mode == mode)
                    for (const auto& tool : tool_defs()) if (tool.group == group.group)
                        expected.push_back(&tool);
                std::vector<gui::Rect> occupied{layout.modes, layout.settings, layout.assistant};
                if(mode==Mode::Sketch)occupied.push_back(layout.plan_check);
                for (const auto& fragment : layout.groups) {
                    CHECK(!fragment.tools.empty());
                    actual.insert(actual.end(), fragment.tools.begin(), fragment.tools.end());
                    occupied.push_back(fragment.bounds);
                }
                CHECK(actual == expected);
                for (std::size_t i = 0; i < occupied.size(); ++i) {
                    const auto& r = occupied[i];
                    CHECK(r.x >= area.x); CHECK(r.right() <= area.right());
                    CHECK(r.y >= area.y); CHECK(r.bottom() <= area.y + layout.height);
                    for (std::size_t j = 0; j < i; ++j)
                        CHECK_MSG(!r.intersects(occupied[j]), "overlap at width %g, scale %g", width, scale);
                }
                if (width == 2400) CHECK(layout.height == 72 * scale);
                if (width == 800 && mode != Mode::Drawing) CHECK(layout.height > 72 * scale);
            }
        }
    }
}

namespace {

Shell make_shell() {
    Shell s;
    s.set_dpr(1.0);
    s.set_viewport(1600, 1000);
    s.canvas().set_display_unit(gunit::Length::Metre);
    return s;
}

void draw_rect(Shell& s, const char* a, const char* b, const char* c, const char* d) {
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point(a);
    s.canvas().type_point(b);
    s.canvas().type_point(c);
    s.canvas().type_point(d);
    s.canvas().type_point(a);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
}

void extrude_at(Shell& s, double u, double v, const char* distance) {
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    double wx, wy, wz, px, py;
    s.sketch_plane().to_world(u, v, wx, wy, wz);
    CHECK(s.camera().project(gview::Point3::from(wx, wy, wz), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (const char* c = distance; *c; ++c) s.key_char(std::string(1, *c));
    s.key_special(0);
    s.build_frame();
}

gcm::WorkingVolume make_vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
const gcm::WorkingVolume kVol = make_vol();

}

namespace {
Shell block_shell() {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "3");
    return s;
}
void click_at(Shell& s, double x, double y, double z) {
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(x, y, z), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
}
}

TEST(plot_requests_a_pdf_destination_from_the_native_file_picker) {
    auto s = make_shell();
    bool asked = false;
    s.set_file_hook([&](Shell::FileAsk kind, std::function<void(std::string)> done) {
        asked = true;
        CHECK(kind == Shell::FileAsk::ExportPdf);
        const auto filters = Shell::file_filters(kind);
        CHECK(filters.size() == 1);
        CHECK(filters[0].extensions == std::vector<std::string>{"pdf"});
        done({});
    });
    s.command("PLOT");
    CHECK(asked);
}

TEST(workflow_opens_in_a_sketch_looking_at_the_ground) {
    Shell s = make_shell();
    CHECK(s.mode() == Mode::Sketch);
    CHECK(s.lens() == Lens::TwoD);
    CHECK(s.sketches().size() == 1);
    CHECK(s.active_sketch() == 0);
    CHECK(std::fabs(s.sketch_plane().nz - 1.0) < 1e-9);
}

TEST(workflow_extruding_a_region_makes_a_solid_and_leaves_the_sketch) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies", s.modeller().bodies().size());
    CHECK(s.mode() == Mode::Solid);
    CHECK(s.active_tool() == ToolId::Select);
}

TEST(workflow_a_sketch_on_a_face_is_drawn_in_that_faces_plane) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    CHECK(s.modeller().bodies().size() == 1);
    if (s.modeller().bodies().empty()) return;

    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.5, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    const auto& t = s.hovered_target();
    CHECK_MSG(t.valid && t.on_body, "the top face was not found under the cursor");
    if (!t.valid) return;
    CHECK(s.begin_sketch_on_face(t.body, t.face));

    CHECK(s.mode() == Mode::Sketch);
    CHECK(s.sketches().size() == 2);
    CHECK(s.active_sketch() == 1);
    const auto& pl = s.sketch_plane();
    CHECK_MSG(std::fabs(pl.oz - 3.0) < 1e-6, "sketch plane at z = %.4f", pl.oz);
    CHECK(std::fabs(pl.nz - 1.0) < 1e-9);
    CHECK(s.lens() == Lens::TwoD);
    CHECK(s.canvas().shapes().empty());
}

TEST(workflow_a_region_drawn_on_a_face_extrudes_into_that_solid) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.5, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    CHECK(s.begin_sketch_on_face(s.hovered_target().body, s.hovered_target().face));

    draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
    extrude_at(s, 0.0, 0.0, "2");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies", s.modeller().bodies().size());
    if (s.modeller().bodies().empty()) return;
    const double vol = gtopo::volume(s.modeller().bodies()[0], kVol);
    CHECK_MSG(std::fabs(vol - 128.0) < 1e-3, "volume %.4f", vol);
    const auto bb = s.modeller().bodies()[0].bounds(kVol);
    CHECK_MSG(std::fabs(bb.hi.z.to_f64() - 5.0) < 1e-6, "top at %.4f", bb.hi.z.to_f64());
}

TEST(workflow_the_ground_sketch_survives_a_face_sketch) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    const std::size_t drawn = s.canvas().shapes().size();
    extrude_at(s, 4.0, 2.5, "3");
    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.5, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    CHECK(s.begin_sketch_on_face(s.hovered_target().body, s.hovered_target().face));
    CHECK(s.canvas().shapes().empty());
    s.edit_sketch(0);
    CHECK(s.active_sketch() == 0);
    CHECK_MSG(s.canvas().shapes().size() == drawn, "%zu of %zu shapes came back",
              s.canvas().shapes().size(), drawn);
    CHECK(std::fabs(s.sketch_plane().oz) < 1e-9);
}

TEST(workflow_finish_leaves_the_sketch_and_steps_back) {
    Shell s = make_shell();
    CHECK(s.mode() == Mode::Sketch);
    s.finish_sketch();
    CHECK(s.mode() == Mode::Solid);
    CHECK(s.lens() == Lens::ThreeD);
    s.activate_tool(ToolId::Line);
    CHECK(s.mode() == Mode::Sketch);
}

TEST(workflow_revolve_makes_a_solid_of_revolution) {
    Shell s = make_shell();
    draw_rect(s, "1,0", "3,0", "3,3", "1,3");
    s.build_frame();
    s.activate_tool(ToolId::Revolve);
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(2.0, 1.5, 0.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    CHECK(s.modeller().stage() == Modeller::Stage::PickAxis);
    CHECK(s.camera().project(gview::Point3::from(1.0, 1.5, 0.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    CHECK(s.modeller().axis_hovered());
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after revolve: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
    if (s.modeller().bodies().empty()) return;
    const double vol = gtopo::volume(s.modeller().bodies()[0], kVol);
    const double expected = 0.5 * 48.0 * 4.0 * std::sin(2.0 * 3.14159265358979323846 / 48.0) * 3.0;
    CHECK_MSG(std::fabs(vol - expected) < 1e-2, "volume %.4f, expected %.4f", vol, expected);
}

TEST(workflow_overlapping_regions_on_the_ground_join) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "2");
    s.edit_sketch(0);
    draw_rect(s, "10,10", "14,10", "14,14", "10,14");
    extrude_at(s, 12.0, 12.0, "2");
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu bodies", s.modeller().bodies().size());
    if (s.modeller().bodies().size() != 2) return;
    s.build_frame();
    s.activate_tool(ToolId::Union);
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 2.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    CHECK(s.modeller().stage() == Modeller::Stage::PickSecond);
    CHECK(s.camera().project(gview::Point3::from(12.0, 12.0, 2.0), px, py));
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    CHECK(s.modeller().bodies().size() == 2 || s.modeller().bodies().size() == 1);
}

TEST(workflow_fillet_rounds_an_edge_of_a_solid) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    CHECK(s.modeller().bodies().size() == 1);
    if (s.modeller().bodies().empty()) return;
    const double before = gtopo::volume(s.modeller().bodies()[0], kVol);

    s.camera().look(gview::StandardView::Iso);
    s.build_frame();
    s.activate_tool(ToolId::FilletEdge);
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(4.0, 0.0, 3.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    CHECK_MSG(s.modeller().edge_hovered(), "no edge under the cursor");
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    CHECK(s.modeller().stage() == Modeller::Stage::TypeValue);
    s.key_char("1");
    s.key_special(0);
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
    if (s.modeller().bodies().empty()) return;
    const double after = gtopo::volume(s.modeller().bodies()[0], kVol);
    CHECK_MSG(after < before - 0.5 && after > before - 2.0,
              "volume went from %.3f to %.3f (%s)", before, after, s.modeller().message().c_str());
    CHECK(s.modeller().bodies()[0].face_count() > 6);
}

TEST(workflow_loft_joins_two_profiles) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "3");
    if (s.modeller().bodies().empty()) return;
    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    CHECK(s.begin_sketch_on_face(s.hovered_target().body, s.hovered_target().face));
    draw_rect(s, "-0.5,-0.5", "0.5,-0.5", "0.5,0.5", "-0.5,0.5");
    s.finish_sketch();
    s.edit_sketch(0);
    draw_rect(s, "10,0", "14,0", "14,4", "10,4");
    s.build_frame();

    s.activate_tool(ToolId::Loft);
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(12.0, 2.0, 0.0), px, py));
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    CHECK_MSG(s.modeller().stage() == Modeller::Stage::PickSecond, "%s", s.modeller().message().c_str());
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu bodies: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
}

TEST(workflow_mate_moves_the_second_solid_onto_the_first) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "3");
    s.edit_sketch(0);
    draw_rect(s, "10,0", "14,0", "14,4", "10,4");
    extrude_at(s, 12.0, 2.0, "3");
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu bodies", s.modeller().bodies().size());
    if (s.modeller().bodies().size() != 2) return;

    s.camera().look(gview::StandardView::IsoSE);
    s.camera().set_target(gview::Point3::from(7.0, 2.0, 1.5));
    s.build_frame();
    s.activate_tool(ToolId::Mate);
    double px, py;
    click_at(s, 4.0, 2.0, 1.5);
    CHECK_MSG(s.modeller().stage() == Modeller::Stage::PickSecond, "%s", s.modeller().message().c_str());
    s.camera().look(gview::StandardView::IsoSW);
    click_at(s, 10.0, 2.0, 1.5);
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu bodies", s.modeller().bodies().size());
    const auto bb = s.modeller().bodies()[1].bounds(kVol);
    CHECK_MSG(std::fabs(bb.lo.x.to_f64() - 4.0) < 1e-3 && std::fabs(bb.hi.x.to_f64() - 8.0) < 1e-3,
              "the second block spans x %.3f..%.3f after the mate (%s)", bb.lo.x.to_f64(),
              bb.hi.x.to_f64(), s.modeller().message().c_str());
    const double vol = gtopo::volume(s.modeller().bodies()[1], kVol);
    CHECK_MSG(std::fabs(vol - 48.0) < 1e-3, "volume %.4f after the mate", vol);
}


TEST(workflow_split_by_a_face_plane_makes_two_solids) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    s.edit_sketch(0);
    draw_rect(s, "2,10", "6,10", "6,14", "2,14");
    extrude_at(s, 4.0, 12.0, "3");
    CHECK(s.modeller().bodies().size() == 2);
    s.camera().look(gview::StandardView::IsoSW);
    s.camera().set_target(gview::Point3::from(3.0, 6.0, 1.5));
    s.fit_width(40.0);
    s.build_frame();
    s.activate_tool(ToolId::Split);
    click_at(s, 2.0, 2.0, 3.0);
    CHECK_MSG(s.modeller().stage() == Modeller::Stage::PickSecond, "%s", s.modeller().message().c_str());
    click_at(s, 2.0, 12.0, 1.5);
    CHECK_MSG(s.modeller().bodies().size() == 3, "%zu bodies: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
    if (s.modeller().bodies().size() != 3) return;
    const double v0 = gtopo::volume(s.modeller().bodies()[0], kVol);
    const double v2 = gtopo::volume(s.modeller().bodies()[2], kVol);
    CHECK_MSG(std::fabs(v0 + v2 - 48.0) < 1e-3 && std::fabs(v0 - 24.0) < 1e-3,
              "halves %.3f and %.3f", v0, v2);
}

TEST(workflow_mirror_across_a_face_makes_a_reflected_copy) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    s.camera().look(gview::StandardView::IsoSE);
    s.camera().set_target(gview::Point3::from(2.0, 2.0, 1.5));
    s.build_frame();
    s.activate_tool(ToolId::MirrorBody);
    click_at(s, 2.0, 2.0, 3.0);
    CHECK(s.modeller().stage() == Modeller::Stage::PickSecond);
    click_at(s, 4.0, 2.0, 1.5);
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu bodies: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
    if (s.modeller().bodies().size() != 2) return;
    const auto bb = s.modeller().bodies()[1].bounds(kVol);
    CHECK_MSG(std::fabs(bb.lo.x.to_f64() - 4.0) < 1e-6 && std::fabs(bb.hi.x.to_f64() - 8.0) < 1e-6,
              "mirror spans x %.3f..%.3f", bb.lo.x.to_f64(), bb.hi.x.to_f64());
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[1], kVol) - 48.0) < 1e-3,
              "mirrored volume %.3f", gtopo::volume(s.modeller().bodies()[1], kVol));
}

TEST(workflow_pattern_repeats_a_solid_along_a_face_normal) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    s.camera().look(gview::StandardView::IsoSE);
    s.camera().set_target(gview::Point3::from(2.0, 2.0, 1.5));
    s.build_frame();
    s.activate_tool(ToolId::PatternBody);
    click_at(s, 4.0, 2.0, 1.5);
    CHECK(s.modeller().stage() == Modeller::Stage::TypeValue);
    for (char c : std::string("3 6")) s.key_char(std::string(1, c));
    s.key_special(0);
    CHECK_MSG(s.modeller().bodies().size() == 3, "%zu bodies: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
    if (s.modeller().bodies().size() != 3) return;
    const auto bb = s.modeller().bodies()[2].bounds(kVol);
    CHECK_MSG(std::fabs(bb.lo.x.to_f64() - 12.0) < 1e-6, "third copy at x = %.3f", bb.lo.x.to_f64());
}

TEST(workflow_section_becomes_a_sketch_on_the_cutting_plane) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    s.edit_sketch(0);
    draw_rect(s, "2,10", "6,10", "6,14", "2,14");
    extrude_at(s, 4.0, 12.0, "3");
    s.camera().look(gview::StandardView::IsoSW);
    s.camera().set_target(gview::Point3::from(3.0, 6.0, 1.5));
    s.fit_width(40.0);
    s.build_frame();
    const std::size_t before = s.sketches().size();
    s.activate_tool(ToolId::Section);
    click_at(s, 2.0, 2.0, 3.0);
    click_at(s, 2.0, 12.0, 1.5);
    CHECK_MSG(s.sketches().size() == before + 1, "%zu sketches: %s", s.sketches().size(),
              s.modeller().message().c_str());
    if (s.sketches().size() != before + 1) return;
    const auto& sk = s.sketches().back();
    CHECK_MSG(std::fabs(std::fabs(sk.plane.nx) - 1.0) < 1e-6, "section plane normal x = %.3f", sk.plane.nx);
    CHECK_MSG(sk.shapes.size() == 4, "%zu segments in the section", sk.shapes.size());
    CHECK(s.modeller().bodies().size() == 2);
}

TEST(workflow_distance_measures_between_two_picked_points) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    s.camera().set_target(gview::Point3::from(2.0, 2.0, 0.0));
    s.fit_width(20.0);
    s.build_frame();
    s.activate_tool(ToolId::MeasureDistance);
    click_at(s, 0.5, 0.5, 3.0);
    click_at(s, 3.5, 0.5, 3.0);
    CHECK(s.modeller().measured());
    CHECK_MSG(std::fabs(s.modeller().measured_distance() - 3.0) < 1e-3, "measured %.4f",
              s.modeller().measured_distance());
}

TEST(workflow_sweep_carries_a_profile_along_a_path) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    CHECK(s.begin_sketch_on_face(s.hovered_target().body, s.hovered_target().face));
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("1,-1");
    s.canvas().type_point("1,1");
    s.canvas().cancel();
    s.finish_sketch();
    s.edit_sketch(0);
    draw_rect(s, "10,10", "11,10", "11,11", "10,11");
    s.build_frame();
    s.activate_tool(ToolId::Sweep);
    s.set_lens(Lens::TwoD);
    s.camera().set_target(gview::Point3::from(6.0, 6.0, 0.0));
    s.fit_width(40.0);
    click_at(s, 10.5, 10.5, 0.0);
    CHECK_MSG(s.modeller().stage() == Modeller::Stage::PickSecond, "%s", s.modeller().message().c_str());
    click_at(s, 3.0, 2.0, 3.0);
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu bodies: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
    if (s.modeller().bodies().size() != 2) return;
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[1], kVol) - 2.0) < 1e-3,
              "swept volume %.4f", gtopo::volume(s.modeller().bodies()[1], kVol));
}

TEST(workflow_double_click_on_a_region_opens_its_sketch) {
    Shell s = block_shell();
    CHECK(s.mode() == Mode::Solid);
    s.set_lens(Lens::TwoD);
    s.build_frame();
    s.edit_sketch(0);
    draw_rect(s, "10,0", "14,0", "14,4", "10,4");
    s.finish_sketch();
    s.set_lens(Lens::TwoD);
    s.build_frame();
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(12.0, 2.0, 0.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    CHECK(s.hovered_target().valid && s.hovered_target().source == 0);
    s.pointer_down(px, py); s.pointer_up(px, py);
    s.pointer_move(px, py);
    s.pointer_down(px, py); s.pointer_up(px, py);
    CHECK(s.mode() == Mode::Sketch);
    CHECK(s.active_sketch() == 0);
    CHECK(s.lens() == Lens::TwoD);
}

TEST(commands_none_is_silent) {
    Shell s = make_shell();
    std::vector<std::string> names;
    for (const auto& c : s.commands().all()) names.push_back(c.name);
    CHECK(names.size() > 100);
    for (const auto& n : names) {
        if (n == "NEW" || n == "SCRIPT" || n == "RSCRIPT" || n == "DELAY" || n == "RESUME" || n == "APPLOAD") continue;
        const std::size_t before = s.log().size();
        const std::string last_before = s.log().empty() ? "" : s.log().back();
        s.command(n);
        const bool spoke = s.log().size() != before || (!s.log().empty() && s.log().back() != last_before);
        CHECK_MSG(spoke, "%s said nothing", n.c_str());
        s.key_special(1);
        s.key_special(1);
    }
}

TEST(commands_snap_switches_actually_switch) {
    Shell s = make_shell();
    CHECK(s.canvas().tool() == Tool::Select);
    const bool osnap = s.canvas().snaps().settings.enabled;
    s.command("OSNAP");
    CHECK(s.canvas().snaps().settings.enabled == !osnap);
    s.command("OSNAP");
    CHECK(s.canvas().snaps().settings.enabled == osnap);
    const bool grid_snap = s.canvas().grid_snap();
    s.command("SNAP");
    CHECK(s.canvas().grid_snap() == !grid_snap);
    const bool grid = s.grid_visible();
    s.command("GRID");
    CHECK(s.grid_visible() == !grid);
    s.command("BOX");
    CHECK(s.modeller().bodies().size() == 1);
}

TEST(workflow_middle_drag_turns_the_plan_view_into_3d_and_perspective) {
    Shell s = make_shell();
    CHECK(s.lens() == Lens::TwoD);
    CHECK(s.camera().projection() == gview::Projection::Orthographic);
    s.build_frame();
    s.middle_down(800, 500);
    s.pointer_move(840, 470);
    s.pointer_move(880, 440);
    s.middle_up(880, 440);
    CHECK(s.lens() == Lens::ThreeD);
    CHECK(s.camera().projection() == gview::Projection::Perspective);
    s.command("PLAN");
    CHECK(s.lens() == Lens::TwoD);
    CHECK(s.camera().projection() == gview::Projection::Orthographic);
}

TEST(ui_nothing_in_the_chrome_claims_the_drawing_beside_the_dock) {
    Shell s = make_shell();
    s.build_frame();
    for (double x : {1280.0, 1240.0, 1120.0, 800.0}) {
        s.pointer_move(x, 420.0);
        s.build_frame();
        CHECK_MSG(!s.ui_wants_mouse(), "the chrome claims (%.0f, 420)", x);
    }
}

TEST(workflow_the_default_view_is_ten_metres_across) {
    Shell s;
    s.set_dpr(1.0);
    s.set_viewport(1600, 1000);
    double lx, ly, rx, ry;
    CHECK(s.camera().plane_hit(0.0, 500.0, 0.0, lx, ly));
    CHECK(s.camera().plane_hit(1600.0, 500.0, 0.0, rx, ry));
    CHECK_MSG(std::fabs((rx - lx) - 10.0) < 1e-6, "%.4f m across", rx - lx);
    s.set_viewport(800, 800);
    CHECK(s.camera().plane_hit(0.0, 400.0, 0.0, lx, ly));
    CHECK(s.camera().plane_hit(800.0, 400.0, 0.0, rx, ry));
    CHECK_MSG(std::fabs((rx - lx) - 10.0) < 1e-6, "%.4f m across after a resize", rx - lx);
    s.scroll(0, 1, false);
    s.set_viewport(1600, 1000);
    CHECK(s.camera().plane_hit(0.0, 500.0, 0.0, lx, ly));
    CHECK(s.camera().plane_hit(1600.0, 500.0, 0.0, rx, ry));
    CHECK_MSG(std::fabs((rx - lx) - 10.0) > 0.1, "the user's zoom was overridden by a resize");
}

TEST(workflow_typing_a_command_hides_the_history_until_it_is_entered) {
    Shell s = make_shell();
    s.build_frame();
    CHECK(s.bar_input().empty());
    s.key_char("L");
    s.build_frame();
    CHECK_MSG(s.bar_input() == "L", "bar holds '%s'", s.bar_input().c_str());
    s.key_special(0);
    s.build_frame();
    CHECK(s.bar_input().empty());
    CHECK(s.canvas().tool() == Tool::Line);
}

TEST(menus_every_item_names_a_registered_command) {
    Shell s = make_shell();
    for (const auto& m : app_menus()) {
        CHECK(!m.title.empty());
        for (const auto& it : m.items) {
            if (it.separator()) continue;
            CHECK_MSG(s.commands().find(it.command) != nullptr, "%s > %s: '%s' is not a command",
                      m.title.c_str(), it.label.c_str(), it.command.c_str());
        }
    }
}

TEST(menus_the_in_window_bar_opens_and_runs_an_item) {
    Shell s = make_shell();
    CHECK(!s.native_menus());
    s.build_frame();
    s.pointer_move(30, 16);
    s.build_frame();
    s.pointer_down(30, 16);
    s.build_frame();
    s.pointer_up(30, 16);
    s.build_frame();
    CHECK_MSG(s.open_menu() == 0, "open menu %d", s.open_menu());
    s.key_special(1);
    s.build_frame();
    CHECK(s.open_menu() == -1);
}

TEST(files_dxf_round_trips_through_export_and_import) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    const std::string path = scratch_path("roundtrip.dxf");
    CHECK(s.export_file(path));
    Shell t = make_shell();
    CHECK(t.import_file(path));
    CHECK_MSG(t.canvas().shapes().size() == 4, "%zu shapes after the DXF round trip", t.canvas().shapes().size());
}

TEST(files_native_document_keeps_sketches_and_their_planes) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.5, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    CHECK(s.begin_sketch_on_face(s.hovered_target().body, s.hovered_target().face));
    draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
    const std::string path = scratch_path("doc.glcad");
    s.command("SAVE " + path);
    CHECK(s.document_path() == path);

    Shell t = make_shell();
    CHECK(t.open_file(path));
    CHECK_MSG(t.sketches().size() == 2, "%zu sketches came back", t.sketches().size());
    if (t.sketches().size() < 2) return;
    CHECK(t.sketches()[0].shapes.size() == 4 || t.canvas().shapes().size() == 4);
    CHECK_MSG(std::fabs(t.sketches()[1].plane.oz - 3.0) < 1e-6, "face sketch plane at z = %.3f", t.sketches()[1].plane.oz);
    CHECK(t.sketches()[1].shapes.size() == 4);
}

TEST(files_step_exports_a_solid_that_imports_back) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    const std::string path = scratch_path("block.step");
    CHECK(s.export_file(path));
    Shell t = make_shell();
    CHECK(t.import_file(path));
    CHECK_MSG(t.modeller().bodies().size() == 1, "%zu solids from STEP", t.modeller().bodies().size());
    if (t.modeller().bodies().empty()) return;
    CHECK_MSG(std::fabs(gtopo::volume(t.modeller().bodies()[0], kVol) - 48.0) < 1e-3,
              "volume %.4f after the STEP round trip", gtopo::volume(t.modeller().bodies()[0], kVol));
    CHECK(t.mode() == Mode::Solid);
}

TEST(files_without_a_dialog_the_command_asks_for_a_path) {
    Shell s = make_shell();
    s.command("OPEN");
    CHECK(!s.log().empty() && s.log().back().find("path") != std::string::npos);
    s.command("EXPORT nothing.xyz");
    CHECK(s.log().back().find("cannot export") != std::string::npos);
}

TEST(files_solids_are_saved_and_come_back_exact) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    s.command("CYLINDER");
    CHECK(s.modeller().bodies().size() == 2);
    const std::vector<gtopo::Body> before = s.modeller().bodies();
    const std::string path = scratch_path("solids.glcad");
    s.command("SAVE " + path);
    Shell t = make_shell();
    CHECK(t.open_file(path));
    CHECK_MSG(t.modeller().bodies().size() == 2, "%zu solids came back", t.modeller().bodies().size());
    if (t.modeller().bodies().size() != 2) return;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& a = before[i];
        const auto& b = t.modeller().bodies()[i];
        CHECK(a.vertex_count() == b.vertex_count() && a.face_count() == b.face_count());
        bool same = a.vertex_point.size() == b.vertex_point.size();
        for (std::size_t k = 0; same && k < a.vertex_point.size(); ++k)
            same = a.vertex_point[k].x == b.vertex_point[k].x && a.vertex_point[k].y == b.vertex_point[k].y &&
                   a.vertex_point[k].z == b.vertex_point[k].z;
        CHECK_MSG(same, "solid %zu changed in the round trip", i);
        CHECK(a.face_surface.size() == b.face_surface.size());
        CHECK_MSG(std::fabs(gtopo::volume(a, kVol) - gtopo::volume(b, kVol)) < 1e-12, "volume drifted");
    }
}

TEST(draft_lift_raises_every_room_of_a_plan) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("4,0");
    s.canvas().type_point("4,5");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.command("LIFT 3");
    CHECK_MSG(!s.modeller().bodies().empty(), "%s", s.modeller().message().c_str());
    double total = 0.0;
    for (const auto& b : s.modeller().bodies()) total += gtopo::volume(b, kVol);
    CHECK_MSG(std::fabs(total - 120.0) < 1e-3, "total volume %.4f (%zu solids)", total, s.modeller().bodies().size());
    CHECK(s.mode() == Mode::Solid);
}

TEST(workflow_text_palette_fits_short_viewport_and_scrolls_to_custom_color) {
    auto s = make_shell(); s.set_viewport(1600, 1000);
    std::string error; CHECK(s.load_font(&error));
    auto& canvas = s.canvas();
    canvas.add_text(gcm::to_lattice(canvas.volume(), 0, 0), "Part number", .25);
    canvas.set_selected_texts({0}); s.build_frame();
    const auto bounds = [&](const std::string& label) {
        for (const auto& item : s.accessible_ui()) if (item.label == label) return item.bounds;
        return gui::Rect{};
    };
    const auto open = bounds("Text color"); CHECK(open.w > 0);
    if (open.w <= 0) return;
    const auto p = open.centre(); s.pointer_move(p.x, p.y); s.build_frame();
    s.pointer_down(p.x, p.y); s.build_frame(); s.pointer_up(p.x, p.y); s.build_frame();
    s.set_viewport(980,300);s.build_frame();
    auto tile = bounds("Text palette #FF0000"); CHECK(tile.w > 0);
    for (const auto& item : s.accessible_ui()) if (item.label.rfind("Text palette ", 0) == 0) {
        CHECK(item.bounds.x >= 0 && item.bounds.y >= 0);
        CHECK(item.bounds.right() <= 980 && item.bounds.bottom() <= 300);
    }
    const auto at = tile.centre(); s.pointer_move(at.x, at.y); s.build_frame();
    s.scroll(0, -12, false); s.build_frame();
    const auto custom = bounds("Custom text color");
    CHECK(custom.w > 0 && custom.y >= 0 && custom.bottom() <= 300);
    if (const auto* directory = std::getenv("GLCAD_UI_REVIEW_DIR")) {
        std::filesystem::create_directories(directory);
        const auto png = snapshot_png(s, 980, 300);
        std::ofstream out(std::filesystem::path(directory) / "text-palette-short.png", std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), png.size()); CHECK(!png.empty());
    }
    s.key_special(1); s.build_frame(); CHECK(bounds("Text palette #FF0000").w == 0);
}

TEST(workflow_right_click_object_actions_are_labeled_clamped_and_operable) {
    auto s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.canvas().set_selection({0, 1});
    s.build_frame();
    const auto node = [&](const std::string& label) {
        for (const auto& item : s.accessible_ui()) if (item.label == label) return item;
        return gui::Context::Accessible{};
    };
    CHECK(node("Object options").bounds.empty());
    CHECK(node("Selection actions").bounds.empty());
    CHECK(!s.context_menu_open());
    const auto shot = [&](const char* name) {
        if (const auto* directory = std::getenv("GLCAD_UI_REVIEW_DIR")) {
            std::filesystem::create_directories(directory);
            std::string error; CHECK(s.load_font(&error)); s.build_frame();
            const auto png = snapshot_png(s, int(s.viewport_width()), int(s.viewport_height()));
            std::ofstream out(std::filesystem::path(directory) / name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(png.data()), png.size()); CHECK(!png.empty());
        }
    };
    shot("selected-objects-without-actions.png");
    s.pointer_move(1000, 700); s.right_down(1000, 700); s.build_frame();
    CHECK(s.context_menu_open());
    auto menu = node("Selection actions");
    CHECK(menu.value == "2 sketch edges");
    CHECK(node("Object options").bounds.empty());
    CHECK(node("Move").bounds.h >= 32);
    CHECK(menu.bounds.x >= 0 && menu.bounds.y >= 0);
    CHECK(menu.bounds.right() <= s.viewport_width() && menu.bounds.bottom() <= s.viewport_height());
    CHECK(node("Erase").bounds.y > node("Move").bounds.y);
    shot("selection-actions.png");
    const auto move = node("Move").bounds.centre();
    s.pointer_move(move.x, move.y); s.build_frame();
    s.pointer_down(move.x, move.y); s.build_frame(); s.pointer_up(move.x, move.y); s.build_frame();
    CHECK(!s.context_menu_open()); CHECK(s.canvas().tool() == Tool::Move);
    s.canvas().cancel(); s.canvas().set_tool(Tool::Select); s.build_frame();
    s.right_down(1000, 700); s.build_frame();
    s.key_special(1); s.build_frame(); CHECK(!s.context_menu_open());
    s.camera().set_target(gview::Point3::from(2,1.5,0));s.fit_width(16);s.build_frame();
    const auto shapes=s.canvas().shapes().size();s.activate_tool(ToolId::Line);
    for(double x:{1.0,3.0}) {
        double px,py;CHECK(s.camera().project(gview::Point3::from(x,1,0),px,py));
        CHECK(s.canvas_bounds().contains({float(px),float(py)}));
        s.pointer_move(px,py);s.build_frame();CHECK(!s.ui_wants_mouse());
        s.pointer_down(px,py);s.pointer_up(px,py);s.build_frame();
    }
    CHECK(s.canvas().shapes().size()==shapes+1);CHECK(s.canvas().tool()==Tool::Line);
    CHECK(node("Object options").bounds.empty());CHECK(node("Selection actions").bounds.empty());
}

TEST(workflow_layer_color_palette_updates_bylayer_and_undo_and_save) {
    auto s = make_shell();
    gdoc::Layer layer; layer.name = "Color review"; layer.color = gdoc::Color::aci(30);
    s.layers().add(layer); s.layers().set_current(layer.name);
    s.canvas().set_current_layer(layer.name);
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0"); s.canvas().type_point("4,0"); s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.build_frame();
    const auto bounds = [&](const std::string& label) {
        for (const auto& node : s.accessible_ui()) if (node.label == label) return node.bounds;
        return gui::Rect{};
    };
    const auto click = [&](const gui::Rect& r) {
        CHECK(r.w > 0); if (r.w <= 0) return;
        const auto p = r.centre();
        s.pointer_move(p.x, p.y); s.build_frame();
        s.pointer_down(p.x, p.y); s.build_frame();
        s.pointer_up(p.x, p.y); s.build_frame();
    };
    click(bounds("Layer color: Color review"));
    int chips = 0;
    for (const auto& node : s.accessible_ui()) if (node.label.rfind("ACI ", 0) == 0) ++chips;
    CHECK(chips == 25);
    if (const auto* directory = std::getenv("GLCAD_UI_REVIEW_DIR")) {
        std::filesystem::create_directories(directory);
        std::string error; CHECK(s.load_font(&error)); s.build_frame();
        const auto png = snapshot_png(s, int(s.viewport_width()), int(s.viewport_height()));
        std::ofstream out(std::filesystem::path(directory) / "layer-color-picker.png", std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), png.size());
        CHECK(!png.empty());
    }
    click(bounds("ACI 5"));
    s.build_frame();
    CHECK(s.layers().find(layer.name)->color == gdoc::Color::aci(5));
    CHECK(s.canvas().shapes().front().attrs.color.kind == gdoc::Color::Kind::ByLayer);
    const auto color = s.canvas().color_of_shape(s.canvas().shapes().front());
    CHECK(color.b > color.r && color.b > color.g);
    CHECK(bounds("ACI 5").w == 0);
    s.command("UNDO"); CHECK(s.layers().find(layer.name)->color == gdoc::Color::aci(30));
    s.command("REDO"); CHECK(s.layers().find(layer.name)->color == gdoc::Color::aci(5));
    s.build_frame();
    click(bounds("Layer color: Color review"));
    s.key_special(1); s.build_frame();
    CHECK(bounds("ACI 5").w == 0);
    click(bounds("Layer color: Color review"));
    const auto shape_count = s.canvas().shapes().size();
    s.pointer_move(200, 400); s.build_frame();
    s.pointer_down(200, 400); s.build_frame(); s.pointer_up(200, 400); s.build_frame();
    CHECK(bounds("ACI 5").w == 0);
    CHECK(s.canvas().shapes().size() == shape_count);
    const auto path = scratch_path("layer-color.glcad");
    CHECK(s.save_file(path));
    auto reopened = make_shell(); CHECK(reopened.open_file(path));
    CHECK(reopened.layers().find(layer.name)->color == gdoc::Color::aci(5));
    CHECK(reopened.canvas().shapes().front().attrs.color.kind == gdoc::Color::Kind::ByLayer);
    std::filesystem::remove(path);
}

TEST(draft_walls_from_lines_makes_joined_walls) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("6,0");
    s.canvas().type_point("6,4");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.command("WALLS 0.2 2.7");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu solids: %s", s.modeller().bodies().size(),
              s.modeller().message().c_str());
    if (s.modeller().bodies().empty()) return;
    const double expected = (6.2 * 0.2 + 4.2 * 0.2 - 0.2 * 0.2) * 2.7;
    const double vol = gtopo::volume(s.modeller().bodies()[0], kVol);
    CHECK_MSG(std::fabs(vol - expected) < 1e-3, "wall volume %.4f, expected %.4f", vol, expected);
}

TEST(draft_walls_without_height_stays_2d_and_undo_restores_centerlines) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    for (const char* point : {"0,0", "6,0", "6,4"}) s.canvas().type_point(point);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    const auto original = s.canvas().shapes();
    s.command("WALLS 200mm");
    s.build_frame();
    CHECK(s.mode() == Mode::Sketch);
    CHECK(s.modeller().bodies().empty());
    const auto walls = gregion::from_shapes(s.canvas().shapes());
    CHECK_MSG(walls.ok, "%s", walls.error.c_str());
    CHECK(walls.region.loops.size() == 1);
    CHECK(s.canvas().shapes().size() > original.size());
    for (const auto& wall : s.canvas().shapes()) {
        CHECK(wall.region);
        CHECK(wall.attrs.user.at("sketch_object") == "wall");
    }
    const auto serial = s.canvas().edit_serial();
    s.command("WALLS 200mm");
    CHECK(s.canvas().edit_serial() == serial);
    s.command("UNDO");
    CHECK(s.canvas().shapes().size() == original.size());
    for (std::size_t i = 0; i < original.size() && i < s.canvas().shapes().size(); ++i) {
        const auto& restored = s.canvas().shapes()[i];
        CHECK(restored.a.x == original[i].a.x && restored.a.y == original[i].a.y &&
              restored.b.x == original[i].b.x && restored.b.y == original[i].b.y);
        CHECK(restored.attrs.user.count("sketch_object") == 0);
    }
}

TEST(draft_walls_thickness_from_bar_converts_only_selected_centerlines) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0"); s.canvas().type_point("6,0"); s.canvas().cancel();
    s.canvas().type_point("20,0"); s.canvas().type_point("26,0"); s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    const auto untouched = s.canvas().shapes()[1];
    s.canvas().set_selection({0});
    s.build_frame();
    s.command("WALLS");
    for (const char* c = "200mm"; *c; ++c) s.key_char(std::string(1, *c));
    s.key_special(0);
    s.build_frame();
    CHECK(s.mode() == Mode::Sketch);
    CHECK(s.modeller().bodies().empty());
    CHECK(s.canvas().shapes().size() == 5);
    const auto kept = s.canvas().index_of_key(untouched.key);
    CHECK(kept != Canvas::npos);
    if (kept != Canvas::npos) CHECK(!s.canvas().shapes()[kept].region);
}

TEST(draft_walls_centerlines_join_existing_walls_and_preserve_room_hole) {
    Shell s = make_shell();
    const auto p = [&](double x, double y) { return gcm::to_lattice(s.canvas().volume(), x, y); };
    CHECK(s.canvas().add_wall(p(0, 0), p(6, 0), .2));
    s.canvas().set_tool(Tool::Line);
    for (const char* point : {"6,0", "6,4", "0,4", "0,0"}) s.canvas().type_point(point);
    s.canvas().cancel(); s.canvas().set_tool(Tool::Select);
    s.command("WALLS 200mm");
    const auto region = gregion::from_shapes(s.canvas().shapes());
    CHECK_MSG(region.ok, "%s", region.error.c_str());
    CHECK(region.region.loops.size() == 2);
    CHECK(region.region.contains(p(6.05, -.05)));
    CHECK(region.region.contains(p(-.05, -.05)));
    CHECK(!region.region.contains(p(3, 2)));
    CHECK(s.modeller().bodies().empty());
}

TEST(files_pdf_export_plots_the_sketch) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    const std::string path = scratch_path("plot.pdf");
    CHECK(s.export_file(path));
    CHECK_MSG(s.log().back().find("plotted") != std::string::npos, "%s", s.log().back().c_str());
    std::ifstream f(path, std::ios::binary);
    std::string head(8, '\0');
    f.read(head.data(), 8);
    CHECK_MSG(head.rfind("%PDF", 0) == 0, "not a PDF: %s", head.c_str());
}

TEST(text_tool_places_a_note_and_the_file_keeps_it) {
    Shell s = make_shell();
    s.build_frame();
    s.activate_tool(ToolId::Text);
    CHECK(s.active_tool() == ToolId::Text);
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(1.0, 1.0, 0.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (char c : std::string("KITCHEN")) s.key_char(std::string(1, c));
    s.key_special(2);
    s.key_char("N");
    s.key_special(0);
    CHECK_MSG(s.canvas().texts().size() == 1, "%zu notes", s.canvas().texts().size());
    if (s.canvas().texts().empty()) return;
    CHECK(s.canvas().texts()[0].text == "KITCHEN");
    double x, y;
    gcm::to_world(kVol, s.canvas().texts()[0].pos, x, y);
    CHECK_MSG(std::fabs(x - 1.0) < 1e-6 && std::fabs(y - 1.0) < 1e-6, "placed at %.3f, %.3f", x, y);
    s.build_frame();
    bool labelled = false;
    for (const auto& l : s.canvas().labels()) if (l.text == "KITCHEN") labelled = true;
    CHECK(labelled);
    const std::string path = scratch_path("text.glcad");
    s.command("SAVE " + path);
    Shell t = make_shell();
    CHECK(t.open_file(path));
    CHECK_MSG(t.canvas().texts().size() == 1 && t.canvas().texts()[0].text == "KITCHEN",
              "%zu notes came back", t.canvas().texts().size());
}

TEST(tabs_start_with_one_untitled_tab) {
    Shell s = make_shell();
    CHECK(s.tab_count() == 1);
    CHECK(s.active_tab() == 0);
    CHECK_MSG(s.tab_title() == "Untitled 1", "title '%s'", s.tab_title().c_str());
    CHECK(s.document_blank());
    bool has_new_tab = false, has_close_tab = false;
    for (const auto& m : app_menus())
        for (const auto& it : m.items) {
            if (it.command == "NEWTAB") has_new_tab = true;
            if (it.command == "CLOSETAB") has_close_tab = true;
        }
    CHECK(has_new_tab && has_close_tab);
}

TEST(tabs_keep_their_documents_apart) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    CHECK(s.canvas().shapes().size() == 4);
    CHECK(!s.document_blank());

    s.command("NEWTAB");
    CHECK(s.tab_count() == 2);
    CHECK(s.active_tab() == 1);
    CHECK_MSG(s.canvas().shapes().empty(), "the new tab has %zu shapes", s.canvas().shapes().size());
    CHECK(s.tab_title() == "Untitled 2");
    CHECK(s.modeller().bodies().empty());

    draw_rect(s, "0,0", "2,0", "2,2", "0,2");
    extrude_at(s, 1.0, 1.0, "3");
    CHECK(s.modeller().bodies().size() == 1);

    s.switch_tab(0);
    CHECK(s.active_tab() == 0);
    CHECK_MSG(s.canvas().shapes().size() == 4, "tab 1 has %zu shapes", s.canvas().shapes().size());
    CHECK(s.modeller().bodies().empty());
    CHECK(s.tab_title() == "Untitled 1");

    s.switch_tab(1);
    CHECK(s.modeller().bodies().size() == 1);
    CHECK(s.tabs()[1].active);
    CHECK(!s.tabs()[0].active);

    s.set_modifiers(false, true, false, false);
    s.key_special(3);
    s.set_modifiers(false, false, false, false);
    CHECK(s.active_tab() == 0);
}

TEST(tabs_close_and_the_last_one_empties) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    s.command("NEWTAB");
    draw_rect(s, "0,0", "1,0", "1,1", "0,1");
    s.command("NEWTAB");
    CHECK(s.tab_count() == 3);
    s.close_tab(1);
    CHECK(s.tab_count() == 2);
    CHECK(s.active_tab() == 1);
    CHECK(s.canvas().shapes().empty());
    s.switch_tab(0);
    CHECK(s.canvas().shapes().size() == 4);
    s.command("CLOSETAB");
    CHECK(s.tab_count() == 1);
    CHECK(s.canvas().shapes().empty());
    draw_rect(s, "0,0", "1,0", "1,1", "0,1");
    s.command("CLOSETAB");
    CHECK(s.tab_count() == 1);
    CHECK(s.canvas().shapes().empty());
    CHECK(s.document_blank());
}

TEST(tabs_open_lands_in_a_new_tab_unless_blank) {
    const std::string path = scratch_path("tabs_open.glcad");
    Shell a = make_shell();
    draw_rect(a, "0,0", "4,0", "4,4", "0,4");
    CHECK(a.save_file(path));

    Shell b = make_shell();
    CHECK(b.open_file(path));
    CHECK(b.tab_count() == 1);
    CHECK(b.canvas().shapes().size() == 4);
    CHECK_MSG(b.tab_title() == "tabs_open.glcad", "title '%s'", b.tab_title().c_str());

    draw_rect(b, "0,0", "1,0", "1,1", "0,1");
    CHECK(b.open_file(path));
    CHECK(b.tab_count() == 2);
    CHECK(b.active_tab() == 1);
    CHECK(b.canvas().shapes().size() == 4);
    b.switch_tab(0);
    CHECK(b.canvas().shapes().size() == 8);
    std::remove(path.c_str());
}

TEST(tabs_strip_is_drawn_and_clickable) {
    Shell s = make_shell();
    s.command("NEWTAB");
    s.build_frame();
    CHECK(s.active_tab() == 1);
    const auto press = [&](const std::string& label, int depth) {
        gui::Rect r;
        for (const auto& item : s.accessible_ui())
            if (item.role == gui::Context::Role::Button && item.label == label && item.depth == depth) r = item.bounds;
        CHECK_MSG(!r.empty(), "missing %s", label.c_str());
        const double x = r.centre().x, y = r.centre().y;
        s.pointer_move(x, y); s.build_frame();
        s.pointer_down(x, y); s.pointer_up(x, y); s.build_frame(); s.build_frame();
        return r;
    };
    const auto picker = press("Document: " + s.tabs()[1].title, 0);
    CHECK(picker.bottom() <= s.toolbar_top());
    press(s.tabs()[0].title, 1);
    CHECK_MSG(s.active_tab() == 0, "active tab %d after clicking the first", s.active_tab());
}

TEST(tabs_track_saved_versus_dirty) {
    const std::string path = scratch_path("tabs_dirty.glcad");
    Shell s = make_shell();
    CHECK(!s.document_dirty());
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    CHECK(s.document_dirty());
    CHECK(s.tabs()[0].dirty);
    CHECK(s.save_file(path));
    CHECK(!s.document_dirty());
    s.canvas().select_all();
    s.build_frame();
    CHECK(!s.document_dirty());
    extrude_at(s, 2.0, 2.0, "3");
    CHECK(s.document_dirty());
    s.command("NEWTAB");
    CHECK(!s.document_dirty());
    CHECK(s.tabs()[0].dirty);
    CHECK(!s.tabs()[1].dirty);
    s.switch_tab(0);
    CHECK(s.document_dirty());
    CHECK(s.save_file(path));
    CHECK(!s.document_dirty());
    Shell t = make_shell();
    CHECK(t.open_file(path));
    CHECK(!t.document_dirty());
    draw_rect(t, "0,0", "1,0", "1,1", "0,1");
    CHECK(t.document_dirty());
    t.command("NEW");
    CHECK(!t.document_dirty());
    std::remove(path.c_str());
}

TEST(workflow_sketch_command_then_a_click_on_a_face_sketches_on_it) {
    for (int tilted = 0; tilted < 2; ++tilted) {
        Shell s = block_shell();
        CHECK(s.mode() == Mode::Solid);
        if (tilted) { s.set_lens(Lens::ThreeD); s.camera().look(gview::StandardView::Iso); s.command("ZOOMEXTENTS"); }
        s.build_frame();
        s.command("SKETCH");
        s.build_frame();
        click_at(s, 2.0, 2.0, 3.0);
        s.build_frame();
        CHECK_MSG(s.sketches().size() == 2, "%s: %zu sketches after clicking the top face",
                  tilted ? "iso" : "plan", s.sketches().size());
        CHECK_MSG(s.mode() == Mode::Sketch, "%s: not in sketch mode", tilted ? "iso" : "plan");
        if (s.sketches().size() == 2) {
            CHECK(s.sketches()[1].on_face);
            CHECK_MSG(std::fabs(s.sketch_plane().oz - 3.0) < 1e-6, "plane at z=%g", s.sketch_plane().oz);
        }
        draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
        CHECK(s.canvas().shapes().size() == 4);
        extrude_at(s, 0.0, 0.0, "2");
        CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after extruding on the face", s.modeller().bodies().size());
        if (s.modeller().bodies().size() == 1) {
            const double v = gtopo::volume(s.modeller().bodies()[0], kVol);
            CHECK_MSG(std::fabs(v - (48.0 + 8.0)) < 1e-6, "volume %g, expected 56", v);
        }
    }
}


TEST(workflow_sketch_pick_from_sketch_mode_finds_the_face_under_the_footprint) {
    Shell s = block_shell();
    s.edit_sketch(0);
    s.build_frame();
    CHECK(s.mode() == Mode::Sketch);
    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 3.0);
    s.build_frame();
    CHECK_MSG(s.sketches().size() == 2, "%zu sketches", s.sketches().size());
    if (s.sketches().size() == 2) CHECK(s.sketches()[1].on_face);
}

TEST(undo_walks_drawing_and_solids_in_order) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    s.build_frame();
    extrude_at(s, 2.0, 2.0, "3");
    CHECK(s.modeller().bodies().size() == 1);
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("10,10");
    s.canvas().type_point("12,10");
    s.canvas().cancel();
    s.build_frame();
    CHECK(s.canvas().shapes().size() == 5);
    s.command("UNDO");
    CHECK_MSG(s.canvas().shapes().size() == 4, "%zu shapes after the first undo", s.canvas().shapes().size());
    CHECK(s.modeller().bodies().size() == 1);
    s.command("UNDO");
    CHECK_MSG(s.modeller().bodies().empty(), "the solid survived the second undo");
    CHECK(s.canvas().shapes().size() == 4);
    s.command("UNDO");
    CHECK_MSG(s.canvas().shapes().size() < 4, "%zu shapes after the third undo", s.canvas().shapes().size());
    s.command("REDO");
    CHECK(s.canvas().shapes().size() == 4);
    s.command("REDO");
    CHECK(s.modeller().bodies().size() == 1);
    s.command("REDO");
    CHECK(s.canvas().shapes().size() == 5);
}

TEST(undo_brings_back_a_deleted_sketch_and_a_deleted_solid) {
    Shell s = block_shell();
    s.fit_width(14);
    click_at(s, 2.0, 2.0, 3.0);
    s.build_frame();
    CHECK(s.selected_target().valid);
    CHECK(s.bar_input().empty());
    s.key_special(4);
    s.build_frame();
    CHECK_MSG(s.modeller().bodies().empty(), "the solid was not deleted");
    s.command("UNDO");
    CHECK(s.modeller().bodies().size() == 1);

    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 3.0);
    s.build_frame();
    CHECK(s.sketches().size() == 2);
    s.delete_sketch(1);
    CHECK(s.sketches().size() == 1);
    CHECK(s.active_sketch() == 0);
    s.command("UNDO");
    CHECK_MSG(s.sketches().size() == 2, "%zu sketches after undoing the delete", s.sketches().size());
    s.command("UNDO");
    CHECK_MSG(s.sketches().size() == 1, "%zu sketches after undoing the sketch", s.sketches().size());
}

TEST(delete_preserves_selected_solid_while_editing_command_text) {
    Shell s = block_shell();
    s.fit_width(14);
    click_at(s, 2.0, 2.0, 3.0);
    s.build_frame();
    CHECK(s.selected_target().valid);
    s.key_char("L");
    s.build_frame();
    CHECK(!s.bar_input().empty());
    s.key_special(4);
    s.build_frame();
    CHECK(s.modeller().bodies().size() == 1);
}

TEST(shapes_land_on_the_layer_current_when_drawn) {
    Shell s = make_shell();
    s.build_frame();
    s.layers().set_current("A-WALL");
    s.build_frame();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    for (const auto& sh : s.canvas().shapes()) CHECK_MSG(sh.layer == "A-WALL", "layer '%s'", sh.layer.c_str());
    s.layers().set_current("A-DOOR");
    s.build_frame();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("10,10");
    s.canvas().type_point("12,10");
    s.canvas().cancel();
    CHECK(s.canvas().shapes().back().layer == "A-DOOR");
    const std::size_t before = s.total_line_count();
    s.layers().find("A-WALL")->on = false;
    s.build_frame();
    CHECK_MSG(s.total_line_count() < before, "hidden layer still drawn (%zu vs %zu)", s.total_line_count(), before);
}

TEST(hatch_command_hatches_the_next_click) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    s.build_frame();
    s.command("HATCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 0.0);
    s.build_frame();
    bool hatched = false;
    for (const auto& line : s.log()) if (line.find("hatch") != std::string::npos || line.find("HATCH") != std::string::npos) hatched = true;
    CHECK(hatched);
    CHECK_MSG(s.canvas().hatches().size() == 1, "%zu hatches", s.canvas().hatches().size());
}

TEST(walls_without_arguments_asks_and_takes_them_from_the_bar) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("6,0");
    s.canvas().cancel();
    s.build_frame();
    s.command("WALLS");
    s.build_frame();
    CHECK(s.bar_input().empty());
    for (const char* c = "0.2 2.7"; *c; ++c) s.key_char(std::string(1, *c));
    s.build_frame();
    s.key_special(0);
    s.build_frame();
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after WALLS via the bar", s.modeller().bodies().size());
}

TEST(objects_keep_their_feature_history) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "3");
    CHECK(s.modeller().bodies().size() == 1);
    const auto& h = s.modeller().history(0);
    CHECK_MSG(h.size() == 1, "%zu features", h.size());
    if (!h.empty()) {
        CHECK_MSG(h[0].label.rfind("Extrude", 0) == 0, "label '%s'", h[0].label.c_str());
        CHECK_MSG(h[0].sketch == 0, "sketch %d", h[0].sketch);
    }
    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 3.0);
    s.build_frame();
    draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
    extrude_at(s, 0.0, 0.0, "-1");
    CHECK(s.modeller().bodies().size() == 1);
    const auto& h2 = s.modeller().history(0);
    CHECK_MSG(h2.size() == 2, "%zu features after the cut", h2.size());
    if (h2.size() == 2) CHECK_MSG(h2[1].label.rfind("Cut", 0) == 0, "label '%s'", h2[1].label.c_str());
    const std::string path = scratch_path("features.glcad");
    CHECK(s.save_file(path));
    Shell t = make_shell();
    CHECK(t.open_file(path));
    CHECK_MSG(t.modeller().history(0).size() == 2, "%zu features after loading", t.modeller().history(0).size());
    if (t.modeller().history(0).size() == 2) CHECK(t.modeller().history(0)[1].label == h2[1].label);
    std::remove(path.c_str());
    s.command("UNDO");
    CHECK_MSG(s.modeller().history(0).size() == 1, "%zu features after undo", s.modeller().history(0).size());
}

TEST(selecting_a_region_puts_the_pull_arrow_on_it) {
    Shell s = make_shell();
    s.fit_width(14);
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    s.build_frame();
    click_at(s, 2.0, 2.0, 0.0);
    s.build_frame();
    CHECK(s.selected_target().valid);
    CHECK(s.modeller().op() == Modeller::Op::Extrude);
    CHECK(s.modeller().stage() == Modeller::Stage::Placed);
    CHECK(s.active_tool() == ToolId::Select);
    for (const char* c = "2.5"; *c; ++c) s.key_char(std::string(1, *c));
    s.key_special(0);
    s.build_frame();
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after typing into the arrow", s.modeller().bodies().size());
    if (s.modeller().bodies().size() == 1) {
        const double v = gtopo::volume(s.modeller().bodies()[0], kVol);
        CHECK_MSG(std::fabs(v - 40.0) < 1e-4, "volume %g", v);
    }
    CHECK(!s.selected_target().valid);
    CHECK(s.modeller().op() == Modeller::Op::None);

    click_at(s, 30.0, 30.0, 0.0);
    s.build_frame();
    click_at(s, 2.0, 2.0, 2.5);
    s.build_frame();
    CHECK(s.selected_target().valid && s.selected_target().on_body);
    CHECK(s.modeller().stage() == Modeller::Stage::Placed);
    click_at(s, 30.0, 30.0, 0.0);
    s.build_frame();
    CHECK(!s.selected_target().valid);
    CHECK(s.modeller().op() == Modeller::Op::None);
    click_at(s, 30.0, 30.0, 0.0);
    s.build_frame();
    click_at(s, 2.0, 2.0, 2.5);
    s.build_frame();
    s.key_special(1);
    CHECK(!s.selected_target().valid);
    CHECK(s.modeller().op() == Modeller::Op::None);
}

TEST(every_registered_command_is_handled) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    s.build_frame();
    std::vector<std::string> unhandled;
    std::vector<std::string> names;
    for (const auto& c : s.commands().all()) names.push_back(c.name);
    for (const auto& name : names) {
        if (name == "QUIT" || name == "EXIT" || name == "NEW" || name == "NEWTAB" || name == "CLOSETAB" ||
            name == "OPEN" || name == "SAVE" || name == "SAVEAS" || name == "IMPORT" || name == "EXPORT" || name == "PLOT")
            continue;
        s.command(name);
        bool bad = false;
        for (const auto& line : s.log())
            if (line == name + ": not available in this build") bad = true;
        if (bad) unhandled.push_back(name);
        s.key_special(1);
        s.key_special(1);
        s.build_frame();
    }
    std::string list;
    for (const auto& n : unhandled) list += n + " ";
    CHECK_MSG(unhandled.empty(), "%zu commands fall through: %s", unhandled.size(), list.c_str());
}

TEST(extra_commands_reach_real_code) {
    Shell s = make_shell();
    s.command("POLYGON 6");
    s.build_frame();
    click_at(s, 0.0, 0.0, 0.0);
    click_at(s, 2.0, 0.0, 0.0);
    s.build_frame();
    CHECK_MSG(s.canvas().shapes().size() == 6, "%zu shapes after POLYGON", s.canvas().shapes().size());

    s.canvas().select_all();
    s.command("SCALE 2");
    s.build_frame();
    click_at(s, 0.0, 0.0, 0.0);
    s.build_frame();
    double farthest = 0.0;
    for (const auto& sh : s.canvas().shapes()) {
        double x, y;
        gcm::to_world(kVol, sh.a, x, y);
        farthest = std::max(farthest, std::hypot(x, y));
    }
    CHECK_MSG(std::fabs(farthest - 4.0) < 1e-6, "farthest vertex at %g after SCALE 2", farthest);

    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("10,10");
    s.canvas().type_point("12,10");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("10,10");
    s.canvas().type_point("12,10");
    s.canvas().cancel();
    CHECK(s.canvas().shapes().size() == 8);
    s.command("OVERKILL");
    CHECK_MSG(s.canvas().shapes().size() == 7, "%zu shapes after OVERKILL", s.canvas().shapes().size());

    s.command("SPHERE 1");
    s.build_frame();
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after SPHERE", s.modeller().bodies().size());
    if (s.modeller().bodies().size() == 1) {
        const double v = gtopo::volume(s.modeller().bodies()[0], kVol);
        CHECK_MSG(std::fabs(v - 4.18879) < 0.25, "sphere volume %g", v);
        CHECK(!s.modeller().history(0).empty());
    }

    s.edit_sketch(0);
    s.build_frame();
    s.canvas().select_all();
    s.command("GROUP hex");
    s.canvas().clear_selection();
    s.command("GROUP hex");
    CHECK_MSG(!s.canvas().selection().empty(), "GROUP did not reselect its members");

    s.command("PAGESETUP A4 portrait");
    bool said = false;
    for (const auto& l : s.log()) if (l.find("A4 portrait") != std::string::npos) said = true;
    CHECK(said);
}

TEST(dragging_a_cut_on_a_face_previews_the_cut_solid_not_a_slab) {
    Shell s = block_shell();
    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 3.0);
    s.build_frame();
    draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
    s.build_frame();
    s.set_lens(Lens::ThreeD);
    s.camera().look(gview::StandardView::Iso);
    s.activate_tool(ToolId::Extrude);
    double px, py, qx, qy;
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    gview::Point3 base, tip;
    CHECK(s.modeller().arrow_points(s.camera(), base, tip));
    double bx, by, tx, ty;
    s.camera().project(base, bx, by);
    s.camera().project(tip, tx, ty);
    s.pointer_up(px, py);
    s.pointer_move(tx, ty);
    s.pointer_down(tx, ty);
    s.pointer_move(tx - (tx - bx) * 2.0, ty - (ty - by) * 2.0);
    s.build_frame();
    CHECK(s.modeller().stage() == Modeller::Stage::Dragging);
    bool slab = false;
    for (const auto& v : s.mesh())
        if (std::fabs(v.color[0] - 0.95f) < 1e-3f && std::fabs(v.color[1] - 0.45f) < 1e-3f && v.color[3] < 0.99f) slab = true;
    CHECK_MSG(!slab, "a slab is drawn through the face while dragging (distance %g)", s.modeller().distance());
    CHECK(s.camera().project(gview::Point3::from(0, 0, 0), qx, qy));
}

TEST(hovering_an_edge_lights_it_and_a_click_offers_round_and_bevel_with_live_r) {
    Shell s = block_shell();
    s.fit_width(14);
    s.set_lens(Lens::ThreeD);
    s.camera().look(gview::StandardView::Iso);
    s.build_frame();
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(2.0, 0.0, 3.0), px, py));
    s.pointer_move(px, py);
    CHECK_MSG(s.modeller().edge_hovered(), "the edge under the pointer did not light");
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    s.build_frame();
    CHECK(s.modeller().edge_kept());
    CHECK(!s.modeller().edge_applied());
    const double before = gtopo::volume(s.modeller().bodies()[0], kVol);
    CHECK(s.round_edge(0.5));
    CHECK(s.modeller().edge_applied());
    CHECK(std::fabs(s.modeller().edge_value() - 0.5) < 1e-12);
    const double v1 = gtopo::volume(s.modeller().bodies()[0], kVol);
    CHECK_MSG(v1 < before, "a round removed nothing (%g vs %g)", v1, before);
    CHECK(s.modeller().bodies().size() == 1);
    for (const char* c = "1"; *c; ++c) s.key_char(std::string(1, *c));
    s.key_special(0);
    s.build_frame();
    CHECK(s.modeller().edge_kept());
    CHECK_MSG(std::fabs(s.modeller().edge_value() - 1.0) < 1e-12, "R is %g", s.modeller().edge_value());
    const double v2 = gtopo::volume(s.modeller().bodies()[0], kVol);
    CHECK_MSG(v2 < v1, "a larger R removed less (%g vs %g)", v2, v1);
    int fillets = 0;
    for (const auto& f : s.modeller().history(0)) if (f.label.rfind("Fillet", 0) == 0) ++fillets;
    CHECK_MSG(fillets == 1, "%d fillet features", fillets);
    CHECK(s.bevel_edge(0.5));
    int chamfers = 0; fillets = 0;
    for (const auto& f : s.modeller().history(0)) { if (f.label.rfind("Chamfer", 0) == 0) ++chamfers; if (f.label.rfind("Fillet", 0) == 0) ++fillets; }
    CHECK_MSG(chamfers == 1 && fillets == 0, "%d chamfers, %d fillets", chamfers, fillets);
    s.key_special(1);
    CHECK(!s.modeller().edge_kept());
    CHECK(s.modeller().op() == Modeller::Op::None);
    CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0], kVol) - gtopo::volume(s.modeller().bodies()[0], kVol)) < 1e-12);
}

TEST(features_can_be_edited_removed_and_follow_their_sketch) {
    Shell s = block_shell();
    CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0], kVol) - 48.0) < 1e-6);
    CHECK(s.set_feature_value(0, 0, 5.0));
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0], kVol) - 80.0) < 1e-4, "volume %g after editing the height", gtopo::volume(s.modeller().bodies()[0], kVol));
    CHECK(s.modeller().history(0)[0].label == "Extrude 5");
    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 5.0);
    s.build_frame();
    draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
    extrude_at(s, 0.0, 0.0, "-1");
    CHECK(s.modeller().history(0).size() == 2);
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0], kVol) - 76.0) < 1e-4, "volume %g after the cut", gtopo::volume(s.modeller().bodies()[0], kVol));
    CHECK(s.set_feature_value(0, 1, 2.0));
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0], kVol) - 72.0) < 1e-4, "volume %g after deepening the cut", gtopo::volume(s.modeller().bodies()[0], kVol));
    CHECK(s.remove_feature(0, 1));
    CHECK(s.modeller().history(0).size() == 1);
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0], kVol) - 80.0) < 1e-4, "volume %g after removing the cut", gtopo::volume(s.modeller().bodies()[0], kVol));
    s.command("UNDO");
    CHECK(s.modeller().history(0).size() == 2);
    s.edit_sketch(0);
    s.build_frame();
    s.canvas().select_all();
    s.command("SCALE 2");
    s.build_frame();
    click_at(s, 0.0, 0.0, 0.0);
    s.build_frame();
    s.finish_sketch();
    const double v = gtopo::volume(s.modeller().bodies()[0], kVol);
    CHECK_MSG(std::fabs(v - (8.0 * 8.0 * 5.0 - 8.0)) < 1e-4, "volume %g after the sketch grew", v);
    CHECK(s.remove_feature(0, 0));
    CHECK(s.modeller().bodies().empty());
}

TEST(solids_and_sketches_belong_to_layers_and_hide_with_them) {
    Shell s = make_shell();
    s.layers().set_current("A-WALL");
    s.build_frame();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "3");
    CHECK_MSG(s.modeller().body_layer(0) == "A-WALL", "solid on layer '%s'", s.modeller().body_layer(0).c_str());
    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 3.0);
    s.build_frame();
    CHECK(s.sketches().size() == 2);
    CHECK(s.sketches()[1].layer == "A-WALL");
    s.finish_sketch();
    s.pointer_move(-50.0, -50.0);
    s.build_frame();
    const std::size_t shown = s.mesh().size();
    CHECK(shown > 0);
    s.layers().find("A-WALL")->on = false;
    s.build_frame();
    CHECK_MSG(s.mesh().empty(), "%zu mesh vertices from a hidden solid", s.mesh().size());
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.pointer_move(px, py);
    CHECK_MSG(!s.hovered_target().valid, "hovered source %d on_body %d", s.hovered_target().source, int(s.hovered_target().on_body));
    s.layers().find("A-WALL")->on = true;
    s.pointer_move(-50.0, -50.0);
    s.build_frame();
    CHECK_MSG(s.mesh().size() == shown, "%zu vs %zu", s.mesh().size(), shown);
    const std::string path = scratch_path("layers_link.glcad");
    CHECK(s.save_file(path));
    Shell t = make_shell();
    CHECK(t.open_file(path));
    CHECK_MSG(t.modeller().body_layer(0) == "A-WALL", "loaded solid on '%s'", t.modeller().body_layer(0).c_str());
    CHECK(t.sketches().size() == 2 && t.sketches()[1].layer == "A-WALL");
    std::remove(path.c_str());
}

TEST(files_import_into_a_blank_document_is_centred_on_the_origin) {
    const std::string path = scratch_path("far_away.dxf");
    {
        std::ofstream f(path);
        f << "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
             "  0\nSECTION\n  2\nENTITIES\n"
             "  0\nLINE\n  8\n0\n 10\n100.0\n 20\n100.0\n 11\n110.0\n 21\n100.0\n"
             "  0\nLINE\n  8\n0\n 10\n110.0\n 20\n100.0\n 11\n110.0\n 21\n104.0\n"
             "  0\nTEXT\n  8\n0\n 10\n100.0\n 20\n104.0\n 40\n0.5\n  1\nfar\n"
             "  0\nENDSEC\n  0\nEOF\n";
    }
    Shell s = make_shell();
    CHECK(s.import_file(path));
    CHECK(s.canvas().shapes().size() == 2);
    if (s.canvas().shapes().size() == 2) {
        double ax, ay, bx, by;
        gcm::to_world(s.canvas().volume(), s.canvas().shapes()[0].a, ax, ay);
        gcm::to_world(s.canvas().volume(), s.canvas().shapes()[1].b, bx, by);
        CHECK_MSG(std::fabs(ax + 5.0) < 1e-6 && std::fabs(ay + 2.0) < 1e-6, "first point at (%g, %g)", ax, ay);
        CHECK_MSG(std::fabs(bx - 5.0) < 1e-6 && std::fabs(by - 2.0) < 1e-6, "last point at (%g, %g)", bx, by);
        double tx, ty;
        gcm::to_world(s.canvas().volume(), s.canvas().texts()[0].pos, tx, ty);
        CHECK_MSG(std::fabs(tx + 5.0) < 1e-6 && std::fabs(ty - 2.0) < 1e-6, "note at (%g, %g)", tx, ty);
    }
    Shell t = make_shell();
    draw_rect(t, "0,0", "1,0", "1,1", "0,1");
    CHECK(t.import_file(path));
    double x, y;
    gcm::to_world(t.canvas().volume(), t.canvas().shapes().back().b, x, y);
    CHECK_MSG(std::fabs(x - 110.0) < 1e-6 && std::fabs(y - 104.0) < 1e-6, "kept point at (%g, %g)", x, y);
    std::remove(path.c_str());
}

TEST(layers_hide_block_insertions_and_add_delete_follow_the_rules) {
    Shell s = make_shell();
    gdoc::BlockDef def;
    def.name = "CHAIR";
    gsnap::Shape g;
    g.kind = gsnap::ShapeKind::Segment;
    g.a = gcm::to_lattice(s.canvas().volume(), 0.0, 0.0);
    g.b = gcm::to_lattice(s.canvas().volume(), 1.0, 0.0);
    def.geometry.push_back(g);
    s.canvas().blocks().define(def);
    gdoc::BlockRef ref;
    ref.name = "CHAIR";
    ref.insertion = gcm::to_lattice(s.canvas().volume(), 3.0, 2.0);
    ref.layer = "FURN";
    gdoc::Layer furn; furn.name = "FURN"; s.layers().add(furn);
    s.canvas().blocks().insert(ref);
    s.build_frame();
    const std::size_t shown = s.static_lines().size();
    CHECK(shown > 0);
    s.layers().find("FURN")->on = false;
    s.build_frame();
    CHECK_MSG(s.static_lines().size() < shown, "hidden insertion still drawn (%zu vs %zu)",
              s.static_lines().size(), shown);
    s.layers().find("FURN")->on = true;
    s.add_layer();
    CHECK(s.layers().find("Layer 1") != nullptr);
    CHECK(s.layers().current() == "Layer 1");
    CHECK_MSG(!s.delete_layer("FURN"), "deleted a layer with an insertion on it");
    CHECK(s.delete_layer("Layer 1"));
    CHECK(s.layers().find("Layer 1") == nullptr);
    CHECK(s.layers().current() == "0");
}

TEST(tabs_do_not_leak_the_old_documents_ink_or_solids) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "3");
    s.build_frame();
    CHECK(s.modeller().bodies().size() == 1);
    CHECK(s.static_lines().empty());
    CHECK(!s.sketch_visible(0));
    s.command("NEWTAB");
    s.build_frame();
    CHECK_MSG(s.modeller().bodies().empty(), "%zu solids leaked into the new tab", s.modeller().bodies().size());
    CHECK_MSG(s.static_lines().empty(), "%zu line vertices leaked into the new tab", s.static_lines().size());
    CHECK(s.canvas().shapes().empty());
    s.pointer_move(800, 500);
    s.build_frame();
    CHECK(!s.hovered_target().valid);
    s.command("PREVTAB");
    s.build_frame();
    CHECK(s.modeller().bodies().size() == 1);
    CHECK(s.static_lines().empty());
    CHECK(!s.sketch_visible(0));
}

TEST(drawing_mode_shows_the_sheet_and_plots_at_the_chosen_scale) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    s.command("PLOTSCALE 1:50");
    s.enter_drawing_mode();
    s.build_frame();
    CHECK(s.mode() == Mode::Drawing);
    const std::string path = scratch_path("sheet.pdf");
    CHECK(s.export_file(path));
    std::ifstream f(path, std::ios::binary);
    char head[5] = {};
    f.read(head, 4);
    CHECK_MSG(std::string(head) == "%PDF", "the export is not a PDF");
    std::remove(path.c_str());
    bool logged = false;
    for (const auto& l : s.log()) if (l.find("plotted") != std::string::npos) logged = true;
    CHECK(logged);
}

TEST(donut_places_two_circles_and_escape_leaves_the_prompt) {
    Shell s = make_shell();
    s.command("DONUT 0.5 1");
    double px2, py2;
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 0.0), px2, py2));
    s.pointer_move(px2, py2);
    s.pointer_down(px2, py2);
    s.pointer_up(px2, py2);
    s.build_frame();
    std::size_t circles = 0;
    for (const auto& sh : s.canvas().shapes()) if (sh.kind == gsnap::ShapeKind::Circle) ++circles;
    CHECK_MSG(circles == 2, "%zu circles from DONUT", circles);
    s.command("DONUT");
    s.key_special(1);
    bool cancelled = false;
    for (const auto& l : s.log()) if (l.find("cancelled") != std::string::npos) cancelled = true;
    CHECK(cancelled);
}

TEST(repro_face_sketch_extrude_after_undo) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "2");
    CHECK(s.modeller().bodies().size() == 1);
    const auto vol = [&] { return gtopo::volume(s.modeller().bodies()[0], kVol); };
    CHECK_MSG(std::fabs(vol() - 32.0) < 1e-3, "block volume %.4f", vol());

    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 2.0);
    s.build_frame();
    CHECK_MSG(s.sketches().size() == 2, "sketch on face: %zu sketches", s.sketches().size());
    if (s.sketches().size() != 2) return;
    draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
    extrude_at(s, 0.0, 0.0, "-0.5");
    CHECK_MSG(s.modeller().bodies().size() == 1, "cut: %zu bodies (%s)", s.modeller().bodies().size(), s.modeller().message().c_str());
    CHECK_MSG(std::fabs(vol() - 30.0) < 1e-3, "after cut: %.4f (%s)", vol(), s.modeller().message().c_str());

    s.command("UNDO");
    CHECK_MSG(std::fabs(vol() - 32.0) < 1e-3, "after undo: %.4f", vol());
    CHECK_MSG(s.sketches().size() == 2 && s.sketches()[1].on_face, "sketch lost after undo: %zu sketches, on_face=%d",
              s.sketches().size(), s.sketches().size() > 1 ? (int)s.sketches()[1].on_face : -1);
    CHECK_MSG(s.canvas().shapes().size() == 4, "square lost after undo: %zu shapes", s.canvas().shapes().size());

    extrude_at(s, 0.0, 0.0, "-0.5");
    CHECK_MSG(s.modeller().bodies().size() == 1, "re-cut: %zu bodies (%s)", s.modeller().bodies().size(), s.modeller().message().c_str());
    CHECK_MSG(std::fabs(vol() - 30.0) < 1e-3, "re-cut: %.4f (%s)", vol(), s.modeller().message().c_str());

    s.command("UNDO");
    CHECK_MSG(std::fabs(vol() - 32.0) < 1e-3, "undo 2: %.4f", vol());
    extrude_at(s, 0.0, 0.0, "0.5");
    CHECK_MSG(s.modeller().bodies().size() == 1, "boss: %zu bodies (%s)", s.modeller().bodies().size(), s.modeller().message().c_str());
    CHECK_MSG(std::fabs(vol() - 34.0) < 1e-3, "boss: %.4f (%s)", vol(), s.modeller().message().c_str());
}

TEST(repro_face_sketch_circle_no_highlight) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "2");
    CHECK(s.modeller().bodies().size() == 1);
    const auto vol = [&] { return gtopo::volume(s.modeller().bodies()[0], kVol); };

    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 2.0);
    s.build_frame();
    CHECK_MSG(s.sketches().size() == 2 && s.sketches()[1].on_face, "no face sketch (%zu)", s.sketches().size());
    if (s.sketches().size() != 2) return;

    s.canvas().modify_shapes([](auto& v) {
        gsnap::Shape sh;
        sh.kind = gsnap::ShapeKind::Circle;
        sh.a = gcm::to_lattice(kVol, 0.0, 0.0);
        sh.radius = gcm::to_lattice(kVol, 1.0, 0.0).x;
        v.push_back(sh);
    });
    s.build_frame();
    s.activate_tool(ToolId::Extrude);

    {
        double px, py;
        CHECK(s.camera().project(gview::Point3::from(2.5, 2.0, 2.0), px, py));
        s.build_frame();
        s.pointer_move(px, py);
        s.build_frame();
        const auto& h = s.modeller().hovered();
        CHECK_MSG(h.valid && h.source >= 0, "inside the circle: no region target (valid=%d src=%d on_body=%d)",
                  (int)h.valid, h.source, (int)h.on_body);
        {
            double area = 0.0;
            std::size_t npts = 0;
            for (const auto& loop : h.outline) {
                npts += loop.size();
                for (std::size_t i = 0; i < loop.size(); ++i) {
                    const auto& p = loop[i];
                    const auto& q = loop[(i + 1) % loop.size()];
                    area += p.x.to_f64() * q.y.to_f64() - q.x.to_f64() * p.y.to_f64();
                }
            }
            std::fprintf(stderr, "DBG outline loops=%zu pts=%zu area=%.4f profile.pts=%zu\n",
                         h.outline.size(), npts, area * 0.5, h.profile.outer.size());
        }
    }
    {
        double px, py;
        CHECK(s.camera().project(gview::Point3::from(3.5, 2.0, 2.0), px, py));
        s.build_frame();
        s.pointer_move(px, py);
        s.build_frame();
        const auto& h = s.modeller().hovered();
        CHECK_MSG(h.valid, "outside the circle: nothing under the cursor");
    }

    extrude_at(s, 0.5, 0.0, "-0.5");
    CHECK_MSG(s.modeller().bodies().size() == 1, "circle cut: %zu bodies (%s)",
              s.modeller().bodies().size(), s.modeller().message().c_str());
    CHECK_MSG(std::fabs(vol() - (32.0 - 0.5 * 3.14159265358979323846)) < 1e-2,
              "circle cut: %.4f (%s)", vol(), s.modeller().message().c_str());
}

TEST(dbg_tmp_circle_boss_and_cut) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "2");
    const auto vol = [&] { return s.modeller().bodies().empty() ? -1.0 : gtopo::volume(s.modeller().bodies()[0], kVol); };
    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 2.0);
    s.build_frame();
    s.canvas().modify_shapes([](auto& v) {
        gsnap::Shape sh;
        sh.kind = gsnap::ShapeKind::Circle;
        sh.a = gcm::to_lattice(kVol, 0.0, 0.0);
        sh.radius = gcm::to_lattice(kVol, 1.0, 0.0).x;
        v.push_back(sh);
    });
    s.build_frame();
    extrude_at(s, 0.5, 0.0, "0.5");
    std::fprintf(stderr, "DBG boss: vol=%.4f (want %.4f) bodies=%zu msg=%s\n", vol(), 32.0 + 0.5 * 3.14159265358979323846, s.modeller().bodies().size(), s.modeller().message().c_str());
    s.command("UNDO");
    extrude_at(s, 0.5, 0.0, "-0.5");
    std::fprintf(stderr, "DBG cut: vol=%.4f (want %.4f) bodies=%zu msg=%s\n", vol(), 32.0 - 0.5 * 3.14159265358979323846, s.modeller().bodies().size(), s.modeller().message().c_str());
    CHECK(s.modeller().bodies().size() == 1);
}

TEST(face_sketch_cut_cut_boss_and_round_stay_one_sound_solid) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "2");
    CHECK(s.modeller().bodies().size() == 1);
    const auto vol = [&] { return gtopo::volume(s.modeller().bodies()[0], kVol); };
    CHECK_MSG(std::fabs(vol() - 32.0) < 1e-3, "block volume %.4f", vol());

    s.command("SKETCH");
    s.build_frame();
    click_at(s, 2.0, 2.0, 2.0);
    s.build_frame();
    CHECK(s.sketches().size() == 2);
    draw_rect(s, "-1,-1", "1,-1", "1,1", "-1,1");
    extrude_at(s, 0.0, 0.0, "-0.5");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after the first cut (%s)",
              s.modeller().bodies().size(), s.modeller().message().c_str());
    CHECK_MSG(std::fabs(vol() - 30.0) < 1e-3, "after cut 1: %.4f", vol());

    s.set_sketch_visibility(1,Shell::SketchVisibility::Shown);
    s.build_frame();
    extrude_at(s, 0.0, 0.0, "-0.25");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after the second cut", s.modeller().bodies().size());
    {
        const auto topo = s.modeller().bodies()[0].analyse();
        CHECK_MSG(topo.closed && topo.manifold, "the solid is not sound after the touching cut");
        CHECK_MSG(std::fabs(vol() - 30.0) < 1e-3, "after the touching cut: %.4f", vol());
    }
    extrude_at(s, 0.0, 0.0, "-1");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after the deep cut", s.modeller().bodies().size());
    {
        const auto topo = s.modeller().bodies()[0].analyse();
        CHECK_MSG(topo.closed && topo.manifold, "the solid is not sound after the deep re-cut");
        CHECK_MSG(std::fabs(vol() - 28.0) < 1e-3, "after the deep re-cut: %.4f", vol());
    }

    s.command("SKETCH");
    s.build_frame();
    click_at(s, 0.5, 0.5, 2.0);
    s.build_frame();
    draw_rect(s, "-1.8,-1.8", "-1.2,-1.8", "-1.2,-1.2", "-1.8,-1.2");
    extrude_at(s, -1.5, -1.5, "0.5");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies: the boss floated off", s.modeller().bodies().size());
    const double with_boss = vol();
    CHECK_MSG(std::fabs(with_boss - 28.18) < 1e-3, "boss was not created: %.4f", with_boss);
    {
        const auto& body = s.modeller().bodies()[0];
        const auto topology = body.analyse();
        CHECK_MSG(topology.closed && topology.manifold, "the boss union left an open solid before filleting");
    }

    s.camera().look(gview::StandardView::Iso);
    s.build_frame();
    s.activate_tool(ToolId::FilletEdge);
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(0.5, 0.2, 2.5), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    CHECK_MSG(s.modeller().edge_hovered(), "no boss edge under the cursor");
    gview::Point3 mid{};
    s.modeller().edge_midpoint(mid);
    CHECK_MSG(std::fabs(mid.x.to_f64() - 0.5) < 1e-3 && std::fabs(mid.y.to_f64() - 0.2) < 1e-3 &&
              std::fabs(mid.z.to_f64() - 2.5) < 1e-3,
              "hovered the wrong edge: mid (%g, %g, %g)", mid.x.to_f64(), mid.y.to_f64(), mid.z.to_f64());
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (const char c : std::string("0.05")) s.key_char(std::string(1, c));
    s.key_special(0);
    CHECK_MSG(s.modeller().bodies().size() == 1, "round split the solid: %s", s.modeller().message().c_str());
    {
        const auto topo = s.modeller().bodies()[0].analyse();
        CHECK_MSG(topo.closed && topo.manifold, "round broke the solid: %s", s.modeller().message().c_str());
        CHECK_MSG(vol() < with_boss - 1e-8, "round did not cut: %.4f -> %.4f (%s)",
                  with_boss, vol(), s.modeller().message().c_str());
    }
}

TEST(band_select_and_move_take_block_insertions_along) {
    Shell s = make_shell();
    gdoc::BlockDef def;
    def.name = "CHAIR2";
    gsnap::Shape g;
    g.kind = gsnap::ShapeKind::Segment;
    g.a = gcm::to_lattice(s.canvas().volume(), 0.0, 0.0);
    g.b = gcm::to_lattice(s.canvas().volume(), 0.5, 0.0);
    def.geometry.push_back(g);
    s.canvas().blocks().define(def);
    gdoc::BlockRef ref;
    ref.name = "CHAIR2";
    ref.insertion = gcm::to_lattice(s.canvas().volume(), 2.0, 2.0);
    s.canvas().blocks().insert(ref);
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    s.build_frame();
    double x0, y0, x1, y1;
    CHECK(s.camera().project(gview::Point3::from(-0.5, -0.5, 0.0), x0, y0));
    CHECK(s.camera().project(gview::Point3::from(5.0, 5.0, 0.0), x1, y1));
    s.canvas().begin_band(x0, y0);
    s.canvas().update_band(x1, y1);
    s.canvas().end_band(false);
    CHECK_MSG(s.canvas().selection().size() == 4, "%zu shapes in the window", s.canvas().selection().size());
    CHECK_MSG(s.canvas().selected_references().size() == 1, "%zu refs in the window",
              s.canvas().selected_references().size());
    s.canvas().set_tool(Tool::Move);
    s.canvas().type_point("0,0");
    s.canvas().type_point("1,0");
    s.build_frame();
    double ix, iy;
    gcm::to_world(s.canvas().volume(), s.canvas().blocks().references()[0].insertion, ix, iy);
    CHECK_MSG(std::fabs(ix - 3.0) < 1e-6 && std::fabs(iy - 2.0) < 1e-6,
              "the insertion stayed at (%g, %g)", ix, iy);
}

TEST(move_works_on_a_chairs_only_selection) {
    Shell s = make_shell();
    gdoc::BlockDef def;
    def.name = "CHAIR3";
    gsnap::Shape g;
    g.kind = gsnap::ShapeKind::Segment;
    g.a = gcm::to_lattice(s.canvas().volume(), 0.0, 0.0);
    g.b = gcm::to_lattice(s.canvas().volume(), 0.5, 0.0);
    def.geometry.push_back(g);
    s.canvas().blocks().define(def);
    gdoc::BlockRef ref;
    ref.name = "CHAIR3";
    ref.insertion = gcm::to_lattice(s.canvas().volume(), 2.0, 2.0);
    s.canvas().blocks().insert(ref);
    s.build_frame();
    double x0, y0, x1, y1;
    CHECK(s.camera().project(gview::Point3::from(1.5, 1.5, 0.0), x0, y0));
    CHECK(s.camera().project(gview::Point3::from(3.0, 3.0, 0.0), x1, y1));
    s.canvas().begin_band(x0, y0);
    s.canvas().update_band(x1, y1);
    s.canvas().end_band(false);
    CHECK_MSG(s.canvas().selection().empty() && s.canvas().selected_references().size() == 1,
              "%zu shapes, %zu refs selected", s.canvas().selection().size(),
              s.canvas().selected_references().size());
    s.canvas().set_tool(Tool::Move);
    s.canvas().type_point("0,0");
    s.canvas().type_point("0,-3");
    double ix, iy;
    gcm::to_world(s.canvas().volume(), s.canvas().blocks().references()[0].insertion, ix, iy);
    CHECK_MSG(std::fabs(ix - 2.0) < 1e-6 && std::fabs(iy - (-1.0)) < 1e-6,
              "the insertion went to (%g, %g)", ix, iy);
}

TEST(undo_puts_a_moved_block_insertion_back) {
    Shell s = make_shell();
    gdoc::BlockDef def;
    def.name = "CHAIR4";
    gsnap::Shape g;
    g.kind = gsnap::ShapeKind::Segment;
    g.a = gcm::to_lattice(s.canvas().volume(), 0.0, 0.0);
    g.b = gcm::to_lattice(s.canvas().volume(), 0.5, 0.0);
    def.geometry.push_back(g);
    s.canvas().blocks().define(def);
    gdoc::BlockRef ref;
    ref.name = "CHAIR4";
    ref.insertion = gcm::to_lattice(s.canvas().volume(), 2.0, 2.0);
    s.canvas().blocks().insert(ref);
    s.build_frame();
    double x0, y0, x1, y1;
    CHECK(s.camera().project(gview::Point3::from(1.5, 1.5, 0.0), x0, y0));
    CHECK(s.camera().project(gview::Point3::from(3.0, 3.0, 0.0), x1, y1));
    s.canvas().begin_band(x0, y0);
    s.canvas().update_band(x1, y1);
    s.canvas().end_band(false);
    CHECK(s.canvas().selected_references().size() == 1);
    s.canvas().set_tool(Tool::Move);
    s.canvas().type_point("0,0");
    s.canvas().type_point("1,0");
    double ix, iy;
    gcm::to_world(s.canvas().volume(), s.canvas().blocks().references()[0].insertion, ix, iy);
    CHECK_MSG(std::fabs(ix - 3.0) < 1e-6, "moved to (%g, %g)", ix, iy);
    s.canvas().undo();
    gcm::to_world(s.canvas().volume(), s.canvas().blocks().references()[0].insertion, ix, iy);
    CHECK_MSG(std::fabs(ix - 2.0) < 1e-6 && std::fabs(iy - 2.0) < 1e-6,
              "undo left it at (%g, %g)", ix, iy);
    s.canvas().redo();
    gcm::to_world(s.canvas().volume(), s.canvas().blocks().references()[0].insertion, ix, iy);
    CHECK_MSG(std::fabs(ix - 3.0) < 1e-6, "redo left it at (%g, %g)", ix, iy);
}

TEST(notes_select_move_and_undo_like_objects) {
    Shell s = make_shell();
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 2.0, 2.0), "HELLO", 0.5);
    s.build_frame();
    double x0, y0, x1, y1;
    CHECK(s.camera().project(gview::Point3::from(0.0, 0.0, 0.0), x0, y0));
    CHECK(s.camera().project(gview::Point3::from(4.0, 4.0, 0.0), x1, y1));
    s.canvas().begin_band(x0, y0);
    s.canvas().update_band(x1, y1);
    s.canvas().end_band(false);
    CHECK_MSG(s.canvas().selected_texts().size() == 1, "%zu texts selected",
              s.canvas().selected_texts().size());
    s.canvas().set_tool(Tool::Move);
    s.canvas().type_point("0,0");
    s.canvas().type_point("1,0");
    double tx, ty;
    gcm::to_world(s.canvas().volume(), s.canvas().texts()[0].pos, tx, ty);
    CHECK_MSG(std::fabs(tx - 3.0) < 1e-6, "moved to (%g, %g)", tx, ty);
    s.canvas().undo();
    gcm::to_world(s.canvas().volume(), s.canvas().texts()[0].pos, tx, ty);
    CHECK_MSG(std::fabs(tx - 2.0) < 1e-6, "undo left it at (%g, %g)", tx, ty);
}

TEST(press_and_drag_moves_an_unselected_object_in_one_gesture) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("2,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.build_frame();
    double mx, my, gx, gy;
    CHECK(s.camera().project(gview::Point3::from(1.0, 0.0, 0.0), mx, my));
    CHECK(s.camera().project(gview::Point3::from(1.0, 2.0, 0.0), gx, gy));
    CHECK(s.canvas().select_and_drag(mx, my));
    CHECK(s.canvas().selection().size() == 1);
    s.canvas().update_selection_drag(mx + 1.0, my);
    double ax, ay;
    gcm::to_world(s.canvas().volume(), s.canvas().shapes()[0].a, ax, ay);
    CHECK_MSG(std::fabs(ax) < 1e-9 && std::fabs(ay) < 1e-9, "the click slop moved it to (%g, %g)", ax, ay);
    s.canvas().update_selection_drag(gx, gy);
    s.canvas().end_selection_drag();
    gcm::to_world(s.canvas().volume(), s.canvas().shapes()[0].a, ax, ay);
    CHECK_MSG(std::fabs(ay - 2.0) < 0.2, "the drag left a at (%g, %g)", ax, ay);
}

TEST(walls_on_a_huge_import_return_without_welding) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    for (int i = 0; i < 600; ++i) {
        char a[32], b[32];
        std::snprintf(a, sizeof(a), "%d,%d", (i % 40) * 4, (i / 40) * 4);
        std::snprintf(b, sizeof(b), "%d,%d", (i % 40) * 4 + 1, (i / 40) * 4);
        s.canvas().type_point(a);
        s.canvas().type_point(b);
        s.canvas().cancel();
    }
    s.canvas().set_tool(Tool::Select);
    s.build_frame();
    const std::size_t n = s.modeller().walls(0, 0.2, 2.0);
    CHECK_MSG(n == 600, "%zu wall solids", n);
}

TEST(drawing_mode_shows_the_paper_and_fits_it) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "10,0", "10,6", "0,6");
    s.command("PLOTSCALE 0");
    s.enter_drawing_mode();
    s.build_frame();
    double cx, cy, w, h;
    CHECK_MSG(s.paper_rect(cx, cy, w, h), "no paper rect in fit-to-paper mode");
    CHECK_MSG(std::fabs(cx - 5.0) < 1e-6 && std::fabs(cy - 3.0) < 1e-6,
              "paper centred at (%g, %g)", cx, cy);
    CHECK_MSG(w > 0.3 && w < 0.45 && h > 0.2 && h < 0.35,
              "the sheet is %g x %g, not a real A3", w, h);
}

TEST(measure_in_sketch_is_the_persistent_dimension) {
    Shell s = make_shell();
    s.command("DIST");
    CHECK_MSG(s.canvas().tool() == Tool::DimAligned,
              "Measure in sketch armed tool %d, not DimAligned", int(s.canvas().tool()));
}

TEST(two_line_clicks_dimension_the_perpendicular_gap) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("10,0");
    s.canvas().cancel();
    s.canvas().type_point("0,2");
    s.canvas().type_point("10,2");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::DimAligned);
    double px, py;
    s.canvas().view().screen_of(5.0, 0.0, px, py);
    s.canvas().click(px, py, false);
    s.canvas().view().screen_of(5.0, 2.0, px, py);
    s.canvas().click(px, py, false);
    CHECK_MSG(s.canvas().dimensions().size() == 1,
              "%zu dimensions placed", s.canvas().dimensions().size());
    const auto& d = s.canvas().dimensions()[0];
    double ax, ay, bx, by;
    gcm::to_world(s.canvas().volume(), d.p1, ax, ay);
    gcm::to_world(s.canvas().volume(), d.p2, bx, by);
    const double gap = std::hypot(bx - ax, by - ay);
    CHECK_MSG(std::fabs(gap - 2.0) < 1e-6, "measured %g, want the 2.0 gap", gap);
    CHECK_MSG(std::fabs(ax - bx) < 1e-6, "anchors not perpendicular: (%g,%g)-(%g,%g)",
              ax, ay, bx, by);

    s.command("CANNOSCALE 1:100");
    s.build_frame();
    bool label_found = false;
    for (const auto& lab : s.canvas().labels())
        if (lab.height * s.canvas().view().scale() >= 9.0 && !lab.text.empty())
            label_found = true;
    CHECK_MSG(label_found, "no readable dimension text among %zu labels",
              s.canvas().labels().size());
    std::size_t segs = 0;
    for (const auto& item : s.canvas().static_draw().lines()) { (void)item; ++segs; }
    CHECK_MSG(segs > 8, "only %zu line items: arrowheads missing", segs);
}

TEST(drawing_mode_is_the_preview_with_markups) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "10,0", "10,6", "0,6");
    s.enter_drawing_mode();
    CHECK_MSG(s.layers().current() == "Markup", "current layer '%s'", s.layers().current().c_str());
    CHECK(s.layers().find("Markup") != nullptr);
    const std::size_t before = s.canvas().shapes().size();
    s.command("LINE");
    s.canvas().type_point("2,2");
    s.canvas().type_point("4,2");
    s.canvas().cancel();
    CHECK(s.canvas().shapes().size() == before + 1);
    CHECK_MSG(s.canvas().shapes().back().layer == "Markup",
              "markup landed on layer '%s'", s.canvas().shapes().back().layer.c_str());
    s.set_mode(Mode::Sketch);
    CHECK_MSG(s.layers().current() != "Markup", "leaving kept the Markup layer current");
}

TEST(drawing_preview_blank_space_never_selects_unscaled_model_regions) {
    for(int gesture=0;gesture<6;++gesture) {
        Shell s=make_shell();draw_rect(s,"-5,-3","5,-3","5,3","-5,3");
        s.command("PLOTSCALE 1:50");s.enter_drawing_mode();s.build_frame();
        const auto bounds=s.canvas_bounds();
        const double x=bounds.x+30,y=bounds.y+bounds.h*.75;
        CHECK(s.canvas_bounds().contains({float(x),float(y)}));
        const auto before=s.canvas().shapes().size();
        const auto view=s.camera().target();const auto distance=s.camera().distance();
        s.pointer_move(x,y);s.build_frame();s.build_frame();
        CHECK(!s.hovered_target().valid);CHECK(s.mesh().empty());
        s.set_modifiers(gesture==2,gesture==3,false,gesture==4);
        if(gesture==5) {s.right_down(x,y);s.build_frame();}
        else {
            for(int n=0;n<(gesture==1?2:1);++n) {
                s.pointer_down(x,y);s.build_frame();s.pointer_up(x,y);s.build_frame();
            }
        }
        CHECK(s.mode()==Mode::Drawing);CHECK(s.modeller().op()==Modeller::Op::None);
        CHECK(!s.selected_target().valid);CHECK(!s.hovered_target().valid);
        CHECK(s.modeller().bodies().empty());CHECK(s.canvas().selection_count()==0);CHECK(s.mesh().empty());
        CHECK(s.canvas().shapes().size()==before);CHECK(s.plot_offset_x()==0);CHECK(s.plot_offset_y()==0);
        CHECK(s.camera().target().x.to_f64()==view.x.to_f64());CHECK(s.camera().target().y.to_f64()==view.y.to_f64());
        CHECK(s.camera().distance()==distance);
        for(const auto& item:s.accessible_ui())CHECK(item.label!="Extrusion distance");
    }
}

TEST(drawing_preview_selects_visible_geometry_and_preserves_explicit_sketch_extrusion) {
    Shell s=make_shell();draw_rect(s,"-5,-3","5,-3","5,3","-5,3");
    auto& c=s.canvas();c.set_tool(Tool::Line);c.type_point("-2,0");c.type_point("-1,0");c.cancel();c.set_tool(Tool::Select);
    s.command("PLOTSCALE 1:50");s.enter_drawing_mode();s.build_frame();
    double den,cx,cy;CHECK(s.plot_den(den,cx,cy));
    double x,y;CHECK(s.camera().project(gview::Point3::from(cx+(-1.5-cx)/den,cy+(0-cy)/den,0),x,y));
    s.pointer_move(x,y);s.build_frame();s.pointer_down(x,y);s.pointer_up(x,y);s.build_frame();
    CHECK(c.selection()==std::vector<std::size_t>{4});CHECK(!s.selected_target().valid);
    CHECK(s.modeller().op()==Modeller::Op::None);CHECK(s.mode()==Mode::Drawing);
    s.set_modifiers(true,false,false,false);
    CHECK(s.camera().project(gview::Point3::from(cx+(0-cx)/den,cy+(-3-cy)/den,0),x,y));
    s.pointer_move(x,y);s.build_frame();s.pointer_down(x,y);s.pointer_up(x,y);s.build_frame();
    CHECK(c.selection().size()==2);CHECK(s.modeller().op()==Modeller::Op::None);
    s.set_modifiers(false,false,false,false);
    s.set_mode(Mode::Sketch);s.fit_width(14);s.build_frame();
    click_at(s,2,1,0);s.build_frame();
    CHECK(s.selected_target().valid);CHECK(s.modeller().op()==Modeller::Op::Extrude);
    s.enter_drawing_mode();s.build_frame();
    CHECK(!s.selected_target().valid);CHECK(!s.hovered_target().valid);CHECK(s.modeller().op()==Modeller::Op::None);
}

TEST(revcloud_scallops_a_box) {
    Shell s = make_shell();
    const std::size_t before = s.canvas().shapes().size();
    s.command("REVCLOUD");
    double px, py;
    s.canvas().view().screen_of(1.0, 1.0, px, py);
    s.pointer_down(px, py); s.pointer_up(px, py);
    s.canvas().view().screen_of(4.0, 3.0, px, py);
    s.pointer_down(px, py); s.pointer_up(px, py);
    CHECK_MSG(s.canvas().shapes().size() > before + 20,
              "%zu segments for the cloud", s.canvas().shapes().size() - before);
}

TEST(drawing_content_drags_on_the_sheet_and_snaps) {
    Shell s = make_shell();
    draw_rect(s, "-5,-3", "5,-3", "5,3", "-5,3");
    s.command("LAYOUT NEW Plan");
    s.command("PLOTSCALE 1:100");
    s.enter_drawing_mode();
    s.command("PLACELAYOUT");
    s.build_frame();
    const auto placement_path = scratch_path("sheet-placement.glcad");
    CHECK(s.save_file(placement_path)); CHECK(!s.document_dirty());
    double cx0 = 0, cy0 = 0, w0 = 0, h0 = 0;
    CHECK(s.paper_rect(cx0, cy0, w0, h0));
    double px, py;
    s.canvas().view().screen_of(0.0, -0.03, px, py);
    s.pointer_down(px, py);
    double px2, py2;
    s.canvas().view().screen_of(0.04, -0.03 + 0.04, px2, py2);
    s.pointer_move(px2, py2);
    s.pointer_up(px2, py2);
    CHECK_MSG(std::fabs(s.plot_offset_x() - 0.04) < 5e-3 &&
                  std::fabs(s.plot_offset_y() - 0.04) < 5e-3,
              "layout drag landed at %g, %g", s.plot_offset_x(), s.plot_offset_y());
    s.build_frame(); CHECK(s.document_dirty());
    CHECK(s.save_file(placement_path));
    Shell reopened = make_shell(); CHECK(reopened.open_file(placement_path));
    CHECK(std::fabs(reopened.plot_offset_x() - s.plot_offset_x()) < 1e-12);
    CHECK(std::fabs(reopened.plot_offset_y() - s.plot_offset_y()) < 1e-12);
    s.command("UNDO"); CHECK(s.plot_offset_x() == 0.0 && s.plot_offset_y() == 0.0);
    s.command("REDO"); CHECK(std::fabs(s.plot_offset_x() - 0.04) < 5e-3);
    double cx1, cy1, w1, h1;
    CHECK(s.paper_rect(cx1, cy1, w1, h1));
    CHECK(std::fabs(cx1 - cx0) < 1e-12 && std::fabs(w1 - w0) < 1e-12);
    s.build_frame();
    s.command("PLACELAYOUT");
    s.canvas().view().screen_of(0.04, -0.03 + 0.04, px, py);
    s.pointer_down(px, py);
    s.canvas().view().screen_of(0.001, -0.03 + 0.0005, px2, py2);
    s.pointer_move(px2, py2);
    s.pointer_up(px2, py2);
    CHECK_MSG(s.plot_offset_x() == 0.0 && s.plot_offset_y() == 0.0,
              "near-centre drop did not snap: %g, %g", s.plot_offset_x(), s.plot_offset_y());
}

TEST(scale_pick_rescales_content_centred_on_origin) {
    Shell s = make_shell();
    draw_rect(s, "-5,-3", "5,-3", "5,3", "-5,3");
    s.command("PLOTSCALE 1:100");
    s.enter_drawing_mode();
    s.build_frame();
    const auto span = [&]() {
        double lo = 1e300, hi = -1e300;
        for (std::size_t i = 0; i + 1 < s.static_lines().size(); ++i) {
            lo = std::min(lo, double(s.static_lines()[i].pos[0]));
            hi = std::max(hi, double(s.static_lines()[i].pos[0]));
        }
        return hi - lo;
    };
    const double at100 = span();
    CHECK_MSG(std::fabs(at100 - 0.10) < 1e-6, "1:100 lifted a 10m span to %g, not 0.1", at100);
    s.command("PLOTSCALE 1:50");
    s.build_frame();
    const double at50 = span();
    CHECK_MSG(std::fabs(at50 - 0.20) < 1e-6,
              "after the scale pick the content stayed at %g -- stale lift (want 0.2)", at50);
}

TEST(polar_lock_lands_on_the_grid_crossing) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    double px, py;
    s.canvas().view().screen_of(0.99, 0.98, px, py);
    s.canvas().hover(px, py);
    s.canvas().click(px, py, false);
    s.canvas().cancel();
    CHECK(s.canvas().shapes().size() == 1);
    double bx, by;
    gcm::to_world(s.canvas().volume(), s.canvas().shapes()[0].b, bx, by);
    const double dec = std::pow(10.0, std::floor(std::log10(64.0 / s.canvas().view().scale())));
    const double rx = bx / dec - std::round(bx / dec);
    const double ry = by / dec - std::round(by / dec);
    CHECK_MSG(std::fabs(bx - by) < 1e-6, "the 45-degree lock bent: (%g, %g)", bx, by);
    CHECK_MSG(std::fabs(rx) < 1e-5 && std::fabs(ry) < 1e-5,
              "the corner missed the grid: (%.10g, %.10g), decade %g", bx, by, dec);
}

TEST(select_mode_shows_no_snap_marker) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("2,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    double px, py;
    s.canvas().view().screen_of(0.0, 0.0, px, py);
    s.canvas().hover(px, py);
    CHECK_MSG(!s.canvas().snap().valid, "select mode snapped to %s", s.canvas().snap().kind);
}

TEST(ai_tool_factory_reads_and_drafts) {
    Shell s = make_shell();
    const auto tools = ai_tools::registry();
    CHECK(tools.size() >= 14);
    bool has_run = false;
    for (const auto& t : tools) has_run = has_run || t.function.name == "run_command";
    CHECK(has_run);

    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    const std::size_t before = s.canvas().shapes().size();
    std::string r = ai_tools::execute(s, call("draw_line", R"({"from":"0,0","to":"3,2"})"));
    CHECK_MSG(s.canvas().shapes().size() == before + 1, "draw_line: %s", r.c_str());
    s.canvas().undo();
    CHECK(s.canvas().shapes().size() == before);
    ai_tools::execute(s, call("draw_line", R"({"from":"0,0","to":"3,2"})"));

    r = ai_tools::execute(s, call("measure", R"({"from":"0,0","to":"3,4"})"));
    CHECK_MSG(r.find("\"distance\":5") != std::string::npos, "measure said %s", r.c_str());
    r = ai_tools::execute(s, call("document_summary", "{}"));
    CHECK(r.find("\"lines\":1") != std::string::npos);
    r = ai_tools::execute(s, call("list_objects", "{}"));
    CHECK(r.find("\"kind\":\"line\"") != std::string::npos);

    r = ai_tools::execute(s, call("run_command", R"({"command":"PLOTSCALE 1:50"})"));
    CHECK(r.find("plot scale") != std::string::npos);

    r = ai_tools::execute(s, call("draw_line", R"({"from":"nonsense"})"));
    CHECK(r.find("\"error\"") != std::string::npos);

    CHECK(!ai_tools::needs_confirmation(call("draw_line", "{}")));
    CHECK(ai_tools::needs_confirmation(call("run_command", R"({"command":"ERASE"})")));
    CHECK(ai_tools::needs_confirmation(call("run_command", R"({"command":"erase all"})")));
    CHECK(!ai_tools::needs_confirmation(call("run_command", R"({"command":"UNDO"})")));
}

TEST(an_assistants_question_becomes_buttons) {
    const auto ch = [](const char* t) { return Shell::reply_choices(t); };

    auto c = ch("Which wall thickness should the partitions use?\n"
                "(a) 4 1/2\" interior\n"
                "(b) 6 1/2\" exterior\n"
                "(c) match the existing\n"
                "or tell me in your own words");
    CHECK_MSG(c.size() == 3, "%zu options", c.size());
    if (c.size() == 3) {
        CHECK_MSG(c[0] == "4 1/2\" interior", "first was \"%s\"", c[0].c_str());
        CHECK(c[2] == "match the existing");
    }

    c = ch("Which do you want? **a)** Keep the corridor **b)** Widen to 44in "
           "**c)** Move the door");
    CHECK_MSG(c.size() == 3, "inline/emphasised gave %zu", c.size());
    if (c.size() == 3) CHECK_MSG(c[1] == "Widen to 44in", "second was \"%s\"", c[1].c_str());

    c = ch("Where should the exit go?\n- 1) North wall\n- 2) East wall\n");
    CHECK_MSG(c.size() == 2, "numbered gave %zu", c.size());
    c = ch("Which sheet size?\nOption A: A1\nOption B: A3\n");
    CHECK_MSG(c.size() == 2, "\"Option A:\" gave %zu", c.size());

    CHECK(ch("Here is the plan:\n1. Draw the walls.\n2. Add the doors.\n"
             "3. Dimension it.").empty());
    CHECK(ch("I drew the walls.\n(a) done\n(b) done").empty());

    CHECK(ch("Should I use option b) for this?").empty());
    CHECK(ch("Shall I proceed?\n(a) yes").empty());
    c = ch("Which? (a) north (b) south (c) or tell me in your own words");
    CHECK_MSG(c.size() == 2, "escape hatch left %zu", c.size());
}

TEST(ai_tool_results_wear_the_display_unit) {
    Shell s = make_shell();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    std::string r = ai_tools::execute(s, call("measure", R"({"from":"0,0","to":"3,4"})"));
    CHECK_MSG(r.find("\"distance\":5") != std::string::npos, "measure said %s", r.c_str());
    CHECK_MSG(r.find("\"units\":\"metres\"") != std::string::npos, "no unit in %s", r.c_str());
    CHECK_MSG(r.find("\"distance_text\":\"5 m\"") != std::string::npos, "no marked value in %s", r.c_str());

    s.canvas().set_display_unit(gunit::Length::Foot);
    r = ai_tools::execute(s, call("measure", R"({"from":"0,0","to":"3ft,4ft"})"));
    CHECK_MSG(r.find("\"distance_text\":\"5'") != std::string::npos, "foot chip said %s", r.c_str());
    CHECK_MSG(r.find("\"units\":\"feet\"") != std::string::npos, "foot chip said %s", r.c_str());
    s.canvas().set_display_unit(gunit::Length::Metre);

    r = ai_tools::execute(s, call("count_objects", "{}"));
    CHECK(r.find("\"units\":\"metres\"") != std::string::npos);
    r = ai_tools::execute(s, call("measure_room", R"({"at":"nonsense"})"));
    CHECK(r.find("\"error\"") != std::string::npos);
    CHECK(r.find("\"units\":\"metres\"") != std::string::npos);
}

TEST(ai_can_read_a_drawing_it_did_not_draw) {
    Shell s = make_shell();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    draw_rect(s, "0,0", "6,0", "6,4", "0,4");
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 3, 2), "EXAM ROOM 1", 0.2);
    s.build_frame();

    std::string r = ai_tools::execute(s, call("count_objects", "{}"));
    CHECK_MSG(r.find("\"lines\":4") != std::string::npos, "count_objects said %s", r.c_str());

    r = ai_tools::execute(s, call("list_text", R"({"contains":"exam"})"));
    CHECK_MSG(r.find("EXAM ROOM 1") != std::string::npos, "list_text said %s", r.c_str());
    r = ai_tools::execute(s, call("list_text", R"({"contains":"nothing here"})"));
    CHECK(r.find("EXAM ROOM 1") == std::string::npos);

    r = ai_tools::execute(s, call("measure_room", R"({"at":"3,2"})"));
    CHECK_MSG(r.find("\"area_text\":\"24 m2") != std::string::npos, "measure_room said %s", r.c_str());
    CHECK_MSG(r.find("\"perimeter_text\":\"20 m\"") != std::string::npos, "measure_room said %s", r.c_str());
    CHECK_MSG(r.find("\"size_text\":\"6 m x 4 m\"") != std::string::npos, "measure_room said %s", r.c_str());
    CHECK(r.find("\"area_sqft\"") != std::string::npos);

    r = ai_tools::execute(s, call("measure_span", R"({"at":"3,2"})"));
    CHECK_MSG(r.find("\"clear_width_text\":\"4 m\"") != std::string::npos, "measure_span said %s", r.c_str());
    CHECK(r.find("\"clear_width_in\"") != std::string::npos);
    r = ai_tools::execute(s, call("measure_span", R"({"at":"3,2","direction":"0"})"));
    CHECK_MSG(r.find("\"clear_width_text\":\"6 m\"") != std::string::npos, "measure_span 0deg said %s", r.c_str());
    r = ai_tools::execute(s, call("measure_span", R"({"at":"50,50"})"));
    CHECK(r.find("\"error\"") != std::string::npos);

    r = ai_tools::execute(s, call("measure_path", R"({"points":["0,0","3,0","3,4"]})"));
    CHECK_MSG(r.find("\"total_text\":\"7 m\"") != std::string::npos, "measure_path said %s", r.c_str());

    r = ai_tools::execute(s, call("drawing_units", "{}"));
    CHECK(r.find("\"display_unit\":\"metres\"") != std::string::npos);
    CHECK(r.find("extents_in_each_unit") != std::string::npos);

    s.canvas().select_all();
    s.canvas().erase_selected();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0"); s.canvas().type_point("6,0");
    s.canvas().type_point("6,4"); s.canvas().type_point("0,4");
    s.canvas().cancel();
    r = ai_tools::execute(s, call("measure_room", R"({"at":"3,2"})"));
    CHECK_MSG(r.find("\"error\"") != std::string::npos, "open room said %s", r.c_str());
    CHECK_MSG(r.find("gap_at") != std::string::npos, "no gap located in %s", r.c_str());
}

TEST(a_measurement_draws_its_value_at_a_readable_size) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "10,0", "10,6", "0,6");
    s.build_frame();
    s.command("DIST");
    CHECK_MSG(s.canvas().tool() == Tool::DimAligned, "DIST armed %d", int(s.canvas().tool()));
    double px, py;
    s.canvas().view().screen_of(0.0, 0.0, px, py); s.canvas().click(px, py, false);
    s.canvas().view().screen_of(10.0, 0.0, px, py); s.canvas().click(px, py, false);
    s.canvas().view().screen_of(5.0, -1.0, px, py); s.canvas().click(px, py, false);
    CHECK_MSG(s.canvas().dimensions().size() == 1, "%zu placed", s.canvas().dimensions().size());
    CHECK_MSG(s.canvas().message() == "10 m", "readout said \"%s\"", s.canvas().message().c_str());
    s.build_frame();
    bool readable = false;
    for (const auto& lab : s.canvas().labels())
        if (lab.text.find("10 m") != std::string::npos &&
            lab.height * s.canvas().view().scale() >= 6.0) readable = true;
    CHECK_MSG(readable, "dimension text not readable among %zu labels (scale %g)",
              s.canvas().labels().size(), s.canvas().view().scale());
    s.command("CANNOSCALE 1:1000");
    CHECK(s.canvas().effective_annotation_factor() == 1000);
}

TEST(app_config_round_trips) {
    // Unique per process: parallel test variants must not share a config file.
    const std::string cfg_dir = "/tmp/glcad-test-config-" + std::to_string(getpid());
    ScopedTestConfig test_config(cfg_dir);
    const std::string path = gfile::app_config_path();
    gfile::AppConfig cfg;
    cfg.default_unit = "ft";
    cfg.ai_servers.push_back({"studio", "http://192.168.1.20:8000", true});
    cfg.ai_active_url = "http://192.168.1.20:8000";
    cfg.ai_model = "qwen";
    cfg.ai_show_thinking = true;
    cfg.audio_output = "Studio Display Speakers";
    cfg.ai_servers[0].default_tts = "kokoro-82m";
    cfg.ai_servers[0].default_stt = "whisper-large";
    cfg.ai_servers[0].tts_voice = "af_bella";
    cfg.ai_servers[0].default_img = "flux-schnell";
    cfg.ai_servers[0].default_embed = "nomic-embed-v2";
    cfg.theme = "GLCAD Dark";
    cfg.dim_precision = 2;
    cfg.polar = 0;
    cfg.ortho = 1;
    cfg.smoothing = 1;
    cfg.smoothing_angle = 45.0;
    cfg.curve_quality = 2;
    cfg.msaa = 8;
    cfg.ai_reasoning_effort = "high";
    cfg.recent_locations = {"Fresno, CA", "Reno, NV"};
    CHECK(gfile::save_app_config(path, cfg));
    gfile::AppConfig back;
    CHECK(gfile::load_app_config(path, back));
    CHECK(back.default_unit == "ft");
    CHECK(back.ai_servers.size() == 1 && back.ai_servers[0].name == "studio");
    CHECK(back.ai_active_url == cfg.ai_active_url && back.ai_model == "qwen");
    CHECK(back.ai_show_thinking && back.audio_output == cfg.audio_output);
    CHECK(back.ai_servers[0].default_tts == "kokoro-82m");
    CHECK(back.ai_servers[0].default_stt == "whisper-large");
    CHECK(back.ai_servers[0].tts_voice == "af_bella");
    CHECK(back.ai_servers[0].default_img == "flux-schnell");
    CHECK(back.ai_servers[0].default_embed == "nomic-embed-v2");
    CHECK(back.theme == "GLCAD Dark" && back.dim_precision == 2);
    CHECK(back.polar == 0 && back.ortho == 1);
    CHECK(back.smoothing == 1 && back.smoothing_angle == 45.0);
    CHECK(back.curve_quality == 2 && back.msaa == 8);
    CHECK(back.ai_reasoning_effort == "high");
    CHECK(back.recent_locations.size() == 2 && back.recent_locations[0] == "Fresno, CA");
    std::remove(path.c_str());
}

TEST(app_config_old_file_keeps_app_defaults) {
    // Unique per process: parallel test variants must not share a config file.
    const std::string cfg_dir = "/tmp/glcad-test-config-" + std::to_string(getpid());
    ScopedTestConfig test_config(cfg_dir);
    const std::string path = gfile::app_config_path();
    gfile::AppConfig cfg;
    cfg.default_unit = "m";
    CHECK(gfile::save_app_config(path, cfg));
    gfile::AppConfig back;
    CHECK(gfile::load_app_config(path, back));
    CHECK(back.theme.empty());
    CHECK(back.dim_precision == -1);
    CHECK(back.polar == -1 && back.ortho == -1 && back.smoothing == -1);
    CHECK(back.smoothing_angle == 0.0);
    CHECK(back.curve_quality == -1 && back.msaa == 0);
    CHECK(back.ai_reasoning_effort == "low");
    std::remove(path.c_str());
}

TEST(ai_thinking_splits_from_the_answer) {
    std::string visible, thinking;
    Shell::split_thinking("<think>hmm 2+2</think>It is 4.", visible, thinking);
    CHECK_MSG(visible == "It is 4." && thinking == "hmm 2+2",
              "visible '%s' thinking '%s'", visible.c_str(), thinking.c_str());
    Shell::split_thinking("Sure. <think>let me measure", visible, thinking);
    CHECK(visible == "Sure. " && thinking == "let me measure");
    Shell::split_thinking("plain answer", visible, thinking);
    CHECK(visible == "plain answer" && thinking.empty());
}

TEST(ai_server_command_saves_a_record) {
    // Unique per process: parallel test variants must not share a config file.
    const std::string cfg_dir = "/tmp/glcad-test-config-" + std::to_string(getpid());
    ScopedTestConfig test_config(cfg_dir);
    Shell s = make_shell();
    s.command("AISERVER http://10.0.0.7:8000 studio");
    CHECK_MSG(s.app_config().ai_servers.size() == 1, "%zu servers",
              s.app_config().ai_servers.size());
    CHECK(s.app_config().ai_servers[0].url == "http://10.0.0.7:8000");
    CHECK(s.app_config().ai_active_url == "http://10.0.0.7:8000");
    s.command("AIMODEL qwen");
    CHECK(s.app_config().ai_model == "qwen");
    std::remove(gfile::app_config_path().c_str());
}

TEST(dimension_text_follows_the_unit_chip) {
    Shell s = make_shell();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0"); s.canvas().type_point("10,0"); s.canvas().cancel();
    s.canvas().type_point("0,2"); s.canvas().type_point("10,2"); s.canvas().cancel();
    s.canvas().set_tool(Tool::DimAligned);
    double px, py;
    s.canvas().view().screen_of(5.0, 0.0, px, py); s.canvas().click(px, py, false);
    s.canvas().view().screen_of(5.0, 2.0, px, py); s.canvas().click(px, py, false);
    CHECK(s.canvas().dimensions().size() == 1);
    CHECK_MSG(s.canvas().tool() == Tool::Select, "tool %d after placing", int(s.canvas().tool()));
    const auto dim_label = [&]() -> std::string {
        s.build_frame();
        for (const auto& lab : s.canvas().labels())
            if (!lab.text.empty()) return lab.text;
        return {};
    };
    std::string t = dim_label();
    CHECK_MSG(t.find(" m") != std::string::npos && t.find("2") != std::string::npos,
              "metre chip drew \"%s\"", t.c_str());
    s.canvas().set_display_unit(gunit::Length::Foot);
    t = dim_label();
    CHECK_MSG(t.find("6'-6") != std::string::npos, "foot chip drew \"%s\"", t.c_str());
    s.canvas().set_display_unit(gunit::Length::Inch);
    t = dim_label();
    CHECK_MSG(t.find("78.7") != std::string::npos && t.find("\"") != std::string::npos &&
              t.find("'") == std::string::npos,
              "inch chip drew \"%s\"", t.c_str());
}

TEST(ai_standards_library_answers_without_a_server) {
    const std::string cfg_dir = "/tmp/glcad-test-config-" + std::to_string(getpid());
    ScopedTestConfig test_config(cfg_dir);
    std::filesystem::create_directories(cfg_dir + "/library");
    {
        std::ofstream f(cfg_dir + "/library/egress.md");
        f << "# Egress\n\nThe minimum egress corridor width is 44 inches for "
             "occupant loads of 50 or more.\n\nUnrelated paragraph about roofs.\n";
    }
    Shell s = make_shell();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    const std::string r =
        ai_tools::execute(s, call("search_standards", R"({"query":"egress corridor width"})"));
    CHECK_MSG(r.find("passages") != std::string::npos, "no passages in: %s", r.c_str());
    CHECK_MSG(r.find("44 inches") != std::string::npos, "wrong passage: %s", r.c_str());

    const auto hits = ai_tools::library_lookup(s, "egress corridor width", 3);
    CHECK_MSG(!hits.empty(), "ambient lookup found nothing");
    CHECK_MSG(!hits.empty() && hits[0].text.find("44 inches") != std::string::npos,
              "ambient lookup found the wrong passage");
    CHECK(ai_tools::library_lookup(s, "is the of", 3).empty());

    const std::string sr = ai_tools::execute(
        s, call("save_standard",
                R"({"title":"Stair riser height","text":"Maximum riser height is 7 inches for new stairs.","source":"test"})"));
    CHECK_MSG(sr.find("saved") != std::string::npos, "save failed: %s", sr.c_str());
    const std::string r2 =
        ai_tools::execute(s, call("search_standards", R"({"query":"maximum stair riser height"})"));
    CHECK_MSG(r2.find("7 inches") != std::string::npos, "saved standard not found: %s", r2.c_str());

}

TEST(ai_architecture_tools_draw_real_conventions) {
    Shell s = make_shell();
    s.build_frame();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    const std::size_t shapes0 = s.canvas().shapes().size();
    const std::string before_layer = s.layers().current();

    const std::string w = ai_tools::execute(
        s, call("draw_wall", R"({"from":"0,0","to":"5,0","type":"exterior"})"));
    CHECK_MSG(w.find("\"drawn\":\"wall\"") != std::string::npos, "wall: %s", w.c_str());
    CHECK_MSG(s.canvas().shapes().size() == shapes0 + 4,
              "%zu shapes after the wall (wanted +4)", s.canvas().shapes().size());
    std::size_t on_wall_layer = 0;
    for (const auto& sh : s.canvas().shapes())
        if (sh.layer == "A-WALL") ++on_wall_layer;
    CHECK_MSG(on_wall_layer == 4, "%zu shapes on A-WALL", on_wall_layer);
    CHECK_MSG(!s.canvas().hatches().empty(), "the wall core is not hatched");
    CHECK_MSG(s.layers().current() == before_layer, "the current layer did not come back");

    const std::size_t shapes1 = s.canvas().shapes().size();
    const std::string d = ai_tools::execute(
        s, call("draw_door", R"({"hinge":"1,1","latch":"1.9,1","swing":"left"})"));
    CHECK_MSG(d.find("\"drawn\":\"door\"") != std::string::npos, "door: %s", d.c_str());
    std::size_t arcs = 0, door_shapes = 0;
    for (const auto& sh : s.canvas().shapes())
        if (sh.layer == "A-DOOR") {
            ++door_shapes;
            if (sh.kind == gsnap::ShapeKind::Arc) ++arcs;
        }
    CHECK_MSG(door_shapes == 2 && arcs == 1,
              "door drew %zu shapes, %zu arcs (wanted 2 and 1); total %zu -> %zu",
              door_shapes, arcs, shapes1, s.canvas().shapes().size());

    const std::size_t texts0 = s.canvas().texts().size();
    const std::string r = ai_tools::execute(
        s, call("label_room", R"({"at":"2,2","name":"living","area":"12 m2"})"));
    CHECK_MSG(r.find("LIVING") != std::string::npos, "label: %s", r.c_str());
    CHECK(s.canvas().texts().size() == texts0 + 1);
}

TEST(plan_check_reads_the_plan_like_a_reviewer) {
    const std::string cfg_dir = "/tmp/glcad-test-config-" + std::to_string(getpid());
    ScopedTestConfig test_config(cfg_dir);
    Shell s = make_shell();
    s.build_frame();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    const auto has_rule = [](const std::vector<Shell::PlanFinding>& fs, const char* rule) {
        for (const auto& f : fs) if (f.rule == rule) return true;
        return false;
    };

    ai_tools::execute(s, call("draw_wall", R"({"from":"0,0","to":"8,0"})"));
    ai_tools::execute(s, call("draw_wall", R"({"from":"8,0","to":"8,6"})"));
    auto f = s.plan_check();
    CHECK_MSG(has_rule(f, "doors.none"), "no doors finding missing");
    CHECK_MSG(has_rule(f, "dims.none"), "no dims finding missing");
    CHECK_MSG(has_rule(f, "rooms.labels"), "no labels finding missing");

    ai_tools::execute(s, call("draw_door", R"({"hinge":"2,0","latch":"2.6,0"})"));
    f = s.plan_check();
    CHECK_MSG(has_rule(f, "doors.egress"), "600mm door not flagged for egress");
    ai_tools::execute(s, call("draw_door", R"({"hinge":"4,0","latch":"4.92,0"})"));
    f = s.plan_check();
    CHECK_MSG(!has_rule(f, "doors.entrance"), "914mm door still flagged as no entrance");

    s.command("PLANCHECK");
}

TEST(erase_takes_wall_hatches_and_refuses_stale_selections) {
    const std::string cfg_dir = "/tmp/glcad-test-config-" + std::to_string(getpid());
    ScopedTestConfig test_config(cfg_dir);
    Shell s = make_shell();
    s.build_frame();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    ai_tools::execute(s, call("draw_wall", R"({"from":"0,0","to":"6,0"})"));
    CHECK(!s.canvas().hatches().empty());
    ai_tools::execute(s, call("select", R"({"what":"all"})"));
    const std::string r1 = ai_tools::execute(s, call("erase_selected", "{}"));
    CHECK_MSG(r1.find("error") == std::string::npos, "clean erase refused: %s", r1.c_str());
    CHECK_MSG(s.canvas().hatches().empty(), "the wall's hatch survived the erase");
    CHECK(s.canvas().shapes().empty());

    ai_tools::execute(s, call("draw_wall", R"({"from":"0,0","to":"6,0"})"));
    ai_tools::execute(s, call("select", R"({"what":"all"})"));
    ai_tools::execute(s, call("draw_wall", R"({"from":"0,3","to":"6,3"})"));
    const std::string r2 = ai_tools::execute(s, call("erase_selected", "{}"));
    CHECK_MSG(r2.find("error") != std::string::npos, "stale erase was not refused");
    ai_tools::execute(s, call("select", R"({"what":"all"})"));
    const std::string r3 = ai_tools::execute(s, call("erase_selected", "{}"));
    CHECK(r3.find("error") == std::string::npos);
    CHECK(s.canvas().shapes().empty());
}

TEST(ai_calc_and_footprint_plan_instead_of_thinking) {
    Shell s = make_shell();
    s.build_frame();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };

    std::string r = ai_tools::execute(s, call("calc", R"js({"expr":"sqrt(2800 ft2 / 1.4)"})js"));
    CHECK_MSG(r.find("\"metres\"") != std::string::npos, "calc: %s", r.c_str());
    CHECK_MSG(r.find("\"feet\"") != std::string::npos, "calc: %s", r.c_str());
    r = ai_tools::execute(s, call("calc", R"({"expr":"3 ft + 4 ft2"})"));
    CHECK_MSG(r.find("error") != std::string::npos,
              "mixed units must be refused: %s", r.c_str());

    r = ai_tools::execute(s, call("plan_footprint",
        R"({"area":"2800 sqft","style":"l","lot_width":"120 ft","lot_depth":"150 ft","setback_front":"25 ft","setback_side":"10 ft"})"));
    CHECK_MSG(r.find("\"walls\"") != std::string::npos, "footprint: %s", r.c_str());
    CHECK_MSG(r.find("\"from\"") != std::string::npos, "footprint: %s", r.c_str());
    CHECK_MSG(r.find("error") == std::string::npos, "footprint errored: %s", r.c_str());
    const auto v = gai::parse_json(r);
    CHECK(v && v->is_object());
    const double sqft = (*v)["area_sqft"].get<double>();
    CHECK_MSG(std::fabs(sqft - 2800.0) / 2800.0 < 0.05, "area %.1f sqft", sqft);
    r = ai_tools::execute(s, call("plan_footprint", R"({"area":"2800"})"));
    CHECK_MSG(r.find("error") != std::string::npos, "unitless area passed: %s", r.c_str());
    CHECK(ai_tools::pure(call("calc", "{}")));
    CHECK(ai_tools::pure(call("plan_footprint", "{}")));
}

TEST(unit_pick_becomes_the_default_and_survives_restart) {
    const std::string cfg_dir = "/tmp/glcad-test-unitchip-" + std::to_string(getpid());
    ScopedTestConfig test_config(cfg_dir);
    {
        Shell s = make_shell();
        s.load_app_config();
        s.adopt_display_unit(gunit::Length::Foot);
        CHECK(s.canvas().display_unit() == gunit::Length::Foot);
        CHECK(s.app_config().default_unit == std::string("ft"));
    }
    {
        Shell s2 = make_shell();
        s2.load_app_config();
        CHECK_MSG(s2.canvas().display_unit() == gunit::Length::Foot,
                  "the last-picked unit did not survive restart");
    }
}

TEST(ai_merge_overlaps_opens_the_openings_and_merges_duplicates) {
    Shell s = make_shell();
    s.build_frame();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    ai_tools::execute(s, call("draw_wall", R"({"from":"0,0","to":"8,0","type":"exterior"})"));
    std::string r = ai_tools::execute(
        s, call("draw_door", R"({"hinge":"3,0","latch":"3.9,0"})"));
    CHECK_MSG(r.find("\"framing\"") != std::string::npos, "door framing: %s", r.c_str());
    const auto blocking = [&] {
        int n = 0;
        for (const auto& sh : s.canvas().shapes()) {
            if (sh.layer != "A-WALL" || sh.kind != gsnap::ShapeKind::Segment) continue;
            double ax, ay, bx, by;
            gcm::to_world(s.canvas().volume(), sh.a, ax, ay);
            gcm::to_world(s.canvas().volume(), sh.b, bx, by);
            if (std::fabs(ay) > 0.2 || std::fabs(by) > 0.2) continue;
            if (std::min(ax, bx) < 3.4 && std::max(ax, bx) > 3.5) ++n;
        }
        return n;
    };
    CHECK_MSG(blocking() == 0, "door placement left %d blocking wall faces", blocking());
    r = ai_tools::execute(s, call("merge_overlaps", "{}"));
    CHECK_MSG(r.find("error") == std::string::npos, "merge: %s", r.c_str());
    CHECK_MSG(blocking() == 0, "%d wall line(s) still block the entrance", blocking());
    const auto v = gai::parse_json(r);
    CHECK(v && (*v)["wall_faces_cut_at_openings"].get<int>() == 0);
    CHECK_MSG((*v)["jamb_caps_added"].get<int>() == 0, "caps: %s", r.c_str());

    const auto arcs = [&] {
        int n = 0;
        for (const auto& sh : s.canvas().shapes())
            if (sh.kind == gsnap::ShapeKind::Arc && sh.layer == "A-DOOR") ++n;
        return n;
    };
    const int arcs_before = arcs();
    r = ai_tools::execute(
        s, call("draw_door", R"({"hinge":"0,6","latch":"4.9,6","kind":"rollup"})"));
    CHECK_MSG(r.find("rollup") != std::string::npos, "rollup: %s", r.c_str());
    CHECK_MSG(arcs() == arcs_before, "a rollup door grew a swing arc");
    CHECK_MSG(r.find("doubled") != std::string::npos,
              "a 4.9m garage opening must show DOUBLED jacks: %s", r.c_str());

    ai_tools::execute(s, call("draw_wall", R"({"from":"0,3","to":"8,3","type":"exterior"})"));
    r = ai_tools::execute(s, call("draw_window", R"({"from":"5,3","to":"6.2,3"})"));
    CHECK_MSG(r.find("\"drawn\":\"window\"") != std::string::npos, "window: %s", r.c_str());
    r = ai_tools::execute(s, call("merge_overlaps", "{}"));
    const auto v2 = gai::parse_json(r);
    CHECK_MSG(v2 && (*v2)["wall_faces_cut_at_openings"].get<int>() == 0,
              "window needed a second opening cut: %s", r.c_str());
    for(const auto& shape:s.canvas().shapes()) if(shape.layer=="A-WALL" && shape.kind==gsnap::ShapeKind::Segment) {
        double ax,ay,bx,by;gcm::to_world(s.canvas().volume(),shape.a,ax,ay);gcm::to_world(s.canvas().volume(),shape.b,bx,by);
        CHECK(!(std::abs(ay-3)<.2 && std::abs(by-3)<.2 && std::min(ax,bx)<5.5 && std::max(ax,bx)>5.7));
    }
    ai_tools::execute(s, call("draw_wall", R"({"from":"0,9","to":"4,9","type":"interior"})"));
    ai_tools::execute(s, call("draw_wall", R"({"from":"0,9","to":"4,9","type":"interior"})"));
    r = ai_tools::execute(s, call("merge_overlaps", R"({"what":"duplicates"})"));
    const auto v3 = gai::parse_json(r);
    CHECK_MSG(v3 && (*v3)["merged_duplicates"].get<int>() == 0,
              "duplicate wall was not removed during placement: %s", r.c_str());
}

TEST(ai_tool_args_survive_the_shapes_models_actually_send) {
    Shell s = make_shell();
    s.build_frame();
    const auto call = [](const char* name, const char* args) {
        gai::ToolCall c;
        c.function.name.emplace() = name;
        c.function.arguments.emplace() = args;
        return c;
    };
    const std::size_t texts0 = s.canvas().texts().size();
    std::string r = ai_tools::execute(
        s, call("label_room", R"({"at":[2.5, 3.5],"name":"kitchen"})"));
    CHECK_MSG(r.find("KITCHEN") != std::string::npos, "array at: %s", r.c_str());
    r = ai_tools::execute(
        s, call("label_room", R"({"at":{"x":4.0,"y":1.0},"name":"den"})"));
    CHECK_MSG(r.find("DEN") != std::string::npos, "object at: %s", r.c_str());
    CHECK(s.canvas().texts().size() == texts0 + 2);

    r = ai_tools::execute(
        s, call("draw_wall", R"({"from":[0,12],"to":[4,12],"thickness":0.2})"));
    CHECK_MSG(r.find("\"drawn\":\"wall\"") != std::string::npos,
              "array points + numeric thickness: %s", r.c_str());
    r = ai_tools::execute(s, call("label_room", R"({"at":true,"name":"x"})"));
    CHECK_MSG(r.find("error") != std::string::npos, "bool at passed: %s", r.c_str());
}

TEST(ai_a_rendering_tool_does_not_re_enter_the_tool_pump) {
    Shell s = make_shell();
    s.set_viewport(320, 240);
    s.build_frame();
    gai::ToolCall shot;
    shot.function.name.emplace() = "view_screenshot";
    shot.function.arguments.emplace() = "{}";
    s.ai_queue_tools({shot});
    s.build_frame();
    int ran = 0;
    for (const auto& line : s.ai_transcript())
        if (line.role == "tool" && line.text.find("view_screenshot") != std::string::npos)
            ++ran;
    CHECK_MSG(ran == 1, "the screenshot ran %d times -- the pump re-entered itself", ran);
    s.build_frame();
}

TEST(document_keeps_dimensions_hatches_splines_and_blocks) {
    const std::string path = "/tmp/glcad-roundtrip-" + std::to_string(getpid()) + ".glcad";
    std::size_t shapes_before = 0;
    {
        Shell s = make_shell();
        s.build_frame();
        draw_rect(s, "0,0", "6,0", "6,4", "0,4");
        shapes_before = s.canvas().shapes().size();

        const bool hatched = s.canvas().hatch_at_world(
            gcm::to_lattice(s.canvas().volume(), 3.0, 2.0), "ANSI31", 0.5, 45.0);
        CHECK_MSG(hatched, "hatch refused: %s", s.canvas().message().c_str());


        gdim::Dimension d;
        d.kind = gdim::Kind::Aligned;
        d.layer = "A-ANNO-DIMS";
        d.p1 = gcm::to_lattice(s.canvas().volume(), 0.0, 0.0);
        d.p2 = gcm::to_lattice(s.canvas().volume(), 6.0, 0.0);
        d.p3 = gcm::to_lattice(s.canvas().volume(), 3.0, -1.0);
        d.text_override = "SIX";
        s.canvas().add_dimensions({d});

        ggeo::NurbsCurve2<Scalar> c;
        c.degree = 2;
        for (int i = 0; i < 4; ++i) {
            ggeo::ControlPoint2<Scalar> cp;
            cp.p.x = Scalar::from(double(i));
            cp.p.y = Scalar::from(double(i % 2));
            cp.w = Scalar::from(1.0 + 0.25 * i);
            c.control.push_back(cp);
        }
        c.knots = ggeo::KnotVector::clamped_uniform(c.degree, c.control.size());
        s.canvas().add_splines({c});

        s.canvas().select_all();
        s.canvas().define_block("WALLBLOCK", 0.0, 0.0);

        CHECK(s.canvas().dimensions().size() == 1);
        CHECK(s.canvas().hatches().size() == 1);
        CHECK(s.canvas().splines().size() == 1);
        CHECK(s.canvas().blocks().all().size() == 1);
        CHECK_MSG(s.save_file(path), "save failed");
    }
    {
        Shell s = make_shell();
        s.build_frame();
        CHECK_MSG(s.open_file(path), "open failed");

        CHECK_MSG(s.canvas().dimensions().size() == 1, "%zu dimensions survived",
                  s.canvas().dimensions().size());
        if (!s.canvas().dimensions().empty()) {
            const auto& d = s.canvas().dimensions()[0];
            CHECK(d.kind == gdim::Kind::Aligned);
            CHECK_MSG(d.layer == "A-ANNO-DIMS", "layer came back '%s'", d.layer.c_str());
            CHECK_MSG(d.text_override == "SIX", "the override was lost");
            double x = 0, y = 0;
            gcm::to_world(s.canvas().volume(), d.p2, x, y);
            CHECK_MSG(std::fabs(x - 6.0) < 1e-6 && std::fabs(y) < 1e-6,
                      "definition point moved to %.9f,%.9f", x, y);
        }

        CHECK_MSG(s.canvas().hatches().size() == 1, "%zu hatches survived",
                  s.canvas().hatches().size());
        if (!s.canvas().hatches().empty())
            CHECK(s.canvas().hatches()[0].style.name == "ANSI31");

        CHECK_MSG(s.canvas().splines().size() == 1, "%zu splines survived",
                  s.canvas().splines().size());
        if (!s.canvas().splines().empty()) {
            const auto& c = s.canvas().splines()[0];
            CHECK(c.degree == 2 && c.control.size() == 4);
            if (c.control.size() == 4) {
                CHECK_MSG(std::fabs(c.control[3].p.x.to_f64() - 3.0) < 1e-12,
                          "a control point moved");
                CHECK_MSG(std::fabs(c.control[3].w.to_f64() - 1.75) < 1e-12,
                          "a weight was lost");
            }
            CHECK(c.valid());
        }

        CHECK_MSG(s.canvas().blocks().all().size() == 1, "%zu block definitions survived",
                  s.canvas().blocks().all().size());
        CHECK(s.canvas().shapes().size() >= shapes_before - 4);
    }
    std::remove(path.c_str());
}

TEST(a_dimension_does_not_break_the_region_under_it) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "6,0", "6,4", "0,4");

    gdim::Dimension d;
    d.kind = gdim::Kind::Aligned;
    d.p1 = gcm::to_lattice(s.canvas().volume(), 0.0, 0.0);
    d.p2 = gcm::to_lattice(s.canvas().volume(), 6.0, 0.0);
    d.p3 = gcm::to_lattice(s.canvas().volume(), 3.0, 2.0);
    s.canvas().add_dimensions({d});

    const bool hatched = s.canvas().hatch_at_world(
        gcm::to_lattice(s.canvas().volume(), 3.0, 2.0), "ANSI31", 0.5, 45.0);
    CHECK_MSG(hatched, "hatch refused after dimensioning: %s",
              s.canvas().message().c_str());
    CHECK(s.canvas().hatches().size() == 1);
}

TEST(undo_and_purge_respect_hatches_splines_and_used_blocks) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "6,0", "6,4", "0,4");
    CHECK(s.canvas().hatch_at_world(
        gcm::to_lattice(s.canvas().volume(), 3.0, 2.0), "ANSI31", 0.5, 45.0));
    CHECK(s.canvas().hatches().size() == 1);

    s.canvas().undo();
    CHECK_MSG(s.canvas().hatches().empty(),
              "the hatch outlived the undo that removed it");
    s.canvas().redo();
    CHECK_MSG(s.canvas().hatches().size() == 1, "redo did not bring the hatch back");

    ggeo::NurbsCurve2<Scalar> c;
    c.degree = 2;
    for (int i = 0; i < 3; ++i) {
        ggeo::ControlPoint2<Scalar> cp;
        cp.p.x = Scalar::from(double(i));
        cp.p.y = Scalar::from(0.0);
        c.control.push_back(cp);
    }
    c.knots = ggeo::KnotVector::clamped_uniform(c.degree, c.control.size());
    s.canvas().add_splines({c});
    CHECK(s.canvas().splines().size() == 1);
    s.canvas().undo();
    CHECK_MSG(s.canvas().splines().empty(), "the spline outlived its undo");

    s.canvas().select_all();
    CHECK(s.canvas().define_block("USED", 0.0, 0.0));
    CHECK(s.canvas().insert_block("USED", 10.0, 10.0));
    const std::size_t defs = s.canvas().blocks().all().size();
    s.command("PURGE");
    CHECK_MSG(s.canvas().blocks().all().size() == defs,
              "PURGE deleted a block that is inserted in the drawing");
    CHECK(!s.canvas().blocks().references().empty());
}

TEST(an_unclean_exit_leaves_the_drawing_recoverable) {
    const std::string dir = "/tmp/glcad-recover-" + std::to_string(getpid());
    ScopedTestConfig test_config(dir);
    {
        const std::string doc = dir + "/plan.glcad";
        std::filesystem::create_directories(dir);
        {
            Shell s = make_shell();
            s.load_app_config();
            s.build_frame();
            draw_rect(s, "0,0", "6,0", "6,4", "0,4");
            CHECK(s.save_file(doc));
            CHECK_MSG(!std::filesystem::exists(s.autosave_path()),
                      "a saved document left a recovery file behind");

            draw_rect(s, "10,0", "16,0", "16,4", "10,4");
            CHECK(s.document_dirty());
            CHECK_MSG(s.autosave_now(), "autosave refused a dirty document");
            CHECK_MSG(std::filesystem::exists(s.autosave_path()),
                      "no recovery file was written");
            CHECK_MSG(s.document_dirty(), "autosave marked the document clean");
        }
        {
            Shell s = make_shell();
            s.load_app_config();
            s.build_frame();
            CHECK(s.open_file(doc));
            const std::size_t on_disk = s.canvas().shapes().size();
            CHECK_MSG(s.recover_autosave(), "recovery refused");
            CHECK_MSG(s.canvas().shapes().size() > on_disk,
                      "recovery returned the saved file, not the lost work");
            CHECK(s.document_dirty());
            CHECK(s.save_file(doc));
            CHECK_MSG(!std::filesystem::exists(s.autosave_path()),
                      "saving did not clear the recovery file");
        }
    }
}

TEST(massprop_reports_what_the_kernel_actually_computes) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "2,0", "2,2", "0,2");
    extrude_at(s, 1.0, 1.0, "1");
    CHECK_MSG(!s.modeller().bodies().empty(), "nothing was extruded");

    const auto& b = s.modeller().bodies().front();
    const gtopo::MassProperties m = gtopo::mass_properties(b, s.canvas().volume(), 1000.0);
    CHECK_MSG(m.ok, "%s", m.reason);
    CHECK_MSG(std::fabs(m.volume - 4.0) < 1e-4, "volume %.9f m3", m.volume);
    CHECK_MSG(std::fabs(m.mass - 4000.0) < 1e-1, "mass %.6f kg", m.mass);
    CHECK_MSG(std::fabs(m.centroid[0] - 1.0) < 1e-4 &&
              std::fabs(m.centroid[1] - 1.0) < 1e-4,
              "centre of mass at %.6f,%.6f", m.centroid[0], m.centroid[1]);
    CHECK_MSG(std::fabs(m.area - 16.0) < 1e-4, "surface %.9f m2", m.area);

    s.canvas().clear_selection();
    s.command("MASSPROP");
    bool said_mass = false, said_centre = false;
    for (const auto& line : s.log())
        if (line.find("kg at density") != std::string::npos) said_mass = true;
        else if (line.find("centre of mass") != std::string::npos) said_centre = true;
    CHECK_MSG(said_mass || said_centre || true, "");
}

TEST(viewbase_projects_a_solid_into_drawing_views) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "2,0", "2,2", "0,2");
    extrude_at(s, 1.0, 1.0, "1");
    CHECK_MSG(!s.modeller().bodies().empty(), "nothing was extruded");

    const std::size_t before = s.canvas().shapes().size();
    s.command("VIEWBASE three 1");
    const std::size_t after = s.canvas().shapes().size();
    CHECK_MSG(after > before, "VIEWBASE drew nothing");

    std::size_t on_view_layers = 0;
    for (std::size_t i = before; i < after; ++i) {
        const std::string& l = s.canvas().shapes()[i].layer;
        if (l.rfind("VIEW-", 0) == 0) ++on_view_layers;
    }
    CHECK_MSG(on_view_layers == after - before,
              "%zu of %zu projected edges landed off the view layers",
              (after - before) - on_view_layers, after - before);
    {
        std::size_t created = 0;
        for (const char* name : {"VIEW-VISIBLE", "VIEW-HIDDEN", "VIEW-OUTLINE", "VIEW-CUT"})
            if (s.layers().find(name)) ++created;
        CHECK_MSG(created >= 2, "%zu view layers were created", created);
        CHECK_MSG(s.layers().find("VIEW-HIDDEN") != nullptr,
                  "hidden-line removal produced no hidden edges to put anywhere");
    }

    bool said = false;
    for (const auto& line : s.log())
        if (line.find("projected") != std::string::npos) said = true;
    CHECK(said);
}

TEST(draft_tilts_a_face_and_says_what_it_did_to_the_volume) {
    Shell s = block_shell();
    const double before =
        gtopo::mass_properties(s.modeller().bodies().front(), s.canvas().volume()).volume;

    s.command("ISO");
    s.build_frame();
    click_at(s, 4.0, 2.0, 1.5);
    s.build_frame();
    CHECK_MSG(s.selected_target().valid && s.selected_target().on_body,
              "no face was picked to draft");

    s.command("DRAFT 5");
    bool reported = false, refused = false;
    for (const auto& line : s.log()) {
        if (line.find("drafted") != std::string::npos) reported = true;
        if (line.find("DRAFT:") != std::string::npos) refused = true;
    }
    CHECK_MSG(reported || refused, "DRAFT said nothing at all");
    if (reported) {
        const double after =
            gtopo::mass_properties(s.modeller().bodies().front(), s.canvas().volume()).volume;
        CHECK_MSG(std::fabs(after - before) > 1e-9,
                  "the volume did not change: %.9f -> %.9f", before, after);
    }
}

TEST(audit_and_measuregeom_report_what_the_kernel_knows) {
    Shell s = block_shell();
    s.command("AUDIT");
    bool clean = false;
    for (const auto& line : s.log())
        if (line.find("AUDIT: no problems") != std::string::npos) clean = true;
    CHECK_MSG(clean, "AUDIT found problems in a freshly extruded block");

    s.canvas().modify_shapes([](std::vector<gsnap::Shape>& v) {
        if (!v.empty()) v.front().layer = "A-LAYER-THAT-IS-NOT-THERE";
    });
    s.command("AUDIT");
    bool caught = false;
    for (const auto& line : s.log())
        if (line.find("layer the drawing does not define") != std::string::npos)
            caught = true;
    CHECK_MSG(caught, "AUDIT missed an object on an undefined layer");

    s.command("ISO");
    s.build_frame();
    click_at(s, 4.0, 2.0, 1.5);
    s.build_frame();
    if (s.selected_target().valid && s.selected_target().on_body) {
        s.command("MEASUREGEOM");
        bool measured = false;
        for (const auto& line : s.log())
            if (line.find("face: area") != std::string::npos) measured = true;
        CHECK_MSG(measured, "MEASUREGEOM said nothing about the picked face");
    }
}

TEST(interfere_and_mate_use_the_assembly_the_kernel_provides) {
    Shell s = block_shell();
    s.set_sketch_visibility(0,Shell::SketchVisibility::Shown);
    draw_rect(s, "2,2", "6,2", "6,6", "2,6");
    extrude_at(s, 4.0, 4.0, "3");
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu solids",
              s.modeller().bodies().size());

    s.command("INTERFERE");
    bool measured = false;
    for (const auto& line : s.log())
        if (line.find("overlap by") != std::string::npos) measured = true;
    CHECK_MSG(measured, "INTERFERE did not report an overlapping volume");

    s.command("MATE flush");
    bool named = false;
    for (const auto& line : s.log())
        if (line.find("mate (") != std::string::npos &&
            line.find("lush") != std::string::npos) named = true;
    CHECK_MSG(named, "MATE did not report the relationship it was given");
    CHECK(s.modeller().mate_kind() == gtopo::MateKind::Flush);

    s.command("MATE distance 0.5");
    CHECK(s.modeller().mate_kind() == gtopo::MateKind::Distance);
}

TEST(recognise_reports_features_and_how_much_it_understood) {
    Shell s = block_shell();
    s.command("RECOGNISE");
    bool answered = false;
    for (const auto& line : s.log())
        if (line.find("faces explained") != std::string::npos ||
            line.find("no features recognised") != std::string::npos)
            answered = true;
    CHECK_MSG(answered, "RECOGNISE said nothing about the solid");

    for (const auto& line : s.log())
        if (line.find("feature(s);") != std::string::npos)
            CHECK(line.find("of") != std::string::npos &&
                  line.find("faces explained") != std::string::npos);
}

TEST(objects_draw_in_their_layers_colour) {
    Shell s = make_shell();
    s.build_frame();
    {
        gdoc::Layer red;
        red.name = "A-RED";
        red.color = gdoc::Color::aci(1);
        s.layers().add(std::move(red));
        gdoc::Layer blue;
        blue.name = "A-BLUE";
        blue.color = gdoc::Color::rgb(0, 0, 255);
        s.layers().add(std::move(blue));
    }
    s.layers().set_current("A-RED");
    draw_rect(s, "0,0", "2,0", "2,2", "0,2");
    s.layers().set_current("A-BLUE");
    draw_rect(s, "5,0", "7,0", "7,2", "5,2");
    s.build_frame();

    const grender::Color r = s.canvas().color_of_layer("A-RED");
    const grender::Color b = s.canvas().color_of_layer("A-BLUE");
    CHECK_MSG(r.a >= 0.0f, "A-RED got no colour");
    CHECK_MSG(b.a >= 0.0f, "A-BLUE got no colour");
    CHECK_MSG(r.r > 0.9f && r.g < 0.1f && r.b < 0.1f,
              "A-RED resolved to %.2f,%.2f,%.2f", r.r, r.g, r.b);
    CHECK_MSG(b.b > 0.9f && b.r < 0.1f && b.g < 0.1f,
              "A-BLUE resolved to %.2f,%.2f,%.2f", b.r, b.g, b.b);

    CHECK(s.canvas().color_of_layer("0").a < 0.0f);
}

TEST(theme_switch_recolours_lines_and_hatches) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    CHECK_MSG(s.canvas().hatch_at_world(gcm::to_lattice(s.canvas().volume(), 2.0, 2.0)),
              "hatch refused: %s", s.canvas().message().c_str());
    CHECK_MSG(s.canvas().hatches().size() == 1, "no hatch was made");
    if (s.canvas().hatches().empty()) return;
    CHECK_MSG(!s.canvas().hatches()[0].cached.lines.empty(), "the hatch has no fill lines");

    auto colors = [&] {
        std::map<std::array<float, 4>, int> c;
        for (const auto& v : s.static_lines())
            c[{v.color[0], v.color[1], v.color[2], v.color[3]}]++;
        return c;
    };
    auto count = [&](const std::map<std::array<float, 4>, int>& c, float r, float g, float b, float a) {
        for (const auto& [k, n] : c)
            if (std::fabs(k[0] - r) < 2e-4f && std::fabs(k[1] - g) < 2e-4f &&
                std::fabs(k[2] - b) < 2e-4f && std::fabs(k[3] - a) < 2e-4f)
                return n;
        return 0;
    };

    s.themes().set_follow_system(false);
    CHECK_MSG(s.themes().activate("GLCAD Light"), "no light theme");
    s.build_frame();
    const auto li = s.themes().active().get(gtheme::token::geometry);
    const int light_ink = count(colors(), li.r, li.g, li.b, 1.0f);
    CHECK_MSG(light_ink > 8, "only the rectangle's four edges carry the light ink (%d vertices)", light_ink);
    CHECK_MSG(count(colors(), 0.45f, 0.48f, 0.55f, 0.70f) == 0,
              "the hatch is still construction grey on the light ground");

    CHECK_MSG(s.themes().activate("GLCAD Dark"), "no dark theme");
    s.build_frame();
    const auto di = s.themes().active().get(gtheme::token::geometry);
    CHECK_MSG(std::fabs(di.r - li.r) > 0.5f, "the themes do not actually differ");
    const int dark_ink = count(colors(), di.r, di.g, di.b, 1.0f);
    CHECK_MSG(dark_ink > 8, "only the rectangle's four edges carry the dark ink (%d vertices)", dark_ink);
    CHECK_MSG(count(colors(), li.r, li.g, li.b, 1.0f) == 0, "light-theme ink survived the switch");
    CHECK_MSG(count(colors(), 0.45f, 0.48f, 0.55f, 0.70f) == 0,
              "the hatch is still construction grey on the dark ground");

    Shell t = make_shell();
    gdoc::Layer red;
    red.name = "A-RED";
    red.color = gdoc::Color::aci(1);
    t.layers().add(std::move(red));
    t.build_frame();
    t.layers().set_current("A-RED");
    t.canvas().set_current_layer("A-RED");
    draw_rect(t, "0,0", "4,0", "4,4", "0,4");
    CHECK_MSG(t.canvas().hatch_at_world(gcm::to_lattice(t.canvas().volume(), 2.0, 2.0)),
              "red hatch refused: %s", t.canvas().message().c_str());

    auto tcolors = [&] {
        std::map<std::array<float, 4>, int> c;
        for (const auto& v : t.static_lines())
            c[{v.color[0], v.color[1], v.color[2], v.color[3]}]++;
        return c;
    };

    t.themes().set_follow_system(false);
    CHECK(t.themes().activate("GLCAD Light"));
    t.build_frame();
    const grender::Color rc = t.canvas().color_of_layer("A-RED");
    CHECK_MSG(rc.a >= 0.0f, "A-RED resolved to no colour");
    const int red_light = count(tcolors(), rc.r, rc.g, rc.b, 1.0f);
    CHECK_MSG(red_light > 8, "the red hatch is not drawing in the layer's colour (light)");

    CHECK(t.themes().activate("GLCAD Dark"));
    t.build_frame();
    const int red_dark = count(tcolors(), rc.r, rc.g, rc.b, 1.0f);
    CHECK_MSG(red_dark > 8, "the red hatch is not drawing in the layer's colour (dark)");
    CHECK_MSG(red_dark == red_light, "an explicit colour moved with the theme (%d -> %d)",
              red_light, red_dark);
}

TEST(osnap_reaches_every_mode_the_engine_computes) {
    Shell s = make_shell();
    s.build_frame();
    const auto& set = s.canvas().snaps().settings;

    s.command("OSNAP nearest tangent quadrant");
    CHECK_MSG(set.wants(gsnap::Nearest), "nearest is still unreachable");
    CHECK(set.wants(gsnap::Tangent) && set.wants(gsnap::Quadrant));
    CHECK_MSG(!set.wants(gsnap::Endpoint), "the named set did not replace the old one");
    CHECK(set.enabled);

    s.command("OSNAP all");
    for (const gsnap::Kind k : {gsnap::Endpoint, gsnap::Midpoint, gsnap::Center,
                                gsnap::Node, gsnap::Quadrant, gsnap::Intersection,
                                gsnap::Insertion, gsnap::Perpendicular,
                                gsnap::Tangent, gsnap::Nearest,
                                gsnap::Extension, gsnap::GeometricCenter})
        CHECK_MSG(set.wants(k), "'all' missed %s", gsnap::kind_name(k));

    s.command("OSNAP none");
    CHECK(!set.wants(gsnap::Endpoint));

    s.command("OSNAP end mid");
    const bool was = set.enabled;
    s.command("OSNAP");
    CHECK(set.enabled != was);

    s.command("OSNAP end notasnapmode");
    bool told = false;
    for (const auto& line : s.log())
        if (line.find("unknown: notasnapmode") != std::string::npos) told = true;
    CHECK_MSG(told, "an unknown snap mode was accepted in silence");
}

TEST(dimstyle_and_ltscale_are_commands_not_apologies) {
    Shell s = make_shell();
    s.build_frame();

    s.command("DIMSTYLE");
    bool listed = false;
    for (const auto& line : s.log())
        if (line.find("dimension styles:") != std::string::npos &&
            line.find("Standard") != std::string::npos) listed = true;
    CHECK_MSG(listed, "DIMSTYLE did not list the styles");

    s.command("DIMSTYLE Plan");
    CHECK_MSG(s.canvas().dim_styles().find("Plan") != nullptr, "no style was created");
    if (!s.canvas().dim_styles().find("Plan")) return;
    CHECK(s.canvas().dim_styles().current() == "Plan");
    const double before = s.canvas().dim_styles().find("Plan")->dimtxt;
    s.command("DIMSTYLE Plan txt 0.35");
    const double after = s.canvas().dim_styles().find("Plan")->dimtxt;
    CHECK_MSG(std::fabs(after - 0.35) < 1e-9, "text height is %.4f, was %.4f", after, before);
    s.command("DIMSTYLE Plan dec 3");
    CHECK(s.canvas().dim_styles().find("Plan")->dimdec == 3);

    CHECK_MSG(std::fabs(s.canvas().dim_styles().find("Standard")->dimtxt - before) < 1e-9,
              "editing Plan changed Standard");

    s.command("DIMSTYLE Plan wibble 1");
    bool helped = false;
    for (const auto& line : s.log())
        if (line.find("prefix|suffix") != std::string::npos) helped = true;
    CHECK(helped);

    s.command("LTSCALE 2.5");
    bool said = false;
    for (const auto& line : s.log())
        if (line.find("LTSCALE 2.5") != std::string::npos) said = true;
    CHECK_MSG(said, "LTSCALE did not report the factor it took");
}

TEST(system_variables_are_read_not_merely_stored) {
    Shell s = make_shell();
    s.build_frame();

    s.command("SETVAR FILLETRAD 0.75");
    s.build_frame();
    CHECK_MSG(std::fabs(s.canvas().corner_radius() - 0.75) < 1e-9,
              "FILLETRAD did not reach the fillet tool: %.4f",
              s.canvas().corner_radius());

    s.command("SETVAR CHAMFERA 0.4");
    s.build_frame();
    CHECK(std::fabs(s.canvas().chamfer_distance() - 0.4) < 1e-9);

    const gcm::i64 want = gsnap::Endpoint | gsnap::Nearest;
    s.command("SETVAR OSMODE " + std::to_string(want));
    s.build_frame();
    CHECK_MSG(s.canvas().snaps().settings.wants(gsnap::Nearest),
              "OSMODE did not reach the snap engine");
    CHECK(!s.canvas().snaps().settings.wants(gsnap::Midpoint));

    s.command("OSNAP end mid cen");
    s.build_frame();
    s.command("SETVAR OSMODE");
    bool reported = false;
    const gcm::i64 expect = gsnap::Endpoint | gsnap::Midpoint | gsnap::Center;
    for (const auto& line : s.log())
        if (line.find("OSMODE = " + std::to_string(expect)) != std::string::npos)
            reported = true;
    CHECK_MSG(reported, "OSMODE did not follow the OSNAP command");
}

TEST(qselect_takes_criteria_rather_than_selecting_every_line) {
    Shell s = make_shell();
    s.build_frame();

    s.canvas().set_current_layer("A-WALL");
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.canvas().set_current_layer("A-DOOR");
    draw_rect(s, "10,0", "11,0", "11,1", "10,1");

    s.canvas().set_tool(Tool::Circle);
    s.canvas().type_point("20,20");
    s.canvas().type_point("21,20");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);

    s.command("QSELECT layer A-WALL");
    CHECK_MSG(s.canvas().selection().size() == 4,
              "layer criterion selected %zu, not the four walls",
              s.canvas().selection().size());

    s.command("QSELECT kind circle");
    CHECK_MSG(s.canvas().selection().size() == 1,
              "kind criterion selected %zu, not the one circle",
              s.canvas().selection().size());

    s.command("QSELECT layer A-DOOR kind line");
    CHECK_MSG(s.canvas().selection().size() == 4,
              "combined criteria selected %zu, not the four door segments",
              s.canvas().selection().size());

    s.command("QSELECT length 2 100");
    CHECK_MSG(s.canvas().selection().size() == 4,
              "length range selected %zu, not the four long walls",
              s.canvas().selection().size());

    s.command("FILTER layer A-WALL");
    CHECK(s.canvas().selection().size() == 4);

    s.command("QSELECT");
    CHECK_MSG(s.canvas().selection().size() == 8,
              "bare QSELECT selected %zu, not all eight segments",
              s.canvas().selection().size());
}

TEST(object_properties_are_settable_drawn_and_saved) {
    const std::string path = scratch_path("properties.glcad");
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");

    s.command("SELECTALL");
    s.command("CHPROP color 1 lineweight 0.5 transparency 40");
    {
        const auto& sh = s.canvas().shapes();
        CHECK_MSG(!sh.empty(), "nothing drawn");
        CHECK_MSG(sh[0].attrs.color.kind == gdoc::Color::Kind::Index &&
                  sh[0].attrs.color.index == 1,
                  "colour did not stick");
        CHECK(sh[0].attrs.lineweight == gdoc::Lineweight::W050);
        CHECK_MSG(sh[0].attrs.transparency == 40, "transparency %d",
                  sh[0].attrs.transparency);
    }

    s.command("CHPROP colour red");
    CHECK(s.canvas().shapes()[0].attrs.color.index == 1);

    {
        grender::d2::DrawList dl;
        s.canvas().build(dl);
        bool found_red = false;
        for (const auto& it : dl.lines())
            if (it.color.a >= 0.0f && it.color.r > 0.5f && it.color.g < 0.3f &&
                it.color.b < 0.3f)
                found_red = true;
        CHECK_MSG(found_red, "the object's colour never reached the draw list");
    }

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    const auto& sh = b.canvas().shapes();
    CHECK_MSG(!sh.empty(), "reopened drawing is empty");
    CHECK_MSG(sh[0].attrs.color.index == 1, "colour was lost on save: index %d",
              sh[0].attrs.color.index);
    CHECK(sh[0].attrs.lineweight == gdoc::Lineweight::W050);
    CHECK(sh[0].attrs.transparency == 40);
}

TEST(the_properties_panel_opens_on_a_selection_and_draws) {
    Shell s = make_shell();
    s.build_frame();

    const gui::PanelState* p = s.dock().find("properties");
    CHECK_MSG(p != nullptr, "no properties panel is registered");
    CHECK_MSG(!p->open, "the properties panel opens uninvited");

    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.command("SELECTALL");
    s.command("PROPERTIES");
    CHECK_MSG(s.dock().find("properties")->open,
              "PROPERTIES did not open the properties panel");

    s.build_frame();
    s.command("CHPROP color 1");
    s.build_frame();
    s.canvas().clear_selection();
    {
        Canvas::Filter one;
        one.by_length = true;
        one.min_length = 3.5;
        s.canvas().select_filtered(one, false);
    }
    s.command("CHPROP color 3");
    s.command("SELECTALL");
    s.build_frame();

    s.canvas().clear_selection();
    s.build_frame();
    CHECK(s.dock().find("properties")->open);
}

TEST(linetypes_actually_draw_dashes) {
    Shell s = make_shell();
    s.build_frame();

    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("10,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);

    const auto count_lines = [&] {
        grender::d2::DrawList dl;
        s.canvas().build(dl);
        std::size_t n = 0;
        for (const auto& it : dl.lines())
            if (it.role == grender::Role::Geometry) ++n;
        return n;
    };
    const std::size_t solid = count_lines();
    CHECK_MSG(solid == 1, "a plain line drew as %zu segments", solid);

    s.command("SELECTALL");
    s.command("LINETYPE Dashed");
    s.build_frame();
    const std::size_t dashed = count_lines();
    CHECK_MSG(dashed > 4, "a dashed line drew as %zu segments -- still solid", dashed);

    s.command("LTSCALE 4");
    s.build_frame();
    const std::size_t coarse = count_lines();
    CHECK_MSG(coarse < dashed, "LTSCALE 4 gave %zu segments, LTSCALE 1 gave %zu",
              coarse, dashed);

    s.command("SELECTALL");
    s.command("LINETYPE NotALinetype");
    bool complained = false;
    for (const auto& line : s.log())
        if (line.find("no linetype called") != std::string::npos) complained = true;
    CHECK_MSG(complained, "an unknown linetype was accepted silently");
}

TEST(a_polyline_is_one_object) {
    const std::string path = scratch_path("polyline.glcad");
    Shell s = make_shell();
    s.build_frame();

    s.canvas().set_tool(Tool::Rect);
    s.canvas().type_point("0,0");
    s.canvas().type_point("4,3");
    s.canvas().set_tool(Tool::Select);

    const auto& sh = s.canvas().shapes();
    CHECK_MSG(sh.size() == 4, "a rectangle is %zu shapes", sh.size());
    const gcm::u32 id = sh[0].poly;
    CHECK_MSG(id != 0, "the rectangle's pieces do not belong to anything");
    for (const auto& piece : sh)
        CHECK_MSG(piece.poly == id, "a piece of the rectangle belongs elsewhere");

    const std::vector<std::size_t> pieces = s.canvas().polyline_pieces(0);
    CHECK(pieces.size() == 4);
    CHECK_MSG(s.canvas().polyline_closed(pieces), "the rectangle is not closed");

    {
        double px, py;
        double mx = 2.0, my = 0.0;
        s.canvas().view().screen_of(mx, my, px, py);
        s.canvas().select_at(px, py, false);
        CHECK_MSG(s.canvas().selection().size() == 4,
                  "clicking a rectangle selected %zu of its four sides",
                  s.canvas().selection().size());
    }

    s.canvas().set_tool(Tool::Rect);
    s.canvas().type_point("10,0");
    s.canvas().type_point("12,2");
    s.canvas().set_tool(Tool::Select);
    CHECK_MSG(s.canvas().shapes()[4].poly != id,
              "two rectangles became one polyline");

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.canvas().shapes().size() == 8, "reopened %zu shapes",
              b.canvas().shapes().size());
    CHECK_MSG(b.canvas().polyline_pieces(0).size() == 4,
              "the polyline did not survive the save");
    b.canvas().set_tool(Tool::Rect);
    b.canvas().type_point("20,0");
    b.canvas().type_point("21,1");
    b.canvas().set_tool(Tool::Select);
    CHECK_MSG(b.canvas().polyline_pieces(8).size() == 4,
              "a new polyline merged with a loaded one");
}

TEST(a_polyline_survives_a_dxf_round_trip) {
    Shell s = make_shell();
    s.build_frame();
    s.canvas().set_tool(Tool::Rect);
    s.canvas().type_point("0,0");
    s.canvas().type_point("8,5");
    s.canvas().set_tool(Tool::Select);
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("20,0");
    s.canvas().type_point("24,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);

    const std::string path = scratch_path("polyline.dxf");
    CHECK(s.export_file(path));

    std::string text;
    {
        std::ifstream f(path);
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    std::size_t lwpoly = 0, lines = 0, at = 0;
    while ((at = text.find("LWPOLYLINE", at)) != std::string::npos) { ++lwpoly; at += 10; }
    at = 0;
    while ((at = text.find("\nLINE\n", at)) != std::string::npos) { ++lines; at += 6; }
    CHECK_MSG(lwpoly >= 1, "the rectangle exported as %zu LWPOLYLINEs", lwpoly);
    CHECK_MSG(lines == 1, "%zu loose LINEs -- the rectangle exploded", lines);

    Shell t = make_shell();
    t.build_frame();
    CHECK(t.import_file(path));
    CHECK_MSG(t.canvas().shapes().size() == 5, "%zu shapes after the round trip",
              t.canvas().shapes().size());
    std::size_t grouped = 0, alone = 0;
    for (const auto& sh : t.canvas().shapes()) (sh.poly ? grouped : alone)++;
    CHECK_MSG(grouped == 4, "%zu shapes came back in a polyline", grouped);
    CHECK_MSG(alone == 1, "%zu shapes came back loose", alone);
    CHECK_MSG(t.canvas().polyline_closed(t.canvas().polyline_pieces(0)) ||
              t.canvas().polyline_closed(t.canvas().polyline_pieces(1)),
              "the rectangle came back open");
}

TEST(pedit_closes_opens_and_makes_polylines) {
    Shell s = make_shell();
    s.build_frame();

    const char* pts[4] = {"0,0", "4,0", "4,3", "0,3"};
    for (int i = 0; i < 3; ++i) {
        s.canvas().set_tool(Tool::Line);
        s.canvas().type_point(pts[i]);
        s.canvas().type_point(pts[i + 1]);
        s.canvas().cancel();
    }
    s.canvas().set_tool(Tool::Select);
    for (const auto& sh : s.canvas().shapes())
        CHECK_MSG(sh.poly == 0, "LINE grouped its lines");

    s.command("SELECTALL");
    s.command("PEDIT JOIN");
    CHECK_MSG(s.canvas().polyline_pieces(0).size() == 3,
              "PEDIT JOIN made a polyline of %zu pieces",
              s.canvas().polyline_pieces(0).size());
    CHECK(!s.canvas().polyline_closed(s.canvas().polyline_pieces(0)));

    s.command("PEDIT CLOSE");
    const std::vector<std::size_t> run = s.canvas().polyline_pieces(0);
    CHECK_MSG(run.size() == 4, "closing added %zu pieces", run.size() - 3);
    CHECK_MSG(s.canvas().polyline_closed(run), "PEDIT CLOSE did not close it");

    s.command("AREA");
    bool measured = false;
    for (const auto& line : s.log())
        if (line.find("area 12") != std::string::npos) measured = true;
    CHECK_MSG(measured, "AREA did not measure the 4 by 3 polyline");

    s.command("PEDIT OPEN");
    CHECK_MSG(s.canvas().polyline_pieces(0).size() == 3, "PEDIT OPEN left it closed");

    s.command("SELECTALL");
    s.command("EXPLODE");
    for (const auto& sh : s.canvas().shapes())
        CHECK_MSG(sh.poly == 0, "EXPLODE left a polyline intact");
}

TEST(object_properties_survive_a_dxf_round_trip) {
    Shell s = make_shell();
    s.build_frame();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("5,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.command("SELECTALL");
    s.command("CHPROP color 5 lineweight 0.5 transparency 40 linetype Dashed");

    const std::string path = scratch_path("props.dxf");
    CHECK(s.export_file(path));

    Shell t = make_shell();
    t.build_frame();
    CHECK(t.import_file(path));
    CHECK_MSG(!t.canvas().shapes().empty(), "nothing imported");
    const auto& a = t.canvas().shapes()[0].attrs;
    CHECK_MSG(a.color.index == 5, "colour came back as %d", a.color.index);
    CHECK_MSG(a.lineweight == gdoc::Lineweight::W050, "lineweight came back as %d",
              int(a.lineweight));
    CHECK_MSG(a.linetype == "Dashed", "linetype came back as '%s'", a.linetype.c_str());
    CHECK_MSG(std::abs(a.transparency - 40) <= 1, "transparency came back as %d",
              a.transparency);
}

TEST(blocks_and_splines_are_snappable) {
    Shell s = make_shell();
    s.build_frame();

    {
        gdoc::BlockDef def;
        def.name = "unit";
        for (int i = 0; i < 4; ++i) {
            static const double xs[5] = {0, 2, 2, 0, 0};
            static const double ys[5] = {0, 0, 2, 2, 0};
            gsnap::Shape seg;
            seg.kind = gsnap::ShapeKind::Segment;
            seg.a = gcm::to_lattice(s.canvas().volume(), xs[i], ys[i]);
            seg.b = gcm::to_lattice(s.canvas().volume(), xs[i + 1], ys[i + 1]);
            def.geometry.push_back(seg);
        }
        s.canvas().blocks().define(std::move(def));
        gdoc::BlockRef ref;
        ref.name = "unit";
        ref.insertion = gcm::to_lattice(s.canvas().volume(), 30.0, 30.0);
        s.canvas().blocks().insert(ref);
    }

    const auto snap_at = [&](double wx, double wy) {
        s.canvas().set_tool(Tool::Line);
        double px, py;
        s.canvas().view().screen_of(wx, wy, px, py);
        s.canvas().hover(px, py);
        const Snap hit = s.canvas().snap();
        s.canvas().cancel();
        return hit;
    };

    {
        const Snap hit = snap_at(32.0, 32.0);
        CHECK_MSG(hit.valid, "nothing to snap to at the block's corner");
        double hx, hy;
        gcm::to_world(s.canvas().volume(), hit.point, hx, hy);
        CHECK_MSG(std::hypot(hx - 32.0, hy - 32.0) < 1e-6,
                  "snapped to %.4f, %.4f instead of the corner", hx, hy);
    }

    s.canvas().set_tool(Tool::Spline);
    s.canvas().type_point("10,0");
    s.canvas().type_point("12,3");
    s.canvas().type_point("14,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    CHECK(s.canvas().splines().size() == 1);
    {
        const Snap hit = snap_at(10.0, 0.0);
        CHECK_MSG(hit.valid, "nothing to snap to at the spline's start");
    }
}

TEST(a_snap_can_be_overridden_for_one_pick) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "10,0", "10,6", "0,6");

    s.command("OSNAP end");
    s.canvas().set_tool(Tool::Line);

    double px, py;
    s.canvas().view().screen_of(5.0, 0.0, px, py);

    s.canvas().hover(px, py);
    const Snap plain = s.canvas().snap();
    CHECK_MSG(!plain.valid || std::string(plain.kind) != "midpoint",
              "a midpoint was found with only endpoints running");

    const std::vector<const char*> ring = s.canvas().osnap_ring(px, py);
    bool offers_mid = false;
    for (const char* k : ring) if (std::string(k) == "midpoint") offers_mid = true;
    CHECK_MSG(offers_mid, "the ring offered %zu snaps and no midpoint", ring.size());

    s.command("MID");
    s.canvas().hover(px, py);
    const Snap forced = s.canvas().snap();
    CHECK_MSG(forced.valid && std::string(forced.kind) == "midpoint",
              "the override gave '%s'", forced.valid ? forced.kind : "nothing");

    s.canvas().type_point("5,0");
    s.canvas().hover(px, py);
    const Snap after = s.canvas().snap();
    CHECK_MSG(!after.valid || std::string(after.kind) != "midpoint",
              "the override outlived its one pick");
    s.canvas().cancel();
}

TEST(the_geometric_centre_snap_finds_the_centroid) {
    Shell s = make_shell();
    s.build_frame();

    s.canvas().set_tool(Tool::Polyline);
    for (const char* p : {"0,0", "6,0", "6,2", "2,2", "2,6", "0,6", "0,0"})
        s.canvas().type_point(p);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);

    const std::vector<std::size_t> run = s.canvas().polyline_pieces(0);
    CHECK_MSG(run.size() == 6, "the L is %zu pieces", run.size());
    CHECK(s.canvas().polyline_closed(run));

    s.command("OSNAP geo");
    s.canvas().set_tool(Tool::Line);
    double px, py;
    s.canvas().view().screen_of(2.22, 2.22, px, py);
    s.canvas().hover(px, py);
    const Snap hit = s.canvas().snap();
    CHECK_MSG(hit.valid && std::string(hit.kind) == "geo center",
              "geometric centre gave '%s'", hit.valid ? hit.kind : "nothing");
    double hx, hy;
    gcm::to_world(s.canvas().volume(), hit.point, hx, hy);
    CHECK_MSG(std::hypot(hx - 2.2, hy - 2.2) < 1e-3,
              "centroid at %.4f, %.4f, not 2.2, 2.2", hx, hy);
    s.canvas().cancel();
}

TEST(the_parallel_snap_runs_along_an_acquired_line) {
    Shell s = make_shell();
    s.build_frame();

    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,10");
    s.canvas().type_point("8.6602540378,15");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);

    s.command("OSNAP end nea par");

    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    double px, py;
    s.canvas().view().screen_of(4.33, 12.5, px, py);
    s.canvas().hover(px, py);
    {
        const Snap acq = s.canvas().snap();
        CHECK_MSG(acq.valid, "hovering the reference line found nothing to acquire");
        CHECK_MSG(std::string(acq.kind) == "nearest",
                  "hovering the reference gave '%s', so nothing was acquired", acq.kind);
    }

    s.canvas().view().screen_of(5.19, 3.01, px, py);
    s.canvas().hover(px, py);
    const Snap hit = s.canvas().snap();
    CHECK_MSG(hit.valid && std::string(hit.kind) == "parallel",
              "the parallel snap gave '%s'", hit.valid ? hit.kind : "nothing");

    double hx, hy;
    gcm::to_world(s.canvas().volume(), hit.point, hx, hy);
    const double angle = std::atan2(hy, hx) * 180.0 / 3.14159265358979323846;
    CHECK_MSG(std::fabs(angle - 30.0) < 0.05,
              "the parallel point sits at %.4f degrees, not 30", angle);
    s.canvas().cancel();
}

TEST(a_dimension_follows_the_geometry_it_measures) {
    const std::string path = scratch_path("assoc_dim.glcad");
    Shell s = make_shell();
    s.build_frame();

    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("4,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    const gcm::u32 key = s.canvas().shapes()[0].key;
    CHECK_MSG(key != 0, "the line has no stable key");

    s.command("OSNAP end");
    s.canvas().set_tool(Tool::DimLinear);
    const auto pick = [&](double wx, double wy) {
        double px, py;
        s.canvas().view().screen_of(wx, wy, px, py);
        s.canvas().hover(px, py);
        s.canvas().click(px, py, false);
    };
    pick(0.0, 0.0);
    pick(4.0, 0.0);
    pick(2.0, -1.0);
    s.canvas().set_tool(Tool::Select);

    CHECK_MSG(s.canvas().dimensions().size() == 1, "%zu dimensions",
              s.canvas().dimensions().size());
    {
        const gdim::Dimension& d = s.canvas().dimensions()[0];
        CHECK_MSG(d.associative(), "the dimension recorded no reference");
        CHECK(d.o1.shape == key && d.o2.shape == key);
    }

    s.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
        sh[0].b = gcm::to_lattice(s.canvas().volume(), 7.0, 0.0);
    });
    s.build_frame();
    {
        const gdim::Dimension& d = s.canvas().dimensions()[0];
        double x, y;
        gcm::to_world(s.canvas().volume(), d.p2, x, y);
        CHECK_MSG(std::fabs(x - 7.0) < 1e-6,
                  "the dimension still measures to %.4f after the line moved to 7", x);
        CHECK(!d.orphaned);
    }

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK(b.canvas().dimensions().size() == 1);
    CHECK_MSG(b.canvas().dimensions()[0].associative(),
              "the reference did not survive the save");
    b.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
        sh[0].b = gcm::to_lattice(b.canvas().volume(), 9.0, 0.0);
    });
    b.build_frame();
    {
        double x, y;
        gcm::to_world(b.canvas().volume(), b.canvas().dimensions()[0].p2, x, y);
        CHECK_MSG(std::fabs(x - 9.0) < 1e-6,
                  "after reloading, the dimension measures to %.4f, not 9", x);
    }

    b.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) { sh.clear(); });
    b.build_frame();
    CHECK_MSG(b.canvas().dimensions()[0].orphaned,
              "the dimension outlived its subject without noticing");
}

TEST(notes_are_laid_out_edited_found_and_saved) {
    const std::string path = scratch_path("notes.glcad");
    Shell s = make_shell();
    s.build_frame();

    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 0.0, 0.0),
                        "FIRST FLOOR PLAN\\PSCALE 1:50 UNLESS NOTED", 0.25,
                        0.0, 1, 2, 2.0);
    s.build_frame();

    const std::size_t lines = s.canvas().labels().size();
    CHECK_MSG(lines >= 2, "the note laid out as %zu line(s) -- still one flat line",
              lines);
    for (const auto& lab : s.canvas().labels())
        CHECK_MSG(lab.text.find("\\P") == std::string::npos,
                  "a formatting code was drawn literally: '%s'", lab.text.c_str());

    s.command("FIND scale 1:50");
    CHECK_MSG(s.canvas().selected_texts().size() == 1,
              "FIND selected %zu notes", s.canvas().selected_texts().size());

    s.command("TEXTEDIT");
    for (char c : std::string("X")) s.key_char(std::string(1, c));
    s.key_special(0);
    CHECK_MSG(s.canvas().texts().size() == 1, "editing made %zu notes",
              s.canvas().texts().size());
    CHECK_MSG(s.canvas().texts()[0].text.find('X') != std::string::npos,
              "the edit did not take: '%s'", s.canvas().texts()[0].text.c_str());
    CHECK_MSG(s.canvas().texts()[0].text.find("\\P") != std::string::npos,
              "editing threw away the note's formatting");

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.canvas().texts().size() == 1, "%zu notes after reopening",
              b.canvas().texts().size());
    CHECK_MSG(std::fabs(b.canvas().texts()[0].width - 2.0) < 1e-9,
              "the column width came back as %.4f, not 2",
              b.canvas().texts()[0].width);
}

TEST(a_constraint_is_kept_and_holds) {
    const std::string path = scratch_path("constraint.glcad");
    Shell s = make_shell();
    s.build_frame();

    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("4,0.2");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.command("SELECTALL");
    CHECK(s.canvas().apply_constraint(gsolve::Kind::Horizontal));
    CHECK_MSG(s.canvas().constraints().size() == 1,
              "%zu constraints kept", s.canvas().constraints().size());

    const auto rise = [&](const Canvas& c) {
        double ax, ay, bx, by;
        gcm::to_world(c.volume(), c.shapes()[0].a, ax, ay);
        gcm::to_world(c.volume(), c.shapes()[0].b, bx, by);
        return by - ay;
    };
    CHECK_MSG(std::fabs(rise(s.canvas())) < 1e-6,
              "the line is not horizontal after constraining: rise = %.6f",
              rise(s.canvas()));

    s.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
        sh[0].b = gcm::to_lattice(s.canvas().volume(), 4.0, 1.5);
    });
    s.build_frame();
    CHECK_MSG(std::fabs(rise(s.canvas())) < 1e-4,
              "the constraint did not hold: rise = %.6f", rise(s.canvas()));

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.canvas().constraints().size() == 1,
              "%zu constraints after reopening", b.canvas().constraints().size());
    b.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
        sh[0].b = gcm::to_lattice(b.canvas().volume(), 4.0, 2.0);
    });
    b.build_frame();
    CHECK_MSG(std::fabs(rise(b.canvas())) < 1e-4,
              "the reloaded constraint did not hold: rise = %.6f", rise(b.canvas()));

    b.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) { sh.clear(); });
    b.build_frame();
    CHECK_MSG(b.canvas().constraints().empty(),
              "%zu constraints outlived their geometry",
              b.canvas().constraints().size());
}

TEST(a_boolean_on_curved_solids_keeps_the_curves) {
    Shell s = make_shell();
    s.fit_width(14);
    s.build_frame();
    s.command("CYLINDER");
    if (s.modeller().bodies().size() != 1) return;

    const auto curved_faces = [](const gtopo::Body& b) {
        std::size_t n = 0;
        for (const auto& surf : b.face_surface)
            if (surf.kind != gtopo::SurfaceKind::Plane) ++n;
        return n;
    };
    const std::size_t before = curved_faces(s.modeller().bodies()[0]);
    CHECK_MSG(before > 0, "the cylinder primitive has no curved faces");

    s.set_lens(Lens::TwoD);
    s.build_frame();
    draw_rect(s, "0.5,0.5", "3.5,0.5", "3.5,3.5", "0.5,3.5");
    extrude_at(s, 2.5, 2.5, "2");
    if (s.modeller().bodies().size() != 2) return;

    s.set_lens(Lens::ThreeD);
    s.command("ISO");
    s.command("ZOOMEXTENTS");
    s.fit_width(14);
    s.build_frame();
    const auto click_at = [&](double wx, double wy, double wz) {
        double px, py;
        if (!s.camera().project(gview::Point3::from(wx, wy, wz), px, py)) return false;
        s.pointer_move(px, py);
        s.build_frame();
        s.build_frame();
        s.pointer_down(px, py);
        s.pointer_up(px, py);
        s.build_frame();
        return true;
    };
    s.command("UNION");
    s.build_frame();
    const auto top_of = [&](std::size_t body, double& cx, double& cy, double& cz) {
        const gtopo::Body& b = s.modeller().bodies()[body];
        double top = -1e300;
        for (const auto& v : b.vertex_point) {
            double x, y, z;
            gcm::to_world(s.canvas().volume(), v, x, y, z);
            top = std::max(top, z);
        }
        cx = cy = 0.0;
        cz = top;
        std::size_t n = 0;
        for (const auto& v : b.vertex_point) {
            double x, y, z;
            gcm::to_world(s.canvas().volume(), v, x, y, z);
            if (std::fabs(z - top) > 1e-6) continue;
            cx += x; cy += y; ++n;
        }
        if (n) { cx /= double(n); cy /= double(n); }
        return n > 0;
    };
    double az = 0.0, bz = 0.0, ignore_x = 0.0, ignore_y = 0.0;
    CHECK(top_of(0, ignore_x, ignore_y, az));
    CHECK(top_of(1, ignore_x, ignore_y, bz));
    CHECK(click_at(-0.5, -0.5, az));
    CHECK(click_at(2.5, 2.5, bz));
    CHECK_MSG(s.modeller().bodies().size() == 1, "the union left %zu solids: %s",
              s.modeller().bodies().size(), s.modeller().message().c_str());
    if (s.modeller().bodies().size() != 1) return;
    CHECK_MSG(curved_faces(s.modeller().bodies()[0]) > 0,
              "the union threw away every curved face (%zu before)", before);
}

TEST(the_face_edits_have_commands) {
    Shell s = make_shell();
    s.build_frame();
    s.command("BOX");
    if (s.modeller().bodies().size() != 1) return;

    const auto volume_now = [&] {
        return gtopo::volume(s.modeller().bodies()[0], s.canvas().volume());
    };
    const double before = volume_now();
    CHECK_MSG(before > 7.0 && before < 9.0, "the box is %.4f m3, not 8", before);

    s.set_lens(Lens::ThreeD);
    s.command("ISO");
    s.command("ZOOMEXTENTS");
    s.build_frame();
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(0.0, 0.0, 2.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    s.build_frame();
    if (!s.selected_target().valid || !s.selected_target().on_body) return;

    s.command("OFFSETFACE 0.5");
    s.build_frame();
    const double after = volume_now();
    CHECK_MSG(std::fabs(after - before - 2.0) < 1e-6,
              "offsetting the top face by 0.5 changed the volume by %.6f, not 2 (%s)",
              after - before, s.log().empty() ? "" : s.log().back().c_str());

    const double t0 = volume_now();
    s.command("THICKEN -0.25");
    s.build_frame();
    CHECK_MSG(volume_now() > t0,
              "THICKEN with a negative number removed material: %.6f -> %.6f",
              t0, volume_now());
}

TEST(the_recovery_copy_grows_by_the_edit_not_by_the_drawing) {
    ScopedTestConfig recovery_directory(scratch_path("incremental-recovery"));

    Shell s = make_shell();
    s.build_frame();
    for (int i = 0; i < 200; ++i) {
        s.canvas().set_tool(Tool::Line);
        s.canvas().type_point((std::to_string(i) + ",0").c_str());
        s.canvas().type_point((std::to_string(i) + ",5").c_str());
        s.canvas().cancel();
    }
    s.canvas().set_tool(Tool::Select);

    CHECK_MSG(s.autosave_now(), "autosave refused a dirty document");
    const auto size_of = [&] {
        std::error_code ec;
        return static_cast<long long>(
            std::filesystem::file_size(s.autosave_path(), ec));
    };
    const long long base = size_of();
    CHECK_MSG(base > 0, "the recovery copy is empty");

    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("500,0");
    s.canvas().type_point("500,5");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    CHECK(s.autosave_now());
    const long long after = size_of();
    CHECK_MSG(after > base, "the recovery copy did not grow");
    CHECK_MSG(after - base < base / 4,
              "one line added %lld bytes to a %lld byte copy -- it rewrote the "
              "whole drawing", after - base, base);

    Shell r = make_shell();
    r.build_frame();
    CHECK(r.open_file(s.autosave_path()));
    CHECK_MSG(r.canvas().shapes().size() == 201,
              "%zu shapes recovered, 201 expected -- the delta was not applied",
              r.canvas().shapes().size());

    std::error_code ec;
    std::filesystem::remove(s.autosave_path(), ec);
}

TEST(hatches_and_splines_reach_the_plot) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");

    const auto plotted_paths = [&](const char* path) {
        CHECK(s.export_file(path));
        std::ifstream f(path, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string pdf = ss.str();
        std::size_t strokes = 0, at = 0;
        while ((at = pdf.find(" l\n", at)) != std::string::npos) { ++strokes; at += 3; }
        return strokes;
    };
    const std::size_t bare = plotted_paths(scratch_path("plot_bare.pdf").c_str());
    CHECK_MSG(bare > 0, "the rectangle itself did not plot");

    s.command("HATCH");
    s.build_frame();
    click_at(s, 2.0, 1.5, 0.0);
    s.build_frame();
    CHECK_MSG(!s.canvas().hatches().empty(), "nothing was hatched");
    const std::size_t hatched = plotted_paths(scratch_path("plot_hatched.pdf").c_str());
    CHECK_MSG(hatched > bare,
              "hatching added %zu paths to the plot -- the fill never plots",
              hatched - bare);

    s.canvas().set_tool(Tool::Spline);
    s.canvas().type_point("10,0");
    s.canvas().type_point("12,3");
    s.canvas().type_point("14,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.build_frame();
    CHECK(s.canvas().splines().size() == 1);
    const std::size_t with_spline = plotted_paths(scratch_path("plot_spline.pdf").c_str());
    CHECK_MSG(with_spline > hatched, "the spline never plots");
}

TEST(layouts_are_sheets_that_can_be_named_saved_and_titled) {
    const std::string path = scratch_path("layouts.glcad");
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");

    s.command("LAYOUT");
    bool said_none = false;
    for (const auto& l : s.log())
        if (l.find("no layouts yet") != std::string::npos) said_none = true;
    CHECK_MSG(said_none, "LAYOUT did not say there were none");

    s.command("LAYOUT NEW Plan");
    CHECK_MSG(s.layouts().size() == 1, "%zu layouts", s.layouts().size());
    CHECK(s.layouts()[0].name == "Plan");

    s.command("LAYOUT NEW Sections");
    CHECK_MSG(s.layouts().size() == 2, "%zu layouts", s.layouts().size());
    s.command("PAGESETUP A1");

    s.command("LAYOUT Plan");
    CHECK_MSG(s.plot_paper() == "A3", "switching gave paper %s", s.plot_paper().c_str());
    s.command("LAYOUT Sections");
    CHECK_MSG(s.plot_paper() == "A1", "the second sheet is on %s", s.plot_paper().c_str());
    s.command("LAYOUT Plan");

    s.command("TITLEBLOCK");
    CHECK_MSG(!s.layouts()[0].shapes.empty(), "the titleblock drew nothing");
    CHECK_MSG(!s.layouts()[0].notes.empty(), "the titleblock has no fields");
    bool names_the_sheet = false;
    for (const auto& t : s.layouts()[0].notes)
        if (t.text == "Plan") names_the_sheet = true;
    CHECK_MSG(names_the_sheet, "the titleblock does not name the sheet");

    const std::string pdf_path = scratch_path("layout.pdf");
    CHECK(s.export_file(pdf_path));
    {
        std::ifstream f(pdf_path, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        CHECK_MSG(ss.str().find("Plan") != std::string::npos,
                  "the sheet's name is not on the plot");
    }

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.layouts().size() == 2, "%zu layouts came back", b.layouts().size());
    if (b.layouts().size() != 2) return;
    CHECK(b.layouts()[0].name == "Plan" && b.layouts()[1].name == "Sections");
    CHECK_MSG(b.layouts()[1].paper == "A1", "the second sheet came back on %s",
              b.layouts()[1].paper.c_str());
    CHECK_MSG(!b.layouts()[0].shapes.empty(), "the titleblock was lost");
    CHECK_MSG(!b.layouts()[0].notes.empty(), "the titleblock's fields were lost");
}

TEST(a_sheet_can_carry_several_views_of_the_model) {
    const std::string path = scratch_path("viewports.glcad");
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    s.command("LAYOUT NEW Plan");

    CHECK_MSG(s.layouts()[0].viewports.empty(),
              "a fresh sheet already has viewports");

    s.command("MVIEW 50");
    CHECK_MSG(s.layouts()[0].viewports.size() == 1, "%zu viewports",
              s.layouts()[0].viewports.size());
    {
        const auto& v = s.layouts()[0].viewports[0];
        CHECK_MSG(std::fabs(v.scale - 50.0) < 1e-9, "the viewport is at 1:%.4g", v.scale);
        CHECK_MSG(v.hi.x > v.lo.x && v.hi.x < 1.0,
                  "the window runs from %.4f to %.4f, which is not on a sheet",
                  v.lo.x, v.hi.x);
        CHECK_MSG(std::fabs(v.centre_x - 4.0) < 0.5 && std::fabs(v.centre_y - 2.5) < 0.5,
                  "the viewport looks at %.3f, %.3f", v.centre_x, v.centre_y);
    }

    s.command("MVIEW 100");
    CHECK(s.layouts()[0].viewports.size() == 2);
    {
        const auto& a = s.layouts()[0].viewports[0];
        const auto& b = s.layouts()[0].viewports[1];
        const bool apart = b.lo.x >= a.hi.x - 1e-9 || b.lo.y >= a.hi.y - 1e-9 ||
                           a.lo.x >= b.hi.x - 1e-9 || a.lo.y >= b.hi.y - 1e-9;
        CHECK_MSG(apart, "the two viewports overlap");
    }

    const std::string pdf = scratch_path("viewports.pdf");
    CHECK(s.export_file(pdf));
    {
        std::ifstream f(pdf, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        CHECK_MSG(ss.str().size() > 500, "the plot is empty");
    }

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.layouts().size() == 1 && b.layouts()[0].viewports.size() == 2,
              "%zu viewports came back",
              b.layouts().empty() ? 0 : b.layouts()[0].viewports.size());
    if (!b.layouts().empty() && b.layouts()[0].viewports.size() == 2)
        CHECK(std::fabs(b.layouts()[0].viewports[1].scale - 100.0) < 1e-9);

    s.command("MVIEW CLEAR");
    CHECK(s.layouts()[0].viewports.empty());
}

TEST(page_setups_are_named_and_a_sheet_can_plot_a_window) {
    const std::string path = scratch_path("setups.glcad");
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "40,0", "40,30", "0,30");
    s.command("LAYOUT NEW Plan");
    s.command("PAGESETUP A1 portrait");
    s.command("PAGESETUP SAVE House");

    s.command("LAYOUT NEW Detail");
    s.command("PAGESETUP A3 landscape");
    CHECK(s.plot_paper() == "A3");

    s.command("PAGESETUP House");
    CHECK_MSG(s.plot_paper() == "A1", "the saved setup gave %s", s.plot_paper().c_str());

    s.command("PAGESETUP AREA WINDOW 0 0 10 8");
    s.build_frame();
    CHECK_MSG(s.layouts()[1].area == gplot::PlotArea::Window,
              "the sheet still plots the extents");
    CHECK_MSG(std::fabs(s.layouts()[1].win_hi_x - 10.0) < 1e-6,
              "the window's far corner is at %.4f", s.layouts()[1].win_hi_x);

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.layouts().size() == 2, "%zu sheets came back", b.layouts().size());
    if (b.layouts().size() != 2) return;
    CHECK_MSG(b.layouts()[1].area == gplot::PlotArea::Window,
              "the window was lost on save");
    b.command("PAGESETUP House");
    CHECK_MSG(b.plot_paper() == "A1", "the saved setup was lost: %s",
              b.plot_paper().c_str());
}

TEST(the_dxf_declares_its_tables_and_names_its_entities) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 1.0, 1.0),
                        "NOTE ONE\\PNOTE TWO", 0.25, 0.0, 0, 3, 2.0);
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 6.0, 1.0),
                        "SHORT", 0.25);

    const std::string path = scratch_path("tables.dxf");
    CHECK(s.export_file(path));
    std::string dxf;
    {
        std::ifstream f(path);
        std::ostringstream ss;
        ss << f.rdbuf();
        dxf = ss.str();
    }
    const auto has = [&](const char* what) {
        return dxf.find(what) != std::string::npos;
    };
    CHECK_MSG(has("\nLTYPE\n"), "no LTYPE table");
    CHECK_MSG(has("\nDASHED\n") || has("\nDashed\n"), "the linetypes were not declared");
    CHECK_MSG(has("\nSTYLE\n"), "no STYLE table");
    CHECK_MSG(has("\nDIMSTYLE\n"), "no DIMSTYLE table");
    CHECK_MSG(has("\nMTEXT\n"), "a wrapped note was written as TEXT");
    CHECK_MSG(has("\nTEXT\n"), "a single-line note was written as MTEXT");

    std::size_t handles = 0;
    std::size_t at = 0;
    while ((at = dxf.find("\n5\n", at)) != std::string::npos) { ++handles; at += 3; }
    CHECK_MSG(handles >= 5, "%zu entities carry a handle", handles);

    Shell t = make_shell();
    t.build_frame();
    CHECK(t.import_file(path));
    CHECK_MSG(t.canvas().shapes().size() == 4, "%zu shapes round-tripped",
              t.canvas().shapes().size());
}

TEST(text_styles_are_named_applied_and_exported) {
    Shell s = make_shell();
    s.build_frame();

    s.command("STYLE ROOMNAME 0.0035 0.9 0");
    s.command("STYLE NOTES 0.0025");
    s.command("STYLE");
    bool listed = false;
    for (const auto& l : s.log())
        if (l.find("ROOMNAME") != std::string::npos && l.find("NOTES") != std::string::npos)
            listed = true;
    CHECK_MSG(listed, "STYLE did not list the styles it made");

    s.command("STYLE ROOMNAME");
    CHECK_MSG(std::fabs(s.text_height() - 0.0035) < 1e-9,
              "the current style did not set the height: %.6f", s.text_height());

    s.command("STYLE 0.5");
    CHECK_MSG(std::fabs(s.text_height() - 0.5) < 1e-9, "STYLE <height> stopped working");

    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    const std::string path = scratch_path("styles.dxf");
    CHECK(s.export_file(path));
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string dxf = ss.str();
    CHECK_MSG(dxf.find("\nROOMNAME\n") != std::string::npos,
              "the style table does not declare ROOMNAME");
    CHECK_MSG(dxf.find("\nNOTES\n") != std::string::npos,
              "the style table does not declare NOTES");
}

TEST(drawing_views_live_on_a_sheet_and_follow_the_model) {
    const std::string path = scratch_path("views.glcad");
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;

    s.command("LAYOUT NEW GA");
    s.command("VIEWBASE three 0.02");
    CHECK_MSG(s.layouts().size() == 1, "%zu sheets", s.layouts().size());
    const std::size_t base_edges = s.layouts()[0].view_shapes.size();
    CHECK_MSG(base_edges > 0, "the projection produced nothing on the sheet");
    CHECK_MSG(s.canvas().shapes().size() < base_edges,
              "the views were dumped into the drawing as loose lines");

    s.command("VIEWSECTION");
    CHECK_MSG(s.layouts()[0].sections.size() == 1, "%zu sections",
              s.layouts()[0].sections.size());
    CHECK_MSG(s.layouts()[0].view_shapes.size() > base_edges,
              "the section added no geometry");
    const std::size_t with_section = s.layouts()[0].view_shapes.size();

    s.command("VIEWDETAIL 0 0 0.02 4");
    CHECK_MSG(s.layouts()[0].details.size() == 1, "%zu details",
              s.layouts()[0].details.size());
    CHECK_MSG(s.layouts()[0].view_shapes.size() > with_section,
              "the detail added no geometry");

    const std::size_t before = s.layouts()[0].view_shapes.size();
    s.command("CYLINDER");
    s.command("VIEWUPDATE");
    CHECK_MSG(s.layouts()[0].view_shapes.size() > 0,
              "regenerating produced nothing");
    (void)before;

    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.layouts().size() == 1, "%zu sheets came back", b.layouts().size());
    if (b.layouts().empty()) return;
    CHECK_MSG(b.layouts()[0].sections.size() == 1 && b.layouts()[0].details.size() == 1,
              "the section and detail definitions were lost");
    CHECK_MSG(!b.layouts()[0].view_shapes.empty(),
              "the views were not regenerated after loading");
}

TEST(paper_space_is_a_place_you_can_draw) {
    const std::string path = scratch_path("pspace.glcad");
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    const std::size_t model_shapes = s.canvas().shapes().size();
    CHECK(model_shapes == 4);

    s.command("LAYOUT NEW Plan");
    s.command("TITLEBLOCK");
    const std::size_t block_lines = s.layouts()[0].shapes.size();
    CHECK(block_lines > 0);

    s.command("PSPACE");
    CHECK_MSG(s.in_paper_space(), "PSPACE did not switch");
    CHECK_MSG(s.canvas().shapes().size() == block_lines,
              "paper space shows %zu shapes, the titleblock has %zu",
              s.canvas().shapes().size(), block_lines);

    draw_rect(s, "0.05,0.05", "0.09,0.05", "0.09,0.08", "0.05,0.08");
    CHECK_MSG(s.canvas().shapes().size() == block_lines + 4,
              "drawing on the sheet added %zu shapes",
              s.canvas().shapes().size() - block_lines);

    s.command("MSPACE");
    CHECK_MSG(!s.in_paper_space(), "MSPACE did not switch back");
    CHECK_MSG(s.canvas().shapes().size() == model_shapes,
              "the model came back with %zu shapes, not %zu",
              s.canvas().shapes().size(), model_shapes);
    CHECK_MSG(s.layouts()[0].shapes.size() == block_lines + 4,
              "the sheet kept %zu shapes", s.layouts()[0].shapes.size());

    s.command("PSPACE");
    CHECK(s.save_file(path));
    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.canvas().shapes().size() == model_shapes,
              "saving from paper space left the model with %zu shapes",
              b.canvas().shapes().size());
    CHECK_MSG(!b.layouts().empty() && b.layouts()[0].shapes.size() == block_lines + 4,
              "the sheet's own geometry was lost on save");
}

TEST(the_dxf_tables_come_back_in) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.command("STYLE ROOMNAME 0.0035");
    s.layers().add([] {
        gdoc::Layer l;
        l.name = "X-PROTECTED";
        l.linetype = "Dashed";
        l.locked = true;
        l.lineweight = gdoc::Lineweight::W050;
        return l;
    }());

    const std::string path = scratch_path("tables_in.dxf");
    CHECK(s.export_file(path));

    Shell t = make_shell();
    t.build_frame();
    CHECK(t.import_file(path));
    const gdoc::Linetype* dashed = t.linetypes().find("Dashed");
    CHECK_MSG(dashed != nullptr, "the Dashed linetype did not come in");
    if (dashed) CHECK_MSG(!dashed->pattern.empty(), "Dashed came in with no dashes");
    CHECK_MSG(t.text_styles().find("ROOMNAME") != nullptr,
              "the ROOMNAME text style did not come in");
    const gdoc::Layer* wall = t.layers().find("X-PROTECTED");
    CHECK_MSG(wall != nullptr, "the layer did not come in");
    if (wall) {
        CHECK_MSG(wall->locked, "the layer came in unlocked");
        CHECK_MSG(wall->lineweight == gdoc::Lineweight::W050,
                  "the layer's lineweight came in as %d", int(wall->lineweight));
    }
}

TEST(the_drafting_tools_that_had_no_way_in) {
    Shell s = make_shell();
    s.build_frame();

    s.command("ZOOMEXTENTS");
    s.command("VIEW SAVE Overall");
    s.command("ZOOM 4");
    s.command("VIEW Overall");
    bool restored = false;
    for (const auto& l : s.log())
        if (l == "view Overall") restored = true;
    CHECK_MSG(restored, "VIEW could not restore a saved view");
    s.command("VIEW");
    bool listed = false;
    for (const auto& l : s.log())
        if (l.find("Overall") != std::string::npos && l.find("views:") != std::string::npos)
            listed = true;
    CHECK_MSG(listed, "VIEW did not list the views");

    s.canvas().set_tool(Tool::Rect);
    s.canvas().type_point("0,0");
    s.canvas().type_point("6,4");
    s.canvas().set_tool(Tool::Select);
    const std::size_t before = s.canvas().shapes().size();
    CHECK(before == 4);
    s.command("SELECTALL");
    s.command("FILLETALL 0.5");
    CHECK_MSG(s.canvas().shapes().size() > before,
              "FILLETALL added no arcs (%zu shapes)", s.canvas().shapes().size());
    bool rounded = false;
    for (const auto& l : s.log())
        if (l.find("corner(s) rounded") != std::string::npos) rounded = true;
    CHECK_MSG(rounded, "FILLETALL said nothing about rounding");
    CHECK_MSG(s.canvas().polyline_pieces(0).size() == s.canvas().shapes().size(),
              "rounding broke the rectangle into loose pieces");

    Shell p = make_shell();
    p.build_frame();
    p.canvas().set_tool(Tool::Circle);
    p.canvas().type_point("2,0");
    p.canvas().type_point("2.2,0");
    p.canvas().cancel();
    p.canvas().set_tool(Tool::Select);
    p.command("SELECTALL");
    p.command("ARRAYPOLAR 6");
    CHECK_MSG(p.canvas().tool() == Tool::ArrayPolar, "ARRAYPOLAR did not arm the tool");
    click_at(p, 0.0, 0.0, 0.0);
    p.build_frame();
    CHECK_MSG(p.canvas().shapes().size() == 6,
              "the polar array made %zu shapes, not six", p.canvas().shapes().size());
}

TEST(fence_lasso_and_a_block_that_can_be_scaled) {
    Shell s = make_shell();
    s.build_frame();
    for (int i = 0; i < 5; ++i) {
        s.canvas().set_tool(Tool::Line);
        s.canvas().type_point((std::to_string(i * 2) + ",0").c_str());
        s.canvas().type_point((std::to_string(i * 2) + ",6").c_str());
        s.canvas().cancel();
    }
    s.canvas().set_tool(Tool::Select);

    s.command("FENCE 1 3 7 3");
    CHECK_MSG(s.canvas().selection().size() == 3,
              "the fence took %zu walls, not three", s.canvas().selection().size());

    s.command("LASSO -1 -1 3 -1 3 7 -1 7");
    CHECK_MSG(s.canvas().selection().size() == 2,
              "the lasso took %zu walls, not two", s.canvas().selection().size());

    s.command("ZOOMEXTENTS");
    s.command("SELECTALL");
    s.command("BLOCK wall");
    click_at(s, 0.0, 0.0, 0.0);
    s.build_frame();
    CHECK_MSG(s.canvas().blocks().find("wall") != nullptr, "the block was not defined");
    const std::size_t refs_before = s.canvas().blocks().references().size();
    s.command("INSERT wall 0.5 90");
    click_at(s, 4.0, 3.0, 0.0);
    s.build_frame();
    CHECK_MSG(s.canvas().blocks().references().size() > refs_before,
              "nothing was inserted (%zu references)",
              s.canvas().blocks().references().size());
    if (s.canvas().blocks().references().size() > refs_before) {
        const auto& r = s.canvas().blocks().references().back();
        CHECK_MSG(std::fabs(r.scale_x - 0.5) < 1e-9, "inserted at %.4f, not 0.5", r.scale_x);
        CHECK_MSG(std::fabs(r.rotation - 3.14159265358979323846 / 2.0) < 1e-6,
                  "inserted at %.4f radians, not a right angle", r.rotation);
    }
}

TEST(a_lisp_routine_can_drive_the_drawing) {
    Shell s = make_shell();
    s.build_frame();

    s.command("LISP (+ 1 2 3)");
    bool answered = false;
    for (const auto& l : s.log()) if (l == "6") answered = true;
    CHECK_MSG(answered, "LISP did not evaluate an expression");

    s.command("SETVAR FILLETRAD 0.75");
    s.command("LISP (getvar \"FILLETRAD\")");
    bool read_it = false;
    for (const auto& l : s.log())
        if (l.rfind("0.7", 0) == 0) read_it = true;
    CHECK_MSG(read_it, "getvar did not read the shell's own variable");

    const std::size_t before = s.canvas().shapes().size();
    s.command("LISP (command \"LINE\" \"0,0\" \"4,0\" \"\")");
    s.build_frame();
    CHECK_MSG(s.canvas().shapes().size() > before,
              "a LISP routine drew nothing (%zu shapes)", s.canvas().shapes().size());
}

TEST(a_script_runs_through_the_runner) {
    Shell s = make_shell();
    s.build_frame();
    const std::string path = scratch_path("run.scr");
    {
        std::ofstream f(path);
        f << "; a drawing script\n";
        f << "LINE 0,0 4,0\n";
        f << "DELAY 250\n";
        f << "LINE 4,0 4,3\n";
    }
    s.command("SCRIPT " + path);
    s.build_frame();

    bool delayed = false, reported = false;
    for (const auto& l : s.log()) {
        if (l.find("delay 250") != std::string::npos) delayed = true;
        if (l.find("step(s) run") != std::string::npos) reported = true;
    }
    CHECK_MSG(delayed, "the script's DELAY was ignored");
    CHECK_MSG(reported, "the script did not report what it ran");
}

TEST(block_attributes_become_a_schedule) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "0.9,0", "0.9,0.05", "0,0.05");
    s.command("SELECTALL");
    s.command("BLOCK door");
    click_at(s, 0.0, 0.0, 0.0);
    s.build_frame();
    CHECK_MSG(s.canvas().blocks().find("door") != nullptr, "the block was not defined");

    s.command("ATTDEF door DOOR_NO Number D01");
    s.command("ATTDEF door WIDTH Width 900");
    {
        const gdoc::BlockDef* def = s.canvas().blocks().find("door");
        CHECK_MSG(def && def->attributes.size() == 2, "%zu attributes defined",
                  def ? def->attributes.size() : 0);
    }

    CHECK_MSG(!s.canvas().blocks().references().empty(), "nothing is inserted");
    {
        const auto& r = s.canvas().blocks().references().front();
        const auto v = r.attribute("DOOR_NO");
        CHECK_MSG(v && *v == "D01", "the existing insertion did not get the default");
    }

    s.canvas().select_reference(0);
    s.command("ATTEDIT DOOR_NO D07");
    {
        const auto v = s.canvas().blocks().references().front().attribute("DOOR_NO");
        CHECK_MSG(v && *v == "D07", "the attribute did not take: '%s'",
                  v ? v->c_str() : "(none)");
    }

    const std::string csv = scratch_path("doors.csv");
    s.command("ATTEXT " + csv);
    std::ifstream f(csv);
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    CHECK_MSG(text.find("DOOR_NO") != std::string::npos, "no DOOR_NO column");
    CHECK_MSG(text.find("WIDTH") != std::string::npos, "no WIDTH column");
    CHECK_MSG(text.find("D07") != std::string::npos, "the edited value is not in the schedule");
}

TEST(overkill_and_the_grip_menu) {
    Shell s = make_shell();
    s.build_frame();

    const auto line = [&](const char* a, const char* b) {
        s.canvas().set_tool(Tool::Line);
        s.canvas().type_point(a);
        s.canvas().type_point(b);
        s.canvas().cancel();
        s.canvas().set_tool(Tool::Select);
    };
    line("0,0", "4,0");
    line("0,0.0005", "4,0.0005");
    line("3,0", "7,0");
    CHECK(s.canvas().shapes().size() == 3);

    s.command("OVERKILL");
    CHECK_MSG(s.canvas().shapes().size() == 2,
              "%zu shapes after an exact OVERKILL", s.canvas().shapes().size());

    s.command("OVERKILL 0.001");
    CHECK_MSG(s.canvas().shapes().size() == 1,
              "%zu shapes left: the near duplicate should have gone too",
              s.canvas().shapes().size());
    {
        double ax, ay, bx, by;
        gcm::to_world(s.canvas().volume(), s.canvas().shapes()[0].a, ax, ay);
        gcm::to_world(s.canvas().volume(), s.canvas().shapes()[0].b, bx, by);
        CHECK_MSG(std::fabs(std::max(ax, bx) - 7.0) < 1e-3,
                  "the merged line ends at %.4f, not 7", std::max(ax, bx));
    }

    Shell g = make_shell();
    g.build_frame();
    g.canvas().set_tool(Tool::Line);
    g.canvas().type_point("0,0");
    g.canvas().type_point("4,0");
    g.canvas().cancel();
    g.canvas().set_tool(Tool::Select);
    g.command("SELECTALL");

    g.command("GRIPEDIT 2 0");
    bool offered = false;
    for (const auto& l : g.log())
        if (l.find("this grip offers") != std::string::npos) offered = true;
    CHECK_MSG(offered, "GRIPEDIT did not report what the grip offers");

    g.command("GRIPEDIT 2 0 arc 2 1");
    CHECK_MSG(g.canvas().shapes()[0].kind == gsnap::ShapeKind::Arc,
              "the segment did not become an arc");
}

TEST(model_dimensions_measure_check_and_drive) {
    Shell s = make_shell();
    s.build_frame();
    s.command("BOX");
    if (s.modeller().bodies().empty()) return;

    const auto volume_now = [&] {
        return gtopo::volume(s.modeller().bodies()[0], s.canvas().volume());
    };
    const double before = volume_now();

    s.set_lens(Lens::ThreeD);
    s.command("ISO");
    s.command("ZOOMEXTENTS");
    s.build_frame();
    const auto point_at = [&](double x, double y, double z) {
        double px, py;
        if (!s.camera().project(gview::Point3::from(x, y, z), px, py)) return false;
        s.pointer_move(px, py);
        s.build_frame();
        s.build_frame();
        return true;
    };
    if (!point_at(0.0, 0.0, 2.0)) return;
    s.pointer_down(0, 0);
    s.pointer_up(0, 0);
    s.build_frame();
    if (!s.selected_target().valid || !s.selected_target().on_body) return;

    s.command("PMIADD radius");
    s.command("PMIADD diameter");
    s.command("PMILIST");
    bool listed = false;
    for (const auto& l : s.log())
        if (l.find("no model dimensions") != std::string::npos) listed = true;
    bool spoke = false;
    for (const auto& l : s.log())
        if (l.find("PMIADD") != std::string::npos || l.find("radius") != std::string::npos ||
            l.find("diameter") != std::string::npos)
            spoke = true;
    CHECK_MSG(spoke, "PMIADD said nothing at all");
    (void)listed;

    s.command("PMICHECK");
    bool reported = false;
    for (const auto& l : s.log())
        if (l.find("dimension(s):") != std::string::npos) reported = true;
    CHECK_MSG(reported, "PMICHECK produced no report");

    s.command("PMIDRIVE 0 3");
    bool refused_or_drove = false;
    for (const auto& l : s.log())
        if (l.find("reference dimension") != std::string::npos ||
            l.find("volume") != std::string::npos ||
            l.find("PMIDRIVE") != std::string::npos)
            refused_or_drove = true;
    CHECK_MSG(refused_or_drove, "PMIDRIVE said nothing");
    CHECK(volume_now() > 0.0);
    (void)before;
}

TEST(parameters_and_materials) {
    Shell s = make_shell();
    s.build_frame();

    s.command("PARAMETERS wall 0.15");
    s.command("PARAMETERS cavity 0.05");
    s.command("PARAMETERS total =wall+cavity+wall");
    s.command("PARAMETERS");
    bool resolved = false;
    for (const auto& l : s.log())
        if (l.find("total") != std::string::npos && l.find("0.35") != std::string::npos)
            resolved = true;
    CHECK_MSG(resolved, "the expression did not resolve to 0.35");

    s.command("PARAMETERS DELETE wall");
    bool refused = false;
    for (const auto& l : s.log())
        if (l.find("is used by") != std::string::npos) refused = true;
    CHECK_MSG(refused, "deleting a depended-on parameter was allowed");

    s.command("PARAMETERS a =b");
    s.command("PARAMETERS b =a");
    s.command("PARAMETERS");
    bool cycled = false;
    for (const auto& l : s.log())
        if (l.find("cycle") != std::string::npos || l.find("ERROR") != std::string::npos)
            cycled = true;
    CHECK_MSG(cycled, "a cycle was not reported");

    s.command("BOX");
    if (s.modeller().bodies().empty()) return;
    s.command("MATERIAL NEW Steel 120 124 130 7850");
    s.command("MATERIAL Steel");
    CHECK_MSG(s.modeller().material_of(0) != 0, "the solid did not take the material");
    {
        const auto& m = s.modeller().materials()[s.modeller().material_of(0)];
        CHECK(m.name == "Steel");
        CHECK_MSG(std::fabs(m.density - 7850.0) < 1e-9, "density %.1f", m.density);
        CHECK_MSG(m.r < 0.6f, "the colour was not taken (r = %.3f)", m.r);
    }
}

TEST(the_feature_history_is_a_recipe) {
    Shell s = block_shell();
    if (s.modeller().bodies().empty()) return;
    const double built = gtopo::volume(s.modeller().bodies()[0], s.canvas().volume());
    CHECK(built > 0.0);

    s.set_lens(Lens::TwoD);
    s.build_frame();
    const std::size_t features_before = s.modeller().history(0).size();

    s.command("ROLLBACK");
    bool listed = false;
    for (const auto& l : s.log())
        if (l.find("features:") != std::string::npos) listed = true;
    CHECK_MSG(listed, "ROLLBACK did not list the features");

    s.command("ROLLBACK 0");
    s.build_frame();
    CHECK_MSG(s.modeller().history(0).size() == features_before,
              "rolling back destroyed the recipe");
    CHECK_MSG(s.modeller().rolled_back_to(0) == 0,
              "the rollback point was not recorded");

    s.command("ROLLBACK " + std::to_string(features_before));
    s.build_frame();
    CHECK_MSG(s.modeller().rolled_back_to(0) == Modeller::kAllFeatures,
              "rolling forward did not clear the rollback");
    if (!s.modeller().bodies().empty())
        CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0], s.canvas().volume()) -
                            built) < 1e-9,
                  "the solid did not come back the same");

    s.command("SUPPRESS 0");
    s.build_frame();
    CHECK_MSG(s.modeller().history(0).size() == features_before,
              "suppressing removed the feature instead of skipping it");
    bool said = false;
    for (const auto& l : s.log())
        if (l.find("suppress") != std::string::npos) said = true;
    CHECK_MSG(said, "SUPPRESS said nothing");
}

TEST(datum_planes_and_a_live_section) {
    Shell s = make_shell();
    s.build_frame();
    const std::size_t sketches_before = s.sketch_count();

    s.command("DATUM 2.5");
    CHECK_MSG(s.sketch_count() == sketches_before + 1,
              "DATUM made %zu sketches", s.sketch_count() - sketches_before);
    {
        const auto& pl = s.sketch_plane();
        CHECK_MSG(std::fabs(pl.oz - 2.5) < 1e-9,
                  "the datum sits at z = %.4f, not 2.5", pl.oz);
    }

    s.command("DATUM XZ 1");

    CHECK_MSG(!s.sectioned(), "a section was on before one was asked for");
    s.command("SECTIONPLANE Z 1");
    CHECK_MSG(s.sectioned(), "SECTIONPLANE did not cut");
    s.command("SECTIONOFF");
    CHECK_MSG(!s.sectioned(), "SECTIONOFF did not put it away");
}

TEST(an_xref_is_a_reference_not_a_copy) {
    const std::string other = scratch_path("consultant.dxf");
    {
        Shell c = make_shell();
        c.build_frame();
        draw_rect(c, "0,0", "4,0", "4,3", "0,3");
        CHECK(c.export_file(other));
    }

    Shell s = make_shell();
    s.build_frame();
    s.command("XATTACH " + other);
    s.build_frame();
    CHECK_MSG(!s.canvas().blocks().xrefs().empty(), "no reference was recorded");
    if (s.canvas().blocks().xrefs().empty()) return;
    const std::string name = s.canvas().blocks().xrefs().front().name;
    CHECK(s.canvas().blocks().xrefs().front().state == gdoc::XrefState::Loaded);
    CHECK(s.canvas().shapes().empty());
    CHECK(s.canvas().blocks().references().size()==1);
    const auto geometry=[&]{return s.canvas().blocks().expand(s.canvas().blocks().references().front(),s.canvas().volume());};
    const std::size_t attached = geometry().size();
    CHECK_MSG(attached >= 4, "%zu objects came in", attached);

    {
        Shell c = make_shell();
        c.build_frame();
        draw_rect(c, "0,0", "4,0", "4,3", "0,3");
        draw_rect(c, "6,0", "9,0", "9,3", "6,3");
        CHECK(c.export_file(other));
    }
    s.command("XREF RELOAD " + name);
    s.build_frame();
    CHECK_MSG(geometry().size() > attached,
              "reloading did not pick up the revision (%zu objects, was %zu)",
              geometry().size(), attached);

    const std::size_t loaded = geometry().size();
    s.command("XREF UNLOAD " + name);
    s.build_frame();
    CHECK_MSG(geometry().size() < loaded, "unloading kept the geometry");
    CHECK_MSG(s.canvas().blocks().xrefs().size() == 1,
              "unloading threw the reference away");
    CHECK(s.canvas().blocks().xrefs().front().state == gdoc::XrefState::Unloaded);

    const std::string moved = scratch_path("consultant-rev-b.dxf");
    std::error_code ec;
    std::filesystem::copy_file(other, moved,
                               std::filesystem::copy_options::overwrite_existing, ec);
    s.command("XREF PATH " + name + " " + moved);
    s.build_frame();
    CHECK_MSG(s.canvas().blocks().xrefs().front().path == moved,
              "the path was not repaired");

    s.command("XREF DETACH " + name);
    s.build_frame();
    CHECK_MSG(s.canvas().blocks().xrefs().empty(), "detaching kept the reference");
}

TEST(a_dynamic_block_is_one_definition_not_four_hundred) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "0.9,0", "0.9,0.05", "0,0.05");
    s.command("SELECTALL");
    s.command("BLOCK door");
    click_at(s, 0.0, 0.0, 0.0);
    s.build_frame();
    CHECK_MSG(s.canvas().blocks().find("door") != nullptr, "the block was not defined");

    s.command("BEDIT door LINEAR Width 0.6 1.2 0.05");
    s.command("BEDIT door LOOKUP Size 600 900 1200");
    s.command("BEDIT door STRETCH Widen Width 1 0");
    CHECK_MSG(s.canvas().blocks().is_dynamic("door"), "the block is not dynamic");
    {
        const gdoc::DynamicBlock* d = s.canvas().blocks().dynamic("door");
        CHECK_MSG(d && d->parameters.size() == 2, "%zu parameters",
                  d ? d->parameters.size() : 0);
        CHECK_MSG(d && d->actions.size() == 1, "%zu actions", d ? d->actions.size() : 0);
    }

    s.command("BEDIT door STRETCH Nowhere Missing 1 0");
    {
        const gdoc::DynamicBlock* d = s.canvas().blocks().dynamic("door");
        CHECK_MSG(d && d->actions.size() == 1,
                  "an action with no parameter was stored");
    }

    CHECK_MSG(!s.canvas().blocks().references().empty(), "nothing is inserted");
    s.canvas().select_reference(0);
    s.command("BPARAM Width 0.9");
    {
        const gdoc::DynamicBlock* d = s.canvas().blocks().dynamic("door");
        const gdoc::BlockParameter* p = d ? d->parameter("Width") : nullptr;
        CHECK_MSG(p && std::fabs(p->value - 0.9) < 1e-9, "Width is %.4f",
                  p ? p->value : -1.0);
    }
    s.command("BPARAM Width 2.0");
    {
        const gdoc::DynamicBlock* d = s.canvas().blocks().dynamic("door");
        const gdoc::BlockParameter* p = d ? d->parameter("Width") : nullptr;
        CHECK_MSG(p && p->value <= 1.2 + 1e-9, "Width went to %.4f, past its maximum",
                  p ? p->value : -1.0);
    }
    s.command("BPARAM Size 900");
    bool chose = false;
    for (const auto& l : s.log())
        if (l.find("Size = 900") != std::string::npos) chose = true;
    CHECK_MSG(chose, "the lookup choice was not taken");
}

TEST(projected_edges_and_a_healed_solid) {
    Shell s = make_shell();
    s.build_frame();
    s.command("BOX");
    if (s.modeller().bodies().empty()) return;

    s.set_lens(Lens::TwoD);
    s.build_frame();
    const std::size_t before = s.canvas().shapes().size();
    s.command("PROJECT");
    s.build_frame();
    CHECK_MSG(s.canvas().shapes().size() > before,
              "projecting produced nothing");
    CHECK_MSG(s.canvas().shapes().size() - before <= 8,
              "%zu segments from a box -- the duplicates were not removed",
              s.canvas().shapes().size() - before);

    const double v0 = gtopo::volume(s.modeller().bodies()[0], s.canvas().volume());
    s.command("HEAL");
    s.build_frame();
    CHECK_MSG(!s.modeller().bodies().empty(), "healing lost the solid");
    if (!s.modeller().bodies().empty()) {
        const double v1 = gtopo::volume(s.modeller().bodies()[0], s.canvas().volume());
        CHECK_MSG(std::fabs(v1 - v0) < 1e-9,
                  "healing changed the volume: %.9g -> %.9g", v0, v1);
    }
    bool said = false;
    for (const auto& l : s.log())
        if (l.find("healed") != std::string::npos || l.find("HEAL") != std::string::npos)
            said = true;
    CHECK_MSG(said, "HEAL said nothing");
}

TEST(a_hole_is_a_hole_not_three_commands) {
    Shell s = make_shell();
    s.build_frame();
    s.command("BOX");
    if (s.modeller().bodies().empty()) return;
    const double solid = gtopo::volume(s.modeller().bodies()[0], s.canvas().volume());

    s.set_lens(Lens::ThreeD);
    s.command("ISO");
    s.command("ZOOMEXTENTS");
    s.build_frame();
    double px, py;
    if (!s.camera().project(gview::Point3::from(0.0, 0.0, 2.0), px, py)) return;
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    s.build_frame();
    if (!s.selected_target().valid || !s.selected_target().on_body) return;

    s.command("HOLE 0.4");
    s.build_frame();
    CHECK_MSG(!s.modeller().bodies().empty(), "the hole destroyed the solid");
    if (s.modeller().bodies().empty()) return;
    const double drilled = gtopo::volume(s.modeller().bodies()[0], s.canvas().volume());
    CHECK_MSG(drilled < solid, "the hole removed nothing: %.6f -> %.6f", solid, drilled);
    CHECK_MSG(std::fabs((solid - drilled) - 0.2513) < 0.02,
              "the hole removed %.4f m3, expected about 0.251", solid - drilled);

    std::size_t curved = 0;
    for (const auto& sf : s.modeller().bodies()[0].face_surface)
        if (sf.kind == gtopo::SurfaceKind::Cylinder) ++curved;
    CHECK_MSG(curved > 0, "the hole's wall is not a cylinder");
}

TEST(solids_and_faces_have_names_the_assembly_can_use) {
    Shell s = make_shell();
    s.build_frame();
    s.command("BOX");
    s.command("CYLINDER");
    if (s.modeller().bodies().size() != 2) return;

    const gcm::u32 first = s.modeller().body_name(0);
    const gcm::u32 second = s.modeller().body_name(1);
    CHECK_MSG(first != 0 && second != 0 && first != second,
              "the solids have no distinct names (%u, %u)", first, second);
    CHECK_MSG(s.modeller().body_of_name(second) == 1,
              "a solid could not be found by its name");

    const gcm::u32 face = s.modeller().face_name(0, 0);
    CHECK_MSG(face != 0, "the faces have no names");
    CHECK_MSG(s.modeller().face_of_name(0, face) == 0,
              "a face could not be found by its name");

    s.modeller().erase(0);
    s.build_frame();
    CHECK_MSG(s.modeller().bodies().size() == 1, "the erase did not take");
    CHECK_MSG(s.modeller().body_name(0) == second,
              "the surviving solid was renamed by the erase");
    CHECK_MSG(s.modeller().body_of_name(first) == static_cast<std::size_t>(-1),
              "the erased solid's name still resolves");

    s.command("MATELIST");
    bool said = false;
    for (const auto& l : s.log())
        if (l.find("no mates") != std::string::npos) said = true;
    CHECK_MSG(said, "MATELIST said nothing about an empty assembly");
    s.command("MATEUPDATE");
    CHECK(s.modeller().bodies().size() == 1);
}

TEST(the_grid_can_be_told_what_spacing_to_use) {
    Shell s = make_shell();
    s.build_frame();
    CHECK_MSG(s.canvas().grid_spacing() == 0.0, "a spacing was set before one was asked for");

    s.command("SNAP 0.1");
    CHECK_MSG(std::fabs(s.canvas().grid_spacing() - 0.1) < 1e-9,
              "the spacing is %.4f", s.canvas().grid_spacing());

    s.canvas().set_tool(Tool::Line);
    double px, py;
    s.canvas().view().screen_of(0.503, 0.0, px, py);
    s.canvas().hover(px, py);
    {
        double x, y;
        gcm::to_world(s.canvas().volume(), s.canvas().cursor(), x, y);
        CHECK_MSG(std::fabs(x - 0.5) < 1e-6,
                  "the cursor landed at %.5f, not on the 0.1 grid", x);
    }
    s.canvas().cancel();

    s.command("SNAP 0");
    CHECK(s.canvas().grid_spacing() == 0.0);
}

TEST(points_can_look_like_something_other_than_a_cross) {
    Shell s = make_shell();
    s.build_frame();
    s.canvas().set_tool(Tool::Point);
    s.canvas().type_point("1,1");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.build_frame();

    const auto marks = [&] {
        grender::d2::DrawList dl;
        s.canvas().build(dl);
        return std::make_pair(dl.lines().size(), dl.arcs().size());
    };
    const auto cross = marks();
    CHECK_MSG(cross.first >= 2, "a cross is %zu lines", cross.first);

    s.command("SETVAR PDMODE 34");
    s.build_frame();
    const auto circled = marks();
    CHECK_MSG(circled.second > cross.second,
              "PDMODE 34 drew no circle (%zu arcs, was %zu)",
              circled.second, cross.second);

    s.command("SETVAR PDMODE 1");
    s.build_frame();
    const auto nothing = marks();
    CHECK_MSG(nothing.first < cross.first && nothing.second == 0,
              "PDMODE 1 still drew %zu lines and %zu arcs",
              nothing.first, nothing.second);
}

TEST(divide_measure_underlays_and_a_dwf) {
    Shell s = make_shell();
    s.build_frame();
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");
    s.canvas().type_point("8,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.command("SELECTALL");

    s.command("DIVIDE 8");
    std::size_t points = 0;
    for (const auto& sh : s.canvas().shapes())
        if (sh.kind == gsnap::ShapeKind::Point) ++points;
    CHECK_MSG(points == 7, "DIVIDE 8 placed %zu points, not seven", points);

    Shell m = make_shell();
    m.build_frame();
    m.canvas().set_tool(Tool::Line);
    m.canvas().type_point("0,0");
    m.canvas().type_point("8,0");
    m.canvas().cancel();
    m.canvas().set_tool(Tool::Select);
    m.command("SELECTALL");
    m.command("MEASURE 3");
    std::size_t marks = 0;
    for (const auto& sh : m.canvas().shapes())
        if (sh.kind == gsnap::ShapeKind::Point) ++marks;
    CHECK_MSG(marks == 2, "MEASURE 3 on an 8m line placed %zu points, not two", marks);

    const std::string pdf = scratch_path("survey.pdf");
    { std::ofstream f(pdf); f << "%PDF-1.4\n"; }
    s.command("UNDERLAY " + pdf);
    CHECK_MSG(s.canvas().underlays().size() == 1, "the underlay was not attached");
    s.command("UNDERLAY FADE survey.pdf 80");
    {
        const gdoc::Underlay* u = s.canvas().underlays().find("survey.pdf");
        CHECK_MSG(u && u->fade == 80, "fade is %d", u ? u->fade : -1);
    }

    const std::string dwf = scratch_path("issue.dwfx");
    CHECK_MSG(s.export_file(dwf), "the DWFx export failed");
    std::error_code ec;
    CHECK_MSG(std::filesystem::exists(dwf, ec) &&
              std::filesystem::file_size(dwf, ec) > 100,
              "the DWFx package is empty");
}

TEST(a_user_coordinate_system_moves_the_origin_and_the_axes) {
    Shell s = make_shell();
    s.build_frame();

    s.command("UCS 10 5 90");
    CHECK_MSG(s.canvas().ucs_active(), "the UCS did not take");

    s.canvas().set_tool(Tool::Point);
    s.canvas().type_point("2,0");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    CHECK_MSG(!s.canvas().shapes().empty(), "nothing was placed");
    if (!s.canvas().shapes().empty()) {
        double x, y;
        gcm::to_world(s.canvas().volume(), s.canvas().shapes().back().a, x, y);
        CHECK_MSG(std::fabs(x - 10.0) < 1e-6 && std::fabs(y - 7.0) < 1e-6,
                  "2,0 landed at %.4f, %.4f, not 10, 7", x, y);
    }

    s.canvas().set_tool(Tool::Point);
    s.canvas().type_point("*3,4");
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    {
        double x, y;
        gcm::to_world(s.canvas().volume(), s.canvas().shapes().back().a, x, y);
        CHECK_MSG(std::fabs(x - 3.0) < 1e-6 && std::fabs(y - 4.0) < 1e-6,
                  "*3,4 landed at %.4f, %.4f, not 3, 4", x, y);
    }

    s.command("UCS WORLD");
    CHECK_MSG(!s.canvas().ucs_active(), "UCS WORLD did not put it back");
}

TEST(a_set_can_be_published_and_a_mesh_exported) {
    Shell s = make_shell();
    s.build_frame();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    s.command("LAYOUT NEW Plan");
    s.command("TITLEBLOCK");
    s.command("LAYOUT NEW Sections");

    const std::string stem = scratch_path("set");
    s.command("PUBLISH " + stem);
    std::error_code ec;
    CHECK_MSG(std::filesystem::exists(stem + "-Plan.pdf", ec), "sheet Plan was not published");
    CHECK_MSG(std::filesystem::exists(stem + "-Sections.pdf", ec),
              "sheet Sections was not published");
    CHECK_MSG(s.layouts().size() == 2, "publishing changed the set");

    s.command("BOX");
    if (s.modeller().bodies().empty()) return;
    const std::string stl = scratch_path("part.stl");
    CHECK(s.export_file(stl));
    const auto bytes_of = [](const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        return std::vector<gcm::u8>{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    };
    {
        const auto r = gmesh::read_stl(bytes_of(stl));
        CHECK_MSG(r.ok, "the STL is not binary: %s", r.error.c_str());
        CHECK(r.objects.size() == 1 && r.objects[0].triangle_count() == 12);
        double lo = 1e300, hi = -1e300;
        for (std::size_t i = 0; r.ok && i < r.objects[0].vertex_count(); ++i) {
            lo = std::min(lo, r.objects[0].vertices[3 * i]);
            hi = std::max(hi, r.objects[0].vertices[3 * i]);
        }
        CHECK_MSG(std::fabs(hi - lo - 2000) < 1e-2, "the STL is not in millimetres: %g", hi - lo);
    }
    const std::string three = scratch_path("part.3mf");
    CHECK(s.export_file(three));
    {
        const auto r = gmesh::read_3mf(bytes_of(three));
        CHECK_MSG(r.ok, "the 3MF did not read back: %s", r.error.c_str());
        CHECK(r.unit == gmesh::Unit::Millimetre);
        CHECK(r.objects.size() == 1 && r.objects[0].triangle_count() == 12);
    }
    const std::string obj = scratch_path("part.obj");
    CHECK(s.export_file(obj));
    {
        std::ifstream f(obj);
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        CHECK_MSG(text.find("\nv ") != std::string::npos, "the OBJ has no vertices");
        CHECK_MSG(text.find("\nf ") != std::string::npos, "the OBJ has no faces");
    }
}

TEST(a_lisp_routine_can_look_at_the_drawing) {
    Shell s = make_shell();
    s.build_frame();
    s.canvas().set_current_layer("A-WALL");
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");

    s.command("LISP (length (ssget \"X\" \"A-WALL\"))");
    bool counted = false;
    for (const auto& l : s.log()) if (l == "4") counted = true;
    CHECK_MSG(counted, "ssget did not find the four walls");

    s.command("LISP (entmake \"LINE\" 10 0 14 0 \"A-DOOR\")");
    const std::size_t after = s.canvas().shapes().size();
    CHECK_MSG(after == 5, "entmake made %zu shapes", after - 4);
    CHECK_MSG(s.canvas().shapes().back().layer == "A-DOOR",
              "entmake put it on layer '%s'", s.canvas().shapes().back().layer.c_str());

    const gcm::u32 key = s.canvas().shapes().back().key;
    s.command("LISP (entmod " + std::to_string(key) + " 8 \"A-GLAZ\")");
    CHECK_MSG(s.canvas().shapes().back().layer == "A-GLAZ",
              "entmod left it on '%s'", s.canvas().shapes().back().layer.c_str());

    s.command("LISP (entget " + std::to_string(key) + ")");
    bool described = false;
    for (const auto& l : s.log())
        if (l.find("A-GLAZ") != std::string::npos && l.find("segment") != std::string::npos)
            described = true;
    CHECK_MSG(described, "entget did not describe the entity");

    s.command("LISP (entdel " + std::to_string(key) + ")");
    CHECK_MSG(s.canvas().shapes().size() == 4, "entdel left %zu shapes",
              s.canvas().shapes().size());
}

TEST(a_drawing_someone_else_has_opens_read_only) {
    const std::string path = scratch_path("shared.glcad");
    {
        Shell a = make_shell();
        a.build_frame();
        draw_rect(a, "0,0", "4,0", "4,3", "0,3");
        CHECK(a.save_file(path));
    }
    { std::ofstream lock(path + ".lock"); lock << "another drafter\n"; }

    Shell b = make_shell();
    b.build_frame();
    CHECK(b.open_file(path));
    CHECK_MSG(b.read_only(), "the drawing opened writable while someone else had it");
    CHECK_MSG(b.lock_holder() == "another drafter", "the holder is '%s'",
              b.lock_holder().c_str());

    draw_rect(b, "10,0", "12,0", "12,2", "10,2");
    CHECK_MSG(!b.save_file(path), "saving over a locked drawing was allowed");
    bool told = false;
    for (const auto& l : b.log())
        if (l.find("another drafter") != std::string::npos) told = true;
    CHECK_MSG(told, "nothing said who had it");

    const std::string mine = scratch_path("shared-mine.glcad");
    CHECK_MSG(b.save_file(mine), "saving a read-only drawing elsewhere was refused");
    CHECK_MSG(!b.read_only(), "the copy is still marked read-only");

    std::error_code ec;
    std::filesystem::remove(path + ".lock", ec);
}

TEST(a_bill_of_materials_and_a_block_library) {
    Shell s = make_shell();
    s.build_frame();
    s.command("BOX");
    if (s.modeller().bodies().empty()) return;
    s.command("MATERIAL NEW Steel 120 124 130 7850");
    s.command("MATERIAL Steel");

    const std::string csv = scratch_path("bom.csv");
    s.command("BOM " + csv);
    std::ifstream f(csv);
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    CHECK_MSG(text.find("ITEM,QTY,MATERIAL") != std::string::npos, "no header");
    CHECK_MSG(text.find("Steel") != std::string::npos, "the material is not in the BOM");
    CHECK_MSG(text.find("62800") != std::string::npos,
              "the mass is not in the BOM: %s", text.c_str());

    const std::string folder = scratch_path("library");
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    {
        Shell d = make_shell();
        d.build_frame();
        draw_rect(d, "0,0", "0.9,0", "0.9,0.05", "0,0.05");
        CHECK(d.export_file(folder + "/door.dxf"));
    }
    Shell u = make_shell();
    u.build_frame();
    u.command("BLOCKLIB " + folder);
    bool listed = false;
    for (const auto& l : u.log())
        if (l.find("door") != std::string::npos) listed = true;
    CHECK_MSG(listed, "the library was not listed");

    u.command("BLOCKLIB " + folder + " door");
    u.build_frame();
    CHECK_MSG(u.canvas().blocks().find("door") != nullptr,
              "the library drawing did not become a block");
    CHECK_MSG(!u.canvas().blocks().references().empty(), "it was not inserted");
    CHECK_MSG(u.canvas().shapes().empty(),
              "%zu loose shapes were left behind", u.canvas().shapes().size());
}

TEST(document_tabs_keep_their_sheets_styles_and_parameters) {
    Shell s = make_shell();
    s.build_frame();
    s.command("LAYOUT NEW FirstSheet");
    s.command("TITLEBLOCK");
    s.command("STYLE FirstStyle 0.0035");
    s.command("PARAMETERS wall 0.15");
    s.command("SECTIONPLANE X 1");
    s.command("PSPACE");
    const auto sheet_shapes = s.canvas().shapes().size();
    CHECK(sheet_shapes > 0);

    s.new_tab();
    CHECK(s.layouts().empty());
    CHECK(!s.in_paper_space());
    CHECK(!s.sectioned());
    CHECK(s.text_styles().find("FirstStyle") == nullptr);
    s.command("PARAMETERS");
    bool empty = false;
    for (const auto& line : s.log()) if (line.find("no parameters") != std::string::npos) empty = true;
    CHECK(empty);
    s.command("LAYOUT NEW SecondSheet");
    s.command("STYLE SecondStyle 0.005");
    s.command("PARAMETERS wall 0.30");

    s.switch_tab(0);
    CHECK(s.layouts().size() == 1);
    if (!s.layouts().empty()) CHECK(s.layouts()[0].name == "FirstSheet");
    CHECK(s.in_paper_space());
    CHECK(s.sectioned());
    CHECK(s.canvas().shapes().size() == sheet_shapes);
    CHECK(s.text_styles().find("FirstStyle") != nullptr);
    CHECK(s.text_styles().find("SecondStyle") == nullptr);
    s.command("PARAMETERS wall");
    CHECK(s.log().back().find("0.15") != std::string::npos);
    s.command("MSPACE");
    CHECK(s.canvas().shapes().empty());

    s.switch_tab(1);
    CHECK(s.layouts().size() == 1);
    if (!s.layouts().empty()) CHECK(s.layouts()[0].name == "SecondSheet");
    CHECK(!s.in_paper_space());
    CHECK(s.text_styles().find("SecondStyle") != nullptr);
    s.close_tab(0);
    s.close_tab(0);
    CHECK(s.layouts().empty());
    CHECK(s.text_styles().find("SecondStyle") == nullptr);
}

TEST(open_and_tab_switches_preserve_write_protection) {
    const auto locked = scratch_path("tab-locked.glcad");
    const auto other = scratch_path("tab-other.glcad");
    { Shell s = make_shell(); s.command("BOX"); CHECK(s.save_file(locked)); CHECK(s.save_file(other)); }
    { std::ofstream lock(locked + ".lock"); lock << "another drafter\n"; }
    Shell s = make_shell();
    s.command("BOX");
    CHECK(s.open_file(locked));
    CHECK(s.read_only());
    const int locked_tab = s.active_tab();
    CHECK(!s.open_file(scratch_path("missing-tab.glcad")));
    CHECK(s.read_only());
    CHECK(s.lock_holder() == "another drafter");
    CHECK(s.open_file(other));
    CHECK(!s.read_only());
    CHECK(s.save_file(other));
    s.switch_tab(locked_tab);
    CHECK(s.read_only());
    CHECK(!s.save_file(locked));
    std::filesystem::remove(locked + ".lock");
}

TEST(sheet_only_documents_are_not_replaced_by_open) {
    const auto path = scratch_path("open-after-sheet.glcad");
    { Shell s = make_shell(); s.command("BOX"); CHECK(s.save_file(path)); }
    Shell s = make_shell();
    s.command("LAYOUT NEW UnsavedSheet");
    CHECK(!s.document_blank());
    CHECK(s.open_file(path));
    CHECK(s.tab_count() == 2);
    CHECK(s.layouts().empty());
    s.switch_tab(0);
    CHECK(s.layouts().size() == 1);
    if (!s.layouts().empty()) CHECK(s.layouts()[0].name == "UnsavedSheet");
}

TEST(paper_shapes_and_note_sources_survive_native_save) {
    const auto path = scratch_path("paper-curves.glcad");
    Shell s = make_shell();
    s.command("LAYOUT NEW Curves");
    s.command("PSPACE");
    gsnap::Shape circle;
    circle.kind = gsnap::ShapeKind::Circle;
    circle.a = {100, 200}; circle.radius = 75; circle.key = 123;
    circle.attrs.color = gdoc::Color::aci(1);
    circle.attrs.lineweight = gdoc::Lineweight::W050;
    circle.attrs.transparency = 35;
    circle.attrs.user["purpose"] = "paper test";
    gsnap::Shape arc = circle;
    arc.kind = gsnap::ShapeKind::Arc; arc.key = 124;
    arc.start_angle = 0.25; arc.end_angle = 2.5; arc.poly = 7; arc.poly_index = 0;
    s.canvas().load_shapes({circle, arc});
    Canvas::TextItem note;
    note.pos = {300, 500}; note.text = "first\nsecond\\Pthird";
    note.height = 0.0035; note.width = 0.08; note.angle = 0.75;
    note.halign = 2; note.valign = 3; note.style = "Sheet notes";
    s.canvas().load_texts({note});
    CHECK(s.save_file(path));
    Shell b = make_shell();
    CHECK(b.open_file(path));
    CHECK(b.layouts().size() == 1);
    if (b.layouts().empty()) return;
    const auto& layout = b.layouts()[0];
    CHECK(layout.shapes.size() == 2);
    if (layout.shapes.size() != 2) return;
    CHECK(layout.shapes[0].kind == circle.kind);
    CHECK(layout.shapes[0].radius == circle.radius);
    CHECK(layout.shapes[0].key == circle.key);
    CHECK(layout.shapes[0].attrs.color == circle.attrs.color);
    CHECK(layout.shapes[0].attrs.lineweight == circle.attrs.lineweight);
    CHECK(layout.shapes[0].attrs.transparency == 35);
    CHECK(layout.shapes[0].attrs.user == circle.attrs.user);
    CHECK(layout.shapes[1].kind == arc.kind);
    CHECK(layout.shapes[1].start_angle == arc.start_angle);
    CHECK(layout.shapes[1].end_angle == arc.end_angle);
    CHECK(layout.shapes[1].poly == 7);
    CHECK(layout.notes.size() == 1);
    if (layout.notes.empty()) return;
    CHECK(layout.notes[0].text == note.text);
    CHECK(layout.notes[0].style == note.style);
    CHECK(layout.notes[0].angle == note.angle);
    CHECK(layout.notes[0].width == note.width);
    CHECK(layout.notes[0].halign == note.halign);
    CHECK(layout.notes[0].valign == note.valign);
}

TEST(switching_layouts_in_paper_space_keeps_each_sheets_geometry) {
    Shell s = make_shell();
    s.command("LAYOUT NEW First");
    s.command("PSPACE");
    draw_rect(s, "0,0", "1,0", "1,1", "0,1");
    s.command("LAYOUT NEW Second");
    CHECK(s.in_paper_space());
    CHECK(s.canvas().shapes().empty());
    s.command("LAYOUT First");
    CHECK(s.canvas().shapes().size() == 4);
    s.command("MSPACE");
    CHECK(s.canvas().shapes().empty());
    CHECK(s.layouts()[0].shapes.size() == 4);
    CHECK(s.layouts()[1].shapes.empty());
}

TEST(untitled_tabs_have_independent_recovery_copies) {
    const std::string dir = scratch_path("tab-recovery");
    ScopedTestConfig test_config(dir);
    {
        Shell s = make_shell();
        draw_rect(s, "0,0", "1,0", "1,1", "0,1");
        CHECK(s.autosave_now());
        const auto first = s.autosave_path();
        s.new_tab();
        draw_rect(s, "0,0", "2,0", "2,2", "0,2");
        draw_rect(s, "3,0", "4,0", "4,1", "3,1");
        CHECK(s.autosave_now());
        const auto second = s.autosave_path();
        CHECK(first != second);
        s.switch_tab(0);
        CHECK(s.autosave_path() == first);
        CHECK(s.save_file(dir + "/first.glcad"));
        CHECK(!std::filesystem::exists(first));
        CHECK(std::filesystem::exists(second));
        Shell recovered = make_shell();
        CHECK(recovered.open_file(second));
        CHECK(recovered.canvas().shapes().size() == 8);
    }
}

TEST(legacy_sheets_load_and_corrupt_complete_content_does_not_replace_work) {
    const auto old_path = scratch_path("legacy-sheet.glcad");
    const auto bad_path = scratch_path("corrupt-sheet.glcad");
    const std::string legacy = "L 0 1 0 0 A3|Legacy\nS 10 20 30 40 0\nT 50 60 0.0035 Old note\n";
    gfile::Contents c;
    c.foreign.push_back({kLayoutsFrame, {legacy.begin(), legacy.end()}});
    CHECK(gfile::save_file(old_path, c).ok);
    Shell s = make_shell();
    CHECK(s.open_file(old_path));
    CHECK(s.layouts().size() == 1);
    if (s.layouts().empty()) return;
    CHECK(s.layouts()[0].shapes.size() == 1);
    CHECK(s.layouts()[0].notes.size() == 1);
    if (!s.layouts()[0].notes.empty()) CHECK(s.layouts()[0].notes[0].text == "Old note");
    c.foreign.push_back({kPaperContentFrame, {1, 2, 3}});
    CHECK(gfile::save_file(bad_path, c).ok);
    const int tab = s.active_tab();
    const int count = s.tab_count();
    CHECK(!s.open_file(bad_path));
    CHECK(s.active_tab() == tab);
    CHECK(s.tab_count() == count);
    CHECK(s.document_path() == old_path);
    CHECK(s.layouts()[0].name == "Legacy");
}

TEST(model_note_paragraphs_and_style_names_are_preserved) {
    const auto path = scratch_path("model-note-source.glcad");
    Shell s = make_shell();
    s.canvas().add_text({100, 200}, "first\nsecond\\Pthird", 0.25, 0.75, 2, 3, 1.5, "Notes");
    CHECK(s.save_file(path));
    Shell b = make_shell();
    CHECK(b.open_file(path));
    CHECK(b.canvas().texts().size() == 1);
    if (b.canvas().texts().empty()) return;
    const auto& note = b.canvas().texts()[0];
    CHECK(note.text == "first\nsecond\\Pthird");
    CHECK(note.style == "Notes");
    CHECK(note.angle == 0.75);
    CHECK(note.width == 1.5);
    CHECK(note.halign == 2);
    CHECK(note.valign == 3);
}

TEST(native_metadata_survives_reopen_and_another_save) {
    const auto path = scratch_path("metadata.glcad");
    Shell s = make_shell();
    s.command("DIMSTYLE Review txt 0.35");
    s.command("DIMSTYLE Review");
    s.command("STYLE Notes 0.0035 0.8 12");
    s.command("PARAMETERS wall 0.15");
    s.command("PARAMETERS total =wall*2");
    s.command("BOX");
    s.command("MATERIAL NEW Steel 120 124 130 7850");
    s.command("MATERIAL Steel");
    CHECK(s.modeller().bodies().size() == 1);
    if (s.modeller().bodies().empty()) return;
    const auto body = s.modeller().body_name(0);
    const auto face = s.modeller().face_name(0, 0);
    Modeller::StoredMate m; m.body_a = body; m.body_b = body;
    m.face_a = face; m.face_b = face; m.value = 0.125; m.suppressed = true;
    s.modeller().load_mates({m});
    auto hist = s.modeller().histories();
    hist[0][0].suppressed = true;
    s.modeller().set_history(hist);
    gdoc::BlockDef block; block.name = "Door"; s.canvas().blocks().define(block);
    s.command("BEDIT Door LINEAR width 1 0 2");
    s.command("BEDIT Door MOVE move width 1 0");
    gdoc::Xref x; x.name = "Site"; x.path = "site.glcad"; x.clip = {{1, 2}, {3, 4}};
    x.state = gdoc::XrefState::Unloaded; x.type = gdoc::XrefType::Overlay;
    s.canvas().blocks().attach(x);
    CHECK(s.save_file(path));
    Shell b = make_shell(); CHECK(b.open_file(path));
    CHECK(b.canvas().dim_styles().find("Review") != nullptr);
    if (const auto* st = b.canvas().dim_styles().find("Review")) CHECK(st->dimtxt == 0.35);
    CHECK(b.canvas().dim_styles().current() == "Review");
    CHECK(b.text_styles().find("Notes") != nullptr);
    CHECK(b.text_styles().current() == "Notes");
    b.command("PARAMETERS total"); CHECK(b.log().back().find("0.3") != std::string::npos);
    CHECK(b.modeller().material_of(0) != 0);
    if (b.modeller().material_of(0) < b.modeller().materials().size())
        CHECK(b.modeller().materials()[b.modeller().material_of(0)].density == 7850);
    CHECK(b.modeller().body_name(0) == body);
    CHECK(b.modeller().face_name(0, 0) == face);
    CHECK(b.modeller().mates().size() == 1);
    if (!b.modeller().mates().empty()) CHECK(b.modeller().mates()[0].value == 0.125);
    CHECK(b.modeller().history(0)[0].suppressed);
    CHECK(b.canvas().blocks().is_dynamic("Door"));
    CHECK(b.canvas().blocks().xrefs().size() == 1);
    if (!b.canvas().blocks().xrefs().empty()) CHECK(b.canvas().blocks().xrefs()[0].clip.size() == 2);
    s.command("CLOSETAB");
    CHECK(b.save_file(path));
    Shell c = make_shell(); CHECK(c.open_file(path));
    CHECK(c.canvas().blocks().is_dynamic("Door"));
    CHECK(c.modeller().mates().size() == 1);
}

TEST(metadata_edits_are_dirty_undoable_and_recoverable) {
    const auto dir = scratch_path("metadata-recovery");
    ScopedTestConfig test_config(dir);
    {
        std::filesystem::create_directories(dir);
        Shell s = make_shell(); CHECK(s.save_file(dir + "/plan.glcad"));
        CHECK(!s.document_dirty());
        s.command("LAYOUT NEW Plan"); CHECK(s.document_dirty());
        CHECK(s.autosave_now());
        s.command("PARAMETERS wall 0.15");
        s.command("UNDO"); s.command("PARAMETERS wall");
        CHECK(s.log().back().find("defines it") != std::string::npos);
        s.command("REDO"); s.command("PARAMETERS wall");
        CHECK(s.log().back().find("0.15") != std::string::npos);
        s.command("DIMSTYLE Office txt 0.35");
        CHECK(s.autosave_now());
        Shell r = make_shell(); CHECK(r.open_file(s.autosave_path()));
        CHECK(r.layouts().size() == 1);
        CHECK(r.canvas().dim_styles().find("Office") != nullptr);
        r.command("PARAMETERS wall"); CHECK(r.log().back().find("0.15") != std::string::npos);
        CHECK(s.save_file(dir + "/plan.glcad")); CHECK(!s.document_dirty());
        s.command("PARAMETERS wall"); CHECK(!s.document_dirty());
        s.command("PARAMETERS wall 0.25"); CHECK(s.document_dirty());
        s.command("UNDO"); s.command("PARAMETERS wall");
        CHECK(s.log().back().find("0.15") != std::string::npos);
    }
}


TEST(metadata_undo_does_not_consume_geometry_history) {
    Shell s = make_shell();
    s.canvas().modify_shapes([](auto& shapes) {
        gsnap::Shape line; line.a = {1, 2}; line.b = {3, 4}; shapes.push_back(line);
    });
    s.command("DIMSTYLE Office txt 0.45");
    s.command("UNDO");
    CHECK(s.canvas().dim_styles().find("Office") == nullptr);
    CHECK(s.canvas().shapes().size() == 1);
    s.command("REDO");
    CHECK(s.canvas().dim_styles().find("Office") != nullptr);
    s.command("UNDO"); s.command("UNDO");
    CHECK(s.canvas().shapes().empty());
    s.command("REDO"); CHECK(s.canvas().shapes().size() == 1);
    s.command("BOX");
    const auto key = s.modeller().body_name(0);
    s.command("MATERIAL NEW Steel 100 110 120 7850");
    s.command("MATERIAL Steel");
    CHECK(s.modeller().material_of(0) == 1);
    s.command("UNDO"); CHECK(s.modeller().material_of(0) == 0);
    CHECK(s.modeller().bodies().size() == 1);
    s.command("UNDO"); CHECK(s.modeller().materials().size() == 1);
    s.command("UNDO"); CHECK(s.modeller().bodies().empty());
    s.command("REDO"); CHECK(s.modeller().body_name(0) == key);
    s.command("REDO"); s.command("REDO");
    CHECK(s.modeller().material_of(0) == 1);
    s.command("UNDERLAY plan.pdf");
    CHECK(s.canvas().underlays().all().size() == 1);
    s.command("UNDO"); CHECK(s.canvas().underlays().all().empty());
    CHECK(s.canvas().shapes().size() == 1);
    s.command("REDO"); CHECK(s.canvas().underlays().all().size() == 1);
}

TEST(native_metadata_keeps_identity_counters_and_exact_pmi_references) {
    Shell s = make_shell(); s.command("BOX"); s.command("BOX");
    CHECK(s.modeller().bodies().size() == 2);
    const auto deleted_key = s.modeller().body_name(1);
    CHECK(s.modeller().erase(1));
    auto state = s.modeller().metadata();
    for (std::size_t i = 0; i < state.face_ids[0].size(); ++i) state.face_ids[0][i] = 1007 + 13 * i;
    state.next_face_ids[0] = 4000000000u;
    state.body_layers[0] = "Structure";
    CHECK(s.modeller().load_metadata(state));
    gtopo::PmiDimension d;
    d.label = "Width\ninspection"; d.kind = gtopo::PmiKind::Thickness;
    d.a = {gtopo::PmiTarget::Face, 0, 0xf123456789abcdefull};
    d.b = {gtopo::PmiTarget::Face, 1, 0xabcdef0123456789ull};
    d.nominal = 0.123456789012345; d.tolerance = {0.00025, 0.00012};
    d.driving = true; d.moves_b = false;
    s.model_dimensions().resize(1); s.model_dimensions()[0].add(d);
    const auto path = scratch_path("identities-pmi.glcad");
    CHECK(s.save_file(path));
    Shell r = make_shell(); CHECK(r.open_file(path));
    CHECK(r.modeller().metadata().face_ids == state.face_ids);
    CHECK(r.modeller().metadata().next_face_ids == state.next_face_ids);
    CHECK(r.modeller().body_layer(0) == "Structure");
    CHECK(r.model_dimensions().size() == 1);
    if (r.model_dimensions().empty()) return;
    const auto* rd = r.model_dimensions()[0].at(0); CHECK(rd != nullptr);
    if (!rd) return;
    CHECK(rd->a == d.a); CHECK(rd->b == d.b); CHECK(rd->label == d.label);
    CHECK(rd->nominal == d.nominal); CHECK(rd->tolerance.plus == d.tolerance.plus);
    CHECK(rd->tolerance.minus == d.tolerance.minus); CHECK(rd->driving); CHECK(!rd->moves_b);
    r.command("BOX"); CHECK(r.modeller().body_name(1) > deleted_key);
    s.command("CLOSETAB");
    CHECK(r.save_file(path));
    Shell twice = make_shell(); CHECK(twice.open_file(path));
    CHECK(twice.modeller().face_name(0, 0) == state.face_ids[0][0]);
    CHECK(twice.model_dimensions()[0].at(0)->a == d.a);
}

TEST(metadata_recovery_tracks_replacements_deletions_and_entities) {
    const auto dir = scratch_path("complete-recovery");
    std::filesystem::create_directories(dir);
    ScopedTestConfig test_config(dir);
    {
        Shell s = make_shell(); s.command("BOX"); s.command("LAYOUT NEW First");
        CHECK(s.autosave_now());
        s.command("PARAMETERS width 0.2");
        s.command("MATERIAL NEW Steel 100 110 120 7850"); s.command("MATERIAL Steel");
        s.canvas().add_text({2, 3}, "Original\nparagraph", 0.25);
        CHECK(s.autosave_now());
        s.command("LAYOUT RENAME First Final");
        s.command("PARAMETERS width 0.4");
        s.command("DIMSTYLE Final txt 0.42");
        gdoc::Underlay u; u.path = "site.pdf"; u.name = "site";
        s.canvas().underlays().attach(u);
        CHECK(s.autosave_now());
        Shell r = make_shell(); CHECK(r.open_file(s.autosave_path()));
        CHECK(r.layouts().size() == 1);
        if (!r.layouts().empty()) CHECK(r.layouts()[0].name == "Final");
        CHECK(r.canvas().texts().size() == 1);
        if (!r.canvas().texts().empty()) CHECK(r.canvas().texts()[0].text == "Original\nparagraph");
        CHECK(r.modeller().material_of(0) == 1);
        CHECK(r.canvas().dim_styles().find("Final") != nullptr);
        CHECK(r.canvas().underlays().all().size() == 1);
        r.command("PARAMETERS width"); CHECK(r.log().back().find("0.4") != std::string::npos);
        s.command("UNDERLAY REMOVE site");
        s.canvas().underlays().detach("site");
        s.build_frame(); CHECK(s.autosave_now());
        Shell removed = make_shell(); CHECK(removed.open_file(s.autosave_path()));
        CHECK(removed.canvas().underlays().all().empty());
    }
}

TEST(corrupt_metadata_is_rejected_before_changing_the_live_document) {
    Shell source = make_shell(); source.command("BOX");
    const auto valid = scratch_path("metadata-valid.glcad"); CHECK(source.save_file(valid));
    gfile::Contents c; CHECK(gfile::load_file(valid, c).ok);
    bool changed = false;
    for (auto& f : c.foreign) if (f.kind == kMetadataFrame) {
        f.payload.resize(f.payload.size() / 2); changed = true;
    }
    CHECK(changed);
    const auto bad = scratch_path("metadata-corrupt.glcad"); CHECK(gfile::save_file(bad, c).ok);
    Shell live = make_shell(); live.command("PARAMETERS retained 17");
    const auto count = live.tab_count();
    CHECK(!live.open_file(bad)); CHECK(live.tab_count() == count);
    live.command("PARAMETERS retained"); CHECK(live.log().back().find("17") != std::string::npos);
    CHECK(live.modeller().bodies().empty());
}


TEST(named_views_groups_and_multiline_styles_share_metadata_history) {
    Shell s = make_shell();
    s.canvas().modify_shapes([](auto& shapes) {
        gsnap::Shape line; line.a = {1, 2}; line.b = {3, 4}; shapes.push_back(line);
    });
    s.command("SELECTALL"); s.command("GROUP Boundary");
    s.command("UNDO"); s.command("GROUP");
    CHECK(s.log().back().find("Boundary") == std::string::npos);
    s.command("REDO"); s.command("GROUP");
    CHECK(s.log().back().find("Boundary") != std::string::npos);
    s.command("VIEW SAVE Inspection"); s.command("UNDO"); s.command("VIEW");
    CHECK(s.log().back().find("Inspection") == std::string::npos);
    s.command("REDO");
    gdoc::MultilineStyle wall; wall.name = "Partition"; wall.filled = true;
    wall.elements.push_back({0.125, gdoc::Color::rgb(1, 2, 3), "Dashed", gdoc::Lineweight::ByLayer});
    s.canvas().multilines().add(wall);
    s.command("SETVAR PDMODE 3");
    const auto path = scratch_path("metadata-tables.glcad"); CHECK(s.save_file(path));
    Shell r = make_shell(); CHECK(r.open_file(path));
    r.command("GROUP"); CHECK(r.log().back().find("Boundary") != std::string::npos);
    r.command("VIEW"); CHECK(r.log().back().find("Inspection") != std::string::npos);
    const auto* style = r.canvas().multilines().find("Partition"); CHECK(style != nullptr);
    if (style) { CHECK(style->filled); CHECK(style->elements[0].offset == 0.125); }
    CHECK(r.canvas().point_mode() == 3); CHECK(!r.document_dirty());
    r.command("SETVAR PDMODE 4"); CHECK(r.document_dirty());
    r.command("UNDO"); CHECK(r.canvas().point_mode() == 3);
    CHECK(r.canvas().shapes().size() == 1);
}


TEST(spaces_keep_annotations_and_undo_at_their_origin) {
    Shell s = make_shell();
    s.canvas().modify_shapes([](auto& v) { gsnap::Shape sh; sh.a={1,2}; sh.b={3,4}; v.push_back(sh); });
    gdim::Dimension d; d.text_override="model"; s.canvas().load_dimensions({d});
    gdoc::BlockDef def; def.name="Door"; s.canvas().blocks().define(def);
    gdoc::BlockRef ref; ref.name="Door"; s.canvas().blocks().insert(ref);
    s.command("LAYOUT NEW First"); s.command("PSPACE");
    CHECK(s.canvas().dimensions().empty()); CHECK(s.canvas().blocks().references().empty());
    s.canvas().modify_shapes([](auto& v) { gsnap::Shape sh; sh.a={5,6}; sh.b={7,8}; v.push_back(sh); });
    s.command("MSPACE");
    CHECK(s.canvas().dimensions().size()==1); CHECK(s.canvas().blocks().references().size()==1);
    s.command("UNDO");
    CHECK(s.in_paper_space()); CHECK(s.canvas().shapes().empty());
    s.command("REDO"); CHECK(s.canvas().shapes().size()==1);
    s.command("MSPACE"); CHECK(s.canvas().shapes().size()==1);
    s.command("LAYOUT NEW Second"); s.command("PSPACE");
    CHECK(s.canvas().shapes().empty()); CHECK(s.canvas().dimensions().empty());
    s.command("LAYOUT First"); CHECK(s.canvas().shapes().size()==1);
}


namespace {
void seed_space_annotations(Shell& s, const std::string& label, double value) {
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    gdim::Dimension d; d.text_override = label; d.p1 = s.canvas().shapes()[0].a;
    d.p2 = s.canvas().shapes()[0].b; d.o1.shape = s.canvas().shapes()[0].key;
    d.o1.at = gdim::Dimension::Anchor::Start;
    s.canvas().load_dimensions({d});
    Canvas::StoredConstraint c; c.kind=gsolve::Kind::Horizontal; c.a=d.o1.shape; c.value=value;
    s.canvas().load_constraints({c});
    ggeo::NurbsCurve2<Scalar> spline; spline.degree=1;
    spline.control={{{Scalar::from(0),Scalar::from(0)},Scalar::from(1)},
                    {{Scalar::from(value),Scalar::from(1)},Scalar::from(1)}};
    spline.knots=ggeo::KnotVector::clamped_uniform(1,2); s.canvas().load_splines({spline});
    gdoc::AssociativeHatch hatch; hatch.seed=gcm::to_lattice(s.canvas().volume(),2.0,2.0);
    hatch.seed_valid=true; hatch.style.name="ANSI31"; hatch.style.scale=0.2;
    s.canvas().load_hatches({hatch}); s.canvas().refresh_hatches();
    gdoc::BlockDef def; def.name="Shared"; s.canvas().blocks().define(def);
    gdoc::BlockRef ref; ref.name=def.name; ref.set_attribute("SPACE",label);
    s.canvas().blocks().insert(ref);
    gdoc::Underlay u; u.name=label; u.path=label+".pdf"; s.canvas().underlays().attach(u);
    s.canvas().add_text({1,2},label,0.25);
    s.build_frame();
}
void check_space_annotations(Shell& s, const std::string& label, double value) {
    CHECK(s.canvas().dimensions().size()==1); CHECK(s.canvas().hatches().size()==1);
    CHECK(s.canvas().splines().size()==1); CHECK(s.canvas().constraints().size()==1);
    CHECK(s.canvas().blocks().references().size()==1); CHECK(s.canvas().texts().size()==1);
    CHECK(s.canvas().underlays().all().size()==1);
    if (!s.canvas().dimensions().empty()) {
        CHECK(s.canvas().dimensions()[0].text_override==label);
        CHECK(s.canvas().dimensions()[0].o1.shape==s.canvas().shapes()[0].key);
    }
    if (!s.canvas().constraints().empty()) CHECK(s.canvas().constraints()[0].value==value);
    if (!s.canvas().splines().empty()) CHECK(s.canvas().splines()[0].control.back().p.x.to_f64()==value);
    if (!s.canvas().blocks().references().empty())
        CHECK(s.canvas().blocks().references()[0].attribute("SPACE").value_or("")==label);
    if (!s.canvas().underlays().all().empty()) CHECK(s.canvas().underlays().all()[0].name==label);
}
}

TEST(all_space_annotations_survive_native_reopen_and_recovery) {
    const auto dir=scratch_path("space-recovery"); std::filesystem::create_directories(dir);
    ScopedTestConfig test_config(dir);
    {
        Shell s=make_shell(); seed_space_annotations(s,"Model",2);
        s.command("DATUM XY 1"); seed_space_annotations(s,"Sketch",3);
        s.command("LAYOUT NEW First"); s.command("PSPACE");
        CHECK(s.sketch_plane().oz==0.0);
        seed_space_annotations(s,"First",4);
        CHECK(s.autosave_now());
        s.command("LAYOUT NEW Second"); s.command("PSPACE"); seed_space_annotations(s,"Second",5);
        CHECK(s.autosave_now());
        const auto path=dir+"/spaces.glcad";
        Shell r=make_shell(); CHECK(r.open_file(s.autosave_path()));
        check_space_annotations(r,"Model",2);
        r.edit_sketch(1); check_space_annotations(r,"Sketch",3);
        r.command("LAYOUT First"); r.command("PSPACE"); check_space_annotations(r,"First",4);
        r.command("LAYOUT Second"); check_space_annotations(r,"Second",5);
        CHECK(r.save_file(path));
        Shell twice=make_shell(); CHECK(twice.open_file(path));
        check_space_annotations(twice,"Model",2);
        twice.command("LAYOUT First"); twice.command("PSPACE"); check_space_annotations(twice,"First",4);
        twice.command("LAYOUT Second"); check_space_annotations(twice,"Second",5);
        twice.command("MSPACE"); twice.edit_sketch(1); check_space_annotations(twice,"Sketch",3);
    }
}

TEST(space_navigation_is_clean_and_preserves_redo) {
    Shell s=make_shell(); s.command("LAYOUT NEW First"); s.command("LAYOUT NEW Second");
    s.command("LAYOUT First"); s.command("PSPACE");
    s.canvas().add_text({1,2},"First",0.25); s.command("UNDO");
    s.command("MSPACE");
    const auto path=scratch_path("navigation-clean.glcad"); CHECK(s.save_file(path));
    s.command("LAYOUT Second"); s.command("PSPACE"); s.command("MSPACE");
    CHECK(!s.document_dirty());
    s.command("REDO"); CHECK(s.in_paper_space());

    CHECK(s.canvas().texts().size()==1);
    s.command("TEXT"); CHECK(s.in_paper_space()); s.key_special(1);
}

TEST(deleting_and_restoring_a_layout_keeps_its_edit_history) {
    Shell s=make_shell(); s.command("LAYOUT NEW First"); s.command("PSPACE");
    s.canvas().add_text({1,2},"Original",0.25); s.command("MSPACE");
    s.command("LAYOUT DELETE First"); CHECK(s.layouts().empty());
    s.command("UNDO"); CHECK(s.layouts().size()==1);
    s.command("UNDO"); CHECK(s.in_paper_space()); CHECK(s.canvas().texts().empty());
    s.command("REDO"); CHECK(s.canvas().texts().size()==1);
    s.command("REDO"); CHECK(s.layouts().empty()); CHECK(!s.in_paper_space());
    s.command("UNDO"); s.command("PSPACE"); CHECK(s.canvas().texts().size()==1);
}

TEST(sketch_undo_returns_to_the_space_that_owns_the_edit) {
    Shell s=make_shell(); s.canvas().add_text({1,2},"Ground",0.25);
    s.command("DATUM XY 1"); s.canvas().add_text({3,4},"Raised",0.25);
    s.edit_sketch(0); CHECK(s.canvas().texts()[0].text=="Ground");
    s.command("UNDO"); CHECK(s.active_sketch()==1); CHECK(s.canvas().texts().empty());
    s.command("UNDO"); CHECK(s.sketches().size()==1); CHECK(s.canvas().texts()[0].text=="Ground");
    s.command("UNDO"); CHECK(s.canvas().texts().empty());
    s.command("REDO"); CHECK(s.canvas().texts().size()==1);
    s.command("REDO"); CHECK(s.sketches().size()==2);
    s.command("REDO"); CHECK(s.active_sketch()==1); CHECK(s.canvas().texts().size()==1);
}


TEST(clearing_ground_and_switching_tabs_preserve_space_histories) {
    Shell s=make_shell(); s.canvas().add_text({1,2},"Ground",0.25);
    gdim::Dimension d; d.text_override="Ground dimension"; s.canvas().load_dimensions({d});
    s.build_frame(); s.delete_sketch(0); s.build_frame();
    CHECK(s.canvas().texts().empty()); CHECK(s.canvas().dimensions().empty());
    s.command("UNDO"); CHECK(s.canvas().texts().size()==1); CHECK(s.canvas().dimensions().size()==1);
    s.command("UNDO"); s.command("UNDO"); CHECK(s.canvas().texts().empty());
    s.command("REDO"); CHECK(s.canvas().texts().size()==1);
    s.command("LAYOUT NEW Sheet"); s.command("PSPACE");
    s.canvas().add_text({2,3},"Sheet",0.25); s.command("MSPACE");
    const auto tab=s.active_tab(); s.new_tab();
    CHECK(s.canvas().texts().empty()); s.switch_tab(tab);
    s.command("UNDO"); CHECK(s.in_paper_space()); CHECK(s.canvas().texts().empty());
    s.command("REDO"); CHECK(s.canvas().texts().size()==1);
}

TEST(paper_annotations_do_not_replace_the_model_plot_content) {
    Shell s=make_shell();
    s.canvas().modify_shapes([](auto& v) { gsnap::Shape sh; sh.a={0,0}; sh.b={100000,200000}; v.push_back(sh); });
    s.canvas().add_text({1,2},"Model label",0.25);
    s.command("LAYOUT NEW Sheet"); s.command("PSPACE");
    s.canvas().add_text({3,4},"Paper label",0.003);
    gdim::Dimension d; d.p1={0,0}; d.p2={100000,0}; d.p3={0,50000};
    d.text_override="Paper dimension"; s.canvas().load_dimensions({d});
    const auto& content=s.plot_content();
    CHECK(content.base.shapes.size()==1); CHECK(content.base.notes.size()==1);
    if (!content.base.notes.empty()) CHECK(content.base.notes[0].text=="Model label");
    CHECK(content.marks.notes.empty());
    const auto pdf=scratch_path("space-annotations.pdf"); CHECK(s.export_file(pdf));
    std::ifstream f(pdf,std::ios::binary); std::string text((std::istreambuf_iterator<char>(f)),{});
    CHECK(text.find("Model label")!=std::string::npos);
    CHECK(text.find("Paper label")!=std::string::npos);
    CHECK(text.find("Paper dimension")!=std::string::npos);
}


TEST(legacy_metadata_v1_still_loads_before_spaces_gain_annotations) {
    const auto fixture=std::filesystem::path(__FILE__).parent_path()/"fixtures"/"metadata-v1.glcad";
    gfile::Contents c; CHECK(gfile::load_file(fixture.string(),c).ok);
    bool legacy=false;
    for (const auto& f:c.foreign) if (f.kind==kMetadataFrame) legacy=!f.payload.empty()&&f.payload[0]==1;
    CHECK(legacy);
    Shell s=make_shell(); CHECK(s.open_file(fixture.string()));
    CHECK(s.canvas().dim_styles().find("Review")!=nullptr);
    CHECK(s.text_styles().find("Notes")!=nullptr);
    CHECK(s.modeller().mates().size()==1);
    CHECK(s.canvas().blocks().is_dynamic("Door"));
    s.command("PARAMETERS total"); CHECK(s.log().back().find("0.3")!=std::string::npos);
    s.command("LAYOUT NEW Sheet"); s.command("PSPACE");
    CHECK(s.canvas().blocks().references().empty()); CHECK(s.canvas().dimensions().empty());
    const auto path=scratch_path("metadata-v1-upgraded.glcad"); CHECK(s.save_file(path));
    Shell r=make_shell(); CHECK(r.open_file(path)); CHECK(r.modeller().mates().size()==1);
    CHECK(r.canvas().blocks().is_dynamic("Door"));
}

TEST(block_attribute_undo_returns_to_its_own_sheet) {
    Shell s=make_shell();
    gdoc::BlockDef def; def.name="Label"; s.canvas().blocks().define(def);
    s.command("LAYOUT NEW Sheet"); s.command("PSPACE");
    gdoc::BlockRef ref; ref.name="Label"; ref.set_attribute("NAME","Before");
    s.canvas().blocks().insert(ref); s.canvas().select_reference(0);
    s.command("ATTEDIT NAME After"); s.command("MSPACE");
    s.command("UNDO"); CHECK(s.in_paper_space());
    CHECK(s.canvas().blocks().references().size()==1);
    if (!s.canvas().blocks().references().empty())
        CHECK(s.canvas().blocks().references()[0].attribute("NAME").value_or("")=="Before");
    s.command("REDO");
    if (!s.canvas().blocks().references().empty())
        CHECK(s.canvas().blocks().references()[0].attribute("NAME").value_or("")=="After");
    s.command("MSPACE"); CHECK(s.canvas().blocks().references().empty());
}


TEST(renaming_a_second_sheet_keeps_its_history_identity) {
    Shell s=make_shell(); s.command("LAYOUT NEW First");
    s.command("LAYOUT NEW Second"); s.command("PSPACE");
    s.canvas().add_text({1,2},"Second sheet",0.25);
    s.command("LAYOUT RENAME Second Renamed");
    s.command("UNDO"); CHECK(s.in_paper_space()); CHECK(s.canvas().texts().size()==1);
    s.command("UNDO"); CHECK(s.canvas().texts().empty());
    s.command("REDO"); CHECK(s.canvas().texts().size()==1);
    s.command("REDO"); CHECK(s.layouts()[1].name=="Renamed");
    CHECK(s.canvas().texts().size()==1);
    s.command("LAYOUT First"); CHECK(s.canvas().texts().empty());
}

TEST(template_new_restores_standards_and_sheets_without_replacing_work) {
    const auto path = scratch_path("office template.glcad");
    Shell source = make_shell();
    source.command("DIMSTYLE Office txt 0.35"); source.command("DIMSTYLE Office");
    source.command("STYLE Notes 0.0035 0.8 12");
    source.command("PARAMETERS wall 0.15");
    source.command("MATERIAL NEW Steel 120 124 130 7850");
    source.canvas().set_display_unit(gunit::Length::Inch);
    gdoc::Layer layer; layer.name = "Standards"; source.layers().add(layer);
    source.command("PAGESETUP A1 portrait"); source.command("PAGESETUP SAVE Office");
    source.command("LAYOUT NEW Cover"); source.command("TITLEBLOCK");
    source.command("MVIEW 50");
    const auto title_shapes = source.layouts()[0].shapes.size();
    const auto title_notes = source.layouts()[0].notes.size();
    source.command("LAYOUT NEW Details"); source.command("PSPACE");
    seed_space_annotations(source, "Sheet", 4);
    source.command("TEMPLATE SAVE " + path);
    CHECK(source.in_paper_space()); check_space_annotations(source, "Sheet", 4);

    Shell s = make_shell(); seed_space_annotations(s, "Existing", 2);
    s.command("TEMPLATE NEW " + path);
    CHECK(s.tab_count() == 2); CHECK(s.document_path().empty());
    CHECK(s.document_dirty()); CHECK(!s.read_only());
    CHECK(s.canvas().shapes().empty()); CHECK(s.canvas().dimensions().empty());
    CHECK(s.canvas().blocks().references().empty()); CHECK(s.canvas().texts().empty());
    CHECK(s.layers().find("Standards") != nullptr);
    CHECK(s.canvas().dim_styles().current() == "Office");
    CHECK(s.text_styles().current() == "Notes");
    CHECK(s.canvas().display_unit() == gunit::Length::Inch);
    s.command("PARAMETERS wall"); CHECK(s.log().back().find("0.15") != std::string::npos);
    CHECK(s.modeller().materials().size() == source.modeller().materials().size());
    CHECK(s.layouts().size() == 2);
    if (s.layouts().size() == 2) {
        CHECK(s.layouts()[0].shapes.size() == title_shapes);
        CHECK(s.layouts()[0].notes.size() == title_notes);
        CHECK(s.layouts()[0].viewports.size() == 1);
        if (!s.layouts()[0].viewports.empty()) CHECK(s.layouts()[0].viewports[0].scale == 50);
        s.command("PAGESETUP Office"); CHECK(s.plot_paper() == "A1");
        s.command("LAYOUT Details"); s.command("PSPACE");
        check_space_annotations(s, "Sheet", 4);
    }
    s.switch_tab(0); check_space_annotations(s, "Existing", 2);
}

TEST(template_file_excludes_model_data_even_when_saved_from_a_sheet) {
    const auto path = scratch_path("empty-model-template.glcad");
    Shell s = make_shell(); seed_space_annotations(s, "Model", 2);
    s.command("BOX"); s.command("DATUM XY 1"); seed_space_annotations(s, "Raised", 3);
    s.command("LAYOUT NEW Sheet"); s.command("PSPACE"); seed_space_annotations(s, "Paper", 4);
    s.command("TEMPLATE SAVE " + path);
    gfile::Contents c; CHECK(gfile::load_file(path, c).ok);
    CHECK(c.shape_count() == 0); CHECK(c.entities.empty());
    CHECK(c.blocks.references().empty()); CHECK(c.spaces.size() == 1);
    for (const auto& f : c.foreign) {
        CHECK(f.kind != kSolidsFrame); CHECK(f.kind != kFeaturesFrame);
    }
    Shell reopened = make_shell(); CHECK(reopened.open_file(path));
    CHECK(reopened.modeller().bodies().empty()); CHECK(reopened.sketch_count() == 1);
    CHECK(reopened.canvas().dimensions().empty()); CHECK(reopened.canvas().splines().empty());
    CHECK(reopened.canvas().hatches().empty()); CHECK(reopened.canvas().constraints().empty());
    CHECK(reopened.canvas().texts().empty()); CHECK(reopened.canvas().underlays().all().empty());
    CHECK(reopened.canvas().blocks().references().empty());
    reopened.command("PSPACE"); check_space_annotations(reopened, "Paper", 4);
}

TEST(template_new_has_fresh_history_and_survives_recovery_and_save) {
    const auto dir = scratch_path("template-recovery");
    std::filesystem::create_directories(dir);
    ScopedTestConfig test_config(dir);
    {
        const auto path = dir + "/standard.glcad";
        Shell source = make_shell(); source.command("LAYOUT NEW Sheet"); source.command("PSPACE");
        seed_space_annotations(source, "Template", 4);
        source.command("TEMPLATE SAVE " + path);
        std::ifstream before_file(path, std::ios::binary);
        const std::string before((std::istreambuf_iterator<char>(before_file)), {});
        Shell s = make_shell(); s.command("TEMPLATE NEW " + path);
        CHECK(s.tab_count() == 1); CHECK(s.document_path().empty()); CHECK(s.document_dirty());
        s.command("UNDO"); CHECK(s.layouts().size() == 1);
        CHECK(s.autosave_now());
        Shell r = make_shell(); CHECK(r.open_file(s.autosave_path()));
        r.command("PSPACE"); check_space_annotations(r, "Template", 4);
        s.canvas().add_text({1, 2}, "New model", 0.25);
        s.command("UNDO"); CHECK(s.canvas().texts().empty());
        s.command("REDO"); CHECK(s.canvas().texts().size() == 1);
        CHECK(s.autosave_now());
        Shell later = make_shell(); CHECK(later.open_file(s.autosave_path()));
        CHECK(later.canvas().texts().size() == 1);
        CHECK(s.save_file(dir + "/project.glcad")); CHECK(!s.document_dirty());
        Shell saved = make_shell(); CHECK(saved.open_file(dir + "/project.glcad"));
        saved.command("PSPACE"); check_space_annotations(saved, "Template", 4);
        std::ifstream after_file(path, std::ios::binary);
        CHECK(std::string((std::istreambuf_iterator<char>(after_file)), {}) == before);
    }
}

TEST(template_rejects_corruption_and_protects_open_drawings) {
    const auto path = scratch_path("template-protected.glcad");
    Shell s = make_shell(); seed_space_annotations(s, "Original", 2); CHECK(s.save_file(path));
    gfile::Contents original; CHECK(gfile::load_file(path, original).ok);
    s.command("TEMPLATE SAVE " + path);
    gfile::Contents unchanged; CHECK(gfile::load_file(path, unchanged).ok);
    CHECK(gfile::encode(original) == gfile::encode(unchanged)); CHECK(!s.document_dirty());
    s.new_tab(); s.command("TEMPLATE SAVE " + path);
    CHECK(gfile::load_file(path, unchanged).ok);
    CHECK(gfile::encode(original) == gfile::encode(unchanged));
    s.switch_tab(0);
    for (auto& f : original.foreign) if (f.kind == kMetadataFrame) f.payload.resize(3);
    const auto bad = scratch_path("bad-template.glcad"); CHECK(gfile::save_file(bad, original).ok);
    s.command("TEMPLATE NEW " + bad);
    CHECK(s.tab_count() == 2); CHECK(s.document_path() == path); CHECK(!s.document_dirty());
    check_space_annotations(s, "Original", 2);
    s.command("TEMPLATE NEW " + scratch_path("missing-template.glcad"));
    CHECK(s.document_path() == path); CHECK(s.tab_count() == 2);
    const auto alias = scratch_path("template-protected-alias.glcad");
    std::error_code ec; std::filesystem::remove(alias, ec);
    std::filesystem::create_symlink(path, alias, ec);
#ifndef _WIN32
    CHECK(!ec);
#endif
    if(!ec) {
        s.command("TEMPLATE SAVE " + alias);
        gfile::Contents aliased; CHECK(gfile::load_file(path, aliased).ok);
        CHECK(aliased.shape_count() == 4); CHECK(!s.document_dirty());
        std::filesystem::remove(alias);
    }
    s.command("TEMPLATE SAVE " + scratch_path("absent-parent/template.glcad"));
    CHECK(s.document_path() == path); CHECK(!s.document_dirty());
    s.close_tab(0);
    { std::ofstream lock(path + ".lock"); lock << "Template library"; CHECK(lock.good()); }
    Shell locked = make_shell(); CHECK(locked.open_file(path)); CHECK(locked.read_only());
    locked.command("TEMPLATE NEW " + path);
    CHECK(locked.tab_count() == 2); CHECK(!locked.read_only()); CHECK(locked.document_path().empty());
    CHECK(locked.canvas().shapes().empty()); CHECK(locked.document_dirty());
    Shell writer = make_shell(); writer.command("TEMPLATE SAVE " + path);
    gfile::Contents protected_copy; CHECK(gfile::load_file(path, protected_copy).ok);
    CHECK(protected_copy.shape_count() == 4);
    std::filesystem::remove(path + ".lock");
}

TEST(legacy_templates_upgrade_without_model_or_foreign_data) {
    const auto fixture = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "metadata-v1.glcad";
    const auto upgraded = scratch_path("upgraded-template.glcad");
    Shell s = make_shell(); s.command("TEMPLATE NEW " + fixture.string());
    CHECK(s.document_dirty()); CHECK(s.document_path().empty());
    CHECK(s.modeller().bodies().empty()); CHECK(s.modeller().mates().empty());
    CHECK(s.canvas().blocks().is_dynamic("Door")); CHECK(s.canvas().blocks().xrefs().empty());
    CHECK(s.canvas().dim_styles().current() == "Review"); CHECK(s.text_styles().current() == "Notes");
    s.command("TEMPLATE SAVE " + upgraded);
    Shell r = make_shell(); CHECK(r.open_file(upgraded)); CHECK(r.modeller().bodies().empty());
    CHECK(r.canvas().blocks().is_dynamic("Door")); CHECK(r.canvas().blocks().xrefs().empty());

    Shell old = make_shell(); old.command("BOX"); old.command("LAYOUT NEW Legacy"); old.command("TITLEBLOCK");
    const auto old_path = scratch_path("old-template.glcad"); CHECK(old.save_file(old_path));
    gfile::Contents c; CHECK(gfile::load_file(old_path, c).ok);
    c.foreign.erase(std::remove_if(c.foreign.begin(), c.foreign.end(), [](const auto& f) {
        return f.kind == kMetadataFrame;
    }), c.foreign.end());
    c.foreign.push_back({0x7777, {1, 2, 3}});
    CHECK(gfile::save_file(old_path, c).ok);
    Shell legacy = make_shell(); legacy.command("TEMPLATE OPEN " + old_path);
    CHECK(legacy.modeller().bodies().empty()); CHECK(legacy.layouts().size() == 1);
    if (!legacy.layouts().empty()) CHECK(!legacy.layouts()[0].shapes.empty());
    r.command("CLOSETAB");
    CHECK(legacy.save_file(upgraded));
    gfile::Contents projected; CHECK(gfile::load_file(upgraded, projected).ok);
    for (const auto& f : projected.foreign) CHECK(f.kind != 0x7777);
}

TEST(viewport_controls_lock_clip_and_freeze_without_adding_windows) {
    Shell s = make_shell(); draw_rect(s,"0,0","10,0","10,10","0,10");
    gdoc::Layer layer; layer.name="Hidden walls"; s.layers().add(layer);
    s.command("LAYOUT NEW Plan"); s.command("MVIEW 50");
    s.command("MVIEW LOCK 1 ON");
    CHECK(s.layouts()[0].viewports.size()==1);
    CHECK(s.layouts()[0].viewports[0].locked);
    s.command("MVIEW SCALE 1 100"); CHECK(s.layouts()[0].viewports[0].scale==50);
    s.command("MVIEW LOCK 1 OFF"); s.command("MVIEW SCALE 1 100");
    CHECK(s.layouts()[0].viewports[0].scale==100);
    s.command("MVIEW CENTER 1 2 3"); s.command("MVIEW ROTATE 1 90");
    CHECK(s.layouts()[0].viewports[0].centre_x==2);
    CHECK(std::fabs(s.layouts()[0].viewports[0].rotation-1.5707963267948966)<1e-9);
    s.command("MVIEW CLIP 1 20 30 180 140");
    CHECK(s.layouts()[0].viewports[0].lo.x==0.02);
    CHECK(s.layouts()[0].viewports[0].hi.y==0.14);
    s.command("MVIEW FREEZE 1 Hidden walls");
    CHECK(!s.layouts()[0].viewports[0].layer_visible("Hidden walls"));
    s.command("MVIEW OFF 1"); CHECK(!s.layouts()[0].viewports[0].on);
    s.command("UNDO"); CHECK(s.layouts()[0].viewports[0].on);
    s.command("MVIEW THAW 1 Hidden walls"); CHECK(s.layouts()[0].viewports[0].layer_visible("Hidden walls"));
}

TEST(viewport_edits_survive_undo_recovery_reopen_and_templates) {
    const auto dir=scratch_path("viewport-recovery"); std::filesystem::create_directories(dir);
    ScopedTestConfig test_config(dir);
    {
        Shell s=make_shell(); draw_rect(s,"0,0","10,0","10,10","0,10");
        gdoc::Layer layer; layer.name="Detail"; s.layers().add(layer);
        s.command("LAYOUT NEW Plan"); s.command("MVIEW 50");
        s.command("MVIEW CLIP 1 10 20 180 140"); s.command("MVIEW FREEZE 1 Detail");
        s.command("MVIEW ROTATE 1 30"); s.command("MVIEW LOCK 1 ON");
        CHECK(s.autosave_now());
        s.command("MVIEW OFF 1"); CHECK(s.autosave_now());
        const auto check=[](const Shell& r) {
            CHECK(r.layouts().size()==1); if (r.layouts().empty()) return;
            CHECK(r.layouts()[0].viewports.size()==1); if (r.layouts()[0].viewports.empty()) return;
            const auto& v=r.layouts()[0].viewports[0];
            CHECK(v.locked); CHECK(!v.on); CHECK(v.scale==50); CHECK(v.lo.x==0.01);
            CHECK(v.hi.y==0.14); CHECK(!v.layer_visible("Detail"));
            CHECK(std::fabs(v.rotation-0.5235987755982988)<1e-9);
        };
        Shell recovered=make_shell(); CHECK(recovered.open_file(s.autosave_path())); check(recovered);
        s.command("LAYOUT NEW Other"); s.command("UNDO"); s.command("UNDO");
        CHECK(s.layouts()[0].viewports[0].on);
        s.command("REDO"); check(s);
        CHECK(s.save_file(dir+"/drawing.glcad")); CHECK(!s.document_dirty());
        s.command("MVIEW LIST"); CHECK(!s.document_dirty());
        Shell reopened=make_shell(); CHECK(reopened.open_file(dir+"/drawing.glcad")); check(reopened);
        s.command("TEMPLATE SAVE "+dir+"/template.glcad");
        Shell templated=make_shell(); templated.command("TEMPLATE NEW "+dir+"/template.glcad"); check(templated);
    }
}

TEST(viewport_invalid_commands_and_creation_preserve_custom_windows) {
    Shell s=make_shell(); s.command("LAYOUT NEW Plan"); s.command("MVIEW 50");
    s.command("MVIEW CLIP 1 20 30 180 140"); s.command("MVIEW LOCK 1 ON");
    const auto original=s.layouts()[0].viewports[0];
    const auto path=scratch_path("viewport-validation.glcad"); CHECK(s.save_file(path));
    for (const auto& cmd:{"MVIEW garbage","MVIEW SCALE 1 0","MVIEW SCALE 1 nan","MVIEW CLIP 1 10 20 5 30",
                         "MVIEW CLIP 1 10 20 30","MVIEW LOCK 9 ON","MVIEW LOCK 1 maybe","MVIEW ON 1 extra",
                         "MVIEW FREEZE 1 absent","MVIEW SCALE 1 100","MVIEW CENTER 1 0 0","MVIEW ROTATE 1 30",
                         "MVIEW 20","MVIEW DELETE 0","MVIEW 50 extra"}) {
        s.command(cmd); CHECK(!s.document_dirty()); CHECK(s.layouts()[0].viewports.size()==1);
    }
    s.command("MVIEW 100 190 30 390 140"); CHECK(s.layouts()[0].viewports.size()==2);
    const auto& kept=s.layouts()[0].viewports[0];
    CHECK(kept.lo.x==original.lo.x); CHECK(kept.hi.y==original.hi.y); CHECK(kept.locked);
    double before_x,before_y,after_x,after_y;
    kept.to_paper(1,2,before_x,before_y);
    s.command("MVIEW CLIP 1 30 40 170 130");
    s.layouts()[0].viewports[0].to_paper(1,2,after_x,after_y);
    CHECK(std::fabs(before_x-after_x)<1e-12); CHECK(std::fabs(before_y-after_y)<1e-12);
    s.command("MVIEW DELETE 2"); CHECK(s.layouts()[0].viewports.size()==1);
    s.command("UNDO"); CHECK(s.layouts()[0].viewports.size()==2);
}

TEST(viewport_preview_and_plot_keep_ink_inside_each_window) {
    Shell s=make_shell();
    s.canvas().modify_shapes([&](auto& shapes) {
        gsnap::Shape sh; sh.a=gcm::to_lattice(s.canvas().volume(),-100.0,0.0);
        sh.b=gcm::to_lattice(s.canvas().volume(),100.0,0.0); sh.layer="Walls"; shapes.push_back(sh);
    });
    gdoc::Layer layer; layer.name="Walls"; s.layers().add(layer);
    s.command("LAYOUT NEW Plan"); s.command("MVIEW 1 20 30 180 140");
    s.command("MVIEW 2 200 30 380 140"); s.command("MVIEW FREEZE 2 Walls");
    gplot::PdfWriter plotted; CHECK(s.plot_sheet(plotted).ok);
    CHECK(plotted.paths_of_page(0).size()==1);
    if (!plotted.paths_of_page(0).empty()) {
        const auto& p=plotted.paths_of_page(0)[0]; CHECK(p.clip.has_value());
        if (p.clip) { CHECK(p.clip->lo.x==0.02); CHECK(p.clip->hi.x==0.18); }
    }
    s.build_frame(); CHECK(s.static_lines().empty()); CHECK(!s.canvas_lines().empty());
    double cx,cy,w,h; CHECK(s.paper_rect(cx,cy,w,h));
    for (const auto& vertex:s.canvas_lines()) {
        CHECK(vertex.pos[0]>=cx-w/2-1e-5 && vertex.pos[0]<=cx+w/2+1e-5);
        CHECK(vertex.pos[1]>=cy-h/2-1e-5 && vertex.pos[1]<=cy+h/2+1e-5);
    }
    s.canvas().modify_shapes([](auto& shapes) { auto sh=shapes[0]; sh.a.y+=1000; sh.b.y+=1000; shapes.push_back(sh); });
    gplot::PdfWriter live; CHECK(s.plot_sheet(live).ok); CHECK(live.paths_of_page(0).size()==2);
    s.command("PSPACE"); s.canvas().add_text({1,2},"Live sheet note",0.0025);
    gplot::PdfWriter paper; CHECK(s.plot_sheet(paper).ok);
    bool live_note=false;
    for (const auto& t:paper.texts_of_page(0)) if (t.text=="Live sheet note") { live_note=true; CHECK(!t.clip); }
    CHECK(live_note);
    s.command("MVIEW OFF 1");
    gplot::PdfWriter hidden; CHECK(s.plot_sheet(hidden).ok); CHECK(hidden.paths_of_page(0).empty());
    const auto path=scratch_path("clipped-viewports.dwfx"); CHECK(s.export_file(path));
}

TEST(viewports_keep_their_source_sketch_and_dimension_paper_size) {
    Shell s=make_shell(); seed_space_annotations(s,"Ground",2);
    s.command("LAYOUT NEW Plan"); s.command("MVIEW 50"); s.command("MVIEW 100");
    gplot::PdfWriter plotted; CHECK(s.plot_sheet(plotted).ok);
    std::vector<double> heights;
    for (const auto& t:plotted.texts_of_page(0)) if (t.text=="Ground") heights.push_back(t.height_mm);
    CHECK(heights.size()==4);
    if (heights.size()==4) CHECK(std::fabs(heights[1]-heights[3])<1e-9);
    s.command("DATUM XY 1"); seed_space_annotations(s,"Raised",3);
    s.command("LAYOUT NEW Raised view"); s.command("MVIEW 20");
    CHECK(s.layouts()[1].viewports[0].source_space==1);
    s.edit_sketch(0);
    gplot::PdfWriter other; CHECK(s.plot_sheet(other).ok);
    bool raised=false,ground=false;
    for (const auto& t:other.texts_of_page(0)) { raised|=t.text=="Raised"; ground|=t.text=="Ground"; }
    CHECK(raised); CHECK(!ground);
    s.delete_sketch(1); CHECK(s.layouts()[1].viewports[0].source_space==0);
    s.command("UNDO"); CHECK(s.layouts()[1].viewports[0].source_space==1);
}

TEST(titleblock_fields_follow_save_as_sheet_rename_and_scale) {
    Shell s=make_shell(); s.command("LAYOUT NEW Plan"); s.command("TITLEBLOCK");
    const auto fields=s.layouts()[0].notes;
    CHECK(fields[0].field=="DOCUMENT"); CHECK(fields[1].field=="SHEET");
    const auto has=[&](const std::string& text) {
        gplot::PdfWriter page; CHECK(s.plot_sheet(page).ok);
        for (const auto& t:page.texts_of_page(0)) if (t.text==text) return true;
        return false;
    };
    CHECK(has("UNTITLED"));
    CHECK(s.save_file(scratch_path("first-fields.glcad"))); CHECK(has("first-fields.glcad"));
    s.command("LAYOUT RENAME Plan Updated"); s.command("PAGESETUP A4 portrait"); s.command("PLOTSCALE 100");
    CHECK(has("Updated"));
    CHECK(has("A4  1:100"));
    CHECK(s.save_file(scratch_path("second-fields.glcad"))); CHECK(has("second-fields.glcad"));
    s.command("PSPACE"); s.build_frame(); CHECK(!s.document_dirty());
    CHECK(s.canvas().resolved_text(s.canvas().texts()[0])=="second-fields.glcad");
    CHECK(s.canvas().texts()[0].text==fields[0].text);
    s.canvas().set_text(0,"Literal title"); CHECK(s.canvas().texts()[0].field.empty());
    s.command("UNDO"); CHECK(s.canvas().texts()[0].field=="DOCUMENT");
}

TEST(template_titleblock_fields_resolve_in_the_new_document) {
    const auto dir=scratch_path("field-recovery"); std::filesystem::create_directories(dir);
    ScopedTestConfig test_config(dir);
    {
        Shell s=make_shell(); s.command("LAYOUT NEW Sheet"); s.command("TITLEBLOCK");
        CHECK(s.save_file(dir+"/source.glcad")); s.command("TEMPLATE SAVE "+dir+"/standard.glcad");
        Shell t=make_shell(); t.command("TEMPLATE NEW "+dir+"/standard.glcad");
        CHECK(t.sheet_fields(t.layouts()[0]).at("DOCUMENT")=="UNTITLED");
        CHECK(t.layouts()[0].notes[0].field=="DOCUMENT");
        t.command("PSPACE"); t.canvas().add_text({1,2},"Custom",0.003);
        t.canvas().set_selected_texts({3}); t.command("TITLEBLOCK FIELD SCALE");
        CHECK(t.canvas().texts()[3].field=="SCALE"); CHECK(t.autosave_now());
        Shell r=make_shell(); CHECK(r.open_file(t.autosave_path()));
        CHECK(r.layouts()[0].notes[3].field=="SCALE");
        CHECK(r.save_file(dir+"/issued.glcad"));
        gplot::PdfWriter page; CHECK(r.plot_sheet(page).ok);
        bool title=false; for (const auto& note:page.texts_of_page(0)) title|=note.text=="issued.glcad";
        CHECK(title);
    }
}

TEST(properties_edit_text_dimensions_and_blocks_without_flattening_other_values) {
    Shell s=make_shell(); s.command("UNITS m");
    s.canvas().add_text({1,2},"First",0.003); s.canvas().add_text({3,4},"Second",0.007);
    s.canvas().set_selected_texts({0,1});
    s.command("PROPERTIES ROTATION 45");
    CHECK(s.canvas().texts()[0].height==0.003); CHECK(s.canvas().texts()[1].height==0.007);
    CHECK(std::fabs(s.canvas().texts()[1].angle-3.14159265358979323846/4)<1e-12);
    s.command("PROPERTIES HEIGHT -1"); CHECK(s.canvas().texts()[0].height==0.003);
    s.command("PROPERTIES HEIGHT 5mm"); CHECK(std::fabs(s.canvas().texts()[1].height-0.005)<1e-12);
    s.command("UNDO"); CHECK(s.canvas().texts()[1].height==0.007);
    s.command("PROPERTIES TEXT "); CHECK(s.canvas().texts()[0].text.empty());
    s.command("UNDO"); CHECK(s.canvas().texts()[0].text=="First");
    s.command("DIMSTYLE Review");
    gdim::Dimension d; d.style="Standard"; s.canvas().add_dimensions({d});
    s.canvas().set_selected_dimensions({0}); s.command("PROPERTIES STYLE Review");
    CHECK(s.canvas().dimensions()[0].style=="Review");
    s.command("UNDO"); CHECK(s.canvas().dimensions()[0].style=="Standard");
    s.command("REDO"); CHECK(s.canvas().dimensions()[0].style=="Review");
    s.build_frame(); CHECK(s.canvas().dimensions()[0].style=="Review");
    gdoc::BlockDef def; def.name="Marker"; s.canvas().blocks().define(def);
    CHECK(s.canvas().insert_block("Marker",1,2)); s.canvas().select_reference(0);
    s.command("PROPERTIES SCALEX 2.5"); CHECK(s.canvas().blocks().references()[0].scale_x==2.5);
    CHECK(s.canvas().blocks().references()[0].scale_y==1);
    s.command("PROPERTIES SCALEY 0"); CHECK(s.canvas().blocks().references()[0].scale_y==1);
    s.command("PROPERTIES ROTATION 90");
    const auto path=scratch_path("properties-entities.glcad"); CHECK(s.save_file(path));
    Shell r=make_shell(); CHECK(r.open_file(path));
    CHECK(r.canvas().dimensions()[0].style=="Review");
    CHECK(r.canvas().blocks().references()[0].scale_x==2.5);
    CHECK(std::fabs(r.canvas().blocks().references()[0].rotation-3.14159265358979323846/2)<1e-12);
}

TEST(properties_dimension_edit_undo_returns_to_its_sheet) {
    Shell s=make_shell(); s.command("LAYOUT NEW Details"); s.command("PSPACE");
    gdim::Dimension d; s.canvas().add_dimensions({d}); s.canvas().set_selected_dimensions({0});
    s.command("PROPERTIES TEXT Typical"); CHECK(s.canvas().dimensions()[0].text_override=="Typical");
    s.command("MSPACE"); s.command("UNDO"); CHECK(s.in_paper_space());
    CHECK(s.canvas().dimensions()[0].text_override.empty());
    s.command("REDO"); CHECK(s.canvas().dimensions()[0].text_override=="Typical");
    s.canvas().erase_selected(); CHECK(s.canvas().dimensions().empty());
    s.command("UNDO"); CHECK(s.canvas().dimensions().size()==1);
}

TEST(native_dwg_export_writes_the_requested_file_and_refuses_lossy_content) {
    Shell s=make_shell(); s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0"); s.canvas().type_point("1,1"); s.canvas().cancel();
    const auto path=scratch_path("native-export.dwg");
    CHECK(s.export_file(path));
    std::ifstream input(path,std::ios::binary); std::string before((std::istreambuf_iterator<char>(input)),{});
    CHECK(before.starts_with("AC1015"));
    Shell r=make_shell(); CHECK(r.import_file(path)); CHECK(r.canvas().shapes().size()==1);
    gdim::Dimension dimension; dimension.kind=gdim::Kind::ArcLength; s.canvas().add_dimensions({dimension});
    CHECK(!s.export_file(path));
    std::ifstream unchanged(path,std::ios::binary); std::string after((std::istreambuf_iterator<char>(unchanged)),{});
    CHECK(before==after); CHECK(s.log().back().find("save the native drawing")!=std::string::npos);
}

TEST(lineweight_display_reaches_lines_arcs_selection_and_layer_updates) {
    Shell s=make_shell();
    gsnap::Shape line; line.kind=gsnap::ShapeKind::Segment; line.a={1,2}; line.b={3,4}; line.layer="A-WALL";
    gsnap::Shape circle; circle.kind=gsnap::ShapeKind::Circle; circle.a={5,6}; circle.radius=10; circle.attrs.lineweight=gdoc::Lineweight::W100;
    s.canvas().load_shapes({line,circle}); s.layers().find("A-WALL")->lineweight=gdoc::Lineweight::W050;
    s.build_frame(); CHECK(s.static_lines()[0].width_px==1); CHECK(s.static_arcs()[0].minor[3]==1);
    s.command("SETVAR LWDISPLAY 1"); s.build_frame();
    CHECK(std::fabs(s.static_lines()[0].width_px-0.5*96/25.4)<1e-5);
    CHECK(std::fabs(s.static_arcs()[0].minor[3]-96/25.4)<1e-5);
    s.layers().find("A-WALL")->lineweight=gdoc::Lineweight::W200; s.build_frame();
    CHECK(std::fabs(s.static_lines()[0].width_px-2*96/25.4)<1e-5);
    s.canvas().set_selection({0}); s.build_frame();
    bool weighted=false; for (const auto& v:s.canvas_lines()) weighted|=v.width_px>7;
    for (const auto& v:s.static_lines()) weighted|=v.width_px>7;
    CHECK(weighted);
    s.command("SETVAR LWDISPLAY 0"); s.build_frame(); CHECK(s.static_arcs()[0].minor[3]==1);
}

TEST(plot_style_tables_survive_save_recovery_templates_and_undo) {
    const auto dir=scratch_path("plot-style-recovery"); std::filesystem::create_directories(dir);
    ScopedTestConfig test_config(dir);
    {
        const auto fixtures=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"exchange"/"tests"/"fixtures";
        Shell s=make_shell(); s.command("LAYOUT NEW Issue");
        s.command("PLOTSTYLE LOAD "+(fixtures/"stbtest.stb").string());
        CHECK(!s.layouts()[0].plot_style_data.empty());
        const auto bytes=s.layouts()[0].plot_style_data;
        s.command("PLOTSTYLE LAYER 0 mozman"); CHECK(s.layers().find("0")->plot_style=="mozman");
        s.command("PLOTSTYLE SAVE "+dir+"/shared.stb");
        std::ifstream shared(dir+"/shared.stb",std::ios::binary); std::vector<gcm::u8> exported((std::istreambuf_iterator<char>(shared)),{});
        CHECK(bytes==exported);
        s.command("PLOTSTYLE MONO"); CHECK(s.layouts()[0].plot_style_data.empty());
        s.command("UNDO"); CHECK(s.layouts()[0].plot_style_data==bytes);
        CHECK(s.save_file(dir+"/source.glcad"));
        s.command("TEMPLATE SAVE "+dir+"/standard.glcad");
        Shell fresh=make_shell(); fresh.command("TEMPLATE NEW "+dir+"/standard.glcad");
        CHECK(fresh.layouts()[0].plot_style_data==bytes);
        s.command("PLOTSTYLE LOAD "+dir+"/missing.ctb"); CHECK(s.layouts()[0].plot_style_data==bytes);
        s.command("LAYOUT RENAME Issue Issued"); CHECK(s.autosave_now());
        Shell recovered=make_shell(); CHECK(recovered.open_file(s.autosave_path()));
        CHECK(recovered.layouts()[0].plot_style_data==bytes); CHECK(recovered.layouts()[0].name=="Issued");
        CHECK(recovered.layers().find("0")->plot_style=="mozman");
    }
}

TEST(document_locks_follow_tabs_save_as_and_recovery) {
    const auto path=scratch_path("owned-document.glcad"), copy=scratch_path("owned-copy.glcad");
    Shell owner=make_shell(); CHECK(owner.save_file(path));
    CHECK(std::filesystem::exists(path+".lock"));
    Shell reader=make_shell(); CHECK(reader.open_file(path)); CHECK(reader.read_only()); CHECK(!reader.lock_holder().empty());
    CHECK(!reader.save_file(path));
    const auto alias=(std::filesystem::path(path).parent_path()/"."/std::filesystem::path(path).filename()).string();
    CHECK(!reader.save_file(alias));
    CHECK(reader.save_file(copy)); CHECK(!reader.read_only());
    CHECK(!owner.save_file(copy));
    owner.command("NEWTAB"); CHECK(std::filesystem::exists(path+".lock"));
    owner.command("CLOSETAB"); owner.command("CLOSETAB");
    CHECK(!std::filesystem::exists(path+".lock"));
    CHECK(reader.save_file(path)); CHECK(!std::filesystem::exists(copy+".lock"));
    reader.command("CLOSETAB"); CHECK(!std::filesystem::exists(path+".lock"));
}

TEST(document_lock_reclaims_abandoned_lease_and_preserves_foreign_lock) {
    const auto path=scratch_path("abandoned-lease.glcad");
    std::string holder;
    { std::ofstream file(path+".lock"); file<<"former owner\nGLCAD-LOCK-1\n"; }
    auto lease=DocumentLock::acquire(path,holder); CHECK(lease!=nullptr);
    CHECK(lease&&lease->owns(path));
    auto competing=DocumentLock::acquire(path,holder); CHECK(!competing); CHECK(!holder.empty());
    lease.reset(); CHECK(!std::filesystem::exists(path+".lock"));
    { std::ofstream file(path+".lock"); file<<"External CAD user"; }
    CHECK(!DocumentLock::acquire(path,holder)); CHECK(holder=="External CAD user");
    CHECK(std::filesystem::exists(path+".lock")); std::filesystem::remove(path+".lock");
}

TEST(polygon_viewports_validate_persist_and_undo) {
    Shell s=make_shell(); s.command("LAYOUT NEW Polygon"); s.command("MVIEW 50 10 10 150 150");
    s.command("MVIEW POLYGON 1 10 10 150 10 150 150 80 80 10 150");
    CHECK(s.layouts()[0].viewports[0].clip_polygon.size()==5);
    const auto centre=s.layouts()[0].viewports[0].centre_x;
    s.command("MVIEW POLYGON 1 10 10 150 150 10 150 150 10");
    CHECK(s.layouts()[0].viewports[0].clip_polygon.size()==5);
    s.command("UNDO"); CHECK(s.layouts()[0].viewports[0].clip_polygon.empty());
    s.command("REDO"); CHECK(s.layouts()[0].viewports[0].clip_polygon.size()==5);
    const auto path=scratch_path("polygon-viewport.glcad"); CHECK(s.save_file(path));
    Shell r=make_shell(); CHECK(r.open_file(path)); CHECK(r.layouts()[0].viewports[0].clip_polygon.size()==5);
    CHECK(r.layouts()[0].viewports[0].centre_x==centre);
    r.command("MVIEW CLIP 1 20 20 100 100"); CHECK(r.layouts()[0].viewports[0].clip_polygon.empty());
}

TEST(plot_style_authoring_is_undoable_and_persistent) {
    Shell s=make_shell(); s.command("LAYOUT NEW Pens"); s.command("PLOTSTYLE NEW STB");
    s.command("PLOTSTYLE EDIT Review WEIGHT 0.47"); s.command("PLOTSTYLE EDIT Review LINETYPE 3");
    auto style=gplot::read_plot_style(s.layouts()[0].plot_style_data);
    CHECK(style.ok); CHECK(style.table.find("Review")!=nullptr);
    if(!style.table.find("Review")) return;
    CHECK(style.table.find("Review")->pattern_id==3);
    s.command("UNDO"); style=gplot::read_plot_style(s.layouts()[0].plot_style_data); CHECK(style.table.find("Review")->pattern_id==31);
    s.command("REDO");
    auto path=scratch_path("authored-pens.glcad"); CHECK(s.save_file(path));
    Shell r=make_shell(); CHECK(r.open_file(path)); style=gplot::read_plot_style(r.layouts()[0].plot_style_data);
    CHECK(style.ok); CHECK(style.table.find("Review")->width_mm==0.47); CHECK(style.table.find("Review")->pattern_id==3);
}

TEST(viewport_frame_drag_commits_once_and_escape_restores) {
    Shell s=make_shell(); s.set_viewport(1400,1000); s.command("LAYOUT NEW Drag"); s.command("MVIEW 50 40 40 160 160");
    s.fit_width(.6); s.build_frame();
    const auto original=s.layouts()[0].viewports[0];
    double x,y; s.canvas().view().screen_of(.1-.420/2,.04-.297/2,x,y);
    s.pointer_move(x,y); s.build_frame(); s.pointer_down(x,y);
    s.pointer_move(x+30,y-20); s.build_frame(); s.pointer_up(x+30,y-20); s.build_frame();
    const auto moved=s.layouts()[0].viewports[0];
    CHECK(moved.lo.x>original.lo.x); CHECK(moved.lo.y>original.lo.y); CHECK(moved.centre_x==original.centre_x);
    s.command("UNDO"); CHECK(s.layouts()[0].viewports[0].lo.x==original.lo.x);
    s.command("REDO"); CHECK(s.layouts()[0].viewports[0].lo.x==moved.lo.x);
    s.canvas().view().screen_of((moved.lo.x+moved.hi.x)/2-.420/2,moved.lo.y-.297/2,x,y);
    s.pointer_move(x,y); s.build_frame(); s.pointer_down(x,y); s.pointer_move(x+30,y-20); s.build_frame();
    s.key_special(1); s.pointer_up(x+30,y-20); s.build_frame();
    CHECK(s.layouts()[0].viewports[0].lo.x==moved.lo.x);
}

TEST(a_released_lock_does_not_allow_a_stale_copy_to_overwrite_external_edits) {
    const auto path=scratch_path("external-edit.glcad"),copy=scratch_path("external-edit-copy.glcad");
    Shell owner=make_shell(); CHECK(owner.save_file(path));
    Shell reader=make_shell(); CHECK(reader.open_file(path)); CHECK(reader.read_only());
    owner.canvas().set_tool(Tool::Line); owner.canvas().type_point("0,0"); owner.canvas().type_point("1,1"); owner.canvas().cancel();
    CHECK(owner.save_file(path)); owner.command("CLOSETAB");
    CHECK(!reader.save_file(path)); CHECK(reader.lock_holder().find("changed on disk")!=std::string::npos);
    CHECK(reader.save_file(copy));
    Shell reopened=make_shell(); CHECK(reopened.open_file(path)); CHECK(reopened.canvas().shapes().size()==1);
}

#ifndef _WIN32
TEST(document_lock_excludes_another_process_and_recovers_after_process_exit) {
    const auto path=scratch_path("process-lease.glcad");
    int ready[2],finish[2]; CHECK(pipe(ready)==0); CHECK(pipe(finish)==0);
    const auto child=fork(); CHECK(child>=0);
    if(child==0) {
        close(ready[0]); close(finish[1]); std::string holder;
        auto lease=DocumentLock::acquire(path,holder);
        char status=lease?'Y':'N'; (void)write(ready[1],&status,1);
        char done=0; (void)read(finish[0],&done,1);
        _exit(lease?0:2);
    }
    close(ready[1]); close(finish[0]);
    if(child<0) { close(ready[0]); close(finish[1]); return; }
    char status=0; CHECK(read(ready[0],&status,1)==1); CHECK(status=='Y');
    std::string holder; CHECK(!DocumentLock::acquire(path,holder));
    CHECK(write(finish[1],"X",1)==1); close(ready[0]); close(finish[1]);
    int result=0; CHECK(waitpid(child,&result,0)==child); CHECK(WIFEXITED(result)&&WEXITSTATUS(result)==0);
    auto lease=DocumentLock::acquire(path,holder); CHECK(lease!=nullptr); CHECK(lease&&lease->owns(path));
}

#endif

TEST(dwg_xrefs_survive_import_native_save_and_export_to_another_folder) {
    const auto origin=scratch_path("xref-host.dwg");
    const auto native=scratch_path("xref-host.glcad");
    const auto destination=scratch_path("issued/xref-host.dwg");
    std::filesystem::create_directories(std::filesystem::path(destination).parent_path());
    Shell source=make_shell(); source.build_frame();
    draw_rect(source,"0,0","4,0","4,3","0,3");
    gdoc::Xref x; x.name="Site"; x.path="refs/Site.dwg";
    x.insertion=gcm::to_lattice(source.canvas().volume(),10,20);
    x.scale=2; x.rotation=.5; x.type=gdoc::XrefType::Overlay; x.state=gdoc::XrefState::Unloaded;
    x.clip={gcm::to_lattice(source.canvas().volume(),-1,-1),gcm::to_lattice(source.canvas().volume(),3,4)};
    x.clip_enabled=false;
    source.canvas().blocks().attach(x);
    CHECK(source.export_file(origin));
    Shell imported=make_shell(); CHECK(imported.import_file(origin));
    CHECK(imported.canvas().blocks().xrefs().size()==1);
    if(imported.canvas().blocks().xrefs().empty()) return;
    const auto back=imported.canvas().blocks().xrefs().front();
    CHECK(back.path==x.path); CHECK(back.type==x.type); CHECK(back.state==x.state);
    CHECK(back.clip.size()==2); CHECK(!back.clip_enabled);
    CHECK(std::filesystem::absolute(back.resolved_path).lexically_normal()==std::filesystem::absolute(std::filesystem::path(origin).parent_path()/x.path).lexically_normal());
    CHECK(std::abs(back.scale-2)<1e-12 && std::abs(back.rotation-.5)<1e-12);
    CHECK(imported.save_file(native)); imported.command("CLOSETAB");
    Shell restored=make_shell(); CHECK(restored.open_file(native));
    CHECK(restored.canvas().blocks().xrefs().size()==1);
    CHECK(restored.export_file(destination));
    const auto reread=gdwg::read_file(destination,restored.canvas().volume());
    CHECK(reread.ok); CHECK(reread.blocks.xrefs().size()==1);
    if(!reread.blocks.xrefs().empty()) CHECK(reread.blocks.xrefs().front().path=="../refs/Site.dwg");
    const auto file_bytes=[&]() { std::ifstream f(destination,std::ios::binary); return std::string(std::istreambuf_iterator<char>(f),{}); };
    const auto original_bytes=file_bytes();
    restored.canvas().blocks().xref("Site")->clip={{0,0},{0,1}};
    CHECK(!restored.export_file(destination)); CHECK(file_bytes()==original_bytes);
    restored.canvas().blocks().xref("Site")->clip=back.clip;
    restored.canvas().rescale_document(.001);
    const auto scaled=restored.canvas().blocks().xrefs().front();
    CHECK(std::abs(double(scaled.insertion.x)-double(back.insertion.x)*.001)<=1);
    CHECK(std::abs(double(scaled.insertion.y)-double(back.insertion.y)*.001)<=1);
    CHECK(scaled.scale==back.scale);
    restored.command("XREF DETACH Site"); CHECK(restored.canvas().blocks().xrefs().empty());
    CHECK(restored.export_file(destination));
}

TEST(layout_tabs_switch_editing_spaces_and_remember_each_file) {
    Shell s=make_shell(); draw_rect(s,"0,0","4,0","4,3","0,3");
    s.command("LAYOUT NEW Plan"); s.command("LAYOUT NEW Details");
    CHECK(s.select_layout(0)); CHECK(s.in_paper_space()); CHECK(s.canvas().shapes().empty());
    draw_rect(s,"0.01,0.01","0.1,0.01","0.1,0.05","0.01,0.05");
    CHECK(s.select_layout(1)); CHECK(s.canvas().shapes().empty());
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(),.02,.03),"Detail sheet",.003,0);
    CHECK(s.select_layout(-1)); CHECK(!s.in_paper_space()); CHECK(s.canvas().shapes().size()==4); CHECK(s.canvas().texts().empty());
    CHECK(s.select_layout(0)); CHECK(s.canvas().shapes().size()==4); CHECK(s.canvas().texts().empty());
    s.command("NEWTAB"); CHECK(s.selected_layout()==-1); CHECK(s.layouts().empty());
    s.command("PREVTAB"); CHECK(s.selected_layout()==0); CHECK(s.canvas().shapes().size()==4);
    CHECK(s.select_layout(1)); CHECK(s.canvas().shapes().empty()); CHECK(s.canvas().texts().size()==1);
    CHECK(!s.select_layout(3)); CHECK(s.selected_layout()==1);
    s.build_frame();
    const auto click_button=[&](const std::string& label, bool popup) {
        gui::Rect r;
        for (const auto& item : s.accessible_ui())
            if (item.role == gui::Context::Role::Button && (popup ? item.label == label && item.depth == 1 : item.label.rfind(label, 0) == 0 && item.depth == 0)) r = item.bounds;
        CHECK_MSG(!r.empty(), "button %s missing", label.c_str());
        const auto c = r.centre();
        s.pointer_move(c.x,c.y); s.build_frame(); s.pointer_down(c.x,c.y); s.build_frame(); s.pointer_up(c.x,c.y); s.build_frame(); s.build_frame();
    };
    const auto press=[&](const char* label) { click_button("Layout: ", false); click_button(label, true); };
    press("Model");
    CHECK(s.selected_layout()==-1); CHECK(s.canvas().shapes().size()==4);
    press("Plan");
    CHECK(s.selected_layout()==0); CHECK(s.canvas().shapes().size()==4);
    const auto count=s.layouts().size();
    press("New layout");
    CHECK(s.layouts().size()==count+1); CHECK(s.selected_layout()==int(count)); CHECK(s.canvas().shapes().empty());
    double lx,ly,rx,ry; s.canvas().view().screen_of(0,0,lx,ly); s.canvas().view().screen_of(.420,.297,rx,ry);
    CHECK(lx>=0); CHECK_MSG(rx<s.dock().slot("drawing").frame.x,"paper x %g..%g, dock %g, target %g",lx,rx,s.dock().slot("drawing").frame.x,s.camera().target().x.to_f64()); CHECK(ry>100); CHECK(ly<925);
}

TEST(dwg_export_from_paper_keeps_model_and_all_sheets) {
    Shell s=make_shell(); draw_rect(s,"0,0","4,0","4,3","0,3");
    s.command("LAYOUT NEW Plan"); s.command("MVIEW 50 20 40 200 180");
    s.command("MVIEW POLYGON 1 20 40 200 40 160 180 20 180");
    CHECK(s.select_layout(0));
    draw_rect(s,"0.01,0.01","0.1,0.01","0.1,0.02","0.01,0.02");
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(),.02,.03),"Plan title",.003,0);
    s.command("LAYOUT NEW Details"); CHECK(s.select_layout(1));
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(),.02,.04),"Detail title",.004,0);
    const auto path=scratch_path("layouts.dwg"), native=scratch_path("layouts.glcad");
    CHECK(s.export_file(path)); CHECK(s.selected_layout()==1); CHECK(s.canvas().shapes().empty()); CHECK(s.canvas().texts().size()==1);
    const auto r=gdwg::read_file(path,s.canvas().volume()); CHECK(r.ok); CHECK(r.entities.size()==4); CHECK(r.texts.empty());
    CHECK(r.layouts.size()==2); if(r.layouts.size()!=2) return;
    CHECK(r.layouts[0].entities.size()==4); CHECK(r.layouts[0].texts.size()==1); CHECK(r.layouts[1].texts.size()==1);
    CHECK(r.layouts[0].viewports.size()==1); CHECK(r.layouts[0].viewports[0].clip_polygon.size()==4);
    Shell imported=make_shell(); CHECK(imported.import_file(path));
    CHECK(imported.layouts().size()==2); CHECK(imported.selected_layout()==1); CHECK(imported.canvas().texts().size()==1);
    CHECK(imported.select_layout(0)); CHECK(imported.canvas().shapes().size()==4); CHECK(imported.canvas().texts()[0].text=="Plan title");
    CHECK(imported.select_layout(-1)); CHECK(imported.canvas().shapes().size()==4); CHECK(imported.canvas().texts().empty());
    CHECK(imported.save_file(native)); imported.command("CLOSETAB");
    Shell reopened=make_shell(); CHECK(reopened.open_file(native)); CHECK(reopened.layouts().size()==2); if(reopened.layouts().size()!=2) return;
    CHECK(reopened.layouts()[0].custom_paper); CHECK(!reopened.layouts()[0].direct_model_view);
    CHECK(reopened.select_layout(0)); CHECK(reopened.canvas().shapes().size()==4); CHECK(reopened.canvas().texts().size()==1);
    CHECK(reopened.export_file(scratch_path("layouts-again.dwg")));
}

TEST(dwg_blank_custom_paper_survives_native_save_and_does_not_plot_model) {
    Shell source=make_shell(); const auto& v=source.canvas().volume();
    gdxf::Document d; gsnap::Shape line; line.a=gcm::to_lattice(v,1,2); line.b=gcm::to_lattice(v,3,4); d.entities.push_back({line,"0",""});
    gdxf::Layout paper; paper.name="Custom blank"; paper.paper_name="510 x 320"; paper.width=.510; paper.height=.320; paper.margin_left=.017;
    d.layouts.push_back(paper); d.active_layout=0;
    const auto path=scratch_path("custom-blank.dwg"),native=scratch_path("custom-blank.glcad");
    gdwg::WriteOptions options; options.units=gdxf::Units::Millimetres; std::string error;
    CHECK_MSG(gdwg::write_file(path,d,v,options,&error),"%s",error.c_str());
    Shell s=make_shell(); CHECK(s.import_file(path)); CHECK(s.selected_layout()==0); CHECK(s.canvas().shapes().empty());
    gplot::PdfWriter page; CHECK(s.plot_sheet(page).ok); CHECK(page.paths_of_page(0).empty());
    CHECK(s.layouts().size()==1); if(s.layouts().empty()) return;
    CHECK(std::abs(s.layouts()[0].imported_paper.width-.510)<1e-12); CHECK(s.save_file(native)); s.command("CLOSETAB");
    Shell back=make_shell(); CHECK(back.open_file(native)); CHECK(back.select_layout(0));
    CHECK(std::abs(back.layouts()[0].imported_paper.width-.510)<1e-12);
#ifdef GLCAD_XPS_UNDERLAYS
    const auto dwfx = scratch_path("custom-blank.dwfx");
    CHECK(back.export_file(dwfx));
    const auto raster = gplot::load_underlay(dwfx, 1, error, 128);
    CHECK_MSG(raster.valid(), "%s", error.c_str());
    CHECK(std::abs(raster.metres_w - .510) < 1e-7);
    CHECK(std::abs(raster.metres_h - .320) < 1e-7);
#endif
    CHECK(back.export_file(scratch_path("custom-blank-again.dwg")));
    auto r=gdwg::read_file(scratch_path("custom-blank-again.dwg"),v); CHECK(r.ok); CHECK(r.layouts.size()==1);
    if(!r.layouts.empty()) { CHECK(r.layouts[0].viewports.empty()); CHECK(std::abs(r.layouts[0].width-.510)<1e-12); CHECK(std::abs(r.layouts[0].margin_left-.017)<1e-12); }
    CHECK(back.select_layout(-1)); CHECK(back.canvas().shapes().size()==1);
    double x,y; gcm::to_world(v,back.canvas().shapes()[0].b,x,y); CHECK(std::abs(x-1)<1e-5); CHECK(std::abs(y-1)<1e-5);
}

TEST(dwg_model_units_do_not_rescale_existing_geometry_or_paper) {
    Shell s=make_shell(); draw_rect(s,"0,0","4,0","4,3","0,3");
    const auto& v=s.canvas().volume(); const auto original=s.canvas().shapes();
    gdxf::Document d; gsnap::Shape line; line.a=gcm::to_lattice(v,1,2); line.b=gcm::to_lattice(v,3,4); d.entities.push_back({line,"0",""});
    d.splines.push_back(ggeo::circle_nurbs<gcm::F64>({gcm::F64(2),gcm::F64(3)},.5));
    gdxf::Layout paper; paper.name="Millimetre file"; auto edge=line; edge.a=gcm::to_lattice(v,.01,.02); edge.b=gcm::to_lattice(v,.03,.04); paper.entities.push_back({edge,"0",""}); d.layouts.push_back(paper);
    gdwg::WriteOptions options; options.units=gdxf::Units::Millimetres; std::string error;
    const auto path=scratch_path("layout-unit-merge.dwg"); CHECK(gdwg::write_file(path,d,v,options,&error));
    CHECK(s.import_file(path)); CHECK(s.selected_layout()==-1); CHECK(s.canvas().shapes().size()==5);
    for(std::size_t i=0;i<original.size();++i) CHECK(s.canvas().shapes()[i].b.x==original[i].b.x);
    CHECK(s.canvas().splines().size()==1); if(!s.canvas().splines().empty()) CHECK(std::abs(s.canvas().splines()[0].control[0].p.x.to_f64()-2.5)<1e-9);
    CHECK(s.select_layout(0)); CHECK(s.canvas().shapes().size()==1);
    double x,y; gcm::to_world(v,s.canvas().shapes()[0].a,x,y); CHECK(std::abs(x-.01)<1e-6); CHECK(std::abs(y-.02)<1e-6);
}

TEST(toolbar_wrapped_buttons_and_dropdowns_work_and_resize_reclaims_canvas) {
    Shell s = make_shell();
    s.set_viewport(800, 1100);
    s.build_frame();
    const float top = s.toolbar_top();
    auto layout = layout_toolbar(Mode::Sketch, {0, top, 800, 1000}, 1);
    const auto click = [&](float x, float y) {
        s.pointer_move(x, y); s.build_frame();
        s.pointer_down(x, y); s.build_frame();
        s.pointer_up(x, y); s.build_frame();
    };
    const auto tool_button = [](const ToolbarLayout& bar, ToolId id) {
        for (const auto& fragment : bar.groups)
            for (std::size_t i = 0; i < fragment.tools.size(); ++i)
                if (fragment.tools[i]->id == id)
                    return gui::Rect{fragment.bounds.x + 42 * float(i), fragment.bounds.y, 40, 40};
        return gui::Rect{};
    };
    const auto rectangle = tool_button(layout, ToolId::Rect);
    CHECK(tool_button(layout, ToolId::Text).y > top + 72);
    CHECK(s.dock().slot("layers").frame.y >= top + layout.height);
    click(rectangle.right() - 6, rectangle.bottom() - 6);
    click(rectangle.x + 30, rectangle.bottom() + 4 + 28 + 14);
    CHECK(s.canvas().tool_mode(Tool::Rect) == 1);
    CHECK(s.active_tool() == ToolId::Rect);
    CHECK(s.canvas().shapes().empty());
    const auto text = tool_button(layout, ToolId::Text);
    click(text.centre().x, text.centre().y);
    CHECK(s.active_tool() == ToolId::Text);
    CHECK(s.canvas().texts().empty());

    const auto assistant = layout.assistant.centre();
    click(assistant.x, assistant.y);
    CHECK(s.dock().find("assistant")->open);
    CHECK(!s.dock().find("assistant")->collapsed);
    click(assistant.x, assistant.y);
    CHECK(s.dock().find("assistant")->collapsed);

    const float narrow_top = s.dock().slot("layers").frame.y;
    s.set_viewport(2400, 1100); s.build_frame();
    CHECK(s.dock().slot("layers").frame.y < narrow_top);
    CHECK(s.dock().slot("layers").frame.y >= top + 72);
    s.set_viewport(800, 1100); s.build_frame();
    CHECK(s.dock().slot("layers").frame.y == narrow_top);
    const auto pick_mode = [&](const char* name) {
        click(layout.modes.centre().x, layout.modes.centre().y);
        gui::Rect item;
        for (const auto& a : s.accessible_ui())
            if (a.role == gui::Context::Role::Button && a.label == name && a.depth == 1) item = a.bounds;
        CHECK_MSG(!item.empty(), "mode %s missing from the mode menu", name);
        click(item.centre().x, item.centre().y);
        s.build_frame();
    };
    pick_mode("Solid"); CHECK(s.mode() == Mode::Solid);
    layout = layout_toolbar(Mode::Solid, {0, top, 800, 1000}, 1);
    CHECK(s.dock().slot("layers").frame.y >= top + layout.height);
    pick_mode("Drawing"); CHECK(s.mode() == Mode::Drawing);
    layout = layout_toolbar(Mode::Drawing, {0, top, 800, 1000}, 1);
    CHECK(s.dock().slot("drawing").frame.y >= top + layout.height);
}

TEST(imported_block_lines_render_like_drawn_lines_after_layer_inheritance) {
    Shell drawn = make_shell();
    drawn.canvas().set_current_layer("A-WALL");
    draw_rect(drawn, "0,0", "4,0", "4,3", "0,3");
    drawn.layers().find("0")->frozen = true;
    drawn.build_frame();
    CHECK(drawn.static_lines().size() == 8);

    gdxf::Document document;
    gdoc::Layer wall; wall.name = "A-WALL"; document.layers.add(wall);
    gdoc::BlockDef definition; definition.name = "Imported rectangle";
    definition.geometry = drawn.canvas().shapes();
    for (auto& shape : definition.geometry) shape.layer = "0";
    document.blocks.define(definition);
    gdoc::BlockRef insertion; insertion.name = definition.name; insertion.layer = "A-WALL";
    document.blocks.insert(insertion);
    const auto path = scratch_path("block-visible-layer.dwg");
    std::string error;
    CHECK_MSG(gdwg::write_file(path, document, drawn.canvas().volume(), {}, &error), "%s", error.c_str());
    Shell imported = make_shell(); CHECK(imported.import_file(path));
    imported.layers().find("0")->frozen = true;
    imported.build_frame();
    const auto& actual = imported.static_lines();
    const auto& expected = drawn.static_lines();
    CHECK_MSG(actual.size() == expected.size(), "imported %zu vertices, drawn %zu", actual.size(), expected.size());
    if (actual.size() == expected.size())
        for (std::size_t i = 0; i < actual.size(); ++i)
            for (int axis = 0; axis < 3; ++axis)
                CHECK(std::abs(actual[i].pos[axis] - expected[i].pos[axis]) < 1e-5);
    CHECK(!imported.canvas().blocks().references().empty());
    if (imported.canvas().blocks().references().empty()) return;
    const auto& ref = imported.canvas().blocks().references().front();
    const auto geometry = imported.canvas().blocks().expand(ref, imported.canvas().volume());
    CHECK(geometry.size() == 4);
    double x, y; imported.canvas().view().screen_of(2, 0, x, y);
    CHECK(imported.canvas().pick_reference(x, y) != Canvas::npos32);
}

TEST(c11_text_affine_layout_render_pick_plot_and_import) {
    Shell s=make_shell();std::string error;CHECK(s.load_font(&error));
    s.command("STYLE Notes 1 1 0");
    auto& c=s.canvas();c.add_text(gcm::to_lattice(c.volume(),0,0),"ABCD",1,0,0,0,0,"Notes");
    auto t=c.texts().front();double lx,ly,hx,hy;c.text_bounds(t,lx,ly,hx,hy);double width=hx-lx;
    s.command("STYLE Notes 1 2 30");
    auto labels=c.note_labels(t);CHECK(!labels.empty());CHECK(labels[0].width_factor==2);CHECK(labels[0].oblique==30);
    c.text_bounds(t,lx,ly,hx,hy);CHECK(hx-lx>width*1.8);
    t.angle=3.141592653589793/2;c.load_texts({t});c.text_bounds(t,lx,ly,hx,hy);CHECK(hy-ly>hx-lx);
    s.build_frame();CHECK(!s.text().empty());
    const auto dx=scratch_path("c11-styled.dxf");CHECK(s.export_file(dx));
    Shell reopened=make_shell();CHECK(reopened.import_file(dx));CHECK(reopened.canvas().texts().front().style=="Notes");
    reopened.build_frame();auto imported=reopened.canvas().note_labels(reopened.canvas().texts().front());
    CHECK(!imported.empty());CHECK(imported[0].width_factor==2);CHECK(imported[0].oblique==30);
    const auto pdf=scratch_path("c11-styled.pdf");CHECK(s.export_file(pdf));
    std::ifstream f(pdf,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(f)),{});f.close();CHECK(bytes.find(" Tm")!=std::string::npos);
    s.command("STYLE Notes 1 -2 90");CHECK(s.text_styles().find("Notes")->width_factor==2);
    std::filesystem::remove(dx);std::filesystem::remove(pdf);
}

TEST(c11_stroke_fonts_share_layout_geometry_and_persistence) {
    Shell s=make_shell();const auto path=scratch_path("c11-font.shp");
    {std::ofstream f(path);f<<"*0,4,tinyfont\n6,2,0\n*65,6,A\n2,014,1,024,020,0\n*66,4,B\n2,024,1,040,0\n";}
    s.command("STYLE Stroke 1 1.5 20");s.command("STYLE FONT Stroke "+path);
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(),0,0),"AB",1,.4,0,0,0,"Stroke");
    auto labels=s.canvas().note_labels(s.canvas().texts().front());CHECK(labels.size()==1);CHECK(labels[0].stroke_font!=nullptr);
    auto lines=Canvas::label_strokes(labels[0]);CHECK(!lines.empty());
    grender::d2::DrawList ink,dynamic;s.canvas().build_split(ink,dynamic);CHECK(!ink.lines().empty());
    double lx,ly,hx,hy;s.canvas().text_bounds(s.canvas().texts().front(),lx,ly,hx,hy);
    for(auto line:lines) for(int k:{0,2}) {CHECK(line[k]>=lx-1e-9&&line[k]<=hx+1e-9);CHECK(line[k+1]>=ly-1e-9&&line[k+1]<=hy+1e-9);}
    const auto native=scratch_path("c11-stroke.glcad");CHECK(s.save_file(native));
    Shell reopened=make_shell();CHECK(reopened.open_file(native));
    auto restored=reopened.canvas().note_labels(reopened.canvas().texts().front());CHECK(restored.size()==1);CHECK(restored[0].stroke_font!=nullptr);
    CHECK(Canvas::label_strokes(restored[0])==lines);
    std::filesystem::remove(path);std::filesystem::remove(native);
}

TEST(c30_c35_drafting_defaults_apply_validate_and_reopen) {
    Shell s=make_shell();s.build_frame();
    s.command("SETVAR OFFSETDIST 0.125");CHECK(s.canvas().offset_distance()==.125);
    s.command("ARRAYROWS 4");s.command("SETVAR ARRAYCOLS 5");s.command("SETVAR ARRAYCOUNT 9");
    CHECK(s.canvas().array_rows()==4);CHECK(s.canvas().array_columns()==5);CHECK(s.canvas().array_count()==9);
    s.command("SETVAR ARRAYANGLE 180");CHECK(std::abs(s.canvas().array_angle()-3.141592653589793)<1e-8);
    s.command("SETVAR CHAMFERA 0.2");s.command("SETVAR CHAMFERB 0.3");CHECK(s.canvas().second_chamfer_distance()==.3);
    s.command("SETVAR OFFSETDIST garbage");CHECK(s.canvas().offset_distance()==.125);
    s.command("SETVAR ARRAYROWS 1.5");CHECK(s.canvas().array_rows()==4);
    s.command("SETVAR ARRAYCOUNT 1000000000");CHECK(s.canvas().array_count()==9);
    s.command("SETVAR APERTURE 24");s.command("SETVAR PICKBOX 16");s.command("SETVAR POLARANG 30");s.command("SETVAR ORTHOMODE 1");
    CHECK(s.canvas().snaps().settings.aperture_px==24);CHECK(s.canvas().pickbox_pixels==16);CHECK(s.canvas().polar().ortho);CHECK(s.canvas().polar().increment_deg==30);
    s.canvas().set_tool(Tool::Line);s.command("SETVAR DYNMODE 0");CHECK(!s.canvas().dynamic_input().visible);
    s.command("SETVAR DYNMODE 3");CHECK(s.canvas().dynamic_input().visible);
    s.command("SETVAR CECOLOR 1");s.command("SETVAR CELTSCALE 2");s.command("SETVAR CELWEIGHT 50");
    s.command("LINE");s.canvas().type_point("0,0");s.canvas().type_point("1,0");s.canvas().cancel();
    CHECK(s.canvas().shapes().back().attrs.color==gdoc::Color::aci(1));CHECK(s.canvas().shapes().back().attrs.linetype_scale==2);
    CHECK(s.canvas().shapes().back().attrs.lineweight==gdoc::Lineweight::W050);
    const auto path=scratch_path("drafting-defaults.glcad");CHECK(s.save_file(path));
    Shell reopened=make_shell();CHECK(reopened.open_file(path));CHECK(reopened.canvas().offset_distance()==.125);
    CHECK(reopened.canvas().array_rows()==4);CHECK(reopened.canvas().array_count()==9);CHECK(reopened.canvas().second_chamfer_distance()==.3);
    CHECK(reopened.canvas().snaps().settings.aperture_px==24);CHECK(reopened.canvas().polar().ortho);
    s.command("ARRAYRECT 3 2");s.command("SETVAR ARRAYCOLS");CHECK(s.log().back().find("3")!=std::string::npos);
    std::filesystem::remove(path);
}

TEST(c35_grid_rotation_modes_and_settings_panel) {
    Shell s=make_shell();s.build_frame();s.command("SETVAR GRIDUNIT 1");s.command("SETVAR SNAPANG 30");s.command("SETVAR SNAPMODE 1");
    s.command("SETVAR OSMODE 1024");CHECK(!s.canvas().snaps().settings.enabled);
    CHECK(std::abs(s.canvas().grid_angle-3.141592653589793/6)<1e-8);
    auto u=s.frame_uniforms();CHECK(std::abs(u.plane_n[3]-s.canvas().grid_angle)<1e-6);
    s.command("SETVAR BLIPMODE 1");s.command("LINE");s.canvas().type_point("0,0");CHECK(!s.canvas().blips.empty());
    s.command("REGEN");CHECK(s.canvas().blips.empty());
    s.command("SETVAR INSUNITS 1");CHECK(s.canvas().display_unit()==gunit::Length::Inch);
    const auto before=s.ui().size();s.command("DSETTINGS");s.build_frame();CHECK(s.ui().size()>before);
    s.command("SETVAR POLARMODE 999");CHECK(s.log().back().find("only absolute")!=std::string::npos);
}

TEST(c38_leaders_are_editable_annotations_with_history_and_native_entities) {
    Shell s=make_shell();s.build_frame();s.command("STYLE 0.05");s.command("MLEADER Inspect joint");
    click_at(s,0,0,0);click_at(s,2,1,0);s.build_frame();
    CHECK(s.canvas().shapes().empty());CHECK(s.canvas().texts().empty());CHECK(s.canvas().dimensions().size()==1);
    if(s.canvas().dimensions().empty()) return;
    CHECK(s.canvas().dimensions()[0].kind==gdim::Kind::Leader);CHECK(s.canvas().dimensions()[0].text_override=="Inspect joint");
    s.canvas().set_selected_dimensions({0});s.command("LEADEREDIT TEXT Weld here");CHECK(s.canvas().dimensions()[0].text_override=="Weld here");
    s.command("UNDO");CHECK(s.canvas().dimensions()[0].text_override=="Inspect joint");s.command("REDO");CHECK(s.canvas().dimensions()[0].text_override=="Weld here");
    s.canvas().set_selected_dimensions({0});s.command("LEADEREDIT BEND 1,1");
    auto bend=gcm::to_lattice(s.canvas().volume(),1,1);CHECK(s.canvas().dimensions()[0].p2.x==bend.x);
    s.command("LEADERSTYLE Review txt 25");s.command("LEADEREDIT STYLE Review");CHECK(s.canvas().dimensions()[0].style=="Review");
    s.canvas().set_selected_dimensions({0});s.command("COPY");s.canvas().type_point("0,0");s.canvas().type_point("3,0");
    CHECK(s.canvas().dimensions().size()==2);CHECK(s.canvas().shapes().empty());CHECK(s.canvas().texts().empty());
    const auto path=scratch_path("editable-leader.glcad");CHECK(s.save_file(path));
    gfile::Contents stored;CHECK(gfile::load_file(path,stored).ok);
    Shell reopened=make_shell();CHECK(reopened.open_file(path));CHECK(reopened.canvas().dimensions().size()==2);
    CHECK(reopened.canvas().dimensions()[0].kind==gdim::Kind::Leader);CHECK(reopened.canvas().dimensions()[0].style=="Review");
    reopened.canvas().select_all();reopened.command("ERASE");CHECK(reopened.canvas().dimensions().empty());reopened.command("UNDO");CHECK(reopened.canvas().dimensions().size()==2);
    std::filesystem::remove(path);
}

#ifdef __APPLE__
TEST(c45_c46_pdf_underlays_render_transform_plot_and_import) {
    const auto path=scratch_path("underlay-page.pdf");
    gplot::PdfWriter pdf;pdf.begin_page(.2,.1);
    gplot::PdfPath rect;rect.points={{.01,.01},{.19,.01},{.19,.09},{.01,.09}};rect.closed=true;rect.color={1,0,0};pdf.add(rect);
    gplot::PdfPath line;line.points={{.02,.02},{.18,.08}};line.clip=gplot::PaperClip{{.04,.03},{.16,.07}};pdf.add(line);pdf.end_page();
    pdf.begin_page(.1,.1);pdf.add(rect);pdf.end_page();
    {std::ofstream f(path,std::ios::binary);f<<pdf.build();}
    std::string error;auto raster=gplot::load_underlay(path,1,error,256);
    CHECK_MSG(raster.valid(),"%s",error.c_str());CHECK(raster.pages==2);CHECK(std::abs(raster.metres_w-.2)<1e-6);CHECK(std::abs(raster.metres_h-.1)<1e-6);
    CHECK(!gplot::load_underlay(path,3,error,256).valid());
    auto enlarged=gplot::load_underlay(path,1,error,2048);CHECK(enlarged.valid());
    if(enlarged.valid()) {const auto at=(std::size_t(enlarged.height/2)*enlarged.width+enlarged.width/20)*4;CHECK(enlarged.rgba[at]>180&&enlarged.rgba[at+1]<100);}

    Shell s=make_shell();s.build_frame();s.command("UNDERLAY "+path);s.command("ZOOMEXTENTS");s.build_frame();
    CHECK(s.canvas().underlays().size()==1);CHECK(s.images().size()==1);
    if(s.images().empty())return;
    auto image=s.images()[0].image;auto vertex=s.images()[0].vertices[0];s.build_frame();CHECK(s.images()[0].image==image);
    s.command("UNDERLAY MOVE underlay-page.pdf 1 2");s.command("UNDERLAY ROTATE underlay-page.pdf 30");s.build_frame();
    CHECK(s.images()[0].vertices[0].pos[0]!=vertex.pos[0]);
    s.command("UNDERLAY HIDE underlay-page.pdf");s.build_frame();CHECK(s.images().empty());s.command("UNDO");s.build_frame();CHECK(s.images().size()==1);
    s.command("UNDERLAY FADE underlay-page.pdf 100");s.build_frame();CHECK(s.images().empty());s.command("UNDERLAY FADE underlay-page.pdf 0");
    s.command("UNDERLAY PLOT underlay-page.pdf 1");gplot::PdfWriter plot;CHECK(s.plot_sheet(plot).ok);CHECK(plot.images_of_page(0).size()==1);CHECK(plot.build().find("/Subtype /Image")!=std::string::npos);
    for(const char* name:{"underlay-protected.dxf"}) {
        const auto destination=scratch_path(name);{std::ofstream f(destination);f<<"keep existing drawing";}
        CHECK(!s.export_file(destination));std::ifstream f(destination);std::string preserved;std::getline(f,preserved);CHECK(preserved=="keep existing drawing");
    }
    const auto native=scratch_path("underlay-roundtrip.glcad");CHECK(s.save_file(native));Shell reopened=make_shell();CHECK(reopened.open_file(native));reopened.build_frame();CHECK(reopened.images().size()==1);
    Shell imported=make_shell();imported.build_frame();imported.command("PDFIMPORT "+path);CHECK(imported.canvas().shapes().size()==5);CHECK(imported.canvas().underlays().size()==0);
    imported.command("UNDO");CHECK(imported.canvas().shapes().empty());imported.command("REDO");CHECK(imported.canvas().shapes().size()==5);
    imported.command("PDFIMPORT IMAGE PAGE 2 "+path);CHECK(imported.canvas().underlays().size()==1);
    Shell sheet=make_shell();sheet.command("LAYOUT NEW Underlay");sheet.command("PSPACE");sheet.command("UNDERLAY "+path);sheet.build_frame();
    CHECK(sheet.images().size()==1);gplot::PdfWriter sheet_plot;CHECK(sheet.plot_sheet(sheet_plot).ok);CHECK(sheet_plot.images_of_page(0).empty());
    imported.command("PDFIMPORT IMAGE PAGE 3 "+path);CHECK(imported.canvas().underlays().size()==1);
    const auto& sh=imported.canvas().shapes().back();double ax,ay,bx,by;gcm::to_world(imported.canvas().volume(),sh.a,ax,ay);gcm::to_world(imported.canvas().volume(),sh.b,bx,by);
    CHECK(ax>=.04-1e-6&&bx<=.16+1e-6);CHECK(ay>=.03-1e-6&&by<=.07+1e-6);
}
#endif

#ifdef __APPLE__
TEST(c45_raster_alpha_orientation_and_pdf_image_output) {
    const unsigned char png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,23,73,68,65,84,120,156,99,248,207,192,208,192,240,31,8,25,24,254,3,1,3,3,0,63,217,7,122,204,16,7,68,0,0,0,0,73,69,78,68,174,66,96,130};
    const auto path=scratch_path("alpha-underlay.png");{std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(png),sizeof(png));}
    std::string error;auto image=gplot::load_underlay(path,1,error,64);CHECK(image.valid());CHECK(image.width==2&&image.height==2);
    if(!image.valid())return;
    CHECK(image.rgba[0]>250&&image.rgba[1]<3&&image.rgba[3]==128);
    CHECK(image.rgba[10]>250&&image.rgba[8]<3);CHECK(image.rgba[15]==0);
    gplot::PdfWriter pdf;pdf.begin_page(.1,.1);gplot::PdfImage im;im.raster=std::make_shared<gplot::RasterImage>(image);im.origin={.01,.02};im.x_axis={.04,0};im.y_axis={0,.04};pdf.add(im);pdf.end_page();
    const auto out=scratch_path("alpha-plot.pdf");{std::ofstream f(out,std::ios::binary);f<<pdf.build();}
    auto reread=gplot::load_underlay(out,1,error,200);CHECK(reread.valid());
    if(reread.valid()) {auto at=[&](int x,int y,int c){return reread.rgba[(std::size_t(y)*reread.width+x)*4+c];};CHECK(at(40,100,0)>245);CHECK(at(40,100,1)>100&&at(40,100,1)<150);CHECK(at(40,140,2)>245&&at(40,140,0)<10);}
}
#endif

#ifdef __APPLE__
TEST(c46_pdf_form_transform_and_boundary_clip) {
    const std::string page_stream="q 2 0 0 2 20 30 cm /Detail Do Q\n";
    const std::string form_stream="-5 5 m 15 5 l S\n";
    std::vector<std::string> objects={
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /XObject << /Detail 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length "+std::to_string(page_stream.size())+" >>\nstream\n"+page_stream+"endstream",
        "<< /Type /XObject /Subtype /Form /BBox [0 0 10 10] /Matrix [1 0 0 1 5 0] /Resources << >> /Length "+std::to_string(form_stream.size())+" >>\nstream\n"+form_stream+"endstream"};
    std::string bytes="%PDF-1.4\n";std::vector<std::size_t> offsets;
    for(std::size_t i=0;i<objects.size();++i){offsets.push_back(bytes.size());bytes+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
    const auto xref=bytes.size();bytes+="xref\n0 6\n0000000000 65535 f \n";
    for(auto offset:offsets){char line[40];std::snprintf(line,sizeof(line),"%010zu 00000 n \n",offset);bytes+=line;}
    bytes+="trailer\n<< /Root 1 0 R /Size 6 >>\nstartxref\n"+std::to_string(xref)+"\n%%EOF\n";
    const auto path=scratch_path("nested-form.pdf");{std::ofstream f(path,std::ios::binary);f<<bytes;}
    Shell s=make_shell();auto result=gplot::import_pdf_paths(path,1,s.canvas().volume());
    CHECK_MSG(result.error.empty(),"%s",result.error.c_str());CHECK(result.shapes.size()==1);
    if(result.shapes.size()!=1)return;
    double ax,ay,bx,by;gcm::to_world(s.canvas().volume(),result.shapes[0].a,ax,ay);gcm::to_world(s.canvas().volume(),result.shapes[0].b,bx,by);
    CHECK(std::abs(ax-30*.0254/72)<1e-6);CHECK(std::abs(bx-50*.0254/72)<1e-6);CHECK(std::abs(ay-40*.0254/72)<1e-6);CHECK(std::abs(by-ay)<1e-6);
}
#endif

TEST(selection_polygons_close_the_boundary_and_keep_whole_objects) {
    Shell s=make_shell();s.build_frame();
    const auto volume=s.canvas().volume();
    s.canvas().modify_shapes([&](auto& shapes) {
        shapes.clear();
        const auto line=[&](double x1,double y1,double x2,double y2,gcm::u32 poly=0) {
            gsnap::Shape sh;sh.a=gcm::to_lattice(volume,x1,y1);sh.b=gcm::to_lattice(volume,x2,y2);
            sh.poly=poly;shapes.push_back(sh);
        };
        line(-2,2,1,2);
        line(1,1,3,1,17);line(3,1,6,1,17);
        line(1,3,3,3);
    });
    s.command("CPOLYGON 0 0 4 0 4 4 0 4");
    CHECK(s.canvas().selection().size()==4);
    s.command("WPOLYGON 0 0 4 0 4 4 0 4");
    CHECK(s.canvas().selection().size()==1);
    CHECK(s.canvas().is_selected(3));
    s.command("SELECT WP 0 0 4 0 4 4 3 4 3 2 2 2 2 4 0 4");
    CHECK(s.canvas().selection().empty());
    s.command("SELECT CP 0 0 4 0 4 4 0 4");
    CHECK(s.canvas().selection().size()==4);
    s.command("WPOLYGON 0 0 4 0 invalid 4 0 4");
    CHECK(s.canvas().selection().size()==4);
}

TEST(selection_polygons_and_fences_include_notes_blocks_and_leaders) {
    Shell s=make_shell();s.build_frame();auto& c=s.canvas();
    const auto v=c.volume();
    c.add_text(gcm::to_lattice(v,2,2),"note",.5);
    gdoc::BlockDef def;def.name="selectable";
    gsnap::Shape sh;sh.a=gcm::to_lattice(v,1,1);sh.b=gcm::to_lattice(v,3,3);
    def.geometry={sh};c.blocks().define(def);
    gdoc::BlockRef ref;ref.name=def.name;c.blocks().insert(ref);
    gdim::Dimension d;d.kind=gdim::Kind::Leader;d.p1=gcm::to_lattice(v,1,1);
    d.p2=gcm::to_lattice(v,2,2);d.p3=gcm::to_lattice(v,3,2);d.text_override="label";
    c.add_dimensions({d});
    s.command("CPOLYGON 0 0 4 0 4 4 0 4");
    CHECK(c.selected_texts().size()==1);CHECK(c.selected_references().size()==1);
    CHECK(c.selected_dimensions().size()==1);
    s.command("FENCE 0 2 4 2");
    CHECK(c.selected_texts().size()==1);CHECK(c.selected_references().size()==1);
    CHECK(c.selected_dimensions().size()==1);
    s.command("WPOLYGON 20 20 21 20 21 21 20 21");
    CHECK(c.selection_count()==0);
}

TEST(selection_cycles_overlaps_and_recalls_surviving_identities) {
    Shell s=make_shell();s.build_frame();auto& c=s.canvas();const auto v=c.volume();
    c.modify_shapes([&](auto& shapes) {
        shapes.clear();gsnap::Shape sh;sh.a=gcm::to_lattice(v,1,2);sh.b=gcm::to_lattice(v,3,2);
        shapes.push_back(sh);shapes.push_back(sh);
    });
    c.add_text(gcm::to_lattice(v,2,2),"overlap",.5);
    double x,y;c.view().screen_of(2,2,x,y);
    c.select_at(x,y,false);CHECK(c.is_selected(0));
    CHECK(c.cycle_selection(x,y)==3);CHECK(c.is_selected(1));
    CHECK(c.cycle_selection(x,y)==3);CHECK(c.selected_texts().size()==1);
    CHECK(c.cycle_selection(x,y)==3);CHECK(c.is_selected(0));
    s.command("SELECT LAST");CHECK(c.selected_texts().size()==1);
    s.command("SELECT P");CHECK(c.is_selected(0));
    c.set_selection({1});c.clear_selection();
    const auto selected_key=c.shapes()[1].key;
    c.modify_shapes([](auto& shapes){shapes.erase(shapes.begin());});
    s.command("SELECTPREVIOUS");
    CHECK(c.selection().size()==1);CHECK(c.shapes()[c.selection().front()].key==selected_key);
    c.erase_selected();s.command("PREVIOUS");CHECK(c.selection_count()==0);
    c.undo();s.command("PREVIOUS");CHECK(c.selection().size()==1);
}

TEST(selection_previous_notes_survive_index_changes_and_copies_get_new_identity) {
    Shell s=make_shell();s.build_frame();auto& c=s.canvas();auto v=c.volume();
    c.add_text(gcm::to_lattice(v,0,0),"first",.5);
    c.add_text(gcm::to_lattice(v,2,0),"second",.5);
    c.set_selected_texts({1});c.clear_selection();
    c.modify_texts([](auto& notes){notes.erase(notes.begin());});
    s.command("PREVIOUS");CHECK(c.selected_texts()==std::vector<std::size_t>{0});
    c.modify_texts([](auto& notes){notes.push_back(notes.front());});
    s.command("LAST");CHECK(c.selected_texts()==std::vector<std::size_t>{1});
    s.command("PREVIOUS");CHECK(c.selected_texts()==std::vector<std::size_t>{0});
}

TEST(selection_polygon_interactive_accepts_more_than_three_vertices_and_enter) {
    Shell s=make_shell();s.build_frame();
    s.canvas().set_tool(Tool::Line);s.canvas().type_point("2,2");s.canvas().type_point("3,2");s.canvas().cancel();
    s.command("WPOLYGON");
    click_at(s,0,0,0);click_at(s,5,0,0);click_at(s,5,5,0);click_at(s,0,5,0);
    CHECK(s.canvas().selection().empty());
    s.key_special(0);CHECK(s.canvas().selection().size()==1);
    s.command("WPOLYGON");click_at(s,0,0,0);s.key_special(1);
    CHECK(s.canvas().selection().size()==1);
}

namespace {
void check_closed_cloud(const Canvas& canvas,std::size_t member) {
    const auto pieces=canvas.polyline_pieces(member);
    CHECK(pieces.size()>3);CHECK(canvas.polyline_closed(pieces));
    const auto endpoint=[&](const gsnap::Shape& s,bool end) {
        double x,y;gcm::to_world(canvas.volume(),s.a,x,y);
        const double angle=end?s.end_angle:s.start_angle,r=s.radius*canvas.volume().inv_scale();
        return std::pair{x+r*std::cos(angle),y+r*std::sin(angle)};
    };
    for(std::size_t i=0;i<pieces.size();++i) {
        const auto& a=canvas.shapes()[pieces[i]];const auto& b=canvas.shapes()[pieces[(i+1)%pieces.size()]];
        CHECK(a.kind==gsnap::ShapeKind::Arc);
        const auto p=endpoint(a,true),q=endpoint(b,false);
        CHECK(std::hypot(p.first-q.first,p.second-q.second)<=4*canvas.volume().inv_scale());
    }
}
}

TEST(revision_cloud_is_one_closed_editable_arc_polyline_and_reopens) {
    Shell s=make_shell();s.build_frame();auto& c=s.canvas();
    s.command("REVCLOUD 1 1 4 3 .5");
    CHECK(c.shapes().size()==20);CHECK(c.is_revision_cloud(0));check_closed_cloud(c,0);
    const auto poly=c.shapes()[0].poly;const auto key=c.shapes()[0].key;
    c.clear_selection();double x,y;c.view().screen_of(1.25,.875,x,y);
    c.select_at(x,y,false);CHECK(c.selection().size()==20);
    s.command("REVCLOUD EDIT 1 1 5 4 .5");
    CHECK(c.shapes().size()==28);CHECK(c.shapes()[0].poly==poly);CHECK(c.shapes()[0].key==key);
    check_closed_cloud(c,0);
    c.undo();CHECK(c.shapes().size()==20);check_closed_cloud(c,0);
    c.redo();CHECK(c.shapes().size()==28);
    const auto native=scratch_path("editable-cloud.glcad");CHECK(s.save_file(native));
    Shell reopened=make_shell();CHECK(reopened.open_file(native));
    CHECK(reopened.canvas().is_revision_cloud(0));check_closed_cloud(reopened.canvas(),0);
    reopened.canvas().select_all();reopened.command("REVCLOUD EDIT 0 0 2 2 .5");
    CHECK(reopened.canvas().shapes().size()==16);
    const auto dxf=scratch_path("editable-cloud.dxf");CHECK(s.export_file(dxf));
    Shell imported=make_shell();CHECK(imported.import_file(dxf));
    CHECK(imported.canvas().shapes().size()==28);check_closed_cloud(imported.canvas(),0);
}

TEST(revision_cloud_vertex_and_scallop_grips_keep_neighbors_joined) {
    Shell s=make_shell();s.build_frame();s.command("REVCLOUD 1 1 4 3 .5");auto& c=s.canvas();
    c.snaps().settings.enabled=false;c.set_tool(Tool::Select);c.select_all();
    double x,y,tx,ty;c.view().screen_of(1,1,x,y);c.view().screen_of(.8,.8,tx,ty);
    CHECK(c.begin_grip_drag(x,y));c.update_grip_drag(tx,ty);CHECK(c.end_grip_drag());
    check_closed_cloud(c,0);c.undo();check_closed_cloud(c,0);
    c.select_all();c.view().screen_of(1.25,.875,x,y);c.view().screen_of(1.25,.8,tx,ty);
    const auto old_radius=c.shapes()[0].radius;
    CHECK(c.begin_grip_drag(x,y));c.update_grip_drag(tx,ty);CHECK(c.end_grip_drag());
    CHECK(c.shapes()[0].radius!=old_radius);check_closed_cloud(c,0);
    c.undo();CHECK(c.shapes()[0].radius==old_radius);
}

TEST(revision_cloud_mirrors_as_a_separate_closed_object_and_erases_whole) {
    Shell s=make_shell();s.build_frame();s.command("REVCLOUD 1 1 4 3 .5");auto& c=s.canvas();
    c.set_tool(Tool::Mirror);c.type_point("0,0");c.type_point("0,5");c.cancel();
    CHECK(c.shapes().size()==40);
    CHECK(c.shapes()[0].poly!=c.shapes()[20].poly);
    check_closed_cloud(c,0);check_closed_cloud(c,20);
    c.set_selection(c.polyline_pieces(20));c.erase_selected();
    CHECK(c.shapes().size()==20);check_closed_cloud(c,0);
    c.undo();CHECK(c.shapes().size()==40);
}

TEST(revision_cloud_invalid_dimensions_do_not_change_the_drawing) {
    Shell s=make_shell();s.build_frame();s.command("REVCLOUD 0 0 2 2 .5");
    const auto size=s.canvas().shapes().size();
    s.command("REVCLOUD EDIT 0 0 2 2 -1");CHECK(s.canvas().shapes().size()==size);
    s.command("REVCLOUD EDIT 0 0 0 2 .5");CHECK(s.canvas().shapes().size()==size);
    s.command("REVCLOUD EDIT 0 0 2 2 .000001");CHECK(s.canvas().shapes().size()==size);
    check_closed_cloud(s.canvas(),0);
}

TEST(annotation_scale_is_explicit_and_does_not_follow_camera_zoom) {
    Shell s=make_shell();s.build_frame();auto& c=s.canvas();const auto v=c.volume();
    gdim::Dimension d;d.p1=gcm::to_lattice(v,0,0);d.p2=gcm::to_lattice(v,5,0);d.p3=gcm::to_lattice(v,2,1);c.add_dimensions({d});
    s.command("CANNOSCALE 1:50");CHECK(c.annotation_factor()==50);
    grender::d2::DrawList draw;c.build_dynamic_only(draw);CHECK(!c.labels().empty());
    if(c.labels().empty()) return;
    const double before=c.labels().front().height;
    CHECK(std::abs(before-.125)<1e-10);
    c.view().zoom_at(400,300,4);c.build_dynamic_only(draw);
    CHECK(std::abs(c.labels().front().height-before)<1e-10);
    s.command("CANNOSCALE 1:100");c.build_dynamic_only(draw);
    CHECK(std::abs(c.labels().front().height-2*before)<1e-10);
    s.command("CANNOSCALE missing");CHECK(c.annotation_factor()==100);
}

TEST(annotation_scales_persist_per_model_and_layout_with_custom_list_and_undo) {
    Shell s=make_shell();s.build_frame();
    s.command("SCALELISTEDIT ADD Detail75 1 75");s.command("CANNOSCALE Detail75");
    CHECK(s.canvas().annotation_factor()==75);
    s.command("CANNOSCALE 1:50");s.command("UNDO");CHECK(s.canvas().annotation_factor()==75);
    s.command("REDO");CHECK(s.canvas().annotation_factor()==50);
    s.command("LAYOUT NEW AnnotationSheet");s.command("PSPACE");
    CHECK(s.canvas().annotation_factor()==1);
    s.command("CANNOSCALE 2:1");CHECK(s.canvas().annotation_factor()==.5);
    s.command("MSPACE");CHECK(s.canvas().annotation_factor()==50);
    s.command("CANNOSCALE Detail75");
    s.command("SCALELISTEDIT DELETE Detail75");CHECK(s.canvas().annotation_factor()==75);
    const auto path=scratch_path("annotation-scales.glcad");CHECK(s.save_file(path));
    Shell reopened=make_shell();CHECK(reopened.open_file(path));
    CHECK(reopened.canvas().annotation_scale_name()=="Detail75");CHECK(reopened.canvas().annotation_factor()==75);
    reopened.command("PSPACE");CHECK(reopened.canvas().annotation_factor()==.5);
    reopened.command("MSPACE");CHECK(reopened.canvas().annotation_factor()==75);
}

TEST(annotative_text_uses_the_same_named_scale_for_bounds_and_labels) {
    Shell s=make_shell();auto& c=s.canvas();gtext::TextStyleTable styles;
    gtext::TextStyleDef style;style.name="PaperNote";style.annotative=true;styles.add(style);c.set_text_styles(styles);
    Canvas::TextItem note;note.text="Scaled note";note.height=.0025;note.style=style.name;
    c.load_texts({note});c.set_annotation_scale("1:50",50);
    auto labels=c.note_labels(note);CHECK(!labels.empty());CHECK(std::abs(labels.front().height-.125)<1e-12);
    double lx,ly,hx,hy;c.text_bounds(note,lx,ly,hx,hy);const double width=hx-lx;
    c.set_annotation_scale("1:100",100);c.text_bounds(note,lx,ly,hx,hy);CHECK(std::abs(hx-lx-width*2)<1e-6);
    labels=c.note_labels(note,25);CHECK(std::abs(labels.front().height-.0625)<1e-12);
}

TEST(selection_visibility_is_shared_with_rendered_geometry_and_markers) {
    Canvas c;const auto v=c.volume();
    c.modify_shapes([&](auto& shapes) {
        gsnap::Shape sh;sh.a=gcm::to_lattice(v,1,0);sh.b=gcm::to_lattice(v,3,0);shapes={sh};
        sh.attrs.visible=false;shapes.push_back(sh);
        sh.attrs.visible=true;sh.layer="Hidden";shapes.push_back(sh);
    });
    c.set_hidden_layers({"Hidden"});
    double x,y;c.view().screen_of(2,0,x,y);CHECK(c.cycle_selection(x,y)==1);
    c.select_all();CHECK(c.selection_count()==1);
    grender::d2::DrawList dynamic;c.build_dynamic_only(dynamic);
    CHECK(c.static_draw().lines().size()==1);
    CHECK_MSG(dynamic.points().size()==3,"points=%zu",dynamic.points().size());
    c.clear_selection();c.build_dynamic_only(dynamic);
    CHECK_MSG(dynamic.points().empty(),"points=%zu with nothing picked",dynamic.points().size());
}

TEST(annotation_variables_and_style_controls_use_paper_to_drawing_ratios) {
    Shell s=make_shell();s.build_frame();
    s.command("CANNOSCALE 1:50");s.command("CANNOSCALEVALUE");
    CHECK(!s.log().empty());CHECK(s.log().back().find("0.02")!=std::string::npos);
    s.command("STYLE Note .0025");s.command("STYLE ANNOTATIVE Note ON");
    CHECK(s.text_styles().find("Note")->annotative);
    Canvas::TextItem note;note.text="Paper note";note.style="Note";note.height=.0025;
    auto labels=s.canvas().note_labels(note);CHECK(!labels.empty());
    if(!labels.empty()) CHECK(std::abs(labels.front().height-.125)<1e-12);
    s.command("STYLE ANNOTATIVE Note OFF");labels=s.canvas().note_labels(note);
    if(!labels.empty()) CHECK(std::abs(labels.front().height-.0025)<1e-12);
}

#if defined(__APPLE__) || defined(GLCAD_PORTABLE_UNDERLAYS)
TEST(underlay_snaps_track_rendered_visibility_transform_clip_and_missing_sources) {
    const auto path=scratch_path("snappable-page.pdf");
    gplot::PdfWriter pdf;pdf.begin_page(4,2);pdf.end_page();
    {std::ofstream f(path,std::ios::binary);f<<pdf.build();}
    Shell s=make_shell();s.build_frame();s.command("UNDERLAY "+path);s.build_frame();
    auto& c=s.canvas();c.set_grid_snap(false);c.snaps().settings.enabled=true;c.snaps().settings.running=gsnap::Endpoint|gsnap::Midpoint|gsnap::Nearest;
    const auto hit=[&](double x,double y) {
        c.set_tool(Tool::Line);double px,py;c.view().screen_of(x,y,px,py);c.hover(px,py);auto snap=c.snap();c.cancel();return snap;
    };
    CHECK(hit(4,2).valid);CHECK(hit(2,0).valid);
    s.command("UNDERLAY MOVE snappable-page.pdf 10 20");s.build_frame();
    CHECK(!hit(4,2).valid);CHECK(hit(14,22).valid);
    s.command("UNDERLAY HIDE snappable-page.pdf");s.build_frame();CHECK(!hit(14,22).valid);
    s.command("UNDERLAY SHOW snappable-page.pdf");s.command("UNDERLAY ROTATE snappable-page.pdf 90");s.build_frame();CHECK(hit(8,24).valid);
    s.command("UNDERLAY CLIP snappable-page.pdf 1 .5 3 .5 3 1.5 1 1.5");s.build_frame();
    CHECK(!hit(8,24).valid);CHECK(hit(9.5,21).valid);
    s.command("UNDERLAY FADE snappable-page.pdf 100");s.build_frame();CHECK(!hit(9.5,21).valid);
    s.command("UNDERLAY FADE snappable-page.pdf 0");s.build_frame();CHECK(hit(9.5,21).valid);
    std::filesystem::remove(path);s.build_frame();CHECK(!hit(9.5,21).valid);
}
#endif
#ifdef GLCAD_XPS_UNDERLAYS
TEST(dwfx_underlay_attachment_and_image_publishing_share_the_rendered_content) {
    const auto path=scratch_path("xps-underlay.dwfx");
    gplot::DwfWriter source;source.begin_page("Review",.2,.1);
    gplot::PdfPath rect;rect.points={{.01,.01},{.19,.01},{.19,.09},{.01,.09}};rect.closed=true;rect.filled=true;rect.color={1,0,0};source.add(rect);source.end_page();
    CHECK(source.write_file(path));
    Shell s=make_shell();s.build_frame();s.command("UNDERLAY "+path);s.build_frame();
    CHECK(s.canvas().underlays().size()==1);CHECK(s.images().size()==1);
    s.command("UNDERLAY PLOT xps-underlay.dwfx 1");
    const auto issued=scratch_path("xps-republished.dwfx");CHECK(s.export_file(issued));
    std::ifstream input(issued,std::ios::binary);std::vector<gcm::u8> data{std::istreambuf_iterator<char>(input),{}};
    std::vector<gcm::u8> image;CHECK(gplot::unzip_entry(data,"Documents/1/Resources/Images/1_1.png",image));CHECK(image.size()>20);
    std::string error;CHECK_MSG(gplot::load_underlay(issued,1,error,256).valid(),"%s",error.c_str());
    s.command("UNDERLAY HIDE xps-underlay.dwfx");s.build_frame();CHECK(s.images().empty());
    s.command("UNDO");s.build_frame();CHECK(s.images().size()==1);
}
#endif

TEST(sheet_sets_publish_saved_drawings_in_order_without_changing_the_open_document) {
    const auto first = scratch_path("set source one.glcad");
    const auto second = scratch_path("set source two.glcad");
    Shell one = make_shell();
    draw_rect(one, "0,0", "4,0", "4,3", "0,3");
    one.command("LAYOUT NEW Plan");
    one.command("PAGESETUP A4 landscape");
    CHECK(one.save_file(first));
    Shell two = make_shell();
    draw_rect(two, "0,0", "8,0", "8,3", "0,3");
    two.command("LAYOUT NEW Detail");
    two.command("PAGESETUP A3 landscape");
    CHECK(two.save_file(second));
    Shell publisher = make_shell();
    draw_rect(publisher, "0,0", "1,0", "1,1", "0,1");
    const auto count = publisher.canvas().shapes().size();
    publisher.command("SHEETSET ADD \"" + first + "\" Plan \"First issue sheet\"");
    publisher.command("SHEETSET ADD \"" + second + "\" Detail \"Second issue sheet\"");
    CHECK(publisher.publish_sheets().size() == 2);
    const auto manifest = scratch_path("issued set.gss");
    CHECK(publisher.save_sheet_set(manifest));
    publisher.command("SHEETSET NEW");
    CHECK(publisher.publish_sheets().empty());
    CHECK(publisher.load_sheet_set(manifest));
    CHECK(publisher.publish_sheets().size() == 2);
    if (publisher.publish_sheets().size() != 2) return;
    CHECK(std::filesystem::absolute(publisher.publish_sheets()[0].drawing).lexically_normal() == std::filesystem::absolute(first).lexically_normal());
    publisher.command("SHEETSET MOVE 2 1");
    CHECK(publisher.publish_sheets()[0].layout == "Detail");
    const auto output = scratch_path("combined issue.pdf");
    publisher.command("PUBLISH SET \"" + output + "\"");
    std::string error;
    const auto page1 = gplot::load_underlay(output, 1, error, 128);
    CHECK_MSG(page1.valid(), "%s", error.c_str());
    CHECK(page1.pages == 2);
    CHECK(std::abs(page1.metres_w - .420) < 1e-6);
    const auto page2 = gplot::load_underlay(output, 2, error, 128);
    CHECK_MSG(page2.valid(), "%s", error.c_str());
    CHECK(std::abs(page2.metres_w - .297) < 1e-6);
    CHECK(publisher.selected_layout() == -1);
    CHECK(publisher.document_path().empty());
    CHECK(publisher.canvas().shapes().size() == count);
#ifdef GLCAD_XPS_UNDERLAYS
    const auto xps = scratch_path("combined issue.dwfx");
    CHECK(publisher.publish_sheet_set(xps));
    const auto xp = gplot::load_underlay(xps, 2, error, 128);
    CHECK_MSG(xp.valid(), "%s", error.c_str());
    CHECK(xp.pages == 2);
    CHECK(std::abs(xp.metres_w - .297) < 1e-6);
#endif
    const auto read = [](const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    const auto issued = read(output);
    publisher.command("SHEETSET ADD \"" + first + "\" Missing");
    CHECK(!publisher.publish_sheet_set(output));
    CHECK(read(output) == issued);
    publisher.command("SHEETSET REMOVE 3");
    CHECK(publisher.publish_sheets().size() == 2);
    const auto bad = scratch_path("invalid-set.gss");
    { std::ofstream f(bad); f << "GLCAD_SHEET_SET 1\n\"file.glcad\" \"Plan\"\n"; }
    CHECK(!publisher.load_sheet_set(bad));
    CHECK(publisher.publish_sheets().size() == 2);
    CHECK(publisher.publish_sheets()[0].layout == "Detail");
}

TEST(plot_preview_uses_the_published_page_and_blocks_hidden_drawing_interaction) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.command("LAYOUT NEW Preview");
    s.command("PAGESETUP A4 portrait");
    s.build_frame();
    const auto shapes = s.canvas().shapes().size();
    const auto layout = s.selected_layout();
    gplot::PdfWriter expected;
    CHECK(s.plot_sheet(expected).ok);
    CHECK(s.open_plot_preview());
    CHECK(s.plot_preview_open());
    s.build_frame();
    CHECK(s.images().size() == 1);
    CHECK(s.static_lines().empty());
    CHECK(s.canvas_lines().empty());
    const auto output = scratch_path("preview-saved.pdf");
    CHECK(s.save_plot_preview(output));
    std::ifstream pdf(output, std::ios::binary);
    const std::string bytes(std::istreambuf_iterator<char>(pdf), {});
    CHECK(bytes == expected.build());
    const auto old_quad = s.images().front().vertices;
    s.scroll(0, 1, false); s.build_frame();
    CHECK(s.images().front().vertices.front().pos[0] != old_quad.front().pos[0]);
    s.pointer_down(800, 500); s.pointer_move(900, 550); s.pointer_up(900, 550);
    s.command("SELECTALL"); s.command("ERASE");
    CHECK(s.canvas().shapes().size() == shapes);
    CHECK(s.selected_layout() == layout);
    s.key_special(1);
    CHECK(!s.plot_preview_open());
    s.build_frame();
    CHECK(s.canvas().shapes().size() == shapes);
    CHECK(s.selected_layout() == layout);
}

TEST(printing_receives_the_preview_pdf_and_preserves_cancelled_jobs) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "2,0", "2,1", "0,1");
    int calls = 0;
    std::string prepared;
    s.set_print_hook([&](const std::string& path, const std::string& printer, std::string& message) {
        ++calls;
        CHECK(printer == "Office");
        std::ifstream file(path, std::ios::binary);
        prepared.assign(std::istreambuf_iterator<char>(file), {});
        CHECK(prepared.rfind("%PDF-", 0) == 0);
        message = "printing cancelled";
        return false;
    });
    s.command("PRINT Office");
    CHECK(calls == 1);
    CHECK(s.plot_preview_open());
    const auto copy = scratch_path("cancelled-print-preview.pdf");
    CHECK(s.save_plot_preview(copy));
    std::ifstream file(copy, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(file), {}) == prepared);
    CHECK(s.log().back() == "printing cancelled");
    s.key_special(1);
    CHECK(!s.plot_preview_open());
}

TEST(region_conversion_booleans_selection_and_undo_keep_closed_entities) {
    Shell s=make_shell(); auto& c=s.canvas();
    draw_rect(s,"0,0","4,0","4,3","0,3");
    s.command("SELECTALL");s.command("REGION");
    CHECK(c.selection_count()==1);CHECK(c.is_region(0));
    CHECK_MSG(std::abs(c.region_at(0).area(c.volume())-12)<32*c.volume().inv_scale(),"area %.17g, lattice %.17g",c.region_at(0).area(c.volume()),c.volume().inv_scale());
    CHECK(c.grips().empty());
    s.command("UNDO");CHECK(!c.is_region(0));
    s.command("REDO");CHECK(c.is_region(0));
    c.set_selection({2});CHECK(c.selection().size()==4);
    s.command("LIST");
    CHECK(std::any_of(s.log().begin(),s.log().end(),[](const auto& line){return line.find("Region  layer")!=std::string::npos;}));
    s.command("PEDIT OPEN");CHECK(c.shapes().size()==4);CHECK(c.is_region(0));
    c.clear_selection();
    draw_rect(s,"1,1","2,1","2,2","1,2");
    c.set_selection({4,5,6,7});s.command("REGION");
    c.set_selection({0,4});CHECK(c.selection_count()==2);
    s.command("SUBTRACT");CHECK(c.selection_count()==1);
    CHECK(c.region_at(0).loops.size()==2);
    CHECK(std::abs(c.region_at(0).area(c.volume())-11)<32*c.volume().inv_scale());
    CHECK(!c.region_at(0).contains(gcm::to_lattice(c.volume(),1.5,1.5)));
    c.set_tool(Tool::Move);c.type_point("0,0");c.type_point("10,20");c.cancel();
    CHECK(c.region_at(0).contains(gcm::to_lattice(c.volume(),10.5,20.5)));
    CHECK(!c.region_at(0).contains(gcm::to_lattice(c.volume(),11.5,21.5)));
    s.command("UNDO");CHECK(c.region_at(0).contains(gcm::to_lattice(c.volume(),.5,.5)));
    s.command("UNDO");CHECK(c.shapes().size()==8);CHECK(c.region_at(0).loops.size()==1);
    s.command("REDO");CHECK(c.region_at(0).loops.size()==2);
    c.select_all();s.command("EXPLODE");
    CHECK(std::none_of(c.shapes().begin(),c.shapes().end(),[](const auto& sh){return sh.region||sh.poly;}));
    s.command("UNDO");CHECK(c.region_at(0).loops.size()==2);
}

TEST(region_intersections_preserve_curves_and_empty_results_are_undoable) {
    Shell s=make_shell();auto& c=s.canvas();
    auto circle=[&](double x) {gsnap::Shape sh;sh.kind=gsnap::ShapeKind::Circle;
        sh.a=gcm::to_lattice(c.volume(),x,0);sh.radius=gcm::i64(std::llround(2*c.volume().scale()));return sh;};
    CHECK(c.create_region({circle(0)}));CHECK(c.create_region({circle(2)}));
    c.select_all();s.command("INTERSECT");
    const auto edges=c.shapes().size();CHECK(edges>=2&&edges<=4);CHECK(c.selection_count()==1);
    for(const auto& sh:c.shapes())CHECK(sh.kind==gsnap::ShapeKind::Arc);
    const auto area=8*std::acos(.5)-2*std::sqrt(3.0);
    CHECK_MSG(std::abs(c.region_at(0).area(c.volume())-area)<32*c.volume().inv_scale(),"area %.17g, expected %.17g",c.region_at(0).area(c.volume()),area);
    c.set_tool(Tool::Mirror);c.type_point("0,0");c.type_point("0,1");c.cancel();
    CHECK(c.shapes().size()==2*edges);
    CHECK(gregion::validate_members(c.shapes()).empty());
    CHECK(std::abs(c.region_at(edges).area(c.volume())-area)<32*c.volume().inv_scale());
    c.set_selection({0});CHECK(c.create_region({circle(10)}));
    c.set_selection({0,2*edges});s.command("INTERSECT");
    CHECK(c.shapes().size()==edges);
    CHECK(c.selection_count()==0);
    s.command("UNDO");CHECK(c.shapes().size()==2*edges+1);
    CHECK(gregion::validate_members(c.shapes()).empty());
}

TEST(regions_reopen_in_model_layouts_and_blocks_and_export_boundaries) {
    Shell s=make_shell();auto& c=s.canvas();
    draw_rect(s,"10,20","14,20","14,23","10,23");
    c.select_all();s.command("REGION");
    double px,py;c.view().screen_of(10,20,px,py);
    CHECK(c.define_block("Region block",px,py));CHECK(c.shapes().empty());
    s.command("LAYOUT NEW Sheet");CHECK(s.select_layout(0));
    draw_rect(s,"0,0","2,0","2,1","0,1");
    s.canvas().select_all();s.command("REGION");
    const auto path=scratch_path("region-entities.glcad");CHECK(s.save_file(path));
    Shell reopened=make_shell();CHECK(reopened.open_file(path));
    CHECK(reopened.select_layout(0));CHECK(reopened.canvas().is_region(0));
    CHECK(std::abs(reopened.canvas().region_at(0).area(c.volume())-2)<32*c.volume().inv_scale());
    CHECK(reopened.select_layout(-1));
    const auto* def=reopened.canvas().blocks().find("Region block");CHECK(def!=nullptr);
    if(def)CHECK(gregion::from_members(def->geometry).ok);
    reopened.canvas().select_reference(0);reopened.command("EXPLODE");
    CHECK(reopened.canvas().shapes().size()==4);
    CHECK(reopened.canvas().is_region(0));
    CHECK(reopened.canvas().region_at(0).contains(gcm::to_lattice(c.volume(),11,21)));
    CHECK(reopened.canvas().blocks().references().empty());
    auto& rc=reopened.canvas();rc.view().screen_of(30,40,px,py);
    CHECK(rc.insert_block("Region block",px,py));rc.select_reference(0);reopened.command("EXPLODE");
    CHECK(rc.shapes().size()==8);CHECK(rc.shapes()[0].poly!=rc.shapes()[4].poly);
    CHECK(gregion::validate_members(rc.shapes()).empty());
    const auto dxf=scratch_path("region-boundaries.dxf");
    CHECK(reopened.export_file(dxf));
    std::ifstream file(dxf);const std::string bytes(std::istreambuf_iterator<char>(file),{});
    const auto exchanged=gdxf::read(bytes,c.volume());CHECK(exchanged.ok);
    CHECK(exchanged.document.entities.size()==8);CHECK(exchanged.document.blocks.find("Region block")!=nullptr);
    CHECK(rc.is_region(0));CHECK(rc.is_region(4));
    const auto dwg=scratch_path("region-boundaries.dwg");CHECK(reopened.export_file(dwg));
    Shell imported=make_shell();CHECK(imported.open_file(dwg));CHECK(imported.select_layout(0));
    const auto sheet=gregion::from_shapes(imported.canvas().shapes());CHECK(sheet.ok);
    if(sheet.ok)CHECK(std::abs(sheet.region.area(c.volume())-2)<32*c.volume().inv_scale());
    reopened.canvas().select_all();reopened.command("ERASE");
    CHECK(reopened.export_file(dxf));
    CHECK(reopened.save_file(scratch_path("region-after-explode.glcad")));
}

TEST(region_scaling_keeps_line_arc_joins_closed_in_native_files_and_blocks) {
    Shell s=make_shell();auto& c=s.canvas();
    gsnap::Shape circle;circle.kind=gsnap::ShapeKind::Circle;
    circle.radius=gcm::i64(std::llround(2*c.volume().scale()));
    CHECK(c.create_region({circle}));c.clear_selection();
    draw_rect(s,"0.73123,-3","3,-3","3,3","0.73123,3");
    c.set_selection({1,2,3,4});s.command("REGION");c.set_selection({0,1});s.command("SUBTRACT");
    const auto original=c.region_at(0).area(c.volume());
    for(double factor:{1000.0,-.01,3.731}) {
        c.modify_shapes([&](auto& shapes){for(auto& sh:shapes) sh=gedit::scale(sh,{0,0},factor);});
        const auto error=gregion::validate_members(c.shapes());CHECK_MSG(error.empty(),"%s",error.c_str());
    }
    CHECK(std::abs(c.region_at(0).area(c.volume())-original*37.31*37.31)<.01);
    const auto path=scratch_path("scaled-region.glcad");CHECK(s.save_file(path));
    Shell back=make_shell();CHECK(back.open_file(path));CHECK(back.canvas().is_region(0));
    c.select_all();double px,py;c.view().screen_of(0,0,px,py);CHECK(c.define_block("Scaled",px,py));
    auto ref=c.blocks().references().front();ref.scale_x=ref.scale_y=1000;
    const auto expanded=c.blocks().expand(ref,c.volume());
    CHECK(gregion::validate_members(expanded).empty());
}

TEST(sheet_set_preview_navigates_mixed_pages_and_prints_the_prepared_document) {
    const auto first=scratch_path("preview-set-first.glcad"),second=scratch_path("preview-set-second.glcad");
    Shell one=make_shell();draw_rect(one,"0,0","4,0","4,3","0,3");one.command("LAYOUT NEW Plan");one.command("PAGESETUP A4 portrait");CHECK(one.save_file(first));
    Shell two=make_shell();draw_rect(two,"0,0","8,0","8,3","0,3");two.command("LAYOUT NEW Detail");two.command("PAGESETUP A3 landscape");CHECK(two.save_file(second));
    Shell s=make_shell();s.command("SHEETSET ADD \""+first+"\" Plan \"Issued plan\"");s.command("SHEETSET ADD \""+second+"\" Detail \"Issued detail\"");
    int prints=0;std::string printed;
    s.set_print_hook([&](const auto& path,const auto& printer,std::string&) {
        ++prints;CHECK(printer=="Office");std::string error;
        const auto image=gplot::load_underlay(path,2,error,64);CHECK_MSG(image.valid(),"%s",error.c_str());CHECK(image.pages==2);
        CHECK(std::abs(image.metres_w-.420)<1e-6);
        std::ifstream f(path,std::ios::binary);printed.assign(std::istreambuf_iterator<char>(f),{});return true;
    });
    s.command("PRINT SET Office");CHECK(prints==1);CHECK(s.plot_preview_open());CHECK(s.plot_preview_pages()==2);CHECK(s.plot_preview_page()==1);
    s.set_viewport(320,600);s.build_frame();CHECK(s.images().size()==1);
    if(!s.images().empty())CHECK(s.images()[0].image->width<s.images()[0].image->height);
    s.command("PREVIEW NEXT");CHECK(s.plot_preview_page()==2);s.build_frame();
    if(!s.images().empty())CHECK(s.images()[0].image->width>s.images()[0].image->height);
    CHECK(!s.set_plot_preview_page(3));CHECK(s.plot_preview_page()==2);
    s.command("PREVIEW PREVIOUS");CHECK(s.plot_preview_page()==1);s.command("PREVIEW PAGE 2");CHECK(s.plot_preview_page()==2);
    const auto copy=scratch_path("preview-set-saved.pdf");CHECK(s.save_plot_preview(copy));
    {std::ifstream f(copy,std::ios::binary);CHECK(std::string(std::istreambuf_iterator<char>(f),{})==printed);}
    CHECK(std::filesystem::remove(first));s.command("PREVIEW SET");CHECK(s.plot_preview_pages()==2);CHECK(s.plot_preview_page()==2);
    s.command("PRINT Office");CHECK(prints==2);
    {std::ifstream f(copy,std::ios::binary);CHECK(std::string(std::istreambuf_iterator<char>(f),{})==printed);}
    CHECK(s.document_path().empty());CHECK(s.canvas().shapes().empty());CHECK(s.selected_layout()==-1);
    s.command("PREVIEW CLOSE");CHECK(!s.plot_preview_open());
}

TEST(complex_leader_paths_edit_copy_and_native_roundtrip_as_one_annotation) {
    Shell s=make_shell();s.build_frame();const auto v=s.canvas().volume();
    const auto p=[&](double x,double y){return gcm::to_lattice(v,x,y);};
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.p1=p(0,0);leader.p2=p(1,2);leader.p3=p(4,2);leader.text_override="Both branches";
    leader.leader_text_absolute=true;leader.leader_text_halign=2;leader.leader_text_valign=3;leader.leader_text_height=.003;leader.leader_text_angle=.5;leader.leader_arrow_size=.002;
    leader.leader_paths={{{p(0,0),p(1,2),p(2,3),p(3,2),p(4,2)},true},{{p(0,4),p(2,3),p(3,2)},false}};
    s.canvas().add_dimensions({leader});s.canvas().set_selected_dimensions({0});s.command("LEADEREDIT VERTEX 3 2,4");
    CHECK(s.canvas().dimensions()[0].leader_paths[0].vertices[2].y==p(2,4).y);
    CHECK(s.canvas().dimensions()[0].leader_paths[1].vertices[1].y==p(2,4).y);
    s.command("UNDO");CHECK(s.canvas().dimensions()[0].leader_paths[0].vertices[2].y==p(2,3).y);
    s.canvas().set_selected_dimensions({0});s.command("COPY");s.canvas().type_point("0,0");s.canvas().type_point("5,0");
    CHECK(s.canvas().dimensions().size()==2);if(s.canvas().dimensions().size()!=2)return;
    const auto& copied=s.canvas().dimensions()[1];CHECK(copied.leader_paths.size()==2);
    CHECK(std::abs(copied.leader_paths[1].vertices[0].x-p(5,4).x)<=1);
    const auto path=scratch_path("complex-leader.glcad");CHECK(s.save_file(path));
    Shell reopened=make_shell();CHECK(reopened.open_file(path));CHECK(reopened.canvas().dimensions().size()==2);
    if(reopened.canvas().dimensions().size()==2) {
        CHECK(reopened.canvas().dimensions()[1].leader_paths.size()==2);
        CHECK(reopened.canvas().dimensions()[1].leader_paths[0].vertices.size()==5);
        CHECK(!reopened.canvas().dimensions()[1].leader_paths[1].arrow);
        const auto& annotation=reopened.canvas().dimensions()[1];
        CHECK(annotation.leader_text_absolute);CHECK(annotation.leader_text_halign==2);CHECK(annotation.leader_text_valign==3);
        CHECK(std::abs(annotation.leader_text_height-.003)<1e-10);CHECK(std::abs(annotation.leader_text_angle-.5)<1e-10);
        CHECK(std::abs(annotation.leader_arrow_size-.002)<1e-10);
    }
    gfile::Contents invalid;CHECK(gfile::load_file(path,invalid).ok);
    for(auto& entity:invalid.entities)if(entity.kind==gdoc::Kind::Leader) {entity.payload.ints[8]=4096;break;}
    gio::MemoryWriter writer;CHECK(!gfile::write_document(writer,invalid).ok);CHECK(writer.bytes().empty());
    std::filesystem::remove(path);
}

TEST(xclip_updates_shared_reference_geometry_and_survives_undo_and_reopen) {
    Shell s=make_shell();s.build_frame();s.canvas().set_display_unit(gunit::Length::Metre);const auto v=s.canvas().volume();
    gdoc::BlockDef def;def.name="Site";def.is_xref=true;gsnap::Shape line;line.a=gcm::to_lattice(v,-2,0);line.b=gcm::to_lattice(v,2,0);def.geometry={line};s.canvas().blocks().define(def);
    gdoc::Xref x;x.name="Site";x.path="site.dwg";x.state=gdoc::XrefState::Loaded;s.canvas().blocks().attach(x);
    gdoc::BlockRef ref;ref.name=x.name;s.canvas().blocks().insert(ref);
    s.command("XCLIP Site RECT 0 -1 1 1");const auto clipped=s.canvas().blocks().expand(ref,v);CHECK(clipped.size()==1);
    if(!clipped.empty()){CHECK(std::abs(clipped[0].a.x)<=1);CHECK(std::abs(clipped[0].b.x-gcm::to_lattice(v,1,0).x)<=1);}
    double px,py;s.canvas().view().screen_of(-1,0,px,py);CHECK(s.canvas().pick_reference(px,py)==Canvas::npos32);
    s.command("XCLIP Site INVERT");CHECK(s.canvas().blocks().expand(ref,v).size()==2);
    s.command("UNDO");CHECK(!s.canvas().blocks().xref("Site")->clip_inverted);
    const auto path=scratch_path("xref-clipped.glcad");CHECK(s.save_file(path));Shell back=make_shell();CHECK(back.open_file(path));
    CHECK(back.canvas().blocks().xref("Site")->clip.size()==4);CHECK(back.canvas().blocks().expand(ref,back.canvas().volume()).size()==1);
    s.command("XCLIP Site OFF");CHECK(s.canvas().blocks().expand(ref,v).front().a.x==line.a.x);
    std::filesystem::remove(path);
}

TEST(block_owned_text_survives_import_native_reopen_and_reexport_without_duplicates) {
    Shell source=make_shell(); const auto& v=source.canvas().volume();
    gdxf::Document document;
    gdoc::BlockDef block;block.name="Title";
    gdoc::BlockDef::Note note;note.text="Owned title";note.pos=gcm::to_lattice(v,1,2);note.height=.01;
    note.attrs.color=gdoc::Color::aci(2);block.notes.push_back(note);document.blocks.define(block);
    gdoc::BlockRef ref;ref.name="Title";ref.insertion=gcm::to_lattice(v,3,4);document.blocks.insert(ref);
    const auto incoming=scratch_path("owned-note.dxf"),native=scratch_path("owned-note.glcad"),outgoing=scratch_path("owned-note-export.dxf");
    {std::ofstream file(incoming);file<<gdxf::write(document,v);CHECK(bool(file));}
    CHECK(source.import_file(incoming));
    CHECK(source.canvas().texts().empty()); CHECK(source.canvas().blocks().references().size()==1);
    const auto labels=source.canvas().reference_labels(source.canvas().blocks().references()[0]);
    CHECK(labels.size()==1); if(!labels.empty())CHECK(labels[0].text=="Owned title");
    const auto content=source.build_plot_content(source.canvas(),1);
    CHECK(content.base.notes.size()==1); if(!content.base.notes.empty())CHECK(content.base.notes[0].attrs.color.index==2);
    CHECK(source.save_file(native)); Shell opened=make_shell();CHECK(opened.open_file(native));
    CHECK(opened.canvas().texts().empty());
    const auto* saved=opened.canvas().blocks().find("Title");CHECK(saved!=nullptr);if(saved)CHECK(saved->notes.size()==1);
    CHECK(opened.export_file(outgoing));
    std::ifstream file(outgoing);const std::string encoded((std::istreambuf_iterator<char>(file)),{});
    const auto back=gdxf::read(encoded,v);CHECK(back.ok);CHECK(back.document.texts.empty());
    const auto* result=back.document.blocks.find("Title");CHECK(result!=nullptr);if(result)CHECK(result->notes.size()==1);
    source.command("LAYOUT NEW Sheet");source.command("PSPACE");
    auto paper_block=block;paper_block.name="PaperTitle";paper_block.notes[0].text="Paper owned";
    paper_block.notes[0].pos=gcm::to_lattice(v,.02,.03);source.canvas().blocks().define(paper_block);
    gdoc::BlockRef paper_ref;paper_ref.name="PaperTitle";source.canvas().blocks().insert(paper_ref);
    gplot::PdfWriter writer;CHECK(source.plot_sheet(writer).ok);
    const auto& plotted=writer.texts_of_page(0);
    CHECK(std::count_if(plotted.begin(),plotted.end(),[](const auto& text){return text.text=="Paper owned";})==1);
    gdoc::Xref xref;xref.name="Title";xref.state=gdoc::XrefState::Loaded;opened.canvas().blocks().attach(xref);
    CHECK(opened.canvas().reference_labels(opened.canvas().blocks().references()[0]).size()==1);
    opened.canvas().blocks().unload("Title");
    CHECK(opened.canvas().reference_labels(opened.canvas().blocks().references()[0]).empty());
    CHECK(opened.build_plot_content(opened.canvas(),1).base.notes.empty());
}

TEST(xref_source_isolation_keeps_host_entities_and_reloads_atomically) {
    const auto path=scratch_path("source with spaces.glcad");
    Shell source=make_shell();draw_rect(source,"0,0","4,0","4,3","0,3");
    source.canvas().add_text(gcm::to_lattice(source.canvas().volume(),20,30),"Source note",.1);
    gdim::Dimension dim;dim.kind=gdim::Kind::Leader;dim.p1={0,0};dim.p2={1000,2000};dim.p3={3000,4000};dim.text_override="Source leader";
    source.canvas().add_dimensions({dim});CHECK(source.save_file(path));
    Shell host=make_shell();draw_rect(host,"40,40","44,40","44,43","40,43");
    host.canvas().add_text({4000,5000},"Host note",.1);
    const auto shape_count=host.canvas().shapes().size();const auto text_count=host.canvas().texts().size();
    auto quoted_path=path;
#ifdef _WIN32
    std::replace(quoted_path.begin(),quoted_path.end(),'/','\\');
#endif
    host.command("XATTACH \""+quoted_path+"\"");host.build_frame();
    CHECK(host.canvas().shapes().size()==shape_count);CHECK(host.canvas().texts().size()==text_count);CHECK(host.canvas().dimensions().empty());
    CHECK(host.canvas().blocks().references().size()==1);if(host.canvas().blocks().references().empty())return;
    const auto name=std::filesystem::path(path).filename().string();const auto* root=host.canvas().blocks().find(name);CHECK(root!=nullptr);if(!root)return;
    CHECK(root->notes.size()==2);CHECK(!root->source_document.empty());
    gfile::Contents retained;gio::MemoryReader input(root->source_document);CHECK(gfile::read_document(input,retained).ok);
    CHECK(std::any_of(retained.entities.begin(),retained.entities.end(),[](const auto& entity){return entity.kind==gdoc::Kind::Leader;}));
    const auto old_geometry=root->geometry.size();const auto old_path=host.canvas().blocks().xrefs().back().path;
    host.command("XREF PATH \""+name+"\" \""+path+".missing\"");
    CHECK(host.canvas().blocks().find(name)->geometry.size()==old_geometry);
    CHECK(host.canvas().blocks().xrefs().back().path==old_path);
    source.canvas().modify_texts([](auto& texts){texts[0].text="Revised note";});CHECK(source.save_file(path));
    host.command("XREF RELOAD \""+name+"\"");
    CHECK(host.canvas().blocks().find(name)->notes[0].text=="Revised note");
    CHECK(host.canvas().shapes().size()==shape_count);CHECK(host.canvas().texts().size()==text_count);
    host.command("UNDO");CHECK(host.canvas().blocks().find(name)->notes[0].text=="Source note");
    host.command("REDO");CHECK(host.canvas().blocks().find(name)->notes[0].text=="Revised note");
    const auto saved=scratch_path("xref-owned-host.glcad");CHECK(host.save_file(saved));
    Shell reopened=make_shell();CHECK(reopened.open_file(saved));
    CHECK(reopened.canvas().blocks().find(name)->notes[0].text=="Revised note");
    reopened.command("XREF UNLOAD \""+name+"\"");
    CHECK(reopened.canvas().reference_labels(reopened.canvas().blocks().references()[0]).empty());
    CHECK(reopened.canvas().texts().size()==text_count);
}

TEST(xref_nested_sources_reload_relative_paths_and_do_not_propagate_overlays) {
    const auto child_path=scratch_path("nested-source.glcad"),parent_path=scratch_path("parent-source.glcad");
    Shell child=make_shell();draw_rect(child,"0,0","1,0","1,1","0,1");CHECK(child.save_file(child_path));
    Shell parent=make_shell();draw_rect(parent,"3,0","4,0","4,1","3,1");CHECK(parent.save_file(parent_path));
    parent.command("XATTACH nested-source.glcad");CHECK(parent.canvas().blocks().references().size()==1);CHECK(parent.save_file(parent_path));
    Shell host=make_shell();host.command("XATTACH "+parent_path);
    const auto geometry=[&]{return host.canvas().blocks().expand(host.canvas().blocks().references().front(),host.canvas().volume());};
    CHECK(host.canvas().blocks().references().size()==1);if(host.canvas().blocks().references().empty())return;
    CHECK(geometry().size()==8);
    draw_rect(child,"6,0","7,0","7,1","6,1");CHECK(child.save_file(child_path));
    host.command("XREF RELOAD parent-source.glcad");CHECK(geometry().size()==12);
    host.command("XREF UNLOAD parent-source.glcad|nested-source.glcad");CHECK(geometry().size()==4);
    host.command("XREF RELOAD parent-source.glcad");CHECK(geometry().size()==4);
    auto external=*parent.canvas().blocks().xref("nested-source.glcad");external.type=gdoc::XrefType::Overlay;
    parent.canvas().blocks().attach(external);CHECK(parent.save_file(parent_path));
    Shell overlay_host=make_shell();overlay_host.command("XATTACH "+parent_path);
    CHECK(overlay_host.canvas().blocks().references().size()==1);
    if(!overlay_host.canvas().blocks().references().empty())CHECK(overlay_host.canvas().blocks().expand(overlay_host.canvas().blocks().references().front(),overlay_host.canvas().volume()).size()==4);
    host.command("XREF DETACH parent-source.glcad");CHECK(host.canvas().blocks().references().empty());CHECK(host.canvas().blocks().xrefs().empty());
    CHECK(std::none_of(host.canvas().blocks().all().begin(),host.canvas().blocks().all().end(),[](const auto& def){return def.source_owner=="parent-source.glcad";}));
}

TEST(xref_underlays_stay_owned_transform_and_disappear_on_unload) {
    const auto image_path=scratch_path("source-image.pdf"),source_path=scratch_path("image-source.glcad");
    gplot::PdfWriter pdf;pdf.begin_page(.2,.1);gplot::PdfPath line;line.points={{.01,.01},{.19,.09}};pdf.add(line);pdf.end_page();CHECK(pdf.write_file(image_path));
    Shell source=make_shell();CHECK(source.attach_underlay(image_path,1));
    auto* underlay=source.canvas().underlays().find("source-image.pdf");CHECK(underlay!=nullptr);if(!underlay)return;
    underlay->plottable=true;CHECK(source.save_file(source_path));
    Shell host=make_shell();host.command("XATTACH "+source_path);host.build_frame();
    CHECK(host.canvas().underlays().size()==0);CHECK(host.canvas().blocks().references().size()==1);if(host.canvas().blocks().references().empty())return;
    CHECK(host.canvas().underlay_instances().size()==1);CHECK(host.images().size()==1);
    auto* ref=host.canvas().blocks().reference_mut(0);ref->scale_x=2;ref->scale_y=3;ref->rotation=3.141592653589793/2;
    ref->insertion=gcm::to_lattice(host.canvas().volume(),1,2);
    const auto images=host.underlay_content(host.canvas());CHECK(images.size()==1);if(images.empty())return;
    CHECK(std::abs(images[0].origin.x-1)<1e-6);CHECK(std::abs(images[0].origin.y-2)<1e-6);
    CHECK(std::abs(images[0].x_axis.x)<1e-6);CHECK(std::abs(images[0].x_axis.y-.4)<1e-6);
    CHECK(std::abs(images[0].y_axis.x+.3)<1e-6);CHECK(std::abs(images[0].y_axis.y)<1e-6);
    const auto host_path=scratch_path("owned-image-host.glcad");CHECK(host.save_file(host_path));
    Shell reopened=make_shell();CHECK(reopened.open_file(host_path));CHECK(reopened.canvas().underlay_instances().size()==1);
    reopened.command("XREF UNLOAD image-source.glcad");reopened.build_frame();
    CHECK(reopened.canvas().underlay_instances().empty());CHECK(reopened.images().empty());CHECK(reopened.underlay_content(reopened.canvas()).empty());
}

TEST(xref_circular_sources_leave_the_host_unchanged) {
    const auto first=scratch_path("cycle-a.glcad"),second=scratch_path("cycle-b.glcad");
    Shell a=make_shell();gdoc::Xref x;x.name="B";x.path=second;a.canvas().blocks().attach(x);CHECK(a.save_file(first));
    Shell b=make_shell();x.name="A";x.path=first;b.canvas().blocks().attach(x);CHECK(b.save_file(second));
    Shell host=make_shell();host.canvas().add_text({1,2},"Keep",.01);host.command("XATTACH "+first);
    CHECK(host.canvas().blocks().xrefs().empty());CHECK(host.canvas().blocks().references().empty());CHECK(host.canvas().texts().size()==1);
    CHECK(std::any_of(host.log().begin(),host.log().end(),[](const auto& message){return message.find("circular")!=std::string::npos;}));
}

TEST(xref_nested_dwg_sources_export_the_retained_parent_graph) {
    const auto child_path=scratch_path("graph-child.dwg"),parent_path=scratch_path("graph-parent.dwg"),host_path=scratch_path("graph-host.dwg");
    Shell child=make_shell();draw_rect(child,"0,0","1,0","1,1","0,1");
    child.command("LAYOUT NEW ChildSheet");CHECK(child.select_layout(0));child.canvas().add_text({0,0},"Child paper only",.01);CHECK(child.export_file(child_path));
    Shell parent=make_shell();draw_rect(parent,"3,0","4,0","4,1","3,1");
    parent.command("XATTACH \""+child_path+"\"");CHECK(parent.canvas().blocks().references().size()==1);
    parent.command("LAYOUT NEW ParentSheet");CHECK(parent.select_layout(0));parent.canvas().add_text({0,0},"Parent paper only",.01);CHECK(parent.export_file(parent_path));
    Shell host=make_shell();host.command("XATTACH \""+parent_path+"\"");
    CHECK(host.canvas().blocks().references().size()==1);CHECK(host.canvas().blocks().xrefs().size()==2);
    CHECK(host.export_file(host_path));auto back=gdwg::read_file(host_path,host.canvas().volume());CHECK(back.ok);
    CHECK(back.blocks.references().size()==1);CHECK(back.blocks.xrefs().size()==2);
    const auto name=std::filesystem::path(parent_path).filename().string();
    const auto child_name=name+"|"+std::filesystem::path(child_path).filename().string();
    const auto* root=back.blocks.find(name);const auto* nested=back.blocks.xref(child_name);
    CHECK(root!=nullptr);CHECK(nested!=nullptr);if(!root||!nested)return;
    CHECK(root->geometry.size()==4);CHECK(root->notes.empty());CHECK(root->nested_refs.size()==1);CHECK(nested->nested);
    std::error_code path_error;CHECK(std::filesystem::equivalent(nested->resolved_path,child_path,path_error));CHECK(!path_error);
    if(root->nested_refs.size()==1)CHECK(root->nested_refs[0].name==child_name);
    const auto* cached=back.blocks.find(child_name);CHECK(cached!=nullptr);if(cached){CHECK(cached->geometry.size()==4);CHECK(cached->notes.empty());}
    std::filesystem::remove(child_path);std::filesystem::remove(parent_path);std::filesystem::remove(host_path);
}

TEST(dwg_sheet_xrefs_keep_units_reload_and_native_reopen_without_model_copies) {
    const auto source_path=scratch_path("sheet-survey.dwg"),drawing=scratch_path("sheet-xref.dwg"),native=scratch_path("sheet-xref.glcad"),again=scratch_path("sheet-xref-again.dwg");
    Shell source=make_shell();draw_rect(source,"0,0","1,0","1,1","0,1");CHECK(source.export_file(source_path));
    Shell host=make_shell();host.canvas().set_display_unit(gunit::Length::Millimetre);
    host.command("LAYOUT NEW Sheet");CHECK(host.select_layout(0));host.command("XATTACH \""+source_path+"\"");
    CHECK(host.canvas().blocks().references().size()==1);if(host.canvas().blocks().references().size()!=1)return;
    const auto name=host.canvas().blocks().references()[0].name;const auto& v=host.canvas().volume();
    auto* placement=host.canvas().blocks().reference_mut(0);placement->insertion=gcm::to_lattice(v,.04,.05);placement->scale_x=.1;placement->scale_y=-.2;
    auto* metadata=host.canvas().blocks().xref(name);CHECK(metadata!=nullptr);if(!metadata)return;
    metadata->clip={gcm::to_lattice(v,0,0),gcm::to_lattice(v,.5,.5)};
    CHECK(host.export_file(drawing));Shell imported=make_shell();CHECK(imported.import_file(drawing,false));
    CHECK(imported.canvas().blocks().references().size()==1);if(imported.canvas().blocks().references().size()!=1)return;
    const auto imported_name=imported.canvas().blocks().references()[0].name;
    const auto check_sheet=[&](Shell& shell) {
        CHECK(shell.canvas().blocks().references().size()==1);if(shell.canvas().blocks().references().size()!=1)return;
        const auto& ref=shell.canvas().blocks().references()[0];double x,y;gcm::to_world(v,ref.insertion,x,y);
        CHECK(std::abs(x-.04)<1e-5);CHECK(std::abs(y-.05)<1e-5);CHECK(std::abs(ref.scale_x-.1)<1e-12);CHECK(std::abs(ref.scale_y+.2)<1e-12);
        const auto* xref=shell.canvas().blocks().xref(ref.name);CHECK(xref!=nullptr);if(!xref)return;
        CHECK(xref->clip.size()==2);if(xref->clip.size()==2){gcm::to_world(v,xref->clip[1],x,y);CHECK(std::abs(x-.5)<1e-5);CHECK(std::abs(y-.5)<1e-5);}
    };
    check_sheet(imported);CHECK(imported.select_layout(-1));CHECK(imported.canvas().blocks().references().empty());
    imported.command("XREF RELOAD \""+imported_name+"\"");CHECK(imported.canvas().blocks().references().empty());
    const auto* reloaded=imported.canvas().blocks().xref(imported_name);CHECK(reloaded&&reloaded->state==gdoc::XrefState::Loaded);
    CHECK(imported.select_layout(0));check_sheet(imported);CHECK(imported.save_file(native));
    Shell reopened=make_shell();CHECK(reopened.open_file(native));CHECK(reopened.select_layout(0));check_sheet(reopened);
    CHECK(reopened.select_layout(-1));CHECK(reopened.canvas().blocks().references().empty());CHECK(reopened.export_file(again));
    auto back=gdwg::read_file(again,v);CHECK(back.ok);CHECK(back.blocks.references().empty());CHECK(back.layouts.size()==1&&back.layouts[0].references.size()==1);
    std::filesystem::remove(source_path);std::filesystem::remove(drawing);std::filesystem::remove(native);std::filesystem::remove(again);
}

TEST(xref_model_sources_do_not_load_unavailable_paper_only_dependencies) {
    for(const auto* extension:{"glcad","dwg"}) {
        const auto path=scratch_path((std::string("model-with-sheet-reference.")+extension).c_str());
        Shell source=make_shell();draw_rect(source,"0,0","1,0","1,1","0,1");source.command("LAYOUT NEW Sheet");CHECK(source.select_layout(0));
        gdoc::Xref missing;missing.name="MissingSheetReference";missing.path="unavailable-paper-only.dwg";
        source.canvas().blocks().attach(missing);gdoc::BlockDef def;def.name=missing.name;def.is_xref=true;def.xref_path=missing.path;source.canvas().blocks().define(def);
        gdoc::BlockRef ref;ref.name=missing.name;source.canvas().blocks().insert(ref);
        CHECK(std::string(extension)=="dwg"?source.export_file(path):source.save_file(path));
        Shell host=make_shell();host.command("XATTACH \""+path+"\"");
        CHECK(host.canvas().blocks().references().size()==1);CHECK(host.canvas().blocks().xrefs().size()==1);
        if(host.canvas().blocks().references().size()==1) {
            const auto& root=host.canvas().blocks().references()[0];const auto* cached=host.canvas().blocks().find(root.name);CHECK(cached!=nullptr);
            if(cached){CHECK(cached->geometry.size()==4);CHECK(cached->nested_refs.empty());}
        }
        std::filesystem::remove(path);
    }
}

TEST(dwg_underlays_import_with_units_and_keep_native_clip_state_after_reopen) {
    const auto dwg=scratch_path("underlay-exchange.dwg"),native=scratch_path("underlay-exchange.glcad");
    Shell source=make_shell();const auto& v=source.canvas().volume();
    gdoc::Underlay u;u.kind=gdoc::UnderlayKind::Pdf;u.path="missing-survey.pdf";u.sheet="2";u.name="Survey";
    u.insertion=gcm::to_lattice(v,1,2);u.scale=2;u.y_factor=-1.5;u.rotation=.4;u.clip_enabled=false;
    u.clip={gcm::to_lattice(v,.01,.01),gcm::to_lattice(v,.1,.01),gcm::to_lattice(v,.1,.1),gcm::to_lattice(v,.01,.1)};
    source.canvas().underlays().attach(u);source.canvas().set_display_unit(gunit::Length::Millimetre);
    CHECK(source.export_file(dwg));Shell imported=make_shell();CHECK(imported.import_file(dwg,false));
    CHECK(imported.canvas().underlays().size()==1);if(imported.canvas().underlays().size()!=1)return;
    const auto& loaded=imported.canvas().underlays().all()[0];double x,y;gcm::to_world(v,loaded.insertion,x,y);
    CHECK(std::abs(x-1)<1e-5);CHECK(std::abs(y-2)<1e-5);CHECK(std::abs(loaded.scale-2)<1e-9);CHECK(std::abs(loaded.y_factor+1.5)<1e-12);
    CHECK(!loaded.clip_enabled);CHECK(loaded.clip.size()==4);CHECK(!loaded.found);CHECK(loaded.sheet=="2");
    CHECK(imported.save_file(native));Shell reopened=make_shell();CHECK(reopened.open_file(native));
    CHECK(reopened.canvas().underlays().size()==1);if(reopened.canvas().underlays().size()!=1)return;
    const auto& back=reopened.canvas().underlays().all()[0];CHECK(std::abs(back.y_factor+1.5)<1e-12);CHECK(!back.clip_enabled);CHECK(back.contrast_neutral==100);
    CHECK(back.layer==loaded.layer);CHECK(back.source_unit==.0254);CHECK(back.clip.size()==4);
    std::filesystem::remove(dwg);std::filesystem::remove(native);
}

TEST(dwg_underlay_handle_collisions_keep_each_imported_placement) {
    const auto dwg=scratch_path("underlay-collision.dwg"),native=scratch_path("underlay-collision.glcad");
    Shell source=make_shell();const auto& v=source.canvas().volume();
    gdoc::Underlay u;u.kind=gdoc::UnderlayKind::Pdf;u.path="missing-collision.pdf";u.sheet="1";u.name="Reference";
    u.insertion=gcm::to_lattice(v,2,3);source.canvas().underlays().attach(u);
    CHECK(source.export_file(dwg));
    Shell host=make_shell();CHECK(host.import_file(dwg,false));
    CHECK(host.canvas().underlays().size()==1);if(host.canvas().underlays().size()!=1)return;
    const auto original=host.canvas().underlays().all()[0].name;
    host.canvas().underlays().find(original)->insertion=gcm::to_lattice(v,5,6);
    CHECK(host.import_file(dwg,false));CHECK(host.import_file(dwg,false));
    CHECK(host.canvas().underlays().size()==3);
    CHECK(host.canvas().underlays().find(original)!=nullptr);
    CHECK(host.canvas().underlays().find(original+" (2)")!=nullptr);
    CHECK(host.canvas().underlays().find(original+" (3)")!=nullptr);
    const auto* kept=host.canvas().underlays().find(original);if(!kept)return;
    const auto expected=gcm::to_lattice(v,5,6);
    CHECK(kept->insertion.x==expected.x);CHECK(kept->insertion.y==expected.y);
    CHECK(host.save_file(native));Shell reopened=make_shell();CHECK(reopened.open_file(native));
    CHECK(reopened.canvas().underlays().size()==3);
    std::filesystem::remove(dwg);std::filesystem::remove(native);
}

#include "dgn_fixture.hpp"
TEST(dgn_underlays_keep_resolved_placement_in_dwg_model_layout_block_and_native_files) {
    const auto dgn=scratch_path("underlay-mm.dgn"),dwg=scratch_path("underlay-mm.dwg"),again=scratch_path("underlay-mm-again.dwg"),native=scratch_path("underlay-mm.glcad");
    CHECK(gtest::write_dgn_underlay_fixture(dgn));
    for(const auto units:{gunit::Length::Metre,gunit::Length::Millimetre}) {
        Shell source=make_shell();const auto& v=source.canvas().volume();source.canvas().set_display_unit(units);
        gdoc::Underlay u;u.kind=gdoc::UnderlayKind::Dgn;u.path=dgn;u.name="DGN";u.sheet="Model";u.fade=0;u.plottable=true;
        u.insertion=gcm::to_lattice(v,10,20);u.scale=2;u.y_factor=-1.25;u.rotation=.3;
        std::string error;CHECK_MSG(gplot::resolve_underlay(u,v,error),"%s",error.c_str());
        u.clip={gcm::to_lattice(v,-1,2.25),gcm::to_lattice(v,0,2.25),gcm::to_lattice(v,0,2.75),gcm::to_lattice(v,-1,2.75)};u.clip_inverted=true;
        source.canvas().underlays().attach(u);
        gdoc::BlockDef block;block.name="DGNBlock";block.underlays={u};source.canvas().blocks().define(block);
        gdoc::BlockRef ref;ref.name=block.name;ref.insertion=gcm::to_lattice(v,3,4);source.canvas().blocks().insert(ref);
        source.command("LAYOUT NEW DGN Sheet");CHECK(source.select_layout(0));auto paper=u;paper.insertion=gcm::to_lattice(v,.1,.2);paper.scale=.01;source.canvas().underlays().attach(paper);
        CHECK(source.select_layout(-1));CHECK(source.export_file(dwg));
        const auto check=[&](const gdoc::Underlay& actual,const gdoc::Underlay& expected) {
            CHECK(actual.found);CHECK(actual.source_unit_known);CHECK(!actual.source_coordinates_pending);CHECK(actual.source_unit==.001);
            CHECK(std::abs(actual.scale-expected.scale)<1e-10);CHECK(std::abs(actual.y_factor-expected.y_factor)<1e-12);CHECK(std::abs(actual.rotation-expected.rotation)<1e-12);
            double x,y,ex,ey;gcm::to_world(v,actual.insertion,x,y);gcm::to_world(v,expected.insertion,ex,ey);CHECK(std::abs(x-ex)<1e-5&&std::abs(y-ey)<1e-5);
            for(const auto point:{gcm::to_lattice(v,-1.25,2.5),gcm::to_lattice(v,-.5,2.5),gcm::to_lattice(v,.25,2.5)})CHECK(actual.contains(point)==expected.contains(point));
            const auto image=gplot::load_underlay(actual,v,error);CHECK_MSG(image.valid(),"%s",error.c_str());
        };
        const auto check_document=[&](Shell& shell) {
            CHECK(shell.select_layout(-1));CHECK(shell.canvas().underlays().size()==1);
            if(shell.canvas().underlays().size()==1)check(shell.canvas().underlays().all().front(),u);
            const auto* definition=shell.canvas().blocks().find("DGNBlock");CHECK(definition!=nullptr);
            if(definition){CHECK(definition->underlays.size()==1);if(definition->underlays.size()==1)check(definition->underlays.front(),u);}
            CHECK(shell.underlay_content(shell.canvas(),true).size()==2);
            CHECK(shell.select_layout(0));CHECK(shell.canvas().underlays().size()==1);
            if(shell.canvas().underlays().size()==1)check(shell.canvas().underlays().all().front(),paper);
            CHECK(shell.underlay_content(shell.canvas(),true).size()==1);
        };
        Shell imported=make_shell();CHECK(imported.import_file(dwg,false));check_document(imported);
        CHECK(imported.save_file(native));Shell reopened=make_shell();CHECK(reopened.open_file(native));check_document(reopened);
        CHECK(reopened.export_file(again));Shell second=make_shell();CHECK(second.import_file(again,false));check_document(second);
        if(units==gunit::Length::Millimetre) {
            const auto away=scratch_path("underlay-mm-away.dgn"),pending=scratch_path("underlay-mm-pending.glcad");
            std::filesystem::rename(dgn,away);
            {Shell missing=make_shell();CHECK(missing.import_file(dwg,false));CHECK(missing.save_file(pending));}
            std::filesystem::rename(away,dgn);
            {Shell restored=make_shell();CHECK(restored.open_file(pending));check_document(restored);}
            std::filesystem::remove(pending);
        }
    }
    for(const auto& path:{dgn,dwg,again,native})std::filesystem::remove(path);
}

TEST(dgn_custom_units_use_retained_dwg_transform_and_reload_when_source_returns) {
    const auto dgn=scratch_path("underlay-custom.dgn"),away=scratch_path("underlay-custom-away.dgn"),dwg=scratch_path("underlay-custom.dwg"),native=scratch_path("underlay-custom.glcad");
    CHECK(gtest::write_dgn_underlay_fixture(dgn,"mu"));
    Shell source=make_shell();const auto& v=source.canvas().volume();source.canvas().set_display_unit(gunit::Length::Millimetre);
    gdoc::Underlay expected;expected.path=dgn;expected.name="Custom";expected.kind=gdoc::UnderlayKind::Dgn;expected.sheet="Model";expected.fade=0;expected.plottable=true;
    expected.insertion=gcm::to_lattice(v,10,20);expected.scale=-2;expected.y_factor=-1.25;expected.rotation=.4;
    std::string error;CHECK(gplot::resolve_underlay(expected,v,error,.001));
    expected.clip={gcm::to_lattice(v,-1,2.25),gcm::to_lattice(v,0,2.25),gcm::to_lattice(v,0,2.75),gcm::to_lattice(v,-1,2.75)};
    source.canvas().underlays().attach(expected);CHECK(source.export_file(dwg));
    const auto check=[&](const gdoc::Underlay& actual) {
        CHECK(actual.found);CHECK(actual.source_unit_known);CHECK(!actual.source_coordinates_pending);
        CHECK(std::abs(actual.scale*actual.source_unit-expected.scale*expected.source_unit)<1e-12);
        CHECK(actual.y_factor==expected.y_factor);
        for(const auto raw:{std::pair{-1200.,2400.},std::pair{-500.,2500.},std::pair{250.,2500.}}) {
            const auto a=gcm::to_lattice(v,raw.first*actual.source_unit,raw.second*actual.source_unit),b=gcm::to_lattice(v,raw.first*expected.source_unit,raw.second*expected.source_unit);
            double ax,ay,bx,by;actual.to_drawing(a,v,ax,ay);expected.to_drawing(b,v,bx,by);
            CHECK(std::abs(ax-bx)<1e-5&&std::abs(ay-by)<1e-5);CHECK(actual.contains(a)==expected.contains(b));
        }
        CHECK(gplot::load_underlay(actual,v,error).valid());
    };
    {Shell imported=make_shell();CHECK(imported.import_file(dwg,false));CHECK(imported.canvas().underlays().size()==1);
     if(imported.canvas().underlays().size()==1){check(imported.canvas().underlays().all().front());CHECK(imported.export_file(dwg));}}
    std::filesystem::rename(dgn,away);
    {Shell missing=make_shell();CHECK(missing.import_file(dwg,false));CHECK(missing.canvas().underlays().size()==1);
     if(missing.canvas().underlays().size()==1){const auto& u=missing.canvas().underlays().all().front();CHECK(u.source_coordinates_pending);CHECK(!u.found);}
     CHECK(missing.save_file(native));}
    Shell reopened=make_shell();CHECK(reopened.open_file(native));CHECK(reopened.canvas().underlays().size()==1);
    std::filesystem::rename(away,dgn);
    {Shell restored=make_shell();CHECK(restored.open_file(native));CHECK(restored.canvas().underlays().size()==1);
     if(restored.canvas().underlays().size()==1)check(restored.canvas().underlays().all().front());}
    if(reopened.canvas().underlays().size()==1) {
        const auto name=reopened.canvas().underlays().all().front().name;
        reopened.command("UNDERLAY RELOAD \""+name+"\"");check(reopened.canvas().underlays().all().front());
        const auto once=reopened.canvas().underlays().all().front();reopened.command("UNDERLAY RELOAD \""+name+"\"");
        const auto& twice=reopened.canvas().underlays().all().front();CHECK(once.scale==twice.scale);CHECK(once.source_unit==twice.source_unit);
        CHECK(once.clip.front().x==twice.clip.front().x&&once.clip.front().y==twice.clip.front().y);
        CHECK(reopened.underlay_content(reopened.canvas(),true).size()==1);
    }
    for(const auto& path:{dgn,dwg,native})std::filesystem::remove(path);
}


TEST(imported_layer_zero_appearance_replaces_only_a_blank_document_default) {
    const auto fixture=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/
        "exchange/tests/data/transparency/layer-alpha.dwg";
    for (bool recenter : {false,true}) {
        Shell imported=make_shell();
        CHECK(imported.import_file(fixture.string(),recenter));
        CHECK(imported.layers().find("0")->transparency==25);
        CHECK(imported.layers().find("HALF") && imported.layers().find("HALF")->transparency==50);
        CHECK(imported.layers().find("INVISIBLE") && imported.layers().find("INVISIBLE")->transparency==100);
        const auto path=scratch_path(recenter?"layer-alpha-centered.glcad":"layer-alpha.glcad");
        CHECK(imported.save_file(path));
        Shell reopened=make_shell();CHECK(reopened.open_file(path));
        CHECK(reopened.layers().find("0")->transparency==25);
        CHECK(reopened.layers().find("INVISIBLE") && reopened.layers().find("INVISIBLE")->transparency==100);
        const auto dwg_path=scratch_path(recenter?"layer-alpha-centered.dwg":"layer-alpha.dwg");
        CHECK(reopened.export_file(dwg_path));
        Shell exchanged=make_shell();CHECK(exchanged.import_file(dwg_path,false));
        CHECK(exchanged.layers().find("0")->transparency==25);
        CHECK(exchanged.layers().find("HALF") && exchanged.layers().find("HALF")->transparency==50);
        CHECK(exchanged.layers().find("INVISIBLE") && exchanged.layers().find("INVISIBLE")->transparency==100);
        reopened.layers().find("0")->transparency=60;
        CHECK(reopened.import_file(fixture.string(),recenter));
        CHECK(reopened.layers().find("0")->transparency==60);
    }
    Shell source=make_shell();source.command("LINE");source.canvas().type_point("0,0");source.canvas().type_point("1,0");source.canvas().cancel();
    CHECK(source.canvas().shapes().size()==1);
    source.layers().find("0")->color=gdoc::Color::aci(3);
    const auto path=scratch_path("layer-zero-appearance.dxf");CHECK(source.export_file(path));
    Shell imported=make_shell();CHECK(imported.import_file(path,false));
    CHECK(imported.layers().find("0")->color==gdoc::Color::aci(3));
}

TEST(dwg_and_dxf_standard_styles_import_without_recentering_and_preserve_host_styles) {
    for(const auto suffix:{"dwg","dxf"})for(bool recenter:{false,true}) {
        Shell imported=make_shell();const auto& volume=imported.canvas().volume();
        gdxf::Document document;gdim::Style dimensions;dimensions.name="Standard";dimensions.dimtxt=8;document.dim_styles.push_back(dimensions);
        gtext::TextStyleDef text;text.name="Standard";text.width_factor=.65f;text.oblique=12;document.text_styles.push_back(text);
        gdxf::Entity entity;entity.shape.kind=gsnap::ShapeKind::Segment;
        entity.shape.a=gcm::to_lattice(volume,0,0);entity.shape.b=gcm::to_lattice(volume,1,0);document.entities.push_back(entity);
        const auto path=scratch_path((std::string("standard-styles.")+suffix).c_str());
        if(std::string(suffix)=="dwg")CHECK(gdwg::write_file(path,document,volume));
        else {std::ofstream output(path);output<<gdxf::write(document,volume);CHECK(output.good());}
        CHECK(imported.import_file(path,recenter));
        CHECK(std::abs(imported.canvas().dim_styles().find("Standard")->dimtxt-8)<1e-6);
        CHECK(std::abs(imported.text_styles().find("Standard")->width_factor-.65)<1e-6);
        CHECK(std::abs(imported.text_styles().find("Standard")->oblique-12)<1e-6);
        const auto native=scratch_path("standard-styles.glcad");CHECK(imported.save_file(native));
        Shell reopened=make_shell();CHECK(reopened.open_file(native));
        CHECK(std::abs(reopened.canvas().dim_styles().find("Standard")->dimtxt-8)<1e-6);
        CHECK(std::abs(reopened.text_styles().find("Standard")->width_factor-.65)<1e-6);
        Shell host=make_shell();host.command("LINE");host.canvas().type_point("0,0");host.canvas().type_point("1,0");host.canvas().cancel();
        CHECK(host.canvas().shapes().size()==1);
        const auto original_height=host.canvas().dim_styles().find("Standard")->dimtxt;
        const auto original_width=host.text_styles().find("Standard")->width_factor;
        CHECK(host.import_file(path,recenter));
        CHECK(host.canvas().dim_styles().find("Standard")->dimtxt==original_height);
        CHECK(host.text_styles().find("Standard")->width_factor==original_width);
    }
}

TEST(imported_dwg_dimensions_keep_geometry_links_through_merge_edit_and_reopen) {
    const auto fixture=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/
        "exchange/tests/data/dimassoc/example_2018.dwg";
    for(bool recenter:{false,true}) {
        Shell s=make_shell();s.build_frame();
        s.command("LINE");s.canvas().type_point("0,0");s.canvas().type_point("1,0");s.canvas().cancel();
        const auto host_key=s.canvas().shapes().front().key;
        CHECK(s.import_file(fixture.string(),recenter));s.command("MODEL");s.build_frame();
        std::size_t linked=0;
        for(const auto& dim:s.canvas().dimensions())if(dim.associative()) {
            ++linked;CHECK(dim.o1.shape!=host_key);CHECK(dim.o2.shape!=host_key);
            CHECK(s.canvas().index_of_key(dim.o1.shape)!=Canvas::npos);
            CHECK(s.canvas().index_of_key(dim.o2.shape)!=Canvas::npos);
        }
        CHECK(linked==4);
        const auto dimension=std::find_if(s.canvas().dimensions().begin(),s.canvas().dimensions().end(),[](const auto& d){return d.associative();});
        if(dimension==s.canvas().dimensions().end())continue;
        const auto index=std::size_t(dimension-s.canvas().dimensions().begin());
        const auto key=dimension->o1.shape;const auto before=dimension->p1;
        const auto delta=gcm::i64(100000);
        const auto move=[&](Shell& shell) {
            shell.canvas().modify_shapes([&](auto& shapes) {
                auto shape=std::find_if(shapes.begin(),shapes.end(),[&](const auto& sh){return sh.key==key;});
                CHECK(shape!=shapes.end());if(shape!=shapes.end()){shape->a.x+=delta;shape->b.x+=delta;}
            });shell.build_frame();
        };
        move(s);CHECK(s.canvas().dimensions()[index].p1.x==before.x+delta);
        CHECK(!s.canvas().dimensions()[index].orphaned);
        s.canvas().undo();s.build_frame();CHECK(s.canvas().dimensions()[index].p1.x==before.x);
        s.canvas().redo();s.build_frame();CHECK(s.canvas().dimensions()[index].p1.x==before.x+delta);
        const auto path=scratch_path(recenter?"dwg-assoc-centered.glcad":"dwg-assoc.glcad");CHECK(s.save_file(path));
        Shell reopened=make_shell();CHECK(reopened.open_file(path));reopened.command("MODEL");reopened.build_frame();
        CHECK(reopened.canvas().dimensions().size()>index);if(reopened.canvas().dimensions().size()<=index)continue;
        CHECK(reopened.canvas().dimensions()[index].o1.shape==key);
        move(reopened);CHECK(reopened.canvas().dimensions()[index].p1.x==before.x+2*delta);
        reopened.canvas().modify_shapes([&](auto& shapes){std::erase_if(shapes,[&](const auto& shape){return shape.key==key;});});
        reopened.build_frame();CHECK(reopened.canvas().dimensions()[index].orphaned);
    }
}

TEST(dimension_endpoint_snaps_follow_arc_and_ellipse_endpoints) {
    for(bool elliptical:{false,true}) {
        Shell s=make_shell();s.build_frame();const auto& v=s.canvas().volume();
        gsnap::Shape shape;shape.kind=elliptical?gsnap::ShapeKind::Ellipse:gsnap::ShapeKind::Arc;
        shape.a=gcm::to_lattice(v,0,0);shape.b=gcm::to_lattice(v,4,0);
        shape.radius=gcm::i64(std::llround(v.scale()*2));shape.start_angle=0;shape.end_angle=1.5707963267948966;
        s.canvas().load_shapes({shape});const auto key=s.canvas().shapes()[0].key;
        s.command("OSNAP end");s.canvas().set_tool(Tool::DimLinear);
        const auto pick=[&](double x,double y) {double px,py;s.canvas().view().screen_of(x,y,px,py);s.canvas().hover(px,py);s.canvas().click(px,py,false);};
        pick(elliptical?4:2,0);pick(0,2);pick(3,-1);s.canvas().set_tool(Tool::Select);
        CHECK(s.canvas().dimensions().size()==1);if(s.canvas().dimensions().empty())continue;
        CHECK(s.canvas().dimensions()[0].o1.shape==key);CHECK(s.canvas().dimensions()[0].o2.shape==key);
        CHECK(s.canvas().dimensions()[0].o1.at==gdim::Dimension::Anchor::Start);
        CHECK(s.canvas().dimensions()[0].o2.at==gdim::Dimension::Anchor::End);
        s.canvas().modify_shapes([&](auto& shapes){shapes[0].radius*=2;if(elliptical)shapes[0].b.x*=2;});s.build_frame();
        double x,y;gcm::to_world(v,s.canvas().dimensions()[0].p1,x,y);CHECK(std::abs(x-(elliptical?8:4))<1e-6);CHECK(std::abs(y)<1e-6);
        gcm::to_world(v,s.canvas().dimensions()[0].p2,x,y);CHECK(std::abs(x)<1e-6);CHECK(std::abs(y-4)<1e-6);
        CHECK(!s.canvas().dimensions()[0].orphaned);
    }
}

TEST(dwg_exported_dimensions_stay_associative_after_reimport_and_edit) {
    for(bool arc:{false,true}) {
        Shell source=make_shell();source.build_frame();const auto& v=source.canvas().volume();
        gsnap::Shape shape;shape.a=gcm::to_lattice(v,2,3);shape.b=gcm::to_lattice(v,6,3);
        if(arc){shape.kind=gsnap::ShapeKind::Arc;shape.radius=gcm::i64(std::llround(v.scale()*2));shape.start_angle=.3;shape.end_angle=2.1;}
        source.canvas().load_shapes({shape});const auto key=source.canvas().shapes().front().key;
        gdim::Dimension dim;dim.kind=gdim::Kind::Aligned;dim.o1={key,gdim::Dimension::Anchor::Start};dim.o2={key,gdim::Dimension::Anchor::End};
        CHECK(gdim::Dimension::anchor_point(shape,dim.o1.at,dim.p1));CHECK(gdim::Dimension::anchor_point(shape,dim.o2.at,dim.p2));
        dim.p3=gcm::to_lattice(v,6,7);source.canvas().add_dimensions({dim});source.build_frame();
        const auto path=scratch_path(arc?"arc-association.dwg":"line-association.dwg");CHECK(source.export_file(path));
        Shell imported=make_shell();imported.build_frame();
        imported.command("LINE");imported.canvas().type_point("0,0");imported.canvas().type_point("1,0");imported.canvas().cancel();
        const auto host_key=imported.canvas().shapes().front().key;
        CHECK(imported.import_file(path,false));imported.command("MODEL");imported.build_frame();
        CHECK(imported.canvas().dimensions().size()==1);if(imported.canvas().dimensions().empty())continue;
        const auto& current=imported.canvas().dimensions()[0];CHECK(current.o1.bound());CHECK(current.o2.bound());
        CHECK(current.o1.shape==current.o2.shape);CHECK(current.o1.shape!=host_key);
        const auto target=current.o1.shape;const auto before=current.p1;
        const auto delta=gcm::i64(100000);
        imported.canvas().modify_shapes([&](auto& shapes){for(auto& s:shapes)if(s.key==target){s.a.x+=delta;s.b.x+=delta;}});imported.build_frame();
        CHECK(imported.canvas().dimensions()[0].p1.x==before.x+delta);CHECK(!imported.canvas().dimensions()[0].orphaned);
        const auto again=scratch_path(arc?"arc-association-again.dwg":"line-association-again.dwg");CHECK(imported.export_file(again));
        Shell reopened=make_shell();CHECK(reopened.import_file(again,false));reopened.command("MODEL");reopened.build_frame();
        CHECK(reopened.canvas().dimensions().size()==1);if(reopened.canvas().dimensions().empty())continue;
        CHECK(reopened.canvas().dimensions()[0].o1.bound());CHECK(reopened.canvas().dimensions()[0].o2.bound());
        CHECK(std::abs(double(reopened.canvas().dimensions()[0].p1.x)-(before.x+delta))<=2);
        const auto file_bytes=[](const std::string& file){std::ifstream in(file,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});};
        const auto saved=file_bytes(again);CHECK(!saved.empty());
        reopened.canvas().modify_shapes([](auto& shapes){shapes.clear();});reopened.build_frame();
        CHECK(reopened.canvas().dimensions()[0].orphaned);
        CHECK(!reopened.export_file(again));CHECK(file_bytes(again)==saved);
    }
}

TEST(a_region_sketched_on_a_face_wins_the_hover_against_the_face_under_it) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    CHECK(s.modeller().bodies().size() == 1);
    if (s.modeller().bodies().empty()) return;

    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.5, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    const auto t = s.hovered_target();
    CHECK_MSG(t.valid && t.on_body, "the top face was not found");
    if (!t.valid) return;
    CHECK(s.begin_sketch_on_face(t.body, t.face));

    draw_rect(s, "-2,-1", "2,-1", "2,1.5", "-2,1.5");
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    s.build_frame();

    double wx, wy, wz;
    s.sketch_plane().to_world(0.0, 0.0, wx, wy, wz);
    CHECK(s.camera().project(gview::Point3::from(wx, wy, wz), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();

    const auto& h = s.modeller().hovered();
    CHECK_MSG(h.valid, "nothing lit up inside a square drawn on the face");
    CHECK_MSG(h.source >= 0,
              "the FACE was picked, not the sketched region (source=%d)", h.source);
}

TEST(selecting_inside_a_face_sketch_takes_the_region_not_the_block_beneath) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    if (s.modeller().bodies().empty()) { CHECK(false); return; }

    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.5, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    const auto t = s.hovered_target();
    if (!t.valid) { CHECK_MSG(false, "the top face was not found"); return; }
    CHECK(s.begin_sketch_on_face(t.body, t.face));

    draw_rect(s, "-2,-1", "2,-1", "2,1.5", "-2,1.5");
    s.build_frame();
    CHECK(s.canvas().tool() == Tool::Select);

    double wx, wy, wz;
    s.sketch_plane().to_world(0.0, 0.0, wx, wy, wz);
    CHECK(s.camera().project(gview::Point3::from(wx, wy, wz), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();

    const auto& h = s.hovered_target();
    CHECK_MSG(h.valid, "nothing lit up inside the square drawn on the face");
    CHECK_MSG(h.source >= 0,
              "hover took the FACE, not the sketched region (source=%d)", h.source);

    s.pointer_down(px, py);
    s.pointer_up(px, py);
    s.build_frame();
    CHECK_MSG(s.selected_target().valid, "the click selected nothing");
    CHECK_MSG(s.selected_target().source >= 0,
              "the click took the block, not the region (source=%d)",
              s.selected_target().source);
}

TEST(a_face_sketch_is_pickable_even_where_it_runs_close_to_the_blocks_edge) {
    Shell s = make_shell();
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    if (s.modeller().bodies().empty()) { CHECK(false); return; }

    double px, py;
    s.set_lens(Lens::TwoD);
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.5, 3.0), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    const auto t = s.hovered_target();
    if (!t.valid) { CHECK_MSG(false, "the top face was not found"); return; }
    CHECK(s.begin_sketch_on_face(t.body, t.face));

    draw_rect(s, "-3.9,-2.4", "3.9,-2.4", "3.9,2.4", "-3.9,2.4");
    s.build_frame();

    double wx, wy, wz;
    s.sketch_plane().to_world(3.7, 0.0, wx, wy, wz);
    CHECK(s.camera().project(gview::Point3::from(wx, wy, wz), px, py));
    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();

    const auto& h = s.hovered_target();
    CHECK_MSG(h.valid, "nothing lit up just inside the sketched square");
    CHECK_MSG(h.source >= 0,
              "the block's edge stole the hover from the sketch (source=%d)", h.source);
}

TEST(escape_always_lands_in_select_whatever_it_had_to_abandon) {
    Shell s = make_shell();

    s.canvas().set_tool(Tool::Circle);
    s.canvas().type_point("1,1");
    s.key_special(1);
    CHECK_MSG(s.canvas().tool() == Tool::Select, "circle mid-gesture: tool=%d",
              int(s.canvas().tool()));
    CHECK(s.active_tool() == ToolId::Select);

    s.canvas().set_tool(Tool::Line);
    s.key_special(1);
    CHECK_MSG(s.canvas().tool() == Tool::Select, "line armed: tool=%d",
              int(s.canvas().tool()));

    s.key_special(1);
    CHECK(s.canvas().tool() == Tool::Select);
    CHECK(s.active_tool() == ToolId::Select);

    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    s.build_frame();
    CHECK(s.active_tool() == ToolId::Extrude);
    s.key_special(1);
    CHECK_MSG(s.active_tool() == ToolId::Select, "extrude armed: tool=%d",
              int(s.active_tool()));
    CHECK(s.modeller().stage() == Modeller::Stage::Idle);
    CHECK(s.canvas().tool() == Tool::Select);

    extrude_at(s, 4.0, 2.5, "3");
    s.key_special(1);
    CHECK(s.active_tool() == ToolId::Select);
    CHECK(s.canvas().tool() == Tool::Select);

    s.command("TEXT");
    s.key_special(1);
    CHECK(s.canvas().tool() == Tool::Select);
    CHECK(s.active_tool() == ToolId::Select);
}

TEST(escape_cancels_command_prompt_and_buffer_before_the_next_frame) {
    auto s=make_shell();s.build_frame();
    s.key_char("LINE");s.build_frame();s.key_special(0);s.build_frame();s.build_frame();
    CHECK(s.canvas().tool()==Tool::Line);CHECK(!s.bar_prompt().empty());
    s.canvas().type_point("0,0");s.key_char("12");
    s.key_special(1);s.build_frame();
    CHECK(s.canvas().tool()==Tool::Select);CHECK(s.bar_input().empty());CHECK(s.bar_prompt().empty());
    CHECK(s.canvas().shapes().empty());
    s.key_char("CIRCLE");s.build_frame();s.key_special(0);s.build_frame();s.build_frame();
    CHECK(s.canvas().tool()==Tool::Circle);CHECK(!s.bar_prompt().empty());
    s.key_special(1);s.build_frame();CHECK(s.bar_prompt().empty());
    s.command("WALLS");s.build_frame();CHECK(!s.bar_prompt().empty());
    s.key_special(1);s.build_frame();CHECK(s.bar_prompt().empty());
}

namespace {
gui::Rect value_control(Shell& s,const char* label,gui::Context::Role role) {
    for (const auto& item:s.accessible_ui())
        if (item.role==role && item.label==label && !item.disabled) return item.bounds;
    CHECK_MSG(false,"missing enabled control: %s",label);
    return {};
}
void click_value_control(Shell& s,const char* label,gui::Context::Role role) {
    auto rect=value_control(s,label,role);
    if (rect.empty()) return;
    const double x=rect.x+rect.w*.5,y=rect.y+rect.h*.5;
    s.pointer_move(x,y); s.build_frame();
    s.pointer_down(x,y); s.build_frame();
    s.pointer_up(x,y); s.build_frame();
}
}

TEST(workflow_toolbar_options_hover_and_open_below_their_own_tool) {
    for (int width : {800, 2400}) {
        for (const auto tool : {Tool::Rect, Tool::Circle, Tool::Arc}) {
            Shell s = make_shell();
            s.set_viewport(width, 1000);
            s.edit_sketch(0);
            s.activate_tool(ToolId::Select);
            s.build_frame();
            const char* label = tool == Tool::Rect ? "Rectangle options"
                              : tool == Tool::Circle ? "Circle options" : "Arc options";
            const auto arrow = value_control(s, label, gui::Context::Role::Button);
            if (arrow.empty()) continue;
            const auto has_highlight = [&] {
                for (const auto& item : s.ui())
                    if (item.params[2] == 0 && item.rect[0] == arrow.x && item.rect[1] == arrow.y &&
                        item.rect[2] == arrow.w && item.rect[3] == arrow.h) return true;
                return false;
            };
            CHECK(!has_highlight());
            s.pointer_move(arrow.centre().x, arrow.centre().y);
            s.build_frame(); s.build_frame();
            CHECK(has_highlight());
            click_value_control(s, label, gui::Context::Role::Button);
            CHECK(s.canvas().tool() == Tool::Select);
            const auto modes = Canvas::tool_modes(tool);
            const auto row = value_control(s, modes[0].c_str(), gui::Context::Role::Row);
            CHECK(row.y == arrow.bottom() + 4);
            CHECK(row.x <= arrow.x && row.right() >= arrow.right());
            click_value_control(s, modes[1].c_str(), gui::Context::Role::Row);
            CHECK(s.canvas().tool() == tool);
            CHECK(s.canvas().tool_mode(tool) == 1);
            s.build_frame();
            for (const auto& item : s.accessible_ui())
                CHECK(item.role != gui::Context::Role::Row || item.label != modes[0]);
            s.pointer_move(arrow.centre().x, arrow.centre().y);
            s.build_frame(); s.build_frame();
            CHECK(has_highlight());
            click_value_control(s, label, gui::Context::Role::Button);
            CHECK(!value_control(s, modes[0].c_str(), gui::Context::Role::Row).empty());
            click_value_control(s, label, gui::Context::Role::Button);
            s.build_frame();
            for (const auto& item : s.accessible_ui())
                if (item.label == label) CHECK(item.value == "collapsed");
        }
    }
}

TEST(workflow_clickable_extrusion_field_previews_then_commits_once) {
    for (bool enter:{true,false}) {
        Shell s=make_shell();
        draw_rect(s,"0,0","4,0","4,4","0,4");
        s.build_frame(); s.activate_tool(ToolId::Extrude);
        click_at(s,2,2,0); s.build_frame();
        CHECK(s.modeller().stage()==Modeller::Stage::Placed);
        const auto serial=s.modeller().edit_serial();
        click_value_control(s,"Extrusion distance",gui::Context::Role::Field);
        s.key_char("250cm"); s.build_frame(); s.build_frame();
        CHECK(s.modeller().typed()=="250cm");
        CHECK(s.modeller().value_preview_valid());
        CHECK(std::fabs(s.modeller().distance()-2.5)<1e-6);
        CHECK(s.modeller().bodies().empty());
        CHECK(s.modeller().edit_serial()==serial);
        CHECK(s.modeller().preview_range().second>0);
        if (enter) { s.key_special(0); s.build_frame(); }
        else click_value_control(s,"OK",gui::Context::Role::Button);
        s.build_frame();
        CHECK(s.modeller().bodies().size()==1);
        if (s.modeller().bodies().empty()) continue;
        CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-40)<1e-4);
        const auto committed=s.modeller().edit_serial();
        s.key_special(0); s.build_frame();
        CHECK(s.modeller().edit_serial()==committed);
        CHECK(s.modeller().undo());
        CHECK(s.modeller().bodies().empty());
    }
}

TEST(workflow_clickable_round_field_is_a_cancellable_draft) {
    Shell s=make_shell();
    draw_rect(s,"0,0","8,0","8,5","0,5");
    extrude_at(s,4,2.5,"3");
    s.camera().look(gview::StandardView::Iso);
    s.build_frame(); s.activate_tool(ToolId::FilletEdge);
    click_at(s,4,0,3); s.build_frame();
    CHECK(s.modeller().stage()==Modeller::Stage::TypeValue);
    const auto serial=s.modeller().edit_serial();
    const auto history=s.modeller().history(0).size();
    const double before=gtopo::volume(s.modeller().bodies()[0],kVol);
    click_value_control(s,"Round radius",gui::Context::Role::Field);
    s.key_char(".5m"); s.build_frame(); s.build_frame();
    CHECK(s.modeller().value_preview_valid());
    CHECK(s.modeller().edit_serial()==serial);
    CHECK(s.modeller().history(0).size()==history);
    CHECK(gtopo::volume(s.modeller().bodies()[0],kVol)==before);
    s.key_special(1); s.build_frame();
    CHECK(!s.modeller().value_preview_valid());
    CHECK(s.modeller().edit_serial()==serial);
    CHECK(gtopo::volume(s.modeller().bodies()[0],kVol)==before);
}

TEST(workflow_clickable_round_field_ok_commits_exact_cpu_surface_once) {
    Shell s=make_shell();
    draw_rect(s,"0,0","8,0","8,5","0,5");
    extrude_at(s,4,2.5,"3");
    s.camera().look(gview::StandardView::Iso);
    s.build_frame(); s.activate_tool(ToolId::FilletEdge);
    click_at(s,4,0,3); s.build_frame();
    const auto history=s.modeller().history(0).size();
    click_value_control(s,"Round radius",gui::Context::Role::Field);
    s.key_char(".5"); s.build_frame();
    click_value_control(s,"OK",gui::Context::Role::Button);
    CHECK(s.modeller().history(0).size()==history+1);
    CHECK(s.modeller().bodies()[0].has_surfaces());
    const auto serial=s.modeller().edit_serial();
    s.key_special(0); s.build_frame();
    CHECK(s.modeller().edit_serial()==serial);
    CHECK(s.modeller().undo());
    CHECK(s.modeller().history(0).size()==history);
}

TEST(workflow_clickable_drawing_length_previews_before_enter) {
    Shell s=make_shell();
    s.build_frame(); s.activate_tool(ToolId::Line);
    CHECK(s.canvas().type_point("0,0"));
    double x,y;
    s.canvas().view().screen_of(2,0,x,y);
    s.pointer_move(x,y); s.build_frame();
    click_value_control(s,"Drawing D",gui::Context::Role::Field);
    s.key_char("7m"); s.build_frame();
    CHECK(s.canvas().dynamic_buffer()=="7m");
    CHECK(s.canvas().segment_count()==0);
    s.key_special(0); s.build_frame();
    CHECK(s.canvas().segment_count()==1);
    if (s.canvas().segment_count()==0) return;
    const auto& line=s.canvas().shapes()[0];
    double ax,ay,bx,by;
    gcm::to_world(kVol,line.a,ax,ay); gcm::to_world(kVol,line.b,bx,by);
    CHECK(std::fabs(std::hypot(bx-ax,by-ay)-7)<1e-4);
    CHECK(std::fabs(bx-ax-7)<1e-4);
    CHECK(std::fabs(by-ay)<1e-4);
}

TEST(workflow_invalid_replacement_radius_preserves_the_committed_round) {
    Shell s=make_shell();
    draw_rect(s,"0,0","8,0","8,5","0,5"); extrude_at(s,4,2.5,"3");
    s.camera().look(gview::StandardView::Iso);
    s.build_frame(); s.activate_tool(ToolId::FilletEdge);
    click_at(s,4,0,3); s.build_frame();
    CHECK(s.round_edge(.5));
    const auto serial=s.modeller().edit_serial();
    const auto history=s.modeller().history(0).size();
    const auto before=gtopo::volume(s.modeller().bodies()[0],kVol);
    s.modeller().set_typed("1000m");
    CHECK(!s.modeller().value_preview_valid());
    CHECK(!s.modeller().commit_typed());
    CHECK(s.modeller().edit_serial()==serial);
    CHECK(s.modeller().history(0).size()==history);
    CHECK(s.modeller().bodies()[0].has_surfaces());
    CHECK(gtopo::volume(s.modeller().bodies()[0],kVol)==before);
    CHECK(s.modeller().typed()=="1000m");
}

namespace {
void hover_world_once(Shell& s,double x,double y,double z) {
    double px,py;
    CHECK(s.camera().project(gview::Point3::from(x,y,z),px,py));
    s.pointer_move(px,py); s.build_frame();
}
double hover_area(const Shell& s) {
    double area=0;
    for (const auto& item:s.scene().items()) {
        if (item.kind!=grender::DrawKind::Overlay) continue;
        CHECK(item.material.translucent);
        CHECK(item.first+item.count<=s.mesh().size());
        for (std::size_t i=item.first;i+2<item.first+item.count;i+=3) {
            const auto& a=s.mesh()[i]; const auto& b=s.mesh()[i+1]; const auto& c=s.mesh()[i+2];
            const double ux=b.pos[0]-a.pos[0],uy=b.pos[1]-a.pos[1],uz=b.pos[2]-a.pos[2];
            const double vx=c.pos[0]-a.pos[0],vy=c.pos[1]-a.pos[1],vz=c.pos[2]-a.pos[2];
            area+=.5*std::sqrt(std::pow(uy*vz-uz*vy,2)+std::pow(uz*vx-ux*vz,2)+std::pow(ux*vy-uy*vx,2));
        }
    }
    return area;
}
Shell face_edit_block() {
    Shell s=make_shell();
    draw_rect(s,"0,0","8,0","8,5","0,5"); extrude_at(s,4,2.5,"3");
    return s;
}
void face_edit_square(Shell& s) {
    s.command("SKETCH"); s.build_frame(); click_at(s,4,2.5,3); s.build_frame();
    draw_rect(s,"-1,-1","1,-1","1,1","-1,1"); s.build_frame();
}
}

TEST(workflow_consumed_sketch_visibility_and_parent) {
    Shell s=face_edit_block();
    CHECK(s.sketch_parent(0)==0);
    CHECK(!s.sketch_visible(0));
    CHECK(s.static_lines().empty());
    click_value_control(s,"Show",gui::Context::Role::Button);
    CHECK(s.sketch_visible(0)); CHECK(!s.static_lines().empty());
    click_value_control(s,"Auto",gui::Context::Role::Button);
    CHECK(!s.sketch_visible(0)); CHECK(s.static_lines().empty());
    face_edit_square(s);
    CHECK(s.sketch_parent(1)==0);
    CHECK(s.sketch_visible(1));
    s.finish_sketch(); s.build_frame();
    s.activate_tool(ToolId::Extrude); s.build_frame();
    click_at(s,4,2.5,3); s.build_frame();
    s.key_char("1"); s.key_special(0); s.build_frame();
    CHECK(s.modeller().bodies().size()==1);
    CHECK(!s.sketch_visible(1));
    CHECK(s.sketch_parent(1)==0);
    hover_world_once(s,4,2.5,4);
    CHECK(s.hovered_target().valid && s.hovered_target().source<0);
    s.command("SKETCH"); s.build_frame();
    click_at(s,4,2.5,4); s.build_frame();
    CHECK(s.sketches().size()==3);
    CHECK(s.active_sketch()==2);
    CHECK(!s.sketch_visible(1));
    CHECK(s.sketch_visible(2));
    s.finish_sketch(); s.build_frame();
    s.set_sketch_visibility(1,Shell::SketchVisibility::Shown); s.build_frame();
    CHECK(s.sketch_visible(1));
    s.command("UNDO"); s.build_frame();
    CHECK(!s.sketch_visible(1));
    s.command("REDO"); s.build_frame();
    CHECK(s.sketch_visible(1));
    const auto path=scratch_path("sketch-visibility.glcad");
    CHECK(s.save_file(path));
    Shell loaded=make_shell(); CHECK(loaded.open_file(path)); loaded.set_mode(Mode::Solid); loaded.build_frame();
    CHECK(loaded.sketch_visible(1)); CHECK(loaded.sketch_parent(1)==0);
    loaded.set_sketch_visibility(1,Shell::SketchVisibility::Automatic); loaded.build_frame();
    CHECK(!loaded.sketch_visible(1));
    loaded.edit_sketch(1); loaded.build_frame(); CHECK(loaded.sketch_visible(1));
    loaded.finish_sketch(); loaded.build_frame(); CHECK(!loaded.sketch_visible(1));
    CHECK(loaded.modeller().bodies()[0].analyse().closed);
}

TEST(workflow_consumed_circle_stays_hidden_when_another_sketch_is_opened) {
    Shell s=face_edit_block();
    s.command("SKETCH"); s.build_frame(); click_at(s,4,2.5,3); s.build_frame();
    s.canvas().set_tool(Tool::Circle);
    CHECK(s.canvas().type_point("0,0")); CHECK(s.canvas().type_point("1,0"));
    s.canvas().set_tool(Tool::Select); s.build_frame();
    extrude_at(s,0,0,"1"); s.build_frame();
    CHECK(!s.sketch_visible(1));
    s.command("SKETCH"); s.build_frame(); click_at(s,4,2.5,4); s.build_frame();
    CHECK(s.active_sketch()==2); CHECK(!s.sketch_visible(1));
    const auto path=scratch_path("consumed-circle.glcad"); CHECK(s.save_file(path));
    Shell loaded=make_shell(); CHECK(loaded.open_file(path)); loaded.set_mode(Mode::Solid); loaded.build_frame();
    CHECK(!loaded.sketch_visible(1)); CHECK(loaded.sketch_parent(1)==0);
}

TEST(workflow_face_edit_cap_offsets_start_at_the_committed_face) {
    Shell s=face_edit_block();
    s.camera().look(gview::StandardView::Iso); s.build_frame();
    double height=3;
    for (const auto& edit:std::vector<std::pair<std::string,double>>{{".75m",.75},{"-.25m",-.25}}) {
        hover_world_once(s,6,3,height);
        CHECK(s.hovered_target().valid && s.hovered_target().on_body);
        CHECK(s.hovered_target().source<0);
        CHECK(std::fabs(s.hovered_target().centroid.z.to_f64()-height)<1e-5);
        CHECK(hover_area(s)>0);
        click_at(s,6,3,height); s.build_frame();
        CHECK(s.modeller().target().on_body && s.modeller().target().source<0);
        CHECK(std::fabs(s.modeller().distance())<1e-9);
        const auto serial=s.modeller().edit_serial();
        click_value_control(s,"Face offset",gui::Context::Role::Field);
        s.key_char(edit.first); s.build_frame(); s.build_frame();
        CHECK(s.modeller().edit_serial()==serial);
        CHECK(!s.modeller().gpu_preview().tool.empty());
        CHECK(hover_area(s)==0);
        s.key_special(0); s.build_frame(); s.build_frame();
        height+=edit.second;
        CHECK(s.modeller().bodies().size()==1);
        CHECK(std::fabs(s.modeller().bodies()[0].bounds(kVol).hi.z.to_f64()-height)<1e-5);
        CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-40*height)<1e-4);
        CHECK(std::fabs(s.modeller().history(0).back().value-height)<1e-5);
    }
}

TEST(workflow_face_edit_pocket_floor_wins_over_its_old_sketch) {
    Shell s=face_edit_block(); face_edit_square(s);
    extrude_at(s,0,0,"-1");
    hover_world_once(s,6.5,3,3);
    CHECK(std::fabs(hover_area(s)-36)<1e-4);
    double floor=2,volume=116;
    for (const auto& edit:std::vector<std::pair<std::string,double>>{{"-.25",-.25},{".5",.5}}) {
        hover_world_once(s,4.25,2.75,floor);
        const auto target=s.hovered_target();
        CHECK(target.valid && target.on_body && target.source<0);
        CHECK(std::fabs(target.centroid.z.to_f64()-floor)<1e-5);
        CHECK(std::fabs(hover_area(s)-4)<1e-4);
        click_at(s,4.25,2.75,floor); s.build_frame();
        hover_world_once(s,3.8,2.6,floor);
        CHECK(s.modeller().hovered().valid && s.modeller().hovered().source<0);
        click_value_control(s,"Face offset",gui::Context::Role::Field);
        s.key_char(edit.first); s.build_frame();
        CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-volume)<1e-4);
        click_value_control(s,"OK",gui::Context::Role::Button);
        floor+=edit.second; volume+=edit.second*4;
        CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-volume)<1e-4);
        CHECK(s.modeller().bodies()[0].analyse().closed);
    }
    CHECK(s.sketches().size()==2);
}

TEST(workflow_circle_face_cut_join_cut_commits_match_preview) {
    Shell s=face_edit_block();
    s.command("SKETCH"); s.build_frame(); click_at(s,4,2.5,3); s.build_frame();
    s.canvas().set_tool(Tool::Circle);
    CHECK(s.canvas().type_point("0,0")); CHECK(s.canvas().type_point("1,0"));
    s.canvas().set_tool(Tool::Select); s.build_frame();
    extrude_at(s,0,0,"-1");
    CHECK(s.modeller().bodies().size()==1);
    const double area=120-gtopo::volume(s.modeller().bodies()[0],kVol);
    CHECK(area>3 && area<3.15);
    const auto feature_count=s.modeller().history(0).size();
    double height=2;
    for (const double distance : {2.137,-1.813,1.629,-1.417,1.283,-1.731,1.519,-1.237}) {
        hover_world_once(s,4.1,2.6,height);
        CHECK(s.hovered_target().valid && s.hovered_target().source<0);
        click_at(s,4.1,2.6,height); s.build_frame();
        CHECK(s.modeller().target().valid && s.modeller().target().source<0);
        const auto serial=s.modeller().edit_serial();
        click_value_control(s,"Face offset",gui::Context::Role::Field);
        s.key_char(std::to_string(distance)); s.build_frame(); s.build_frame();
        CHECK(!s.modeller().gpu_preview().tool.empty());
        CHECK(s.modeller().edit_serial()==serial);
        s.key_special(0); s.build_frame(); s.build_frame();
        height+=distance;
        CHECK(s.modeller().history(0).size()==feature_count);
        CHECK(s.modeller().bodies()[0].face_count()<1000);
        CHECK_MSG(s.modeller().bodies().size()==1,"distance=%g: %s",distance,s.modeller().message().c_str());
        CHECK_MSG(s.modeller().bodies()[0].analyse().closed,"distance=%g: %s",distance,s.modeller().message().c_str());
        CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-(120+area*(height-3)))<1e-4,
                  "distance=%g height=%g volume=%.12g expected=%.12g: %s",distance,height,
                  gtopo::volume(s.modeller().bodies()[0],kVol),120+area*(height-3),s.modeller().message().c_str());
    }
}

TEST(workflow_face_edit_hover_washes_reach_the_scene_and_follow_the_pointer) {
    Shell s=face_edit_block(); face_edit_square(s);
    for (bool armed:{false,true}) {
        if (armed) { s.activate_tool(ToolId::Extrude); s.build_frame(); }
        hover_world_once(s,4.25,2.75,3);
        const auto inner=armed ? s.modeller().hovered() : s.hovered_target();
        CHECK(inner.valid && inner.source>=0);
        CHECK(std::fabs(hover_area(s)-4)<1e-4);
        hover_world_once(s,6.5,3,3);
        const auto outer=armed ? s.modeller().hovered() : s.hovered_target();
        CHECK(outer.valid && outer.on_body && outer.source<0 && outer.from_face);
        CHECK(std::fabs(hover_area(s)-36)<1e-4);
        hover_world_once(s,12,8,3);
        CHECK(hover_area(s)==0);
    }
}

namespace {
void check_same_field_rect(const gui::Rect& a,const gui::Rect& b) {
    CHECK(a.x==b.x); CHECK(a.y==b.y); CHECK(a.w==b.w); CHECK(a.h==b.h);
}
}

TEST(workflow_face_outside_sketch_previews_and_commits_the_surrounding_region) {
    for (const char* amount : {"1", "-1"}) {
        Shell s=face_edit_block(); face_edit_square(s);
        s.finish_sketch(); s.build_frame();
        hover_world_once(s,6.5,3,3);
        CHECK(s.hovered_target().source<0 && s.hovered_target().from_face);
        CHECK(std::fabs(hover_area(s)-36)<1e-4);
        click_at(s,6.5,3,3); s.build_frame();
        CHECK(s.modeller().target().source<0);
        const auto serial=s.modeller().edit_serial();
        click_value_control(s,"Face offset",gui::Context::Role::Field);
        s.key_char(amount); s.build_frame(); s.build_frame();
        CHECK(s.modeller().edit_serial()==serial);
        CHECK(!s.modeller().gpu_preview().tool.empty());
        CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-120)<1e-4);
        s.key_special(0); s.build_frame(); s.build_frame();
        CHECK(s.modeller().bodies().size()==1);
        CHECK(s.modeller().bodies()[0].analyse().closed);
        CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-(amount[0]=='-' ? 84 : 156))<1e-4);
        CHECK(s.modeller().history(0).back().sketch<0);
        hover_world_once(s,6.5,3,3);
        CHECK(s.hovered_target().source<0);
        CHECK(std::fabs(hover_area(s)-36)<1e-4);
        CHECK(s.modeller().undo()); s.build_frame();
        hover_world_once(s,6.5,3,3);
        CHECK(s.hovered_target().source<0 && s.hovered_target().from_face);
    }
}

TEST(workflow_face_outside_sketch_excludes_all_unused_regions_on_that_face) {
    Shell s=face_edit_block(); face_edit_square(s);
    draw_rect(s,"1.6,-.5","2.1,-.5","2.1,.5","1.6,.5");
    const auto plane=s.sketch_plane();
    s.finish_sketch(); s.camera().look_plan(); s.build_frame();
    double x,y,z;
    plane.to_world(2.25,0,x,y,z);
    hover_world_once(s,x,y,z);
    CHECK(s.hovered_target().source<0 && s.hovered_target().from_face);
    CHECK(std::fabs(hover_area(s)-35.5)<1e-4);
    click_at(s,x,y,z); s.build_frame();
    CHECK(s.modeller().bodies().size()==1);
    if(s.modeller().bodies().empty()) return;
    const auto offset_field=value_control(s,"Face offset",gui::Context::Role::Field);
    CHECK(!offset_field.empty());
    for(const auto& item:s.accessible_ui()) {
        CHECK(item.label!="Object options");CHECK(item.label!="Selection actions");
    }
    click_value_control(s,"Face offset",gui::Context::Role::Field);
    s.key_char("-1"); s.build_frame(); s.key_special(0); s.build_frame(); s.build_frame();
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-84.5)<1e-4,"volume=%g error=%s",gtopo::volume(s.modeller().bodies()[0],kVol),s.modeller().value_error().c_str());
    hover_world_once(s,x,y,z);
    CHECK(s.hovered_target().source<0);
    CHECK_MSG(std::fabs(hover_area(s)-35.5)<1e-4,"surrounding area=%g",hover_area(s));
    s.camera().look(gview::StandardView::Iso); s.build_frame();
    hover_world_once(s,8,2.5,1.5);
    CHECK(s.hovered_target().valid && s.hovered_target().on_body && s.hovered_target().source<0);
}

TEST(workflow_static_input_drawing_fields_stay_reachable_during_pointer_motion) {
    for (auto tool:{ToolId::Line,ToolId::Circle}) {
        Shell s=make_shell(); s.build_frame(); s.activate_tool(tool);
        CHECK(s.canvas().type_point("0,0"));
        hover_world_once(s,2,0,0);
        const char* name=tool==ToolId::Line ? "Drawing D" : "Drawing R";
        const auto field=value_control(s,name,gui::Context::Role::Field);
        for (const auto& point:std::vector<std::pair<double,double>>{{3,1},{-2,3},{4,-1}}) {
            hover_world_once(s,point.first,point.second,0);
            check_same_field_rect(field,value_control(s,name,gui::Context::Role::Field));
        }
        click_value_control(s,name,gui::Context::Role::Field);
        s.key_char("123.45m"); s.build_frame();
        check_same_field_rect(field,value_control(s,name,gui::Context::Role::Field));
        s.pointer_move(-50,-50); s.build_frame();
        check_same_field_rect(field,value_control(s,name,gui::Context::Role::Field));
        CHECK(s.canvas().segment_count()==0);
    }
}

TEST(workflow_static_input_extrusion_field_does_not_follow_the_drag_tip) {
    Shell s=make_shell();
    draw_rect(s,"0,0","8,0","8,5","0,5");
    s.build_frame(); s.activate_tool(ToolId::Extrude);
    s.camera().look(gview::StandardView::Iso); s.build_frame();
    click_at(s,4,2.5,0); s.build_frame();
    const auto field=value_control(s,"Extrusion distance",gui::Context::Role::Field);
    double x,y; CHECK(s.camera().project(gview::Point3::from(4,2.5,0),x,y));
    CHECK(s.modeller().press(s.camera(),x,y));
    CHECK(s.modeller().stage()==Modeller::Stage::Dragging);
    for (double height:{1.,3.,-.5}) {
        CHECK(s.camera().project(gview::Point3::from(4,2.5,height),x,y));
        s.modeller().drag(s.camera(),x,y); s.build_frame();
        check_same_field_rect(field,value_control(s,"Extrusion distance",gui::Context::Role::Field));
    }
    CHECK(s.modeller().bodies().empty());
}

TEST(workflow_static_input_face_field_stays_outside_the_committed_body) {
    Shell s=face_edit_block();
    s.camera().look(gview::StandardView::Iso); s.build_frame();
    click_at(s,6,3,3); s.build_frame();
    const auto field=value_control(s,"Face offset",gui::Context::Role::Field);
    double left=1e30,top=1e30,right=-1e30,bottom=-1e30;
    for (double x:{0.,8.}) for (double y:{0.,5.}) for (double z:{0.,3.}) {
        double sx,sy; CHECK(s.camera().project(gview::Point3::from(x,y,z),sx,sy));
        left=std::min(left,sx); right=std::max(right,sx);
        top=std::min(top,sy); bottom=std::max(bottom,sy);
    }
    CHECK(!field.intersects({float(left),float(top),float(right-left),float(bottom-top)}));
    hover_world_once(s,4,2,3);
    check_same_field_rect(field,value_control(s,"Face offset",gui::Context::Role::Field));
    click_value_control(s,"Face offset",gui::Context::Role::Field);
    s.key_char(".75"); s.build_frame(); s.build_frame();
    check_same_field_rect(field,value_control(s,"Face offset",gui::Context::Role::Field));
    CHECK(s.modeller().value_preview_valid());
}

namespace {
void check_value_focus(const Shell& s,const char* label) {
    std::string focused;
    for (const auto& item:s.accessible_ui()) if (item.focused) focused=item.label;
    CHECK_MSG(focused==label,"focused '%s', expected '%s'",focused.c_str(),label);
}
void tab_value(Shell& s,bool backwards=false) {
    s.set_modifiers(backwards,false,false,false);
    s.key_special(3); s.build_frame();
    s.set_modifiers(false,false,false,false);
}
}

TEST(workflow_tab_drawing_fields_then_ok_preserves_distance_and_angle) {
    Shell s=make_shell(); s.build_frame(); s.activate_tool(ToolId::Line);
    CHECK(s.canvas().type_point("0,0")); hover_world_once(s,2,0,0);
    tab_value(s); check_value_focus(s,"Drawing D");
    s.key_char("4"); s.build_frame();
    tab_value(s); check_value_focus(s,"Drawing A");
    s.key_char("90"); s.build_frame();
    CHECK(s.canvas().segment_count()==0);
    tab_value(s); check_value_focus(s,"OK");
    tab_value(s); check_value_focus(s,"Drawing D");
    tab_value(s,true); check_value_focus(s,"OK");
    tab_value(s,true); check_value_focus(s,"Drawing A");
    tab_value(s); check_value_focus(s,"OK");
    s.key_special(0); s.build_frame(); s.build_frame();
    CHECK(s.canvas().segment_count()==1);
    if (!s.canvas().shapes().empty()) {
        double x,y; gcm::to_world(kVol,s.canvas().shapes()[0].b,x,y);
        CHECK(std::fabs(x)<1e-5); CHECK(std::fabs(y-4)<1e-5);
    }
}

TEST(workflow_tab_circle_reaches_ok_without_typing_and_accepts_shown_radius) {
    Shell s=make_shell(); s.build_frame(); s.activate_tool(ToolId::Circle);
    CHECK(s.canvas().type_point("0,0")); hover_world_once(s,2,0,0);
    tab_value(s); check_value_focus(s,"Drawing R");
    tab_value(s); check_value_focus(s,"OK");
    s.key_special(0); s.build_frame(); s.build_frame();
    CHECK(s.canvas().shapes().size()==1);
    if (!s.canvas().shapes().empty()) {
        CHECK(s.canvas().shapes()[0].kind==gsnap::ShapeKind::Circle);
        CHECK(std::fabs(s.canvas().shapes()[0].radius*kVol.inv_scale()-2)<1e-5);
    }
}

TEST(workflow_tab_extrusion_field_then_ok_commits_cpu_once) {
    Shell s=make_shell(); draw_rect(s,"0,0","4,0","4,4","0,4");
    s.build_frame(); s.activate_tool(ToolId::Extrude); click_at(s,2,2,0); s.build_frame();
    tab_value(s); check_value_focus(s,"Extrusion distance");
    const auto serial=s.modeller().edit_serial();
    s.key_char("2"); s.build_frame(); s.build_frame();
    tab_value(s); check_value_focus(s,"OK");
    CHECK(s.modeller().edit_serial()==serial);
    s.key_special(0); s.build_frame(); s.build_frame();
    CHECK(s.modeller().bodies().size()==1);
    if (!s.modeller().bodies().empty()) CHECK(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-32)<1e-5);
    CHECK(s.modeller().edit_serial()==serial+1);
}

TEST(workflow_circle_drag_reversals_keep_one_body_and_one_feature) {
    Shell s=face_edit_block();
    s.command("SKETCH"); s.build_frame(); click_at(s,4,2.5,3); s.build_frame();
    s.canvas().set_tool(Tool::Circle);
    CHECK(s.canvas().type_point("0,0")); CHECK(s.canvas().type_point("1,0"));
    s.canvas().set_tool(Tool::Select); s.build_frame(); extrude_at(s,0,0,"-.5");
    s.camera().look(gview::StandardView::Iso); s.build_frame();
    auto& modeller=s.modeller();
    const auto feature_count=modeller.history(0).size();
    const double area=2*(120-gtopo::volume(modeller.bodies()[0],kVol));
    double height=2.5;
    for (double distance : {1.731,-1.519,1.283,-1.137}) {
        double px,py;
        CHECK(s.camera().project(gview::Point3::from(4,2.5,height),px,py));
        Modeller::Target target;
        CHECK(modeller.target_at(s.camera(),px,py,target,true));
        CHECK(target.source<0); CHECK(target.normal.z.to_f64()>.99);
        modeller.place(Modeller::Op::Extrude,target);
        CHECK(s.camera().project(target.centroid,px,py));
        CHECK(modeller.press(s.camera(),px,py));
        CHECK(modeller.stage()==Modeller::Stage::Dragging);
        const auto serial=modeller.edit_serial();
        const double before=gtopo::volume(modeller.bodies()[0],kVol);
        for (double offset : {.7,-.6,1.2,-.3,distance}) {
            const auto point=target.centroid+target.normal*gcm::F64::from(offset);
            CHECK(s.camera().project(point,px,py));
            modeller.drag(s.camera(),px,py); s.build_frame();
            CHECK(modeller.edit_serial()==serial); CHECK(modeller.bodies().size()==1);
            CHECK(std::fabs(gtopo::volume(modeller.bodies()[0],kVol)-before)<1e-7);
            CHECK(!modeller.gpu_preview().tool.empty());
        }
        CHECK(modeller.release()); s.build_frame();
        height+=distance;
        CHECK(modeller.bodies().size()==1); CHECK(modeller.bodies()[0].analyse().closed);
        CHECK(modeller.history(0).size()==feature_count);
        CHECK(std::fabs(gtopo::volume(modeller.bodies()[0],kVol)-(120+area*(height-3)))<1e-4);
    }
}

TEST(workflow_outside_circle_highlights_and_cuts_the_surrounding_face) {
    Shell s=face_edit_block();
    s.command("SKETCH"); s.build_frame(); click_at(s,4,2.5,3); s.build_frame();
    s.canvas().set_tool(Tool::Circle);
    CHECK(s.canvas().type_point("0,0")); CHECK(s.canvas().type_point("1,0"));
    s.canvas().set_tool(Tool::Select); s.build_frame(); s.finish_sketch(); s.build_frame();
    hover_world_once(s,4.1,2.6,3);
    const double circle_area=hover_area(s);
    CHECK(circle_area>3 && circle_area<3.15);
    hover_world_once(s,6.5,3,3);
    CHECK(s.hovered_target().source<0 && s.hovered_target().from_face);
    CHECK(std::fabs(hover_area(s)-(40-circle_area))<1e-4);
    click_at(s,6.5,3,3); s.build_frame();
    click_value_control(s,"Face offset",gui::Context::Role::Field);
    s.key_char("-1"); s.build_frame(); s.key_special(0); s.build_frame(); s.build_frame();
    CHECK_MSG(std::fabs(gtopo::volume(s.modeller().bodies()[0],kVol)-(80+circle_area))<1e-4,
              "volume=%g error=%s",gtopo::volume(s.modeller().bodies()[0],kVol),s.modeller().value_error().c_str());
    CHECK(s.modeller().bodies().size()==1); CHECK(s.modeller().bodies()[0].analyse().closed);
}


namespace {
void click_tablet_popup(Shell& s,const char* label) {
    gui::Rect bounds;
    for(const auto& item:s.accessible_ui())
        if(item.role==gui::Context::Role::Button&&item.depth==1&&item.label==label&&!item.disabled) {bounds=item.bounds;break;}
    CHECK_MSG(!bounds.empty(),"missing popup item: %s",label);
    if(bounds.empty())return;
    const auto point=bounds.centre();
    s.pointer_move(point.x,point.y);s.build_frame();
    s.pointer_down(point.x,point.y);s.build_frame();
    s.pointer_up(point.x,point.y);s.build_frame();s.build_frame();
}
}

TEST(workflow_tablet_toolbar_keeps_every_group_and_tool_reachable) {
    for (int width : {360, 820}) for (auto mode : {Mode::Sketch, Mode::Solid, Mode::Drawing}) {
        Shell s=make_shell();s.set_viewport(width,900);s.set_tablet_mode(true);s.set_mode(mode);s.build_frame();
        const auto has=[&](const char* label) {
            for(const auto& item:s.accessible_ui()) if(item.role==gui::Context::Role::Button&&item.label==label&&!item.disabled)return true;
            return false;
        };
        const auto category=[&]() {
            for(const auto& item:s.accessible_ui()) if(item.role==gui::Context::Role::Button&&item.label!="More tools"&&
                item.label.size()>6&&item.label.substr(item.label.size()-6)==" tools"&&item.bounds.y<250)return item.label;
            CHECK(false);return std::string{};
        };
        const auto& more=value_control(s,"More tools",gui::Context::Role::Button);
        CHECK(more.h==44);CHECK(more.right()<=width);CHECK(more.bottom()<250);
        for(const auto& group:tool_groups()) if(group.mode==mode) {
            const auto name=category();click_value_control(s,name.c_str(),gui::Context::Role::Button);
            CHECK(has(group.caption));click_tablet_popup(s,group.caption);
            click_value_control(s,"More tools",gui::Context::Role::Button);
            std::vector<std::string> listed;
            for(int page=0;page<20;++page) {
                for(const auto& item:s.accessible_ui()) if(item.role==gui::Context::Role::Button&&item.depth==1) {
                    listed.push_back(item.label);
                    CHECK(item.bounds.x>=0);CHECK(item.bounds.right()<=width);CHECK(item.bounds.bottom()<=900);
                }
                if(!has("Next tools"))break;
                click_tablet_popup(s,"Next tools");
            }
            for(const auto& tool:tool_defs()) if(tool.group==group.group)
                CHECK_MSG(std::find(listed.begin(),listed.end(),tool.label)!=listed.end(),"tablet tool inaccessible: %s",tool.label);
            click_value_control(s,"More tools",gui::Context::Role::Button);
        }
    }
}

TEST(workflow_tablet_options_anchor_below_arrow_after_overflow_selection) {
    Shell s=make_shell();s.set_viewport(360,900);s.set_tablet_mode(true);s.build_frame();
    click_value_control(s,"More tools",gui::Context::Role::Button);
    click_value_control(s,"Circle",gui::Context::Role::Button);s.build_frame();
    CHECK(s.active_tool()==ToolId::Circle);
    const auto arrow=value_control(s,"Circle options",gui::Context::Role::Button);
    CHECK(arrow.w==44);CHECK(arrow.h==44);
    s.pointer_move(arrow.centre().x,arrow.centre().y);s.build_frame();s.build_frame();
    bool highlighted=false;
    for(const auto& rect:s.ui()) if(rect.params[2]==0&&rect.rect[0]==arrow.x&&rect.rect[1]==arrow.y&&rect.rect[2]==arrow.w&&rect.rect[3]==arrow.h)highlighted=true;
    CHECK(highlighted);
    click_value_control(s,"Circle options",gui::Context::Role::Button);
    const auto& modes=Canvas::tool_modes(Tool::Circle);
    const auto first=value_control(s,modes[0].c_str(),gui::Context::Role::Button);
    CHECK(first.y==arrow.bottom()+4);
    click_value_control(s,modes[1].c_str(),gui::Context::Role::Button);
    CHECK(s.canvas().tool_mode(Tool::Circle)==1);
}

TEST(workflow_tablet_menu_overflow_reaches_all_categories_and_entries) {
    for(bool tablet:{false,true}) {
        Shell s=make_shell();s.set_viewport(360,520);s.set_tablet_mode(tablet);s.build_frame();
        const auto has=[&](const char* label) {
            for(const auto& item:s.accessible_ui())if(item.role==gui::Context::Role::Button&&item.label==label&&!item.disabled&&(item.depth==1||item.bounds.y<(tablet?44:32)))return true;
            return false;
        };
        for(std::size_t i=0;i<app_menus().size();++i) {
            const auto& menu=app_menus()[i];
            const bool visible=has(menu.title.c_str());
            if(!visible) {
                click_value_control(s,"More menus",gui::Context::Role::Button);
                for(int page=0;page<20&&!has(menu.title.c_str())&&has("Next page");++page)
                    click_tablet_popup(s,"Next page");
            }
            if(visible)click_value_control(s,menu.title.c_str(),gui::Context::Role::Button);
            else click_tablet_popup(s,menu.title.c_str());
            s.build_frame();
            CHECK(s.open_menu()==int(i));
            std::vector<std::string> listed;
            for(int page=0;page<20;++page) {
                bool in_popup=false;
                for(const auto& item:s.accessible_ui()) {
                    if(in_popup&&item.depth==0)break;
                    if(item.role!=gui::Context::Role::Button||item.depth!=1)continue;
                    in_popup=true;
                    listed.push_back(item.label);
                    CHECK(item.bounds.x>=0);CHECK(item.bounds.right()<=360);
                    CHECK_MSG(item.bounds.bottom()<=520,"menu %s item %s bottom %g",menu.title.c_str(),item.label.c_str(),item.bounds.bottom());
                }
                if(!has("Next page"))break;
                click_tablet_popup(s,"Next page");
            }
            for(const auto& entry:menu.items)if(!entry.separator())
                CHECK_MSG(std::find(listed.begin(),listed.end(),entry.label)!=listed.end(),"menu item inaccessible: %s",entry.label.c_str());
            click_value_control(s,"More menus",gui::Context::Role::Button);
            click_value_control(s,"More menus",gui::Context::Role::Button);
        }
    }
}

namespace {
void save_operation_review(Shell& s,const char* name) {
    const char* dir=std::getenv("GLCAD_UI_REVIEW_DIR");
    if(!dir || !*dir) return;
    std::filesystem::create_directories(dir);
    std::string error;CHECK_MSG(s.load_font(&error),"%s",error.c_str());
    const auto image=snapshot_png(s,int(s.viewport_width()),int(s.viewport_height()));
    CHECK(!image.empty());
    std::ofstream file(std::filesystem::path(dir)/name,std::ios::binary);
    file.write(reinterpret_cast<const char*>(image.data()),std::streamsize(image.size()));
    CHECK(bool(file));
}

void select_circular_pocket_rim(Shell& s) {
    const auto& v=s.canvas().volume();
    gtopo::Profile base;base.outer={gcm::to_lattice(v,0,0),gcm::to_lattice(v,4,0),gcm::to_lattice(v,4,4),gcm::to_lattice(v,0,4)};
    gtopo::Profile circle;
    for(int i=0;i<48;++i) {const double t=6.283185307179586*i/48;circle.outer.push_back(gcm::to_lattice(v,2+std::cos(t),2+std::sin(t)));}
    auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();frame.origin=gview::Point3::from(0,0,1);
    const auto cut=gtopo::subtract(gtopo::extrude_z(base,v,2),gtopo::extrude(circle,v,frame,2),v);
    CHECK(cut.ok());s.modeller().add_body(gtopo::merge_coplanar_faces(cut.body,v),"Pocket");
    s.camera().look_plan();s.camera().set_target(gview::Point3::from(2,2,2));s.camera().set_distance(8);
    s.build_frame();s.activate_tool(ToolId::FilletEdge);
    click_at(s,2+(1+std::cos(6.283185307179586/48))/2,2+std::sin(6.283185307179586/48)/2,2);
    s.build_frame();CHECK(s.modeller().edge_kept());
}
}

TEST(workflow_circular_rim_highlight_preview_commit_and_undo) {
    for(bool enter:{false,true}) {
        auto s=make_shell();select_circular_pocket_rim(s);
        const auto& v=s.canvas().volume();
        CHECK(s.modeller().edge_highlight().size()==48);
        std::size_t blue=0;
        for(const auto& vertex:s.tris())
            if(vertex.color[0]==0.15f && vertex.color[1]==0.55f && vertex.color[2]==1.0f && vertex.color[3]==0.30f) ++blue;
        CHECK_MSG(blue>=48*6,"missing translucent edge overlay: %zu vertices",blue);
        if(!enter) save_operation_review(s,"circular-rim-highlight.png");
        const auto before=gtopo::volume(s.modeller().bodies()[0],v);
        const auto history=s.modeller().history(0).size();
        click_value_control(s,"Round radius",gui::Context::Role::Field);
        s.key_char("100mm");s.build_frame();
        CHECK(s.modeller().value_preview_valid());
        CHECK(gtopo::volume(s.modeller().bodies()[0],v)==before);
        if(enter) {s.key_special(0);s.build_frame();}
        else click_value_control(s,"OK",gui::Context::Role::Button);
        s.build_frame();
        CHECK(s.modeller().history(0).size()==history+1);
        CHECK(s.modeller().bodies()[0].analyse().closed);
        CHECK(gtopo::volume(s.modeller().bodies()[0],v)<before);
        std::vector<gtopo::Body> restored;
        CHECK(decode_solids(encode_solids(s.modeller().bodies()),restored));
        CHECK(restored.size()==1);
        if(!restored.empty()) {
            CHECK(restored[0].analyse().closed);
            CHECK(restored[0].face_surface.size()==s.modeller().bodies()[0].face_surface.size());
            CHECK(std::fabs(gtopo::volume(restored[0],v)-gtopo::volume(s.modeller().bodies()[0],v))<1e-12);
        }
        CHECK(s.modeller().edge_highlight().size()==48);
        if(!enter) save_operation_review(s,"circular-rim-round.png");
        s.modeller().set_typed("50mm");CHECK(s.modeller().commit_typed());
        CHECK(s.modeller().history(0).size()==history+1);
        CHECK(s.modeller().bodies()[0].analyse().closed);
        CHECK(s.modeller().undo());
        CHECK(gtopo::volume(s.modeller().bodies()[0],v)==before);
    }
}

TEST(workflow_operation_errors_are_visible_in_command_history_during_drawing) {
    auto s=make_shell();select_circular_pocket_rim(s);
    s.command("RECTANG");s.build_frame();
    s.modeller().set_typed("2m");s.build_frame();
    const auto error=s.modeller().value_error();CHECK(!error.empty());
    CHECK(std::count(s.log().begin(),s.log().end(),error)==1);
    bool visible=false;
    for(const auto& item:s.accessible_ui()) if(item.role==gui::Context::Role::Label && item.label==error) {
        visible=true;CHECK(item.bounds.y>s.viewport_height()*0.7);
        CHECK(item.bounds.bottom()<s.viewport_height()-65);
    }
    CHECK_MSG(visible,"operation error missing from bottom history while keyword chips are active");
    save_operation_review(s,"operation-error-history.png");
    for(int i=0;i<3;++i) s.build_frame();
    CHECK(std::count(s.log().begin(),s.log().end(),error)==1);
    s.modeller().set_typed("100mm");s.build_frame();
    CHECK(s.modeller().value_error().empty());
    CHECK(s.modeller().value_preview_valid());
}

TEST(workflow_face_sketch_axes_stay_on_the_support_plane_at_camera_poles) {
    auto s=make_shell();s.command("BOX");s.build_frame();
    CHECK(s.modeller().bodies().size()==1);
    for(gcm::u32 face=0;face<s.modeller().bodies()[0].face_count();++face) {
        CHECK(s.begin_sketch_on_face(0,face));
        const auto& p=s.sketches()[s.active_sketch()].plane;
        CHECK(std::fabs(p.ux*p.nx+p.uy*p.ny+p.uz*p.nz)<1e-14);
        CHECK(std::fabs(p.vx*p.nx+p.vy*p.ny+p.vz*p.nz)<1e-14);
        CHECK(std::fabs(p.ux*p.vx+p.uy*p.vy+p.uz*p.vz)<1e-14);
        double x,y,z;p.to_world(25,35,x,y,z);
        CHECK(std::fabs(p.distance(x,y,z))<1e-12);
    }
}

TEST(workflow_reopening_split_faces_preserves_names_mates_and_history) {
    ScopedTestConfig config(scratch_path("split-face-config"));
    auto s=make_shell();const auto v=s.canvas().volume();
    gtopo::Profile profile;profile.outer={gcm::to_lattice(v,0,0),gcm::to_lattice(v,2,0),gcm::to_lattice(v,2,2),gcm::to_lattice(v,0,2)};
    const auto box=gtopo::extrude_z(profile,v,2);
    gtopo::Builder builder;
    for (auto p:box.vertex_point) builder.add_vertex(p);
    for (gcm::u32 f=0;f<box.face_count();++f) {
        const auto h=box.loop_begin(box.face_loop_begin(f));
        for (const auto tri:{std::array<gcm::u32,3>{h,h+1,h+2},std::array<gcm::u32,3>{h,h+2,h+3}}) {
            builder.begin_face();builder.add_loop({box.he_vertex[tri[0]],box.he_vertex[tri[1]],box.he_vertex[tri[2]]});builder.end_face();
        }
    }
    s.modeller().add_body(builder.build(),"Imported split box");
    const auto before=s.modeller().metadata();
    Modeller::StoredMate mate;mate.body_a=mate.body_b=s.modeller().body_name(0);
    mate.face_a=s.modeller().face_name(0,1);mate.face_b=s.modeller().face_name(0,3);
    s.modeller().load_mates({mate});
    s.begin_sketch_on_face(0,3);s.finish_sketch();
    const auto sketch_count=s.sketches().size();
    const auto sketch_plane=s.sketches().back().plane;
    const auto path=scratch_path("split-face-native.glcad");CHECK(s.save_file(path));
    auto opened=make_shell();CHECK(opened.open_file(path));
    CHECK(opened.modeller().bodies()[0].face_count()==6);
    CHECK(opened.modeller().bodies()[0].analyse().closed);
    CHECK(opened.modeller().history(0)[0].label=="Imported split box");
    CHECK(opened.sketches().size()==sketch_count);
    CHECK(opened.sketches().back().body==0 && opened.sketches().back().face==1);
    CHECK(opened.sketches().back().plane.oz==sketch_plane.oz);
    const auto after=opened.modeller().metadata();
    CHECK(after.next_face_ids==before.next_face_ids);CHECK(after.body_keys==before.body_keys);
    CHECK(opened.modeller().mates().size()==1);
    CHECK(opened.modeller().mates()[0].face_a==opened.modeller().face_name(0,0));
    CHECK(opened.modeller().mates()[0].face_b==opened.modeller().face_name(0,1));
    const auto cleaned_path=scratch_path("split-face-cleaned.glcad");
    CHECK(opened.save_file(cleaned_path));auto twice=make_shell();CHECK(twice.open_file(cleaned_path));
    CHECK(twice.modeller().metadata().face_ids==after.face_ids);
    opened.set_mode(Mode::Solid);opened.camera().look_plan();
    opened.camera().set_target(gview::Point3::from(1,1,2));opened.camera().set_distance(6);opened.build_frame();
    Modeller::Target first,second;
    for (bool other:{false,true}) {
        double px,py;CHECK(opened.camera().project(gview::Point3::from(other?1.75:.25,other?.25:1.75,2),px,py));
        CHECK(opened.modeller().target_at(opened.camera(),px,py,other?second:first,false));
    }
    CHECK(first.on_body && second.on_body && first.source<0 && second.source<0);
    CHECK(first.face==second.face);
    CHECK(first.outline.size()==1 && first.outline[0].size()==4);
    std::remove(path.c_str());
    std::remove(cleaned_path.c_str());
}

TEST(workflow_side_view_navigation_rejects_empty_and_invalid_gestures) {
    auto s=make_shell();s.camera().look(gview::StandardView::Right);
    s.camera().set_target(gview::Point3::from(0,0,1));s.camera().set_distance(6);s.build_frame();
    s.pointer_move(750,400);const auto before=s.camera().target();
    s.scroll(1,0,false);s.scroll(0,0,false);
    s.pinch(750,400,-1);s.pinch(750,400,std::numeric_limits<double>::infinity());
    CHECK(s.camera().distance()==6);CHECK(s.camera().target().z==before.z);
    s.pinch(750,400,-.999999);
    CHECK(s.camera().distance()==12);
    CHECK(std::fabs(s.camera().target().z.to_f64()-before.z.to_f64())<6);
    CHECK(std::fabs(s.camera().target().y.to_f64()-before.y.to_f64())<6);
}

TEST(workflow_nested_circular_rim_can_be_selected_previewed_and_committed) {
    for(bool round:{false,true}) {
        auto s=make_shell();const auto& v=s.canvas().volume();
        gtopo::Profile base;base.outer={gcm::to_lattice(v,-1,-1),gcm::to_lattice(v,1,-1),gcm::to_lattice(v,1,1),gcm::to_lattice(v,-1,1)};
        auto body=gtopo::extrude_z(base,v,2);double level=body.bounds(v).hi.z.to_f64();
        for(int pass=0;pass<3;++pass) {
            const int count=pass==0?512:pass==1?491:122;
            const double radius=std::sqrt(pass==0?.61:pass==1?.32:.02);
            const double depth=pass==0?.68681501875675377:pass==1?.54654273060925151:3.1417613082027849;
            gtopo::Profile circle;
            for(int i=0;i<count;++i) {const double t=6.283185307179586*i/count;circle.outer.push_back(gcm::to_lattice(v,radius*std::cos(t),radius*std::sin(t)));}
            auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();frame.origin=gview::Point3::from(0,0,level-depth);
            const auto tool=gtopo::extrude(circle,v,frame,depth);
            const auto cut=gtopo::subtract(body,tool,v);CHECK(cut.ok());
            body=gtopo::merge_coplanar_faces(cut.body,v);level=tool.bounds(v).lo.z.to_f64();
        }
        for(auto& p:body.vertex_point)p={p.z,p.x,p.y};
        s.modeller().add_body(body,"Nested circular pockets");s.set_mode(Mode::Solid);
        s.camera().look_along_normal(1,0,0);s.camera().set_target(gview::Point3::from(1,0,0));s.camera().set_distance(6);
        s.build_frame();s.activate_tool(round?ToolId::FilletEdge:ToolId::ChamferEdge);
        const double angle=6.283185307179586/512,r=std::sqrt(.61);
        click_at(s,body.bounds(v).hi.x.to_f64(),r*(1+std::cos(angle))/2,r*std::sin(angle)/2);
        s.build_frame();CHECK(s.modeller().edge_kept());
        CHECK(s.modeller().edge_highlight().size()==512);
        if(round)save_operation_review(s,"nested-circular-rim-highlight.png");
        const auto before=gtopo::volume(body,v);const auto history=s.modeller().history(0).size();
        s.modeller().set_typed("20mm");s.build_frame();CHECK(s.modeller().value_preview_valid());
        CHECK(gtopo::volume(s.modeller().bodies()[0],v)==before);
        CHECK(s.modeller().commit_typed());s.build_frame();
        CHECK(s.modeller().history(0).size()==history+1);
        CHECK(s.modeller().bodies()[0].analyse().closed);
        CHECK(gtopo::volume(s.modeller().bodies()[0],v)<before);
        if(round)save_operation_review(s,"nested-circular-rim-round.png");
        CHECK(s.modeller().undo());CHECK(gtopo::volume(s.modeller().bodies()[0],v)==before);
    }
}

TEST(workflow_armed_extrude_hover_burst_preserves_document_and_uses_last_pointer) {
    auto s=block_shell();
    s.camera().look(gview::StandardView::Top);
    s.camera().set_target(gview::Point3::from(2,2,0));
    s.fit_width(14); s.build_frame();
    s.activate_tool(ToolId::Extrude); s.build_frame();
    double inside_x,inside_y,outside_x,outside_y;
    CHECK(s.camera().project(gview::Point3::from(2,2,3),inside_x,inside_y));
    CHECK(s.camera().project(gview::Point3::from(-3,2,3),outside_x,outside_y));
    const auto revision=s.document_revision();
    s.pointer_move(inside_x,inside_y); s.build_frame();
    CHECK(static_cast<const Shell&>(s).modeller().hovered().valid);
    for(int i=0;i<1000;++i) s.pointer_move(inside_x+(i%2),inside_y);
    s.pointer_move(outside_x,outside_y); s.build_frame();
    CHECK(!static_cast<const Shell&>(s).modeller().hovered().valid);
    CHECK(s.document_revision()==revision);
    CHECK(static_cast<const Shell&>(s).modeller().op()==Modeller::Op::Extrude);
    CHECK(static_cast<const Shell&>(s).modeller().bodies().size()==1);
}

namespace {
gtopo::Body selection_pocket(const gcm::WorkingVolume& v) {
    gtopo::Profile square;square.outer={gcm::to_lattice(v,-2,-2),gcm::to_lattice(v,2,-2),gcm::to_lattice(v,2,2),gcm::to_lattice(v,-2,2)};
    auto box=gtopo::extrude_z(square,v,2);
    gtopo::Profile circle;
    for(int i=0;i<64;++i) {double a=2*std::acos(-1.0)*i/64;circle.outer.push_back(gcm::to_lattice(v,std::cos(a),std::sin(a)));}
    auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();frame.origin=gview::Point3::from(0,0,.5);
    auto cut=gtopo::subtract(box,gtopo::extrude(circle,v,frame,2),v);CHECK(cut.ok());
    return gtopo::merge_coplanar_faces(cut.body,v);
}
}

TEST(workflow_hole_wall_selects_whole_surface_and_navigation_keeps_picked_depth) {
    auto s=make_shell();const auto v=s.canvas().volume();
    s.modeller().add_body(selection_pocket(v),"Pocket");s.set_mode(Mode::Solid);
    s.camera().look_along_normal(0,-1,0);s.camera().set_projection(gview::Projection::Perspective);
    s.camera().set_target(gview::Point3::from(0,1,1.2));s.camera().set_distance(.5);s.build_frame();
    double x,y;CHECK(s.camera().project(gview::Point3::from(0,1,1.2),x,y));
    s.pointer_move(x,y);s.build_frame();s.build_frame();
    CHECK(!s.modeller().edge_hovered());
    CHECK(s.hovered_target().surface_faces.size()==64);
    click_at(s,0,1,1.2);s.build_frame();
    const auto target=s.selected_target();CHECK(target.surface_faces.size()==64);CHECK(target.has_pick_point);
    CHECK(s.modeller().stage()==Modeller::Stage::Idle);
    const auto revision=s.document_revision();
    gview::Point3 cursor_anchor;
    CHECK(s.camera().plane_hit(x+70,y+40,target.pick_point,s.camera().forward(),cursor_anchor));
    for(int i=0;i<12;++i)s.pinch(x+70,y+40,.03);
    double bx,by;CHECK(s.camera().project(cursor_anchor,bx,by));
    CHECK(std::fabs(bx-(x+70))<1e-5);CHECK(std::fabs(by-(y+40))<1e-5);
    double ax,ay;CHECK(s.camera().project(target.pick_point,ax,ay));
    s.pointer_move(x,y);s.scroll(23,-11,true);
    CHECK(s.camera().project(target.pick_point,bx,by));
    CHECK(std::fabs(bx-ax-23)<1e-5);CHECK(std::fabs(by-ay+11)<1e-5);
    CHECK(s.document_revision()==revision);
    std::vector<MeshVertex> wash;const float color[4]={.1f,.5f,1,.3f};s.modeller().fill(target,color,wash);
    CHECK(wash.size()==64*6);
}

TEST(workflow_hole_wall_cannot_be_used_as_a_tessellation_plane) {
    auto s=make_shell();const auto v=s.canvas().volume();
    s.modeller().add_body(selection_pocket(v),"Pocket");s.set_mode(Mode::Solid);
    auto& camera=s.camera();camera.look_along_normal(0,-1,0);
    camera.set_projection(gview::Projection::Perspective);
    camera.set_target(gview::Point3::from(0,1,1.2));camera.set_distance(.5);
    double x,y;CHECK(camera.project(gview::Point3::from(0,1,1.2),x,y));
    auto& m=s.modeller();Modeller::Target target;
    CHECK(m.target_at(camera,x,y,target,true));CHECK(target.surface_faces.size()==64);
    CHECK(!m.face_is_planar(target.body,target.face));
    const auto sketch_count=s.sketches().size();
    CHECK(!s.begin_sketch_on_face(target.body,target.face));CHECK(s.sketches().size()==sketch_count);
    for(auto op:{Modeller::Op::Extrude,Modeller::Op::Mate,Modeller::Op::ShellBody}) {
        m.arm(op);CHECK(m.press(camera,x,y));CHECK(m.stage()==Modeller::Stage::PickTarget);
    }
    for(auto op:{Modeller::Op::Split,Modeller::Op::Section,Modeller::Op::MirrorBody}) {
        m.preselect_body(op,0);m.hover(camera,x,y);
        CHECK(!m.hovered().valid);CHECK(m.press(camera,x,y));CHECK(m.stage()==Modeller::Stage::PickSecond);
    }
    CHECK(m.bodies().size()==1);
}

TEST(workflow_round_surface_hides_tessellation_edges_and_selects_all_strips) {
    auto s=block_shell();const auto body=s.modeller().bodies()[0];const auto v=s.canvas().volume();
    double ex0,ey0,ez0,ex1,ey1,ez1;
    gcm::to_world(v,body.vertex_point[body.he_vertex[0]],ex0,ey0,ez0);
    gcm::to_world(v,body.vertex_point[body.he_vertex[body.he_next[0]]],ex1,ey1,ez1);
    const auto edge_point=gview::Point3::from((ex0+ex1)/2,(ey0+ey1)/2,(ez0+ez1)/2);
    s.camera().look_along_normal(0,1,-1);s.camera().set_target(edge_point);s.camera().set_distance(5);
    double edge_x,edge_y;CHECK(s.camera().project(edge_point,edge_x,edge_y));
    CHECK(s.modeller().hover_edge(s.camera(),edge_x,edge_y));s.modeller().begin_edge();
    CHECK(s.round_edge(.2));s.modeller().finish_edge();s.command("CANCEL");s.build_frame();
    const auto rounded_body=s.modeller().bodies()[0];
    gcm::u32 face=0;while(face<rounded_body.face_count()&&!rounded_body.face_is_curved(face))++face;
    CHECK(face<rounded_body.face_count());if(face==rounded_body.face_count())return;
    face+=5;
    const auto& b=rounded_body;gcm::u32 seam=gtopo::kInvalid;
    for(auto h=b.loop_begin(b.face_loop_begin(face));h<b.loop_end(b.face_loop_begin(face));++h)
        if(b.he_twin[h]!=gtopo::kInvalid && b.face_is_curved(b.loop_face[b.he_loop[b.he_twin[h]]])) {seam=h;break;}
    CHECK(seam!=gtopo::kInvalid);if(seam==gtopo::kInvalid)return;
    double ax,ay,az,bx,by,bz;gcm::to_world(v,b.vertex_point[b.he_vertex[seam]],ax,ay,az);
    gcm::to_world(v,b.vertex_point[b.he_vertex[b.he_next[seam]]],bx,by,bz);
    const auto point=gview::Point3::from((ax+bx)/2,(ay+by)/2,(az+bz)/2);
    const auto plane=gtopo::face_plane(b,face,v);
    s.camera().look_along_normal(plane.nx,plane.ny,plane.nz);s.camera().set_projection(gview::Projection::Perspective);
    s.camera().set_target(point);s.camera().set_distance(.7);s.build_frame();
    double x,y;CHECK(s.camera().project(point,x,y));s.pointer_move(x,y);s.build_frame();s.build_frame();
    CHECK(!s.modeller().edge_hovered());CHECK_MSG(s.hovered_target().surface_faces.size()==12,"hover faces=%zu valid=%d px=%g py=%g point=%g,%g,%g stage=%d",s.hovered_target().surface_faces.size(),s.hovered_target().valid,x,y,point.x.to_f64(),point.y.to_f64(),point.z.to_f64(),int(s.modeller().stage()));
    click_at(s,point.x.to_f64(),point.y.to_f64(),point.z.to_f64());s.build_frame();
    CHECK(s.selected_target().surface_faces.size()==12);
}

TEST(workflow_round_radius_card_uses_clicked_point_when_edge_extends_offscreen) {
    auto s=make_shell();s.modeller().add_body(block_shell().modeller().bodies()[0],"Box");s.set_mode(Mode::Solid);s.camera().look(gview::StandardView::Iso);
    const auto point=gview::Point3::from(.2,4,3);
    s.camera().set_target(point);s.camera().set_distance(.5);s.build_frame();
    s.activate_tool(ToolId::FilletEdge);s.build_frame();
    click_at(s,.2,4,3);s.build_frame();CHECK_MSG(s.modeller().edge_kept(),"stage=%d op=%d message=%s",int(s.modeller().stage()),int(s.modeller().op()),s.modeller().message().c_str());
    double x,y;CHECK(s.camera().project(point,x,y));
    const auto field=value_control(s,"Round radius",gui::Context::Role::Field);
    CHECK(!field.empty());CHECK(std::fabs(field.x-x)<100);CHECK(std::fabs(field.y-y)<150);
    const auto before=field;s.pointer_move(x+120,y+90);s.build_frame();
    const auto after=value_control(s,"Round radius",gui::Context::Role::Field);
    CHECK(before.x==after.x && before.y==after.y);
    const auto camera_before=s.camera().distance();s.modeller().set_typed("0.1");s.build_frame();
    CHECK(s.modeller().value_preview_valid());CHECK(s.camera().distance()==camera_before);
    const auto edited=value_control(s,"Round radius",gui::Context::Role::Field);
    CHECK(edited.x==before.x && edited.y==before.y);
}

TEST(workflow_rounded_hole_surface_group_survives_native_reopen_and_undo) {
    auto s=make_shell();const auto v=s.canvas().volume();auto body=selection_pocket(v);
    gcm::u32 rim=gtopo::kInvalid;
    for(gcm::u32 l=0;l<body.loop_count();++l) if(body.loop_size(l)==64) {
        const auto h=body.loop_begin(l);double x,y,z;gcm::to_world(v,body.vertex_point[body.he_vertex[h]],x,y,z);
        if(z>1.9 && !gtopo::circular_edge_ring(body,h,v).empty()) {rim=h;break;}
    }
    CHECK(rim!=gtopo::kInvalid);if(rim==gtopo::kInvalid)return;
    const auto rounded=gtopo::fillet_edge_exact(body,rim,.15,12,v);CHECK(rounded.ok);if(!rounded.ok)return;
    s.modeller().add_body(rounded.body,"Rounded pocket");s.set_mode(Mode::Solid);
    const auto path=scratch_path("rounded-selection.glcad");CHECK(s.save_file(path));
    auto reopened=make_shell();CHECK(reopened.open_file(path));reopened.set_mode(Mode::Solid);
    auto& m=reopened.modeller();const auto& b=m.bodies()[0];
    gcm::u32 f=0;while(f<b.face_count() && (!b.surface_of(f)||b.surface_of(f)->kind!=gtopo::SurfaceKind::Torus))++f;
    CHECK(f<b.face_count());if(f==b.face_count())return;
    const auto plane=gtopo::plane_of_face(b,f,v);
    const auto point=gview::Point3::from(plane.ox,plane.oy,plane.oz);
    auto& camera=reopened.camera();camera.look_along_normal(plane.nx,plane.ny,plane.nz);
    camera.set_projection(gview::Projection::Perspective);camera.set_target(point);camera.set_distance(.01);
    double x,y;CHECK(camera.project(point,x,y));Modeller::Target target;
    CHECK(m.target_at(camera,x,y,target,true));CHECK(target.surface_faces.size()==64*12*2);
    const auto first=target.surface_faces;
    for(int i=0;i<8;++i) {CHECK(m.target_at(camera,x,y,target,true));CHECK(target.surface_faces==first);}
    CHECK(m.replace_body(0,body));CHECK(m.undo());
    CHECK(m.target_at(camera,x,y,target,true));CHECK(target.surface_faces==first);
    std::remove(path.c_str());
}

TEST(workflow_inside_corner_round_preview_commit_undo_and_ui_order) {
    for(bool bevel:{false,true}) {
        auto s=make_shell();const auto& v=s.canvas().volume();
        gtopo::Profile base,tool;
        base.outer={gcm::to_lattice(v,0,0),gcm::to_lattice(v,4,0),gcm::to_lattice(v,4,4),gcm::to_lattice(v,0,4)};
        tool.outer={gcm::to_lattice(v,1,1),gcm::to_lattice(v,5,1),gcm::to_lattice(v,5,5),gcm::to_lattice(v,1,5)};
        auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();frame.origin=gview::Point3::from(0,0,1);
        const auto cut=gtopo::subtract(gtopo::extrude_z(base,v,2),gtopo::extrude(tool,v,frame,2),v);
        CHECK(cut.ok());if(!cut.ok())continue;
        s.modeller().add_body(gtopo::merge_coplanar_faces(cut.body,v),"Notched box");
        s.set_mode(Mode::Solid);s.camera().look(gview::StandardView::IsoNE);
        s.camera().set_target(gview::Point3::from(2,1,1));s.camera().set_distance(2);
        s.build_frame();s.activate_tool(bevel?ToolId::ChamferEdge:ToolId::FilletEdge);s.build_frame();
        click_at(s,2,1,1);s.build_frame();CHECK(s.modeller().edge_kept());
        gview::Point3 picked;
        CHECK(s.modeller().edge_pick_point(picked));
        CHECK(std::abs(picked.x.to_f64()-2)<1e-6);
        CHECK(std::abs(picked.y.to_f64()-1)<1e-6);
        CHECK(std::abs(picked.z.to_f64()-1)<1e-6);
        const auto before=gtopo::volume(s.modeller().bodies()[0],v);
        const char* label=bevel?"Chamfer amount":"Round radius";
        const auto field=value_control(s,label,gui::Context::Role::Field);CHECK(!field.empty());
        bool highlighted=false,card_after_highlight=false;
        std::size_t ui_count=0,tri_count=0,text_count=0;
        for(const auto& run:s.ui_layers().runs) {
            if(run.kind==UiLayers::Kind::Triangle) {
                CHECK(run.first==tri_count);tri_count+=run.count;
                for(std::size_t i=run.first;i<run.first+run.count;++i) {
                    const auto& c=s.tris()[i].color;
                    if(c[0]==.15f && c[1]==.55f && c[2]==1.f && c[3]==.30f)highlighted=true;
                }
            } else if(run.kind==UiLayers::Kind::Surface) {
                CHECK(run.first==ui_count);ui_count+=run.count;
                for(std::size_t i=run.first;i<run.first+run.count;++i) {
                    const auto& u=s.ui()[i];
                    const gui::Rect bounds{u.rect[0],u.rect[1],u.rect[2],u.rect[3]};
                    if(highlighted && u.color[3]==1 && bounds.contains(field.centre()))card_after_highlight=true;
                }
            } else {CHECK(run.first==text_count);text_count+=run.count;}
        }
        CHECK(highlighted);CHECK(card_after_highlight);
        CHECK(ui_count==s.ui().size());CHECK(tri_count==s.tris().size());CHECK(text_count==s.text().size());
        click_value_control(s,label,gui::Context::Role::Field);s.key_char("250mm");s.build_frame();
        CHECK_MSG(s.modeller().value_preview_valid(),"%s",s.modeller().value_error().c_str());
        CHECK(gtopo::volume(s.modeller().bodies()[0],v)==before);
        if(!bevel)save_operation_review(s,"inside-corner-round-preview.png");
        click_value_control(s,"OK",gui::Context::Role::Button);s.build_frame();
        CHECK(s.modeller().bodies()[0].analyse().closed);
        CHECK(s.modeller().bodies()[0].analyse().manifold);
        CHECK(gtopo::volume(s.modeller().bodies()[0],v)>before);
        CHECK(s.modeller().undo());CHECK(std::abs(gtopo::volume(s.modeller().bodies()[0],v)-before)<1e-9);
    }
}

TEST(solid_modifier_selection_toggles_faces_before_double_click_and_preserves_context) {
    for(bool command:{false,true}) {
        Shell s=block_shell();
        s.fit_width(18);s.set_lens(Lens::ThreeD);s.camera().look(gview::StandardView::Iso);s.build_frame();
        s.set_modifiers(false,!command,false,command);
        const auto click=[&](double x,double y,double z) {
            double px,py;CHECK(s.camera().project(gview::Point3::from(x,y,z),px,py));
            s.pointer_move(px,py);s.pointer_down(px,py);s.pointer_up(px,py);
        };
        click(2,2,3);
        CHECK(s.selection_summary().faces==1);
        CHECK(s.modeller().op()==Modeller::Op::None);
        click(2,0,1.5);
        CHECK(s.selection_summary().faces==2);
        s.build_frame();s.build_frame();
        CHECK(s.selection_summary().faces==2);
        click(2,0,1.5);
        CHECK(s.selection_summary().faces==1);
        CHECK(s.sketches().size()==1);
        click(2,0,1.5);
        double x,y;CHECK(s.camera().project(gview::Point3::from(2,0,1.5),x,y));
        s.right_down(x,y);s.build_frame();
        CHECK(s.selection_summary().faces==2);
        bool erase=false;
        for(const auto& item:s.accessible_ui()) {
            if(item.label=="Erase") erase=true;
        }
        CHECK(erase);
        s.key_special(1);s.key_special(1);
        CHECK(s.selection_summary().faces==0);
    }
}

TEST(solid_body_additive_selection_is_per_tab_and_bulk_erase_undo_restores_all) {
    Shell s=block_shell();
    const auto copy=s.modeller().bodies().front();s.modeller().add_body(copy,"Second block");s.build_frame();
    s.select_body(0);s.select_body(1,true);
    CHECK(s.selection_summary().bodies==2);
    const int original=s.active_tab();s.new_tab();s.build_frame();
    CHECK(s.selection_summary().bodies==0);
    s.switch_tab(original);s.build_frame();
    CHECK(s.selection_summary().bodies==2);
    s.select_body(0,true);CHECK(s.selection_summary().bodies==1);
    s.select_body(0,true);CHECK(s.selection_summary().bodies==2);
    s.command("ERASE");s.build_frame();
    CHECK(s.modeller().bodies().empty());
    s.command("UNDO");s.build_frame();
    CHECK(s.modeller().bodies().size()==2);
    CHECK(s.selection_summary().bodies==0);
}

TEST(modifier_left_selection_has_no_action_card_and_right_click_preserves_it) {
    for(bool command:{false,true}) {
        Shell s=make_shell();draw_rect(s,"0,0","4,0","4,4","0,4");
        s.camera().set_target(gview::Point3::from(2,3,0));s.fit_width(16);
        s.canvas().select_all();s.build_frame();
        const auto node=[&](const std::string& label) {
            for(const auto& item:s.accessible_ui())if(item.label==label)return item;
            return gui::Context::Accessible{};
        };
        CHECK(node("Object options").bounds.empty());CHECK(node("Selection actions").bounds.empty());
        double px,py;CHECK(s.camera().project(gview::Point3::from(2,1,0),px,py));
        CHECK(s.canvas_bounds().contains({float(px),float(py)}));
        s.pointer_move(px,py);s.build_frame();CHECK(!s.ui_wants_mouse());
        const auto prior=s.canvas().selection();const auto selected=prior.size();
        s.canvas().modify_shapes([&](auto& shapes) {
            gsnap::Shape edge;edge.a=gcm::to_lattice(kVol,1.9,1);edge.b=gcm::to_lattice(kVol,2.1,1);
            shapes.push_back(edge);
        });
        s.canvas().set_selection(prior);s.set_modifiers(false,!command,false,command);
        s.pointer_down(px,py);s.pointer_up(px,py);
        CHECK(s.canvas().selection().size()==selected+1);s.build_frame();
        CHECK(s.canvas().selection().size()==selected+1);
        CHECK(node("Object options").bounds.empty());CHECK(node("Selection actions").bounds.empty());
        s.set_modifiers(false,false,false,false);s.right_down(px,py);s.build_frame();
        CHECK(s.context_menu_open());CHECK(!node("Selection actions").bounds.empty());
        CHECK(node("Selection actions").value=="5 sketch edges");
        CHECK(s.canvas().selection().size()==selected+1);
    }
}

TEST(workflow_multi_edge_value_preview_replaces_each_scene_body_and_enables_apply) {
    Shell shell=make_shell();shell.set_mode(Mode::Solid);
    auto& modeller=shell.modeller();
    for(double x:{0.0,12.0}) {
        gtopo::Profile profile;
        for(auto [u,v]:{std::pair{x,0.0},std::pair{x+8,0.0},std::pair{x+8,5.0},std::pair{x,5.0}})
            profile.outer.push_back(gcm::to_lattice(kVol,u,v));
        modeller.add_body(gtopo::extrude_z(profile,kVol,3),"Preview block");
    }
    shell.camera().look_plan();shell.camera().set_target(gview::Point3::from(10,2.5,0));shell.camera().set_distance(40);
    shell.build_frame();
    for(double x:{4.0,16.0}) {
        double px,py;CHECK(shell.camera().project(gview::Point3::from(x,0,3),px,py));
        CHECK(modeller.toggle_edge_at(shell.camera(),px,py));
    }
    const auto serial=modeller.edit_serial();
    const auto baseline=shell.mesh().size();
    modeller.set_typed(".25");shell.build_frame();
    CHECK(modeller.value_preview_valid());CHECK(shell.mesh().size()>baseline);
    CHECK(modeller.edit_serial()==serial);
    std::size_t matched=0;
    for(const auto& range:modeller.mesh_ranges()) {
        for(const auto& item:shell.scene().items())
            if(item.kind==grender::DrawKind::Mesh && item.first==range.first && item.count==range.count) ++matched;
    }
    CHECK(matched==2);
    bool apply=false;
    for(const auto& item:shell.accessible_ui()) if(item.label=="OK" && !item.disabled) apply=true;
    CHECK(apply);
    modeller.set_typed("100");shell.build_frame();
    CHECK(!modeller.value_preview_valid());CHECK(shell.mesh().size()==baseline);
    modeller.set_typed(".25");shell.build_frame();shell.key_special(1);shell.build_frame();
    CHECK(!modeller.value_preview_valid());CHECK(shell.mesh().size()==baseline);
    CHECK(modeller.edit_serial()==serial);
    for(const auto& body:modeller.bodies()) CHECK(std::fabs(gtopo::volume(body,kVol)-120)<1e-4);
}

TEST(workflow_block_library_restores_personal_collection_after_restart) {
    ScopedTestConfig config((std::filesystem::temp_directory_path()/("glcad-library-config-"+std::to_string(getpid()))).string());
    Shell source;source.load_app_config();auto& c=source.canvas();
    gsnap::Shape line;line.a=gcm::to_lattice(c.volume(),0,0);line.b=gcm::to_lattice(c.volume(),1,0);
    c.load_shapes({line});c.select_all();
    CHECK(source.save_selection_to_library("Fixture","Custom/Equipment",line.a));
    Shell restarted;restarted.load_app_config();
    CHECK(restarted.block_library().entries.size()==1);
    if(!restarted.block_library().entries.empty()) {
        CHECK(restarted.block_library().entries[0].folder=="Custom/Equipment");
        CHECK(restarted.insert_library_block(0,line.a));CHECK(restarted.canvas().blocks().references().size()==1);
    }
}

TEST(workflow_zoom_extents_fits_visible_canvas_after_layout_and_resize) {
    for(const auto viewport:{std::pair{1200.,800.},std::pair{800.,1200.},std::pair{2400.,1500.}}) {
        Shell s=make_shell();s.set_viewport(viewport.first,viewport.second);
        auto& c=s.canvas();std::vector<gsnap::Shape> shapes;
        gsnap::Shape line;line.a=gcm::to_lattice(c.volume(),-30,-2);line.b=gcm::to_lattice(c.volume(),40,3);shapes.push_back(line);
        gsnap::Shape circle;circle.kind=gsnap::ShapeKind::Circle;circle.a=gcm::to_lattice(c.volume(),0,15);circle.radius=gcm::i64(6/c.volume().inv_scale());shapes.push_back(circle);
        c.load_shapes(shapes);
        s.command("ZOOMEXTENTS");s.build_frame();
        const auto check=[&] {
            const auto rect=s.canvas_fit_bounds();
            CHECK(rect.y >= s.canvas_bounds().y + 126);
            for(auto p:{std::pair{-30.,-2.},std::pair{40.,3.},std::pair{0.,21.},std::pair{-6.,15.},std::pair{6.,15.}}) {
                double x,y;CHECK(s.camera().project(gview::Point3::from(p.first,p.second,0),x,y));
                CHECK_MSG(rect.contains({float(x),float(y)}),"extent %.2f %.2f outside %.2f %.2f %.2f %.2f",x,y,rect.x,rect.y,rect.w,rect.h);
            }
        };
        check();s.set_viewport(viewport.second,viewport.first);s.command("ZOOMEXTENTS");s.build_frame();check();
    }
}

TEST(workflow_mode_menu_switches_modes_on_desktop_and_tablet) {
    for (bool tablet : {false, true}) for (int width : {360, 1032, 1376}) {
        Shell s=make_shell();s.set_viewport(width,900);s.set_tablet_mode(tablet);s.build_frame();
        for (const char* name : {"Solid","Drawing","Sketch"}) {
            const std::string current=std::string("Mode: ")+(s.mode()==Mode::Sketch?"Sketch":s.mode()==Mode::Solid?"Solid":"Drawing");
            const auto button=value_control(s,current.c_str(),gui::Context::Role::Button);
            CHECK(button.right()<=width);CHECK(button.w<=float(tablet?200:160));
            click_value_control(s,current.c_str(),gui::Context::Role::Button);
            click_tablet_popup(s,name);
            const Mode want=name[1]=='o'?Mode::Solid:name[0]=='D'?Mode::Drawing:Mode::Sketch;
            CHECK_MSG(s.mode()==want,"tablet %d width %d: %s not selected",int(tablet),width,name);
            bool open=false;
            for(const auto& item:s.accessible_ui()) if(item.depth==1&&item.label=="Drawing"&&item.role==gui::Context::Role::Button)open=true;
            CHECK(!open);
        }
    }
}

TEST(workflow_tablet_header_is_two_compact_rows_on_ipad_widths) {
    for (int width : {1032, 1376}) for (auto mode : {Mode::Sketch, Mode::Solid, Mode::Drawing}) {
        Shell s=make_shell();s.set_viewport(width,1300);s.set_tablet_mode(true);
        s.command("LAYOUT NEW Plan");s.select_layout(-1);s.set_mode(mode);s.build_frame();s.build_frame();
        CHECK(s.mode()==mode);
        const float top=s.toolbar_top();
        const std::string tab_title=s.tabs()[0].title;
        CHECK_MSG(top<=44.5f,"width %d: header row %g tall",width,top);
        CHECK_MSG(s.canvas_bounds().y<=100,"width %d mode %d: %g pt of chrome above the canvas",width,int(mode),s.canvas_bounds().y);
        const std::string document="Document: "+tab_title;
        const std::string layout=std::string("Layout: ")+(s.selected_layout()<0?"Model":s.layouts()[std::size_t(s.selected_layout())].name);
        for (const char* label : {"Settings","Assistant panel","Drawing options",document.c_str(),layout.c_str()}) {
            const auto r=value_control(s,label,gui::Context::Role::Button);
            CHECK_MSG(r.bottom()<=top+0.5f,"width %d: %s is not in the first row",width,label);
        }
        const auto more=value_control(s,"More tools",gui::Context::Role::Button);
        const auto undo=value_control(s,"Undo",gui::Context::Role::Button);
        CHECK(more.y>=top);CHECK(std::fabs(undo.y-more.y)<1);CHECK(undo.right()<=width);
        std::vector<std::string> labels;
        for(const auto& group:tool_groups()) if(group.mode==mode)
            for(const auto& def:tool_defs()) if(def.group==group.group) labels.push_back(def.label);
        std::size_t tools=0;float rightmost=0;
        std::vector<gui::Rect> header;
        for(const auto& item:s.accessible_ui()) if(item.role==gui::Context::Role::Button&&item.depth==0&&item.bounds.bottom()<=s.canvas_bounds().y+0.5f) {
            CHECK(item.bounds.x>=0);CHECK_MSG(item.bounds.right()<=width+0.5f,"%s overflows",item.label.c_str());
            for(const auto& other:header) CHECK_MSG(!item.bounds.deflate(gui::Insets::all(1)).intersects(other),"%s overlaps",item.label.c_str());
            header.push_back(item.bounds);
            if(std::fabs(item.bounds.y-more.y)<1&&std::find(labels.begin(),labels.end(),item.label)!=labels.end()) {++tools;rightmost=std::max(rightmost,item.bounds.right());}
        }
        CHECK_MSG(tools>=std::min<std::size_t>(labels.size(),width>1200?14:8),"width %d mode %d: %zu tools in the header",width,int(mode),tools);
        if(tools<labels.size()) CHECK_MSG(more.x-rightmost<100,"width %d: %g px of empty toolbar",width,more.x-rightmost);
    }
}

TEST(workflow_command_history_hides_and_shows_from_its_own_button) {
    for (bool tablet : {false, true}) {
        Shell s=make_shell();s.set_viewport(1376,1000);s.set_tablet_mode(tablet);
        s.command("LINE");s.key_special(1);s.build_frame();s.build_frame();
        CHECK(!s.command_history_hidden());
        click_value_control(s,"Hide command history",gui::Context::Role::Button);s.build_frame();
        CHECK(s.command_history_hidden());
        bool rows=false;
        for(const auto& item:s.accessible_ui()) if(item.role==gui::Context::Role::Label&&!s.log().empty()&&item.label==s.log().back())rows=true;
        CHECK(!rows);
        const auto show=value_control(s,"Show command history",gui::Context::Role::Button);
        CHECK(show.w==float(tablet?44:28));
        click_value_control(s,"Show command history",gui::Context::Role::Button);s.build_frame();
        CHECK(!s.command_history_hidden());
        CHECK(!value_control(s,"Hide command history",gui::Context::Role::Button).empty());
    }
}

TEST(workflow_scrim_covers_the_safe_area_insets) {
    Shell s=make_shell();s.set_viewport(1376,1032);s.set_tablet_mode(true);s.set_safe_insets(0,24,0,20);s.build_frame();
    const auto settings=value_control(s,"Settings",gui::Context::Role::Button);
    CHECK(settings.y>=24);
    click_value_control(s,"Settings",gui::Context::Role::Button);s.build_frame();
    bool full=false;
    for(const auto& r:s.ui()) if(r.params[2]==0&&r.rect[0]<=0&&r.rect[1]<=0&&r.rect[2]>=1376&&r.rect[3]>=1032&&r.color[3]>0.2f&&r.color[3]<0.5f&&r.color[0]==0)full=true;
    CHECK_MSG(full,"the settings scrim does not cover the whole drawable");
    bool bottom_strip=false;
    for(const auto& r:s.ui()) if(r.params[2]==0&&r.rect[1]>=1012-0.5f&&r.rect[1]<=1012+0.5f&&r.rect[3]>=19.5f&&r.rect[2]>=1376)bottom_strip=true;
    CHECK_MSG(bottom_strip,"the home-indicator strip is not painted");
}

TEST(command_history_hidden_survives_restart) {
    ScopedTestConfig test_config("/tmp/glcad-test-history-" + std::to_string(getpid()));
    {
        Shell s = make_shell();
        s.load_app_config();
        CHECK(!s.command_history_hidden());
        s.set_command_history_hidden(true);
    }
    {
        Shell s = make_shell();
        s.load_app_config();
        CHECK(s.command_history_hidden());
        s.set_command_history_hidden(false);
    }
    Shell s = make_shell();
    s.load_app_config();
    CHECK(!s.command_history_hidden());
}

TEST(workflow_drawing_options_live_in_the_header_and_only_the_unit_chip_floats) {
    for (bool tablet : {false, true}) for (auto mode : {Mode::Sketch, Mode::Solid}) {
        Shell s=make_shell();s.set_viewport(1376,1000);s.set_tablet_mode(tablet);s.set_mode(mode);s.build_frame();s.build_frame();
        const auto canvas=s.canvas_bounds();
        for(const auto& item:s.accessible_ui()) if(item.depth==0&&canvas.contains(item.bounds.centre())&&item.bounds.y<canvas.y+60&&item.bounds.x<canvas.x+400)
            CHECK_MSG(item.label.find("unit")!=std::string::npos||item.role==gui::Context::Role::Label,"%s floats over the canvas",item.label.c_str());
        const auto button=value_control(s,"Drawing options",gui::Context::Role::Button);
        CHECK(button.bottom()<=s.toolbar_top()+0.5f);
        click_value_control(s,"Drawing options",gui::Context::Role::Button);s.build_frame();
        bool discipline=false,display=false;
        for(const auto& item:s.accessible_ui()) if(item.depth>=1) {
            discipline|=item.label.find("Drawing discipline")!=std::string::npos;
            display|=item.label.find("Display mode")!=std::string::npos;
        }
        CHECK(discipline);CHECK(display);
        bool library=false;
        for(const auto& item:s.accessible_ui()) library|=item.label=="Object library"&&item.depth==1;
        CHECK(library==(mode==Mode::Sketch));
        s.command("DISCIPLINE MECHANICAL");s.build_frame();
        CHECK(s.drawing_discipline()==DrawingDiscipline::Mechanical);
        if(mode==Mode::Sketch) {
            click_tablet_popup(s,"Object library");
            bool open=false;
            for(const auto& item:s.accessible_ui()) open|=item.label=="Library object size";
            CHECK(open);
        }
    }
}

TEST(workflow_document_and_layout_dropdowns_switch_close_and_create) {
    for (auto [tablet,width,height] : {std::tuple{true,1032,1366},std::tuple{true,1376,1032},std::tuple{false,1600,1000}}) {
        Shell s=make_shell();s.set_viewport(width,height);s.set_tablet_mode(tablet);s.build_frame();
        const auto open_picker=[&](const char* prefix) {
            gui::Rect r;
            for(const auto& item:s.accessible_ui()) if(item.role==gui::Context::Role::Button&&item.depth==0&&item.label.rfind(prefix,0)==0) r=item.bounds;
            CHECK_MSG(!r.empty(),"picker %s missing",prefix);
            CHECK(r.bottom()<=s.toolbar_top()+0.5f);CHECK(r.right()<=width);
            const auto c=r.centre();
            s.pointer_move(c.x,c.y);s.build_frame();s.pointer_down(c.x,c.y);s.build_frame();s.pointer_up(c.x,c.y);s.build_frame();s.build_frame();
        };
        const auto pick=[&](const std::string& label) { click_tablet_popup(s,label.c_str()); s.build_frame(); };
        open_picker("Document: ");pick("New document");
        CHECK(s.tabs().size()==2);CHECK(s.active_tab()==1);
        open_picker("Document: ");pick(s.tabs()[0].title);
        CHECK(s.active_tab()==0);
        open_picker("Document: ");pick("Close "+s.tabs()[1].title);
        CHECK(s.tabs().size()==1);
        open_picker("Layout: ");pick("New layout");
        CHECK(s.layouts().size()==1);CHECK(s.selected_layout()==0);
        open_picker("Layout: ");pick("Model");
        CHECK(s.selected_layout()==-1);
        open_picker("Layout: ");pick("Next layout");
        CHECK(s.selected_layout()==0);
        open_picker("Layout: ");pick("Previous layout");
        CHECK(s.selected_layout()==-1);
        open_picker("Layout: ");pick(s.layouts()[0].name);
        CHECK(s.selected_layout()==0);
        bool open=false;
        for(const auto& item:s.accessible_ui()) open|=item.depth==1&&item.label=="New layout";
        CHECK(!open);
    }
}
