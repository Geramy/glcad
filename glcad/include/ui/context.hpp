#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/draw.hpp"
#include "ui/geometry.hpp"

namespace gui {

using Id = gcm::u64;

Id hash_id(std::string_view s, Id seed = 1469598103934665603ull);

struct InputState {
    Vec2 mouse{};
    Vec2 mouse_delta{};
    bool down{false};
    bool pressed{false};
    bool released{false};
    bool right_pressed{false};
    f32  wheel{0.0f};
    bool shift{false}, ctrl{false}, alt{false}, cmd{false};

    f64 time{0.0};

    std::string typed;
    Id replacement_id{0};
    std::string replacement_text;
    bool key_enter{false}, key_escape{false}, key_tab{false};
    bool key_backspace{false}, key_delete{false};
    bool key_left{false}, key_right{false};
    bool key_up{false}, key_down{false};
    bool key_home{false}, key_end{false};
};

struct Style {
    f32 radius{12.0f};
    f32 radius_small{8.0f};
    f32 radius_full{999.0f};
    f32 border{1.0f};

    f32 unit{4.0f};

    f32 row_height{48.0f};
    f32 row_height_dense{40.0f};
    f32 control_height{40.0f};
    f32 icon_button{40.0f};
    f32 icon_size{24.0f};
    f32 rail_width{80.0f};
    f32 app_bar_height{64.0f};

    f32 font_size{14.0f};
    f32 font_size_small{12.0f};
    f32 font_size_title{16.0f};

    Insets padding{Insets::xy(16.0f, 8.0f)};
    f32 spacing{8.0f};
    f32 panel_gap{8.0f};
    f32 shadow_spread{16.0f};

    f32 min_touch_target{48.0f};

    f32 hover_opacity{0.22f};
    f32 focus_opacity{0.10f};
    f32 press_opacity{0.12f};

    f32 grid(f32 v) const noexcept {
        return unit * static_cast<f32>(static_cast<int>(v / unit + 0.5f));
    }
};

class Context {
public:
    Context();

    void begin(const Rect& viewport, const InputState& in, const gtheme::Theme& theme);
    void end();

    std::function<void(const std::string&)> clipboard;
    bool copy_requested{false};

    DrawList&              draw() noexcept { return overlay_depth_ > 0 ? overlay_layers_[std::size_t(std::min(overlay_depth_, kOverlayLayers) - 1)] : draw_; }
    const DrawList&        draw() const noexcept { return overlay_depth_ > 0 ? overlay_layers_[std::size_t(std::min(overlay_depth_, kOverlayLayers) - 1)] : draw_; }

    void begin_overlay(const Rect& r);
    void end_overlay();
    bool in_overlay() const noexcept { return overlay_depth_ > 0; }
    // Occlusion must match draw order, or clicks fall through popups.
    bool covered_by_overlay(Vec2 p, int above_layer = 0) const noexcept;
    std::size_t              overlay_start() const noexcept { return overlay_start_; }
    const std::vector<Rect>& overlay_rects() const noexcept { return overlay_rects_; }
    const std::vector<int>&         overlay_rect_layers() const noexcept { return overlay_rect_layers_; }
    const std::vector<std::size_t>& overlay_layer_starts() const noexcept { return overlay_layer_starts_; }
    const gtheme::Theme&   theme() const noexcept { return *theme_; }
    const InputState&      input() const noexcept { return input_; }
    Style&                 style() noexcept { return style_; }
    const Style&           style() const noexcept { return style_; }
    const Rect&            viewport() const noexcept { return viewport_; }

    void push_id(std::string_view s);
    void pop_id();
    Id   id(std::string_view label) const;

    struct Interaction {
        bool hovered{false};
        bool held{false};
        bool clicked{false};
        bool right_clicked{false};
        bool focused{false};
    };
    Interaction interact(Id id, const Rect& r, bool enabled = true);

    bool is_hovered(Id id) const noexcept { return hot_ == id; }
    bool is_active(Id id) const noexcept { return active_ == id; }
    bool is_focused(Id id) const noexcept { return focus_ == id; }
    void set_focus(Id id) noexcept { focus_ = id; }
    void clear_focus() noexcept { focus_ = 0; }
    Id   focused() const noexcept { return focus_; }

    bool wants_mouse() const noexcept { return hot_ != 0 || active_ != 0; }
    bool wants_keys() const noexcept { return focus_ != 0; }
    bool focus_is_field() const noexcept {
        for (const auto& a : accessible_)
            if (a.id == focus_) return a.role == Role::Field;
        return false;
    }

    void register_focusable(Id id, const Rect& r, std::string_view label);
    bool focus_next(bool backwards = false);
    bool activated(Id id) const noexcept;
    std::size_t focusable_count() const noexcept { return focusables_.size(); }

    enum class Role : u8 {
        None, Button, Toggle, Checkbox, Field, Label, Panel, Tab, Row,
        TreeItem, Slider, Canvas,
    };
    struct Accessible {
        Id          id{0};
        Role        role{Role::None};
        std::string label;
        std::string value;
        Rect        bounds{};
        int         depth{0};
        bool        focused{false};
        bool        selected{false};
        bool        disabled{false};
    };
    void announce(Id id, Role role, std::string_view label,
                  std::string_view value = "", const Rect& r = {},
                  bool selected = false, bool disabled = false);
    const std::vector<Accessible>& accessibility_tree() const noexcept {
        return accessible_;
    }
    std::string focused_description() const;
    static const char* role_name(Role r) noexcept;

    f32&  state_f32(Id id, f32 initial = 0.0f);
    i32&  state_i32(Id id, i32 initial = 0);
    bool& state_bool(Id id, bool initial = false);
    std::string& state_text(Id id, std::string_view initial = "");

    using Measure = std::function<f32(std::string_view, f32 size)>;
    void set_measure(Measure m) { measure_ = std::move(m); }

    void set_icons_available(bool v) noexcept { icons_available_ = v; }
    bool icons_available() const noexcept { return icons_available_; }
    bool has_real_metrics() const noexcept { return static_cast<bool>(measure_); }
    f32  measure(std::string_view text, f32 size) const;

    std::size_t fit(std::string_view text, f32 size, f32 width) const;

    std::size_t caret_at(std::string_view text, f32 size, f32 x) const;

    void push_clip(const Rect& r);
    void pop_clip();
    Rect clip() const noexcept { return clips_.empty() ? viewport_ : clips_.back(); }

    u32 frame() const noexcept { return frame_; }

private:
    DrawList             draw_;
    const gtheme::Theme* theme_{nullptr};
    InputState           input_{};
    Style                style_{};
    Rect                 viewport_{};

    std::vector<Id>   id_stack_;
    std::vector<Rect> clips_;

    struct Focusable {
        Id          id{0};
        Rect        rect{};
        std::string label;
    };
    std::vector<Focusable>  focusables_;
    std::vector<Accessible> accessible_;
    Measure                 measure_;
    bool                    icons_available_{false};
    bool                    activate_focus_{false};

    Id  hot_{0}, active_{0}, focus_{0};
    static constexpr int kOverlayLayers = 4;
    DrawList          overlay_layers_[kOverlayLayers];
    int               overlay_depth_{0};
    std::vector<Rect> overlay_rects_, prev_overlay_rects_;
    std::vector<int>  overlay_rect_layers_, prev_overlay_rect_layers_;
    std::size_t       overlay_start_{0};
    std::vector<std::size_t> overlay_layer_starts_;
    Id  next_hot_{0};
    u32 frame_{0};

    std::unordered_map<Id, f32>         f32_;
    std::unordered_map<Id, i32>         i32_;
    std::unordered_map<Id, bool>        bool_;
    std::unordered_map<Id, std::string> text_;
};

}
