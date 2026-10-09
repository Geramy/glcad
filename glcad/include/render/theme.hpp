#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "math/config.hpp"

namespace gtheme {

using gcm::f32;
using gcm::u32;
using gcm::u8;

struct Color {
    f32 r{1}, g{1}, b{1}, a{1};

    static constexpr Color rgb(f32 r, f32 g, f32 b) noexcept { return {r, g, b, 1.0f}; }
    static constexpr Color rgba(f32 r, f32 g, f32 b, f32 a) noexcept { return {r, g, b, a}; }

    static Color hex(u32 v) noexcept;            // 0xRRGGBB or 0xAARRGGBB
    static bool  parse(std::string_view s, Color& out) noexcept;  // "#RRGGBB[AA]"
    std::string  to_hex() const;

    Color with_alpha(f32 na) const noexcept { return {r, g, b, na}; }
    Color mix(Color o, f32 t) const noexcept {
        return {r + (o.r - r) * t, g + (o.g - g) * t,
                b + (o.b - b) * t, a + (o.a - a) * t};
    }
    f32 luminance() const noexcept { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
};

namespace token {
inline constexpr const char* canvas_background   = "canvas.background";
inline constexpr const char* canvas_grid_minor   = "canvas.grid.minor";
inline constexpr const char* canvas_grid_major   = "canvas.grid.major";
inline constexpr const char* canvas_axis_x       = "canvas.axis.x";
inline constexpr const char* canvas_axis_y       = "canvas.axis.y";
inline constexpr const char* canvas_axis_z       = "canvas.axis.z";

inline constexpr const char* geometry            = "geometry";
inline constexpr const char* geometry_preview    = "geometry.preview";
inline constexpr const char* geometry_construction = "geometry.construction";
inline constexpr const char* geometry_endpoint   = "geometry.endpoint";
inline constexpr const char* geometry_selected   = "geometry.selected";
inline constexpr const char* geometry_highlight  = "geometry.highlight";
inline constexpr const char* geometry_locked     = "geometry.locked";
inline constexpr const char* geometry_reference  = "geometry.reference";

inline constexpr const char* snap_marker         = "snap.marker";
inline constexpr const char* snap_track          = "snap.track";
inline constexpr const char* snap_polar          = "snap.polar";

inline constexpr const char* select_window       = "select.window";
inline constexpr const char* select_crossing     = "select.crossing";

inline constexpr const char* constraint_ok       = "constraint.satisfied";
inline constexpr const char* constraint_conflict = "constraint.conflict";
inline constexpr const char* status_error        = "status.error";
inline constexpr const char* status_warning      = "status.warning";
inline constexpr const char* status_ok           = "status.ok";

inline constexpr const char* ui_background       = "ui.background";
inline constexpr const char* ui_surface          = "ui.surface";
inline constexpr const char* ui_surface_raised   = "ui.surface.raised";
inline constexpr const char* ui_border           = "ui.border";
inline constexpr const char* ui_text             = "ui.text";
inline constexpr const char* ui_text_dim         = "ui.text.dim";
inline constexpr const char* ui_accent           = "ui.accent";
inline constexpr const char* ui_accent_text      = "ui.accent.text";
inline constexpr const char* ui_hover            = "ui.hover";
inline constexpr const char* ui_active           = "ui.active";
inline constexpr const char* ui_shadow           = "ui.shadow";
}

const std::vector<std::string>& all_tokens();

class Theme {
public:
    std::string name{"Untitled"};
    bool        dark{true};

    Color get(std::string_view tok) const;
    Color get_or(std::string_view tok, Color fallback) const;
    void  set(std::string_view tok, Color c);
    bool  has(std::string_view tok) const;

    std::vector<std::string> missing_tokens() const;
    bool complete() const { return missing_tokens().empty(); }

    bool        load(std::string_view text, std::string* error = nullptr);
    std::string save() const;

    static Theme dark_default();
    static Theme light_default();
    static Theme high_contrast();

    std::size_t size() const noexcept { return colors_.size(); }

    struct RolePalette {
        Color geometry, preview, construction, endpoint, snap, selected,
              highlight, error, annotation;
    };
    RolePalette role_palette() const;

private:
    std::unordered_map<std::string, Color> colors_;
};

enum class Appearance : u8 { Unknown, Light, Dark };

const char* appearance_name(Appearance a) noexcept;

Appearance system_appearance() noexcept;

class ThemeSet {
public:
    ThemeSet();
    void         add(Theme t);
    bool         activate(std::string_view name);
    Theme&       active() noexcept { return themes_[active_]; }
    const Theme& active() const noexcept { return themes_[active_]; }
    std::vector<std::string> names() const;
    std::size_t  size() const noexcept { return themes_.size(); }
    const Theme* find(std::string_view name) const;
    Theme*       find(std::string_view name);

    void set_follow_system(bool on) noexcept { follow_ = on; }
    bool follow_system() const noexcept { return follow_; }
    void set_system_pair(std::string_view light, std::string_view dark);

    bool sync_with_system();
    bool apply_appearance(Appearance a);

private:
    std::vector<Theme> themes_;
    std::size_t        active_{0};
    bool               follow_{false};
    std::string        light_name_{"GLCAD Light"};
    std::string        dark_name_{"GLCAD Dark"};
};

class ThemeEditor {
public:
    explicit ThemeEditor(ThemeSet& set);

    struct Group {
        std::string              name;
        std::vector<std::string> tokens;
    };
    std::vector<Group> groups() const;

    Color value(std::string_view token) const;
    void  set(std::string_view token, Color c);

    bool dirty() const noexcept { return dirty_; }
    void commit();
    void revert();

    bool duplicate_active(std::string_view new_name);

private:
    ThemeSet& set_;
    Theme     saved_;
    bool      dirty_{false};
};

}
