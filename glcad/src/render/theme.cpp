#include "render/theme.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace gtheme {

Color Color::hex(u32 v) noexcept {
    // 0xRRGGBB (opaque) or 0xAARRGGBB.
    const bool has_alpha = (v & 0xFF000000u) != 0;
    const f32 a = has_alpha ? static_cast<f32>((v >> 24) & 0xFF) / 255.0f : 1.0f;
    return {static_cast<f32>((v >> 16) & 0xFF) / 255.0f,
            static_cast<f32>((v >> 8) & 0xFF) / 255.0f,
            static_cast<f32>(v & 0xFF) / 255.0f, a};
}

bool Color::parse(std::string_view s, Color& out) noexcept {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
    if (!s.empty() && s.front() == '#') s.remove_prefix(1);
    if (s.size() != 6 && s.size() != 8) return false;

    u32 v = 0;
    for (char c : s) {
        u32 d;
        if (c >= '0' && c <= '9') d = static_cast<u32>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<u32>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<u32>(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    if (s.size() == 6) {
        out = Color::hex(v);
        out.a = 1.0f;
    } else {
        // #RRGGBBAA
        out = {static_cast<f32>((v >> 24) & 0xFF) / 255.0f,
               static_cast<f32>((v >> 16) & 0xFF) / 255.0f,
               static_cast<f32>((v >> 8) & 0xFF) / 255.0f,
               static_cast<f32>(v & 0xFF) / 255.0f};
    }
    return true;
}

std::string Color::to_hex() const {
    auto q = [](f32 v) {
        int i = static_cast<int>(v * 255.0f + 0.5f);
        return i < 0 ? 0 : (i > 255 ? 255 : i);
    };
    char buf[16];
    if (a >= 0.999f) std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", q(r), q(g), q(b));
    else std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", q(r), q(g), q(b), q(a));
    return buf;
}

const std::vector<std::string>& all_tokens() {
    static const std::vector<std::string> t = {
        token::canvas_background, token::canvas_grid_minor, token::canvas_grid_major,
        token::canvas_axis_x, token::canvas_axis_y, token::canvas_axis_z,
        token::geometry, token::geometry_preview, token::geometry_construction,
        token::geometry_endpoint, token::geometry_selected, token::geometry_highlight,
        token::geometry_locked, token::geometry_reference,
        token::snap_marker, token::snap_track, token::snap_polar,
        token::select_window, token::select_crossing,
        token::constraint_ok, token::constraint_conflict,
        token::status_error, token::status_warning, token::status_ok,
        token::ui_background, token::ui_surface, token::ui_surface_raised,
        token::ui_border, token::ui_text, token::ui_text_dim,
        token::ui_accent, token::ui_accent_text, token::ui_hover,
        token::ui_active, token::ui_shadow,
    };
    return t;
}

Color Theme::get(std::string_view tok) const {
    const auto it = colors_.find(std::string(tok));
    return it == colors_.end() ? Color::rgb(1, 0, 1) : it->second;   // magenta = missing
}
Color Theme::get_or(std::string_view tok, Color fb) const {
    const auto it = colors_.find(std::string(tok));
    return it == colors_.end() ? fb : it->second;
}
void Theme::set(std::string_view tok, Color c) { colors_[std::string(tok)] = c; }
bool Theme::has(std::string_view tok) const {
    return colors_.count(std::string(tok)) != 0;
}

std::vector<std::string> Theme::missing_tokens() const {
    std::vector<std::string> miss;
    for (const auto& t : all_tokens())
        if (!colors_.count(t)) miss.push_back(t);
    return miss;
}

bool Theme::load(std::string_view text, std::string* error) {
    std::size_t i = 0, line_no = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string_view::npos) e = text.size();
        std::string_view line = text.substr(i, e - i);
        i = e + 1;
        ++line_no;

        auto trim = [](std::string_view v) {
            while (!v.empty() && std::isspace((unsigned char)v.front())) v.remove_prefix(1);
            while (!v.empty() && std::isspace((unsigned char)v.back())) v.remove_suffix(1);
            return v;
        };
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;

        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            if (error) *error = "line " + std::to_string(line_no) + ": expected key = value";
            return false;
        }
        const std::string_view key = trim(line.substr(0, eq));
        std::string_view val = trim(line.substr(eq + 1));

        // Strip trailing "# ..." comments without eating a colour's leading '#'.
        if (!val.empty() && val.front() == '#') {
            std::size_t sp = val.find_first_of(" \t");
            if (sp != std::string_view::npos) val = trim(val.substr(0, sp));
        } else {
            const std::size_t h2 = val.find('#');
            if (h2 != std::string_view::npos) val = trim(val.substr(0, h2));
        }

        if (key == "name") { name = std::string(val); continue; }
        if (key == "dark") { dark = (val == "true" || val == "1" || val == "yes"); continue; }

        Color c;
        if (!Color::parse(val, c)) {
            if (error)
                *error = "line " + std::to_string(line_no) + ": bad colour \"" +
                         std::string(val) + "\" for " + std::string(key);
            return false;
        }
        colors_[std::string(key)] = c;
    }
    return true;
}

std::string Theme::save() const {
    std::string out;
    out += "# GLCAD theme\n";
    out += "name = " + name + "\n";
    out += std::string("dark = ") + (dark ? "true" : "false") + "\n\n";

    std::string group;
    for (const auto& t : all_tokens()) {
        const std::size_t dot = t.find('.');
        const std::string g = dot == std::string::npos ? t : t.substr(0, dot);
        if (g != group) { out += "\n"; group = g; }
        const auto it = colors_.find(t);
        if (it != colors_.end()) out += t + " = " + it->second.to_hex() + "\n";
    }
    for (const auto& [k, v] : colors_) {
        if (std::find(all_tokens().begin(), all_tokens().end(), k) == all_tokens().end())
            out += k + " = " + v.to_hex() + "\n";
    }
    return out;
}

Theme Theme::dark_default() {
    Theme t;
    t.name = "GLCAD Dark";
    t.dark = true;
    t.set(token::canvas_background,   Color::hex(0x16181B));
    t.set(token::canvas_grid_minor,   Color::hex(0x292D33));
    t.set(token::canvas_grid_major,   Color::hex(0x3D424A));
    t.set(token::canvas_axis_x,       Color::hex(0x8C3D42));
    t.set(token::canvas_axis_y,       Color::hex(0x42804D));
    t.set(token::canvas_axis_z,       Color::hex(0x3A5F94));
    t.set(token::geometry,            Color::hex(0xDEE3EB));
    t.set(token::geometry_preview,    Color::rgba(0.35f, 0.75f, 1.0f, 0.85f));
    t.set(token::geometry_construction, Color::rgba(0.62f, 0.65f, 0.72f, 0.80f));
    t.set(token::geometry_endpoint,   Color::hex(0x737D8C));
    t.set(token::geometry_selected,   Color::hex(0x33AEFF));
    t.set(token::geometry_highlight,  Color::hex(0xFFFFFF));
    t.set(token::geometry_locked,     Color::rgba(0.45f, 0.47f, 0.52f, 0.55f));
    t.set(token::geometry_reference,  Color::rgba(0.55f, 0.58f, 0.64f, 0.60f));
    t.set(token::snap_marker,         Color::hex(0xFFC747));
    t.set(token::snap_track,          Color::rgba(0.95f, 0.78f, 0.30f, 0.55f));
    t.set(token::snap_polar,          Color::rgba(0.40f, 0.85f, 0.75f, 0.60f));
    t.set(token::select_window,       Color::rgba(0.20f, 0.45f, 0.95f, 0.20f));
    t.set(token::select_crossing,     Color::rgba(0.30f, 0.85f, 0.40f, 0.20f));
    t.set(token::constraint_ok,       Color::hex(0x4FB477));
    t.set(token::constraint_conflict, Color::hex(0xE0A030));
    t.set(token::status_error,        Color::hex(0xF25C5C));
    t.set(token::status_warning,      Color::hex(0xE0A030));
    t.set(token::status_ok,           Color::hex(0x4FB477));
    t.set(token::ui_background,       Color::hex(0x0F1113));
    t.set(token::ui_surface,          Color::hex(0x1B1E22));
    t.set(token::ui_surface_raised,   Color::hex(0x24282E));
    t.set(token::ui_border,           Color::hex(0x33383F));
    t.set(token::ui_text,             Color::hex(0xE6EAF0));
    t.set(token::ui_text_dim,         Color::hex(0x8B94A1));
    t.set(token::ui_accent,           Color::hex(0x3B9EFF));
    t.set(token::ui_accent_text,      Color::hex(0x08121C));
    t.set(token::ui_hover,            Color::rgba(0.25f, 0.55f, 1.0f, 0.22f));
    t.set(token::ui_active,           Color::rgba(0.25f, 0.55f, 1.0f, 0.36f));
    t.set(token::ui_shadow,           Color::rgba(0.0f, 0.0f, 0.0f, 0.45f));
    return t;
}

Theme Theme::light_default() {
    Theme t;
    t.name = "GLCAD Light";
    t.dark = false;
    t.set(token::canvas_background,   Color::hex(0xFBFBFC));
    t.set(token::canvas_grid_minor,   Color::hex(0xE4E7EB));
    t.set(token::canvas_grid_major,   Color::hex(0xC9CEd6));
    t.set(token::canvas_axis_x,       Color::hex(0xC24A50));
    t.set(token::canvas_axis_y,       Color::hex(0x3E8C4C));
    t.set(token::canvas_axis_z,       Color::hex(0x3B6FB0));
    t.set(token::geometry,            Color::hex(0x1B1E24));
    t.set(token::geometry_preview,    Color::rgba(0.10f, 0.45f, 0.85f, 0.85f));
    t.set(token::geometry_construction, Color::rgba(0.45f, 0.48f, 0.55f, 0.70f));
    t.set(token::geometry_endpoint,   Color::hex(0x5A626E));
    t.set(token::geometry_selected,   Color::hex(0x0A6FD1));
    t.set(token::geometry_highlight,  Color::hex(0x000000));
    t.set(token::geometry_locked,     Color::rgba(0.55f, 0.57f, 0.62f, 0.55f));
    t.set(token::geometry_reference,  Color::rgba(0.45f, 0.48f, 0.54f, 0.60f));
    t.set(token::snap_marker,         Color::hex(0xC77A00));
    t.set(token::snap_track,          Color::rgba(0.75f, 0.50f, 0.05f, 0.55f));
    t.set(token::snap_polar,          Color::rgba(0.10f, 0.55f, 0.50f, 0.60f));
    t.set(token::select_window,       Color::rgba(0.20f, 0.45f, 0.95f, 0.18f));
    t.set(token::select_crossing,     Color::rgba(0.20f, 0.70f, 0.30f, 0.18f));
    t.set(token::constraint_ok,       Color::hex(0x2E7D4F));
    t.set(token::constraint_conflict, Color::hex(0xB07206));
    t.set(token::status_error,        Color::hex(0xC42B2B));
    t.set(token::status_warning,      Color::hex(0xB07206));
    t.set(token::status_ok,           Color::hex(0x2E7D4F));
    t.set(token::ui_background,       Color::hex(0xF2F3F5));
    t.set(token::ui_surface,          Color::hex(0xFFFFFF));
    t.set(token::ui_surface_raised,   Color::hex(0xFFFFFF));
    t.set(token::ui_border,           Color::hex(0xD5D9DF));
    t.set(token::ui_text,             Color::hex(0x14171C));
    t.set(token::ui_text_dim,         Color::hex(0x646C79));
    t.set(token::ui_accent,           Color::hex(0x0A6FD1));
    t.set(token::ui_accent_text,      Color::hex(0xFFFFFF));
    t.set(token::ui_hover,            Color::rgba(0.25f, 0.55f, 1.0f, 0.22f));
    t.set(token::ui_active,           Color::rgba(0.25f, 0.55f, 1.0f, 0.36f));
    t.set(token::ui_shadow,           Color::rgba(0.0f, 0.0f, 0.0f, 0.14f));
    return t;
}

Theme Theme::high_contrast() {
    Theme t = dark_default();
    t.name = "GLCAD High Contrast";
    t.set(token::canvas_background, Color::hex(0x000000));
    t.set(token::geometry,          Color::hex(0xFFFFFF));
    t.set(token::canvas_grid_minor, Color::hex(0x3A3A3A));
    t.set(token::canvas_grid_major, Color::hex(0x606060));
    t.set(token::geometry_selected, Color::hex(0x00CCFF));
    t.set(token::snap_marker,       Color::hex(0xFFD400));
    t.set(token::ui_text,           Color::hex(0xFFFFFF));
    t.set(token::ui_border,         Color::hex(0x777777));
    return t;
}

ThemeSet::ThemeSet() {
    themes_.push_back(Theme::dark_default());
    themes_.push_back(Theme::light_default());
    themes_.push_back(Theme::high_contrast());
}

void ThemeSet::add(Theme t) {
    for (auto& e : themes_)
        if (e.name == t.name) { e = std::move(t); return; }
    themes_.push_back(std::move(t));
}

bool ThemeSet::activate(std::string_view n) {
    for (std::size_t i = 0; i < themes_.size(); ++i)
        if (themes_[i].name == n) { active_ = i; return true; }
    return false;
}

std::vector<std::string> ThemeSet::names() const {
    std::vector<std::string> n;
    n.reserve(themes_.size());
    for (const auto& t : themes_) n.push_back(t.name);
    return n;
}


const char* appearance_name(Appearance a) noexcept {
    switch (a) {
        case Appearance::Light: return "light";
        case Appearance::Dark:  return "dark";
        case Appearance::Unknown: return "unknown";
    }
    return "unknown";
}

Appearance system_appearance() noexcept {
#if defined(__APPLE__)
    // macOS removes AppleInterfaceStyle in light mode: absence means Light.
    if (const char* forced = std::getenv("GLCAD_APPEARANCE")) {
        if (std::string(forced) == "dark") return Appearance::Dark;
        if (std::string(forced) == "light") return Appearance::Light;
    }
    std::FILE* pipe = popen(
        "defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
    if (!pipe) return Appearance::Unknown;
    char buf[64] = {};
    const bool got = std::fgets(buf, sizeof(buf), pipe) != nullptr;
    pclose(pipe);
    if (!got) return Appearance::Light;
    return std::string(buf).rfind("Dark", 0) == 0 ? Appearance::Dark
                                                  : Appearance::Light;
#else
    if (const char* forced = std::getenv("GLCAD_APPEARANCE")) {
        if (std::string(forced) == "dark") return Appearance::Dark;
        if (std::string(forced) == "light") return Appearance::Light;
    }
    return Appearance::Unknown;
#endif
}

const Theme* ThemeSet::find(std::string_view name) const {
    for (const auto& t : themes_)
        if (t.name == name) return &t;
    return nullptr;
}

Theme* ThemeSet::find(std::string_view name) {
    for (auto& t : themes_)
        if (t.name == name) return &t;
    return nullptr;
}

void ThemeSet::set_system_pair(std::string_view light, std::string_view dark) {
    if (find(light)) light_name_ = std::string(light);
    if (find(dark)) dark_name_ = std::string(dark);
}

bool ThemeSet::apply_appearance(Appearance a) {
    if (a == Appearance::Unknown) return false;
    const std::string& want = a == Appearance::Dark ? dark_name_ : light_name_;
    if (active().name == want) return false;
    return activate(want);
}

bool ThemeSet::sync_with_system() {
    if (!follow_) return false;
    return apply_appearance(system_appearance());
}

ThemeEditor::ThemeEditor(ThemeSet& set) : set_(set), saved_(set.active()) {}

std::vector<ThemeEditor::Group> ThemeEditor::groups() const {
    std::vector<Group> out;
    for (const std::string& tok : all_tokens()) {
        const std::size_t dot = tok.find('.');
        const std::string group = dot == std::string::npos ? tok : tok.substr(0, dot);

        Group* g = nullptr;
        for (auto& existing : out)
            if (existing.name == group) g = &existing;
        if (!g) {
            out.push_back({group, {}});
            g = &out.back();
        }
        g->tokens.push_back(tok);
    }
    return out;
}

Color ThemeEditor::value(std::string_view token) const {
    return set_.active().get(token);
}

void ThemeEditor::set(std::string_view token, Color c) {
    set_.active().set(token, c);
    dirty_ = true;
}

void ThemeEditor::commit() {
    saved_ = set_.active();
    dirty_ = false;
}

void ThemeEditor::revert() {
    const std::string name = set_.active().name;
    set_.active() = saved_;
    set_.active().name = name;
    dirty_ = false;
}

bool ThemeEditor::duplicate_active(std::string_view new_name) {
    if (new_name.empty() || set_.find(new_name)) return false;
    Theme copy = set_.active();
    copy.name = std::string(new_name);
    set_.add(copy);
    if (!set_.activate(new_name)) return false;
    saved_ = set_.active();
    dirty_ = false;
    return true;
}

Theme::RolePalette Theme::role_palette() const {
    RolePalette p;
    p.geometry     = get(token::geometry);
    p.preview      = get(token::geometry_preview);
    p.construction = get(token::geometry_construction);
    p.endpoint     = get(token::geometry_endpoint);
    p.snap         = get(token::snap_marker);
    p.selected     = get(token::geometry_selected);
    p.highlight    = get(token::geometry_highlight);
    p.error        = get(token::status_error);
    p.annotation   = get(token::geometry);
    return p;
}

}
