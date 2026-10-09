#include "test.hpp"

#include <cmath>

#include "engine/lisp.hpp"

using namespace gcmd;

namespace {

LispValue run(std::string_view src, Interpreter& in) { return in.run(src); }

std::string result(std::string_view src) {
    Interpreter in;
    const LispValue v = in.run(src);
    if (!in.error().none()) return "!" + in.error().message;
    return v.print();
}

}

TEST(lisp_reads_atoms_lists_and_strings) {
    const ParseResult p = parse("(setq a 1) (foo \"bar\" 2.5 nil T)");
    CHECK_MSG(p.ok, "%s", p.error.c_str());
    CHECK(p.forms.size() == 2);
    CHECK(p.forms[0].type == LispType::List);
    CHECK(p.forms[0].items.size() == 3);
    CHECK(p.forms[1].items[1].type == LispType::Str);
    CHECK(p.forms[1].items[1].text == "bar");
    CHECK(p.forms[1].items[2].type == LispType::Real);
    CHECK(p.forms[1].items[3].type == LispType::Nil);
    CHECK(p.forms[1].items[4].type == LispType::True);
}

TEST(lisp_distinguishes_integers_from_reals) {
    CHECK(result("(/ 7 2)") == "3");
    CHECK(result("(/ 7 2.0)") == "3.5");
    CHECK(result("(+ 1 2)") == "3");
    CHECK(result("(+ 1 2.0)") == "3");
    CHECK(result("(* 2 3)") == "6");
}

TEST(lisp_reports_unbalanced_parentheses_with_a_line) {
    const ParseResult p = parse("(defun foo ()\n  (bar)\n");
    CHECK(!p.ok);
    CHECK(!p.error.empty());
    CHECK_MSG(p.line >= 1, "no line number was reported");

    const ParseResult q = parse("(foo))");
    CHECK(!q.ok);
}

TEST(lisp_reports_an_unterminated_string) {
    const ParseResult p = parse("(prompt \"never closed)");
    CHECK(!p.ok);
    CHECK(p.error.find("string") != std::string::npos);
}

TEST(lisp_skips_both_kinds_of_comment) {
    const ParseResult p = parse(
        "; a line comment\n"
        ";| a block\n   comment |;\n"
        "(setq a 1)");
    CHECK_MSG(p.ok, "%s", p.error.c_str());
    CHECK(p.forms.size() == 1);
}

TEST(lisp_quote_is_a_reader_macro) {
    CHECK(result("'(1 2 3)") == "(1 2 3)");
    CHECK(result("(quote (1 2))") == "(1 2)");
    CHECK(result("'foo") == "foo");
}

TEST(lisp_symbols_are_case_insensitive) {
    CHECK(result("(SETQ A 5) (+ a 1)") == "6");
    CHECK(result("(Defun Foo () 7) (FOO)") == "7");
}

TEST(lisp_only_nil_is_false) {
    CHECK(result("(if 0 \"yes\" \"no\")") == "\"yes\"");
    CHECK(result("(if \"\" \"yes\" \"no\")") == "\"yes\"");
    CHECK(result("(if nil \"yes\" \"no\")") == "\"no\"");
    CHECK(result("(if '() \"yes\" \"no\")") == "\"no\"");
}

TEST(lisp_nil_and_the_empty_list_are_the_same_value) {
    CHECK(result("(null '())") == "T");
    CHECK(result("(null (cdr '(1)))") == "T");
    CHECK(result("(listp nil)") == "T");
}

TEST(lisp_defun_defines_a_callable_function) {
    CHECK(result("(defun sq (x) (* x x)) (sq 7)") == "49");
    CHECK(result("(defun add3 (a b c) (+ a b c)) (add3 1 2 3)") == "6");
}

TEST(lisp_a_missing_argument_is_nil_not_an_error) {
    CHECK(result("(defun f (a b) (if b \"two\" \"one\")) (f 1)") == "\"one\"");
}

TEST(lisp_locals_after_the_slash_do_not_leak) {
    Interpreter in;
    run("(setq n 100)"
        "(defun f (x / n) (setq n (* x 2)) n)"
        "(setq r (f 5))",
        in);
    CHECK(in.error().none());
    CHECK_MSG(in.variables()["r"].print() == "10", "got %s",
              in.variables()["r"].print().c_str());
    CHECK_MSG(in.variables()["n"].print() == "100",
              "a local leaked into the global: %s",
              in.variables()["n"].print().c_str());
}

TEST(lisp_locals_start_nil_rather_than_shadowing_a_global_value) {
    Interpreter in;
    run("(setq n 100) (defun f (/ n) n) (setq r (f))", in);
    CHECK(in.error().none());
    CHECK_MSG(in.variables()["r"].print() == "nil",
              "a local was born holding the global's value: %s",
              in.variables()["r"].print().c_str());
}

TEST(lisp_setq_assigns_to_the_innermost_binding_that_has_the_name) {
    Interpreter in;
    run("(setq g 1)"
        "(defun outer (/ g) (setq g 2) g)"
        "(setq r (outer))",
        in);
    CHECK(in.variables()["r"].print() == "2");
    CHECK(in.variables()["g"].print() == "1");
}

TEST(lisp_setq_takes_pairs) {
    Interpreter in;
    run("(setq a 1 b 2 c 3)", in);
    CHECK(in.variables()["a"].print() == "1");
    CHECK(in.variables()["b"].print() == "2");
    CHECK(in.variables()["c"].print() == "3");
}

TEST(lisp_recursion_works) {
    CHECK(result("(defun fact (n) (if (<= n 1) 1 (* n (fact (1- n))))) (fact 6)") ==
          "720");
}

TEST(lisp_cond_takes_the_first_true_clause) {
    CHECK(result("(cond ((= 1 2) \"a\") ((= 1 1) \"b\") (T \"c\"))") == "\"b\"");
    CHECK(result("(cond ((= 1 2) \"a\") (T \"c\"))") == "\"c\"");
    CHECK(result("(cond ((= 1 2) \"a\"))") == "nil");
}

TEST(lisp_a_cond_clause_with_only_a_test_returns_the_test) {
    CHECK(result("(cond (5))") == "5");
    CHECK(result("(cond (nil) (7))") == "7");
}

TEST(lisp_and_and_or_short_circuit) {
    Interpreter in;
    run("(setq ran nil) (defun mark () (setq ran T) T) (and nil (mark))", in);
    CHECK_MSG(in.variables()["ran"].print() == "nil",
              "and evaluated past a false test");

    Interpreter in2;
    run("(setq ran nil) (defun mark () (setq ran T) T) (or T (mark))", in2);
    CHECK(in2.variables()["ran"].print() == "nil");

    CHECK(result("(or nil nil 3)") == "3");
    CHECK(result("(and 1 2 3)") == "T");
}

TEST(lisp_while_loops_until_the_test_fails) {
    Interpreter in;
    run("(setq i 0 total 0)"
        "(while (< i 5) (setq total (+ total i)) (setq i (1+ i)))",
        in);
    CHECK(in.error().none());
    CHECK_MSG(in.variables()["total"].print() == "10", "got %s",
              in.variables()["total"].print().c_str());
}

TEST(lisp_repeat_runs_a_fixed_count) {
    Interpreter in;
    run("(setq n 0) (repeat 4 (setq n (1+ n)))", in);
    CHECK(in.variables()["n"].print() == "4");
}

TEST(lisp_repeat_with_a_negative_count_runs_nothing) {
    Interpreter in;
    in.limits.max_steps = 5000;
    run("(setq n 0) (repeat -3 (setq n (1+ n)))", in);
    CHECK(in.error().none());
    CHECK(in.variables()["n"].print() == "0");
}

TEST(lisp_foreach_binds_its_symbol_locally) {
    Interpreter in;
    run("(setq x 99 total 0)"
        "(foreach x '(1 2 3) (setq total (+ total x)))",
        in);
    CHECK(in.variables()["total"].print() == "6");
    CHECK_MSG(in.variables()["x"].print() == "99",
              "foreach clobbered the outer x");
}

TEST(lisp_a_runaway_loop_stops_and_says_so) {
    Interpreter in;
    in.limits.max_steps = 2000;
    run("(while T (setq x 1))", in);
    CHECK_MSG(!in.error().none(), "an infinite loop ran to completion");
    CHECK(in.error().message.find("too long") != std::string::npos);
    CHECK(in.steps() <= in.limits.max_steps + 4);
}

TEST(lisp_runaway_recursion_stops_and_says_so) {
    Interpreter in;
    in.limits.max_depth = 32;
    run("(defun f (n) (f (1+ n))) (f 0)", in);
    CHECK_MSG(!in.error().none(), "infinite recursion ran to completion");
}

TEST(lisp_list_operations) {
    CHECK(result("(car '(1 2 3))") == "1");
    CHECK(result("(cdr '(1 2 3))") == "(2 3)");
    CHECK(result("(cadr '(1 2 3))") == "2");
    CHECK(result("(caddr '(1 2 3))") == "3");
    CHECK(result("(cons 0 '(1 2))") == "(0 1 2)");
    CHECK(result("(append '(1 2) '(3 4))") == "(1 2 3 4)");
    CHECK(result("(length '(1 2 3))") == "3");
    CHECK(result("(nth 1 '(1 2 3))") == "2");
    CHECK(result("(reverse '(1 2 3))") == "(3 2 1)");
    CHECK(result("(member 2 '(1 2 3))") == "(2 3)");
    CHECK(result("(member 9 '(1 2 3))") == "nil");
}

TEST(lisp_list_access_out_of_range_is_nil_not_a_crash) {
    CHECK(result("(nth 9 '(1 2))") == "nil");
    CHECK(result("(nth -1 '(1 2))") == "nil");
    CHECK(result("(car nil)") == "nil");
    CHECK(result("(cdr nil)") == "nil");
    CHECK(result("(cadr '(1))") == "nil");
}

TEST(lisp_mapcar_and_apply) {
    CHECK_MSG(result("(mapcar '1+ '(1 2 3))") == "(2 3 4)",
              "got %s", result("(mapcar '1+ '(1 2 3))").c_str());

    Interpreter in;
    const LispValue v = run("(defun d (x) (* 2 x)) (mapcar 'd '(1 2 3))", in);
    CHECK_MSG(in.error().none(), "%s", in.error().message.c_str());
    CHECK_MSG(v.print() == "(2 4 6)", "got %s", v.print().c_str());

    Interpreter in2;
    const LispValue s = run("(defun add (a b c) (+ a b c)) (apply 'add '(1 2 3))", in2);
    CHECK_MSG(in2.error().none(), "%s", in2.error().message.c_str());
    CHECK(s.print() == "6");

    CHECK(result("(apply '+ '(1 2 3))") == "6");
    CHECK(result("(apply 'strcat '(\"a\" \"b\"))") == "\"ab\"");
}

TEST(lisp_mapcar_over_strings_does_not_evaluate_them_twice) {
    CHECK(result("(mapcar 'strlen '(\"ab\" \"cde\"))") == "(2 3)");
    Interpreter in;
    const LispValue v = run("(mapcar 'car '((1 2) (3 4)))", in);
    CHECK_MSG(in.error().none(), "%s", in.error().message.c_str());
    CHECK_MSG(v.print() == "(1 3)", "got %s", v.print().c_str());
}

TEST(lisp_lambda_can_be_applied) {
    Interpreter in;
    const LispValue v = run("(mapcar (lambda (x) (* x x)) '(2 3 4))", in);
    CHECK_MSG(in.error().none(), "%s", in.error().message.c_str());
    CHECK(v.print() == "(4 9 16)");
}

TEST(lisp_substr_indexes_from_one) {
    CHECK(result("(substr \"ABCDEF\" 1 3)") == "\"ABC\"");
    CHECK(result("(substr \"ABCDEF\" 4)") == "\"DEF\"");
    CHECK(result("(substr \"ABCDEF\" 3 2)") == "\"CD\"");
    CHECK(result("(substr \"ABC\" 9)") == "\"\"");
}

TEST(lisp_string_functions) {
    CHECK(result("(strcat \"a\" \"b\" \"c\")") == "\"abc\"");
    CHECK(result("(strlen \"hello\")") == "5");
    CHECK(result("(itoa 42)") == "\"42\"");
    CHECK(result("(atoi \"42\")") == "42");
    CHECK(result("(atof \"1.5\")") == "1.5");
    CHECK(result("(strcase \"abc\")") == "\"ABC\"");
    CHECK(result("(strcase \"ABC\" T)") == "\"abc\"");
    CHECK(result("(rtos 3.14159 2 2)") == "\"3.14\"");
}

TEST(lisp_strings_compare_as_strings_not_as_numbers) {
    CHECK(result("(= \"WALLS\" \"DOORS\")") == "nil");
    CHECK(result("(= \"WALLS\" \"WALLS\")") == "T");
    CHECK(result("(< \"a\" \"b\")") == "T");
}

TEST(lisp_divide_by_zero_is_reported_not_silently_infinite) {
    Interpreter in;
    run("(/ 1 0)", in);
    CHECK(!in.error().none());
    CHECK(in.error().message.find("zero") != std::string::npos);

    Interpreter in2;
    run("(/ 1.0 0.0)", in2);
    CHECK(!in2.error().none());
}

TEST(lisp_unary_minus_and_divide) {
    CHECK(result("(- 5)") == "-5");
    CHECK(result("(/ 4.0)") == "0.25");
}

TEST(lisp_maths) {
    CHECK(result("(abs -3)") == "3");
    CHECK(result("(min 3 1 2)") == "1");
    CHECK(result("(max 3 1 2)") == "3");
    CHECK(result("(fix 3.9)") == "3");
    CHECK(result("(rem 7 3)") == "1");
    CHECK(result("(expt 2 10)") == "1024");
    Interpreter in;
    CHECK(std::fabs(run("(sqrt 2)", in).as_real() - std::sqrt(2.0)) < 1e-12);
}

TEST(lisp_comparison_chains) {
    CHECK(result("(< 1 2 3)") == "T");
    CHECK(result("(< 1 3 2)") == "nil");
    CHECK(result("(= 2 2 2)") == "T");
    CHECK(result("(>= 3 3 1)") == "T");
}

TEST(lisp_an_unimplemented_function_is_an_error_not_a_no_op) {
    Interpreter in;
    run("(vla-get-something 1)", in);
    CHECK_MSG(!in.error().none(), "an unknown function was quietly ignored");
    CHECK(in.error().message.find("no such function") != std::string::npos);
    CHECK_MSG(!in.error().form.empty(), "the failing form was not reported");
}

TEST(lisp_evaluation_stops_at_the_first_error) {
    Interpreter in;
    run("(setq a 1) (nope) (setq a 2)", in);
    CHECK(!in.error().none());
    CHECK_MSG(in.variables()["a"].print() == "1",
              "evaluation continued past the failure");
}

TEST(lisp_the_first_failure_is_the_one_reported) {
    Interpreter in;
    run("(defun f () (nope) (also-nope)) (f)", in);
    CHECK(!in.error().none());
    CHECK(in.error().message.find("nope") != std::string::npos);
    CHECK(in.error().message.find("also-nope") == std::string::npos);
}

TEST(lisp_an_unbound_symbol_is_nil_not_an_error) {
    Interpreter in;
    const LispValue v = run("(if never-set 1 2)", in);
    CHECK(in.error().none());
    CHECK(v.print() == "2");
}

TEST(lisp_command_becomes_script_steps_not_a_second_command_path) {
    Interpreter in;
    run("(command \"LINE\" '(0 0) '(10 0) \"\")", in);
    CHECK_MSG(in.error().none(), "%s", in.error().message.c_str());
    const Script& s = in.script();
    CHECK_MSG(s.size() == 4, "%zu steps", s.size());
    CHECK(s.steps[0].text == "LINE");
    CHECK_MSG(s.steps[1].text == "0,0", "got '%s'", s.steps[1].text.c_str());
    CHECK(s.steps[2].text == "10,0");
    CHECK(s.steps[3].text.empty());
}

TEST(lisp_a_point_list_reaches_the_command_line_as_coordinates) {
    Interpreter in;
    run("(setq p (list 1.5 -2.25 3.0)) (command \"MOVE\" p)", in);
    CHECK(in.error().none());
    CHECK(in.script().size() == 2);
    CHECK_MSG(in.script().steps[1].text == "1.5,-2.25,3", "got '%s'",
              in.script().steps[1].text.c_str());
}

TEST(lisp_a_loop_that_draws_produces_one_step_run_per_iteration) {
    Interpreter in;
    run("(setq i 0)"
        "(while (< i 3)"
        "  (command \"CIRCLE\" (list i 0) 1.0)"
        "  (setq i (1+ i)))",
        in);
    CHECK_MSG(in.error().none(), "%s", in.error().message.c_str());
    CHECK_MSG(in.script().size() == 9, "%zu steps for three circles",
              in.script().size());
    CHECK(in.script().steps[0].text == "CIRCLE");
    CHECK(in.script().steps[1].text == "0,0");
    CHECK(in.script().steps[4].text == "1,0");
    CHECK(in.script().steps[7].text == "2,0");
}

TEST(lisp_setvar_and_getvar_share_the_variable_table) {
    Interpreter in;
    const LispValue v = run("(setvar \"OSMODE\" 35) (getvar \"OSMODE\")", in);
    CHECK(in.error().none());
    CHECK(v.print() == "35");
    CHECK(result("(getvar \"NEVERSET\")") == "nil");
}

TEST(lisp_prompt_writes_to_the_interpreter_not_the_console) {
    Interpreter in;
    run("(prompt \"Pick a point\") (princ \"done\")", in);
    CHECK(in.error().none());
    CHECK_MSG(in.output().size() == 2, "%zu lines of output",
              in.output().size());
    CHECK(in.output()[0] == "Pick a point");
    CHECK(in.output()[1] == "done");
}

TEST(lisp_a_host_builtin_can_be_added) {
    Interpreter in;
    in.define("entcount", [](Interpreter&, const LispList&) {
        return LispValue::num(gcm::i64{17});
    });
    CHECK(in.defined("entcount"));
    CHECK(in.defined("ENTCOUNT"));
    CHECK(!in.defined("nothing-here"));
    const LispValue v = run("(+ (entcount) 1)", in);
    CHECK_MSG(in.error().none(), "%s", in.error().message.c_str());
    CHECK(v.print() == "18");
}

TEST(lisp_a_real_routine_runs_end_to_end) {
    Interpreter in;
    run(R"LISP(
        ;| Draw a circular bolt pattern.
           n holes of diameter d on a pitch circle of radius r. |;
        (defun bolt-circle (n r d / i a step)
          (setq step (/ 6.283185307179586 n))
          (setq i 0)
          (while (< i n)
            (setq a (* i step))
            (command "CIRCLE" (list (* r (cos a)) (* r (sin a))) (/ d 2.0))
            (setq i (1+ i)))
          (strcat "placed " (itoa n) " holes"))

        (setq msg (bolt-circle 6 50.0 10.0))
        (prompt msg)
    )LISP", in);

    CHECK_MSG(in.error().none(), "%s (%s)", in.error().message.c_str(),
              in.error().form.c_str());
    CHECK_MSG(in.script().size() == 18, "%zu steps for six holes",
              in.script().size());
    CHECK(in.output().size() == 1);
    CHECK_MSG(in.output()[0] == "placed 6 holes", "got '%s'",
              in.output()[0].c_str());

    // First hole on +X at the pitch radius; every hole has radius 5.0.
    CHECK(in.script().steps[0].text == "CIRCLE");
    CHECK(in.script().steps[1].text.rfind("50,0", 0) == 0);
    CHECK(in.script().steps[2].text == "5");
}
