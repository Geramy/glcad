#include "render/layout.hpp"

#include <algorithm>

namespace gtext {

Layout layout_text(const Font& f, std::string_view utf8, const TextStyle& st) {
    Layout out;
    if (!f.loaded() || utf8.empty()) return out;

    const f32 line_h = f.metrics().line_height() * st.line_spacing;

    struct Word { std::size_t begin, end; f32 width; };

    std::vector<std::pair<std::size_t, std::size_t>> lines;
    {
        std::size_t start = 0;
        for (std::size_t i = 0; i <= utf8.size(); ++i) {
            if (i == utf8.size() || utf8[i] == '\n') {
                lines.emplace_back(start, i);
                start = i + 1;
            }
        }
    }

    std::vector<std::pair<std::size_t, std::size_t>> final_lines;
    for (const auto& [ls, le] : lines) {
        if (st.wrap_width <= 0.0f) { final_lines.emplace_back(ls, le); continue; }

        std::size_t line_start = ls;
        std::size_t last_space = std::string_view::npos;
        f32 w = 0.0f;
        u32 prev = 0;

        std::size_t i = ls;
        while (i < le) {
            const std::size_t glyph_start = i;
            const u32 cp = next_codepoint(utf8, i);
            if (cp == ' ') last_space = glyph_start;

            f32 adv = 0.0f;
            if (const Glyph* g = f.glyph(cp)) {
                if (prev && st.kerning) adv += f.kerning(prev, cp);
                adv += g->advance + st.letter_spacing;
            }
            prev = cp;

            if (w + adv > st.wrap_width / st.size && glyph_start > line_start) {
                const std::size_t brk =
                    (last_space != std::string_view::npos && last_space > line_start)
                        ? last_space : glyph_start;
                final_lines.emplace_back(line_start, brk);
                line_start = (brk == last_space) ? brk + 1 : brk;
                last_space = std::string_view::npos;
                w = 0.0f;
                prev = 0;
                i = line_start;
                continue;
            }
            w += adv;
        }
        final_lines.emplace_back(line_start, le);
    }

    out.line_count = static_cast<u32>(final_lines.size());

    std::vector<f32> widths(final_lines.size(), 0.0f);
    f32 widest = 0.0f;
    for (std::size_t li = 0; li < final_lines.size(); ++li) {
        const auto [b, e] = final_lines[li];
        f32 w = 0.0f;
        u32 prev = 0;
        std::size_t i = b;
        while (i < e) {
            const u32 cp = next_codepoint(utf8, i);
            if (const Glyph* g = f.glyph(cp)) {
                if (prev && st.kerning) w += f.kerning(prev, cp);
                w += g->advance + st.letter_spacing;
            }
            prev = cp;
        }
        widths[li] = w;
        widest = std::max(widest, w);
    }

    f32 y0 = 0.0f;
    const f32 block_h = line_h * static_cast<f32>(final_lines.size());
    switch (st.valign) {
        case VAlign::Baseline: y0 = 0.0f; break;
        case VAlign::Top:      y0 = -f.metrics().ascent; break;
        case VAlign::Middle:   y0 = block_h * 0.5f - f.metrics().ascent; break;
        case VAlign::Bottom:   y0 = block_h - f.metrics().ascent; break;
    }

    out.min_x = 1e30f; out.min_y = 1e30f;
    out.max_x = -1e30f; out.max_y = -1e30f;

    for (std::size_t li = 0; li < final_lines.size(); ++li) {
        const auto [b, e] = final_lines[li];

        f32 pen_x = 0.0f;
        switch (st.align) {
            case Align::Left:   pen_x = 0.0f; break;
            case Align::Centre: pen_x = -widths[li] * 0.5f; break;
            case Align::Right:  pen_x = -widths[li]; break;
        }
        const f32 pen_y = y0 - line_h * static_cast<f32>(li);

        u32 prev = 0;
        std::size_t i = b;
        while (i < e) {
            const u32 cp = next_codepoint(utf8, i);
            const Glyph* g = f.glyph(cp);
            if (!g) { prev = cp; continue; }

            if (prev && st.kerning) pen_x += f.kerning(prev, cp);

            if (g->valid()) {
                PlacedGlyph p;
                p.glyph = g;
                p.x = (pen_x + g->bearing_x) * st.size;
                p.y = (pen_y + g->bearing_y - g->height) * st.size;
                p.w = g->width * st.size;
                p.h = g->height * st.size;
                p.line = static_cast<u32>(li);

                out.min_x = std::min(out.min_x, p.x);
                out.min_y = std::min(out.min_y, p.y);
                out.max_x = std::max(out.max_x, p.x + p.w);
                out.max_y = std::max(out.max_y, p.y + p.h);
                out.glyphs.push_back(p);
            }
            pen_x += g->advance + st.letter_spacing;
            prev = cp;
        }
    }

    out.width  = widest * st.size;
    out.height = block_h * st.size;
    if (out.glyphs.empty()) { out.min_x = out.min_y = out.max_x = out.max_y = 0.0f; }
    return out;
}

void measure_text(const Font& f, std::string_view utf8, const TextStyle& st,
                  f32& w, f32& h) {
    const Layout l = layout_text(f, utf8, st);
    w = l.width;
    h = l.height;
}

}
