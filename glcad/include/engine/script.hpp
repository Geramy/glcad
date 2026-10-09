#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/commandline.hpp"

namespace gcmd {

enum class StepKind : gcm::u8 {
    Input,
    Pause,
    Delay,
    Comment,
    Echo,
    Resume,
};

struct Step {
    StepKind    kind{StepKind::Input};
    std::string text;
    int         milliseconds{0};
    int         line{0};
};

struct Script {
    std::string        name;
    std::vector<Step>  steps;
    std::size_t        size() const noexcept { return steps.size(); }
    bool               empty() const noexcept { return steps.empty(); }
};

Script parse_scr(std::string_view text, std::string_view name = "script");

std::string write_scr(const Script& s);

enum class RunState : gcm::u8 { Idle, Running, Paused, Finished, Failed };

struct RunReport {
    RunState    state{RunState::Idle};
    std::size_t executed{0};
    std::size_t failed{0};
    std::size_t step{0};
    std::string message;
    bool ok() const noexcept { return state == RunState::Finished && failed == 0; }
};

class ScriptRunner {
public:
    ScriptRunner(CommandLine& cli, Registry& reg) : cli_(cli), reg_(reg) {}

    void load(Script s);
    void reset();

    bool step();
    RunReport run(int max_steps = 100000);

    RunState state() const noexcept { return state_; }
    std::size_t position() const noexcept { return pc_; }
    const Script& script() const noexcept { return script_; }
    const std::vector<std::string>& log() const noexcept { return log_; }

    void resume() { if (state_ == RunState::Paused) state_ = RunState::Running; }
    void stop() { state_ = RunState::Finished; }

    std::function<void(int)> on_delay;
    std::function<void(const std::string&)> on_echo;

    std::function<void(const std::string&)> on_input;

private:
    CommandLine& cli_;
    Registry&    reg_;
    Script       script_;
    std::size_t  pc_{0};
    RunState     state_{RunState::Idle};
    std::size_t  executed_{0}, failed_{0};
    std::vector<std::string> log_;
};

struct Argument {
    std::string name;
    std::string value;
};

struct Call {
    std::string           command;
    std::vector<Argument> arguments;

    const std::string* find(std::string_view name) const;
};

struct CallResult {
    bool        ok{false};
    std::string message;
    std::string output;
};

Script call_to_script(const Call& c);
CallResult invoke(CommandLine& cli, Registry& reg, const Call& c);

struct LispImport {
    Script                    script;
    std::vector<std::string>  unsupported;
    std::size_t               forms{0};
    bool ok() const noexcept { return unsupported.empty(); }
};

LispImport import_lisp(std::string_view text);

}
