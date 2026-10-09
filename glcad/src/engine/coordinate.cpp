#include "engine/coordinate.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace gcmd {

namespace {

constexpr f64 kPi = 3.14159265358979323846;

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// Split on commas outside brackets: `(2+3),4` is two fields.
std::vector<std::string_view> fields(std::string_view s) {
    std::vector<std::string_view> out;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')') --depth;
        else if (s[i] == ',' && depth == 0) {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

}

void Coordinate::resolve(f64 last_x, f64 last_y, f64 last_z, f64 direction,
                         f64& out_x, f64& out_y, f64& out_z) const {
    const bool rel = relative() && !world_override;
    const f64 bx = rel ? last_x : 0.0;
    const f64 by = rel ? last_y : 0.0;
    const f64 bz = rel ? last_z : 0.0;

    switch (form) {
        case CoordForm::AbsoluteCartesian:
        case CoordForm::RelativeCartesian:
            out_x = bx + x;
            out_y = by + y;
            out_z = bz + (has_z ? z : 0.0);
            return;

        case CoordForm::AbsolutePolar:
        case CoordForm::RelativePolar:
            out_x = bx + distance * std::cos(angle);
            out_y = by + distance * std::sin(angle);
            out_z = bz + (has_z ? z : 0.0);
            return;

        case CoordForm::DistanceOnly:
            out_x = last_x + distance * std::cos(direction);
            out_y = last_y + distance * std::sin(direction);
            out_z = last_z;
            return;

        default:
            out_x = last_x;
            out_y = last_y;
            out_z = last_z;
            return;
    }
}

std::optional<Coordinate> parse_coordinate(std::string_view text,
                                           gunit::Length unit) {
    std::string_view s = trim(text);
    if (s.empty()) return std::nullopt;

    Coordinate c;

    if (s.front() == '*') {
        c.world_override = true;
        s.remove_prefix(1);
        s = trim(s);
    }
    bool rel = false;
    if (!s.empty() && s.front() == '@') {
        rel = true;
        s.remove_prefix(1);
        s = trim(s);
    }
    if (s.empty()) return std::nullopt;

    std::size_t lt = std::string_view::npos;
    {
        int depth = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '(') ++depth;
            else if (s[i] == ')') --depth;
            else if (s[i] == '<' && depth == 0) { lt = i; break; }
        }
    }

    if (lt != std::string_view::npos) {
        const auto d = gunit::parse_length(trim(s.substr(0, lt)), unit);
        if (!d) return std::nullopt;

        const auto rest = fields(trim(s.substr(lt + 1)));
        if (rest.empty() || rest[0].empty()) return std::nullopt;

        const auto a = gunit::parse_angle(rest[0], gunit::Angle::Degree);
        if (!a) return std::nullopt;

        c.form = rel ? CoordForm::RelativePolar : CoordForm::AbsolutePolar;
        c.distance = *d;
        c.angle = *a;
        if (rest.size() > 1 && !rest[1].empty()) {
            const auto z = gunit::parse_length(rest[1], unit);
            if (!z) return std::nullopt;
            c.z = *z;
            c.has_z = true;
        }
        return c;
    }

    const auto parts = fields(s);
    if (parts.size() == 1) {
        const auto d = gunit::parse_length(parts[0], unit);
        if (!d) return std::nullopt;
        c.form = CoordForm::DistanceOnly;
        c.distance = *d;
        return c;
    }

    if (parts.size() > 3) return std::nullopt;

    const auto x = gunit::parse_length(parts[0], unit);
    const auto y = gunit::parse_length(parts[1], unit);
    if (!x || !y) return std::nullopt;

    c.form = rel ? CoordForm::RelativeCartesian : CoordForm::AbsoluteCartesian;
    c.x = *x;
    c.y = *y;
    if (parts.size() == 3) {
        const auto z = gunit::parse_length(parts[2], unit);
        if (!z) return std::nullopt;
        c.z = *z;
        c.has_z = true;
    }
    return c;
}

namespace {

std::string number(f64 metres, gunit::Length unit, int decimals) {
    const f64 shown = metres / gunit::metres_per(unit);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, shown);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return (s.empty() ? "0" : s) + gunit::suffix(unit);
}

}

std::string format_coordinate(f64 x, f64 y, gunit::Length unit, int decimals) {
    return number(x, unit, decimals) + "," + number(y, unit, decimals);
}

std::string format_relative(f64 dx, f64 dy, gunit::Length unit, int decimals) {
    return "@" + number(dx, unit, decimals) + "," + number(dy, unit, decimals);
}

std::string format_polar(f64 distance, f64 angle_rad, gunit::Length unit,
                         int decimals) {
    f64 deg = angle_rad * 180.0 / kPi;
    while (deg < 0.0) deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, deg);
    std::string a = buf;
    if (a.find('.') != std::string::npos) {
        while (!a.empty() && a.back() == '0') a.pop_back();
        if (!a.empty() && a.back() == '.') a.pop_back();
    }
    return "@" + number(distance, unit, decimals) + "<" + a;
}

}
