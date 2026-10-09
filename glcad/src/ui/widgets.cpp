#include <cstdio>

#include "ui/widgets.hpp"
#include "ui/elements/dropdown.hpp"

#include <algorithm>
#include <cctype>

namespace gui {

namespace T = gtheme::token;

namespace {
Color tok(Context& c, const char* name) { return c.theme().get(name); }

Color surface_for(Context& c, const Context::Interaction& in, Color base) {
    if (in.held) return base.mix(tok(c, T::ui_accent), 0.40f);
    if (in.hovered) return base.mix(tok(c, T::ui_accent), 0.24f);
    return base;
}
}

Rect panel(Context& c, const Rect& bounds, std::string_view title, bool raised) {
    const Style& s = c.style();
    if (raised)
        c.draw().shadow(bounds, s.radius, s.shadow_spread, tok(c, T::ui_shadow));

    c.draw().round_rect(bounds, s.radius,
                        tok(c, raised ? T::ui_surface_raised : T::ui_surface));
    c.draw().border(bounds, s.radius, s.border, tok(c, T::ui_border));

    Rect content = bounds.deflate(Insets::all(s.border));
    if (!title.empty()) {
        const Rect bar = content.cut_top(s.row_height);
        c.draw().text(bar.deflate(s.padding), std::string(title), s.font_size,
                      tok(c, T::ui_text));
        c.draw().line({bar.x, bar.bottom()}, {bar.right(), bar.bottom()},
                      s.border, tok(c, T::ui_border));
    }
    return content.deflate(Insets::all(s.spacing));
}

Rect Stack::next(f32 extent) {
    return axis_ == Axis::Vertical ? area_.cut_top(extent) : area_.cut_left(extent);
}
Rect Stack::next() {
    return next(axis_ == Axis::Vertical ? ctx_.style().control_height : 96.0f);
}
void Stack::gap(f32 amount) {
    const f32 g = amount < 0.0f ? ctx_.style().spacing : amount;
    if (axis_ == Axis::Vertical) area_.cut_top(g);
    else area_.cut_left(g);
}

bool button(Context& c, const Rect& r, std::string_view lbl, bool enabled,
            bool primary) {
    const Style& s = c.style();
    const Id id = c.id(lbl);
    const auto in = c.interact(id, r, enabled);
    if (enabled) c.register_focusable(id, r, lbl);
    c.announce(id, Context::Role::Button, lbl, "", r, false, !enabled);

    Color base = primary ? tok(c, T::ui_accent) : tok(c, T::ui_surface_raised);
    Color fg   = primary ? tok(c, T::ui_accent_text) : tok(c, T::ui_text);
    if (!enabled) {
        base = base.with_alpha(0.45f);
        fg = tok(c, T::ui_text_dim);
    } else {
        base = surface_for(c, in, base);
    }

    c.draw().round_rect(r, s.radius_small, base);
    if (!primary) c.draw().border(r, s.radius_small, s.border, tok(c, T::ui_border));
    if (in.focused && enabled)
        c.draw().border(r.inflate(Insets::all(2.0f)), s.radius_small + 2.0f,
                        s.border * 2.0f, tok(c, T::ui_accent));
    c.draw().text(r, std::string(lbl), s.font_size, fg, TextAlign::Centre);
    return in.clicked || (enabled && c.activated(id));
}

bool toggle(Context& c, const Rect& r, std::string_view lbl, bool& value,
            bool enabled) {
    const Style& s = c.style();
    const Id id = c.id(lbl);
    const auto in = c.interact(id, r, enabled);
    if (enabled) c.register_focusable(id, r, lbl);
    c.announce(id, Context::Role::Toggle, lbl, value ? "on" : "off", r, value,
               !enabled);
    if (in.clicked || (enabled && c.activated(id))) value = !value;

    Color base = value ? tok(c, T::ui_accent) : tok(c, T::ui_surface_raised);
    const Color fg = value ? tok(c, T::ui_accent_text)
                           : tok(c, enabled ? T::ui_text : T::ui_text_dim);
    if (enabled) base = surface_for(c, in, base);

    c.draw().round_rect(r, s.radius_small, base);
    if (!value) c.draw().border(r, s.radius_small, s.border, tok(c, T::ui_border));
    c.draw().text(r, std::string(lbl), s.font_size, fg, TextAlign::Centre);
    return in.clicked || (enabled && c.activated(id));
}

bool checkbox(Context& c, const Rect& r, std::string_view lbl, bool& value) {
    const Style& s = c.style();
    const Id id = c.id(lbl);
    const auto in = c.interact(id, r);
    c.register_focusable(id, r, lbl);
    c.announce(id, Context::Role::Checkbox, lbl, value ? "checked" : "unchecked",
               r, value);
    if (in.clicked || c.activated(id)) value = !value;

    Rect box = r;
    const f32 side = std::min(16.0f, r.h);
    box = {r.x, r.y + (r.h - side) * 0.5f, side, side};

    c.draw().round_rect(box, 3.0f,
                        value ? tok(c, T::ui_accent)
                              : surface_for(c, in, tok(c, T::ui_surface_raised)));
    c.draw().border(box, 3.0f, s.border, tok(c, T::ui_border));
    if (value) {
        const f32 p = side * 0.25f;
        c.draw().line({box.x + p, box.centre().y},
                      {box.centre().x - p * 0.2f, box.bottom() - p}, 2.0f,
                      tok(c, T::ui_accent_text));
        c.draw().line({box.centre().x - p * 0.2f, box.bottom() - p},
                      {box.right() - p * 0.6f, box.y + p}, 2.0f,
                      tok(c, T::ui_accent_text));
    }
    Rect text = r;
    text.x = box.right() + s.spacing;
    text.w -= (box.w + s.spacing);
    c.draw().text(text, std::string(lbl), s.font_size, tok(c, T::ui_text));
    return in.clicked;
}

void label(Context& c, const Rect& r, std::string_view text, TextAlign align,
           bool dim) {
    const f32 size = c.style().font_size;
    std::string out(text);
    if (c.measure(text, size) > r.w) {
        static const char* kEllipsis = "\xe2\x80\xa6";
        const f32 room = r.w - c.measure(kEllipsis, size);
        const std::size_t keep = room > 0.0f ? c.fit(text, size, room) : 0;
        out = std::string(text.substr(0, keep)) + kEllipsis;
    }

    c.draw().text(r, out, c.style().font_size,
                  tok(c, dim ? T::ui_text_dim : T::ui_text), align);
}

bool text_field(Context& c, const Rect& r, std::string_view id_label,
                std::string& value, std::string_view placeholder) {
    const Style& s = c.style();
    const Id id = c.id(id_label);
    const auto in = c.interact(id, r);
    c.register_focusable(id, r, id_label);

    const Rect inner = r.deflate(s.padding);

    // The caret is a byte offset, not a character index.
    i32& caret = c.state_i32(id, static_cast<i32>(value.size()));
    auto clamp_caret = [&] {
        if (caret < 0) caret = 0;
        if (caret > static_cast<i32>(value.size()))
            caret = static_cast<i32>(value.size());
        // Never leave the caret inside a multi-byte UTF-8 sequence.
        while (caret > 0 && caret < static_cast<i32>(value.size()) &&
               (static_cast<unsigned char>(value[static_cast<std::size_t>(caret)]) &
                0xC0u) == 0x80u)
            --caret;
    };
    clamp_caret();

    if (in.clicked || (in.held && c.input().pressed))
        caret = static_cast<i32>(
            c.caret_at(value, s.font_size, c.input().mouse.x - inner.x));

    bool changed = false;
    if (in.focused) {
        const auto& i = c.input();
        if (i.replacement_id==id) { changed=value!=i.replacement_text; value=i.replacement_text; caret=i32(value.size()); }
        auto prev_boundary = [&](i32 at) {
            if (at <= 0) return 0;
            --at;
            while (at > 0 &&
                   (static_cast<unsigned char>(value[static_cast<std::size_t>(at)]) &
                    0xC0u) == 0x80u)
                --at;
            return at;
        };
        auto next_boundary = [&](i32 at) {
            const i32 n = static_cast<i32>(value.size());
            if (at >= n) return n;
            ++at;
            while (at < n &&
                   (static_cast<unsigned char>(value[static_cast<std::size_t>(at)]) &
                    0xC0u) == 0x80u)
                ++at;
            return at;
        };

        if (!i.typed.empty()) {
            value.insert(static_cast<std::size_t>(caret), i.typed);
            caret += static_cast<i32>(i.typed.size());
            changed = true;
        }
        if (i.key_backspace && caret > 0) {
            const i32 from = prev_boundary(caret);
            value.erase(static_cast<std::size_t>(from),
                        static_cast<std::size_t>(caret - from));
            caret = from;
            changed = true;
        }
        if (i.key_delete && caret < static_cast<i32>(value.size())) {
            const i32 to = next_boundary(caret);
            value.erase(static_cast<std::size_t>(caret),
                        static_cast<std::size_t>(to - caret));
            changed = true;
        }
        if (i.key_left)  caret = prev_boundary(caret);
        if (i.key_right) caret = next_boundary(caret);
        if (i.key_home)  caret = 0;
        if (i.key_end)   caret = static_cast<i32>(value.size());
    }
    clamp_caret();

    c.draw().round_rect(r, s.radius_small,
                        surface_for(c, in, tok(c, T::ui_background)));
    c.draw().border(r, s.radius_small, in.focused ? s.border * 2.0f : s.border,
                    tok(c, in.focused ? T::ui_accent : T::ui_border));

    if (value.empty() && !placeholder.empty())
        c.draw().text(inner, std::string(placeholder), s.font_size,
                      tok(c, T::ui_text_dim));
    else
        c.draw().text(inner, value, s.font_size, tok(c, T::ui_text));

    if (in.focused) {
        const f32 cx =
            inner.x +
            c.measure(std::string_view(value).substr(
                          0, static_cast<std::size_t>(caret)),
                      s.font_size);
        c.draw().line({cx, inner.y + 2.0f}, {cx, inner.bottom() - 2.0f}, 1.5f,
                      tok(c, T::ui_accent));
    }
    c.announce(id, Context::Role::Field, id_label, value, r);
    return changed;
}

namespace {

std::size_t utf8_len(std::string_view s, std::size_t i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    std::size_t n = 1;
    if ((ch & 0xE0u) == 0xC0u) n = 2;
    else if ((ch & 0xF0u) == 0xE0u) n = 3;
    else if ((ch & 0xF8u) == 0xF0u) n = 4;
    if (i + n > s.size()) return 1;
    return n;
}

std::vector<std::pair<std::size_t, std::size_t>> wrap_ranges(
    const Context& c, std::string_view v, f32 size, f32 width) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t start = 0, last_sp = std::string::npos, i = 0;
    while (i < v.size()) {
        if (v[i] == '\n') {
            out.push_back({start, i});
            ++i;
            start = i;
            last_sp = std::string::npos;
            continue;
        }
        const std::size_t end = i + utf8_len(v, i);
        if (i > start &&
            c.measure(v.substr(start, end - start), size) > width) {
            const std::size_t brk =
                (last_sp != std::string::npos && last_sp >= start) ? last_sp + 1 : i;
            out.push_back({start, brk});
            start = brk;
            last_sp = std::string::npos;
            continue;
        }
        if (v[i] == ' ') last_sp = i;
        i = end;
    }
    out.push_back({start, v.size()});
    return out;
}

}

int text_area_lines(const Context& c, std::string_view value, f32 size, f32 width) {
    if (value.empty() || width <= 0.0f) return 1;
    return static_cast<int>(wrap_ranges(c, value, size, width).size());
}

bool text_area(Context& c, const Rect& r, std::string_view id_label,
               std::string& value, std::string_view placeholder) {
    const Style& s = c.style();
    const Id id = c.id(id_label);
    const auto in = c.interact(id, r);
    c.register_focusable(id, r, id_label);

    const Rect inner = r.deflate(Insets::xy(s.padding.l, 6.0f));
    const f32 line_h = s.font_size + 6.0f;
    const auto lines = wrap_ranges(c, value, s.font_size, inner.w);

    const int fit = std::max(1, int(inner.h / line_h));
    i32& scroll_top =
        c.state_i32(c.id(std::string(id_label) + "##scroll"), 0);

    i32& caret = c.state_i32(id, static_cast<i32>(value.size()));
    const auto clamp_caret = [&] {
        caret = std::clamp(caret, 0, static_cast<i32>(value.size()));
        while (caret > 0 && caret < static_cast<i32>(value.size()) &&
               (static_cast<unsigned char>(value[std::size_t(caret)]) & 0xC0u) == 0x80u)
            --caret;
    };
    clamp_caret();

    i32& anchor = c.state_i32(c.id(std::string(id_label) + "##anchor"), -1);
    if (anchor > static_cast<i32>(value.size())) anchor = -1;
    const auto has_sel = [&] { return anchor >= 0 && anchor != caret; };
    const auto sel_lo = [&] { return std::min(anchor, caret); };
    const auto sel_hi = [&] { return std::max(anchor, caret); };

    const auto line_of = [&](i32 at) {
        for (std::size_t li = 0; li < lines.size(); ++li)
            if (std::size_t(at) <= lines[li].second) return li;
        return lines.size() - 1;
    };
    const auto caret_to = [&](std::size_t li, f32 x) {
        li = std::min(li, lines.size() - 1);
        const auto [b, e] = lines[li];
        caret = static_cast<i32>(
            b + c.caret_at(std::string_view(value).substr(b, e - b), s.font_size, x));
    };

    if (in.held || in.clicked) {
        const f32 rel_y = c.input().mouse.y - inner.y;
        const std::size_t li = std::size_t(
            std::clamp(int(rel_y / line_h) + scroll_top, 0,
                       int(lines.size()) - 1));
        caret_to(li, c.input().mouse.x - inner.x);
        if (c.input().pressed && !(c.input().shift && anchor >= 0))
            anchor = caret;
    }

    bool changed = false;
    if (in.focused) {
        const auto& i = c.input();
        if (i.replacement_id==id) { changed=value!=i.replacement_text; value=i.replacement_text; caret=i32(value.size()); anchor=-1; }
        const auto prev_b = [&](i32 at) {
            if (at <= 0) return 0;
            --at;
            while (at > 0 &&
                   (static_cast<unsigned char>(value[std::size_t(at)]) & 0xC0u) == 0x80u)
                --at;
            return at;
        };
        const auto next_b = [&](i32 at) {
            const i32 n = static_cast<i32>(value.size());
            if (at >= n) return n;
            at += static_cast<i32>(utf8_len(value, std::size_t(at)));
            return std::min(at, n);
        };
        const auto erase_sel = [&] {
            if (!has_sel()) return false;
            const i32 lo = sel_lo(), hi = sel_hi();
            value.erase(std::size_t(lo), std::size_t(hi - lo));
            caret = lo;
            anchor = -1;
            changed = true;
            return true;
        };
        if (!i.typed.empty()) {
            erase_sel();
            value.insert(std::size_t(caret), i.typed);
            caret += static_cast<i32>(i.typed.size());
            anchor = -1;
            changed = true;
        }
        if (i.key_enter && i.shift) {
            erase_sel();
            value.insert(std::size_t(caret), 1, '\n');
            ++caret;
            anchor = -1;
            changed = true;
        }
        if (i.key_backspace) {
            if (!erase_sel() && caret > 0) {
                const auto space_at = [&](i32 at) {
                    const unsigned char ch =
                        static_cast<unsigned char>(value[std::size_t(at)]);
                    return ch != '\n' && std::isspace(ch) != 0;
                };
                i32 from;
                if (i.cmd) {
                    from = caret;
                    while (from > 0 && value[std::size_t(from - 1)] != '\n') --from;
                    if (from == caret) --from;
                } else if (i.alt || i.ctrl) {
                    from = caret;
                    while (from > 0 && space_at(from - 1)) --from;
                    while (from > 0 && !space_at(from - 1) &&
                           value[std::size_t(from - 1)] != '\n')
                        from = prev_b(from);
                } else {
                    from = prev_b(caret);
                }
                from = std::max(from, 0);
                value.erase(std::size_t(from), std::size_t(caret - from));
                caret = from;
                changed = true;
            }
            anchor = -1;
        }
        if (i.key_delete) {
            if (!erase_sel() && caret < static_cast<i32>(value.size())) {
                value.erase(std::size_t(caret), std::size_t(next_b(caret) - caret));
                changed = true;
            }
            anchor = -1;
        }
        if (i.key_left || i.key_right || i.key_up || i.key_down ||
            i.key_home || i.key_end) {
            if (i.shift) { if (anchor < 0) anchor = caret; }
            else anchor = -1;
        }
        if (i.key_left)  caret = prev_b(caret);
        if (i.key_right) caret = next_b(caret);
        if ((i.key_up || i.key_down) && !changed) {
            const auto fresh = wrap_ranges(c, value, s.font_size, inner.w);
            std::size_t li = 0;
            for (; li < fresh.size(); ++li)
                if (std::size_t(caret) <= fresh[li].second) break;
            const f32 x = c.measure(
                std::string_view(value).substr(fresh[li].first,
                                               std::size_t(caret) - fresh[li].first),
                s.font_size);
            if (i.key_up && li > 0) {
                const auto [b, e] = fresh[li - 1];
                caret = static_cast<i32>(b + c.caret_at(
                    std::string_view(value).substr(b, e - b), s.font_size, x));
            }
            if (i.key_down && li + 1 < fresh.size()) {
                const auto [b, e] = fresh[li + 1];
                caret = static_cast<i32>(b + c.caret_at(
                    std::string_view(value).substr(b, e - b), s.font_size, x));
            }
        }
        if (i.key_home) caret = static_cast<i32>(lines[line_of(caret)].first);
        if (i.key_end)  caret = static_cast<i32>(lines[line_of(caret)].second);
    }
    clamp_caret();

    if (in.focused && c.copy_requested && has_sel() && c.clipboard) {
        c.copy_requested = false;
        c.clipboard(value.substr(std::size_t(sel_lo()),
                                 std::size_t(sel_hi() - sel_lo())));
    }

    c.draw().round_rect(r, s.radius_small,
                        surface_for(c, in, tok(c, T::ui_background)));
    c.draw().border(r, s.radius_small, in.focused ? s.border * 2.0f : s.border,
                    tok(c, in.focused ? T::ui_accent : T::ui_border));

    if (value.empty() && !placeholder.empty()) {
        scroll_top = 0;
        anchor = -1;
        Rect row{inner.x, inner.y, inner.w, line_h};
        c.draw().text(row, std::string(placeholder), s.font_size, tok(c, T::ui_text_dim));
    } else {
        const auto shown = wrap_ranges(c, value, s.font_size, inner.w);
        std::size_t cli = 0;
        for (; cli < shown.size(); ++cli)
            if (std::size_t(caret) <= shown[cli].second) break;
        cli = std::min(cli, shown.size() - 1);
        scroll_top = std::clamp(scroll_top, 0,
                                std::max(0, int(shown.size()) - fit));
        if (in.focused) {
            if (int(cli) < scroll_top) scroll_top = int(cli);
            if (int(cli) >= scroll_top + fit) scroll_top = int(cli) - fit + 1;
        }
        const std::size_t last =
            std::min(shown.size(), std::size_t(scroll_top + fit));
        if (has_sel()) {
            Color selc = tok(c, T::ui_accent);
            selc.a = 0.20f;
            for (std::size_t li = std::size_t(scroll_top); li < last; ++li) {
                const auto [b, e] = shown[li];
                const std::size_t a0 = std::max(std::size_t(sel_lo()), b);
                const std::size_t a1 = std::min(std::size_t(sel_hi()), e);
                if (a1 <= a0) continue;
                const f32 x0 = inner.x +
                    c.measure(std::string_view(value).substr(b, a0 - b), s.font_size);
                const f32 x1 = inner.x +
                    c.measure(std::string_view(value).substr(b, a1 - b), s.font_size);
                c.draw().rect({x0, inner.y + line_h * f32(int(li) - scroll_top),
                               std::max(2.0f, x1 - x0), line_h}, selc);
            }
        }
        for (std::size_t li = std::size_t(scroll_top); li < last; ++li) {
            const auto [b, e] = shown[li];
            Rect row{inner.x, inner.y + line_h * f32(int(li) - scroll_top),
                     inner.w, line_h};
            c.draw().text(row, value.substr(b, e - b), s.font_size, tok(c, T::ui_text));
        }
        if (in.focused && int(cli) >= scroll_top && int(cli) < scroll_top + fit) {
            const f32 cx = inner.x + c.measure(
                std::string_view(value).substr(shown[cli].first,
                                               std::size_t(caret) - shown[cli].first),
                s.font_size);
            const f32 cy = inner.y + line_h * f32(int(cli) - scroll_top);
            c.draw().line({cx, cy + 2.0f}, {cx, cy + line_h - 2.0f}, 1.5f,
                          tok(c, T::ui_accent));
        }
    }
    if (in.focused && value.empty()) {
        c.draw().line({inner.x, inner.y + 2.0f}, {inner.x, inner.y + line_h - 2.0f},
                      1.5f, tok(c, T::ui_accent));
    }
    c.announce(id, Context::Role::Field, id_label, value, r);
    return changed;
}

NumericResult numeric_field(Context& c, const Rect& r, std::string_view id_label,
                            std::string& value, std::string_view suffix) {
    NumericResult out;
    Rect field = r;
    if (!suffix.empty()) {
        const Rect suf = field.cut_right(34.0f);
        label(c, suf, suffix, TextAlign::Centre, true);
    }
    out.changed = text_field(c, field, id_label, value);
    if (c.is_focused(c.id(id_label)) && c.input().key_enter) out.committed = true;
    out.text = value;
    return out;
}

bool slider(Context& c, const Rect& r, std::string_view id_label, f32& value,
            f32 lo, f32 hi) {
    const Style& s = c.style();
    const Id id = c.id(id_label);
    const auto in = c.interact(id, r);
    c.register_focusable(id, r, id_label);
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.3g", double(value));
        c.announce(id, Context::Role::Slider, id_label, buf, r);
    }

    bool changed = false;
    if (in.held && r.w > 0.0f) {
        const f32 t = std::clamp((c.input().mouse.x - r.x) / r.w, 0.0f, 1.0f);
        const f32 nv = lo + (hi - lo) * t;
        if (nv != value) { value = nv; changed = true; }
    }
    if (in.focused && hi > lo) {
        const auto& keys = c.input();
        const f32 step = (hi - lo) * .01f;
        f32 next = value;
        if (keys.key_left || keys.key_down) next -= step;
        if (keys.key_right || keys.key_up) next += step;
        if (keys.key_home) next = lo;
        if (keys.key_end) next = hi;
        next = std::clamp(next, lo, hi);
        if (next != value) { value = next; changed = true; }
    }

    const f32 track_h = 4.0f;
    const Rect track{r.x, r.centre().y - track_h * 0.5f, r.w, track_h};
    c.draw().round_rect(track, track_h * 0.5f, tok(c, T::ui_background));

    const f32 t = (hi > lo) ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const Rect filled{track.x, track.y, track.w * t, track.h};
    c.draw().round_rect(filled, track_h * 0.5f, tok(c, T::ui_accent));

    const f32 knob = 12.0f;
    const Rect kr{r.x + r.w * t - knob * 0.5f, r.centre().y - knob * 0.5f, knob, knob};
    c.draw().round_rect(kr, knob * 0.5f,
                        surface_for(c, in, tok(c, T::ui_surface_raised)));
    c.draw().border(kr, knob * 0.5f, s.border, tok(c, T::ui_border));
    return changed;
}

i32 dropdown(Context& c, const Rect& r, std::string_view id_label,
             const std::vector<std::string>& items, i32& selected) {
    Dropdown d(c);
    c.push_id(id_label);
    const i32 picked = d.run(r, id_label, items, selected);
    c.pop_id();
    return picked;
}

void separator(Context& c, const Rect& r) {
    c.draw().line({r.x, r.centre().y}, {r.right(), r.centre().y}, 1.0f,
                  tok(c, T::ui_border));
}

void color_swatch(Context& c, const Rect& r, Color col, bool selected) {
    c.draw().round_rect(r, 3.0f, col);
    c.draw().border(r, 3.0f, selected ? 2.0f : 1.0f,
                    tok(c, selected ? T::ui_accent : T::ui_border));
}

void command_bar(Context& c, const Rect& r, CommandBarState& st) {
    const Style& s = c.style();
    c.draw().round_rect(r, s.radius_small, tok(c, T::ui_background));
    c.draw().border(r, s.radius_small, s.border, tok(c, T::ui_border));

    Rect row = r.deflate(s.padding);

    if (!st.prompt.empty()) {
        const f32 w = static_cast<f32>(st.prompt.size()) * s.font_size * 0.55f + 8.0f;
        const Rect pr = row.cut_left(w);
        c.draw().text(pr, st.prompt + ":", s.font_size, tok(c, T::ui_text_dim));
    }

    const Id id = c.id("command-input");
    c.interact(id,row);
    c.register_focusable(id,row,"Command");
    c.announce(id,Context::Role::Field,"Command",st.input,row);
    if (c.focused() == 0) c.set_focus(id);
    if (!c.is_focused(id)) {
        c.draw().text(row, st.input, s.font_size, tok(c, T::ui_text_dim));
        st.submitted = false;
        st.cancelled = false;
        st.chosen_keyword = -1;
        st.chosen_completion = -1;
        return;
    }
    const auto& in = c.input();
    if (in.replacement_id==id) st.input=in.replacement_text;
    if (!in.typed.empty()) st.input += in.typed;
    if (in.key_backspace && !st.input.empty()) st.input.pop_back();
    st.submitted = in.key_enter;
    st.cancelled = in.key_escape;

    c.draw().text(row, st.input, s.font_size, tok(c, T::ui_text));
    const f32 cx = row.x + static_cast<f32>(st.input.size()) * s.font_size * 0.55f + 2.0f;
    c.draw().line({cx, row.y + 2.0f}, {cx, row.bottom() - 2.0f}, 1.5f,
                  tok(c, T::ui_accent));

    st.chosen_keyword = -1;
    if (!st.keywords.empty()) {
        Rect chips{r.x, r.y - s.control_height - 4.0f, r.w, s.control_height};
        for (std::size_t i = 0; i < st.keywords.size(); ++i) {
            const f32 w = c.measure(st.keywords[i], s.font_size) + 28.0f;
            const Rect chip = chips.cut_left(w);
            c.push_id("kw");
            c.push_id(st.keywords[i]);
            if (button(c, chip, st.keywords[i])) st.chosen_keyword = static_cast<i32>(i);
            c.pop_id();
            c.pop_id();
            chips.cut_left(4.0f);
        }
    }

    st.chosen_completion = -1;
    if (!st.completions.empty()) {
        const f32 h = s.row_height_dense * static_cast<f32>(st.completions.size());
        const Rect list{r.x, r.y - h - 6.0f, std::min(r.w, 420.0f), h};
        c.begin_overlay(list);
        c.draw().shadow(list, s.radius_small, s.shadow_spread, tok(c, T::ui_shadow));
        c.draw().round_rect(list, s.radius_small, tok(c, T::ui_surface_raised));
        c.draw().border(list, s.radius_small, s.border, tok(c, T::ui_border));

        const i32 n = static_cast<i32>(st.completions.size());
        if (st.highlight < 0 || st.highlight >= n) st.highlight = 0;
        if (in.key_down) st.highlight = (st.highlight + 1) % n;
        if (in.key_up) st.highlight = (st.highlight + n - 1) % n;
        Rect cursor = list;
        for (std::size_t i = 0; i < st.completions.size(); ++i) {
            const Rect row2 = cursor.cut_top(s.row_height_dense);
            c.push_id("cmp");
            c.push_id(st.completions[i]);
            const auto ri = c.interact(c.id("row"), row2);
            c.pop_id();
            c.pop_id();
            const bool lit = static_cast<i32>(i) == st.highlight;
            if (lit) c.draw().rect(row2, tok(c, T::ui_active));
            else if (ri.hovered) c.draw().rect(row2, tok(c, T::ui_hover));
            c.draw().text(row2.deflate(s.padding), st.completions[i], s.font_size,
                          tok(c, lit ? T::ui_accent : T::ui_text));
            if (ri.clicked) st.chosen_completion = static_cast<i32>(i);
        }
        if (st.submitted && st.chosen_completion < 0) {
            st.chosen_completion = st.highlight;
            st.submitted = false;
        }
        c.end_overlay();
    } else {
        st.highlight = 0;
    }
}

bool status_toggle(Context& c, Rect& cursor, std::string_view lbl, bool& value) {
    const f32 w = static_cast<f32>(lbl.size()) * c.style().font_size * 0.62f + 20.0f;
    const Rect r = cursor.cut_left(w);
    cursor.cut_left(6.0f);
    return toggle(c, r, lbl, value);
}

void status_text(Context& c, Rect& cursor, std::string_view text, f32 width) {
    const Rect r = cursor.cut_left(width);
    cursor.cut_left(2.0f);
    label(c, r, text, TextAlign::Left, true);
}

bool layer_row(Context& c, const Rect& r, LayerRow& row) {
    const Style& s = c.style();
    bool changed = false;

    c.push_id(row.name);
    if (row.current) c.draw().rect(r, tok(c, T::ui_active));

    Rect cursor = r;
    const f32 icon_w = 24.0f;

    if (toggle(c, cursor.cut_left(icon_w), "V", row.on)) changed = true;
    if (toggle(c, cursor.cut_left(icon_w), "F", row.frozen)) changed = true;
    if (toggle(c, cursor.cut_left(icon_w), "L", row.locked)) changed = true;

    cursor.cut_left(s.spacing);
    const Rect sw = cursor.cut_left(24.0f);
    const f32 chip = 14.0f;
    color_swatch(c, {sw.x + (sw.w - chip) * 0.5f, sw.y + (sw.h - chip) * 0.5f, chip, chip}, row.color);

    cursor.cut_left(s.spacing);
    label(c, cursor, row.name, TextAlign::Left, !row.on || row.frozen);

    c.pop_id();
    return changed;
}

bool tool_button(Context& c, const Rect& r, u32 icon, std::string_view tooltip,
                 bool active) {
    const Style& s = c.style();
    c.push_id(tooltip);
    const Id id = c.id("tool");
    const auto in = c.interact(id, r);

    Color base = active ? tok(c, T::ui_accent) : tok(c, T::ui_surface);
    base = surface_for(c, in, base);
    c.draw().round_rect(r, s.radius_small, base);
    c.draw().icon(r, icon, tok(c, active ? T::ui_accent_text : T::ui_text));

    if (in.hovered && !tooltip.empty()) {
        const f32 w = c.measure(tooltip, s.font_size_small) + 24.0f;
        const Rect tip{r.right() + 8.0f, r.centre().y - 14.0f, w, 28.0f};
        c.draw().shadow(tip, 6.0f, 8.0f, tok(c, T::ui_shadow));
        c.draw().round_rect(tip, 6.0f, tok(c, T::ui_surface_raised));
        c.draw().border(tip, 6.0f, 1.0f, tok(c, T::ui_border));
        c.draw().text(tip, std::string(tooltip), s.font_size_small, tok(c, T::ui_text),
                      TextAlign::Centre);
    }
    c.pop_id();
    return in.clicked;
}

}


namespace gui {

int quick_bar(Context& c, Vec2 anchor, const Rect& bounds,
              const std::vector<QuickAction>& actions) {
    if (actions.empty()) return -1;
    const Style& st = c.style();
    const f32 chip_h = st.grid(11);
    const f32 pad = st.grid(2);
    const f32 gap = st.grid(2);
    const f32 side = st.grid(4);
    const f32 icon_w = st.grid(5);

    std::vector<f32> widths;
    f32 total = pad;
    for (const auto& a : actions) {
        f32 w = side + c.measure(a.label, st.font_size) + side;
        if (a.icon) w += icon_w + st.grid(2);
        widths.push_back(st.grid(w));
        total += widths.back() + gap;
    }
    total += pad - gap;
    const f32 bar_h = chip_h + pad * 2.0f;

    Rect bar{anchor.x - total * 0.5f, anchor.y - bar_h, total, bar_h};
    bar.x = std::clamp(bar.x, bounds.x + pad, std::max(bounds.x + pad, bounds.right() - bar.w - pad));
    bar.y = std::clamp(bar.y, bounds.y + pad, std::max(bounds.y + pad, bounds.bottom() - bar.h - pad));

    gui::Color sh = c.theme().get(gtheme::token::ui_shadow);
    sh.a = std::max(sh.a, 0.28f);
    c.draw().shadow(bar, bar_h * 0.5f, st.grid(6), sh);
    c.draw().round_rect(bar, bar_h * 0.5f, c.theme().get(gtheme::token::ui_surface_raised));
    c.draw().border(bar, bar_h * 0.5f, 1.0f, c.theme().get(gtheme::token::ui_border));

    int hit = -1;
    f32 x = bar.x + pad;
    c.push_id("quick-bar");
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const Rect chip{x, bar.y + pad, widths[i], chip_h};
        c.push_id(std::to_string(i));
        const auto in = c.interact(c.id("chip"), chip);
        c.draw().round_rect(chip, chip_h * 0.5f,
                            surface_for(c, in, tok(c, T::ui_surface)));
        c.draw().border(chip, chip_h * 0.5f, 1.0f, tok(c, T::ui_border));
        f32 tx = chip.x + side;
        if (actions[i].icon) {
            const Rect ib{tx, chip.y, icon_w, chip_h};
            std::string glyph;
            const u32 cp = actions[i].icon;
            glyph.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            glyph.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            glyph.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            c.draw().text(ib, glyph, icon_w, tok(c, T::ui_text), TextAlign::Centre);
            tx = ib.right() + st.grid(2);
        }
        const Rect lb{tx, chip.y, chip.right() - side - tx, chip_h};
        c.draw().text(lb, actions[i].label, st.font_size, tok(c, T::ui_text),
                      TextAlign::Left);
        if (in.clicked) hit = actions[i].id;
        c.pop_id();
        x += widths[i] + gap;
    }
    c.pop_id();
    return hit;
}

}
