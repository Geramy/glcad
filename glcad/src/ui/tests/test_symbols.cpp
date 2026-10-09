#include "test.hpp"
#include "ui/shell.hpp"
#include "ui/symbols.hpp"
#include "ui/ai_tools.hpp"
#include "ui/mcp.hpp"
#include <cmath>
#include <filesystem>
#include <unistd.h>

namespace {
using namespace gcapp;
gai::Json symbol_tool(Shell& shell,const char* name,gai::Json args=gai::Json::object()) {
    gai::ToolCall call;call.function.name=name;call.function.arguments=args.dump();
    return gai::Json::parse(ai_tools::execute(shell,call));
}
Shell symbol_shell() {Shell s;s.set_dpr(1);s.set_viewport(1200,800);s.canvas().set_display_unit(gunit::Length::Metre);return s;}
bool same_point(gcm::Lattice2 a,gcm::Lattice2 b) {return a.x==b.x && a.y==b.y;}
bool near_point(gcm::Lattice2 a,gcm::Lattice2 b) {return std::abs(a.x-b.x)<=3 && std::abs(a.y-b.y)<=3;}
bool has_note(const gdoc::BlockDef* def,const std::string& text) {
    if(def)for(const auto& n:def->notes)if(n.text==text)return true;
    return false;
}
}
TEST(symbol_catalog_and_arrows_use_movable_blocks_and_validate_before_edit) {
    auto shell=symbol_shell();auto& c=shell.canvas();
    const auto catalog=symbol_tool(shell,"list_symbols");CHECK(catalog["catalog"].size()==6);
    CHECK(symbol_tool(shell,"symbol_legend",{{"at","0,0"}}).contains("error"));
    for(const auto& item:symbol_catalog()) {
        CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol",item.id},{"at","2,3"},{"size","250mm"},{"rotation","90"}}).contains("error"));
        const auto& ref=c.blocks().references().back();CHECK(std::abs(ref.scale_x-.25)<1e-9);
        CHECK(std::abs(ref.rotation-1.5707963267948966)<1e-9);CHECK(!c.blocks().expand(ref,c.volume()).empty());
    }
    CHECK(!symbol_tool(shell,"draw_arrow",{{"from","1,1"},{"to","1,4"},{"direction","both"}}).contains("error"));
    const auto& arrow=c.blocks().references().back();CHECK(std::abs(arrow.scale_x-3)<1e-9);
    const auto shapes=c.blocks().expand(arrow,c.volume());CHECK(shapes.size()==5);
    CHECK(same_point(shapes[0].a,gcm::to_lattice(c.volume(),1,1)));CHECK(near_point(shapes[0].b,gcm::to_lattice(c.volume(),1,4)));
    const auto serial=c.edit_serial();
    CHECK(symbol_tool(shell,"draw_arrow",{{"from","0,0"},{"to","0,0"}}).contains("error"));
    CHECK(symbol_tool(shell,"draw_arrow",{{"from","0,0"},{"to","1,1"},{"direction","invalid"}}).contains("error"));
    CHECK(symbol_tool(shell,"draw_symbol",{{"symbol","unknown"},{"at","0,0"}}).contains("error"));
    CHECK(symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","0,0"},{"size","-2m"}}).contains("error"));
    CHECK(symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","nan,0"}}).contains("error"));
    CHECK(c.edit_serial()==serial);CHECK(c.blocks().references().size()==7);
    c.undo();CHECK(c.blocks().references().size()==6);c.redo();CHECK(c.blocks().references().size()==7);
}
TEST(symbol_legend_updates_counts_retains_movement_and_undo_restores_content) {
    auto shell=symbol_shell();auto& c=shell.canvas();
    CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","0,0"}}).contains("error"));
    CHECK(!symbol_tool(shell,"symbol_legend",{{"at","4,5"},{"scale","2"}}).contains("error"));
    const auto original=c.blocks().references()[1].name;CHECK(has_note(c.blocks().find(original),"1"));
    CHECK(c.blocks().find(original)->nested_refs.size()==1);CHECK(used_symbols(c.blocks()).size()==1);
    c.select_reference(1);CHECK(c.selected_references().size()==1);
    c.modify_references([&](auto& refs){refs[1].insertion=gcm::to_lattice(c.volume(),8,9);refs[1].rotation=.7;});
    CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","1,0"}}).contains("error"));
    const auto serial=c.edit_serial();const auto result=symbol_tool(shell,"symbol_legend");CHECK(!result.contains("error"));CHECK(result["updated"]==true);
    CHECK(c.edit_serial()==serial);CHECK(c.blocks().references().size()==3);
    const auto ref=c.blocks().references()[1];CHECK(ref.name==original);
    CHECK(same_point(ref.insertion,gcm::to_lattice(c.volume(),8,9)));CHECK(ref.scale_x==2);CHECK(ref.rotation==.7);
    CHECK(has_note(c.blocks().find(ref.name),"2"));CHECK(used_symbols(c.blocks())[0].count==2);
    c.undo();CHECK(c.blocks().references()[1].name==original);CHECK(has_note(c.blocks().find(original),"1"));
    c.redo();CHECK(c.blocks().references()[1].name==ref.name);CHECK(has_note(c.blocks().find(ref.name),"2"));
    gdoc::BlockDef custom;custom.name="Fire extinguisher";custom.geometry=c.blocks().find(c.blocks().references()[0].name)->geometry;c.blocks().define(custom);
    gdoc::BlockDef external=custom;external.name="External plan";external.is_xref=true;c.blocks().define(external);
    c.modify_references([&](auto& refs){gdoc::BlockRef r;r.name=custom.name;refs.push_back(r);r.name=external.name;refs.push_back(r);});
    CHECK(used_symbols(c.blocks()).size()==2);
    CHECK(!symbol_tool(shell,"symbol_legend").contains("error"));
    CHECK(has_note(c.blocks().find(c.blocks().references()[1].name),custom.name));
}
TEST(symbols_and_legend_survive_native_round_trip_and_update) {
    auto shell=symbol_shell();auto& c=shell.canvas();
    CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","section"},{"at","2,3"}}).contains("error"));
    CHECK(!symbol_tool(shell,"draw_arrow",{{"from","3,3"},{"to","5,4"}}).contains("error"));
    CHECK(!symbol_tool(shell,"draw_door",{{"hinge","0,0"},{"latch","1,0"},{"framing","none"}}).contains("error"));
    c.set_selection({0});CHECK(c.set_door_flow("both"));
    CHECK(!symbol_tool(shell,"symbol_legend",{{"at","8,5"}}).contains("error"));
    const auto path=(std::filesystem::temp_directory_path()/("glcad-symbols-"+std::to_string(getpid())+".glcad")).string();
    CHECK(shell.save_file(path));
    shell.close_tab(0);
    const auto cwd=std::filesystem::current_path();auto loaded=symbol_shell();CHECK(loaded.open_file(path));std::filesystem::current_path(cwd);
    auto& restored=loaded.canvas();CHECK(restored.blocks().references().size()==3);
    CHECK(used_symbols(restored.blocks()).size()==2);
    CHECK(used_symbols(restored).size()==3);CHECK(restored.door_flow_shapes().size()==5);
    const auto& ref=restored.blocks().references()[2];CHECK(is_symbol_legend(restored.blocks(),ref.name));
    const auto* def=restored.blocks().find(ref.name);CHECK(def->nested_refs.size()==2);CHECK(has_note(def,"Section marker"));
    CHECK(has_note(def,"Swing door"));CHECK(def->geometry.size()==11);
    CHECK(!restored.blocks().expand(ref,restored.volume()).empty());CHECK(!restored.reference_labels(ref).empty());
    CHECK(!symbol_tool(loaded,"symbol_legend").contains("error"));CHECK(restored.blocks().references().size()==3);
    std::filesystem::remove(path);
}
TEST(mcp_symbols_and_discoverable_commands_share_the_same_tool_path) {
    auto shell=symbol_shell();mcp::Server server(shell);using gai::Json;
    const auto request=[](int id,const char* method,Json params=Json::object()){return Json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}};};
    CHECK(server.handle(request(1,"initialize",{{"protocolVersion","2025-11-25"}}))->contains("result"));
    for(const char* name:{"SYMBOLS","SYMBOL","ARROW","SYMBOLLEGEND"})CHECK(shell.commands().find(name));
    shell.command("SYMBOLS");shell.build_frame();CHECK(shell.canvas().blocks().references().empty());
    auto drawn=server.handle(request(2,"tools/call",{{"name","draw_symbol"},{"arguments",{{"symbol","datum"},{"at","1,2"}}}}));
    CHECK(!drawn->at("result")["isError"].get<bool>());
    auto legend=server.handle(request(3,"tools/call",{{"name","symbol_legend"},{"arguments",{{"at","4,2"}}}}));
    CHECK(!legend->at("result")["isError"].get<bool>());CHECK(shell.canvas().blocks().references().size()==2);
    shell.command("SYMBOLLEGEND");CHECK(shell.canvas().blocks().references().size()==2);
    CHECK(used_symbols(shell.canvas().blocks()).size()==1);
}
TEST(symbol_legend_counts_semantic_compounds_and_preserves_flow_samples) {
    auto shell=symbol_shell();auto& c=shell.canvas();
    CHECK(!symbol_tool(shell,"draw_door",{{"hinge","0,0"},{"latch","1,0"},{"framing","none"}}).contains("error"));
    c.set_selection({0});CHECK(c.set_door_flow("both"));
    CHECK(!symbol_tool(shell,"draw_door",{{"hinge","3,0"},{"latch","4,0"},{"framing","none"}}).contains("error"));
    CHECK(!symbol_tool(shell,"draw_window",{{"from","6,0"},{"to","8,0"},{"framing","none"}}).contains("error"));
    CHECK(!symbol_tool(shell,"draw_door",{{"hinge","10,0"},{"latch","11,0"},{"component","frame"}}).contains("error"));
    CHECK(!symbol_tool(shell,"draw_door",{{"hinge","12,0"},{"latch","13,0"},{"component","hinge"}}).contains("error"));
    CHECK(!symbol_tool(shell,"draw_door",{{"hinge","15,0"},{"latch","18,0"},{"kind","rollup"},{"framing","none"}}).contains("error"));
    const auto usage=used_symbols(c);CHECK(usage.size()==5);
    bool swing=false;
    for(const auto& row:usage) {
        if(row.label=="Swing door") {CHECK(row.count==2);CHECK(row.geometry.size()==7);swing=true;}
        else CHECK(row.count==1);
    }
    CHECK(swing);
    const auto before=c.shapes();
    CHECK(!symbol_tool(shell,"symbol_legend",{{"at","20,10"}}).contains("error"));
    CHECK(c.shapes().size()==before.size());
    for(std::size_t i=0;i<before.size();++i) {CHECK(same_point(c.shapes()[i].a,before[i].a));CHECK(c.shapes()[i].poly==before[i].poly);CHECK(c.shapes()[i].attrs.user==before[i].attrs.user);}
    const auto& ref=c.blocks().references().back();const auto* def=c.blocks().find(ref.name);
    CHECK(has_note(def,"Swing door"));CHECK(has_note(def,"Window"));CHECK(has_note(def,"Rollup door"));CHECK(!def->geometry.empty());
    c.modify_shapes([](auto& shapes){shapes.clear();});
    CHECK(!symbol_tool(shell,"symbol_legend").contains("error"));
    CHECK(has_note(c.blocks().find(c.blocks().references().back().name),"No symbols used"));
    CHECK(used_symbols(c).empty());
    gsnap::Shape imported;imported.kind=gsnap::ShapeKind::Arc;imported.a=gcm::to_lattice(c.volume(),1,1);
    imported.radius=gcm::i64(std::llround(1/c.volume().inv_scale()));imported.layer="A-DOOR";
    imported.end_angle=1.5707963267948966;c.modify_shapes([&](auto& shapes){shapes.push_back(imported);});
    CHECK(used_symbols(c).size()==1);CHECK(used_symbols(c)[0].count==1);
}
TEST(symbol_marker_and_legend_text_bounds_align_with_geometry) {
    auto shell=symbol_shell();std::string error;CHECK(shell.load_font(&error));auto& c=shell.canvas();
    CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","section"},{"at","0,0"},{"size","1m"}}).contains("error"));
    const auto bounds=[&](const gdoc::BlockNote& note,double& lx,double& ly,double& hx,double& hy) {
        Canvas::TextItem text{note.pos,note.text,note.height,note.angle,note.halign,note.valign,note.width,note.style};
        CHECK(c.labels_bounds(c.note_labels(text),lx,ly,hx,hy));
    };
    const auto* marker=c.blocks().find(c.blocks().references()[0].name);CHECK(marker->notes.size()==2);
    for(const auto& note:marker->notes) {
        double lx,ly,hx,hy;bounds(note,lx,ly,hx,hy);
        CHECK(lx>=-.3 && hx<=.3);CHECK(ly>=-.3 && hy<=.3);
        if(note.text=="A")CHECK(ly>0);else CHECK(hy<0);
    }
    CHECK(!symbol_tool(shell,"symbol_legend",{{"at","4,3"}}).contains("error"));
    const auto* legend=c.blocks().find(c.blocks().references().back().name);
    for(const auto& note:legend->notes) {
        double lx,ly,hx,hy;bounds(note,lx,ly,hx,hy);
        const bool heading=note.text=="Symbol legend" || note.text=="Count";
        CHECK(std::abs((ly+hy)*.5-(heading?.12:-.68))<1e-5);
    }
}
TEST(symbol_legend_auto_updates_in_source_document_transaction_without_block_growth) {
    auto shell=symbol_shell();auto& c=shell.canvas();shell.sync_document();
    CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","0,0"}}).contains("error"));
    shell.sync_document();
    CHECK(!symbol_tool(shell,"symbol_legend",{{"at","4,3"},{"scale","2"}}).contains("error"));
    shell.sync_document();
    const auto name=c.blocks().references().back().name;
    const auto definitions=c.blocks().size();
    c.modify_references([&](auto& refs){refs.back().insertion=gcm::to_lattice(c.volume(),9,8);refs.back().rotation=.4;});
    shell.sync_document();
    const auto assert_placement=[&] {
        const auto& refs=c.blocks().references();bool found=false;
        for(const auto& ref:refs)if(ref.name==name) {
            found=true;CHECK(same_point(ref.insertion,gcm::to_lattice(c.volume(),9,8)));
            CHECK(ref.rotation==.4);CHECK(ref.scale_x==2);
        }
        CHECK(found);
    };
    CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","2,0"}}).contains("error"));
    CHECK(has_note(c.blocks().find(name),"2"));CHECK(c.blocks().size()==definitions);assert_placement();
    shell.sync_document();
    CHECK(shell.undo_through_document(false));CHECK(c.blocks().references().size()==2);
    CHECK(has_note(c.blocks().find(name),"1"));assert_placement();
    CHECK(shell.undo_through_document(true));CHECK(c.blocks().references().size()==3);
    CHECK(has_note(c.blocks().find(name),"2"));assert_placement();
    c.modify_references([&](auto& refs){refs.erase(std::remove_if(refs.begin(),refs.end(),[&](const auto& ref){return ref.name!=name;}),refs.end());});
    shell.sync_document();CHECK(has_note(c.blocks().find(name),"No symbols used"));
    CHECK(c.blocks().size()==definitions);assert_placement();
    CHECK(shell.undo_through_document(false));CHECK(has_note(c.blocks().find(name),"2"));
    CHECK(shell.undo_through_document(true));CHECK(has_note(c.blocks().find(name),"No symbols used"));
    shell.build_frame();shell.build_frame();
    const auto revision=c.blocks().revision(),document=shell.document_revision();
    for(int i=0;i<12;++i)shell.build_frame();
    CHECK(c.blocks().revision()==revision);CHECK(shell.document_revision()==document);
    CHECK(c.blocks().size()==definitions);
}
TEST(symbol_legend_auto_updates_semantic_edits_and_native_reopened_drawing) {
    auto shell=symbol_shell();auto& c=shell.canvas();
    CHECK(!symbol_tool(shell,"draw_door",{{"hinge","0,0"},{"latch","1,0"},{"framing","none"}}).contains("error"));
    CHECK(!symbol_tool(shell,"symbol_legend",{{"at","4,3"}}).contains("error"));
    shell.sync_document();const auto name=c.blocks().references().back().name;
    const auto definitions=c.blocks().size();CHECK(c.blocks().find(name)->geometry.size()==6);
    c.set_selection({0});CHECK(c.set_door_flow("both"));shell.sync_document();
    CHECK(c.blocks().find(name)->geometry.size()==11);CHECK(c.blocks().size()==definitions);
    CHECK(shell.undo_through_document(false));CHECK(c.blocks().find(name)->geometry.size()==6);
    CHECK(c.door_flow_shapes().empty());
    CHECK(shell.undo_through_document(true));CHECK(c.blocks().find(name)->geometry.size()==11);
    CHECK(c.door_flow_shapes().size()==5);
    CHECK(!symbol_tool(shell,"draw_window",{{"from","6,0"},{"to","8,0"},{"framing","none"}}).contains("error"));
    CHECK(has_note(c.blocks().find(name),"Window"));CHECK(c.blocks().size()==definitions);
    const auto path=(std::filesystem::temp_directory_path()/("glcad-auto-legend-"+std::to_string(getpid())+".glcad")).string();
    CHECK(shell.save_file(path));shell.close_tab(0);
    const auto cwd=std::filesystem::current_path();auto loaded=symbol_shell();CHECK(loaded.open_file(path));std::filesystem::current_path(cwd);
    auto& restored=loaded.canvas();
    CHECK(!symbol_tool(loaded,"draw_door",{{"hinge","3,0"},{"latch","4,0"},{"framing","none"}}).contains("error"));
    CHECK(has_note(restored.blocks().find(name),"2"));CHECK(restored.blocks().size()==definitions);
    loaded.sync_document();CHECK(loaded.undo_through_document(false));
    CHECK(!has_note(restored.blocks().find(name),"2"));CHECK(has_note(restored.blocks().find(name),"Window"));
    CHECK(loaded.undo_through_document(true));CHECK(has_note(restored.blocks().find(name),"2"));
    loaded.close_tab(0);std::filesystem::remove(path);
}
TEST(symbol_legend_source_and_derived_changes_share_undo_in_both_command_modes) {
    for(bool document_mode:{false,true}) {
        auto shell=symbol_shell();shell.set_document_undo(document_mode);auto& c=shell.canvas();shell.build_frame();
        CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","0,0"}}).contains("error"));shell.build_frame();
        CHECK(!symbol_tool(shell,"symbol_legend",{{"at","4,3"}}).contains("error"));shell.build_frame();
        const auto name=c.blocks().references().back().name;
        const auto depth=shell.undo_depth();
        CHECK(!symbol_tool(shell,"draw_symbol",{{"symbol","north"},{"at","2,0"}}).contains("error"));shell.build_frame();
        CHECK(shell.undo_depth()==depth+1);CHECK(has_note(c.blocks().find(name),"2"));
        shell.command("UNDO");shell.build_frame();CHECK(c.blocks().references().size()==2);
        CHECK(has_note(c.blocks().find(name),"1"));
        shell.command("REDO");shell.build_frame();CHECK(c.blocks().references().size()==3);
        CHECK(has_note(c.blocks().find(name),"2"));
        shell.command("UNDO");shell.command("UNDO");shell.build_frame();
        CHECK(c.blocks().references().size()==1);
        shell.command("REDO");shell.build_frame();CHECK(c.blocks().references().size()==2);
        CHECK(has_note(c.blocks().find(name),"1"));
    }
}
