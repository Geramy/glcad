#pragma once
#include "engine/block.hpp"
#include "engine/state_archive.hpp"
#include <cmath>

namespace gfile::state {
#define GLCAD_FIELDS(Type, ...) template<> struct Fields<Type> { \
    template<class A, class V> static void visit(A& a, V& v) { a(__VA_ARGS__); } };
GLCAD_FIELDS(gcm::Lattice2, v.x, v.y)
GLCAD_FIELDS(gdoc::Underlay, v.path, v.kind, v.sheet, v.name, v.insertion, v.scale, v.rotation,
             v.fade, v.contrast, v.monochrome, v.visible, v.plottable, v.locked, v.clip,
             v.clip_inverted, v.extent_lo, v.extent_hi, v.found)
struct UnderlayDetails {
    double y_factor{1},source_unit{1},contrast_neutral{50};
    bool clip_enabled{true},adjust_background{false},source_unit_known{false},source_coordinates_pending{false};
    std::vector<gcm::Lattice2> clip_hole;
    std::string layer{"0"};
};
GLCAD_FIELDS(UnderlayDetails,v.y_factor,v.source_unit,v.contrast_neutral,v.clip_enabled,v.adjust_background,v.source_unit_known,v.source_coordinates_pending,v.clip_hole,v.layer)
inline std::vector<UnderlayDetails> underlay_details(const std::vector<gdoc::Underlay>& items) {
    std::vector<UnderlayDetails> result;
    for(const auto& u:items)result.push_back({u.y_factor,u.source_unit,u.contrast_neutral,u.clip_enabled,u.adjust_background,u.source_unit_known,u.source_coordinates_pending,u.clip_hole,u.layer});
    return result;
}
inline bool apply_underlay_details(std::vector<gdoc::Underlay>& items,const std::vector<UnderlayDetails>& details) {
    if(items.size()!=details.size())return false;
    for(std::size_t i=0;i<items.size();++i) {
        auto& u=items[i];const auto& d=details[i];
        if(!std::isfinite(d.y_factor)||d.y_factor==0||!std::isfinite(d.source_unit)||d.source_unit<=0||!std::isfinite(d.contrast_neutral)||d.contrast_neutral<=0||d.clip_hole.size()>4096)return false;
        u.y_factor=d.y_factor;u.source_unit=d.source_unit;u.contrast_neutral=d.contrast_neutral;u.clip_enabled=d.clip_enabled;
        u.adjust_background=d.adjust_background;u.source_unit_known=d.source_unit_known;u.source_coordinates_pending=d.source_coordinates_pending;u.clip_hole=d.clip_hole;u.layer=d.layer;
    }
    return true;
}
GLCAD_FIELDS(gdoc::BlockParameter, v.name, v.kind, v.base, v.end, v.value,
             v.minimum, v.maximum, v.increment, v.choices, v.choice)
GLCAD_FIELDS(gdoc::BlockAction, v.name, v.kind, v.parameter, v.geometry, v.dx,
             v.dy, v.scale, v.origin, v.window_lo, v.window_hi)
GLCAD_FIELDS(gdoc::DynamicBlock::VisibilitySet, v.name, v.geometry)
GLCAD_FIELDS(gdoc::DynamicBlock, v.name, v.parameters, v.actions, v.visibility, v.visible_set)
GLCAD_FIELDS(gdoc::Xref, v.name, v.path, v.resolved_path, v.type, v.state,
             v.insertion, v.scale, v.rotation, v.clip, v.clip_inverted, v.clip_enabled, v.nested)
#undef GLCAD_FIELDS
}
