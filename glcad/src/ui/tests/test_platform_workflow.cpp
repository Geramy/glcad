#include "test.hpp"
#include "ui/shell.hpp"
#include "ui/ai_tools.hpp"
#include "engine/document_file.hpp"
#include "exchange/dxf.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace gcapp;

TEST(platform_suspend_recovers_every_tab_without_changing_active_document) {
    Shell s; s.set_viewport(1200,900); s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0"); s.canvas().type_point("1,0"); s.canvas().cancel();
    s.build_frame();
    const auto first=s.autosave_path();
    s.command("NEWTAB"); s.build_frame();
    s.canvas().set_tool(Tool::Circle);
    s.canvas().type_point("0,0"); s.canvas().type_point("2,0"); s.canvas().cancel();
    s.build_frame();
    const auto second=s.autosave_path();
    const auto undo_depth=s.document().undo_depth();
    const auto entity_count=s.document().entity_count();
    const auto count=s.canvas().shapes().size();
    CHECK(first!=second);
    CHECK(s.prepare_suspend());
    CHECK(s.canvas().shapes().size()==count);
    CHECK(s.autosave_path()==second);
    CHECK(s.document().undo_depth()==undo_depth);
    CHECK(s.document().entity_count()==entity_count);
    CHECK(std::filesystem::exists(first)); CHECK(std::filesystem::exists(second));
    Shell a,b;
    CHECK(a.open_file(first)); CHECK(b.open_file(second));
    CHECK(a.canvas().shapes().size()==1); CHECK(b.canvas().shapes().size()==1);
    if (!a.canvas().shapes().empty()) CHECK(a.canvas().shapes()[0].kind==gsnap::ShapeKind::Segment);
    if (!b.canvas().shapes().empty()) CHECK(b.canvas().shapes()[0].kind==gsnap::ShapeKind::Circle);
    CHECK(s.prepare_suspend());
    std::filesystem::remove(first); std::filesystem::remove(second);
}

TEST(platform_native_text_replacement_rejects_stale_targets_and_runs_commands) {
    Shell s; s.set_viewport(1200,900); s.canvas().set_display_unit(gunit::Length::Metre); s.build_frame();
    gui::Id id=0;
    for (const auto& a:s.accessible_ui()) if(a.label=="Command") id=a.id;
    CHECK(id!=0); CHECK(s.focus_accessible(id));
    CHECK(!s.replace_focused_text(id+1,"ERASE"));
    CHECK(s.replace_focused_text(id,"LINE")); s.build_frame();
    s.key_special(0); s.build_frame();
    CHECK(s.canvas().tool()==Tool::Line);
    CHECK(!s.focus_accessible(id+1));
}

TEST(platform_subscription_lapse_preserves_work_and_blocks_edit_routes) {
    Shell s; s.set_viewport(1200,900); s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    CHECK(s.canvas().type_point("0,0")); CHECK(s.canvas().type_point("2,0"));
    s.canvas().cancel(); s.build_frame();
    const auto count=s.canvas().shapes().size();
    const auto undo=s.document().undo_depth();
    s.set_product_edit_access(false); s.build_frame();
    CHECK(!s.product_edit_access()); CHECK(s.canvas().tool()==Tool::Select);
    s.command("ERASE"); s.command("UNDO"); s.command("LINE");
    s.activate_tool(ToolId::Circle);
    s.key_char("12"); s.key_special(0);
    s.pointer_down(600,450); s.pointer_move(650,500); s.pointer_up(650,500); s.build_frame();
    CHECK(s.canvas().shapes().size()==count); CHECK(s.document().undo_depth()==undo);
    CHECK(s.canvas().tool()==Tool::Select);
    CHECK(!s.set_feature_value(0,0,5)); CHECK(!s.round_edge(1));
    bool account=false;
    s.set_product_hook([&]{ account=true; }); s.command("ABOUT"); CHECK(account);
    bool export_requested=false;
    s.set_file_hook([&](Shell::FileAsk kind, auto reply) {
        export_requested=kind==Shell::FileAsk::Export;
        reply("");
    });
    s.command("EXPORT"); CHECK(export_requested);
    CHECK(s.autosave_now() || !s.document_dirty());
    Shell reopened; CHECK(reopened.open_file(s.autosave_path()));
    CHECK(reopened.canvas().shapes().size()==count);
    s.set_product_edit_access(true); s.build_frame();
    s.command("LINE"); CHECK(s.canvas().tool()==Tool::Line);
    std::filesystem::remove(s.autosave_path());
}

TEST(platform_file_picker_cannot_save_a_different_tab_or_import_after_lapse) {
    Shell s; s.set_viewport(1200,900); s.canvas().set_display_unit(gunit::Length::Metre); s.build_frame();
    std::function<void(std::string)> complete;
    s.set_file_hook([&](Shell::FileAsk, auto reply){ complete=std::move(reply); });
    s.command("SAVEAS"); CHECK(bool(complete));
    if (!complete) return;
    const auto origin=s.document_identity();
    s.command("NEWTAB"); s.build_frame(); CHECK(s.document_identity()!=origin);
    const auto path=std::filesystem::temp_directory_path()/("glcad-delayed-save-"+origin+".glcad");
    complete(path.string()); CHECK(!std::filesystem::exists(path));
    CHECK(!s.log().empty() && s.log().back().find("document changed")!=std::string::npos);
    complete={}; s.command("IMPORT"); CHECK(bool(complete));
    if (!complete) return;
    s.set_product_edit_access(false);
    complete("not-an-import.glcad");
    CHECK(!s.log().empty() && s.log().back().find("subscription")!=std::string::npos);
    CHECK(s.canvas().shapes().empty());
}

TEST(platform_expiry_recovery_excludes_an_unconfirmed_selection_drag) {
    Shell s; s.set_viewport(1200,900); s.canvas().set_display_unit(gunit::Length::Metre); auto& c=s.canvas();
    c.set_tool(Tool::Line); CHECK(c.type_point("0,0")); CHECK(c.type_point("2,0"));
    c.cancel(); c.set_tool(Tool::Select); c.snaps().settings.enabled=false;
    s.build_frame();
    double x,y,to_x,to_y;
    c.view().screen_of(1,0,x,y); c.view().screen_of(1,2,to_x,to_y);
    CHECK(c.select_and_drag(x,y)); c.update_selection_drag(to_x,to_y);
    double moved_x,moved_y;
    gcm::to_world(c.volume(),c.shapes()[0].a,moved_x,moved_y);
    CHECK(moved_y>1);
    s.set_product_edit_access(false); s.build_frame();
    gcm::to_world(c.volume(),c.shapes()[0].a,moved_x,moved_y);
    CHECK(std::fabs(moved_y)<1e-8);
    Shell recovered; CHECK(recovered.open_file(s.autosave_path()));
    CHECK(recovered.canvas().shapes().size()==1);
    if (!recovered.canvas().shapes().empty()) {
        gcm::to_world(recovered.canvas().volume(),recovered.canvas().shapes()[0].a,moved_x,moved_y);
        CHECK(std::fabs(moved_y)<1e-8);
    }
    std::filesystem::remove(s.autosave_path());
}

TEST(platform_annotation_layers_survive_layer_switch_native_reopen_and_export) {
    Shell s; s.set_viewport(1200,900); s.canvas().set_display_unit(gunit::Length::Metre);
    gdoc::Layer notes; notes.name="Notes"; s.layers().add(notes);
    gdoc::Layer fills; fills.name="Fills"; s.layers().add(fills);
    auto& c=s.canvas();
    c.add_text(gcm::to_lattice(c.volume(),1,1),"Layer retained",0.2,0,0,0,0,"","Notes");
    c.set_tool(Tool::Line);
    for (const auto* point : {"0,0","4,0","4,3","0,3","0,0"}) CHECK(c.type_point(point));
    c.cancel(); c.set_tool(Tool::Select); c.set_current_layer("Fills");
    CHECK(c.hatch_at_world(gcm::to_lattice(c.volume(),1,1)));
    s.build_frame();
    CHECK(c.texts().size()==1); CHECK(c.hatches().size()==1);
    if (c.texts().empty() || c.hatches().empty()) return;
    CHECK(c.texts()[0].layer=="Notes"); CHECK(c.hatches()[0].layer=="Fills");
    const auto stem=std::filesystem::temp_directory_path()/("glcad-layer-"+s.document_identity());
    const auto native=stem.string()+".glcad", dxf=stem.string()+".dxf";
    CHECK(s.save_file(native));
    Shell reopened; CHECK(reopened.open_file(native)); reopened.build_frame();
    CHECK(reopened.canvas().texts().size()==1); CHECK(reopened.canvas().hatches().size()==1);
    if (!reopened.canvas().texts().empty()) CHECK(reopened.canvas().texts()[0].layer=="Notes");
    if (!reopened.canvas().hatches().empty()) CHECK(reopened.canvas().hatches()[0].layer=="Fills");
    CHECK(reopened.export_file(dxf));
    std::ifstream stream(dxf);
    const std::string text{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
    const auto imported=gdxf::read(text,c.volume());
    CHECK(imported.ok);
    CHECK(imported.document.texts.size()==1); CHECK(imported.document.hatches.size()==1);
    if (!imported.document.texts.empty()) CHECK(imported.document.texts[0].layer=="Notes");
    if (!imported.document.hatches.empty()) CHECK(imported.document.hatches[0].layer=="Fills");
    std::filesystem::remove(native); std::filesystem::remove(dxf);
}

TEST(platform_file_commands_reach_the_native_picker_once_outside_the_frame) {
    for (const char* command : {"EXPORT", "SAVEAS", "OPEN", "IMPORT"}) {
        Shell s; s.set_viewport(1200,900); s.build_frame();
        int asks=0, depth=0, deepest=0;
        bool inside_frame=false;
        s.set_file_hook([&](Shell::FileAsk, auto reply) {
            ++asks; ++depth; deepest=std::max(deepest, depth);
            inside_frame=inside_frame || s.building_frame();
            if (depth < 8) s.build_frame();
            --depth;
            reply("");
        });
        for (const char* c = command; *c; ++c) s.key_char(std::string(1, *c));
        s.key_special(0);
        s.build_frame();
        s.build_frame();
        CHECK_MSG(asks == 1, "%s asked %d times", command, asks);
        CHECK(deepest == 1);
        CHECK(!inside_frame);
    }
}

namespace {
gai::Json plan_tool(Shell& shell, const char* name, gai::Json args) {
    gai::ToolCall call; call.function.name=name; call.function.arguments=args.dump();
    return gai::Json::parse(ai_tools::execute(shell,call));
}
}

TEST(platform_plan_hatches_stay_attached_through_edits_and_reopen) {
    Shell s; s.set_viewport(1200,900); s.build_frame();
    plan_tool(s,"set_drawing_discipline",{{"discipline","architectural"}});
    for (auto [a,b] : std::vector<std::pair<const char*,const char*>>{{"0,0","12000,0"},{"12000,0","12000,9000"},{"12000,9000","0,9000"},{"0,9000","0,0"}})
        plan_tool(s,"draw_wall",{{"from",a},{"to",b},{"type","exterior"}});
    for (auto [a,b] : std::vector<std::pair<const char*,const char*>>{{"7000,0","7000,9000"},{"7000,4500","12000,4500"},{"0,5500","7000,5500"}})
        plan_tool(s,"draw_wall",{{"from",a},{"to",b},{"type","interior"}});
    for (auto [a,b] : std::vector<std::pair<const char*,const char*>>{{"2500,0","3400,0"},{"7000,1500","7000,2400"},{"7000,6000","7000,6900"},{"4600,5500","5500,5500"}})
        plan_tool(s,"draw_door",{{"hinge",a},{"latch",b}});
    for (auto [a,b] : std::vector<std::pair<const char*,const char*>>{{"4000,0","6000,0"},{"1000,9000","3000,9000"},{"12000,6000","12000,8000"},{"12000,1500","12000,3500"},{"8500,9000","10500,9000"},{"8500,0","10500,0"}})
        plan_tool(s,"draw_window",{{"from",a},{"to",b}});
    for (auto [a,c] : std::vector<std::pair<const char*,const char*>>{{"2000,2000","#E9D8A6"},{"2000,7800","#A8DADC"},{"9500,7500","#CDB4DB"},{"9500,1500","#B5E48C"}})
        plan_tool(s,"area_background",{{"at",a},{"color",c},{"opacity","45"}});
    s.build_frame();
    const auto orphans = [](const Shell& shell) {
        std::size_t n = 0;
        for (const auto& h : shell.canvas().hatches()) n += h.orphaned ? 1 : 0;
        return n;
    };
    CHECK(s.canvas().hatches().size() >= 10);
    CHECK_MSG(orphans(s) == 0, "%zu orphaned before saving", orphans(s));
    const auto path=std::filesystem::temp_directory_path()/"glcad-plan-hatches.glcad";
    CHECK(s.save_file(path.string()));
    Shell r; r.set_viewport(1200,900); r.build_frame();
    CHECK(r.open_file(path.string())); r.build_frame(); r.build_frame();
    CHECK(r.canvas().hatches().size() == s.canvas().hatches().size());
    CHECK_MSG(orphans(r) == 0, "%zu orphaned after reopening", orphans(r));
    r.command("PLANCHECK");
    for (const auto& line : r.log()) CHECK_MSG(line.find("lost their boundary") == std::string::npos, "%s", line.c_str());
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".lock");
}

namespace {
Shell fitted_square(double w, double h) {
    Shell s; s.set_dpr(1.0); s.set_viewport(w, h); s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    for (const char* p : {"0,0", "8,0", "8,8", "0,8", "0,0"}) s.canvas().type_point(p);
    s.canvas().cancel(); s.canvas().set_tool(Tool::Select);
    s.build_frame();
    return s;
}
std::array<double, 4> screen_box(Shell& s) {
    std::array<double, 4> box{1e9, 1e9, -1e9, -1e9};
    for (auto [u, v] : std::vector<std::pair<double, double>>{{0, 0}, {8, 0}, {8, 8}, {0, 8}}) {
        double wx, wy, wz, px, py;
        s.sketch_plane().to_world(u, v, wx, wy, wz);
        if (!s.camera().project(gview::Point3::from(wx, wy, wz), px, py)) continue;
        box = {std::min(box[0], px), std::min(box[1], py), std::max(box[2], px), std::max(box[3], py)};
    }
    return box;
}
}

TEST(platform_fit_made_under_the_keyboard_refits_when_the_keyboard_hides) {
    Shell reference = fitted_square(1200, 900);
    reference.command("ZOOMEXTENTS"); reference.build_frame(); reference.build_frame();
    const auto want = screen_box(reference);

    Shell s = fitted_square(1200, 900);
    s.set_viewport(1200, 480); s.build_frame();
    s.command("ZOOMEXTENTS"); s.build_frame(); s.build_frame();
    s.set_viewport(1200, 900); s.build_frame(); s.build_frame();
    const auto got = screen_box(s);
    for (int i = 0; i < 4; ++i) CHECK_MSG(std::fabs(got[i] - want[i]) < 2.0, "edge %d: %g vs %g", i, got[i], want[i]);

    Shell moved = fitted_square(1200, 900);
    moved.set_viewport(1200, 480); moved.build_frame();
    moved.command("ZOOMEXTENTS"); moved.build_frame(); moved.build_frame();
    moved.scroll(0, 3, false); moved.build_frame();
    const double distance = moved.camera().distance();
    const auto target = moved.camera().target();
    moved.set_viewport(1200, 900); moved.build_frame(); moved.build_frame();
    CHECK(moved.camera().distance() == distance);
    CHECK(moved.camera().target().x.to_f64() == target.x.to_f64() && moved.camera().target().y.to_f64() == target.y.to_f64());
}

TEST(platform_small_holes_are_round_at_any_zoom) {
    std::size_t counts[2]{};
    for (int zoom = 0; zoom < 2; ++zoom) {
        Shell s; s.set_dpr(1.0); s.set_viewport(1200, 900); s.canvas().set_display_unit(gunit::Length::Metre);
        if (zoom) for (int i = 0; i < 12; ++i) s.scroll(0, -3, false);
        s.build_frame();
        s.canvas().set_tool(Tool::Circle);
        CHECK(s.canvas().type_point("0,0")); CHECK(s.canvas().type_point("0.009,0"));
        s.canvas().cancel(); s.canvas().set_tool(Tool::Select);
        s.build_frame();
        counts[zoom] = s.sketch_segment_count();
    }
    CHECK_MSG(counts[0] >= 64, "%zu segments for an 18 mm bore", counts[0]);
    CHECK_MSG(counts[0] == counts[1], "%zu vs %zu after zooming out", counts[0], counts[1]);
}
