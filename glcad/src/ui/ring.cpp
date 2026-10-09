#include <cmath>

#include "ui/icons.hpp"
#include "ui/widgets.hpp"

namespace gui {

namespace T = gtheme::token;

namespace {
constexpr f32 kTau = 6.28318530717958647692f;
}

i32 ring_sector(Vec2 d, std::size_t count, f32 dead_zone) {
    if (count == 0) return -1;
    const f32 r = std::hypot(d.x, d.y);
    if (r < dead_zone) return -1;

    f32 a = std::atan2(d.x, -d.y);
    if (a < 0.0f) a += kTau;

    const f32 step = kTau / static_cast<f32>(count);
    // Half a sector of rotation so item 0 is centred at twelve o'clock.
    i32 i = static_cast<i32>(std::floor((a + step * 0.5f) / step));
    const i32 n = static_cast<i32>(count);
    i %= n;
    if (i < 0) i += n;
    return i;
}

Vec2 ring_item_centre(Vec2 centre, std::size_t index, std::size_t count,
                      f32 radius) {
    if (count == 0) return centre;
    const f32 step = kTau / static_cast<f32>(count);
    const f32 a = step * static_cast<f32>(index);
    return {centre.x + std::sin(a) * radius, centre.y - std::cos(a) * radius};
}

bool arm_long_press(RingState& s, const InputState& in, f64 hold, f32 slop) {
    if (in.pressed) {
        s.pressing = true;
        s.press_started = in.time;
        s.press_at = in.mouse;
        return false;
    }
    if (!in.down) {
        s.pressing = false;
        return false;
    }
    if (!s.pressing) return false;

    if (std::hypot(in.mouse.x - s.press_at.x, in.mouse.y - s.press_at.y) > slop) {
        s.pressing = false;
        return false;
    }

    if (in.time - s.press_started >= hold) {
        s.pressing = false;
        s.open = true;
        s.centre = s.press_at;
        s.hovered = -1;
        s.chosen = -1;
        return true;
    }
    return false;
}

i32 ring(Context& c, RingState& s, const std::vector<RingItem>& items,
         f32 radius, f32 dead_zone) {
    s.chosen = -1;
    if (!s.open || items.empty()) {
        s.open = s.open && !items.empty();
        return -1;
    }
    if (c.input().key_escape) {
        s.open = false;
        s.hovered = -1;
        return -1;
    }

    const Style& st = c.style();
    const Vec2 d{c.input().mouse.x - s.centre.x, c.input().mouse.y - s.centre.y};
    const i32 sector = ring_sector(d, items.size(), dead_zone);
    s.hovered = (sector >= 0 && items[static_cast<std::size_t>(sector)].enabled)
                    ? sector
                    : -1;

    const f32 outer = radius + st.min_touch_target * 0.5f;
    c.draw().shadow({s.centre.x - outer, s.centre.y - outer, outer * 2.0f,
                     outer * 2.0f},
                    outer, st.shadow_spread, c.theme().get(T::ui_shadow));

    const f32 size = st.min_touch_target;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Vec2 p = ring_item_centre(s.centre, i, items.size(), radius);
        const Rect cell{p.x - size * 0.5f, p.y - size * 0.5f, size, size};
        const bool hot = s.hovered == static_cast<i32>(i);

        c.draw().round_rect(cell, size * 0.5f,
                            c.theme().get(hot ? T::ui_accent : T::ui_surface));
        c.draw().border(cell, size * 0.5f, st.border,
                        c.theme().get(T::ui_border));
        if (items[i].icon)
            draw_icon(c, static_cast<Icon>(items[i].icon), cell,
                      c.theme().get(items[i].enabled ? T::ui_text
                                                     : T::ui_text_dim),
                      1.6f);

        if (hot && !items[i].label.empty()) {
            const Rect lab{p.x - 60.0f, p.y + size * 0.5f + 2.0f, 120.0f,
                           st.row_height};
            c.draw().text(lab, items[i].label, st.font_size_small,
                          c.theme().get(T::ui_text), TextAlign::Centre);
        }

        c.announce(c.id(items[i].label), Context::Role::Button, items[i].label, "", cell,
                   hot, !items[i].enabled);
    }

    const Rect eye{s.centre.x - dead_zone, s.centre.y - dead_zone,
                   dead_zone * 2.0f, dead_zone * 2.0f};
    c.draw().border(eye, dead_zone, st.border, c.theme().get(T::ui_border));

    if (c.input().released) {
        s.open = false;
        if (s.hovered >= 0) {
            s.chosen = s.hovered;
            s.hovered = -1;
            return s.chosen;
        }
        s.hovered = -1;
    }
    return -1;
}

}
