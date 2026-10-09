#include "engine/lisp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gcmd {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}
bool is_delim(char c) { return is_space(c) || c == '(' || c == ')' || c == ';'; }

std::string upper(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return o;
}

std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return o;
}

bool parse_number(std::string_view t, LispValue& out) {
    if (t.empty()) return false;
    std::size_t i = 0;
    if (t[0] == '+' || t[0] == '-') i = 1;
    if (i >= t.size()) return false;

    bool digits = false, dot = false, expo = false;
    for (std::size_t k = i; k < t.size(); ++k) {
        const char c = t[k];
        if (c >= '0' && c <= '9') { digits = true; continue; }
        if (c == '.' && !dot && !expo) { dot = true; continue; }
        if ((c == 'e' || c == 'E') && digits && !expo) {
            expo = true;
            if (k + 1 < t.size() && (t[k + 1] == '+' || t[k + 1] == '-')) ++k;
            continue;
        }
        return false;
    }
    if (!digits) return false;

    const std::string s(t);
    if (dot || expo) out = LispValue::num(std::stod(s));
    else             out = LispValue::num(static_cast<gcm::i64>(std::stoll(s)));
    return true;
}

}

LispValue LispValue::num(gcm::i64 i) {
    LispValue v;
    v.type = LispType::Int;
    v.integer = i;
    return v;
}
LispValue LispValue::num(double d) {
    LispValue v;
    v.type = LispType::Real;
    v.real = d;
    return v;
}
LispValue LispValue::str(std::string s) {
    LispValue v;
    v.type = LispType::Str;
    v.text = std::move(s);
    return v;
}
LispValue LispValue::sym(std::string s) {
    LispValue v;
    v.type = LispType::Sym;
    v.text = std::move(s);
    return v;
}
LispValue LispValue::list(LispList l) {
    LispValue v;
    // nil and the empty list are the same value.
    if (l.empty()) return nil();
    v.type = LispType::List;
    v.items = std::move(l);
    return v;
}

std::string LispValue::print() const {
    switch (type) {
        case LispType::Nil:  return "nil";
        case LispType::True: return "T";
        case LispType::Int:  return std::to_string(integer);
        case LispType::Real: {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.6g", real);
            return buf;
        }
        case LispType::Str:  return "\"" + text + "\"";
        case LispType::Sym:  return text;
        case LispType::Fun:  return "<subr>";
        case LispType::List: {
            std::string out = "(";
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out += " ";
                out += items[i].print();
            }
            return out + ")";
        }
    }
    return "";
}

namespace {

struct Reader {
    std::string_view s;
    std::size_t      i{0};
    std::size_t      line{1};

    void skip() {
        while (i < s.size()) {
            if (is_space(s[i])) {
                if (s[i] == '\n') ++line;
                ++i;
            } else if (s[i] == ';') {
                // AutoLISP comments: ';' to end of line, and ';| ... |;' blocks.
                if (i + 1 < s.size() && s[i + 1] == '|') {
                    i += 2;
                    while (i + 1 < s.size() && !(s[i] == '|' && s[i + 1] == ';')) {
                        if (s[i] == '\n') ++line;
                        ++i;
                    }
                    i = std::min(i + 2, s.size());
                } else {
                    while (i < s.size() && s[i] != '\n') ++i;
                }
            } else {
                return;
            }
        }
    }

    bool read(LispValue& out, std::string& error) {
        skip();
        if (i >= s.size()) return false;

        if (s[i] == ')') {
            error = "unexpected )";
            return false;
        }

        if (s[i] == '\'') {
            ++i;
            LispValue inner;
            if (!read(inner, error)) {
                if (error.empty()) error = "quote with nothing after it";
                return false;
            }
            out = LispValue::list({LispValue::sym("quote"), inner});
            return true;
        }

        if (s[i] == '(') {
            ++i;
            LispList items;
            for (;;) {
                skip();
                if (i >= s.size()) {
                    error = "unbalanced (";
                    return false;
                }
                if (s[i] == ')') { ++i; break; }
                LispValue item;
                if (!read(item, error)) {
                    if (error.empty()) error = "unbalanced (";
                    return false;
                }
                items.push_back(std::move(item));
            }
            out = LispValue::list(std::move(items));
            return true;
        }

        if (s[i] == '"') {
            ++i;
            std::string text;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    ++i;
                    switch (s[i]) {
                        case 'n': text += '\n'; break;
                        case 't': text += '\t'; break;
                        case '\\': text += '\\'; break;
                        case '"': text += '"'; break;
                        default: text += s[i]; break;
                    }
                } else {
                    if (s[i] == '\n') ++line;
                    text += s[i];
                }
                ++i;
            }
            if (i >= s.size()) {
                error = "unterminated string";
                return false;
            }
            ++i;
            out = LispValue::str(std::move(text));
            return true;
        }

        const std::size_t start = i;
        while (i < s.size() && !is_delim(s[i])) ++i;
        const std::string_view tok = s.substr(start, i - start);

        if (parse_number(tok, out)) return true;
        const std::string up = upper(tok);
        if (up == "NIL") { out = LispValue::nil(); return true; }
        if (up == "T")   { out = LispValue::truth(); return true; }
        out = LispValue::sym(lower(tok));
        return true;
    }
};

}

ParseResult parse(std::string_view text) {
    ParseResult out;
    Reader r{text};
    for (;;) {
        std::string error;
        LispValue form;
        const std::size_t at = r.line;
        if (!r.read(form, error)) {
            if (!error.empty()) {
                out.ok = false;
                out.error = error;
                out.line = at;
            }
            break;
        }
        out.forms.push_back(std::move(form));
    }
    return out;
}

Interpreter::Interpreter() { frames_.emplace_back(); }

void Interpreter::define(std::string_view name, Builtin fn) {
    builtins_[lower(name)] = std::move(fn);
}

bool Interpreter::defined(std::string_view name) const {
    const std::string n = lower(name);
    return builtins_.count(n) != 0 || funs_.count(n) != 0;
}

void Interpreter::fail(std::string message, const LispValue& form) {
    if (!error_.none()) return;      // keep the FIRST failure, not the last
    error_.message = std::move(message);
    error_.form = form.print();
}

bool Interpreter::step() {
    if (!error_.none()) return false;
    if (++steps_ > limits.max_steps) {
        fail("the routine ran too long and was stopped", LispValue::nil());
        return false;
    }
    return true;
}

LispValue* Interpreter::lookup(const std::string& name) {
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        const auto f = it->locals.find(name);
        if (f != it->locals.end()) return &f->second;
    }
    const auto g = vars_.find(name);
    return g != vars_.end() ? &g->second : nullptr;
}

LispValue Interpreter::eval(const LispValue& form) {
    if (!step()) return LispValue::nil();

    switch (form.type) {
        case LispType::Sym: {
            if (LispValue* v = lookup(form.text)) return *v;
            // An unbound symbol is nil in AutoLISP, not an error.
            return LispValue::nil();
        }
        case LispType::List:
            return eval_list(form);
        default:
            return form;
    }
}

LispValue Interpreter::eval_list(const LispValue& form) {
    if (form.items.empty()) return LispValue::nil();
    const LispValue& head = form.items.front();

    if (head.type == LispType::Sym) return eval_call(head.text, form.items);

    const LispValue fn = eval(head);
    LispList args;
    for (std::size_t i = 1; i < form.items.size(); ++i)
        args.push_back(eval(form.items[i]));
    return apply(fn, args, "<expr>");
}

namespace {

bool any_real(const LispList& v) {
    for (const LispValue& x : v)
        if (x.type == LispType::Real) return true;
    return false;
}

}

LispValue Interpreter::eval_call(const std::string& name, const LispList& form) {
    if (!error_.none()) return LispValue::nil();

    auto arg = [&](std::size_t i) -> const LispValue& {
        static const LispValue kNil;
        return i < form.size() ? form[i] : kNil;
    };
    auto evaluated = [&](std::size_t from) {
        LispList out;
        for (std::size_t i = from; i < form.size(); ++i) out.push_back(eval(form[i]));
        return out;
    };

    if (name == "quote") return arg(1);

    if (name == "setq") {
        LispValue last;
        for (std::size_t i = 1; i + 1 < form.size(); i += 2) {
            if (form[i].type != LispType::Sym) {
                fail("setq needs a symbol", form[i]);
                return LispValue::nil();
            }
            last = eval(form[i + 1]);
            bool assigned = false;
            for (auto it = frames_.rbegin(); it != frames_.rend() && !assigned; ++it) {
                const auto f = it->locals.find(form[i].text);
                if (f != it->locals.end()) { f->second = last; assigned = true; }
            }
            if (!assigned) vars_[form[i].text] = last;
        }
        return last;
    }

    if (name == "defun") {
        if (form.size() < 3 || form[1].type != LispType::Sym) {
            fail("defun needs a name and an argument list", LispValue::list(form));
            return LispValue::nil();
        }
        LispValue fn;
        fn.type = LispType::Fun;
        fn.text = form[1].text;
        for (std::size_t i = 2; i < form.size(); ++i) fn.items.push_back(form[i]);
        funs_[form[1].text] = fn;
        return LispValue::sym(form[1].text);
    }

    if (name == "lambda") {
        LispValue fn;
        fn.type = LispType::Fun;
        fn.text = "lambda";
        for (std::size_t i = 1; i < form.size(); ++i) fn.items.push_back(form[i]);
        return fn;
    }

    if (name == "if") {
        if (form.size() < 3) {
            fail("if needs a test and a then-form", LispValue::list(form));
            return LispValue::nil();
        }
        if (eval(form[1]).truthy()) return eval(form[2]);
        return form.size() > 3 ? eval(form[3]) : LispValue::nil();
    }

    if (name == "cond") {
        for (std::size_t i = 1; i < form.size(); ++i) {
            const LispValue& clause = form[i];
            if (clause.type != LispType::List || clause.items.empty()) continue;
            const LispValue test = eval(clause.items[0]);
            if (!test.truthy()) continue;
            LispValue r = test;
            for (std::size_t k = 1; k < clause.items.size(); ++k)
                r = eval(clause.items[k]);
            return r;
        }
        return LispValue::nil();
    }

    if (name == "while") {
        if (form.size() < 2) return LispValue::nil();
        LispValue r;
        while (error_.none() && eval(form[1]).truthy()) {
            if (!step()) break;
            for (std::size_t i = 2; i < form.size(); ++i) r = eval(form[i]);
        }
        return r;
    }

    if (name == "repeat") {
        if (form.size() < 2) return LispValue::nil();
        const LispValue n = eval(form[1]);
        LispValue r;
        for (gcm::i64 k = 0; k < n.as_int() && error_.none(); ++k) {
            if (!step()) break;
            for (std::size_t i = 2; i < form.size(); ++i) r = eval(form[i]);
        }
        return r;
    }

    if (name == "foreach") {
        if (form.size() < 3 || form[1].type != LispType::Sym) {
            fail("foreach needs a symbol and a list", LispValue::list(form));
            return LispValue::nil();
        }
        const LispValue seq = eval(form[2]);
        LispValue r;
        frames_.emplace_back();
        for (const LispValue& item : seq.items) {
            if (!step()) break;
            frames_.back().locals[form[1].text] = item;
            for (std::size_t i = 3; i < form.size(); ++i) r = eval(form[i]);
            if (!error_.none()) break;
        }
        frames_.pop_back();
        return r;
    }

    if (name == "progn") {
        LispValue r;
        for (std::size_t i = 1; i < form.size(); ++i) r = eval(form[i]);
        return r;
    }

    if (name == "and") {
        for (std::size_t i = 1; i < form.size(); ++i)
            if (!eval(form[i]).truthy()) return LispValue::nil();
        return LispValue::truth();
    }
    if (name == "or") {
        for (std::size_t i = 1; i < form.size(); ++i) {
            const LispValue v = eval(form[i]);
            if (v.truthy()) return v;
        }
        return LispValue::nil();
    }

    const LispList a = evaluated(1);
    if (!error_.none()) return LispValue::nil();

    if (name == "not" || name == "null")
        return a.empty() || !a[0].truthy() ? LispValue::truth() : LispValue::nil();

    if (name == "+" || name == "-" || name == "*" || name == "/") {
        if (a.empty()) return LispValue::num(gcm::i64{0});
        for (const LispValue& x : a)
            if (!x.numeric()) {
                fail("arithmetic on a non-number", x);
                return LispValue::nil();
            }
        const bool real = any_real(a);
        if (name == "+" || name == "*") {
            double dr = (name == "+") ? 0.0 : 1.0;
            gcm::i64 ir = (name == "+") ? 0 : 1;
            for (const LispValue& x : a) {
                if (name == "+") { dr += x.as_real(); ir += x.as_int(); }
                else             { dr *= x.as_real(); ir *= x.as_int(); }
            }
            return real ? LispValue::num(dr) : LispValue::num(ir);
        }
        // Unary minus negates; unary divide is the reciprocal, as in AutoLISP.
        if (a.size() == 1) {
            if (name == "-")
                return real ? LispValue::num(-a[0].as_real())
                            : LispValue::num(-a[0].as_int());
            if (a[0].as_real() == 0.0) {
                fail("divide by zero", a[0]);
                return LispValue::nil();
            }
            return LispValue::num(1.0 / a[0].as_real());
        }
        double dr = a[0].as_real();
        gcm::i64 ir = a[0].as_int();
        for (std::size_t i = 1; i < a.size(); ++i) {
            if (name == "-") { dr -= a[i].as_real(); ir -= a[i].as_int(); }
            else {
                if (a[i].as_real() == 0.0) {
                    fail("divide by zero", a[i]);
                    return LispValue::nil();
                }
                dr /= a[i].as_real();
                // Integer division truncates: (/ 7 2) is 3.
                if (a[i].as_int() == 0) {
                    fail("divide by zero", a[i]);
                    return LispValue::nil();
                }
                ir /= a[i].as_int();
            }
        }
        return real ? LispValue::num(dr) : LispValue::num(ir);
    }

    if (name == "1+" || name == "1-") {
        if (a.empty() || !a[0].numeric()) {
            fail("1+ needs a number", LispValue::list(form));
            return LispValue::nil();
        }
        const double d = name == "1+" ? 1.0 : -1.0;
        return a[0].type == LispType::Real
                   ? LispValue::num(a[0].real + d)
                   : LispValue::num(a[0].integer + static_cast<gcm::i64>(d));
    }

    if (name == "abs")
        return a.empty() ? LispValue::nil()
               : a[0].type == LispType::Real
                   ? LispValue::num(std::fabs(a[0].real))
                   : LispValue::num(static_cast<gcm::i64>(std::llabs(a[0].integer)));
    if (name == "sqrt")
        return a.empty() ? LispValue::nil() : LispValue::num(std::sqrt(a[0].as_real()));
    if (name == "sin")
        return a.empty() ? LispValue::nil() : LispValue::num(std::sin(a[0].as_real()));
    if (name == "cos")
        return a.empty() ? LispValue::nil() : LispValue::num(std::cos(a[0].as_real()));
    if (name == "atan") {
        if (a.size() >= 2)
            return LispValue::num(std::atan2(a[0].as_real(), a[1].as_real()));
        return a.empty() ? LispValue::nil() : LispValue::num(std::atan(a[0].as_real()));
    }
    if (name == "expt")
        return a.size() < 2 ? LispValue::nil()
                            : LispValue::num(std::pow(a[0].as_real(), a[1].as_real()));
    if (name == "rem" || name == "mod") {
        if (a.size() < 2 || a[1].as_int() == 0) {
            fail("rem by zero", LispValue::list(form));
            return LispValue::nil();
        }
        return LispValue::num(a[0].as_int() % a[1].as_int());
    }
    if (name == "min" || name == "max") {
        if (a.empty()) return LispValue::nil();
        double best = a[0].as_real();
        for (const LispValue& x : a)
            best = name == "min" ? std::min(best, x.as_real())
                                 : std::max(best, x.as_real());
        return any_real(a) ? LispValue::num(best)
                           : LispValue::num(static_cast<gcm::i64>(best));
    }
    if (name == "fix")
        return a.empty() ? LispValue::nil()
                         : LispValue::num(static_cast<gcm::i64>(a[0].as_real()));
    if (name == "float")
        return a.empty() ? LispValue::nil() : LispValue::num(a[0].as_real());

    if (name == "=" || name == "/=" || name == "<" || name == ">" ||
        name == "<=" || name == ">=") {
        if (a.size() < 2) return LispValue::truth();
        auto compare = [&](const LispValue& x, const LispValue& y) {
            if (x.type == LispType::Str || y.type == LispType::Str) {
                if (name == "=")  return x.text == y.text;
                if (name == "/=") return x.text != y.text;
                if (name == "<")  return x.text < y.text;
                if (name == ">")  return x.text > y.text;
                if (name == "<=") return x.text <= y.text;
                return x.text >= y.text;
            }
            const double p = x.as_real(), q = y.as_real();
            if (name == "=")  return p == q;
            if (name == "/=") return p != q;
            if (name == "<")  return p < q;
            if (name == ">")  return p > q;
            if (name == "<=") return p <= q;
            return p >= q;
        };
        for (std::size_t i = 1; i < a.size(); ++i)
            if (!compare(a[i - 1], a[i])) return LispValue::nil();
        return LispValue::truth();
    }

    if (name == "eq" || name == "equal") {
        if (a.size() < 2) return LispValue::truth();
        return a[0].print() == a[1].print() ? LispValue::truth() : LispValue::nil();
    }

    if (name == "list") {
        if (a.size() > limits.max_list) {
            fail("list too large", LispValue::list(form));
            return LispValue::nil();
        }
        return LispValue::list(a);
    }
    if (name == "car")
        return (a.empty() || a[0].items.empty()) ? LispValue::nil() : a[0].items[0];
    if (name == "cdr") {
        if (a.empty() || a[0].items.size() < 2) return LispValue::nil();
        return LispValue::list(LispList(a[0].items.begin() + 1, a[0].items.end()));
    }
    if (name == "cadr")
        return (a.empty() || a[0].items.size() < 2) ? LispValue::nil() : a[0].items[1];
    if (name == "caddr")
        return (a.empty() || a[0].items.size() < 3) ? LispValue::nil() : a[0].items[2];
    if (name == "cons") {
        if (a.size() < 2) return LispValue::nil();
        LispList out{a[0]};
        for (const LispValue& x : a[1].items) out.push_back(x);
        if (a[1].type != LispType::List && a[1].type != LispType::Nil)
            out.push_back(a[1]);
        return LispValue::list(out);
    }
    if (name == "append") {
        LispList out;
        for (const LispValue& x : a) {
            if (x.type == LispType::List)
                out.insert(out.end(), x.items.begin(), x.items.end());
            else if (x.type != LispType::Nil)
                out.push_back(x);
        }
        return LispValue::list(out);
    }
    if (name == "length")
        return LispValue::num(static_cast<gcm::i64>(a.empty() ? 0 : a[0].items.size()));
    if (name == "nth") {
        if (a.size() < 2) return LispValue::nil();
        const gcm::i64 i = a[0].as_int();
        if (i < 0 || static_cast<std::size_t>(i) >= a[1].items.size())
            return LispValue::nil();
        return a[1].items[static_cast<std::size_t>(i)];
    }
    if (name == "reverse") {
        if (a.empty()) return LispValue::nil();
        LispList out = a[0].items;
        std::reverse(out.begin(), out.end());
        return LispValue::list(out);
    }
    if (name == "member") {
        if (a.size() < 2) return LispValue::nil();
        for (std::size_t i = 0; i < a[1].items.size(); ++i)
            if (a[1].items[i].print() == a[0].print())
                return LispValue::list(
                    LispList(a[1].items.begin() + static_cast<long>(i),
                             a[1].items.end()));
        return LispValue::nil();
    }
    if (name == "apply") {
        if (a.size() < 2) return LispValue::nil();
        return apply(a[0], a[1].items, a[0].text);
    }
    if (name == "mapcar") {
        if (a.size() < 2) return LispValue::nil();
        LispList out;
        for (const LispValue& x : a[1].items) {
            if (!step()) break;
            out.push_back(apply(a[0], {x}, a[0].text));
        }
        return LispValue::list(out);
    }

    if (name == "listp")
        return (a.empty() || a[0].type == LispType::List ||
                a[0].type == LispType::Nil)
                   ? LispValue::truth() : LispValue::nil();
    if (name == "numberp")
        return (!a.empty() && a[0].numeric()) ? LispValue::truth() : LispValue::nil();
    if (name == "atom")
        return (a.empty() || a[0].type != LispType::List) ? LispValue::truth()
                                                          : LispValue::nil();
    if (name == "type") {
        if (a.empty()) return LispValue::nil();
        switch (a[0].type) {
            case LispType::Int:  return LispValue::sym("int");
            case LispType::Real: return LispValue::sym("real");
            case LispType::Str:  return LispValue::sym("str");
            case LispType::Sym:  return LispValue::sym("sym");
            case LispType::List: return LispValue::sym("list");
            case LispType::Fun:  return LispValue::sym("subr");
            default:             return LispValue::nil();
        }
    }

    if (name == "strcat") {
        std::string out;
        for (const LispValue& x : a)
            out += x.type == LispType::Str ? x.text : x.print();
        return LispValue::str(out);
    }
    if (name == "strlen")
        return LispValue::num(static_cast<gcm::i64>(a.empty() ? 0 : a[0].text.size()));
    if (name == "substr") {
        if (a.size() < 2) return LispValue::nil();
        // AutoLISP indexes strings from one, not zero.
        const gcm::i64 from = a[1].as_int() - 1;
        if (from < 0 || static_cast<std::size_t>(from) > a[0].text.size())
            return LispValue::str("");
        const std::size_t n = a.size() > 2
                                  ? static_cast<std::size_t>(std::max<gcm::i64>(0, a[2].as_int()))
                                  : std::string::npos;
        return LispValue::str(a[0].text.substr(static_cast<std::size_t>(from), n));
    }
    if (name == "itoa")
        return LispValue::str(a.empty() ? "0" : std::to_string(a[0].as_int()));
    if (name == "rtos") {
        char buf[64];
        const int places = a.size() > 2 ? static_cast<int>(a[2].as_int()) : 4;
        std::snprintf(buf, sizeof buf, "%.*f", std::clamp(places, 0, 15),
                      a.empty() ? 0.0 : a[0].as_real());
        return LispValue::str(buf);
    }
    if (name == "atoi")
        return LispValue::num(a.empty() ? gcm::i64{0}
                                        : static_cast<gcm::i64>(std::atoll(a[0].text.c_str())));
    if (name == "atof")
        return LispValue::num(a.empty() ? 0.0 : std::atof(a[0].text.c_str()));
    if (name == "strcase") {
        if (a.empty()) return LispValue::nil();
        const bool down = a.size() > 1 && a[1].truthy();
        return LispValue::str(down ? lower(a[0].text) : upper(a[0].text));
    }

    if (name == "command") {
        for (const LispValue& x : a) {
            Step s;
            s.kind = StepKind::Input;
            s.text = x.type == LispType::Str ? x.text
                     : x.type == LispType::List ? [&] {
                           std::string p;
                           for (std::size_t i = 0; i < x.items.size(); ++i) {
                               if (i) p += ",";
                               char buf[48];
                               std::snprintf(buf, sizeof buf, "%.10g",
                                             x.items[i].as_real());
                               p += buf;
                           }
                           return p;
                       }()
                                                : x.print();
            script_.steps.push_back(std::move(s));
        }
        return LispValue::nil();
    }

    if (name == "setvar") {
        if (a.size() < 2) return LispValue::nil();
        vars_[lower(a[0].text)] = a[1];
        return a[1];
    }
    if (name == "getvar") {
        if (a.empty()) return LispValue::nil();
        const auto it = vars_.find(lower(a[0].text));
        return it != vars_.end() ? it->second : LispValue::nil();
    }
    if (name == "prompt" || name == "princ" || name == "print") {
        for (const LispValue& x : a)
            output_.push_back(x.type == LispType::Str ? x.text : x.print());
        return a.empty() ? LispValue::nil() : a.back();
    }

    const auto b = builtins_.find(name);
    if (b != builtins_.end()) return b->second(*this, a);

    const auto f = funs_.find(name);
    if (f != funs_.end()) return apply(f->second, a, name);

    fail("no such function: " + name, LispValue::list(form));
    return LispValue::nil();
}

LispValue Interpreter::apply(const LispValue& fn, const LispList& args,
                             const std::string& name) {
    if (fn.type == LispType::Sym) {
        const std::string fname = lower(fn.text);
        const auto user = funs_.find(fname);
        if (user != funs_.end()) return apply(user->second, args, fname);
        // Arguments are quoted so they are not evaluated twice.
        LispList form{LispValue::sym(fname)};
        for (const LispValue& x : args)
            form.push_back(LispValue::list({LispValue::sym("quote"), x}));
        return eval_call(fname, form);
    }

    if (fn.type != LispType::Fun) {
        fail("not a function: " + name, fn);
        return LispValue::nil();
    }
    if (fn.items.empty()) return LispValue::nil();
    if (++depth_ > limits.max_depth) {
        --depth_;
        fail("recursion too deep in " + name, fn);
        return LispValue::nil();
    }

    // AutoLISP's `/` separates arguments from locals.
    std::vector<std::string> params, locals;
    bool after_slash = false;
    for (const LispValue& p : fn.items[0].items) {
        if (p.type == LispType::Sym && p.text == "/") { after_slash = true; continue; }
        if (p.type != LispType::Sym) continue;
        (after_slash ? locals : params).push_back(p.text);
    }

    frames_.emplace_back();
    for (std::size_t i = 0; i < params.size(); ++i)
        frames_.back().locals[params[i]] = i < args.size() ? args[i] : LispValue::nil();
    for (const std::string& l : locals) frames_.back().locals[l] = LispValue::nil();

    LispValue r;
    for (std::size_t i = 1; i < fn.items.size() && error_.none(); ++i)
        r = eval(fn.items[i]);

    frames_.pop_back();
    --depth_;
    return r;
}

LispValue Interpreter::run(std::string_view text) {
    const ParseResult p = parse(text);
    if (!p.ok) {
        error_.message = p.error + " on line " + std::to_string(p.line);
        error_.form.clear();
        return LispValue::nil();
    }
    LispValue r;
    for (const LispValue& form : p.forms) {
        r = eval(form);
        if (!error_.none()) break;
    }
    return r;
}

}
