#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/command.hpp"
#include "engine/sysvar.hpp"

namespace gcmd {

struct PromptState {
    bool                     active{false};
    std::string              text;
    ArgKind                  kind{ArgKind::None};
    std::vector<std::string> keywords;
};

enum class Submit : gcm::u8 {
    Nothing,
    RanCommand,
    Advanced,
    Finished,
    Keyword,
    SetVariable,
    Unknown,
};

struct SubmitResult {
    Submit         kind{Submit::Nothing};
    const Command* command{nullptr};
    std::string    keyword;
    std::string    message;
};

class CommandLine {
public:
    CommandLine(Registry& r, SysVars& v) : reg_(r), vars_(v) {}

    void        type(char c);
    void        type(std::string_view s);
    void        backspace();
    void        clear_input();
    const std::string& input() const noexcept { return input_; }

    SubmitResult submit();
    SubmitResult repeat_last();
    void         cancel();

    const Command* active() const noexcept { return active_; }
    PromptState    prompt() const;
    bool           busy() const noexcept { return active_ != nullptr; }

    SubmitResult supply_point();
    SubmitResult supply_value();

    std::vector<Match> completions(Modes lens = Modes::Both) const;
    const std::vector<std::string>& history() const noexcept { return history_; }
    std::optional<std::string> history_prev();
    std::optional<std::string> history_next();

    void set_lens(Modes m) noexcept { lens_ = m; }
    Modes lens() const noexcept { return lens_; }

private:
    SubmitResult start(const Command* c);
    SubmitResult advance();
    std::optional<std::string> match_keyword(std::string_view token) const;

    Registry& reg_;
    SysVars&  vars_;

    std::string              input_;
    std::vector<std::string> history_;
    int                      history_pos_{-1};

    const Command* active_{nullptr};
    const Command* last_{nullptr};
    std::size_t    prompt_index_{0};
    Modes          lens_{Modes::TwoD};
};

}
