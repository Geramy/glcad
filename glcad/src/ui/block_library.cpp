#include "ui/block_library.hpp"
#include "ai/json.hpp"
#include "exchange/dxf.hpp"
#include "exchange/dwg_write.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace gcapp {
namespace {
using gai::Json;
std::string extension(const std::string& path) {
    auto value=std::filesystem::path(path).extension().string();
    for(auto& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
Json style_json(const gtext::TextStyleDef& s) {
    return {{"name",s.name},{"font",s.font},{"big_font",s.big_font},{"height",s.height},
        {"width",s.width_factor},{"oblique",s.oblique},{"backwards",s.backwards},
        {"upside_down",s.upside_down},{"vertical",s.vertical},{"annotative",s.annotative}};
}
std::vector<gcm::u8> definition_bytes(const gdoc::BlockDef& def,const gdoc::DynamicBlock* dynamic) {
    gfile::Contents c;c.blocks.define(def);
    if(dynamic)c.blocks.make_dynamic(def.name)=*dynamic;
    const auto bytes=gfile::encode(c);gfile::Contents normalized;
    return gfile::decode(bytes,normalized).ok?gfile::encode(normalized):bytes;
}
}

std::string library_folder(std::string folder) {
    std::replace(folder.begin(),folder.end(),'\\','/');
    std::string result,part;std::istringstream in(folder);
    while(std::getline(in,part,'/')) {
        if(part.empty()||part==".")continue;
        if(part=="..")return {};
        if(!result.empty())result+='/';result+=part;
    }
    return result;
}

bool validate_block_library(const BlockLibrary& library,std::string& error) {
    const auto fail=[&](std::string message){error=std::move(message);return false;};
    if(library.entries.size()>10000||library.contents.blocks.size()>50000)return fail("Library has too many entries");
    const auto valid=gfile::validate_geometry(library.contents);
    if(!valid.ok)return fail(valid.message);
    std::set<std::pair<std::string,std::string>> entries;
    for(const auto& entry:library.entries) {
        if(entry.name.empty()||entry.name.size()>512||entry.folder.size()>2048||library_folder(entry.folder)!=entry.folder)
            return fail("Invalid library item name or folder");
        if(!library.contents.blocks.find(entry.block))return fail("Library item has no block definition: "+entry.name);
        if(!entries.emplace(entry.folder,entry.name).second)return fail("Duplicate library item: "+entry.name);
    }
    std::map<std::string,int> state;
    std::function<bool(const std::string&,unsigned)> visit=[&](const std::string& name,unsigned depth) {
        if(depth>64)return fail("Block nesting exceeds 64 levels");
        if(state[name]==2)return true;
        if(state[name]==1)return fail("Circular block dependency: "+name);
        const auto* def=library.contents.blocks.find(name);
        if(!def)return fail("Missing nested block: "+name);
        if(def->is_xref||!def->underlays.empty())return fail("Bind external references and remove linked underlays before sharing: "+name);
        state[name]=1;
        for(const auto& ref:library.contents.blocks.nested_insertions(*def))if(!visit(ref.name,depth+1))return false;
        state[name]=2;return true;
    };
    for(const auto& def:library.contents.blocks.all())if(!visit(def.name,0))return false;
    return true;
}

bool read_native_library(const std::string& path,BlockLibrary& out,std::string& error) {
    BlockLibrary staged;
    const auto loaded=gfile::load_file(path,staged.contents);
    if(!loaded.ok){error=loaded.message;return false;}
    bool manifest=false;
    try {
        for(const auto& frame:staged.contents.foreign)if(frame.kind==kBlockLibraryFrame) {
            if(manifest)throw std::runtime_error("Duplicate library manifest");
            manifest=true;
            if(frame.payload.size()>16*1024*1024)throw std::runtime_error("Library manifest is too large");
            const auto json=Json::parse(frame.payload.begin(),frame.payload.end());
            if(json.at("format")!="GLCAD block library"||json.at("version")!=1)
                throw std::runtime_error("Unsupported library version");
            for(const auto& item:json.at("entries"))staged.entries.push_back({item.at("name"),item.at("folder"),item.at("block")});
            for(const auto& item:json.at("text_styles")) {
                gtext::TextStyleDef s;s.name=item.at("name");s.font=item.at("font");s.big_font=item.at("big_font");
                s.height=item.at("height");s.width_factor=item.at("width");s.oblique=item.at("oblique");
                s.backwards=item.at("backwards");s.upside_down=item.at("upside_down");s.vertical=item.at("vertical");s.annotative=item.at("annotative");
                if(!std::isfinite(s.height)||!std::isfinite(s.width_factor)||!std::isfinite(s.oblique)||s.height<0||s.width_factor<=0)
                    throw std::runtime_error("Invalid library text style");
                staged.text_styles.push_back(std::move(s));
            }
        }
        if(!manifest)throw std::runtime_error("This file has no block-library manifest");
    } catch(const std::exception& e){error=e.what();return false;}
    std::erase_if(staged.contents.foreign,[](const auto& frame){return frame.kind==kBlockLibraryFrame;});
    staged.contents.blocks.set_references({});
    if(!validate_block_library(staged,error))return false;
    out=std::move(staged);return true;
}

bool write_block_library(const std::string& path,const BlockLibrary& library,std::string& error) {
    if(!validate_block_library(library,error))return false;
    const auto ext=extension(path);
    if(ext!=".gllib"&&ext!=".glcad"&&ext!=".dwg"&&ext!=".dxf") {error="Choose .gllib, .glcad, .dwg or .dxf";return false;}
    static std::atomic<unsigned> sequence{0};
    const auto temp=path+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
    struct Cleanup {std::string path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{temp};
    auto contents=library.contents;contents.blocks.set_references({});
    contents.geometry.clear();contents.entities.clear();contents.spaces.clear();
    std::erase_if(contents.foreign,[](const auto& frame){return frame.kind==kBlockLibraryFrame;});
    gdoc::Space model;model.name="Model";model.kind=gdoc::SpaceKind::Model;contents.spaces.push_back(model);
    if(ext==".gllib"||ext==".glcad") {
        Json manifest={{"format","GLCAD block library"},{"version",1},{"entries",Json::array()},{"text_styles",Json::array()}};
        for(const auto& entry:library.entries)manifest["entries"].push_back({{"name",entry.name},{"folder",entry.folder},{"block",entry.block}});
        for(const auto& style:library.text_styles)manifest["text_styles"].push_back(style_json(style));
        const auto text=manifest.dump();contents.foreign.push_back({kBlockLibraryFrame,{text.begin(),text.end()}});
        const auto result=gfile::save_file(temp,contents);if(!result.ok){error=result.message;return false;}
    } else {
        gdxf::Document drawing;drawing.blocks=contents.blocks;drawing.layers=contents.layers;
        drawing.linetypes=contents.linetypes;drawing.text_styles=library.text_styles;
        if(ext==".dwg") {
            gdwg::WriteOptions options;options.units=gdxf::Units::Metres;options.quantize_true_color=true;
            if(!gdwg::write_file(temp,drawing,contents.volume,options,&error))return false;
        } else {
            for(const auto& block:contents.blocks.all())if(contents.blocks.dynamic(block.name)) {
                error="DXF export cannot preserve dynamic block behavior; save this collection as .gllib or .glcad";return false;
            }
            gdxf::WriteOptions options;options.units=gdxf::Units::Metres;
            const auto text=gdxf::write(drawing,contents.volume,options);
            if(text.empty()){error="This collection contains block features the DXF writer cannot preserve";return false;}
            std::ofstream file(temp,std::ios::binary);file<<text;file.close();
            if(!file){error="Cannot write library";return false;}
        }
    }
#ifdef _WIN32
    const bool replaced=MoveFileExW(std::filesystem::u8path(temp).c_str(),std::filesystem::u8path(path).c_str(),
        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    const bool replaced=std::rename(temp.c_str(),path.c_str())==0;
#endif
    if(!replaced){error="Cannot replace library file: "+path;return false;}
    return true;
}

BlockLibrary library_subset(const BlockLibrary& source,const std::vector<std::string>& roots) {
    BlockLibrary result;result.contents.volume=source.contents.volume;result.contents.layers=source.contents.layers;
    result.contents.linetypes=source.contents.linetypes;result.contents.units=source.contents.units;result.text_styles=source.text_styles;
    std::set<std::string> used;
    std::function<void(const std::string&)> visit=[&](const std::string& name) {
        if(!used.insert(name).second)return;
        if(const auto* def=source.contents.blocks.find(name)) {
            result.contents.blocks.define(*def);
            if(const auto* dynamic=source.contents.blocks.dynamic(name))result.contents.blocks.make_dynamic(name)=*dynamic;
            for(const auto& ref:source.contents.blocks.nested_insertions(*def))visit(ref.name);
        }
    };
    for(const auto& name:roots)visit(name);
    for(const auto& entry:source.entries)if(std::find(roots.begin(),roots.end(),entry.block)!=roots.end())result.entries.push_back(entry);
    return result;
}

bool merge_block_library(BlockLibrary& into,const BlockLibrary& source,std::map<std::string,std::string>& names,std::string& error) {
    if(!validate_block_library(source,error))return false;
    auto staged=into;names.clear();
    std::map<std::string,std::string> layer_names,style_names,line_names;
    for(auto type:source.contents.linetypes.all()) {
        const auto base=type.name;
        for(unsigned n=1;;++n) {
            type.name=n==1?base:base+"_"+std::to_string(n);
            const auto* found=staged.contents.linetypes.find(type.name);if(!found)break;
            gfile::Contents a,b;a.linetypes.add(*found);b.linetypes.add(type);
            if(gfile::encode(a)==gfile::encode(b))break;
        }
        line_names[base]=type.name;staged.contents.linetypes.add(type);
    }
    for(auto layer:source.contents.layers.all()) {
        const auto base=layer.name;if(line_names.contains(layer.linetype))layer.linetype=line_names[layer.linetype];
        if(base!="0")for(unsigned n=1;;++n) {
            layer.name=n==1?base:base+"_"+std::to_string(n);
            const auto* found=staged.contents.layers.find(layer.name);if(!found)break;
            gfile::Contents a,b;a.layers.add(*found);b.layers.add(layer);
            if(gfile::encode(a)==gfile::encode(b))break;
        }
        layer_names[base]=layer.name;if(base!="0"||!staged.contents.layers.find("0"))staged.contents.layers.add(layer);
    }
    for(auto style:source.text_styles) {
        const auto base=style.name;
        const auto occupied=[&](const auto& name){return std::find_if(staged.text_styles.begin(),staged.text_styles.end(),[&](const auto& s){return s.name==name;});};
        for(unsigned n=1;;++n) {
            style.name=n==1?base:base+"_"+std::to_string(n);
            const auto found=occupied(style.name);
            if(found==staged.text_styles.end()||*found==style)break;
        }
        style_names[base]=style.name;if(occupied(style.name)==staged.text_styles.end())staged.text_styles.push_back(style);
    }
    const auto& from=source.contents.volume;const auto& to=staged.contents.volume;
    const auto point=[&](gcm::Lattice2& p){double x,y;gcm::to_world(from,p,x,y);p=gcm::to_lattice(to,x,y);};
    const auto attrs=[&](gdoc::Attributes& a){if(line_names.contains(a.linetype))a.linetype=line_names[a.linetype];};
    const auto note=[&](gdoc::BlockNote& n){point(n.pos);if(layer_names.contains(n.layer))n.layer=layer_names[n.layer];if(style_names.contains(n.style))n.style=style_names[n.style];attrs(n.attrs);};
    std::function<void(const std::string&)> visit=[&](const std::string& original) {
        if(names.contains(original))return;
        auto def=*source.contents.blocks.find(original);
        for(const auto& ref:source.contents.blocks.nested_insertions(def))visit(ref.name);
        point(def.base);
        for(auto& shape:def.geometry){point(shape.a);point(shape.b);shape.radius=gcm::i64(std::llround(shape.radius*from.inv_scale()/to.inv_scale()));if(layer_names.contains(shape.layer))shape.layer=layer_names[shape.layer];attrs(shape.attrs);}
        for(auto& n:def.notes)note(n);
        for(auto& a:def.attributes){point(a.position);if(style_names.contains(a.style))a.style=style_names[a.style];}
        for(auto& name:def.nested)name=names[name];
        for(auto& ref:def.nested_refs){ref.name=names[ref.name];point(ref.insertion);if(layer_names.contains(ref.layer))ref.layer=layer_names[ref.layer];attrs(ref.attrs);for(auto& n:ref.notes)note(n);}
        auto dyn=source.contents.blocks.dynamic(original)?std::optional<gdoc::DynamicBlock>(*source.contents.blocks.dynamic(original)):std::nullopt;
        if(dyn){for(auto& p:dyn->parameters){point(p.base);point(p.end);}for(auto& a:dyn->actions){point(a.origin);point(a.window_lo);point(a.window_hi);}}
        for(unsigned n=1;;++n) {
            def.name=n==1?original:original+"_"+std::to_string(n);if(dyn)dyn->name=def.name;
            const auto* existing=staged.contents.blocks.find(def.name);
            if(!existing) {staged.contents.blocks.define(def);if(dyn)staged.contents.blocks.make_dynamic(def.name)=*dyn;break;}
            if(definition_bytes(*existing,staged.contents.blocks.dynamic(def.name))==definition_bytes(def,dyn?&*dyn:nullptr))break;
        }
        names[original]=def.name;
    };
    for(const auto& def:source.contents.blocks.all())visit(def.name);
    for(auto entry:source.entries) {
        entry.block=names[entry.block];
        const auto base=entry.name;
        for(unsigned n=1;;++n) {
            entry.name=n==1?base:base+"_"+std::to_string(n);
            const auto found=std::find_if(staged.entries.begin(),staged.entries.end(),[&](const auto& e){return e.name==entry.name&&e.folder==entry.folder;});
            if(found==staged.entries.end()){staged.entries.push_back(std::move(entry));break;}
            if(found->block==entry.block)break;
        }
    }
    const auto valid=gfile::validate_geometry(staged.contents);
    if(!valid.ok){error=valid.message;return false;}
    into=std::move(staged);return true;
}
}
