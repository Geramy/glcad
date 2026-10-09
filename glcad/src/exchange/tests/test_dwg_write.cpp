#include "test.hpp"

#include <cmath>

#include "exchange/dwg.hpp"
#include "exchange/dwg_write.hpp"

using namespace gdwg;

namespace {

gcm::WorkingVolume volume() {
    gcm::WorkingVolume v;
    v.extent = 1000.0;
    v.resolution_bits = 32;
    return v;
}

gsnap::Shape segment(const gcm::WorkingVolume& v, double ax, double ay, double bx, double by) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = gcm::to_lattice(v, ax, ay);
    s.b = gcm::to_lattice(v, bx, by);
    return s;
}

gsnap::Shape circle(const gcm::WorkingVolume& v, double cx, double cy, double r) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Circle;
    s.a = gcm::to_lattice(v, cx, cy);
    s.b = s.a;
    s.radius = static_cast<gcm::i64>(std::llround(r * v.scale()));
    return s;
}

gdwg::WriteOptions metres() {
    gdwg::WriteOptions o;
    o.units = gdxf::Units::Metres;
    return o;
}

gdxf::Document one_line(const gcm::WorkingVolume& v) {
    gdxf::Document d;
    gdxf::Entity e;
    e.shape = segment(v, 1.0, 2.0, 5.0, 7.0);
    e.layer = "0";
    d.entities.push_back(e);
    return d;
}

}

TEST(dwg_write_produces_a_file_the_reader_accepts) {
    const gcm::WorkingVolume v = volume();
    const std::vector<gcm::u8> bytes = write(one_line(v), v, metres());
    CHECK(!bytes.empty());

    FileHeader h;
    std::string error;
    CHECK_MSG(read_header(bytes.data(), bytes.size(), h, error), "%s", error.c_str());
    CHECK(h.version == Version::R2000);
    CHECK(h.tag == "AC1015");
    CHECK(h.sections.size() == 6);
}

TEST(dwg_write_a_line_comes_back_where_it_was_put) {
    const gcm::WorkingVolume v = volume();
    const std::vector<gcm::u8> bytes = write(one_line(v), v, metres());
    const ReadResult r = read(bytes.data(), bytes.size(), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.entities.size() == 1, "%zu entities came back", r.entities.size());

    double ax, ay, bx, by;
    gcm::to_world(v, r.entities[0].shape.a, ax, ay);
    gcm::to_world(v, r.entities[0].shape.b, bx, by);
    CHECK(r.entities[0].shape.kind == gsnap::ShapeKind::Segment);
    CHECK(std::fabs(ax - 1.0) < 1e-6 && std::fabs(ay - 2.0) < 1e-6);
    CHECK(std::fabs(bx - 5.0) < 1e-6 && std::fabs(by - 7.0) < 1e-6);
}

TEST(dwg_write_carries_circles_arcs_and_points) {
    const gcm::WorkingVolume v = volume();
    gdxf::Document d;
    {
        gdxf::Entity e; e.shape = circle(v, 3.0, 4.0, 2.5); d.entities.push_back(e);
    }
    {
        gdxf::Entity e;
        e.shape = circle(v, -6.0, 1.0, 4.0);
        e.shape.kind = gsnap::ShapeKind::Arc;
        e.shape.start_angle = 0.25;
        e.shape.end_angle = 2.0;
        d.entities.push_back(e);
    }
    {
        gdxf::Entity e;
        e.shape.kind = gsnap::ShapeKind::Point;
        e.shape.a = gcm::to_lattice(v, 8.0, -3.0);
        e.shape.b = e.shape.a;
        d.entities.push_back(e);
    }

    const std::vector<gcm::u8> bytes = write(d, v, metres());
    const ReadResult r = read(bytes.data(), bytes.size(), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.entities.size() == 3, "%zu entities came back", r.entities.size());

    std::size_t circles = 0, arcs = 0, points = 0;
    for (const Entity& e : r.entities) {
        if (e.shape.kind == gsnap::ShapeKind::Circle) {
            ++circles;
            double x, y;
            gcm::to_world(v, e.shape.a, x, y);
            CHECK(std::fabs(x - 3.0) < 1e-6 && std::fabs(y - 4.0) < 1e-6);
            CHECK(std::fabs(double(e.shape.radius) / v.scale() - 2.5) < 1e-6);
        } else if (e.shape.kind == gsnap::ShapeKind::Arc) {
            ++arcs;
            CHECK(std::fabs(e.shape.start_angle - 0.25) < 1e-9);
            CHECK(std::fabs(e.shape.end_angle - 2.0) < 1e-9);
        } else if (e.shape.kind == gsnap::ShapeKind::Point) {
            ++points;
        }
    }
    CHECK(circles == 1 && arcs == 1 && points == 1);
}

TEST(dwg_write_states_its_units_where_the_reader_looks) {
    const gcm::WorkingVolume v = volume();
    gdwg::WriteOptions o;
    o.units = gdxf::Units::Millimetres;
    const std::vector<gcm::u8> bytes = write(one_line(v), v, o);
    const ReadResult r = read(bytes.data(), bytes.size(), v);
    CHECK(r.ok);
    CHECK_MSG(r.insunits == static_cast<gcm::i32>(gdxf::Units::Millimetres),
              "INSUNITS came back as %d", r.insunits);
}

TEST(dwg_write_millimetres_scale_the_numbers_in_the_file) {
    const gcm::WorkingVolume v = volume();
    gdxf::Document d;
    gdxf::Entity e;
    e.shape = segment(v, 0.0, 0.0, 1.0, 0.0);
    d.entities.push_back(e);

    gdwg::WriteOptions o;
    o.units = gdxf::Units::Millimetres;
    const std::vector<gcm::u8> bytes = write(d, v, o);

    gcm::WorkingVolume big;
    big.extent = 100000.0;
    big.resolution_bits = 32;
    const ReadResult r = read(bytes.data(), bytes.size(), big);
    CHECK(r.ok);
    CHECK(r.entities.size() == 1);
    double ax, ay, bx, by;
    gcm::to_world(big, r.entities[0].shape.a, ax, ay);
    gcm::to_world(big, r.entities[0].shape.b, bx, by);
    CHECK_MSG(std::fabs((bx - ax) - 1000.0) < 1e-3, "the line came back %f long", bx - ax);
}

TEST(dwg_write_layers_come_back_named_and_coloured) {
    const gcm::WorkingVolume v = volume();
    gdxf::Document d;
    gdoc::Layer wall;
    wall.name = "A-WALL";
    wall.color = gdoc::Color::aci(3);
    d.layers.add(wall);
    gdoc::Layer hidden;
    hidden.name = "A-HIDE";
    hidden.on = false;
    hidden.locked = true;
    d.layers.add(hidden);

    gdxf::Entity e;
    e.shape = segment(v, 0.0, 0.0, 1.0, 1.0);
    e.layer = "A-WALL";
    d.entities.push_back(e);

    const std::vector<gcm::u8> bytes = write(d, v, metres());
    const ReadResult r = read(bytes.data(), bytes.size(), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());

    const gdoc::Layer* wall_back = r.layers.find("A-WALL");
    CHECK_MSG(wall_back != nullptr, "A-WALL did not come back");
    if (wall_back) {
        CHECK(wall_back->color.index == 3);
        CHECK(wall_back->on && !wall_back->locked);
    }
    const gdoc::Layer* hidden_back = r.layers.find("A-HIDE");
    CHECK_MSG(hidden_back != nullptr, "A-HIDE did not come back");
    if (hidden_back) {
        CHECK(!hidden_back->on);
        CHECK(hidden_back->locked);
    }
    CHECK(r.entities.size() == 1);
    if (r.entities.size() == 1) CHECK(r.entities[0].layer == "A-WALL");
}

TEST(dwg_write_notes_come_back_as_text) {
    const gcm::WorkingVolume v = volume();
    gdxf::Document d;
    gdxf::Text t;
    t.pos = gcm::to_lattice(v, 2.0, 3.0);
    t.text = "ROOM 101";
    t.height = 0.25;
    t.angle = 0.0;
    d.texts.push_back(t);

    const std::vector<gcm::u8> bytes = write(d, v, metres());
    const ReadResult r = read(bytes.data(), bytes.size(), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.texts.size() == 1, "%zu notes came back", r.texts.size());
    if (r.texts.size() == 1) {
        CHECK(r.texts[0].text == "ROOM 101");
        CHECK(std::fabs(r.texts[0].height - 0.25) < 1e-9);
        double x, y;
        gcm::to_world(v, r.texts[0].pos, x, y);
        CHECK(std::fabs(x - 2.0) < 1e-6 && std::fabs(y - 3.0) < 1e-6);
    }
}

TEST(dwg_write_blocks_come_back_defined) {
    const gcm::WorkingVolume v = volume();
    gdxf::Document d;
    gdoc::BlockDef def;
    def.name = "DOOR";
    def.base = gcm::to_lattice(v, 0.0, 0.0);
    def.geometry.push_back(segment(v, 0.0, 0.0, 0.9, 0.0));
    def.geometry.push_back(segment(v, 0.9, 0.0, 0.9, 0.05));
    d.blocks.define(def);

    const std::vector<gcm::u8> bytes = write(d, v, metres());
    const ReadResult r = read(bytes.data(), bytes.size(), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.blocks.find("DOOR") != nullptr, "DOOR was not defined in the file");
    CHECK_MSG(r.entities.empty(), "%zu loose entities where none were exported",
              r.entities.size());
}

TEST(dwg_write_refuses_a_version_it_does_not_write) {
    const gcm::WorkingVolume v = volume();
    gdwg::WriteOptions o;
    o.version = "AC1032";
    std::string error;
    CHECK(write(one_line(v), v, o, &error).empty());
    CHECK(!error.empty());
}

TEST(dwg_write_an_empty_drawing_is_still_a_drawing) {
    const gcm::WorkingVolume v = volume();
    gdxf::Document d;
    const std::vector<gcm::u8> bytes = write(d, v, metres());
    CHECK(!bytes.empty());
    const ReadResult r = read(bytes.data(), bytes.size(), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.entities.empty());
    CHECK(r.layers.find("0") != nullptr);
}

TEST(dwg_write_rejects_unsupported_content_instead_of_dropping_it) {
    auto d=one_line(volume()); std::string error;
    d.dimensions.push_back({}); d.dimensions.back().kind=gdim::Kind::ArcLength; CHECK(write(d,volume(),metres(),&error).empty());
    CHECK(error.find("dimensions")!=std::string::npos);
    d.dimensions.clear(); d.entities[0].shape.attrs.color=gdoc::Color::rgb(123,45,67);
    CHECK(write(d,volume(),metres(),&error).empty());
}

TEST(dwg_write_native_dimensions_hatches_splines_and_unicode) {
    const auto v=volume(); gdxf::Document d;
    gdim::Dimension dim; dim.kind=gdim::Kind::Horizontal; dim.p1=gcm::to_lattice(v,0,0); dim.p2=gcm::to_lattice(v,4,0); dim.p3=gcm::to_lattice(v,2,1); d.dimensions.push_back(dim);
    gdxf::HatchRec hatch; hatch.solid=true; hatch.boundary={circle(v,1,2,1)}; d.hatches.push_back(hatch);
    d.splines.push_back(ggeo::circle_nurbs<gcm::F64>({gcm::F64(2),gcm::F64(3)},1));
    gdxf::Text note; note.text="Café 東京 😀"; note.height=.003; d.texts.push_back(note);
    std::string error; auto bytes=write(d,v,metres(),&error); CHECK_MSG(!bytes.empty(),"%s",error.c_str());
    auto r=read(bytes.data(),bytes.size(),v); CHECK_MSG(r.ok,"%s",r.error.c_str());
    CHECK(r.dimensions.size()==1); CHECK(r.hatches.size()==1); CHECK(r.splines.size()==1);
    CHECK(std::any_of(r.texts.begin(),r.texts.end(),[&](const auto& t){return t.text==note.text;}));
    d.texts[0].text=std::string(1,char(0xff)); CHECK(write(d,v,metres(),&error).empty());
}

TEST(dwg_write_block_attributes_and_radial_dimensions_roundtrip) {
    const auto v=volume(); gdxf::Document d;
    gdoc::BlockDef block; block.name="Label"; gdoc::AttributeDef attribute; attribute.tag="NUMBER"; attribute.default_value="Default"; attribute.prompt="Number"; block.attributes.push_back(attribute); d.blocks.define(block);
    gdoc::BlockRef ref; ref.name=block.name; ref.set_attribute("NUMBER","A-101"); d.blocks.insert(ref);
    gdim::Dimension dim; dim.kind=gdim::Kind::Radial; dim.centre=gcm::to_lattice(v,5,6); dim.p3=gcm::to_lattice(v,9,6); dim.radius=gcm::to_lattice(v,2,0).x; d.dimensions.push_back(dim);
    auto bytes=write(d,v,metres()); CHECK(!bytes.empty()); auto r=read(bytes.data(),bytes.size(),v); CHECK(r.ok);
    CHECK(r.blocks.find("Label")!=nullptr);
    if(r.blocks.find("Label")) CHECK(r.blocks.find("Label")->attributes.size()==1);
    CHECK(r.blocks.references().size()==1);
    if(!r.blocks.references().empty()) CHECK(r.blocks.references()[0].attribute("NUMBER").value_or("")=="A-101");
    CHECK(r.dimensions.size()==1);
    if(!r.dimensions.empty()) CHECK(std::abs(gdim::measure(r.dimensions[0].dim,v)-2)<1e-6);
}

TEST(dwg_write_block_attribute_notes_roundtrip) {
    const auto v=volume(); gdxf::Document d;
    gdoc::BlockDef block; block.name="Tag"; block.base=gcm::to_lattice(v,0,0);
    gdoc::AttributeDef attribute; attribute.tag="NUMBER"; attribute.default_value="Default";
    attribute.prompt="Number"; attribute.position=gcm::to_lattice(v,1,0); attribute.height=.005;
    block.attributes.push_back(attribute); d.blocks.define(block);
    gdoc::BlockRef ref; ref.name=block.name; ref.insertion=gcm::to_lattice(v,10,20);
    ref.scale_x=2; ref.scale_y=2; ref.set_attribute("NUMBER","A-101"); d.blocks.insert(ref);
    const auto bytes=write(d,v,metres()); CHECK(!bytes.empty());
    const auto r=read(bytes.data(),bytes.size(),v); CHECK(r.ok);
    CHECK(r.blocks.references().size()==1);
    if(r.blocks.references().empty())return;
    const auto& back=r.blocks.references()[0];
    CHECK(back.attribute("NUMBER").value_or("")=="A-101");
    CHECK(back.notes.size()==1);
    if(back.notes.empty())return;
    CHECK(back.notes[0].text=="A-101");
    double x,y; gcm::to_world(v,back.notes[0].pos,x,y);
    CHECK(std::abs(x-1)<1e-6 && std::abs(y-0)<1e-6);
    CHECK(std::abs(back.notes[0].height-.005)<1e-9);
}

TEST(dwg_write_external_references_retain_paths_modes_and_placement) {
    const auto v=volume(); auto d=one_line(v);
    for (int i=0;i<2;++i) {
        gdoc::Xref x; x.name=i?"Services":"Site"; x.path=i?"../consultants/Services.dwg":"refs/東京.dwg";
        x.type=i?gdoc::XrefType::Overlay:gdoc::XrefType::Attach;
        x.state=i?gdoc::XrefState::Unloaded:gdoc::XrefState::Loaded;
        x.insertion=gcm::to_lattice(v,10+i,20+i); x.scale=2; x.rotation=.5;
        x.clip=i?std::vector<gcm::Lattice2>{gcm::to_lattice(v,-1,-2),gcm::to_lattice(v,4,3)}
                :std::vector<gcm::Lattice2>{gcm::to_lattice(v,0,0),gcm::to_lattice(v,4,0),gcm::to_lattice(v,4,4),gcm::to_lattice(v,2,2),gcm::to_lattice(v,0,4)};
        x.clip_enabled=i==0;
        d.blocks.attach(x);
        auto cached=d.entities.front(); cached.layer=cached.shape.layer="XREF|"+x.name; d.entities.push_back(cached);
    }
    std::string error; auto bytes=write(d,v,metres(),&error);
    CHECK_MSG(!bytes.empty(),"%s",error.c_str());
    const auto r=read(bytes.data(),bytes.size(),v); CHECK_MSG(r.ok,"%s",r.error.c_str());
    CHECK(r.entities.size()==1); CHECK(r.blocks.references().size()==2); CHECK(r.blocks.xrefs().size()==2);
    for (const auto& x:d.blocks.xrefs()) {
        const auto* back=r.blocks.xref(x.name); CHECK(back!=nullptr); if(!back) continue;
        CHECK(back->path==x.path); CHECK(back->type==x.type);
        CHECK((back->state==gdoc::XrefState::Unloaded)==(x.state==gdoc::XrefState::Unloaded));
        CHECK(back->insertion.x==x.insertion.x && back->insertion.y==x.insertion.y);
        CHECK(std::abs(back->scale-2)<1e-12); CHECK(std::abs(back->rotation-.5)<1e-12);
        CHECK(r.blocks.find(x.name) && r.blocks.find(x.name)->is_xref);
        CHECK(back->clip_enabled==x.clip_enabled); CHECK(back->clip.size()==x.clip.size());
        if(back->clip.size()==x.clip.size()) for(std::size_t i=0;i<x.clip.size();++i) CHECK(back->clip[i].x==x.clip[i].x && back->clip[i].y==x.clip[i].y);
    }
    gdxf::Document again; again.blocks=r.blocks;
    auto second=write(again,v,metres(),&error); CHECK_MSG(!second.empty(),"%s",error.c_str());
    const auto reread=read(second.data(),second.size(),v); CHECK(reread.ok); CHECK(reread.blocks.xrefs().size()==2);
    auto clipped=d; clipped.blocks.xref("Site")->clip={gcm::to_lattice(v,0,0),gcm::to_lattice(v,0,1)};
    CHECK(write(clipped,v,metres(),&error).empty()); CHECK(error.find("clipping")!=std::string::npos);
    auto conflicting=d; gdoc::BlockDef local; local.name="Site"; conflicting.blocks.define(local);
    CHECK(write(conflicting,v,metres(),&error).empty()); CHECK(error.find("conflicts")!=std::string::npos);
    auto foreign=d; foreign.blocks.xref("Site")->path="refs/Site.dxf";
    CHECK(write(foreign,v,metres(),&error).empty()); CHECK(error.find("point to DWG")!=std::string::npos);
    auto nonuniform=d; gdoc::BlockDef external; external.name="Site"; external.is_xref=true; external.xref_path="refs/東京.dwg";
    nonuniform.blocks.define(external); gdoc::BlockRef extra; extra.name="Site"; extra.scale_y=3; nonuniform.blocks.insert(extra);
    auto transformed=write(nonuniform,v,metres(),&error); CHECK_MSG(!transformed.empty(),"%s",error.c_str());
    const auto placed=read(transformed.data(),transformed.size(),v); CHECK(placed.ok);
    CHECK(placed.blocks.xrefs().size()==2); CHECK(placed.blocks.references().size()==2);
    if(!placed.blocks.references().empty()) CHECK(placed.blocks.references()[0].scale_y==3);
    gdxf::Document third; third.blocks=placed.blocks;
    CHECK_MSG(!write(third,v,metres(),&error).empty(),"%s",error.c_str());
}

TEST(dwg_nested_xrefs_keep_parent_insertions_cached_members_and_clips) {
    const auto v=volume();gdxf::Document d;
    gdoc::Xref root;root.name="Campus";root.path="refs/Campus.dwg";root.state=gdoc::XrefState::Loaded;
    root.insertion=gcm::to_lattice(v,10,20);root.scale=2;root.rotation=.3;d.blocks.attach(root);
    gdoc::Xref child;child.name="Campus|Survey";child.path="survey/Survey.dwg";child.nested=true;
    child.type=gdoc::XrefType::Overlay;child.state=gdoc::XrefState::Unloaded;child.clip_enabled=false;
    child.clip={gcm::to_lattice(v,-1,-1),gcm::to_lattice(v,3,2)};d.blocks.attach(child);
    gdoc::BlockDef survey;survey.name=child.name;survey.is_xref=true;survey.xref_path=child.path;
    survey.geometry.push_back(segment(v,0,0,2,1));survey.notes.push_back({gcm::to_lattice(v,1,1),"Survey",.01});d.blocks.define(survey);
    gdoc::BlockRef nested;nested.name=child.name;nested.insertion=gcm::to_lattice(v,3,4);
    nested.scale_x=-2;nested.scale_y=.5;nested.rotation=-.4;
    gdoc::BlockDef campus;campus.name=root.name;campus.is_xref=true;campus.xref_path=root.path;
    campus.geometry.push_back(segment(v,0,0,4,0));campus.nested_refs.push_back(nested);d.blocks.define(campus);
    std::string error;auto bytes=write(d,v,metres(),&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());if(bytes.empty())return;
    auto back=read(bytes.data(),bytes.size(),v);CHECK_MSG(back.ok,"%s",back.error.c_str());
    CHECK(back.blocks.references().size()==1);CHECK(back.blocks.xrefs().size()==2);
    const auto* parent=back.blocks.find(root.name);const auto* leaf=back.blocks.find(child.name);const auto* x=back.blocks.xref(child.name);
    CHECK(parent!=nullptr);CHECK(leaf!=nullptr);CHECK(x!=nullptr);if(!parent||!leaf||!x)return;
    CHECK(parent->geometry.size()==1);CHECK(parent->nested_refs.size()==1);CHECK(leaf->geometry.size()==1);CHECK(leaf->notes.size()==1);
    CHECK(x->nested);CHECK(x->path==child.path);CHECK(x->type==child.type);CHECK(x->state==child.state);CHECK(!x->clip_enabled);CHECK(x->clip.size()==2);
    if(parent->nested_refs.size()==1) {
        const auto& ref=parent->nested_refs[0];CHECK(ref.name==child.name);
        CHECK(ref.insertion.x==nested.insertion.x);CHECK(ref.insertion.y==nested.insertion.y);
        CHECK(ref.scale_x==-2);CHECK(ref.scale_y==.5);CHECK(std::abs(ref.rotation+.4)<1e-12);
    }
    if(x->clip.size()==2)for(std::size_t i=0;i<2;++i){CHECK(x->clip[i].x==child.clip[i].x);CHECK(x->clip[i].y==child.clip[i].y);}
    gdxf::Document again;again.blocks=back.blocks;auto second=write(again,v,metres(),&error);
    CHECK_MSG(!second.empty(),"%s",error.c_str());auto retained=read(second.data(),second.size(),v);CHECK(retained.ok);
    CHECK(retained.blocks.references().size()==1);CHECK(retained.blocks.xref(child.name)&&retained.blocks.xref(child.name)->nested);
    auto orphan=d;auto no_child=*orphan.blocks.find(root.name);no_child.nested_refs.clear();orphan.blocks.define(no_child);
    CHECK(write(orphan,v,metres(),&error).empty());CHECK(error.find("parent insertion")!=std::string::npos);
}

TEST(dwg_paper_xrefs_retain_sheet_ownership_without_model_insertions) {
    const auto v=volume();
    for(auto unit:{gdxf::Units::Metres,gdxf::Units::Millimetres}) {
        gdxf::Document d;gdoc::Xref x;x.name="Survey";x.path="refs/Survey.dwg";x.insertion=gcm::to_lattice(v,100,200);
        x.clip={gcm::to_lattice(v,0,0),gcm::to_lattice(v,.5,.5)};d.blocks.attach(x);
        gdoc::BlockDef def;def.name=x.name;def.is_xref=true;def.xref_path=x.path;def.geometry.push_back(segment(v,0,0,1,1));d.blocks.define(def);
        gdxf::Layout sheet;sheet.name="Sheet";sheet.width=.3;sheet.height=.2;
        gdoc::BlockRef ref;ref.name=x.name;ref.insertion=gcm::to_lattice(v,.02,.04);ref.scale_x=.1;ref.scale_y=-.2;ref.rotation=.4;
        sheet.references.push_back(ref);d.layouts.push_back(sheet);d.active_layout=0;
        auto options=metres();options.units=unit;std::string error;auto bytes=write(d,v,options,&error);
        CHECK_MSG(!bytes.empty(),"%s",error.c_str());if(bytes.empty())continue;
        auto back=read(bytes.data(),bytes.size(),v);CHECK_MSG(back.ok,"%s",back.error.c_str());
        CHECK(back.blocks.references().empty());CHECK(back.blocks.xrefs().size()==1);CHECK(back.layouts.size()==1);if(back.layouts.size()!=1)continue;
        CHECK(back.layouts[0].references.size()==1);if(back.layouts[0].references.size()!=1)continue;
        const auto& placed=back.layouts[0].references[0];const auto* metadata=back.blocks.xref(placed.name);
        CHECK(metadata!=nullptr);if(!metadata)continue;
        CHECK(metadata->path==x.path);CHECK(!metadata->nested);CHECK(metadata->clip.size()==2);
        CHECK(placed.insertion.x==ref.insertion.x);CHECK(placed.insertion.y==ref.insertion.y);
        CHECK(metadata->insertion.x==placed.insertion.x);CHECK(metadata->insertion.y==placed.insertion.y);
        CHECK(std::abs(placed.scale_x-.1)<1e-12);CHECK(std::abs(placed.scale_y+.2)<1e-12);CHECK(std::abs(metadata->scale-.1)<1e-12);
        if(unit==gdxf::Units::Metres) {
            gdxf::Document again;again.blocks=back.blocks;again.layouts=back.layouts;
            auto second=write(again,v,options,&error);CHECK_MSG(!second.empty(),"%s",error.c_str());
            auto retained=read(second.data(),second.size(),v);CHECK(retained.ok);CHECK(retained.blocks.references().empty());
            CHECK(retained.layouts.size()==1&&retained.layouts[0].references.size()==1);
        }
    }
}

TEST(dwg_layouts_keep_paper_geometry_and_viewports_separate_from_model) {
    const auto v=volume();
    for(auto units:{gdxf::Units::Metres,gdxf::Units::Millimetres,gdxf::Units::Inches}) {
        gdxf::Document d; d.entities.push_back({segment(v,1,2,3,4),"0",""});
        gdoc::BlockDef block; block.name="SheetMarker"; block.geometry.push_back(segment(v,0,0,.01,.02)); d.blocks.define(block);
        for(int i=0;i<2;++i) {
            gdxf::Layout l; l.name=i?"Details":"Plan 東京"; l.width=.510+i*.010; l.height=.320; l.paper_name="Custom";
            l.entities.push_back({segment(v,.02,.03,.12,.03),"0",""});
            gdxf::Text t; t.pos=gcm::to_lattice(v,.04,.05); t.text=l.name; t.height=.003; l.texts.push_back(t);
            gdim::Dimension dim; dim.kind=gdim::Kind::Horizontal; dim.p1=gcm::to_lattice(v,.02,.03); dim.p2=gcm::to_lattice(v,.12,.03); dim.p3=gcm::to_lattice(v,.06,.04); l.dimensions.push_back(dim);
            l.splines.push_back(ggeo::circle_nurbs<gcm::F64>({gcm::F64(.10),gcm::F64(.15)},.01));
            gdxf::HatchRec h; h.solid=true; auto circle=segment(v,.1,.1,0,0); circle.kind=gsnap::ShapeKind::Circle; circle.radius=gcm::to_lattice(v,.01,0).x; h.boundary={circle}; l.hatches.push_back(h);
            gdoc::BlockRef ref; ref.name=block.name; ref.insertion=gcm::to_lattice(v,.02,.06); ref.scale_x=2; ref.scale_y=3; l.references.push_back(ref);
            gplot::Viewport vp; vp.lo={.02,.04}; vp.hi={.20,.18}; vp.centre_x=3; vp.centre_y=4; vp.scale=50; vp.rotation=.3; vp.locked=true;
            vp.frozen_layers={"0"}; vp.on=i==0;
            if(i==0) vp.clip_polygon={{.02,.04},{.20,.04},{.16,.18},{.02,.18}};
            l.viewports.push_back(vp); d.layouts.push_back(l);
        }
        d.active_layout=1; WriteOptions o; o.units=units;
        std::string error; const auto bytes=write(d,v,o,&error); CHECK_MSG(!bytes.empty(),"%s",error.c_str());
        const auto r=read(bytes.data(),bytes.size(),v); CHECK_MSG(r.ok,"%s",r.error.c_str());
        CHECK(r.entities.size()==1); CHECK(r.texts.empty()); CHECK(r.dimensions.empty()); CHECK(r.hatches.empty()); CHECK(r.splines.empty()); CHECK(r.blocks.references().empty());
        CHECK(r.layouts.size()==2); CHECK(r.active_layout==1);
        if(r.layouts.size()!=2) continue;
        for(std::size_t i=0;i<2;++i) {
            const auto& l=r.layouts[i]; const auto& source=d.layouts[i];
            CHECK(l.name==source.name); CHECK(std::abs(l.width-source.width)<1e-12);
            CHECK(l.entities.size()==1); CHECK(l.texts.size()==1); CHECK(l.dimensions.size()==1); CHECK(l.hatches.size()==1); CHECK(l.splines.size()==1); CHECK(l.references.size()==1);
            if(!l.entities.empty()) CHECK(std::abs(double(l.entities[0].shape.a.x-source.entities[0].shape.a.x))<2);
            if(!l.texts.empty()) CHECK(std::abs(l.texts[0].height-.003)<1e-12);
            if(!l.references.empty()) { CHECK(std::abs(l.references[0].scale_x-2)<1e-12); CHECK(std::abs(l.references[0].scale_y-3)<1e-12); }
            CHECK(l.viewports.size()==1);
            if(l.viewports.empty()) continue;
            const auto& vp=l.viewports[0]; const double unit=gdxf::metres_per(units);
            CHECK(std::abs(vp.centre_x*unit-3)<1e-9); CHECK(std::abs(vp.centre_y*unit-4)<1e-9);
            CHECK(std::abs(vp.scale*unit-50)<1e-9); CHECK(std::abs(vp.rotation-.3)<1e-12);
            CHECK(vp.locked); CHECK(vp.on==(i==0)); CHECK(vp.frozen_layers==source.viewports[0].frozen_layers);
            CHECK(vp.clip_polygon.size()==source.viewports[0].clip_polygon.size());
        }
    }
}

TEST(dwg_leader_text_and_named_styles_roundtrip) {
    const auto v=volume();auto d=one_line(v);
    gdim::Style style;style.name="Callouts";style.dimtxt=4;style.dimasz=3;style.dimscale=2;d.dim_styles.push_back(style);
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.style=style.name;leader.layer="0";
    leader.p1=gcm::to_lattice(v,1,2);leader.p2=gcm::to_lattice(v,2,3);leader.p3=gcm::to_lattice(v,3,3);leader.text_override="Check anchor";d.dimensions.push_back(leader);
    gtext::TextStyleDef textstyle;textstyle.name="Narrow";textstyle.width_factor=.7f;textstyle.oblique=12;d.text_styles.push_back(textstyle);
    gdxf::Text text;text.pos=gcm::to_lattice(v,4,5);text.text="Wrapped\\Pnote";text.width=.1;text.height=.004;text.style="Narrow";d.texts.push_back(text);
    auto bytes=write(d,v,metres());auto r=read(bytes.data(),bytes.size(),v);
    CHECK_MSG(r.ok,"%s",r.error.c_str());CHECK(r.dimensions.size()==1);CHECK(r.texts.size()==1);
    if(!r.dimensions.empty()){CHECK(r.dimensions[0].dim.kind==gdim::Kind::Leader);CHECK(r.dimensions[0].dim.text_override=="Check anchor");CHECK(r.dimensions[0].dim.style=="Callouts");}
    if(!r.texts.empty()){CHECK(r.texts[0].text==text.text);CHECK(r.texts[0].style=="Narrow");CHECK(std::abs(r.texts[0].width-.1)<1e-8);}
    auto st=std::find_if(r.text_styles.begin(),r.text_styles.end(),[](const auto& s){return s.name=="Narrow";});CHECK(st!=r.text_styles.end());
    if(st!=r.text_styles.end()){CHECK(std::abs(st->width_factor-.7)<1e-6);CHECK(std::abs(st->oblique-12)<1e-6);}
    auto ds=std::find_if(r.dim_styles.begin(),r.dim_styles.end(),[](const auto& s){return s.name=="Callouts";});CHECK(ds!=r.dim_styles.end());
    if(ds!=r.dim_styles.end()){CHECK(std::abs(ds->dimtxt-4)<1e-7);CHECK(std::abs(ds->dimasz-3)<1e-7);CHECK(ds->dimscale==2);}
}

TEST(dwg_common_appearance_survives_model_paper_and_block_geometry) {
    const auto v=volume();auto d=one_line(v);
    auto& attrs=d.entities.front().shape.attrs;
    attrs.color=gdoc::Color::aci(3);attrs.visible=false;
    attrs.lineweight=gdoc::Lineweight::W050;attrs.linetype="Continuous";attrs.linetype_scale=2.5;
    gdoc::Layer zero;zero.name="0";zero.color=gdoc::Color::aci(2);
    zero.lineweight=gdoc::Lineweight::W035;zero.plottable=false;d.layers.add(zero);
    gdoc::BlockDef block;block.name="Appearance";block.geometry.push_back(d.entities.front().shape);d.blocks.define(block);
    gdoc::BlockRef ref;ref.name=block.name;d.blocks.insert(ref);
    gdxf::Layout paper;paper.name="Proof";paper.width=.210;paper.height=.297;
    paper.entities.push_back(d.entities.front());d.layouts.push_back(paper);
    const auto bytes=write(d,v,metres());CHECK(!bytes.empty());if(bytes.empty())return;
    const auto back=read(bytes.data(),bytes.size(),v);CHECK_MSG(back.ok,"%s",back.error.c_str());
    const auto check=[&](const gsnap::Shape& shape) {
        CHECK(shape.attrs.color==attrs.color);CHECK(!shape.attrs.visible);
        CHECK(shape.attrs.lineweight==attrs.lineweight);CHECK(shape.attrs.linetype==attrs.linetype);
        CHECK(std::abs(shape.attrs.linetype_scale-2.5)<1e-12);
    };
    CHECK(back.entities.size()==1);if(!back.entities.empty())check(back.entities.front().shape);
    const auto* def=back.blocks.find("Appearance");CHECK(def);
    if(def){CHECK(def->geometry.size()==1);if(!def->geometry.empty())check(def->geometry.front());}
    CHECK(back.layouts.size()==1);
    if(!back.layouts.empty()){CHECK(back.layouts.front().entities.size()==1);if(!back.layouts.front().entities.empty())check(back.layouts.front().entities.front().shape);}
    const auto* layer=back.layers.find("0");CHECK(layer);
    if(layer){CHECK(layer->color==zero.color);CHECK(layer->lineweight==zero.lineweight);CHECK(!layer->plottable);}
}

TEST(dwg_insertion_appearance_roundtrips_and_declines_unrepresentable_transparency) {
    const auto v=volume();gdxf::Document d;gdoc::BlockDef block;block.name="Symbol";block.geometry.push_back(segment(v,0,0,1,0));d.blocks.define(block);
    gdoc::BlockRef ref;ref.name=block.name;ref.attrs.color=gdoc::Color::aci(3);ref.attrs.visible=false;ref.attrs.lineweight=gdoc::Lineweight::W050;d.blocks.insert(ref);
    const auto bytes=write(d,v,metres());CHECK(!bytes.empty());const auto back=read(bytes.data(),bytes.size(),v);
    CHECK(back.ok);CHECK(back.blocks.references().size()==1);
    if(!back.blocks.references().empty()) {
        const auto& attrs=back.blocks.references().front().attrs;
        CHECK(attrs.color==ref.attrs.color);CHECK(!attrs.visible);CHECK(attrs.lineweight==ref.attrs.lineweight);
    }
    d.blocks.reference_mut(0)->attrs.transparency=50;CHECK(write(d,v,metres()).empty());
    d.blocks.reference_mut(0)->attrs.transparency=-2;CHECK(write(d,v,metres()).empty());
}

TEST(dwg_leader_preserves_every_bend_and_disabled_arrow) {
    const auto v=volume();gdxf::Document d;gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.text_override="Routing";
    std::vector<gcm::Lattice2> points;for(auto p:std::vector<std::pair<double,double>>{{0,0},{1,2},{2,3},{3,2},{4,2}})points.push_back(gcm::to_lattice(v,p.first,p.second));
    leader.p1=points.front();leader.p2=points[1];leader.p3=points.back();leader.leader_paths.push_back({points,false});d.dimensions.push_back(leader);
    const auto bytes=write(d,v,metres());CHECK(!bytes.empty());const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);CHECK(back.dimensions.size()==1);
    if(back.dimensions.size()!=1)return;
    const auto paths=back.dimensions.front().dim.paths();CHECK(paths.size()==1);CHECK(paths.front().vertices.size()==points.size());CHECK(!paths.front().arrow);
    for(std::size_t i=0;i<std::min(points.size(),paths.front().vertices.size());++i) {
        CHECK(std::abs(paths.front().vertices[i].x-points[i].x)<=1);CHECK(std::abs(paths.front().vertices[i].y-points[i].y)<=1);
    }
}

TEST(leader_exchange_retains_rotated_text_attachment) {
    const auto v=volume();gdxf::Document d;
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;
    leader.p1=gcm::to_lattice(v,0,0);leader.p2=gcm::to_lattice(v,1,2);leader.p3=gcm::to_lattice(v,3,4);
    leader.text_override="Aligned note";leader.leader_text_absolute=true;
    leader.leader_text_halign=2;leader.leader_text_valign=3;leader.leader_text_height=.004;leader.leader_text_angle=.5;
    d.dimensions.push_back(leader);
    const auto bytes=write(d,v,metres());CHECK(!bytes.empty());const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);CHECK(back.dimensions.size()==1);if(back.dimensions.empty())return;const auto& got=back.dimensions.front().dim;
    CHECK(got.text_override==leader.text_override);CHECK(got.leader_text_absolute);
    CHECK(got.leader_text_halign==2);CHECK(got.leader_text_valign==3);
    CHECK(std::abs(got.leader_text_height-.004)<1e-8);CHECK(std::abs(got.leader_text_angle-.5)<1e-8);
    CHECK(std::abs(got.p3.x-leader.p3.x)<=1);CHECK(std::abs(got.p3.y-leader.p3.y)<=1);
}

TEST(leader_arrow_override_exports_without_changing_the_shared_style) {
    const auto v=volume();gdxf::Document d;gdim::Style style;style.name="Callouts";style.dimscale=2;style.dimasz=3;d.dim_styles.push_back(style);
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.style=style.name;
    leader.p1=gcm::to_lattice(v,0,0);leader.p2=gcm::to_lattice(v,1,2);leader.p3=gcm::to_lattice(v,3,2);
    leader.leader_arrow_size=.009;d.dimensions.push_back(leader);
    const auto bytes=write(d,v,metres());CHECK(!bytes.empty());const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);
    CHECK(d.dim_styles.size()==1);CHECK(d.dim_styles.front().dimasz==3);CHECK(d.dimensions.front().style=="Callouts");
    CHECK(back.dim_styles.size()==2);CHECK(back.dimensions.size()==1);if(back.dimensions.empty())return;
    const auto found=std::find_if(back.dim_styles.begin(),back.dim_styles.end(),[&](const auto& s){return s.name==back.dimensions.front().dim.style;});
    CHECK(found!=back.dim_styles.end());if(found!=back.dim_styles.end())CHECK(std::abs(found->scaled(found->dimasz)*.001-.009)<1e-10);
}

TEST(nested_block_exchange_preserves_definition_graph_and_placements) {
    const auto v=volume();gdxf::Document document;
    gdoc::BlockDef child;child.name="Child";child.base=gcm::to_lattice(v,1,1);
    gsnap::Shape line;line.a=child.base;line.b=gcm::to_lattice(v,2,1);child.geometry.push_back(line);gdoc::AttributeDef attr;attr.tag="ID";attr.default_value="Unset";attr.position=child.base;child.attributes.push_back(attr);document.blocks.define(child);
    gdoc::BlockDef parent;parent.name="Parent";gdoc::BlockRef inner;inner.name=child.name;
    inner.insertion=gcm::to_lattice(v,3,4);inner.scale_x=-2;inner.scale_y=3;inner.rotation=.5;inner.attrs.color=gdoc::Color::aci(2);inner.set_attribute("ID","B-12");
    parent.nested_refs.push_back(inner);document.blocks.define(parent);
    gdoc::BlockRef outer;outer.name=parent.name;outer.insertion=gcm::to_lattice(v,10,20);document.blocks.insert(outer);
    const auto bytes=write(document,v,metres());CHECK(!bytes.empty());const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);
    const auto* result=back.blocks.find("Parent");CHECK(result!=nullptr);if(!result)return;
    CHECK(result->geometry.empty());CHECK(result->nested_refs.size()==1);if(result->nested_refs.empty())return;
    const auto& got=result->nested_refs.front();CHECK(got.name=="Child");CHECK(got.scale_x==-2);CHECK(got.scale_y==3);CHECK(std::abs(got.rotation-.5)<1e-8);
    CHECK(std::abs(got.insertion.x-inner.insertion.x)<=1);CHECK(std::abs(got.insertion.y-inner.insertion.y)<=1);CHECK(got.attrs.color.index==2);
    CHECK(got.attribute("ID").value_or("")=="B-12");
    CHECK(back.blocks.references().size()==1);
    const auto expected=document.blocks.expand(outer,v),actual=back.blocks.expand(outer,v);CHECK(actual.size()==1);
    if(actual.size()==1){CHECK(std::abs(actual[0].b.x-expected[0].b.x)<=4);CHECK(std::abs(actual[0].b.y-expected[0].b.y)<=4);}
}

TEST(elliptical_arc_exchange_retains_its_parameter_interval) {
    const auto v=volume();gdxf::Document document;gsnap::Shape arc;arc.kind=gsnap::ShapeKind::Ellipse;
    arc.a=gcm::to_lattice(v,1,2);arc.b=gcm::to_lattice(v,5,2);arc.radius=std::llround(2*v.scale());
    arc.start_angle=.5;arc.end_angle=2.5;document.entities.push_back({arc,"0","ELLIPSE"});
    const auto bytes=write(document,v,metres());CHECK(!bytes.empty());const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);
    const auto& entities=back.entities;CHECK(entities.size()==1);if(entities.empty())return;
    CHECK(std::abs(entities[0].shape.start_angle-.5)<1e-8);CHECK(std::abs(entities[0].shape.end_angle-2.5)<1e-8);
}

TEST(dwg_block_notes_keep_ownership_formatting_style_and_appearance) {
    const auto v=volume(); gdxf::Document document;
    gdoc::Layer layer; layer.name="Labels"; document.layers.add(layer);
    gtext::TextStyleDef style; style.name="Narrow"; style.width_factor=.7; document.text_styles.push_back(style);
    gdoc::BlockDef block; block.name="Title"; block.base=gcm::to_lattice(v,1,2);
    gdoc::BlockDef::Note note;
    note.pos=gcm::to_lattice(v,2,3); note.text="Project\\PSecond line";
    note.height=.003; note.width=.08; note.angle=.4; note.layer="Labels";
    note.halign=2; note.valign=3; note.style="Narrow";
    note.attrs.color=gdoc::Color::aci(2); note.attrs.visible=false;
    note.attrs.linetype="ByBlock"; note.attrs.lineweight=gdoc::Lineweight::ByBlock;
    block.notes={note};
    note.text="Unwrapped\\PParagraph"; note.width=0; block.notes.push_back(note);
    document.blocks.define(block);
    gdxf::Text loose; loose.text="Outside"; loose.height=.01; loose.attrs.color=gdoc::Color::aci(3);
    document.texts.push_back(loose);
    std::string error; const auto bytes=write(document,v,metres(),&error); CHECK_MSG(!bytes.empty(),"%s",error.c_str());
    const auto back=read(bytes.data(),bytes.size(),v); CHECK_MSG(back.ok,"%s",back.error.c_str());
    CHECK(back.texts.size()==1); if(!back.texts.empty())CHECK(back.texts[0].attrs.color.index==3);
    const auto* result=back.blocks.find("Title"); CHECK(result!=nullptr); if(!result)return;
    CHECK(result->notes.size()==2); if(result->notes.size()!=2)return;
    for(std::size_t i=0;i<result->notes.size();++i) {
        const auto& got=result->notes[i]; const auto& source=block.notes[i];
        CHECK(got.text==source.text); CHECK(got.pos.x==source.pos.x); CHECK(got.pos.y==source.pos.y);
        CHECK(std::abs(got.height-.003)<1e-9); CHECK(std::abs(got.width-source.width)<1e-9); CHECK(std::abs(got.angle-.4)<1e-9);
        CHECK(got.layer=="Labels"); CHECK(got.halign==2); CHECK(got.valign==3); CHECK(got.style=="Narrow");
        CHECK(got.attrs.color.index==2); CHECK(!got.attrs.visible);
        CHECK(got.attrs.linetype=="ByBlock"); CHECK(got.attrs.lineweight==gdoc::Lineweight::ByBlock);
    }
    block.notes[0].attrs.transparency=40; document.blocks.define(block);
    CHECK(write(document,v,metres(),&error).empty()); CHECK(error.find("appearance")!=std::string::npos);
}

TEST(dwg_underlay_definitions_and_placements_survive_model_layout_and_block_exchange) {
    const auto v=volume();gdxf::Document d;
    gdoc::Underlay u;u.kind=gdoc::UnderlayKind::Pdf;u.path="survey.pdf";u.sheet="2";u.name="Survey";
    u.insertion=gcm::to_lattice(v,1,2);u.scale=2;u.y_factor=-1.5;u.rotation=.4;
    u.plottable=false;u.locked=true;u.fade=33;u.contrast=69;u.contrast_neutral=100;
    u.clip={gcm::to_lattice(v,.01,.02),gcm::to_lattice(v,.1,.02),gcm::to_lattice(v,.1,.2),gcm::to_lattice(v,.01,.2)};
    u.clip_enabled=false;u.monochrome=true;u.adjust_background=true;d.underlays={u};
    gdoc::BlockDef block;block.name="Images";block.underlays={u};d.blocks.define(block);
    gdoc::BlockRef ref;ref.name=block.name;d.blocks.insert(ref);
    gdxf::Layout layout;layout.name="Sheet";layout.underlays={u};d.layouts.push_back(layout);
    WriteOptions options;options.units=gdxf::Units::Millimetres;std::string error;
    const auto bytes=write(d,v,options,&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());if(bytes.empty())return;
    const auto result=read(bytes.data(),bytes.size(),v);CHECK(result.ok);CHECK(result.crc_failures==0);
    CHECK(result.underlays.size()==1);CHECK(result.layouts.size()==1);CHECK(result.blocks.find("Images")!=nullptr);
    if(result.underlays.empty()||result.layouts.empty()||!result.blocks.find("Images"))return;
    CHECK(result.layouts[0].underlays.size()==1);CHECK(result.blocks.find("Images")->underlays.size()==1);
    if(result.layouts[0].underlays.empty()||result.blocks.find("Images")->underlays.empty())return;
    for(const auto* got:{&result.underlays[0],&result.layouts[0].underlays[0],&result.blocks.find("Images")->underlays[0]}) {
        CHECK(got->path==u.path);CHECK(got->sheet==u.sheet);CHECK(got->kind==u.kind);
        CHECK(std::abs(got->y_factor+1.5)<1e-12);CHECK(got->rotation==u.rotation);
        CHECK(!got->clip_enabled);CHECK(got->clip.size()==4);CHECK(got->monochrome);CHECK(got->adjust_background);
        CHECK(!got->plottable);CHECK(got->locked);CHECK(got->fade==33);CHECK(got->contrast==69);CHECK(got->contrast_neutral==100);
        CHECK(std::abs(got->clip[0].x-u.clip[0].x)<=1);CHECK(std::abs(got->clip[0].y-u.clip[0].y)<=1);
    }
    CHECK(std::abs(result.underlays[0].scale-2000)<1e-8);CHECK(std::abs(result.layouts[0].underlays[0].scale-2)<1e-8);
    CHECK(std::count_if(result.classes.begin(),result.classes.end(),[](const auto& c){return c.second=="PDFDEFINITION";})==1);
    const auto def_type=std::find_if(result.classes.begin(),result.classes.end(),[](const auto& c){return c.second=="PDFDEFINITION";})->first;
    CHECK(std::count_if(result.raw.begin(),result.raw.end(),[&](const auto& o){return o.type==def_type;})==1);
}

TEST(dwg_inverted_underlays_preserve_the_visible_footprint) {
    const auto v=volume();gdxf::Document d;gdoc::Underlay u;
    u.kind=gdoc::UnderlayKind::Pdf;u.path="page.pdf";u.sheet="1";u.extent_hi=gcm::to_lattice(v,.3,.2);
    u.clip={gcm::to_lattice(v,.05,.05),gcm::to_lattice(v,.15,.05),gcm::to_lattice(v,.15,.15),gcm::to_lattice(v,.05,.15)};
    u.clip_inverted=true;d.underlays={u};WriteOptions options;options.units=gdxf::Units::Metres;
    std::string error;const auto bytes=write(d,v,options,&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());if(bytes.empty())return;
    const auto result=read(bytes.data(),bytes.size(),v);CHECK(result.ok);CHECK(result.underlays.size()==1);if(result.underlays.empty())return;
    const auto& back=result.underlays[0];CHECK(back.clip_hole.size()==4);
    for(const auto p:{gcm::to_lattice(v,.01,.01),gcm::to_lattice(v,.1,.1),gcm::to_lattice(v,.25,.1)})CHECK(back.contains(p)==u.contains(p));
}

TEST(dwg_dwf_and_dgn_reference_exchange_requires_known_source_coordinates) {
    const auto v=volume();
    for(const auto kind:{gdoc::UnderlayKind::Dwf,gdoc::UnderlayKind::Dgn}) {
        gdxf::Document d;gdoc::Underlay u;u.kind=kind;u.path=kind==gdoc::UnderlayKind::Dwf?"sheet.dwf":"model.dgn";u.sheet="Model";d.underlays={u};
        std::string error;CHECK(write(d,v,{},&error).empty());
        d.underlays[0].source_unit_known=true;d.underlays[0].source_unit=.001;
        const auto bytes=write(d,v,{},&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());if(bytes.empty())continue;
        const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);CHECK(back.underlays.size()==1);if(back.underlays.empty())continue;
        CHECK(back.underlays[0].kind==kind);CHECK(back.underlays[0].path==u.path);CHECK(back.underlays[0].sheet=="Model");CHECK(back.underlays[0].source_unit_known);CHECK(back.underlays[0].source_coordinates_pending);
    }
}

TEST(dwg_layer_transparency_preserves_all_percentages_and_layer_ownership) {
    const auto v=volume();
    auto d=one_line(v);
    d.layers.find("0")->transparency=35;
    for(int percent=0;percent<=100;++percent) {
        gdoc::Layer layer;layer.name="Alpha"+std::to_string(percent);
        layer.transparency=percent;layer.color=gdoc::Color::aci(3);
        layer.locked=percent%2;layer.plottable=percent%3;
        d.layers.add(layer);
    }
    gdoc::BlockDef block;block.name="FadedBlock";
    block.geometry.push_back(segment(v,0,0,1,0));block.geometry.back().layer="Alpha50";
    d.blocks.define(block);
    gdoc::BlockRef ref;ref.name=block.name;ref.layer="Alpha75";d.blocks.insert(ref);
    gdxf::Layout sheet;sheet.name="FadedSheet";sheet.entities.push_back(d.entities.front());
    sheet.entities.front().layer="Alpha100";d.layouts.push_back(sheet);
    for(int pass=0;pass<2;++pass) {
        std::string error;const auto bytes=write(d,v,metres(),&error);
        CHECK_MSG(!bytes.empty(),"%s",error.c_str());if(bytes.empty())return;
        const auto back=read(bytes.data(),bytes.size(),v);CHECK_MSG(back.ok,"%s",back.error.c_str());
        CHECK(back.layers.find("0")->transparency==35);
        for(int percent=0;percent<=100;++percent) {
            const auto* layer=back.layers.find("Alpha"+std::to_string(percent));CHECK(layer);
            if(layer) {
                CHECK(layer->transparency==percent);CHECK(layer->color==gdoc::Color::aci(3));
                CHECK(layer->locked==bool(percent%2));CHECK(layer->plottable==bool(percent%3));
            }
        }
        CHECK(std::count_if(back.raw.begin(),back.raw.end(),[](const auto& o){return o.type==67;})==2);
        CHECK(back.entities.size()==1 && back.entities.front().layer=="0");
        const auto* saved=back.blocks.find(block.name);CHECK(saved);
        if(saved)CHECK(saved->geometry.size()==1 && saved->geometry.front().layer=="Alpha50");
        CHECK(back.blocks.references().size()==1 && back.blocks.references().front().layer=="Alpha75");
        CHECK(back.layouts.size()==1 && back.layouts.front().entities.size()==1 &&
              back.layouts.front().entities.front().layer=="Alpha100");
        d.layers=back.layers;
    }
    for(int invalid : {-1,101}) {
        d.layers.find("0")->transparency=invalid;
        CHECK(write(d,v,metres()).empty());
    }
}

TEST(dwg_r2000_declines_explicit_opaque_entity_overrides) {
    const auto v=volume();
    for(int layer_alpha : {0,50}) {
        auto d=one_line(v);d.layers.find("0")->transparency=layer_alpha;
        d.entities.front().shape.attrs.transparency=0;
        std::string error;CHECK(write(d,v,metres(),&error).empty());
        CHECK(error.find("transparency")!=std::string::npos);
        d.entities.clear();
        gdoc::BlockDef block;block.name="Opaque";block.geometry.push_back(segment(v,0,0,1,0));
        block.geometry.front().attrs.transparency=0;d.blocks.define(block);
        CHECK(write(d,v,metres()).empty());
        block.geometry.front().attrs.transparency=-1;d.blocks.define(block);
        gdoc::BlockRef ref;ref.name=block.name;ref.attrs.transparency=0;d.blocks.insert(ref);
        CHECK(write(d,v,metres()).empty());
    }
}

TEST(dwg_vertical_text_style_remains_a_text_style) {
    const auto v=volume();auto d=one_line(v);
    gtext::TextStyleDef style;style.name="VerticalNotes";style.vertical=true;
    style.height=.0025f;style.width_factor=.75f;style.oblique=12;style.backwards=true;style.font="simplex.shx";
    d.text_styles.push_back(style);
    const auto bytes=write(d,v,metres());CHECK(!bytes.empty());
    const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);
    const auto found=std::find_if(back.text_styles.begin(),back.text_styles.end(),[&](const auto& s){return s.name==style.name;});
    CHECK(found!=back.text_styles.end());if(found==back.text_styles.end())return;
    CHECK(found->vertical);CHECK(found->font==style.font);CHECK(found->backwards);
    CHECK(found->height==style.height && found->width_factor==style.width_factor);
    CHECK(std::abs(found->oblique-style.oblique)<1e-5);
}

TEST(dwg_dimension_style_formats_and_suppression_preserve_formatted_measurements) {
    const auto v=volume();
    for(auto format:{gunit::Style::Decimal,gunit::Style::Scientific,gunit::Style::Engineering,
                     gunit::Style::Architectural,gunit::Style::Fractional})
        for(auto units:{gdxf::Units::Metres,gdxf::Units::Millimetres,gdxf::Units::Inches}) {
            gdxf::Document d;gdim::Style style;style.name="Measurement";style.format=format;
            style.unit=gunit::Length::Millimetre;style.dimlfac=1.25;style.dimdec=3;
            style.dimtxt=3;style.dimasz=4;style.text_pos=gdim::TextPos::Below;
            style.dimsd1=true;style.dimsd2=true;style.dimse1=true;style.dimse2=false;
            style.prefix="[";style.suffix="]";d.dim_styles.push_back(style);
            gdim::Dimension dim;dim.kind=gdim::Kind::Horizontal;dim.style=style.name;
            dim.p1=gcm::to_lattice(v,0,0);dim.p2=gcm::to_lattice(v,1.2345,0);dim.p3=gcm::to_lattice(v,0,.02);
            d.dimensions.push_back(dim);
            WriteOptions options;options.units=units;
            const auto bytes=write(d,v,options);CHECK(!bytes.empty());
            const auto back=read(bytes.data(),bytes.size(),v);CHECK(back.ok);
            const auto found=std::find_if(back.dim_styles.begin(),back.dim_styles.end(),[&](const auto& s){return s.name==style.name;});
            CHECK(found!=back.dim_styles.end());if(found==back.dim_styles.end())continue;
            CHECK(found->format==format && found->dimdec==3);
            CHECK(found->text_pos==gdim::TextPos::Below && found->dimsd1 && found->dimsd2);
            CHECK(found->dimse1 && !found->dimse2);CHECK(std::abs(found->dimtxt-3)<1e-9);
            // gdwg::read leaves entity points in file units; style sizes/factors are normalized.
            CHECK_MSG(gdim::format(dim,*found,v)==gdim::format(dim,style,v),"format %d, units %d: %s / %s",int(format),int(units),gdim::format(dim,*found,v).c_str(),gdim::format(dim,style,v).c_str());
        }
}

TEST(dwg_shared_dimension_style_keeps_model_and_layout_unit_contexts) {
    const auto v=volume();gdxf::Document d;gdim::Style style;
    style.name="Shared";style.dimtxt=2500;style.dimasz=3000;style.unit=gunit::Length::Metre;d.dim_styles.push_back(style);
    gdim::Dimension dim;dim.kind=gdim::Kind::Horizontal;dim.style=style.name;
    dim.p1=gcm::to_lattice(v,0,0);dim.p2=gcm::to_lattice(v,1,0);dim.p3=gcm::to_lattice(v,0,.02);d.dimensions.push_back(dim);
    for(const auto name:{"Millimetres","Inches"}){gdxf::Layout sheet;sheet.name=name;sheet.dimensions.push_back(dim);d.layouts.push_back(sheet);}
    auto bytes=write(d,v,metres());const auto original=read(bytes.data(),bytes.size(),v);CHECK(original.ok);
    u64 shared=0,paper=0;u16 layout_type=0;
    for(const auto& [type,name]:original.classes)if(name=="LAYOUT")layout_type=type;
    for(const auto& raw:original.raw)if(raw.type==69) {
        gdim::Style decoded;CHECK(read_dimension_style(bytes.data(),bytes.size(),raw,Version::R2000,decoded));
        if(decoded.name=="Shared")shared=raw.handle;
        if(decoded.name=="Shared_Paper")paper=raw.handle;
    }
    CHECK(shared && paper && layout_type);if(!shared || !paper || !layout_type)return;
    const auto replace=[&](const RawObject& raw,std::size_t start,const BitWriter& value) {
        for(std::size_t bit=0;bit<value.bit_position();++bit) {
            const auto at=raw.offset+(start+bit)/8;
            const u8 mask=u8(1u<<(7-(start+bit)%8));
            if(value.bytes()[bit/8] & (1u<<(7-bit%8)))bytes[at]|=mask;else bytes[at]&=u8(~mask);
        }
        const auto count=raw.body_at/8+raw.size;
        const auto crc=crc8(0xc0c1,bytes.data()+raw.offset,count);
        bytes[raw.offset+count]=u8(crc);bytes[raw.offset+count+1]=u8(crc>>8);
    };
    int references=0,inch_layouts=0;
    for(const auto& raw:original.raw) {
        if(raw.type==u16(ObjectType::DimLinear)) {
            BitReader handles(bytes.data()+raw.offset,raw.body_at/8+raw.size);handles.seek_bits(data_end_bits(raw));
            while(handles.ok() && handles.bit_position()+8<=handles.size_bits()) {
                const auto at=handles.bit_position();const auto h=handles.handle(raw.handle);
                if(handles.ok() && h.code<6 && h.value==paper) {
                    BitWriter replacement;replacement.handle({h.code,shared});
                    CHECK(replacement.bit_position()==handles.bit_position()-at);
                    replace(raw,at,replacement);++references;
                }
            }
        }
        if(raw.type==layout_type) {
            BitReader r(bytes.data()+raw.offset,raw.body_at/8+raw.size);r.seek_bits(raw.data_at);
            r.handle();CHECK(r.bs()==0);CHECK(r.bl()==0);r.tv();r.tv();r.bs();
            for(int i=0;i<6;++i)r.bd();r.tv();r.bd();r.bd();
            const auto units_at=r.bit_position();const auto units=r.bs();r.bs();r.bs();
            for(int i=0;i<4;++i)r.bd();r.tv();r.bd();r.bd();r.tv();r.bs();r.bd();r.bd();r.bd();
            const auto name=r.tv();
            if(name=="Inches") {
                CHECK(units==1);
                // Preserve field width while setting the valid BS byte form of zero.
                BitWriter replacement;replacement.bb(1);replacement.rc(0);
                replace(raw,units_at,replacement);++inch_layouts;
            }
        }
    }
    CHECK(references==2 && inch_layouts==1);
    const auto back=read(bytes.data(),bytes.size(),v);CHECK_MSG(back.ok,"%s",back.error.c_str());
    const auto find_style=[&](const std::string& name)->const gdim::Style* {
        for(const auto& s:back.dim_styles)if(s.name==name)return &s;return nullptr;
    };
    CHECK(back.dimensions.size()==1);if(back.dimensions.empty())return;
    const auto* model=find_style(back.dimensions.front().dim.style);CHECK(model);
    if(model){CHECK(model->name=="Shared");CHECK(model->dimtxt==2500 && model->dimasz==3000 && model->dimlfac==1);}
    CHECK(back.layouts.size()==2);
    for(const auto& sheet:back.layouts) {
        CHECK(sheet.dimensions.size()==1);if(sheet.dimensions.empty())continue;
        const auto* s=find_style(sheet.dimensions.front().style);CHECK(s);if(!s)continue;
        CHECK(s->name!="Shared" && s->name!="Shared_Paper");
        const double unit=sheet.name=="Inches"?.0254:.001;
        CHECK(std::abs(s->dimtxt-2.5*unit*1000)<1e-9);
        CHECK(std::abs(s->dimlfac-1/unit)<1e-9);
    }
}

TEST(dwg_write_dimension_associations_keep_anchors_and_shared_geometry_reactors) {
    const auto v=volume();using A=gdim::Dimension::Anchor;using K=gsnap::ShapeKind;
    for(auto units:{gdxf::Units::Metres,gdxf::Units::Millimetres,gdxf::Units::Inches})
        for(auto kind:{K::Segment,K::Arc,K::Ellipse,K::Circle,K::Point}) {
            gdxf::Document d;auto shape=segment(v,2,3,6,3);shape.kind=kind;shape.key=7;
            shape.radius=gcm::i64(std::llround(v.scale()));shape.start_angle=.3;shape.end_angle=2.1;
            if(kind==K::Point)shape.b=shape.a;
            auto target=segment(v,14,5,19,5);target.key=9;
            d.entities.push_back({shape,"0",""});d.entities.push_back({target,"0",""});
            const auto first=kind==K::Circle?A::Centre:A::Start;
            gdim::Dimension dim;dim.kind=gdim::Kind::Aligned;dim.p3=gcm::to_lattice(v,5,8);
            dim.o1={7,first};dim.o2={9,A::End};
            dim.p1=dim.p2={0,0};d.dimensions.push_back(dim);
            dim.o1={};dim.o2={7,kind==K::Circle?A::Centre:kind==K::Point?A::Start:A::End};
            dim.p1=gcm::to_lattice(v,18,8);d.dimensions.push_back(dim);
            WriteOptions options;options.units=units;std::string error;
            auto bytes=write(d,v,options,&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());
            if(bytes.empty())continue;
            auto r=read(bytes.data(),bytes.size(),v);CHECK(r.ok);CHECK(r.entities.size()==2);
            CHECK(r.dimensions.size()==2);CHECK(r.dimension_associations.size()==2);
            for(std::size_t i=0;i<r.classes.size();++i)CHECK(r.classes[i].first==500+i);
            if(r.dimensions.size()!=2 || r.dimension_associations.size()!=2)continue;
            CHECK(r.dimensions[0].dim.o1.bound());CHECK(r.dimensions[0].dim.o2.bound());
            CHECK(r.dimensions[0].dim.o1.at==first);
            CHECK(!r.dimensions[1].dim.o1.bound());CHECK(r.dimensions[1].dim.o2.bound());
            CHECK(r.dimensions[0].dim.o1.shape==r.dimensions[1].dim.o2.shape);
            CHECK(r.dimensions[0].dim.o1.shape!=r.dimensions[0].dim.o2.shape);
            CHECK(r.dimension_associations[0].flags==3);CHECK(r.dimension_associations[1].flags==2);
            const auto& p=r.dimension_associations[0].points.front();
            CHECK(p.osnap==(kind==K::Point?4:kind==K::Circle?3:13));
            CHECK(p.parameter==((kind==K::Arc||kind==K::Ellipse)?.3:0));
            CHECK(p.objects.size()==1);CHECK(p.objects.front()==r.entities[0].handle);
            CHECK(r.skipped.empty());
            gcm::Lattice2 expected;CHECK(gdim::Dimension::anchor_point(shape,first,expected));
            double x,y;gcm::to_world(v,expected,x,y);
            const double factor=1/gdxf::metres_per(units);
            CHECK(std::abs(p.x-x*factor)<1e-8);CHECK(std::abs(p.y-y*factor)<1e-8);
            CHECK(r.entities[0].layer=="0");CHECK(r.entities[1].layer=="0");
            CHECK(d.dimensions[0].p1.x==0);CHECK(d.dimensions[0].p1.y==0);
        }
}

TEST(dwg_write_dimension_associations_resolve_keys_in_model_and_each_layout) {
    const auto v=volume();gdxf::Document d;
    for(int space=-1;space<2;++space) {
        auto shape=segment(v,space<0?2:.02,space<0?3:.03,space<0?6:.06,space<0?3:.03);shape.key=1;
        if(space==1){shape.a.x*=2;shape.b.x*=2;}
        gdim::Dimension dim;dim.kind=gdim::Kind::Horizontal;dim.o1={1,gdim::Dimension::Anchor::Start};dim.o2={1,gdim::Dimension::Anchor::End};
        dim.p1=shape.a;dim.p2=shape.b;dim.p3=shape.a;dim.p3.y+=gcm::i64(std::llround(v.scale()*(space<0?1:.01)));
        if(space<0){d.entities.push_back({shape,"0",""});d.dimensions.push_back(dim);}
        else {gdxf::Layout sheet;sheet.name="Sheet "+std::to_string(space);sheet.width=.297;sheet.height=.210;sheet.entities.push_back({shape,"0",""});sheet.dimensions.push_back(dim);d.layouts.push_back(sheet);}
    }
    std::string error;auto bytes=write(d,v,metres(),&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());
    auto r=read(bytes.data(),bytes.size(),v);CHECK(r.ok);CHECK(r.dimension_associations.size()==3);
    CHECK(r.dimensions.size()==1);CHECK(r.layouts.size()==2);
    if(r.dimensions.size()!=1 || r.layouts.size()!=2)return;
    CHECK(r.dimensions[0].dim.o1.shape==r.entities[0].shape.key);
    for(std::size_t i=0;i<2;++i) {
        const auto& sheet=r.layouts[i];CHECK(sheet.dimensions.size()==1);CHECK(sheet.entities.size()==1);
        if(sheet.dimensions.empty()||sheet.entities.empty())continue;
        CHECK(sheet.dimensions[0].o1.shape==sheet.entities[0].shape.key);
        CHECK(sheet.dimensions[0].o1.shape!=r.dimensions[0].dim.o1.shape);
        double x,y;gcm::to_world(v,sheet.dimensions[0].p1,x,y);CHECK(std::abs(x-.02*(i+1))<1e-6);
    }
}

TEST(dwg_write_dimension_associations_report_missing_ambiguous_and_unsupported_targets) {
    const auto v=volume();auto d=one_line(v);d.entities[0].shape.key=12;
    gdim::Dimension dim;dim.kind=gdim::Kind::Aligned;dim.o1={12,gdim::Dimension::Anchor::Start};
    dim.p1=d.entities[0].shape.a;dim.p2=d.entities[0].shape.b;dim.p3=gcm::to_lattice(v,2,8);d.dimensions.push_back(dim);
    std::string error;CHECK(!write(d,v,metres(),&error).empty());
    d.dimensions[0].o1.shape=13;CHECK(write(d,v,metres(),&error).empty());CHECK(error.find("missing geometry")!=std::string::npos);
    d.dimensions[0].o1.shape=12;d.entities.push_back(d.entities[0]);CHECK(write(d,v,metres(),&error).empty());CHECK(error.find("duplicate geometry")!=std::string::npos);
    d.entities.pop_back();d.dimensions[0].o1.at=gdim::Dimension::Anchor::Centre;CHECK(write(d,v,metres(),&error).empty());CHECK(error.find("anchor")!=std::string::npos);
    d.dimensions[0].o1.at=gdim::Dimension::Anchor::Start;d.dimensions[0].kind=gdim::Kind::Angular;
    CHECK(write(d,v,metres(),&error).empty());CHECK(error.find("unsupported DWG geometry associations")!=std::string::npos);
    d.dimensions[0].kind=gdim::Kind::Aligned;
    gdxf::Layout sheet;sheet.name="Paper";sheet.width=.297;sheet.height=.210;sheet.dimensions=d.dimensions;d.layouts.push_back(sheet);d.dimensions.clear();
    CHECK(write(d,v,metres(),&error).empty());CHECK(error.find("missing geometry in its drawing space")!=std::string::npos);
}

TEST(dwg_custom_class_slots_are_contiguous_with_only_dwf_or_dgn_underlays) {
    for(auto kind:{gdoc::UnderlayKind::Dwf,gdoc::UnderlayKind::Dgn}) {
        gdxf::Document d;gdoc::Underlay u;u.kind=kind;u.path=kind==gdoc::UnderlayKind::Dwf?"sheet.dwfx":"sheet.dgn";u.sheet="1";u.source_unit_known=true;u.source_unit=.001;d.underlays.push_back(u);
        std::string error;const auto bytes=write(d,volume(),metres(),&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());
        const auto r=read(bytes.data(),bytes.size(),volume());CHECK(r.ok);CHECK(r.underlays.size()==1);
        for(std::size_t i=0;i<r.classes.size();++i)CHECK(r.classes[i].first==500+i);
        if(!r.underlays.empty())CHECK(r.underlays.front().kind==kind);
    }
}

TEST(dwg_write_edge_pair_dimension_preserves_static_measurement) {
    const auto v=volume();gdxf::Document d;
    auto a=segment(v,0,0,0,6),b=segment(v,4,0,4,6);a.key=1;b.key=2;
    d.entities.push_back({a,"0",""});d.entities.push_back({b,"0",""});
    gdim::Dimension dim;dim.kind=gdim::Kind::Aligned;
    dim.p1=gcm::to_lattice(v,0,3);dim.p2=gcm::to_lattice(v,4,3);dim.p3=gcm::to_lattice(v,2,5);
    dim.o1={1,gdim::Dimension::Anchor::Edge};dim.o2={2,gdim::Dimension::Anchor::Edge};
    d.dimensions.push_back(dim);std::string error;
    const auto bytes=write(d,v,metres(),&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());
    if(bytes.empty())return;
    const auto r=read(bytes.data(),bytes.size(),v);CHECK(r.ok);CHECK(r.dimensions.size()==1);
    CHECK(r.dimension_associations.empty());CHECK(d.dimensions[0].associative());
    if(r.dimensions.size()==1) {
        CHECK(!r.dimensions[0].dim.associative());
        CHECK(std::abs(gdim::measure(r.dimensions[0].dim,v)-4)<1e-6);
    }
}

TEST(dwg_shifted_dimension_extensions_export_static_without_changing_measurement) {
    const auto v=volume();
    for(auto kind:{gdim::Kind::Horizontal,gdim::Kind::Vertical,gdim::Kind::Aligned,gdim::Kind::Linear}) {
        gdxf::Document d;gdim::Dimension dim;dim.kind=kind;
        const auto p=[&](double x,double y){return gcm::to_lattice(v,x,y);};
        dim.p1=p(0,0);dim.p2=p(4,0);dim.p3=p(2,1);
        double sx=0,sy=.5;
        if(kind==gdim::Kind::Vertical){dim.p2=p(0,4);dim.p3=p(1,2);sx=.5;sy=0;}
        if(kind==gdim::Kind::Aligned){dim.p2=p(4,3);dim.p3=p(1.4,2.3);sx=-.3;sy=.4;}
        if(kind==gdim::Kind::Linear){dim.p2=p(4,1);dim.p3=p(5,3);sx=0;sy=3;}
        dim.extension_offset={gcm::i64(std::llround(sx/v.inv_scale())),gcm::i64(std::llround(sy/v.inv_scale()))};
        dim.o1={1,gdim::Dimension::Anchor::Start};dim.o2={1,gdim::Dimension::Anchor::End};
        d.dimensions.push_back(dim);
        const auto expected=gdim::build(dim,gdim::Style{},v);CHECK(expected.ok);
        std::string error;const auto bytes=write(d,v,metres(),&error);CHECK_MSG(!bytes.empty(),"%s",error.c_str());
        if(bytes.empty())continue;
        const auto decoded=read(bytes.data(),bytes.size(),v);CHECK(decoded.ok);CHECK(decoded.dimensions.size()==1);
        if(decoded.dimensions.empty())continue;
        const auto& got=decoded.dimensions.front().dim;CHECK(!got.associative());CHECK(decoded.dimension_associations.empty());
        CHECK(got.extension_offset.x==0);CHECK(got.extension_offset.y==0);
        CHECK(std::abs(gdim::measure(got,v)-gdim::measure(dim,v))<1e-6);
        const auto actual=gdim::build(got,gdim::Style{},v);CHECK(actual.ok);CHECK(actual.lines.size()==expected.lines.size());
        if(actual.lines.size()==expected.lines.size())for(std::size_t i=0;i<actual.lines.size();++i) {
            CHECK(std::abs(double(actual.lines[i].a.x)-expected.lines[i].a.x)<=2);
            CHECK(std::abs(double(actual.lines[i].a.y)-expected.lines[i].a.y)<=2);
            CHECK(std::abs(double(actual.lines[i].b.x)-expected.lines[i].b.x)<=2);
            CHECK(std::abs(double(actual.lines[i].b.y)-expected.lines[i].b.y)<=2);
        }
        CHECK(d.dimensions[0].o1.shape==1);CHECK(d.dimensions[0].p1.x==dim.p1.x);
        CHECK(d.dimensions[0].extension_offset.y==dim.extension_offset.y);
    }
}
