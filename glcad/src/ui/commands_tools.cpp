#include "ui/commands.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "ui/shell.hpp"

namespace gcapp {

namespace {

struct Pgp : CommandBase<Pgp> {
    static constexpr auto name = "PGP";

    void execute(CommandContext& c) const {
        if (c.arg().empty()) { c.log("PGP <file.pgp>: loads command aliases"); return; }
        std::ifstream f(c.arg());
        if (!f) { c.log("PGP: could not open " + c.arg()); return; }
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::size_t got = c.shell().commands().load_pgp(ss.str());
        c.log("loaded " + std::to_string(got) + " alias(es) from " + c.arg());
    }
};

}

void register_tools_commands(CommandTable& t) {
    t.add<Pgp>();
}

}
