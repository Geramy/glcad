#include "ui/context.hpp"

#include <cmath>

namespace gui {

Id hash_id(std::string_view s, Id seed) {
    Id h = seed;
    for (char c : s) {
        h ^= static_cast<Id>(static_cast<unsigned char>(c));
        h *= 1099511628211ull;
    }
    return h;
}

Context::Context() { id_stack_.push_back(1469598103934665603ull); }

void Context::begin(const Rect& viewport, const InputState& in,
                    const gtheme::Theme& theme) {
    viewport_ = viewport;
    input_ = in;
    theme_ = &theme;
    copy_requested = false;

    draw_.clear();
    for (auto& layer : overlay_layers_) layer.clear();
    overlay_depth_ = 0;
    prev_overlay_rects_ = std::move(overlay_rects_);
    prev_overlay_rect_layers_ = std::move(overlay_rect_layers_);
    overlay_rects_.clear();
    overlay_rect_layers_.clear();
    clips_.clear();
    id_stack_.resize(1);

    hot_ = next_hot_;
    next_hot_ = 0;

    if (in.pressed && hot_ == 0) focus_ = 0;
    if (in.key_escape) focus_ = 0;

    if (in.key_tab) focus_next(in.shift);
    focusables_.clear();
    accessible_.clear();
    activate_focus_ = in.key_enter || in.typed == " ";

    ++frame_;
}

void Context::end() {
    hot_ = next_hot_;
    if (!input_.down) active_ = 0;
    overlay_depth_ = 0;
    while (draw_.clip_depth() > 0) draw_.pop_clip();
    overlay_start_ = draw_.size();
    overlay_layer_starts_.clear();
    for (auto& layer : overlay_layers_) {
        overlay_layer_starts_.push_back(draw_.size());
        draw_.append(layer);
        layer.clear();
    }
}

void Context::begin_overlay(const Rect& r) {
    overlay_rects_.push_back(r);
    overlay_rect_layers_.push_back(std::min(overlay_depth_ + 1, kOverlayLayers));
    ++overlay_depth_;
    clips_.push_back(viewport_);
    draw().push_clip(viewport_);
}

void Context::end_overlay() {
    if (overlay_depth_ <= 0) return;
    draw().pop_clip();
    clips_.pop_back();
    --overlay_depth_;
}

bool Context::covered_by_overlay(Vec2 p, int above_layer) const noexcept {
    for (std::size_t i = 0; i < overlay_rects_.size(); ++i)
        if (overlay_rect_layers_[i] > above_layer && overlay_rects_[i].contains(p))
            return true;
    for (std::size_t i = 0; i < prev_overlay_rects_.size(); ++i)
        if (i < prev_overlay_rect_layers_.size() &&
            prev_overlay_rect_layers_[i] > above_layer && prev_overlay_rects_[i].contains(p))
            return true;
    return false;
}

void Context::push_id(std::string_view s) {
    id_stack_.push_back(hash_id(s, id_stack_.back()));
}
void Context::pop_id() {
    if (id_stack_.size() > 1) id_stack_.pop_back();
}
Id Context::id(std::string_view label) const {
    return hash_id(label, id_stack_.back());
}

Context::Interaction Context::interact(Id id, const Rect& r, bool enabled) {
    Interaction out;
    if (!enabled || r.empty()) return out;

    const Rect visible = r.intersect(clip());
    if (visible.empty()) return out;

    const bool over = visible.contains(input_.mouse) &&
                      !covered_by_overlay(input_.mouse, overlay_depth_);
    if (over && active_ == 0) next_hot_ = id;

    out.hovered = (hot_ == id) || (over && active_ == id);

    if (over && input_.pressed) {
        active_ = id;
        focus_ = id;
    }
    if (over && input_.right_pressed) out.right_clicked = true;

    out.held = (active_ == id);
    if (active_ == id && input_.released) {
        if (over) out.clicked = true;
        active_ = 0;
    }
    out.focused = (focus_ == id);
    return out;
}

f32& Context::state_f32(Id id, f32 initial) {
    const auto it = f32_.find(id);
    if (it != f32_.end()) return it->second;
    return f32_.emplace(id, initial).first->second;
}
i32& Context::state_i32(Id id, i32 initial) {
    const auto it = i32_.find(id);
    if (it != i32_.end()) return it->second;
    return i32_.emplace(id, initial).first->second;
}
bool& Context::state_bool(Id id, bool initial) {
    const auto it = bool_.find(id);
    if (it != bool_.end()) return it->second;
    return bool_.emplace(id, initial).first->second;
}
std::string& Context::state_text(Id id, std::string_view initial) {
    const auto it = text_.find(id);
    if (it != text_.end()) return it->second;
    return text_.emplace(id, std::string(initial)).first->second;
}


namespace {

std::size_t utf8_step(std::string_view s, std::size_t i) noexcept {
    if (i >= s.size()) return 0;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t n = 1;
    if ((c & 0xE0u) == 0xC0u) n = 2;
    else if ((c & 0xF0u) == 0xE0u) n = 3;
    else if ((c & 0xF8u) == 0xF0u) n = 4;
    if (i + n > s.size()) return 1;
    for (std::size_t k = 1; k < n; ++k)
        if ((static_cast<unsigned char>(s[i + k]) & 0xC0u) != 0x80u) return 1;
    return n;
}

}

f32 Context::measure(std::string_view text, f32 size) const {
    if (measure_) return measure_(text, size);

    std::size_t glyphs = 0;
    for (std::size_t i = 0; i < text.size();) {
        i += utf8_step(text, i);
        ++glyphs;
    }
    return static_cast<f32>(glyphs) * size * 0.5f;
}

std::size_t Context::fit(std::string_view text, f32 size, f32 width) const {
    if (width <= 0.0f) return 0;
    std::size_t last = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t n = utf8_step(text, i);
        if (n == 0) break;
        const std::size_t end = i + n;
        if (measure(text.substr(0, end), size) > width) return last;
        last = end;
        i = end;
    }
    return text.size();
}

std::size_t Context::caret_at(std::string_view text, f32 size, f32 x) const {
    if (x <= 0.0f) return 0;
    std::size_t best = 0;
    f32 best_d = std::abs(x);
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t n = utf8_step(text, i);
        if (n == 0) break;
        i += n;
        const f32 d = std::abs(measure(text.substr(0, i), size) - x);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

void Context::push_clip(const Rect& r) {
    const Rect merged = r.intersect(clip());
    clips_.push_back(merged);
    draw().push_clip(merged);
}
void Context::pop_clip() {
    if (clips_.empty()) return;
    clips_.pop_back();
    draw().pop_clip();
}


void Context::register_focusable(Id id, const Rect& r, std::string_view label) {
    const Rect visible = r.intersect(clip()).intersect(viewport_);
    if (visible.empty()) {
        if (focus_ == id) focus_ = 0;
        return;
    }
    focusables_.push_back({id, visible, std::string(label)});
}

bool Context::focus_next(bool backwards) {
    if (focusables_.empty()) return false;

    std::size_t current = focusables_.size();
    for (std::size_t i = 0; i < focusables_.size(); ++i)
        if (focusables_[i].id == focus_) current = i;

    if (current >= focusables_.size()) {
        focus_ = backwards ? focusables_.back().id : focusables_.front().id;
        return true;
    }

    const std::size_t n = focusables_.size();
    const std::size_t next = backwards ? (current + n - 1) % n : (current + 1) % n;
    focus_ = focusables_[next].id;
    return true;
}

bool Context::activated(Id id) const noexcept {
    return activate_focus_ && focus_ == id;
}

const char* Context::role_name(Role r) noexcept {
    switch (r) {
        case Role::None:     return "";
        case Role::Button:   return "button";
        case Role::Toggle:   return "toggle";
        case Role::Checkbox: return "checkbox";
        case Role::Field:    return "text field";
        case Role::Label:    return "label";
        case Role::Panel:    return "panel";
        case Role::Tab:      return "tab";
        case Role::Row:      return "row";
        case Role::TreeItem: return "tree item";
        case Role::Slider:   return "slider";
        case Role::Canvas:   return "drawing area";
    }
    return "";
}

void Context::announce(Id id, Role role, std::string_view label,
                       std::string_view value, const Rect& r, bool selected,
                       bool disabled) {
    const Rect visible = r.intersect(clip()).intersect(viewport_);
    if (visible.empty()) {
        if (focus_ == id) focus_ = 0;
        return;
    }
    Accessible a;
    a.id = id;
    a.role = role;
    a.label = std::string(label);
    a.value = std::string(value);
    a.bounds = visible;
    a.depth = static_cast<int>(clips_.size());
    a.focused = focus_ == id;
    a.selected = selected;
    a.disabled = disabled;
    accessible_.push_back(std::move(a));
}

std::string Context::focused_description() const {
    for (const Accessible& a : accessible_) {
        if (a.id != focus_) continue;
        std::string out = a.label;
        const char* role = role_name(a.role);
        if (role[0]) {
            if (!out.empty()) out += ", ";
            out += role;
        }
        if (!a.value.empty()) {
            out += ", ";
            out += a.value;
        }
        if (a.selected) out += ", selected";
        if (a.disabled) out += ", unavailable";
        return out;
    }
    return {};
}

}
