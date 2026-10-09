#include "ui/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "geometry/edit.hpp"
#include "geometry/nurbs.hpp"
#include "ui/shell.hpp"

namespace gcapp {

namespace {

struct Scale : CommandBase<Scale> {
    static constexpr auto name = "SCALE";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        if (!c.have_nums() || c.nums()[0] <= 0.0) { c.ask("SCALE", "factor", "SCALE: type the factor, e.g. 2"); return; }
        const double factor = c.nums()[0];
        const std::vector<std::size_t> sel = c.selected_indices();
        c.pick(1, "SCALE: click the base point",
               [&shell = c.shell(), factor, sel](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            Canvas& canvas = k.canvas();
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) if (i < sh.size()) sh[i] = gedit::scale(sh[i], p[0], factor);
            });
            k.log("scaled " + std::to_string(sel.size()) + " by " + std::to_string(factor));
        });
    }
};

struct Lengthen : CommandBase<Lengthen> {
    static constexpr auto name = "LENGTHEN";
    static constexpr bool wants_selection = true;
    static constexpr bool refuses_regions = true;

    void execute(CommandContext& c) const {
        if (!c.have_nums()) { c.ask("LENGTHEN", "delta", "LENGTHEN: type the change in length, e.g. 0.5 (negative shortens)"); return; }
        const double delta = c.nums()[0];
        std::size_t done = 0;
        const auto& vol = c.vol();
        c.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            for (std::size_t i : c.selected_indices()) {
                if (auto r = gedit::lengthen(sh[i], gedit::LengthenMode::Delta, delta, sh[i].b, vol)) { sh[i] = *r; ++done; }
            }
        });
        c.log("lengthened " + std::to_string(done) + " object(s) by " + std::to_string(delta));
    }
};

struct Break : CommandBase<Break> {
    static constexpr auto name = "BREAK";
    static constexpr bool wants_selection = true;
    static constexpr bool refuses_regions = true;

    void execute(CommandContext& c) const {
        const std::size_t target = c.selected_indices()[0];
        c.pick(2, "BREAK: click the two break points on the selected object",
               [&shell = c.shell(), target](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            std::string why;
            const auto& vol = k.vol();
            k.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                if (target >= sh.size()) return;
                const gedit::BreakResult r = gedit::break_between(sh[target], p[0], p[1], vol);
                if (!r.changed) { why = r.reason; return; }
                const std::string layer = sh[target].layer;
                sh.erase(sh.begin() + static_cast<std::ptrdiff_t>(target));
                for (auto piece : r.pieces) { piece.layer = layer; sh.push_back(piece); }
            });
            k.log(why.empty() ? "broken" : "BREAK: " + why);
        });
    }
};

void join_selection(CommandContext& c, const std::string& n) {
    std::size_t joined = 0;
    std::string why;
    Canvas& canvas = c.canvas();
    const auto& vol = c.vol();
    canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
        const std::vector<std::size_t> sel = c.selected_indices();
        std::vector<gsnap::Shape> chosen;
        for (std::size_t i : sel) chosen.push_back(sh[i]);
        const gedit::JoinResult r = gedit::join(chosen, vol);
        if (r.joined == 0) { why = r.reason; return; }
        joined = r.joined;
        std::vector<gsnap::Shape> rest;
        for (std::size_t i = 0; i < sh.size(); ++i)
            if (std::find(sel.begin(), sel.end(), i) == sel.end()) rest.push_back(sh[i]);
        for (auto s : r.shapes) { if (s.layer.empty()) s.layer = canvas.current_layer(); rest.push_back(s); }
        sh = std::move(rest);
    });
    c.log(joined ? "joined " + std::to_string(joined) + " object(s)" : n + ": " + (why.empty() ? "nothing joins" : why));
}

struct Join : CommandBase<Join> {
    static constexpr auto name = "JOIN";
    static constexpr bool wants_selection = true;
    static constexpr bool refuses_regions = true;

    void execute(CommandContext& c) const { join_selection(c, name); }
};

struct Pedit : CommandBase<Pedit> {
    static constexpr auto name = "PEDIT";

    void execute(CommandContext& c) const {
        if (c.upper_arg() != "EXPLODE" && !c.refuse_region(name)) return;
        if (!c.has_arg()) { if (!c.need_selection(name)) return; join_selection(c, name); return; }
        if (!c.need_selection("PEDIT")) return;
        std::string what = c.arg();
        for (char& ch : what) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        const std::vector<std::size_t> sel = c.selected_indices();
        Canvas& canvas = c.canvas();

        if (what == "CLOSE" || what == "OPEN") {
            const std::vector<std::size_t> run = canvas.polyline_pieces(sel.front());
            if (run.size() < 2) { c.log("PEDIT: that is not a polyline"); return; }
            const bool closed = canvas.polyline_closed(run);
            if (what == "CLOSE" && closed) { c.log("PEDIT: already closed"); return; }
            if (what == "OPEN" && !closed) { c.log("PEDIT: already open"); return; }
            if (what == "CLOSE") {
                const gcm::Lattice2 from = canvas.shapes()[run.back()].b;
                const gcm::Lattice2 to   = canvas.shapes()[run.front()].a;
                const gcm::u32 id = canvas.shapes()[run.front()].poly;
                const gcm::u32 next = canvas.shapes()[run.back()].poly_index + 1;
                const std::string layer = canvas.shapes()[run.front()].layer;
                canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                    gsnap::Shape closer;
                    closer.kind = gsnap::ShapeKind::Segment;
                    closer.a = from;
                    closer.b = to;
                    closer.layer = layer;
                    closer.poly = id;
                    closer.poly_index = next;
                    sh.push_back(closer);
                });
                canvas.set_selection(canvas.polyline_pieces(run.front()));
                c.log("polyline closed");
            } else {
                const std::size_t drop = run.back();
                canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                    sh.erase(sh.begin() + static_cast<long>(drop));
                });
                canvas.set_selection(canvas.polyline_pieces(
                    run.front() < drop ? run.front() : run.front() - 1));
                c.log("polyline opened");
            }
            return;
        }
        if (what == "EXPLODE" || what == "DECURVE") {
            std::size_t freed = 0;
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel)
                    if (sh[i].poly != 0) { sh[i].poly = 0; sh[i].poly_index = 0; sh[i].region=false; sh[i].region_loop=0; sh[i].region_reversed=false; ++freed; }
            });
            c.log(std::to_string(freed) + " piece(s) are separate objects again");
            return;
        }
        if (what == "MAKE" || what == "POLY" || what == "JOIN") {
            if (sel.size() < 2) { c.log("PEDIT: select the lines to join"); return; }
            std::size_t made = 0;
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                std::vector<std::size_t> rest = sel;
                std::vector<std::size_t> chain{rest.front()};
                rest.erase(rest.begin());
                bool grew = true;
                while (grew && !rest.empty()) {
                    grew = false;
                    const gcm::Lattice2 tail = sh[chain.back()].b;
                    for (std::size_t k = 0; k < rest.size(); ++k) {
                        gsnap::Shape& piece = sh[rest[k]];
                        if (piece.a.x == tail.x && piece.a.y == tail.y) {
                            chain.push_back(rest[k]);
                            rest.erase(rest.begin() + static_cast<long>(k));
                            grew = true;
                            break;
                        }
                        if (piece.b.x == tail.x && piece.b.y == tail.y &&
                            piece.kind == gsnap::ShapeKind::Segment) {
                            std::swap(piece.a, piece.b);
                            chain.push_back(rest[k]);
                            rest.erase(rest.begin() + static_cast<long>(k));
                            grew = true;
                            break;
                        }
                    }
                }
                if (chain.size() < 2) return;
                const gcm::u32 id = canvas.next_polyline_id();
                gcm::u32 order = 0;
                for (std::size_t i : chain) {
                    sh[i].poly = id;
                    sh[i].poly_index = order++;
                }
                made = chain.size();
            });
            if (made) canvas.set_selection(canvas.polyline_pieces(sel.front()));
            c.log(made ? std::to_string(made) + " piece(s) are now one polyline"
                       : "PEDIT: those lines do not meet end to end");
            return;
        }
        c.log("PEDIT: CLOSE, OPEN, JOIN or EXPLODE");
    }
};

struct Explode : CommandBase<Explode> {
    static constexpr auto name = "EXPLODE";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        if (!canvas.selected_references().empty()) {
            auto refs=canvas.selected_references(); std::sort(refs.rbegin(),refs.rend());
            for (auto ref:refs) { canvas.explode_block(ref); c.log(canvas.message()); }
            return;
        }
        if (!c.need_selection("EXPLODE")) return;
        std::size_t exploded = 0;
        std::vector<std::size_t> sel = c.selected_indices();
        std::sort(sel.rbegin(), sel.rend());
        for (std::size_t i : sel)
            if (i < canvas.shapes().size() && canvas.shapes()[i].kind == gsnap::ShapeKind::Block && canvas.explode_block(i)) ++exploded;
        std::size_t freed = 0;
        canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            for (std::size_t i : sel)
                if (i < sh.size() && sh[i].poly != 0) {
                    sh[i].poly = 0;
                    sh[i].poly_index = 0; sh[i].region=false; sh[i].region_loop=0; sh[i].region_reversed=false;
                    ++freed;
                }
        });
        if (freed) c.log("exploded compound boundaries into " + std::to_string(freed) + " object(s)");
        else c.log(exploded ? "exploded " + std::to_string(exploded) + " block(s)" : "EXPLODE: nothing selected comes apart further");
    }
};

struct Align : CommandBase<Align> {
    static constexpr auto name = "ALIGN";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        const std::vector<std::size_t> sel = c.selected_indices();
        c.pick(4, "ALIGN: click source point 1, destination 1, source 2, destination 2",
               [&shell = c.shell(), sel](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            const auto& vol = k.vol();
            k.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                std::vector<gsnap::Shape> chosen;
                for (std::size_t i : sel) chosen.push_back(sh[i]);
                const std::vector<gsnap::Shape> moved = gedit::align(chosen, p[0], p[2], p[1], p[3], false, vol);
                for (std::size_t j = 0; j < sel.size() && j < moved.size(); ++j) sh[sel[j]] = moved[j];
            });
            k.log("aligned " + std::to_string(sel.size()) + " object(s)");
        });
    }
};

struct Overkill : CommandBase<Overkill> {
    static constexpr auto name = "OVERKILL";
    static constexpr bool refuses_regions = true;

    void execute(CommandContext& c) const {
        double tol = 0.0;
        if (c.have_nums() && c.nums()[0] > 0.0) tol = c.nums()[0];
        const gcm::i64 t = gcm::i64(std::llround(tol * c.vol().scale()));
        std::size_t removed = 0, merged = 0;

        const auto near_pt = [&](gcm::Lattice2 p, gcm::Lattice2 q) {
            return std::llabs(p.x - q.x) <= t && std::llabs(p.y - q.y) <= t;
        };
        c.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            std::vector<gsnap::Shape> kept;
            for (const auto& s : sh) {
                bool dup = false;
                for (auto& k : kept) {
                    if (k.kind != s.kind) continue;
                    if (std::llabs(k.radius - s.radius) > t) continue;
                    const bool same = (near_pt(k.a, s.a) && near_pt(k.b, s.b)) ||
                                      (s.kind == gsnap::ShapeKind::Segment &&
                                       near_pt(k.a, s.b) && near_pt(k.b, s.a));
                    if (same && (!s.is_curve() ||
                                 (std::fabs(k.start_angle - s.start_angle) < 1e-9 &&
                                  std::fabs(k.end_angle - s.end_angle) < 1e-9))) {
                        dup = true;
                        break;
                    }
                    if (s.kind != gsnap::ShapeKind::Segment ||
                        k.kind != gsnap::ShapeKind::Segment || k.layer != s.layer)
                        continue;
                    const double kx = double(k.b.x - k.a.x), ky = double(k.b.y - k.a.y);
                    const double sx = double(s.b.x - s.a.x), sy = double(s.b.y - s.a.y);
                    const double klen = std::hypot(kx, ky), slen = std::hypot(sx, sy);
                    if (klen < 1.0 || slen < 1.0) continue;
                    const double cross = (kx * sy - ky * sx) / (klen * slen);
                    if (std::fabs(cross) > 1e-9) continue;
                    const double off = std::fabs(kx * double(s.a.y - k.a.y) -
                                                 ky * double(s.a.x - k.a.x)) / klen;
                    if (off > double(t) + 0.5) continue;
                    const double ux = kx / klen, uy = ky / klen;
                    const auto along = [&](gcm::Lattice2 p) {
                        return (double(p.x - k.a.x) * ux + double(p.y - k.a.y) * uy);
                    };
                    double k0 = 0.0, k1 = klen;
                    double s0 = along(s.a), s1 = along(s.b);
                    if (s1 < s0) std::swap(s0, s1);
                    if (s1 < k0 - double(t) - 0.5 || s0 > k1 + double(t) + 0.5) continue;
                    const double lo = std::min(k0, s0), hi = std::max(k1, s1);
                    k.a = {k.a.x + gcm::i64(std::llround(ux * lo)),
                           k.a.y + gcm::i64(std::llround(uy * lo))};
                    k.b = {k.a.x + gcm::i64(std::llround(ux * (hi - lo))),
                           k.a.y + gcm::i64(std::llround(uy * (hi - lo)))};
                    dup = true;
                    ++merged;
                    break;
                }
                if (dup) ++removed; else kept.push_back(s);
            }
            sh = std::move(kept);
        });
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "overkill: removed %zu duplicate(s), of which %zu were "
                      "overlaps merged into one line%s",
                      removed, merged,
                      tol > 0.0 ? "" : " -- OVERKILL <tolerance> catches near misses");
        c.log(buf);
    }
};

struct Blend : CommandBase<Blend> {
    static constexpr auto name = "BLEND";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        const std::vector<std::size_t> sel = c.selected_indices();
        if (sel.size() != 2) { c.log("BLEND: select exactly two objects"); return; }
        const gsnap::Shape& a = canvas.shapes()[sel[0]];
        const gsnap::Shape& b = canvas.shapes()[sel[1]];
        if (a.kind != gsnap::ShapeKind::Segment || b.kind != gsnap::ShapeKind::Segment) { c.log("BLEND: two lines, please"); return; }
        double ax, ay, bx, by, cx, cy, dx, dy;
        c.world(a.a, ax, ay); c.world(a.b, bx, by); c.world(b.a, cx, cy); c.world(b.b, dx, dy);
        struct End { double x, y, tx, ty; };
        const End ea[2] = {{ax, ay, ax - bx, ay - by}, {bx, by, bx - ax, by - ay}};
        const End eb[2] = {{cx, cy, cx - dx, cy - dy}, {dx, dy, dx - cx, dy - cy}};
        double best = 1e300; End from = ea[1], to = eb[0];
        for (const End& p : ea) for (const End& q : eb) {
            const double d = std::hypot(p.x - q.x, p.y - q.y);
            if (d < best) { best = d; from = p; to = q; }
        }
        const double gap = std::max(best, 1e-9);
        const auto unit = [](double x, double y, double& ux, double& uy) { const double l = std::hypot(x, y); ux = l > 0 ? x / l : 0; uy = l > 0 ? y / l : 0; };
        double fx, fy, tx, ty;
        unit(from.tx, from.ty, fx, fy);
        unit(to.tx, to.ty, tx, ty);
        std::vector<ggeo::d2::Point<gcm::F64>> pts = {
            ggeo::d2::Point<gcm::F64>::from(from.x, from.y),
            ggeo::d2::Point<gcm::F64>::from(from.x + fx * gap / 3.0, from.y + fy * gap / 3.0),
            ggeo::d2::Point<gcm::F64>::from(to.x + tx * gap / 3.0, to.y + ty * gap / 3.0),
            ggeo::d2::Point<gcm::F64>::from(to.x, to.y)};
        canvas.add_splines({ggeo::interpolate_points<gcm::F64>(pts, 3)});
        c.log("blended with a spline");
    }
};

struct Reverse : CommandBase<Reverse> {
    static constexpr auto name = "REVERSE";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        std::size_t done = 0;
        c.canvas().modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            for (std::size_t i : c.selected_indices()) {
                if (sh[i].kind == gsnap::ShapeKind::Segment || sh[i].kind == gsnap::ShapeKind::Ray || sh[i].kind == gsnap::ShapeKind::Xline) {
                    std::swap(sh[i].a, sh[i].b);
                    ++done;
                }
            }
        });
        c.log("reversed " + std::to_string(done) + " object(s)");
    }
};

struct GripEdit : CommandBase<GripEdit> {
    static constexpr auto name = "GRIPEDIT";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        if (canvas.selection().empty()) {
            c.log("GRIPEDIT: select an object first, then GRIPEDIT "
                  "<x> <y> [operation] at one of its grips");
            return;
        }
        std::vector<double> v;
        std::string what;
        {
            std::istringstream in(c.arg());
            std::string token;
            while (in >> token) {
                try {
                    std::size_t used = 0;
                    const double d = std::stod(token, &used);
                    if (used == token.size()) { v.push_back(d); continue; }
                } catch (...) {}
                if (what.empty()) what = token;
            }
            for (char& ch : what) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (v.size() < 2) {
            c.log("GRIPEDIT <x> <y> [arc|line|radius|diameter|rotate|"
                  "lengthen|move|addvertex|removevertex] [to-x to-y]");
            return;
        }
        double px2, py2;
        canvas.view().screen_of(v[0], v[1], px2, py2);

        const auto options = canvas.grip_options(px2, py2);
        if (options.empty()) { c.log("GRIPEDIT: no grip there"); return; }
        if (what.empty()) {
            std::string said;
            for (const auto& o : options) said += (said.empty() ? "" : ", ") + std::string(o.label);
            c.log("this grip offers: " + said);
            return;
        }

        static const struct { const char* word; gedit::GripOp op; } kOps[] = {
            {"stretch", gedit::GripOp::Stretch},   {"move", gedit::GripOp::Move},
            {"lengthen", gedit::GripOp::Lengthen}, {"arc", gedit::GripOp::ConvertToArc},
            {"line", gedit::GripOp::ConvertToLine},{"radius", gedit::GripOp::Radius},
            {"diameter", gedit::GripOp::Diameter}, {"rotate", gedit::GripOp::Rotate},
            {"addvertex", gedit::GripOp::AddVertex},
            {"removevertex", gedit::GripOp::RemoveVertex},
        };
        gedit::GripOp op = gedit::GripOp::Stretch;
        bool known = false;
        for (const auto& k : kOps) if (what == k.word) { op = k.op; known = true; }
        if (!known) { c.log("GRIPEDIT: no operation called " + what); return; }
        const gcm::Lattice2 to = v.size() >= 4 ? c.lat(v[2], v[3]) : c.lat(v[0], v[1]);
        canvas.apply_grip_option(px2, py2, op, to);
        c.log(canvas.message());
    }
};

struct SelectPrevious : CommandBase<SelectPrevious> {
    static constexpr auto name = "SELECTPREVIOUS";

    void execute(CommandContext& c) const {
        const auto count=c.canvas().select_previous();
        c.log(std::to_string(count)+" selected");
    }
};

struct SelectLast : CommandBase<SelectLast> {
    static constexpr auto name = "SELECTLAST";

    void execute(CommandContext& c) const {
        const auto count=c.canvas().select_last();
        c.log(std::to_string(count)+" selected");
    }
};

struct SelectCycle : CommandBase<SelectCycle> {
    static constexpr auto name = "SELECTCYCLE";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        if(c.nums().size()==2 && std::isfinite(c.nums()[0]) && std::isfinite(c.nums()[1])) {
            double x,y;canvas.view().screen_of(c.nums()[0],c.nums()[1],x,y);
            canvas.cycle_selection(x,y);c.log(canvas.message());
        } else c.pick(1,"Select a point to cycle overlapping objects",[&shell = c.shell()](const auto& pts) {
            CommandContext k(shell, {});
            Canvas& canv = k.canvas();
            double x,y,px,py;k.world(pts.front(),x,y);canv.view().screen_of(x,y,px,py);
            canv.cycle_selection(px,py);k.log(canv.message());
        });
    }
};

struct ArrayRect : CommandBase<ArrayRect> {
    static constexpr auto name = "ARRAYRECT";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        const auto& nums = c.nums();
        if (nums.size() >= 2 && nums[0] >= 1 && nums[1] >= 1 && nums[0]*nums[1]<=10000) {
            canvas.array_columns() = int(nums[0]);
            canvas.array_rows() = int(nums[1]);
        }
        canvas.set_tool(Tool::ArrayRect);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "rectangular array %d x %d: select, then drag the spacing",
                      canvas.array_columns(), canvas.array_rows());
        c.log(buf);
    }
};

struct ArrayPolar : CommandBase<ArrayPolar> {
    static constexpr auto name = "ARRAYPOLAR";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        const auto& nums = c.nums();
        if (!nums.empty() && nums[0] >= 2 && nums[0]<=10000) canvas.array_count() = int(nums[0]);
        if (nums.size() >= 2 && nums[1] > 0.0)
            canvas.array_angle() = nums[1] * 3.14159265358979323846 / 180.0;
        canvas.set_tool(Tool::ArrayPolar);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "polar array x%d through %.4g degrees: select, then pick the centre",
                      canvas.array_count(),
                      canvas.array_angle() * 180.0 / 3.14159265358979323846);
        c.log(buf);
    }
};

struct ArrayPath : CommandBase<ArrayPath> {
    static constexpr auto name = "ARRAYPATH";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        const std::vector<std::size_t> sel = c.selected_indices();
        const int count = !c.nums().empty() && c.nums()[0] >= 2 && c.nums()[0]<=10000 ? int(c.nums()[0]) : canvas.array_count();
        canvas.array_count()=count;
        c.pick(1, "ARRAYPATH: click the path",
               [&shell = c.shell(), sel, count](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            Canvas& canv = k.canvas();
            double x, y, px2, py2;
            k.world(p[0], x, y);
            canv.view().screen_of(x, y, px2, py2);
            const std::size_t hit = canv.pick(px2, py2);
            if (hit == Canvas::npos) { k.log("ARRAYPATH: nothing there"); return; }
            const std::vector<std::size_t> run = canv.polyline_pieces(hit);
            std::vector<gcm::Lattice2> path;
            for (std::size_t i : run) {
                if (path.empty()) path.push_back(canv.shapes()[i].a);
                path.push_back(canv.shapes()[i].b);
            }
            if (path.size() < 2) { k.log("ARRAYPATH: that is not a path"); return; }
            std::vector<gsnap::Shape> picked;
            for (std::size_t i : sel)
                if (std::find(run.begin(), run.end(), i) == run.end())
                    picked.push_back(canv.shapes()[i]);
            if (picked.empty()) { k.log("ARRAYPATH: select what to array first"); return; }
            const auto made = gedit::array_path(picked, path, count, k.vol());
            if (made.empty()) { k.log("ARRAYPATH: produced nothing"); return; }
            canv.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (auto piece : made) {
                    if (piece.layer.empty()) piece.layer = canv.current_layer();
                    sh.push_back(std::move(piece));
                }
            });
            k.log("arrayed " + std::to_string(picked.size()) + " object(s) " +
                  std::to_string(count) + " times along the path");
        });
    }
};

void polyline_corners(CommandContext& c, const std::string& n, bool fillet) {
    Canvas& canvas = c.canvas();
    const std::vector<std::size_t> sel = c.selected_indices();
    const std::vector<std::size_t> run = canvas.polyline_pieces(sel.front());
    if (run.size() < 2) {
        c.log(n + ": select a polyline (PEDIT JOIN makes one from lines)");
        return;
    }
    double value = fillet ? canvas.corner_radius() : canvas.chamfer_distance();
    if (c.have_nums() && c.nums()[0] > 0.0) value = c.nums()[0];
    if (value <= 0.0) { c.log(n + ": a radius of zero rounds nothing"); return; }

    std::vector<gsnap::Shape> pieces;
    for (std::size_t i : run) pieces.push_back(canvas.shapes()[i]);
    const gedit::PolylineCornerResult r =
        fillet ? gedit::fillet_polyline(pieces, value, c.vol())
               : gedit::chamfer_polyline(pieces, value, c.vol());
    if (r.rounded == 0) {
        c.log(n + ": " + (r.reason[0] ? r.reason : "no corner takes that size"));
        return;
    }
    const gcm::u32 id = canvas.shapes()[run.front()].poly;
    const std::string layer = canvas.shapes()[run.front()].layer;
    canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
        std::vector<std::size_t> drop = run;
        std::sort(drop.rbegin(), drop.rend());
        for (std::size_t i : drop) sh.erase(sh.begin() + static_cast<long>(i));
        gcm::u32 order = 0;
        for (auto piece : r.shapes) {
            piece.layer = layer;
            piece.poly = id;
            piece.poly_index = order++;
            sh.push_back(std::move(piece));
        }
    });
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%zu corner(s) %s, %zu too tight",
                  r.rounded, fillet ? "rounded" : "chamfered", r.skipped);
    c.log(buf);
}

struct FilletAll : CommandBase<FilletAll> {
    static constexpr auto name = "FILLETALL";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const { polyline_corners(c, name, true); }
};

struct ChamferAll : CommandBase<ChamferAll> {
    static constexpr auto name = "CHAMFERALL";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const { polyline_corners(c, name, false); }
};

}

void register_modify_commands(CommandTable& t) {
    t.add<Scale>();
    t.add<Lengthen>();
    t.add<Break>();
    t.add<Join>();
    t.add<Pedit>();
    t.add<Explode>();
    t.add<Align>();
    t.add<Overkill>();
    t.add<Blend>();
    t.add<Reverse>();
    t.add<GripEdit>();
    t.add<SelectPrevious>();
    t.add<SelectLast>();
    t.add<SelectCycle>();
    t.add<ArrayRect>();
    t.add<ArrayPolar>();
    t.add<ArrayPath>();
    t.add<FilletAll>();
    t.add<ChamferAll>();
}

}
