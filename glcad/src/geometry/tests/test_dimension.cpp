#include "test.hpp"

#include <cmath>

#include "geometry/dimension.hpp"

using namespace gdim;
constexpr f64 kPi = 3.14159265358979323846;

static Volume vol() {
    Volume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
static Point pt(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }

TEST(dim_linear_measures_the_axis_the_placement_implies) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Linear;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 3, 4);

    d.p3 = pt(v, 1.5, 6);
    CHECK_MSG(std::fabs(measure(d, v) - 3.0) < 1e-6,
              "expected 3, got %.6f", measure(d, v));

    d.p3 = pt(v, 8, 2);
    CHECK_MSG(std::fabs(measure(d, v) - 4.0) < 1e-6,
              "expected 4, got %.6f", measure(d, v));
}

TEST(dim_aligned_measures_the_true_distance) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Aligned;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 3, 4);
    d.p3 = pt(v, 2, 3);
    CHECK(std::fabs(measure(d, v) - 5.0) < 1e-6);
}

TEST(dim_horizontal_and_vertical_are_explicit) {
    const auto v = vol();
    Dimension d;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 3, 4);
    d.p3 = pt(v, 1, 1);

    d.kind = Kind::Horizontal;
    CHECK(std::fabs(measure(d, v) - 3.0) < 1e-6);
    d.kind = Kind::Vertical;
    CHECK(std::fabs(measure(d, v) - 4.0) < 1e-6);
}

TEST(dim_radial_and_diameter) {
    const auto v = vol();
    Dimension d;
    d.centre = pt(v, 0, 0);
    d.radius = static_cast<i64>(std::llround(2.5 * v.scale()));
    d.p3 = pt(v, 3, 0);

    d.kind = Kind::Radial;
    CHECK(std::fabs(measure(d, v) - 2.5) < 1e-3);
    d.kind = Kind::Diameter;
    CHECK(std::fabs(measure(d, v) - 5.0) < 1e-3);
}

TEST(dim_angular_measures_the_sweep) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Angular;
    d.centre = pt(v, 0, 0);
    d.p1 = pt(v, 5, 0);
    d.p2 = pt(v, 0, 5);
    d.p3 = pt(v, 3, 3);
    CHECK_MSG(std::fabs(measure(d, v) - kPi * 0.5) < 1e-6,
              "expected pi/2, got %.6f", measure(d, v));
}

TEST(dim_arc_length) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::ArcLength;
    d.centre = pt(v, 0, 0);
    d.radius = static_cast<i64>(std::llround(4.0 * v.scale()));
    d.p1 = pt(v, 4, 0);
    d.p2 = pt(v, 0, 4);
    CHECK(std::fabs(measure(d, v) - 2.0 * kPi) < 1e-3);
}

TEST(dim_formats_through_the_style_and_units) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 1.0668, 0);
    d.p3 = pt(v, 0.5, 1);

    Style mm;
    mm.unit = gunit::Length::Millimetre;
    mm.dimdec = 1;
    CHECK_MSG(format(d, mm, v) == "1066.8", "got \"%s\"", format(d, mm, v).c_str());

    Style arch;
    arch.unit = gunit::Length::Inch;
    arch.format = gunit::Style::Architectural;
    arch.dimdec = 4;
    CHECK_MSG(format(d, arch, v) == "3'-6\"", "got \"%s\"",
              format(d, arch, v).c_str());
}

TEST(dim_diameter_and_radius_carry_their_markers) {
    const auto v = vol();
    Dimension d;
    d.centre = pt(v, 0, 0);
    d.radius = static_cast<i64>(std::llround(0.025 * v.scale()));
    d.p3 = pt(v, 0.03, 0);

    Style s;
    s.unit = gunit::Length::Millimetre;
    s.dimdec = 0;

    d.kind = Kind::Radial;
    CHECK_MSG(format(d, s, v).rfind("R", 0) == 0, "radius lost its R: \"%s\"",
              format(d, s, v).c_str());

    d.kind = Kind::Diameter;
    CHECK_MSG(format(d, s, v).rfind("\xe2\x8c\x80", 0) == 0,
              "diameter lost its marker: \"%s\"", format(d, s, v).c_str());
}

TEST(dim_prefix_suffix_and_override) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 0.1, 0);
    d.p3 = pt(v, 0.05, 0.05);

    Style s;
    s.unit = gunit::Length::Millimetre;
    s.dimdec = 0;
    s.prefix = "L=";
    s.suffix = " TYP";
    CHECK(format(d, s, v) == "L=100 TYP");

    d.text_override = "SEE DETAIL";
    CHECK(format(d, s, v) == "SEE DETAIL");
    CHECK(d.has_override());
}

TEST(dim_tolerance_appended) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 0.05, 0);
    d.p3 = pt(v, 0.025, 0.02);

    Style s;
    s.unit = gunit::Length::Millimetre;
    s.dimdec = 1;
    s.dimtol = true;
    s.dimtp = 0.2;
    s.dimtm = 0.1;
    const std::string out = format(d, s, v);
    CHECK_MSG(out.find("+0.2") != std::string::npos, "got \"%s\"", out.c_str());
    CHECK(out.find("-0.1") != std::string::npos);
}

TEST(dim_measurement_scale_factor) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 0.05, 0);
    d.p3 = pt(v, 0.025, 0.02);

    Style s;
    s.unit = gunit::Length::Millimetre;
    s.dimdec = 0;
    CHECK(format(d, s, v) == "50");
    s.dimlfac = 2.0;
    CHECK(format(d, s, v) == "100");
}

TEST(dim_linear_geometry_has_extension_and_dimension_lines) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 4, 0);
    d.p3 = pt(v, 2, 2);

    Style s;
    const Geometry g = build(d, s, v);
    CHECK_MSG(g.ok, "%s", g.reason);
    CHECK_MSG(g.lines.size() == 3, "expected 3 lines, got %zu", g.lines.size());
    CHECK_MSG(!g.arrows.empty(), "no arrowheads generated");
    CHECK(!g.text.empty());

    f64 tx, ty;
    gcm::to_world(v, g.text_at, tx, ty);
    CHECK(tx > 1.9 && tx < 2.1);
    CHECK(ty > 2.0);
}

TEST(dim_extension_lines_can_be_suppressed) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 4, 0);
    d.p3 = pt(v, 2, 2);

    Style s;
    s.dimse1 = true;
    CHECK(build(d, s, v).lines.size() == 2);
    s.dimse2 = true;
    CHECK(build(d, s, v).lines.size() == 1);
}

TEST(dim_arrow_style_changes_the_geometry) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 4, 0);
    d.p3 = pt(v, 2, 2);

    Style closed;
    closed.arrow = ArrowType::ClosedFilled;
    Style tick;
    tick.arrow = ArrowType::Tick;
    Style none;
    none.arrow = ArrowType::None;

    CHECK(build(d, closed, v).arrows.size() == 12);
    CHECK(build(d, tick, v).arrows.size() == 2);
    CHECK(build(d, none, v).arrows.empty());
}

TEST(dim_scale_multiplies_every_size) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 4, 0);
    d.p3 = pt(v, 2, 2);

    Style s;
    const f64 h1 = build(d, s, v).text_height;
    s.dimscale = 4.0;
    const f64 h4 = build(d, s, v).text_height;
    CHECK_MSG(std::fabs(h4 / h1 - 4.0) < 1e-9, "ratio %.4f", h4 / h1);
}

TEST(dim_radial_geometry_points_at_the_circle) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Radial;
    d.centre = pt(v, 0, 0);
    d.radius = static_cast<i64>(std::llround(3.0 * v.scale()));
    d.p3 = pt(v, 6, 0);

    Style s;
    const Geometry g = build(d, s, v);
    CHECK(g.ok);
    CHECK(g.lines.size() == 1);
    CHECK(!g.arrows.empty());
}

TEST(dim_diameter_spans_the_circle) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Diameter;
    d.centre = pt(v, 0, 0);
    d.radius = static_cast<i64>(std::llround(3.0 * v.scale()));
    d.p3 = pt(v, 6, 0);

    const Geometry g = build(d, Style{}, v);
    CHECK(g.ok);
    f64 ax, ay, bx, by;
    gcm::to_world(v, g.lines[0].a, ax, ay);
    gcm::to_world(v, g.lines[0].b, bx, by);
    CHECK(std::hypot(bx - ax, by - ay) >= 6.0 - 1e-3);
}

TEST(dim_angular_geometry_includes_an_arc) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Angular;
    d.centre = pt(v, 0, 0);
    d.p1 = pt(v, 5, 0);
    d.p2 = pt(v, 0, 5);
    d.p3 = pt(v, 2, 2);

    const Geometry g = build(d, Style{}, v);
    CHECK(g.ok);
    bool has_arc = false;
    for (const auto& l : g.lines)
        if (l.kind == gsnap::ShapeKind::Arc) has_arc = true;
    CHECK_MSG(has_arc, "an angular dimension needs its arc");
}

TEST(dim_style_table) {
    StyleTable t;
    CHECK(t.size() == 1);
    CHECK(t.current() == "Standard");

    Style detail;
    detail.name = "Detail";
    detail.dimscale = 2.0;
    detail.dimdec = 3;
    t.add(detail);

    CHECK(t.size() == 2);
    CHECK(t.find("Detail")->dimscale == 2.0);
    t.set_current("Detail");
    CHECK(t.current() == "Detail");
    CHECK(t.find("Nonexistent") == nullptr);
}

TEST(dim_regeneration_is_pure) {
    const auto v = vol();
    Dimension d;
    d.kind = Kind::Horizontal;
    d.p1 = pt(v, 0, 0);
    d.p2 = pt(v, 4, 0);
    d.p3 = pt(v, 2, 2);

    const Geometry a = build(d, Style{}, v);
    const Geometry b = build(d, Style{}, v);
    CHECK(a.lines.size() == b.lines.size());
    CHECK(a.text == b.text);
    CHECK(a.text_at.x == b.text_at.x && a.text_at.y == b.text_at.y);

    Dimension moved = d;
    moved.p2 = pt(v, 6, 0);
    CHECK(build(moved, Style{}, v).text != a.text);
}

TEST(leader_paths_keep_all_vertices_shared_junctions_and_arrow_flags) {
    const auto v=vol();Dimension d;d.kind=Kind::Leader;
    d.p1=pt(v,0,0);d.p2=pt(v,1,2);d.p3=pt(v,4,2);
    d.leader_paths={{{d.p1,d.p2,pt(v,2,3),pt(v,3,2),d.p3},false},{{pt(v,0,4),pt(v,2,3),pt(v,3,2)},true}};
    const auto geometry=build(d,Style{},v);CHECK(geometry.ok);CHECK(geometry.lines.size()==6);CHECK(!geometry.arrows.empty());
    const auto grips=d.leader_grips();CHECK(grips.size()==6);
    CHECK(d.move_leader_grip(2,pt(v,2,4)));
    CHECK(d.leader_paths[0].vertices[2].y==d.leader_paths[1].vertices[1].y);
    const auto old=d.leader_paths[1].vertices[0];d.transform_points([](auto& point){point.x+=100;point.y-=50;});
    CHECK(d.leader_paths[1].vertices[0].x==old.x+100);CHECK(d.leader_paths[1].vertices[0].y==old.y-50);
    CHECK(!d.move_leader_grip(99,{}));
}

TEST(rotating_a_leader_rotates_its_text_gap_and_preserves_attachment) {
    const auto v=vol();Dimension d;d.kind=Kind::Leader;
    d.p1=pt(v,0,0);d.p2=pt(v,1,2);d.p3=pt(v,4,2);d.text_override="Route";
    d.leader_text_height=.004;d.leader_arrow_size=.003;
    const auto original=build(d,Style{},v);CHECK(original.ok);
    d.transform_points([](auto& p){const auto x=p.x;p.x=-p.y;p.y=x;});
    const auto rotated=build(d,Style{},v);CHECK(rotated.ok);
    CHECK(std::abs(rotated.text_at.x+original.text_at.y)<=1);
    CHECK(std::abs(rotated.text_at.y-original.text_at.x)<=1);
    CHECK(rotated.halign==original.halign);
    CHECK(std::abs(rotated.text_angle-kPi/2)<1e-10);
    CHECK(std::abs(rotated.text_height-.004)<1e-10);
    d.leader_text_absolute=true;d.leader_text_halign=2;d.leader_text_valign=3;
    const auto attached=build(d,Style{},v);CHECK(attached.halign==2);CHECK(attached.valign==3);
    CHECK(attached.text_at.x==d.p3.x);CHECK(attached.text_at.y==d.p3.y);
}

TEST(dim_anchors_reject_nonexistent_and_changed_geometry_points) {
    gsnap::Shape shape;Point p{123,456};
    shape.kind=gsnap::ShapeKind::Circle;shape.a={10,20};shape.radius=50;
    CHECK(!Dimension::anchor_point(shape,Dimension::Anchor::Start,p));CHECK(p.x==123);
    CHECK(!Dimension::anchor_point(shape,Dimension::Anchor::End,p));
    CHECK(Dimension::anchor_point(shape,Dimension::Anchor::Centre,p));CHECK(p.x==10 && p.y==20);
    shape.kind=gsnap::ShapeKind::Ellipse;shape.b={100,20};
    CHECK(!Dimension::anchor_point(shape,Dimension::Anchor::End,p));
    shape.kind=gsnap::ShapeKind::Xline;CHECK(!Dimension::anchor_point(shape,Dimension::Anchor::Start,p));
    shape.kind=gsnap::ShapeKind::Ray;
    CHECK(Dimension::anchor_point(shape,Dimension::Anchor::Start,p));
    CHECK(!Dimension::anchor_point(shape,Dimension::Anchor::End,p));
    shape.kind=gsnap::ShapeKind::Segment;
    CHECK(!Dimension::anchor_point(shape,Dimension::Anchor::Centre,p));
    CHECK(!Dimension::anchor_point(shape,Dimension::Anchor::None,p));
}

TEST(dim_extension_offset_transforms_as_vector) {
    Dimension d;d.p1={10,20};d.p2={30,40};d.p3={50,60};d.extension_offset={3,5};
    d.transform_points([](Point& p){p.x+=100;p.y-=200;});
    CHECK(d.p1.x==110&&d.p1.y==-180);
    CHECK(d.extension_offset.x==3&&d.extension_offset.y==5);
    d.transform_points([](Point& p){const auto x=p.x;p.x=-2*p.y+17;p.y=2*x-13;});
    CHECK(d.extension_offset.x==-10&&d.extension_offset.y==6);
}
