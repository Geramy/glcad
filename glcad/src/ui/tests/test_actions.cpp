#include "test.hpp"

#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "ui/actions.hpp"
#include "ui/menus.hpp"
#include "ui/shell.hpp"
#include "ui/tools.hpp"

using namespace gcapp;

namespace {

std::vector<std::string> offered(const SelectionSummary& s) {
    std::vector<std::string> out;
    for (const auto& a : selection_actions())
        if (a.applies(s)) out.push_back(a.command);
    return out;
}

std::vector<std::string> menu_offered(const SelectionSummary& s) {
    std::vector<std::string> out;
    for(const auto* action:SelectionActionFactory::build(s))out.push_back(action->command);
    return out;
}

bool has(const std::vector<std::string>& v, const char* cmd) {
    for (const auto& c : v)
        if (c == cmd) return true;
    return false;
}

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

void click_at(Shell& s, double x, double y, double z) {
    double px, py;
    CHECK(s.camera().project(gview::Point3::from(x, y, z), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
}

void right_at(Shell& s, double x, double y, double z=0) {
    s.build_frame();
    double px=0,py=0;
    CHECK(s.camera().project(gview::Point3::from(x,y,z),px,py));
    CHECK_MSG(s.canvas_bounds().contains({float(px),float(py)}),
              "context target projects outside canvas at %.1f, %.1f",px,py);
    s.pointer_move(px,py);s.build_frame();s.right_down(px,py);s.build_frame();
}

int panels(const Shell& s,const char* name) {
    int count=0;
    for(const auto& item:s.accessible_ui())
        if(item.role==gui::Context::Role::Panel && item.label==name)++count;
    return count;
}

void no_object_menu(Shell& s) {
    s.build_frame();
    CHECK(!s.context_menu_open());
    CHECK(panels(s,"Object options")==0);
    CHECK(panels(s,"Selection actions")==0);
}

void one_object_menu(const Shell& s) {
    CHECK(s.context_menu_open());
    CHECK(panels(s,"Object options")==0);
    CHECK(panels(s,"Selection actions")==1);
}

bool context_row(const Shell& s,const char* label) {
    gui::Rect menu{};
    for(const auto& item:s.accessible_ui())
        if(item.role==gui::Context::Role::Panel && item.label=="Selection actions")menu=item.bounds;
    for(const auto& item:s.accessible_ui())
        if(item.role==gui::Context::Role::Button && item.label==label &&
           menu.contains(item.bounds.centre()))return true;
    return false;
}

void line(Shell& s,double ax,double ay,double bx,double by) {
    auto& canvas=s.canvas();
    canvas.modify_shapes([&](auto& shapes) {
        gsnap::Shape shape;shape.a=gcm::to_lattice(canvas.volume(),ax,ay);
        shape.b=gcm::to_lattice(canvas.volume(),bx,by);shapes.push_back(shape);
    });
}

Shell two_block_shell() {
    Shell s = make_shell();
    draw_rect(s, "0,0", "4,0", "4,4", "0,4");
    extrude_at(s, 2.0, 2.0, "3");
    s.edit_sketch(0);
    draw_rect(s, "10,0", "14,0", "14,4", "10,4");
    extrude_at(s, 12.0, 2.0, "3");
    s.camera().look(gview::StandardView::IsoSE);
    s.camera().set_target(gview::Point3::from(7.0, 2.0, 1.5));
    s.build_frame();
    return s;
}

}

TEST(actions_empty_selection_offers_only_drawing_background_in_sketch_mode) {
    SelectionSummary s;
    CHECK(menu_offered(s)==std::vector<std::string>{"BACKGROUND"});
    s.sketch_mode=false;CHECK(menu_offered(s).empty());
}

TEST(actions_every_entry_names_a_registered_command_or_typed_handler) {
    Shell s;
    for (const auto& a : selection_actions()) {
        CHECK_MSG(a.handler!=SelectionAction::Command || s.commands().find(a.command) != nullptr,
                  "'%s' is not a registered command", a.command);
        CHECK(a.label != nullptr && a.label[0] != '\0');
        CHECK(a.applies != nullptr);
    }
}

TEST(every_command_is_reachable_without_typing_its_name) {
    static const std::set<std::string> elsewhere = {
        "END", "MID", "CEN", "NOD", "QUA", "INT", "INS",
        "PER", "TAN", "NEA", "APP", "EXT", "PAR", "GEO",
        "DELAY",
        "WALL", "DOOR", "WINDOW", "DOORFRAME", "HINGE",
        "DISCIPLINE",
        "BLOCKSPALETTE", "CONTENT",
        "SYMBOL",
        "DOORSWING", "DOORHINGE",
    };

    std::set<std::string> reachable = elsewhere;
    for (const auto& t : tool_defs()) reachable.insert(t.command);
    for (const auto& m : app_menus())
        for (const auto& it : m.items)
            if (!it.separator()) reachable.insert(it.command);

    Shell s;
    for (const auto& c : s.commands().all())
        CHECK_MSG(reachable.count(c.name) == 1,
                  "%s has no toolbar button and no menu item", c.name.c_str());
}

TEST(every_menu_item_names_a_command_that_runs) {
    Shell s;
    for (const auto& m : app_menus())
        for (const auto& it : m.items) {
            if (it.separator()) continue;
            CHECK_MSG(s.commands().by_name(it.command) != nullptr,
                      "menu item '%s' names no command", it.label.c_str());
        }
}

TEST(actions_two_bodies_offer_the_combines_and_the_mate) {
    SelectionSummary s;
    s.faces = 1;
    s.bodies = 2;
    s.two_bodies = true;
    const auto v = offered(s);
    CHECK(has(v, "UNION"));
    CHECK(has(v, "SUBTRACT"));
    CHECK(has(v, "INTERSECT"));
    CHECK(has(v, "MATE"));
    CHECK(has(v, "DIST"));
    CHECK(!has(v, "MOVE"));
    CHECK(!has(v, "EXTRUDE"));
    CHECK(!has(v, "HATCH"));
}

TEST(actions_a_lone_region_offers_the_pull_verbs) {
    SelectionSummary s;
    s.region = true;
    const auto v = offered(s);
    CHECK(has(v, "EXTRUDE"));
    CHECK(has(v, "OFFSET"));
    CHECK(has(v, "HATCH"));
    CHECK(!has(v, "MATE"));
    CHECK(!has(v, "UNION"));
    CHECK(!has(v, "ERASE"));
    CHECK(!has(v, "DIST"));
}

TEST(actions_sketch_shapes_offer_the_drafting_verbs) {
    SelectionSummary s;
    s.shapes = 2;
    const auto v = offered(s);
    CHECK(has(v, "MOVE"));
    CHECK(has(v, "COPY"));
    CHECK(has(v, "ROTATE"));
    CHECK(has(v, "MIRROR"));
    CHECK(has(v, "SCALE"));
    CHECK(has(v, "ERASE"));
    CHECK(has(v, "DIST"));
    CHECK(!has(v, "EXTRUDE"));
    CHECK(!has(v, "UNION"));
}

TEST(actions_factory_supports_annotations_and_preserves_hatch_transform_limits) {
    for(bool text:{false,true}) {
        SelectionSummary s;s.objects=2;
        if(text)s.texts=2;else s.dimensions=2;
        const auto actions=menu_offered(s);
        CHECK(has(actions,"MOVE"));CHECK(has(actions,"COPY"));CHECK(has(actions,"ERASE"));
        CHECK(!has(actions,"FILLETEDGE"));CHECK(!has(actions,"EXTRUDE"));
        CHECK(has(actions,"TEXTSMALLER")==text);CHECK(has(actions,"TEXTBIGGER")==text);
    }
    SelectionSummary hatch;hatch.objects=1;hatch.hatches=1;
    const auto actions=menu_offered(hatch);
    CHECK(has(actions,"ERASE"));CHECK(!has(actions,"MOVE"));CHECK(!has(actions,"SCALE"));
    CHECK(!has(actions,"EXTRUDE"));
}

TEST(actions_factory_merges_former_face_body_and_region_actions) {
    SelectionSummary face;face.faces=1;face.bodies=1;face.solid_items=1;
    face.planar_face=true;face.sketch_mode=false;
    const auto face_actions=menu_offered(face);
    CHECK(has(face_actions,"EXTRUDE"));CHECK(has(face_actions,"SKETCHONFACE"));
    CHECK(has(face_actions,"MASSPROP"));CHECK(has(face_actions,"ERASE"));
    CHECK(!has(face_actions,"MIRROR3D"));CHECK(!has(face_actions,"HATCH"));
    SelectionSummary body;body.bodies=1;body.solid_items=1;body.sketch_mode=false;
    const auto body_actions=menu_offered(body);
    CHECK(has(body_actions,"MIRROR3D"));CHECK(has(body_actions,"MASSPROP"));CHECK(has(body_actions,"ERASE"));
    CHECK(!has(body_actions,"SKETCHONFACE"));CHECK(!has(body_actions,"EXTRUDE"));
    SelectionSummary region;region.region=true;region.solid_items=1;
    region.can_revolve=true;region.editable_sketch=true;
    const auto region_actions=menu_offered(region);
    CHECK(has(region_actions,"EXTRUDE"));CHECK(has(region_actions,"REVOLVE"));
    CHECK(has(region_actions,"EDITSKETCH"));CHECK(has(region_actions,"HATCH"));
    CHECK(!has(region_actions,"MASSPROP"));
}

TEST(actions_factory_limits_mixed_and_bulk_solid_actions_but_keeps_multi_edge_rounding) {
    SelectionSummary mixed;mixed.objects=1;mixed.shapes=1;mixed.faces=1;mixed.bodies=1;mixed.solid_items=1;
    CHECK(menu_offered(mixed)==std::vector<std::string>{"ERASE"});
    SelectionSummary faces;faces.faces=2;faces.bodies=1;faces.solid_items=2;
    CHECK(menu_offered(faces)==std::vector<std::string>{"ERASE"});
    SelectionSummary edges;edges.edges=2;edges.bodies=1;edges.solid_items=2;
    const auto actions=menu_offered(edges);
    CHECK(has(actions,"FILLETEDGE"));CHECK(has(actions,"CHAMFEREDGE"));CHECK(has(actions,"ERASE"));
    CHECK(!has(actions,"EXTRUDE"));CHECK(!has(actions,"MOVE"));
}

TEST(actions_selecting_a_face_of_one_of_two_solids_reads_as_two_bodies) {
    Shell s = two_block_shell();
    CHECK_MSG(s.modeller().bodies().size() == 2, "%zu bodies", s.modeller().bodies().size());
    if (s.modeller().bodies().size() != 2) return;

    click_at(s, 4.0, 2.0, 1.5);
    s.build_frame();
    CHECK(s.selected_target().valid && s.selected_target().on_body);

    const SelectionSummary sum = s.selection_summary();
    CHECK(sum.faces == 1);
    CHECK(sum.bodies == 1);
    CHECK(sum.two_bodies);
    CHECK(!sum.region);
    CHECK(sum.shapes == 0);
}

TEST(actions_right_click_on_a_selected_face_opens_the_menu_and_escape_closes_it) {
    Shell s = two_block_shell();
    if (s.modeller().bodies().size() != 2) return;

    double px, py;
    CHECK(s.camera().project(gview::Point3::from(4.0, 2.0, 1.5), px, py));
    click_at(s, 4.0, 2.0, 1.5);
    s.build_frame();
    CHECK(s.selected_target().valid);
    no_object_menu(s);

    s.right_down(px, py);
    s.build_frame();
    one_object_menu(s);

    s.key_special(1);
    s.build_frame();
    CHECK(!s.context_menu_open());
}

TEST(actions_right_click_without_selection_offers_area_background) {
    Shell s = make_shell();
    s.build_frame();
    s.right_down(800, 500);
    s.build_frame();
    one_object_menu(s);
    CHECK(context_row(s,"Area background…"));
    s.key_special(1);s.build_frame();CHECK(!s.context_menu_open());
}

TEST(actions_wall_menu_uses_window_height_and_exposes_move_ungroup_and_properties) {
    for(double dpr:{1.,2.}) {
        Shell s;s.set_dpr(dpr);s.scale_ui(1.6);s.set_native_menus(true);s.set_viewport(1100*dpr,640*dpr);
        auto& c=s.canvas();CHECK(c.add_wall(gcm::to_lattice(c.volume(),-2,0),gcm::to_lattice(c.volume(),2,0),.3));
        s.build_frame();const auto canvas=s.canvas_bounds();
        double x,y;CHECK(s.camera().project(gview::Point3::from(0,0,0),x,y));
        CHECK(canvas.contains({float(x),float(y)}));s.pointer_move(x,y);s.build_frame();s.right_down(x,y);s.build_frame();
        one_object_menu(s);CHECK(s.selection_summary().block_refs==1);
        for(const auto* label:{"Move","Copy","Rotate","Mirror","Ungroup","Scale","Properties…","Erase"})
            CHECK_MSG(context_row(s,label),"missing wall action %s at DPR %g",label,dpr);
        for(const auto& item:s.accessible_ui())if(item.role==gui::Context::Role::Panel && item.label=="Selection actions") {
            CHECK(item.value=="Wall section");CHECK(item.bounds.y>=0);CHECK(item.bounds.bottom()<=640*dpr);
        }
    }
}

TEST(actions_left_click_selects_shapes_regions_and_notes_without_action_menus) {
    {
        auto s=make_shell();line(s,0,0,4,0);
        click_at(s,2,0,0);CHECK(s.canvas().selection().size()==1);no_object_menu(s);
    }
    {
        auto s=make_shell();draw_rect(s,"0,0","4,0","4,4","0,4");
        s.camera().set_target(gview::Point3::from(2,2,0));s.fit_width(16);
        click_at(s,2,2,0);CHECK(s.selected_target().valid);no_object_menu(s);
    }
    {
        auto s=make_shell();std::string error;CHECK(s.load_font(&error));
        auto& c=s.canvas();c.add_text(gcm::to_lattice(c.volume(),0,0),"Room note",.35);
        double lx=0,ly=0,hx=0,hy=0;c.text_bounds(c.texts()[0],lx,ly,hx,hy);
        click_at(s,(lx+hx)*.5,(ly+hy)*.5,0);
        CHECK(c.selected_texts().size()==1);no_object_menu(s);
        right_at(s,(lx+hx)*.5,(ly+hy)*.5);
        CHECK(c.selected_texts().size()==1);one_object_menu(s);
        CHECK(context_row(s,"Move"));
    }
}

TEST(actions_left_click_with_drawing_tool_keeps_the_tool_active_without_action_menus) {
    auto s=make_shell();line(s,0,0,4,0);s.activate_tool(ToolId::Line);
    click_at(s,2,0,0);CHECK(s.canvas().tool()==Tool::Line);no_object_menu(s);
    click_at(s,2,-2,0);CHECK(s.canvas().tool()==Tool::Line);no_object_menu(s);
    CHECK(s.canvas().shapes().size()==2);
}

TEST(actions_right_click_text_size_handler_changes_the_note_and_undo_restores_it) {
    auto s=make_shell();std::string error;CHECK(s.load_font(&error));
    auto& c=s.canvas();c.add_text(gcm::to_lattice(c.volume(),0,0),"Room note",.35);
    s.build_frame();
    double lx=0,ly=0,hx=0,hy=0;c.text_bounds(c.texts()[0],lx,ly,hx,hy);
    right_at(s,(lx+hx)*.5,(ly+hy)*.5);one_object_menu(s);
    CHECK(c.selected_texts().size()==1);CHECK(context_row(s,"Larger text"));
    gui::Rect row{};
    for(const auto& item:s.accessible_ui())
        if(item.role==gui::Context::Role::Button && item.label=="Larger text")row=item.bounds;
    CHECK(!row.empty());if(row.empty())return;
    const double before=c.texts()[0].height;
    const auto at=row.centre();s.pointer_move(at.x,at.y);s.build_frame();
    s.pointer_down(at.x,at.y);s.build_frame();s.pointer_up(at.x,at.y);s.build_frame();
    CHECK(!s.context_menu_open());CHECK(c.texts().size()==1);
    CHECK(c.texts()[0].text=="Room note");CHECK(std::abs(c.texts()[0].height-before*1.25)<1e-9);
    s.build_frame();
    CHECK(panels(s,"Object options")==0);CHECK(panels(s,"Selection actions")==0);
    s.command("UNDO");s.build_frame();
    CHECK(c.texts().size()==1);CHECK(std::abs(c.texts()[0].height-before)<1e-9);
}

TEST(actions_right_click_preserves_selected_member_and_retargets_an_unselected_edge) {
    auto s=make_shell();line(s,0,0,4,0);line(s,0,2,4,2);line(s,0,4,4,4);
    s.camera().set_target(gview::Point3::from(2,2,0));s.fit_width(16);
    s.canvas().set_selection({0,1});no_object_menu(s);
    right_at(s,2,0);one_object_menu(s);
    CHECK(s.canvas().selection()==std::vector<std::size_t>({0,1}));
    CHECK(context_row(s,"Move"));CHECK(context_row(s,"Copy"));
    CHECK(!context_row(s,"Union"));CHECK(!context_row(s,"Round edges…"));
    s.key_special(1);s.build_frame();
    right_at(s,2,4);one_object_menu(s);
    CHECK(s.canvas().selection()==std::vector<std::size_t>({2}));
    CHECK(context_row(s,"Move"));
}

TEST(actions_right_click_retargets_a_different_solid_without_left_click_menu) {
    auto s=two_block_shell();CHECK(s.modeller().bodies().size()==2);
    if(s.modeller().bodies().size()!=2)return;
    s.fit_width(40);s.build_frame();
    click_at(s,4,2,1.5);CHECK(s.selected_target().valid);no_object_menu(s);
    const auto first=s.selected_target().body;
    right_at(s,14,2,1.5);one_object_menu(s);
    CHECK(s.selected_target().valid && s.selected_target().on_body);
    CHECK(s.selected_target().body!=first);
    CHECK(s.selection_summary().faces==1);
}

TEST(actions_right_click_bulk_bodies_preserves_the_selection_and_safe_actions) {
    auto s=two_block_shell();CHECK(s.modeller().bodies().size()==2);
    if(s.modeller().bodies().size()!=2)return;
    s.select_body(0);s.select_body(1,true);no_object_menu(s);
    right_at(s,4,2,1.5);one_object_menu(s);
    CHECK(s.selection_summary().bodies==2);
    CHECK(context_row(s,"Erase"));
    CHECK(!context_row(s,"Move"));CHECK(!context_row(s,"Sketch on face"));
}

TEST(actions_right_click_mixed_sketch_and_body_preserves_both_and_offers_safe_actions) {
    auto s=two_block_shell();CHECK(s.modeller().bodies().size()==2);
    if(s.modeller().bodies().size()!=2)return;
    s.select_body(0);s.canvas().set_selection({0});no_object_menu(s);
    const auto before=s.selection_summary();CHECK(before.bodies==1);CHECK(before.objects==1);
    right_at(s,4,2,1.5);one_object_menu(s);
    const auto after=s.selection_summary();CHECK(after.bodies==1);CHECK(after.objects==1);
    CHECK(context_row(s,"Erase"));CHECK(!context_row(s,"Move"));
    CHECK(!context_row(s,"Extrude"));CHECK(!context_row(s,"Round edges…"));
}
