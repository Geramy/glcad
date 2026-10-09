#include "render/mtext.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gtext {

bool Format::operator==(const Format& o) const noexcept {
    return font == o.font && height == o.height && width == o.width &&
           oblique == o.oblique && tracking == o.tracking && bold == o.bold &&
           italic == o.italic && underline == o.underline &&
           overline == o.overline && strike == o.strike &&
           has_color == o.has_color && aci == o.aci &&
           true_color == o.true_color && r == o.r && g == o.g && b == o.b &&
           align == o.align;
}

namespace {

bool take_arg(std::string_view s, std::size_t& i, std::string& out) {
    const std::size_t start = i;
    while (i < s.size() && s[i] != ';') {
        if (s[i] == '{' || s[i] == '}') { i = start; return false; }
        out.push_back(s[i]);
        ++i;
    }
    if (i >= s.size()) { i = start; out.clear(); return false; }
    ++i;
    return true;
}

f32 to_f32(const std::string& s, f32 fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? fallback : static_cast<f32>(v);
}

void parse_font_arg(const std::string& arg, Format& f) {
    std::size_t bar = arg.find('|');
    f.font = arg.substr(0, bar);
    while (bar != std::string::npos) {
        const std::size_t next = arg.find('|', bar + 1);
        const std::string field = arg.substr(bar + 1, next == std::string::npos
                                                          ? std::string::npos
                                                          : next - bar - 1);
        if (field.size() >= 2) {
            if (field[0] == 'b') f.bold = field[1] != '0';
            else if (field[0] == 'i') f.italic = field[1] != '0';
        }
        bar = next;
    }
}

void parse_paragraph_arg(const std::string& arg, Paragraph& p) {
    std::size_t i = 0;
    if (i < arg.size() && arg[i] == 'x') ++i;
    while (i < arg.size()) {
        const char key = arg[i++];
        std::string num;
        while (i < arg.size() && arg[i] != ',') num.push_back(arg[i++]);
        if (i < arg.size()) ++i;

        if (key == 'i') p.indent_first = to_f32(num, 0.0f);
        else if (key == 'l') p.indent_left = to_f32(num, 0.0f);
        else if (key == 'r') p.indent_right = to_f32(num, 0.0f);
        else if (key == 't') {
            std::string one;
            for (char c : num) {
                if (c == ',') { p.tabs.push_back(to_f32(one, 0.0f)); one.clear(); }
                else one.push_back(c);
            }
            if (!one.empty()) p.tabs.push_back(to_f32(one, 0.0f));
        } else if (key == 'q') {
            if (!num.empty()) {
                if (num[0] == 'c') p.align = Align::Centre;
                else if (num[0] == 'r') p.align = Align::Right;
                else p.align = Align::Left;
            }
        }
    }
}

bool split_stack(const std::string& s, std::string& num, std::string& den,
                 Stack& kind) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        Stack k = Stack::None;
        if (s[i] == '/') k = Stack::Horizontal;
        else if (s[i] == '#') k = Stack::Diagonal;
        else if (s[i] == '^') k = Stack::Tolerance;
        if (k == Stack::None) continue;
        num = s.substr(0, i);
        den = s.substr(i + 1);
        kind = k;
        return true;
    }
    return false;
}

}

std::string MTextContent::plain() const {
    std::string out;
    u32 para = 0;
    for (const auto& r : runs) {
        if (r.paragraph > para) {
            out.push_back('\n');
            para = r.paragraph;
        }
        out += r.text;
        if (r.stack != Stack::None) {
            out.push_back('/');
            out += r.denominator;
        }
    }
    return out;
}

MTextContent parse_mtext(std::string_view s) {
    MTextContent c;
    std::vector<Format> stack{Format{}};
    Format cur;
    std::string text;
    u32 para = 0;
    c.paragraphs.push_back(Paragraph{});

    auto flush = [&](bool para_break) {
        if (text.empty() && !para_break) return;
        if (!text.empty()) {
            Run r;
            r.text = text;
            r.format = cur;
            r.paragraph = para;
            c.runs.push_back(std::move(r));
            text.clear();
        }
        if (para_break) {
            ++para;
            c.paragraphs.push_back(c.paragraphs.back());
            if (!c.runs.empty()) c.runs.back().paragraph_break = true;
        }
    };

    for (std::size_t i = 0; i < s.size();) {
        const char ch = s[i];

        if (ch == '{') {
            flush(false);
            stack.push_back(cur);
            ++i;
            continue;
        }
        if (ch == '}') {
            flush(false);
            if (stack.size() > 1) {
                cur = stack.back();
                stack.pop_back();
            } else {
                text.push_back('}');
            }
            ++i;
            continue;
        }
        if (ch != '\\') {
            text.push_back(ch);
            ++i;
            continue;
        }

        if (i + 1 >= s.size()) {
            text.push_back('\\');
            ++i;
            continue;
        }

        const char code = s[i + 1];
        std::size_t j = i + 2;
        std::string arg;

        switch (code) {
            case 'P':
                flush(true);
                i = j;
                continue;
            case '\\': text.push_back('\\'); i = j; continue;
            case '{':  text.push_back('{');  i = j; continue;
            case '}':  text.push_back('}');  i = j; continue;
            case '~':  text.push_back(' ');  i = j; continue;

            case 'L': flush(false); cur.underline = true;  i = j; continue;
            case 'l': flush(false); cur.underline = false; i = j; continue;
            case 'O': flush(false); cur.overline = true;   i = j; continue;
            case 'o': flush(false); cur.overline = false;  i = j; continue;
            case 'K': flush(false); cur.strike = true;     i = j; continue;
            case 'k': flush(false); cur.strike = false;    i = j; continue;

            case 'f':
            case 'F':
                if (!take_arg(s, j, arg)) break;
                flush(false);
                parse_font_arg(arg, cur);
                i = j;
                continue;

            case 'H': {
                if (!take_arg(s, j, arg)) break;
                flush(false);
                // A trailing `x` means a multiple of the current height.
                if (!arg.empty() && (arg.back() == 'x' || arg.back() == 'X')) {
                    arg.pop_back();
                    cur.height *= to_f32(arg, 1.0f);
                } else {
                    cur.height = to_f32(arg, cur.height);
                }
                i = j;
                continue;
            }

            case 'W':
                if (!take_arg(s, j, arg)) break;
                flush(false);
                cur.width = to_f32(arg, cur.width);
                i = j;
                continue;

            case 'Q':
                if (!take_arg(s, j, arg)) break;
                flush(false);
                cur.oblique = to_f32(arg, cur.oblique);
                i = j;
                continue;

            case 'T':
                if (!take_arg(s, j, arg)) break;
                flush(false);
                cur.tracking = to_f32(arg, cur.tracking);
                i = j;
                continue;

            case 'C':
                if (!take_arg(s, j, arg)) break;
                flush(false);
                cur.has_color = true;
                cur.true_color = false;
                cur.aci = static_cast<i32>(to_f32(arg, 7.0f));
                i = j;
                continue;

            case 'c': {
                if (!take_arg(s, j, arg)) break;
                flush(false);
                const long v = std::strtol(arg.c_str(), nullptr, 10);
                cur.has_color = true;
                cur.true_color = true;
                // AutoCAD packs true colour as 0x00BBGGRR.
                cur.r = static_cast<u8>(v & 0xFF);
                cur.g = static_cast<u8>((v >> 8) & 0xFF);
                cur.b = static_cast<u8>((v >> 16) & 0xFF);
                i = j;
                continue;
            }

            case 'A': {
                if (!take_arg(s, j, arg)) break;
                flush(false);
                const int v = arg.empty() ? 0 : arg[0] - '0';
                cur.align = v == 1 ? RunAlign::Centre
                          : v == 2 ? RunAlign::Top
                                   : RunAlign::Bottom;
                i = j;
                continue;
            }

            case 'S': {
                if (!take_arg(s, j, arg)) break;
                flush(false);
                Run r;
                r.format = cur;
                r.paragraph = para;
                if (!split_stack(arg, r.text, r.denominator, r.stack)) {
                    r.text = arg;
                    r.stack = Stack::None;
                }
                c.runs.push_back(std::move(r));
                i = j;
                continue;
            }

            case 'p':
                if (!take_arg(s, j, arg)) break;
                flush(false);
                parse_paragraph_arg(arg, c.paragraphs[para]);
                i = j;
                continue;

            default:
                break;
        }

        text.push_back('\\');
        ++i;
    }

    flush(false);
    return c;
}

std::string format_mtext(const MTextContent& c) {
    std::string out;
    Format cur;
    u32 para = 0;

    auto emit_escaped = [&](const std::string& t) {
        for (char ch : t) {
            if (ch == '\\' || ch == '{' || ch == '}') out.push_back('\\');
            out.push_back(ch);
        }
    };

    char buf[64];
    for (const auto& r : c.runs) {
        while (r.paragraph > para) { out += "\\P"; ++para; }

        if (r.format.font != cur.font || r.format.bold != cur.bold || r.format.italic != cur.italic) {
            out += "\\f" + r.format.font;
            out += r.format.bold ? "|b1" : "|b0";
            out += r.format.italic ? "|i1" : "|i0";
            out.push_back(';');
        }
        if (r.format.height != cur.height) {
            std::snprintf(buf, sizeof(buf), "\\H%g;", double(r.format.height));
            out += buf;
        }
        if (r.format.width != cur.width) {
            std::snprintf(buf, sizeof(buf), "\\W%g;", double(r.format.width));
            out += buf;
        }
        if (r.format.oblique != cur.oblique) {
            std::snprintf(buf, sizeof(buf), "\\Q%g;", double(r.format.oblique));
            out += buf;
        }
        if (r.format.tracking != cur.tracking) {
            std::snprintf(buf, sizeof(buf), "\\T%g;", double(r.format.tracking));
            out += buf;
        }
        if (r.format.underline != cur.underline)
            out += r.format.underline ? "\\L" : "\\l";
        if (r.format.overline != cur.overline)
            out += r.format.overline ? "\\O" : "\\o";
        if (r.format.strike != cur.strike)
            out += r.format.strike ? "\\K" : "\\k";
        if (!r.format.has_color && cur.has_color) out += "\\C256;";
        if (r.format.has_color &&
            (r.format.aci != cur.aci || r.format.true_color != cur.true_color ||
             r.format.r != cur.r || r.format.g != cur.g || r.format.b != cur.b || !cur.has_color)) {
            if (r.format.true_color) {
                const long v = (long(r.format.b) << 16) | (long(r.format.g) << 8) |
                               long(r.format.r);
                std::snprintf(buf, sizeof(buf), "\\c%ld;", v);
            } else {
                std::snprintf(buf, sizeof(buf), "\\C%d;", int(r.format.aci));
            }
            out += buf;
        }
        cur = r.format;

        if (r.stack != Stack::None) {
            const char op = r.stack == Stack::Horizontal ? '/'
                          : r.stack == Stack::Diagonal   ? '#'
                                                         : '^';
            out += "\\S";
            out += r.text;
            out.push_back(op);
            out += r.denominator;
            out.push_back(';');
        } else {
            emit_escaped(r.text);
        }
    }
    return out;
}

namespace {

f32 run_width(const Font& f, const std::string& s, const Format& fmt,
              const MTextStyle& style) {
    if (s.empty()) return 0.0f;
    if(style.measure_run) return style.measure_run(s,fmt)*style.height*fmt.height*fmt.width;
    if(style.measure) return style.measure(s)*style.height*fmt.height*fmt.width;
    TextStyle ts;
    ts.size = style.height * fmt.height;
    ts.letter_spacing = (fmt.tracking - 1.0f) * 0.1f;
    f32 w = 0.0f, h = 0.0f;
    measure_text(f, s, ts, w, h);
    return w * fmt.width;
}

std::vector<std::string> words_of(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        cur.push_back(ch);
        if (ch == ' ') {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}

MTextLayout layout_mtext(const Font& f, const MTextContent& c,
                         const MTextStyle& style) {
    MTextLayout out;
    if (c.runs.empty()) return out;

    struct Line {
        std::vector<MTextPiece> pieces;
        f32 width{0.0f};
        f32 height{0.0f};
    };
    std::vector<Line> lines;
    lines.emplace_back();
    u32 para = 0;

    auto line_indent = [&](u32 p, bool first) {
        if (p >= c.paragraphs.size()) return 0.0f;
        const Paragraph& pp = c.paragraphs[p];
        return pp.indent_left + (first ? pp.indent_first : 0.0f);
    };

    auto push = [&](const std::string& text, const std::string& den, Stack st,
                    const Format& fmt, f32 w, f32 h) {
        MTextPiece p;
        p.text = text;
        p.denominator = den;
        p.stack = st;
        p.format = fmt;
        p.width = w;
        p.height = h;
        lines.back().pieces.push_back(std::move(p));
        lines.back().width += w;
        lines.back().height = std::max(lines.back().height, h);
    };

    for (const auto& r : c.runs) {
        while (r.paragraph > para) {
            lines.emplace_back();
            ++para;
        }

        const f32 h = style.height * r.format.height;
        if (lines.back().height <= 0.0f) lines.back().height = h;

        if (r.stack != Stack::None) {
            const f32 w = std::max(run_width(f, r.text, r.format, style),
                                   run_width(f, r.denominator, r.format, style));
            push("", "", r.stack, r.format, w, h * 1.6f);
            lines.back().pieces.back().text = r.text;
            lines.back().pieces.back().denominator = r.denominator;
            continue;
        }

        if (style.width <= 0.0f) {
            push(r.text, "", Stack::None, r.format,
                 run_width(f, r.text, r.format, style), h);
            continue;
        }

        std::string pending;
        f32 pending_w = 0.0f;
        const f32 first_indent = line_indent(para, lines.size() == 1);

        for (const std::string& word : words_of(r.text)) {
            const f32 ww = run_width(f, word, r.format, style);
            const f32 used = lines.back().width + pending_w + first_indent;

            if (used + ww > style.width && (used > first_indent || !pending.empty())) {
                if (!pending.empty()) {
                    push(pending, "", Stack::None, r.format, pending_w, h);
                    pending.clear();
                    pending_w = 0.0f;
                }
                lines.emplace_back();
                lines.back().height = h;
            }

            if (ww > style.width) {
                if (!pending.empty()) {
                    push(pending, "", Stack::None, r.format, pending_w, h);
                    pending.clear();
                    pending_w = 0.0f;
                }
                std::string chunk;
                for (char ch : word) {
                    chunk.push_back(ch);
                    const f32 cw = run_width(f, chunk, r.format, style);
                    if (cw > style.width && chunk.size() > 1) {
                        chunk.pop_back();
                        push(chunk, "", Stack::None, r.format,
                             run_width(f, chunk, r.format, style), h);
                        lines.emplace_back();
                        lines.back().height = h;
                        chunk.assign(1, ch);
                    }
                }
                if (!chunk.empty()) {
                    pending = chunk;
                    pending_w = run_width(f, chunk, r.format, style);
                }
                continue;
            }

            pending += word;
            pending_w += ww;
        }
        if (!pending.empty()) push(pending, "", Stack::None, r.format, pending_w, h);
    }

    f32 y = 0.0f;
    u32 index = 0;
    for (auto& line : lines) {
        const f32 lh = line.height > 0.0f ? line.height : style.height;
        const f32 step = style.exact_spacing ? style.height * style.line_spacing
                                             : lh * style.line_spacing;
        y -= step;

        f32 x = 0.0f;
        if (style.width > 0.0f) {
            if (style.align == Align::Centre) x = (style.width - line.width) * 0.5f;
            else if (style.align == Align::Right) x = style.width - line.width;
        }
        if (x < 0.0f) x = 0.0f;

        for (auto& p : line.pieces) {
            p.x = x;
            p.y = y;
            p.line = index;
            x += p.width;
            out.pieces.push_back(p);
        }
        out.width = std::max(out.width, line.width);
        ++index;
    }

    out.line_count = index;
    out.height = -y;
    return out;
}

MTextLayout layout_mtext(const Font& f, std::string_view s,
                         const MTextStyle& style) {
    return layout_mtext(f, parse_mtext(s), style);
}

}
