#pragma once

#include <optional>
#include <string>
#include <vector>

#include "render/font.hpp"

namespace gtext {

enum class FontKind : u8 { TrueType, OpenType, SHX, Unknown };

struct FontEntry {
    std::string name;
    std::string path;
    FontKind    kind{FontKind::TrueType};
    bool        monospace{false};
    bool        italic{false};
    int         weight{400};     // 100..900

    bool is_stroke() const noexcept { return kind == FontKind::SHX; }
};

class FontRegistry {
public:
    std::size_t scan_system(const std::vector<std::string>& extra_dirs = {});
    void        add(FontEntry e);

    const std::vector<FontEntry>& all() const noexcept { return fonts_; }
    std::size_t size() const noexcept { return fonts_.size(); }

    const FontEntry* find(std::string_view name) const;
    std::vector<const FontEntry*> search(std::string_view partial) const;
    std::vector<const FontEntry*> monospace() const;

    const FontEntry* resolve(std::string_view requested, bool& substituted) const;

    void map_substitute(std::string_view missing, std::string_view replacement);
    const std::vector<std::pair<std::string, std::string>>& substitutions() const noexcept {
        return subs_;
    }

    void set_fallbacks(std::vector<std::string> names) { fallbacks_ = std::move(names); }

private:
    std::vector<FontEntry> fonts_;
    std::vector<std::pair<std::string, std::string>> subs_;
    std::vector<std::string> fallbacks_{"Helvetica", "Arial", "DejaVu Sans"};
};

struct TextStyleDef {
    std::string name{"Standard"};
    std::string font;
    std::string big_font;
    f32  height{0.0f};                // 0 = ask at insertion time
    f32  width_factor{1.0f};
    f32  oblique{0.0f};               // degrees
    bool backwards{false};
    bool upside_down{false};
    bool vertical{false};
    bool annotative{false};

    bool operator==(const TextStyleDef&) const = default;
    bool height_fixed() const noexcept { return height > 0.0f; }
};

class TextStyleTable {
public:
    TextStyleTable();

    const TextStyleDef* find(std::string_view name) const;
    TextStyleDef&       add(TextStyleDef s);
    bool                remove(std::string_view name);
    void                set_current(std::string_view name);
    const std::string&  current() const noexcept { return current_; }
    std::size_t         size() const noexcept { return styles_.size(); }
    const std::vector<TextStyleDef>& all() const noexcept { return styles_; }

private:
    std::vector<TextStyleDef> styles_;
    std::string current_{"Standard"};
};

}
