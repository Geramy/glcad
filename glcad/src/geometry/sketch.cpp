#include "geometry/sketch.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gtopo {
namespace {

constexpr f64 kEps = 1e-12;
constexpr f64 kPi = 3.14159265358979323846;

struct V3 { f64 x, y, z; };
f64 dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 norm(const V3& a) {
    const f64 n = std::sqrt(dot(a, a));
    return n > kEps ? V3{a.x / n, a.y / n, a.z / n} : V3{0, 0, 0};
}

V3 world_of(const gcm::WorkingVolume& v, const gcm::Lattice3& p) {
    f64 x, y, z;
    gcm::to_world(v, p, x, y, z);
    return {x, y, z};
}

}

void SketchPlane::to_world(f64 x, f64 y, f64& wx, f64& wy, f64& wz) const noexcept {
    wx = ox + ux * x + vx * y;
    wy = oy + uy * x + vy * y;
    wz = oz + uz * x + vz * y;
}

void SketchPlane::to_plane(f64 wx, f64 wy, f64 wz, f64& x, f64& y) const noexcept {
    const f64 dx = wx - ox, dy = wy - oy, dz = wz - oz;
    x = dx * ux + dy * uy + dz * uz;
    y = dx * vx + dy * vy + dz * vz;
}

f64 SketchPlane::distance(f64 wx, f64 wy, f64 wz) const noexcept {
    return (wx - ox) * nx + (wy - oy) * ny + (wz - oz) * nz;
}

void stable_axes(f64 nx, f64 ny, f64 nz, f64 u[3], f64 vv[3]) noexcept {
    const V3 n = norm({nx, ny, nz});
    // Least-aligned world axis, so the axes depend only on the normal.
    const f64 ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    V3 seed{0, 0, 1};
    if (ax <= ay && ax <= az) seed = {1, 0, 0};
    else if (ay <= az)        seed = {0, 1, 0};

    const V3 uu = norm(cross(seed, n));
    const V3 v = cross(n, uu);
    u[0] = uu.x; u[1] = uu.y; u[2] = uu.z;
    vv[0] = v.x; vv[1] = v.y; vv[2] = v.z;
}

SketchPlane SketchPlane::from_normal(f64 px, f64 py, f64 pz, f64 nx, f64 ny,
                                     f64 nz) noexcept {
    SketchPlane p;
    const V3 n = norm({nx, ny, nz});
    p.ox = px; p.oy = py; p.oz = pz;
    p.nx = n.x; p.ny = n.y; p.nz = n.z;
    f64 u[3], v[3];
    stable_axes(n.x, n.y, n.z, u, v);
    p.ux = u[0]; p.uy = u[1]; p.uz = u[2];
    p.vx = v[0]; p.vy = v[1]; p.vz = v[2];
    return p;
}

const char* sketch_state_name(SketchState s) noexcept {
    switch (s) {
        case SketchState::Ok:            return "attached";
        case SketchState::FaceGone:      return "the face is gone";
        case SketchState::FaceNotPlanar: return "the face is no longer flat";
        case SketchState::FaceMoved:     return "the face moved";
    }
    return "";
}

SketchPlane plane_of_face(const Body& b, u32 face, const gcm::WorkingVolume& v) {
    SketchPlane p;
    if (face >= b.face_count()) return p;
    const u32 loop = b.face_loop_begin(face);
    if (loop >= b.face_loop_end(face)) return p;
    const u32 lb = b.loop_begin(loop), le = b.loop_end(loop);
    if (le - lb < 3) return p;

    V3 n{0, 0, 0}, c{0, 0, 0};
    for (u32 h = lb; h < le; ++h) {
        const V3 a = world_of(v, b.vertex_point[b.he_vertex[h]]);
        const V3 d = world_of(v, b.vertex_point[b.he_vertex[h + 1 < le ? h + 1 : lb]]);
        n.x += (a.y - d.y) * (a.z + d.z);
        n.y += (a.z - d.z) * (a.x + d.x);
        n.z += (a.x - d.x) * (a.y + d.y);
        c.x += a.x; c.y += a.y; c.z += a.z;
    }
    const f64 count = static_cast<f64>(le - lb);
    c={c.x/count,c.y/count,c.z/count};
    // Area weights keep the origin fixed when booleans subdivide an edge.
    const V3 origin=world_of(v,b.vertex_point[b.he_vertex[lb]]);
    const V3 normal=norm(n);
    V3 weighted{0,0,0};
    f64 area=0;
    for (u32 h=lb+1;h+1<le;++h) {
        const auto a=world_of(v,b.vertex_point[b.he_vertex[h]]);
        const auto d=world_of(v,b.vertex_point[b.he_vertex[h+1]]);
        const V3 u{a.x-origin.x,a.y-origin.y,a.z-origin.z};
        const V3 w{d.x-origin.x,d.y-origin.y,d.z-origin.z};
        const f64 weight=dot(cross(u,w),normal);
        weighted.x+=(u.x+w.x)*weight;
        weighted.y+=(u.y+w.y)*weight;
        weighted.z+=(u.z+w.z)*weight;
        area+=weight;
    }
    if (std::fabs(area)>1e-30)
        c={origin.x+weighted.x/(3*area),origin.y+weighted.y/(3*area),origin.z+weighted.z/(3*area)};
    return SketchPlane::from_normal(c.x,c.y,c.z,n.x,n.y,n.z);
}

namespace {

std::vector<gsnap::Shape> project_boundary(const Body& b, u32 face,
                                           const gcm::WorkingVolume& v,
                                           const SketchPlane& p) {
    std::vector<gsnap::Shape> out;
    if (face >= b.face_count()) return out;
    for (u32 loop = b.face_loop_begin(face); loop < b.face_loop_end(face); ++loop) {
        const u32 lb = b.loop_begin(loop), le = b.loop_end(loop);
        for (u32 h = lb; h < le; ++h) {
            const u32 nx = (h + 1 < le) ? h + 1 : lb;
            const V3 a = world_of(v, b.vertex_point[b.he_vertex[h]]);
            const V3 c = world_of(v, b.vertex_point[b.he_vertex[nx]]);
            f64 ax, ay, cx, cy;
            p.to_plane(a.x, a.y, a.z, ax, ay);
            p.to_plane(c.x, c.y, c.z, cx, cy);

            gsnap::Shape s;
            s.kind = gsnap::ShapeKind::Segment;
            s.a = gcm::to_lattice(v, ax, ay);
            s.b = gcm::to_lattice(v, cx, cy);
            if (s.a.x == s.b.x && s.a.y == s.b.y) continue;
            out.push_back(s);
        }
    }
    return out;
}

}

std::optional<Sketch> sketch_on_face(const Body& b, u32 face,
                                     const gcm::WorkingVolume& v,
                                     u64 body_revision) {
    if (face >= b.face_count()) return std::nullopt;
    if (b.face_is_curved(face)) return std::nullopt;

    Sketch s;
    s.name = "Sketch on face " + std::to_string(face);
    s.support = SketchSupport::Face;
    s.face = face;
    s.body_revision = body_revision;
    s.plane = plane_of_face(b, face, v);
    s.original = s.plane;
    s.reference = project_boundary(b, face, v, s.plane);
    return s;
}

Sketch sketch_on_plane(const SketchPlane& p, std::string name) {
    Sketch s;
    s.name = std::move(name);
    s.support = SketchSupport::Datum;
    s.plane = p;
    s.original = p;
    return s;
}

SketchState check(const Sketch& s, const Body& b, const gcm::WorkingVolume& v) {
    if (!s.is_linked()) return SketchState::Ok;
    if (s.face >= b.face_count()) return SketchState::FaceGone;
    if (b.face_is_curved(s.face)) return SketchState::FaceNotPlanar;

    const SketchPlane now = plane_of_face(b, s.face, v);
    const f64 tilt = now.nx * s.original.nx + now.ny * s.original.ny +
                     now.nz * s.original.nz;
    const f64 offset = s.original.distance(now.ox, now.oy, now.oz);
    if (tilt < 1.0 - 1e-9 || std::fabs(offset) > 1e-9) return SketchState::FaceMoved;
    return SketchState::Ok;
}

bool reattach(Sketch& s, const Body& b, const gcm::WorkingVolume& v,
              u64 body_revision) {
    if (!s.is_linked()) return false;
    if (s.face >= b.face_count() || b.face_is_curved(s.face)) return false;

    s.plane = plane_of_face(b, s.face, v);
    s.original = s.plane;
    s.reference = project_boundary(b, s.face, v, s.plane);
    s.body_revision = body_revision;
    return true;
}

std::vector<std::vector<std::array<f64, 3>>> lift(const Sketch& s,
                                                  const gcm::WorkingVolume& v,
                                                  f64 chord_tolerance) {
    std::vector<std::vector<std::array<f64, 3>>> out;
    for (const gsnap::Shape& sh : s.geometry) {
        std::vector<std::array<f64, 3>> poly;
        auto emit = [&](f64 x, f64 y) {
            f64 wx, wy, wz;
            s.plane.to_world(x, y, wx, wy, wz);
            poly.push_back({wx, wy, wz});
        };

        if (sh.kind == gsnap::ShapeKind::Segment) {
            f64 ax, ay, bx, by;
            gcm::to_world(v, sh.a, ax, ay);
            gcm::to_world(v, sh.b, bx, by);
            emit(ax, ay);
            emit(bx, by);
        } else if (sh.is_curve()) {
            f64 cx, cy;
            gcm::to_world(v, sh.a, cx, cy);
            const f64 r = f64(sh.radius) / v.scale();
            if (r <= 0.0) continue;

            f64 a0 = sh.start_angle, a1 = sh.end_angle;
            if (sh.kind == gsnap::ShapeKind::Circle) { a0 = 0.0; a1 = 2.0 * kPi; }
            f64 sweep = a1 - a0;
            while (sweep < 0.0) sweep += 2.0 * kPi;

            const f64 t = std::clamp(chord_tolerance, 1e-9, r * 0.5);
            const f64 step = 2.0 * std::acos(1.0 - t / r);
            const int n = std::max(4, static_cast<int>(std::ceil(sweep / step)));
            for (int i = 0; i <= n; ++i) {
                const f64 a = a0 + sweep * (static_cast<f64>(i) / n);
                emit(cx + r * std::cos(a), cy + r * std::sin(a));
            }
        } else {
            continue;
        }
        if (poly.size() >= 2) out.push_back(std::move(poly));
    }
    return out;
}

SketchSystem to_system(const Sketch& s, const gcm::WorkingVolume& v) {
    SketchSystem out;
    for (u32 i = 0; i < s.geometry.size(); ++i) {
        const gsnap::Shape& sh = s.geometry[i];
        SketchSystem::Mapping m;
        m.shape = i;

        if (sh.kind == gsnap::ShapeKind::Segment) {
            f64 ax, ay, bx, by;
            gcm::to_world(v, sh.a, ax, ay);
            gcm::to_world(v, sh.b, bx, by);
            m.a = out.system.add_point(ax, ay);
            m.b = out.system.add_point(bx, by);
            m.entity = out.system.add_line(m.a, m.b);
        } else if (sh.kind == gsnap::ShapeKind::Circle) {
            f64 cx, cy;
            gcm::to_world(v, sh.a, cx, cy);
            m.a = out.system.add_point(cx, cy);
            m.entity = out.system.add_circle(m.a, f64(sh.radius) / v.scale());
        } else if (sh.kind == gsnap::ShapeKind::Arc) {
            f64 cx, cy;
            gcm::to_world(v, sh.a, cx, cy);
            m.a = out.system.add_point(cx, cy);
            m.entity = out.system.add_arc(m.a, f64(sh.radius) / v.scale(),
                                          sh.start_angle, sh.end_angle);
        } else {
            continue;
        }
        out.mapping.push_back(m);
    }
    return out;
}

void write_back(Sketch& s, const SketchSystem& sys, const gcm::WorkingVolume& v) {
    for (const auto& m : sys.mapping) {
        if (m.shape >= s.geometry.size()) continue;
        gsnap::Shape& sh = s.geometry[m.shape];

        if (sh.kind == gsnap::ShapeKind::Segment) {
            f64 ax, ay, bx, by;
            sys.system.point_at(m.a, ax, ay);
            sys.system.point_at(m.b, bx, by);
            sh.a = gcm::to_lattice(v, ax, ay);
            sh.b = gcm::to_lattice(v, bx, by);
        } else if (sh.is_curve()) {
            f64 cx, cy;
            sys.system.point_at(m.a, cx, cy);
            sh.a = gcm::to_lattice(v, cx, cy);
            const f64 r = sys.system.radius_of(m.entity);
            if (r > 0.0)
                sh.radius = static_cast<gcm::i64>(std::llround(r * v.scale()));
        }
    }
}

const char* badge_name(DofBadge b) noexcept {
    switch (b) {
        case DofBadge::Empty:            return "Empty";
        case DofBadge::UnderConstrained: return "Under-constrained";
        case DofBadge::FullyConstrained: return "Fully constrained";
        case DofBadge::OverConstrained:  return "Over-constrained";
    }
    return "";
}

BadgeState badge_of(const gsolve::System& s) {
    BadgeState out;
    out.dof = s.analyse();

    if (s.entity_count() == 0) {
        out.badge = DofBadge::Empty;
        out.text = "Empty sketch";
        return out;
    }

    if (out.dof.over_constrained()) {
        out.badge = DofBadge::OverConstrained;
        out.text = out.dof.redundant == 1
                       ? "1 redundant relation"
                       : std::to_string(out.dof.redundant) +
                             " redundant relations";
        for (u32 i = 0; i < s.constraint_count(); ++i) out.conflicting.push_back(i);
        return out;
    }
    if (out.dof.fully_constrained()) {
        out.badge = DofBadge::FullyConstrained;
        out.text = "Fully constrained";
        return out;
    }

    out.badge = DofBadge::UnderConstrained;
    out.text = std::to_string(out.dof.remaining) +
               (out.dof.remaining == 1 ? " dimension or relation required"
                                       : " dimensions or relations required");
    return out;
}

BadgeState auto_constrain(Sketch& s, const gcm::WorkingVolume& v,
                          const gsolve::InferenceSettings& in) {
    SketchSystem sys = to_system(s, v);

    if (!sys.mapping.empty() && sys.mapping.front().a != gsolve::kInvalid)
        sys.system.fix(sys.mapping.front().a, true);

    gsolve::auto_constrain(sys.system, in);
    const gsolve::SolveResult r = sys.system.solve();
    if (r.ok()) write_back(s, sys, v);
    return badge_of(sys.system);
}

}
