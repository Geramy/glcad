#include "geometry/system.hpp"

#include <algorithm>
#include <cmath>

namespace gsolve {

const char* kind_name(Kind k) noexcept {
    switch (k) {
        case Kind::Coincident:     return "coincident";
        case Kind::Horizontal:     return "horizontal";
        case Kind::Vertical:       return "vertical";
        case Kind::Parallel:       return "parallel";
        case Kind::Perpendicular:  return "perpendicular";
        case Kind::Collinear:      return "collinear";
        case Kind::Equal:          return "equal";
        case Kind::Distance:       return "distance";
        case Kind::PointOnLine:    return "point on line";
        case Kind::PointOnCircle:  return "point on circle";
        case Kind::Tangent:        return "tangent";
        case Kind::Concentric:     return "concentric";
        case Kind::Radius:         return "radius";
        case Kind::Diameter:       return "diameter";
        case Kind::Angle:          return "angle";
        case Kind::Symmetric:      return "symmetric";
        case Kind::FixPoint:       return "fixed";
        case Kind::HorizontalDist: return "horizontal distance";
        case Kind::VerticalDist:   return "vertical distance";
    }
    return "";
}

const char* SolveResult::message() const noexcept {
    switch (status) {
        case Status::Solved:            return "solved";
        case Status::AlreadySatisfied:  return "already satisfied";
        case Status::DidNotConverge:    return "did not converge";
        case Status::Overconstrained:   return "over-constrained: conflicting constraints";
        case Status::Empty:             return "nothing to solve";
    }
    return "";
}

const char* Dof::describe() const noexcept {
    if (over_constrained()) return "Over-constrained";
    if (remaining == 0) return "Fully constrained";
    return "Under-constrained";
}

u32 System::add_point(f64 x, f64 y) {
    Entity e;
    e.kind = EntityKind::Point;
    e.base = static_cast<u32>(params_.size());
    e.count = 2;
    params_.push_back(x);
    params_.push_back(y);
    fixed_.push_back(false);
    fixed_.push_back(false);
    entities_.push_back(e);
    return static_cast<u32>(entities_.size() - 1);
}

u32 System::add_line(u32 a, u32 b) {
    Entity e;
    e.kind = EntityKind::Line;
    e.p0 = a;
    e.p1 = b;
    entities_.push_back(e);
    return static_cast<u32>(entities_.size() - 1);
}

u32 System::add_circle(u32 centre, f64 r) {
    Entity e;
    e.kind = EntityKind::Circle;
    e.p0 = centre;
    e.base = static_cast<u32>(params_.size());
    e.count = 1;
    params_.push_back(r);
    fixed_.push_back(false);
    entities_.push_back(e);
    return static_cast<u32>(entities_.size() - 1);
}

u32 System::add_arc(u32 centre, f64 r, f64 a0, f64 a1) {
    Entity e;
    e.kind = EntityKind::Arc;
    e.p0 = centre;
    e.base = static_cast<u32>(params_.size());
    e.count = 3;
    params_.push_back(r);
    params_.push_back(a0);
    params_.push_back(a1);
    for (int i = 0; i < 3; ++i) fixed_.push_back(false);
    entities_.push_back(e);
    return static_cast<u32>(entities_.size() - 1);
}

void System::point_at(u32 e, f64& x, f64& y) const {
    const Entity& en = entities_[e];
    x = params_[en.base];
    y = params_[en.base + 1];
}
f64 System::radius_of(u32 e) const { return params_[entities_[e].base]; }
void System::set_point(u32 e, f64 x, f64 y) {
    params_[entities_[e].base] = x;
    params_[entities_[e].base + 1] = y;
}

void System::fix(u32 e, bool f) {
    const Entity& en = entities_[e];
    for (u32 i = 0; i < en.count; ++i) fixed_[en.base + i] = f;
}
bool System::is_fixed(u32 e) const { return fixed_[entities_[e].base]; }

u32 System::add(Constraint c) {
    constraints_.push_back(std::move(c));
    return static_cast<u32>(constraints_.size() - 1);
}
bool System::remove(u32 i) {
    if (i >= constraints_.size()) return false;
    constraints_.erase(constraints_.begin() + i);
    return true;
}

namespace {
struct P { f64 x, y; };
}

void System::residual_of(const Constraint& c, std::vector<f64>& out) const {
    auto pt = [&](u32 e) {
        const Entity& en = entities_[e];
        return P{params_[en.base], params_[en.base + 1]};
    };
    auto line_pts = [&](u32 e, P& a, P& b) {
        const Entity& en = entities_[e];
        a = pt(en.p0);
        b = pt(en.p1);
    };
    auto rad = [&](u32 e) { return params_[entities_[e].base]; };
    auto centre = [&](u32 e) { return pt(entities_[e].p0); };

    switch (c.kind) {
        case Kind::Coincident: {
            const P a = pt(c.entities[0]), b = pt(c.entities[1]);
            out.push_back(a.x - b.x);
            out.push_back(a.y - b.y);
            break;
        }
        case Kind::Horizontal: {
            P a, b;
            line_pts(c.entities[0], a, b);
            out.push_back(a.y - b.y);
            break;
        }
        case Kind::Vertical: {
            P a, b;
            line_pts(c.entities[0], a, b);
            out.push_back(a.x - b.x);
            break;
        }
        case Kind::Parallel: {
            P a, b, cc, d;
            line_pts(c.entities[0], a, b);
            line_pts(c.entities[1], cc, d);
            out.push_back((b.x - a.x) * (d.y - cc.y) - (b.y - a.y) * (d.x - cc.x));
            break;
        }
        case Kind::Perpendicular: {
            P a, b, cc, d;
            line_pts(c.entities[0], a, b);
            line_pts(c.entities[1], cc, d);
            out.push_back((b.x - a.x) * (d.x - cc.x) + (b.y - a.y) * (d.y - cc.y));
            break;
        }
        case Kind::Collinear: {
            P a, b, cc, d;
            line_pts(c.entities[0], a, b);
            line_pts(c.entities[1], cc, d);
            out.push_back((b.x - a.x) * (d.y - cc.y) - (b.y - a.y) * (d.x - cc.x));
            out.push_back((b.x - a.x) * (cc.y - a.y) - (b.y - a.y) * (cc.x - a.x));
            break;
        }
        case Kind::Equal: {
            const Entity& e0 = entities_[c.entities[0]];
            if (e0.kind == EntityKind::Line) {
                P a, b, cc, d;
                line_pts(c.entities[0], a, b);
                line_pts(c.entities[1], cc, d);
                out.push_back(std::hypot(b.x - a.x, b.y - a.y) -
                              std::hypot(d.x - cc.x, d.y - cc.y));
            } else {
                out.push_back(rad(c.entities[0]) - rad(c.entities[1]));
            }
            break;
        }
        case Kind::Distance: {
            const P a = pt(c.entities[0]), b = pt(c.entities[1]);
            out.push_back(std::hypot(b.x - a.x, b.y - a.y) - c.value);
            break;
        }
        case Kind::HorizontalDist: {
            const P a = pt(c.entities[0]), b = pt(c.entities[1]);
            out.push_back((b.x - a.x) - c.value);
            break;
        }
        case Kind::VerticalDist: {
            const P a = pt(c.entities[0]), b = pt(c.entities[1]);
            out.push_back((b.y - a.y) - c.value);
            break;
        }
        case Kind::PointOnLine: {
            const P p = pt(c.entities[0]);
            P a, b;
            line_pts(c.entities[1], a, b);
            const f64 len = std::hypot(b.x - a.x, b.y - a.y);
            const f64 cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            out.push_back(len > 1e-12 ? cross / len : cross);
            break;
        }
        case Kind::PointOnCircle: {
            const P p = pt(c.entities[0]);
            const P cc = centre(c.entities[1]);
            out.push_back(std::hypot(p.x - cc.x, p.y - cc.y) - rad(c.entities[1]));
            break;
        }
        case Kind::Tangent: {
            P a, b;
            line_pts(c.entities[0], a, b);
            const P cc = centre(c.entities[1]);
            const f64 len = std::hypot(b.x - a.x, b.y - a.y);
            const f64 cross = (b.x - a.x) * (cc.y - a.y) - (b.y - a.y) * (cc.x - a.x);
            const f64 dist = len > 1e-12 ? std::fabs(cross) / len : 0.0;
            out.push_back(dist - rad(c.entities[1]));
            break;
        }
        case Kind::Concentric: {
            const P a = centre(c.entities[0]), b = centre(c.entities[1]);
            out.push_back(a.x - b.x);
            out.push_back(a.y - b.y);
            break;
        }
        case Kind::Radius:
            out.push_back(rad(c.entities[0]) - c.value);
            break;
        case Kind::Diameter:
            out.push_back(2.0 * rad(c.entities[0]) - c.value);
            break;
        case Kind::Angle: {
            P a, b, cc, d;
            line_pts(c.entities[0], a, b);
            line_pts(c.entities[1], cc, d);
            const f64 a1 = std::atan2(b.y - a.y, b.x - a.x);
            const f64 a2 = std::atan2(d.y - cc.y, d.x - cc.x);
            f64 diff = a2 - a1 - c.value;
            while (diff > 3.14159265358979323846) diff -= 2.0 * 3.14159265358979323846;
            while (diff < -3.14159265358979323846) diff += 2.0 * 3.14159265358979323846;
            out.push_back(diff);
            break;
        }
        case Kind::Symmetric: {
            const P p = pt(c.entities[0]), q = pt(c.entities[1]);
            P a, b;
            line_pts(c.entities[2], a, b);
            const f64 dx = b.x - a.x, dy = b.y - a.y;
            const f64 len2 = dx * dx + dy * dy;
            if (len2 < 1e-24) { out.push_back(p.x - q.x); out.push_back(p.y - q.y); break; }
            const P mid{(p.x + q.x) * 0.5, (p.y + q.y) * 0.5};
            out.push_back(dx * (mid.y - a.y) - dy * (mid.x - a.x));
            out.push_back(dx * (q.x - p.x) + dy * (q.y - p.y));
            break;
        }
        case Kind::FixPoint: {
            const P p = pt(c.entities[0]);
            out.push_back(p.x - c.value);
            out.push_back(p.y - c.value);
            break;
        }
    }
}

std::vector<f64> System::residuals() const {
    std::vector<f64> out;
    for (const auto& c : constraints_)
        if (c.driving) residual_of(c, out);
    return out;
}

f64 System::total_error() const {
    f64 s = 0.0;
    for (f64 r : residuals()) s += r * r;
    return std::sqrt(s);
}

std::vector<f64> System::all_residuals(const std::vector<f64>& p) const {
    System tmp = *this;
    tmp.params_ = p;
    return tmp.residuals();
}

SolveResult System::solve(int max_iter, f64 tol) {
    SolveResult r;

    std::vector<u32> free_idx;
    for (u32 i = 0; i < params_.size(); ++i)
        if (!fixed_[i]) free_idx.push_back(i);

    std::vector<f64> res = residuals();
    if (res.empty() || free_idx.empty()) {
        r.status = Status::Empty;
        return r;
    }
    if (total_error() < tol) {
        r.status = Status::AlreadySatisfied;
        r.residual = total_error();
        return r;
    }

    const std::size_t m = res.size(), n = free_idx.size();
    f64 lambda = 1e-3;

    for (int iter = 0; iter < max_iter; ++iter) {
        res = residuals();
        f64 err = 0.0;
        for (f64 v : res) err += v * v;
        if (std::sqrt(err) < tol) {
            r.status = Status::Solved;
            r.iterations = iter;
            r.residual = std::sqrt(err);
            return r;
        }

        std::vector<i32> column(params_.size(), -1);
        for (std::size_t j = 0; j < n; ++j) column[free_idx[j]] = static_cast<i32>(j);

        std::vector<f64> J(m * n, 0.0);
        std::size_t row = 0;
        std::vector<std::vector<std::pair<u32, f64>>> rows;
        std::vector<f64> before, after;

        for (const Constraint& c : constraints_) {
            std::vector<f64> probe;
            residual_of(c, probe);
            const std::size_t count = probe.size();
            if (count == 0) continue;

            if (constraint_jacobian(c, rows) && rows.size() == count) {
                // Accumulate, never assign: shared parameters' partials must add.
                for (std::size_t i = 0; i < count && row + i < m; ++i)
                    for (const auto& [param, d] : rows[i]) {
                        if (param >= column.size()) continue;
                        const i32 col = column[param];
                        if (col >= 0)
                            J[(row + i) * n + static_cast<std::size_t>(col)] += d;
                    }
                row += count;
                continue;
            }

            for (u32 param : touched_parameters(c)) {
                if (param >= column.size()) continue;
                const i32 col = column[param];
                if (col < 0) continue;

                const f64 orig = params_[param];
                const f64 h = std::max(1e-7, std::fabs(orig) * 1e-7);
                params_[param] = orig + h;
                after.clear();
                residual_of(c, after);
                params_[param] = orig - h;
                before.clear();
                residual_of(c, before);
                params_[param] = orig;

                for (std::size_t i = 0; i < count && row + i < m; ++i)
                    if (i < after.size() && i < before.size())
                        J[(row + i) * n + static_cast<std::size_t>(col)] =
                            (after[i] - before[i]) / (2.0 * h);
            }
            row += count;
        }

        // Normal equations with damping: (J'J + lambda*diag) dx = -J'r
        std::vector<f64> A(n * n, 0.0), g(n, 0.0);
        for (std::size_t i = 0; i < m; ++i) {
            for (std::size_t a = 0; a < n; ++a) {
                g[a] -= J[i * n + a] * res[i];
                for (std::size_t b = 0; b < n; ++b)
                    A[a * n + b] += J[i * n + a] * J[i * n + b];
            }
        }
        for (std::size_t a = 0; a < n; ++a) A[a * n + a] *= (1.0 + lambda);
        for (std::size_t a = 0; a < n; ++a) A[a * n + a] += 1e-12;

        std::vector<f64> M = A, rhs = g;
        std::vector<f64> dx(n, 0.0);
        bool singular = false;
        for (std::size_t col = 0; col < n; ++col) {
            std::size_t piv = col;
            for (std::size_t row = col + 1; row < n; ++row)
                if (std::fabs(M[row * n + col]) > std::fabs(M[piv * n + col])) piv = row;
            if (std::fabs(M[piv * n + col]) < 1e-14) { singular = true; break; }
            if (piv != col) {
                for (std::size_t k = 0; k < n; ++k)
                    std::swap(M[col * n + k], M[piv * n + k]);
                std::swap(rhs[col], rhs[piv]);
            }
            for (std::size_t row = col + 1; row < n; ++row) {
                const f64 f = M[row * n + col] / M[col * n + col];
                if (f == 0.0) continue;
                for (std::size_t k = col; k < n; ++k) M[row * n + k] -= f * M[col * n + k];
                rhs[row] -= f * rhs[col];
            }
        }
        if (!singular) {
            for (std::size_t i = n; i-- > 0;) {
                f64 sum = rhs[i];
                for (std::size_t k = i + 1; k < n; ++k) sum -= M[i * n + k] * dx[k];
                dx[i] = sum / M[i * n + i];
            }
        }

        const std::vector<f64> saved = params_;
        for (std::size_t j = 0; j < n; ++j) params_[free_idx[j]] += dx[j];

        f64 new_err = 0.0;
        for (f64 v : residuals()) new_err += v * v;

        if (new_err < err) {
            lambda = std::max(1e-9, lambda * 0.4);
        } else {
            params_ = saved;
            lambda *= 6.0;
            if (lambda > 1e9) break;
        }
    }

    r.residual = total_error();
    if (r.residual < tol * 1000.0) {
        r.status = Status::Solved;
        return r;
    }

    const Dof d = analyse();
    if (d.over_constrained()) {
        r.status = Status::Overconstrained;
        for (u32 i = 0; i < constraints_.size(); ++i)
            if (constraints_[i].driving) r.conflicting.push_back(i);
        if (r.conflicting.size() > 3)
            r.conflicting.erase(r.conflicting.begin(),
                                r.conflicting.end() - 3);
    } else {
        r.status = Status::DidNotConverge;
    }
    r.iterations = max_iter;
    return r;
}

i32 System::jacobian_rank(f64 eps) const {
    std::vector<u32> free_idx;
    for (u32 i = 0; i < params_.size(); ++i)
        if (!fixed_[i]) free_idx.push_back(i);

    const std::vector<f64> res = residuals();
    const std::size_t m = res.size(), n = free_idx.size();
    if (m == 0 || n == 0) return 0;

    System tmp = *this;
    std::vector<f64> J(m * n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        const u32 pi = free_idx[j];
        const f64 orig = tmp.params_[pi];
        const f64 h = std::max(1e-6, std::fabs(orig) * 1e-6);
        tmp.params_[pi] = orig + h;
        const std::vector<f64> rp = tmp.residuals();
        tmp.params_[pi] = orig - h;
        const std::vector<f64> rm = tmp.residuals();
        tmp.params_[pi] = orig;
        for (std::size_t i = 0; i < m; ++i) J[i * n + j] = (rp[i] - rm[i]) / (2.0 * h);
    }

    i32 rank = 0;
    std::size_t row = 0;
    for (std::size_t col = 0; col < n && row < m; ++col) {
        std::size_t piv = row;
        for (std::size_t r2 = row + 1; r2 < m; ++r2)
            if (std::fabs(J[r2 * n + col]) > std::fabs(J[piv * n + col])) piv = r2;
        if (std::fabs(J[piv * n + col]) < eps) continue;

        if (piv != row)
            for (std::size_t k = 0; k < n; ++k) std::swap(J[row * n + k], J[piv * n + k]);

        for (std::size_t r2 = row + 1; r2 < m; ++r2) {
            const f64 f = J[r2 * n + col] / J[row * n + col];
            if (f == 0.0) continue;
            for (std::size_t k = col; k < n; ++k) J[r2 * n + k] -= f * J[row * n + k];
        }
        ++row;
        ++rank;
    }
    return rank;
}

Dof System::analyse() const {
    Dof d;
    d.parameters = static_cast<i32>(params_.size());
    for (bool f : fixed_) if (f) ++d.fixed;

    i32 rows = 0;
    for (const auto& c : constraints_) {
        if (!c.driving) continue;
        std::vector<f64> tmp;
        residual_of(c, tmp);
        rows += static_cast<i32>(tmp.size());
    }

    const i32 rank = jacobian_rank();
    d.constraints = rank;
    d.redundant = rows - rank;
    d.remaining = (d.parameters - d.fixed) - rank;
    return d;
}

bool System::would_conflict(const Constraint& c) const {
    System probe = *this;
    const i32 before = probe.jacobian_rank();
    probe.constraints_.push_back(c);
    const i32 after = probe.jacobian_rank();

    std::vector<f64> tmp;
    residual_of(c, tmp);
    return after < before + static_cast<i32>(tmp.size());
}

}
