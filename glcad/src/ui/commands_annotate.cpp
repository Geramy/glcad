#include "ui/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "ui/shell.hpp"

namespace gcapp {

namespace {

struct DimStyle : CommandBase<DimStyle> {
    static constexpr auto name = "DIMSTYLE";

    void execute(CommandContext& c) const {
        gdim::StyleTable& table = c.canvas().dim_styles();
        std::string a = c.arg();
        if (a.empty()) {
            std::string msg = "dimension styles: ";
            for (const auto& st : table.all()) {
                msg += st.name;
                if (st.name == table.current()) msg += " (current)";
                msg += "  ";
            }
            c.log(msg);
            return;
        }
        std::string name = a, field, value;
        if (const std::size_t sp = a.find(' '); sp != std::string::npos) {
            name = a.substr(0, sp);
            const std::string rest = a.substr(sp + 1);
            const std::size_t sp2 = rest.find(' ');
            field = sp2 == std::string::npos ? rest : rest.substr(0, sp2);
            value = sp2 == std::string::npos ? std::string{} : rest.substr(sp2 + 1);
            for (char& ch : field) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (!table.find(name)) {
            gdim::Style fresh = *table.find(table.current());
            fresh.name = name;
            table.add(std::move(fresh));
            c.log("created dimension style " + name);
        }
        if (field.empty()) {
            table.set_current(name);
            c.log("dimension style: " + name);
            return;
        }
        double v = 0.0;
        try { v = std::stod(value); } catch (...) {}
        gdim::Style* st = table.find_mut(name);
        if (!st) { c.log("no such dimension style"); return; }
        if      (field == "txt"  || field == "dimtxt")  st->dimtxt = v;
        else if (field == "asz"  || field == "dimasz")  st->dimasz = v;
        else if (field == "exe"  || field == "dimexe")  st->dimexe = v;
        else if (field == "exo"  || field == "dimexo")  st->dimexo = v;
        else if (field == "gap"  || field == "dimgap")  st->dimgap = v;
        else if (field == "dec"  || field == "dimdec")  st->dimdec = int(v);
        else if (field == "scale"|| field == "dimscale") st->dimscale = v;
        else if (field == "lfac" || field == "dimlfac") st->dimlfac = v;
        else if (field == "prefix") st->prefix = value;
        else if (field == "suffix") st->suffix = value;
        else {
            c.log("DIMSTYLE <name> [txt|asz|exe|exo|gap|dec|scale|lfac|"
                  "prefix|suffix] <value>");
            return;
        }
        c.invalidate_frame_caches();
        c.log(name + ": " + field + " = " + value);
    }
};

void run_dim_chain(CommandContext& c, const char* what, bool baseline) {
    const std::string n = what;
    if (c.canvas().dimensions().empty()) { c.log(n + ": place a linear dimension first"); return; }
    const gdim::Dimension last = c.canvas().dimensions().back();
    c.pick(1, n + ": click the next extension point",
           [&shell = c.shell(), last, baseline](const std::vector<gcm::Lattice2>& p) {
        CommandContext k(shell, {});
        gdim::Dimension d = last;
        d.p1 = baseline ? last.p1 : last.p2;
        d.p2 = p[0];
        if (baseline) {
            d.p3.x = last.p3.x + (last.p3.x - last.p1.x) / 2;
            d.p3.y = last.p3.y + (last.p3.y - last.p1.y) / 2;
        }
        d.text_override.clear();
        k.canvas().add_dimensions({d});
        k.log(baseline ? "baseline dimension added" : "continued dimension added");
    });
}

struct DimContinue : CommandBase<DimContinue> {
    static constexpr auto name = "DIMCONTINUE";
    void execute(CommandContext& c) const { run_dim_chain(c, name, false); }
};

struct DimBaseline : CommandBase<DimBaseline> {
    static constexpr auto name = "DIMBASELINE";
    void execute(CommandContext& c) const { run_dim_chain(c, name, true); }
};

struct DimArc : CommandBase<DimArc> {
    static constexpr auto name = "DIMARC";

    void execute(CommandContext& c) const {
        c.pick(2, "DIMARC: click the arc, then place the dimension",
               [&shell = c.shell()](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            double wx, wy;
            k.world(p[0], wx, wy);
            const gsnap::Shape* best = nullptr;
            double best_d = 1e300;
            for (const auto& sh : k.canvas().shapes()) {
                if (sh.kind != gsnap::ShapeKind::Arc && sh.kind != gsnap::ShapeKind::Circle) continue;
                double cx, cy;
                k.world(sh.a, cx, cy);
                const double r = double(sh.radius) * k.vol().inv_scale();
                const double d2 = std::fabs(std::hypot(wx - cx, wy - cy) - r);
                if (d2 < best_d) { best_d = d2; best = &sh; }
            }
            if (!best) { k.log("DIMARC: no arc there"); return; }
            gdim::Dimension d;
            d.kind = gdim::Kind::ArcLength;
            d.centre = gcm::Lattice2{best->a.x, best->a.y};
            d.radius = best->radius;
            double cx, cy;
            k.world(best->a, cx, cy);
            const double r = double(best->radius) * k.vol().inv_scale();
            const double a0 = best->kind == gsnap::ShapeKind::Circle ? 0.0 : best->start_angle;
            const double a1 = best->kind == gsnap::ShapeKind::Circle ? 3.14159265358979 : best->end_angle;
            d.p1 = k.lat(cx + r * std::cos(a0), cy + r * std::sin(a0));
            d.p2 = k.lat(cx + r * std::cos(a1), cy + r * std::sin(a1));
            d.p3 = gcm::Lattice2{p[1].x, p[1].y};
            k.canvas().add_dimensions({d});
            k.log("arc length dimension placed");
        });
    }
};

struct DimCenter : CommandBase<DimCenter> {
    static constexpr auto name = "DIMCENTER";

    void execute(CommandContext& c) const {
        c.pick(1, "DIMCENTER: click the circle or arc",
               [&shell = c.shell()](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            double wx, wy;
            k.world(p[0], wx, wy);
            const gsnap::Shape* best = nullptr;
            double best_d = 1e300;
            for (const auto& sh : k.canvas().shapes()) {
                if (!sh.is_curve()) continue;
                double cx, cy;
                k.world(sh.a, cx, cy);
                const double r = double(sh.radius) * k.vol().inv_scale();
                const double d2 = std::fabs(std::hypot(wx - cx, wy - cy) - r);
                if (d2 < best_d) { best_d = d2; best = &sh; }
            }
            if (!best) { k.log("DIMCENTER: no curve there"); return; }
            k.canvas().add_centre_mark({best->a.x, best->a.y},
                                       double(best->radius) * k.vol().inv_scale());
            k.log(k.canvas().message());
        });
    }
};

struct DimJogged : CommandBase<DimJogged> {
    static constexpr auto name = "DIMJOGGED";

    void execute(CommandContext& c) const {
        if (c.canvas().selection().empty()) {
            c.log("DIMJOGGED: select a circle or an arc first");
            return;
        }
        const gsnap::Shape curve = c.canvas().shapes()[c.canvas().selection().front()];
        if (!curve.is_curve()) { c.log("DIMJOGGED: select a circle or an arc"); return; }
        c.pick(2, "DIMJOGGED: click where the leader ends, then where the jog goes",
               [&shell = c.shell(), curve](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            double cx, cy, ex, ey, jx, jy;
            k.world(curve.a, cx, cy);
            k.world(p[0], ex, ey);
            k.world(p[1], jx, jy);
            const double r = double(curve.radius) * k.vol().inv_scale();
            const double dx = ex - cx, dy = ey - cy;
            const double len = std::hypot(dx, dy);
            if (len < 1e-9) { k.log("DIMJOGGED: pick a leader end away from the centre"); return; }
            const double ux = dx / len, uy = dy / len;
            const double rim_x = cx + ux * r, rim_y = cy + uy * r;

            double along = ((jx - rim_x) * ux + (jy - rim_y) * uy);
            along = std::clamp(along, r * 0.15, std::max(r * 0.15, len - r));
            const double step = std::max(r * 0.12, 1e-3);
            const double nx = -uy, ny = ux;
            const double ax = rim_x + ux * (along - step), ay = rim_y + uy * (along - step);
            const double bx = rim_x + ux * along + nx * step;
            const double by = rim_y + uy * along + ny * step;
            const double dx2 = rim_x + ux * (along + step) - nx * step;
            const double dy2 = rim_y + uy * (along + step) - ny * step;
            const double fx = rim_x + ux * (along + 2.0 * step);
            const double fy = rim_y + uy * (along + 2.0 * step);

            Canvas& canvas = k.canvas();
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                const auto line = [&](double x0, double y0, double x1, double y1) {
                    gsnap::Shape seg;
                    seg.kind = gsnap::ShapeKind::Segment;
                    seg.a = k.lat(x0, y0);
                    seg.b = k.lat(x1, y1);
                    seg.layer = canvas.current_layer();
                    sh.push_back(std::move(seg));
                };
                line(rim_x, rim_y, ax, ay);
                line(ax, ay, bx, by);
                line(bx, by, dx2, dy2);
                line(dx2, dy2, fx, fy);
                line(fx, fy, ex, ey);
            });
            canvas.add_text(k.lat(ex, ey),
                            "R" + gunit::format_length(r, gunit::Format{canvas.display_unit()}),
                            shell.text_height(), 0.0, 0, 1, 0.0, shell.text_styles().current());
            k.log("jogged radius R" +
                  gunit::format_length(r, gunit::Format{canvas.display_unit()}));
        });
    }
};

struct DimOrdinate : CommandBase<DimOrdinate> {
    static constexpr auto name = "DIMORDINATE";

    void execute(CommandContext& c) const {
        c.pick(2, "DIMORDINATE: click the feature, then the leader end",
               [&shell = c.shell()](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            gdim::Dimension d;
            d.kind = gdim::Kind::Ordinate;
            d.p1 = p[0];
            d.p2 = p[0];
            d.p3 = p[1];
            k.canvas().add_dimensions({d});
            k.log("ordinate dimension added");
        });
    }
};

}

void register_annotate_commands(CommandTable& t) {
    t.add<DimStyle>();
    t.add<DimContinue>();
    t.add<DimBaseline>();
    t.add<DimArc>();
    t.add<DimCenter>();
    t.add<DimJogged>();
    t.add<DimOrdinate>();
}

}
