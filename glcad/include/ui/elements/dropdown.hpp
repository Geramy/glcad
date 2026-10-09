#pragma once

#include <string>
#include <vector>

#include "ui/element.hpp"

namespace gui {

class Dropdown : public Element<Dropdown> {
public:
    using Element::Element;

    i32 paint(std::string_view id_label, const std::vector<std::string>& items,
              i32& selected) {
        Context& c = ctx_;
        const Style& s = style();
        const Rect r = rect_;
        const Id id = c.id(id_label);
        const auto in = c.interact(id, r);
        c.register_focusable(id,r,std::string(id_label));
        bool& open = c.state_bool(id, false);
        if (in.clicked || c.activated(id)) open = !open;
        if (c.input().key_escape) open = false;

        c.draw().round_rect(r, s.radius_small,
                            c.theme().get(in.hovered ? gtheme::token::ui_hover
                                                     : gtheme::token::ui_surface_raised));
        c.draw().border(r, s.radius_small, s.border, c.theme().get(gtheme::token::ui_border));
        const std::string shown =
            (selected >= 0 && selected < static_cast<i32>(items.size()))
                ? items[static_cast<std::size_t>(selected)] : std::string{};
        c.announce(id,Context::Role::Button,id_label,shown,r,open);
        contained_text(r.deflate(s.padding), shown, s.font_size,
                       c.theme().get(gtheme::token::ui_text), TextAlign::Left);

        const Vec2 ct{r.right() - 16.0f, r.centre().y};
        c.draw().line({ct.x - 4.0f, ct.y - 2.0f}, {ct.x, ct.y + 2.0f}, 1.5f,
                      c.theme().get(gtheme::token::ui_text_dim));
        c.draw().line({ct.x, ct.y + 2.0f}, {ct.x + 4.0f, ct.y - 2.0f}, 1.5f,
                      c.theme().get(gtheme::token::ui_text_dim));

        i32 picked = -1;
        if (open && !items.empty()) {
            const f32 row_h = s.row_height_dense;
            const f32 want = row_h * static_cast<f32>(items.size());
            const f32 below = c.viewport().bottom() - (r.bottom() + 2.0f);
            const f32 above = r.y - 2.0f - c.viewport().y;
            const f32 room = std::max(std::max(below, above) - 8.0f, row_h * 3.0f);
            const f32 h = std::min(want, room);
            Rect list{r.x, r.bottom() + 2.0f, r.w, h};
            if (below < h && above > below) list.y = r.y - 2.0f - h;
            f32& off = c.state_f32(c.id("list-scroll"), 0.0f);
            const f32 max_off = std::max(0.0f, want - h);
            if (in.clicked && selected >= 0)
                off = std::clamp(row_h * f32(selected) - h * 0.5f, 0.0f, max_off);
            if (list.contains(c.input().mouse) && c.input().wheel != 0.0f)
                off = std::clamp(off - c.input().wheel * row_h, 0.0f, max_off);
            off = std::clamp(off, 0.0f, max_off);
            if (c.input().pressed && !r.contains(c.input().mouse) &&
                !list.contains(c.input().mouse))
                open = false;
            c.begin_overlay(list);
            c.draw().shadow(list, s.radius_small, s.shadow_spread,
                            c.theme().get(gtheme::token::ui_shadow));
            c.draw().round_rect(list, s.radius_small,
                                c.theme().get(gtheme::token::ui_surface_raised));
            c.draw().border(list, s.radius_small, s.border,
                            c.theme().get(gtheme::token::ui_border));
            if (want > h) {
                const f32 th = std::max(h * h / want, 14.0f);
                const f32 ty = list.y + (h - th) * (max_off > 0 ? off / max_off : 0.0f);
                Color bar = c.theme().get(gtheme::token::ui_text_dim);
                bar.a = 0.35f;
                c.draw().round_rect({list.right() - 4.0f, ty, 3.0f, th}, 1.5f, bar);
            }
            c.draw().push_clip(list);
            for (std::size_t i = 0; i < items.size(); ++i) {
                const Rect row{list.x, list.y + row_h * f32(i) - off, list.w, row_h};
                if (row.bottom() <= list.y || row.y >= list.bottom()) continue;
                c.push_id(items[i]);
                const auto item_id=c.id("item");
                const auto visible=row.intersect(list);
                const auto ri = c.interact(item_id, visible);
                c.register_focusable(item_id,visible,items[i]);
                c.announce(item_id,Context::Role::Row,items[i],"",visible,static_cast<i32>(i)==selected);
                c.pop_id();
                if (ri.hovered)
                    c.draw().rect(row, c.theme().get(gtheme::token::ui_hover));
                const bool is_sel = static_cast<i32>(i) == selected;
                Rect lab = row.deflate(s.padding);
                if (!lab.empty())
                    c.draw().text(lab, items[i], s.font_size,
                                  c.theme().get(is_sel ? gtheme::token::ui_accent
                                                       : gtheme::token::ui_text),
                                  TextAlign::Left);
                if (ri.clicked || c.activated(item_id)) {
                    selected = static_cast<i32>(i);
                    picked = selected;
                    open = false;
                    c.set_focus(id);
                }
            }
            c.draw().pop_clip();
            c.end_overlay();
        }
        return picked;
    }
};

}
