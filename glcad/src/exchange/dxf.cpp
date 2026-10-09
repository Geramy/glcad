#include "exchange/dxf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>

namespace gdxf {
namespace {

constexpr f64 kPi = 3.14159265358979323846;
constexpr f64 kEps = 1e-12;

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

std::string upper(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return o;
}

std::string number(f64 v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10f", v);
    std::string s = buf;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s += "0";
    return s;
}

f64 normalise_angle(f64 a) {
    while (a < 0.0) a += 2.0 * kPi;
    while (a >= 2.0 * kPi) a -= 2.0 * kPi;
    return a;
}

}

bool Group::as_int(i32& out) const {
    try {
        out = static_cast<i32>(std::stol(value));
        return true;
    } catch (...) {
        return false;
    }
}

bool Group::as_real(f64& out) const {
    try {
        out = std::stod(value);
        return true;
    } catch (...) {
        return false;
    }
}

const char* units_name(Units u) noexcept {
    switch (u) {
        case Units::Unspecified: return "unspecified";
        case Units::Inches:      return "inches";
        case Units::Feet:        return "feet";
        case Units::Miles:       return "miles";
        case Units::Millimetres: return "millimetres";
        case Units::Centimetres: return "centimetres";
        case Units::Metres:      return "metres";
        case Units::Kilometres:  return "kilometres";
        case Units::Microinches: return "microinches";
        case Units::Mils:        return "mils";
        case Units::Yards:       return "yards";
        case Units::Angstroms:   return "angstroms";
        case Units::Nanometres:  return "nanometres";
        case Units::Microns:     return "microns";
        case Units::Decimetres:  return "decimetres";
        case Units::Decametres:  return "decametres";
        case Units::Hectometres: return "hectometres";
        case Units::Gigametres:  return "gigametres";
    }
    return "unspecified";
}

f64 metres_per(Units u) noexcept {
    switch (u) {
        case Units::Unspecified: return 0.0;
        case Units::Inches:      return 0.0254;
        case Units::Feet:        return 0.3048;
        case Units::Miles:       return 1609.344;
        case Units::Millimetres: return 0.001;
        case Units::Centimetres: return 0.01;
        case Units::Metres:      return 1.0;
        case Units::Kilometres:  return 1000.0;
        case Units::Microinches: return 0.0254e-6;
        case Units::Mils:        return 0.0254e-3;
        case Units::Yards:       return 0.9144;
        case Units::Angstroms:   return 1e-10;
        case Units::Nanometres:  return 1e-9;
        case Units::Microns:     return 1e-6;
        case Units::Decimetres:  return 0.1;
        case Units::Decametres:  return 10.0;
        case Units::Hectometres: return 100.0;
        case Units::Gigametres:  return 1e9;
    }
    return 0.0;
}

Bulge arc_from_bulge(f64 x0, f64 y0, f64 x1, f64 y1, f64 bulge) {
    Bulge out;
    if (std::fabs(bulge) < 1e-12) return out;      // a straight segment

    const f64 dx = x1 - x0, dy = y1 - y0;
    const f64 chord = std::hypot(dx, dy);
    if (chord < kEps) return out;

    const f64 included = 4.0 * std::atan(std::fabs(bulge));
    const f64 radius = chord / (2.0 * std::sin(included * 0.5));
    if (!(radius > 0.0) || !std::isfinite(radius)) return out;

    const f64 sagitta_side = (bulge > 0.0) ? 1.0 : -1.0;
    const f64 mx = (x0 + x1) * 0.5, my = (y0 + y1) * 0.5;
    const f64 h = std::sqrt(std::max(0.0, radius * radius - chord * chord * 0.25));
    const f64 ux = -dy / chord, uy = dx / chord;
    const f64 toward = (std::fabs(bulge) > 1.0) ? -1.0 : 1.0;   // major arc
    out.cx = mx + ux * h * sagitta_side * toward;
    out.cy = my + uy * h * sagitta_side * toward;
    out.radius = radius;

    out.start_angle = std::atan2(y0 - out.cy, x0 - out.cx);
    out.end_angle = std::atan2(y1 - out.cy, x1 - out.cx);
    // A negative bulge is the same arc clockwise, stored by swapping the ends.
    if (bulge < 0.0) std::swap(out.start_angle, out.end_angle);
    out.start_angle = normalise_angle(out.start_angle);
    out.end_angle = normalise_angle(out.end_angle);
    out.is_arc = true;
    return out;
}

f64 bulge_from_arc(f64 start_angle, f64 end_angle) {
    f64 sweep = end_angle - start_angle;
    while (sweep <= 0.0) sweep += 2.0 * kPi;
    while (sweep > 2.0 * kPi) sweep -= 2.0 * kPi;
    return std::tan(sweep * 0.25);
}

Tape lex(std::string_view text) {
    Tape out;

    static constexpr char kBinary[] = "AutoCAD Binary DXF";
    if (text.size() >= sizeof(kBinary) - 1 &&
        text.compare(0, sizeof(kBinary) - 1, kBinary) == 0) {
        out.error = "this is a binary DXF; only the text form is supported";
        return out;
    }

    std::vector<std::string> lines;
    {
        std::string current;
        for (char c : text) {
            if (c == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty()) lines.push_back(current);
    }

    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        const std::string code_text = trim(lines[i]);
        if (code_text.empty()) {
            i -= 1;
            continue;
        }
        Group g;
        g.line = i + 1;
        bool numeric = !code_text.empty();
        for (std::size_t k = 0; k < code_text.size(); ++k) {
            const char ch = code_text[k];
            if (!(std::isdigit(static_cast<unsigned char>(ch)) || (k == 0 && (ch == '-' || ch == '+')))) { numeric = false; break; }
        }
        if (!numeric) {
            ++out.junk_lines;
            i -= 1;
            continue;
        }
        g.code = static_cast<i32>(std::stol(code_text));
        g.value = trim(lines[i + 1]);
        out.groups.push_back(std::move(g));
    }

    if (out.groups.empty()) {
        out.error = "the file contains no group codes";
        return out;
    }
    out.ok = true;
    return out;
}

}
