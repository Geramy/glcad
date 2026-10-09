#include "engine/script.hpp"

#include <cctype>
#include <cstdlib>

namespace gcmd {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}

Script parse_scr(std::string_view text, std::string_view name) {
    Script out;
    out.name = std::string(name);

    int line_no = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view raw =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos
                                                          : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++line_no;

        const std::string line = trim(raw);
        Step s;
        s.line = line_no;

        if (line.empty()) {
            // A blank line is an Enter in .scr.
            s.kind = StepKind::Input;
            s.text.clear();
            out.steps.push_back(std::move(s));
            continue;
        }

        if (line[0] == ';') {
            s.kind = StepKind::Comment;
            s.text = line.substr(1);
            out.steps.push_back(std::move(s));
            continue;
        }

        const std::string up = upper(line);
        if (up == "\\" || up == "PAUSE") {
            s.kind = StepKind::Pause;
            out.steps.push_back(std::move(s));
            continue;
        }
        if (up == "RESUME") {
            s.kind = StepKind::Resume;
            out.steps.push_back(std::move(s));
            continue;
        }
        if (up.rfind("DELAY", 0) == 0) {
            s.kind = StepKind::Delay;
            s.milliseconds = std::atoi(line.c_str() + 5);
            out.steps.push_back(std::move(s));
            continue;
        }
        if (up.rfind("ECHO ", 0) == 0) {
            s.kind = StepKind::Echo;
            s.text = trim(std::string_view(line).substr(5));
            out.steps.push_back(std::move(s));
            continue;
        }

        s.kind = StepKind::Input;
        s.text = line;
        out.steps.push_back(std::move(s));
    }

    // Drop the trailing split's empty line, or each round trip adds an Enter.
    if (!out.steps.empty() && out.steps.back().kind == StepKind::Input &&
        out.steps.back().text.empty())
        out.steps.pop_back();

    return out;
}

std::string write_scr(const Script& s) {
    std::string out;
    for (const Step& step : s.steps) {
        switch (step.kind) {
            case StepKind::Input:   out += step.text; break;
            case StepKind::Comment: out += ";" + step.text; break;
            case StepKind::Pause:   out += "\\"; break;
            case StepKind::Resume:  out += "RESUME"; break;
            case StepKind::Delay:   out += "DELAY " + std::to_string(step.milliseconds);
                                    break;
            case StepKind::Echo:    out += "ECHO " + step.text; break;
        }
        out.push_back('\n');
    }
    return out;
}

void ScriptRunner::load(Script s) {
    script_ = std::move(s);
    reset();
}

void ScriptRunner::reset() {
    pc_ = 0;
    executed_ = 0;
    failed_ = 0;
    log_.clear();
    state_ = script_.empty() ? RunState::Finished : RunState::Running;
}

bool ScriptRunner::step() {
    if (state_ == RunState::Paused || state_ == RunState::Finished ||
        state_ == RunState::Failed)
        return false;
    if (pc_ >= script_.steps.size()) {
        state_ = RunState::Finished;
        return false;
    }

    const Step& s = script_.steps[pc_++];
    switch (s.kind) {
        case StepKind::Comment:
            return true;

        case StepKind::Echo:
            log_.push_back(s.text);
            if (on_echo) on_echo(s.text);
            return true;

        case StepKind::Delay:
            if (on_delay) on_delay(s.milliseconds);
            return true;

        case StepKind::Pause:
            state_ = RunState::Paused;
            return true;

        case StepKind::Resume:
            state_ = RunState::Running;
            return true;

        case StepKind::Input: {
            if (on_input) {
                on_input(s.text);
                ++executed_;
                return true;
            }
            for (char c : s.text) cli_.type(c);
            const SubmitResult r = cli_.submit();
            ++executed_;
            if (r.kind == Submit::Unknown) {
                ++failed_;
                log_.push_back("line " + std::to_string(s.line) + ": unknown '" +
                               s.text + "'");
            } else if (!r.message.empty()) {
                log_.push_back(r.message);
            }
            return true;
        }
    }
    return true;
}

RunReport ScriptRunner::run(int max_steps) {
    RunReport out;
    int guard = 0;
    while (guard++ < max_steps && step()) {
        if (state_ == RunState::Paused) break;
    }

    out.state = state_;
    out.executed = executed_;
    out.failed = failed_;
    out.step = pc_;
    if (state_ == RunState::Paused) out.message = "waiting for input";
    else if (failed_ > 0)
        out.message = std::to_string(failed_) + " step(s) were not understood";
    else if (state_ == RunState::Finished) out.message = "finished";
    return out;
}

const std::string* Call::find(std::string_view name) const {
    for (const Argument& a : arguments)
        if (a.name == name) return &a.value;
    return nullptr;
}

Script call_to_script(const Call& c) {
    Script s;
    s.name = c.command;

    Step cmd;
    cmd.kind = StepKind::Input;
    cmd.text = c.command;
    s.steps.push_back(std::move(cmd));

    for (const Argument& a : c.arguments) {
        Step step;
        step.kind = StepKind::Input;
        step.text = a.value;
        s.steps.push_back(std::move(step));
    }
    return s;
}

CallResult invoke(CommandLine& cli, Registry& reg, const Call& c) {
    CallResult out;
    if (c.command.empty()) {
        out.message = "no command named";
        return out;
    }
    if (!reg.find(c.command)) {
        out.message = "no such command: " + c.command;
        return out;
    }

    ScriptRunner runner(cli, reg);
    runner.load(call_to_script(c));
    const RunReport r = runner.run();

    out.ok = r.failed == 0;
    out.message = r.message;
    for (const std::string& l : runner.log()) {
        if (!out.output.empty()) out.output.push_back('\n');
        out.output += l;
    }
    return out;
}

namespace {

std::vector<std::string> split_form(std::string_view body) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool in_string = false;

    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (in_string) {
            cur.push_back(c);
            if (c == '"') {
                in_string = false;
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        if (c == '"') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            in_string = true;
            cur.push_back(c);
            continue;
        }
        if (c == '(') {
            // A leading quote belongs to the list that follows it.
            if (depth == 0 && !cur.empty() && cur != "'") {
                out.push_back(cur);
                cur.clear();
            }
            ++depth;
            cur.push_back(c);
            continue;
        }
        if (c == ')') {
            cur.push_back(c);
            if (--depth == 0) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        if (depth == 0 && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

bool point_literal(const std::string& form, std::string& out) {
    std::string body = form;
    if (!body.empty() && body.front() == '\'') body.erase(body.begin());
    if (body.size() < 2 || body.front() != '(' || body.back() != ')') return false;
    body = body.substr(1, body.size() - 2);

    std::vector<std::string> parts = split_form(body);
    if (!parts.empty() && (parts[0] == "list" || parts[0] == "LIST"))
        parts.erase(parts.begin());
    if (parts.size() < 2 || parts.size() > 3) return false;

    out.clear();
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string& p = parts[i];
        if (p.empty()) return false;
        for (char ch : p)
            if (!std::isdigit(static_cast<unsigned char>(ch)) && ch != '.' &&
                ch != '-' && ch != '+' && ch != 'e' && ch != 'E')
                return false;
        if (i) out.push_back(',');
        out += p;
    }
    return true;
}

}

LispImport import_lisp(std::string_view text) {
    LispImport out;
    out.script.name = "lisp";

    std::size_t i = 0;
    int line = 1;
    while (i < text.size()) {
        if (text[i] == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (text[i] == ';') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (text[i] != '(') {
            ++i;
            continue;
        }

        const std::size_t start = i;
        int depth = 0;
        bool in_string = false;
        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (in_string) {
                if (c == '"') in_string = false;
                continue;
            }
            if (c == '"') in_string = true;
            else if (c == '(') ++depth;
            else if (c == ')' && --depth == 0) {
                ++i;
                break;
            }
        }

        const std::string form(text.substr(start, i - start));
        ++out.forms;

        const std::vector<std::string> parts =
            split_form(std::string_view(form).substr(1, form.size() - 2));
        if (parts.empty()) continue;

        const std::string head = parts[0];

        if (head == "command" || head == "COMMAND") {
            for (std::size_t k = 1; k < parts.size(); ++k) {
                Step s;
                s.kind = StepKind::Input;
                s.line = line;

                std::string pointed;
                if (point_literal(parts[k], pointed)) {
                    s.text = pointed;
                } else if (parts[k] == "pause" || parts[k] == "PAUSE") {
                    s.kind = StepKind::Pause;
                } else if (!parts[k].empty() && parts[k].front() == '(') {
                    out.unsupported.push_back(parts[k]);
                    continue;
                } else {
                    s.text = unquote(parts[k]);
                }
                out.script.steps.push_back(std::move(s));
            }
            continue;
        }

        if (head == "setvar" || head == "SETVAR") {
            if (parts.size() >= 3) {
                Step s;
                s.kind = StepKind::Input;
                s.line = line;
                s.text = "SETVAR";
                out.script.steps.push_back(s);
                s.text = unquote(parts[1]);
                out.script.steps.push_back(s);
                s.text = unquote(parts[2]);
                out.script.steps.push_back(std::move(s));
                continue;
            }
        }

        if (head == "prompt" || head == "princ" || head == "PROMPT" ||
            head == "PRINC") {
            Step s;
            s.kind = StepKind::Echo;
            s.line = line;
            s.text = parts.size() > 1 ? unquote(parts[1]) : std::string();
            if (!s.text.empty()) out.script.steps.push_back(std::move(s));
            continue;
        }

        out.unsupported.push_back(form);
    }

    return out;
}

}
