#include "ui/commands.hpp"

#include <string>

#include "ui/shell.hpp"

namespace gcapp {

namespace {

void apply(CommandContext& c, const char* name, gsolve::Kind kind, double value) {
    Canvas& canvas = c.canvas();
    const bool ok = canvas.apply_constraint(kind, value);
    c.log(canvas.message().empty()
              ? (ok ? "constraint applied" : std::string(name) + ": could not apply that")
              : canvas.message());
}

struct GeomConstraint : CommandBase<GeomConstraint> {
    static constexpr auto name = "GEOMCONSTRAINT";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        const std::string u = c.upper_arg();
        struct Named { const char* name; gsolve::Kind kind; };
        static const Named kinds[] = {
            {"COINCIDENT", gsolve::Kind::Coincident}, {"HORIZONTAL", gsolve::Kind::Horizontal},
            {"VERTICAL", gsolve::Kind::Vertical},     {"PARALLEL", gsolve::Kind::Parallel},
            {"PERPENDICULAR", gsolve::Kind::Perpendicular}, {"COLLINEAR", gsolve::Kind::Collinear},
            {"EQUAL", gsolve::Kind::Equal},           {"TANGENT", gsolve::Kind::Tangent},
            {"CONCENTRIC", gsolve::Kind::Concentric}, {"SYMMETRIC", gsolve::Kind::Symmetric},
            {"FIX", gsolve::Kind::FixPoint}};
        gsolve::Kind kind = gsolve::Kind::Symmetric;
        bool found = false;
        for (const auto& k : kinds) if (u == k.name) { kind = k.kind; found = true; }
        if (!found) {
            c.log("GEOMCONSTRAINT <kind>: coincident, horizontal, vertical, parallel, perpendicular, collinear, equal, tangent, concentric, symmetric, fix");
            return;
        }
        apply(c, name, kind, 0.0);
    }
};

struct GcSymmetric : CommandBase<GcSymmetric> {
    static constexpr auto name = "GCSYMMETRIC";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        apply(c, name, gsolve::Kind::Symmetric, 0.0);
    }
};

struct DimConstraint : CommandBase<DimConstraint> {
    static constexpr auto name = "DIMCONSTRAINT";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        if (!c.have_nums() || c.nums()[0] <= 0.0) {
            c.ask("DIMCONSTRAINT", "distance", "DIMCONSTRAINT: type the distance to hold between the two selected points, e.g. 2.5");
            return;
        }
        apply(c, name, gsolve::Kind::Distance, c.nums()[0]);
    }
};

struct AutoConstrain : CommandBase<AutoConstrain> {
    static constexpr auto name = "AUTOCONSTRAIN";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        canvas.auto_constrain();
        c.log(canvas.message());
        c.mark_dirty();
    }
};

}

void register_parametric_commands(CommandTable& t) {
    t.add<GeomConstraint>();
    t.add<GcSymmetric>();
    t.add<DimConstraint>();
    t.add<AutoConstrain>();
}

}
