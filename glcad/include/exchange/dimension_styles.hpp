#pragma once

#include "geometry/dimension.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

namespace gexchange::detail {

inline bool prepare_extension_offsets(std::vector<gdim::Dimension>& dimensions,const gcm::WorkingVolume& volume) {
    const auto add=[](gcm::i64 a,gcm::i64 b,gcm::i64& result) {
        if((b>0 && a>std::numeric_limits<gcm::i64>::max()-b) ||
           (b<0 && a<std::numeric_limits<gcm::i64>::min()-b))return false;
        result=a+b;return true;
    };
    for(auto& dim:dimensions) {
        const auto offset=dim.extension_offset;
        if(!offset.x && !offset.y)continue;
        if(dim.kind!=gdim::Kind::Linear && dim.kind!=gdim::Kind::Horizontal &&
           dim.kind!=gdim::Kind::Vertical && dim.kind!=gdim::Kind::Aligned)return false;
        if(dim.kind==gdim::Kind::Linear)
            dim.kind=gdim::linear_is_horizontal(dim,volume)?gdim::Kind::Horizontal:gdim::Kind::Vertical;
        if(!add(dim.p1.x,offset.x,dim.p1.x) || !add(dim.p1.y,offset.y,dim.p1.y) ||
           !add(dim.p2.x,offset.x,dim.p2.x) || !add(dim.p2.y,offset.y,dim.p2.y))return false;
        dim.extension_offset={};dim.o1={};dim.o2={};
    }
    return true;
}

inline bool prepare_leader_styles(std::vector<gdim::Dimension>& dimensions,
                                  std::vector<gdim::Style>& styles,
                                  const gdim::Style& fallback) {
    if(styles.empty())styles.push_back(fallback);
    std::map<std::pair<std::string,double>,std::string> overrides;
    for(auto& dim:dimensions) {
        auto found=std::find_if(styles.begin(),styles.end(),[&](const auto& s){return s.name==dim.style;});
        if(found==styles.end()) {
            auto style=fallback;style.name=dim.style;styles.push_back(std::move(style));found=styles.end()-1;
        }
        if(dim.kind!=gdim::Kind::Leader)continue;
        if(!std::isfinite(dim.leader_arrow_size)||dim.leader_arrow_size<0)return false;
        if(dim.leader_arrow_size==0)continue;
        if(!std::isfinite(found->dimscale)||found->dimscale<=0)return false;
        if(std::abs(found->scaled(found->dimasz)*.001-dim.leader_arrow_size)<1e-12)continue;
        const auto key=std::make_pair(dim.style,dim.leader_arrow_size);
        if(const auto it=overrides.find(key);it!=overrides.end()) {dim.style=it->second;continue;}
        auto style=*found;style.name+="_Leader";
        while(std::any_of(styles.begin(),styles.end(),[&](const auto& s){return s.name==style.name;}))style.name+="_";
        style.dimasz=dim.leader_arrow_size*1000/style.dimscale;
        overrides.emplace(key,style.name);dim.style=style.name;styles.push_back(std::move(style));
    }
    return true;
}

}
