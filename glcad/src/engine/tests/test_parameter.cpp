#include "test.hpp"

#include <algorithm>
#include <cmath>

#include "engine/parameter.hpp"

using namespace gunit;

namespace {
Parameter literal(const char* name, const char* expr,
                  ParamKind k = ParamKind::Length) {
    Parameter p;
    p.name = name;
    p.expression = expr;
    p.kind = k;
    return p;
}
}

TEST(param_literal_values_parse_in_the_drawing_unit) {
    ParameterTable t;
    t.add(literal("WallThickness", "200"));
    const EvalReport r = t.evaluate(Length::Millimetre);
    CHECK(r.ok());
    CHECK_MSG(std::fabs(*t.value_of("WallThickness") - 0.2) < 1e-12, "%.9f",
              *t.value_of("WallThickness"));
}

TEST(param_expression_references_another_parameter) {
    ParameterTable t;
    t.add(literal("WallThickness", "200"));
    t.add(literal("Cavity", "= WallThickness * 2"));
    CHECK(t.evaluate(Length::Millimetre).ok());
    CHECK_MSG(std::fabs(*t.value_of("Cavity") - 0.4) < 1e-9, "%.9f",
              *t.value_of("Cavity"));

    t.find("WallThickness")->expression = "300";
    t.evaluate(Length::Millimetre);
    CHECK_MSG(std::fabs(*t.value_of("Cavity") - 0.6) < 1e-9, "%.9f",
              *t.value_of("Cavity"));
}

TEST(param_evaluation_order_is_derived_not_assumed) {
    ParameterTable t;
    t.add(literal("Total", "= A + B"));
    t.add(literal("B", "= A * 3"));
    t.add(literal("A", "100"));

    const EvalReport r = t.evaluate(Length::Millimetre);
    CHECK_MSG(r.ok(), "%zu failed", r.failed);
    CHECK_MSG(std::fabs(*t.value_of("Total") - 0.4) < 1e-9, "%.9f",
              *t.value_of("Total"));
}

TEST(param_units_inside_an_expression_mean_what_they_always_mean) {
    ParameterTable t;
    t.add(literal("Base", "100"));
    t.add(literal("Offset", "= Base + 1in"));
    CHECK(t.evaluate(Length::Millimetre).ok());
    CHECK_MSG(std::fabs(*t.value_of("Offset") - (0.1 + 0.0254)) < 1e-9, "%.9f",
              *t.value_of("Offset"));
}

TEST(param_cycles_are_refused_and_named) {
    ParameterTable t;
    t.add(literal("A", "= B + 1"));
    t.add(literal("B", "= A + 1"));

    const EvalReport r = t.evaluate(Length::Millimetre);
    CHECK_MSG(!r.ok(), "a cycle was accepted");
    CHECK_MSG(r.cycles.size() == 2, "%zu named", r.cycles.size());
    CHECK(!t.find("A")->ok);
    CHECK(t.find("A")->error.find("circular") != std::string::npos);
}

TEST(param_self_reference_is_a_cycle) {
    ParameterTable t;
    t.add(literal("A", "= A * 2"));
    CHECK(!t.evaluate(Length::Millimetre).ok());
}

TEST(param_a_failed_expression_keeps_its_last_good_value) {
    ParameterTable t;
    t.add(literal("A", "100"));
    t.add(literal("B", "= A * 2"));
    t.evaluate(Length::Millimetre);
    const f64 good = *t.value_of("B");
    CHECK(std::fabs(good - 0.2) < 1e-9);

    t.find("B")->expression = "= Missing * 2";
    const EvalReport r = t.evaluate(Length::Millimetre);
    CHECK(r.failed == 1);
    CHECK(!t.find("B")->ok);
    CHECK_MSG(std::fabs(t.find("B")->value - good) < 1e-12,
              "the last good value was lost");
    CHECK(!t.find("B")->error.empty());
    CHECK(!t.value_of("B").has_value());
}

TEST(param_references_ignores_unit_suffixes) {
    const auto refs = ParameterTable::references("= Width * 2 + 10mm");
    CHECK_MSG(refs.size() == 1, "%zu references", refs.size());
    CHECK(refs[0] == "Width");
}

TEST(param_dependents_are_transitive) {
    ParameterTable t;
    t.add(literal("A", "10"));
    t.add(literal("B", "= A * 2"));
    t.add(literal("C", "= B + 5"));
    t.add(literal("D", "99"));

    const auto deps = t.dependents("A");
    CHECK_MSG(deps.size() == 2, "%zu dependents", deps.size());
    CHECK(std::find(deps.begin(), deps.end(), "C") != deps.end());
    CHECK(t.dependents("D").empty());
}

TEST(param_delete_is_refused_while_something_depends_on_it) {
    ParameterTable t;
    t.add(literal("A", "10"));
    t.add(literal("B", "= A * 2"));
    CHECK_MSG(!t.remove("A"), "a depended-on parameter was deleted");
    CHECK(t.remove("B"));
    CHECK(t.remove("A"));
    CHECK(t.size() == 0);
}

TEST(param_rename_rewrites_every_expression_that_used_it) {
    ParameterTable t;
    t.add(literal("W", "200"));
    t.add(literal("Cavity", "= W * 2"));
    t.add(literal("Other", "= W + 50"));

    CHECK(t.rename("W", "WallThickness"));
    CHECK(t.find("W") == nullptr);
    CHECK(t.find("WallThickness") != nullptr);
    CHECK_MSG(t.find("Cavity")->expression == "= WallThickness * 2", "'%s'",
              t.find("Cavity")->expression.c_str());
    CHECK(t.evaluate(Length::Millimetre).ok());
    CHECK(std::fabs(*t.value_of("Cavity") - 0.4) < 1e-9);
}

TEST(param_rename_refuses_a_clash_or_a_bad_name) {
    ParameterTable t;
    t.add(literal("A", "1"));
    t.add(literal("B", "2"));
    CHECK(!t.rename("A", "B"));
    CHECK(!t.rename("A", "9bad"));
    CHECK(!t.rename("Missing", "C"));
    CHECK(t.rename("A", "A"));
}

TEST(param_add_replaces_by_name) {
    ParameterTable t;
    t.add(literal("A", "1"));
    t.add(literal("A", "2"));
    CHECK(t.size() == 1);
    t.evaluate(Length::Millimetre);
    CHECK(std::fabs(*t.value_of("A") - 0.002) < 1e-12);
}

TEST(param_angles_evaluate_in_degrees) {
    ParameterTable t;
    Parameter p = literal("Slope", "30", ParamKind::Angle);
    t.add(p);
    t.evaluate(Length::Millimetre);
    CHECK_MSG(std::fabs(*t.value_of("Slope") - 3.14159265358979323846 / 6.0) < 1e-9,
              "%.9f", *t.value_of("Slope"));
}

TEST(param_empty_table_evaluates_cleanly) {
    ParameterTable t;
    const EvalReport r = t.evaluate(Length::Millimetre);
    CHECK(r.ok());
    CHECK(r.evaluated == 0);
    CHECK(!t.value_of("anything").has_value());
}

TEST(unit_dual_display_shows_both) {
    DualFormat d;
    d.enabled = true;
    d.primary.unit = Length::Millimetre;
    d.primary.precision = 0;
    d.secondary.unit = Length::Inch;
    d.secondary.style = Style::Fractional;
    d.secondary.precision = 4;

    const std::string s = format_dual(0.1016, d);   // 101.6 mm = 4 inches
    CHECK_MSG(s.find("102") != std::string::npos || s.find("101") != std::string::npos,
              "primary missing from '%s'", s.c_str());
    CHECK_MSG(s.find('[') != std::string::npos && s.find(']') != std::string::npos,
              "the secondary is not bracketed: '%s'", s.c_str());
    CHECK(s.find("4") != std::string::npos);
}

TEST(unit_dual_display_off_is_just_the_primary) {
    DualFormat d;
    d.primary.unit = Length::Millimetre;
    CHECK(format_dual(0.1, d) == format_length(0.1, d.primary));
}

TEST(unit_dual_brackets_are_configurable) {
    DualFormat d;
    d.enabled = true;
    d.open = " (";
    d.close = ")";
    const std::string s = format_dual(0.1, d);
    CHECK(s.find('(') != std::string::npos);
    CHECK(s.find('[') == std::string::npos);
}

TEST(param_a_bare_multiplier_still_works_after_substitution) {
    ParameterTable t;
    t.add(literal("W", "200"));
    t.add(literal("Half", "= W / 2"));
    t.add(literal("Triple", "= 3 * W"));
    t.add(literal("Sum", "= W + Half"));
    CHECK(t.evaluate(Length::Millimetre).ok());
    CHECK_MSG(std::fabs(*t.value_of("Half") - 0.1) < 1e-9, "%.9f", *t.value_of("Half"));
    CHECK(std::fabs(*t.value_of("Triple") - 0.6) < 1e-9);
    CHECK(std::fabs(*t.value_of("Sum") - 0.3) < 1e-9);
}

TEST(param_the_drawing_unit_does_not_change_a_stored_value) {
    ParameterTable a, b;
    a.add(literal("W", "1m"));
    b.add(literal("W", "1m"));
    a.evaluate(Length::Millimetre);
    b.evaluate(Length::Inch);
    CHECK(std::fabs(*a.value_of("W") - *b.value_of("W")) < 1e-12);
}
