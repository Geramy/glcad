#include "test.hpp"

#include <cmath>
#include <filesystem>

#include "render/layout.hpp"
#include "render/registry.hpp"

using namespace gtext;

TEST(text_utf8_decoding) {
    CHECK(decode_utf8("abc").size() == 3);
    CHECK(decode_utf8("abc")[0] == 'a');

    const auto deg = decode_utf8("\xc2\xb0");        // degree
    CHECK(deg.size() == 1 && deg[0] == 0x00B0);

    const auto dia = decode_utf8("\xe2\x8c\x80");    // diameter sign
    CHECK(dia.size() == 1 && dia[0] == 0x2300);

    const auto mixed = decode_utf8("R12.5\xc2\xb0");
    CHECK(mixed.size() == 6);
    CHECK(mixed[5] == 0x00B0);
}

TEST(text_registry_finds_system_fonts) {
    FontRegistry r;
    const std::size_t n = r.scan_system();
    CHECK_MSG(n > 0, "no system fonts found");
    CHECK(r.size() == n);
}

TEST(text_registry_search_and_monospace) {
    FontRegistry r;
    r.scan_system();
    if (r.size() == 0) return;

    const auto hits = r.search("hel");
    for (const auto* f : hits)
        CHECK(f != nullptr);

    const auto mono = r.monospace();
    CHECK_MSG(!mono.empty(), "no monospace font detected");
    for (const auto* f : mono) CHECK(f->monospace);
}

TEST(text_substitution_is_explicit_and_reported) {
    FontRegistry r;
    FontEntry a; a.name = "Helvetica"; a.path = "/x/Helvetica.ttf"; r.add(a);
    FontEntry b; b.name = "romans";    b.path = "/x/romans.shx";
    b.kind = FontKind::SHX; r.add(b);

    bool subbed = true;
    const FontEntry* got = r.resolve("Helvetica", subbed);
    CHECK(got != nullptr);
    CHECK_MSG(!subbed, "an exact match must not report substitution");

    got = r.resolve("NoSuchFont", subbed);
    CHECK(got != nullptr);
    CHECK_MSG(subbed, "a missing font must report that it was substituted");

    r.map_substitute("simplex", "romans");
    got = r.resolve("simplex", subbed);
    CHECK(subbed);
    CHECK(got->name == "romans");
    CHECK(r.substitutions().size() == 1);
}

TEST(text_shx_fonts_are_recognised_as_stroke_fonts) {
    FontEntry e;
    e.name = "romans";
    e.kind = FontKind::SHX;
    CHECK(e.is_stroke());

    FontEntry t;
    t.kind = FontKind::TrueType;
    CHECK(!t.is_stroke());
}

TEST(text_style_table) {
    TextStyleTable t;
    CHECK(t.size() == 1);
    CHECK(t.current() == "Standard");
    CHECK_MSG(!t.remove("Standard"), "Standard must be permanent");

    TextStyleDef s;
    s.name = "Title";
    s.font = "Helvetica";
    s.height = 0.005;
    s.width_factor = 0.9f;
    s.oblique = 15.0f;
    t.add(s);

    CHECK(t.size() == 2);
    CHECK(t.find("Title")->height_fixed());
    CHECK(!t.find("Standard")->height_fixed());
    CHECK(std::fabs(t.find("Title")->width_factor - 0.9f) < 1e-6f);

    t.set_current("Title");
    CHECK(t.current() == "Title");
    CHECK(!t.remove("Title"));
}

static bool load_any_font(Font& f) {
    FontRegistry r;
    r.scan_system();

    for (const char* preferred : {"Helvetica", "Arial", "HelveticaNeue",
                                  "DejaVuSans", "Times New Roman", "Verdana",
                                  "SFNS", "Geneva"}) {
        if (const FontEntry* e = r.find(preferred)) {
            std::string err;
            if (f.load_file(e->path, 32, 4, &err) && f.covers_ascii()) return true;
        }
    }
    for (const auto& e : r.all()) {
        if (e.kind != FontKind::TrueType && e.kind != FontKind::OpenType) continue;
        std::string err;
        if (f.load_file(e.path, 32, 4, &err) && f.covers_ascii()) return true;
    }
    return false;
}

TEST(text_font_rejects_nonfont_and_truncated_text_or_icon_data) {
    const std::string stroke="*0,4,tinyfont\n6,2,0\n*65,6,A\n2,014,1,024,020,0\n";
    std::vector<std::vector<u8>> invalid={{},{0},{0,1,0,0},
        std::vector<u8>(stroke.begin(),stroke.end()),
        {'t','t','c','f',0,1,0,0,0,0,0,1,0xff,0xff,0xff,0xff},
        {0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0}};
    for(const auto& bytes:invalid) {
        Font font;std::string error;
        CHECK(!font.load(bytes,16,2,&error));CHECK(!font.loaded());CHECK(!error.empty());
    }
    Font source;
    CHECK(load_any_font(source));if(!source.loaded()) return;
    const auto bytes=source.source_bytes();
    for(const auto& icons:invalid) {
        Font font;
        CHECK(font.load_with_icons(*bytes,icons,{0xf001},16,2));
        CHECK(font.loaded());CHECK(font.covers_ascii());CHECK(font.glyph(0xf001)==nullptr);
    }
}

TEST(text_font_loads_and_builds_an_sdf_atlas) {
    Font f;
    if (!load_any_font(f)) { CHECK_MSG(false, "no loadable TrueType font found"); return; }

    CHECK(f.loaded());
    CHECK_MSG(f.covers_ascii(), "loaded font does not cover the ASCII alphabet");
    CHECK_MSG(f.coverage() >= 90, "only %zu printable ASCII glyphs", f.coverage());
    CHECK(f.atlas_width() > 0);
    CHECK(f.atlas_height() > 0);
    CHECK(f.atlas().size() == static_cast<std::size_t>(f.atlas_width()) * f.atlas_height());

    std::size_t nonzero = 0;
    for (u8 v : f.atlas()) if (v > 0) ++nonzero;
    CHECK_MSG(nonzero > f.atlas().size() / 20, "atlas looks empty (%zu non-zero)", nonzero);
}

TEST(text_metrics_are_sane) {
    Font f;
    if (!load_any_font(f)) return;

    const auto& m = f.metrics();
    CHECK_MSG(m.ascent > 0.5f && m.ascent < 1.5f, "ascent %.3f", m.ascent);
    CHECK_MSG(m.descent < 0.0f, "descent %.3f should be negative", m.descent);
    CHECK(m.line_height() > m.ascent);

    const Glyph* M = f.glyph('M');
    const Glyph* i = f.glyph('i');
    CHECK(M && i);
    if (M && i) CHECK(M->advance > i->advance);

    const Glyph* sp = f.glyph(' ');
    CHECK(sp && sp->advance > 0.0f);
    CHECK(!sp->valid());
}

TEST(text_measure_scales_with_content) {
    Font f;
    if (!load_any_font(f)) return;
    CHECK(f.measure("") == 0.0f);
    CHECK(f.measure("WWWW") > f.measure("iiii"));
    CHECK(f.measure("abcdef") > f.measure("abc"));
}

TEST(text_layout_places_glyphs_left_to_right) {
    Font f;
    if (!load_any_font(f)) return;

    TextStyle st;
    st.size = 1.0f;
    const Layout l = layout_text(f, "ABC", st);

    CHECK(l.glyphs.size() == 3);
    CHECK(l.glyphs[0].x < l.glyphs[1].x);
    CHECK(l.glyphs[1].x < l.glyphs[2].x);
    CHECK(l.width > 0.0f);
    CHECK(l.line_count == 1);
}

TEST(text_layout_handles_newlines) {
    Font f;
    if (!load_any_font(f)) return;

    TextStyle st;
    const Layout l = layout_text(f, "AB\nCD\nEF", st);
    CHECK(l.line_count == 3);

    CHECK(l.glyphs.front().line == 0);
    CHECK(l.glyphs.back().line == 2);
    CHECK(l.glyphs.back().y < l.glyphs.front().y);
}

TEST(text_layout_alignment) {
    Font f;
    if (!load_any_font(f)) return;

    TextStyle left;   left.align = Align::Left;
    TextStyle centre; centre.align = Align::Centre;
    TextStyle right;  right.align = Align::Right;

    const Layout a = layout_text(f, "HELLO", left);
    const Layout b = layout_text(f, "HELLO", centre);
    const Layout c = layout_text(f, "HELLO", right);

    CHECK(a.min_x > b.min_x);
    CHECK(b.min_x > c.min_x);
    CHECK(std::fabs(a.width - b.width) < 1e-4f);
}

TEST(text_layout_wraps_at_the_given_width) {
    Font f;
    if (!load_any_font(f)) return;

    TextStyle st;
    st.size = 1.0f;
    const Layout unwrapped = layout_text(f, "the quick brown fox jumps", st);
    CHECK(unwrapped.line_count == 1);

    st.wrap_width = unwrapped.width * 0.4f;
    const Layout wrapped = layout_text(f, "the quick brown fox jumps", st);
    CHECK_MSG(wrapped.line_count > 1, "wrapping produced %u lines", wrapped.line_count);
    CHECK(wrapped.width < unwrapped.width);
}

TEST(text_layout_size_scales_linearly) {
    Font f;
    if (!load_any_font(f)) return;

    TextStyle a; a.size = 1.0f;
    TextStyle b; b.size = 3.0f;
    const Layout la = layout_text(f, "SCALE", a);
    const Layout lb = layout_text(f, "SCALE", b);
    CHECK_MSG(std::fabs(lb.width / la.width - 3.0f) < 0.01f,
              "ratio %.4f", lb.width / la.width);
}

TEST(text_kerning_tightens_pairs) {
    Font f;
    if (!load_any_font(f)) return;

    TextStyle on;  on.kerning = true;
    TextStyle off; off.kerning = false;

    const Layout a = layout_text(f, "AVATAR To Wa", on);
    const Layout b = layout_text(f, "AVATAR To Wa", off);
    CHECK_MSG(a.width <= b.width, "kerned %.4f wider than unkerned %.4f",
              a.width, b.width);
}

TEST(text_vertical_alignment_shifts_the_block) {
    Font f;
    if (!load_any_font(f)) return;

    TextStyle base; base.valign = VAlign::Baseline;
    TextStyle top;  top.valign = VAlign::Top;
    TextStyle mid;  mid.valign = VAlign::Middle;

    const Layout a = layout_text(f, "Hg", base);
    const Layout b = layout_text(f, "Hg", top);
    const Layout c = layout_text(f, "Hg", mid);

    CHECK(b.min_y < a.min_y);
    CHECK(c.min_y != a.min_y);
}

TEST(text_letter_spacing_widens) {
    Font f;
    if (!load_any_font(f)) return;
    TextStyle a;
    TextStyle b; b.letter_spacing = 0.1f;
    CHECK(layout_text(f, "SPACED", b).width > layout_text(f, "SPACED", a).width);
}


TEST(text_coverage_rejects_symbol_fonts) {
    Font good;
    if (!load_any_font(good)) { CHECK_MSG(false, "no text-capable font"); return; }
    CHECK(good.covers_ascii());
    CHECK(good.coverage() >= 90);

    FontRegistry r;
    r.scan_system();

    bool found_symbol = false;
    int examined = 0;
    for (const auto& e : r.all()) {
        if (e.kind != FontKind::TrueType && e.kind != FontKind::OpenType) continue;
        if (++examined > (int)::t::iters(40)) break;
        Font f;
        std::string err;
        if (!f.load_file(e.path, 16, 2, &err)) continue;
        if (!f.covers_ascii()) {
            found_symbol = true;
            CHECK_MSG(f.coverage() < 90,
                      "font \"%s\" reports no ASCII coverage but %zu glyphs",
                      e.name.c_str(), f.coverage());
            break;
        }
    }
    (void)found_symbol;
}
