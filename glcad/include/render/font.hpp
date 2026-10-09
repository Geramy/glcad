#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "math/config.hpp"
#include "engine/stream.hpp"

namespace gtext {

using gcm::f32;
using gcm::f64;
using gcm::i32;
using gcm::u32;
using gcm::u64;
using gcm::u8;

struct Glyph {
    u32 codepoint{0};

    i32 ax{0}, ay{0}, aw{0}, ah{0};

    f32 bearing_x{0.0f}, bearing_y{0.0f};
    f32 width{0.0f}, height{0.0f};
    f32 advance{0.0f};

    bool valid() const noexcept { return aw > 0 && ah > 0; }
};

struct FontMetrics {
    f32 ascent{0.8f};
    f32 descent{-0.2f};
    f32 line_gap{0.0f};
    f32 line_height() const noexcept { return ascent - descent + line_gap; }
    f32 cap_height{0.7f};
    f32 x_height{0.5f};
};

struct OutlinePoint { double x{}, y{}; };
struct GlyphOutline {
    std::vector<std::vector<OutlinePoint>> contours;
    mutable std::vector<std::array<OutlinePoint,3>> triangles;
    mutable std::array<double,3> triangulation_volume{};
    mutable int triangulation_bits{-1};
};

class Font {
public:
    static constexpr u8 sdf_on_edge = 128;
    static constexpr f32 sdf_distance_scale = 36.0f;
    static constexpr f32 sdf_distance_range = 255.0f / sdf_distance_scale;

    bool load(const std::vector<u8>& ttf, i32 sdf_size = 48, i32 padding = 6,
              std::string* error = nullptr);
    bool load(gio::Reader& r, i32 sdf_size = 48, i32 padding = 6,
              std::string* error = nullptr);
    bool load_file(const std::string& path, i32 sdf_size = 48, i32 padding = 6,
                   std::string* error = nullptr);

    bool load_with_icons(const std::vector<u8>& ttf, const std::vector<u8>& icons,
                         const std::vector<u32>& icon_codepoints,
                         i32 sdf_size = 48, i32 padding = 6,
                         std::string* error = nullptr);
    bool load_with_icons_files(const std::string& text_path,
                               const std::string& icon_path,
                               const std::vector<u32>& icon_codepoints,
                               i32 sdf_size = 48, i32 padding = 6,
                               std::string* error = nullptr);

    bool loaded() const noexcept { return loaded_; }
    const std::string& family() const noexcept { return family_; }
    std::shared_ptr<const std::vector<u8>> source_bytes() const noexcept { return source_; }

    const Glyph* glyph(u32 codepoint) const;
    const GlyphOutline& outline(u32 codepoint) const;
    const FontMetrics& metrics() const noexcept { return metrics_; }

    f32 kerning(u32 a, u32 b) const;

    const std::vector<u8>& atlas() const noexcept { return atlas_; }
    i32 atlas_width() const noexcept { return atlas_w_; }
    i32 atlas_height() const noexcept { return atlas_h_; }
    i32 sdf_padding() const noexcept { return padding_; }
    i32 sdf_size() const noexcept { return sdf_size_; }

    std::size_t glyph_count() const noexcept { return glyphs_.size(); }

    bool covers_ascii() const noexcept;
    std::size_t coverage() const noexcept;

    f32 measure(std::string_view utf8) const;

private:
    bool loaded_{false};
    std::shared_ptr<const std::vector<u8>> source_;
    std::string family_;
    std::unordered_map<u32, Glyph> glyphs_;
    mutable std::unordered_map<u32, GlyphOutline> outlines_;
    std::unordered_map<u64, f32>   kerning_;
    FontMetrics metrics_;

    std::vector<u8> atlas_;
    i32 atlas_w_{0}, atlas_h_{0};
    i32 sdf_size_{48}, padding_{6};
};

u32 next_codepoint(std::string_view s, std::size_t& i);
std::vector<u32> decode_utf8(std::string_view s);

}
