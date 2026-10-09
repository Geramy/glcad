#include "geometry/mate.hpp"

#include <algorithm>
#include <cmath>

#include "geometry/boolean.hpp"

namespace gtopo {

const char* mate_kind_name(MateKind k) noexcept {
    switch (k) {
        case MateKind::Coincident:    return "coincident";
        case MateKind::Flush:         return "flush";
        case MateKind::Parallel:      return "parallel";
        case MateKind::Perpendicular: return "perpendicular";
        case MateKind::Distance:      return "distance";
        case MateKind::Angle:         return "angle";
        case MateKind::Concentric:    return "concentric";
        case MateKind::Tangent:       return "tangent";
        case MateKind::Collinear:     return "collinear";
        case MateKind::PointOnFace:   return "point on face";
        case MateKind::Fix:           return "fix";
    }
    return "";
}

int mate_dof_removed(MateKind k) noexcept {
    switch (k) {
        case MateKind::Coincident:  return 3;   // one translation, two rotations
        case MateKind::Flush:       return 3;
        case MateKind::Parallel:    return 2;   // two rotations
        case MateKind::Perpendicular: return 1;
        case MateKind::Distance:    return 3;
        case MateKind::Angle:       return 1;
        case MateKind::Concentric:  return 4;   // two translations, two rotations
        case MateKind::Tangent:     return 1;
        case MateKind::Collinear:   return 4;
        case MateKind::PointOnFace: return 1;
        case MateKind::Fix:         return 6;
    }
    return 0;
}

const char* MateResult::message() const noexcept {
    switch (status) {
        case MateStatus::Solved:           return "solved";
        case MateStatus::AlreadySatisfied: return "already satisfied";
        case MateStatus::DidNotConverge:   return "the mates could not be closed";
        case MateStatus::OverConstrained:  return "over-constrained";
        case MateStatus::Conflicting:      return "conflicting mates";
        case MateStatus::Empty:            return "nothing to solve";
    }
    return "";
}

Placement Placement::translation(f64 x, f64 y, f64 z) noexcept {
    Placement p;
    p.t[0] = x;
    p.t[1] = y;
    p.t[2] = z;
    return p;
}

Placement Placement::rotation(f64 ax, f64 ay, f64 az, f64 radians) noexcept {
    Placement p;
    const f64 len = std::sqrt(ax * ax + ay * ay + az * az);
    if (len < 1e-15) return p;
    const f64 x = ax / len, y = ay / len, z = az / len;
    const f64 c = std::cos(radians), s = std::sin(radians), k = 1.0 - c;

    p.m[0] = c + x * x * k;      p.m[1] = x * y * k - z * s;  p.m[2] = x * z * k + y * s;
    p.m[3] = y * x * k + z * s;  p.m[4] = c + y * y * k;      p.m[5] = y * z * k - x * s;
    p.m[6] = z * x * k - y * s;  p.m[7] = z * y * k + x * s;  p.m[8] = c + z * z * k;
    return p;
}

void Placement::apply(f64 x, f64 y, f64 z, f64& ox, f64& oy, f64& oz) const noexcept {
    ox = m[0] * x + m[1] * y + m[2] * z + t[0];
    oy = m[3] * x + m[4] * y + m[5] * z + t[1];
    oz = m[6] * x + m[7] * y + m[8] * z + t[2];
}

void Placement::apply_direction(f64 x, f64 y, f64 z, f64& ox, f64& oy,
                                f64& oz) const noexcept {
    ox = m[0] * x + m[1] * y + m[2] * z;
    oy = m[3] * x + m[4] * y + m[5] * z;
    oz = m[6] * x + m[7] * y + m[8] * z;
}

Placement Placement::then(const Placement& n) const noexcept {
    Placement r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            f64 sum = 0.0;
            for (int k = 0; k < 3; ++k) sum += n.m[i * 3 + k] * m[k * 3 + j];
            r.m[i * 3 + j] = sum;
        }
    n.apply(t[0], t[1], t[2], r.t[0], r.t[1], r.t[2]);
    return r;
}

bool Placement::is_identity(f64 tol) const noexcept {
    const f64 id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i)
        if (std::fabs(m[i] - id[i]) > tol) return false;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(t[i]) > tol) return false;
    return true;
}

u32 Assembly::add(Instance i) {
    instances_.push_back(std::move(i));
    return static_cast<u32>(instances_.size() - 1);
}

Instance* Assembly::instance(u32 id) {
    return id < instances_.size() ? &instances_[id] : nullptr;
}
const Instance* Assembly::instance(u32 id) const {
    return id < instances_.size() ? &instances_[id] : nullptr;
}

u32 Assembly::add_mate(Mate m) {
    mates_.push_back(std::move(m));
    return static_cast<u32>(mates_.size() - 1);
}

bool Assembly::remove_mate(u32 index) {
    if (index >= mates_.size()) return false;
    mates_.erase(mates_.begin() + static_cast<long>(index));
    return true;
}

Plane Assembly::ref_plane(const MateRef& r, const Volume& v) const {
    Plane p{0, 0, 0, 0};
    const Instance* inst = instance(r.body);
    if (!inst || !inst->body || r.kind != RefKind::Face) return p;

    const Plane local = face_plane(*inst->body, r.index, v);
    if (!local.valid()) return p;

    f64 nx, ny, nz;
    inst->placement.apply_direction(local.nx, local.ny, local.nz, nx, ny, nz);
    const f64 px = local.nx * local.d, py = local.ny * local.d, pz = local.nz * local.d;
    f64 wx, wy, wz;
    inst->placement.apply(px, py, pz, wx, wy, wz);

    p.nx = nx;
    p.ny = ny;
    p.nz = nz;
    p.d = nx * wx + ny * wy + nz * wz;
    return p;
}

bool Assembly::ref_axis(const MateRef& r, const Volume& v, f64& ox, f64& oy,
                        f64& oz, f64& dx, f64& dy, f64& dz) const {
    const Instance* inst = instance(r.body);
    if (!inst || !inst->body) return false;
    const Body& b = *inst->body;

    if (r.kind == RefKind::Edge) {
        if (r.index >= b.halfedge_count()) return false;
        const u32 v0 = b.he_vertex[r.index];
        const u32 v1 = b.he_vertex[b.he_next[r.index]];
        f64 ax, ay, az, bx, by, bz;
        gcm::to_world(v, b.vertex_point[v0], ax, ay, az);
        gcm::to_world(v, b.vertex_point[v1], bx, by, bz);

        f64 wax, way, waz, wbx, wby, wbz;
        inst->placement.apply(ax, ay, az, wax, way, waz);
        inst->placement.apply(bx, by, bz, wbx, wby, wbz);
        const f64 len = std::sqrt((wbx - wax) * (wbx - wax) +
                                  (wby - way) * (wby - way) +
                                  (wbz - waz) * (wbz - waz));
        if (len < 1e-15) return false;
        ox = wax; oy = way; oz = waz;
        dx = (wbx - wax) / len;
        dy = (wby - way) / len;
        dz = (wbz - waz) / len;
        return true;
    }

    if (r.kind == RefKind::Face || r.kind == RefKind::Axis) {
        const Plane p = ref_plane({r.body, RefKind::Face, r.index}, v);
        if (!p.valid()) return false;
        ox = p.nx * p.d;
        oy = p.ny * p.d;
        oz = p.nz * p.d;
        dx = p.nx;
        dy = p.ny;
        dz = p.nz;
        return true;
    }
    return false;
}

bool Assembly::ref_point(const MateRef& r, const Volume& v, f64& x, f64& y,
                         f64& z) const {
    const Instance* inst = instance(r.body);
    if (!inst || !inst->body) return false;
    const Body& b = *inst->body;

    if (r.kind == RefKind::Vertex) {
        if (r.index >= b.vertex_count()) return false;
        f64 lx, ly, lz;
        gcm::to_world(v, b.vertex_point[r.index], lx, ly, lz);
        inst->placement.apply(lx, ly, lz, x, y, z);
        return true;
    }
    if (r.kind == RefKind::Face) {
        const Plane p = ref_plane(r, v);
        if (!p.valid()) return false;
        x = p.nx * p.d;
        y = p.ny * p.d;
        z = p.nz * p.d;
        return true;
    }
    f64 dx, dy, dz;
    return ref_axis(r, v, x, y, z, dx, dy, dz);
}

MateError Assembly::measure(const Mate& m, const Volume& v) const {
    MateError e;
    if (m.suppressed) {
        e.satisfied = true;
        return e;
    }

    auto dot = [](const Plane& a, const Plane& b) {
        return a.nx * b.nx + a.ny * b.ny + a.nz * b.nz;
    };

    switch (m.kind) {
        case MateKind::Fix:
            e.satisfied = true;
            return e;

        case MateKind::Coincident:
        case MateKind::Flush:
        case MateKind::Distance: {
            const Plane pa = ref_plane(m.a, v), pb = ref_plane(m.b, v);
            if (!pa.valid() || !pb.valid()) return e;

            const f64 want = (m.kind == MateKind::Flush) ? 1.0 : -1.0;
            const f64 sense = m.flip ? -want : want;
            e.angular = std::acos(std::clamp(dot(pa, pb) * sense, -1.0, 1.0));

            const f64 gap = pb.d * (dot(pa, pb) >= 0.0 ? 1.0 : -1.0) - pa.d;
            e.linear = m.kind == MateKind::Distance ? gap - m.value : gap;
            e.satisfied = std::fabs(e.linear) < 1e-9 && e.angular < 1e-9;
            return e;
        }

        case MateKind::Parallel: {
            const Plane pa = ref_plane(m.a, v), pb = ref_plane(m.b, v);
            if (!pa.valid() || !pb.valid()) return e;
            e.angular = std::acos(std::clamp(std::fabs(dot(pa, pb)), -1.0, 1.0));
            e.satisfied = e.angular < 1e-9;
            return e;
        }

        case MateKind::Perpendicular: {
            const Plane pa = ref_plane(m.a, v), pb = ref_plane(m.b, v);
            if (!pa.valid() || !pb.valid()) return e;
            e.angular = std::fabs(dot(pa, pb));
            e.satisfied = e.angular < 1e-9;
            return e;
        }

        case MateKind::Angle: {
            const Plane pa = ref_plane(m.a, v), pb = ref_plane(m.b, v);
            if (!pa.valid() || !pb.valid()) return e;
            const f64 got = std::acos(std::clamp(dot(pa, pb), -1.0, 1.0));
            e.angular = std::fabs(got - m.value);
            e.satisfied = e.angular < 1e-9;
            return e;
        }

        case MateKind::Concentric:
        case MateKind::Collinear: {
            f64 ax, ay, az, adx, ady, adz;
            f64 bx, by, bz, bdx, bdy, bdz;
            if (!ref_axis(m.a, v, ax, ay, az, adx, ady, adz)) return e;
            if (!ref_axis(m.b, v, bx, by, bz, bdx, bdy, bdz)) return e;

            const f64 d = adx * bdx + ady * bdy + adz * bdz;
            e.angular = std::acos(std::clamp(std::fabs(d), -1.0, 1.0));

            const f64 vx = bx - ax, vy = by - ay, vz = bz - az;
            const f64 along = vx * adx + vy * ady + vz * adz;
            const f64 px = vx - adx * along, py = vy - ady * along,
                      pz = vz - adz * along;
            e.linear = std::sqrt(px * px + py * py + pz * pz);
            e.satisfied = e.linear < 1e-9 && e.angular < 1e-9;
            return e;
        }

        case MateKind::Tangent: {
            const Plane pa = ref_plane(m.a, v);
            f64 bx, by, bz, bdx, bdy, bdz;
            if (!pa.valid() || !ref_axis(m.b, v, bx, by, bz, bdx, bdy, bdz))
                return e;
            e.linear = std::fabs(pa.distance(bx, by, bz)) - m.value;
            e.satisfied = std::fabs(e.linear) < 1e-9;
            return e;
        }

        case MateKind::PointOnFace: {
            f64 x, y, z;
            const Plane pb = ref_plane(m.b, v);
            if (!ref_point(m.a, v, x, y, z) || !pb.valid()) return e;
            e.linear = pb.distance(x, y, z);
            e.satisfied = std::fabs(e.linear) < 1e-9;
            return e;
        }
    }
    return e;
}

int Assembly::degrees_of_freedom() const {
    int total = 0;
    for (const Instance& i : instances_)
        if (!i.grounded) total += 6;
    for (const Mate& m : mates_) {
        if (m.suppressed) continue;
        const Instance* a = instance(m.a.body);
        const Instance* b = instance(m.b.body);
        if (a && b && a->grounded && b->grounded) continue;
        total -= mate_dof_removed(m.kind);
    }
    return total;
}

MateResult Assembly::solve(const Volume& v, int max_iterations, f64 tolerance) {
    MateResult r;
    if (mates_.empty() || instances_.empty()) return r;

    bool any_free = false;
    for (const Instance& i : instances_)
        if (!i.grounded) any_free = true;
    if (!any_free) {
        r.status = MateStatus::AlreadySatisfied;
        return r;
    }

    auto total_error = [&]() {
        f64 e = 0.0;
        for (const Mate& m : mates_) {
            if (m.suppressed) continue;
            const MateError me = measure(m, v);
            e += me.linear * me.linear + me.angular * me.angular;
        }
        return std::sqrt(e);
    };

    if (total_error() < tolerance) {
        r.status = MateStatus::AlreadySatisfied;
        r.residual = total_error();
        return r;
    }

    for (int iter = 0; iter < max_iterations; ++iter) {
        const f64 before = total_error();
        if (before < tolerance) {
            r.status = MateStatus::Solved;
            r.iterations = iter;
            r.residual = before;
            return r;
        }

        for (std::size_t mi = 0; mi < mates_.size(); ++mi) {
            const Mate& m = mates_[mi];
            if (m.suppressed || m.kind == MateKind::Fix) continue;

            Instance* a = instance(m.a.body);
            Instance* b = instance(m.b.body);
            if (!a || !b) continue;
            const bool a_free = !a->grounded, b_free = !b->grounded;
            if (!a_free && !b_free) continue;

            const f64 share = (a_free && b_free) ? 0.5 : 1.0;
            Instance* mover = b_free ? b : a;
            const f64 sign = b_free ? 1.0 : -1.0;

            switch (m.kind) {
                case MateKind::Coincident:
                case MateKind::Flush:
                case MateKind::Distance:
                case MateKind::Parallel:
                case MateKind::Perpendicular:
                case MateKind::Angle: {
                    const Plane pa = ref_plane(m.a, v), pb = ref_plane(m.b, v);
                    if (!pa.valid() || !pb.valid()) break;

                    f64 target[3] = {pa.nx, pa.ny, pa.nz};
                    if (m.kind == MateKind::Coincident) {
                        target[0] = -pa.nx; target[1] = -pa.ny; target[2] = -pa.nz;
                    } else if (m.kind == MateKind::Distance) {
                        target[0] = -pa.nx; target[1] = -pa.ny; target[2] = -pa.nz;
                    } else if (m.kind == MateKind::Perpendicular) {
                        const f64 d = pa.nx * pb.nx + pa.ny * pb.ny + pa.nz * pb.nz;
                        target[0] = pb.nx - pa.nx * d;
                        target[1] = pb.ny - pa.ny * d;
                        target[2] = pb.nz - pa.nz * d;
                    } else if (m.kind == MateKind::Angle) {
                        const f64 got = std::acos(std::clamp(
                            pa.nx * pb.nx + pa.ny * pb.ny + pa.nz * pb.nz, -1.0, 1.0));
                        const f64 delta = (m.value - got) * share;
                        f64 axx = pa.ny * pb.nz - pa.nz * pb.ny;
                        f64 axy = pa.nz * pb.nx - pa.nx * pb.nz;
                        f64 axz = pa.nx * pb.ny - pa.ny * pb.nx;
                        if (std::sqrt(axx * axx + axy * axy + axz * axz) < 1e-12) {
                            axx = 1.0; axy = 0.0; axz = 0.0;
                        }
                        mover->placement = mover->placement.then(
                            Placement::rotation(axx, axy, axz, -sign * delta));
                        break;
                    } else if (m.kind == MateKind::Parallel) {
                        const f64 d = pa.nx * pb.nx + pa.ny * pb.ny + pa.nz * pb.nz;
                        if (d < 0.0) {
                            target[0] = -pa.nx; target[1] = -pa.ny; target[2] = -pa.nz;
                        }
                    }
                    if (m.flip) {
                        target[0] = -target[0];
                        target[1] = -target[1];
                        target[2] = -target[2];
                    }
                    if (m.kind == MateKind::Angle) break;

                    const f64 mx = b_free ? pb.nx : pa.nx;
                    const f64 my = b_free ? pb.ny : pa.ny;
                    const f64 mz = b_free ? pb.nz : pa.nz;
                    const f64 tx = b_free ? target[0] : -target[0];
                    const f64 ty = b_free ? target[1] : -target[1];
                    const f64 tz = b_free ? target[2] : -target[2];

                    f64 axx = my * tz - mz * ty;
                    f64 axy = mz * tx - mx * tz;
                    f64 axz = mx * ty - my * tx;
                    const f64 alen = std::sqrt(axx * axx + axy * axy + axz * axz);
                    const f64 d = std::clamp(mx * tx + my * ty + mz * tz, -1.0, 1.0);
                    const f64 angle = std::acos(d);
                    if (angle > 1e-12) {
                        if (alen < 1e-12) {
                            axx = std::fabs(mx) < 0.9 ? 1.0 : 0.0;
                            axy = std::fabs(mx) < 0.9 ? 0.0 : 1.0;
                            axz = 0.0;
                        }
                        mover->placement = mover->placement.then(
                            Placement::rotation(axx, axy, axz, angle * share));
                    }

                    if (m.kind == MateKind::Parallel ||
                        m.kind == MateKind::Perpendicular)
                        break;

                    const MateError e = measure(m, v);
                    const Plane now = ref_plane(m.a, v);
                    const f64 push = -e.linear * share * sign;
                    mover->placement = mover->placement.then(Placement::translation(
                        now.nx * push, now.ny * push, now.nz * push));
                    break;
                }

                case MateKind::Concentric:
                case MateKind::Collinear: {
                    f64 ax, ay, az, adx, ady, adz;
                    f64 bx, by, bz, bdx, bdy, bdz;
                    if (!ref_axis(m.a, v, ax, ay, az, adx, ady, adz)) break;
                    if (!ref_axis(m.b, v, bx, by, bz, bdx, bdy, bdz)) break;

                    f64 mx = b_free ? bdx : adx, my = b_free ? bdy : ady,
                        mz = b_free ? bdz : adz;
                    f64 tx = b_free ? adx : bdx, ty = b_free ? ady : bdy,
                        tz = b_free ? adz : bdz;
                    if (mx * tx + my * ty + mz * tz < 0.0) {
                        tx = -tx; ty = -ty; tz = -tz;
                    }
                    f64 axx = my * tz - mz * ty;
                    f64 axy = mz * tx - mx * tz;
                    f64 axz = mx * ty - my * tx;
                    const f64 alen = std::sqrt(axx * axx + axy * axy + axz * axz);
                    if (alen > 1e-12) {
                        const f64 angle = std::asin(std::clamp(alen, -1.0, 1.0));
                        mover->placement = mover->placement.then(
                            Placement::rotation(axx, axy, axz, angle * share));
                    }

                    if (!ref_axis(m.a, v, ax, ay, az, adx, ady, adz)) break;
                    if (!ref_axis(m.b, v, bx, by, bz, bdx, bdy, bdz)) break;
                    const f64 vx = bx - ax, vy = by - ay, vz = bz - az;
                    const f64 along = vx * adx + vy * ady + vz * adz;
                    const f64 px = vx - adx * along, py = vy - ady * along,
                              pz = vz - adz * along;
                    mover->placement = mover->placement.then(Placement::translation(
                        -px * share * sign, -py * share * sign, -pz * share * sign));
                    break;
                }

                case MateKind::PointOnFace: {
                    const Plane p = ref_plane(m.b, v);
                    if (!p.valid()) break;
                    const MateError e = measure(m, v);
                    const f64 push = (mover == a ? -e.linear : e.linear) * share;
                    mover->placement = mover->placement.then(Placement::translation(
                        p.nx * push, p.ny * push, p.nz * push));
                    break;
                }

                case MateKind::Tangent: {
                    const Plane p = ref_plane(m.a, v);
                    f64 bx, by, bz, bdx, bdy, bdz;
                    if (!p.valid() || !ref_axis(m.b, v, bx, by, bz, bdx, bdy, bdz))
                        break;
                    const f64 signed_gap = p.distance(bx, by, bz);
                    const f64 want = signed_gap >= 0.0 ? m.value : -m.value;
                    const f64 delta = want - signed_gap;
                    const f64 push = (mover == a ? -delta : delta) * share;
                    mover->placement = mover->placement.then(Placement::translation(
                        p.nx * push, p.ny * push, p.nz * push));
                    break;
                }

                default:
                    break;
            }
        }

        const f64 after = total_error();
        if (after > before * 0.999999 && after > tolerance && iter > 8) {
            r.status = MateStatus::DidNotConverge;
            r.residual = after;
            r.iterations = iter;
            for (std::size_t mi = 0; mi < mates_.size(); ++mi)
                if (!measure(mates_[mi], v).satisfied)
                    r.conflicting.push_back(static_cast<u32>(mi));
            return r;
        }
    }

    r.residual = total_error();
    r.iterations = max_iterations;
    r.status = r.residual < tolerance ? MateStatus::Solved
                                      : MateStatus::DidNotConverge;
    if (r.status == MateStatus::DidNotConverge)
        for (std::size_t mi = 0; mi < mates_.size(); ++mi)
            if (!measure(mates_[mi], v).satisfied)
                r.conflicting.push_back(static_cast<u32>(mi));
    return r;
}

namespace {

struct Box { f64 lo[3], hi[3]; };

bool placed_bounds(const Instance& i, const Volume& v, Box& out) {
    if (!i.body || i.body->vertex_count() == 0) return false;
    bool first = true;
    for (u32 k = 0; k < i.body->vertex_count(); ++k) {
        f64 x, y, z;
        gcm::to_world(v, i.body->vertex_point[k], x, y, z);
        f64 wx, wy, wz;
        i.placement.apply(x, y, z, wx, wy, wz);
        const f64 p[3] = {wx, wy, wz};
        for (int a = 0; a < 3; ++a) {
            if (first) {
                out.lo[a] = out.hi[a] = p[a];
            } else {
                out.lo[a] = std::min(out.lo[a], p[a]);
                out.hi[a] = std::max(out.hi[a], p[a]);
            }
        }
        first = false;
    }
    return true;
}

Body placed_copy(const Instance& i, const Volume& v) {
    Body b = *i.body;
    for (auto& p : b.vertex_point) {
        f64 x, y, z;
        gcm::to_world(v, p, x, y, z);
        f64 wx, wy, wz;
        i.placement.apply(x, y, z, wx, wy, wz);
        p = gcm::to_lattice(v, wx, wy, wz);
    }
    return b;
}

}

std::vector<Assembly::Interference> Assembly::interference(const Volume& v) const {
    std::vector<Interference> out;

    for (u32 i = 0; i < instances_.size(); ++i) {
        Box bi;
        if (!placed_bounds(instances_[i], v, bi)) continue;

        for (u32 j = i + 1; j < instances_.size(); ++j) {
            Box bj;
            if (!placed_bounds(instances_[j], v, bj)) continue;

            bool apart = false;
            for (int a = 0; a < 3; ++a)
                if (bi.hi[a] < bj.lo[a] - 1e-12 || bj.hi[a] < bi.lo[a] - 1e-12)
                    apart = true;
            if (apart) continue;

            const Body pa = placed_copy(instances_[i], v);
            const Body pb = placed_copy(instances_[j], v);
            if (!all_faces_planar(pa) || !all_faces_planar(pb)) continue;

            const BoolResult r = intersect(pa, pb, v);
            if (!r.ok()) continue;

            Interference hit;
            hit.a = i;
            hit.b = j;
            hit.volume = volume(r.body, v);
            hit.touching = hit.volume <= 1e-12;
            if (hit.volume > 1e-12 || hit.touching) out.push_back(hit);
        }
    }
    return out;
}


int Pattern::total() const noexcept {
    switch (kind) {
        case PatternKind::Linear:
            return std::max(1, count1) * std::max(1, count2);
        case PatternKind::Circular:
            return std::max(1, count1);
        case PatternKind::Mirror:
            return 2;
        case PatternKind::Table:
            return static_cast<int>(placements.size());
    }
    return 0;
}

bool Pattern::is_skipped(int index) const noexcept {
    for (int s : skipped)
        if (s == index) return true;
    return false;
}

std::vector<Placement> expand_pattern(const Pattern& p, const Placement& seed) {
    std::vector<Placement> out;

    switch (p.kind) {
        case PatternKind::Linear: {
            const int n1 = std::max(1, p.count1), n2 = std::max(1, p.count2);
            for (int j = 0; j < n2; ++j)
                for (int i = 0; i < n1; ++i) {
                    const int index = j * n1 + i;
                    if (p.is_skipped(index)) continue;
                    out.push_back(seed.then(Placement::translation(
                        p.dx1 * i + p.dx2 * j, p.dy1 * i + p.dy2 * j,
                        p.dz1 * i + p.dz2 * j)));
                }
            break;
        }

        case PatternKind::Circular: {
            const int n = std::max(1, p.count1);
            const bool full = std::fabs(std::fabs(p.total_angle) -
                                        6.28318530717958647692) < 1e-9;
            const f64 step = p.total_angle / (full ? n : std::max(1, n - 1));

            for (int i = 0; i < n; ++i) {
                if (p.is_skipped(i)) continue;
                const Placement to_origin =
                    Placement::translation(-p.ox, -p.oy, -p.oz);
                const Placement spin =
                    Placement::rotation(p.ax, p.ay, p.az, step * i);
                const Placement back = Placement::translation(p.ox, p.oy, p.oz);

                if (p.rotate_instances) {
                    out.push_back(seed.then(to_origin).then(spin).then(back));
                } else {
                    f64 x, y, z;
                    seed.apply(0, 0, 0, x, y, z);
                    f64 px, py, pz;
                    to_origin.then(spin).then(back).apply(x, y, z, px, py, pz);
                    Placement moved = seed;
                    moved.t[0] += px - x;
                    moved.t[1] += py - y;
                    moved.t[2] += pz - z;
                    out.push_back(moved);
                }
            }
            break;
        }

        case PatternKind::Mirror: {
            if (!p.is_skipped(0)) out.push_back(seed);
            if (p.is_skipped(1)) break;

            // Reflection: x - 2(n.x - d)n.
            const Plane& m = p.mirror_plane;
            Placement r;
            r.m[0] = 1 - 2 * m.nx * m.nx;
            r.m[1] = -2 * m.nx * m.ny;
            r.m[2] = -2 * m.nx * m.nz;
            r.m[3] = -2 * m.ny * m.nx;
            r.m[4] = 1 - 2 * m.ny * m.ny;
            r.m[5] = -2 * m.ny * m.nz;
            r.m[6] = -2 * m.nz * m.nx;
            r.m[7] = -2 * m.nz * m.ny;
            r.m[8] = 1 - 2 * m.nz * m.nz;
            r.t[0] = 2 * m.d * m.nx;
            r.t[1] = 2 * m.d * m.ny;
            r.t[2] = 2 * m.d * m.nz;
            out.push_back(seed.then(r));
            break;
        }

        case PatternKind::Table:
            for (std::size_t i = 0; i < p.placements.size(); ++i) {
                if (p.is_skipped(static_cast<int>(i))) continue;
                out.push_back(seed.then(p.placements[i]));
            }
            break;
    }

    return out;
}

u32 Assembly::add_pattern(Pattern p) {
    patterns_.push_back(std::move(p));
    return static_cast<u32>(patterns_.size() - 1);
}

bool Assembly::remove_pattern(u32 index) {
    if (index >= patterns_.size()) return false;
    patterns_.erase(patterns_.begin() + static_cast<long>(index));
    return true;
}

std::size_t Assembly::patterned_instance_count() const {
    std::size_t n = 0;
    for (const Pattern& p : patterns_) {
        const int total = p.total();
        for (int i = 0; i < total; ++i)
            if (!p.is_skipped(i)) ++n;
    }
    return n;
}

std::size_t Assembly::expand_patterns() {
    std::size_t made = 0;

    // Snapshot the count so new instances don't become seeds.
    const std::size_t seeds = instances_.size();
    for (const Pattern& p : patterns_) {
        if (p.seed >= seeds) continue;

        // By value: pushing into instances_ below reallocates.
        const Instance seed = instances_[p.seed];

        const std::vector<Placement> places = expand_pattern(p, seed.placement);
        for (std::size_t i = 0; i < places.size(); ++i) {
            if (i == 0 && p.kind != PatternKind::Table) continue;
            Instance copy = seed;
            copy.placement = places[i];
            copy.grounded = false;
            copy.name = seed.name + " (" + std::to_string(i) + ")";
            instances_.push_back(std::move(copy));
            ++made;
        }
    }
    return made;
}

u32 Assembly::add_subassembly(const Assembly* sub, Placement at,
                              std::string name) {
    Instance i;
    i.sub = sub;
    i.placement = at;
    i.name = std::move(name);
    return add(std::move(i));
}

void Assembly::flatten_into(std::vector<FlatPart>& out, const Placement& parent,
                            const std::string& prefix, u32 depth) const {
    if (depth > 32) return;

    for (const Instance& i : instances_) {
        const Placement world = i.placement.then(parent);
        const std::string path =
            prefix.empty() ? i.name : prefix + "/" + i.name;

        if (i.sub) {
            i.sub->flatten_into(out, world, path, depth + 1);
            continue;
        }
        if (!i.body) continue;
        out.push_back({i.body, world, path, depth});
    }
}

std::vector<Assembly::FlatPart> Assembly::flatten() const {
    std::vector<FlatPart> out;
    flatten_into(out, Placement::identity(), "", 0);
    return out;
}

std::size_t Assembly::part_count() const { return flatten().size(); }

u32 Assembly::depth() const {
    u32 deepest = 0;
    for (const FlatPart& p : flatten()) deepest = std::max(deepest, p.depth);
    return deepest;
}

namespace d2mate {

void Placement2::apply(f64 x, f64 y, f64& ox, f64& oy) const noexcept {
    const f64 c = std::cos(angle), s = std::sin(angle);
    ox = x * c - y * s + tx;
    oy = x * s + y * c + ty;
}

void Placement2::apply_direction(f64 x, f64 y, f64& ox, f64& oy) const noexcept {
    const f64 c = std::cos(angle), s = std::sin(angle);
    ox = x * c - y * s;
    oy = x * s + y * c;
}

bool Placement2::is_identity(f64 tol) const noexcept {
    return std::fabs(angle) <= tol && std::fabs(tx) <= tol && std::fabs(ty) <= tol;
}

u32 Assembly2::add(Part p) {
    parts_.push_back(std::move(p));
    return static_cast<u32>(parts_.size() - 1);
}

Part* Assembly2::part(u32 id) { return id < parts_.size() ? &parts_[id] : nullptr; }
const Part* Assembly2::part(u32 id) const {
    return id < parts_.size() ? &parts_[id] : nullptr;
}

u32 Assembly2::add_mate(Mate2 m) {
    mates_.push_back(std::move(m));
    return static_cast<u32>(mates_.size() - 1);
}

bool Assembly2::edge_of(const EdgeRef& r, const Volume& v, f64& x0, f64& y0,
                        f64& x1, f64& y1) const {
    const Part* p = part(r.part);
    if (!p || !r.valid() || r.shape >= p->shapes.size()) return false;
    const Edge& s = p->shapes[r.shape];

    f64 ax, ay, bx, by;
    gcm::to_world(v, s.a, ax, ay);
    gcm::to_world(v, s.b, bx, by);
    p->placement.apply(ax, ay, x0, y0);
    p->placement.apply(bx, by, x1, y1);
    return true;
}

namespace {

struct Edge2 {
    f64 x0, y0, x1, y1;
    f64 dx, dy, len;
    f64 nx, ny;
};

bool edge_geometry(const Assembly2& a, const EdgeRef& r, const Volume& v,
                   Edge2& e) {
    if (!a.edge_of(r, v, e.x0, e.y0, e.x1, e.y1)) return false;
    e.dx = e.x1 - e.x0;
    e.dy = e.y1 - e.y0;
    e.len = std::hypot(e.dx, e.dy);
    if (e.len < 1e-15) return false;
    e.dx /= e.len;
    e.dy /= e.len;
    e.nx = -e.dy;
    e.ny = e.dx;
    return true;
}

}

int Assembly2::degrees_of_freedom() const {
    int total = 0;
    for (const Part& p : parts_)
        if (!p.grounded) total += 3;      // x, y and one rotation
    for (const Mate2& m : mates_) {
        if (m.suppressed) continue;
        switch (m.kind) {
            case MateKind::Collinear:
            case MateKind::Coincident:  total -= 2; break;
            case MateKind::Distance:    total -= 2; break;
            case MateKind::Parallel:
            case MateKind::Perpendicular:
            case MateKind::Angle:       total -= 1; break;
            case MateKind::Fix:         total -= 3; break;
            default:                    total -= 1; break;
        }
    }
    return total;
}

MateResult Assembly2::solve(const Volume& v, int max_iterations, f64 tolerance) {
    MateResult r;
    if (mates_.empty() || parts_.size() < 2) return r;

    auto error_of = [&](const Mate2& m) {
        Edge2 ea, eb;
        MateError e;
        if (!edge_geometry(*this, m.a, v, ea) || !edge_geometry(*this, m.b, v, eb))
            return e;

        const f64 dot = ea.dx * eb.dx + ea.dy * eb.dy;
        switch (m.kind) {
            case MateKind::Collinear:
            case MateKind::Coincident:
            case MateKind::Distance: {
                e.angular = std::acos(std::clamp(std::fabs(dot), -1.0, 1.0));
                const f64 off = (eb.x0 - ea.x0) * ea.nx + (eb.y0 - ea.y0) * ea.ny;
                e.linear = m.kind == MateKind::Distance ? std::fabs(off) - m.value
                                                        : off;
                break;
            }
            case MateKind::Parallel:
                e.angular = std::acos(std::clamp(std::fabs(dot), -1.0, 1.0));
                break;
            case MateKind::Perpendicular:
                e.angular = std::fabs(dot);
                break;
            case MateKind::Angle: {
                const f64 got = std::atan2(eb.dy, eb.dx) - std::atan2(ea.dy, ea.dx);
                e.angular = std::fabs(
                    std::remainder(got - m.value, 6.28318530717958647692));
                break;
            }
            default:
                break;
        }
        e.satisfied = std::fabs(e.linear) < 1e-9 && std::fabs(e.angular) < 1e-9;
        return e;
    };

    auto total = [&]() {
        f64 sum = 0.0;
        for (const Mate2& m : mates_) {
            if (m.suppressed) continue;
            const MateError e = error_of(m);
            sum += e.linear * e.linear + e.angular * e.angular;
        }
        return std::sqrt(sum);
    };

    if (total() < tolerance) {
        r.status = MateStatus::AlreadySatisfied;
        return r;
    }

    for (int iter = 0; iter < max_iterations; ++iter) {
        const f64 before = total();
        if (before < tolerance) {
            r.status = MateStatus::Solved;
            r.iterations = iter;
            r.residual = before;
            return r;
        }

        for (const Mate2& m : mates_) {
            if (m.suppressed || m.kind == MateKind::Fix) continue;
            if (!m.a.valid() || !m.b.valid() || m.a.part == m.b.part) continue;
            if (m.a.part >= parts_.size() || m.b.part >= parts_.size()) continue;

            EdgeRef anchor = m.a, mover_ref = m.b;
            if (parts_[m.b.part].grounded) {
                if (parts_[m.a.part].grounded) continue;
                anchor = m.b;
                mover_ref = m.a;
            }
            Part& mover = parts_[mover_ref.part];

            Edge2 fixed_e, moving_e;
            if (!edge_geometry(*this, anchor, v, fixed_e)) continue;
            if (!edge_geometry(*this, mover_ref, v, moving_e)) continue;

            f64 want = std::atan2(fixed_e.dy, fixed_e.dx);
            if (m.kind == MateKind::Perpendicular) want += 1.5707963267948966;
            else if (m.kind == MateKind::Angle) want += m.value;
            const f64 have = std::atan2(moving_e.dy, moving_e.dx);
            f64 delta = std::remainder(want - have, 6.28318530717958647692);
            if (m.kind != MateKind::Angle && std::fabs(delta) > 1.5707963267948966)
                delta = std::remainder(delta + 3.14159265358979323846,
                                       6.28318530717958647692);

            if (std::fabs(delta) > 1e-15) {
                const f64 px = moving_e.x0, py = moving_e.y0;
                const f64 c = std::cos(delta), s = std::sin(delta);
                const f64 ox = mover.placement.tx - px;
                const f64 oy = mover.placement.ty - py;
                mover.placement.angle += delta;
                mover.placement.tx = px + ox * c - oy * s;
                mover.placement.ty = py + ox * s + oy * c;
            }

            if (m.kind == MateKind::Parallel || m.kind == MateKind::Perpendicular ||
                m.kind == MateKind::Angle)
                continue;

            if (!edge_geometry(*this, anchor, v, fixed_e)) continue;
            if (!edge_geometry(*this, mover_ref, v, moving_e)) continue;
            const f64 off = (moving_e.x0 - fixed_e.x0) * fixed_e.nx +
                            (moving_e.y0 - fixed_e.y0) * fixed_e.ny;
            const f64 want_off =
                m.kind == MateKind::Distance ? (off >= 0.0 ? m.value : -m.value) : 0.0;
            const f64 push = want_off - off;
            mover.placement.tx += fixed_e.nx * push;
            mover.placement.ty += fixed_e.ny * push;
        }

        const f64 after = total();
        if (after > before * 0.999999 && after > tolerance && iter > 8) {
            r.status = MateStatus::DidNotConverge;
            r.residual = after;
            r.iterations = iter;
            return r;
        }
    }

    r.residual = total();
    r.iterations = max_iterations;
    r.status = r.residual < tolerance ? MateStatus::Solved
                                      : MateStatus::DidNotConverge;
    return r;
}

}
}
