#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/script.hpp"

namespace gcmd {

enum class LispType : gcm::u8 { Nil, Int, Real, Str, Sym, List, Fun, True };

struct LispValue;
using LispList = std::vector<LispValue>;

struct LispValue {
    LispType    type{LispType::Nil};
    gcm::i64    integer{0};
    double      real{0.0};
    std::string text;
    LispList    items;

    static LispValue nil() noexcept { return {}; }
    static LispValue truth() noexcept { LispValue v; v.type = LispType::True; return v; }
    static LispValue num(gcm::i64 i);
    static LispValue num(double d);
    static LispValue str(std::string s);
    static LispValue sym(std::string s);
    static LispValue list(LispList l);

    bool truthy() const noexcept { return type != LispType::Nil; }
    bool numeric() const noexcept {
        return type == LispType::Int || type == LispType::Real;
    }
    double as_real() const noexcept {
        return type == LispType::Int ? static_cast<double>(integer) : real;
    }
    gcm::i64 as_int() const noexcept {
        return type == LispType::Real ? static_cast<gcm::i64>(real) : integer;
    }
    std::string print() const;
};

struct ParseResult {
    LispList    forms;
    bool        ok{true};
    std::string error;
    std::size_t line{0};
};

ParseResult parse(std::string_view text);

struct LispLimits {
    std::size_t max_steps{200000};
    std::size_t max_depth{256};
    std::size_t max_list{100000};
};

struct LispError {
    std::string message;
    std::string form;
    bool        none() const noexcept { return message.empty(); }
};

class Interpreter {
public:
    Interpreter();

    LispLimits limits;

    std::unordered_map<std::string, LispValue>& variables() noexcept { return vars_; }

    const Script& script() const noexcept { return script_; }
    void          clear_script() noexcept { script_ = Script{}; }

    const std::vector<std::string>& output() const noexcept { return output_; }

    const LispError& error() const noexcept { return error_; }
    std::size_t      steps() const noexcept { return steps_; }

    LispValue eval(const LispValue& form);
    LispValue run(std::string_view text);

    using Builtin = std::function<LispValue(Interpreter&, const LispList& args)>;
    void define(std::string_view name, Builtin fn);

    bool defined(std::string_view name) const;

private:
    struct Frame {
        std::unordered_map<std::string, LispValue> locals;
    };

    LispValue eval_list(const LispValue& form);
    LispValue eval_call(const std::string& head, const LispList& form);
    LispValue apply(const LispValue& fn, const LispList& args,
                    const std::string& name);
    LispValue* lookup(const std::string& name);
    void       fail(std::string message, const LispValue& form);
    bool       step();

    std::unordered_map<std::string, LispValue> vars_;
    std::unordered_map<std::string, LispValue> funs_;
    std::unordered_map<std::string, Builtin>   builtins_;
    std::vector<Frame> frames_;

    Script                   script_;
    std::vector<std::string> output_;
    LispError                error_;
    std::size_t              steps_{0};
    std::size_t              depth_{0};
};

}
