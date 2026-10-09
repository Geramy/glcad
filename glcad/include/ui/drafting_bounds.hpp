#pragma once

#include "engine/hatch.hpp"
#include <algorithm>
#include <map>
#include <numeric>

namespace gcapp {
struct DraftingBounds {
    long double lx{0},ly{0},hx{0},hy{0};
    bool valid{false};
    void include(long double x,long double y) {
        if(!valid){lx=hx=x;ly=hy=y;valid=true;}
        else {lx=std::min(lx,x);ly=std::min(ly,y);hx=std::max(hx,x);hy=std::max(hy,y);}
    }
    void include(const DraftingBounds& b) {if(b.valid){include(b.lx,b.ly);include(b.hx,b.hy);}}
    DraftingBounds expanded(long double margin) const {
        auto b=*this;b.lx-=margin;b.ly-=margin;b.hx+=margin;b.hy+=margin;return b;
    }
    bool overlaps(const DraftingBounds& b) const {
        return valid&&b.valid&&lx<=b.hx&&b.lx<=hx&&ly<=b.hy&&b.ly<=hy;
    }
    bool contains(const DraftingBounds& b) const {
        return valid&&b.valid&&lx<=b.lx&&ly<=b.ly&&hx>=b.hx&&hy>=b.hy;
    }
};
inline DraftingBounds drafting_bounds(const gsnap::Shape& s) {
    DraftingBounds b;b.include(s.a.x,s.a.y);
    if(s.is_curve()) {b.include(static_cast<long double>(s.a.x)-s.radius,static_cast<long double>(s.a.y)-s.radius);
        b.include(static_cast<long double>(s.a.x)+s.radius,static_cast<long double>(s.a.y)+s.radius);}
    else b.include(s.b.x,s.b.y);
    return b;
}
inline DraftingBounds drafting_bounds(const std::vector<gsnap::Shape>& shapes) {
    DraftingBounds b;for(const auto& s:shapes)b.include(drafting_bounds(s));return b;
}
inline DraftingBounds drafting_bounds(const gdoc::AssociativeHatch& h) {
    auto b=drafting_bounds(h.boundary_geometry);
    if(b.valid)return b;
    for(const auto& loop:h.cached.fill_loops)for(auto p:loop)b.include(p.x,p.y);
    if(b.valid)return b;
    for(const auto& s:h.cached.lines){b.include(s.a.x,s.a.y);b.include(s.b.x,s.b.y);}
    return b;
}
inline bool drafting_touches(const DraftingBounds& bounds,const std::vector<DraftingBounds>& queries) {
    return std::any_of(queries.begin(),queries.end(),[&](const auto& q){return bounds.overlaps(q);});
}
inline bool drafting_hatch_affected(const gdoc::AssociativeHatch& h,const std::vector<DraftingBounds>& dirty,long double gap) {
    if(dirty.empty())return true;
    if(h.boundary_geometry.empty()&&h.cached.fill_loops.empty())return true;
    return drafting_touches(drafting_bounds(h).expanded(gap),dirty);
}

inline std::vector<bool> nearby_wall_contours(const std::vector<gsnap::Shape>& walls,
                                             const std::vector<DraftingBounds>& queries) {
    std::vector<std::size_t> parent(walls.size());std::iota(parent.begin(),parent.end(),0);
    const auto root=[&](std::size_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    std::map<std::pair<gcm::i64,gcm::i64>,std::size_t> vertices;
    for(std::size_t i=0;i<walls.size();++i)for(auto p:{walls[i].a,walls[i].b}) {
        const auto [it,added]=vertices.emplace(std::pair{p.x,p.y},i);
        if(!added)parent[root(i)]=root(it->second);
    }
    std::map<std::size_t,DraftingBounds> bounds;
    for(std::size_t i=0;i<walls.size();++i)bounds[root(i)].include(drafting_bounds(walls[i]));
    std::map<std::size_t,bool> selected;
    for(const auto& [id,b]:bounds)selected[id]=drafting_touches(b,queries);
    std::vector<std::size_t> pending;
    for(const auto& [id,keep]:selected)if(keep)pending.push_back(id);
    while(!pending.empty()) {
        const auto outer=bounds.at(pending.back());pending.pop_back();
        for(const auto& [id,b]:bounds)if(!selected[id]&&(outer.contains(b)||b.contains(outer))) {
            selected[id]=true;pending.push_back(id);
        }
    }
    std::vector<bool> keep(walls.size());
    for(std::size_t i=0;i<walls.size();++i)keep[i]=selected[root(i)];
    return keep;
}
}
