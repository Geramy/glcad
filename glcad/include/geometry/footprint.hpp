#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "math/config.hpp"

namespace ggeo::plan {

using gcm::f64;

// dim: 0 = scalar, 1 = length (m), 2 = area (m²).
struct Quantity {
    f64 v{0.0};
    int dim{0};
};

namespace detail {

struct CalcParser {
    std::string_view s;
    std::size_t      i{0};
    std::string      err;
    int              depth{0};

    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool at(char c) {
        skip();
        return i < s.size() && s[i] == c;
    }
    bool eat(char c) {
        if (!at(c)) return false;
        ++i;
        return true;
    }

    std::optional<Quantity> unit_word() {
        skip();
        std::size_t b = i;
        while (i < s.size() &&
               (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '2' ||
                s[i] == '\xc2'))
            ++i;
        std::string w(s.substr(b, i - b));
        for (char& c : w) c = char(std::tolower(static_cast<unsigned char>(c)));
        const auto is = [&](const char* a) { return w == a; };
        if (is("mm")) return Quantity{0.001, 1};
        if (is("cm")) return Quantity{0.01, 1};
        if (is("m")) return Quantity{1.0, 1};
        if (is("km")) return Quantity{1000.0, 1};
        if (is("in") || is("inch") || is("inches")) return Quantity{0.0254, 1};
        if (is("ft") || is("foot") || is("feet")) return Quantity{0.3048, 1};
        if (is("yd") || is("yard") || is("yards")) return Quantity{0.9144, 1};
        if (is("mm2") || is("sqmm")) return Quantity{1e-6, 2};
        if (is("cm2") || is("sqcm")) return Quantity{1e-4, 2};
        if (is("m2") || is("sqm") || is("m\xc2\xb2")) return Quantity{1.0, 2};
        if (is("in2") || is("sqin")) return Quantity{0.00064516, 2};
        if (is("ft2") || is("sqft")) return Quantity{0.09290304, 2};
        if (is("acre") || is("acres")) return Quantity{4046.8564224, 2};
        if (is("ha") || is("hectare") || is("hectares")) return Quantity{10000.0, 2};
        i = b;
        return std::nullopt;
    }

    std::optional<Quantity> factor() {
        if (++depth > 64) { err = "expression nests too deep"; return std::nullopt; }
        struct Undo { int& d; ~Undo() { --d; } } undo{depth};
        skip();
        if (eat('-')) {
            auto q = factor();
            if (q) q->v = -q->v;
            return q;
        }
        if (eat('(')) {
            auto q = expr();
            if (!q) return q;
            if (!eat(')')) { err = "missing ')'"; return std::nullopt; }
            return after_number(*q);
        }
        if (i + 4 <= s.size() && s.substr(i, 4) == "sqrt") {
            i += 4;
            if (!eat('(')) { err = "sqrt needs (...)"; return std::nullopt; }
            auto q = expr();
            if (!q) return q;
            if (!eat(')')) { err = "missing ')'"; return std::nullopt; }
            if (q->v < 0.0) { err = "sqrt of a negative"; return std::nullopt; }
            if (q->dim % 2 != 0) { err = "sqrt of a length is no unit anyone draws in"; return std::nullopt; }
            return after_number(Quantity{std::sqrt(q->v), q->dim / 2});
        }
        std::size_t b = i;
        while (i < s.size() &&
               (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
            ++i;
        if (i == b) { err = "expected a number at '" + std::string(s.substr(i)) + "'"; return std::nullopt; }
        f64 v = 0.0;
        try { v = std::stod(std::string(s.substr(b, i - b))); }
        catch (...) { err = "unreadable number"; return std::nullopt; }
        return after_number(Quantity{v, 0});
    }

    std::optional<Quantity> after_number(Quantity q) {
        const std::size_t save = i;
        skip();
        if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])))) {
            if (s.substr(i, 4) != "sqrt") {
                if (const auto u = unit_word()) {
                    if (q.dim != 0) { err = "a unit on a value that already has one"; return std::nullopt; }
                    return Quantity{q.v * u->v, u->dim};
                }
                i = save;
            }
        }
        return q;
    }

    std::optional<Quantity> term() {
        auto a = factor();
        if (!a) return a;
        for (;;) {
            skip();
            if (eat('*')) {
                auto b = factor();
                if (!b) return b;
                a->v *= b->v;
                a->dim += b->dim;
            } else if (eat('/')) {
                auto b = factor();
                if (!b) return b;
                if (b->v == 0.0) { err = "division by zero"; return std::nullopt; }
                a->v /= b->v;
                a->dim -= b->dim;
            } else {
                return a;
            }
        }
    }

    std::optional<Quantity> expr() {
        auto a = term();
        if (!a) return a;
        for (;;) {
            skip();
            if (eat('+')) {
                auto b = term();
                if (!b) return b;
                if (a->dim != b->dim) { err = "adding values of different units"; return std::nullopt; }
                a->v += b->v;
            } else if (at('-') && s[i] == '-') {
                ++i;
                auto b = term();
                if (!b) return b;
                if (a->dim != b->dim) { err = "subtracting values of different units"; return std::nullopt; }
                a->v -= b->v;
            } else {
                return a;
            }
        }
    }
};

}

inline std::optional<Quantity> calc(std::string_view text, std::string& err) {
    detail::CalcParser p{text};
    auto q = p.expr();
    p.skip();
    if (q && p.i != p.s.size()) {
        err = "unread input at '" + std::string(p.s.substr(p.i)) + "'";
        return std::nullopt;
    }
    if (!q) err = p.err.empty() ? "unreadable expression" : p.err;
    return q;
}

struct FootprintSpec {
    f64         area{0.0};          // m², required
    std::string style{"rectangle"};
    f64         aspect{1.4};        // width / depth
    f64         grid{0.1524};       // snap increment (6 in); 0 = none
    f64         lot_w{0.0}, lot_d{0.0};   // 0 = no lot given
    f64         front_setback{0.0}, side_setback{0.0}, rear_setback{0.0};
};

struct Footprint {
    std::vector<std::pair<f64, f64>> pts;   // closed CCW; last != first
    f64         area{0.0};
    f64         width{0.0}, depth{0.0};
    std::string note;
};

namespace detail {
inline f64 snap(f64 v, f64 g) {
    if (g <= 0.0) return v;
    return std::max(g, std::round(v / g) * g);
}
}

inline std::optional<Footprint> footprint(const FootprintSpec& spec, std::string& err) {
    if (spec.area <= 0.0) { err = "the target area must be positive"; return std::nullopt; }
    const f64 aspect = std::clamp(spec.aspect <= 0.0 ? 1.4 : spec.aspect, 0.2, 5.0);
    const f64 g = spec.grid;

    f64 max_w = 0.0, max_d = 0.0;
    if (spec.lot_w > 0.0 && spec.lot_d > 0.0) {
        max_w = spec.lot_w - 2.0 * spec.side_setback;
        max_d = spec.lot_d - spec.front_setback - spec.rear_setback;
        if (max_w <= 0.0 || max_d <= 0.0) { err = "the setbacks leave no buildable width"; return std::nullopt; }
        if (max_w * max_d < spec.area) { err = "the buildable area is smaller than the target"; return std::nullopt; }
    }

    f64 w0 = std::sqrt(spec.area * aspect);
    if (max_w > 0.0) w0 = std::min(w0, max_w);
    const f64 wcap = max_w > 0.0 ? (g > 0.0 ? std::floor(max_w / g) * g : max_w) : 1e300;
    const f64 dcap = max_d > 0.0 ? (g > 0.0 ? std::floor(max_d / g) * g : max_d) : 1e300;
    f64 best_w = 0.0, best_d = 0.0, best_off = 1e300;
    for (int k = -3; k <= 3; ++k) {
        f64 w = detail::snap(w0 + f64(k) * (g > 0.0 ? g : w0 * 0.02), g);
        if (w <= 0.0) continue;
        w = std::min(w, wcap);
        f64 d = std::min(detail::snap(spec.area / w, g), dcap);
        if (d <= 0.0) continue;
        const f64 off = std::fabs(w * d - spec.area);
        if (off < best_off) { best_off = off; best_w = w; best_d = d; }
    }
    if (best_w <= 0.0) { err = "no snapped rectangle reaches the target area"; return std::nullopt; }

    std::string style = spec.style;
    for (char& c : style) c = char(std::tolower(static_cast<unsigned char>(c)));

    Footprint out;
    f64 W = best_w, D = best_d;

    if (style == "l" || style == "t" || style == "u") {
        const f64 step = g > 0.0 ? g : W * 0.02;
        W = std::min(detail::snap(W + 2.0 * step, g), wcap);
        D = std::min(detail::snap(spec.area * (style == "u" ? 1.15 : 1.08) / W, g), dcap);
        f64 excess = W * D - spec.area;
        if (excess <= 0.0) { style = "rectangle"; W = best_w; D = best_d; }
        else {
            const int notches = style == "l" ? 1 : (style == "t" ? 2 : 1);
            f64 nw = detail::snap(W / 3.0, g);
            if (style == "u") nw = detail::snap(W / 2.5, g);
            f64 nd = detail::snap(excess / (nw * f64(notches)), g);
            nd = std::clamp(nd, g > 0.0 ? g : 0.1, D - (g > 0.0 ? g : 0.1));
            const f64 x0 = 0.0, y0 = 0.0;
            if (style == "l") {
                out.pts = {{x0, y0}, {W, y0}, {W, D - nd}, {W - nw, D - nd},
                           {W - nw, D}, {x0, D}};
            } else if (style == "t") {
                out.pts = {{x0, y0}, {W, y0}, {W, D - nd}, {W - nw, D - nd},
                           {W - nw, D}, {nw, D}, {nw, D - nd}, {x0, D - nd}};
            } else {
                const f64 nx = detail::snap((W - nw) * 0.5, g);
                out.pts = {{x0, y0},           {W, y0},
                           {W, D},             {nx + nw, D},
                           {nx + nw, D - nd},  {nx, D - nd},
                           {nx, D},            {x0, D}};
            }
        }
    }
    if (out.pts.empty())
        out.pts = {{0.0, 0.0}, {W, 0.0}, {W, D}, {0.0, D}};

    f64 a2 = 0.0;
    for (std::size_t i = 0; i < out.pts.size(); ++i) {
        const auto& p = out.pts[i];
        const auto& q = out.pts[(i + 1) % out.pts.size()];
        a2 += p.first * q.second - q.first * p.second;
    }
    out.area = std::fabs(a2) * 0.5;
    out.width = W;
    out.depth = D;

    if (spec.lot_w > 0.0 && spec.lot_d > 0.0) {
        const f64 ox = (spec.lot_w - W) * 0.5;
        const f64 oy = spec.front_setback;
        for (auto& p : out.pts) { p.first += ox; p.second += oy; }
        out.note = "placed on the lot: centred, front setback " +
                   std::to_string(spec.front_setback) + " m";
    }
    return out;
}

}
