#include <algorithm>
#include <cmath>

#include "ui/widgets.hpp"

namespace gui {

namespace T = gtheme::token;

namespace {

f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

}

Rect begin_scroll(Context& c, const Rect& r, ScrollState& s) {
    c.push_clip(r);

    const Vec2 m = c.input().mouse;
    const bool over = m.x >= r.x && m.x <= r.x + r.w && m.y >= r.y &&
                      m.y <= r.y + r.h;
    if (over && c.input().wheel != 0.0f) s.offset -= c.input().wheel * 40.0f;

    const f32 max = std::max(0.0f, s.content_extent - r.h);
    s.offset = clampf(s.offset, 0.0f, max);

    Rect inner = r;
    inner.y -= s.offset;
    inner.h = std::max(r.h, s.content_extent);
    return inner;
}

void end_scroll(Context& c, const Rect& r, ScrollState& s, f32 content_extent) {
    s.content_extent = content_extent;
    const f32 max = std::max(0.0f, content_extent - r.h);
    s.offset = clampf(s.offset, 0.0f, max);
    c.pop_clip();

    if (max <= 0.0f) return;

    const f32 track_w = 6.0f;
    const Rect track{r.x + r.w - track_w - 2.0f, r.y, track_w, r.h};
    c.draw().round_rect(track, track_w * 0.5f, c.theme().get(T::ui_surface));

    const f32 visible = r.h / std::max(1.0f, content_extent);
    const f32 thumb_h = std::max(24.0f, r.h * visible);
    const f32 t = max > 0.0f ? s.offset / max : 0.0f;
    const Rect thumb{track.x, r.y + (r.h - thumb_h) * t, track_w, thumb_h};

    const Id id = c.id("scroll.thumb");
    const auto hit = c.interact(id, thumb);
    if (hit.held) {
        s.offset += c.input().mouse_delta.y * (content_extent / std::max(1.0f, r.h));
        s.offset = clampf(s.offset, 0.0f, max);
    }
    c.draw().round_rect(thumb, track_w * 0.5f,
                        c.theme().get(hit.hovered ? T::ui_accent : T::ui_border));
}

int tabs(Context& c, const Rect& r, const std::vector<std::string>& labels,
         int current) {
    if (labels.empty()) return current;

    const f32 w = r.w / static_cast<f32>(labels.size());
    int chosen = current;

    for (std::size_t i = 0; i < labels.size(); ++i) {
        const Rect tab{r.x + w * static_cast<f32>(i), r.y, w, r.h};
        c.push_id(labels[i]);
        const Id id = c.id("tab");
        const auto hit = c.interact(id, tab);
        c.pop_id();

        const bool active = static_cast<int>(i) == current;
        if (hit.clicked) chosen = static_cast<int>(i);

        if (active || hit.hovered)
            c.draw().rect(tab, c.theme().get(active ? T::ui_surface : T::ui_hover));
        c.draw().text({tab.x, tab.y, tab.w, tab.h}, labels[i],
                      c.style().font_size,
                      c.theme().get(active ? T::ui_text : T::ui_text_dim),
                      TextAlign::Centre);

        if (active)
            c.draw().rect({tab.x, tab.y + tab.h - 2.0f, tab.w, 2.0f},
                          c.theme().get(T::ui_accent));
    }

    c.draw().line({r.x, r.y + r.h}, {r.x + r.w, r.y + r.h}, 1.0f,
                  c.theme().get(T::ui_border));
    return chosen;
}

bool TreeState::is_collapsed(std::string_view id) const {
    for (const std::string& s : collapsed)
        if (s == id) return true;
    return false;
}

void TreeState::toggle(std::string_view id) {
    for (std::size_t i = 0; i < collapsed.size(); ++i)
        if (collapsed[i] == id) {
            collapsed.erase(collapsed.begin() + static_cast<long>(i));
            return;
        }
    collapsed.emplace_back(id);
}

TreeResult tree(Context& c, const Rect& r, const std::vector<TreeNode>& nodes,
                TreeState& state) {
    TreeResult out;
    const f32 row = c.style().row_height;
    const f32 indent = 14.0f;

    f32 y = r.y;
    int skip_below_depth = -1;

    for (const TreeNode& n : nodes) {
        if (skip_below_depth >= 0) {
            if (n.depth > skip_below_depth) continue;
            skip_below_depth = -1;
        }

        const Rect line{r.x, y, r.w, row};
        y += row;
        out.rows_drawn++;

        if (y > r.y + r.h + row) {
            if (n.has_children && state.is_collapsed(n.id))
                skip_below_depth = n.depth;
            continue;
        }

        c.push_id(n.id);
        const Id row_id = c.id("row");
        const auto hit = c.interact(row_id, line);

        const bool selected = state.selected == n.id;
        if (selected) c.draw().rect(line, c.theme().get(T::ui_accent));
        else if (hit.hovered) c.draw().rect(line, c.theme().get(T::ui_hover));

        const f32 x = r.x + 6.0f + indent * static_cast<f32>(n.depth);

        if (n.has_children) {
            const Rect chev{x, y - row, 14.0f, row};
            const Id chev_id = c.id("chev");
            const auto chit = c.interact(chev_id, chev);
            if (chit.clicked) {
                state.toggle(n.id);
                out.toggled = n.id;
            }
            const bool folded = state.is_collapsed(n.id);
            const Vec2 mid{chev.x + 5.0f, chev.y + chev.h * 0.5f};
            const Color ink = c.theme().get(T::ui_text_dim);
            if (folded) {
                c.draw().line({mid.x, mid.y - 4.0f}, {mid.x + 4.0f, mid.y}, 1.4f, ink);
                c.draw().line({mid.x + 4.0f, mid.y}, {mid.x, mid.y + 4.0f}, 1.4f, ink);
            } else {
                c.draw().line({mid.x - 4.0f, mid.y - 2.0f}, {mid.x, mid.y + 2.0f},
                              1.4f, ink);
                c.draw().line({mid.x, mid.y + 2.0f}, {mid.x + 4.0f, mid.y - 2.0f},
                              1.4f, ink);
            }
        }

        c.draw().text({x + 16.0f, line.y, r.w - (x - r.x) - 20.0f, row}, n.label,
                      c.style().font_size,
                      c.theme().get(selected ? T::ui_text : T::ui_text_dim));

        if (hit.clicked) {
            state.selected = n.id;
            out.clicked = n.id;
        }
        c.pop_id();

        if (n.has_children && state.is_collapsed(n.id)) skip_below_depth = n.depth;
    }

    out.content_height = y - r.y;
    return out;
}

TableResult table(Context& c, const Rect& r,
                  const std::vector<TableColumn>& columns,
                  const std::vector<std::vector<std::string>>& rows,
                  TableState& state) {
    TableResult out;
    if (columns.empty()) return out;

    const f32 head_h = c.style().row_height;
    const Rect head{r.x, r.y, r.w, head_h};
    c.draw().rect(head, c.theme().get(T::ui_surface));

    f32 x = r.x;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const Rect cell{x, head.y, columns[i].width, head_h};
        c.push_id(columns[i].label);
        const Id id = c.id("head");
        const auto hit = c.interact(id, cell);
        c.pop_id();

        if (hit.clicked) {
            if (state.sort_column == static_cast<int>(i))
                state.sort_ascending = !state.sort_ascending;
            else {
                state.sort_column = static_cast<int>(i);
                state.sort_ascending = true;
            }
            out.sort_changed = true;
        }

        std::string title = columns[i].label;
        if (state.sort_column == static_cast<int>(i))
            title += state.sort_ascending ? "  \xe2\x96\xb2" : "  \xe2\x96\xbc";
        c.draw().text({cell.x + 6.0f, cell.y, cell.w - 12.0f, cell.h}, title,
                      c.style().font_size_small, c.theme().get(T::ui_text_dim),
                      columns[i].align);
        x += columns[i].width;
    }
    c.draw().line({r.x, r.y + head_h}, {r.x + r.w, r.y + head_h}, 1.0f,
                  c.theme().get(T::ui_border));

    const Rect body{r.x, r.y + head_h, r.w, r.h - head_h};
    const Rect inner = begin_scroll(c, body, state.scroll);

    const f32 row_h = c.style().row_height;
    f32 y = inner.y;
    for (std::size_t ri = 0; ri < rows.size(); ++ri) {
        const Rect line{inner.x, y, inner.w, row_h};
        y += row_h;
        if (line.y + row_h < body.y || line.y > body.y + body.h) continue;

        c.push_id(std::to_string(ri));
        const Id id = c.id("row");
        const auto hit = c.interact(id, line);
        c.pop_id();

        const bool selected = state.selected_row == static_cast<int>(ri);
        if (selected) c.draw().rect(line, c.theme().get(T::ui_accent));
        else if (hit.hovered) c.draw().rect(line, c.theme().get(T::ui_hover));
        else if (ri % 2)
            c.draw().rect(line, c.theme().get(T::ui_surface));

        f32 cx = inner.x;
        for (std::size_t ci = 0; ci < columns.size(); ++ci) {
            const std::string& text =
                ci < rows[ri].size() ? rows[ri][ci] : std::string();
            c.draw().text({cx + 6.0f, line.y, columns[ci].width - 12.0f, row_h},
                          text, c.style().font_size,
                          c.theme().get(selected ? T::ui_text : T::ui_text_dim),
                          columns[ci].align);
            if (hit.clicked && c.input().mouse.x >= cx &&
                c.input().mouse.x < cx + columns[ci].width)
                out.clicked_column = static_cast<int>(ci);
            cx += columns[ci].width;
        }

        if (hit.clicked) {
            state.selected_row = static_cast<int>(ri);
            out.clicked_row = static_cast<int>(ri);
        }
    }

    end_scroll(c, body, state.scroll, static_cast<f32>(rows.size()) * row_h);
    return out;
}

}
