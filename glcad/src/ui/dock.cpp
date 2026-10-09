#include "ui/dock.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

namespace gui {

namespace T = gtheme::token;

void Dock::add(PanelState p) {
    for (auto& e : panels_)
        if (e.id == p.id) { e = std::move(p); return; }
    panels_.push_back(std::move(p));
}

PanelState* Dock::find(std::string_view id) {
    for (auto& p : panels_)
        if (p.id == id) return &p;
    return nullptr;
}
const PanelState* Dock::find(std::string_view id) const {
    for (const auto& p : panels_)
        if (p.id == id) return &p;
    return nullptr;
}

void Dock::toggle(std::string_view id) {
    if (auto* p = find(id)) p->open = !p->open;
}
void Dock::collapse(std::string_view id, bool c) {
    if (auto* p = find(id)) p->collapsed = c;
}
void Dock::move_to(std::string_view id, Side s) {
    if (auto* p = find(id)) p->side = s;
}

PanelSlot Dock::slot(std::string_view id) const {
    for (const auto& [k, v] : slots_)
        if (k == id) return v;
    return {};
}

void Dock::draw_frame(Context& c, PanelState& p, const Rect& frame) {
    const Style& st = c.style();
    c.draw().round_rect(frame, st.radius, c.theme().get(T::ui_surface));
    c.draw().border(frame, st.radius, st.border, c.theme().get(T::ui_border));

    Rect inner = frame.deflate(Insets::all(st.border));
    Rect bar = inner.cut_top(st.row_height);

    c.push_id(p.id);

    Rect chev = bar.cut_left(st.row_height);
    const Id cid = c.id("collapse");
    const auto ci = c.interact(cid, chev);
    if (ci.hovered) c.draw().round_rect(chev, st.radius_small, c.theme().get(T::ui_hover));
    draw_icon(c, p.collapsed ? Icon::ChevronRight : Icon::ChevronDown, chev,
              c.theme().get(T::ui_text_dim), 1.5f);
    if (ci.clicked) p.collapsed = !p.collapsed;

    Rect close = bar.cut_right(st.row_height);
    const Id xid = c.id("close");
    const auto xi = c.interact(xid, close);
    if (xi.hovered) c.draw().round_rect(close, st.radius_small, c.theme().get(T::ui_hover));
    draw_icon(c, Icon::Close, close, c.theme().get(T::ui_text_dim), 1.4f);
    if (xi.clicked) p.open = false;

    const Id drag_id = c.id("drag");
    const auto di = c.interact(drag_id, bar);
    if (di.held && !drag_.active) {
        drag_.active = true;
        drag_.id = p.id;
        drag_.grab = {c.input().mouse.x - bar.x, c.input().mouse.y - bar.y};
    }

    c.draw().text(bar.deflate(Insets::xy(4.0f, 0.0f)), p.title, st.font_size,
                  c.theme().get(T::ui_text));

    if (!p.collapsed)
        c.draw().line({inner.x, bar.bottom()}, {inner.right(), bar.bottom()},
                      st.border, c.theme().get(T::ui_border));
    c.pop_id();
}

Rect Dock::layout_side(Context& c, Rect& area, Side s) {
    const Style& st = c.style();
    const bool horizontal = (s == Side::Left || s == Side::Right);

    std::vector<PanelState*> here;
    for (auto& p : panels_)
        if (p.side == s && p.open) here.push_back(&p);
    if (here.empty()) return {};
    std::sort(here.begin(), here.end(),
              [](const PanelState* a, const PanelState* b) { return a->order < b->order; });

    f32 extent = 0.0f;
    for (const auto* p : here)
        extent = std::max(extent, p->collapsed ? st.row_height + 2.0f * st.border
                                               : p->extent);

    Rect band = horizontal ? (s == Side::Left ? area.cut_left(extent)
                                              : area.cut_right(extent))
                           : (s == Side::Top ? area.cut_top(extent)
                                             : area.cut_bottom(extent));

    {
        const f32 grip = 5.0f;
        Rect sp = horizontal
            ? Rect{s == Side::Left ? band.right() - grip * 0.5f
                                   : band.x - grip * 0.5f, band.y, grip, band.h}
            : Rect{band.x, s == Side::Top ? band.bottom() - grip * 0.5f
                                          : band.y - grip * 0.5f, band.w, grip};
        c.push_id("dock-split");
        c.push_id(horizontal ? (s == Side::Left ? "L" : "R")
                             : (s == Side::Top ? "T" : "B"));
        const auto si = c.interact(c.id("grip"), sp);
        if (si.hovered || si.held)
            c.draw().rect(sp, c.theme().get(si.held ? T::ui_accent : T::ui_hover));
        if (si.held) {
            const f32 d = horizontal ? c.input().mouse_delta.x : c.input().mouse_delta.y;
            const f32 sign = (s == Side::Left || s == Side::Top) ? 1.0f : -1.0f;
            for (auto* p : here)
                if (!p->collapsed)
                    p->extent = std::clamp(p->extent + d * sign, p->min_extent,
                                           p->max_extent);
            if (d != 0.0f) changed_ = true;
        }
        c.pop_id();
        c.pop_id();
    }

    c.draw().rect(band, c.theme().get(T::ui_background));
    Rect cursor = band.deflate(Insets::all(st.panel_gap * 0.5f));

    const f32 title_h = st.row_height + 2.0f * st.border;
    std::vector<f32> heights(here.size(), 0.0f);
    if (horizontal) {
        f32 avail = cursor.h - st.panel_gap * 0.5f * static_cast<f32>(here.size());
        f32 shares = 0.0f;
        for (std::size_t i = 0; i < here.size(); ++i) {
            if (here[i]->collapsed) { heights[i] = title_h; avail -= title_h; }
            else shares += std::max(here[i]->share, 0.05f);
        }
        avail = std::max(avail, 0.0f);
        f32 needs = 0.0f;
        for (std::size_t i = 0; i < here.size(); ++i) {
            if (here[i]->collapsed) continue;
            heights[i] = std::max(here[i]->preferred, st.row_height * 3.0f);
            needs += heights[i];
        }
        const f32 extra = std::max(avail - needs, 0.0f);
        for (std::size_t i = 0; i < here.size(); ++i)
            if (!here[i]->collapsed)
                heights[i] += extra * std::max(here[i]->share, 0.05f) / std::max(shares, 1e-6f);
        for (std::size_t i = 0; i < here.size(); ++i)
            if (!here[i]->collapsed) heights[i] = std::max(heights[i], title_h);
        f32 sum = 0.0f;
        for (f32 h : heights) sum += h;
        const f32 room = std::max(cursor.h - st.panel_gap * 0.5f * static_cast<f32>(here.size()), 0.0f);
        if (sum > room && room > 0.0f) {
            const f32 floor_h = title_h + st.row_height_dense * 2.0f;
            f32 closed_sum = 0.0f, floors = 0.0f, excess = 0.0f;
            for (std::size_t i = 0; i < here.size(); ++i) {
                if (here[i]->collapsed) { closed_sum += heights[i]; continue; }
                const f32 fl = std::min(heights[i], floor_h);
                floors += fl;
                excess += heights[i] - fl;
            }
            const f32 spare = std::max(room - closed_sum - floors, 0.0f);
            for (std::size_t i = 0; i < here.size(); ++i) {
                if (here[i]->collapsed) continue;
                const f32 fl = std::min(heights[i], floor_h);
                heights[i] = fl + (excess > 0.0f ? (heights[i] - fl) * spare / excess : 0.0f);
            }
        }
    }

    for (std::size_t i = 0; i < here.size(); ++i) {
        auto* p = here[i];
        const f32 want = horizontal ? std::min(cursor.h, heights[i]) : p->extent;

        Rect frame = horizontal ? cursor.cut_top(want) : cursor.cut_left(p->extent);

        if (horizontal && i + 1 < here.size() && !p->collapsed && !here[i + 1]->collapsed) {
            const f32 grip = 6.0f;
            Rect sp{frame.x, frame.bottom() - grip * 0.5f + st.panel_gap * 0.25f, frame.w, grip};
            c.push_id("dock-hsplit");
            c.push_id(p->id);
            const auto si = c.interact(c.id("grip"), sp);
            if (si.hovered || si.held)
                c.draw().rect(sp, c.theme().get(si.held ? T::ui_accent : T::ui_hover));
            if (si.held) {
                const f32 d = c.input().mouse_delta.y;
                const f32 total = heights[i] + heights[i + 1];
                if (d != 0.0f && total > 0.0f) {
                    const f32 a = std::clamp(heights[i] + d, st.row_height * 2.0f, total - st.row_height * 2.0f);
                    const f32 pair = p->share + here[i + 1]->share;
                    p->share = pair * a / total;
                    here[i + 1]->share = pair - p->share;
                    changed_ = true;
                }
            }
            c.pop_id();
            c.pop_id();
        }
        cursor.cut_top(st.panel_gap * 0.5f);

        draw_frame(c, *p, frame);

        PanelSlot sl;
        sl.frame = frame;
        {
            Rect bar = frame.deflate(Insets::all(st.border));
            bar.h = st.row_height;
            sl.title = bar;
        }
        sl.visible = true;
        sl.collapsed = p->collapsed;
        Rect visible{};
        if (!p->collapsed) {
            Rect content = frame.deflate(Insets::all(st.border));
            content.cut_top(st.row_height);
            visible = content.deflate(Insets::all(st.spacing));
            sl.content = visible;
            const f32 need = std::max(p->preferred, p->content_h);
            if (need > visible.h + 1.0f) {
                f32* offset = nullptr;
                for (auto& [k, v] : scroll_) if (k == p->id) offset = &v;
                if (!offset) { scroll_.emplace_back(p->id, 0.0f); offset = &scroll_.back().second; }
                const f32 max_off = need - visible.h;
                if (frame.contains(c.input().mouse) && c.input().wheel != 0.0f)
                    *offset = std::clamp(*offset - c.input().wheel * 24.0f, 0.0f, max_off);
                *offset = std::clamp(*offset, 0.0f, max_off);
                sl.content.y -= *offset;
                sl.content.h = need;
                const f32 track = visible.h;
                const f32 thumb_h = std::max(track * visible.h / need, 16.0f);
                const f32 thumb_y = visible.y + (track - thumb_h) * (*offset / max_off);
                Color bar = c.theme().get(T::ui_text_dim);
                bar.a = 0.35f;
                c.draw().round_rect({visible.right() - 3.0f, thumb_y, 3.0f, thumb_h}, 1.5f, bar);
            }
        }
        slots_.emplace_back(p->id, sl);
        clips_.emplace_back(p->id, visible);
    }
    return band;
}

Rect Dock::layout(Context& c, Rect area) {
    const Rect full = area;
    slots_.clear();
    clips_.clear();

    layout_side(c, area, Side::Top);
    layout_side(c, area, Side::Bottom);
    layout_side(c, area, Side::Left);
    layout_side(c, area, Side::Right);

    const Style& st = c.style();
    for (auto& p : panels_) {
        if (p.side != Side::Floating || !p.open) continue;
        c.draw().shadow(p.floating, st.radius, st.shadow_spread,
                        c.theme().get(T::ui_shadow));
        draw_frame(c, p, p.floating);

        c.push_id(p.id);
        Rect bar = p.floating;
        bar.h = st.row_height;
        bar.x += st.row_height;
        bar.w -= st.row_height * 2.0f;
        const auto di = c.interact(c.id("drag"), bar);
        if (di.held) {
            p.floating.x += c.input().mouse_delta.x;
            p.floating.y += c.input().mouse_delta.y;
        }
        c.pop_id();

        PanelSlot sl;
        sl.frame = p.floating;
        sl.visible = true;
        sl.collapsed = p.collapsed;
        if (!p.collapsed) {
            Rect content = p.floating.deflate(Insets::all(st.border));
            content.cut_top(st.row_height);
            sl.content = content.deflate(Insets::all(st.spacing));
        }
        slots_.emplace_back(p.id, sl);
    }

    if (drag_.active) {
        drag_.target = drop_target(full, c.input().mouse, drag_.target_valid);
        if (drag_.target_valid) {
            const PanelState* dragged = find(drag_.id);
            const Rect preview = drop_preview(
                full, drag_.target, dragged ? dragged->extent : 260.0f);
            Color accent = c.theme().get(T::ui_accent);
            accent.a = 0.25f;
            c.draw().rect(preview, accent);
            c.draw().border(preview, st.radius, 2.0f, c.theme().get(T::ui_accent));
        }

        if (c.input().released) {
            if (PanelState* p = find(drag_.id)) {
                if (drag_.target_valid) {
                    if (drag_.target == Side::Floating) {
                        p->floating.x = c.input().mouse.x - drag_.grab.x;
                        p->floating.y = c.input().mouse.y - drag_.grab.y;
                    }
                    p->side = drag_.target;
                }
            }
            drag_ = DragState{};
        }
    }

    return area;
}

void Dock::begin_content(Context& c, std::string_view id) const {
    for (const auto& [k, r] : clips_)
        if (k == id) { c.push_clip(r); return; }
}
void Dock::end_content(Context& c, std::string_view id) const {
    for (const auto& [k, r] : clips_)
        if (k == id) { c.pop_clip(); return; }
}


std::string Dock::save(f32 vw, f32 vh) const {
    std::string out;
    char buf[256];
    for (const auto& p : panels_) {
        const bool horizontal = (p.side == Side::Left || p.side == Side::Right);
        const f32 frac = horizontal ? (vw > 0 ? p.extent / vw : 0) : (vh > 0 ? p.extent / vh : 0);
        std::snprintf(buf, sizeof(buf), "%s side=%d extent=%.5f share=%.4f open=%d collapsed=%d order=%d\n",
                      p.id.c_str(), static_cast<int>(p.side), frac, p.share, p.open ? 1 : 0,
                      p.collapsed ? 1 : 0, p.order);
        out += buf;
    }
    return out;
}

void Dock::restore(std::string_view text, f32 vw, f32 vh) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string line(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        const std::size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        PanelState* p = find(line.substr(0, sp));
        if (!p) continue;
        int side = 0, open = 1, collapsed = 0, order = 0;
        float frac = 0, share = 1;
        if (std::sscanf(line.c_str() + sp, " side=%d extent=%f share=%f open=%d collapsed=%d order=%d",
                        &side, &frac, &share, &open, &collapsed, &order) != 6) continue;
        p->side = static_cast<Side>(side);
        const bool horizontal = (p->side == Side::Left || p->side == Side::Right);
        const f32 px = frac * (horizontal ? vw : vh);
        if (px > 0.0f) p->extent = std::clamp(px, p->min_extent, p->max_extent);
        p->share = std::clamp(share, 0.05f, 20.0f);
        p->open = open != 0;
        p->collapsed = collapsed != 0;
        p->order = order;
    }
}

Side Dock::drop_target(const Rect& area, Vec2 at, bool& valid) const {
    const f32 band_x = area.w * 0.2f;
    const f32 band_y = area.h * 0.2f;

    valid = true;
    if (at.x < area.x || at.x > area.right() || at.y < area.y ||
        at.y > area.bottom()) {
        valid = false;
        return Side::Floating;
    }

    const f32 dl = at.x - area.x;
    const f32 dr = area.right() - at.x;
    const f32 dt = at.y - area.y;
    const f32 db = area.bottom() - at.y;

    const f32 nearest = std::min(std::min(dl, dr), std::min(dt, db));
    if (nearest == dl && dl < band_x) return Side::Left;
    if (nearest == dr && dr < band_x) return Side::Right;
    if (nearest == dt && dt < band_y) return Side::Top;
    if (nearest == db && db < band_y) return Side::Bottom;

    return Side::Floating;
}

Rect Dock::drop_preview(const Rect& area, Side s, f32 extent) const {
    const f32 w = std::clamp(extent, 0.0f, area.w);
    const f32 h = std::clamp(extent, 0.0f, area.h);
    switch (s) {
        case Side::Left:   return {area.x, area.y, w, area.h};
        case Side::Right:  return {area.right() - w, area.y, w, area.h};
        case Side::Top:    return {area.x, area.y, area.w, h};
        case Side::Bottom: return {area.x, area.bottom() - h, area.w, h};
        case Side::Floating:
            return {area.x + area.w * 0.5f - w * 0.5f,
                    area.y + area.h * 0.5f - h * 0.5f, w, h};
    }
    return {};
}

}
