#include "test.hpp"

#include <cmath>

#include "geometry/annotation.hpp"

using namespace gdim;

namespace {

Volume vol() {
    Volume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
Point at(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }
f64 wx(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    (void)y;
    return x;
}
f64 wy(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    (void)x;
    return y;
}

}

TEST(annotation_scale_factor_is_drawing_over_paper) {
    const AnnotationScale* s = find_scale("1:50");
    CHECK(s != nullptr);
    CHECK_MSG(std::fabs(s->factor() - 50.0) < 1e-12, "1:50 gave %.6f", s->factor());

    const AnnotationScale* up = find_scale("2:1");
    CHECK(up != nullptr);
    CHECK(std::fabs(up->factor() - 0.5) < 1e-12);
}

TEST(annotation_imperial_scales_are_correct) {
    const AnnotationScale* q = find_scale("1/4\" = 1'-0\"");
    CHECK(q != nullptr);
    CHECK_MSG(std::fabs(q->factor() - 48.0) < 1e-9, "got %.6f", q->factor());

    const AnnotationScale* e = find_scale("1/8\" = 1'-0\"");
    CHECK(std::fabs(e->factor() - 96.0) < 1e-9);
}

TEST(annotation_size_is_paper_size_times_the_factor) {
    CHECK(std::fabs(annotative_size(0.0025, 50.0) - 0.125) < 1e-12);
    CHECK(std::fabs(annotative_size(0.0025, 1.0) - 0.0025) < 1e-12);
    CHECK(std::fabs(annotative_size(0.0025, 0.0) - 0.0025) < 1e-12);
}

TEST(annotation_scale_list_add_find_remove) {
    ScaleList l;
    CHECK(l.size() > 10);
    CHECK(l.find("1:100") != nullptr);
    CHECK(l.find("1:37") == nullptr);

    l.add({"1:37", 1.0, 37.0});
    CHECK(l.find("1:37") != nullptr);
    CHECK(l.remove("1:37"));
    CHECK(l.find("1:37") == nullptr);

    CHECK_MSG(!l.remove("1:1"), "1:1 was removed");
}

TEST(annotation_current_scale_drives_the_factor) {
    ScaleList l;
    CHECK(std::fabs(l.current_factor() - 1.0) < 1e-12);
    l.set_current("1:200");
    CHECK(l.current() == "1:200");
    CHECK(std::fabs(l.current_factor() - 200.0) < 1e-12);

    l.set_current("1:nonsense");
    CHECK(l.current() == "1:200");
}

TEST(annotation_object_carries_its_scales) {
    Annotative a;
    CHECK(!a.enabled);
    a.add_scale("1:50");
    a.add_scale("1:100");
    a.add_scale("1:50");
    CHECK(a.enabled);
    CHECK(a.scales.size() == 2);
    CHECK(a.appears_at("1:50"));
    CHECK(!a.appears_at("1:20"));
    CHECK(a.remove_scale("1:50"));
    CHECK(!a.appears_at("1:50"));
    CHECK(!a.remove_scale("1:50"));
}

TEST(annotation_baseline_measures_from_one_origin) {
    const Volume v = vol();
    Style s;
    Chain c;
    c.origin = at(v, 0, 0);
    c.points = {at(v, 2, 0), at(v, 5, 0), at(v, 9, 0)};
    c.location = at(v, 0, 3);

    const ChainResult r = baseline(c, s, v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK(r.dimensions.size() == 3);

    for (const auto& d : r.dimensions) {
        CHECK(d.p1.x == c.origin.x && d.p1.y == c.origin.y);
    }
    CHECK(std::fabs(measure(r.dimensions[0], v) - 2.0) < 1e-4);
    CHECK(std::fabs(measure(r.dimensions[1], v) - 5.0) < 1e-4);
    CHECK(std::fabs(measure(r.dimensions[2], v) - 9.0) < 1e-4);
}

TEST(annotation_baseline_stacks_its_dimension_lines) {
    const Volume v = vol();
    Style s;
    Chain c;
    c.origin = at(v, 0, 0);
    c.points = {at(v, 2, 0), at(v, 5, 0)};
    c.location = at(v, 0, 3);
    c.spacing = 10.0;

    const ChainResult r = baseline(c, s, v);
    CHECK(r.ok);
    const f64 y0 = wy(v, r.dimensions[0].p3);
    const f64 y1 = wy(v, r.dimensions[1].p3);
    CHECK_MSG(y1 > y0, "the second dimension line did not step out (%.4f, %.4f)",
              y0, y1);
    CHECK_MSG(std::fabs((y1 - y0) - 0.010) < 1e-5, "step was %.6f", y1 - y0);
}

TEST(annotation_baseline_stacks_away_from_the_geometry) {
    const Volume v = vol();
    Style s;
    Chain c;
    c.origin = at(v, 0, 0);
    c.points = {at(v, 2, 0), at(v, 5, 0)};
    c.location = at(v, 0, -3);

    const ChainResult r = baseline(c, s, v);
    CHECK(r.ok);
    CHECK_MSG(wy(v, r.dimensions[1].p3) < wy(v, r.dimensions[0].p3),
              "the chain stacked back toward the geometry");
}

TEST(annotation_continued_runs_end_to_end) {
    const Volume v = vol();
    Style s;
    Chain c;
    c.origin = at(v, 0, 0);
    c.points = {at(v, 2, 0), at(v, 5, 0), at(v, 9, 0)};
    c.location = at(v, 0, 3);

    const ChainResult r = continued(c, s, v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK(r.dimensions.size() == 3);

    CHECK(std::fabs(measure(r.dimensions[0], v) - 2.0) < 1e-4);
    CHECK_MSG(std::fabs(measure(r.dimensions[1], v) - 3.0) < 1e-4,
              "second span measured %.4f", measure(r.dimensions[1], v));
    CHECK(std::fabs(measure(r.dimensions[2], v) - 4.0) < 1e-4);
}

TEST(annotation_continued_shares_one_dimension_line) {
    const Volume v = vol();
    Style s;
    Chain c;
    c.origin = at(v, 0, 0);
    c.points = {at(v, 2, 0), at(v, 5, 0)};
    c.location = at(v, 0, 3);

    const ChainResult r = continued(c, s, v);
    CHECK(r.ok);
    for (const auto& d : r.dimensions)
        CHECK_MSG(d.p3.y == c.location.y, "a continued run broke its line");
}

TEST(annotation_a_chain_with_nothing_to_measure_says_so) {
    const Volume v = vol();
    Style s;
    Chain c;
    c.origin = at(v, 0, 0);
    CHECK(!baseline(c, s, v).ok);
    CHECK(!continued(c, s, v).ok);
    CHECK(std::string(baseline(c, s, v).reason).size() > 0);
}

TEST(annotation_chain_members_are_ordinary_dimensions) {
    const Volume v = vol();
    Style s;
    Chain c;
    c.kind = Kind::Aligned;
    c.style = "Metric";
    c.origin = at(v, 0, 0);
    c.points = {at(v, 3, 4)};
    c.location = at(v, 0, 2);

    const ChainResult r = baseline(c, s, v);
    CHECK(r.ok);
    CHECK(r.dimensions[0].kind == Kind::Aligned);
    CHECK(r.dimensions[0].style == "Metric");
    CHECK(std::fabs(measure(r.dimensions[0], v) - 5.0) < 1e-4);
}

TEST(annotation_multileader_draws_leader_landing_and_text) {
    const Volume v = vol();
    MLeaderStyle s;
    MLeader m;
    m.lines = {{at(v, 0, 0), at(v, 1, 1)}};
    m.landing = at(v, 1, 1);
    m.text = "TYP. 3 PLACES";

    const MLeaderGeometry g = build(m, s, v);
    CHECK_MSG(g.ok, "%s", g.reason);
    CHECK_MSG(!g.lines.empty(), "no leader line was drawn");
    CHECK_MSG(!g.arrows.empty(), "no arrowhead was drawn");
    CHECK(g.text == "TYP. 3 PLACES");
    CHECK(g.text_height > 0.0);
    CHECK_MSG(wx(v, g.text_at) > 1.0, "text landed at x=%.4f", wx(v, g.text_at));
}

TEST(annotation_multileader_carries_several_leaders_to_one_note) {
    const Volume v = vol();
    MLeaderStyle s;
    MLeader m;
    m.landing = at(v, 5, 5);
    for (int i = 0; i < 5; ++i)
        m.lines.push_back({at(v, i, 0), at(v, 5, 5)});
    m.text = "BRACKET TYPE B";

    const MLeaderGeometry g = build(m, s, v);
    CHECK(g.ok);
    CHECK_MSG(g.arrows.size() >= 5, "expected five arrowheads, got %zu",
              g.arrows.size());
}

TEST(annotation_multileader_ends_every_leader_at_the_shared_landing) {
    const Volume v = vol();
    MLeaderStyle s;
    s.landing = false;
    MLeader m;
    m.landing = at(v, 5, 5);
    m.lines = {{at(v, 0, 0), at(v, 9, 9)}, {at(v, 2, 0), at(v, 1, 1)}};

    const MLeaderGeometry g = build(m, s, v);
    CHECK(g.ok);
    for (const auto& l : g.lines) {
        const bool ends_at_landing =
            (std::fabs(wx(v, l.b) - 5.0) < 1e-4 && std::fabs(wy(v, l.b) - 5.0) < 1e-4);
        CHECK_MSG(ends_at_landing, "a leader ended at (%.3f, %.3f)",
                  wx(v, l.b), wy(v, l.b));
    }
}

TEST(annotation_multileader_text_can_sit_left) {
    const Volume v = vol();
    MLeaderStyle s;
    MLeader m;
    m.lines = {{at(v, 10, 0), at(v, 5, 5)}};
    m.landing = at(v, 5, 5);
    m.text = "NOTE";
    m.text_left = true;

    const MLeaderGeometry g = build(m, s, v);
    CHECK(g.ok);
    CHECK_MSG(wx(v, g.text_at) < 5.0, "left-attached text landed at %.4f",
              wx(v, g.text_at));
}

TEST(annotation_multileader_without_a_leader_is_refused) {
    const Volume v = vol();
    MLeaderStyle s;
    MLeader m;
    m.text = "orphan";
    const MLeaderGeometry g = build(m, s, v);
    CHECK(!g.ok);
    CHECK(std::string(g.reason).size() > 0);
}

TEST(annotation_multileader_style_scale_reaches_the_geometry) {
    const Volume v = vol();
    MLeader m;
    m.lines = {{at(v, 0, 0), at(v, 1, 1)}};
    m.landing = at(v, 1, 1);
    m.text = "X";

    MLeaderStyle a;
    MLeaderStyle b;
    b.scale = 10.0;
    CHECK(build(m, b, v).text_height > build(m, a, v).text_height * 9.0);
}

TEST(annotation_multileader_style_table) {
    MLeaderStyleTable t;
    CHECK(t.find("Standard") != nullptr);
    MLeaderStyle s;
    s.name = "Keynote";
    s.arrow = ArrowType::Dot;
    t.add(s);
    CHECK(t.find("Keynote") != nullptr);
    CHECK(t.find("Keynote")->arrow == ArrowType::Dot);
    t.set_current("Keynote");
    CHECK(t.current() == "Keynote");
    t.set_current("Nope");
    CHECK(t.current() == "Keynote");
}

TEST(annotation_gdt_symbols_are_the_real_characters) {
    CHECK(std::string(symbol_glyph(GeoSymbol::Position)) != "P");
    CHECK(std::string(symbol_glyph(GeoSymbol::Perpendicularity)) == "\xE2\x8A\xA5");
    CHECK(std::string(symbol_glyph(GeoSymbol::Parallelism)) == "\xE2\x88\xA5");
    CHECK(std::string(symbol_glyph(GeoSymbol::Circularity)) == "\xE2\x97\x8B");
    CHECK(std::string(symbol_name(GeoSymbol::TotalRunout)) == "Total Runout");

    CHECK(std::string(material_glyph(Material::RFS)).empty());
    CHECK(!std::string(material_glyph(Material::MMC)).empty());
}

TEST(annotation_feature_control_frame_builds_its_cells) {
    const Volume v = vol();
    FeatureControlFrame f;
    f.at = at(v, 0, 0);
    f.height = 0.005;
    GeoSegment g;
    g.symbol = GeoSymbol::Position;
    g.tolerance = "0.25";
    g.diameter = true;
    g.material = Material::MMC;
    g.primary = {"A", Material::RFS};
    g.secondary = {"B", Material::MMC};
    g.tertiary = {"C", Material::RFS};
    f.segments.push_back(g);

    const FrameGeometry r = build(f, v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK_MSG(r.cells.size() == 5, "expected five cells, got %zu", r.cells.size());
    CHECK(r.width > 0.0);
    CHECK(std::fabs(r.height - 0.005) < 1e-9);

    CHECK_MSG(r.lines.size() == 20, "%zu lines", r.lines.size());

    CHECK(r.cells[1].text.find("0.25") != std::string::npos);
    CHECK(r.cells[1].text.find("\xE2\x8C\x80") != std::string::npos);
    CHECK(r.cells[1].text.find("\xE2\x93\x82") != std::string::npos);
}

TEST(annotation_frame_omits_unused_datum_columns) {
    const Volume v = vol();
    FeatureControlFrame f;
    f.at = at(v, 0, 0);
    GeoSegment g;
    g.symbol = GeoSymbol::Flatness;
    g.tolerance = "0.05";
    f.segments.push_back(g);

    const FrameGeometry r = build(f, v);
    CHECK(r.ok);
    CHECK_MSG(r.cells.size() == 2, "expected symbol and tolerance only, got %zu",
              r.cells.size());
    CHECK(r.lines.size() == 8);
}

TEST(annotation_composite_frame_stacks_and_aligns) {
    const Volume v = vol();
    FeatureControlFrame f;
    f.at = at(v, 0, 0);
    f.height = 0.004;

    GeoSegment a;
    a.symbol = GeoSymbol::Position;
    a.tolerance = "0.5";
    a.primary = {"A"};
    a.secondary = {"B"};
    a.tertiary = {"C"};
    GeoSegment b;
    b.symbol = GeoSymbol::Position;
    b.tolerance = "0.125";
    b.primary = {"A"};
    f.segments = {a, b};

    const FrameGeometry r = build(f, v);
    CHECK(r.ok);
    CHECK_MSG(std::fabs(r.height - 0.008) < 1e-9, "height %.6f", r.height);

    f64 xs[2] = {0, 0};
    int found = 0;
    for (const auto& c : r.cells)
        if (c.text.find("0.5") != std::string::npos ||
            c.text.find("0.125") != std::string::npos)
            if (found < 2) xs[found++] = c.x - (c.width * 0.5);
    CHECK(found == 2);
}

TEST(annotation_empty_frame_is_refused) {
    const Volume v = vol();
    FeatureControlFrame f;
    f.at = at(v, 0, 0);
    CHECK(!build(f, v).ok);

    f.segments.push_back(GeoSegment{});
    f.height = 0.0;
    CHECK(!build(f, v).ok);
}

TEST(annotation_datum_feature_has_both_triangle_and_box) {
    const Volume v = vol();
    DatumFeature d;
    d.letter = "A";
    d.at = at(v, 1, 1);
    d.height = 0.005;

    const FrameGeometry r = build(d, v);
    CHECK_MSG(r.ok, "%s", r.reason);
    CHECK(r.cells.size() == 1);
    CHECK(r.cells[0].text == "A");
    CHECK_MSG(r.lines.size() == 8, "%zu lines", r.lines.size());
    CHECK(r.height > 0.0 && r.width > 0.0);
}

TEST(annotation_datum_feature_can_hang_below) {
    const Volume v = vol();
    DatumFeature up, down;
    up.at = down.at = at(v, 0, 0);
    down.below = true;

    const FrameGeometry a = build(up, v);
    const FrameGeometry b = build(down, v);
    CHECK(a.ok && b.ok);
    CHECK_MSG(b.cells[0].y < a.cells[0].y,
              "the below-side datum did not go below (%.6f vs %.6f)",
              b.cells[0].y, a.cells[0].y);
}

TEST(annotation_datum_without_a_letter_is_refused) {
    const Volume v = vol();
    DatumFeature d;
    d.letter.clear();
    CHECK(!build(d, v).ok);
}

TEST(annotation_ordinate_run_measures_every_feature_from_one_datum) {
    const Volume v = vol();
    Style s;
    OrdinateRun r;
    r.datum = at(v, 0, 0);
    r.features = {at(v, 12, 0), at(v, 40, 0), at(v, 95, 0)};
    r.leader_length = 0.008;

    const ChainResult out = ordinate_run(r, s, v);
    CHECK_MSG(out.ok, "%s", out.reason);
    CHECK(out.dimensions.size() == 3);
    for (const auto& d : out.dimensions) {
        CHECK(d.kind == Kind::Ordinate);
        CHECK_MSG(d.p1.x == r.datum.x && d.p1.y == r.datum.y,
                  "a feature measured from somewhere other than the datum");
    }
}

TEST(annotation_ordinate_leaders_step_away_from_the_datum) {
    const Volume v = vol();
    Style s;
    OrdinateRun r;
    r.datum = at(v, 0, 0);
    r.features = {at(v, 10, 5), at(v, 20, -5)};
    r.leader_length = 0.01;

    const ChainResult out = ordinate_run(r, s, v);
    CHECK(out.ok);
    CHECK_MSG(wy(v, out.dimensions[0].p3) > 0.0, "the leader ran the wrong way");
    CHECK(wy(v, out.dimensions[1].p3) < 0.0);
}

TEST(annotation_ordinate_run_with_no_features_is_refused) {
    const Volume v = vol();
    Style s;
    OrdinateRun r;
    r.datum = at(v, 0, 0);
    CHECK(!ordinate_run(r, s, v).ok);
}

TEST(annotation_jogged_radius_measures_the_true_centre) {
    const Volume v = vol();
    Style s;
    JoggedRadius j;
    j.centre = at(v, 0, 0);
    j.radius = static_cast<i64>(std::llround(500.0 * v.scale()));
    j.origin = at(v, 30, 20);
    j.jog = at(v, 20, 14);
    j.centre_override = at(v, 5, 3);

    const Geometry g = build(j, s, v);
    CHECK_MSG(g.ok, "%s", g.reason);
    CHECK_MSG(g.text.find("500") != std::string::npos,
              "the jogged radius reads '%s'", g.text.c_str());
    CHECK_MSG(g.lines.size() == 6, "%zu lines", g.lines.size());
    CHECK(!g.arrows.empty());
}

TEST(annotation_jogged_radius_rejects_bad_input) {
    const Volume v = vol();
    Style s;
    JoggedRadius j;
    CHECK(!build(j, s, v).ok);

    j.radius = 1000;
    j.jog = at(v, 1, 1);
    j.centre_override = at(v, 1, 1);
    CHECK_MSG(!build(j, s, v).ok, "a zero-length jog was accepted");
}

TEST(annotation_inspection_frames_the_value_with_a_label_and_a_rate) {
    const Volume v = vol();
    Style s;
    Inspection i;
    i.dimension.kind = Kind::Linear;
    i.dimension.p1 = at(v, 0, 0);
    i.dimension.p2 = at(v, 4, 0);
    i.dimension.p3 = at(v, 2, 2);
    i.label = "A1";
    i.rate = "100%";

    const InspectionGeometry g = build(i, s, v);
    CHECK_MSG(g.ok, "%s", g.reason);
    CHECK_MSG(g.cells.size() == 3, "%zu cells", g.cells.size());
    CHECK(g.cells[0].text == "A1");
    CHECK(g.cells[2].text == "100%");
    CHECK_MSG(!g.frame.empty(), "the inspection frame drew nothing");
    CHECK(g.cells[1].text == g.base.text);
}

TEST(annotation_inspection_shapes_differ) {
    const Volume v = vol();
    Style s;
    Inspection i;
    i.dimension.kind = Kind::Linear;
    i.dimension.p1 = at(v, 0, 0);
    i.dimension.p2 = at(v, 4, 0);
    i.dimension.p3 = at(v, 2, 2);
    i.label = "A";

    i.shape = InspectShape::Round;
    const auto round = build(i, s, v);
    i.shape = InspectShape::Angular;
    const auto angular = build(i, s, v);
    i.shape = InspectShape::None;
    const auto none = build(i, s, v);

    CHECK(round.ok && angular.ok && none.ok);
    bool round_has_arc = false;
    for (const auto& sh : round.frame)
        if (sh.kind == gsnap::ShapeKind::Arc) round_has_arc = true;
    CHECK_MSG(round_has_arc, "the round frame has no arcs");
    for (const auto& sh : angular.frame)
        CHECK(sh.kind == gsnap::ShapeKind::Segment);
}

TEST(annotation_inspection_of_a_bad_dimension_is_refused) {
    const Volume v = vol();
    Style s;
    Inspection i;
    i.dimension.kind = Kind::Radial;
    i.dimension.radius = 0;
    const InspectionGeometry g = build(i, s, v);
    CHECK(!g.ok || g.base.ok);
}

TEST(annotation_fields_parse_both_written_forms) {
    const auto a = parse_fields("Sheet %<SheetNumber>% of %<SheetCount>%");
    CHECK_MSG(a.size() == 2, "%zu fields", a.size());
    CHECK(a[0].kind == FieldKind::SheetNumber);
    CHECK(a[1].kind == FieldKind::SheetCount);

    const auto b = parse_fields("%<\\AcVar SheetNumber>%");
    CHECK_MSG(b.size() == 1, "the DWG form was not recognised");
    CHECK(b[0].kind == FieldKind::SheetNumber);
}

TEST(annotation_fields_evaluate_against_a_context) {
    FieldContext ctx;
    ctx.set("SheetNumber", "3");
    ctx.set("SheetCount", "12");
    ctx.set("DrawingTitle", "GROUND FLOOR PLAN");

    const std::string out =
        evaluate_fields("Sheet %<SheetNumber>% of %<SheetCount>% \xe2\x80\x94 "
                        "%<DrawingTitle>%", ctx);
    CHECK_MSG(out.find("Sheet 3 of 12") == 0, "got '%s'", out.c_str());
    CHECK(out.find("GROUND FLOOR PLAN") != std::string::npos);
}

TEST(annotation_unresolved_fields_are_obviously_wrong) {
    FieldContext ctx;
    const std::string out = evaluate_fields("Rev %<Revision>%", ctx);
    CHECK_MSG(out == "Rev ####", "got '%s'", out.c_str());
}

TEST(annotation_text_without_fields_passes_through) {
    FieldContext ctx;
    ctx.set("SheetNumber", "1");
    CHECK(evaluate_fields("PLAIN TEXT", ctx) == "PLAIN TEXT");
    CHECK(evaluate_fields("", ctx).empty());
    CHECK(evaluate_fields("half %<open", ctx) == "half %<open");
}

TEST(annotation_field_context_replaces_by_key) {
    FieldContext ctx;
    ctx.set("A", "1");
    ctx.set("A", "2");
    CHECK(ctx.size() == 1);
    CHECK(*ctx.get("A") == "2");
    CHECK(ctx.get("B") == nullptr);
}

TEST(annotation_every_field_kind_is_named) {
    for (int i = 0; i <= static_cast<int>(FieldKind::Custom); ++i)
        CHECK_MSG(std::string(field_name(static_cast<FieldKind>(i))).size() > 0,
                  "field kind %d has no name", i);
}

TEST(annotation_custom_fields_carry_their_own_name) {
    const auto f = parse_fields("%<ProjectNumber>%");
    CHECK(f.size() == 1);
    CHECK(f[0].kind == FieldKind::Custom);
    CHECK_MSG(f[0].argument == "ProjectNumber", "argument '%s'",
              f[0].argument.c_str());

    FieldContext ctx;
    ctx.set("ProjectNumber", "24-118");
    CHECK(evaluate_fields("%<ProjectNumber>%", ctx) == "24-118");
}
