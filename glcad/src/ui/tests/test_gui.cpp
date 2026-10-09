#include "test.hpp"

#include <cmath>
#include <functional>

#include "ui/access.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"

using namespace gui;

static gtheme::Theme theme() { return gtheme::Theme::dark_default(); }

struct Harness {
    Context      ctx;
    gtheme::Theme th{theme()};
    InputState   in;

    void frame(const std::function<void(Context&)>& body,
               const Rect& vp = {0, 0, 1440, 900}) {
        ctx.begin(vp, in, th);
        body(ctx);
        ctx.end();
        in.pressed = false;
        in.released = false;
        in.right_pressed = false;
        in.typed.clear();
        in.key_enter = in.key_escape = in.key_backspace = false;
    }
    void move_to(f32 x, f32 y) { in.mouse = {x, y}; }
    void press() { in.pressed = true; in.down = true; }
    void release() { in.released = true; in.down = false; }
    void click_at(f32 x, f32 y, const std::function<void(Context&)>& body) {
        move_to(x, y);
        frame(body);
        press();
        frame(body);
        release();
        frame(body);
    }
};

TEST(gui_rect_cutting_is_the_layout_primitive) {
    Rect r{0, 0, 100, 100};
    const Rect top = r.cut_top(20);
    CHECK(top.y == 0 && top.h == 20);
    CHECK(r.y == 20 && r.h == 80);

    const Rect left = r.cut_left(30);
    CHECK(left.w == 30);
    CHECK(r.x == 30 && r.w == 70);

    Rect small{0, 0, 10, 10};
    const Rect all = small.cut_top(50);
    CHECK(all.h == 10);
    CHECK(small.h == 0);
    CHECK(small.empty());
}

TEST(gui_rect_containment_and_intersection) {
    Rect a{10, 10, 100, 50};
    CHECK(a.contains({50, 30}));
    CHECK(!a.contains({5, 30}));
    CHECK(!a.contains({110, 30}));

    Rect b{60, 30, 100, 50};
    CHECK(a.intersects(b));
    const Rect i = a.intersect(b);
    CHECK(i.x == 60 && i.y == 30 && i.w == 50 && i.h == 30);

    Rect far{500, 500, 10, 10};
    CHECK(!a.intersects(far));
    CHECK(a.intersect(far).empty());
}

TEST(gui_insets_deflate_and_inflate) {
    Rect r{0, 0, 100, 100};
    const Rect d = r.deflate(Insets::all(10));
    CHECK(d.x == 10 && d.w == 80);
    const Rect back = d.inflate(Insets::all(10));
    CHECK(back.x == 0 && back.w == 100);

    CHECK(r.deflate(Insets::all(80)).empty());
}

TEST(gui_ids_are_stable_and_scoped) {
    Context c;
    const Id a = c.id("button");
    const Id b = c.id("button");
    CHECK(a == b);

    c.push_id("panel-1");
    const Id inner = c.id("button");
    c.pop_id();
    CHECK_MSG(inner != a, "id scoping must distinguish identical labels");

    c.push_id("panel-2");
    const Id other = c.id("button");
    c.pop_id();
    CHECK(other != inner);
}

TEST(gui_widget_state_persists_across_frames) {
    Harness h;
    Id id = 0;
    h.frame([&](Context& c) {
        id = c.id("slider");
        c.state_f32(id, 0.5f) = 0.75f;
    });
    h.frame([&](Context& c) {
        CHECK(std::fabs(c.state_f32(id, 0.5f) - 0.75f) < 1e-6f);
    });
}

TEST(gui_button_reports_a_click_only_on_release_inside) {
    Harness h;
    bool clicked = false;
    const Rect r{100, 100, 120, 30};

    h.click_at(150, 115, [&](Context& c) {
        if (button(c, r, "OK")) clicked = true;
    });
    CHECK(clicked);

    clicked = false;
    h.move_to(150, 115);
    h.frame([&](Context& c) { button(c, r, "OK2"); });
    h.press();
    h.frame([&](Context& c) { button(c, r, "OK2"); });
    h.move_to(600, 600);
    h.release();
    h.frame([&](Context& c) { if (button(c, r, "OK2")) clicked = true; });
    CHECK_MSG(!clicked, "releasing outside must not fire the button");
}

TEST(gui_toggle_flips_its_value) {
    Harness h;
    bool ortho = false;
    const Rect r{0, 0, 80, 26};

    h.click_at(40, 13, [&](Context& c) { toggle(c, r, "ORTHO", ortho); });
    CHECK(ortho);
    h.click_at(40, 13, [&](Context& c) { toggle(c, r, "ORTHO", ortho); });
    CHECK(!ortho);
}

TEST(gui_disabled_widgets_do_not_respond) {
    Harness h;
    bool clicked = false;
    h.click_at(50, 15, [&](Context& c) {
        if (button(c, {0, 0, 100, 30}, "Disabled", false)) clicked = true;
    });
    CHECK(!clicked);
}

TEST(gui_wants_mouse_tells_the_canvas_to_stand_down) {
    Harness h;
    const Rect r{200, 200, 100, 30};

    h.move_to(250, 215);
    h.frame([&](Context& c) { button(c, r, "Hit"); });
    h.frame([&](Context& c) {
        button(c, r, "Hit");
        CHECK_MSG(c.wants_mouse(), "pointer over a widget must claim the mouse");
    });

    h.move_to(900, 700);
    h.frame([&](Context& c) { button(c, r, "Hit"); });
    h.frame([&](Context& c) {
        button(c, r, "Hit");
        CHECK(!c.wants_mouse());
    });
}

TEST(gui_clipping_blocks_interaction_outside_the_clip) {
    Harness h;
    bool clicked = false;
    h.click_at(50, 300, [&](Context& c) {
        c.push_clip({0, 0, 400, 200});
        if (button(c, {0, 280, 100, 30}, "Scrolled away")) clicked = true;
        c.pop_clip();
    });
    CHECK_MSG(!clicked, "a clipped-away widget must not be interactive");
}

TEST(gui_scrolling_overlay_clips_its_own_controls_and_restores_parent_clip) {
    Harness h;
    bool hidden_clicked = false, visible_clicked = false;
    const Rect view{100, 100, 300, 100};
    const auto body = [&](Context& c) {
        c.push_clip({0, 0, 50, 50});
        c.begin_overlay({80, 80, 350, 350});
        c.push_clip(view);
        if (button(c, {110, 180, 100, 40}, "Partial")) visible_clicked = true;
        if (button(c, {110, 250, 100, 30}, "Hidden")) hidden_clicked = true;
        c.pop_clip();
        c.end_overlay();
        CHECK(c.clip().w == 50);
        c.pop_clip();
    };
    h.click_at(130, 265, body); CHECK(!hidden_clicked);
    h.click_at(130, 210, body); CHECK(!visible_clicked);
    h.click_at(130, 190, body); CHECK(visible_clicked);
    bool overlay_clip = false;
    const auto& commands = h.ctx.draw().commands();
    for (std::size_t i = h.ctx.overlay_start(); i < commands.size(); ++i)
        if (commands[i].prim == Prim::Clip && commands[i].rect.x == view.x &&
            commands[i].rect.y == view.y && commands[i].rect.h == view.h) overlay_clip = true;
    CHECK(overlay_clip);
}

TEST(gui_panel_emits_surface_border_and_returns_content) {
    Harness h;
    Rect content{};
    h.frame([&](Context& c) {
        content = panel(c, {10, 10, 300, 400}, "Layers");
        CHECK(c.draw().count(Prim::RoundRect) >= 1);
        CHECK(c.draw().count(Prim::Border) >= 1);
        CHECK(c.draw().count(Prim::Text) >= 1);
    });
    CHECK(content.x > 10);
    CHECK(content.y > 10 + 20);
    CHECK(content.w < 300);
}

TEST(gui_no_widget_names_a_colour) {
    Harness dark;
    Harness light;
    light.th = gtheme::Theme::light_default();

    gui::Color dark_bg{}, light_bg{};
    dark.frame([&](Context& c) {
        panel(c, {0, 0, 200, 100});
        dark_bg = c.draw().commands()[0].color;
    });
    light.frame([&](Context& c) {
        panel(c, {0, 0, 200, 100});
        light_bg = c.draw().commands()[0].color;
    });
    CHECK_MSG(dark_bg.luminance() < light_bg.luminance(),
              "panel surface did not follow the theme");
}

TEST(gui_stack_lays_out_without_manual_arithmetic) {
    Harness h;
    h.frame([&](Context& c) {
        Stack s(c, {0, 0, 200, 300});
        const Rect a = s.next(30);
        s.gap();
        const Rect b = s.next(30);
        CHECK(a.y == 0 && a.h == 30);
        CHECK(b.y > a.bottom());
        CHECK(s.remaining().h < 300);
    });
}

TEST(gui_slider_tracks_the_pointer) {
    Harness h;
    f32 v = 0.0f;
    const Rect r{0, 0, 200, 20};

    h.move_to(100, 10);
    h.frame([&](Context& c) { slider(c, r, "s", v, 0.0f, 1.0f); });
    h.press();
    h.frame([&](Context& c) { slider(c, r, "s", v, 0.0f, 1.0f); });
    CHECK_MSG(std::fabs(v - 0.5f) < 0.01f, "slider value %.3f", v);

    h.move_to(200, 10);
    h.frame([&](Context& c) { slider(c, r, "s", v, 0.0f, 1.0f); });
    CHECK(std::fabs(v - 1.0f) < 0.01f);
}

TEST(gui_dropdown_opens_and_selects) {
    Harness h;
    std::vector<std::string> items = {"Continuous", "Dashed", "Hidden"};
    i32 sel = 0;

    const Style& st = h.ctx.style();
    const f32 closed_h = st.control_height;
    const Rect box{0, 0, 160, closed_h};

    h.click_at(50, closed_h * 0.5f,
               [&](Context& c) { dropdown(c, box, "lt", items, sel); });
    const f32 row_y = closed_h + 2.0f + st.row_height_dense * 2.5f;
    h.click_at(50, row_y, [&](Context& c) { dropdown(c, box, "lt", items, sel); });
    CHECK_MSG(sel == 2, "expected selection 2, got %d", sel);
}

TEST(gui_dropdown_announces_value_and_supports_keyboard_activation) {
    Harness h;std::vector<std::string> items={"Default","Serif"};i32 selected=0;
    const auto draw=[&](Context& c){dropdown(c,{10,10,160,40},"Text font",items,selected);};
    h.frame(draw);
    Id control=0;for(const auto& a:h.ctx.accessibility_tree())if(a.label=="Text font") {control=a.id;CHECK(a.value=="Default");}
    CHECK(control!=0);h.ctx.set_focus(control);h.in.key_enter=true;h.frame(draw);
    Id row=0;for(const auto& a:h.ctx.accessibility_tree())if(a.label=="Serif")row=a.id;
    CHECK(row!=0);h.ctx.set_focus(row);h.in.key_enter=true;h.frame(draw);
    CHECK(selected==1);
}

TEST(gui_text_field_accepts_typing_when_focused) {
    Harness h;
    std::string value;
    const Rect r{0, 0, 200, 26};

    h.click_at(100, 13, [&](Context& c) { text_field(c, r, "name", value); });
    h.in.typed = "A-WALL";
    h.frame([&](Context& c) { text_field(c, r, "name", value); });
    CHECK(value == "A-WALL");

    h.in.key_backspace = true;
    h.frame([&](Context& c) { text_field(c, r, "name", value); });
    CHECK(value == "A-WAL");
}

TEST(gui_command_bar_captures_typing_and_submit) {
    Harness h;
    CommandBarState st;
    st.prompt = "Specify next point";
    st.completions = {"LINE", "LINETYPE"};
    st.keywords = {"Close", "Undo"};

    h.in.typed = "L";
    h.frame([&](Context& c) { command_bar(c, {0, 860, 600, 30}, st); });
    CHECK(st.input == "L");
    CHECK(!st.submitted);

    h.in.key_enter = true;
    h.frame([&](Context& c) { command_bar(c, {0, 860, 600, 30}, st); });
    CHECK(!st.submitted);
    CHECK_MSG(st.chosen_completion == 0, "chosen %d", st.chosen_completion);

    h.in.key_enter = false;
    h.in.key_down = true;
    h.frame([&](Context& c) { command_bar(c, {0, 860, 600, 30}, st); });
    CHECK(st.highlight == 1);
    h.in.key_down = false;
    h.in.key_enter = true;
    h.frame([&](Context& c) { command_bar(c, {0, 860, 600, 30}, st); });
    CHECK_MSG(st.chosen_completion == 1, "chosen %d", st.chosen_completion);

    st.completions.clear();
    h.in.key_enter = true;
    h.frame([&](Context& c) { command_bar(c, {0, 860, 600, 30}, st); });
    CHECK(st.submitted);
}

TEST(gui_layer_row_toggles_report_changes) {
    Harness h;
    LayerRow row;
    row.name = "A-WALL";
    row.color = gui::Color::rgb(1, 0, 0);

    bool changed = false;
    h.click_at(12, 13, [&](Context& c) {
        if (layer_row(c, {0, 0, 260, 26}, row)) changed = true;
    });
    CHECK_MSG(changed, "clicking the on/off toggle must report a change");
    CHECK(!row.on);
}

TEST(gui_tool_button_shows_a_tooltip_on_hover) {
    Harness h;
    h.move_to(28, 28);
    h.frame([&](Context& c) { tool_button(c, {8, 8, 40, 40}, 0x25A1, "Rectangle", false); });
    h.frame([&](Context& c) {
        tool_button(c, {8, 8, 40, 40}, 0x25A1, "Rectangle", false);
        CHECK(c.draw().count(Prim::Shadow) >= 1);
        CHECK(c.draw().count(Prim::Text) >= 1);
    });
}

TEST(gui_touch_targets_meet_the_accessibility_minimum) {
    Context c;
    CHECK(c.style().min_touch_target >= 44.0f);
}

TEST(gui_focus_clears_on_a_click_in_empty_space) {
    Harness h;
    std::string value;
    const Rect field{0, 0, 200, 26};

    h.click_at(100, 13, [&](Context& c) { text_field(c, field, "f", value); });
    h.frame([&](Context& c) {
        text_field(c, field, "f", value);
        CHECK(c.wants_keys());
    });

    h.click_at(900, 700, [&](Context& c) { text_field(c, field, "f", value); });
    h.frame([&](Context& c) {
        text_field(c, field, "f", value);
        CHECK_MSG(!c.wants_keys(), "clicking empty space must drop focus");
    });
}

static std::vector<RingItem> ring_items(std::size_t n) {
    std::vector<RingItem> v;
    for (std::size_t i = 0; i < n; ++i) {
        RingItem it;
        it.label = "item" + std::to_string(i);
        it.id = static_cast<u32>(i);
        it.icon = static_cast<u32>(Icon::Line);
        v.push_back(it);
    }
    return v;
}

TEST(ring_selects_by_direction_not_by_distance) {
    const std::size_t n = 8;
    for (f32 d : {40.0f, 120.0f, 900.0f}) {
        CHECK_MSG(ring_sector({0.0f, -d}, n, 24.0f) == 0,
                  "up at %.0f px chose %d", d, ring_sector({0, -d}, n, 24.0f));
        CHECK(ring_sector({d, 0.0f}, n, 24.0f) == 2);
        CHECK(ring_sector({0.0f, d}, n, 24.0f) == 4);
        CHECK(ring_sector({-d, 0.0f}, n, 24.0f) == 6);
    }
}

TEST(ring_item_zero_is_centred_at_twelve_oclock) {
    const std::size_t n = 6;
    for (f32 wobble : {-8.0f, -3.0f, 0.0f, 3.0f, 8.0f})
        CHECK_MSG(ring_sector({wobble, -100.0f}, n, 24.0f) == 0,
                  "a %.0f px wobble picked %d", wobble,
                  ring_sector({wobble, -100.0f}, n, 24.0f));
}

TEST(ring_dead_zone_is_the_way_out) {
    CHECK(ring_sector({0.0f, 0.0f}, 8, 24.0f) == -1);
    CHECK(ring_sector({10.0f, 10.0f}, 8, 24.0f) == -1);
    CHECK(ring_sector({0.0f, -23.9f}, 8, 24.0f) == -1);
    CHECK(ring_sector({0.0f, -24.1f}, 8, 24.0f) == 0);
}

TEST(ring_sectors_are_even_and_cover_the_circle) {
    for (std::size_t n : {2u, 3u, 4u, 5u, 6u, 8u, 12u}) {
        std::vector<int> hits(n, 0);
        for (int deg = 0; deg < 360; ++deg) {
            const f32 a = static_cast<f32>(deg) * 3.14159265f / 180.0f;
            const i32 s = ring_sector({std::sin(a) * 100.0f, -std::cos(a) * 100.0f},
                                      n, 24.0f);
            CHECK_MSG(s >= 0 && s < static_cast<i32>(n),
                      "%zu items: %d degrees fell outside the ring", n, deg);
            ++hits[static_cast<std::size_t>(s)];
        }
        for (std::size_t i = 0; i < n; ++i)
            CHECK_MSG(hits[i] > 0, "%zu items: item %zu is unreachable", n, i);
        const int want = 360 / static_cast<int>(n);
        for (std::size_t i = 0; i < n; ++i)
            CHECK_MSG(std::abs(hits[i] - want) <= 2,
                      "%zu items: item %zu got %d degrees, not about %d", n, i,
                      hits[i], want);
    }
}

TEST(ring_item_centres_sit_on_the_circle_where_their_sector_points) {
    const std::size_t n = 8;
    const Vec2 c{500.0f, 400.0f};
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 p = ring_item_centre(c, i, n, 100.0f);
        CHECK(std::fabs(std::hypot(p.x - c.x, p.y - c.y) - 100.0f) < 1e-3f);
        CHECK_MSG(ring_sector({p.x - c.x, p.y - c.y}, n, 24.0f) ==
                      static_cast<i32>(i),
                  "item %zu is drawn where item %d is picked", i,
                  ring_sector({p.x - c.x, p.y - c.y}, n, 24.0f));
    }
}

TEST(ring_empty_menu_never_opens) {
    Harness h;
    RingState s;
    s.open = true;
    i32 chosen = 0;
    h.move_to(500, 400);
    h.frame([&](Context& c) { chosen = ring(c, s, {}); });
    CHECK(chosen == -1);
    CHECK(!s.open);
}

TEST(long_press_arms_only_after_the_hold_time) {
    Harness h;
    RingState s;
    h.move_to(300, 300);
    h.in.time = 0.0;
    h.press();
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    CHECK(!s.open);

    h.in.time = 0.20;
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    CHECK(!s.open);

    h.in.time = 0.40;
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    CHECK_MSG(s.open, "a 0.4 s press did not arm a 0.35 s ring");
}

TEST(long_press_that_moves_is_a_drag_not_a_press) {
    Harness h;
    RingState s;
    h.move_to(300, 300);
    h.in.time = 0.0;
    h.press();
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });

    h.in.time = 0.10;
    h.move_to(340, 300);
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });

    h.in.time = 1.00;
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    CHECK_MSG(!s.open, "a drag opened the ring");
}

TEST(long_press_tolerates_a_small_wobble) {
    Harness h;
    RingState s;
    h.move_to(300, 300);
    h.in.time = 0.0;
    h.press();
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    h.in.time = 0.20;
    h.move_to(303, 302);
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    h.in.time = 0.50;
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    CHECK(s.open);
}

TEST(ring_opens_where_the_press_began) {
    Harness h;
    RingState s;
    h.move_to(300, 300);
    h.in.time = 0.0;
    h.press();
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    h.in.time = 0.20;
    h.move_to(304, 301);
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    h.in.time = 0.60;
    h.frame([&](Context& c) { (void)c; arm_long_press(s, h.in); });
    CHECK(s.open);
    CHECK_MSG(std::fabs(s.centre.x - 300.0f) < 1e-3f &&
                  std::fabs(s.centre.y - 300.0f) < 1e-3f,
              "the ring opened at %.1f,%.1f", s.centre.x, s.centre.y);
}

TEST(ring_release_over_a_sector_chooses_it) {
    Harness h;
    RingState s;
    const auto items = ring_items(4);
    s.open = true;
    s.centre = {500, 400};

    h.in.down = true;
    h.move_to(500, 250);
    i32 got = -1;
    h.frame([&](Context& c) { got = ring(c, s, items); });
    CHECK(got == -1);
    CHECK(s.hovered == 0);

    h.release();
    h.frame([&](Context& c) { got = ring(c, s, items); });
    CHECK_MSG(got == 0, "released over item 0 and got %d", got);
    CHECK(!s.open);
    CHECK(s.chosen == 0);
}

TEST(ring_release_in_the_dead_zone_chooses_nothing) {
    Harness h;
    RingState s;
    const auto items = ring_items(6);
    s.open = true;
    s.centre = {500, 400};

    h.in.down = true;
    h.move_to(502, 398);
    i32 got = 0;
    h.frame([&](Context& c) { got = ring(c, s, items); });
    CHECK(s.hovered == -1);
    h.release();
    h.frame([&](Context& c) { got = ring(c, s, items); });
    CHECK_MSG(got == -1, "the dead zone chose item %d", got);
    CHECK(!s.open);
}

TEST(ring_escape_closes_without_choosing) {
    Harness h;
    RingState s;
    const auto items = ring_items(6);
    s.open = true;
    s.centre = {500, 400};
    h.move_to(500, 250);
    h.in.key_escape = true;
    i32 got = 0;
    h.frame([&](Context& c) { got = ring(c, s, items); });
    CHECK(got == -1);
    CHECK(!s.open);
}

TEST(ring_will_not_choose_a_disabled_item) {
    Harness h;
    RingState s;
    auto items = ring_items(4);
    items[0].enabled = false;
    s.open = true;
    s.centre = {500, 400};

    h.in.down = true;
    h.move_to(500, 250);
    i32 got = 0;
    h.frame([&](Context& c) { got = ring(c, s, items); });
    CHECK_MSG(s.hovered == -1, "a disabled item highlighted");
    h.release();
    h.frame([&](Context& c) { got = ring(c, s, items); });
    CHECK(got == -1);
}

TEST(ring_announces_every_item_separately) {
    Harness h;
    RingState s;
    const auto items = ring_items(5);
    s.open = true;
    s.centre = {500, 400};
    h.in.down = true;
    h.move_to(500, 250);
    h.frame([&](Context& c) {
        ring(c, s, items);
        CHECK_MSG(c.accessibility_tree().size() >= items.size(),
                  "%zu items announced for a %zu item ring",
                  c.accessibility_tree().size(), items.size());
        std::size_t selected = 0;
        for (const auto& a : c.accessibility_tree()) if (a.selected) ++selected;
        CHECK_MSG(selected == 1, "%zu items reported as current", selected);
    });
}

TEST(ring_draws_something_for_every_item) {
    Harness h;
    RingState s;
    const auto items = ring_items(6);
    s.open = true;
    s.centre = {700, 450};
    h.in.down = true;
    h.move_to(700, 300);
    h.frame([&](Context& c) {
        ring(c, s, items);
        CHECK_MSG(c.draw().commands().size() >= items.size(),
                  "%zu draw commands for %zu items",
                  c.draw().commands().size(), items.size());
    });
}

TEST(measure_counts_codepoints_not_bytes_without_a_font) {
    Harness h;
    h.frame([&](Context& c) {
        CHECK(!c.has_real_metrics());
        const f32 ascii = c.measure("abcde", 12.0f);
        const f32 accented = c.measure("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",
                                       12.0f);
        CHECK_MSG(std::fabs(ascii - accented) < 1e-3f,
                  "five letters measured %.2f and %.2f", ascii, accented);
        CHECK(c.measure("", 12.0f) == 0.0f);
        CHECK(std::fabs(c.measure("abc", 24.0f) - 2.0f * c.measure("abc", 12.0f)) <
              1e-3f);
    });
}

TEST(measure_uses_the_installed_font_when_there_is_one) {
    Harness h;
    h.ctx.set_measure([](std::string_view s, f32 size) {
        f32 w = 0.0f;
        for (char ch : s) w += (ch == 'i' ? 0.25f : ch == 'W' ? 0.9f : 0.5f) * size;
        return w;
    });
    h.frame([&](Context& c) {
        CHECK(c.has_real_metrics());
        CHECK_MSG(c.measure("iii", 10.0f) < c.measure("WWW", 10.0f),
                  "a proportional font measured iii and WWW the same");
        CHECK(std::fabs(c.measure("iii", 10.0f) - 7.5f) < 1e-4f);
    });
}

TEST(fit_never_cuts_a_character_in_half) {
    Harness h;
    const std::string text = "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9";
    h.frame([&](Context& c) {
        for (f32 w = 0.0f; w <= 40.0f; w += 0.7f) {
            const std::size_t n = c.fit(text, 10.0f, w);
            CHECK_MSG(n % 2 == 0, "%.1f px cut %zu bytes into a 2-byte glyph",
                      w, n);
            CHECK(n <= text.size());
            if (n > 0)
                CHECK_MSG(c.measure(text.substr(0, n), 10.0f) <= w + 1e-3f,
                          "%zu bytes do not fit in %.1f px", n, w);
        }
        CHECK(c.fit(text, 10.0f, 1000.0f) == text.size());
        CHECK(c.fit(text, 10.0f, 0.0f) == 0);
    });
}

TEST(caret_at_snaps_to_the_nearer_boundary) {
    Harness h;
    h.frame([&](Context& c) {
        const std::string text = "abcd";
        const f32 w = c.measure("a", 12.0f);
        CHECK(c.caret_at(text, 12.0f, -5.0f) == 0);
        CHECK(c.caret_at(text, 12.0f, w * 0.2f) == 0);
        CHECK(c.caret_at(text, 12.0f, w * 0.8f) == 1);
        CHECK(c.caret_at(text, 12.0f, w * 2.1f) == 2);
        CHECK(c.caret_at(text, 12.0f, 1000.0f) == 4);
    });
}

TEST(caret_at_lands_on_utf8_boundaries) {
    Harness h;
    const std::string text = "\xc3\xa9\xc3\xa9\xc3\xa9";
    h.frame([&](Context& c) {
        for (f32 x = 0.0f; x < 60.0f; x += 0.5f) {
            const std::size_t n = c.caret_at(text, 12.0f, x);
            CHECK_MSG(n % 2 == 0, "a click at %.1f put the caret inside a glyph",
                      x);
        }
    });
}

TEST(text_field_inserts_at_the_caret_not_at_the_end) {
    Harness h;
    std::string v = "hello";
    auto body = [&](Context& c) { text_field(c, {100, 100, 300, 30}, "f", v); };

    h.click_at(120, 110, body);
    h.in.key_home = true;
    h.frame(body);
    h.in.key_home = false;
    h.in.typed = "X";
    h.frame(body);
    h.in.typed.clear();
    CHECK_MSG(v == "Xhello", "got '%s'", v.c_str());

    h.in.key_end = true;
    h.frame(body);
    h.in.key_end = false;
    h.in.typed = "!";
    h.frame(body);
    h.in.typed.clear();
    CHECK_MSG(v == "Xhello!", "got '%s'", v.c_str());
}

TEST(text_field_arrows_move_the_caret_and_backspace_follows_it) {
    Harness h;
    std::string v = "abcd";
    auto body = [&](Context& c) { text_field(c, {100, 100, 300, 30}, "f", v); };
    h.click_at(120, 110, body);
    h.in.key_end = true;
    h.frame(body);
    h.in.key_end = false;

    h.in.key_left = true;
    h.frame(body);
    h.frame(body);
    h.in.key_left = false;

    h.in.key_backspace = true;
    h.frame(body);
    h.in.key_backspace = false;
    CHECK_MSG(v == "acd", "got '%s'", v.c_str());

    h.in.key_delete = true;
    h.frame(body);
    h.in.key_delete = false;
    CHECK_MSG(v == "ad", "got '%s'", v.c_str());
}

TEST(slider_keyboard_and_accessibility_steps_are_focused_and_bounded) {
    Harness h;
    float first = 50, second = 20;
    bool changed = false;
    auto body = [&](Context& c) {
        changed = slider(c, {100, 100, 200, 30}, "first slider", first, 0, 100);
        slider(c, {100, 140, 200, 30}, "second slider", second, 0, 100);
    };
    h.frame(body);
    h.ctx.set_focus(h.ctx.id("first slider"));
    h.in.key_right = true;
    h.frame(body);
    CHECK(changed); CHECK(first == 51); CHECK(second == 20);
    h.in.key_right = false; h.in.key_end = true;
    h.frame(body);
    CHECK(first == 100);
    h.in.key_end = false; h.in.key_right = true;
    h.frame(body);
    CHECK(!changed); CHECK(first == 100);
    h.in.key_right = false; h.in.key_home = true;
    h.frame(body);
    CHECK(first == 0);
    h.in.key_home = false; h.in.key_left = true;
    h.frame(body);
    CHECK(!changed); CHECK(first == 0); CHECK(second == 20);
}

TEST(text_field_editing_a_multibyte_string_never_makes_mojibake) {
    Harness h;
    std::string v = "\xc3\xa9\xc3\xa9\xc3\xa9";
    auto body = [&](Context& c) { text_field(c, {100, 100, 300, 30}, "f", v); };
    h.click_at(120, 110, body);
    h.in.key_end = true;
    h.frame(body);
    h.in.key_end = false;

    h.in.key_left = true;
    h.frame(body);
    h.in.key_left = false;
    h.in.typed = "x";
    h.frame(body);
    h.in.typed.clear();
    CHECK_MSG(v == "\xc3\xa9\xc3\xa9x\xc3\xa9", "got %zu bytes", v.size());

    h.in.key_backspace = true;
    h.frame(body);
    CHECK_MSG(v == "\xc3\xa9\xc3\xa9\xc3\xa9", "got %zu bytes", v.size());
    h.in.key_backspace = true;
    h.frame(body);
    CHECK_MSG(v == "\xc3\xa9\xc3\xa9", "got %zu bytes", v.size());
}

TEST(text_field_clicking_positions_the_caret_where_it_was_clicked) {
    Harness h;
    std::string v = "abcdefgh";
    auto body = [&](Context& c) { text_field(c, {100, 100, 300, 30}, "f", v); };
    h.click_at(120, 110, body);
    h.in.key_end = true;
    h.frame(body);
    h.in.key_end = false;

    h.click_at(104, 110, body);
    h.in.typed = "Z";
    h.frame(body);
    h.in.typed.clear();
    CHECK_MSG(v.rfind("Z", 0) == 0, "typed at the wrong place: '%s'", v.c_str());
}

TEST(label_truncation_is_measured_and_lands_on_a_boundary) {
    Harness h;
    h.frame([&](Context& c) {
        std::string text;
        for (int i = 0; i < 40; ++i) text += "\xc3\xa9";
        label(c, {0, 0, 60, 20}, text);
        bool found = false;
        for (const auto& cmd : c.draw().commands())
            if (!cmd.text.empty()) {
                found = true;
                CHECK_MSG(cmd.text.size() >= 3, "truncated to nothing");
                const std::string body = cmd.text.substr(0, cmd.text.size() - 3);
                CHECK_MSG(body.size() % 2 == 0,
                          "the label was cut inside a glyph");
                CHECK(cmd.text.substr(cmd.text.size() - 3) == "\xe2\x80\xa6");
            }
        CHECK(found);
    });
}

TEST(label_that_fits_is_left_alone) {
    Harness h;
    h.frame([&](Context& c) {
        label(c, {0, 0, 400, 20}, "short");
        for (const auto& cmd : c.draw().commands())
            if (!cmd.text.empty())
                CHECK_MSG(cmd.text == "short", "a label that fits was cut to '%s'",
                          cmd.text.c_str());
    });
}

static std::vector<PaletteCommand> commands() {
    return {
        {"Line", "L", "", true, 1},
        {"Circle", "C", "", true, 2},
        {"Zoom Extents", "ZE", "", true, 3},
        {"Copy", "CO", "", true, 4},
        {"Chamfer", "CHA", "", true, 5},
        {"Move", "M", "", false, 6},
    };
}

TEST(palette_matches_a_subsequence_the_way_every_palette_does) {
    i32 score = 0;
    CHECK(palette_match("Zoom Extents", "zex", score));
    CHECK(palette_match("Zoom Extents", "zoom", score));
    CHECK(palette_match("Zoom Extents", "", score));
    CHECK(!palette_match("Zoom Extents", "zzz", score));
    CHECK(!palette_match("Line", "linex", score));
    CHECK(palette_match("Line", "LI", score));
    CHECK(palette_match("LINE", "li", score));
}

TEST(palette_ranks_a_prefix_above_a_scattered_match) {
    const auto cmds = commands();
    const auto hits = palette_filter(cmds, "ci");
    CHECK_MSG(!hits.empty(), "ci matched nothing");
    CHECK_MSG(cmds[hits[0]].name == "Circle", "ci found %s first",
              cmds[hits[0]].name.c_str());

    i32 prefix = 0, scattered = 0;
    CHECK(palette_match("Circle", "ci", prefix));
    CHECK(palette_match("Chamfer", "ci", scattered) == false ||
          prefix < scattered);
}

TEST(palette_ranks_adjacent_letters_above_scattered_ones) {
    i32 tight = 0, loose = 0;
    CHECK(palette_match("Chamfer", "cha", tight));
    CHECK(palette_match("Chamfer", "cmr", loose));
    CHECK_MSG(tight < loose, "cha scored %d and cmr scored %d", tight, loose);
}

TEST(palette_finds_a_command_by_its_shortcut) {
    const auto cmds = commands();
    const auto hits = palette_filter(cmds, "ze");
    bool found = false;
    for (std::size_t i : hits) if (cmds[i].name == "Zoom Extents") found = true;
    CHECK(found);
}

TEST(palette_ordering_is_stable_so_a_learned_list_does_not_shuffle) {
    const auto cmds = commands();
    const auto a = palette_filter(cmds, "c");
    const auto b = palette_filter(cmds, "c");
    CHECK(a == b);
    CHECK(!a.empty());
}

TEST(palette_empty_query_shows_everything_in_order) {
    const auto cmds = commands();
    const auto hits = palette_filter(cmds, "");
    CHECK(hits.size() == cmds.size());
    for (std::size_t i = 0; i < hits.size(); ++i) CHECK(hits[i] == i);
}

TEST(palette_arrows_move_the_highlight_and_enter_runs_it) {
    Harness h;
    CommandPaletteState st;
    st.open = true;
    const auto cmds = commands();
    i32 ran = -1;
    auto body = [&](Context& c) {
        ran = command_palette(c, {300, 200, 500, 400}, cmds, st);
    };

    h.frame(body);
    CHECK(st.highlighted == 0);
    h.in.key_down = true;
    h.frame(body);
    CHECK_MSG(st.highlighted == 1, "down moved to %d", st.highlighted);
    h.in.key_down = false;
    h.in.key_up = true;
    h.frame(body);
    CHECK(st.highlighted == 0);
    h.in.key_up = false;

    h.in.key_enter = true;
    h.frame(body);
    CHECK_MSG(ran == 0, "Enter ran %d", ran);
    CHECK_MSG(!st.open, "the palette stayed open after running a command");
}

TEST(palette_highlight_wraps_rather_than_stopping_dead) {
    Harness h;
    CommandPaletteState st;
    st.open = true;
    const auto cmds = commands();
    auto body = [&](Context& c) {
        command_palette(c, {300, 200, 500, 400}, cmds, st);
    };
    h.frame(body);
    h.in.key_up = true;
    h.frame(body);
    CHECK_MSG(st.highlighted == static_cast<i32>(cmds.size()) - 1,
              "up from the top landed on %d", st.highlighted);
}

TEST(palette_typing_resets_the_highlight_to_the_best_match) {
    Harness h;
    CommandPaletteState st;
    st.open = true;
    const auto cmds = commands();
    auto body = [&](Context& c) {
        command_palette(c, {300, 200, 500, 400}, cmds, st);
    };
    h.frame(body);
    h.in.key_down = true;
    h.frame(body);
    h.frame(body);
    h.in.key_down = false;
    CHECK(st.highlighted > 0);

    h.in.typed = "c";
    h.frame(body);
    h.in.typed.clear();
    CHECK_MSG(st.highlighted == 0, "typing left the highlight at %d",
              st.highlighted);
}

TEST(palette_escape_closes_and_will_not_run_anything) {
    Harness h;
    CommandPaletteState st;
    st.open = true;
    const auto cmds = commands();
    i32 ran = 0;
    h.in.key_escape = true;
    h.frame([&](Context& c) {
        ran = command_palette(c, {300, 200, 500, 400}, cmds, st);
    });
    CHECK(ran == -1);
    CHECK(!st.open);
}

TEST(palette_will_not_run_a_disabled_command) {
    Harness h;
    CommandPaletteState st;
    st.open = true;
    st.query = "move";
    const auto cmds = commands();
    i32 ran = 0;
    auto body = [&](Context& c) {
        ran = command_palette(c, {300, 200, 500, 400}, cmds, st);
    };
    h.frame(body);
    h.in.key_enter = true;
    h.frame(body);
    CHECK_MSG(ran == -1, "a disabled command ran");
    CHECK(st.open);
}

TEST(palette_says_so_when_nothing_matches) {
    Harness h;
    CommandPaletteState st;
    st.open = true;
    st.query = "qqqq";
    const auto cmds = commands();
    i32 ran = 0;
    h.frame([&](Context& c) {
        ran = command_palette(c, {300, 200, 500, 400}, cmds, st);
        bool said = false;
        for (const auto& cmd : c.draw().commands())
            if (cmd.text.find("No matching") != std::string::npos) said = true;
        CHECK_MSG(said, "an empty palette said nothing");
    });
    CHECK(ran == -1);
}

TEST(palette_closed_draws_nothing) {
    Harness h;
    CommandPaletteState st;
    const auto cmds = commands();
    h.frame([&](Context& c) {
        const std::size_t before = c.draw().commands().size();
        CHECK(command_palette(c, {300, 200, 500, 400}, cmds, st) == -1);
        CHECK(c.draw().commands().size() == before);
    });
}

static std::vector<PropertyRow> props() {
    std::vector<PropertyRow> v;
    PropertyRow a;
    a.group = "Geometry";
    a.name = "Length";
    a.value = "10";
    a.kind = PropertyKind::Number;
    a.suffix = "m";
    v.push_back(a);

    PropertyRow b;
    b.group = "Geometry";
    b.name = "Layer";
    b.value = "0";
    b.kind = PropertyKind::Choice;
    b.choices = {"0", "WALLS", "DIMS"};
    v.push_back(b);

    PropertyRow c;
    c.group = "Appearance";
    c.name = "Colour";
    c.value = "#ff0000";
    c.kind = PropertyKind::Color;
    v.push_back(c);

    PropertyRow d;
    d.group = "Appearance";
    d.name = "Plottable";
    d.value = "1";
    d.kind = PropertyKind::Bool;
    v.push_back(d);

    PropertyRow e;
    e.group = "Geometry";
    e.name = "Area";
    e.value = "40";
    e.kind = PropertyKind::ReadOnly;
    v.push_back(e);
    return v;
}

TEST(properties_panel_groups_rows_in_the_order_given) {
    Harness h;
    PropertyPanelState st;
    const auto rows = props();
    h.frame([&](Context& c) {
        properties_panel(c, {0, 0, 320, 600}, rows, st);
        bool geometry = false, appearance = false;
        for (const auto& cmd : c.draw().commands()) {
            if (cmd.text == "Geometry") geometry = true;
            if (cmd.text == "Appearance") appearance = true;
        }
        CHECK(geometry && appearance);
    });
}

TEST(properties_panel_collapses_a_group) {
    Harness h;
    PropertyPanelState st;
    const auto rows = props();
    CHECK(!st.is_collapsed("Geometry"));
    st.toggle_group("Geometry");
    CHECK(st.is_collapsed("Geometry"));

    h.frame([&](Context& c) {
        properties_panel(c, {0, 0, 320, 600}, rows, st);
        for (const auto& cmd : c.draw().commands())
            CHECK_MSG(cmd.text != "Length",
                      "a collapsed group still drew its rows");
    });
    st.toggle_group("Geometry");
    CHECK(!st.is_collapsed("Geometry"));
}

TEST(properties_panel_shows_varies_rather_than_one_objects_value) {
    Harness h;
    PropertyPanelState st;
    auto rows = props();
    rows[0].varies = true;
    rows[0].value = "10";

    h.frame([&](Context& c) {
        properties_panel(c, {0, 0, 320, 600}, rows, st);
        bool varies = false, leaked = false;
        for (const auto& cmd : c.draw().commands()) {
            if (cmd.text == "*VARIES*") varies = true;
            if (cmd.text == "10") leaked = true;
        }
        CHECK_MSG(varies, "a mixed field did not say so");
        CHECK_MSG(!leaked, "a mixed field showed one object's value");
    });
}

TEST(properties_panel_reports_a_bool_edit) {
    Harness h;
    PropertyPanelState st;
    const auto rows = props();
    PropertyEdit edit;
    auto body = [&](Context& c) {
        const PropertyEdit e = properties_panel(c, {0, 0, 320, 600}, rows, st);
        if (e.edited) edit = e;
    };
    h.frame(body);
    Rect box{};
    h.frame([&](Context& c) {
        properties_panel(c, {0, 0, 320, 600}, rows, st);
        for (const auto& a : c.accessibility_tree())
            if (a.role == Context::Role::Checkbox) box = a.bounds;
    });
    CHECK_MSG(box.w > 0.0f, "no checkbox was announced");
    h.click_at(box.x + box.w * 0.5f, box.y + box.h * 0.5f, body);
    CHECK_MSG(edit.edited, "clicking a bool property reported no edit");
    CHECK_MSG(edit.value == "0", "the bool became '%s'", edit.value.c_str());
}

TEST(properties_panel_does_not_apply_a_number_until_it_is_committed) {
    Harness h;
    PropertyPanelState st;
    const auto rows = props();
    PropertyEdit edit;
    auto body = [&](Context& c) {
        const PropertyEdit e = properties_panel(c, {0, 0, 320, 600}, rows, st);
        if (e.edited) edit = e;
    };
    h.frame(body);
    Rect field{};
    h.frame([&](Context& c) {
        properties_panel(c, {0, 0, 320, 600}, rows, st);
        for (const auto& a : c.accessibility_tree())
            if (a.role == Context::Role::Field && a.label == "n") field = a.bounds;
    });
    if (field.w > 0.0f) {
        h.click_at(field.x + 10.0f, field.y + field.h * 0.5f, body);
        h.in.typed = "5";
        h.frame(body);
        h.in.typed.clear();
        CHECK_MSG(!edit.edited, "typing committed a number edit on its own");
        h.in.key_enter = true;
        h.frame(body);
        h.in.key_enter = false;
        CHECK_MSG(edit.edited, "Enter did not commit the number");
    }
}

TEST(properties_panel_with_no_rows_draws_nothing_and_does_not_crash) {
    Harness h;
    PropertyPanelState st;
    h.frame([&](Context& c) {
        const PropertyEdit e = properties_panel(c, {0, 0, 320, 600}, {}, st);
        CHECK(!e.edited);
    });
}

TEST(theme_editor_panel_lists_every_group_and_commits) {
    Harness h;
    gtheme::ThemeSet set;
    gtheme::ThemeEditor editor(set);
    ThemeEditorState st;

    h.frame([&](Context& c) {
        theme_editor_panel(c, {0, 0, 380, 800}, editor, st);
        bool any_group = false;
        for (const auto& g : editor.groups())
            for (const auto& cmd : c.draw().commands())
                if (cmd.text == g.name) any_group = true;
        CHECK_MSG(any_group, "the editor drew no group headers");
    });

    const auto groups = editor.groups();
    CHECK(!groups.empty() && !groups[0].tokens.empty());
    editor.set(groups[0].tokens[0], gtheme::Color::rgb(0.1f, 0.2f, 0.3f));
    CHECK(editor.dirty());
    editor.commit();
    CHECK(!editor.dirty());
}

TEST(theme_editor_panel_collapses_a_group) {
    Harness h;
    gtheme::ThemeSet set;
    gtheme::ThemeEditor editor(set);
    ThemeEditorState st;
    const auto groups = editor.groups();
    CHECK(!groups.empty());
    st.toggle_group(groups[0].name);
    CHECK(st.is_collapsed(groups[0].name));

    h.frame([&](Context& c) {
        theme_editor_panel(c, {0, 0, 380, 800}, editor, st);
        for (const auto& cmd : c.draw().commands())
            CHECK_MSG(cmd.text != groups[0].tokens[0],
                      "a collapsed group still drew its tokens");
    });
}

TEST(theme_editor_panel_applies_a_complete_hex_and_ignores_a_partial_one) {
    Harness h;
    gtheme::ThemeSet set;
    gtheme::ThemeEditor editor(set);
    ThemeEditorState st;
    const auto groups = editor.groups();
    const std::string token = groups[0].tokens[0];
    const gtheme::Color before = editor.value(token);

    st.selected = token;
    st.hex = "#1";
    auto body = [&](Context& c) { theme_editor_panel(c, {0, 0, 380, 800}, editor, st); };
    h.frame(body);
    const gtheme::Color after_partial = editor.value(token);
    CHECK_MSG(std::fabs(after_partial.r - before.r) < 1e-6f &&
                  std::fabs(after_partial.g - before.g) < 1e-6f,
              "a partial hex was applied");

    Rect field{};
    h.frame([&](Context& c) {
        theme_editor_panel(c, {0, 0, 380, 800}, editor, st);
        for (const auto& a : c.accessibility_tree())
            if (a.role == Context::Role::Field && a.label == "hex") field = a.bounds;
    });
    CHECK_MSG(field.w > 0.0f, "the hex field was never drawn");
}

TEST(theme_editor_revert_restores_the_last_commit) {
    gtheme::ThemeSet set;
    gtheme::ThemeEditor editor(set);
    const auto groups = editor.groups();
    const std::string token = groups[0].tokens[0];
    const gtheme::Color start = editor.value(token);

    editor.set(token, gtheme::Color::rgb(0.9f, 0.1f, 0.1f));
    CHECK(editor.dirty());
    editor.revert();
    const gtheme::Color back = editor.value(token);
    CHECK_MSG(std::fabs(back.r - start.r) < 1e-6f, "revert did not restore");
    CHECK(!editor.dirty());
}

TEST(live_rules_panel_says_what_is_holding_not_merely_what_was_noticed) {
    Harness h;
    LiveRulesState st;
    std::vector<LiveRuleRow> rules = {
        {"Coplanar", "faces 3 and 7, 0.0001 apart", true, true, 0.0001f},
        {"Symmetric about YZ", "the two bosses", true, false, 0.002f},
        {"Concentric", "holes 1 and 2", false, false, 0.0f},
    };

    h.frame([&](Context& c) {
        const LiveRulesResult r = live_rules_panel(c, {0, 0, 320, 400}, rules, st);
        CHECK(r.toggled == -1);
        std::size_t holding = 0;
        for (const auto& a : c.accessibility_tree())
            if (a.value == "holding") ++holding;
        CHECK_MSG(holding == 1, "%zu rules reported as holding", holding);
    });
}

TEST(live_rules_panel_toggling_a_rule_reports_it) {
    Harness h;
    LiveRulesState st;
    std::vector<LiveRuleRow> rules = {
        {"Coplanar", "", true, true, 0.0f},
    };
    auto body = [&](Context& c) { live_rules_panel(c, {0, 0, 320, 400}, rules, st); };

    h.frame(body);
    Rect box{};
    h.frame([&](Context& c) {
        live_rules_panel(c, {0, 0, 320, 400}, rules, st);
        for (const auto& a : c.accessibility_tree())
            if (a.role == Context::Role::Checkbox) box = a.bounds;
    });
    CHECK(box.w > 0.0f);
    h.click_at(box.x + box.w * 0.5f, box.y + box.h * 0.5f, body);
    CHECK_MSG(!rules[0].enabled, "the rule did not switch off");
}

TEST(live_rules_panel_with_no_rules_says_so) {
    Harness h;
    LiveRulesState st;
    std::vector<LiveRuleRow> none;
    h.frame([&](Context& c) {
        live_rules_panel(c, {0, 0, 320, 400}, none, st);
        bool said = false;
        for (const auto& cmd : c.draw().commands())
            if (cmd.text.find("No rules") != std::string::npos) said = true;
        CHECK(said);
    });
}

namespace {

std::size_t count_of(const std::vector<AccessEvent>& v, AccessEventKind k) {
    std::size_t n = 0;
    for (const auto& e : v) if (e.kind == k) ++n;
    return n;
}

}

TEST(access_snapshot_nests_by_the_clip_stack) {
    Harness h;
    AccessSnapshot snap;
    h.frame([&](Context& c) {
        c.announce(c.id("panel"), Context::Role::Panel, "Layers", "",
                   {0, 0, 200, 400});
        c.push_clip({0, 0, 200, 400});
        c.announce(c.id("row1"), Context::Role::Row, "0", "", {0, 0, 200, 20});
        c.announce(c.id("row2"), Context::Role::Row, "WALLS", "", {0, 20, 200, 20});
        c.pop_clip();
        c.announce(c.id("status"), Context::Role::Label, "Ready", "",
                   {0, 400, 200, 20});
        snap = snapshot(c);
    });

    CHECK_MSG(snap.size() == 4, "%zu nodes", snap.size());
    const AccessNode* panel = nullptr;
    for (const AccessNode& n : snap.nodes)
        if (n.label == "Layers") panel = &n;
    CHECK(panel != nullptr);
    CHECK_MSG(panel->parent == 0, "the panel got a parent");

    const auto kids = snap.children_of(panel->id);
    CHECK_MSG(kids.size() == 2, "the panel has %zu children", kids.size());

    for (const AccessNode& n : snap.nodes)
        if (n.label == "Ready")
            CHECK_MSG(n.parent == 0, "a sibling was made a child");
}

TEST(access_a_ui_at_rest_produces_a_silent_bridge) {
    Harness h;
    AccessBridge bridge;

    auto build = [&](Context& c) {
        c.announce(c.id("b"), Context::Role::Button, "Apply", "",
                   {10, 10, 80, 24});
    };

    AccessSnapshot s1, s2, s3;
    h.frame([&](Context& c) { build(c); s1 = snapshot(c); });
    h.frame([&](Context& c) { build(c); s2 = snapshot(c); });
    h.frame([&](Context& c) { build(c); s3 = snapshot(c); });

    const auto first = bridge.update(s1);
    CHECK_MSG(count_of(first, AccessEventKind::Added) == 1,
              "the first frame did not describe the tree");

    CHECK_MSG(bridge.update(s2).empty(), "a still UI produced events");
    CHECK_MSG(bridge.update(s3).empty(), "a still UI produced events");
}

TEST(access_a_node_that_moves_keeps_its_identity) {
    Harness h;
    AccessBridge bridge;
    AccessSnapshot a, b;
    h.frame([&](Context& c) {
        c.announce(c.id("row"), Context::Role::Row, "WALLS", "", {0, 0, 100, 20});
        a = snapshot(c);
    });
    h.frame([&](Context& c) {
        c.announce(c.id("row"), Context::Role::Row, "WALLS", "", {0, 40, 100, 20});
        b = snapshot(c);
    });

    bridge.update(a);
    const auto events = bridge.update(b);
    CHECK_MSG(count_of(events, AccessEventKind::Removed) == 0,
              "a moved node was reported as removed");
    CHECK_MSG(count_of(events, AccessEventKind::Added) == 0,
              "a moved node was reported as added");
    CHECK_MSG(count_of(events, AccessEventKind::BoundsChanged) == 1,
              "the move was not reported at all");
}

TEST(access_a_subpixel_wobble_is_not_a_move) {
    Harness h;
    AccessBridge bridge;
    AccessSnapshot a, b;
    h.frame([&](Context& c) {
        c.announce(c.id("x"), Context::Role::Button, "Go", "", {10.0f, 10.0f, 40, 20});
        a = snapshot(c);
    });
    h.frame([&](Context& c) {
        c.announce(c.id("x"), Context::Role::Button, "Go", "", {10.2f, 10.1f, 40, 20});
        b = snapshot(c);
    });
    bridge.update(a);
    CHECK(bridge.update(b).empty());
}

TEST(access_a_changed_value_is_reported_once) {
    Harness h;
    AccessBridge bridge;
    auto with = [&](const char* value) {
        AccessSnapshot s;
        h.frame([&](Context& c) {
            c.announce(c.id("f"), Context::Role::Field, "Radius", value,
                       {0, 0, 100, 24});
            s = snapshot(c);
        });
        return s;
    };

    bridge.update(with("10"));
    const auto changed = bridge.update(with("25"));
    CHECK_MSG(count_of(changed, AccessEventKind::ValueChanged) == 1,
              "%zu value events", count_of(changed, AccessEventKind::ValueChanged));
    for (const auto& e : changed)
        if (e.kind == AccessEventKind::ValueChanged) CHECK(e.text == "25");

    CHECK(bridge.update(with("25")).empty());
}

TEST(access_focus_is_reported_once_not_every_frame) {
    Harness h;
    AccessBridge bridge;
    std::string v;
    auto body = [&](Context& c) { text_field(c, {10, 10, 200, 24}, "field", v); };

    h.frame(body);
    AccessSnapshot s;
    h.frame([&](Context& c) { body(c); s = snapshot(c); });
    bridge.update(s);

    h.click_at(50, 20, body);
    AccessSnapshot focused;
    h.frame([&](Context& c) { body(c); focused = snapshot(c); });
    const auto got = bridge.update(focused);
    CHECK_MSG(count_of(got, AccessEventKind::FocusChanged) == 1,
              "%zu focus events on the frame focus moved",
              count_of(got, AccessEventKind::FocusChanged));

    AccessSnapshot still;
    h.frame([&](Context& c) { body(c); still = snapshot(c); });
    CHECK_MSG(count_of(bridge.update(still), AccessEventKind::FocusChanged) == 0,
              "focus was announced again while nothing moved");
}

TEST(access_removed_nodes_are_reported) {
    Harness h;
    AccessBridge bridge;
    AccessSnapshot both, one;
    h.frame([&](Context& c) {
        c.announce(c.id("a"), Context::Role::Button, "A", "", {0, 0, 10, 10});
        c.announce(c.id("b"), Context::Role::Button, "B", "", {0, 20, 10, 10});
        both = snapshot(c);
    });
    h.frame([&](Context& c) {
        c.announce(c.id("a"), Context::Role::Button, "A", "", {0, 0, 10, 10});
        one = snapshot(c);
    });
    bridge.update(both);
    const auto events = bridge.update(one);
    CHECK_MSG(count_of(events, AccessEventKind::Removed) == 1,
              "%zu removals", count_of(events, AccessEventKind::Removed));
}

TEST(access_state_changes_are_reported_separately_from_values) {
    Harness h;
    AccessBridge bridge;
    auto with = [&](bool selected, bool disabled) {
        AccessSnapshot s;
        h.frame([&](Context& c) {
            c.announce(c.id("r"), Context::Role::Row, "WALLS", "", {0, 0, 10, 10},
                       selected, disabled);
            s = snapshot(c);
        });
        return s;
    };
    bridge.update(with(false, false));
    CHECK(count_of(bridge.update(with(true, false)),
                   AccessEventKind::StateChanged) == 1);
    CHECK(count_of(bridge.update(with(true, true)),
                   AccessEventKind::StateChanged) == 1);
    CHECK(bridge.update(with(true, true)).empty());
}

TEST(access_a_live_region_is_queued_and_drained_not_lost) {
    Harness h;
    AccessBridge bridge;
    AccessSnapshot s;
    h.frame([&](Context& c) {
        c.announce(c.id("x"), Context::Role::Label, "x", "", {0, 0, 1, 1});
        s = snapshot(c);
    });
    bridge.update(s);

    bridge.announce("3 objects selected");
    bridge.announce("constraint conflict");
    bridge.announce("");

    const auto events = bridge.update(s);
    CHECK_MSG(count_of(events, AccessEventKind::Announcement) == 2,
              "%zu announcements", count_of(events, AccessEventKind::Announcement));
    CHECK(events.back().text == "constraint conflict");
    CHECK(bridge.update(s).empty());
}

TEST(access_the_first_frame_describes_the_whole_tree) {
    Harness h;
    AccessBridge bridge;
    AccessSnapshot s;
    h.frame([&](Context& c) {
        for (int i = 0; i < 5; ++i)
            c.announce(c.id("n" + std::to_string(i)), Context::Role::Row,
                       "row" + std::to_string(i), "", {0, f32(i) * 20, 100, 20});
        s = snapshot(c);
    });
    const auto first = bridge.update(s);
    CHECK(count_of(first, AccessEventKind::Added) == 5);
    for (const auto& e : first)
        if (e.kind == AccessEventKind::Added)
            CHECK_MSG(!e.text.empty(), "an added node was described as nothing");
}

TEST(access_a_node_describes_itself_in_reading_order) {
    AccessNode n;
    n.role = Context::Role::Checkbox;
    n.label = "Plottable";
    n.value = "on";
    n.selected = true;
    n.disabled = true;
    CHECK_MSG(n.describe() == "Plottable, checkbox, on, selected, unavailable",
              "got '%s'", n.describe().c_str());

    AccessNode bare;
    bare.role = Context::Role::None;
    CHECK(bare.describe().empty());
}

TEST(access_bridge_reports_what_the_reader_should_be_saying) {
    Harness h;
    AccessBridge bridge;
    std::string v = "42";
    auto body = [&](Context& c) { text_field(c, {10, 10, 200, 24}, "Radius", v); };
    h.click_at(50, 20, body);
    AccessSnapshot s;
    h.frame([&](Context& c) { body(c); s = snapshot(c); });
    bridge.update(s);
    CHECK_MSG(bridge.focus_description().find("Radius") != std::string::npos,
              "got '%s'", bridge.focus_description().c_str());
    CHECK(bridge.focus() != 0);
}

TEST(access_bridge_reset_starts_over) {
    Harness h;
    AccessBridge bridge;
    AccessSnapshot s;
    h.frame([&](Context& c) {
        c.announce(c.id("x"), Context::Role::Button, "Go", "", {0, 0, 10, 10});
        s = snapshot(c);
    });
    CHECK(bridge.update(s).size() >= 1);
    CHECK(bridge.update(s).empty());

    bridge.reset();
    CHECK(bridge.current().size() == 0);
    CHECK_MSG(count_of(bridge.update(s), AccessEventKind::Added) == 1,
              "after a reset the tree was not re-described");
}

TEST(access_event_names_exist) {
    for (AccessEventKind k :
         {AccessEventKind::Added, AccessEventKind::Removed,
          AccessEventKind::LabelChanged, AccessEventKind::ValueChanged,
          AccessEventKind::StateChanged, AccessEventKind::BoundsChanged,
          AccessEventKind::FocusChanged, AccessEventKind::Announcement})
        CHECK(access_event_name(k)[0] != '\0');
}

TEST(access_a_real_panel_produces_a_walkable_tree) {
    Harness h;
    PropertyPanelState st;
    const auto rows = props();
    AccessBridge bridge;
    AccessSnapshot s;
    h.frame([&](Context& c) {
        properties_panel(c, {0, 0, 320, 600}, rows, st);
        s = snapshot(c);
    });

    CHECK_MSG(!s.nodes.empty(), "the panel announced nothing at all");
    std::size_t groups = 0;
    for (const AccessNode& n : s.nodes)
        if (n.role == Context::Role::TreeItem &&
            (n.label == "Geometry" || n.label == "Appearance"))
            ++groups;
    CHECK_MSG(groups == 2, "%zu group headers announced", groups);
    const auto events = bridge.update(s);
    CHECK(count_of(events, AccessEventKind::Added) == s.size());

    for (const AccessNode& n : s.nodes)
        CHECK_MSG(!n.describe().empty(), "a node in the panel is silent");
}

TEST(gui_a_dropdown_list_is_clickable_outside_its_panel) {
    Harness h;
    i32 sel = 0;
    const std::vector<std::string> items = {"1:1", "1:50", "1:200"};
    auto body = [&](Context& c) {
        c.push_clip({0, 0, 400, 60});
        dropdown(c, {10, 10, 120, 30}, "scale", items, sel);
        c.pop_clip();
    };
    h.click_at(60, 25, body);
    const f32 row_h = h.ctx.style().row_height_dense;
    h.click_at(60, 42 + row_h * 2.5f, body);
    CHECK_MSG(sel == 2, "the row outside the panel clip did not take the click (sel=%d)", sel);
}

TEST(gui_a_popup_click_does_not_fall_through_inside_a_modal) {
    Harness h;
    const std::vector<std::string> items = {"System default", "Speakers", "Headset"};
    i32 sel1 = -1, sel2 = -1;
    const Rect modal{100, 100, 400, 400};
    const auto body = [&](Context& c) {
        c.begin_overlay(modal);
        dropdown(c, {120, 120, 200, 30}, "out", items, sel1);
        dropdown(c, {120, 160, 200, 30}, "in", items, sel2);
        c.end_overlay();
    };
    h.click_at(150, 135, body);
    h.click_at(150, 165, body);
    h.frame(body);
    CHECK_MSG(sel1 == 0, "the row under the pointer did not take the click (sel1=%d)", sel1);
    CHECK_MSG(sel2 == -1, "the click fell through the list into the field beneath");
    bool second_open = true;
    h.frame([&](Context& c) {
        c.push_id("in");
        second_open = c.state_bool(c.id("in"), false);
        c.pop_id();
    });
    CHECK_MSG(!second_open, "the covered dropdown opened from a click meant for the list");
}

TEST(gui_base_widgets_are_dead_under_a_modal) {
    Harness h;
    bool base_clicked = false;
    const auto body = [&](Context& c) {
        if (button(c, {10, 10, 100, 30}, "under")) base_clicked = true;
        c.begin_overlay({0, 0, 1440, 900});
        c.draw().rect({0, 0, 1440, 900}, {0, 0, 0, 0.4f});
        c.end_overlay();
    };
    h.click_at(50, 25, body);
    CHECK_MSG(!base_clicked, "a button under the modal scrim took the click");
}

TEST(gui_text_field_keeps_spaces_and_the_caret_tracks) {
    Harness h;
    h.ctx.set_measure([](std::string_view s, f32 size) {
        return static_cast<f32>(s.size()) * size * 0.6f;
    });
    std::string value;
    const Rect r{10, 10, 300, 30};
    h.click_at(100, 25, [&](Context& c) { text_field(c, r, "name", value); });
    for (const char* piece : {"macbook", " ", "pro", " ", "m5", " ", "max"}) {
        h.in.typed = piece;
        h.frame([&](Context& c) { text_field(c, r, "name", value); });
    }
    CHECK_MSG(value == "macbook pro m5 max", "field holds '%s'", value.c_str());

    h.in.key_left = true;
    h.frame([&](Context& c) { text_field(c, r, "name", value); });
    h.in.key_left = false;
    h.in.typed = "!";
    h.frame([&](Context& c) { text_field(c, r, "name", value); });
    h.in.typed.clear();
    CHECK_MSG(value == "macbook pro m5 ma!x", "caret insert put '%s'", value.c_str());
}

TEST(gui_text_area_scrolls_instead_of_overflowing) {
    Harness h;
    std::string value;
    const Rect r{10, 10, 300, 107};
    const auto body = [&](Context& c) { text_area(c, r, "msg", value, "say"); };
    h.click_at(20, 20, body);
    h.in.typed = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight";
    f32  max_bottom = 0.0f;
    bool tail_visible = false, head_visible = false;
    h.frame([&](Context& c) {
        body(c);
        for (const auto& d : c.draw().commands()) {
            if (d.prim != Prim::Text) continue;
            max_bottom = std::max(max_bottom, d.rect.y + d.rect.h);
            if (d.text == "eight") tail_visible = true;
            if (d.text == "one") head_visible = true;
        }
    });
    h.in.typed.clear();
    CHECK_MSG(max_bottom <= r.bottom() + 0.5f,
              "a text row was drawn %.1fpx past the box", max_bottom - r.bottom());
    CHECK_MSG(tail_visible, "the caret's line (the tail) is not in the window");
    CHECK_MSG(!head_visible, "eight lines cannot all fit; the window did not scroll");
}

TEST(gui_text_area_selection_word_and_line_delete) {
    Harness h;
    std::string value = "hello world";
    const Rect r{10, 10, 300, 60};
    const auto body = [&](Context& c) { text_area(c, r, "msg", value, ""); };

    f32 inner_x = 0, fs = 0, line_h = 0;
    h.frame([&](Context& c) {
        inner_x = r.deflate(Insets::xy(c.style().padding.l, 6.0f)).x;
        fs = c.style().font_size;
        line_h = fs + 6.0f;
        body(c);
    });
    const f32 ymid = 16.0f + line_h * 0.5f;
    f32 x0 = 0, x1 = 0;
    h.frame([&](Context& c) {
        x0 = inner_x + c.measure("hello ", fs) + 1.0f;
        x1 = inner_x + c.measure("hello world", fs) + 8.0f;
        body(c);
    });

    h.move_to(x0, ymid);
    h.press();
    h.frame(body);
    h.move_to(x1, ymid);
    h.frame(body);
    h.release();
    h.frame(body);

    std::string copied;
    h.ctx.clipboard = [&](const std::string& s) { copied = s; };
    h.frame([&](Context& c) {
        c.copy_requested = true;
        body(c);
    });
    CHECK_MSG(copied == "world", "copied '%s'", copied.c_str());

    h.in.typed = "there";
    h.frame(body);
    h.in.typed.clear();
    CHECK_MSG(value == "hello there", "type-over made '%s'", value.c_str());

    h.in.alt = true;
    h.in.key_backspace = true;
    h.frame(body);
    h.in.alt = false;
    CHECK_MSG(value == "hello ", "word delete made '%s'", value.c_str());

    h.in.typed = "x\nsecond line";
    h.frame(body);
    h.in.typed.clear();
    h.in.cmd = true;
    h.in.key_backspace = true;
    h.frame(body);
    CHECK_MSG(value == "hello x\n", "line delete made '%s'", value.c_str());
    h.in.key_backspace = true;
    h.frame(body);
    h.in.cmd = false;
    CHECK_MSG(value == "hello x", "line delete at line start made '%s'", value.c_str());
}

TEST(gui_native_replacement_is_atomic_and_reports_current_value) {
    Harness h; std::string a="first", b="untouched";
    const auto draw=[&](Context& c) {
        text_field(c,{0,0,240,40},"a",a);
        text_field(c,{0,50,240,40},"b",b);
    };
    h.frame(draw);
    const auto id=h.ctx.id("a"); h.ctx.set_focus(id);
    h.in.replacement_id=id; h.in.replacement_text="日本語 café";
    h.frame(draw);
    CHECK(a=="日本語 café"); CHECK(b=="untouched");
    CHECK(h.ctx.accessibility_tree()[0].value==a);
    h.in.replacement_id=0; h.in.replacement_text.clear();
    h.in.typed="!"; h.frame(draw); CHECK(a=="日本語 café!");
    h.in.replacement_id=id; h.in.replacement_text=""; h.frame(draw); CHECK(a.empty());
    h.ctx.set_focus(h.ctx.id("b"));
    h.in.replacement_text="stale"; h.frame(draw); CHECK(a.empty()); CHECK(b=="untouched");
}


TEST(access_viewport_and_scroll_clipping_match_visible_activation_bounds) {
    Harness h;
    h.frame([&](Context& c) {
        button(c,{-20,-10,60,40},"Viewport partial");
        button(c,{300,20,80,30},"Offscreen");
        c.push_clip({40,40,100,80});
        c.push_clip({0,60,200,100});
        button(c,{20,50,100,40},"Panel partial");
        button(c,{50,130,60,30},"Scrolled away");
        button(c,{60,90,60,20},"Disabled visible",false);
        c.pop_clip();c.pop_clip();
    },{0,0,300,200});
    const auto tree=snapshot(h.ctx);
    CHECK(tree.size()==3);
    const auto* viewport=tree.find(h.ctx.id("Viewport partial"));
    CHECK(viewport!=nullptr);
    if(viewport) {CHECK(viewport->bounds.x==0);CHECK(viewport->bounds.y==0);CHECK(viewport->bounds.w==40);CHECK(viewport->bounds.h==30);}
    const auto* partial=tree.find(h.ctx.id("Panel partial"));
    CHECK(partial!=nullptr);
    if(partial) {CHECK(partial->bounds.x==40);CHECK(partial->bounds.y==60);CHECK(partial->bounds.w==80);CHECK(partial->bounds.h==30);}
    CHECK(!tree.find(h.ctx.id("Offscreen")));CHECK(!tree.find(h.ctx.id("Scrolled away")));
    const auto* disabled=tree.find(h.ctx.id("Disabled visible"));
    CHECK(disabled&&disabled->disabled);
    CHECK(h.ctx.focusable_count()==2);
    h.ctx.clear_focus();CHECK(h.ctx.focus_next());CHECK(h.ctx.focused()==h.ctx.id("Viewport partial"));
    CHECK(h.ctx.focus_next());CHECK(h.ctx.focused()==h.ctx.id("Panel partial"));
    CHECK(h.ctx.focus_next());CHECK(h.ctx.focused()==h.ctx.id("Viewport partial"));
}

TEST(access_scrolled_focus_is_removed_once_and_cannot_activate_invisibly) {
    Harness h;AccessBridge bridge;float y=60;bool clicked=false;
    const auto body=[&](Context& c) {
        c.push_clip({10,10,100,80});
        clicked=button(c,{20,y,80,40},"Scrolled command");
        c.pop_clip();
    };
    h.frame(body);const Id id=h.ctx.id("Scrolled command");h.ctx.set_focus(id);
    h.frame(body);const auto initial=snapshot(h.ctx);bridge.update(initial);
    CHECK(initial.focus==id);CHECK(initial.find(id)&&initial.find(id)->bounds.h==30);
    y=100;h.in.key_enter=true;h.frame(body);
    CHECK(!clicked);CHECK(h.ctx.focused()==0);CHECK(h.ctx.focusable_count()==0);
    const auto hidden=snapshot(h.ctx);CHECK(hidden.size()==0);CHECK(hidden.focus==0);
    const auto removed=bridge.update(hidden);
    CHECK(count_of(removed,AccessEventKind::Removed)==1);
    CHECK(count_of(removed,AccessEventKind::FocusChanged)==1);
    h.frame(body);CHECK(bridge.update(snapshot(h.ctx)).empty());
    y=30;h.frame(body);const auto shown=snapshot(h.ctx);
    CHECK(shown.find(id)!=nullptr);CHECK(shown.focus==0);
    CHECK(count_of(bridge.update(shown),AccessEventKind::Added)==1);
}

TEST(access_overlay_escapes_parent_clip_but_honors_its_scroll_and_viewport) {
    Harness h;
    h.frame([&](Context& c) {
        c.push_clip({0,0,30,30});
        c.begin_overlay({50,50,220,150});
        button(c,{60,60,80,30},"Popup outside parent");
        c.push_clip({50,100,300,70});
        button(c,{240,150,100,40},"Popup partial");
        button(c,{60,180,80,30},"Popup scrolled away");
        c.pop_clip();c.end_overlay();
        button(c,{60,60,80,30},"Parent hidden");
        c.pop_clip();
    },{0,0,300,200});
    const auto tree=snapshot(h.ctx);CHECK(tree.size()==2);
    const auto* popup=tree.find(h.ctx.id("Popup outside parent"));CHECK(popup!=nullptr);
    const auto* partial=tree.find(h.ctx.id("Popup partial"));CHECK(partial!=nullptr);
    if(partial) {CHECK(partial->bounds.x==240);CHECK(partial->bounds.y==150);CHECK(partial->bounds.w==60);CHECK(partial->bounds.h==20);}
    CHECK(!tree.find(h.ctx.id("Popup scrolled away")));CHECK(!tree.find(h.ctx.id("Parent hidden")));
}
