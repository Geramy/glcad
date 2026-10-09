#include "ui/shell.hpp"
#include "ui/symbols.hpp"
#include "ui/solids_file.hpp"
#include "engine/appconfig.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace gcapp {
namespace {
std::string ext_of(const std::string& path) {
    auto ext=std::filesystem::path(path).extension().string();
    for(auto& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));return ext;
}
bool visible_block(const gdoc::BlockDef& def) {
    return !def.name.empty()&&def.name[0]!='*'&&!def.is_xref&&def.source_owner!="glcad.wall"&&
        !def.name.starts_with("__glcad_");
}
std::string personal_library_path() {return (std::filesystem::path(gfile::app_config_path()).parent_path()/"object-library.gllib").string();}
}

void Shell::persist_block_library() {
    if(!config_loaded_)return;
    std::string error;
    if(!write_block_library(personal_library_path(),block_library_,error))log_line("Library is available this session, but could not be saved: "+error);
}

bool Shell::save_block_library(const std::string& path) const {
    std::string error;
    if(!document_path_.empty()&&DocumentLock::identity(path)==DocumentLock::identity(document_path_)) {
        const_cast<Shell*>(this)->log_line("Choose a library file separate from the open drawing");return false;
    }
    const bool ok=write_block_library(path,block_library_,error);
    const_cast<Shell*>(this)->log_line(ok?"Saved "+std::to_string(block_library_.entries.size())+" library items to "+path:"Library save: "+error);
    return ok;
}

bool Shell::load_block_library(const std::string& path) {
    namespace fs=std::filesystem;
    std::error_code ec;std::vector<fs::path> files;
    const bool directory=fs::is_directory(path,ec);
    if(directory) {
        for(fs::recursive_directory_iterator it(path,fs::directory_options::skip_permission_denied,ec),end;it!=end&&!ec;it.increment(ec)) {
            if(it.depth()>16){it.disable_recursion_pending();continue;}
            if(!it->is_regular_file(ec))continue;
            const auto ext=ext_of(it->path().string());
            if(ext==".gllib"||ext==".glcad"||ext==".dwg"||ext==".dxf")files.push_back(it->path());
            if(files.size()>1000){log_line("Library folder contains more than 1000 drawing files");return false;}
        }
        if(ec){log_line("Cannot read library folder: "+ec.message());return false;}
        std::sort(files.begin(),files.end());
    } else files.emplace_back(path);
    if(files.empty()){log_line("No block collections found in "+path);return false;}
    auto staged=block_library_;
    for(const auto& file:files) {
        BlockLibrary incoming;std::string error;
        const auto ext=ext_of(file.string());
        bool collection=false;
        if(ext==".gllib"||ext==".glcad")collection=read_native_library(file.string(),incoming,error);
        if(!collection) {
            if(ext==".gllib"){log_line("Library load: "+error);return false;}
            Shell source;
            if(ext==".glcad") {
                gfile::Contents contents;const auto loaded=gfile::load_file(file.string(),contents);
                if(std::any_of(contents.foreign.begin(),contents.foreign.end(),[](const auto& frame){return frame.kind==kBlockLibraryFrame;})) {
                    log_line("Library load: "+error);return false;
                }
                source.vol_=contents.volume;
                if(!loaded.ok||!source.load_contents(contents,file.string(),true,"library reader")){log_line("Cannot read library drawing: "+file.string());return false;}
            } else if(ext==".dwg"||ext==".dxf") {
                if(!source.import_file(file.string(),false)){log_line("Library load: "+(source.log_.empty()?file.string():source.log_.back()));return false;}
                for(const auto& message:source.log_)if(message.find("not imported")!=std::string::npos||message.find("not decoded")!=std::string::npos)
                    log_line("Library "+file.filename().string()+": "+message);
            } else {log_line("Libraries can be .gllib, .glcad, .dwg, .dxf or a folder");return false;}
            source.leave_paper_space();source.edit_sketch(0);
            incoming.contents.volume=source.vol_;incoming.contents.blocks=source.canvas_.blocks();
            incoming.contents.blocks.set_references({});incoming.contents.layers=source.layers_;incoming.contents.linetypes=source.linetypes_;
            incoming.text_styles=source.text_styles_.all();
            std::vector<std::string> roots;
            for(const auto& def:source.canvas_.blocks().all())if(visible_block(def)&&!is_symbol_legend(source.canvas_.blocks(),def.name)) {
                incoming.entries.push_back({def.name,file.stem().string(),def.name});roots.push_back(def.name);
            }
            if(!source.canvas_.shapes().empty()||!source.canvas_.texts().empty()||!source.canvas_.blocks().references().empty()) {
                gdoc::BlockDef def;def.name=file.stem().string();
                for(unsigned n=2;incoming.contents.blocks.find(def.name);++n)def.name=file.stem().string()+"_"+std::to_string(n);
                def.geometry=source.canvas_.shapes();def.nested_refs=source.canvas_.blocks().references();
                for(const auto& t:source.canvas_.texts())def.notes.push_back({t.pos,source.canvas_.resolved_text(t),t.height,t.angle,t.layer,t.halign,t.valign,t.width,t.style,t.attrs});
                incoming.entries.push_back({def.name,file.stem().string(),def.name});roots.push_back(def.name);incoming.contents.blocks.define(std::move(def));
                if(!source.canvas_.dimensions().empty()||!source.canvas_.hatches().empty()||!source.canvas_.splines().empty()) {
                    log_line("Library load: model-space dimensions, hatches and splines must be converted to block geometry first: "+file.string());return false;
                }
            }
            incoming=library_subset(incoming,roots);
        }
        if(directory)for(auto& entry:incoming.entries) {
            const auto relative=file.parent_path().lexically_relative(fs::path(path));
            const auto prefix=library_folder(fs::path(path).filename().string()+"/"+relative.generic_string());
            entry.folder=library_folder(prefix+"/"+entry.folder);
        }
        if(incoming.entries.empty()){log_line("No reusable blocks in "+file.string());return false;}
        std::map<std::string,std::string> names;
        if(!merge_block_library(staged,incoming,names,error)){log_line("Library load: "+error);return false;}
    }
    block_library_=std::move(staged);library_drag_command_.clear();library_folders_.clear();library_folders_.insert("Library");
    for(const auto& entry:block_library_.entries) {
        std::string folder="Library";std::istringstream in(entry.folder);std::string part;
        while(std::getline(in,part,'/')){folder+="/"+part;library_folders_.insert(folder);}
    }
    drafting_feature_panel_=3;library_scroll_=0;persist_block_library();
    log_line("Loaded block collection: "+path+" ("+std::to_string(block_library_.entries.size())+" library items)");return true;
}

bool Shell::save_selection_to_library(const std::string& name,const std::string& folder,gcm::Lattice2 base) {
    if(canvas_.selection_count()==0){log_line("Select the objects to save as a symbol first");return false;}
    if(!canvas_.selected_dimensions().empty()||!canvas_.selected_hatches().empty()) {
        log_line("Convert dimensions and hatches to block geometry before saving this selection");return false;
    }
    if(name.empty()||name.find_first_of("\n\r/\\")!=std::string::npos){log_line("Enter a symbol name without slashes or line breaks");return false;}
    const auto destination=library_folder(folder);
    if(!folder.empty()&&destination.empty()){log_line("Enter a valid library folder");return false;}
    BlockLibrary source;source.contents.volume=vol_;source.contents.layers=layers_;source.contents.linetypes=linetypes_;
    source.contents.blocks=canvas_.blocks();source.contents.blocks.set_references({});source.text_styles=text_styles_.all();
    gdoc::BlockDef def;def.name=name;
    for(unsigned n=2;source.contents.blocks.find(def.name);++n)def.name=name+"_"+std::to_string(n);
    def.base=base;
    for(auto i:canvas_.selection())if(i<canvas_.shapes().size())def.geometry.push_back(canvas_.shapes()[i]);
    for(auto i:canvas_.selected_texts())if(i<canvas_.texts().size()) {
        const auto& t=canvas_.texts()[i];def.notes.push_back({t.pos,canvas_.resolved_text(t),t.height,t.angle,t.layer,t.halign,t.valign,t.width,t.style,t.attrs});
    }
    for(auto i:canvas_.selected_references())if(i<canvas_.blocks().references().size())def.nested_refs.push_back(canvas_.blocks().references()[i]);
    source.entries.push_back({name,destination,def.name});source.contents.blocks.define(def);
    source=library_subset(source,{def.name});
    std::map<std::string,std::string> names;std::string error;
    if(!merge_block_library(block_library_,source,names,error)){log_line("Save symbol: "+error);return false;}
    library_folders_.clear();library_folders_.insert("Library");
    std::string current="Library";std::istringstream in(destination);std::string part;
    while(std::getline(in,part,'/')){current+="/"+part;library_folders_.insert(current);}
    library_scroll_=0;drafting_feature_panel_=3;library_save_selection_open_=false;persist_block_library();
    log_line("Saved symbol "+name+" to "+(destination.empty()?"Library":destination));return true;
}

bool Shell::insert_library_block(std::size_t entry,gcm::Lattice2 position) {
    if(entry>=block_library_.entries.size()||!require_edit_access())return false;
    const auto& item=block_library_.entries[entry];
    const auto source=library_subset(block_library_,{item.block});
    BlockLibrary drawing;drawing.contents.volume=vol_;drawing.contents.blocks=canvas_.blocks();
    drawing.contents.layers=layers_;drawing.contents.linetypes=linetypes_;drawing.text_styles=text_styles_.all();
    std::map<std::string,std::string> names;std::string error;
    if(!merge_block_library(drawing,source,names,error)){log_line("Insert symbol: "+error);return false;}
    touch_metadata("insert library symbol");
    canvas_.modify_references([&](auto& refs) {
        canvas_.blocks()=std::move(drawing.contents.blocks);
        gdoc::BlockRef ref;ref.name=names[item.block];ref.insertion=position;
        ref.layer=canvas_.current_layer();ref.attrs=canvas_.current_attributes;
        canvas_.blocks().populate_attributes(ref);refs.push_back(std::move(ref));
    });
    layers_=std::move(drawing.contents.layers);linetypes_=std::move(drawing.contents.linetypes);
    for(auto& style:drawing.text_styles)text_styles_.add(std::move(style));canvas_.set_text_styles(text_styles_);
    invalidate_frame_caches();log_line("Inserted "+item.name);return true;
}

bool Shell::add_drawing_blocks_to_library() {
    BlockLibrary source;source.contents.volume=vol_;source.contents.blocks=canvas_.blocks();
    source.contents.layers=layers_;source.contents.linetypes=linetypes_;source.text_styles=text_styles_.all();
    const auto folder=document_path_.empty()?"Drawing blocks":std::filesystem::path(document_path_).stem().string();
    std::vector<std::string> roots;
    for(const auto& def:canvas_.blocks().all())if(visible_block(def)&&!is_symbol_legend(canvas_.blocks(),def.name)) {
        roots.push_back(def.name);source.entries.push_back({def.name,library_folder(folder),def.name});
    }
    if(roots.empty()){log_line("This drawing has no reusable block definitions");return false;}
    source=library_subset(source,roots);
    std::map<std::string,std::string> names;std::string error;
    if(!merge_block_library(block_library_,source,names,error)){log_line("Add drawing blocks: "+error);return false;}
    library_folders_.clear();library_folders_.insert("Library");library_folders_.insert("Library/"+library_folder(folder));
    drafting_feature_panel_=3;library_scroll_=0;persist_block_library();
    log_line("Added "+std::to_string(roots.size())+" drawing blocks to the library");return true;
}

bool Shell::run_block_library_command(const std::string& name,const std::string& arguments) {
    if(name=="BLOCKSPALETTE"||name=="CONTENT") {drafting_feature_panel_=3;return true;}
    if(name=="LIBRARYCOLLECT") {add_drawing_blocks_to_library();return true;}
    if(name=="LIBRARYLOAD"||name=="LIBRARYSAVE") {
        auto path=arguments;if(path.size()>1&&path.front()=='"'&&path.back()=='"')path=path.substr(1,path.size()-2);
        if(path.empty())ask_file(name=="LIBRARYLOAD"?FileAsk::LoadLibrary:FileAsk::SaveLibrary);
        else if(name=="LIBRARYLOAD")load_block_library(path);
        else save_block_library(std::filesystem::path(path).extension().empty()?path+".gllib":path);
        return true;
    }
    if(name!="LIBRARYADD")return false;
    if(mode_!=Mode::Sketch||!canvas_.selection_count()){log_line("Select drawing objects in Sketch mode first");return true;}
    std::istringstream in(arguments);std::string symbol,folder;
    if(in>>std::quoted(symbol)) {
        in>>std::quoted(folder);
        begin_pick(1,"Pick the symbol's insertion point",[this,symbol,folder](const auto& points){save_selection_to_library(symbol,folder,points[0]);});
    } else {library_save_selection_open_=true;drafting_feature_panel_=3;}
    return true;
}
}
