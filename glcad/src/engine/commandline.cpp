#include "engine/commandline.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace gcmd {

void CommandLine::type(char c) { input_.push_back(c); history_pos_ = -1; }
void CommandLine::type(std::string_view s) { input_.append(s); history_pos_ = -1; }
void CommandLine::backspace() { if (!input_.empty()) input_.pop_back(); }
void CommandLine::clear_input() { input_.clear(); history_pos_ = -1; }

void CommandLine::cancel() {
    input_.clear();
    active_ = nullptr;
    prompt_index_ = 0;
    history_pos_ = -1;
}

PromptState CommandLine::prompt() const {
    PromptState p;
    if (!active_ || prompt_index_ >= active_->prompts.size()) return p;
    const Prompt& q = active_->prompts[prompt_index_];
    p.active = true;
    p.text = q.text;
    p.kind = q.kind;
    p.keywords = q.keywords;
    return p;
}

SubmitResult CommandLine::start(const Command* c) {
    active_ = c;
    last_ = c;
    prompt_index_ = 0;
    history_.push_back(c->name);
    input_.clear();
    history_pos_ = -1;

    SubmitResult r;
    r.command = c;
    if (c->prompts.empty()) {
        active_ = nullptr;
        r.kind = Submit::Finished;
        r.message = c->name;
    } else {
        r.kind = Submit::RanCommand;
        r.message = c->name + ": " + c->prompts[0].text;
    }
    return r;
}

SubmitResult CommandLine::advance() {
    SubmitResult r;
    r.command = active_;
    ++prompt_index_;

    if (!active_) { r.kind = Submit::Nothing; return r; }

    if (prompt_index_ >= active_->prompts.size()) {
        const bool chains = !active_->prompts.empty() &&
                            active_->prompts.back().kind == ArgKind::Point &&
                            active_->prompts.size() > 1;
        if (chains) {
            prompt_index_ = active_->prompts.size() - 1;
            r.kind = Submit::Advanced;
            r.message = active_->prompts[prompt_index_].text;
            return r;
        }
        r.kind = Submit::Finished;
        r.message = std::string(active_->name) + " complete";
        active_ = nullptr;
        prompt_index_ = 0;
        return r;
    }
    r.kind = Submit::Advanced;
    r.message = active_->prompts[prompt_index_].text;
    return r;
}

SubmitResult CommandLine::supply_point() { return advance(); }
SubmitResult CommandLine::supply_value() { return advance(); }

std::optional<std::string> CommandLine::match_keyword(std::string_view token) const {
    if (!active_ || prompt_index_ >= active_->prompts.size()) return std::nullopt;
    const std::string t = to_upper(token);
    if (t.empty()) return std::nullopt;

    // Capitals in a keyword are its shortcut: "Ttr" answers to T.
    for (const auto& k : active_->prompts[prompt_index_].keywords) {
        std::string caps;
        for (char c : k)
            if (std::isupper((unsigned char)c) || std::isdigit((unsigned char)c))
                caps.push_back(c);
        if (!caps.empty() && caps == t) return k;
        if (to_upper(k).rfind(t, 0) == 0) return k;
    }
    return std::nullopt;
}

SubmitResult CommandLine::submit() {
    SubmitResult r;

    std::string tok = input_;
    while (!tok.empty() && std::isspace((unsigned char)tok.front())) tok.erase(tok.begin());
    while (!tok.empty() && std::isspace((unsigned char)tok.back())) tok.pop_back();

    if (tok.empty()) return repeat_last();

    if (active_) {
        if (auto kw = match_keyword(tok)) {
            input_.clear();
            r.kind = Submit::Keyword;
            r.command = active_;
            r.keyword = *kw;
            r.message = *kw;
            return r;
        }
    }

    // A command of the same name wins over a system variable (e.g. DIMSTYLE).
    {
        std::string name = tok, value;
        const std::size_t sp = tok.find(' ');
        if (sp != std::string::npos) { name = tok.substr(0, sp); value = tok.substr(sp + 1); }
        const bool is_command = reg_.find(name) != nullptr;
        if (SysVar* v = !is_command ? vars_.find(name) : nullptr) {
            input_.clear();
            history_.push_back(tok);
            r.kind = Submit::SetVariable;
            if (value.empty()) {
                r.message = v->name + " = " +
                            (v->type == VarType::Text ? v->sval
                             : v->type == VarType::Int ? std::to_string(v->ival)
                                                       : std::to_string(v->rval));
            } else if (v->read_only) {
                r.message = v->name + " is read-only";
            } else {
                std::string error;
                r.message=vars_.set_value(v->name,value,&error)?v->name+" set to "+value:v->name+": "+error;
            }
            return r;
        }
    }

    if (tok.rfind("SETVAR ", 0) == 0 || tok.rfind("setvar ", 0) == 0) {
        std::string rest = tok.substr(7);
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
        std::string name = rest, value;
        if (const std::size_t sp = rest.find(' '); sp != std::string::npos) {
            name = rest.substr(0, sp);
            value = rest.substr(sp + 1);
        }
        input_.clear();
        history_.push_back(tok);
        r.kind = Submit::SetVariable;
        SysVar* v = vars_.find(name);
        if (!v) {
            r.message = name + " is not a system variable";
        } else if (value.empty()) {
            r.message = v->name + " = " +
                        (v->type == VarType::Text ? v->sval
                         : v->type == VarType::Int ? std::to_string(v->ival)
                                                   : std::to_string(v->rval));
        } else if (v->read_only) {
            r.message = v->name + " is read-only";
        } else {
            std::string error;
            r.message=vars_.set_value(v->name,value,&error)?v->name+" set to "+value:v->name+": "+error;
        }
        return r;
    }

    if (const Command* c = reg_.find(tok)) {
        if (active_ && c->transparent) {
            const Command* keep = active_;
            const std::size_t keep_i = prompt_index_;
            SubmitResult t = start(c);
            active_ = keep;
            prompt_index_ = keep_i;
            t.message = "'" + c->name;
            return t;
        }
        return start(c);
    }

    input_.clear();
    r.kind = Submit::Unknown;
    r.message = "Unknown command \"" + tok + "\". Press F1 for help.";
    return r;
}

SubmitResult CommandLine::repeat_last() {
    SubmitResult r;
    if (active_) {
        r.kind = Submit::Finished;
        r.command = active_;
        r.message = std::string(active_->name) + " complete";
        active_ = nullptr;
        prompt_index_ = 0;
        return r;
    }
    if (last_ && last_->repeatable) return start(last_);
    r.kind = Submit::Nothing;
    return r;
}

std::vector<Match> CommandLine::completions(Modes lens) const {
    return reg_.complete(input_, lens);
}

std::optional<std::string> CommandLine::history_prev() {
    if (history_.empty()) return std::nullopt;
    if (history_pos_ < 0) history_pos_ = static_cast<int>(history_.size());
    if (history_pos_ == 0) return history_[0];
    --history_pos_;
    input_ = history_[history_pos_];
    return input_;
}

std::optional<std::string> CommandLine::history_next() {
    if (history_.empty() || history_pos_ < 0) return std::nullopt;
    ++history_pos_;
    if (history_pos_ >= static_cast<int>(history_.size())) {
        history_pos_ = -1;
        input_.clear();
        return std::string{};
    }
    input_ = history_[history_pos_];
    return input_;
}

}
