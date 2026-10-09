#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/units.hpp"

namespace gunit {

enum class ParamKind : gcm::u8 {
    Length,
    Angle,
    Number,
    Text,
};

struct Parameter {
    std::string name;
    ParamKind   kind{ParamKind::Length};

    std::string expression;
    f64         value{0.0};

    std::string description;
    bool        ok{true};
    std::string error;

    bool is_expression() const noexcept {
        return !expression.empty() && expression.front() == '=';
    }
};

struct EvalReport {
    std::size_t evaluated{0};
    std::size_t failed{0};
    std::vector<std::string> cycles;
    bool ok() const noexcept { return failed == 0 && cycles.empty(); }
};

class ParameterTable {
public:
    Parameter&       add(Parameter p);
    const Parameter* find(std::string_view name) const;
    Parameter*       find(std::string_view name);
    bool             remove(std::string_view name);
    bool             rename(std::string_view from, std::string_view to);

    std::size_t size() const noexcept { return params_.size(); }
    const std::vector<Parameter>& all() const noexcept { return params_; }

    EvalReport evaluate(Length default_unit = Length::Millimetre);

    std::optional<f64> value_of(std::string_view name) const;

    static std::vector<std::string> references(std::string_view expression);

    std::vector<std::string> dependents(std::string_view name) const;

private:
    std::vector<Parameter> params_;
};

std::optional<f64> evaluate_expression(std::string_view expr,
                                       const ParameterTable& table,
                                       Length default_unit,
                                       std::string* error = nullptr);

struct DualFormat {
    Format primary{};
    Format secondary{};
    bool   enabled{false};
    std::string open{" ["};
    std::string close{"]"};
};

std::string format_dual(f64 metres, const DualFormat& f);

}
