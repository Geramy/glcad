#include <algorithm>
#include <cmath>

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

namespace gui {

namespace T = gtheme::token;

namespace {

Color tok(Context& c, const char* t) { return c.theme().get(t); }

bool group_header(Context& c, const Rect& r, std::string_view name,
                  bool collapsed) {
    const Style& s = c.style();
    const Id id = c.id(name);
    const auto in = c.interact(id, r);
    c.register_focusable(id, r, name);

    if (in.hovered) c.draw().round_rect(r, s.radius_small, tok(c, T::ui_hover));
    Rect row = r;
    const Rect chev = row.cut_left(s.row_height);
    draw_icon(c, collapsed ? Icon::ChevronRight : Icon::ChevronDown, chev,
              tok(c, T::ui_text_dim), 1.5f);
    c.draw().text(row, std::string(name), s.font_size, tok(c, T::ui_text));

    c.announce(id, Context::Role::TreeItem, name, collapsed ? "collapsed" : "expanded",
               r, false, false);
    return in.clicked || c.activated(id);
}

bool contains(const std::vector<std::string>& v, std::string_view s) {
    for (const std::string& x : v)
        if (x == s) return true;
    return false;
}

void toggle(std::vector<std::string>& v, std::string_view s) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i] == s) { v.erase(v.begin() + static_cast<long>(i)); return; }
    v.emplace_back(s);
}

}

bool ThemeEditorState::is_collapsed(std::string_view g) const {
    return contains(collapsed, g);
}
void ThemeEditorState::toggle_group(std::string_view g) { toggle(collapsed, g); }

bool PropertyPanelState::is_collapsed(std::string_view g) const {
    return contains(collapsed, g);
}
void PropertyPanelState::toggle_group(std::string_view g) { toggle(collapsed, g); }

ThemeEditorResult theme_editor_panel(Context& c, const Rect& r,
                                     gtheme::ThemeEditor& editor,
                                     ThemeEditorState& st) {
    ThemeEditorResult out;
    const Style& s = c.style();
    c.push_id("theme-editor");

    Rect area = r;

    Rect actions = area.cut_bottom(s.row_height + s.spacing * 2.0f);
    actions = actions.deflate(Insets::all(s.spacing));

    const Rect view = begin_scroll(c, area, st.scroll);
    Rect cursor = view;
    f32 used = 0.0f;

    for (const auto& g : editor.groups()) {
        const Rect head = cursor.cut_top(s.row_height);
        used += s.row_height;
        if (group_header(c, head, g.name, st.is_collapsed(g.name)))
            st.toggle_group(g.name);
        if (st.is_collapsed(g.name)) continue;

        for (const std::string& token : g.tokens) {
            Rect row = cursor.cut_top(s.row_height);
            used += s.row_height;

            const gtheme::Color value = editor.value(token);
            c.push_id(token);

            const Rect sw = row.cut_left(s.row_height);
            const Id sid = c.id("swatch");
            const auto si = c.interact(sid, sw);
            color_swatch(c, sw.deflate(Insets::all(3.0f)),
                         Color{value.r, value.g, value.b, value.a},
                         st.selected == token);
            if (si.clicked) {
                st.selected = token;
                st.hex = value.to_hex();
            }

            const Rect hex_box = row.cut_right(96.0f);
            label(c, row, token, TextAlign::Left, st.selected != token);

            if (st.selected == token) {
                if (text_field(c, hex_box, "hex", st.hex)) {
                    gtheme::Color parsed;
                    if (gtheme::Color::parse(st.hex, parsed)) {
                        editor.set(token, parsed);
                        out.changed = true;
                    }
                }
            } else {
                label(c, hex_box, value.to_hex(), TextAlign::Right, true);
            }
            c.pop_id();
        }
    }

    if (!st.selected.empty()) {
        c.push_id("channels");
        gtheme::Color value = editor.value(st.selected);
        f32* ch[4] = {&value.r, &value.g, &value.b, &value.a};
        const char* names[4] = {"R", "G", "B", "A"};
        bool moved = false;
        for (int i = 0; i < 4; ++i) {
            const Rect row = cursor.cut_top(s.row_height);
            used += s.row_height;
            Rect lab = row;
            const Rect sl = lab.cut_right(row.w - 24.0f);
            label(c, lab, names[i], TextAlign::Left, true);
            if (slider(c, sl, names[i], *ch[i], 0.0f, 1.0f)) moved = true;
        }
        if (moved) {
            editor.set(st.selected, value);
            st.hex = value.to_hex();
            out.changed = true;
        }
        c.pop_id();
    }

    end_scroll(c, area, st.scroll, used);

    const f32 bw = (actions.w - s.spacing * 2.0f) / 3.0f;
    Rect ar = actions;
    const Rect b0 = ar.cut_left(bw);
    ar.cut_left(s.spacing);
    const Rect b1 = ar.cut_left(bw);
    ar.cut_left(s.spacing);
    const Rect b2 = ar;

    if (button(c, b0, "Commit", editor.dirty(), true)) {
        editor.commit();
        out.committed = true;
    }
    if (button(c, b1, "Revert", editor.dirty())) {
        editor.revert();
        st.hex = st.selected.empty() ? std::string()
                                     : editor.value(st.selected).to_hex();
        out.reverted = true;
    }
    if (button(c, b2, "Duplicate")) {
        out.duplicated = editor.duplicate_active(
            st.new_name.empty() ? "My Theme" : st.new_name);
    }

    c.pop_id();
    return out;
}

PropertyEdit properties_panel(Context& c, const Rect& r,
                              const std::vector<PropertyRow>& rows,
                              PropertyPanelState& st) {
    PropertyEdit out;
    const Style& s = c.style();
    c.push_id("properties");

    const Rect view = begin_scroll(c, r, st.scroll);
    Rect cursor = view;
    f32 used = 0.0f;

    std::vector<std::string> order;
    for (const PropertyRow& row : rows)
        if (!contains(order, row.group)) order.push_back(row.group);

    for (const std::string& g : order) {
        const Rect head = cursor.cut_top(s.row_height);
        used += s.row_height;
        if (group_header(c, head, g, st.is_collapsed(g))) st.toggle_group(g);
        if (st.is_collapsed(g)) continue;

        for (std::size_t i = 0; i < rows.size(); ++i) {
            const PropertyRow& row = rows[i];
            if (row.group != g) continue;

            Rect line = cursor.cut_top(s.row_height);
            used += s.row_height;
            c.push_id(row.name);

            const Rect field = line.cut_right(line.w * 0.5f);
            label(c, line, row.name, TextAlign::Left, !row.enabled);

            if (row.varies) {
                label(c, field, "*VARIES*", TextAlign::Left, true);
                c.pop_id();
                continue;
            }

            switch (row.kind) {
                case PropertyKind::ReadOnly:
                    label(c, field, row.value, TextAlign::Left, true);
                    break;

                case PropertyKind::Bool: {
                    bool v = (row.value == "1" || row.value == "true" ||
                              row.value == "Yes");
                    if (checkbox(c, field, "", v)) {
                        out.edited = true;
                        out.row = i;
                        out.value = v ? "1" : "0";
                    }
                    break;
                }

                case PropertyKind::Color: {
                    gtheme::Color col;
                    if (!gtheme::Color::parse(row.value, col))
                        col = gtheme::Color::rgb(1, 1, 1);
                    Rect sw = field;
                    sw.w = s.row_height;
                    color_swatch(c, sw.deflate(Insets::all(3.0f)),
                                 Color{col.r, col.g, col.b, col.a});
                    Rect rest = field;
                    rest.x += s.row_height + s.spacing;
                    rest.w -= s.row_height + s.spacing;
                    std::string v = row.value;
                    if (text_field(c, rest, "c", v)) {
                        out.edited = true;
                        out.row = i;
                        out.value = v;
                    }
                    break;
                }

                case PropertyKind::Choice: {
                    i32 sel = -1;
                    for (std::size_t k = 0; k < row.choices.size(); ++k)
                        if (row.choices[k] == row.value) sel = static_cast<i32>(k);
                    const i32 picked = dropdown(c, field, "ch", row.choices, sel);
                    if (picked >= 0 &&
                        static_cast<std::size_t>(picked) < row.choices.size()) {
                        out.edited = true;
                        out.row = i;
                        out.value = row.choices[static_cast<std::size_t>(picked)];
                    }
                    break;
                }

                case PropertyKind::Number: {
                    std::string v = row.value;
                    const NumericResult n =
                        numeric_field(c, field, "n", v, row.suffix);
                    if (n.committed) {
                        out.edited = true;
                        out.row = i;
                        out.value = n.text;
                    }
                    break;
                }

                case PropertyKind::Text:
                default: {
                    std::string v = row.value;
                    if (text_field(c, field, "t", v)) {
                        out.edited = true;
                        out.row = i;
                        out.value = v;
                    }
                    break;
                }
            }
            c.pop_id();
        }
    }

    end_scroll(c, r, st.scroll, used);
    c.pop_id();
    return out;
}

bool palette_match(std::string_view command, std::string_view query, i32& score) {
    score = 0;
    if (query.empty()) return true;

    auto lower = [](char ch) {
        return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    };

    std::size_t qi = 0;
    i32 gaps = 0;
    std::size_t last = 0;
    bool first = true;
    for (std::size_t i = 0; i < command.size() && qi < query.size(); ++i) {
        if (lower(command[i]) != lower(query[qi])) continue;
        if (first) {
            score += static_cast<i32>(i) * 4;
            first = false;
        } else {
            gaps += static_cast<i32>(i - last - 1);
        }
        last = i;
        ++qi;
    }
    if (qi != query.size()) return false;
    score += gaps;
    return true;
}

std::vector<std::size_t> palette_filter(const std::vector<PaletteCommand>& cmds,
                                        std::string_view query) {
    std::vector<std::pair<i32, std::size_t>> hits;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        i32 score = 0;
        if (!palette_match(cmds[i].name, query, score)) {
            i32 alt = 0;
            if (cmds[i].shortcut.empty() ||
                !palette_match(cmds[i].shortcut, query, alt))
                continue;
            score = alt + 1000;
        }
        hits.push_back({score, i});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::size_t> out;
    out.reserve(hits.size());
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

i32 command_palette(Context& c, const Rect& r,
                    const std::vector<PaletteCommand>& cmds,
                    CommandPaletteState& st) {
    if (!st.open) return -1;
    const Style& s = c.style();
    c.push_id("palette");

    if (c.input().key_escape) {
        st.open = false;
        c.pop_id();
        return -1;
    }

    c.draw().shadow(r, s.radius, s.shadow_spread, tok(c, T::ui_shadow));
    c.draw().round_rect(r, s.radius, tok(c, T::ui_surface_raised));
    c.draw().border(r, s.radius, s.border, tok(c, T::ui_border));

    Rect area = r.deflate(Insets::all(s.spacing));
    const Rect field = area.cut_top(s.control_height);
    area.cut_top(s.spacing);

    const Id fid = c.id("query");
    if (!c.is_focused(fid)) c.set_focus(fid);
    const std::string before = st.query;
    text_field(c, field, "query", st.query, "Type a command");
    if (st.query != before) st.highlighted = 0;

    const std::vector<std::size_t> shown = palette_filter(cmds, st.query);
    const i32 n = static_cast<i32>(shown.size());
    if (n == 0) {
        label(c, area, "No matching command", TextAlign::Centre, true);
        c.pop_id();
        return -1;
    }

    if (c.input().key_down) st.highlighted = (st.highlighted + 1) % n;
    if (c.input().key_up) st.highlighted = (st.highlighted + n - 1) % n;
    if (st.highlighted >= n) st.highlighted = 0;
    if (st.highlighted < 0) st.highlighted = 0;

    i32 run = -1;
    const Rect view = begin_scroll(c, area, st.scroll);
    Rect cursor = view;
    for (i32 i = 0; i < n; ++i) {
        const PaletteCommand& cmd = cmds[shown[static_cast<std::size_t>(i)]];
        Rect row = cursor.cut_top(s.row_height);
        const Id rid = c.id(cmd.name);
        const auto in = c.interact(rid, row, cmd.enabled);

        if (in.hovered) st.highlighted = i;
        if (st.highlighted == i)
            c.draw().round_rect(row, s.radius_small, tok(c, T::ui_accent));

        Rect line = row.deflate(Insets::xy(s.spacing, 0.0f));
        if (!cmd.shortcut.empty()) {
            const Rect sc = line.cut_right(80.0f);
            label(c, sc, cmd.shortcut, TextAlign::Right, true);
        }
        label(c, line, cmd.name, TextAlign::Left, !cmd.enabled);
        c.announce(rid, Context::Role::Row, cmd.name, cmd.shortcut, row,
                   st.highlighted == i, !cmd.enabled);

        if (cmd.enabled && (in.clicked || (c.input().key_enter && st.highlighted == i)))
            run = static_cast<i32>(shown[static_cast<std::size_t>(i)]);
    }
    end_scroll(c, area, st.scroll, static_cast<f32>(n) * s.row_height);

    if (run >= 0) {
        st.open = false;
        st.query.clear();
        st.highlighted = 0;
    }
    c.pop_id();
    return run;
}

LiveRulesResult live_rules_panel(Context& c, const Rect& r,
                                 std::vector<LiveRuleRow>& rules,
                                 LiveRulesState& st) {
    LiveRulesResult out;
    const Style& s = c.style();
    c.push_id("live-rules");

    const Rect view = begin_scroll(c, r, st.scroll);
    Rect cursor = view;

    for (std::size_t i = 0; i < rules.size(); ++i) {
        LiveRuleRow& rule = rules[i];
        Rect row = cursor.cut_top(s.row_height);
        c.push_id(rule.name + std::to_string(i));

        const Id rid = c.id("row");
        const auto in = c.interact(rid, row);
        if (in.hovered) out.hovered = static_cast<i32>(i);

        const Rect box = row.cut_left(s.row_height);
        bool on = rule.enabled;
        if (checkbox(c, box, "", on)) {
            rule.enabled = on;
            out.toggled = static_cast<i32>(i);
        }

        const Rect tol = row.cut_right(64.0f);
        label(c, row, rule.name, TextAlign::Left,
              !rule.enabled || !rule.applied);
        if (rule.tolerance > 0.0f) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.4g", static_cast<double>(rule.tolerance));
            label(c, tol, buf, TextAlign::Right, true);
        }

        if (!rule.detail.empty()) {
            const Rect sub = cursor.cut_top(s.row_height * 0.8f);
            Rect indented = sub;
            indented.x += s.row_height;
            indented.w -= s.row_height;
            label(c, indented, rule.detail, TextAlign::Left, true);
        }

        c.announce(rid, Context::Role::Row, rule.name,
                   rule.applied ? "holding" : "not applied", row,
                   out.hovered == static_cast<i32>(i), false);
        c.pop_id();
    }

    if (rules.empty())
        label(c, view, "No rules inferred", TextAlign::Centre, true);

    end_scroll(c, r, st.scroll,
               static_cast<f32>(rules.size()) * s.row_height * 1.8f);
    st.hovered = out.hovered;
    c.pop_id();
    return out;
}

}
