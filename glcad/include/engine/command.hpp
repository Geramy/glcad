#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "math/config.hpp"

namespace gcmd {

using gcm::f64;
using gcm::i64;
using gcm::u32;

enum class Modes : gcm::u8 { TwoD = 1, ThreeD = 2, Both = 3 };

constexpr bool applies(Modes m, Modes lens) noexcept {
    return (static_cast<gcm::u8>(m) & static_cast<gcm::u8>(lens)) != 0;
}

enum class ArgKind : gcm::u8 {
    None, Point, Distance, Angle, Integer, Text, Keyword, Selection,
};

struct Prompt {
    std::string              text;
    ArgKind                  kind{ArgKind::Point};
    std::vector<std::string> keywords;
    bool                     optional{false};
};

struct Command {
    std::string              name;
    std::vector<std::string> aliases;
    std::string              description;
    std::string              group;
    Modes                    modes{Modes::Both};
    std::vector<Prompt>      prompts;
    bool                     repeatable{true};
    bool                     transparent{false};
};

enum class MatchKind : gcm::u8 { ExactAlias, ExactName, Prefix, Fuzzy };

struct Match {
    const Command* cmd{nullptr};
    MatchKind      kind{MatchKind::Fuzzy};
    int            score{0};
};

class Registry {
public:
    void add(Command c);
    void add_standard();
    bool remove(std::string_view name) {
        for (auto it = cmds_.begin(); it != cmds_.end(); ++it)
            if (it->name == name) { cmds_.erase(it); return true; }
        return false;
    }

    const Command* find(std::string_view token) const;
    const Command* by_name(std::string_view name) const;

    std::vector<Match> complete(std::string_view partial, Modes lens = Modes::Both,
                                std::size_t limit = 12) const;

    std::size_t size() const noexcept { return cmds_.size(); }
    const std::vector<Command>& all() const noexcept { return cmds_; }

    bool add_alias(std::string_view alias, std::string_view command_name);
    std::size_t load_pgp(std::string_view text);

private:
    std::vector<Command> cmds_;
};

std::string to_upper(std::string_view s);

}
