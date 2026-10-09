#include "test.hpp"

#include <cmath>
#include <string>

#include "exchange/dxf.hpp"

using namespace gdxf;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

gsnap::Shape seg(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = gcm::to_lattice(v, x0, y0);
    s.b = gcm::to_lattice(v, x1, y1);
    return s;
}

gsnap::Shape circ(const gcm::WorkingVolume& v, f64 cx, f64 cy, f64 r) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Circle;
    s.a = gcm::to_lattice(v, cx, cy);
    s.b = s.a;
    s.radius = static_cast<gcm::i64>(std::llround(r * v.scale()));
    return s;
}

gsnap::Shape arc(const gcm::WorkingVolume& v, f64 cx, f64 cy, f64 r, f64 a0,
                 f64 a1) {
    gsnap::Shape s = circ(v, cx, cy, r);
    s.kind = gsnap::ShapeKind::Arc;
    s.start_angle = a0;
    s.end_angle = a1;
    return s;
}

void world(const gcm::WorkingVolume& v, const gcm::Lattice2& p, f64& x, f64& y) {
    gcm::to_world(v, p, x, y);
}

Document one(const gcm::WorkingVolume& v, gsnap::Shape s,
             const char* layer = "0") {
    Document d;
    gdoc::Layer l;
    l.name = layer;
    d.layers.add(l);
    d.entities.push_back({s, layer, ""});
    return d;
}

std::string dxf(const char* pairs) { return pairs; }

}

TEST(dxf_lexes_group_pairs) {
    const Tape t = lex("  0\nSECTION\n  2\nHEADER\n  0\nENDSEC\n  0\nEOF\n");
    CHECK_MSG(t.ok, "%s", t.error.c_str());
    CHECK_MSG(t.size() == 4, "%zu groups", t.size());
    CHECK(t.groups[0].code == 0 && t.groups[0].value == "SECTION");
    CHECK(t.groups[1].code == 2 && t.groups[1].value == "HEADER");
    CHECK(t.groups[3].value == "EOF");
}

TEST(dxf_keeps_a_value_with_a_space_in_it) {
    const Tape t = lex("  8\nWALL LAYER 2\n");
    CHECK(t.ok);
    CHECK_MSG(t.groups[0].value == "WALL LAYER 2", "got '%s'",
              t.groups[0].value.c_str());
}

TEST(dxf_refuses_a_binary_file_rather_than_garbling_it) {
    const Tape t = lex("AutoCAD Binary DXF\r\n\x1a\x00\x01\x02");
    CHECK(!t.ok);
    CHECK_MSG(t.error.find("binary") != std::string::npos, "%s",
              t.error.c_str());
}

TEST(dxf_skips_a_line_that_is_not_a_group_code) {
    const Tape t = lex("  0\nSECTION\nnot-a-code\n  2\nENTITIES\n  0\nENDSEC\n  0\nEOF\n");
    CHECK(t.ok);
    CHECK_MSG(t.junk_lines == 1, "%zu junk lines", t.junk_lines);
    CHECK(t.groups.size() == 4);
}

TEST(dxf_an_empty_file_is_refused) {
    CHECK(!lex("").ok);
    CHECK(!lex("\n\n").ok);
}

TEST(dxf_unit_conversions) {
    CHECK(std::fabs(metres_per(Units::Millimetres) - 0.001) < 1e-15);
    CHECK(std::fabs(metres_per(Units::Inches) - 0.0254) < 1e-15);
    CHECK(std::fabs(metres_per(Units::Metres) - 1.0) < 1e-15);
    CHECK_MSG(metres_per(Units::Unspecified) == 0.0,
              "unspecified units were given a scale");
    for (Units u : {Units::Inches, Units::Millimetres, Units::Metres,
                    Units::Unspecified, Units::Feet})
        CHECK(units_name(u)[0] != '\0');
}

TEST(dxf_writes_insunits_and_reads_it_back) {
    const auto v = vol();
    const Document d = one(v, seg(v, 0, 0, 1.0, 0));

    for (Units u : {Units::Millimetres, Units::Metres, Units::Inches,
                    Units::Centimetres}) {
        WriteOptions o;
        o.units = u;
        const std::string text = write(d, v, o);
        CHECK_MSG(text.find("$INSUNITS") != std::string::npos,
                  "%s: no unit was stated", units_name(u));

        const ReadResult r = read(text, v);
        CHECK_MSG(r.ok, "%s: %s", units_name(u), r.error.c_str());
        CHECK_MSG(r.document.units == u, "wrote %s and read %s", units_name(u),
                  units_name(r.document.units));
        CHECK(r.document.entities.size() == 1);

        f64 ax, ay, bx, by;
        world(v, r.document.entities[0].shape.a, ax, ay);
        world(v, r.document.entities[0].shape.b, bx, by);
        CHECK_MSG(std::fabs(std::hypot(bx - ax, by - ay) - 1.0) < 1e-4,
                  "%s: the line came back %g long", units_name(u),
                  std::hypot(bx - ax, by - ay));
    }
}

TEST(dxf_a_file_with_no_units_says_so_rather_than_guessing) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n0.0\n 11\n1.0\n 21\n0.0\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK(r.ok);
    CHECK(r.document.units == Units::Unspecified);
    bool warned = false;
    for (const std::string& s : r.skipped)
        if (s.find("units") != std::string::npos) warned = true;
    CHECK_MSG(warned, "an unstated unit passed without comment");
}

TEST(dxf_a_line_round_trips) {
    const auto v = vol();
    const Document d = one(v, seg(v, -1.5, 2.0, 3.25, -4.0), "WALLS");
    WriteOptions o;
    o.units = Units::Metres;

    const ReadResult r = read(write(d, v, o), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.document.entities.size() == 1);
    const gsnap::Shape& s = r.document.entities[0].shape;
    CHECK(s.kind == gsnap::ShapeKind::Segment);
    CHECK(r.document.entities[0].layer == "WALLS");

    f64 ax, ay, bx, by;
    world(v, s.a, ax, ay);
    world(v, s.b, bx, by);
    CHECK(std::fabs(ax + 1.5) < 1e-6 && std::fabs(ay - 2.0) < 1e-6);
    CHECK(std::fabs(bx - 3.25) < 1e-6 && std::fabs(by + 4.0) < 1e-6);
}

TEST(dxf_a_circle_and_an_arc_round_trip) {
    const auto v = vol();
    WriteOptions o;
    o.units = Units::Metres;

    Document d;
    d.entities.push_back({circ(v, 1.0, 2.0, 3.0), "0", ""});
    d.entities.push_back({arc(v, -1.0, 0.5, 2.0, 0.3, 2.1), "0", ""});

    const ReadResult r = read(write(d, v, o), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.document.entities.size() == 2, "%zu entities",
              r.document.entities.size());

    const gsnap::Shape& c = r.document.entities[0].shape;
    CHECK(c.kind == gsnap::ShapeKind::Circle);
    CHECK(std::fabs(f64(c.radius) / v.scale() - 3.0) < 1e-6);

    const gsnap::Shape& a = r.document.entities[1].shape;
    CHECK(a.kind == gsnap::ShapeKind::Arc);
    CHECK(std::fabs(f64(a.radius) / v.scale() - 2.0) < 1e-6);
    CHECK_MSG(std::fabs(a.start_angle - 0.3) < 1e-6,
              "the start angle came back as %g", a.start_angle);
    CHECK(std::fabs(a.end_angle - 2.1) < 1e-6);
}

TEST(dxf_arc_angles_are_written_in_degrees) {
    const auto v = vol();
    WriteOptions o;
    o.units = Units::Metres;
    const Document d = one(v, arc(v, 0, 0, 1.0, 0.0, kPi * 0.5));
    const std::string text = write(d, v, o);

    const Tape t = lex(text);
    CHECK(t.ok);
    bool found_ninety = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t.groups[i].code != 51) continue;
        f64 value = 0;
        CHECK(t.groups[i].as_real(value));
        if (std::fabs(value - 90.0) < 1e-6) found_ninety = true;
    }
    CHECK_MSG(found_ninety, "the end angle was not 90 degrees");
}

TEST(dxf_construction_lines_stay_infinite) {
    const auto v = vol();
    WriteOptions o;
    o.units = Units::Metres;

    for (gsnap::ShapeKind kind : {gsnap::ShapeKind::Ray, gsnap::ShapeKind::Xline}) {
        gsnap::Shape s = seg(v, 0, 0, 1.0, 1.0);
        s.kind = kind;
        const ReadResult r = read(write(one(v, s), v, o), v);
        CHECK_MSG(r.ok, "%s", r.error.c_str());
        CHECK(r.document.entities.size() == 1);
        CHECK_MSG(r.document.entities[0].shape.kind == kind,
                  "an infinite line came back as something else");
        CHECK(r.document.entities[0].shape.is_infinite());
    }
}

TEST(dxf_a_point_and_an_ellipse_round_trip) {
    const auto v = vol();
    WriteOptions o;
    o.units = Units::Metres;

    gsnap::Shape p;
    p.kind = gsnap::ShapeKind::Point;
    p.a = gcm::to_lattice(v, 3.0, 4.0);
    p.b = p.a;

    gsnap::Shape e;
    e.kind = gsnap::ShapeKind::Ellipse;
    e.a = gcm::to_lattice(v, 0.0, 0.0);
    e.b = gcm::to_lattice(v, 4.0, 0.0);          // major axis endpoint
    e.radius = static_cast<gcm::i64>(std::llround(2.0 * v.scale()));

    Document d;
    d.entities.push_back({p, "0", ""});
    d.entities.push_back({e, "0", ""});

    const ReadResult r = read(write(d, v, o), v);
    CHECK(r.ok && r.document.entities.size() == 2);
    CHECK(r.document.entities[0].shape.kind == gsnap::ShapeKind::Point);

    const gsnap::Shape& back = r.document.entities[1].shape;
    CHECK(back.kind == gsnap::ShapeKind::Ellipse);
    f64 cx, cy, mx, my;
    world(v, back.a, cx, cy);
    world(v, back.b, mx, my);
    CHECK_MSG(std::fabs(std::hypot(mx - cx, my - cy) - 4.0) < 1e-5,
              "the major axis came back %g", std::hypot(mx - cx, my - cy));
    CHECK_MSG(std::fabs(f64(back.radius) / v.scale() - 2.0) < 1e-5,
              "the minor radius came back %g", f64(back.radius) / v.scale());
}

TEST(dxf_a_bulge_of_zero_is_a_straight_segment) {
    const Bulge b = arc_from_bulge(0, 0, 10, 0, 0.0);
    CHECK(!b.is_arc);
}

TEST(dxf_a_bulge_of_one_is_a_semicircle) {
    const Bulge b = arc_from_bulge(-1.0, 0.0, 1.0, 0.0, 1.0);
    CHECK(b.is_arc);
    CHECK_MSG(std::fabs(b.radius - 1.0) < 1e-9, "the radius is %g", b.radius);
    CHECK_MSG(std::hypot(b.cx, b.cy) < 1e-9, "the centre is at %g,%g", b.cx, b.cy);
}

TEST(dxf_a_quarter_bulge_gives_a_quarter_arc) {
    // tan(90/4) = tan(22.5 degrees) = 0.41421
    const f64 quarter = std::tan(kPi * 0.5 * 0.25);
    const Bulge b = arc_from_bulge(1.0, 0.0, 0.0, 1.0, quarter);
    CHECK(b.is_arc);
    CHECK_MSG(std::fabs(b.radius - 1.0) < 1e-6, "the radius is %g", b.radius);
    CHECK(std::hypot(b.cx, b.cy) < 1e-6);
}

TEST(dxf_the_bulge_sign_chooses_which_way_the_arc_goes) {
    const Bulge up = arc_from_bulge(-1.0, 0.0, 1.0, 0.0, 1.0);
    const Bulge down = arc_from_bulge(-1.0, 0.0, 1.0, 0.0, -1.0);
    CHECK(up.is_arc && down.is_arc);
    CHECK_MSG(std::fabs(up.start_angle - down.end_angle) < 1e-6 ||
                  std::fabs(up.start_angle - down.end_angle - 2.0 * kPi) < 1e-6,
              "the two directions are not each other's reverse");
}

TEST(dxf_bulge_and_arc_are_inverses) {
    for (f64 sweep : {0.2, 1.0, kPi * 0.5, kPi, 4.0, 6.0}) {
        const f64 b = bulge_from_arc(0.0, sweep);
        const Bulge back = arc_from_bulge(1.0, 0.0, std::cos(sweep),
                                          std::sin(sweep), b);
        if (!back.is_arc) continue;
        CHECK_MSG(std::fabs(back.radius - 1.0) < 1e-5,
                  "a sweep of %g came back with radius %g", sweep, back.radius);
    }
}

TEST(dxf_a_polyline_with_bulges_imports_as_lines_and_arcs) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLWPOLYLINE\n  8\n0\n 90\n3\n 70\n0\n"
        " 10\n0.0\n 20\n0.0\n"
        " 10\n1.0\n 20\n0.0\n 42\n1.0\n"          // a semicircular bulge
        " 10\n1.0\n 20\n2.0\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.document.entities.size() == 2, "%zu segments",
              r.document.entities.size());
    CHECK(r.document.entities[0].shape.kind == gsnap::ShapeKind::Segment);
    CHECK_MSG(r.document.entities[1].shape.kind == gsnap::ShapeKind::Arc,
              "the bulged segment came back as a chord");
    CHECK(std::fabs(f64(r.document.entities[1].shape.radius) / v.scale() - 1.0) <
          1e-5);
}

TEST(dxf_a_closed_polyline_closes) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLWPOLYLINE\n  8\n0\n 90\n3\n 70\n1\n"
        " 10\n0.0\n 20\n0.0\n"
        " 10\n1.0\n 20\n0.0\n"
        " 10\n1.0\n 20\n1.0\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK(r.ok);
    CHECK_MSG(r.document.entities.size() == 3, "%zu segments for a closed "
              "triangle", r.document.entities.size());
}

TEST(dxf_a_negative_extrusion_mirrors_the_entity) {
    const auto v = vol();
    const char* body =
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nCIRCLE\n  8\n0\n 10\n5.0\n 20\n3.0\n 40\n1.0\n"
        "210\n0.0\n220\n0.0\n230\n-1.0\n"
        "  0\nENDSEC\n  0\nEOF\n";

    const ReadResult r = read(body, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.document.entities.size() == 1);
    CHECK_MSG(r.mirrored_entities == 1, "the mirrored entity was not noticed");

    f64 x, y;
    world(v, r.document.entities[0].shape.a, x, y);
    CHECK_MSG(std::fabs(x + 5.0) < 1e-6,
              "the mirrored circle sits at x=%g, not -5", x);
    CHECK_MSG(std::fabs(y - 3.0) < 1e-6, "y should not have moved, got %g", y);
}

TEST(dxf_a_positive_extrusion_changes_nothing) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nCIRCLE\n  8\n0\n 10\n5.0\n 20\n3.0\n 40\n1.0\n"
        "210\n0.0\n220\n0.0\n230\n1.0\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK(r.ok);
    CHECK(r.mirrored_entities == 0);
    f64 x, y;
    world(v, r.document.entities[0].shape.a, x, y);
    CHECK(std::fabs(x - 5.0) < 1e-6);
}

TEST(dxf_a_mirrored_arc_is_turned_round_as_well_as_moved) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nARC\n  8\n0\n 10\n0.0\n 20\n0.0\n 40\n1.0\n 50\n0.0\n 51\n90.0\n"
        "210\n0.0\n220\n0.0\n230\n-1.0\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK(r.ok && r.document.entities.size() == 1);
    const gsnap::Shape& a = r.document.entities[0].shape;
    // 0..90 mirrored about x becomes 90..180.
    CHECK_MSG(std::fabs(a.start_angle - kPi * 0.5) < 1e-6,
              "the mirrored arc starts at %g", a.start_angle);
    CHECK_MSG(std::fabs(a.end_angle - kPi) < 1e-6,
              "the mirrored arc ends at %g", a.end_angle);
}

TEST(dxf_layers_round_trip_including_the_off_ones) {
    const auto v = vol();
    Document d;
    gdoc::Layer on;
    on.name = "WALLS";
    on.color = gdoc::Color::aci(5);
    d.layers.add(on);

    gdoc::Layer off;
    off.name = "GRID";
    off.color = gdoc::Color::aci(8);
    off.on = false;
    d.layers.add(off);

    gdoc::Layer frozen;
    frozen.name = "OLD";
    frozen.frozen = true;
    d.layers.add(frozen);

    d.entities.push_back({seg(v, 0, 0, 1, 0), "WALLS", ""});

    WriteOptions o;
    o.units = Units::Metres;
    const ReadResult r = read(write(d, v, o), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());

    const gdoc::Layer* w = r.document.layers.find("WALLS");
    CHECK(w != nullptr);
    CHECK(w->on);
    CHECK_MSG(w->color.index == 5, "the colour came back as %d", w->color.index);

    const gdoc::Layer* g = r.document.layers.find("GRID");
    CHECK(g != nullptr);
    CHECK_MSG(!g->on, "a switched-off layer imported as visible");
    CHECK_MSG(g->color.index == 8, "the off layer's colour is %d",
              g->color.index);

    const gdoc::Layer* old = r.document.layers.find("OLD");
    CHECK(old != nullptr && old->frozen);
}

TEST(dxf_layer_zero_always_exists) {
    const auto v = vol();
    Document d;
    d.entities.push_back({seg(v, 0, 0, 1, 0), "0", ""});
    WriteOptions o;
    o.units = Units::Metres;
    const std::string text = write(d, v, o);
    CHECK(text.find("\n0\n") != std::string::npos);

    const ReadResult r = read(text, v);
    CHECK(r.ok);
    CHECK_MSG(r.document.layers.find("0") != nullptr, "layer 0 is missing");
}

TEST(dxf_blocks_and_insertions_round_trip) {
    const auto v = vol();
    Document d;
    gdoc::BlockDef def;
    def.name = "DOOR";
    def.geometry.push_back(seg(v, 0, 0, 1.0, 0));
    def.geometry.push_back(seg(v, 1.0, 0, 1.0, 1.0));
    d.blocks.define(def);

    gdoc::BlockRef ref;
    ref.name = "DOOR";
    ref.insertion = gcm::to_lattice(v, 5.0, 6.0);
    ref.scale_x = 2.0;
    ref.scale_y = 2.0;
    ref.rotation = kPi * 0.5;
    d.blocks.insert(ref);

    WriteOptions o;
    o.units = Units::Metres;
    const std::string text = write(d, v, o);
    CHECK(text.find("BLOCKS") != std::string::npos);
    CHECK(text.find("DOOR") != std::string::npos);
    CHECK(text.find("INSERT") != std::string::npos);

    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.document.blocks.find("DOOR") != nullptr,
              "the block definition was lost");
    CHECK_MSG(r.document.blocks.find("DOOR")->geometry.size() == 2,
              "the block came back with %zu shapes",
              r.document.blocks.find("DOOR")->geometry.size());

    CHECK_MSG(r.document.blocks.references().size() == 1, "%zu insertions",
              r.document.blocks.references().size());
    const gdoc::BlockRef& back = r.document.blocks.references()[0];
    CHECK(back.name == "DOOR");
    f64 x, y;
    world(v, back.insertion, x, y);
    CHECK(std::fabs(x - 5.0) < 1e-6 && std::fabs(y - 6.0) < 1e-6);
    CHECK_MSG(std::fabs(back.scale_x - 2.0) < 1e-9, "the scale came back %g",
              back.scale_x);
    CHECK_MSG(std::fabs(back.rotation - kPi * 0.5) < 1e-6,
              "the rotation came back %g", back.rotation);
}

TEST(dxf_code_42_is_a_scale_on_an_insert_and_a_bulge_on_a_polyline) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nINSERT\n  8\n0\n  2\nDOOR\n 10\n0.0\n 20\n0.0\n 41\n3.0\n 42\n3.0\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK(r.ok);
    CHECK(r.document.blocks.references().size() == 1);
    CHECK_MSG(std::fabs(r.document.blocks.references()[0].scale_y - 3.0) < 1e-9,
              "the insert's y scale came back %g",
              r.document.blocks.references()[0].scale_y);
}

TEST(dxf_an_unknown_entity_is_counted_not_fatal) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nWIPEOUT\n  8\n0\n 10\n1.0\n 20\n2.0\n"
        "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n0.0\n 11\n1.0\n 21\n1.0\n"
        "  0\nSOMETHING_NEW\n  8\n0\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.document.entities.size() == 1, "the line was lost with the "
              "unknowns");
    CHECK_MSG(r.unknown_entities == 2, "%zu unknown entities counted",
              r.unknown_entities);
}

TEST(dxf_a_file_with_no_geometry_says_so) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nEOF\n", v);
    CHECK(r.ok);
    CHECK(r.document.entities.empty());
    CHECK_MSG(!r.error.empty(), "an empty import said nothing");
}

TEST(dxf_export_is_deterministic) {
    const auto v = vol();
    Document d;
    d.entities.push_back({seg(v, 0, 0, 1, 1), "0", ""});
    d.entities.push_back({circ(v, 2, 2, 1), "0", ""});
    WriteOptions o;
    o.units = Units::Millimetres;
    CHECK(write(d, v, o) == write(d, v, o));
}

TEST(dxf_a_whole_drawing_survives_a_round_trip) {
    const auto v = vol();
    Document d;
    gdoc::Layer walls;
    walls.name = "WALLS";
    walls.color = gdoc::Color::aci(3);
    d.layers.add(walls);

    d.entities.push_back({seg(v, 0, 0, 10, 0), "WALLS", ""});
    d.entities.push_back({seg(v, 10, 0, 10, 8), "WALLS", ""});
    d.entities.push_back({arc(v, 10, 8, 2.0, kPi, kPi * 1.5), "WALLS", ""});
    d.entities.push_back({circ(v, 5, 4, 1.5), "0", ""});

    WriteOptions o;
    o.units = Units::Millimetres;
    const ReadResult r = read(write(d, v, o), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.document.entities.size() == 4, "%zu entities came back",
              r.document.entities.size());

    CHECK(r.document.entities[0].shape.kind == gsnap::ShapeKind::Segment);
    CHECK(r.document.entities[2].shape.kind == gsnap::ShapeKind::Arc);
    CHECK(r.document.entities[3].shape.kind == gsnap::ShapeKind::Circle);
    CHECK(r.document.entities[0].layer == "WALLS");
    CHECK(r.document.entities[3].layer == "0");

    f64 x, y;
    world(v, r.document.entities[1].shape.b, x, y);
    CHECK_MSG(std::fabs(x - 10.0) < 1e-5 && std::fabs(y - 8.0) < 1e-5,
              "the corner came back at %g,%g", x, y);
}

TEST(dxf_notes_round_trip) {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    gdxf::Document d;
    d.texts.push_back({gcm::to_lattice(v, 1.5, 2.0), "HOLD TO 0.1", 0.005, 0.5, "NOTES"});
    d.texts.push_back({gcm::to_lattice(v, -3.0, 4.0), "SEE DETAIL A", 0.0035, 0.0, "0"});
    gdxf::WriteOptions o;
    o.units = gdxf::Units::Millimetres;
    const std::string text = gdxf::write(d, v, o);
    CHECK(text.find("\nTEXT\n") != std::string::npos);
    CHECK(text.find("HOLD TO 0.1") != std::string::npos);
    const gdxf::ReadResult r = gdxf::read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.document.texts.size() == 2, "%zu notes", r.document.texts.size());
    if (r.document.texts.size() != 2) return;
    const gdxf::Text& t = r.document.texts[0];
    f64 x, y;
    gcm::to_world(v, t.pos, x, y);
    CHECK_MSG(std::fabs(x - 1.5) < 1e-6 && std::fabs(y - 2.0) < 1e-6, "at %g %g", x, y);
    CHECK(t.text == "HOLD TO 0.1");
    CHECK_MSG(std::fabs(t.height - 0.005) < 1e-12, "height %g", t.height);
    CHECK_MSG(std::fabs(t.angle - 0.5) < 1e-9, "angle %g", t.angle);
    CHECK(t.layer == "NOTES");
}

TEST(dxf_mtext_retains_editable_formatting_and_paragraphs) {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    const std::string text =
        "0\nSECTION\n2\nENTITIES\n0\nMTEXT\n8\n0\n10\n1\n20\n2\n30\n0\n40\n2.5\n"
        "1\n{\\fArial|b1;BREAK}\\PALL EDGES\n0\nENDSEC\n0\nEOF\n";
    const gdxf::ReadResult r = gdxf::read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.document.texts.size() == 1, "%zu notes", r.document.texts.size());
    if (r.document.texts.empty()) return;
    CHECK_MSG(r.document.texts[0].text == "{\\fArial|b1;BREAK}\\PALL EDGES", "got '%s'", r.document.texts[0].text.c_str());
}

TEST(dxf_multileader_imports_its_lines_dogleg_and_note) {
    const auto v = vol();
    const ReadResult r = read(
        "  0\nSECTION\n  2\nHEADER\n  9\n$INSUNITS\n 70\n6\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nMULTILEADER\n  8\nNOTES\n100\nAcDbMLeader\n"
        "300\nCONTEXT_DATA{\n 40\n1\n 10\n5.0\n 20\n5.0\n 41\n0.25\n140\n0.18\n"
        "304\nSEE DETAIL\n 12\n5.5\n 22\n5.0\n"
        "302\nLEADER{\n290\n1\n 10\n5.0\n 20\n5.0\n 11\n1\n 21\n0\n 40\n0.5\n"
        "304\nLEADER_LINE{\n 10\n1.0\n 20\n1.0\n 10\n3.0\n 20\n4.0\n 91\n0\n305\n}\n"
        "303\n}\n301\n}\n"
        "  0\nENDSEC\n  0\nEOF\n", v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.unknown_entities == 0, "%zu unknown", r.unknown_entities);
    CHECK(r.document.entities.empty());CHECK(r.document.texts.empty());CHECK(r.document.dimensions.size()==1);
    if(r.document.dimensions.empty())return;
    const auto& leader=r.document.dimensions.front();CHECK(leader.kind==gdim::Kind::Leader);
    CHECK(leader.leader_paths.size()==1);CHECK(leader.leader_paths.front().vertices.size()==4);
    CHECK(leader.layer=="NOTES");CHECK(leader.text_override=="SEE DETAIL");
    f64 x,y;world(v,leader.p3,x,y);CHECK(std::abs(x-5.5)<1e-6);CHECK(std::abs(y-5)<1e-6);
    CHECK(std::abs(leader.leader_text_height-.25)<1e-9);CHECK(leader.leader_text_absolute);

}

TEST(dxf_mirrored_insert_flips_hand_and_angle) {
    // 230 = -1: mirrored OCS; negate x and the angle, and flip the hand.
    const char* text =
        "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nCH\n10\n0\n20\n0\n"
        "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n21\n0\n0\nENDBLK\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nINSERT\n8\n0\n2\nCH\n10\n100\n20\n50\n41\n1\n42\n1\n50\n30\n"
        "210\n0\n220\n0\n230\n-1\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto r = gdxf::read(text, vol());
    CHECK(r.ok);
    CHECK(r.document.blocks.references().size() == 1);
    const auto& ref = r.document.blocks.references()[0];
    double x, y;
    gcm::to_world(vol(), ref.insertion, x, y);
    CHECK_MSG(std::fabs(x + 100.0) < 1e-6 && std::fabs(y - 50.0) < 1e-6, "ins=(%g, %g)", x, y);
    CHECK_MSG(std::fabs(ref.rotation + 30.0 * 3.14159265358979 / 180.0) < 1e-9,
              "rotation=%g, want -30 degrees", ref.rotation);
    CHECK_MSG(ref.scale_x == -1.0 && ref.scale_y == 1.0, "scale=(%g, %g)", ref.scale_x, ref.scale_y);
}

TEST(dxf_centred_text_anchors_at_its_second_point) {
    // 72 = 1: centred text anchors at the second point (11/21).
    const char* text =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nTEXT\n8\n0\n10\n3\n20\n4\n40\n0.5\n1\nA7\n72\n1\n73\n2\n11\n10\n21\n20\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto r = gdxf::read(text, vol());
    CHECK(r.ok);
    CHECK(r.document.texts.size() == 1);
    const auto& t = r.document.texts[0];
    double x, y;
    gcm::to_world(vol(), t.pos, x, y);
    CHECK_MSG(std::fabs(x - 10.0) < 1e-6 && std::fabs(y - 20.0) < 1e-6, "anchored at (%g, %g)", x, y);
    CHECK_MSG(t.halign == 1 && t.valign == 2, "alignment (%d, %d)", t.halign, t.valign);
}

TEST(dxf_writes_the_dimensions_hatches_and_splines_it_was_given) {
    const gcm::WorkingVolume v{};
    gdxf::Document d;

    gsnap::Shape a;
    a.kind = gsnap::ShapeKind::Segment;
    const auto P = [&](double x, double y) { return gcm::to_lattice(v, x, y); };
    const double c[4][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    for (int i = 0; i < 4; ++i) {
        a.a = P(c[i][0], c[i][1]);
        a.b = P(c[(i + 1) % 4][0], c[(i + 1) % 4][1]);
        d.entities.push_back({a, "A-WALL", ""});
    }

    gdim::Dimension dim;
    dim.kind = gdim::Kind::Aligned;
    dim.layer = "A-ANNO-DIMS";
    dim.style = "Standard";
    dim.p1 = P(0, 0);
    dim.p2 = P(4, 0);
    dim.p3 = P(2, -1);
    dim.text_override = "FOUR";
    d.dimensions.push_back(dim);

    gdxf::HatchRec h;
    h.pattern = "ANSI31";
    h.scale = 0.5;
    h.angle = 45.0;
    h.seed = P(2, 2);
    h.layer = "A-WALL";
    for (int i = 0; i < 4; ++i) {
        gsnap::Shape e = a;
        e.a = P(c[i][0], c[i][1]);
        e.b = P(c[(i + 1) % 4][0], c[(i + 1) % 4][1]);
        h.boundary.push_back(e);
    }
    d.hatches.push_back(h);

    ggeo::NurbsCurve2<gcm::F64> spl;
    spl.degree = 2;
    for (int i = 0; i < 4; ++i) {
        ggeo::ControlPoint2<gcm::F64> cp;
        cp.p.x = gcm::F64::from(double(i));
        cp.p.y = gcm::F64::from(double(i % 2));
        cp.w = gcm::F64::from(i == 1 ? 2.0 : 1.0);      // rational, on purpose
        spl.control.push_back(cp);
    }
    spl.knots = ggeo::KnotVector::clamped_uniform(spl.degree, spl.control.size());
    d.splines.push_back(spl);

    const std::string text = gdxf::write(d, v, gdxf::WriteOptions{});
    CHECK_MSG(text.find("DIMENSION") != std::string::npos, "no DIMENSION written");
    CHECK_MSG(text.find("HATCH") != std::string::npos, "no HATCH written");
    CHECK_MSG(text.find("SPLINE") != std::string::npos, "no SPLINE written");
    CHECK_MSG(text.find("ANSI31") != std::string::npos, "the pattern name was lost");
    CHECK_MSG(text.find("FOUR") != std::string::npos, "the dimension override was lost");

    const gdxf::ReadResult r = gdxf::read(text, v);
    CHECK_MSG(r.ok, "re-reading what we wrote failed: %s", r.error.c_str());
    CHECK_MSG(r.document.dimensions.size() == 1, "%zu dimensions came back",
              r.document.dimensions.size());
    if (!r.document.dimensions.empty()) {
        const auto& got = r.document.dimensions[0];
        CHECK(got.kind == gdim::Kind::Aligned);
        CHECK_MSG(got.text_override == "FOUR", "override came back '%s'",
                  got.text_override.c_str());
        double x = 0, y = 0;
        gcm::to_world(v, got.p2, x, y);
        CHECK_MSG(std::fabs(x - 4.0) < 1e-6, "definition point came back at %.6f", x);
    }
    CHECK_MSG(r.document.hatches.size() == 1, "%zu hatches came back",
              r.document.hatches.size());
    if (!r.document.hatches.empty())
        CHECK(r.document.hatches[0].pattern == "ANSI31");
    CHECK_MSG(r.document.splines.size() == 1, "%zu splines came back",
              r.document.splines.size());
    if (!r.document.splines.empty()) {
        const auto& back = r.document.splines[0];
        CHECK(back.degree == 2 && back.control.size() == 4);
        if (back.control.size() == 4)
            CHECK_MSG(std::fabs(back.control[1].w.to_f64() - 2.0) < 1e-9,
                      "the weight came back %.6f", back.control[1].w.to_f64());
    }
}

TEST(dxf_leader_keeps_annotation_and_named_style) {
    const auto v=vol();Document d;gdim::Style st;st.name="Callouts";st.dimasz=4;st.dimscale=2;d.dim_styles.push_back(st);
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.style=st.name;leader.layer="0";
    leader.p1=gcm::to_lattice(v,1,2);leader.p2=gcm::to_lattice(v,2,3);leader.p3=gcm::to_lattice(v,3,3);leader.text_override="Anchor note";d.dimensions.push_back(leader);
    WriteOptions options;options.units=Units::Metres;auto r=read(write(d,v,options),v);
    CHECK(r.ok);CHECK(r.document.dimensions.size()==1);CHECK(r.document.texts.empty());
    if(!r.document.dimensions.empty()){const auto& x=r.document.dimensions[0];CHECK(x.kind==gdim::Kind::Leader);CHECK(x.style=="Callouts");CHECK(x.text_override=="Anchor note");}
    CHECK(!r.document.dim_styles.empty());if(!r.document.dim_styles.empty()){CHECK(r.document.dim_styles[0].name=="Callouts");CHECK(r.document.dim_styles[0].dimasz==4);CHECK(r.document.dim_styles[0].dimscale==2);}
}

TEST(dxf_transparency_distinguishes_opaque_bylayer_byblock_and_alpha) {
    const auto v=vol();
    for(const int transparency:{-2,-1,0,1,50,90,95,100}) {
        Document d;auto shape=seg(v,0,0,1,1);shape.attrs.transparency=transparency;
        d.entities.push_back({shape,"0","LINE"});
        const auto back=read(write(d,v,WriteOptions{}),v);
        CHECK(back.ok);CHECK(back.document.entities.size()==1);
        if(!back.document.entities.empty())CHECK(back.document.entities.front().shape.attrs.transparency==transparency);
        if(transparency==-2)CHECK(!shape.attrs.all_by_layer());
    }
}

TEST(dxf_block_insertion_keeps_appearance_overrides) {
    const auto v=vol();Document d;gdoc::BlockDef def;def.name="Symbol";
    def.geometry.push_back(seg(v,0,0,1,0));d.blocks.define(def);
    gdoc::BlockRef ref;ref.name="Symbol";ref.attrs.transparency=50;ref.attrs.color=gdoc::Color::aci(3);ref.attrs.visible=false;d.blocks.insert(ref);
    const auto back=read(write(d,v,WriteOptions{}),v);CHECK(back.ok);CHECK(back.document.blocks.references().size()==1);
    if(!back.document.blocks.references().empty()) {
        const auto& attrs=back.document.blocks.references().front().attrs;
        CHECK(attrs.transparency==50);CHECK(attrs.color==ref.attrs.color);CHECK(!attrs.visible);
    }
}

TEST(dxf_leader_preserves_every_bend_and_disabled_arrow) {
    const auto v=vol();Document d;gdim::Dimension leader;leader.kind=gdim::Kind::Leader;
    for(auto p:std::vector<std::pair<double,double>>{{0,0},{1,2},{2,3},{3,2},{4,2}}) {
        if(leader.leader_paths.empty())leader.leader_paths.push_back({{},false});
        leader.leader_paths.front().vertices.push_back(gcm::to_lattice(v,p.first,p.second));
    }
    const auto points=leader.leader_paths.front().vertices;leader.p1=points[0];leader.p2=points[1];leader.p3=points.back();d.dimensions.push_back(leader);
    const auto back=read(write(d,v,WriteOptions{}),v);CHECK(back.ok);CHECK(back.document.dimensions.size()==1);
    if(back.document.dimensions.empty())return;
    const auto paths=back.document.dimensions.front().paths();CHECK(paths.size()==1);CHECK(paths.front().vertices.size()==5);CHECK(!paths.front().arrow);
}

TEST(leader_exchange_retains_rotated_text_attachment) {
    const auto v=vol();Document d;
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;
    leader.p1=gcm::to_lattice(v,0,0);leader.p2=gcm::to_lattice(v,1,2);leader.p3=gcm::to_lattice(v,3,4);
    leader.text_override="Aligned note";leader.leader_text_absolute=true;
    leader.leader_text_halign=2;leader.leader_text_valign=3;leader.leader_text_height=.004;leader.leader_text_angle=.5;
    d.dimensions.push_back(leader);
    const auto text=write(d,v,WriteOptions{});CHECK(!text.empty());const auto back=read(text,v);CHECK(back.ok);CHECK(back.document.dimensions.size()==1);if(back.document.dimensions.empty())return;const auto& got=back.document.dimensions.front();
    CHECK(got.text_override==leader.text_override);CHECK(got.leader_text_absolute);
    CHECK(got.leader_text_halign==2);CHECK(got.leader_text_valign==3);
    CHECK(std::abs(got.leader_text_height-.004)<1e-8);CHECK(std::abs(got.leader_text_angle-.5)<1e-8);
    CHECK(std::abs(got.p3.x-leader.p3.x)<=1);CHECK(std::abs(got.p3.y-leader.p3.y)<=1);
}

TEST(leader_arrow_override_exports_without_changing_the_shared_style) {
    const auto v=vol();Document d;gdim::Style style;style.name="Callouts";style.dimscale=2;style.dimasz=3;d.dim_styles.push_back(style);
    gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.style=style.name;
    leader.p1=gcm::to_lattice(v,0,0);leader.p2=gcm::to_lattice(v,1,2);leader.p3=gcm::to_lattice(v,3,2);
    leader.leader_arrow_size=.009;d.dimensions.push_back(leader);
    WriteOptions options;options.units=Units::Metres;const auto text=write(d,v,options);CHECK(!text.empty());const auto back=read(text,v);CHECK(back.ok);
    CHECK(d.dim_styles.size()==1);CHECK(d.dim_styles.front().dimasz==3);CHECK(d.dimensions.front().style=="Callouts");
    CHECK(back.document.dim_styles.size()==2);CHECK(back.document.dimensions.size()==1);if(back.document.dimensions.empty())return;
    const auto found=std::find_if(back.document.dim_styles.begin(),back.document.dim_styles.end(),[&](const auto& s){return s.name==back.document.dimensions.front().style;});
    CHECK(found!=back.document.dim_styles.end());if(found!=back.document.dim_styles.end())CHECK(std::abs(found->scaled(found->dimasz)*.001-.009)<1e-10);
}

TEST(dxf_foreign_dimension_style_sizes_are_in_drawing_units) {
    const auto v=vol();
    const auto back=read("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n6\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nDIMSTYLE\n0\nDIMSTYLE\n2\nMetres\n40\n2\n41\n0.003\n140\n0.004\n147\n0.0005\n42\n0.0008\n44\n0.001\n0\nENDTAB\n0\nENDSEC\n0\nEOF\n",v);
    CHECK(back.ok);CHECK(back.document.dim_styles.size()==1);if(back.document.dim_styles.empty())return;
    const auto& style=back.document.dim_styles.front();CHECK(style.dimscale==2);
    CHECK(std::abs(style.dimasz-3)<1e-10);CHECK(std::abs(style.dimtxt-4)<1e-10);
    CHECK(std::abs(style.dimgap-.5)<1e-10);CHECK(std::abs(style.dimexo-.8)<1e-10);CHECK(std::abs(style.dimexe-1)<1e-10);
    Document d;d.dim_styles.push_back(style);WriteOptions options;options.units=Units::Metres;
    const auto output=write(d,v,options);const auto tape=lex(output);
    bool arrow=false,in_style=false;
    for(const auto& group:tape.groups) {
        if(group.code==0)in_style=group.value=="DIMSTYLE";
        if(in_style&&group.code==41){double size=0;CHECK(group.as_real(size));CHECK(std::abs(size-.003)<1e-10);arrow=true;}
    }
    CHECK(arrow);
}

TEST(nested_block_exchange_preserves_definition_graph_and_placements) {
    const auto v=vol();Document document;
    gdoc::BlockDef child;child.name="Child";child.base=gcm::to_lattice(v,1,1);
    gsnap::Shape line;line.a=child.base;line.b=gcm::to_lattice(v,2,1);child.geometry.push_back(line);document.blocks.define(child);
    gdoc::BlockDef parent;parent.name="Parent";gdoc::BlockRef inner;inner.name=child.name;
    inner.insertion=gcm::to_lattice(v,3,4);inner.scale_x=-2;inner.scale_y=3;inner.rotation=.5;inner.attrs.color=gdoc::Color::aci(2);
    parent.nested_refs.push_back(inner);document.blocks.define(parent);
    gdoc::BlockRef outer;outer.name=parent.name;outer.insertion=gcm::to_lattice(v,10,20);document.blocks.insert(outer);
    const auto text=write(document,v,WriteOptions{});CHECK(!text.empty());const auto back=read(text,v);CHECK(back.ok);
    const auto* result=back.document.blocks.find("Parent");CHECK(result!=nullptr);if(!result)return;
    CHECK(result->geometry.empty());CHECK(result->nested_refs.size()==1);if(result->nested_refs.empty())return;
    const auto& got=result->nested_refs.front();CHECK(got.name=="Child");CHECK(got.scale_x==-2);CHECK(got.scale_y==3);CHECK(std::abs(got.rotation-.5)<1e-8);
    CHECK(std::abs(got.insertion.x-inner.insertion.x)<=1);CHECK(std::abs(got.insertion.y-inner.insertion.y)<=1);CHECK(got.attrs.color.index==2);
    CHECK(back.document.blocks.references().size()==1);
    const auto expected=document.blocks.expand(outer,v),actual=back.document.blocks.expand(outer,v);CHECK(actual.size()==1);
    if(actual.size()==1){CHECK(std::abs(actual[0].b.x-expected[0].b.x)<=4);CHECK(std::abs(actual[0].b.y-expected[0].b.y)<=4);}
}

TEST(elliptical_arc_exchange_retains_its_parameter_interval) {
    const auto v=vol();Document document;gsnap::Shape arc;arc.kind=gsnap::ShapeKind::Ellipse;
    arc.a=gcm::to_lattice(v,1,2);arc.b=gcm::to_lattice(v,5,2);arc.radius=std::llround(2*v.scale());
    arc.start_angle=.5;arc.end_angle=2.5;document.entities.push_back({arc,"0","ELLIPSE"});
    const auto text=write(document,v,WriteOptions{});CHECK(!text.empty());const auto back=read(text,v);CHECK(back.ok);
    const auto& entities=back.document.entities;CHECK(entities.size()==1);if(entities.empty())return;
    CHECK(std::abs(entities[0].shape.start_angle-.5)<1e-8);CHECK(std::abs(entities[0].shape.end_angle-2.5)<1e-8);
}

TEST(dxf_block_notes_keep_ownership_formatting_style_and_appearance) {
    const auto v=vol(); Document document;
    gdoc::Layer layer; layer.name="Labels"; document.layers.add(layer);
    gtext::TextStyleDef style; style.name="Narrow"; style.width_factor=.7; document.text_styles.push_back(style);
    gdoc::BlockDef block; block.name="Title"; block.base=gcm::to_lattice(v,1,2);
    gdoc::BlockDef::Note note;
    note.pos=gcm::to_lattice(v,2,3); note.text="Project\\PSecond line";
    note.height=.003; note.width=.08; note.angle=.4; note.layer="Labels";
    note.halign=2; note.valign=3; note.style="Narrow";
    note.attrs.color=gdoc::Color::rgb(10,20,30); note.attrs.transparency=40; note.attrs.visible=false;
    block.notes={note}; document.blocks.define(block);
    Text loose; loose.text="Outside"; loose.height=.01; loose.attrs.color=gdoc::Color::aci(2);
    document.texts.push_back(loose);
    WriteOptions options; options.units=Units::Millimetres;
    const auto encoded=write(document,v,options); CHECK(!encoded.empty());
    const auto back=read(encoded,v); CHECK_MSG(back.ok,"%s",back.error.c_str());
    CHECK(back.document.texts.size()==1);
    if(!back.document.texts.empty()) {CHECK(back.document.texts[0].text=="Outside");CHECK(back.document.texts[0].attrs.color.index==2);}
    const auto* result=back.document.blocks.find("Title"); CHECK(result!=nullptr); if(!result)return;
    CHECK(result->notes.size()==1); if(result->notes.empty())return;
    const auto& got=result->notes[0];
    CHECK(got.text==note.text); CHECK(got.pos.x==note.pos.x); CHECK(got.pos.y==note.pos.y);
    CHECK(std::abs(got.height-.003)<1e-9); CHECK(std::abs(got.width-.08)<1e-9); CHECK(std::abs(got.angle-.4)<1e-9);
    CHECK(got.layer=="Labels"); CHECK(got.halign==2); CHECK(got.valign==3); CHECK(got.style=="Narrow");
    CHECK(got.attrs.color.kind==gdoc::Color::Kind::True); CHECK(got.attrs.color.r==10);
    CHECK(got.attrs.color.g==20); CHECK(got.attrs.color.b==30); CHECK(got.attrs.transparency==40); CHECK(!got.attrs.visible);
}

TEST(dxf_dimensions_reference_rendered_anonymous_blocks_even_without_user_blocks) {
    const auto v=vol();
    Document document;
    gdim::Dimension dim;
    dim.kind=gdim::Kind::Aligned;
    dim.p1=gcm::to_lattice(v,0,0); dim.p2=gcm::to_lattice(v,8,0);
    dim.p3=gcm::to_lattice(v,4,-.5); dim.text_override="Eight metres";
    document.dimensions.push_back(dim);
    WriteOptions options; options.units=Units::Metres; options.write_blocks=false;
    const auto encoded=write(document,v,options);
    CHECK(!encoded.empty());
    const auto tape=lex(encoded); CHECK(tape.ok);
    std::string block_name,dimension_block;
    bool block=false,dimension=false,in_dimension_subclass=false,has_geometry=false,has_text=false;
    for (const auto& group:tape.groups) {
        if (group.code==0) {
            block=group.value=="BLOCK";
            dimension=group.value=="DIMENSION";
            in_dimension_subclass=false;
            if (group.value=="LINE" && !block_name.empty() && dimension_block.empty()) has_geometry=true;
            if (group.value=="TEXT" && !block_name.empty() && dimension_block.empty()) has_text=true;
        }
        if (block && group.code==2) block_name=group.value;
        if (dimension && group.code==100 && group.value=="AcDbDimension") in_dimension_subclass=true;
        if (dimension && group.code==2) {CHECK(in_dimension_subclass);dimension_block=group.value;}
    }
    CHECK(!block_name.empty()); CHECK(dimension_block==block_name);
    CHECK(has_geometry); CHECK(has_text);
    const auto restored=read(encoded,v); CHECK(restored.ok);
    CHECK(restored.document.dimensions.size()==1);
    CHECK(restored.document.entities.empty()); CHECK(restored.document.texts.empty());
}

TEST(dxf_hatch_exports_separate_closed_paths_and_a_pattern_definition) {
    const auto v=vol(); Document document; HatchRec hatch;
    hatch.pattern="ANSI31"; hatch.scale=.25; hatch.angle=45;
    hatch.boundary={seg(v,0,0,8,0),seg(v,8,5,8,0),seg(v,8,5,0,5),
                    seg(v,0,0,0,5),circ(v,4,2.5,1)};
    hatch.seed=gcm::to_lattice(v,1,1); document.hatches.push_back(hatch);
    const auto encoded=write(document,v,WriteOptions{}); CHECK(!encoded.empty());
    const auto tape=lex(encoded); bool in_hatch=false; int paths=0,definitions=0;
    for (const auto& group:tape.groups) {
        if (group.code==0) in_hatch=group.value=="HATCH";
        if (!in_hatch) continue;
        if (group.code==91) group.as_int(paths);
        if (group.code==78) group.as_int(definitions);
    }
    CHECK(paths==2); CHECK(definitions>0);
    const auto restored=read(encoded,v); CHECK(restored.ok);
    CHECK(restored.document.hatches.size()==1);
    if (!restored.document.hatches.empty()) CHECK(restored.document.hatches[0].boundary.size()==5);
    document.hatches[0].boundary.pop_back();
    document.hatches[0].boundary.pop_back();
    CHECK(write(document,v,WriteOptions{}).empty());
}

TEST(dxf_shifted_dimension_extensions_export_static_without_changing_measurement) {
    const auto v=vol();
    for(auto kind:{gdim::Kind::Horizontal,gdim::Kind::Vertical,gdim::Kind::Aligned,gdim::Kind::Linear}) {
        Document d;gdim::Dimension dim;dim.kind=kind;
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
        const auto encoded=write(d,v,WriteOptions{});CHECK(!encoded.empty());
        const auto decoded=read(encoded,v);CHECK(decoded.ok);CHECK(decoded.document.dimensions.size()==1);
        if(decoded.document.dimensions.empty())continue;
        const auto& got=decoded.document.dimensions.front();CHECK(!got.associative());
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
