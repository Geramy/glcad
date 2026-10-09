#include "test.hpp"
#include "ui/shell.hpp"
#include "ui/block_library.hpp"
#include "ai/json.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace gcapp;
namespace {
std::string library_path(const char* file) {
    const auto dir=std::filesystem::temp_directory_path()/("glcad-library-tests-"+std::to_string(getpid()));
    std::filesystem::create_directories(dir);return (dir/file).string();
}
BlockLibrary fixture() {
    BlockLibrary library;auto& c=library.contents;const auto p=[&](double x,double y){return gcm::to_lattice(c.volume,x,y);};
    gdoc::Layer layer;layer.name="Symbols";layer.color=gdoc::Color::aci(3);c.layers.add(layer);
    gdoc::BlockDef leaf;leaf.name="Leaf";leaf.base=p(1,2);
    gsnap::Shape edge;edge.a=p(1,2);edge.b=p(2,2);edge.layer="Symbols";leaf.geometry.push_back(edge);
    leaf.notes.push_back({p(1.1,2.1),"Door A",.1,0,"Symbols"});
    gdoc::AttributeDef attribute;attribute.tag="TYPE";attribute.prompt="Door type";attribute.default_value="Door";
    attribute.position=p(1,2);attribute.height=.12;attribute.rotation=.25;attribute.preset=true;leaf.attributes.push_back(attribute);
    c.blocks.define(leaf);
    gdoc::BlockDef parent;parent.name="Door";parent.description="A nested door symbol";parent.base=p(3,4);
    gdoc::BlockRef ref;ref.name="Leaf";ref.insertion=p(3,4);ref.rotation=.5;ref.scale_x=ref.scale_y=2;parent.nested_refs.push_back(ref);
    c.blocks.define(parent);library.entries.push_back({"Door symbol","Architecture/Doors","Door"});
    gtext::TextStyleDef style;style.font="DejaVu Sans";library.text_styles.push_back(style);return library;
}
std::string bytes(const std::string& path){std::ifstream f(path,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
}

TEST(block_library_native_collection_round_trips_folders_dependencies_and_styles) {
    const auto library=fixture();std::string error;
    for(const auto ext:{"collection.gllib","collection.glcad"}) {
        const auto path=library_path(ext);CHECK_MSG(write_block_library(path,library,error),"%s",error.c_str());
        BlockLibrary loaded;CHECK_MSG(read_native_library(path,loaded,error),"%s",error.c_str());
        CHECK(loaded.entries.size()==1);if(loaded.entries.empty())continue;
        CHECK(loaded.entries[0].name=="Door symbol");CHECK(loaded.entries[0].folder=="Architecture/Doors");
        CHECK(loaded.contents.blocks.size()==2);CHECK(loaded.contents.blocks.references().empty());
        const auto* parent=loaded.contents.blocks.find("Door");CHECK(parent!=nullptr);
        if(parent){CHECK(parent->description=="A nested door symbol");CHECK(parent->nested_refs.size()==1);CHECK(parent->base.x==library.contents.blocks.find("Door")->base.x && parent->base.y==library.contents.blocks.find("Door")->base.y);}
        CHECK(loaded.text_styles==library.text_styles);
        const auto* leaf=loaded.contents.blocks.find("Leaf");CHECK(leaf&&leaf->notes.size()==1&&leaf->attributes.size()==1);
        Shell host;host.set_viewport(1600,1000);host.build_frame();
        CHECK_MSG(host.load_block_library(path),"%s",host.log().back().c_str());CHECK(host.canvas().blocks().size()==0);
        CHECK(host.insert_library_block(0,gcm::to_lattice(host.canvas().volume(),8,9)));
        const auto& ref=host.canvas().blocks().references().back();auto shapes=host.canvas().blocks().expand(ref,host.canvas().volume());
        CHECK(shapes.size()==1);if(!shapes.empty()){double x,y;gcm::to_world(host.canvas().volume(),shapes[0].a,x,y);CHECK(std::abs(x-8)<1e-5);CHECK(std::abs(y-9)<1e-5);}
    }
}

TEST(block_library_dwg_and_dxf_collections_keep_unplaced_nested_blocks) {
    std::string error;const auto library=fixture();
    for(const auto ext:{"blocks.dwg","blocks.dxf"}) {
        const auto path=library_path(ext);CHECK_MSG(write_block_library(path,library,error),"%s",error.c_str());
        Shell host;host.set_viewport(1600,1000);host.build_frame();
        CHECK_MSG(host.load_block_library(path),"%s",host.log().empty()?"":host.log().back().c_str());
        CHECK(host.canvas().shapes().empty());CHECK(host.canvas().blocks().size()==0);
        const auto& collection=host.block_library();CHECK(collection.entries.size()==2);
        const auto* parent=collection.contents.blocks.find("Door");CHECK(parent!=nullptr);
        if(parent){CHECK(parent->nested_refs.size()==1);CHECK(parent->base.x==library.contents.blocks.find("Door")->base.x && parent->base.y==library.contents.blocks.find("Door")->base.y);}
        const auto* leaf=collection.contents.blocks.find("Leaf");CHECK_MSG(leaf&&leaf->notes.size()==1&&leaf->attributes.size()==1,"%s: notes=%zu attributes=%zu",ext,leaf?leaf->notes.size():0,leaf?leaf->attributes.size():0);
        if(leaf&&!leaf->attributes.empty()) {
            const auto& attr=leaf->attributes.front();CHECK(attr.tag=="TYPE");CHECK(attr.prompt=="Door type");CHECK(attr.default_value=="Door");
            CHECK(attr.preset);CHECK(std::abs(attr.height-.12)<1e-6);CHECK(std::abs(attr.rotation-.25)<1e-6);
        }
        auto found=std::find_if(collection.entries.begin(),collection.entries.end(),[](const auto& e){return e.block=="Door";});
        if(found==collection.entries.end())continue;
        CHECK(host.insert_library_block(std::size_t(found-collection.entries.begin()),gcm::to_lattice(host.canvas().volume(),5,6)));
        auto shapes=host.canvas().blocks().expand(host.canvas().blocks().references().back(),host.canvas().volume());
        CHECK(shapes.size()==1);if(!shapes.empty()){double x,y;gcm::to_world(host.canvas().volume(),shapes[0].a,x,y);CHECK(std::abs(x-5)<1e-5);CHECK(std::abs(y-6)<1e-5);}
    }
}

TEST(block_library_merge_preserves_conflicts_and_reuses_definitions) {
    auto a=fixture(),b=fixture();std::string error;std::map<std::string,std::string> names;
    auto leaf=*b.contents.blocks.find("Leaf");leaf.notes[0].text="Different";b.contents.blocks.define(leaf);
    b.contents.layers.find("Symbols")->color=gdoc::Color::aci(1);b.text_styles[0].font="Other font";
    CHECK_MSG(merge_block_library(a,b,names,error),"%s",error.c_str());
    CHECK(names["Leaf"]!="Leaf");CHECK(names["Door"]!="Door");CHECK(a.contents.blocks.find("Leaf")->notes[0].text=="Door A");
    const auto blocks=a.contents.blocks.size(),layers=a.contents.layers.size(),styles=a.text_styles.size(),entries=a.entries.size();
    CHECK_MSG(merge_block_library(a,b,names,error),"%s",error.c_str());
    CHECK(a.contents.blocks.size()==blocks);CHECK(a.contents.layers.size()==layers);CHECK(a.text_styles.size()==styles);CHECK(a.entries.size()==entries);
    const auto* parent=a.contents.blocks.find(names["Door"]);CHECK(parent&&parent->nested_refs[0].name==names["Leaf"]);
}

TEST(block_library_selection_is_non_destructive_and_insertions_survive_drawing_files) {
    Shell s;s.set_viewport(1600,1000);s.build_frame();auto& c=s.canvas();const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    gsnap::Shape line;line.a=p(10,20);line.b=p(12,20);c.load_shapes({line});c.set_selection({0});
    c.add_text(p(10,21),"Custom label",.1);c.select_all();s.build_frame();
    CHECK(s.save_selection_to_library("My symbol","Custom/Equipment",p(10,20)));
    CHECK(c.shapes().size()==1);CHECK(c.texts().size()==1);CHECK(c.shapes()[0].a.x==line.a.x && c.shapes()[0].a.y==line.a.y);CHECK(c.blocks().size()==0);
    CHECK(s.insert_library_block(0,p(0,0)));s.build_frame();const auto count=c.blocks().size();
    s.command("UNDO");s.build_frame();CHECK(c.blocks().size()==0);CHECK(c.blocks().references().empty());
    s.command("REDO");s.build_frame();CHECK(c.blocks().size()==count);CHECK(c.blocks().references().size()==1);
    CHECK(s.insert_library_block(0,p(4,0)));s.build_frame();CHECK(c.blocks().size()==count);CHECK(c.blocks().references().size()==2);
    s.command("UNDO");s.build_frame();CHECK(c.blocks().references().size()==1);
    s.command("REDO");s.build_frame();CHECK(c.blocks().references().size()==2);
    for(const auto ext:{"embedded.glcad","embedded.dwg"}) {
        const auto path=library_path(ext);CHECK_MSG(s.save_file(path),"%s",s.log().back().c_str());
        Shell reopened;CHECK_MSG(reopened.open_file(path),"%s",reopened.log().back().c_str());
        CHECK(reopened.canvas().blocks().references().size()==2);
        const auto& ref=reopened.canvas().blocks().references().front();const auto* def=reopened.canvas().blocks().find(ref.name);
        CHECK_MSG(def&&def->geometry.size()==1&&def->notes.size()==1,"%s: geometry=%zu notes=%zu",ext,def?def->geometry.size():0,def?def->notes.size():0);
    }
}

TEST(block_library_invalid_files_and_failed_exports_leave_existing_state_intact) {
    auto library=fixture();std::string error;const auto good=library_path("safe.gllib");CHECK(write_block_library(good,library,error));
    Shell s;CHECK(s.load_block_library(good));const auto before=s.block_library().entries.size();
    auto broken=bytes(good);broken.resize(broken.size()/2);const auto bad=library_path("truncated.gllib");
    {std::ofstream f(bad,std::ios::binary);f<<broken;}
    CHECK(!s.load_block_library(bad));CHECK(s.block_library().entries.size()==before);
    auto def=*library.contents.blocks.find("Leaf");def.nested.push_back("Door");library.contents.blocks.define(def);
    const auto original=bytes(good);CHECK(!write_block_library(good,library,error));CHECK(bytes(good)==original);
    library=fixture();const auto dwg=library_path("safe.dwg");CHECK(write_block_library(dwg,library,error));const auto old=bytes(dwg);
    library.contents.blocks.make_dynamic("Door");CHECK(!write_block_library(dwg,library,error));CHECK(bytes(dwg)==old);
    CHECK(write_block_library(library_path("dynamic.gllib"),library,error));BlockLibrary loaded;
    CHECK(read_native_library(library_path("dynamic.gllib"),loaded,error));CHECK(loaded.contents.blocks.is_dynamic("Door"));
    CHECK(!write_block_library(library_path("dynamic.dxf"),library,error));
    gfile::Contents future;CHECK(gfile::load_file(good,future).ok);
    for(auto& frame:future.foreign)if(frame.kind==kBlockLibraryFrame) {
        auto json=gai::Json::parse(frame.payload.begin(),frame.payload.end());json["version"]=999;
        const auto payload=json.dump();frame.payload.assign(payload.begin(),payload.end());
    }
    const auto unsupported=library_path("future.glcad");CHECK(gfile::save_file(unsupported,future).ok);
    CHECK(!s.load_block_library(unsupported));CHECK(s.block_library().entries.size()==before);
}

TEST(block_library_palette_imports_folders_drags_symbols_and_collects_drawing_blocks) {
    namespace fs=std::filesystem;
    const auto root=fs::path(library_path("Shared collection"));fs::create_directories(root/"Equipment");
    std::string error;CHECK(write_block_library((root/"Equipment"/"Doors.gllib").string(),fixture(),error));
    Shell s;s.set_dpr(1);s.set_viewport(1600,1000);s.build_frame();
    s.command("LIBRARYLOAD \""+root.string()+"\"");s.build_frame();
    CHECK(s.block_library().entries.size()==1);if(s.block_library().entries.empty())return;
    CHECK(s.block_library().entries[0].folder=="Shared collection/Equipment/Architecture/Doors");
    CHECK(s.canvas().blocks().size()==0);
    const auto row=[&](const char* label){gui::Rect bounds{};for(const auto& item:s.accessible_ui())if(item.label==label)bounds=item.bounds;return bounds;};
    for(int i=0;i<8&&row("Door symbol").w==0;++i) {s.pointer_move(240,400);s.scroll(0,-1,false);s.build_frame();}
    std::string labels;for(const auto& item:s.accessible_ui())labels+=item.label+", ";
    const auto bounds=row("Door symbol");CHECK_MSG(bounds.w>0,"%s",labels.c_str());if(bounds.w==0)return;
    const auto from=bounds.centre();const double x=1000,y=450;
    s.pointer_move(from.x,from.y);s.build_frame();s.pointer_down(from.x,from.y);s.build_frame();
    s.pointer_move(x,y);s.build_frame();s.pointer_up(x,y);s.build_frame();
    CHECK(s.canvas().blocks().references().size()==1);CHECK(s.canvas().blocks().size()==2);
    Shell::FileAsk requested=Shell::FileAsk::Open;
    const auto saved=library_path("Palette share.gllib");
    s.set_file_hook([&](auto kind,auto done){requested=kind;done(saved);});
    s.command("LIBRARYSAVE");CHECK(requested==Shell::FileAsk::SaveLibrary);
    BlockLibrary reopened;CHECK(read_native_library(saved,reopened,error));CHECK(reopened.entries.size()==1);
    Shell second;second.set_viewport(1600,1000);second.build_frame();second.canvas().blocks()=s.canvas().blocks();
    second.command("LIBRARYCOLLECT");second.build_frame();CHECK(second.block_library().entries.size()==2);
    second.command("LIBRARYCOLLECT");CHECK(second.block_library().entries.size()==2);
}
