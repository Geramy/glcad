#include "test.hpp"

#include <cmath>

#include "engine/block.hpp"
#include "engine/layer.hpp"

using namespace gdoc;
constexpr f64 kPi = 3.14159265358979323846;

static gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
static Point2 pt(const gcm::WorkingVolume& v, f64 x, f64 y) {
    return gcm::to_lattice(v, x, y);
}
static bool at(const gcm::WorkingVolume& v, Point2 p, f64 x, f64 y, f64 tol = 1e-3) {
    f64 px, py;
    gcm::to_world(v, p, px, py);
    return std::fabs(px - x) < tol && std::fabs(py - y) < tol;
}
static gsnap::Shape seg(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = pt(v, x0, y0);
    s.b = pt(v, x1, y1);
    return s;
}

static BlockDef unit_square(const gcm::WorkingVolume& v, const char* name) {
    BlockDef d;
    d.name = name;
    d.base = pt(v, 0, 0);
    d.geometry = {seg(v, 0, 0, 1, 0), seg(v, 1, 0, 1, 1),
                  seg(v, 1, 1, 0, 1), seg(v, 0, 1, 0, 0)};
    return d;
}

TEST(block_define_and_find) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "SQUARE"));
    CHECK(t.size() == 1);
    CHECK(t.find("SQUARE") != nullptr);
    CHECK(t.find("SQUARE")->geometry.size() == 4);
    CHECK(t.find("MISSING") == nullptr);
}

TEST(block_cannot_be_removed_while_referenced) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "SQUARE"));

    BlockRef r;
    r.name = "SQUARE";
    r.insertion = pt(v, 5, 5);
    t.insert(r);

    CHECK_MSG(!t.remove("SQUARE"), "a referenced block must not be removable");
    CHECK(t.size() == 1);
}

TEST(block_rename_updates_every_reference) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "OLD"));

    BlockRef r;
    r.name = "OLD";
    t.insert(r);
    t.insert(r);

    CHECK(t.rename("OLD", "NEW"));
    CHECK(t.find("NEW") != nullptr);
    CHECK(t.find("OLD") == nullptr);
    CHECK_MSG(t.references_to("NEW").size() == 2,
              "insertions did not follow the rename");
    CHECK(t.references_to("OLD").empty());

    t.define(unit_square(v, "TAKEN"));
    CHECK(!t.rename("NEW", "TAKEN"));
}

TEST(block_insertion_places_geometry) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "SQUARE"));

    BlockRef r;
    r.name = "SQUARE";
    r.insertion = pt(v, 10, 20);
    const auto id = t.insert(r);

    const auto g = t.expand(*t.reference(id), v);
    CHECK(g.size() == 4);
    CHECK_MSG(at(v, g[0].a, 10, 20), "insertion point not honoured");
    CHECK(at(v, g[0].b, 11, 20));
}

TEST(block_insertion_scales_and_rotates) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "SQUARE"));

    BlockRef r;
    r.name = "SQUARE";
    r.insertion = pt(v, 0, 0);
    r.scale_x = r.scale_y = 3.0;
    const auto id = t.insert(r);

    auto g = t.expand(*t.reference(id), v);
    CHECK(at(v, g[0].b, 3, 0));

    t.reference(id)->rotation = kPi * 0.5;
    g = t.expand(*t.reference(id), v);
    CHECK_MSG(at(v, g[0].b, 0, 3, 1e-2), "rotation not applied");
}

TEST(block_non_uniform_scale_is_detectable) {
    BlockRef r;
    r.scale_x = 2.0;
    r.scale_y = 2.0;
    CHECK(r.uniform_scale());
    r.scale_y = 3.0;
    CHECK(!r.uniform_scale());
}

TEST(block_base_point_is_the_insertion_origin) {
    const auto v = vol();
    BlockTable t;
    BlockDef d = unit_square(v, "OFFSET");
    d.base = pt(v, 1, 1);
    t.define(d);

    BlockRef r;
    r.name = "OFFSET";
    r.insertion = pt(v, 0, 0);
    const auto id = t.insert(r);

    const auto g = t.expand(*t.reference(id), v);
    CHECK_MSG(at(v, g[0].a, -1, -1), "base point ignored");
}

TEST(block_nested_definitions_expand) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "INNER"));

    BlockDef outer;
    outer.name = "OUTER";
    outer.base = pt(v, 0, 0);
    outer.geometry = {seg(v, 0, 0, 5, 0)};
    outer.nested = {"INNER"};
    t.define(outer);

    BlockRef r;
    r.name = "OUTER";
    const auto id = t.insert(r);
    const auto g = t.expand(*t.reference(id), v);
    CHECK_MSG(g.size() == 5, "expected 1 + 4 shapes, got %zu", g.size());
}

TEST(block_recursion_is_detected) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "A"));

    BlockDef b = unit_square(v, "B");
    b.nested = {"A"};
    t.define(b);

    CHECK_MSG(t.would_recurse("A", {"B"}),
              "A containing B, which contains A, is a cycle");
    CHECK_MSG(t.would_recurse("A", {"A"}), "direct self-reference is a cycle");
    CHECK(!t.would_recurse("C", {"A", "B"}));
}

TEST(block_expansion_is_depth_limited) {
    const auto v = vol();
    BlockTable t;
    BlockDef a = unit_square(v, "A");
    a.nested = {"A"};
    t.define(a);

    BlockRef r;
    r.name = "A";
    const auto id = t.insert(r);
    const auto g = t.expand(*t.reference(id), v);
    CHECK_MSG(g.size() < 1000, "expansion did not terminate (%zu shapes)", g.size());
}

TEST(block_attributes_default_on_insertion) {
    const auto v = vol();
    BlockTable t;
    BlockDef d = unit_square(v, "DOOR");
    d.attributes = {{"DOOR_NO", "Door number", "D-01"},
                    {"WIDTH", "Leaf width", "900"}};
    t.define(d);

    BlockRef r;
    r.name = "DOOR";
    const auto id = t.insert(r);

    CHECK(t.reference(id)->attributes.size() == 2);
    CHECK(*t.reference(id)->attribute("DOOR_NO") == "D-01");
    CHECK(*t.reference(id)->attribute("WIDTH") == "900");
    CHECK(!t.reference(id)->attribute("NOSUCH").has_value());
}

TEST(block_attribute_values_are_per_insertion) {
    const auto v = vol();
    BlockTable t;
    BlockDef d = unit_square(v, "DOOR");
    d.attributes = {{"DOOR_NO", "Door number", "D-01"}};
    t.define(d);

    BlockRef r;
    r.name = "DOOR";
    const auto a = t.insert(r);
    const auto b = t.insert(r);

    t.reference(a)->set_attribute("DOOR_NO", "D-07");
    CHECK(*t.reference(a)->attribute("DOOR_NO") == "D-07");
    CHECK_MSG(*t.reference(b)->attribute("DOOR_NO") == "D-01",
              "attribute values leaked between insertions");
}

TEST(block_attribute_flags) {
    AttributeDef a;
    CHECK(a.prompts_user());

    a.constant = true;
    CHECK(!a.prompts_user());

    a.constant = false;
    a.preset = true;
    CHECK_MSG(!a.prompts_user(), "a preset attribute takes its default silently");
}

TEST(block_explode_returns_world_geometry) {
    const auto v = vol();
    BlockTable t;
    t.define(unit_square(v, "SQUARE"));

    BlockRef r;
    r.name = "SQUARE";
    r.insertion = pt(v, 4, 4);
    const auto id = t.insert(r);

    const auto g = t.explode(id, v);
    CHECK(g.has_value());
    CHECK(g->size() == 4);
    CHECK(at(v, (*g)[0].a, 4, 4));
}

TEST(block_explode_can_be_refused) {
    const auto v = vol();
    BlockTable t;
    BlockDef d = unit_square(v, "TITLEBLOCK");
    d.allow_exploding = false;
    t.define(d);

    BlockRef r;
    r.name = "TITLEBLOCK";
    const auto id = t.insert(r);
    CHECK_MSG(!t.explode(id, v).has_value(),
              "a non-explodable block was exploded");
}

TEST(xref_attach_and_state) {
    BlockTable t;
    Xref x;
    x.name = "SITE";
    x.path = "/proj/site.dwg";
    x.type = XrefType::Attach;
    t.attach(x);

    CHECK(t.xrefs().size() == 1);
    CHECK(t.xref("SITE")->state == XrefState::Unresolved);
    CHECK(!t.xref("SITE")->visible());

    t.reload("SITE", true);
    CHECK(t.xref("SITE")->state == XrefState::Loaded);
    CHECK(t.xref("SITE")->visible());
    CHECK(t.xref("SITE")->resolved_path == "/proj/site.dwg");

    t.unload("SITE");
    CHECK(t.xref("SITE")->state == XrefState::Unloaded);
    CHECK(!t.xref("SITE")->visible());

    t.reload("SITE", false);
    CHECK(t.xref("SITE")->state == XrefState::NotFound);
}

TEST(xref_overlay_versus_attach) {
    BlockTable t;
    Xref a; a.name = "A"; a.type = XrefType::Attach;
    Xref b; b.name = "B"; b.type = XrefType::Overlay;
    t.attach(a);
    t.attach(b);
    CHECK(t.xref("A")->type == XrefType::Attach);
    CHECK(t.xref("B")->type == XrefType::Overlay);
}

TEST(xref_path_repair_fixes_every_instance) {
    BlockTable t;
    for (const char* n : {"A", "B", "C"}) {
        Xref x;
        x.name = n;
        x.path = std::string("/old/server/") + n + ".dwg";
        x.state = XrefState::NotFound;
        t.attach(x);
    }
    Xref other;
    other.name = "D";
    other.path = "/elsewhere/D.dwg";
    t.attach(other);

    const std::size_t fixed = t.repair_paths("/old/server/", "/new/share/");
    CHECK_MSG(fixed == 3, "expected 3 repairs, got %zu", fixed);
    CHECK(t.xref("A")->path == "/new/share/A.dwg");
    CHECK_MSG(t.xref("D")->path == "/elsewhere/D.dwg",
              "an unrelated path was rewritten");
}

TEST(xref_detach) {
    BlockTable t;
    Xref x;
    x.name = "SITE";
    t.attach(x);
    CHECK(t.detach("SITE"));
    CHECK(t.xref("SITE") == nullptr);
    CHECK(!t.detach("SITE"));
}

TEST(xref_clip_boundary) {
    const auto v = vol();
    BlockTable t;
    Xref x;
    x.name = "SITE";
    x.clip = {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)};
    t.attach(x);

    CHECK(t.xref("SITE")->clip.size() == 4);
    CHECK(t.xref("SITE")->clip_enabled);
    CHECK(!t.xref("SITE")->clip_inverted);
}

TEST(block_expansion_keeps_nested_region_identities_separate) {
    gdoc::BlockTable table;
    gsnap::Shape circle;circle.kind=gsnap::ShapeKind::Circle;circle.radius=1000;
    circle.region=true;circle.poly=1;
    gdoc::BlockDef inner;inner.name="Inner";inner.geometry={circle};table.define(inner);
    auto outer=inner;outer.name="Outer";outer.geometry[0].a.x=10000;outer.nested={"Inner","Inner"};table.define(outer);
    gdoc::BlockRef ref;ref.name="Outer";
    const auto geometry=table.expand(ref,gcm::WorkingVolume{});
    CHECK(geometry.size()==3);
    if(geometry.size()==3) {
        CHECK(geometry[0].region&&geometry[1].region&&geometry[2].region);
        CHECK(geometry[0].poly!=geometry[1].poly);
        CHECK(geometry[1].poly!=geometry[2].poly);
        CHECK(geometry[0].poly!=geometry[2].poly);
    }
}

TEST(block_appearance_uses_insertion_layer_for_byblock_without_changing_member_layer) {
    const auto v=vol();BlockTable blocks;auto block=unit_square(v,"Symbol");
    for(auto& shape:block.geometry){shape.layer="Member";shape.attrs.transparency=-2;shape.attrs.color=Color::by_block();}
    block.geometry.back().attrs.transparency=0;blocks.define(block);
    LayerTable layers;Layer insertion;insertion.name="Insert";insertion.transparency=60;insertion.color=Color::aci(3);layers.add(insertion);
    Layer member;member.name="Member";member.transparency=20;layers.add(member);
    BlockRef ref;ref.name="Symbol";ref.layer="Insert";
    const auto inherited=blocks.expand(ref,v,8,&layers);CHECK(inherited.size()==4);
    if(inherited.size()!=4)return;
    CHECK(inherited[0].layer=="Member");CHECK(inherited[0].attrs.transparency==60);CHECK(inherited[0].attrs.color==insertion.color);
    CHECK(inherited[3].attrs.transparency==0);
    ref.attrs.transparency=35;ref.attrs.visible=false;
    const auto explicit_value=blocks.expand(ref,v,8,&layers);
    CHECK(explicit_value[0].attrs.transparency==35);CHECK(!explicit_value[0].attrs.visible);
}

TEST(nested_insertions_keep_child_base_transform_and_inherited_appearance) {
    const auto v=vol();BlockTable table;
    BlockDef child;child.name="Child";child.base=pt(v,1,1);child.geometry={seg(v,1,1,2,1)};
    child.geometry.front().attrs.color=Color::by_block();child.geometry.front().attrs.transparency=-2;table.define(child);
    BlockDef parent;parent.name="Parent";parent.base=pt(v,1,2);
    BlockRef inner;inner.name=child.name;inner.insertion=pt(v,3,4);inner.scale_x=2;inner.rotation=std::acos(-1.)/2;
    inner.attrs.color=Color::by_block();inner.attrs.transparency=-2;parent.nested_refs.push_back(inner);table.define(parent);
    BlockRef outer;outer.name=parent.name;outer.insertion=pt(v,10,20);outer.scale_x=-2;outer.scale_y=3;
    outer.attrs.color=Color::aci(3);outer.attrs.transparency=40;
    const auto geometry=table.expand(outer,v);CHECK(geometry.size()==1);if(geometry.empty())return;
    CHECK(at(v,geometry[0].a,6,26));CHECK(at(v,geometry[0].b,6,32));
    CHECK(geometry[0].attrs.color.index==3);CHECK(geometry[0].attrs.transparency==40);
    CHECK(table.would_recurse("Child",{"Parent"}));CHECK(!table.remove("Child"));
    CHECK(table.rename("Child","Renamed"));CHECK(table.find("Parent")->nested_refs[0].name=="Renamed");
    CHECK(table.find("Parent")->geometry.empty());
    LayerTable layers;Layer root;root.name="Root";root.color=Color::aci(5);layers.add(root);
    Layer member;member.name="Member";member.color=Color::aci(1);layers.add(member);
    auto updated=*table.find("Parent");updated.nested_refs[0].layer="Member";table.define(updated);
    outer.layer="Root";outer.attrs.color=Color::by_block();
    const auto inherited=table.expand(outer,v,8,&layers);CHECK(inherited.size()==1);
    if(!inherited.empty())CHECK(inherited[0].attrs.color.index==5);
}

TEST(nonuniform_nested_curves_preserve_the_transformed_conic_and_arc_ends) {
    const auto v=vol();gsnap::Shape curve;curve.kind=gsnap::ShapeKind::Arc;curve.a=pt(v,1,2);
    curve.radius=std::llround(v.scale()*2);curve.start_angle=.2;curve.end_angle=2.;
    BlockRef first;first.scale_x=2;first.scale_y=1;first.rotation=.6;
    BlockRef second;second.scale_x=-1;second.scale_y=3;second.rotation=-.4;second.insertion=pt(v,2,5);
    const auto result=place_in_block(place_in_block(curve,first,{},v),second,{},v);
    CHECK(result.kind==gsnap::ShapeKind::Ellipse);CHECK(result.ellipse_sweep()<2*std::acos(-1.));
    double cx,cy,bx,by;gcm::to_world(v,result.a,cx,cy);gcm::to_world(v,result.b,bx,by);
    const double major=std::hypot(bx-cx,by-cy),minor=double(result.radius)/v.scale();
    const double ux=(bx-cx)/major,uy=(by-cy)/major;
    const auto mapped=[&](double t){gsnap::Shape point;point.a=pt(v,1+2*std::cos(t),2+2*std::sin(t));return place_in_block(place_in_block(point,first,{},v),second,{},v).a;};
    for(int i=0;i<=20;++i) {
        double x,y;gcm::to_world(v,mapped(.2+1.8*i/20),x,y);
        const auto a=((x-cx)*ux+(y-cy)*uy)/major,b=(-(x-cx)*uy+(y-cy)*ux)/minor;
        CHECK(std::abs(a*a+b*b-1)<1e-5);
    }
    const auto endpoint=[&](double t){return pt(v,cx+major*ux*std::cos(t)-minor*uy*std::sin(t),cy+major*uy*std::cos(t)+minor*ux*std::sin(t));};
    const auto start=endpoint(result.start_angle),end=endpoint(result.end_angle);
    CHECK(std::abs(start.x-mapped(curve.end_angle).x)<=5);CHECK(std::abs(start.y-mapped(curve.end_angle).y)<=5);
    CHECK(std::abs(end.x-mapped(curve.start_angle).x)<=5);CHECK(std::abs(end.y-mapped(curve.start_angle).y)<=5);
}

TEST(xref_clipping_uses_insertion_coordinates_and_applies_to_nested_geometry) {
    const auto v=vol();BlockTable table;BlockDef child;child.name="Child";child.geometry={seg(v,-2,0,2,0)};table.define(child);
    BlockDef parent;parent.name="Reference";parent.is_xref=true;parent.nested={child.name};table.define(parent);
    Xref x;x.name=parent.name;x.state=XrefState::Loaded;x.clip={pt(v,0,-1),pt(v,1,1)};table.attach(x);
    BlockRef ref;ref.name=x.name;ref.scale_x=2;ref.rotation=std::acos(-1.)/2;ref.insertion=pt(v,10,20);
    const auto clipped=table.expand(ref,v);CHECK(clipped.size()==1);if(!clipped.empty()){CHECK(at(v,clipped[0].a,10,20));CHECK(at(v,clipped[0].b,10,22));}
    table.xref(x.name)->clip_inverted=true;const auto outside=table.expand(ref,v);CHECK(outside.size()==2);
    table.xref(x.name)->clip_enabled=false;CHECK(table.expand(ref,v).size()==1);
    table.unload(x.name);CHECK(table.expand(ref,v).empty());
}

TEST(xref_overlays_do_not_propagate_through_a_parent_reference) {
    const auto v=vol();BlockTable table;BlockDef child;child.name="Overlay";child.is_xref=true;child.geometry={seg(v,0,0,1,0)};table.define(child);
    Xref overlay;overlay.name=child.name;overlay.type=XrefType::Overlay;overlay.state=XrefState::Loaded;table.attach(overlay);
    BlockDef parent;parent.name="Attached";parent.is_xref=true;BlockRef nested;nested.name=child.name;parent.nested_refs={nested};table.define(parent);
    Xref attached;attached.name=parent.name;attached.state=XrefState::Loaded;table.attach(attached);
    BlockRef ref;ref.name=parent.name;CHECK(table.expand(ref,v).empty());CHECK(table.expand(nested,v).size()==1);
    table.xref(child.name)->type=XrefType::Attach;CHECK(table.expand(ref,v).size()==1);
    table.insert(nested);CHECK(table.detach(child.name));CHECK(table.references().empty());CHECK(table.find(parent.name)->nested_refs.empty());CHECK(!table.find(child.name));
}

TEST(reference_cache_rename_and_detach_keep_ownership_consistent) {
    gdoc::BlockTable blocks;
    gdoc::BlockDef root;root.name="Site";root.is_xref=true;root.source_owner="Site";
    gdoc::BlockDef child;child.name="Site|Child";child.source_owner="Site";
    gdoc::BlockRef inner;inner.name=child.name;root.nested_refs.push_back(inner);
    gdoc::Xref x;x.name="Site";x.state=gdoc::XrefState::Loaded;
    blocks.replace_xref_contents(x,{root,child},{},{});
    gdoc::BlockRef insertion;insertion.name="Site";blocks.insert(insertion);
    const auto* name_owner=blocks.find("Site");CHECK(name_owner!=nullptr);if(!name_owner)return;
    CHECK(blocks.rename(name_owner->name,"Survey"));
    CHECK(blocks.xref("Site")==nullptr);CHECK(blocks.xref("Survey")!=nullptr);
    CHECK(blocks.references()[0].name=="Survey");CHECK(blocks.find("Site|Child")->source_owner=="Survey");
    CHECK(blocks.detach("Survey"));CHECK(blocks.references().empty());CHECK(blocks.xrefs().empty());CHECK(blocks.find("Site|Child")==nullptr);
}

TEST(nested_reference_notes_and_images_share_composed_clip_boundaries) {
    const auto v=vol();BlockTable table;
    BlockDef leaf;leaf.name="Leaf";leaf.is_xref=true;leaf.base=pt(v,1,2);
    BlockDef::Note note;note.text="Clipped";note.pos=pt(v,2,3);leaf.notes={note};
    Underlay image;image.path="plan.pdf";leaf.underlays={image};table.define(leaf);
    Xref inner;inner.name=leaf.name;inner.state=XrefState::Loaded;inner.clip={pt(v,1,2),pt(v,3,4)};inner.clip_inverted=true;table.attach(inner);
    BlockRef child;child.name=leaf.name;child.insertion=pt(v,4,6);child.scale_x=2;child.scale_y=3;
    BlockDef parent;parent.name="Parent";parent.is_xref=true;parent.nested_refs={child};table.define(parent);
    Xref outer;outer.name=parent.name;outer.state=XrefState::Loaded;outer.clip={pt(v,0,0),pt(v,10,20)};table.attach(outer);
    BlockRef root;root.name=parent.name;root.insertion=pt(v,10,20);root.rotation=std::acos(-1.)/2;
    const auto notes=table.note_instances(root,v);const auto images=table.underlay_instances(root,v);
    CHECK(notes.size()==1);CHECK(images.size()==1);if(notes.empty()||images.empty())return;
    CHECK(notes[0].clips.size()==2);CHECK(images[0].clips.size()==2);if(notes[0].clips.size()!=2||images[0].clips.size()!=2)return;
    CHECK(notes[0].clips[0].inverted);CHECK(!notes[0].clips[1].inverted);
    CHECK(at(v,notes[0].clips[0].points[0],4,24));CHECK(at(v,notes[0].clips[0].points[2],-2,28));
    CHECK(at(v,notes[0].clips[1].points[0],10,20));CHECK(at(v,notes[0].clips[1].points[2],-10,30));
    for(int i=0;i<2;++i)for(int j=0;j<4;++j){CHECK(notes[0].clips[i].points[j].x==images[0].clips[i].points[j].x);CHECK(notes[0].clips[i].points[j].y==images[0].clips[i].points[j].y);}
}

TEST(block_reference_notes_are_placed_with_the_insertion) {
    const auto v=vol();BlockTable table;
    BlockDef def;def.name="Tag";def.base=pt(v,0,0);
    BlockDef::Note fixed;fixed.text="FIXED";fixed.pos=pt(v,0,0);fixed.height=.1;def.notes={fixed};
    table.define(def);
    BlockRef ref;ref.name="Tag";ref.insertion=pt(v,10,20);ref.scale_x=2;ref.scale_y=2;
    BlockNote attr;attr.text="A-101";attr.pos=pt(v,1,0);attr.height=.1;ref.notes={attr};
    const auto notes=table.note_instances(ref,v);
    CHECK(notes.size()==2);
    const auto world=[&](const std::array<double,6>& m,Point2 p){
        double x,y;gcm::to_world(v,p,x,y);
        return std::pair<double,double>{m[0]*x+m[1]*y+m[2],m[3]*x+m[4]*y+m[5]};
    };
    for(const auto& n:notes) {
        const auto p=world(n.transform,n.note.pos);
        if(n.note.text=="FIXED") CHECK(std::fabs(p.first-10)<1e-3 && std::fabs(p.second-20)<1e-3);
        if(n.note.text=="A-101") CHECK(std::fabs(p.first-12)<1e-3 && std::fabs(p.second-20)<1e-3);
    }
}

TEST(block_reference_notes_follow_nested_insertions) {
    const auto v=vol();BlockTable table;
    BlockDef inner;inner.name="Inner";inner.base=pt(v,0,0);table.define(inner);
    BlockDef outer;outer.name="Outer";outer.base=pt(v,0,0);
    BlockRef child;child.name="Inner";child.insertion=pt(v,5,0);
    BlockNote inner_note;inner_note.text="CHILD";inner_note.pos=pt(v,1,0);inner_note.height=.1;child.notes={inner_note};
    outer.nested_refs={child};table.define(outer);
    BlockRef root;root.name="Outer";root.insertion=pt(v,10,0);root.scale_x=2;root.scale_y=2;
    const auto notes=table.note_instances(root,v);
    CHECK(notes.size()==1);
    if(notes.empty())return;
    double x,y;gcm::to_world(v,notes[0].note.pos,x,y);
    const auto& m=notes[0].transform;
    // (1,0) in Inner space -> (6,0) in Outer space -> (22,0) in the drawing.
    CHECK(std::fabs(m[0]*x+m[1]*y+m[2]-22)<1e-3);
    CHECK(std::fabs(m[3]*x+m[4]*y+m[5]-0)<1e-3);
}

TEST(underlay_independent_axes_and_disabled_clips_share_forward_inverse_placement) {
    const auto v=vol();Underlay u;u.insertion=pt(v,10,20);u.rotation=.4;u.scale=2;u.y_factor=-3;
    u.clip={pt(v,0,0),pt(v,1,0),pt(v,1,1),pt(v,0,1)};
    double x,y;u.to_drawing(pt(v,.25,.5),v,x,y);gcm::Lattice2 back;u.to_underlay(x,y,v,back);
    CHECK(at(v,back,.25,.5));CHECK(!u.contains(pt(v,2,2)));u.clip_enabled=false;CHECK(u.contains(pt(v,2,2)));
    CHECK(!u.has_clip());CHECK(u.clip.size()==4);
}
