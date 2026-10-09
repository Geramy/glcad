#include "engine/units.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gunit {

const char* suffix(Length u) noexcept {
    switch (u) {
        case Length::Metre:      return "m";
        case Length::Millimetre: return "mm";
        case Length::Centimetre: return "cm";
        case Length::Kilometre:  return "km";
        case Length::Micrometre: return "um";
        case Length::Inch:       return "\"";
        case Length::Foot:       return "ft";
        case Length::Yard:       return "yd";
        case Length::Mile:       return "mi";
        case Length::Mil:        return "mil";
    }
    return "";
}

const char* name(Length u) noexcept {
    switch (u) {
        case Length::Metre:      return "metres";
        case Length::Millimetre: return "millimetres";
        case Length::Centimetre: return "centimetres";
        case Length::Kilometre:  return "kilometres";
        case Length::Micrometre: return "micrometres";
        case Length::Inch:       return "inches";
        case Length::Foot:       return "feet";
        case Length::Yard:       return "yards";
        case Length::Mile:       return "miles";
        case Length::Mil:        return "mils";
    }
    return "";
}

std::optional<Length> length_from_suffix(std::string_view s) noexcept {
    if (s == "m")                        return Length::Metre;
    if (s == "mm")                       return Length::Millimetre;
    if (s == "cm")                       return Length::Centimetre;
    if (s == "km")                       return Length::Kilometre;
    if (s == "um" || s == "\xc2\xb5m")   return Length::Micrometre;
    if (s == "\"" || s == "in")          return Length::Inch;
    if (s == "'" || s == "ft")           return Length::Foot;
    if (s == "yd")                       return Length::Yard;
    if (s == "mi")                       return Length::Mile;
    if (s == "mil")                      return Length::Mil;
    return std::nullopt;
}

static std::string trim_zeros(std::string s) {
    if (s.find('.') == std::string::npos) return s;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

static void reduce(long long& n, long long& d) {
    long long a = n < 0 ? -n : n, b = d;
    while (b) { const long long t = a % b; a = b; b = t; }
    if (a > 1) { n /= a; d /= a; }
}

// value in inches -> "6 1/2" style, at 1/2^precision resolution.
static std::string inches_fraction(f64 inches, i32 precision, bool& negative) {
    negative = inches < 0;
    inches = std::fabs(inches);

    long long den = 1LL << std::clamp(precision, 0, 8);
    long long whole = static_cast<long long>(std::floor(inches));
    long long num = llround((inches - static_cast<f64>(whole)) * static_cast<f64>(den));
    if (num == den) { ++whole; num = 0; }

    std::string out = std::to_string(whole);
    if (num > 0) {
        reduce(num, den);
        out += " " + std::to_string(num) + "/" + std::to_string(den);
    }
    return out;
}

std::string format_length(f64 metres, const Format& f) {
    char buf[128];

    switch (f.style) {
        case Style::Scientific: {
            std::snprintf(buf, sizeof(buf), "%.*E", f.precision, from_metres(metres, f.unit));
            std::string s = buf;
            if (f.show_unit) s += suffix(f.unit);
            return s;
        }

        case Style::Engineering: {
            const bool neg = metres < 0;
            const f64 total_in = std::fabs(from_metres(metres, Length::Inch));
            const long long feet = static_cast<long long>(std::floor(total_in / 12.0));
            const f64 inch = total_in - static_cast<f64>(feet) * 12.0;
            std::snprintf(buf, sizeof(buf), "%s%lld'-%.*f\"", neg ? "-" : "", feet,
                          f.precision, inch);
            return buf;
        }

        case Style::Architectural: {
            const bool neg = metres < 0;
            const f64 total_in = std::fabs(from_metres(metres, Length::Inch));
            long long feet = static_cast<long long>(std::floor(total_in / 12.0));
            f64 inch = total_in - static_cast<f64>(feet) * 12.0;

            bool dummy = false;
            std::string frac = inches_fraction(inch, f.precision, dummy);
            // Rounding can push inches to 12; carry into feet.
            if (frac.rfind("12", 0) == 0 && (frac.size() == 2 || frac[2] == ' ')) {
                ++feet;
                frac = "0";
            }
            std::string s = (neg ? "-" : "") + std::to_string(feet) + "'-" + frac + "\"";
            return s;
        }

        case Style::Fractional: {
            bool neg = false;
            const f64 v = from_metres(metres, f.unit);
            std::string s = inches_fraction(v, f.precision, neg);
            if (neg) s = "-" + s;
            if (f.show_unit) s += suffix(f.unit);
            return s;
        }

        case Style::Decimal:
        default: {
            std::snprintf(buf, sizeof(buf), "%.*f", f.precision, from_metres(metres, f.unit));
            std::string s = buf;
            if (f.suppress_trailing_zeros) s = trim_zeros(s);
            if (f.show_unit) s += suffix(f.unit);
            return s;
        }
    }
}

std::string format_angle(f64 radians, const AngleFormat& f) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", f.precision, from_radians(radians, f.unit));
    std::string s = buf;
    if (f.show_unit) {
        switch (f.unit) {
            case Angle::Degree:  s += "\xc2\xb0"; break;   // degree sign
            case Angle::Gradian: s += "g"; break;
            case Angle::Radian:  s += " rad"; break;
        }
    }
    return s;
}

namespace {

struct Parser {
    std::string_view s;
    size_t           i{0};
    Length           def;
    bool             ok{true};
    // Explicit unit: already metres, do not scale by the default again.
    bool             absolute{false};

    void skip() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eof() { skip(); return i >= s.size(); }
    char peek() { skip(); return i < s.size() ? s[i] : '\0'; }

    std::optional<Length> read_suffix() {
        skip();
        // Longest match first so "mil" is not read as "mi".
        static const char* toks[] = {"mil", "mm", "cm", "km", "um", "yd", "mi",
                                     "in", "ft", "m", "\"", "'"};
        for (const char* t : toks) {
            const size_t n = std::char_traits<char>::length(t);
            if (s.size() - i >= n && s.compare(i, n, t) == 0) {
                // Do not swallow a letter that begins a longer word.
                if (std::isalpha((unsigned char)t[0]) && i + n < s.size() &&
                    std::isalpha((unsigned char)s[i + n]))
                    continue;
                i += n;
                return length_from_suffix(t);
            }
        }
        return std::nullopt;
    }

    f64 number() {
        skip();
        const size_t start = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) ++i;
        if (i == start) { ok = false; return 0.0; }
        return std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
    }

    // primary := number [unit] [number '/' number] | '(' expr ')'
    f64 primary() {
        skip();
        if (peek() == '(') {
            ++i;
            const f64 v = expr();
            if (peek() == ')') ++i; else ok = false;
            return v;
        }
        if (peek() == '-') { ++i; return -primary(); }
        if (peek() == '+') { ++i; return primary(); }

        f64 v = number();
        if (!ok) return 0.0;

        auto unit = read_suffix();

        if (unit && *unit == Length::Foot) {
            f64 m = to_metres(v, Length::Foot);
            skip();
            if (i < s.size() && s[i] == '-') ++i;
            skip();
            if (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) {
                const f64 in = number();
                f64 extra = in;
                skip();
                if (i < s.size() && (std::isdigit((unsigned char)s[i]))) {
                    const size_t save = i;
                    const f64 n2 = number();
                    skip();
                    if (i < s.size() && s[i] == '/') {
                        ++i;
                        const f64 d2 = number();
                        if (d2 != 0.0) extra += n2 / d2;
                    } else { i = save; }
                } else if (i < s.size() && s[i] == '/') {
                    ++i;
                    const f64 d2 = number();
                    if (d2 != 0.0) extra = in / d2;
                }
                skip();
                if (i < s.size() && s[i] == '"') ++i;
                m += to_metres(extra, Length::Inch);
            }
            absolute = true;
            return m;
        }

        skip();
        if (i < s.size() && s[i] == '/' && !unit) {
            const size_t save = i;
            ++i;
            const f64 d = number();
            if (ok && d != 0.0) {
                const f64 q = v / d;
                if (auto u2 = read_suffix()) { absolute = true; return to_metres(q, *u2); }
                return q;
            }
            i = save;
            ok = true;
        }

        if (unit) { absolute = true; return to_metres(v, *unit); }
        return v;
    }

    f64 term() {
        f64 v = primary();
        for (;;) {
            const char c = peek();
            if (c == '*') { ++i; v *= primary(); }
            else if (c == '/') { ++i; const f64 d = primary(); if (d == 0.0) { ok = false; return 0.0; } v /= d; }
            else return v;
        }
    }

    f64 expr() {
        f64 v = term();
        for (;;) {
            const char c = peek();
            if (c == '+') { ++i; v += term(); }
            else if (c == '-') { ++i; v -= term(); }
            else return v;
        }
    }
};

}

std::optional<f64> parse_length(std::string_view text, Length default_unit) {
    if (text.empty()) return std::nullopt;
    Parser p{text, 0, default_unit};
    const f64 v = p.expr();
    if (!p.ok || !p.eof()) return std::nullopt;
    return p.absolute ? v : to_metres(v, default_unit);
}

std::optional<f64> parse_angle(std::string_view text, Angle default_unit) {
    if (text.empty()) return std::nullopt;
    std::string t(text);
    Angle u = default_unit;
    auto ends = [&](const char* suf) {
        const size_t n = std::char_traits<char>::length(suf);
        return t.size() >= n && t.compare(t.size() - n, n, suf) == 0;
    };
    if (ends("\xc2\xb0")) { u = Angle::Degree; t.resize(t.size() - 2); }
    else if (ends("deg")) { u = Angle::Degree; t.resize(t.size() - 3); }
    else if (ends("rad")) { u = Angle::Radian; t.resize(t.size() - 3); }
    else if (ends("g"))   { u = Angle::Gradian; t.resize(t.size() - 1); }

    Parser p{t, 0, Length::Metre};
    const f64 v = p.expr();
    if (!p.ok || !p.eof()) return std::nullopt;
    return to_radians(v, u);
}

}
