#include "exchange/step.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace gstep {
namespace {

constexpr f64 kEps = 1e-12;

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && is_space(s[a])) ++a;
    while (b > a && is_space(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

std::string upper(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

std::string number(f64 v) {
    // Every real needs a decimal point; "1" is an INTEGER in part 21.
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    std::string s = buf;
    if (s.find('.') == std::string::npos && s.find('E') == std::string::npos &&
        s.find('e') == std::string::npos)
        s += ".";
    return s;
}

}

const char* length_unit_name(LengthUnit u) noexcept {
    switch (u) {
        case LengthUnit::Metre:      return "metre";
        case LengthUnit::Millimetre: return "millimetre";
        case LengthUnit::Centimetre: return "centimetre";
        case LengthUnit::Inch:       return "inch";
        case LengthUnit::Foot:       return "foot";
    }
    return "";
}

f64 metres_per(LengthUnit u) noexcept {
    switch (u) {
        case LengthUnit::Metre:      return 1.0;
        case LengthUnit::Millimetre: return 0.001;
        case LengthUnit::Centimetre: return 0.01;
        case LengthUnit::Inch:       return 0.0254;
        case LengthUnit::Foot:       return 0.3048;
    }
    return 1.0;
}

std::string encode_string(std::string_view s) {
    // Escape codepoints, not UTF-8 bytes.
    auto next = [&](std::size_t& i, unsigned& cp) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n = 1;
        if ((c & 0xE0u) == 0xC0u) n = 2;
        else if ((c & 0xF0u) == 0xE0u) n = 3;
        else if ((c & 0xF8u) == 0xF0u) n = 4;
        if (i + n > s.size()) n = 1;

        if (n == 1) { cp = c; }
        else {
            cp = c & (0xFFu >> (n + 1));
            for (std::size_t k = 1; k < n; ++k) {
                const unsigned char t = static_cast<unsigned char>(s[i + k]);
                if ((t & 0xC0u) != 0x80u) { cp = c; n = 1; break; }
                cp = (cp << 6) | (t & 0x3Fu);
            }
        }
        i += n;
    };

    std::string out = "'";
    bool in_escape = false;
    for (std::size_t i = 0; i < s.size();) {
        unsigned cp = 0;
        next(i, cp);

        if (cp >= 0x20 && cp < 0x7F && cp != '\'' && cp != '\\') {
            // Close the \X2\ run before ordinary text resumes.
            if (in_escape) { out += "\\X0\\"; in_escape = false; }
            out += static_cast<char>(cp);
            continue;
        }
        if (cp == '\'') {
            if (in_escape) { out += "\\X0\\"; in_escape = false; }
            out += "''";        // doubled: part 21's only rule for apostrophes
            continue;
        }
        if (cp == '\\') {
            if (in_escape) { out += "\\X0\\"; in_escape = false; }
            out += "\\\\";
            continue;
        }

        if (!in_escape) { out += "\\X2\\"; in_escape = true; }
        char buf[16];
        if (cp <= 0xFFFF) {
            std::snprintf(buf, sizeof buf, "%04X", cp);
            out += buf;
        } else {
            // Beyond the BMP: a surrogate pair; the run is 16-bit code units.
            const unsigned u = cp - 0x10000;
            std::snprintf(buf, sizeof buf, "%04X%04X", 0xD800 + (u >> 10),
                          0xDC00 + (u & 0x3FF));
            out += buf;
        }
    }
    if (in_escape) out += "\\X0\\";
    return out + "'";
}

std::string decode_string(std::string_view s) {
    std::string_view v = s;
    if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'')
        v = v.substr(1, v.size() - 2);

    auto hex = [](char c, unsigned& out) {
        if (c >= '0' && c <= '9') { out = static_cast<unsigned>(c - '0'); return true; }
        if (c >= 'A' && c <= 'F') { out = static_cast<unsigned>(c - 'A' + 10); return true; }
        if (c >= 'a' && c <= 'f') { out = static_cast<unsigned>(c - 'a' + 10); return true; }
        return false;
    };
    auto put_utf8 = [](std::string& out, unsigned c) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    };

    std::string out;
    std::size_t i = 0;
    while (i < v.size()) {
        if (v[i] == '\'' && i + 1 < v.size() && v[i + 1] == '\'') {
            out += '\'';
            i += 2;
            continue;
        }
        if (v[i] != '\\') {
            out += v[i++];
            continue;
        }

        if (i + 1 < v.size() && v[i + 1] == '\\') {
            out += '\\';
            i += 2;
            continue;
        }
        // \X2\hhhh...\X0\ : 16-bit code units, terminated explicitly.
        if (i + 3 < v.size() && v[i + 1] == 'X' && v[i + 2] == '2' &&
            v[i + 3] == '\\') {
            std::size_t j = i + 4;
            while (j + 3 < v.size() &&
                   !(v[j] == '\\' && v[j + 1] == 'X' && v[j + 2] == '0' &&
                     v[j + 3] == '\\')) {
                unsigned value = 0;
                bool good = j + 4 <= v.size();
                for (int k = 0; k < 4 && good; ++k) {
                    unsigned digit = 0;
                    if (!hex(v[j + static_cast<std::size_t>(k)], digit)) good = false;
                    else value = (value << 4) | digit;
                }
                if (!good) break;
                j += 4;
                if (value >= 0xD800 && value <= 0xDBFF && j + 3 < v.size()) {
                    unsigned low = 0;
                    bool paired = true;
                    for (int k = 0; k < 4 && paired; ++k) {
                        unsigned digit = 0;
                        if (!hex(v[j + static_cast<std::size_t>(k)], digit))
                            paired = false;
                        else low = (low << 4) | digit;
                    }
                    if (paired && low >= 0xDC00 && low <= 0xDFFF) {
                        const unsigned cp =
                            0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                        out += static_cast<char>(0xF0 | (cp >> 18));
                        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                        j += 4;
                        continue;
                    }
                }
                put_utf8(out, value);
            }
            if (j + 3 < v.size() && v[j] == '\\' && v[j + 1] == 'X' &&
                v[j + 2] == '0' && v[j + 3] == '\\')
                j += 4;
            i = j;
            continue;
        }
        // \X\hh : a single byte.
        if (i + 4 < v.size() && v[i + 1] == 'X' && v[i + 2] == '\\') {
            unsigned hi = 0, lo = 0;
            if (hex(v[i + 3], hi) && hex(v[i + 4], lo)) {
                put_utf8(out, (hi << 4) | lo);
                i += 5;
                continue;
            }
        }
        out += v[i++];
    }
    return out;
}

std::vector<std::string> split_arguments(std::string_view s) {
    std::vector<std::string> out;
    if (s.empty()) return out;

    int depth = 0;
    bool in_string = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (in_string) {
            if (c == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
                in_string = false;
            }
            continue;
        }
        if (c == '\'') { in_string = true; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')') { --depth; continue; }
        if (c == ',' && depth == 0) {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

const Instance* ParseResult::find(u64 id) const {
    const auto it = index.find(id);
    return it == index.end() ? nullptr : &instances[it->second];
}

ParseResult parse(std::string_view text) {
    ParseResult out;

    // Part 21 comments may span lines.
    std::string clean;
    clean.reserve(text.size());
    bool in_string = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!in_string && text[i] == '/' && i + 1 < text.size() &&
            text[i + 1] == '*') {
            std::size_t j = i + 2;
            while (j + 1 < text.size() && !(text[j] == '*' && text[j + 1] == '/'))
                ++j;
            for (std::size_t k = i; k < std::min(j + 2, text.size()); ++k)
                if (text[k] == '\n') clean += '\n';
            i = std::min(j + 1, text.size() - 1);
            continue;
        }
        if (text[i] == '\'') {
            if (in_string && i + 1 < text.size() && text[i + 1] == '\'') {
                clean += "''";
                ++i;
                continue;
            }
            in_string = !in_string;
        }
        clean += text[i];
    }

    std::vector<std::pair<std::string, std::size_t>> statements;
    {
        std::string current;
        std::size_t line = 1, started = 1;
        in_string = false;
        for (std::size_t i = 0; i < clean.size(); ++i) {
            const char c = clean[i];
            if (c == '\n') ++line;
            if (c == '\'') {
                if (in_string && i + 1 < clean.size() && clean[i + 1] == '\'') {
                    current += "''";
                    ++i;
                    continue;
                }
                in_string = !in_string;
            }
            if (c == ';' && !in_string) {
                if (!trim(current).empty()) statements.push_back({current, started});
                current.clear();
                started = line;
                continue;
            }
            current += c;
        }
        if (!trim(current).empty()) statements.push_back({current, started});
    }

    bool seen_iso = false;
    for (const auto& [raw, line] : statements) {
        const std::string s = trim(raw);
        if (s.empty()) continue;
        const std::string up = upper(s);

        if (up.rfind("ISO-10303-21", 0) == 0) { seen_iso = true; continue; }
        if (up.rfind("END-ISO-10303-21", 0) == 0) continue;
        if (up == "HEADER" || up == "DATA" || up == "ENDSEC") continue;
        if (up.rfind("DATA", 0) == 0 && s.find('(') != std::string::npos) continue;

        if (up.rfind("FILE_SCHEMA", 0) == 0) {
            const auto open = s.find('(');
            const auto close = s.rfind(')');
            if (open != std::string::npos && close != std::string::npos)
                out.schema = decode_string(trim(s.substr(open + 1, close - open - 1)));
            if (!out.schema.empty() && out.schema.front() == '(') {
                const auto inner = split_arguments(
                    out.schema.substr(1, out.schema.size() - 2));
                if (!inner.empty()) out.schema = decode_string(inner.front());
            }
            continue;
        }
        if (up.rfind("FILE_NAME", 0) == 0) {
            const auto open = s.find('(');
            const auto close = s.rfind(')');
            if (open != std::string::npos && close != std::string::npos) {
                const auto args = split_arguments(s.substr(open + 1, close - open - 1));
                if (!args.empty()) out.name = decode_string(args[0]);
            }
            continue;
        }
        if (up.rfind("FILE_DESCRIPTION", 0) == 0) continue;

        if (s[0] != '#') continue;

        std::size_t i = 1;
        u64 id = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            id = id * 10 + static_cast<u64>(s[i] - '0');
            ++i;
        }
        while (i < s.size() && is_space(s[i])) ++i;
        if (i >= s.size() || s[i] != '=') {
            out.error = "expected = after an instance id";
            out.line = line;
            return out;
        }
        ++i;
        while (i < s.size() && is_space(s[i])) ++i;

        const std::size_t open = s.find('(', i);
        const std::size_t close = s.rfind(')');
        if (open == std::string::npos || close == std::string::npos ||
            close < open) {
            out.error = "an instance has no argument list";
            out.line = line;
            return out;
        }

        Instance inst;
        inst.id = id;
        inst.type = upper(trim(s.substr(i, open - i)));
        inst.args = split_arguments(s.substr(open + 1, close - open - 1));

        const auto existing = out.index.find(id);
        if (existing != out.index.end()) {
            out.instances[existing->second] = std::move(inst);
        } else {
            out.index[id] = out.instances.size();
            out.instances.push_back(std::move(inst));
        }
    }

    if (!seen_iso) {
        out.error = "not an ISO-10303-21 file";
        return out;
    }
    out.ok = true;
    return out;
}

}
