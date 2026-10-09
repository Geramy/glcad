#include "render/world.hpp"

#include <algorithm>
#include <cmath>

namespace gtext {
namespace {

constexpr f64 kEps = 1e-12;

struct V3 { f64 x, y, z; };
f64 dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 norm(const V3& a) {
    const f64 n = std::sqrt(dot(a, a));
    return n > kEps ? V3{a.x / n, a.y / n, a.z / n} : V3{0, 0, 0};
}

std::size_t utf8_len(std::string_view s, std::size_t i) {
    if (i >= s.size()) return 0;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t n = 1;
    if ((c & 0xE0u) == 0xC0u) n = 2;
    else if ((c & 0xF0u) == 0xE0u) n = 3;
    else if ((c & 0xF8u) == 0xF0u) n = 4;
    return (i + n <= s.size()) ? n : 1;
}

u32 code_at(std::string_view s, std::size_t i) {
    std::size_t k = i;
    return next_codepoint(s, k);
}

}

void TextFrame::to_world(f64 x, f64 y, f64& wx, f64& wy, f64& wz) const noexcept {
    wx = ox + ux * x + vx * y;
    wy = oy + uy * x + vy * y;
    wz = oz + uz * x + vz * y;
}

TextFrame TextFrame::on_plane(f64 px, f64 py, f64 pz, f64 nx, f64 ny,
                              f64 nz) noexcept {
    TextFrame f;
    const V3 n = norm({nx, ny, nz});
    f.ox = px; f.oy = py; f.oz = pz;
    f.nx = n.x; f.ny = n.y; f.nz = n.z;

    // Cross with the world axis least aligned to the normal: stable axes.
    const f64 ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    V3 seed{0, 0, 1};
    if (ax <= ay && ax <= az) seed = {1, 0, 0};
    else if (ay <= az)        seed = {0, 1, 0};

    const V3 u = norm(cross(seed, n));
    const V3 v = cross(n, u);
    f.ux = u.x; f.uy = u.y; f.uz = u.z;
    f.vx = v.x; f.vy = v.y; f.vz = v.z;
    return f;
}

TextFrame TextFrame::along(f64 px, f64 py, f64 pz, f64 nx, f64 ny, f64 nz,
                           f64 dx, f64 dy, f64 dz) noexcept {
    const V3 n = norm({nx, ny, nz});
    V3 d{dx, dy, dz};
    const f64 along_n = dot(d, n);
    d = norm({d.x - n.x * along_n, d.y - n.y * along_n, d.z - n.z * along_n});
    if (dot(d, d) < 0.5) return on_plane(px, py, pz, nx, ny, nz);

    TextFrame f;
    f.ox = px; f.oy = py; f.oz = pz;
    f.nx = n.x; f.ny = n.y; f.nz = n.z;
    const V3 v = cross(n, d);
    f.ux = d.x; f.uy = d.y; f.uz = d.z;
    f.vx = v.x; f.vy = v.y; f.vz = v.z;
    return f;
}

WorldText place_in_world(const Font& font, std::string_view utf8,
                         const TextFrame& frame, const WorldTextStyle& style,
                         const f64 eye[3]) {
    WorldText out;
    const Layout l = layout_bidi(font, utf8, style.text);
    out.width = l.width * style.height;
    out.height = l.height * style.height;

    TextFrame f = frame;

    if (style.facing != FacingMode::Flat) {
        const V3 to_eye = norm({eye[0] - f.ox, eye[1] - f.oy, eye[2] - f.oz});
        if (dot(to_eye, to_eye) > 0.5) {
            const V3 up{f.vx, f.vy, f.vz};
            V3 right = cross(up, to_eye);
            if (dot(right, right) < 1e-9) {
                right = cross(V3{0, 0, 1}, to_eye);
                if (dot(right, right) < 1e-9) right = {1, 0, 0};
            }
            right = norm(right);
            const V3 v = cross(to_eye, right);
            f.nx = to_eye.x; f.ny = to_eye.y; f.nz = to_eye.z;
            f.ux = right.x; f.uy = right.y; f.uz = right.z;
            f.vx = v.x; f.vy = v.y; f.vz = v.z;
        }
    } else {
        const V3 to_eye{eye[0] - f.ox, eye[1] - f.oy, eye[2] - f.oz};
        const f64 side = dot(to_eye, V3{f.nx, f.ny, f.nz});
        if (side < 0.0) {
            if (style.hide_when_behind) {
                out.visible = false;
                return out;
            }
            if (style.flip_when_behind) {
                f.ux = -f.ux; f.uy = -f.uy; f.uz = -f.uz;
                f.nx = -f.nx; f.ny = -f.ny; f.nz = -f.nz;
                out.flipped = true;
            }
        }
    }

    const f64 h = style.height;
    for (const PlacedGlyph& g : l.glyphs) {
        if (!g.glyph) continue;
        WorldGlyph w;
        w.glyph = g.glyph;
        const f64 x0 = g.x * h, y0 = g.y * h;
        const f64 x1 = (g.x + g.w) * h, y1 = (g.y + g.h) * h;
        const f64 xs[4] = {x0, x1, x1, x0};
        const f64 ys[4] = {y0, y0, y1, y1};
        for (int i = 0; i < 4; ++i)
            f.to_world(xs[i], ys[i], w.x[i], w.y[i], w.z[i]);
        out.glyphs.push_back(w);
    }
    return out;
}

BidiClass bidi_class(u32 c) noexcept {
    if ((c >= 0x0590 && c <= 0x05FF) || (c >= 0xFB1D && c <= 0xFB4F))
        return BidiClass::RightToLeft;
    if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0700 && c <= 0x074F) ||
        (c >= 0x0750 && c <= 0x077F) || (c >= 0x0780 && c <= 0x07BF) ||
        (c >= 0x08A0 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) ||
        (c >= 0xFE70 && c <= 0xFEFF)) {
        // Arabic-Indic digits are numbers, not letters.
        if (c >= 0x0660 && c <= 0x0669) return BidiClass::ArabicNumber;
        if (c >= 0x06F0 && c <= 0x06F9) return BidiClass::ArabicNumber;
        if (c == 0x060C || c == 0x061B || c == 0x061F) return BidiClass::Neutral;
        return BidiClass::ArabicLetter;
    }
    if (c >= '0' && c <= '9') return BidiClass::EuropeanNumber;

    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return BidiClass::LeftToRight;
    if (c >= 0x00C0 && c <= 0x024F) return BidiClass::LeftToRight;
    if (c >= 0x0370 && c <= 0x058F) return BidiClass::LeftToRight;
    if (c >= 0x0900 && c <= 0x1FFF) return BidiClass::LeftToRight;
    if (c >= 0x2C00 && c <= 0xD7FF) return BidiClass::LeftToRight;
    if (c >= 0xF900 && c <= 0xFAFF) return BidiClass::LeftToRight;
    return BidiClass::Neutral;
}

Direction paragraph_direction(std::string_view utf8) noexcept {
    for (std::size_t i = 0; i < utf8.size();) {
        const u32 c = code_at(utf8, i);
        i += utf8_len(utf8, i);
        const BidiClass k = bidi_class(c);
        if (k == BidiClass::LeftToRight) return Direction::LeftToRight;
        if (k == BidiClass::RightToLeft || k == BidiClass::ArabicLetter)
            return Direction::RightToLeft;
    }
    return Direction::LeftToRight;
}

std::vector<BidiRun> bidi_runs(std::string_view utf8, Direction paragraph) {
    std::vector<BidiRun> out;
    if (utf8.empty()) return out;

    const u8 base = paragraph == Direction::RightToLeft ? 1 : 0;

    struct Ch { std::size_t at, len; BidiClass k; u8 level; };
    std::vector<Ch> chars;
    for (std::size_t i = 0; i < utf8.size();) {
        const std::size_t n = utf8_len(utf8, i);
        chars.push_back({i, n, bidi_class(code_at(utf8, i)), base});
        i += n;
    }

    // Levels are relative to the paragraph: Latin inside Arabic is level 2.
    const u8 ltr_level = base == 1 ? 2 : 0;

    for (Ch& c : chars) {
        switch (c.k) {
            case BidiClass::LeftToRight:
                c.level = ltr_level;
                break;
            case BidiClass::RightToLeft:
            case BidiClass::ArabicLetter:
                c.level = 1;
                break;
            case BidiClass::EuropeanNumber:
                c.level = ltr_level;
                break;
            case BidiClass::ArabicNumber:
                c.level = 2;
                break;
            case BidiClass::Neutral:
                c.level = base;
                break;
        }
    }

    for (std::size_t i = 0; i < chars.size(); ++i) {
        if (chars[i].k != BidiClass::Neutral) continue;
        std::size_t j = i;
        while (j < chars.size() && chars[j].k == BidiClass::Neutral) ++j;

        auto strong_dir = [&](std::size_t at) -> int {
            if (at >= chars.size()) return -1;
            switch (chars[at].k) {
                case BidiClass::LeftToRight:
                case BidiClass::EuropeanNumber:
                case BidiClass::ArabicNumber:
                    return 0;
                case BidiClass::RightToLeft:
                case BidiClass::ArabicLetter:
                    return 1;
                default:
                    return -1;
            }
        };
        const int before = i == 0 ? -1 : strong_dir(i - 1);
        const int after = strong_dir(j);
        u8 level = base;
        if (before >= 0 && before == after)
            level = before == 1 ? 1 : ltr_level;
        for (std::size_t k = i; k < j; ++k) chars[k].level = level;
        i = j - 1;
    }

    std::vector<BidiRun> logical;
    for (std::size_t i = 0; i < chars.size();) {
        std::size_t j = i;
        while (j < chars.size() && chars[j].level == chars[i].level) ++j;
        BidiRun r;
        r.begin = chars[i].at;
        r.end = chars[j - 1].at + chars[j - 1].len;
        r.level = chars[i].level;
        r.direction = (r.level % 2) ? Direction::RightToLeft
                                    : Direction::LeftToRight;
        logical.push_back(r);
        i = j;
    }

    // Unicode reordering: from the highest level down, reverse each stretch.
    u8 highest = 0, lowest_odd = 63;
    for (const BidiRun& r : logical) {
        highest = std::max(highest, r.level);
        if (r.level % 2) lowest_odd = std::min(lowest_odd, r.level);
    }
    out = logical;
    for (int level = highest; level >= lowest_odd && level >= 1; --level) {
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (out[i].level < level) continue;
            std::size_t j = i;
            while (j < out.size() && out[j].level >= level) ++j;
            std::reverse(out.begin() + static_cast<long>(i),
                         out.begin() + static_cast<long>(j));
            i = j;
        }
    }
    return out;
}

namespace {

struct ArabicLetter {
    u32  base;
    u32  forms;       // the isolated form in the presentation block
    bool dual;        // true when it also has initial and medial forms
};

const ArabicLetter kArabic[] = {
    {0x0621, 0xFE80, false},  // hamza
    {0x0622, 0xFE81, false},  // alef madda
    {0x0623, 0xFE83, false},  // alef hamza above
    {0x0624, 0xFE85, false},  // waw hamza
    {0x0625, 0xFE87, false},  // alef hamza below
    {0x0626, 0xFE89, true},   // yeh hamza
    {0x0627, 0xFE8D, false},  // alef
    {0x0628, 0xFE8F, true},   // beh
    {0x0629, 0xFE93, false},  // teh marbuta
    {0x062A, 0xFE95, true},   // teh
    {0x062B, 0xFE99, true},   // theh
    {0x062C, 0xFE9D, true},   // jeem
    {0x062D, 0xFEA1, true},   // hah
    {0x062E, 0xFEA5, true},   // khah
    {0x062F, 0xFEA9, false},  // dal
    {0x0630, 0xFEAB, false},  // thal
    {0x0631, 0xFEAD, false},  // reh
    {0x0632, 0xFEAF, false},  // zain
    {0x0633, 0xFEB1, true},   // seen
    {0x0634, 0xFEB5, true},   // sheen
    {0x0635, 0xFEB9, true},   // sad
    {0x0636, 0xFEBD, true},   // dad
    {0x0637, 0xFEC1, true},   // tah
    {0x0638, 0xFEC5, true},   // zah
    {0x0639, 0xFEC9, true},   // ain
    {0x063A, 0xFECD, true},   // ghain
    {0x0641, 0xFED1, true},   // feh
    {0x0642, 0xFED5, true},   // qaf
    {0x0643, 0xFED9, true},   // kaf
    {0x0644, 0xFEDD, true},   // lam
    {0x0645, 0xFEE1, true},   // meem
    {0x0646, 0xFEE5, true},   // noon
    {0x0647, 0xFEE9, true},   // heh
    {0x0648, 0xFEED, false},  // waw
    {0x0649, 0xFEEF, true},   // alef maksura
    {0x064A, 0xFEF1, true},   // yeh
};

const ArabicLetter* find_letter(u32 c) {
    for (const ArabicLetter& l : kArabic)
        if (l.base == c) return &l;
    return nullptr;
}

bool is_transparent(u32 c) {
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
           (c >= 0x06D6 && c <= 0x06ED);
}

}

bool joins_right(u32 c) noexcept {
    return find_letter(c) != nullptr;
}

bool joins_left(u32 c) noexcept {
    const ArabicLetter* l = find_letter(c);
    return l && l->dual;
}

std::vector<ShapedGlyph> shape_arabic(std::string_view utf8) {
    std::vector<ShapedGlyph> out;

    std::vector<u32> cps;
    for (std::size_t i = 0; i < utf8.size();) {
        cps.push_back(code_at(utf8, i));
        i += utf8_len(utf8, i);
    }

    for (std::size_t i = 0; i < cps.size(); ++i) {
        ShapedGlyph g;
        g.source = cps[i];
        g.codepoint = cps[i];

        const ArabicLetter* l = find_letter(cps[i]);
        if (!l) {
            out.push_back(g);
            continue;
        }

        std::size_t p = i;
        bool has_prev = false;
        while (p > 0) {
            --p;
            if (is_transparent(cps[p])) continue;
            has_prev = joins_left(cps[p]);
            break;
        }
        std::size_t n = i + 1;
        bool has_next = false;
        while (n < cps.size()) {
            if (is_transparent(cps[n])) { ++n; continue; }
            has_next = joins_right(cps[n]);
            break;
        }

        if (has_prev && has_next && l->dual) g.form = JoiningForm::Medial;
        else if (has_prev)                   g.form = JoiningForm::Final;
        else if (has_next && l->dual)        g.form = JoiningForm::Initial;
        else                                 g.form = JoiningForm::Isolated;

        // Presentation order: isolated, final, initial, medial.
        switch (g.form) {
            case JoiningForm::Isolated: g.codepoint = l->forms; break;
            case JoiningForm::Final:    g.codepoint = l->forms + 1; break;
            case JoiningForm::Initial:  g.codepoint = l->forms + 2; break;
            case JoiningForm::Medial:   g.codepoint = l->forms + 3; break;
        }
        out.push_back(g);
    }
    return out;
}

Layout layout_bidi(const Font& f, std::string_view utf8, const TextStyle& style,
                   Direction paragraph) {
    bool needs_bidi = false;
    for (std::size_t i = 0; i < utf8.size();) {
        const BidiClass k = bidi_class(code_at(utf8, i));
        i += utf8_len(utf8, i);
        if (k == BidiClass::RightToLeft || k == BidiClass::ArabicLetter) {
            needs_bidi = true;
            break;
        }
    }
    if (!needs_bidi) return layout_text(f, utf8, style);

    std::string visual;
    for (const BidiRun& r : bidi_runs(utf8, paragraph)) {
        const std::string_view piece = utf8.substr(r.begin, r.end - r.begin);
        if (r.direction == Direction::LeftToRight) {
            visual += piece;
            continue;
        }
        const std::vector<ShapedGlyph> shaped = shape_arabic(piece);
        for (auto it = shaped.rbegin(); it != shaped.rend(); ++it) {
            const u32 c = it->codepoint;
            if (c < 0x80) {
                visual += static_cast<char>(c);
            } else if (c < 0x800) {
                visual += static_cast<char>(0xC0 | (c >> 6));
                visual += static_cast<char>(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                visual += static_cast<char>(0xE0 | (c >> 12));
                visual += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                visual += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                visual += static_cast<char>(0xF0 | (c >> 18));
                visual += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                visual += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                visual += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
    }
    return layout_text(f, visual, style);
}

}
