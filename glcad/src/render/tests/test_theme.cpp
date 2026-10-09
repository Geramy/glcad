#include "test.hpp"

#include <cmath>

#include "render/theme.hpp"

using namespace gtheme;

static bool near(f32 a, f32 b, f32 tol = 0.005f) { return std::fabs(a - b) < tol; }

TEST(theme_colour_hex_roundtrip) {
    for (u32 v : {0x000000u, 0xFFFFFFu, 0x16181Bu, 0x3B9EFFu, 0xC24A50u}) {
        const Color c = Color::hex(v);
        Color back;
        CHECK(Color::parse(c.to_hex(), back));
        CHECK(near(c.r, back.r));
        CHECK(near(c.g, back.g));
        CHECK(near(c.b, back.b));
    }
}

TEST(theme_colour_parses_with_and_without_alpha) {
    Color c;
    CHECK(Color::parse("#FF8000", c));
    CHECK(near(c.r, 1.0f) && near(c.g, 0.502f) && near(c.b, 0.0f) && near(c.a, 1.0f));

    CHECK(Color::parse("3B9EFF80", c));
    CHECK(near(c.a, 0.502f));

    CHECK(!Color::parse("#GGGGGG", c));
    CHECK(!Color::parse("#FFF", c));
    CHECK(!Color::parse("", c));
}

TEST(theme_builtins_are_complete) {
    for (const Theme& t : {Theme::dark_default(), Theme::light_default(),
                           Theme::high_contrast()}) {
        const auto miss = t.missing_tokens();
        CHECK_MSG(miss.empty(), "theme \"%s\" is missing %zu tokens, first: %s",
                  t.name.c_str(), miss.size(),
                  miss.empty() ? "" : miss[0].c_str());
    }
}

TEST(theme_dark_and_light_actually_differ_in_luminance) {
    const Theme d = Theme::dark_default();
    const Theme l = Theme::light_default();

    CHECK(d.get(token::canvas_background).luminance() < 0.2f);
    CHECK(l.get(token::canvas_background).luminance() > 0.8f);

    const f32 dc = std::fabs(d.get(token::geometry).luminance() -
                             d.get(token::canvas_background).luminance());
    const f32 lc = std::fabs(l.get(token::geometry).luminance() -
                             l.get(token::canvas_background).luminance());
    CHECK_MSG(dc > 0.5f, "dark theme geometry contrast only %.2f", dc);
    CHECK_MSG(lc > 0.5f, "light theme geometry contrast only %.2f", lc);
}

TEST(theme_high_contrast_beats_the_default) {
    const Theme d = Theme::dark_default();
    const Theme h = Theme::high_contrast();
    const f32 dc = std::fabs(d.get(token::geometry).luminance() -
                             d.get(token::canvas_background).luminance());
    const f32 hc = std::fabs(h.get(token::geometry).luminance() -
                             h.get(token::canvas_background).luminance());
    CHECK(hc > dc);
    CHECK(near(hc, 1.0f, 0.02f));
}

TEST(theme_save_load_roundtrip) {
    const Theme original = Theme::dark_default();
    const std::string text = original.save();

    Theme reloaded;
    std::string err;
    CHECK_MSG(reloaded.load(text, &err), "load failed: %s", err.c_str());

    CHECK(reloaded.name == original.name);
    CHECK(reloaded.dark == original.dark);
    CHECK(reloaded.complete());

    for (const auto& tok : all_tokens()) {
        const Color a = original.get(tok);
        const Color b = reloaded.get(tok);
        CHECK_MSG(near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a),
                  "token %s did not survive the roundtrip", tok.c_str());
    }
}

TEST(theme_config_is_human_editable) {
    const char* cfg =
        "# my theme\n"
        "name = Midnight\n"
        "dark = true\n"
        "\n"
        "  canvas.background = #0A0A0F   # deliberately very dark\n"
        "geometry=#E0E0FF\n"
        "\n"
        "ui.accent = #FF6600\n";

    Theme t;
    std::string err;
    CHECK_MSG(t.load(cfg, &err), "load failed: %s", err.c_str());
    CHECK(t.name == "Midnight");
    CHECK(t.dark);
    CHECK(near(t.get(token::canvas_background).r, 10.0f / 255.0f));
    CHECK(near(t.get(token::ui_accent).r, 1.0f));

    CHECK(!t.complete());
    CHECK(t.missing_tokens().size() > 20);
}

TEST(theme_load_reports_the_offending_line) {
    Theme t;
    std::string err;
    CHECK(!t.load("name = X\ncanvas.background = notacolour\n", &err));
    CHECK_MSG(err.find("line 2") != std::string::npos, "error was: %s", err.c_str());
    CHECK(err.find("canvas.background") != std::string::npos);

    CHECK(!t.load("this line has no equals sign\n", &err));
    CHECK(err.find("line 1") != std::string::npos);
}

TEST(theme_editing_at_runtime) {
    Theme t = Theme::dark_default();
    const Color before = t.get(token::ui_accent);
    t.set(token::ui_accent, Color::hex(0xFF00AA));
    const Color after = t.get(token::ui_accent);
    CHECK(!near(before.r, after.r) || !near(before.b, after.b));
    CHECK(near(after.r, 1.0f));

    Theme back;
    CHECK(back.load(t.save()));
    CHECK(near(back.get(token::ui_accent).r, 1.0f));
}

TEST(theme_set_switches_the_active_theme) {
    ThemeSet set;
    CHECK(set.size() == 3);
    CHECK(set.active().name == "GLCAD Dark");

    CHECK(set.activate("GLCAD Light"));
    CHECK(set.active().name == "GLCAD Light");
    CHECK(!set.active().dark);

    CHECK(!set.activate("Nonexistent"));
    CHECK(set.active().name == "GLCAD Light");

    Theme custom = Theme::dark_default();
    custom.name = "Mine";
    set.add(custom);
    CHECK(set.size() == 4);
    CHECK(set.activate("Mine"));

    const auto names = set.names();
    CHECK(names.size() == 4);
}

TEST(theme_colour_helpers) {
    const Color a = Color::rgb(0.0f, 0.0f, 0.0f);
    const Color b = Color::rgb(1.0f, 1.0f, 1.0f);
    CHECK(near(a.mix(b, 0.5f).r, 0.5f));
    CHECK(near(a.with_alpha(0.25f).a, 0.25f));
    CHECK(near(b.luminance(), 1.0f));
    CHECK(near(a.luminance(), 0.0f));
}

TEST(theme_system_appearance_is_readable_or_honestly_unknown) {
    const Appearance a = system_appearance();
    CHECK(a == Appearance::Light || a == Appearance::Dark ||
          a == Appearance::Unknown);
    CHECK(std::string(appearance_name(a)).size() > 0);
}

TEST(theme_following_the_system_switches_between_a_named_pair) {
    ThemeSet set;
    CHECK(set.find("GLCAD Dark") != nullptr);
    CHECK(set.find("GLCAD Light") != nullptr);
    CHECK(set.find("Nonexistent") == nullptr);

    set.set_system_pair("GLCAD Light", "GLCAD Dark");
    CHECK(set.apply_appearance(Appearance::Light));
    CHECK(set.active().name == "GLCAD Light");
    CHECK(set.apply_appearance(Appearance::Dark));
    CHECK(set.active().name == "GLCAD Dark");

    CHECK_MSG(!set.apply_appearance(Appearance::Dark),
              "re-applying the active theme reported a change");
    CHECK(!set.apply_appearance(Appearance::Unknown));
    CHECK(set.active().name == "GLCAD Dark");
}

TEST(theme_sync_does_nothing_unless_following_is_on) {
    ThemeSet set;
    set.activate("GLCAD Dark");
    set.set_follow_system(false);
    CHECK(!set.sync_with_system());

    set.set_follow_system(true);
    CHECK(set.follow_system());
    set.sync_with_system();
    CHECK(set.size() >= 3);
}

TEST(theme_system_pair_refuses_names_that_do_not_exist) {
    ThemeSet set;
    set.set_system_pair("Not A Theme", "Also Not");
    set.apply_appearance(Appearance::Light);
    CHECK(set.active().complete());
}

TEST(theme_editor_groups_tokens_by_their_own_names) {
    ThemeSet set;
    ThemeEditor ed(set);
    const auto groups = ed.groups();
    CHECK(!groups.empty());

    std::size_t total = 0;
    bool has_canvas = false, has_geometry = false;
    for (const auto& g : groups) {
        total += g.tokens.size();
        if (g.name == "canvas") has_canvas = true;
        if (g.name == "geometry") has_geometry = true;
        CHECK_MSG(!g.tokens.empty(), "group '%s' is empty", g.name.c_str());
    }
    CHECK(has_canvas && has_geometry);
    CHECK_MSG(total == all_tokens().size(), "%zu of %zu tokens grouped", total,
              all_tokens().size());
}

TEST(theme_editor_applies_changes_immediately) {
    ThemeSet set;
    set.activate("GLCAD Dark");
    ThemeEditor ed(set);
    CHECK(!ed.dirty());

    const Color before = ed.value(token::geometry);
    ed.set(token::geometry, Color::rgb(1.0f, 0.0f, 0.0f));
    CHECK(ed.dirty());
    CHECK_MSG(set.active().get(token::geometry).r > 0.99f,
              "the change did not reach the live theme");
    CHECK(before.r != 1.0f || before.g != 0.0f);
}

TEST(theme_editor_revert_returns_to_the_last_commit) {
    ThemeSet set;
    set.activate("GLCAD Dark");
    ThemeEditor ed(set);

    ed.set(token::geometry, Color::rgb(1, 0, 0));
    ed.commit();
    CHECK(!ed.dirty());

    ed.set(token::geometry, Color::rgb(0, 1, 0));
    CHECK(ed.dirty());
    ed.revert();
    CHECK(!ed.dirty());
    CHECK_MSG(set.active().get(token::geometry).r > 0.99f,
              "revert went back further than the last commit");
    CHECK(set.active().get(token::geometry).g < 0.01f);
}

TEST(theme_editor_duplicate_starts_from_a_builtin_without_destroying_it) {
    ThemeSet set;
    set.activate("GLCAD Dark");
    ThemeEditor ed(set);

    CHECK(ed.duplicate_active("My Theme"));
    CHECK(set.active().name == "My Theme");
    CHECK(!ed.dirty());

    ed.set(token::geometry, Color::rgb(1, 0, 1));
    CHECK_MSG(set.find("GLCAD Dark")->get(token::geometry).r < 0.99f ||
                  set.find("GLCAD Dark")->get(token::geometry).b < 0.99f,
              "editing the copy changed the built-in");

    CHECK_MSG(!ed.duplicate_active("My Theme"), "a duplicate name was accepted");
    CHECK(!ed.duplicate_active(""));
}

TEST(theme_role_palette_covers_every_drawing_role) {
    const Theme light = Theme::light_default();
    const auto p = light.role_palette();

    const f32 lum = 0.2126f * p.geometry.r + 0.7152f * p.geometry.g +
                    0.0722f * p.geometry.b;
    CHECK_MSG(lum < 0.5f, "light-theme geometry has luminance %.3f", lum);

    Theme edited = light;
    edited.set(token::geometry, Color::rgb(0.1f, 0.2f, 0.3f));
    CHECK(std::fabs(edited.role_palette().geometry.r - 0.1f) < 1e-6f);
}

TEST(theme_editor_survives_a_theme_switch_underneath_it) {
    ThemeSet set;
    set.activate("GLCAD Dark");
    ThemeEditor ed(set);
    ed.set(token::geometry, Color::rgb(1, 0, 0));

    set.activate("GLCAD Light");
    CHECK(ed.value(token::geometry).r < 0.99f);
}
