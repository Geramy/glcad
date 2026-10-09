#include "geometry/live.hpp"

#include <algorithm>
#include <cmath>

#include "geometry/measure.hpp"

namespace gtopo {

namespace {

f64 angle_between(const Plane& a, const Plane& b) {
    const f64 d = std::clamp(a.nx * b.nx + a.ny * b.ny + a.nz * b.nz, -1.0, 1.0);
    return std::acos(d);
}

std::vector<FaceId> face_neighbours(const Body& b, FaceId f) {
    std::vector<FaceId> out;
    for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li)
        for (u32 h = b.loop_begin(li); h < b.loop_end(li); ++h) {
            const u32 twin = b.he_twin[h];
            if (twin == kInvalid || twin >= b.halfedge_count()) continue;
            const FaceId other = b.loop_face[b.he_loop[twin]];
            if (other != f &&
                std::find(out.begin(), out.end(), other) == out.end())
                out.push_back(other);
        }
    return out;
}

}

const char* rule_kind_name(RuleKind k) noexcept {
    switch (k) {
        case RuleKind::Coplanar:      return "coplanar";
        case RuleKind::Concentric:    return "concentric";
        case RuleKind::Parallel:      return "parallel";
        case RuleKind::Perpendicular: return "perpendicular";
        case RuleKind::Tangent:       return "tangent";
        case RuleKind::Symmetric:     return "symmetric";
        case RuleKind::EqualRadius:   return "equal radius";
        case RuleKind::Coaxial:       return "coaxial";
    }
    return "?";
}

bool LiveRule::involves(FaceId f) const noexcept {
    return std::find(faces.begin(), faces.end(), f) != faces.end();
}

void LiveRules::discover(const Body& b, const Volume& v,
                         const LiveRuleSettings& s) {
    rules_.clear();
    const u32 n = b.face_count();
    if (n == 0) return;

    std::vector<Plane> planes(n);
    for (FaceId f = 0; f < n; ++f) planes[f] = face_plane(b, f, v);

    auto add = [&](RuleKind k, FaceId a, FaceId c, f64 tol) {
        if (rules_.size() >= s.max_rules) return;
        LiveRule r;
        r.kind = k;
        r.faces = {a, c};
        r.tolerance = tol;
        rules_.push_back(std::move(r));
    };

    for (FaceId a = 0; a < n; ++a) {
        if (!planes[a].valid()) continue;
        for (FaceId c = a + 1; c < n; ++c) {
            if (!planes[c].valid()) continue;

            const f64 ang = angle_between(planes[a], planes[c]);
            const bool same_way = ang < s.angle_tolerance;
            const bool opposed = std::fabs(ang - 3.14159265358979323846) <
                                 s.angle_tolerance;

            if (s.coplanar && same_way &&
                std::fabs(planes[a].d - planes[c].d) < s.distance_tolerance) {
                add(RuleKind::Coplanar, a, c,
                    std::fabs(planes[a].d - planes[c].d));
                continue;
            }

            if (s.parallel && (same_way || opposed)) {
                if (s.symmetric && opposed) {
                    LiveRule r;
                    r.kind = RuleKind::Symmetric;
                    r.faces = {a, c};
                    r.tolerance =
                        std::fabs(ang - 3.14159265358979323846);
                    r.plane.nx = planes[a].nx;
                    r.plane.ny = planes[a].ny;
                    r.plane.nz = planes[a].nz;
                    r.plane.d = (planes[a].d - planes[c].d) * 0.5;
                    if (rules_.size() < s.max_rules) rules_.push_back(std::move(r));
                }
                add(RuleKind::Parallel, a, c,
                    opposed ? std::fabs(ang - 3.14159265358979323846) : ang);
                continue;
            }

            if (s.perpendicular &&
                std::fabs(ang - 1.5707963267948966) < s.angle_tolerance)
                add(RuleKind::Perpendicular, a, c, std::fabs(ang - 1.5707963267948966));
        }
    }

    if (b.has_surfaces()) {
        for (FaceId a = 0; a < n; ++a) {
            const Surface* sa = b.surface_of(a);
            if (!sa || sa->kind == SurfaceKind::Plane) continue;
            for (FaceId c = a + 1; c < n; ++c) {
                const Surface* sc = b.surface_of(c);
                if (!sc || sc->kind != sa->kind) continue;

                const f64 dr = std::fabs(sa->radius - sc->radius);
                if (s.equal_radius && dr < s.distance_tolerance)
                    add(RuleKind::EqualRadius, a, c, dr);

                const f64 axis_dot =
                    std::fabs(sa->ax * sc->ax + sa->ay * sc->ay + sa->az * sc->az);
                if (std::fabs(axis_dot - 1.0) > s.angle_tolerance) continue;

                const f64 vx = sc->ox - sa->ox, vy = sc->oy - sa->oy,
                          vz = sc->oz - sa->oz;
                const f64 along = vx * sa->ax + vy * sa->ay + vz * sa->az;
                const f64 off = std::sqrt(std::pow(vx - sa->ax * along, 2) +
                                          std::pow(vy - sa->ay * along, 2) +
                                          std::pow(vz - sa->az * along, 2));
                if (off < s.distance_tolerance) {
                    if (s.concentric) add(RuleKind::Concentric, a, c, off);
                    else if (s.coplanar) add(RuleKind::Coaxial, a, c, off);
                }
            }
        }
    }

    if (s.tangent) {
        for (FaceId f = 0; f < n; ++f) {
            if (!planes[f].valid()) continue;
            for (FaceId g : face_neighbours(b, f)) {
                if (g <= f || !planes[g].valid()) continue;
                const f64 ang = angle_between(planes[f], planes[g]);
                if (ang < s.angle_tolerance * 5.0 && ang > 1e-12)
                    add(RuleKind::Tangent, f, g, ang);
            }
        }
    }
}

void LiveRules::set_enabled(RuleKind k, bool on) {
    for (LiveRule& r : rules_)
        if (r.kind == k) r.enabled = on;
}

void LiveRules::set_enabled(std::size_t index, bool on) {
    if (index < rules_.size()) rules_[index].enabled = on;
}

std::vector<std::size_t> LiveRules::rules_for(FaceId face) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < rules_.size(); ++i)
        if (rules_[i].involves(face)) out.push_back(i);
    return out;
}

std::vector<LiveRules::Companion> LiveRules::companions(FaceId face) const {
    std::vector<Companion> out;
    if (suspended_) return out;

    for (const LiveRule& r : rules_) {
        if (!r.enabled || !r.involves(face)) continue;
        for (FaceId f : r.faces) {
            if (f == face) continue;
            if (std::find_if(out.begin(), out.end(), [&](const Companion& c) {
                    return c.face == f;
                }) != out.end())
                continue;

            Companion c;
            c.face = f;
            c.because = r.kind;
            c.factor = r.kind == RuleKind::Symmetric ? -1.0 : 1.0;
            out.push_back(c);
        }
    }
    return out;
}

const char* select_scope_name(SelectScope s) noexcept {
    switch (s) {
        case SelectScope::Face:       return "face";
        case SelectScope::Feature:    return "feature";
        case SelectScope::Tangent:    return "tangent faces";
        case SelectScope::Coplanar:   return "coplanar faces";
        case SelectScope::SameRadius: return "same radius";
        case SelectScope::Body:       return "body";
        case SelectScope::Loop:       return "loop";
    }
    return "?";
}

void SelectManager::set_body(const Body* b, const Volume* v) {
    body_ = b;
    volume_ = v;
}

SelectScope SelectManager::next_scope(SelectScope current) const {
    switch (current) {
        case SelectScope::Face:    return SelectScope::Feature;
        case SelectScope::Feature: return SelectScope::Tangent;
        case SelectScope::Tangent: return SelectScope::Body;
        default:                   return SelectScope::Face;
    }
}

SelectionResult SelectManager::select(FaceId picked, SelectScope scope) const {
    SelectionResult out;
    out.scope = scope;
    if (!body_ || !volume_ || picked >= body_->face_count()) {
        out.description = "nothing under the cursor";
        return out;
    }

    const Body& b = *body_;
    const Volume& v = *volume_;

    switch (scope) {
        case SelectScope::Face:
            out.faces = {picked};
            break;

        case SelectScope::Body:
            for (FaceId f = 0; f < b.face_count(); ++f) out.faces.push_back(f);
            break;

        case SelectScope::Feature: {
            if (features_) {
                for (const Feature& f : features_->features) {
                    const bool owns =
                        std::find(f.primary_faces.begin(), f.primary_faces.end(),
                                  picked) != f.primary_faces.end();
                    if (!owns) continue;
                    out.faces = f.faces;
                    out.description = feature_kind_name(f.kind);
                    break;
                }
            }
            if (!out.faces.empty()) break;
            out.scope = SelectScope::Tangent;
            [[fallthrough]];
        }

        case SelectScope::Tangent: {
            const auto groups = connected_face_groups(b, v, tangent_tolerance_);
            for (const auto& g : groups)
                if (std::find(g.begin(), g.end(), picked) != g.end()) {
                    out.faces = g;
                    break;
                }
            break;
        }

        case SelectScope::Coplanar: {
            const Plane p = face_plane(b, picked, v);
            if (!p.valid()) break;
            for (FaceId f = 0; f < b.face_count(); ++f) {
                const Plane q = face_plane(b, f, v);
                if (!q.valid()) continue;
                if (angle_between(p, q) < 1e-6 && std::fabs(p.d - q.d) < 1e-9)
                    out.faces.push_back(f);
            }
            break;
        }

        case SelectScope::SameRadius: {
            const Surface* s = b.surface_of(picked);
            if (!s || s->kind == SurfaceKind::Plane) break;
            for (FaceId f = 0; f < b.face_count(); ++f) {
                const Surface* t = b.surface_of(f);
                if (!t || t->kind != s->kind) continue;
                if (std::fabs(t->radius - s->radius) < radius_tolerance_)
                    out.faces.push_back(f);
            }
            break;
        }

        case SelectScope::Loop: {
            out.faces = {picked};
            for (FaceId f : face_neighbours(b, picked)) out.faces.push_back(f);
            break;
        }
    }

    if (out.faces.empty()) {
        out.description = "nothing matched";
        return out;
    }

    if (out.description.empty()) out.description = select_scope_name(out.scope);
    out.description += " (" + std::to_string(out.faces.size()) + " faces)";
    out.ok = true;
    return out;
}

}
