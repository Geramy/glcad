#include "engine/linetype.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace gdoc {

f64 Linetype::cycle_length() const noexcept {
    f64 t = 0.0;
    for (const auto& d : pattern) t += std::fabs(d.length);
    return t;
}

bool Linetype::draws_at(f64 d, f64 scale) const noexcept {
    if (solid()) return true;
    const f64 cycle = cycle_length() * scale;
    if (cycle <= 0.0) return true;

    f64 pos = std::fmod(d, cycle);
    if (pos < 0.0) pos += cycle;

    for (const auto& e : pattern) {
        const f64 len = std::fabs(e.length) * scale;
        if (pos < len) return e.length >= 0.0;   // dots (0) draw
        pos -= len;
    }
    return true;
}

std::vector<std::pair<f64, f64>> Linetype::spans(f64 length, f64 scale,
                                                 f64 dot) const {
    std::vector<std::pair<f64, f64>> out;
    if (length <= 0.0) return out;
    const f64 cycle = cycle_length() * scale;
    // Zero-length or collapsed patterns draw solid, or this never advances.
    if (solid() || cycle <= 1e-12) {
        out.push_back({0.0, length});
        return out;
    }

    f64 at = 0.0;
    // Past the segment cap the line draws solid.
    const std::size_t kMaxSpans = 20000;
    while (at < length && out.size() < kMaxSpans) {
        for (const auto& e : pattern) {
            if (at >= length) break;
            const f64 len = std::fabs(e.length) * scale;
            if (e.length > 0.0) {
                out.push_back({at, std::min(at + len, length)});
            } else if (e.length == 0.0 && dot > 0.0) {
                const f64 half = dot * 0.5;
                out.push_back({std::max(at - half, 0.0), std::min(at + half, length)});
            }
            at += len;
        }
    }
    if (out.size() >= kMaxSpans) return {{0.0, length}};
    return out;
}

LinetypeTable::LinetypeTable() {
    add({"Continuous", "Solid line", {}});
    add({"Center",  "Centre ____ _ ____ _ ____", {{1.25}, {-0.25}, {0.25}, {-0.25}}});
    add({"Dashed",  "Dashed __ __ __ __",        {{0.5},  {-0.25}}});
    add({"Hidden",  "Hidden __ __ __ __",        {{0.25}, {-0.125}}});
    add({"Phantom", "Phantom ____ _ _ ____",     {{1.25}, {-0.25}, {0.25}, {-0.25}, {0.25}, {-0.25}}});
    add({"Dashdot", "Dash dot __ . __ . __",     {{0.5},  {-0.25}, {0.0},  {-0.25}}});
    add({"Divide",  "Divide ____ . . ____",      {{0.5},  {-0.25}, {0.0},  {-0.25}, {0.0}, {-0.25}}});
    add({"Border",  "Border __ __ . __ __ .",    {{0.5},  {-0.25}, {0.5},  {-0.25}, {0.0}, {-0.25}}});
    add({"Dot",     "Dot . . . . . . . .",       {{0.0},  {-0.25}}});
}

const Linetype* LinetypeTable::find(std::string_view name) const {
    for (const auto& t : types_)
        if (t.name.size() == name.size() &&
            std::equal(t.name.begin(), t.name.end(), name.begin(),
                       [](char a, char b) {
                           return std::tolower((unsigned char)a) ==
                                  std::tolower((unsigned char)b);
                       }))
            return &t;
    return nullptr;
}

Linetype& LinetypeTable::add(Linetype lt) {
    for (auto& t : types_)
        if (t.name == lt.name) { t = std::move(lt); return t; }
    types_.push_back(std::move(lt));
    return types_.back();
}

std::size_t LinetypeTable::load_lin(std::string_view text) {
    std::size_t i = 0, loaded = 0;
    std::string pending_name, pending_desc;

    auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
        return s;
    };

    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string_view::npos) e = text.size();
        std::string_view line = trim(text.substr(i, e - i));
        i = e + 1;
        if (line.empty() || line.front() == ';') continue;

        if (line.front() == '*') {
            line.remove_prefix(1);
            const std::size_t c = line.find(',');
            pending_name = std::string(trim(c == std::string_view::npos ? line
                                                                       : line.substr(0, c)));
            pending_desc = c == std::string_view::npos
                               ? std::string{}
                               : std::string(trim(line.substr(c + 1)));
            continue;
        }

        if (pending_name.empty()) continue;
        if (line.front() != 'A' && line.front() != 'a') continue;

        Linetype lt;
        lt.name = pending_name;
        lt.description = pending_desc;

        std::size_t p = line.find(',');
        while (p != std::string_view::npos) {
            std::size_t q = line.find(',', p + 1);
            const std::string_view tok =
                trim(line.substr(p + 1, (q == std::string_view::npos ? line.size() : q) - p - 1));
            if (!tok.empty() && tok.front() != '[')
                lt.pattern.push_back({std::atof(std::string(tok).c_str()), "", 1.0});
            p = q;
        }
        add(std::move(lt));
        ++loaded;
        pending_name.clear();
    }
    return loaded;
}

f64 MultilineStyle::max_offset() const noexcept {
    f64 m = 0.0;
    for (const auto& e : elements) m = std::max(m, e.offset);
    return m;
}
f64 MultilineStyle::min_offset() const noexcept {
    f64 m = 0.0;
    for (const auto& e : elements) m = std::min(m, e.offset);
    return m;
}
f64 MultilineStyle::width() const noexcept { return max_offset() - min_offset(); }

MultilineTable::MultilineTable() {
    auto elem = [](f64 off) {
        MultilineElement e;
        e.offset = off;
        return e;
    };

    add({"Standard", "Two lines, 1 unit apart",
         {elem(0.5), elem(-0.5)}, false, Color::aci(8), EndCap::None, EndCap::None, false});

    add({"Wall-100", "100 mm internal partition",
         {elem(0.05), elem(-0.05)}, true, Color::aci(9),
         EndCap::Line, EndCap::Line, false});

    add({"Wall-150", "150 mm stud wall",
         {elem(0.075), elem(-0.075)}, true, Color::aci(9),
         EndCap::Line, EndCap::Line, false});

    add({"Wall-200", "200 mm loadbearing wall",
         {elem(0.10), elem(-0.10)}, true, Color::aci(9),
         EndCap::Line, EndCap::Line, false});

    add({"Wall-Cavity-300", "Cavity wall: 100 outer, 100 cavity, 100 inner",
         {elem(0.15), elem(0.05), elem(-0.05), elem(-0.15)}, true, Color::aci(9),
         EndCap::Line, EndCap::Line, false});

    add({"Pony-Wall-900", "900 mm high pony wall, capped",
         {elem(0.05), elem(-0.05)}, true, Color::aci(31),
         EndCap::Line, EndCap::Line, true});

    add({"Window", "Window opening with glazing line",
         {elem(0.05), elem(0.015), elem(-0.015), elem(-0.05)}, false, Color::aci(4),
         EndCap::Line, EndCap::Line, false});

    add({"Door", "Door opening, jambs only",
         {elem(0.05), elem(-0.05)}, false, Color::aci(3),
         EndCap::Line, EndCap::Line, false});

    add({"Curtain-Wall", "Glazed curtain wall",
         {elem(0.03), elem(-0.03)}, true, Color::aci(4),
         EndCap::Line, EndCap::Line, true});

    add({"Duct-Round", "Round duct, centreline and walls",
         {elem(0.15), elem(0.0), elem(-0.15)}, false, Color::aci(6),
         EndCap::OuterArc, EndCap::OuterArc, false});
}

const MultilineStyle* MultilineTable::find(std::string_view name) const {
    for (const auto& s : styles_)
        if (s.name == name) return &s;
    return nullptr;
}

MultilineStyle& MultilineTable::add(MultilineStyle s) {
    std::sort(s.elements.begin(), s.elements.end(),
              [](const MultilineElement& a, const MultilineElement& b) {
                  return a.offset > b.offset;
              });
    for (auto& e : styles_)
        if (e.name == s.name) { e = std::move(s); return e; }
    styles_.push_back(std::move(s));
    return styles_.back();
}

std::vector<f64> resolve_offsets(const MultilineStyle& s, Justify j) {
    f64 shift = 0.0;
    switch (j) {
        case Justify::Top:    shift = -s.max_offset(); break;
        case Justify::Bottom: shift = -s.min_offset(); break;
        case Justify::Zero:   shift = 0.0; break;
    }
    std::vector<f64> out;
    out.reserve(s.elements.size());
    for (const auto& e : s.elements) out.push_back(e.offset + shift);
    return out;
}

}
