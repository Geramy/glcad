#include "test.hpp"

#include <string>

#include "engine/script.hpp"

using namespace gcmd;

namespace {

struct Rig {
    Registry reg;
    SysVars vars;
    CommandLine cli{reg, vars};
    Rig() { reg.add_standard(); }
};

}

TEST(script_parses_a_line_per_step) {
    const Script s = parse_scr("LINE\n0,0\n10,0\n\n");
    CHECK_MSG(s.size() == 4, "%zu steps", s.size());
    CHECK(s.steps[0].text == "LINE");
    CHECK(s.steps[2].text == "10,0");
}

TEST(script_a_blank_line_is_an_enter) {
    const Script s = parse_scr("LINE\n0,0\n10,0\n\nZOOM\n");
    bool found_blank = false;
    for (const Step& st : s.steps)
        if (st.kind == StepKind::Input && st.text.empty()) found_blank = true;
    CHECK_MSG(found_blank, "the blank line was swallowed");
}

TEST(script_comments_and_directives) {
    const Script s = parse_scr("; a comment\nDELAY 250\nECHO hello there\n\\\nRESUME\n");
    CHECK(s.steps[0].kind == StepKind::Comment);
    CHECK(s.steps[1].kind == StepKind::Delay);
    CHECK(s.steps[1].milliseconds == 250);
    CHECK(s.steps[2].kind == StepKind::Echo);
    CHECK(s.steps[2].text == "hello there");
    CHECK(s.steps[3].kind == StepKind::Pause);
    CHECK(s.steps[4].kind == StepKind::Resume);
}

TEST(script_line_numbers_are_kept_for_reporting) {
    const Script s = parse_scr("LINE\n0,0\nNONSENSE\n");
    CHECK(s.steps[2].line == 3);
}

TEST(script_never_refuses_a_file) {
    const Script s = parse_scr("LINE\n@#$%^&*\nZOOM\n");
    CHECK(s.size() == 3);
    CHECK(s.steps[2].text == "ZOOM");
}

TEST(script_round_trips_through_write) {
    const char* source = "; setup\nLINE\n0,0\n10,0\n\nDELAY 100\nECHO done\n";
    const Script a = parse_scr(source);
    const Script b = parse_scr(write_scr(a));
    CHECK_MSG(a.size() == b.size(), "%zu vs %zu steps", a.size(), b.size());
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        CHECK(a.steps[i].kind == b.steps[i].kind);
        CHECK(a.steps[i].text == b.steps[i].text);
        CHECK(a.steps[i].milliseconds == b.steps[i].milliseconds);
    }
}

TEST(script_runs_commands_through_the_command_line) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    runner.load(parse_scr("LINE\nZOOM\n"));
    const RunReport rep = runner.run();
    CHECK_MSG(rep.ok(), "%s", rep.message.c_str());
    CHECK(rep.executed == 2);
    CHECK(rep.failed == 0);
    CHECK(runner.state() == RunState::Finished);
}

TEST(script_reports_the_lines_it_did_not_understand) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    runner.load(parse_scr("LINE\nNOTACOMMAND\nZOOM\n"));
    const RunReport rep = runner.run();
    CHECK_MSG(!rep.ok(), "an unknown command was reported as success");
    CHECK(rep.failed == 1);
    CHECK_MSG(rep.executed == 3, "%zu executed", rep.executed);

    bool named = false;
    for (const std::string& l : runner.log())
        if (l.find("NOTACOMMAND") != std::string::npos) named = true;
    CHECK_MSG(named, "the failing line was not named in the log");
}

TEST(script_pauses_and_resumes) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    runner.load(parse_scr("LINE\n\\\nZOOM\n"));

    const RunReport first = runner.run();
    CHECK(first.state == RunState::Paused);
    CHECK(first.message.find("waiting") != std::string::npos);

    runner.resume();
    const RunReport second = runner.run();
    CHECK(second.state == RunState::Finished);
    CHECK(second.executed == 2);
}

TEST(script_hands_out_delays_rather_than_sleeping) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    int total = 0;
    runner.on_delay = [&](int ms) { total += ms; };
    runner.load(parse_scr("DELAY 300\nDELAY 200\n"));
    runner.run();
    CHECK_MSG(total == 500, "the runner reported %d ms of delay", total);
}

TEST(script_echo_reaches_the_host) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    std::string heard;
    runner.on_echo = [&](const std::string& m) { heard = m; };
    runner.load(parse_scr("ECHO plotting sheet 3\n"));
    runner.run();
    CHECK(heard == "plotting sheet 3");
    CHECK(runner.log().size() == 1);
}

TEST(script_can_be_stepped_one_line_at_a_time) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    runner.load(parse_scr("LINE\nZOOM\nUNDO\n"));
    CHECK(runner.position() == 0);
    CHECK(runner.step());
    CHECK(runner.position() == 1);
    CHECK(runner.step());
    CHECK(runner.step());
    CHECK(!runner.step());
    CHECK(runner.state() == RunState::Finished);
}

TEST(script_reset_starts_over) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    runner.load(parse_scr("LINE\nZOOM\n"));
    runner.run();
    runner.reset();
    CHECK(runner.position() == 0);
    CHECK(runner.state() == RunState::Running);
}

TEST(script_an_empty_script_finishes_immediately) {
    Rig r;
    ScriptRunner runner(r.cli, r.reg);
    runner.load(parse_scr(""));
    CHECK(runner.state() == RunState::Finished);
    CHECK(!runner.step());
}

TEST(script_a_structured_call_becomes_keystrokes) {
    Call c;
    c.command = "LINE";
    c.arguments = {{"start", "0,0"}, {"end", "10,0"}};

    const Script s = call_to_script(c);
    CHECK(s.size() == 3);
    CHECK(s.steps[0].text == "LINE");
    CHECK(s.steps[1].text == "0,0");
    CHECK(s.steps[2].text == "10,0");
}

TEST(script_invoke_runs_a_structured_call) {
    Rig r;
    Call c;
    c.command = "ZOOM";
    const CallResult res = invoke(r.cli, r.reg, c);
    CHECK_MSG(res.ok, "%s", res.message.c_str());
}

TEST(script_invoke_refuses_an_unknown_command) {
    Rig r;
    Call c;
    c.command = "NOTREAL";
    const CallResult res = invoke(r.cli, r.reg, c);
    CHECK(!res.ok);
    CHECK(res.message.find("NOTREAL") != std::string::npos);

    Call empty;
    CHECK(!invoke(r.cli, r.reg, empty).ok);
}

TEST(script_call_arguments_are_findable_by_name) {
    Call c;
    c.arguments = {{"radius", "5"}, {"centre", "0,0"}};
    CHECK(c.find("radius") != nullptr);
    CHECK(*c.find("radius") == "5");
    CHECK(c.find("nothing") == nullptr);
}

TEST(lisp_translates_a_command_form) {
    const LispImport i = import_lisp("(command \"LINE\" \"0,0\" \"10,0\" \"\")");
    CHECK_MSG(i.ok(), "%zu unsupported", i.unsupported.size());
    CHECK_MSG(i.script.size() == 4, "%zu steps", i.script.size());
    CHECK(i.script.steps[0].text == "LINE");
    CHECK(i.script.steps[1].text == "0,0");
    CHECK(i.script.steps[3].text.empty());     // the closing Enter
}

TEST(lisp_translates_point_lists) {
    const LispImport i =
        import_lisp("(command \"LINE\" (list 1.0 2.0) (list 3.5 -4.5) \"\")");
    CHECK_MSG(i.ok(), "unsupported: %zu", i.unsupported.size());
    CHECK(i.script.steps[1].text == "1.0,2.0");
    CHECK_MSG(i.script.steps[2].text == "3.5,-4.5", "got '%s'",
              i.script.steps[2].text.c_str());

    const LispImport q = import_lisp("(command \"CIRCLE\" '(0 0) \"5\")");
    CHECK(q.script.steps[1].text == "0,0");
}

TEST(lisp_translates_pause_and_prompts) {
    const LispImport i =
        import_lisp("(prompt \"Pick a point\")\n(command \"LINE\" pause pause \"\")");
    CHECK(i.script.steps[0].kind == StepKind::Echo);
    CHECK(i.script.steps[0].text == "Pick a point");
    int pauses = 0;
    for (const Step& s : i.script.steps)
        if (s.kind == StepKind::Pause) ++pauses;
    CHECK_MSG(pauses == 2, "%d pauses", pauses);
}

TEST(lisp_translates_setvar) {
    const LispImport i = import_lisp("(setvar \"OSMODE\" 175)");
    CHECK(i.ok());
    CHECK(i.script.size() == 3);
    CHECK(i.script.steps[0].text == "SETVAR");
    CHECK(i.script.steps[1].text == "OSMODE");
    CHECK(i.script.steps[2].text == "175");
}

TEST(lisp_refuses_what_it_cannot_translate_rather_than_approximating) {
    const LispImport i = import_lisp(
        "(defun c:mytool ()\n  (command \"LINE\")\n  (princ)\n)\n"
        "(command \"ZOOM\" \"E\")");
    CHECK_MSG(!i.ok(), "a defun was quietly accepted");
    CHECK_MSG(!i.unsupported.empty(), "nothing was reported as unsupported");
    CHECK(i.unsupported[0].find("defun") != std::string::npos);

    bool has_zoom = false;
    for (const Step& s : i.script.steps)
        if (s.text == "ZOOM") has_zoom = true;
    CHECK_MSG(has_zoom, "the supported form was lost with the unsupported one");
}

TEST(lisp_refuses_a_nested_expression_inside_a_command) {
    const LispImport i = import_lisp("(command \"CIRCLE\" (getpoint) \"5\")");
    CHECK(!i.ok());
    for (const Step& s : i.script.steps)
        CHECK_MSG(s.text.find("getpoint") == std::string::npos,
                  "an expression was typed as text: '%s'", s.text.c_str());
}

TEST(lisp_ignores_comments) {
    const LispImport i = import_lisp("; a header\n(command \"ZOOM\")  ; trailing\n");
    CHECK(i.ok());
    CHECK(i.script.size() == 1);
}

TEST(lisp_counts_the_forms_it_saw) {
    const LispImport i =
        import_lisp("(command \"A\")(command \"B\")(setvar \"X\" 1)");
    CHECK_MSG(i.forms == 3, "%zu forms", i.forms);
}

TEST(lisp_of_nothing_is_nothing) {
    const LispImport i = import_lisp("");
    CHECK(i.ok());
    CHECK(i.script.empty());
    CHECK(i.forms == 0);
}

TEST(lisp_output_runs_through_the_same_runner) {
    Rig r;
    const LispImport i = import_lisp("(command \"ZOOM\")");
    ScriptRunner runner(r.cli, r.reg);
    runner.load(i.script);
    const RunReport rep = runner.run();
    CHECK_MSG(rep.ok(), "%s", rep.message.c_str());
}
