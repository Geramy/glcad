#include "ui/commands.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "ui/shell.hpp"

namespace gcapp {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Polygon : CommandBase<Polygon> {
    static constexpr auto name = "POLYGON";

    void execute(CommandContext& c) const {
        if (!c.have_nums() || c.nums()[0] < 3) {
            c.ask(name, "sides", "POLYGON: type the number of sides, e.g. 6");
            return;
        }
        const int sides = static_cast<int>(c.nums()[0]);
        c.pick(2, "POLYGON: click the centre, then a vertex",
               [&shell = c.shell(), sides](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            double cx, cy, vx, vy;
            k.world(p[0], cx, cy);
            k.world(p[1], vx, vy);
            const double r = std::hypot(vx - cx, vy - cy);
            const double a0 = std::atan2(vy - cy, vx - cx);
            if (r < 1e-12) { k.log("POLYGON: the vertex is on the centre"); return; }
            Canvas& canvas = k.canvas();
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                const std::size_t first = sh.size();
                for (int i = 0; i < sides; ++i) {
                    const double a = a0 + 2.0 * kPi * i / sides;
                    const double b = a0 + 2.0 * kPi * (i + 1) / sides;
                    gsnap::Shape s;
                    s.kind = gsnap::ShapeKind::Segment;
                    s.a = k.lat(cx + r * std::cos(a), cy + r * std::sin(a));
                    s.b = k.lat(cx + r * std::cos(b), cy + r * std::sin(b));
                    s.layer = canvas.current_layer();
                    sh.push_back(s);
                }
                canvas.stamp_polyline(sh, first);
            });
            k.log("polygon: " + std::to_string(sides) + " sides");
        });
    }
};

struct Donut : CommandBase<Donut> {
    static constexpr auto name = "DONUT";

    void execute(CommandContext& c) const {
        if (c.nums().size() < 2 || c.nums()[1] <= 0.0) { c.ask("DONUT", "inside outside", "DONUT: type the inside and outside diameters, e.g. 0.5 1"); return; }
        const double ri = c.nums()[0] * 0.5, ro = c.nums()[1] * 0.5;
        c.pick(1, "DONUT: click the centre",
               [&shell = c.shell(), ri, ro](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            Canvas& canvas = k.canvas();
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (double r : {ri, ro}) {
                    if (r <= 0.0) continue;
                    gsnap::Shape s;
                    s.kind = gsnap::ShapeKind::Circle;
                    s.a = p[0]; s.b = p[0];
                    s.radius = static_cast<gcm::i64>(std::llround(r * k.vol().scale()));
                    s.layer = canvas.current_layer();
                    sh.push_back(s);
                }
            });
            k.log("donut placed");
        });
    }
};

void trace_boundary(const CommandContext& c, const char* n) {
    c.pick(1, std::string(n) + ": click inside the area",
           [&shell = c.shell(), n](const std::vector<gcm::Lattice2>& p) {
        CommandContext k(shell, {});
        Canvas& canvas = k.canvas();
        auto loop = canvas.boundary_at_world(p[0]);
        if (loop.empty()) { k.log(std::string(n) + ": no closed boundary around that point"); return; }
        for (auto& s : loop) { s.layer = canvas.current_layer(); s.attrs = canvas.current_attributes; }
        canvas.modify_shapes([&](auto& shapes) { const auto first = shapes.size(); shapes.insert(shapes.end(), loop.begin(), loop.end()); canvas.stamp_polyline(shapes, first); });
        k.log("created closed boundary polyline");
    });
}

struct Boundary : CommandBase<Boundary> {
    static constexpr auto name = "BOUNDARY";
    void execute(CommandContext& c) const { trace_boundary(c, name); }
};

struct Region : CommandBase<Region> {
    static constexpr auto name = "REGION";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        const auto selected = c.selected_indices();
        if (!selected.empty()) {
            std::vector<gsnap::Shape> boundaries;
            for (auto i : selected) boundaries.push_back(canvas.shapes()[i]);
            canvas.create_region(boundaries, selected);
            c.log(canvas.message());
            return;
        }
        c.log("REGION: select closed lines, arcs or circles first");
    }
};

struct Revcloud : CommandBase<Revcloud> {
    static constexpr auto name = "REVCLOUD";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        std::string arguments = c.arg();
        const bool edit = c.upper_arg().starts_with("EDIT"); gcm::u32 replace = 0;
        if (edit) {
            arguments = arguments.substr(4);
            const auto selected = c.selected_indices();
            if (selected.empty() || (!canvas.selected_texts().empty() || !canvas.selected_references().empty() || !canvas.selected_dimensions().empty()) || !canvas.is_revision_cloud(selected.front())) {
                c.log("REVCLOUD EDIT: select one cloud first"); return;
            }
            replace = canvas.shapes()[selected.front()].poly;
            for (auto i : selected) if (canvas.shapes()[i].poly != replace) {
                c.log("REVCLOUD EDIT: select only one cloud"); return;
            }
        }
        if (arguments.find_first_not_of(' ') != std::string::npos) {
            std::istringstream input(arguments); std::vector<double> values; double v;
            while (input >> v) values.push_back(v);
            if (!input.eof() || (values.size() != 4 && values.size() != 5) ||
                std::any_of(values.begin(), values.end(), [](double x) { return !std::isfinite(x); }) ||
                (values.size() == 5 && values[4] <= 0)) {
                c.log("REVCLOUD [EDIT] x0 y0 x1 y1 [positive chord length]"); return;
            }
            const double unit = gunit::metres_per(canvas.display_unit());
            canvas.revision_cloud(c.lat(values[0] * unit, values[1] * unit),
                                  c.lat(values[2] * unit, values[3] * unit),
                                  values.size() == 5 ? values[4] * unit : 0, replace);
            c.log(canvas.message());
        } else {
            c.pick(2, edit ? "REVCLOUD EDIT: pick new corners" : "REVCLOUD: pick two corners",
                   [&shell = c.shell(), replace](const std::vector<gcm::Lattice2>& pts) {
                CommandContext k(shell, {});
                Canvas& canvas = k.canvas();
                canvas.revision_cloud(pts[0], pts[1], 0, replace);
                k.log(canvas.message());
            });
        }
    }
};

void divide_or_measure(const CommandContext& c, const char* n, bool divide) {
    if (!c.have_nums() || c.nums()[0] <= 0.0) {
        c.ask(n, divide ? "segments" : "spacing",
              divide ? "DIVIDE: how many segments, e.g. 8"
                     : "MEASURE: how far apart, e.g. 0.6");
        return;
    }
    Canvas& canvas = c.canvas();
    const std::vector<std::size_t> sel = c.selected_indices();
    const double amount = c.nums()[0];
    std::size_t placed = 0;
    canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
        for (std::size_t i : sel) {
            const gsnap::Shape src = sh[i];
            double length = 0.0;
            if (src.kind == gsnap::ShapeKind::Segment) {
                double ax, ay, bx, by;
                c.world(src.a, ax, ay);
                c.world(src.b, bx, by);
                length = std::hypot(bx - ax, by - ay);
            } else if (src.is_curve()) {
                const double r = double(src.radius) * c.vol().inv_scale();
                double sweep = src.kind == gsnap::ShapeKind::Circle
                                   ? 6.28318530717958647692
                                   : src.end_angle - src.start_angle;
                while (sweep < 0.0) sweep += 6.28318530717958647692;
                length = sweep * r;
            } else {
                continue;
            }
            if (length < 1e-9) continue;

            const int count = divide
                                  ? std::max(2, int(std::lround(amount)))
                                  : std::max(1, int(std::floor(length / amount)));
            const double step = divide ? length / double(count) : amount;
            const int first = 1;
            const int last = divide ? count - 1 : count;
            for (int k = first; k <= last; ++k) {
                const double t = step * double(k);
                if (t > length + 1e-9) break;
                gsnap::Shape p;
                p.kind = gsnap::ShapeKind::Point;
                p.layer = src.layer.empty() ? canvas.current_layer() : src.layer;
                if (src.kind == gsnap::ShapeKind::Segment) {
                    double ax, ay, bx, by;
                    c.world(src.a, ax, ay);
                    c.world(src.b, bx, by);
                    const double f = t / length;
                    p.a = c.lat(ax + (bx - ax) * f, ay + (by - ay) * f);
                } else {
                    double cx, cy;
                    c.world(src.a, cx, cy);
                    const double r = double(src.radius) * c.vol().inv_scale();
                    const double a0 = src.kind == gsnap::ShapeKind::Circle
                                          ? 0.0 : src.start_angle;
                    const double ang = a0 + t / r;
                    p.a = c.lat(cx + r * std::cos(ang), cy + r * std::sin(ang));
                }
                sh.push_back(std::move(p));
                ++placed;
            }
        }
    });
    c.log(std::to_string(placed) + " point(s) placed -- PDMODE says how "
          "they draw");
}

struct Divide : CommandBase<Divide> {
    static constexpr auto name = "DIVIDE";
    static constexpr bool wants_selection = true;
    void execute(CommandContext& c) const { divide_or_measure(c, name, true); }
};

struct Measure : CommandBase<Measure> {
    static constexpr auto name = "MEASURE";
    static constexpr bool wants_selection = true;
    void execute(CommandContext& c) const { divide_or_measure(c, name, false); }
};

}

void register_draw_commands(CommandTable& t) {
    t.add<Polygon>();
    t.add<Donut>();
    t.add<Boundary>();
    t.add<Region>();
    t.add<Revcloud>();
    t.add<Divide>();
    t.add<Measure>();
}

}
