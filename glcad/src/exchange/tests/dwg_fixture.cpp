#include "exchange/dwg_write.hpp"
#include <iostream>
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    gcm::WorkingVolume v; v.extent=1000; v.resolution_bits=32;
    gdxf::Document d;
    for (int i=0;i<2500;++i) {
        gsnap::Shape shape; shape.kind=gsnap::ShapeKind::Segment;
        shape.a=gcm::to_lattice(v,1+i*0.001,2); shape.b=gcm::to_lattice(v,5,7);
        shape.attrs.color=gdoc::Color::aci(1); shape.attrs.lineweight=gdoc::Lineweight::W050;
        d.entities.push_back({shape,"0",""});
    }
    gdxf::Text text; text.pos=gcm::to_lattice(v,3,4); text.text="Independent reader"; text.height=0.003; d.texts.push_back(text);
    gdoc::BlockDef block; block.name="Marker"; block.geometry.push_back(d.entities[0].shape); gdoc::AttributeDef attr; attr.tag="LABEL"; attr.default_value="Default"; attr.prompt="Label"; block.attributes.push_back(attr); d.blocks.define(block);
    gdoc::BlockRef ref; ref.name="Marker"; ref.insertion=gcm::to_lattice(v,10,20); ref.scale_x=2; ref.scale_y=3; ref.rotation=0.5; ref.set_attribute("LABEL","Assigned"); d.blocks.insert(ref);
    d.splines.push_back(ggeo::circle_nurbs<gcm::F64>({gcm::F64(2),gcm::F64(3)},1));
    d.entities[1].shape.attrs.linetype="Dashed";
    d.entities[1].shape.attrs.user["plot_style"]="Review"; d.layers.find("0")->plot_style="Review";
    gdim::Dimension dim; dim.kind=gdim::Kind::Horizontal; dim.p1=gcm::to_lattice(v,1,2); dim.p2=gcm::to_lattice(v,5,2); dim.p3=gcm::to_lattice(v,3,3); d.dimensions.push_back(dim);
    for(auto kind:{gdim::Kind::Radial,gdim::Kind::Diameter}) { auto radial=dim; radial.kind=kind; radial.centre=gcm::to_lattice(v,7,8); radial.radius=gcm::to_lattice(v,2,0).x; radial.p3=gcm::to_lattice(v,10,8); d.dimensions.push_back(radial); }
    gdxf::HatchRec hatch; hatch.solid=true; auto circle=d.entities[0].shape; circle.kind=gsnap::ShapeKind::Circle; circle.radius=gcm::to_lattice(v,1,0).x; hatch.boundary={circle}; d.hatches.push_back(hatch);
    gdwg::WriteOptions options; options.units=gdxf::Units::Metres;
    for (int i=0;i<2;++i) {
        gdoc::Xref x; x.name=i?"Services":"Site"; x.path=i?"../Services.dwg":"refs/Site.dwg";
        x.type=i?gdoc::XrefType::Overlay:gdoc::XrefType::Attach;
        x.state=i?gdoc::XrefState::Unloaded:gdoc::XrefState::Loaded;
        x.insertion=gcm::to_lattice(v,10+i,20+i); x.scale=2; x.rotation=.5;
        x.clip=i?std::vector<gcm::Lattice2>{gcm::to_lattice(v,-1,-2),gcm::to_lattice(v,4,3)}
                :std::vector<gcm::Lattice2>{gcm::to_lattice(v,0,0),gcm::to_lattice(v,4,0),gcm::to_lattice(v,4,4),gcm::to_lattice(v,2,2),gcm::to_lattice(v,0,4)};
        x.clip_enabled=i==0;
        if(i==1) {
            gdoc::BlockDef external; external.name=x.name; external.is_xref=true; external.xref_path=x.path; d.blocks.define(external);
            gdoc::BlockRef placement; placement.name=x.name; placement.insertion=x.insertion; placement.scale_x=2; placement.scale_y=3; placement.rotation=x.rotation; d.blocks.insert(placement);
        }
        d.blocks.attach(std::move(x));
    }
    for(int i=0;i<2;++i) {
        gdxf::Layout sheet; sheet.name=i?"Details":"Floor plan";
        sheet.width=i?.297:.420; sheet.height=i?.210:.297;
        auto shape=d.entities[0].shape; shape.a=gcm::to_lattice(v,.01,.02); shape.b=gcm::to_lattice(v,.10,.02);
        sheet.entities.push_back({shape,"0",""});
        auto note=text; note.pos=gcm::to_lattice(v,.02,.03); note.text=sheet.name; sheet.texts.push_back(note);
        gplot::Viewport vp; vp.lo={.02,.04}; vp.hi={.20,.18}; vp.centre_x=3; vp.centre_y=4; vp.scale=50; vp.rotation=.3; vp.locked=true;
        vp.frozen_layers={"0"}; vp.on=i==0;
        if(i==0) vp.clip_polygon={{.02,.04},{.20,.04},{.16,.18},{.02,.18}};
        gdim::Dimension paper_dim; paper_dim.kind=gdim::Kind::Horizontal;
        paper_dim.p1=gcm::to_lattice(v,.02,.03); paper_dim.p2=gcm::to_lattice(v,.12,.03); paper_dim.p3=gcm::to_lattice(v,.06,.04);
        sheet.dimensions.push_back(paper_dim);
        sheet.viewports.push_back(vp); d.layouts.push_back(sheet);
    }
    gdim::Style callout;callout.name="Callouts";callout.dimtxt=4;callout.dimasz=3;d.dim_styles.push_back(callout);
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.style="Callouts";leader.layer="0";
    leader.p1=gcm::to_lattice(v,1,2);leader.p2=gcm::to_lattice(v,2,3);leader.p3=gcm::to_lattice(v,3,3);leader.text_override="Anchor note";d.dimensions.push_back(leader);
    d.active_layout=1;
    std::string error;
    if (!gdwg::write_file(argv[1],d,v,options,&error)) { std::cerr<<error<<'\n'; return 1; }
}
