#include "geometry/pmi.hpp"

#include <algorithm>
#include <cmath>

#include "geometry/boolean.hpp"

namespace gtopo {
namespace {

constexpr f64 kEps = 1e-9;

// Quantise to the lattice before hashing so rebuilds hash identically.
void mix(u64& h, f64 value, f64 scale) {
    const long long q = std::llround(value * scale);
    h ^= static_cast<u64>(q) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}
void mix_u(u64& h, u64 value) {
    h ^= value + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}

}

const char* pmi_kind_name(PmiKind k) noexcept {
    switch (k) {
        case PmiKind::Distance:  return "Distance";
        case PmiKind::Diameter:  return "Diameter";
        case PmiKind::Radius:    return "Radius";
        case PmiKind::Angle:     return "Angle";
        case PmiKind::Thickness: return "Thickness";
    }
    return "";
}

u64 fingerprint_of(const Body& b, PmiTarget t, u32 index, const Volume& v) {
    u64 h = 1469598103934665603ull;
    mix_u(h, static_cast<u64>(t));

    if (t == PmiTarget::Vertex) {
        if (index >= b.vertex_count()) return 0;
        const gcm::Lattice3 p = b.vertex_point[index];
        mix_u(h, static_cast<u64>(p.x));
        mix_u(h, static_cast<u64>(p.y));
        mix_u(h, static_cast<u64>(p.z));
        return h;
    }

    if (t == PmiTarget::Edge) {
        if (index >= b.halfedge_count()) return 0;
        const u32 loop = b.he_loop[index];
        const u32 lb = b.loop_begin(loop), le = b.loop_end(loop);
        const u32 nx = (index + 1 < le) ? index + 1 : lb;
        const gcm::Lattice3 p0 = b.vertex_point[b.he_vertex[index]];
        const gcm::Lattice3 p1 = b.vertex_point[b.he_vertex[nx]];
        u64 a = 1ull, c = 1ull;
        mix_u(a, static_cast<u64>(p0.x)); mix_u(a, static_cast<u64>(p0.y));
        mix_u(a, static_cast<u64>(p0.z));
        mix_u(c, static_cast<u64>(p1.x)); mix_u(c, static_cast<u64>(p1.y));
        mix_u(c, static_cast<u64>(p1.z));
        mix_u(h, a ^ c);
        mix_u(h, a * c);
        return h;
    }

    if (index >= b.face_count()) return 0;

    if (const Surface* s = b.surface_of(index); s && s->kind != SurfaceKind::Plane) {
        mix_u(h, static_cast<u64>(s->kind));
        mix(h, s->ox, v.scale()); mix(h, s->oy, v.scale()); mix(h, s->oz, v.scale());
        mix(h, s->ax, 1e6); mix(h, s->ay, 1e6); mix(h, s->az, 1e6);
        mix(h, s->radius, v.scale());
    } else {
        const Plane p = face_plane(b, index, v);
        mix(h, p.nx, 1e6);
        mix(h, p.ny, 1e6);
        mix(h, p.nz, 1e6);
        mix(h, p.d, v.scale());
    }
    mix_u(h, b.face_loop_count(index));
    return h;
}

PmiRef make_ref(const Body& b, PmiTarget t, u32 index, const Volume& v) {
    PmiRef r;
    r.target = t;
    r.index = index;
    r.fingerprint = fingerprint_of(b, t, index, v);
    return r;
}

u32 resolve(const PmiRef& r, const Body& b, const Volume& v) {
    if (r.fingerprint == 0) return kInvalid;

    if (fingerprint_of(b, r.target, r.index, v) == r.fingerprint) return r.index;

    const u32 n = r.target == PmiTarget::Face     ? b.face_count()
                  : r.target == PmiTarget::Edge   ? b.halfedge_count()
                                                  : b.vertex_count();
    u32 found = kInvalid;
    for (u32 i = 0; i < n; ++i) {
        if (fingerprint_of(b, r.target, i, v) != r.fingerprint) continue;
        if (found != kInvalid) return kInvalid;
        found = i;
    }
    return found;
}

namespace {

bool plane_pair(const Body& b, const PmiDimension& d, const Volume& v,
                Plane& pa, Plane& pb, std::string& reason) {
    const u32 fa = resolve(d.a, b, v);
    const u32 fb = resolve(d.b, b, v);
    if (fa == kInvalid || fb == kInvalid) {
        reason = "the dimension has lost the face it measured";
        return false;
    }
    pa = face_plane(b, fa, v);
    pb = face_plane(b, fb, v);
    if (!pa.valid() || !pb.valid()) {
        reason = "a referenced face has no plane";
        return false;
    }
    return true;
}

}

PmiMeasurement measure(const Body& b, const PmiDimension& d, const Volume& v) {
    PmiMeasurement out;

    switch (d.kind) {
        case PmiKind::Distance:
        case PmiKind::Thickness: {
            Plane pa, pb;
            if (!plane_pair(b, d, v, pa, pb, out.reason)) return out;
            const f64 align = pa.nx * pb.nx + pa.ny * pb.ny + pa.nz * pb.nz;
            if (std::fabs(std::fabs(align) - 1.0) > 1e-6) {
                out.reason = "the faces are not parallel";
                return out;
            }
            // Opposed normals: flip first so the thickness comes out positive.
            const Plane q = align < 0.0 ? pb.flipped() : pb;
            out.value = std::fabs(q.d - pa.d);
            break;
        }

        case PmiKind::Angle: {
            Plane pa, pb;
            if (!plane_pair(b, d, v, pa, pb, out.reason)) return out;
            f64 c = pa.nx * pb.nx + pa.ny * pb.ny + pa.nz * pb.nz;
            c = std::clamp(c, -1.0, 1.0);
            out.value = 180.0 - std::acos(c) * 180.0 / 3.14159265358979323846;
            break;
        }

        case PmiKind::Diameter:
        case PmiKind::Radius: {
            const u32 f = resolve(d.a, b, v);
            if (f == kInvalid) {
                out.reason = "the dimension has lost the face it measured";
                return out;
            }
            const Surface* s = b.surface_of(f);
            if (!s || (s->kind != SurfaceKind::Cylinder &&
                       s->kind != SurfaceKind::Sphere)) {
                out.reason = "a diameter needs a round face";
                return out;
            }
            out.value = d.kind == PmiKind::Diameter ? s->radius * 2.0 : s->radius;
            break;
        }
    }

    out.ok = true;
    if (!d.tolerance.none()) {
        out.in_tolerance = d.tolerance.contains(d.nominal, out.value);
    } else if (d.nominal != 0.0) {
        // Lattice-relative slack: an absolute epsilon fails exact dimensions.
        const f64 slack = v.scale() > 0.0 ? 4.0 / v.scale() : 1e-9;
        out.in_tolerance = std::fabs(out.value - d.nominal) <= slack;
    } else {
        out.in_tolerance = true;
    }
    return out;
}

DriveResult drive(const Body& b, const PmiDimension& d, f64 value,
                  const Volume& v) {
    DriveResult out;
    out.body = b;
    out.volume_before = volume(b, v);
    out.volume_after = out.volume_before;

    if (!d.driving) {
        out.reason = "this dimension is a reference, not a driving one";
        return out;
    }

    const PmiMeasurement now = measure(b, d, v);
    if (!now.ok) {
        out.reason = now.reason;
        return out;
    }
    out.before = now.value;

    switch (d.kind) {
        case PmiKind::Distance:
        case PmiKind::Thickness: {
            const u32 mover = resolve(d.moves_b ? d.b : d.a, b, v);
            if (mover == kInvalid) {
                out.reason = "the face this dimension drives is gone";
                return out;
            }
            const f64 delta = value - now.value;
            if (std::fabs(delta) < kEps) {
                out.ok = true;
                out.after = now.value;
                out.reason = "already at that size";
                return out;
            }
            const EditResult e = offset_face(b, mover, delta, v);
            if (!e.ok) {
                out.reason = e.reason;
                return out;
            }
            out.body = e.body;
            out.volume_after = e.volume_after;
            break;
        }

        case PmiKind::Diameter:
        case PmiKind::Radius: {
            const u32 f = resolve(d.a, b, v);
            if (f == kInvalid || !b.has_surfaces() || f >= b.face_surface.size()) {
                out.reason = "the face this dimension drives is gone";
                return out;
            }
            const f64 r = d.kind == PmiKind::Diameter ? value * 0.5 : value;
            if (!(r > 0.0)) {
                out.reason = "a hole cannot have a radius of zero";
                return out;
            }
            out.body.face_surface[f].radius = r;
            break;
        }

        case PmiKind::Angle:
            out.reason = "angle dimensions do not drive yet";
            return out;
    }

    out.updated = d;
    {
        const u32 ia = resolve(d.a, b, v);
        const u32 ib = resolve(d.b, b, v);
        if (ia != kInvalid) out.updated.a = make_ref(out.body, d.a.target, ia, v);
        if (ib != kInvalid && d.kind != PmiKind::Diameter &&
            d.kind != PmiKind::Radius)
            out.updated.b = make_ref(out.body, d.b.target, ib, v);
    }

    const PmiMeasurement after = measure(out.body, out.updated, v);
    if (!after.ok) {
        out.after = now.value;
        out.reason = "the drive could not be verified: " + after.reason;
        return out;
    }
    out.after = after.value;
    if (std::fabs(out.after - value) > 1e-6) {
        out.reason = "the model did not reach the requested size";
        return out;
    }
    out.ok = true;
    out.reason = "driven";
    return out;
}

u32 PmiSet::add(PmiDimension d) {
    dims_.push_back(std::move(d));
    return static_cast<u32>(dims_.size() - 1);
}

bool PmiSet::remove(u32 i) {
    if (i >= dims_.size()) return false;
    dims_.erase(dims_.begin() + static_cast<long>(i));
    return true;
}

PmiDimension* PmiSet::at(u32 i) {
    return i < dims_.size() ? &dims_[i] : nullptr;
}
const PmiDimension* PmiSet::at(u32 i) const {
    return i < dims_.size() ? &dims_[i] : nullptr;
}

std::vector<PmiConflict> PmiSet::conflicts(const Body& b, const Volume& v) const {
    std::vector<PmiConflict> out;
    for (u32 i = 0; i < dims_.size(); ++i) {
        if (!dims_[i].driving) continue;
        const u32 mi = resolve(dims_[i].moves_b ? dims_[i].b : dims_[i].a, b, v);
        if (mi == kInvalid) continue;
        for (u32 j = i + 1; j < dims_.size(); ++j) {
            if (!dims_[j].driving) continue;
            const u32 mj = resolve(dims_[j].moves_b ? dims_[j].b : dims_[j].a, b, v);
            if (mj == mi)
                out.push_back({i, j, "both dimensions drive the same face"});
        }
    }
    return out;
}

PmiSet::Report PmiSet::inspect(const Body& b, const Volume& v) const {
    Report r;
    for (const PmiDimension& d : dims_) {
        const PmiMeasurement m = measure(b, d, v);
        if (!m.ok) ++r.broken;
        else if (!m.in_tolerance) ++r.out_of_tolerance;
        r.measurements.push_back(m);
    }
    return r;
}

std::size_t PmiSet::reattach(const Body& b, const Volume& v) {
    std::size_t fixed = 0;
    auto repair = [&](PmiRef& ref) {
        if (ref.fingerprint == 0) return;
        const u32 found = resolve(ref, b, v);
        if (found == kInvalid || found == ref.index) return;
        ref.index = found;
        ++fixed;
    };
    for (PmiDimension& d : dims_) {
        repair(d.a);
        repair(d.b);
    }
    return fixed;
}

}
