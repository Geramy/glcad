#include "exchange/region_export.hpp"
#include "geometry/region.hpp"

#include <algorithm>
#include <set>

namespace gexchange {
bool prepare_region_boundaries(gdxf::Document& document, std::string& error) {
    const auto boundary=[](gsnap::Shape& shape) {
        if(!shape.region)return;
        shape.region=false;
        shape.region_loop=shape.poly=shape.poly_index=0;
        shape.region_reversed=false;
    };
    const auto entities=[&](auto& values) {
        std::vector<gsnap::Shape> shapes;
        for(const auto& value:values)shapes.push_back(value.shape);
        error=gregion::validate_members(shapes);
        if(!error.empty())return false;
        for(auto& value:values)boundary(value.shape);
        return true;
    };
    if(!entities(document.entities))return false;
    for(auto& layout:document.layouts)if(!entities(layout.entities))return false;

    std::set<std::string> wall_sources;
    for(const auto& block:document.blocks.all())
        if(block.source_owner=="glcad.wall")wall_sources.insert(block.name);
    const auto hidden=[&](const gdoc::BlockRef& ref) {
        return ref.attrs.user.contains("wall_section") || wall_sources.contains(ref.name);
    };
    auto references=document.blocks.references();
    std::erase_if(references,hidden);
    document.blocks.set_references(std::move(references));
    for(auto& layout:document.layouts)std::erase_if(layout.references,hidden);
    const auto blocks=document.blocks.all();
    for(auto block:blocks) {
        if(!wall_sources.contains(block.name)) {
            error=gregion::validate_members(block.geometry);
            if(!error.empty())return false;
            for(auto& shape:block.geometry)boundary(shape);
        }
        std::erase_if(block.nested,[&](const auto& name){return wall_sources.contains(name);});
        std::erase_if(block.nested_refs,hidden);
        document.blocks.define(std::move(block));
    }
    for(const auto& name:wall_sources)document.blocks.remove(name);
    return true;
}
}
