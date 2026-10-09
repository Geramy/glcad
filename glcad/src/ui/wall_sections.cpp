#include "ui/canvas.hpp"

#include <iomanip>
#include <map>
#include <sstream>

namespace gcapp {
namespace {
bool wall(const gsnap::Shape& shape) {
    const auto type=shape.attrs.user.find("sketch_object");
    return type!=shape.attrs.user.end() && type->second=="wall";
}
std::vector<gsnap::Shape> boundary(const gregion::Region& region) {
    std::vector<gsnap::Shape> result;
    for(const auto& loop:region.loops)for(const auto& edge:loop)result.push_back(edge.shape);
    return result;
}
std::string outline(const std::vector<gsnap::Shape>& shapes) {
    std::ostringstream out;out<<std::setprecision(17);
    for(const auto& shape:shapes)out<<int(shape.kind)<<' '<<shape.a.x<<' '<<shape.a.y<<' '
        <<shape.b.x<<' '<<shape.b.y<<' '<<shape.radius<<' '<<shape.start_angle<<' '<<shape.end_angle<<' ';
    return out.str();
}
gregion::Result read_outline(const std::string& text) {
    std::istringstream in(text);std::vector<gsnap::Shape> shapes;int kind;
    gsnap::Shape shape;
    while(in>>kind>>shape.a.x>>shape.a.y>>shape.b.x>>shape.b.y>>shape.radius>>shape.start_angle>>shape.end_angle) {
        shape.kind=gsnap::ShapeKind(kind);shapes.push_back(shape);
    }
    return gregion::from_shapes(shapes);
}
gregion::Result unite(std::vector<gregion::Region> parts) {
    if(parts.empty())return {true,{}, {}};
    while(parts.size()>1) {
        std::vector<gregion::Region> next;
        for(std::size_t i=0;i<parts.size();i+=2) {
            if(i+1==parts.size()) {next.push_back(std::move(parts[i]));continue;}
            auto joined=gregion::boolean_op(parts[i],parts[i+1],gregion::Operation::Union);
            if(!joined.ok)return joined;
            next.push_back(std::move(joined.region));
        }
        parts=std::move(next);
    }
    return {true,{},std::move(parts.front())};
}
}

void Canvas::add_wall_section(const std::vector<gsnap::Shape>& shapes) {
    if(shapes.empty())return;
    const auto region=gregion::from_shapes(shapes);if(!region.ok)return;
    gdoc::BlockDef definition;
    unsigned id=1;
    do {definition.name="__glcad_wall_"+std::to_string(id++);}while(blocks_.find(definition.name));
    definition.description="Wall section";
    definition.source_owner="glcad.wall";
    definition.base=shapes.front().a;
    gcm::u32 order=0;
    for(gcm::u32 loop=0;loop<region.region.loops.size();++loop)for(const auto& edge:region.region.loops[loop]) {
        auto shape=edge.shape;
        shape.key=shape.id=0;shape.poly=1;shape.poly_index=order++;
        shape.region=true;shape.region_loop=loop;shape.region_reversed=edge.reversed;
        shape.attrs.user.erase("wall_joined");shape.attrs.user.erase("wall_source_outline");
        shape.attrs.user.erase("wall_source_signature");
        shape.layer="0";
        definition.geometry.push_back(std::move(shape));
    }
    gdoc::BlockRef ref;ref.name=definition.name;ref.insertion=definition.base;
    ref.layer=shapes.front().layer.empty()?"A-WALL":shapes.front().layer;
    ref.attrs.user["wall_section"]="1";
    blocks_.define(std::move(definition));blocks_.insert(std::move(ref));
}

void Canvas::adopt_wall_sections() {
    std::map<gcm::u32,std::vector<gsnap::Shape>> legacy;
    for(const auto& shape:shapes_)if(wall(shape) && !is_joined_wall(shape))legacy[shape.poly].push_back(shape);
    if(legacy.empty())return;
    for(const auto& [poly,shapes]:legacy) {
        auto region=gregion::from_shapes(shapes);if(!region.ok)continue;
        auto remaining=region.region;
        struct Strip {double area;std::vector<gsnap::Shape> edges;};
        std::vector<Strip> strips;
        for(std::size_t i=0;i<shapes.size();++i)for(std::size_t j=i+1;j<shapes.size();++j) {
            const auto& a=shapes[i];const auto& b=shapes[j];
            if(a.kind!=gsnap::ShapeKind::Segment || b.kind!=gsnap::ShapeKind::Segment)continue;
            double ax,ay,bx,by,cx,cy,dx,dy;
            gcm::to_world(vol_,a.a,ax,ay);gcm::to_world(vol_,a.b,bx,by);
            gcm::to_world(vol_,b.a,cx,cy);gcm::to_world(vol_,b.b,dx,dy);
            const double length=std::hypot(bx-ax,by-ay),other=std::hypot(dx-cx,dy-cy);
            if(length<1e-6 || other<1e-6)continue;
            const double ux=(bx-ax)/length,uy=(by-ay)/length,nx=-uy,ny=ux;
            if(std::abs((dx-cx)*nx+(dy-cy)*ny)>other*1e-6)continue;
            const double width=(cx-ax)*nx+(cy-ay)*ny;
            if(std::abs(width)<.025 || std::abs(width)>.6)continue;
            const double c=(cx-ax)*ux+(cy-ay)*uy,d=(dx-ax)*ux+(dy-ay)*uy;
            const double lo=std::max(0.,std::min(c,d)),hi=std::min(length,std::max(c,d));
            if(hi-lo<std::abs(width)*2)continue;
            const gcm::Lattice2 points[]={gcm::to_lattice(vol_,ax+ux*lo,ay+uy*lo),gcm::to_lattice(vol_,ax+ux*hi,ay+uy*hi),
                gcm::to_lattice(vol_,ax+ux*hi+nx*width,ay+uy*hi+ny*width),gcm::to_lattice(vol_,ax+ux*lo+nx*width,ay+uy*lo+ny*width)};
            Strip strip{(hi-lo)*std::abs(width),{}};
            for(int k=0;k<4;++k) {auto edge=a;edge.a=points[k];edge.b=points[(k+1)%4];strip.edges.push_back(edge);}
            strips.push_back(std::move(strip));
        }
        std::sort(strips.begin(),strips.end(),[](const auto& a,const auto& b){return a.area>b.area;});
        for(const auto& strip:strips) {
            const auto box=gregion::from_shapes(strip.edges);if(!box.ok)continue;
            const auto part=gregion::boolean_op(remaining,box.region,gregion::Operation::Intersect);
            if(!part.ok || part.region.empty() || part.region.area(vol_)<.0001)continue;
            auto rest=gregion::boolean_op(remaining,part.region,gregion::Operation::Subtract);if(!rest.ok)continue;
            add_wall_section(boundary(part.region));remaining=std::move(rest.region);
        }
        if(!remaining.empty())add_wall_section(boundary(remaining));
        for(auto& shape:shapes_)if(shape.poly==poly && wall(shape))shape.attrs.user["wall_joined"]="1";
    }
    sync_wall_sections();
}

bool Canvas::sync_wall_sections() {
    std::vector<std::vector<gsnap::Shape>> sources;
    gcm::u64 signature=1469598103934665603ull;
    for(const auto& ref:blocks_.references())if(is_wall_section(ref)) {
        const auto source=blocks_.expand(ref,vol_,8,&appearance_layers_);
        sources.push_back(source);
        for(unsigned char c:outline(source))signature=(signature^c)*1099511628211ull;
        for(unsigned char c:ref.layer)signature=(signature^c)*1099511628211ull;
    }
    const auto fingerprint=std::to_string(signature);
    std::vector<gsnap::Shape> current;
    std::string previous_outline;
    for(const auto& shape:shapes_)if(is_joined_wall(shape)) {
        current.push_back(shape);
        if(const auto saved=shape.attrs.user.find("wall_source_signature");saved!=shape.attrs.user.end() && saved->second==fingerprint)return false;
        if(const auto saved=shape.attrs.user.find("wall_source_outline");saved!=shape.attrs.user.end())previous_outline=saved->second;
    }
    if(sources.empty() && current.empty())return false;
    std::vector<gregion::Region> parts;
    for(const auto& source:sources) {
        auto region=gregion::from_shapes(source);
        if(!region.ok) {message_="Wall section: "+region.error;return false;}
        parts.push_back(std::move(region.region));
    }
    auto joined=unite(std::move(parts));if(!joined.ok) {message_=joined.error;return false;}
    const auto full=joined.region;
    const auto old=previous_outline.empty()?gregion::Result{true,{},full}:read_outline(previous_outline);
    const auto material=gregion::from_shapes(current);
    if(old.ok && material.ok && !current.empty()) {
        const auto gaps=gregion::boolean_op(old.region,material.region,gregion::Operation::Subtract);
        if(gaps.ok && !gaps.region.empty()) {
            const auto cut=gregion::boolean_op(joined.region,gaps.region,gregion::Operation::Subtract);
            if(cut.ok)joined=cut;
        }
    }
    for(const auto& shape:shapes_) {
        const auto saved=shape.attrs.user.find("opening_material");if(saved==shape.attrs.user.end())continue;
        std::istringstream in(saved->second);gcm::u32 group;in>>group;
        std::vector<gsnap::Shape> patch;gsnap::Shape edge;
        while(in>>edge.a.x>>edge.a.y>>edge.b.x>>edge.b.y)patch.push_back(edge);
        const auto gap=gregion::from_shapes(patch);if(!gap.ok)continue;
        const auto cut=gregion::boolean_op(joined.region,gap.region,gregion::Operation::Subtract);if(cut.ok)joined=cut;
    }
    std::vector<gcm::u32> selected;
    for(auto i:selection_)if(i<shapes_.size())selected.push_back(shapes_[i].key);
    std::erase_if(shapes_,[](const auto& shape){return is_joined_wall(shape);});
    const auto poly=next_polyline_id();gcm::u32 order=0;
    for(gcm::u32 loop=0;loop<joined.region.loops.size();++loop)for(const auto& edge:joined.region.loops[loop]) {
        auto shape=edge.shape;shape.key=shape.id=0;
        shape.poly=poly;shape.poly_index=order++;shape.region=true;shape.region_loop=loop;shape.region_reversed=edge.reversed;
        shape.layer="A-WALL";shape.attrs.user["sketch_object"]="wall";shape.attrs.user["wall_joined"]="1";
        shape.attrs.user["wall_source_signature"]=fingerprint;shape.attrs.user.erase("wall_source_outline");
        if(order==1)shape.attrs.user["wall_source_outline"]=outline(boundary(full));
        for(const auto& before:current)if(before.a.x==shape.a.x && before.a.y==shape.a.y && before.b.x==shape.b.x && before.b.y==shape.b.y) {shape.key=before.key;break;}
        shapes_.push_back(std::move(shape));
    }
    assign_keys();selection_.clear();for(auto key:selected)if(auto i=index_of_key(key);i!=npos)selection_.push_back(i);
    ++edit_serial_;align_dirty_=true;rebuild_grips();refresh_hatches();return true;
}

gcm::u32 Canvas::pick_wall_section(double px,double py) const {
    double x,y;view_.world_of(px,py,x,y);const auto at=gcm::to_lattice(vol_,x,y);
    std::vector<gsnap::Shape> material;
    for(const auto& shape:shapes_)if(is_joined_wall(shape) && shape_visible(shape))material.push_back(shape);
    const auto joined=gregion::from_shapes(material);
    if(!joined.ok || !joined.region.contains(at))return npos32;
    for(gcm::u32 i=0;i<blocks_.references().size();++i) {
        const auto& ref=blocks_.references()[i];if(!is_wall_section(ref) || layer_hidden(ref.layer))continue;
        const auto region=gregion::from_shapes(blocks_.expand(ref,vol_,8,&appearance_layers_));
        if(region.ok && region.region.contains(at))return i;
    }
    return npos32;
}
}
