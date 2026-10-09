#include "test.hpp"

#include <cmath>
#include <string>

#include "exchange/step.hpp"
#include "geometry/boolean.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/extrude.hpp"

using namespace gstep;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

gtopo::Loop2 rect(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    return {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
            gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
}

gtopo::Body box(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1,
                f64 h, f64 z0 = 0.0) {
    gtopo::Profile p;
    p.outer = rect(v, x0, y0, x1, y1);
    p.normalize();
    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
    return gtopo::extrude(p, v, frame, h);
}

gtopo::Body prism(const gcm::WorkingVolume& v, f64 cx, f64 cy, f64 r, f64 h,
                  int sides, f64 z0 = 0.0) {
    gtopo::Loop2 loop;
    for (int i = 0; i < sides; ++i) {
        const f64 a = 2.0 * kPi * static_cast<f64>(i) / sides;
        loop.push_back(gcm::to_lattice(v, cx + r * std::cos(a),
                                       cy + r * std::sin(a)));
    }
    gtopo::Profile p;
    p.outer = loop;
    p.normalize();
    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
    gtopo::Body b = gtopo::extrude(p, v, frame, h);

    b.face_surface.assign(b.face_count(), gtopo::Surface{});
    for (gcm::u32 f = 0; f < b.face_count(); ++f) {
        const gtopo::Plane pl = gtopo::face_plane(b, f, v);
        if (pl.valid() && std::fabs(pl.nz) < 1e-6)
            b.face_surface[f] = gtopo::Surface::cylinder(cx, cy, 0, 0, 0, 1, r);
    }
    return b;
}

}

TEST(step_splits_arguments_respecting_nesting_and_quotes) {
    const auto a = split_arguments("'a,b',#3,(1.,2.,3.),.T.");
    CHECK_MSG(a.size() == 4, "%zu arguments", a.size());
    CHECK(a[0] == "'a,b'");
    CHECK(a[1] == "#3");
    CHECK(a[2] == "(1.,2.,3.)");
    CHECK(a[3] == ".T.");

    const auto nested = split_arguments("(#1,#2),(#3,(#4,#5))");
    CHECK(nested.size() == 2);
    CHECK(nested[1] == "(#3,(#4,#5))");

    const auto quoted = split_arguments("'it''s, fine',#9");
    CHECK_MSG(quoted.size() == 2, "%zu arguments", quoted.size());
    CHECK(quoted[0] == "'it''s, fine'");
}

TEST(step_strings_round_trip_including_the_awkward_ones) {
    for (const char* s : {"Part", "it's", "a,b", "line\nbreak",
                          "caf\xc3\xa9", "", "back\\slash"}) {
        const std::string encoded = encode_string(s);
        CHECK_MSG(encoded.front() == '\'' && encoded.back() == '\'',
                  "'%s' was not quoted", s);
        const std::string back = decode_string(encoded);
        CHECK_MSG(back == s, "'%s' came back as '%s'", s, back.c_str());
    }
}

TEST(step_encoding_keeps_non_ascii_off_the_wire_as_raw_bytes) {
    const std::string encoded = encode_string("caf\xc3\xa9");
    for (char c : encoded)
        CHECK_MSG(static_cast<unsigned char>(c) < 0x80,
                  "a raw high byte survived encoding");
    CHECK(encoded.find("\\X2\\") != std::string::npos);
}

TEST(step_parses_a_minimal_file) {
    const ParseResult p = parse(
        "ISO-10303-21;\n"
        "HEADER;\n"
        "FILE_NAME('Widget','1980-01-01T00:00:00',(''),(''),'','','');\n"
        "FILE_SCHEMA(('AUTOMOTIVE_DESIGN'));\n"
        "ENDSEC;\n"
        "DATA;\n"
        "#1 = CARTESIAN_POINT('',(0.,0.,0.));\n"
        "#2 = DIRECTION('',(0.,0.,1.));\n"
        "ENDSEC;\n"
        "END-ISO-10303-21;\n");
    CHECK_MSG(p.ok, "%s", p.error.c_str());
    CHECK(p.instances.size() == 2);
    CHECK(p.find(1) != nullptr && p.find(1)->type == "CARTESIAN_POINT");
    CHECK(p.find(2) != nullptr);
    CHECK(p.find(99) == nullptr);
    CHECK(p.name == "Widget");
    CHECK(p.schema.find("AUTOMOTIVE_DESIGN") != std::string::npos);
}

TEST(step_refuses_a_file_that_is_not_step) {
    const ParseResult p = parse("this is not a step file");
    CHECK(!p.ok);
    CHECK(!p.error.empty());
}

TEST(step_handles_a_reference_that_points_forward) {
    const ParseResult p = parse(
        "ISO-10303-21;\nDATA;\n"
        "#2 = VERTEX_POINT('',#1);\n"
        "#1 = CARTESIAN_POINT('',(1.,2.,3.));\n"
        "ENDSEC;\nEND-ISO-10303-21;\n");
    CHECK(p.ok);
    CHECK(p.find(1) != nullptr);
    CHECK(p.find(2) != nullptr);
}

TEST(step_skips_comments_including_ones_that_span_lines) {
    const ParseResult p = parse(
        "ISO-10303-21;\nDATA;\n"
        "/* this comment\n   spans two lines */\n"
        "#1 = CARTESIAN_POINT('',(0.,0.,0.));\n"
        "ENDSEC;\nEND-ISO-10303-21;\n");
    CHECK_MSG(p.ok, "%s", p.error.c_str());
    CHECK_MSG(p.instances.size() == 1, "%zu instances survived",
              p.instances.size());
}

TEST(step_a_semicolon_inside_a_string_ends_nothing) {
    const ParseResult p = parse(
        "ISO-10303-21;\nDATA;\n"
        "#1 = PRODUCT('a;b','c',#2,());\n"
        "ENDSEC;\nEND-ISO-10303-21;\n");
    CHECK(p.ok);
    CHECK_MSG(p.instances.size() == 1, "%zu instances", p.instances.size());
    CHECK(p.find(1)->args[0] == "'a;b'");
}

TEST(step_unit_conversions) {
    CHECK(std::fabs(metres_per(LengthUnit::Metre) - 1.0) < 1e-15);
    CHECK(std::fabs(metres_per(LengthUnit::Millimetre) - 0.001) < 1e-15);
    CHECK(std::fabs(metres_per(LengthUnit::Inch) - 0.0254) < 1e-15);
    for (LengthUnit u : {LengthUnit::Metre, LengthUnit::Millimetre,
                         LengthUnit::Centimetre, LengthUnit::Inch,
                         LengthUnit::Foot})
        CHECK(length_unit_name(u)[0] != '\0');
}

TEST(step_writes_the_unit_it_says_it_wrote) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 0.010, 0.020, 0.005);

    WriteOptions mm;
    mm.unit = LengthUnit::Millimetre;
    const std::string as_mm = write(b, v, mm);
    CHECK(as_mm.find(".MILLI.") != std::string::npos);

    const ParseResult parsed = parse(as_mm);
    CHECK(parsed.ok);
    f64 largest = 0.0;
    for (const Instance& i : parsed.instances) {
        if (i.type != "CARTESIAN_POINT" || i.args.size() < 2) continue;
        const std::string& list = i.args[1];
        for (const std::string& c :
             split_arguments(list.substr(1, list.size() - 2)))
            largest = std::max(largest, std::fabs(std::stod(c)));
    }
    CHECK_MSG(std::fabs(largest - 20.0) < 1e-3,
              "the largest millimetre coordinate is %g, not 20", largest);

    WriteOptions m;
    m.unit = LengthUnit::Metre;
    const std::string as_m = write(b, v, m);
    CHECK(as_m.find(".MILLI.") == std::string::npos);
    CHECK(as_m.find("SI_UNIT($,.METRE.)") != std::string::npos);
}

TEST(step_reads_back_the_unit_and_scales_by_it) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 0.010, 0.020, 0.005);

    for (LengthUnit u : {LengthUnit::Metre, LengthUnit::Millimetre,
                         LengthUnit::Centimetre, LengthUnit::Inch, LengthUnit::Foot}) {
        WriteOptions o;
        o.unit = u;
        const auto encoded=write(b,v,o);
        if (u==LengthUnit::Inch || u==LengthUnit::Foot)
            CHECK(encoded.find("DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.)")!=std::string::npos);
        const ReadResult r = read(encoded, v);
        CHECK_MSG(r.ok, "%s: %s", length_unit_name(u), r.error.c_str());
        CHECK_MSG(r.unit_stated, "%s was not stated in the file",
                  length_unit_name(u));
        CHECK_MSG(r.unit == u, "wrote %s and read %s", length_unit_name(u),
                  length_unit_name(r.unit));
        CHECK(r.bodies.size() == 1);

        const f64 want = gtopo::volume(b, v);
        const f64 got = gtopo::volume(r.bodies[0], v);
        CHECK_MSG(std::fabs(got - want) < std::fabs(want) * 1e-6 + 1e-12,
                  "%s: volume %g, expected %g", length_unit_name(u), got, want);
    }
}

TEST(step_a_box_survives_a_round_trip) {
    const auto v = vol();
    const gtopo::Body b = box(v, -1.0, -2.0, 3.0, 4.0, 5.0);

    const std::string text = write(b, v);
    CHECK(text.rfind("ISO-10303-21;", 0) == 0);
    CHECK(text.find("END-ISO-10303-21;") != std::string::npos);
    CHECK(text.find("ADVANCED_BREP_SHAPE_REPRESENTATION") != std::string::npos);
    CHECK(text.find("MANIFOLD_SOLID_BREP") != std::string::npos);
    CHECK(text.find("CLOSED_SHELL") != std::string::npos);

    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.bodies.size() == 1, "%zu bodies", r.bodies.size());

    const gtopo::Body& back = r.bodies[0];
    CHECK_MSG(back.face_count() == b.face_count(), "%u faces, expected %u",
              back.face_count(), b.face_count());
    CHECK_MSG(back.vertex_count() == b.vertex_count(), "%u vertices, expected %u",
              back.vertex_count(), b.vertex_count());
    CHECK_MSG(std::fabs(gtopo::volume(back, v) - gtopo::volume(b, v)) < 1e-6,
              "volume %g, expected %g", gtopo::volume(back, v),
              gtopo::volume(b, v));

    const gtopo::Topology t = back.analyse();
    CHECK_MSG(t.closed, "the imported solid is not closed");
    CHECK_MSG(t.manifold, "the imported solid is not manifold");
}

TEST(step_a_solid_does_not_import_inside_out) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 4, 3, 2);
    const ReadResult r = read(write(b, v), v);
    CHECK(r.ok && r.bodies.size() == 1);
    CHECK_MSG(gtopo::volume(r.bodies[0], v) > 0.0,
              "the imported solid has volume %g", gtopo::volume(r.bodies[0], v));
}

TEST(step_one_edge_is_written_once_and_shared) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 4, 3, 2);
    const std::string text = write(b, v);

    std::size_t curves = 0, oriented = 0;
    for (std::size_t at = text.find("EDGE_CURVE"); at != std::string::npos;
         at = text.find("EDGE_CURVE", at + 1))
        ++curves;
    for (std::size_t at = text.find("ORIENTED_EDGE"); at != std::string::npos;
         at = text.find("ORIENTED_EDGE", at + 1))
        ++oriented;

    CHECK_MSG(curves == 12, "%zu edge curves for a box", curves);
    CHECK_MSG(oriented == 24, "%zu oriented edges", oriented);
}

TEST(step_a_hole_stays_a_hole) {
    const auto v = vol();
    gtopo::Profile p;
    p.outer = rect(v, -5, -5, 5, 5);
    p.holes.push_back(rect(v, -1, -1, 1, 1));
    p.normalize();
    const gtopo::Body b =
        gtopo::extrude(p, v, ggeo::d3::Frame<gcm::F64>::world_xy(), 2.0);

    const std::string text = write(b, v);
    CHECK_MSG(text.find("FACE_BOUND") != std::string::npos,
              "the hole was written as an outer bound");

    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.bodies.size() == 1);
    CHECK_MSG(std::fabs(gtopo::volume(r.bodies[0], v) - gtopo::volume(b, v)) <
                  1e-6,
              "the hole did not survive: %g against %g",
              gtopo::volume(r.bodies[0], v), gtopo::volume(b, v));
    CHECK(r.bodies[0].analyse().closed);
}

TEST(step_exports_a_cylinder_as_a_cylinder) {
    const auto v = vol();
    const gtopo::Body b = prism(v, 0, 0, 2.0, 4.0, 16);
    CHECK(b.any_face_curved());

    const std::string text = write(b, v);
    CHECK_MSG(text.find("CYLINDRICAL_SURFACE") != std::string::npos,
              "the cylinder was exported as planes");
    CHECK(text.find("CIRCLE('") != std::string::npos);
    CHECK(text.find("SHAPE_DEFINITION_REPRESENTATION") != std::string::npos);

    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.bodies.size() == 1);
    CHECK(r.bodies[0].analyse().closed);
    CHECK(r.bodies[0].analyse().manifold);
    CHECK_MSG(std::fabs(gtopo::volume(r.bodies[0],v)-kPi*2*2*4)<0.01,
              "cylinder volume %.12g",gtopo::volume(r.bodies[0],v));
    CHECK_MSG(r.bodies[0].any_face_curved(),
              "the cylinder did not come back");

    std::size_t cylindrical = 0;
    for (gcm::u32 f = 0; f < r.bodies[0].face_count(); ++f) {
        const gtopo::Surface* s = r.bodies[0].surface_of(f);
        if (!s || s->kind != gtopo::SurfaceKind::Cylinder) continue;
        ++cylindrical;
        CHECK_MSG(std::fabs(s->radius - 2.0) < 1e-6,
                  "the imported radius is %g", s->radius);
    }
    CHECK_MSG(cylindrical >= 16, "%zu cylindrical faces came back", cylindrical);
}

TEST(step_can_be_told_to_export_without_the_exact_surfaces) {
    const auto v = vol();
    const gtopo::Body b = prism(v, 0, 0, 2.0, 4.0, 12);
    WriteOptions flat;
    flat.exact_surfaces = false;
    const std::string text = write(b, v, flat);
    CHECK(text.find("CYLINDRICAL_SURFACE") == std::string::npos);
    const ReadResult r = read(text, v);
    CHECK(r.ok && r.bodies.size() == 1);
    CHECK(r.bodies[0].analyse().closed);
}

TEST(step_an_unknown_entity_is_skipped_not_fatal) {
    const auto v = vol();
    std::string text = write(box(v, 0, 0, 4, 3, 2), v);
    const std::size_t at = text.find("DATA;");
    CHECK(at != std::string::npos);
    text.insert(at + 5,
                "\n#90001 = SOME_FUTURE_ENTITY('x',(1.,2.),#90002);"
                "\n#90002 = ANOTHER_ONE(.UNSET.);");

    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.bodies.size() == 1, "the solid was lost with the unknowns");
    CHECK_MSG(r.unknown_entities >= 2, "%zu unknown entities counted",
              r.unknown_entities);
}

TEST(step_a_file_with_no_solid_says_so_rather_than_returning_nothing_quietly) {
    const auto v = vol();
    const ReadResult r = read(
        "ISO-10303-21;\nDATA;\n"
        "#1 = CARTESIAN_POINT('',(0.,0.,0.));\n"
        "ENDSEC;\nEND-ISO-10303-21;\n", v);
    CHECK(r.ok);
    CHECK(r.bodies.empty());
    CHECK_MSG(!r.error.empty(), "an empty import said nothing");
}

TEST(step_export_is_deterministic) {
    const auto v = vol();
    const gtopo::Body b = box(v, 0, 0, 4, 3, 2);
    CHECK(write(b, v) == write(b, v));
}

TEST(step_the_product_name_reaches_the_header_and_comes_back) {
    const auto v = vol();
    WriteOptions o;
    o.product_name = "Bracket, left hand";
    const std::string text = write(box(v, 0, 0, 1, 1, 1), v, o);
    const ReadResult r = read(text, v);
    CHECK(r.ok);
    CHECK_MSG(r.product_name == o.product_name, "the name came back as '%s'",
              r.product_name.c_str());
    CHECK(r.schema.find("AUTOMOTIVE_DESIGN") != std::string::npos);
}

TEST(step_reals_always_carry_a_decimal_point) {
    const auto v = vol();
    const std::string text = write(box(v, 0, 0, 1, 1, 1), v);
    const ParseResult p = parse(text);
    CHECK(p.ok);
    for (const Instance& i : p.instances) {
        if (i.type != "CARTESIAN_POINT" || i.args.size() < 2) continue;
        const std::string& coords = i.args[1];
        for (const std::string& c :
             split_arguments(coords.substr(1, coords.size() - 2)))
            CHECK_MSG(c.find('.') != std::string::npos ||
                          c.find('E') != std::string::npos,
                      "the coordinate '%s' has no decimal point", c.c_str());
    }
}

TEST(step_a_body_with_a_hole_cut_by_a_boolean_round_trips) {
    const auto v = vol();
    const gtopo::Body block = box(v, -5, -5, 5, 5, 4);
    const gtopo::Body pin = prism(v, 0, 0, 2.0, 6.0, 16, -1.0);
    const gtopo::CurvedBoolResult cut =
        gtopo::boolean_curved(block, pin, gtopo::BoolOp::Subtract, v);
    CHECK_MSG(cut.ok(), "%s", cut.message());

    const ReadResult r = read(write(cut.body, v), v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.bodies.size() == 1);
    CHECK_MSG(std::fabs(gtopo::volume(r.bodies[0], v) -(400-kPi*2*2*4)) < 0.01,
              "the bore's volume changed: %g",gtopo::volume(r.bodies[0], v));
    CHECK(r.bodies[0].analyse().closed); CHECK(r.bodies[0].analyse().manifold);
    CHECK_MSG(r.bodies[0].any_face_curved(),
              "the bore lost its cylinder on the way through STEP");
}

TEST(step_follows_a_curved_edge_round_instead_of_cutting_across_it) {
    const auto v = vol();
    const std::string text =
        "ISO-10303-21;\nHEADER;\n"
        "FILE_NAME('Arc','',(''),(''),'','','');\n"
        "FILE_SCHEMA(('AUTOMOTIVE_DESIGN'));\nENDSEC;\nDATA;\n"
        "#1 = CARTESIAN_POINT('',(0.,0.,0.));\n"      // arc start
        "#2 = CARTESIAN_POINT('',(2.,0.,0.));\n"      // arc end
        "#3 = CARTESIAN_POINT('',(2.,-2.,0.));\n"
        "#4 = CARTESIAN_POINT('',(0.,-2.,0.));\n"
        "#5 = CARTESIAN_POINT('',(1.,0.,0.));\n"      // arc centre
        "#6 = DIRECTION('',(0.,0.,1.));\n"            // arc axis
        "#7 = DIRECTION('',(1.,0.,0.));\n"            // arc ref
        "#8 = AXIS2_PLACEMENT_3D('',#5,#6,#7);\n"
        "#9 = CIRCLE('',#8,1.);\n"
        "#10 = VERTEX_POINT('',#1);\n"
        "#11 = VERTEX_POINT('',#2);\n"
        "#12 = VERTEX_POINT('',#3);\n"
        "#13 = VERTEX_POINT('',#4);\n"
        "#20 = EDGE_CURVE('',#10,#11,#9,.T.);\n"      // the ARC
        "#15 = VECTOR('',#7,1.);\n"
        "#16 = LINE('',#1,#15);\n"
        "#21 = EDGE_CURVE('',#11,#12,#16,.T.);\n"
        "#22 = EDGE_CURVE('',#12,#13,#16,.T.);\n"
        "#23 = EDGE_CURVE('',#13,#10,#16,.T.);\n"
        "#30 = ORIENTED_EDGE('',*,*,#20,.T.);\n"
        "#31 = ORIENTED_EDGE('',*,*,#21,.T.);\n"
        "#32 = ORIENTED_EDGE('',*,*,#22,.T.);\n"
        "#33 = ORIENTED_EDGE('',*,*,#23,.T.);\n"
        "#40 = EDGE_LOOP('',(#30,#31,#32,#33));\n"
        "#41 = FACE_OUTER_BOUND('',#40,.T.);\n"
        "#50 = CARTESIAN_POINT('',(0.,0.,0.));\n"
        "#51 = AXIS2_PLACEMENT_3D('',#50,#6,#7);\n"
        "#52 = PLANE('',#51);\n"
        "#60 = ADVANCED_FACE('',(#41),#52,.T.);\n"
        "#70 = CLOSED_SHELL('',(#60));\n"
        "ENDSEC;\nEND-ISO-10303-21;\n";

    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(!r.bodies.empty(), "nothing imported");
    if (r.bodies.empty()) return;

    CHECK_MSG(r.bodies[0].vertex_count() > 6,
              "the arc imported as %u vertices -- still a chord",
              r.bodies[0].vertex_count());

    std::size_t on_the_arc = 0;
    for (const auto& p : r.bodies[0].vertex_point) {
        f64 x, y, z;
        gcm::to_world(v, p, x, y, z);
        if (std::fabs(z) > 1e-9) continue;
        const f64 d = std::hypot(x - 0.001, y);
        if (std::fabs(d - 0.001) < 1e-6 && std::fabs(y) > 1e-6) ++on_the_arc;
    }
    CHECK_MSG(on_the_arc >= 3, "%zu of %u vertices sit on the arc",
              on_the_arc, r.bodies[0].vertex_count());

    bool said = false;
    for (const auto& note : r.skipped)
        if (note.find("curved edge") != std::string::npos) said = true;
    CHECK_MSG(said, "the reader did not report following the arcs");
}

TEST(step_carries_cones_and_tori_as_the_surfaces_they_are) {
    const auto v = vol();
    gtopo::Body b = box(v, 0, 0, 4, 3, 2);
    CHECK(b.face_count() >= 6);
    b.face_surface.assign(b.face_count(), gtopo::Surface{});
    b.face_surface[0] = gtopo::Surface::cone(1.0, 1.0, -1.0, 0.0, 0.0, 1.0, 0.4);
    b.face_surface[1] = gtopo::Surface::torus(2.0, 1.5, 1.0, 0.0, 0.0, 1.0, 0.9, 0.2);

    WriteOptions o;
    o.exact_surfaces = true;
    const std::string text = write(b, v, o);
    CHECK_MSG(text.find("CONICAL_SURFACE") != std::string::npos,
              "the cone went out as facets");
    CHECK_MSG(text.find("TOROIDAL_SURFACE") != std::string::npos,
              "the torus went out as facets");

    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.bodies.size() == 1);
    if (r.bodies.empty()) return;
    std::size_t cones = 0, tori = 0;
    for (const auto& sf : r.bodies[0].face_surface) {
        if (sf.kind == gtopo::SurfaceKind::Cone) ++cones;
        if (sf.kind == gtopo::SurfaceKind::Torus) ++tori;
    }
    CHECK_MSG(cones == 1, "%zu cones came back", cones);
    CHECK_MSG(tori == 1, "%zu tori came back", tori);

    for (const auto& sf : r.bodies[0].face_surface)
        if (sf.kind == gtopo::SurfaceKind::Cone) {
            CHECK_MSG(std::fabs(sf.ox - 1.0) < 1e-9 && std::fabs(sf.oz + 1.0) < 1e-9,
                      "the apex came back at %.6f, %.6f, %.6f", sf.ox, sf.oy, sf.oz);
            CHECK(std::fabs(sf.half_angle - 0.4) < 1e-9);
        }
}
