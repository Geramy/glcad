#include "engine/parameter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

namespace gunit {

namespace {

bool name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// A unit suffix (`10mm`) is not a parameter reference.
bool is_unit_word(std::string_view w) {
    return length_from_suffix(w).has_value();
}

}

Parameter& ParameterTable::add(Parameter p) {
    for (auto& e : params_)
        if (e.name == p.name) {
            e = std::move(p);
            return e;
        }
    params_.push_back(std::move(p));
    return params_.back();
}

const Parameter* ParameterTable::find(std::string_view n) const {
    for (const auto& p : params_)
        if (p.name == n) return &p;
    return nullptr;
}

Parameter* ParameterTable::find(std::string_view n) {
    for (auto& p : params_)
        if (p.name == n) return &p;
    return nullptr;
}

bool ParameterTable::remove(std::string_view n) {
    if (!dependents(n).empty()) return false;
    const auto it = std::find_if(params_.begin(), params_.end(),
                                 [&](const Parameter& p) { return p.name == n; });
    if (it == params_.end()) return false;
    params_.erase(it);
    return true;
}

bool ParameterTable::rename(std::string_view from, std::string_view to) {
    if (from == to) return true;
    if (to.empty() || !name_start(to.front())) return false;
    if (find(to)) return false;
    Parameter* p = find(from);
    if (!p) return false;

    const std::string old(from), fresh(to);
    for (auto& e : params_) {
        if (!e.is_expression()) continue;
        std::string out;
        const std::string& s = e.expression;
        for (std::size_t i = 0; i < s.size();) {
            if (!name_start(s[i])) {
                out.push_back(s[i]);
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < s.size() && name_char(s[j])) ++j;
            const std::string word = s.substr(i, j - i);
            out += (word == old) ? fresh : word;
            i = j;
        }
        e.expression = out;
    }
    p->name = fresh;
    return true;
}

std::vector<std::string> ParameterTable::references(std::string_view expr) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < expr.size();) {
        if (!name_start(expr[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < expr.size() && name_char(expr[j])) ++j;
        const std::string_view word = expr.substr(i, j - i);
        if (!is_unit_word(word) &&
            std::find(out.begin(), out.end(), word) == out.end())
            out.emplace_back(word);
        i = j;
    }
    return out;
}

std::vector<std::string> ParameterTable::dependents(std::string_view name) const {
    std::vector<std::string> out;
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& p : params_) {
            if (p.name == name) continue;
            if (std::find(out.begin(), out.end(), p.name) != out.end()) continue;
            if (!p.is_expression()) continue;

            for (const std::string& r : references(p.expression)) {
                const bool hits = r == name ||
                                  std::find(out.begin(), out.end(), r) != out.end();
                if (!hits) continue;
                out.push_back(p.name);
                grew = true;
                break;
            }
        }
    }
    return out;
}

std::optional<f64> ParameterTable::value_of(std::string_view name) const {
    const Parameter* p = find(name);
    if (!p || !p->ok) return std::nullopt;
    return p->value;
}

std::optional<f64> evaluate_expression(std::string_view expr,
                                       const ParameterTable& table,
                                       Length default_unit, std::string* error) {
    std::string_view s = expr;
    if (!s.empty() && s.front() == '=') s.remove_prefix(1);

    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (!name_start(s[i])) {
            out.push_back(s[i]);
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < s.size() && name_char(s[j])) ++j;
        const std::string word(s.substr(i, j - i));

        if (is_unit_word(word)) {
            out += word;
            i = j;
            continue;
        }

        const Parameter* p = table.find(word);
        if (!p) {
            if (error) *error = "unknown parameter '" + word + "'";
            return std::nullopt;
        }
        if (!p->ok) {
            if (error) *error = "'" + word + "' has no value";
            return std::nullopt;
        }

        // Substitute lengths with an explicit `m` suffix: `100 + 1in` is ambiguous.
        char buf[64];
        if (p->kind == ParamKind::Length) {
            std::snprintf(buf, sizeof(buf), "(%.12gm)", p->value);
        } else {
            std::snprintf(buf, sizeof(buf), "(%.12g)", p->value);
        }
        out += buf;
        i = j;
    }

    const auto v = parse_length(out, default_unit);
    if (!v) {
        if (error) *error = "cannot evaluate '" + std::string(expr) + "'";
        return std::nullopt;
    }
    return *v;
}

EvalReport ParameterTable::evaluate(Length default_unit) {
    EvalReport report;

    std::map<std::string, std::vector<std::string>> deps;
    std::map<std::string, int> indegree;
    for (const auto& p : params_) {
        indegree[p.name] += 0;
        if (!p.is_expression()) continue;
        for (const std::string& r : references(p.expression)) {
            if (!find(r)) continue;         // reported as an error, not a cycle
            deps[r].push_back(p.name);
            indegree[p.name] += 1;
        }
    }

    std::vector<std::string> ready;
    for (const auto& [name, n] : indegree)
        if (n == 0) ready.push_back(name);

    std::vector<std::string> order;
    while (!ready.empty()) {
        const std::string name = ready.back();
        ready.pop_back();
        order.push_back(name);
        for (const std::string& d : deps[name])
            if (--indegree[d] == 0) ready.push_back(d);
    }

    for (const auto& [name, n] : indegree)
        if (n > 0) {
            report.cycles.push_back(name);
            if (Parameter* p = find(name)) {
                p->ok = false;
                p->error = "circular reference";
                ++report.failed;
            }
        }

    for (const std::string& name : order) {
        Parameter* p = find(name);
        if (!p) continue;
        if (!p->is_expression()) {
            if (!p->expression.empty() && p->kind != ParamKind::Text) {
                const auto v = p->kind == ParamKind::Angle
                                   ? parse_angle(p->expression, Angle::Degree)
                                   : parse_length(p->expression, default_unit);
                if (v) p->value = *v;
            }
            p->ok = true;
            p->error.clear();
            ++report.evaluated;
            continue;
        }

        std::string err;
        const auto v = evaluate_expression(p->expression, *this, default_unit, &err);
        if (!v) {
            p->ok = false;
            p->error = err;
            ++report.failed;
            continue;
        }
        p->value = *v;
        p->ok = true;
        p->error.clear();
        ++report.evaluated;
    }

    return report;
}

std::string format_dual(f64 metres, const DualFormat& f) {
    const std::string primary = format_length(metres, f.primary);
    if (!f.enabled) return primary;
    return primary + f.open + format_length(metres, f.secondary) + f.close;
}

}
