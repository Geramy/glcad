#include "ui/symbols.hpp"
#include "ui/canvas.hpp"
#include "ui/shell.hpp"
#include "engine/document_file.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace gcapp {
namespace {
constexpr const char* legend_marker = "Drawing symbol legend";
std::string block_name(std::string_view id) { return "__glcad_symbol_" + std::string(id); }
bool note_bounds(const Canvas& canvas, const gdoc::BlockNote& note,
                 double& lx,double& ly,double& hx,double& hy) {
    Canvas::TextItem text{note.pos,note.text,note.height,note.angle,note.halign,note.valign,note.width,note.style};
    return canvas.labels_bounds(canvas.note_labels(text),lx,ly,hx,hy);
}
gdoc::BlockNote positioned_note(const Canvas& canvas,double x,double y,
                               const std::string& text,double height,bool center=false) {
    gdoc::BlockNote note;note.pos=gcm::to_lattice(canvas.volume(),0,0);
    note.text=text;note.height=height;note.valign=3;
    double lx,ly,hx,hy;
    if(note_bounds(canvas,note,lx,ly,hx,hy))
        note.pos=gcm::to_lattice(canvas.volume(),x-lx,y-(center?(ly+hy)*.5:hy));
    else note.pos=gcm::to_lattice(canvas.volume(),x,y);
    return note;
}
gdoc::BlockDef symbol_definition(std::string_view id, const Canvas& canvas) {
    const auto& v=canvas.volume();
    gdoc::BlockDef d; d.name = block_name(id);
    d.base=gcm::to_lattice(v,0,0);
    for (const auto& item : symbol_catalog()) if (id == item.id) d.description = item.label;
    const auto point = [&](double x, double y) { return gcm::to_lattice(v,x,y); };
    const auto line = [&](double ax,double ay,double bx,double by) {
        gsnap::Shape s; s.a=point(ax,ay); s.b=point(bx,by); s.layer="0"; d.geometry.push_back(s);
    };
    const auto circle = [&](double x,double y,double r) {
        gsnap::Shape s; s.kind=gsnap::ShapeKind::Circle; s.a=point(x,y);
        s.radius=gcm::i64(std::llround(r/v.inv_scale())); s.layer="0"; d.geometry.push_back(s);
    };
    const auto note = [&](double x,double y,const char* text) {
        d.notes.push_back(positioned_note(canvas,x,y,text,.2));
    };
    if (id=="direction" || id=="both_directions") {
        line(0,0,1,0); line(1,0,.75,.15); line(1,0,.75,-.15);
        if(id=="both_directions") {line(0,0,.25,.15);line(0,0,.25,-.15);}
    } else if(id=="north") {
        line(0,0,0,.75);line(0,.75,-.18,.4);line(-.18,.4,.18,.4);line(.18,.4,0,.75);
        note(-.08,1,"N");
    } else if(id=="section") {
        circle(0,0,.3);line(-.3,0,.3,0);line(.3,.3,.7,0);line(.7,0,.3,-.3);line(.3,-.3,.3,.3);
        note(-.07,.25,"A"); note(-.07,-.04,"1");
    } else if(id=="elevation") {
        circle(0,0,.3);line(.3,.2,.6,0);line(.6,0,.3,-.2);line(.3,-.2,.3,.2);note(-.07,.1,"1");
    } else if(id=="datum") {
        line(-.4,0,.6,0);line(0,0,-.2,.3);line(-.2,.3,.2,.3);line(.2,.3,0,0);note(.3,.3,"0.00");
    }
    return d;
}
}
const std::vector<SymbolEntry>& symbol_catalog() {
    static const std::vector<SymbolEntry> entries{{"north","North arrow"},{"section","Section marker"},
        {"elevation","Elevation marker"},{"datum","Datum / level"},
        {"direction","Direction / flow arrow"},{"both_directions","Two-way arrow"}};
    return entries;
}
std::vector<gsnap::Shape> symbol_preview(std::string_view id,const Canvas& canvas) {
    return symbol_definition(id,canvas).geometry;
}
bool is_symbol_legend(const gdoc::BlockTable& blocks, std::string_view name) {
    const auto* d=blocks.find(name); return d && d->description==legend_marker;
}
std::vector<SymbolUsage> used_symbols(const gdoc::BlockTable& blocks) {
    std::map<std::string,std::size_t> counts;
    for(const auto& r:blocks.references()) {
        const auto* d=blocks.find(r.name);
        if(d && !d->is_xref && !is_symbol_legend(blocks,r.name)) ++counts[r.name];
    }
    std::vector<SymbolUsage> result;
    for(const auto& [name,count]:counts) {
        std::string label=name;
        for(const auto& item:symbol_catalog()) if(name==block_name(item.id)) label=item.label;
        result.push_back({name,label,count});
    }
    return result;
}
std::vector<SymbolUsage> used_symbols(const Canvas& canvas) {
    auto result=used_symbols(canvas.blocks());
    const auto& shapes=canvas.shapes();
    std::map<std::string,std::set<gcm::u64>> groups;
    std::map<std::string,std::size_t> rows;
    for(std::size_t i=0;i<shapes.size();++i) {
        const auto& shape=shapes[i];std::string type,label;
        const auto attr=shape.attrs.user.find("sketch_object");
        if(attr!=shape.attrs.user.end()) {
            type=attr->second;
            if(type=="window")label="Window";
            else if(type=="frame")label="Door frame";
            else if(type=="hinge")label="Door hinge";
            else if(type=="rollup")label="Rollup door";
        }
        if(label.empty() && shape.kind==gsnap::ShapeKind::Arc && shape.radius>0 &&
           (shape.layer=="A-DOOR" || shape.attrs.user.count("door"))) {type="swing";label="Swing door";}
        if(label.empty())continue;
        const gcm::u64 group=shape.poly?shape.poly:(gcm::u64{1}<<32)+i;
        if(!groups[type].insert(group).second)continue;
        auto found=rows.find(type);
        if(found!=rows.end()) {++result[found->second].count;continue;}
        SymbolUsage usage{"",label,1,{}};
        if(shape.poly) {
            for(const auto& part:shapes)if(part.poly==shape.poly)usage.geometry.push_back(part);
        } else usage.geometry.push_back(shape);
        if(type=="swing") {
            const auto flow=canvas.door_flow_shapes(i);
            usage.geometry.insert(usage.geometry.end(),flow.begin(),flow.end());
        }
        rows[type]=result.size();result.push_back(std::move(usage));
    }
    return result;
}
bool Canvas::place_symbol(std::string_view id, gcm::Lattice2 at, double size, double rotation) {
    const auto& entries=symbol_catalog();
    if(std::none_of(entries.begin(),entries.end(),[&](const auto& item){return id==item.id;}) ||
       !std::isfinite(size) || size<=0 || size>10000 || !std::isfinite(rotation)) {
        message_="Choose a known symbol and a size greater than zero (up to 10000 m)";return false;
    }
    const auto name=block_name(id);
    if(!blocks_.find(name)) blocks_.define(symbol_definition(id,*this));
    mark();
    gdoc::BlockRef ref; ref.name=name;ref.insertion=at;ref.scale_x=ref.scale_y=size;
    ref.rotation=rotation;ref.layer=current_layer_;ref.attrs=current_attributes;
    blocks_.insert(ref);message_="Placed "+std::string(id);return true;
}
namespace {
gdoc::BlockDef legend_definition(const Canvas& canvas,const std::vector<SymbolUsage>& usage) {
    gdoc::BlockDef legend;legend.description=legend_marker;
    legend.base=gcm::to_lattice(canvas.volume(),0,0);
    double width=5;
    for(const auto& item:usage) width=std::max(width,2.5+item.label.size()*.2);
    const auto point=[&](double x,double y){return gcm::to_lattice(canvas.volume(),x,y);};
    const auto note=[&](double x,double y,const std::string& text,double height) {
        legend.notes.push_back(positioned_note(canvas,x,y+.12,text,height,true));
    };
    note(0,0,"Symbol legend",.24);note(width-.65,0,"Count",.15);
    double y=-.8;
    if(usage.empty()) note(0,y,"No symbols used",.18);
    for(const auto& item:usage) {
        gdoc::BlockRef sample;sample.name=item.block;sample.insertion=point(0,0);
        const auto shapes=item.block.empty()?item.geometry:canvas.blocks().expand(sample,canvas.volume());
        double lx=1e100,ly=1e100,hx=-1e100,hy=-1e100;
        const auto include=[&](double x,double yy){lx=std::min(lx,x);ly=std::min(ly,yy);hx=std::max(hx,x);hy=std::max(hy,yy);};
        for(const auto& s:shapes) {
            double ax,ay,bx,by;gcm::to_world(canvas.volume(),s.a,ax,ay);gcm::to_world(canvas.volume(),s.b,bx,by);
            const double radius=s.radius*canvas.volume().inv_scale();
            if(s.kind==gsnap::ShapeKind::Circle || s.kind==gsnap::ShapeKind::Arc) {include(ax-radius,ay-radius);include(ax+radius,ay+radius);}
            else {include(ax,ay);include(bx,by);if(s.kind==gsnap::ShapeKind::Ellipse) {const auto r=std::max(std::hypot(bx-ax,by-ay),radius);include(ax-r,ay-r);include(ax+r,ay+r);}}
        }
        for(const auto& n:canvas.blocks().note_instances(sample,canvas.volume())) {
            double a,b,c,d;if(!note_bounds(canvas,n.note,a,b,c,d))continue;
            const auto& m=n.transform;
            for(const auto& p:{std::pair{a,b},std::pair{a,d},std::pair{c,b},std::pair{c,d}})
                include(m[0]*p.first+m[1]*p.second+m[2],m[3]*p.first+m[4]*p.second+m[5]);
        }
        if(lx<=hx) {
            const double scale=.55/std::max({hx-lx,hy-ly,.001});sample.scale_x=sample.scale_y=scale;
            sample.insertion=point(.35-scale*(lx+hx)*.5,y+.12-scale*(ly+hy)*.5);
            if(!item.block.empty()) legend.nested_refs.push_back(sample);
            else for(const auto& shape:shapes) {
                auto placed=gdoc::place_in_block(shape,sample,point(0,0),canvas.volume());
                placed.layer="0";placed.key=0;placed.poly=0;placed.poly_index=0;
                placed.attrs.user.clear();legend.geometry.push_back(std::move(placed));
            }
        }
        note(1,y,item.label,.18);note(width-.5,y,std::to_string(item.count),.18);y-=.8;
    }
    const double bottom=-.8*std::max<std::size_t>(1,usage.size())-.35;
    const std::pair<double,double> corners[]={{-.2,.6},{width+.2,.6},{width+.2,bottom},{-.2,bottom}};
    for(int i=0;i<4;++i) {
        gsnap::Shape line;line.a=point(corners[i].first,corners[i].second);
        line.b=point(corners[(i+1)%4].first,corners[(i+1)%4].second);line.layer="0";
        legend.geometry.push_back(line);
    }
    return legend;
}
std::vector<gcm::u8> legend_image(gdoc::BlockDef definition) {
    definition.name.clear();
    gfile::Contents content;content.blocks.define(std::move(definition));
    return gfile::encode(content);
}
}
bool Canvas::refresh_symbol_legends(bool force) {
    if(!force && legend_sync_edit_==edit_serial_ && legend_sync_blocks_==blocks_.revision())return false;
    std::set<std::string> names;
    for(const auto& ref:blocks_.references())if(is_symbol_legend(blocks_,ref.name))names.insert(ref.name);
    bool changed=false;
    if(!names.empty()) {
        auto generated=legend_definition(*this,used_symbols(*this));
        const auto image=legend_image(generated);
        for(const auto& name:names) {
            if(legend_image(*blocks_.find(name))==image)continue;
            generated.name=name;blocks_.define(generated);changed=true;
        }
    }
    legend_sync_edit_=edit_serial_;legend_sync_blocks_=blocks_.revision();
    legend_metadata_dirty_|=changed;
    return changed;
}
bool Shell::refresh_symbol_legends() {
    if(read_only_ || restoring_history_ || canvas_.selection_dragging() || canvas_.grip_dragging())return false;
    canvas_.refresh_symbol_legends();
    if(!canvas_.take_symbol_legend_changes())return false;
    touch_metadata("symbol legend");mesh_dirty_=true;return true;
}
bool Canvas::update_symbol_legend(gcm::Lattice2 at, double size) {
    if(!std::isfinite(size) || size<=0 || size>10000) {message_="Legend scale must be greater than zero (up to 10000)";return false;}
    for(const auto& ref:blocks_.references())if(is_symbol_legend(blocks_,ref.name)) {
        refresh_symbol_legends(true);
        message_="Symbol legend is current; placement retained";return true;
    }
    const auto usage=used_symbols(*this);
    if(usage.empty()) {message_="Place a symbol or block before generating a legend";return false;}
    auto legend=legend_definition(*this,usage);
    unsigned version=1;
    do {legend.name="__glcad_legend_"+std::to_string(version++);} while(blocks_.find(legend.name));
    gdoc::BlockRef ref;ref.name=legend.name;ref.insertion=at;ref.scale_x=ref.scale_y=size;
    ref.layer=current_layer_;ref.attrs=current_attributes;
    mark();blocks_.define(std::move(legend));blocks_.insert(ref);
    legend_sync_edit_=edit_serial_;legend_sync_blocks_=blocks_.revision();
    legend_metadata_dirty_=true;
    message_="Placed symbol legend; counts update automatically";return true;
}
}
