#include "test.hpp"

#include "engine/commandline.hpp"

using namespace gcmd;

static Registry standard() {
    Registry r;
    r.add_standard();
    return r;
}

TEST(cmd_the_standard_alias_set_resolves) {
    const Registry r = standard();
    struct { const char* alias; const char* name; } cases[] = {
        {"L", "LINE"},       {"PL", "PLINE"},    {"C", "CIRCLE"},
        {"A", "ARC"},        {"REC", "RECTANG"}, {"O", "OFFSET"},
        {"TR", "TRIM"},      {"EX", "EXTEND"},   {"F", "FILLET"},
        {"CHA", "CHAMFER"},  {"M", "MOVE"},      {"CO", "COPY"},
        {"RO", "ROTATE"},    {"MI", "MIRROR"},   {"SC", "SCALE"},
        {"H", "HATCH"},      {"LA", "LAYER"},    {"B", "BLOCK"},
        {"I", "INSERT"},     {"X", "EXPLODE"},   {"Z", "ZOOM"},
        {"P", "PAN"},        {"RE", "REGEN"},    {"E", "ERASE"},
        {"U", "UNDO"},       {"T", "MTEXT"},     {"D", "DIMSTYLE"},
        {"DLI", "DIMLINEAR"},{"LE", "LEADER"},   {"DI", "DIST"},
        {"EXT", "EXTRUDE"},  {"REV", "REVOLVE"}, {"SW", "SWEEP"},
        {"LOF", "LOFT"},     {"SH", "SHELL"},    {"UNI", "UNION"},
        {"SU", "SUBTRACT"},  {"IN", "INTERSECT"},{"CYL", "CYLINDER"},
    };
    for (const auto& c : cases) {
        const Command* cmd = r.find(c.alias);
        CHECK_MSG(cmd != nullptr, "alias %s resolved to nothing", c.alias);
        if (cmd) CHECK_MSG(cmd->name == c.name, "alias %s -> %s, expected %s",
                           c.alias, cmd->name.c_str(), c.name);
    }
}

TEST(cmd_alias_lookup_is_case_insensitive) {
    const Registry r = standard();
    CHECK(r.find("l")->name == "LINE");
    CHECK(r.find("Line")->name == "LINE");
    CHECK(r.find("LINE")->name == "LINE");
}

TEST(cmd_no_duplicate_aliases) {
    const Registry r = standard();
    std::vector<std::string> seen;
    for (const auto& c : r.all())
        for (const auto& a : c.aliases) {
            for (const auto& s : seen)
                CHECK_MSG(s != a, "alias %s is defined twice", a.c_str());
            seen.push_back(a);
        }
    CHECK(seen.size() > 60);
}

TEST(cmd_exact_alias_always_outranks_fuzzy) {
    const Registry r = standard();
    const auto m = r.complete("L");
    CHECK(!m.empty());
    CHECK_MSG(m[0].cmd->name == "LINE", "top match for L was %s",
              m[0].cmd->name.c_str());
    CHECK(m[0].kind == MatchKind::ExactAlias);
}

TEST(cmd_prefix_ranks_shorter_names_first) {
    const Registry r = standard();
    const auto m = r.complete("LIN");
    CHECK(!m.empty());
    CHECK_MSG(m[0].cmd->name == "LINE", "top match for LIN was %s",
              m[0].cmd->name.c_str());
}

TEST(cmd_completion_respects_the_active_lens) {
    const Registry r = standard();
    bool found_2d = false, found_3d = false;
    for (const auto& m : r.complete("EXT", Modes::TwoD, 30))
        if (m.cmd->name == "EXTRUDE") found_2d = true;
    for (const auto& m : r.complete("EXT", Modes::ThreeD, 30))
        if (m.cmd->name == "EXTRUDE") found_3d = true;
    CHECK(!found_2d);
    CHECK(found_3d);

    CHECK(applies(r.find("L")->modes, Modes::TwoD));
    CHECK(applies(r.find("L")->modes, Modes::ThreeD));
}

TEST(cmd_user_aliases_and_pgp_import) {
    Registry r = standard();
    CHECK(r.add_alias("LL", "LINE"));
    CHECK(r.find("LL")->name == "LINE");
    CHECK(!r.add_alias("ZZ", "NOSUCHCOMMAND"));

    const char* pgp =
        "; comment line\n"
        "3A,       *3DARRAY\n"
        "CI,       *CIRCLE\n"
        "QQ,       *ERASE\n";
    const std::size_t n = r.load_pgp(pgp);
    CHECK_MSG(n == 2, "expected 2 aliases loaded, got %zu", n);
    CHECK(r.find("CI")->name == "CIRCLE");
    CHECK(r.find("QQ")->name == "ERASE");
}

TEST(sysvar_standard_set_and_defaults) {
    SysVars v;
    v.add_standard();
    CHECK(v.size() > 40);
    CHECK(v.get_int("LUNITS") == 2);
    CHECK(v.get_real("LTSCALE") == 1.0);
    CHECK(v.get_text("CLAYER") == "0");
}

TEST(sysvar_osmode_is_a_bit_field_like_autocad) {
    SysVars v;
    v.add_standard();
    CHECK(v.osnap_enabled(Osnap_Endpoint));
    CHECK(v.osnap_enabled(Osnap_Midpoint));
    CHECK(!v.osnap_enabled(Osnap_Tangent));

    v.set_osnap(Osnap_Tangent, true);
    CHECK(v.osnap_enabled(Osnap_Tangent));

    v.set_osnap(Osnap_Off, true);
    CHECK(!v.osnap_enabled(Osnap_Endpoint));
    v.set_osnap(Osnap_Off, false);
    CHECK(v.osnap_enabled(Osnap_Endpoint));
}

TEST(sysvar_engine_variables_are_read_only) {
    SysVars v;
    v.add_standard();
    CHECK(v.get_text("KERNELUNITS") == "metre");
    CHECK(!v.set_text("KERNELUNITS", "inch"));
    CHECK(v.get_text("KERNELUNITS") == "metre");
}

TEST(cmdline_typing_an_alias_starts_the_command) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("L");
    const auto res = cl.submit();
    CHECK(res.kind == Submit::RanCommand);
    CHECK(res.command->name == "LINE");
    CHECK(cl.busy());
    CHECK(cl.prompt().active);
    CHECK(cl.prompt().text == "Specify first point");
}

TEST(cmdline_prompts_advance_and_the_line_tool_chains) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("LINE");
    cl.submit();
    CHECK(cl.prompt().text == "Specify first point");

    cl.supply_point();
    CHECK(cl.prompt().text == "Specify next point");

    cl.supply_point();
    CHECK(cl.busy());
    CHECK(cl.prompt().text == "Specify next point");
    cl.supply_point();
    CHECK(cl.busy());

    const auto done = cl.submit();
    CHECK(done.kind == Submit::Finished);
    CHECK(!cl.busy());
}

TEST(cmdline_keywords_answer_the_live_prompt) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("CIRCLE");
    cl.submit();
    CHECK(cl.prompt().keywords.size() == 3);   // 3P / 2P / Ttr

    cl.type("T");
    const auto k = cl.submit();
    CHECK(k.kind == Submit::Keyword);
    CHECK(k.keyword == "Ttr");
}

TEST(cmdline_reads_and_writes_system_variables) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("LUNITS");
    auto res = cl.submit();
    CHECK(res.kind == Submit::SetVariable);
    CHECK(res.message == "LUNITS = 2");

    cl.type("LUNITS 4");
    res = cl.submit();
    CHECK(res.kind == Submit::SetVariable);
    CHECK(v.get_int("LUNITS") == 4);
}

TEST(cmdline_enter_repeats_the_last_command) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("L");
    cl.submit();
    cl.cancel();

    const auto again = cl.submit();
    CHECK(again.kind == Submit::RanCommand);
    CHECK(again.command->name == "LINE");
}

TEST(cmdline_transparent_commands_do_not_cancel_the_active_one) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("LINE");
    cl.submit();
    cl.supply_point();
    CHECK(cl.prompt().text == "Specify next point");

    cl.type("Z");
    const auto z = cl.submit();
    CHECK(z.command->name == "ZOOM");
    CHECK_MSG(cl.busy() && cl.active()->name == "LINE",
              "a transparent command cancelled the active one");
    CHECK(cl.prompt().text == "Specify next point");
}

TEST(cmdline_unknown_command_reports_clearly) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);
    cl.type("NOTACOMMAND");
    const auto res = cl.submit();
    CHECK(res.kind == Submit::Unknown);
    CHECK(res.message.find("Unknown command") != std::string::npos);
}

TEST(cmdline_history_navigates) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("LINE"); cl.submit(); cl.cancel();
    cl.type("CIRCLE"); cl.submit(); cl.cancel();

    CHECK(cl.history().size() == 2);
    CHECK(*cl.history_prev() == "CIRCLE");
    CHECK(*cl.history_prev() == "LINE");
}

TEST(cmd_registry_covers_the_documented_surface) {
    const Registry r = standard();
    CHECK_MSG(r.size() >= 100, "only %zu commands registered", r.size());

    for (const char* n : {"LINE", "HATCH", "LAYER", "DIMLINEAR", "EXTRUDE",
                          "UNION", "PLOT", "OSNAP", "THEME", "MTEXT",
                          "XREF", "BLOCK", "ZOOM", "MASSPROP"})
        CHECK_MSG(r.by_name(n) != nullptr, "%s is not registered", n);
}

TEST(a_command_wins_over_a_system_variable_of_the_same_name) {
    Registry r = standard();
    SysVars v; v.add_standard();
    CommandLine cl(r, v);

    cl.type("DIMSTYLE");
    const auto cmd = cl.submit();
    CHECK_MSG(cmd.kind != Submit::SetVariable,
              "DIMSTYLE was read as a variable: %s", cmd.message.c_str());

    cl.type("OSMODE");
    const auto var = cl.submit();
    CHECK(var.kind == Submit::SetVariable);
    CHECK(var.message.find("OSMODE") != std::string::npos);

    cl.type("SETVAR DIMSTYLE");
    const auto sv = cl.submit();
    CHECK_MSG(sv.kind == Submit::SetVariable &&
                  sv.message.find("DIMSTYLE") != std::string::npos,
              "SETVAR could not read a shadowed variable: %s", sv.message.c_str());

    cl.type("SETVAR OSMODE 33");
    const auto set = cl.submit();
    CHECK(set.kind == Submit::SetVariable);
    CHECK_MSG(v.find("OSMODE") && v.find("OSMODE")->ival == 33,
              "SETVAR did not write the variable");
}
