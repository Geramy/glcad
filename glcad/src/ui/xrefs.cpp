#include "ui/shell.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

namespace gcapp {
namespace {
std::string canonical_source(const std::string& path,const std::string& drawing) {
    std::filesystem::path source(path);
    if(source.is_relative()&&!drawing.empty())source=std::filesystem::path(drawing).parent_path()/source;
    std::error_code error;const auto absolute=std::filesystem::weakly_canonical(source,error);
    return error?std::string{}:absolute.string();
}
std::string extension_of(const std::string& path) {
    auto result=std::filesystem::path(path).extension().string();
    for(auto& c:result)c=char(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
}

bool Shell::reload_xref(gdoc::Xref reference) {
    if(reference.nested)if(const auto* def=canvas_.blocks().find(reference.name);def&&!def->source_owner.empty()&&def->source_owner!=reference.name)
        if(const auto* root=std::as_const(canvas_.blocks()).xref(def->source_owner))reference=*root;
    std::vector<std::string> chain;
    if(!document_path_.empty())chain.push_back(canonical_source(document_path_,{}));
    return load_xref(std::move(reference),chain);
}

bool Shell::load_xref(gdoc::Xref reference,std::vector<std::string>& chain) {
    const auto fail=[&](const std::string& error){log_.push_back("XREF: "+reference.name+": "+error);return false;};
    const auto path=canonical_source(reference.resolved_path.empty()?reference.path:reference.resolved_path,document_path_);
    if(path.empty())return fail("cannot resolve source path");
    if(chain.size()>=16||std::find(chain.begin(),chain.end(),path)!=chain.end())return fail("circular or excessively nested reference: "+path);
    if(const auto* def=canvas_.blocks().find(reference.name);def&&!def->is_xref&&def->source_owner!=reference.name)
        return fail("a drawing block already uses that name");
    auto next_chain=chain;next_chain.push_back(path);
    Shell source;
    const auto extension=extension_of(path);
    if(extension==".glcad") {
        gfile::Contents contents;const auto result=gfile::load_file(path,contents);
        if(!result.ok||!source.load_contents(contents,path,true,"reference reader"))return fail("cannot read "+path);
    } else if(extension==".dwg"||extension==".dxf") {
        if(!source.import_file(path,false))return fail("cannot read "+path);
        source.document_path_=path;
    } else return fail("source must be a native drawing, DWG or DXF");
    source.leave_paper_space();source.edit_sketch(0);
    const auto model_definitions=[&] {
        std::set<std::string> names;std::vector<std::string> pending;
        for(const auto& ref:source.canvas_.blocks().references())pending.push_back(ref.name);
        for(const auto& x:source.canvas_.blocks().xrefs())if(!x.nested) {
            bool placed=!source.canvas_.blocks().references_to(x.name).empty();
            for(const auto& [id,state]:source.space_states_)if(id!=source.active_space_id())
                placed|=std::any_of(state.content.refs.begin(),state.content.refs.end(),[&](const auto& ref){return ref.name==x.name;});
            for(const auto& def:source.canvas_.blocks().all())for(const auto& ref:source.canvas_.blocks().nested_insertions(def))
                placed|=ref.name==x.name;
            if(!placed)pending.push_back(x.name);
        }
        while(!pending.empty()) {
            auto name=std::move(pending.back());pending.pop_back();
            if(!names.insert(name).second)continue;
            if(const auto* def=source.canvas_.blocks().find(name))
                for(const auto& ref:source.canvas_.blocks().nested_insertions(*def))pending.push_back(ref.name);
        }
        return names;
    };
    const auto initial_definitions=model_definitions();
    for(auto child:source.canvas_.blocks().xrefs()) {
        if(!initial_definitions.contains(child.name)||child.nested||child.state==gdoc::XrefState::Unloaded||child.type==gdoc::XrefType::Overlay)continue;
        if(!source.load_xref(child,next_chain))return fail("cannot reload nested reference "+child.name+(source.log_.empty()?std::string{}:": "+source.log_.back()));
    }
    gio::MemoryWriter saved;
    if(!gfile::write_document(saved,source.document_contents()).ok)return fail("cannot retain source document state");
    const auto used_definitions=model_definitions();
    const auto prefix=reference.name+"|";
    const auto named=[&](const std::string& name){return prefix+name;};
    const auto layer_name=[&](const std::string& name,bool block_member=false) {
        return block_member&&(name.empty()||name=="0")?std::string("0"):named(name.empty()?"0":name);
    };
    const auto attrs=[&](gdoc::Attributes& value) {
        if(!value.linetype.empty()&&value.linetype!="ByLayer"&&value.linetype!="ByBlock"&&value.linetype!="Continuous")value.linetype=named(value.linetype);
    };
    const auto shape=[&](gsnap::Shape& value,bool member) {value.layer=layer_name(value.layer,member);attrs(value.attrs);};
    const auto note=[&](gdoc::BlockDef::Note& value,bool member) {
        value.layer=layer_name(value.layer,member);attrs(value.attrs);value.style=named(value.style.empty()?source.text_styles_.current():value.style);
    };
    const auto insertion=[&](gdoc::BlockRef& value,bool member) {
        value.name=named(value.name);value.layer=layer_name(value.layer,member);attrs(value.attrs);value.selection_id=0;
    };
    const auto underlay=[&](gdoc::Underlay& value) {
        value.path=canonical_source(value.path,path);value.name=named(value.name);value.layer=layer_name(value.layer,false);
    };
    std::vector<gdoc::BlockDef> definitions;
    for(auto def:source.canvas_.blocks().all()) {
        if(!used_definitions.contains(def.name))continue;
        def.name=named(def.name);def.source_owner=reference.name;
        if(const auto* existing=canvas_.blocks().find(def.name);existing&&existing->source_owner!=reference.name)
            return fail("a drawing block already uses dependent name "+def.name);
        for(auto& value:def.geometry)shape(value,true);
        for(auto& value:def.notes)note(value,true);
        for(auto& value:def.attributes)value.style=named(value.style);
        for(auto& value:def.nested)value=named(value);
        for(auto& value:def.nested_refs)insertion(value,true);
        for(auto& value:def.underlays)underlay(value);
        definitions.push_back(std::move(def));
    }
    gdoc::BlockDef root;root.name=reference.name;root.is_xref=true;root.allow_exploding=false;
    root.xref_path=reference.path;root.source_owner=reference.name;root.source_document=saved.bytes();
    root.geometry=source.canvas_.shapes();
    for(const auto& text:source.canvas_.texts()) {
        gdoc::BlockDef::Note value;value.pos=text.pos;value.text=source.canvas_.resolved_text(text);
        value.height=text.height;value.angle=text.angle;value.halign=text.halign;value.valign=text.valign;
        value.width=text.width;value.style=text.style;root.notes.push_back(std::move(value));
    }
    for(const auto& dimension:source.canvas_.dimensions()) {
        const auto* style=source.canvas_.dim_styles().find(dimension.style);if(!style)continue;
        const auto built=gdim::build(dimension,source.canvas_.styled_for(*style,source.canvas_.annotation_factor_for(*style)),source.vol_);
        if(!built.ok)continue;
        for(auto value:built.lines){value.layer=dimension.layer;root.geometry.push_back(std::move(value));}
        for(auto value:built.arrows){value.layer=dimension.layer;root.geometry.push_back(std::move(value));}
        if(!built.text.empty())root.notes.push_back({built.text_at,built.text,built.text_height,built.text_angle,dimension.layer,built.halign,built.valign});
    }
    for(const auto& hatch:source.canvas_.hatches())if(hatch.valid&&!hatch.orphaned) {
        std::string layer="0";
        if(!hatch.boundary_shapes.empty()&&hatch.boundary_shapes[0]<source.canvas_.shapes().size())layer=source.canvas_.shapes()[hatch.boundary_shapes[0]].layer;
        for(const auto& line:hatch.cached.lines){gsnap::Shape value;value.a=line.a;value.b=line.b;value.layer=layer;root.geometry.push_back(std::move(value));}
    }
    for(const auto& spline:source.canvas_.splines()) {
        const auto points=spline.tessellate(source.canvas_.spline_tolerance());
        for(std::size_t i=1;i<points.size();++i){gsnap::Shape value;value.a=gcm::to_lattice(vol_,points[i-1].x.to_f64(),points[i-1].y.to_f64());value.b=gcm::to_lattice(vol_,points[i].x.to_f64(),points[i].y.to_f64());root.geometry.push_back(value);}
    }
    for(auto& value:root.geometry)shape(value,false);
    for(auto& value:root.notes)note(value,false);
    root.nested_refs=source.canvas_.blocks().references();
    for(auto& value:root.nested_refs)insertion(value,false);
    root.underlays=source.canvas_.underlays().all();for(auto& value:root.underlays)underlay(value);
    definitions.push_back(std::move(root));
    std::vector<gdoc::Xref> nested=source.canvas_.blocks().xrefs();
    std::erase_if(nested,[&](const auto& child){return !used_definitions.contains(child.name);});
    for(auto& child:nested){child.name=named(child.name);if(child.resolved_path.empty())child.resolved_path=canonical_source(child.path,path);child.nested=true;if(const auto* existing=std::as_const(canvas_.blocks()).xref(child.name);existing&&existing->state==gdoc::XrefState::Unloaded)child.state=gdoc::XrefState::Unloaded;}
    auto dynamics=source.canvas_.blocks().dynamics();
    std::erase_if(dynamics,[&](const auto& dynamic){return !used_definitions.contains(dynamic.name);});
    for(auto& dynamic:dynamics)dynamic.name=named(dynamic.name);

    const bool replacing=std::as_const(canvas_.blocks()).xref(reference.name)!=nullptr;
    if(!replacing) {
        for(const auto& layer:source.layers_.all())if(layers_.find(named(layer.name)))return fail("dependent layer name already exists: "+named(layer.name));
        for(const auto& style:source.text_styles_.all())if(text_styles_.find(named(style.name)))return fail("dependent style name already exists: "+named(style.name));
    }
    auto layers=layers_;auto linetypes=linetypes_;auto styles=text_styles_;
    for(auto layer:source.layers_.all()) {
        layer.name=named(layer.name);layer.locked=true;
        if(!layer.linetype.empty()&&layer.linetype!="Continuous")layer.linetype=named(layer.linetype);
        layers.add(std::move(layer));
    }
    for(auto linetype:source.linetypes_.all())if(linetype.name!="Continuous") {linetype.name=named(linetype.name);linetypes.add(std::move(linetype));}
    for(auto style:source.text_styles_.all()) {style.name=named(style.name);styles.add(std::move(style));}
    const auto root_layer="XREF|"+reference.name;
    if(!layers.find(root_layer)){gdoc::Layer layer;layer.name=root_layer;layer.locked=true;layers.add(std::move(layer));}
    auto blocks=canvas_.blocks();
    bool placed=!blocks.references_to(reference.name).empty();
    for(const auto& [id,state]:space_states_)if(id!=active_space_id())
        placed|=std::any_of(state.content.refs.begin(),state.content.refs.end(),[&](const auto& ref){return ref.name==reference.name;});
    reference.resolved_path=path;reference.state=gdoc::XrefState::Loaded;
    blocks.replace_xref_contents(reference,std::move(definitions),std::move(nested),std::move(dynamics));
    if(!reference.nested&&!placed) {
        gdoc::BlockRef ref;ref.name=reference.name;ref.insertion=reference.insertion;
        ref.scale_x=ref.scale_y=reference.scale;ref.rotation=reference.rotation;ref.layer=root_layer;blocks.insert(std::move(ref));
    }
    layers_=std::move(layers);linetypes_=std::move(linetypes);text_styles_=std::move(styles);canvas_.blocks()=std::move(blocks);
    // Remove old caches only once the replacement is ready.
    if(std::any_of(canvas_.shapes().begin(),canvas_.shapes().end(),[&](const auto& value){return value.layer==root_layer;}))
        canvas_.modify_shapes([&](auto& shapes){std::erase_if(shapes,[&](const auto& value){return value.layer==root_layer;});});
    canvas_.set_text_styles(text_styles_);canvas_.clear_selection();invalidate_frame_caches();
    return true;
}
}
