#include "engine/command.hpp"

#include <algorithm>
#include <cctype>

namespace gcmd {

std::string to_upper(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = static_cast<char>(std::toupper((unsigned char)c));
    return o;
}

void Registry::add(Command c) {
    c.name = to_upper(c.name);
    for (auto& a : c.aliases) a = to_upper(a);
    cmds_.push_back(std::move(c));
}

const Command* Registry::by_name(std::string_view name) const {
    const std::string n = to_upper(name);
    for (const auto& c : cmds_)
        if (c.name == n) return &c;
    return nullptr;
}

const Command* Registry::find(std::string_view token) const {
    const std::string t = to_upper(token);
    for (const auto& c : cmds_)
        for (const auto& a : c.aliases)
            if (a == t) return &c;
    for (const auto& c : cmds_)
        if (c.name == t) return &c;
    return nullptr;
}

bool Registry::add_alias(std::string_view alias, std::string_view command_name) {
    const std::string a = to_upper(alias);
    for (auto& c : cmds_)
        if (c.name == to_upper(command_name)) {
            if (std::find(c.aliases.begin(), c.aliases.end(), a) == c.aliases.end())
                c.aliases.push_back(a);
            return true;
        }
    return false;
}

std::size_t Registry::load_pgp(std::string_view text) {
    // acad.pgp lines look like:  L,   *LINE
    std::size_t n = 0, i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string_view::npos) e = text.size();
        std::string_view line = text.substr(i, e - i);
        i = e + 1;

        const std::size_t sc = line.find(';');
        if (sc != std::string_view::npos) line = line.substr(0, sc);
        const std::size_t comma = line.find(',');
        const std::size_t star = line.find('*');
        if (comma == std::string_view::npos || star == std::string_view::npos) continue;
        if (star < comma) continue;

        auto trim = [](std::string_view s) {
            while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
            while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
            return s;
        };
        const std::string_view alias = trim(line.substr(0, comma));
        std::string_view cmd = trim(line.substr(star + 1));
        if (alias.empty() || cmd.empty()) continue;
        if (add_alias(alias, cmd)) ++n;
    }
    return n;
}

static int fuzzy_score(std::string_view needle, std::string_view hay) {
    if (needle.empty()) return 0;
    std::size_t h = 0;
    int score = 0, streak = 0;
    for (char c : needle) {
        bool found = false;
        while (h < hay.size()) {
            if (hay[h] == c) {
                score += 2 + streak;
                if (h == 0) score += 4;
                ++streak;
                ++h;
                found = true;
                break;
            }
            streak = 0;
            ++h;
        }
        if (!found) return -1;
    }
    return score;
}

std::vector<Match> Registry::complete(std::string_view partial, Modes lens,
                                      std::size_t limit) const {
    std::vector<Match> out;
    if (partial.empty()) return out;
    const std::string t = to_upper(partial);

    for (const auto& c : cmds_) {
        if (!applies(c.modes, lens)) continue;

        MatchKind kind = MatchKind::Fuzzy;
        int score = -1;

        bool exact_alias = false;
        for (const auto& a : c.aliases)
            if (a == t) { exact_alias = true; break; }

        if (exact_alias)              { kind = MatchKind::ExactAlias; score = 10000; }
        else if (c.name == t)         { kind = MatchKind::ExactName;  score = 9000; }
        else if (c.name.rfind(t, 0) == 0) {
            kind = MatchKind::Prefix;
            score = 5000 - static_cast<int>(c.name.size());
        } else {
            bool alias_prefix = false;
            for (const auto& a : c.aliases)
                if (a.rfind(t, 0) == 0) { alias_prefix = true; break; }
            if (alias_prefix) { kind = MatchKind::Prefix; score = 4000; }
            else {
                const int f = fuzzy_score(t, c.name);
                if (f < 0) continue;
                kind = MatchKind::Fuzzy;
                score = f;
            }
        }
        out.push_back({&c, kind, score});
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const Match& a, const Match& b) { return a.score > b.score; });
    if (out.size() > limit) out.resize(limit);
    return out;
}

}
