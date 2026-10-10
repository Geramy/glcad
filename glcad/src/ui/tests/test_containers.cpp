#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ui/dock.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

using namespace gui;

namespace {

struct Frame {
    gtheme::ThemeSet themes;
    Context ctx;
    InputState in{};

    void begin(f32 w = 800, f32 h = 600) {
        ctx.begin({0, 0, w, h}, in, themes.active());
    }
    void end() { ctx.end(); }
    void press_at(f32 x, f32 y) {
        in.mouse = {x, y};
        in.pressed = true;
        in.down = true;
        in.released = false;
    }
    void release_at(f32 x, f32 y) {
        in.mouse = {x, y};
        in.pressed = false;
        in.down = false;
        in.released = true;
    }
    void idle() {
        in.pressed = false;
        in.released = false;
        in.down = false;
        in.wheel = 0.0f;
        in.key_tab = false;
        in.key_enter = false;
        in.typed.clear();
    }
};

}

TEST(gui_scroll_offset_is_clamped_to_the_content) {
    Frame f;
    ScrollState s;
    const Rect r{0, 0, 200, 100};

    f.begin();
    f.ctx.push_id("a");
    begin_scroll(f.ctx, r, s);
    end_scroll(f.ctx, r, s, 400.0f);
    f.ctx.pop_id();
    f.end();

    s.offset = 10000.0f;
    f.idle();
    f.begin();
    f.ctx.push_id("a");
    begin_scroll(f.ctx, r, s);
    end_scroll(f.ctx, r, s, 400.0f);
    f.ctx.pop_id();
    f.end();
    CHECK_MSG(std::fabs(s.offset - 300.0f) < 1e-3,
              "offset ran to %.3f, max is 300", s.offset);

    s.offset = -50.0f;
    f.begin();
    f.ctx.push_id("a");
    begin_scroll(f.ctx, r, s);
    end_scroll(f.ctx, r, s, 400.0f);
    f.ctx.pop_id();
    f.end();
    CHECK(s.offset >= 0.0f);
}

TEST(gui_scroll_wheel_moves_the_content_under_the_pointer) {
    Frame f;
    ScrollState s;
    s.content_extent = 400.0f;
    const Rect r{0, 0, 200, 100};

    f.in.mouse = {50, 50};
    f.in.wheel = -1.0f;
    f.begin();
    begin_scroll(f.ctx, r, s);
    end_scroll(f.ctx, r, s, 400.0f);
    f.end();
    CHECK_MSG(s.offset > 0.0f, "the wheel did nothing (%.3f)", s.offset);

    const f32 was = s.offset;
    f.in.mouse = {900, 900};
    f.in.wheel = -1.0f;
    f.begin();
    begin_scroll(f.ctx, r, s);
    end_scroll(f.ctx, r, s, 400.0f);
    f.end();
    CHECK_MSG(std::fabs(s.offset - was) < 1e-6,
              "a wheel outside the region scrolled it");
}

TEST(gui_no_thumb_when_everything_fits) {
    Frame f;
    ScrollState s;
    const Rect r{0, 0, 200, 400};

    f.begin();
    const std::size_t before = f.ctx.draw().commands().size();
    begin_scroll(f.ctx, r, s);
    end_scroll(f.ctx, r, s, 100.0f);
    const std::size_t after = f.ctx.draw().commands().size();
    f.end();
    CHECK_MSG(after - before <= 2, "%zu commands drawn for a full-fit region",
              after - before);
}

TEST(gui_tabs_report_the_one_that_was_clicked) {
    Frame f;
    const std::vector<std::string> labels = {"Model", "A-101", "A-102"};
    const Rect r{0, 0, 300, 30};

    f.begin();
    CHECK(tabs(f.ctx, r, labels, 0) == 0);
    f.end();

    f.idle();
    f.press_at(250, 15);
    f.begin();
    tabs(f.ctx, r, labels, 0);
    f.end();

    f.release_at(250, 15);
    f.begin();
    const int chosen = tabs(f.ctx, r, labels, 0);
    f.end();
    CHECK_MSG(chosen == 2, "clicked tab %d", chosen);
}

TEST(gui_tabs_of_nothing_are_harmless) {
    Frame f;
    f.begin();
    CHECK(tabs(f.ctx, {0, 0, 100, 30}, {}, 3) == 3);
    f.end();
}

namespace {
std::vector<TreeNode> sample_tree() {
    return {{"root", "Xrefs", 0, true, true, false, 0},
            {"a", "PLAN.dwg", 1, true, true, false, 0},
            {"a1", "A-WALL", 2, false, false, false, 0},
            {"a2", "A-DOOR", 2, false, false, false, 0},
            {"b", "SITE.dwg", 1, false, false, false, 0}};
}
}

TEST(gui_tree_draws_every_row_when_expanded) {
    Frame f;
    TreeState st;
    f.begin();
    const TreeResult r = tree(f.ctx, {0, 0, 240, 400}, sample_tree(), st);
    f.end();
    CHECK_MSG(r.rows_drawn == 5, "%zu rows", r.rows_drawn);
}

TEST(gui_tree_skips_the_children_of_a_folded_node) {
    Frame f;
    TreeState st;
    st.toggle("a");
    CHECK(st.is_collapsed("a"));

    f.begin();
    const TreeResult r = tree(f.ctx, {0, 0, 240, 400}, sample_tree(), st);
    f.end();
    CHECK_MSG(r.rows_drawn == 3, "%zu rows with one node folded", r.rows_drawn);
}

TEST(gui_tree_folding_the_root_hides_everything_below_it) {
    Frame f;
    TreeState st;
    st.toggle("root");
    f.begin();
    const TreeResult r = tree(f.ctx, {0, 0, 240, 400}, sample_tree(), st);
    f.end();
    CHECK(r.rows_drawn == 1);
}

TEST(gui_tree_selection_follows_a_click) {
    Frame f;
    TreeState st;
    f.begin();
    tree(f.ctx, {0, 0, 240, 400}, sample_tree(), st);
    f.end();

    f.idle();
    f.press_at(120, 14);
    f.begin();
    tree(f.ctx, {0, 0, 240, 400}, sample_tree(), st);
    f.end();

    f.release_at(120, 14);
    f.begin();
    const TreeResult r = tree(f.ctx, {0, 0, 240, 400}, sample_tree(), st);
    f.end();
    CHECK_MSG(r.clicked == "root", "clicked '%s'", r.clicked.c_str());
    CHECK(st.selected == "root");
}

TEST(gui_tree_state_toggle_is_reversible) {
    TreeState st;
    CHECK(!st.is_collapsed("x"));
    st.toggle("x");
    CHECK(st.is_collapsed("x"));
    st.toggle("x");
    CHECK(!st.is_collapsed("x"));
}

namespace {
std::vector<TableColumn> schedule_columns() {
    return {{"MARK", 80.0f, TextAlign::Left},
            {"WIDTH", 70.0f, TextAlign::Right},
            {"TYPE", 140.0f, TextAlign::Left}};
}
std::vector<std::vector<std::string>> schedule_rows() {
    return {{"D-01", "900", "SOLID CORE"},
            {"D-02", "800", "HOLLOW"},
            {"D-03", "1800", "DOUBLE"}};
}
}

TEST(gui_table_reports_the_cell_that_was_clicked) {
    Frame f;
    TableState st;
    const Rect r{0, 0, 300, 200};

    f.begin();
    table(f.ctx, r, schedule_columns(), schedule_rows(), st);
    f.end();

    const Style& style = f.ctx.style();
    const f32 first_row_y = style.row_height + style.row_height * 0.5f;

    f.idle();
    f.press_at(100, first_row_y);
    f.begin();
    table(f.ctx, r, schedule_columns(), schedule_rows(), st);
    f.end();

    f.release_at(100, first_row_y);
    f.begin();
    const TableResult res = table(f.ctx, r, schedule_columns(), schedule_rows(), st);
    f.end();
    CHECK_MSG(res.clicked_row == 0, "row %d", res.clicked_row);
    CHECK_MSG(res.clicked_column == 1, "column %d", res.clicked_column);
    CHECK(st.selected_row == 0);
}

TEST(gui_table_header_click_sorts_and_reverses) {
    Frame f;
    TableState st;
    const Rect r{0, 0, 300, 200};

    f.begin();
    table(f.ctx, r, schedule_columns(), schedule_rows(), st);
    f.end();

    auto click_header = [&](f32 x) {
        f.idle();
        f.press_at(x, 14);
        f.begin();
        table(f.ctx, r, schedule_columns(), schedule_rows(), st);
        f.end();
        f.release_at(x, 14);
        f.begin();
        const TableResult res =
            table(f.ctx, r, schedule_columns(), schedule_rows(), st);
        f.end();
        return res;
    };

    const TableResult a = click_header(40);
    CHECK(a.sort_changed);
    CHECK(st.sort_column == 0);
    CHECK(st.sort_ascending);

    click_header(40);
    CHECK_MSG(!st.sort_ascending, "the second click did not reverse the sort");

    click_header(120);
    CHECK(st.sort_column == 1);
    CHECK_MSG(st.sort_ascending, "a new column should start ascending");
}

TEST(gui_table_of_nothing_is_harmless) {
    Frame f;
    TableState st;
    f.begin();
    const TableResult r = table(f.ctx, {0, 0, 200, 100}, {}, {}, st);
    f.end();
    CHECK(r.clicked_row == -1);
}

TEST(gui_tab_moves_focus_through_the_controls) {
    Frame f;
    auto draw = [&]() {
        f.begin();
        button(f.ctx, {0, 0, 80, 24}, "One");
        button(f.ctx, {0, 30, 80, 24}, "Two");
        button(f.ctx, {0, 60, 80, 24}, "Three");
        f.end();
    };

    draw();
    CHECK_MSG(f.ctx.focusable_count() == 3, "%zu focusables",
              f.ctx.focusable_count());

    f.idle();
    f.in.key_tab = true;
    draw();
    const auto first = f.ctx.accessibility_tree();
    bool any_focused = false;
    for (const auto& a : first)
        if (a.focused) any_focused = true;
    CHECK_MSG(any_focused, "Tab focused nothing");
}

TEST(gui_tab_wraps_around) {
    Frame f;
    auto draw = [&]() {
        f.begin();
        button(f.ctx, {0, 0, 80, 24}, "One");
        button(f.ctx, {0, 30, 80, 24}, "Two");
        f.end();
    };

    draw();
    std::string focused;
    for (int i = 0; i < 5; ++i) {
        f.idle();
        f.in.key_tab = true;
        draw();
        for (const auto& a : f.ctx.accessibility_tree())
            if (a.focused) focused = a.label;
    }
    CHECK_MSG(!focused.empty(), "focus was lost while tabbing");
}

TEST(gui_enter_activates_the_focused_button) {
    Frame f;
    auto draw = [&](bool& pressed) {
        f.begin();
        pressed = button(f.ctx, {0, 0, 80, 24}, "Go");
        f.end();
    };

    bool pressed = false;
    draw(pressed);
    CHECK(!pressed);

    f.idle();
    f.in.key_tab = true;
    draw(pressed);

    f.idle();
    f.in.key_enter = true;
    draw(pressed);
    CHECK_MSG(pressed, "Enter did not press the focused button");
}

TEST(gui_space_activates_too) {
    Frame f;
    bool value = false;
    auto draw = [&]() {
        f.begin();
        toggle(f.ctx, {0, 0, 80, 24}, "Ortho", value);
        f.end();
    };
    draw();
    f.idle();
    f.in.key_tab = true;
    draw();
    f.idle();
    f.in.typed = " ";
    draw();
    CHECK_MSG(value, "Space did not toggle the focused control");
}

TEST(gui_every_control_announces_itself) {
    Frame f;
    bool flag = false;
    std::string text = "A-WALL";
    f.begin();
    button(f.ctx, {0, 0, 80, 24}, "Draw");
    toggle(f.ctx, {0, 30, 80, 24}, "Ortho", flag);
    checkbox(f.ctx, {0, 60, 80, 24}, "Plottable", flag);
    text_field(f.ctx, {0, 90, 160, 24}, "Layer", text);
    f.end();

    const auto& tree = f.ctx.accessibility_tree();
    CHECK_MSG(tree.size() == 4, "%zu announcements", tree.size());

    bool has_button = false, has_field = false;
    for (const auto& a : tree) {
        CHECK_MSG(!a.label.empty(), "an element announced no label");
        if (a.role == Context::Role::Button) has_button = true;
        if (a.role == Context::Role::Field) {
            has_field = true;
            CHECK_MSG(a.value == "A-WALL", "the field's value was not announced");
        }
    }
    CHECK(has_button && has_field);
}

TEST(gui_a_toggle_announces_its_state_not_just_its_name) {
    Frame f;
    bool on = true;
    f.begin();
    toggle(f.ctx, {0, 0, 80, 24}, "Ortho", on);
    f.end();

    const auto& tree = f.ctx.accessibility_tree();
    CHECK(tree.size() == 1);
    CHECK_MSG(tree[0].value == "on", "announced '%s'", tree[0].value.c_str());
    CHECK(tree[0].selected);
}

TEST(gui_focused_description_reads_as_a_sentence) {
    Frame f;
    std::string text = "12.5";
    auto draw = [&]() {
        f.begin();
        text_field(f.ctx, {0, 0, 160, 24}, "Radius", text);
        f.end();
    };
    draw();
    f.idle();
    f.in.key_tab = true;
    draw();

    const std::string said = f.ctx.focused_description();
    CHECK_MSG(said.find("Radius") != std::string::npos, "said '%s'", said.c_str());
    CHECK_MSG(said.find("text field") != std::string::npos, "said '%s'",
              said.c_str());
    CHECK(said.find("12.5") != std::string::npos);
}

TEST(gui_a_disabled_control_says_so) {
    Frame f;
    f.begin();
    button(f.ctx, {0, 0, 80, 24}, "Plot", false);
    f.end();
    const auto& tree = f.ctx.accessibility_tree();
    CHECK(tree.size() == 1);
    CHECK_MSG(tree[0].disabled, "a disabled button did not announce it");
    CHECK(f.ctx.focusable_count() == 0);
}

TEST(gui_the_tree_is_rebuilt_every_frame) {
    Frame f;
    f.begin();
    button(f.ctx, {0, 0, 80, 24}, "One");
    f.end();
    CHECK(f.ctx.accessibility_tree().size() == 1);

    f.idle();
    f.begin();
    button(f.ctx, {0, 0, 80, 24}, "One");
    button(f.ctx, {0, 30, 80, 24}, "Two");
    f.end();
    CHECK(f.ctx.accessibility_tree().size() == 2);
}

TEST(gui_every_role_has_a_name) {
    for (int i = 0; i <= static_cast<int>(Context::Role::Canvas); ++i) {
        const auto r = static_cast<Context::Role>(i);
        if (r == Context::Role::None) continue;
        CHECK_MSG(std::string(Context::role_name(r)).size() > 0,
                  "role %d has no name", i);
    }
}

TEST(gui_drop_target_follows_the_nearest_edge) {
    Dock d;
    const Rect area{0, 0, 1000, 800};
    bool valid = false;

    CHECK(d.drop_target(area, {20, 400}, valid) == Side::Left);
    CHECK(valid);
    CHECK(d.drop_target(area, {980, 400}, valid) == Side::Right);
    CHECK(d.drop_target(area, {500, 20}, valid) == Side::Top);
    CHECK(d.drop_target(area, {500, 780}, valid) == Side::Bottom);
}

TEST(gui_the_middle_means_float_here) {
    Dock d;
    const Rect area{0, 0, 1000, 800};
    bool valid = false;
    CHECK(d.drop_target(area, {500, 400}, valid) == Side::Floating);
    CHECK(valid);
}

TEST(gui_a_drop_outside_the_window_is_refused) {
    Dock d;
    const Rect area{0, 0, 1000, 800};
    bool valid = true;
    d.drop_target(area, {-50, 400}, valid);
    CHECK_MSG(!valid, "a drop outside the window was accepted");
}

TEST(gui_drop_preview_shows_where_it_will_land) {
    Dock d;
    const Rect area{0, 0, 1000, 800};
    const f32 extent = 280.0f;

    const Rect left = d.drop_preview(area, Side::Left, extent);
    CHECK_MSG(std::fabs(left.w - extent) < 1e-6,
              "the preview is %.1f wide for a %.1f panel", left.w, extent);
    CHECK(std::fabs(left.x - area.x) < 1e-6);
    CHECK(std::fabs(left.h - area.h) < 1e-6);
    CHECK(left.w < area.w);

    const Rect right = d.drop_preview(area, Side::Right, extent);
    CHECK_MSG(right.x > area.w * 0.5f, "the right preview is at x=%.1f", right.x);

    const Rect top = d.drop_preview(area, Side::Top, extent);
    CHECK(std::fabs(top.w - area.w) < 1e-6);
    CHECK_MSG(std::fabs(top.h - extent) < 1e-6,
              "the preview is %.1f tall for a %.1f panel", top.h, extent);

    const Rect middle = d.drop_preview(area, Side::Floating, extent);
    CHECK(middle.x > area.x && middle.right() < area.right());
}

TEST(gui_dock_is_not_dragging_until_a_title_bar_is_grabbed) {
    Dock d;
    PanelState p;
    p.id = "layers";
    p.title = "Layers";
    d.add(p);
    CHECK(!d.dragging());
    CHECK(!d.drag().active);
}

TEST(gui_every_icon_has_a_symbol) {
    for (u16 v = 1; v <= static_cast<u16>(Icon::DeleteBody); ++v) {
        const Icon i = static_cast<Icon>(v);
        CHECK_MSG(icon_codepoint(i) != 0, "icon %u (%s) has no codepoint", v,
                  icon_name(i));
        CHECK_MSG(icon_symbol_name(i)[0] != '\0', "icon %u has no symbol name", v);
    }
    CHECK(icon_codepoint(Icon::None) == 0);
}

static bool drawn_as_vector(Icon i) {
    switch (i) {
        case Icon::Radius: case Icon::Angle: case Icon::Cylinder: case Icon::Sphere:
        case Icon::EllipseShape: case Icon::Draft: case Icon::Slice: return true;
        default: return false;
    }
}

TEST(gui_icon_codepoints_are_private_use_and_unique) {
    std::vector<u32> seen;
    for (u16 v = 1; v <= static_cast<u16>(Icon::Options); ++v) {
        const Icon i = static_cast<Icon>(v);
        const u32 cp = icon_codepoint(i);
        if (drawn_as_vector(i)) { CHECK_MSG(cp == 0, "icon %u is listed as vector but has a glyph", v); continue; }
        CHECK_MSG(cp >= 0xE000u && cp <= 0xF8FFu, "icon %u maps to U+%04X, outside the private use area", v, cp);
        seen.push_back(cp);
    }
    std::sort(seen.begin(), seen.end());
    for (std::size_t i = 1; i < seen.size(); ++i)
        CHECK_MSG(seen[i] != seen[i - 1], "two icons share U+%04X", seen[i]);
}

TEST(gui_icon_codepoint_list_matches_the_enum) {
    const auto& all = icon_codepoints();
    std::size_t glyphs = 0;
    for (u16 v = 1; v <= static_cast<u16>(Icon::Options); ++v) {
        const Icon i = static_cast<Icon>(v);
        if (drawn_as_vector(i)) continue;
        ++glyphs;
        CHECK_MSG(std::find(all.begin(), all.end(), icon_codepoint(i)) != all.end(),
                  "icon %u is drawable but not in the atlas list", v);
    }
    CHECK_MSG(all.size() == glyphs, "the atlas gets %zu codepoints for %zu glyph icons", all.size(), glyphs);
}

TEST(gui_icon_utf8_round_trips) {
    for (u16 v = 1; v <= static_cast<u16>(Icon::DeleteBody); ++v) {
        const Icon i = static_cast<Icon>(v);
        const std::string s = icon_utf8(i);
        CHECK_MSG(s.size() == 3, "%s encoded to %zu bytes", icon_name(i), s.size());
        const u32 cp = (static_cast<u32>(s[0] & 0x0F) << 12) |
                       (static_cast<u32>(s[1] & 0x3F) << 6) |
                       static_cast<u32>(s[2] & 0x3F);
        CHECK(cp == icon_codepoint(i));
    }
    CHECK(icon_utf8(Icon::None).empty());
}
