#include "render/stroke.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gtext {

// Octant-grid steps (direction 1 is (1, 0.5)), not cos/sin of 22.5 deg.
void shx_direction(u8 dir, f32& dx, f32& dy) noexcept {
    static const f32 kx[16] = {1.0f,  1.0f,  1.0f,  0.5f,  0.0f, -0.5f, -1.0f, -1.0f,
                               -1.0f, -1.0f, -1.0f, -0.5f, 0.0f,  0.5f,  1.0f,  1.0f};
    static const f32 ky[16] = {0.0f,  0.5f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  0.5f,
                               0.0f, -0.5f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -0.5f};
    const u8 d = static_cast<u8>(dir & 0x0F);
    dx = kx[d];
    dy = ky[d];
}

namespace {

bool parse_byte(const std::string& tok, u8& out) {
    if (tok.empty()) return false;
    long v = 0;
    if (tok.size() > 1 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
        v = std::strtol(tok.c_str() + 2, nullptr, 16);
    else if (tok.size() > 1 && tok[0] == '0')
        v = std::strtol(tok.c_str() + 1, nullptr, 16);
    else
        v = std::strtol(tok.c_str(), nullptr, 10);
    if (v < -128 || v > 255) return false;
    out = static_cast<u8>(v < 0 ? (256 + v) : v);
    return true;
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<u32> decode_utf8_cp(std::string_view s) {
    std::vector<u32> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        u32 cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

}

void StrokeFont::clear() {
    shapes_.clear();
    cache_.clear();
    info_ = StrokeFontInfo{};
    loaded_ = false;
}

bool StrokeFont::run(const std::vector<u8>& code, StrokeGlyph& out, f32& x, f32& y,
                     bool& pen_down, f32 scale, int depth) const {
    if (depth > 16) return false;

    std::vector<StrokePoint> current;
    if (pen_down) current.push_back({x, y});

    auto lift = [&]() {
        if (current.size() > 1) out.strokes.push_back(current);
        current.clear();
    };

    std::vector<StrokePoint> saved;

    for (std::size_t i = 0; i < code.size(); ++i) {
        const u8 b = code[i];

        // High nibble = length, low nibble = direction.
        if (b >= 0x10) {
            const f32 len = static_cast<f32>((b >> 4) & 0x0F) * scale;
            f32 dx, dy;
            shx_direction(static_cast<u8>(b & 0x0F), dx, dy);
            x += dx * len;
            y += dy * len;
            if (pen_down) {
                if (current.empty()) current.push_back({x - dx * len, y - dy * len});
                current.push_back({x, y});
            }
            continue;
        }

        switch (b) {
            case 0x00: lift(); return true;                 // end of shape

            case 0x01: pen_down = true; current.clear(); current.push_back({x, y});
                       break;
            case 0x02: lift(); pen_down = false; break;

            case 0x03:                                      // divide scale
                if (++i < code.size() && code[i]) scale /= static_cast<f32>(code[i]);
                break;
            case 0x04:                                      // multiply scale
                if (++i < code.size()) scale *= static_cast<f32>(code[i]);
                break;

            case 0x05: saved.push_back({x, y}); break;      // push location
            case 0x06:                                      // pop location
                if (!saved.empty()) {
                    lift();
                    x = saved.back().x;
                    y = saved.back().y;
                    saved.pop_back();
                    if (pen_down) current.push_back({x, y});
                }
                break;

            case 0x07: {                                    // draw a subshape
                if (++i >= code.size()) return true;
                u32 sub = code[i];
                if (info_.kind == ShxKind::UniFont) {
                    if (++i >= code.size()) return true;
                    sub = (sub << 8) | code[i];
                }
                const auto it = shapes_.find(sub);
                if (it != shapes_.end()) {
                    lift();
                    run(it->second.code, out, x, y, pen_down, scale, depth + 1);
                    if (pen_down) { current.clear(); current.push_back({x, y}); }
                }
                break;
            }

            case 0x08: {                                    // one x,y displacement
                if (i + 2 >= code.size()) return true;
                const f32 dx = static_cast<f32>(static_cast<int8_t>(code[i + 1])) * scale;
                const f32 dy = static_cast<f32>(static_cast<int8_t>(code[i + 2])) * scale;
                i += 2;
                x += dx;
                y += dy;
                if (pen_down) {
                    if (current.empty()) current.push_back({x - dx, y - dy});
                    current.push_back({x, y});
                }
                break;
            }

            case 0x09: {                                    // many, until 0,0
                while (i + 2 < code.size()) {
                    const int8_t dx = static_cast<int8_t>(code[i + 1]);
                    const int8_t dy = static_cast<int8_t>(code[i + 2]);
                    i += 2;
                    if (dx == 0 && dy == 0) break;
                    const f32 fx = static_cast<f32>(dx) * scale;
                    const f32 fy = static_cast<f32>(dy) * scale;
                    x += fx;
                    y += fy;
                    if (pen_down) {
                        if (current.empty()) current.push_back({x - fx, y - fy});
                        current.push_back({x, y});
                    }
                }
                break;
            }

            case 0x0A: {                                    // octant arc
                if (i + 2 >= code.size()) return true;
                const f32 radius = static_cast<f32>(code[i + 1]) * scale;
                const int8_t spec = static_cast<int8_t>(code[i + 2]);
                i += 2;

                const bool clockwise = spec < 0;
                const int s = std::abs(static_cast<int>(spec));
                const int start = (s >> 4) & 0x07;
                int span = s & 0x07;
                if (span == 0) span = 8;                    // a full circle

                const f32 a0 = static_cast<f32>(start) * 0.78539816339744831f;
                const f32 sweep = static_cast<f32>(span) * 0.78539816339744831f *
                                  (clockwise ? -1.0f : 1.0f);
                const f32 cx = x - radius * std::cos(a0);
                const f32 cy = y - radius * std::sin(a0);

                const int steps = std::max(4, span * 4);
                if (pen_down && current.empty()) current.push_back({x, y});
                for (int k = 1; k <= steps; ++k) {
                    const f32 t = a0 + sweep * (static_cast<f32>(k) / steps);
                    x = cx + radius * std::cos(t);
                    y = cy + radius * std::sin(t);
                    if (pen_down) current.push_back({x, y});
                }
                break;
            }

            case 0x0B: {                                    // fractional arc
                if (i + 5 >= code.size()) return true;
                const f32 start_off = static_cast<f32>(code[i + 1]) / 256.0f;
                const f32 end_off   = static_cast<f32>(code[i + 2]) / 256.0f;
                const f32 radius = (static_cast<f32>(code[i + 3]) * 256.0f +
                                    static_cast<f32>(code[i + 4])) * scale;
                const int8_t spec = static_cast<int8_t>(code[i + 5]);
                i += 5;

                const bool clockwise = spec < 0;
                const int s = std::abs(static_cast<int>(spec));
                const int start = (s >> 4) & 0x07;
                int span = s & 0x07;
                if (span == 0) span = 8;

                const f32 oct = 0.78539816339744831f;
                const f32 a0 = (static_cast<f32>(start) + start_off) * oct;
                const f32 sweep = (static_cast<f32>(span) + end_off - start_off) *
                                  oct * (clockwise ? -1.0f : 1.0f);
                const f32 cx = x - radius * std::cos(a0);
                const f32 cy = y - radius * std::sin(a0);

                const int steps = std::max(4, span * 4);
                if (pen_down && current.empty()) current.push_back({x, y});
                for (int k = 1; k <= steps; ++k) {
                    const f32 t = a0 + sweep * (static_cast<f32>(k) / steps);
                    x = cx + radius * std::cos(t);
                    y = cy + radius * std::sin(t);
                    if (pen_down) current.push_back({x, y});
                }
                break;
            }

            case 0x0C: {                                    // bulge arc
                if (i + 3 >= code.size()) return true;
                const f32 dx = static_cast<f32>(static_cast<int8_t>(code[i + 1])) * scale;
                const f32 dy = static_cast<f32>(static_cast<int8_t>(code[i + 2])) * scale;
                const f32 bulge = static_cast<f32>(static_cast<int8_t>(code[i + 3])) /
                                  127.0f;
                i += 3;
                const f32 x0 = x, y0 = y;
                x += dx;
                y += dy;
                if (pen_down) {
                    if (current.empty()) current.push_back({x0, y0});
                    // A bulge is the tangent of a quarter of the included angle.
                    const int steps = 8;
                    for (int k = 1; k <= steps; ++k) {
                        const f32 t = static_cast<f32>(k) / steps;
                        const f32 lx = x0 + dx * t, ly = y0 + dy * t;
                        const f32 off = bulge * 0.5f * (1.0f - (2.0f * t - 1.0f) *
                                                                   (2.0f * t - 1.0f));
                        current.push_back({lx - dy * off, ly + dx * off});
                    }
                }
                break;
            }

            case 0x0D: {                                    // many bulge arcs
                while (i + 3 < code.size()) {
                    const int8_t dx = static_cast<int8_t>(code[i + 1]);
                    const int8_t dy = static_cast<int8_t>(code[i + 2]);
                    if (dx == 0 && dy == 0) { i += 2; break; }
                    i += 3;
                    const f32 fx = static_cast<f32>(dx) * scale;
                    const f32 fy = static_cast<f32>(dy) * scale;
                    x += fx;
                    y += fy;
                    if (pen_down) {
                        if (current.empty()) current.push_back({x - fx, y - fy});
                        current.push_back({x, y});
                    }
                }
                break;
            }

            case 0x0E:                                      // next command only
                                                            // in vertical text
                ++i;                                        // skipped here
                break;

            default:
                break;
        }
    }

    lift();
    return true;
}

const StrokeGlyph* StrokeFont::glyph(u32 cp) const {
    const auto cached = cache_.find(cp);
    if (cached != cache_.end()) return &cached->second;

    const auto it = shapes_.find(cp);
    if (it == shapes_.end()) return nullptr;

    StrokeGlyph g;
    f32 x = 0.0f, y = 0.0f;
    bool pen = false;
    run(it->second.code, g, x, y, pen, 1.0f, 0);

    // The pen's final x is the advance.
    const f32 units = info_.above > 0.0f ? info_.above : 1.0f;
    g.advance = x / units;

    bool first = true;
    for (auto& s : g.strokes)
        for (auto& p : s) {
            p.x /= units;
            p.y /= units;
            if (first) {
                g.min_x = g.max_x = p.x;
                g.min_y = g.max_y = p.y;
                first = false;
            }
            g.min_x = std::min(g.min_x, p.x);
            g.max_x = std::max(g.max_x, p.x);
            g.min_y = std::min(g.min_y, p.y);
            g.max_y = std::max(g.max_y, p.y);
        }

    return &cache_.emplace(cp, std::move(g)).first->second;
}

bool StrokeFont::has(u32 cp) const { return shapes_.count(cp) != 0; }

std::vector<std::vector<StrokePoint>> StrokeFont::stroke_text(
    std::string_view utf8, f32 width_factor, f32 oblique_deg) const {
    std::vector<std::vector<StrokePoint>> out;
    const f32 slant = std::tan(oblique_deg * 3.14159265358979323846f / 180.0f);

    f32 pen = 0.0f;
    for (u32 cp : decode_utf8_cp(utf8)) {
        if (cp == '\n') continue;
        const StrokeGlyph* g = glyph(cp);
        if (!g) {
            pen += 0.6f * width_factor;
            continue;
        }
        for (const auto& s : g->strokes) {
            std::vector<StrokePoint> line;
            line.reserve(s.size());
            for (const auto& p : s)
                line.push_back({pen + (p.x + p.y * slant) * width_factor, p.y});
            out.push_back(std::move(line));
        }
        pen += g->advance * width_factor;
    }
    return out;
}

f32 StrokeFont::text_width(std::string_view utf8, f32 width_factor) const {
    f32 pen = 0.0f;
    for (u32 cp : decode_utf8_cp(utf8)) {
        if (cp == '\n') continue;
        const StrokeGlyph* g = glyph(cp);
        pen += (g ? g->advance : 0.6f) * width_factor;
    }
    return pen;
}

bool StrokeFont::load_shp(std::string_view source, std::string* error) {
    clear();

    u32 current = 0;
    bool in_shape = false;
    std::vector<u8> code;
    std::string name;

    auto commit = [&]() {
        if (!in_shape) return;
        Shape s;
        s.code = code;
        s.name = name;
        shapes_[current] = std::move(s);
        code.clear();
        in_shape = false;
    };

    std::size_t pos = 0;
    while (pos <= source.size()) {
        const std::size_t nl = source.find('\n', pos);
        const std::string line =
            trim(source.substr(pos, nl == std::string_view::npos
                                        ? std::string_view::npos
                                        : nl - pos));
        pos = nl == std::string_view::npos ? source.size() + 1 : nl + 1;

        if (line.empty() || line[0] == ';') continue;

        if (line[0] == '*') {
            commit();
            // *number,defbytes,name
            std::string num, rest = line.substr(1);
            std::size_t c1 = rest.find(',');
            num = trim(rest.substr(0, c1));
            long id = 0;
            if (num.size() > 1 && (num[0] == '0') &&
                (num[1] == 'x' || num[1] == 'X'))
                id = std::strtol(num.c_str() + 2, nullptr, 16);
            else
                id = std::strtol(num.c_str(), nullptr, 10);
            current = static_cast<u32>(id);

            name.clear();
            if (c1 != std::string::npos) {
                const std::size_t c2 = rest.find(',', c1 + 1);
                if (c2 != std::string::npos) name = trim(rest.substr(c2 + 1));
            }
            in_shape = true;
            continue;
        }

        if (!in_shape) continue;

        std::string tok;
        bool in_quote = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char ch = line[i];
            if (in_quote) {
                if (ch == '"') in_quote = false;
                else code.push_back(static_cast<u8>(ch));
                continue;
            }
            if (ch == '"') { in_quote = true; continue; }
            if (ch == ',' || ch == ' ' || ch == '\t') {
                u8 b;
                if (!tok.empty() && parse_byte(tok, b)) code.push_back(b);
                tok.clear();
                continue;
            }
            if (ch == ';') break;
            tok.push_back(ch);
        }
        u8 b;
        if (!tok.empty() && parse_byte(tok, b)) code.push_back(b);
    }
    commit();

    if (shapes_.empty()) {
        if (error) *error = "no shapes found in the .shp source";
        return false;
    }

    // Shape 0 is the font header: above, below, modes, then the name.
    const auto hdr = shapes_.find(0);
    if (hdr != shapes_.end() && hdr->second.code.size() >= 2) {
        info_.above = static_cast<f32>(hdr->second.code[0]);
        info_.below = static_cast<f32>(hdr->second.code[1]);
        info_.name = hdr->second.name;
    }
    if (info_.above <= 0.0f) info_.above = 1.0f;

    info_.kind = ShxKind::Shapes1_1;
    info_.glyph_count = static_cast<u32>(shapes_.size());
    loaded_ = true;
    return true;
}

bool StrokeFont::load_shx(gio::Reader& r, std::string* error) {
    clear();

    std::vector<u8> d;
    if (!r.read_all(d) || d.size() < 32) {
        if (error) *error = "the font stream was too short to be an .shx";
        return false;
    }

    const std::string head(reinterpret_cast<const char*>(d.data()),
                           std::min<std::size_t>(d.size(), 32));
    if (head.rfind("AutoCAD-86 shapes 1.0", 0) == 0) info_.kind = ShxKind::Shapes1_0;
    else if (head.rfind("AutoCAD-86 shapes 1.1", 0) == 0) info_.kind = ShxKind::Shapes1_1;
    else if (head.rfind("AutoCAD-86 shapes", 0) == 0) info_.kind = ShxKind::Shapes1_1;
    else if (head.rfind("AutoCAD-86 unifont 1.0", 0) == 0) info_.kind = ShxKind::UniFont;
    else if (head.rfind("AutoCAD-86 bigfont 1.0", 0) == 0) info_.kind = ShxKind::BigFont;
    else {
        if (error) *error = "not an AutoCAD .shx font";
        return false;
    }

    // The header is terminated by 0x1A, whatever its exact text.
    std::size_t p = 0;
    while (p < d.size() && d[p] != 0x1A) ++p;
    if (p + 1 >= d.size()) {
        if (error) *error = "the .shx header is not terminated";
        return false;
    }
    p += 2;                                    // past 0x1A and the pad byte

    auto u16le = [&](std::size_t at) -> u32 {
        return at + 1 < d.size()
                   ? static_cast<u32>(d[at]) | (static_cast<u32>(d[at + 1]) << 8)
                   : 0;
    };

    if (info_.kind == ShxKind::UniFont) {
        // Unifont: glyph count, then self-numbered records; 2-byte shape numbers.
        const u32 count = u16le(p);
        p += 4;                                // count, then two reserved bytes
        info_.unicode = true;

        for (u32 k = 0; k < count && p + 3 < d.size(); ++k) {
            const u32 code = u16le(p);
            const u32 len = u16le(p + 2);
            p += 4;
            if (len == 0 || p + len > d.size()) break;
            Shape s;
            s.code.assign(d.begin() + static_cast<long>(p),
                          d.begin() + static_cast<long>(p + len));
            shapes_[code] = std::move(s);
            p += len;
        }
    } else {
        // Shapes 1.0/1.1: first, last, count, (number, length) index, then data.
        const u32 first = u16le(p);
        const u32 last  = u16le(p + 2);
        const u32 count = u16le(p + 4);
        (void)first;
        (void)last;
        p += 6;

        std::vector<std::pair<u32, u32>> index;
        index.reserve(count);
        for (u32 k = 0; k < count && p + 3 < d.size(); ++k) {
            index.emplace_back(u16le(p), u16le(p + 2));
            p += 4;
        }
        for (const auto& [num, len] : index) {
            if (len == 0 || p + len > d.size()) break;
            Shape s;
            // The name is stored first, NUL terminated, then the byte code.
            std::size_t q = p;
            while (q < p + len && d[q] != 0) {
                s.name.push_back(static_cast<char>(d[q]));
                ++q;
            }
            ++q;
            if (q <= p + len)
                s.code.assign(d.begin() + static_cast<long>(q),
                              d.begin() + static_cast<long>(p + len));
            shapes_[num] = std::move(s);
            p += len;
        }
    }

    if (shapes_.empty()) {
        if (error) *error = "the .shx contained no glyphs";
        return false;
    }

    const auto hdr = shapes_.find(0);
    if (hdr != shapes_.end() && hdr->second.code.size() >= 2) {
        info_.above = static_cast<f32>(hdr->second.code[0]);
        info_.below = static_cast<f32>(hdr->second.code[1]);
        if (info_.name.empty()) info_.name = hdr->second.name;
    }
    if (info_.above <= 0.0f) info_.above = 1.0f;

    info_.glyph_count = static_cast<u32>(shapes_.size());
    loaded_ = true;
    return true;
}

bool StrokeFont::load_file(const std::string& path, std::string* error) {
    gio::FileReader f;
    const gio::Status s = f.open(path);
    if (!s.ok) {
        if (error) *error = s.message;
        return false;
    }

    std::vector<u8> data;
    f.read_all(data);
    if (data.size() > 17 &&
        std::string(reinterpret_cast<const char*>(data.data()), 10) == "AutoCAD-86") {
        gio::MemoryReader m(data);
        return load_shx(m, error);
    }
    return load_shp(std::string_view(reinterpret_cast<const char*>(data.data()),
                                     data.size()),
                    error);
}

}
