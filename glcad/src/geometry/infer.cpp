#include <algorithm>
#include <cmath>

#include "geometry/system.hpp"

namespace gsolve {

namespace {

constexpr f64 kPi = 3.14159265358979323846;

struct P { f64 x, y; };

}

std::vector<u32> System::touched_parameters(const Constraint& c) const {
    std::vector<u32> out;
    auto add_entity = [&](u32 e) {
        if (e >= entities_.size()) return;
        const Entity& en = entities_[e];
        if (en.kind == EntityKind::Line) {
            for (u32 sub : {en.p0, en.p1}) {
                if (sub >= entities_.size()) continue;
                const Entity& s = entities_[sub];
                for (u32 k = 0; k < s.count; ++k) out.push_back(s.base + k);
            }
            return;
        }
        if (en.kind == EntityKind::Circle || en.kind == EntityKind::Arc) {
            if (en.p0 < entities_.size()) {
                const Entity& s = entities_[en.p0];
                for (u32 k = 0; k < s.count; ++k) out.push_back(s.base + k);
            }
        }
        for (u32 k = 0; k < en.count; ++k) out.push_back(en.base + k);
    };

    for (u32 e : c.entities) add_entity(e);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<System::Cluster> System::decompose() const {
    // Fixed parameters do not join clusters.
    std::vector<u32> parent(params_.size());
    for (u32 i = 0; i < parent.size(); ++i) parent[i] = i;

    std::function<u32(u32)> find = [&](u32 a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    auto join = [&](u32 a, u32 b) {
        const u32 ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    };

    std::vector<std::vector<u32>> touched(constraints_.size());
    for (std::size_t ci = 0; ci < constraints_.size(); ++ci) {
        std::vector<u32> free_params;
        for (u32 p : touched_parameters(constraints_[ci]))
            if (p < fixed_.size() && !fixed_[p]) free_params.push_back(p);
        touched[ci] = free_params;
        for (std::size_t k = 1; k < free_params.size(); ++k)
            join(free_params[0], free_params[k]);
    }

    std::map<u32, Cluster> groups;
    for (std::size_t ci = 0; ci < constraints_.size(); ++ci) {
        if (touched[ci].empty()) continue;
        const u32 root = find(touched[ci][0]);
        Cluster& g = groups[root];
        g.constraints.push_back(static_cast<u32>(ci));
        for (u32 p : touched[ci]) g.parameters.push_back(p);
    }

    std::vector<Cluster> out;
    out.reserve(groups.size());
    for (auto& [root, g] : groups) {
        (void)root;
        std::sort(g.parameters.begin(), g.parameters.end());
        g.parameters.erase(std::unique(g.parameters.begin(), g.parameters.end()),
                           g.parameters.end());
        out.push_back(std::move(g));
    }
    std::sort(out.begin(), out.end(),
              [](const Cluster& a, const Cluster& b) { return a.size() > b.size(); });
    return out;
}

SolveResult System::solve_clusters(int max_iterations, f64 tolerance) {
    const std::vector<Cluster> clusters = decompose();
    if (clusters.size() <= 1) return solve(max_iterations, tolerance);

    SolveResult best;
    best.status = Status::AlreadySatisfied;

    const std::vector<Constraint> all = constraints_;
    const std::vector<bool> was_fixed = fixed_;

    for (const Cluster& g : clusters) {
        constraints_.clear();
        for (u32 ci : g.constraints) constraints_.push_back(all[ci]);

        std::vector<bool> pin(params_.size(), true);
        for (u32 p : g.parameters) pin[p] = was_fixed[p];
        fixed_ = pin;

        const SolveResult r = solve(max_iterations, tolerance);
        best.iterations += r.iterations;
        best.residual = std::max(best.residual, r.residual);
        if (!r.ok() && r.status != Status::Empty) {
            best.status = r.status;
            for (std::size_t k = 0; k < g.constraints.size(); ++k)
                best.conflicting.push_back(g.constraints[k]);
        } else if (best.status == Status::AlreadySatisfied &&
                   r.status == Status::Solved) {
            best.status = Status::Solved;
        }
    }

    constraints_ = all;
    fixed_ = was_fixed;
    return best;
}

namespace {

struct LineInfo {
    u32 entity{kInvalid};
    P   a{}, b{};
    f64 length{0.0};
    f64 angle{0.0};        // radians, folded into [0, pi)
};

f64 fold_angle(f64 a) {
    while (a < 0.0) a += kPi;
    while (a >= kPi) a -= kPi;
    return a;
}

f64 angle_gap(f64 a, f64 b) {
    f64 d = std::fabs(a - b);
    if (d > kPi * 0.5) d = kPi - d;
    return d;
}

}

std::vector<Constraint> infer_constraints(const System& s,
                                          const InferenceSettings& in) {
    std::vector<Constraint> out;
    const f64 ang_tol = in.angle_tolerance_deg * kPi / 180.0;

    std::vector<u32> points, circles;
    std::vector<LineInfo> lines;

    for (u32 e = 0; e < s.entity_count(); ++e) {
        const Entity& en = s.entities()[e];
        switch (en.kind) {
            case EntityKind::Point:
                points.push_back(e);
                break;
            case EntityKind::Circle:
            case EntityKind::Arc:
                circles.push_back(e);
                break;
            case EntityKind::Line: {
                LineInfo li;
                li.entity = e;
                f64 x, y;
                s.point_at(en.p0, x, y);
                li.a = {x, y};
                s.point_at(en.p1, x, y);
                li.b = {x, y};
                li.length = std::hypot(li.b.x - li.a.x, li.b.y - li.a.y);
                li.angle = fold_angle(std::atan2(li.b.y - li.a.y, li.b.x - li.a.x));
                lines.push_back(li);
                break;
            }
        }
    }

    auto emit = [&](Kind k, std::vector<u32> ents, const char* label) {
        if (out.size() >= in.max_suggestions) return;
        Constraint c;
        c.kind = k;
        c.entities = std::move(ents);
        c.label = label;
        out.push_back(std::move(c));
    };

    if (in.coincident) {
        for (std::size_t i = 0; i < points.size(); ++i)
            for (std::size_t j = i + 1; j < points.size(); ++j) {
                f64 ax, ay, bx, by;
                s.point_at(points[i], ax, ay);
                s.point_at(points[j], bx, by);
                if (std::hypot(bx - ax, by - ay) <= in.point_tolerance)
                    emit(Kind::Coincident, {points[i], points[j]}, "coincident");
            }
    }

    enum class Axis : gcm::u8 { None, Horizontal, Vertical };
    std::vector<Axis> axis(lines.size(), Axis::None);

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const LineInfo& l = lines[i];
        if (l.length < 1e-12) continue;
        if (angle_gap(l.angle, 0.0) <= ang_tol) {
            axis[i] = Axis::Horizontal;
            if (in.horizontal) emit(Kind::Horizontal, {l.entity}, "horizontal");
        } else if (angle_gap(l.angle, kPi * 0.5) <= ang_tol) {
            axis[i] = Axis::Vertical;
            if (in.vertical) emit(Kind::Vertical, {l.entity}, "vertical");
        }
    }

    for (std::size_t i = 0; i < lines.size(); ++i)
        for (std::size_t j = i + 1; j < lines.size(); ++j) {
            const LineInfo& a = lines[i];
            const LineInfo& b = lines[j];
            if (a.length < 1e-12 || b.length < 1e-12) continue;

            const bool implied = axis[i] != Axis::None && axis[j] != Axis::None;

            const f64 gap = angle_gap(a.angle, b.angle);
            if (!implied) {
                if (in.parallel && gap <= ang_tol)
                    emit(Kind::Parallel, {a.entity, b.entity}, "parallel");
                else if (in.perpendicular && std::fabs(gap - kPi * 0.5) <= ang_tol)
                    emit(Kind::Perpendicular, {a.entity, b.entity}, "perpendicular");
            }

            if (in.equal && std::fabs(a.length - b.length) <= in.length_tolerance)
                emit(Kind::Equal, {a.entity, b.entity}, "equal length");
        }

    for (std::size_t i = 0; i < circles.size(); ++i) {
        for (std::size_t j = i + 1; j < circles.size(); ++j) {
            f64 ax, ay, bx, by;
            s.point_at(s.entities()[circles[i]].p0, ax, ay);
            s.point_at(s.entities()[circles[j]].p0, bx, by);
            if (in.concentric && std::hypot(bx - ax, by - ay) <= in.point_tolerance)
                emit(Kind::Concentric, {circles[i], circles[j]}, "concentric");
            if (in.equal &&
                std::fabs(s.radius_of(circles[i]) - s.radius_of(circles[j])) <=
                    in.length_tolerance)
                emit(Kind::Equal, {circles[i], circles[j]}, "equal radius");
        }

        if (!in.tangent) continue;
        f64 cx, cy;
        s.point_at(s.entities()[circles[i]].p0, cx, cy);
        const f64 r = s.radius_of(circles[i]);
        for (const LineInfo& l : lines) {
            if (l.length < 1e-12) continue;
            const f64 cross = (l.b.x - l.a.x) * (cy - l.a.y) -
                              (l.b.y - l.a.y) * (cx - l.a.x);
            const f64 dist = std::fabs(cross) / l.length;
            if (std::fabs(dist - r) <= in.length_tolerance)
                emit(Kind::Tangent, {l.entity, circles[i]}, "tangent");
        }
    }

    return out;
}

std::size_t auto_constrain(System& s, const InferenceSettings& in) {
    std::size_t added = 0;
    for (const Constraint& c : infer_constraints(s, in)) {
        if (s.would_conflict(c)) continue;
        s.add(c);
        ++added;
    }
    return added;
}


bool System::constraint_jacobian(
    const Constraint& c,
    std::vector<std::vector<std::pair<u32, f64>>>& rows) const {
    rows.clear();

    auto px = [&](u32 e) { return entities_[e].base; };
    auto py = [&](u32 e) { return entities_[e].base + 1; };
    auto at = [&](u32 e, f64& x, f64& y) {
        x = params_[entities_[e].base];
        y = params_[entities_[e].base + 1];
    };
    auto line = [&](u32 e, u32& a, u32& b) {
        a = entities_[e].p0;
        b = entities_[e].p1;
    };
    auto rad_index = [&](u32 e) { return entities_[e].base; };
    auto centre = [&](u32 e) { return entities_[e].p0; };

    switch (c.kind) {
        case Kind::Coincident:
        case Kind::Concentric: {
            const u32 a = c.kind == Kind::Coincident ? c.entities[0]
                                                     : centre(c.entities[0]);
            const u32 b = c.kind == Kind::Coincident ? c.entities[1]
                                                     : centre(c.entities[1]);
            rows.push_back({{px(a), 1.0}, {px(b), -1.0}});
            rows.push_back({{py(a), 1.0}, {py(b), -1.0}});
            return true;
        }

        case Kind::Horizontal: {
            u32 a, b;
            line(c.entities[0], a, b);
            rows.push_back({{py(a), 1.0}, {py(b), -1.0}});
            return true;
        }
        case Kind::Vertical: {
            u32 a, b;
            line(c.entities[0], a, b);
            rows.push_back({{px(a), 1.0}, {px(b), -1.0}});
            return true;
        }

        case Kind::Parallel: {
            // r = (bx-ax)(dy-cy) - (by-ay)(dx-cx)
            u32 a, b, cc, d;
            line(c.entities[0], a, b);
            line(c.entities[1], cc, d);
            f64 ax, ay, bx, by, cx, cy, dx, dy;
            at(a, ax, ay); at(b, bx, by); at(cc, cx, cy); at(d, dx, dy);
            rows.push_back({{px(a), -(dy - cy)}, {px(b), (dy - cy)},
                            {py(a), (dx - cx)},  {py(b), -(dx - cx)},
                            {px(cc), (by - ay)}, {px(d), -(by - ay)},
                            {py(cc), -(bx - ax)},{py(d), (bx - ax)}});
            return true;
        }

        case Kind::Perpendicular: {
            // r = (bx-ax)(dx-cx) + (by-ay)(dy-cy)
            u32 a, b, cc, d;
            line(c.entities[0], a, b);
            line(c.entities[1], cc, d);
            f64 ax, ay, bx, by, cx, cy, dx, dy;
            at(a, ax, ay); at(b, bx, by); at(cc, cx, cy); at(d, dx, dy);
            rows.push_back({{px(a), -(dx - cx)}, {px(b), (dx - cx)},
                            {py(a), -(dy - cy)}, {py(b), (dy - cy)},
                            {px(cc), -(bx - ax)},{px(d), (bx - ax)},
                            {py(cc), -(by - ay)},{py(d), (by - ay)}});
            return true;
        }

        case Kind::Distance: {
            // r = |b - a| - value
            f64 ax, ay, bx, by;
            at(c.entities[0], ax, ay);
            at(c.entities[1], bx, by);
            const f64 dx = bx - ax, dy = by - ay;
            const f64 len = std::hypot(dx, dy);
            if (len < 1e-12) return false;
            rows.push_back({{px(c.entities[0]), -dx / len},
                            {py(c.entities[0]), -dy / len},
                            {px(c.entities[1]), dx / len},
                            {py(c.entities[1]), dy / len}});
            return true;
        }

        case Kind::HorizontalDist:
            rows.push_back({{px(c.entities[0]), -1.0}, {px(c.entities[1]), 1.0}});
            return true;
        case Kind::VerticalDist:
            rows.push_back({{py(c.entities[0]), -1.0}, {py(c.entities[1]), 1.0}});
            return true;

        case Kind::Radius:
            rows.push_back({{rad_index(c.entities[0]), 1.0}});
            return true;
        case Kind::Diameter:
            rows.push_back({{rad_index(c.entities[0]), 2.0}});
            return true;

        case Kind::PointOnCircle: {
            // r = |p - centre| - radius
            const u32 cen = centre(c.entities[1]);
            f64 pxv, pyv, cxv, cyv;
            at(c.entities[0], pxv, pyv);
            at(cen, cxv, cyv);
            const f64 dx = pxv - cxv, dy = pyv - cyv;
            const f64 len = std::hypot(dx, dy);
            if (len < 1e-12) return false;
            rows.push_back({{px(c.entities[0]), dx / len},
                            {py(c.entities[0]), dy / len},
                            {px(cen), -dx / len},
                            {py(cen), -dy / len},
                            {rad_index(c.entities[1]), -1.0}});
            return true;
        }

        case Kind::Equal: {
            const Entity& e0 = entities_[c.entities[0]];
            if (e0.kind != EntityKind::Line) {
                rows.push_back({{rad_index(c.entities[0]), 1.0},
                                {rad_index(c.entities[1]), -1.0}});
                return true;
            }
            u32 a, b, cc, d;
            line(c.entities[0], a, b);
            line(c.entities[1], cc, d);
            f64 ax, ay, bx, by, cx, cy, dx2, dy2;
            at(a, ax, ay); at(b, bx, by); at(cc, cx, cy); at(d, dx2, dy2);
            const f64 ux = bx - ax, uy = by - ay;
            const f64 vx = dx2 - cx, vy = dy2 - cy;
            const f64 lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
            if (lu < 1e-12 || lv < 1e-12) return false;
            rows.push_back({{px(a), -ux / lu}, {py(a), -uy / lu},
                            {px(b), ux / lu},  {py(b), uy / lu},
                            {px(cc), vx / lv}, {py(cc), vy / lv},
                            {px(d), -vx / lv}, {py(d), -vy / lv}});
            return true;
        }

        case Kind::FixPoint:
            rows.push_back({{px(c.entities[0]), 1.0}});
            rows.push_back({{py(c.entities[0]), 1.0}});
            return true;

        default:
            // These divide by a length that can vanish mid-solve: use finite differences.
            return false;
    }
}

}
