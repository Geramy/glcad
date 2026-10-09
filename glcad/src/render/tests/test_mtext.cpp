#include "test.hpp"

#include <cmath>
#include <string>

#include "render/mtext.hpp"
#include "render/registry.hpp"
#include "render/stroke.hpp"

using namespace gtext;

TEST(mtext_plain_string_is_one_run) {
    const MTextContent c = parse_mtext("GENERAL NOTES");
    CHECK(c.runs.size() == 1);
    CHECK(c.runs[0].text == "GENERAL NOTES");
    CHECK(c.plain() == "GENERAL NOTES");
}

TEST(mtext_paragraph_breaks) {
    const MTextContent c = parse_mtext("FIRST\\PSECOND\\PTHIRD");
    CHECK_MSG(c.runs.size() == 3, "%zu runs", c.runs.size());
    CHECK(c.runs[0].paragraph == 0);
    CHECK(c.runs[1].paragraph == 1);
    CHECK(c.runs[2].paragraph == 2);
    CHECK(c.plain() == "FIRST\nSECOND\nTHIRD");
}

TEST(mtext_escapes_are_literal) {
    const MTextContent c = parse_mtext("A \\\\ B \\{ C \\} D");
    CHECK_MSG(c.plain() == "A \\ B { C } D", "got '%s'", c.plain().c_str());
}

TEST(mtext_non_breaking_space) {
    const MTextContent c = parse_mtext("10\\~mm");
    CHECK(c.plain() == "10 mm");
}

TEST(mtext_height_can_be_absolute_or_a_multiple) {
    const MTextContent a = parse_mtext("\\H3.5;BIG");
    CHECK(a.runs.size() == 1);
    CHECK_MSG(std::fabs(a.runs[0].format.height - 3.5f) < 1e-6f, "%f",
              a.runs[0].format.height);

    const MTextContent b = parse_mtext("\\H2x;DOUBLE");
    CHECK_MSG(std::fabs(b.runs[0].format.height - 2.0f) < 1e-6f, "%f",
              b.runs[0].format.height);

    const MTextContent c = parse_mtext("\\H2x;\\H2x;QUAD");
    CHECK(std::fabs(c.runs[0].format.height - 4.0f) < 1e-6f);
}

TEST(mtext_groups_scope_their_overrides) {
    const MTextContent c = parse_mtext("NORMAL {\\H2x;BIG} NORMAL AGAIN");
    CHECK_MSG(c.runs.size() == 3, "%zu runs", c.runs.size());
    CHECK(std::fabs(c.runs[0].format.height - 1.0f) < 1e-6f);
    CHECK(std::fabs(c.runs[1].format.height - 2.0f) < 1e-6f);
    CHECK_MSG(std::fabs(c.runs[2].format.height - 1.0f) < 1e-6f,
              "the override leaked past its brace (%f)", c.runs[2].format.height);
}

TEST(mtext_nested_groups) {
    const MTextContent c = parse_mtext("A{\\H2x;B{\\H2x;C}D}E");
    CHECK(c.runs.size() == 5);
    CHECK(std::fabs(c.runs[0].format.height - 1.0f) < 1e-6f);
    CHECK(std::fabs(c.runs[1].format.height - 2.0f) < 1e-6f);
    CHECK(std::fabs(c.runs[2].format.height - 4.0f) < 1e-6f);
    CHECK(std::fabs(c.runs[3].format.height - 2.0f) < 1e-6f);
    CHECK(std::fabs(c.runs[4].format.height - 1.0f) < 1e-6f);
}

TEST(mtext_font_code_with_flags) {
    const MTextContent c = parse_mtext("\\fArial|b1|i1|c0|p34;BOLD ITALIC");
    CHECK(c.runs.size() == 1);
    CHECK(c.runs[0].format.font == "Arial");
    CHECK(c.runs[0].format.bold);
    CHECK(c.runs[0].format.italic);
}

TEST(mtext_underline_overline_strike) {
    const MTextContent c = parse_mtext("\\LUNDER\\l PLAIN \\OOVER\\o");
    CHECK(c.runs.size() >= 3);
    CHECK(c.runs[0].format.underline);
    CHECK(!c.runs[1].format.underline);
    CHECK(c.runs[2].format.overline);
}

TEST(mtext_colour_codes) {
    const MTextContent a = parse_mtext("\\C1;RED");
    CHECK(a.runs[0].format.has_color);
    CHECK(!a.runs[0].format.true_color);
    CHECK(a.runs[0].format.aci == 1);

    const MTextContent b = parse_mtext("\\c255;PURE RED");
    CHECK(b.runs[0].format.true_color);
    CHECK_MSG(b.runs[0].format.r == 255 && b.runs[0].format.g == 0 &&
                  b.runs[0].format.b == 0,
              "got %d,%d,%d", int(b.runs[0].format.r), int(b.runs[0].format.g),
              int(b.runs[0].format.b));
}

TEST(mtext_width_oblique_and_tracking) {
    const MTextContent c = parse_mtext("\\W0.8;\\Q15;\\T1.5;CONDENSED");
    CHECK(std::fabs(c.runs[0].format.width - 0.8f) < 1e-6f);
    CHECK(std::fabs(c.runs[0].format.oblique - 15.0f) < 1e-6f);
    CHECK(std::fabs(c.runs[0].format.tracking - 1.5f) < 1e-6f);
}

TEST(mtext_stacked_fractions) {
    const MTextContent a = parse_mtext("\\S1/2;");
    CHECK(a.runs.size() == 1);
    CHECK(a.runs[0].stack == Stack::Horizontal);
    CHECK(a.runs[0].text == "1");
    CHECK(a.runs[0].denominator == "2");

    const MTextContent b = parse_mtext("\\S3#4;");
    CHECK(b.runs[0].stack == Stack::Diagonal);

    const MTextContent c = parse_mtext("\\S+0.1^-0.1;");
    CHECK(c.runs[0].stack == Stack::Tolerance);
    CHECK(c.runs[0].text == "+0.1");
    CHECK(c.runs[0].denominator == "-0.1");
}

TEST(mtext_paragraph_properties) {
    const MTextContent c = parse_mtext("\\pxi-5,l10;INDENTED");
    CHECK(c.paragraphs.size() >= 1);
    CHECK(std::fabs(c.paragraphs[0].indent_first + 5.0f) < 1e-6f);
    CHECK(std::fabs(c.paragraphs[0].indent_left - 10.0f) < 1e-6f);
}

TEST(mtext_unknown_and_unterminated_codes_stay_visible) {
    const MTextContent a = parse_mtext("BEFORE \\Z AFTER");
    CHECK_MSG(a.plain().find("BEFORE") != std::string::npos, "'%s'",
              a.plain().c_str());
    CHECK(a.plain().find("AFTER") != std::string::npos);

    const MTextContent b = parse_mtext("TEXT \\H3.5 UNTERMINATED");
    CHECK(b.plain().find("UNTERMINATED") != std::string::npos);

    const MTextContent c = parse_mtext("STRAY } BRACE");
    CHECK(c.plain().find("BRACE") != std::string::npos);

    const MTextContent d = parse_mtext("TRAILING \\");
    CHECK(d.plain().find("TRAILING") != std::string::npos);
}

TEST(mtext_empty_input) {
    const MTextContent c = parse_mtext("");
    CHECK(c.empty());
    CHECK(c.plain().empty());
}

TEST(mtext_round_trips_through_format_and_parse) {
    const char* sources[] = {
        "SIMPLE NOTE",
        "FIRST\\PSECOND",
        "\\H2x;BIG\\H0.5x;SMALL",
        "\\C1;RED \\C3;GREEN",
        "\\W0.8;CONDENSED",
        "NOTE WITH \\\\ BACKSLASH",
    };
    for (const char* s : sources) {
        const MTextContent a = parse_mtext(s);
        const MTextContent b = parse_mtext(format_mtext(a));
        CHECK_MSG(a.plain() == b.plain(), "'%s' became '%s'", a.plain().c_str(),
                  b.plain().c_str());
        CHECK_MSG(a.runs.size() == b.runs.size(), "'%s': %zu vs %zu runs", s,
                  a.runs.size(), b.runs.size());
        for (std::size_t i = 0; i < a.runs.size() && i < b.runs.size(); ++i)
            CHECK_MSG(a.runs[i].format == b.runs[i].format,
                      "'%s' run %zu changed format", s, i);
    }
}

TEST(mtext_reencoding_escapes_braces) {
    MTextContent c;
    Run r;
    r.text = "LITERAL { AND }";
    c.runs.push_back(r);
    const std::string s = format_mtext(c);
    CHECK(parse_mtext(s).plain() == "LITERAL { AND }");
}

namespace {

const char* kTinyShp = R"(
;; a minimal test font
*0,4,tinyfont
6,2,0

*65,6,uppercase A
2,014,1,024,020,0
*66,4,uppercase B
2,024,1,040,0
)";

}

TEST(stroke_direction_table_is_the_octant_grid) {
    f32 dx, dy;
    shx_direction(0, dx, dy);
    CHECK(std::fabs(dx - 1.0f) < 1e-6f && std::fabs(dy) < 1e-6f);

    shx_direction(1, dx, dy);
    CHECK_MSG(std::fabs(dx - 1.0f) < 1e-6f && std::fabs(dy - 0.5f) < 1e-6f,
              "direction 1 is (%f, %f)", dx, dy);

    shx_direction(4, dx, dy);
    CHECK(std::fabs(dx) < 1e-6f && std::fabs(dy - 1.0f) < 1e-6f);

    shx_direction(8, dx, dy);
    CHECK(std::fabs(dx + 1.0f) < 1e-6f && std::fabs(dy) < 1e-6f);

    shx_direction(12, dx, dy);
    CHECK(std::fabs(dx) < 1e-6f && std::fabs(dy + 1.0f) < 1e-6f);
}

TEST(stroke_shp_source_loads) {
    StrokeFont f;
    std::string err;
    CHECK_MSG(f.load_shp(kTinyShp, &err), "%s", err.c_str());
    CHECK(f.loaded());
    CHECK(f.has('A'));
    CHECK(f.has('B'));
    CHECK(!f.has('Z'));
    CHECK_MSG(std::fabs(f.info().above - 6.0f) < 1e-6f, "above=%f",
              f.info().above);
    CHECK(std::fabs(f.info().below - 2.0f) < 1e-6f);
}

TEST(stroke_glyph_produces_polylines) {
    StrokeFont f;
    CHECK(f.load_shp(kTinyShp));
    const StrokeGlyph* g = f.glyph('A');
    CHECK_MSG(g != nullptr, "glyph A did not build");
    CHECK_MSG(!g->strokes.empty(), "glyph A drew nothing");
    for (const auto& s : g->strokes)
        CHECK_MSG(s.size() >= 2, "a stroke with one point is not a stroke");
    CHECK(g->max_x >= g->min_x);
}

TEST(stroke_glyphs_are_normalised_by_the_font_height) {
    StrokeFont f;
    CHECK(f.load_shp(kTinyShp));
    const StrokeGlyph* g = f.glyph('B');
    CHECK(g != nullptr);
    for (const auto& s : g->strokes)
        for (const auto& p : s)
            CHECK_MSG(std::fabs(p.x) < 5.0f && std::fabs(p.y) < 5.0f,
                      "unnormalised point (%f, %f)", p.x, p.y);
}

TEST(stroke_missing_glyph_still_advances) {
    StrokeFont f;
    CHECK(f.load_shp(kTinyShp));
    const f32 known = f.text_width("AB");
    const f32 with_gap = f.text_width("AZB");
    CHECK_MSG(with_gap > known, "a missing glyph consumed no width");
}

TEST(stroke_text_places_glyphs_along_the_pen) {
    StrokeFont f;
    CHECK(f.load_shp(kTinyShp));
    const auto one = f.stroke_text("A");
    const auto two = f.stroke_text("AA");
    CHECK(two.size() == one.size() * 2);

    f32 first_max = -1e9f, second_min = 1e9f;
    for (std::size_t i = 0; i < one.size(); ++i)
        for (const auto& p : two[i]) first_max = std::max(first_max, p.x);
    for (std::size_t i = one.size(); i < two.size(); ++i)
        for (const auto& p : two[i]) second_min = std::min(second_min, p.x);
    CHECK_MSG(second_min >= first_max - 1e-4f,
              "the second glyph overlapped the first (%f vs %f)", second_min,
              first_max);
}

TEST(stroke_width_factor_and_oblique_apply) {
    StrokeFont f;
    CHECK(f.load_shp(kTinyShp));
    CHECK(std::fabs(f.text_width("AB", 2.0f) - f.text_width("AB", 1.0f) * 2.0f) <
          1e-4f);

    const auto plain = f.stroke_text("A", 1.0f, 0.0f);
    const auto slanted = f.stroke_text("A", 1.0f, 30.0f);
    bool moved = false;
    for (std::size_t i = 0; i < plain.size(); ++i)
        for (std::size_t k = 0; k < plain[i].size(); ++k)
            if (plain[i][k].y > 0.01f && slanted[i][k].x > plain[i][k].x + 1e-5f)
                moved = true;
    CHECK_MSG(moved, "the oblique angle did nothing");
}

TEST(stroke_pen_up_breaks_the_polyline) {
    StrokeFont f;
    CHECK(f.load_shp("*0,2,t\n4,0\n*65,8,A\n2,010,1,020,2,010,1,020,0\n"));
    const StrokeGlyph* g = f.glyph('A');
    CHECK(g != nullptr);
    CHECK_MSG(g->strokes.size() == 2, "expected two strokes, got %zu",
              g->strokes.size());
}

TEST(stroke_scale_codes_change_the_step) {
    StrokeFont f;
    CHECK(f.load_shp("*0,2,t\n1,0\n*65,10,A\n1,004,2,020,0\n"));
    const StrokeGlyph* a = f.glyph('A');
    CHECK(a != nullptr);

    StrokeFont g;
    CHECK(g.load_shp("*0,2,t\n1,0\n*65,10,A\n1,020,0\n"));
    const StrokeGlyph* b = g.glyph('A');
    CHECK(b != nullptr);
    CHECK_MSG(a->max_x > b->max_x * 1.5f,
              "the scale multiplier did nothing (%f vs %f)", a->max_x, b->max_x);
}

TEST(stroke_push_and_pop_restore_the_pen) {
    StrokeFont f;
    CHECK(f.load_shp("*0,2,t\n1,0\n*65,12,A\n1,005,030,006,040,0\n"));
    const StrokeGlyph* g = f.glyph('A');
    CHECK(g != nullptr);
    CHECK_MSG(!g->strokes.empty(), "nothing was drawn");
}

TEST(stroke_self_reference_terminates) {
    StrokeFont f;
    CHECK(f.load_shp("*0,2,t\n1,0\n*65,6,A\n1,007,65,0\n"));
    const StrokeGlyph* g = f.glyph('A');
    CHECK(g != nullptr);
}

TEST(stroke_shx_binary_is_recognised_by_its_header) {
    std::vector<gcm::u8> junk(256, 0x41);
    gio::MemoryReader r(junk);
    StrokeFont f;
    std::string err;
    CHECK(!f.load_shx(r, &err));
    CHECK_MSG(err.find("not an AutoCAD") != std::string::npos, "%s", err.c_str());
}

TEST(stroke_shx_truncated_stream_is_refused) {
    StrokeFont f;
    gio::MemoryReader r(std::vector<gcm::u8>{'A', 'u', 't', 'o'});
    std::string err;
    CHECK(!f.load_shx(r, &err));
    CHECK(!err.empty());
}

TEST(stroke_shx_shapes_format_parses) {
    std::vector<gcm::u8> d;
    const char* head = "AutoCAD-86 shapes 1.1\r\n";
    for (const char* p = head; *p; ++p) d.push_back(gcm::u8(*p));
    d.push_back(0x1A);
    d.push_back(0x00);

    auto put16 = [&](unsigned v) {
        d.push_back(gcm::u8(v & 0xFF));
        d.push_back(gcm::u8((v >> 8) & 0xFF));
    };

    const std::vector<gcm::u8> s0 = {'t', 0, 8, 2, 0, 0};
    const std::vector<gcm::u8> s65 = {'A', 0, 0x01, 0x04, 0x00};

    put16(0);                    // first
    put16(65);                   // last
    put16(2);                    // count
    put16(0);  put16(unsigned(s0.size()));
    put16(65); put16(unsigned(s65.size()));
    d.insert(d.end(), s0.begin(), s0.end());
    d.insert(d.end(), s65.begin(), s65.end());

    gio::MemoryReader r(d);
    StrokeFont f;
    std::string err;
    CHECK_MSG(f.load_shx(r, &err), "%s", err.c_str());
    CHECK(f.info().kind == ShxKind::Shapes1_1);
    CHECK_MSG(f.has('A'), "glyph 65 was not indexed");
    CHECK_MSG(std::fabs(f.info().above - 8.0f) < 1e-6f, "above=%f", f.info().above);
    CHECK(f.glyph('A') != nullptr);
}

TEST(stroke_shx_unifont_format_parses) {
    std::vector<gcm::u8> d;
    const char* head = "AutoCAD-86 unifont 1.0\r\n";
    for (const char* p = head; *p; ++p) d.push_back(gcm::u8(*p));
    d.push_back(0x1A);
    d.push_back(0x00);

    auto put16 = [&](unsigned v) {
        d.push_back(gcm::u8(v & 0xFF));
        d.push_back(gcm::u8((v >> 8) & 0xFF));
    };

    put16(2);                       // glyph count
    put16(0);                       // reserved

    const std::vector<gcm::u8> s0 = {'u', 0, 10, 3, 0, 0};
    const std::vector<gcm::u8> s = {0x01, 0x04, 0x00};
    put16(0);   put16(unsigned(s0.size()));
    d.insert(d.end(), s0.begin(), s0.end());
    put16(0x4E2D); put16(unsigned(s.size()));
    d.insert(d.end(), s.begin(), s.end());

    gio::MemoryReader r(d);
    StrokeFont f;
    std::string err;
    CHECK_MSG(f.load_shx(r, &err), "%s", err.c_str());
    CHECK(f.info().kind == ShxKind::UniFont);
    CHECK(f.info().unicode);
    CHECK_MSG(f.has(0x4E2D), "the unicode glyph was not indexed");
}

TEST(stroke_empty_source_is_refused) {
    StrokeFont f;
    std::string err;
    CHECK(!f.load_shp("", &err));
    CHECK(!err.empty());
    CHECK(!f.loaded());
}

TEST(stroke_missing_file_is_reported) {
    StrokeFont f;
    std::string err;
    CHECK(!f.load_file("/tmp/glcad_no_such.shx", &err));
    CHECK(!err.empty());
}

namespace {

bool load_layout_font(Font& f) {
    FontRegistry r;
    r.scan_system();
    for (const char* preferred : {"Helvetica", "Arial", "HelveticaNeue",
                                  "DejaVuSans", "Times New Roman", "Verdana",
                                  "SFNS", "Geneva"})
        if (const FontEntry* e = r.find(preferred)) {
            std::string err;
            if (f.load_file(e->path, 32, 4, &err) && f.covers_ascii()) return true;
        }
    for (const auto& e : r.all()) {
        if (e.kind != FontKind::TrueType && e.kind != FontKind::OpenType) continue;
        std::string err;
        if (f.load_file(e.path, 32, 4, &err) && f.covers_ascii()) return true;
    }
    return false;
}

}

TEST(mtext_layout_places_pieces_on_lines) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    MTextStyle st;
    st.height = 1.0f;
    const MTextLayout l = layout_mtext(f, "FIRST\\PSECOND", st);
    CHECK_MSG(!l.empty(), "nothing was laid out");
    CHECK_MSG(l.line_count == 2, "%u lines", l.line_count);
    CHECK(l.pieces.size() == 2);
    CHECK_MSG(l.pieces[1].y < l.pieces[0].y,
              "the second paragraph did not go below the first");
    CHECK(l.height > 0.0f);
}

TEST(mtext_layout_hangs_downward) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }
    MTextStyle st;
    const MTextLayout l = layout_mtext(f, "A\\PB\\PC", st);
    for (const auto& p : l.pieces) CHECK(p.y <= 0.0f);
}

TEST(mtext_layout_wraps_at_the_column_width) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    MTextStyle narrow;
    narrow.height = 1.0f;
    narrow.width = 4.0f;
    const char* note = "ALL DIMENSIONS ARE IN MILLIMETRES UNLESS NOTED OTHERWISE";
    const MTextLayout wrapped = layout_mtext(f, note, narrow);
    CHECK_MSG(wrapped.line_count > 1, "the column width did not wrap anything");
    CHECK_MSG(wrapped.width <= narrow.width * 1.05f,
              "a line overflowed the column: %f > %f", wrapped.width, narrow.width);

    MTextStyle wide;
    wide.height = 1.0f;
    CHECK(layout_mtext(f, note, wide).line_count == 1);
}

TEST(mtext_layout_breaks_a_word_longer_than_the_column) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }
    MTextStyle st;
    st.height = 1.0f;
    st.width = 3.0f;
    const MTextLayout l = layout_mtext(f, "SUPERCALIFRAGILISTICEXPIALIDOCIOUS", st);
    CHECK(l.line_count > 1);
    CHECK_MSG(l.width <= st.width * 1.05f, "the long word overflowed: %f", l.width);
}

TEST(mtext_layout_alignment_moves_the_lines) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    MTextStyle left, right;
    left.width = right.width = 20.0f;
    right.align = Align::Right;
    const f32 lx = layout_mtext(f, "SHORT", left).pieces[0].x;
    const f32 rx = layout_mtext(f, "SHORT", right).pieces[0].x;
    CHECK_MSG(rx > lx, "right alignment did nothing (%f vs %f)", lx, rx);
}

TEST(mtext_layout_taller_runs_open_the_line_spacing) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }
    MTextStyle st;
    st.height = 1.0f;
    const f32 plain = layout_mtext(f, "A\\PB", st).height;
    const f32 tall  = layout_mtext(f, "\\H3x;A\\PB", st).height;
    CHECK_MSG(tall > plain, "a 3x run did not open the line (%f vs %f)", plain, tall);

    st.exact_spacing = true;
    CHECK(std::fabs(layout_mtext(f, "\\H3x;A\\PB", st).height - plain) < 1e-3f);
}

TEST(mtext_layout_stacked_fraction_is_one_piece) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }
    MTextStyle st;
    const MTextLayout l = layout_mtext(f, "PLATE \\S1/2;\" THICK", st);
    int stacked = 0;
    for (const auto& p : l.pieces)
        if (p.stack != Stack::None) {
            ++stacked;
            CHECK(p.text == "1");
            CHECK(p.denominator == "2");
            CHECK_MSG(p.height > st.height, "a stacked fraction is two lines tall");
        }
    CHECK_MSG(stacked == 1, "%d stacked pieces", stacked);
}

TEST(mtext_layout_of_nothing_is_empty) {
    Font f;
    if (!load_layout_font(f)) { CHECK_MSG(false, "no loadable font"); return; }
    MTextStyle st;
    CHECK(layout_mtext(f, "", st).empty());
}
