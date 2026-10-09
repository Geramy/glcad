#include "test.hpp"
#include "exchange/dxf.hpp"
#include "exchange/dwg.hpp"
#include "exchange/dwg_write.hpp"
#include "geometry/region.hpp"

#include <algorithm>
#include <cmath>

namespace {
std::vector<gsnap::Shape> rectangle(const gcm::WorkingVolume& v,double x,double y,double w,double h) {
    const gcm::Lattice2 points[]={gcm::to_lattice(v,x,y),gcm::to_lattice(v,x+w,y),
        gcm::to_lattice(v,x+w,y+h),gcm::to_lattice(v,x,y+h)};
    std::vector<gsnap::Shape> shapes;
    for(int i=0;i<4;++i) {gsnap::Shape s;s.a=points[i];s.b=points[(i+1)%4];shapes.push_back(s);}
    return shapes;
}
gsnap::Shape circle(const gcm::WorkingVolume& v,double x,double y,double r) {
    gsnap::Shape s;s.kind=gsnap::ShapeKind::Circle;s.a=gcm::to_lattice(v,x,y);
    s.radius=std::llround(r*v.scale());return s;
}
std::vector<gsnap::Shape> members(const gregion::Region& region,gcm::u32 poly) {
    std::vector<gsnap::Shape> result;
    for(gcm::u32 loop=0;loop<region.loops.size();++loop)for(const auto& edge:region.loops[loop]) {
        auto s=edge.shape;s.poly=poly;s.poly_index=gcm::u32(result.size());s.key=poly*100+s.poly_index+1;
        s.region=true;s.region_loop=loop;s.region_reversed=edge.reversed;
        s.layer="Boundary";s.attrs.color=gdoc::Color::aci(3);s.attrs.lineweight=gdoc::Lineweight::W050;
        result.push_back(s);
    }
    return result;
}
}

TEST(region_export_preserves_holes_islands_curves_blocks_and_units_without_mutating_input) {
    gcm::WorkingVolume v;v.extent=1000;v.resolution_bits=40;
    auto edges=rectangle(v,0,0,8,6);
    edges.push_back(circle(v,3,3,1));edges.push_back(circle(v,12,3,1.5));
    const auto source=gregion::from_shapes(edges);CHECK(source.ok);if(!source.ok)return;
    const auto disk=gregion::from_shapes({circle(v,20,3,2)});
    const auto box=gregion::from_shapes(rectangle(v,21,0,3,6));
    const auto trimmed=gregion::boolean_op(disk.region,box.region,gregion::Operation::Subtract);
    CHECK(trimmed.ok);if(!trimmed.ok)return;
    gdxf::Document d;gdoc::Layer layer;layer.name="Boundary";d.layers.add(layer);
    for(const auto& s:members(source.region,1))d.entities.push_back({s,s.layer,{}});
    for(const auto& s:members(trimmed.region,2))d.entities.push_back({s,s.layer,{}});
    gdoc::BlockDef block;block.name="Curved region";block.geometry=members(trimmed.region,1);
    block.base=gcm::to_lattice(v,20,3);d.blocks.define(block);
    gdoc::BlockRef ref;ref.name=block.name;ref.insertion=gcm::to_lattice(v,30,3);ref.rotation=.25;
    d.blocks.insert(ref);
    gdxf::Layout sheet;sheet.name="Regions";for(const auto& s:block.geometry)sheet.entities.push_back({s,s.layer,{}});
    d.layouts.push_back(sheet);
    const auto check=[&](const auto& result,const auto& blocks,const char* format,gdxf::Units unit) {
        CHECK(result.size()==d.entities.size());std::vector<gsnap::Shape> back;
        for(const auto& e:result) {back.push_back(e.shape);CHECK(e.layer=="Boundary");CHECK(e.shape.attrs.color==gdoc::Color::aci(3));}
        const auto region=gregion::from_shapes(back);CHECK_MSG(region.ok,"%s units=%d: %s",format,int(unit),region.error.c_str());
        if(region.ok) {
            CHECK(std::abs(region.region.area(v)-source.region.area(v)-trimmed.region.area(v))<1e-6);
            CHECK(!region.region.contains(gcm::to_lattice(v,3,3)));
            CHECK(region.region.contains(gcm::to_lattice(v,12,3)));
            CHECK(!region.region.contains(gcm::to_lattice(v,21.5,3)));
        }
        CHECK(std::count_if(back.begin(),back.end(),[](const auto& s){return s.kind==gsnap::ShapeKind::Circle;})==2);
        CHECK(std::any_of(back.begin(),back.end(),[](const auto& s){return s.kind==gsnap::ShapeKind::Arc;}));
        CHECK(blocks.references().size()==1);const auto* restored=blocks.find(block.name);CHECK(restored!=nullptr);
        if(restored) {const auto boundary=gregion::from_shapes(restored->geometry);CHECK_MSG(boundary.ok,"%s units=%d: %s",format,int(unit),boundary.error.c_str());
            if(boundary.ok)CHECK(std::abs(boundary.region.area(v)-trimmed.region.area(v))<1e-6);}
    };
    for(auto unit:{gdxf::Units::Metres,gdxf::Units::Millimetres}) {
        gdxf::WriteOptions dx;dx.units=unit;const auto text=gdxf::write(d,v,dx);CHECK(!text.empty());
        const auto a=gdxf::read(text,v);CHECK_MSG(a.ok,"%s",a.error.c_str());check(a.document.entities,a.document.blocks,"DXF",unit);
        gdwg::WriteOptions dw;dw.units=unit;std::string error;const auto bytes=gdwg::write(d,v,dw,&error);
        CHECK_MSG(!bytes.empty(),"%s",error.c_str());
        auto b=gdwg::read(bytes.data(),bytes.size(),v);CHECK_MSG(b.ok,"%s",b.error.c_str());
        CHECK(b.insunits==int(unit));
        // The DWG reader returns file units; the importer applies INSUNITS.
        const auto scale=[&](gsnap::Shape& shape) {
            const double factor=gdxf::metres_per(unit);
            for(auto* p:{&shape.a,&shape.b}) {p->x=std::llround(p->x*factor);p->y=std::llround(p->y*factor);}
            shape.radius=std::llround(shape.radius*factor);
        };
        for(auto& e:b.entities)scale(e.shape);
        const auto definitions=b.blocks.all();
        for(auto def:definitions) {for(auto& shape:def.geometry)scale(shape);b.blocks.define(std::move(def));}
        check(b.entities,b.blocks,"DWG",unit);
        CHECK(b.layouts.size()==1);
        if(!b.layouts.empty()) {std::vector<gsnap::Shape> shapes;for(const auto& e:b.layouts[0].entities)shapes.push_back(e.shape);
            const auto boundary=gregion::from_shapes(shapes);CHECK(boundary.ok);
            if(boundary.ok)CHECK(std::abs(boundary.region.area(v)-trimmed.region.area(v))<1e-6);}
    }
    CHECK(d.entities.front().shape.region);CHECK(d.blocks.find(block.name)->geometry.front().region);
    CHECK(d.layouts.front().entities.front().shape.region);
    d.entities.erase(d.entities.begin());
    CHECK(gdxf::write(d,v).empty());std::string error;CHECK(gdwg::write(d,v,{},&error).empty());
    CHECK(error.find("invalid region boundary")!=std::string::npos);
}
