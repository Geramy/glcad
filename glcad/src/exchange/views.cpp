#include "exchange/views.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gdraft {
namespace {

constexpr f64 kEps = 1e-12;

struct V3 { f64 x, y, z; };

V3 sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
f64 dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 normalise(const V3& a) {
    const f64 n = std::sqrt(dot(a, a));
    return n > kEps ? V3{a.x / n, a.y / n, a.z / n} : V3{0, 0, 0};
}

V3 world_of(const gcm::WorkingVolume& v, const gcm::Lattice3& p) {
    f64 x, y, z;
    gcm::to_world(v, p, x, y, z);
    return {x, y, z};
}

}

const char* projection_angle_name(ProjectionAngle a) noexcept {
    return a == ProjectionAngle::First ? "First angle" : "Third angle";
}

const char* view_kind_name(ViewKind k) noexcept {
    switch (k) {
        case ViewKind::Base:      return "Base";
        case ViewKind::Projected: return "Projected";
        case ViewKind::Isometric: return "Isometric";
        case ViewKind::Section:   return "Section";
        case ViewKind::Detail:    return "Detail";
    }
    return "";
}

const char* layout_name(Layout l) noexcept {
    switch (l) {
        case Layout::OneView:   return "One view";
        case Layout::TwoView:   return "Two view";
        case Layout::ThreeView: return "Three view";
        case Layout::FourView:  return "Four view";
    }
    return "";
}

std::vector<StandardView> layout_views(Layout l) {
    switch (l) {
        case Layout::OneView:
            return {StandardView::Front};
        case Layout::TwoView:
            return {StandardView::Front, StandardView::Top};
        case Layout::ThreeView:
            return {StandardView::Front, StandardView::Top, StandardView::Right};
        case Layout::FourView:
            return {StandardView::Front, StandardView::Top, StandardView::Right,
                    StandardView::Iso};
    }
    return {};
}

void view_axes(StandardView v, f64 right[3], f64 up[3], f64 toward[3]) noexcept {
    // Z is up in the model; the front view looks along +Y.
    auto set = [](f64 d[3], f64 x, f64 y, f64 z) { d[0] = x; d[1] = y; d[2] = z; };
    switch (v) {
        case StandardView::Front:
            set(right, 1, 0, 0); set(up, 0, 0, 1); set(toward, 0, -1, 0); return;
        case StandardView::Back:
            set(right, -1, 0, 0); set(up, 0, 0, 1); set(toward, 0, 1, 0); return;
        case StandardView::Right:
            set(right, 0, 1, 0); set(up, 0, 0, 1); set(toward, 1, 0, 0); return;
        case StandardView::Left:
            set(right, 0, -1, 0); set(up, 0, 0, 1); set(toward, -1, 0, 0); return;
        case StandardView::Top:
            set(right, 1, 0, 0); set(up, 0, 1, 0); set(toward, 0, 0, 1); return;
        case StandardView::Bottom:
            set(right, 1, 0, 0); set(up, 0, -1, 0); set(toward, 0, 0, -1); return;
        default: break;
    }

    f64 sx = 1.0, sy = -1.0;
    switch (v) {
        case StandardView::IsoNE: sx =  1.0; sy =  1.0; break;
        case StandardView::IsoNW: sx = -1.0; sy =  1.0; break;
        case StandardView::IsoSW: sx = -1.0; sy = -1.0; break;
        case StandardView::IsoSE:
        case StandardView::Iso:
        default:                  sx =  1.0; sy = -1.0; break;
    }
    const V3 t = normalise({sx, sy, 1.0});
    const V3 r = normalise(cross({0, 0, 1}, t));
    const V3 u = cross(t, r);
    set(toward, t.x, t.y, t.z);
    set(right, r.x, r.y, r.z);
    set(up, u.x, u.y, u.z);
}

void projection_offset(StandardView from, StandardView to, ProjectionAngle angle,
                       f64& right, f64& up) {
    right = 0.0;
    up = 0.0;
    if (from != StandardView::Front) {
    }

    // Third angle: views go on the side they are seen from; first angle is opposite.
    const f64 s = (angle == ProjectionAngle::Third) ? 1.0 : -1.0;

    switch (to) {
        case StandardView::Top:    up    =  s; break;
        case StandardView::Bottom: up    = -s; break;
        case StandardView::Right:  right =  s; break;
        case StandardView::Left:   right = -s; break;
        case StandardView::Back:   right =  2.0 * s; break;
        case StandardView::Front:  break;
        default:
            right = 1.0;
            up    = 1.0;
            break;
    }
}

Alignment alignment_for(StandardView /*from*/, StandardView to) {
    switch (to) {
        case StandardView::Top:
        case StandardView::Bottom:
            return Alignment::Vertical;     // slides up and down only
        case StandardView::Left:
        case StandardView::Right:
        case StandardView::Back:
            return Alignment::Horizontal;   // slides left and right only
        default:
            return Alignment::None;
    }
}

ViewContent project_body(const gtopo::Body& b, const gcm::WorkingVolume& v,
                         StandardView dir, f64 scale, bool hidden_lines) {
    ViewContent out;
    if (scale <= 0.0) scale = 1.0;

    f64 ra[3], ua[3], ta[3];
    view_axes(dir, ra, ua, ta);
    const V3 R{ra[0], ra[1], ra[2]}, U{ua[0], ua[1], ua[2]}, T{ta[0], ta[1], ta[2]};

    const u32 nv = b.vertex_count();
    std::vector<f64> px(nv), py(nv), depth(nv);
    for (u32 i = 0; i < nv; ++i) {
        const V3 w = world_of(v, b.vertex_point[i]);
        px[i] = dot(w, R) / scale;
        py[i] = dot(w, U) / scale;
        depth[i] = dot(w, T);          // larger is nearer the eye
    }

    const u32 nf = b.face_count();
    std::vector<V3> normal(nf, V3{0, 0, 0});
    // An edge-on face counts as not front-facing.
    std::vector<char> front_facing(nf, 0);
    for (u32 f = 0; f < nf; ++f) {
        if (b.face_loop_begin(f) >= b.face_loop_end(f)) continue;
        const u32 loop = b.face_loop_begin(f);
        const u32 lb = b.loop_begin(loop), le = b.loop_end(loop);
        if (le - lb < 3) continue;

        V3 n{0, 0, 0};
        for (u32 h = lb; h < le; ++h) {
            const V3 a = world_of(v, b.vertex_point[b.he_vertex[h]]);
            const V3 c = world_of(v, b.vertex_point[b.he_vertex[h + 1 < le ? h + 1 : lb]]);
            n.x += (a.y - c.y) * (a.z + c.z);
            n.y += (a.z - c.z) * (a.x + c.x);
            n.z += (a.x - c.x) * (a.y + c.y);
        }
        normal[f] = normalise(n);
        front_facing[f] = dot(normal[f], T) > 1e-9 ? 1 : 0;
    }

    const u32 nh = b.halfedge_count();
    std::vector<char> done(nh, 0);
    for (u32 h = 0; h < nh; ++h) {
        if (done[h]) continue;
        done[h] = 1;
        const u32 twin = b.he_twin[h];
        if (twin != gtopo::kInvalid && twin < nh) done[twin] = 1;

        const u32 loop = b.he_loop[h];
        const u32 lb = b.loop_begin(loop), le = b.loop_end(loop);
        const u32 next = (h + 1 < le) ? h + 1 : lb;
        const u32 a = b.he_vertex[h], c = b.he_vertex[next];
        if (a >= nv || c >= nv) continue;

        Edge2 e;
        e.x0 = px[a]; e.y0 = py[a];
        e.x1 = px[c]; e.y1 = py[c];
        e.face_a = b.loop_face[loop];
        e.face_b = (twin != gtopo::kInvalid && twin < nh)
                       ? b.loop_face[b.he_loop[twin]]
                       : gtopo::kInvalid;

        if (std::hypot(e.x1 - e.x0, e.y1 - e.y0) < 1e-12) {
            ++out.degenerate;
            continue;
        }

        // Visible if either face looks at the eye; exactly one is the silhouette.
        const bool fa = e.face_a < nf ? front_facing[e.face_a] != 0 : true;
        const bool fb = e.face_b < nf ? front_facing[e.face_b] != 0 : fa;

        if (!fa && !fb) {
            if (hidden_lines) out.hidden.push_back(e);
        } else {
            out.visible.push_back(e);
            if (fa != fb) out.silhouette.push_back(e);
        }
    }

    f64 lo_x = 0, hi_x = 0, lo_y = 0, hi_y = 0;
    bool any = false;
    auto grow = [&](const std::vector<Edge2>& es) {
        for (const Edge2& e : es) {
            const f64 xs[2] = {e.x0, e.x1}, ys[2] = {e.y0, e.y1};
            for (int i = 0; i < 2; ++i) {
                if (!any) { lo_x = hi_x = xs[i]; lo_y = hi_y = ys[i]; any = true; }
                lo_x = std::min(lo_x, xs[i]); hi_x = std::max(hi_x, xs[i]);
                lo_y = std::min(lo_y, ys[i]); hi_y = std::max(hi_y, ys[i]);
            }
        }
    };
    grow(out.visible);
    grow(out.hidden);
    if (any) {
        const f64 cx = (lo_x + hi_x) * 0.5, cy = (lo_y + hi_y) * 0.5;
        auto shift = [&](std::vector<Edge2>& es) {
            for (Edge2& e : es) {
                e.x0 -= cx; e.x1 -= cx;
                e.y0 -= cy; e.y1 -= cy;
            }
        };
        shift(out.visible);
        shift(out.hidden);
        shift(out.silhouette);
        out.width = hi_x - lo_x;
        out.height = hi_y - lo_y;
    }
    return out;
}


namespace {

std::vector<std::vector<std::pair<f64, f64>>> chain_loops(
    std::vector<std::array<f64, 4>> segs, f64 tol) {
    std::vector<std::vector<std::pair<f64, f64>>> loops;
    std::vector<char> used(segs.size(), 0);

    auto near = [&](f64 ax, f64 ay, f64 bx, f64 by) {
        return std::hypot(ax - bx, ay - by) <= tol;
    };

    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (used[i]) continue;
        used[i] = 1;
        std::vector<std::pair<f64, f64>> loop{{segs[i][0], segs[i][1]},
                                              {segs[i][2], segs[i][3]}};
        bool grew = true;
        while (grew) {
            grew = false;
            for (std::size_t j = 0; j < segs.size(); ++j) {
                if (used[j]) continue;
                const auto& s = segs[j];
                const auto& tail = loop.back();
                if (near(tail.first, tail.second, s[0], s[1])) {
                    loop.push_back({s[2], s[3]});
                } else if (near(tail.first, tail.second, s[2], s[3])) {
                    loop.push_back({s[0], s[1]});
                } else {
                    continue;
                }
                used[j] = 1;
                grew = true;
            }
        }
        if (loop.size() >= 4 &&
            near(loop.front().first, loop.front().second, loop.back().first,
                 loop.back().second)) {
            loop.pop_back();
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

}

u32 ViewSet::add_section(const SourceModel& m, u32 parent,
                         const SectionLine& line, f64 gap) {
    if (parent >= views_.size()) return kNoView;
    const DrawingView& p = views_[parent];
    gap_ = gap;

    f64 pr[3], pu[3], pt[3];
    view_axes(p.direction, pr, pu, pt);
    const V3 R{pr[0], pr[1], pr[2]}, U{pu[0], pu[1], pu[2]};

    const f64 dn = std::hypot(line.look_x, line.look_y);
    if (dn < kEps) return kNoView;
    const f64 dx = line.look_x / dn, dy = line.look_y / dn;
    const V3 L = normalise({dx * R.x + dy * U.x, dx * R.y + dy * U.y,
                            dx * R.z + dy * U.z});

    const f64 mx = (line.x0 + line.x1) * 0.5 * p.scale;
    const f64 my = (line.y0 + line.y1) * 0.5 * p.scale;

    const auto bb = m.body.bounds(m.volume);
    const V3 centre{(bb.lo.x.to_f64() + bb.hi.x.to_f64()) * 0.5,
                    (bb.lo.y.to_f64() + bb.hi.y.to_f64()) * 0.5,
                    (bb.lo.z.to_f64() + bb.hi.z.to_f64()) * 0.5};
    const V3 on_plane{centre.x + mx * R.x + my * U.x,
                      centre.y + mx * R.y + my * U.y,
                      centre.z + mx * R.z + my * U.z};

    DrawingView v;
    v.kind = ViewKind::Section;
    v.label = "SECTION " + line.label + "-" + line.label;
    v.parent = parent;
    v.scale = p.scale;
    v.scale_from_parent = true;
    v.source = m.id;
    v.source_revision = m.revision;
    v.direction = p.direction;
    v.alignment = (std::fabs(dx) > std::fabs(dy)) ? Alignment::Horizontal
                                                  : Alignment::Vertical;

    const V3 T = {-L.x, -L.y, -L.z};
    const V3 Rv = normalise({-dy * R.x + dx * U.x, -dy * R.y + dx * U.y,
                             -dy * R.z + dx * U.z});
    const V3 Uv = cross(T, Rv);

    const u32 nv = m.body.vertex_count();
    std::vector<f64> px(nv), py(nv), side(nv);
    for (u32 i = 0; i < nv; ++i) {
        const V3 w = world_of(m.volume, m.body.vertex_point[i]);
        px[i] = dot(w, Rv) / v.scale;
        py[i] = dot(w, Uv) / v.scale;
        side[i] = dot(sub(w, on_plane), L);   // positive is beyond the plane
    }

    const u32 nh = m.body.halfedge_count();
    std::vector<char> done(nh, 0);
    std::vector<std::array<f64, 4>> cut_segs;

    for (u32 h = 0; h < nh; ++h) {
        if (done[h]) continue;
        done[h] = 1;
        const u32 twin = m.body.he_twin[h];
        if (twin != gtopo::kInvalid && twin < nh) done[twin] = 1;

        const u32 loop = m.body.he_loop[h];
        const u32 lb = m.body.loop_begin(loop), le = m.body.loop_end(loop);
        const u32 next = (h + 1 < le) ? h + 1 : lb;
        const u32 a = m.body.he_vertex[h], c = m.body.he_vertex[next];
        if (a >= nv || c >= nv) continue;

        f64 ax = px[a], ay = py[a], cx = px[c], cy = py[c];
        const f64 sa = side[a], sc = side[c];
        if (sa < -1e-12 && sc < -1e-12) continue;
        if (sa * sc < 0.0) {
            const f64 t = sa / (sa - sc);
            const f64 ix = ax + (cx - ax) * t, iy = ay + (cy - ay) * t;
            if (sa < 0.0) { ax = ix; ay = iy; } else { cx = ix; cy = iy; }
        }
        if (std::hypot(cx - ax, cy - ay) < 1e-12) {
            ++v.content.degenerate;
            continue;
        }
        Edge2 e;
        e.x0 = ax; e.y0 = ay; e.x1 = cx; e.y1 = cy;
        v.content.visible.push_back(e);
    }

    for (u32 f = 0; f < m.body.face_count(); ++f) {
        std::vector<std::pair<f64, f64>> hits;
        for (u32 loop = m.body.face_loop_begin(f);
             loop < m.body.face_loop_end(f); ++loop) {
            const u32 lb = m.body.loop_begin(loop), le = m.body.loop_end(loop);
            for (u32 h = lb; h < le; ++h) {
                const u32 nx = (h + 1 < le) ? h + 1 : lb;
                const u32 a = m.body.he_vertex[h], c = m.body.he_vertex[nx];
                if (a >= nv || c >= nv) continue;
                const f64 sa = side[a], sc = side[c];
                if (sa * sc >= 0.0) continue;
                const f64 t = sa / (sa - sc);
                hits.push_back({px[a] + (px[c] - px[a]) * t,
                                py[a] + (py[c] - py[a]) * t});
            }
        }
        if (hits.size() < 2) continue;

        // The hits lie on one line; sorted, consecutive pairs are solid spans.
        f64 lo_x = hits[0].first, hi_x = hits[0].first;
        f64 lo_y = hits[0].second, hi_y = hits[0].second;
        for (const auto& q : hits) {
            lo_x = std::min(lo_x, q.first);  hi_x = std::max(hi_x, q.first);
            lo_y = std::min(lo_y, q.second); hi_y = std::max(hi_y, q.second);
        }
        const bool along_x = (hi_x - lo_x) >= (hi_y - lo_y);
        std::sort(hits.begin(), hits.end(),
                  [&](const std::pair<f64, f64>& a, const std::pair<f64, f64>& b) {
                      return along_x ? a.first < b.first : a.second < b.second;
                  });
        for (std::size_t i = 0; i + 1 < hits.size(); i += 2)
            cut_segs.push_back({hits[i].first, hits[i].second,
                                hits[i + 1].first, hits[i + 1].second});
    }

    std::vector<CutFace> faces;
    for (auto& l : chain_loops(cut_segs, 1e-9)) {
        CutFace cf;
        cf.loop = std::move(l);
        faces.push_back(std::move(cf));
    }

    f64 lo_x = 0, hi_x = 0, lo_y = 0, hi_y = 0;
    bool any = false;
    for (const Edge2& e : v.content.visible) {
        const f64 xs[2] = {e.x0, e.x1}, ys[2] = {e.y0, e.y1};
        for (int i = 0; i < 2; ++i) {
            if (!any) { lo_x = hi_x = xs[i]; lo_y = hi_y = ys[i]; any = true; }
            lo_x = std::min(lo_x, xs[i]); hi_x = std::max(hi_x, xs[i]);
            lo_y = std::min(lo_y, ys[i]); hi_y = std::max(hi_y, ys[i]);
        }
    }
    if (any) {
        const f64 cx = (lo_x + hi_x) * 0.5, cy = (lo_y + hi_y) * 0.5;
        for (Edge2& e : v.content.visible) {
            e.x0 -= cx; e.x1 -= cx; e.y0 -= cy; e.y1 -= cy;
        }
        for (CutFace& cf : faces)
            for (auto& q : cf.loop) { q.first -= cx; q.second -= cy; }
        v.content.width = hi_x - lo_x;
        v.content.height = hi_y - lo_y;
    }
    v.generated = true;

    const f64 s = (angle_ == ProjectionAngle::Third) ? 1.0 : -1.0;
    const f64 step_x = (p.content.width + v.content.width) * 0.5 + gap;
    const f64 step_y = (p.content.height + v.content.height) * 0.5 + gap;
    v.x = p.x - s * dx * step_x;
    v.y = p.y - s * dy * step_y;

    views_.push_back(std::move(v));
    const u32 id = static_cast<u32>(views_.size() - 1);
    sections_.push_back({id, line});
    cuts_.push_back({id, std::move(faces)});
    return id;
}

u32 ViewSet::add_detail(const SourceModel& m, u32 parent,
                        const DetailCircle& circle, f64 scale, f64 gap) {
    if (parent >= views_.size()) return kNoView;
    if (!(scale > 0.0)) return kNoView;
    if (!(circle.radius > 0.0)) return kNoView;
    const DrawingView& p = views_[parent];
    gap_ = gap;

    DrawingView v;
    v.kind = ViewKind::Detail;
    v.label = "DETAIL " + circle.label;
    v.parent = parent;
    v.direction = p.direction;
    v.scale = scale;
    v.scale_from_parent = false;
    v.alignment = Alignment::None;
    v.source = m.id;
    v.source_revision = m.revision;
    v.clipped = true;

    const f64 k = p.scale / scale;
    v.clip_x = circle.x * k;
    v.clip_y = circle.y * k;
    v.clip_radius = circle.radius * k;

    regenerate_one(v, m);
    v.x = p.x + p.content.width * 0.5 + v.content.width * 0.5 + gap;
    v.y = p.y;

    views_.push_back(std::move(v));
    const u32 id = static_cast<u32>(views_.size() - 1);
    details_.push_back({id, circle});
    return id;
}

const SectionLine* ViewSet::section_line(u32 i) const {
    for (const auto& [id, l] : sections_)
        if (id == i) return &l;
    return nullptr;
}

const DetailCircle* ViewSet::detail_circle(u32 i) const {
    for (const auto& [id, c] : details_)
        if (id == i) return &c;
    return nullptr;
}

const std::vector<CutFace>& ViewSet::cut_faces(u32 i) const {
    static const std::vector<CutFace> none;
    for (const auto& [id, f] : cuts_)
        if (id == i) return f;
    return none;
}


const char* break_style_name(BreakStyle s) noexcept {
    switch (s) {
        case BreakStyle::Straight: return "Straight";
        case BreakStyle::Zigzag:   return "Zigzag";
        case BreakStyle::Jagged:   return "Jagged";
    }
    return "";
}

std::vector<std::vector<std::pair<f64, f64>>> break_symbol(const ViewBreak& b,
                                                           const DrawingView& v) {
    std::vector<std::vector<std::pair<f64, f64>>> out;
    if (!b.valid()) return out;

    const f64 at = std::min(b.from, b.to);
    const f64 half = b.gap * 0.5;
    const f64 span = b.horizontal ? v.content.height : v.content.width;
    const f64 reach = span * 0.5 + span * 0.06 + 0.002;

    for (int side = 0; side < 2; ++side) {
        const f64 offset = at + (side == 0 ? -half : half);
        std::vector<std::pair<f64, f64>> line;

        auto point = [&](f64 along, f64 across) {
            // `along` runs across the part; `across` runs along the break.
            if (b.horizontal) line.push_back({offset + across, along});
            else              line.push_back({along, offset + across});
        };

        switch (b.style) {
            case BreakStyle::Straight:
                point(-reach, 0.0);
                point(reach, 0.0);
                break;

            case BreakStyle::Zigzag: {
                const f64 z = std::max(0.002, span * 0.08);
                point(-reach, 0.0);
                point(-z, 0.0);
                point(-z * 0.5, (side == 0 ? -1.0 : 1.0) * z * 0.5);
                point(z * 0.5, (side == 0 ? 1.0 : -1.0) * z * 0.5);
                point(z, 0.0);
                point(reach, 0.0);
                break;
            }

            case BreakStyle::Jagged: {
                const int teeth = 9;
                const f64 amp = std::max(0.0015, span * 0.05);
                for (int i = 0; i <= teeth; ++i) {
                    const f64 t = static_cast<f64>(i) / teeth;
                    const f64 along = -reach + 2.0 * reach * t;
                    const f64 sign = (i % 2 == 0) ? 1.0 : -1.0;
                    point(along, (i == 0 || i == teeth) ? 0.0 : sign * amp);
                }
                break;
            }
        }
        out.push_back(std::move(line));
    }
    return out;
}

namespace {

void apply_break(ViewContent& c, const ViewBreak& b) {
    if (!b.valid()) return;
    const f64 lo = std::min(b.from, b.to), hi = std::max(b.from, b.to);
    const f64 shift = (hi - lo) - b.gap;

    auto move = [&](std::vector<Edge2>& es) {
        std::vector<Edge2> keep;
        for (Edge2 e : es) {
            f64* p0 = b.horizontal ? &e.x0 : &e.y0;
            f64* p1 = b.horizontal ? &e.x1 : &e.y1;
            if (*p0 > lo && *p0 < hi && *p1 > lo && *p1 < hi) continue;

            auto trim = [&](f64& a, f64& other, f64& oa, f64& ob) {
                if (a > lo && a < hi) {
                    const f64 t = (other > hi ? hi : lo);
                    const f64 span = other - a;
                    if (std::fabs(span) > kEps) {
                        const f64 f = (t - a) / span;
                        oa += (ob - oa) * f;
                    }
                    a = t;
                }
            };
            f64& q0 = b.horizontal ? e.y0 : e.x0;
            f64& q1 = b.horizontal ? e.y1 : e.x1;
            trim(*p0, *p1, q0, q1);
            trim(*p1, *p0, q1, q0);

            if (*p0 >= hi) *p0 -= shift;
            if (*p1 >= hi) *p1 -= shift;
            if (std::hypot(e.x1 - e.x0, e.y1 - e.y0) < kEps) continue;
            keep.push_back(e);
        }
        es.swap(keep);
    };
    move(c.visible);
    move(c.hidden);
    move(c.silhouette);

    if (b.horizontal) c.width = std::max(0.0, c.width - shift);
    else              c.height = std::max(0.0, c.height - shift);
}

void apply_crop(ViewContent& c, const ViewCrop& r) {
    if (!r.enabled || !r.valid()) return;
    auto clip = [&](std::vector<Edge2>& es) {
        std::vector<Edge2> keep;
        for (const Edge2& e : es) {
            f64 t0 = 0.0, t1 = 1.0;
            const f64 dx = e.x1 - e.x0, dy = e.y1 - e.y0;
            const f64 p[4] = {-dx, dx, -dy, dy};
            const f64 q[4] = {e.x0 - r.lo_x, r.hi_x - e.x0, e.y0 - r.lo_y,
                              r.hi_y - e.y0};
            bool keep_it = true;
            for (int i = 0; i < 4 && keep_it; ++i) {
                if (std::fabs(p[i]) < kEps) {
                    if (q[i] < 0.0) keep_it = false;
                    continue;
                }
                const f64 t = q[i] / p[i];
                if (p[i] < 0.0) t0 = std::max(t0, t);
                else            t1 = std::min(t1, t);
                if (t0 > t1) keep_it = false;
            }
            if (!keep_it) continue;
            Edge2 o = e;
            o.x0 = e.x0 + dx * t0;
            o.y0 = e.y0 + dy * t0;
            o.x1 = e.x0 + dx * t1;
            o.y1 = e.y0 + dy * t1;
            if (std::hypot(o.x1 - o.x0, o.y1 - o.y0) < kEps) continue;
            keep.push_back(o);
        }
        es.swap(keep);
    };
    clip(c.visible);
    clip(c.hidden);
    clip(c.silhouette);

    c.width = r.hi_x - r.lo_x;
    c.height = r.hi_y - r.lo_y;
}

}

bool ViewSet::add_break(u32 view, const ViewBreak& b) {
    if (view >= views_.size() || !b.valid()) return false;
    if (b.gap >= b.removed()) return false;
    views_[view].breaks.push_back(b);
    apply_break(views_[view].content, b);
    return true;
}

bool ViewSet::clear_breaks(u32 view) {
    if (view >= views_.size() || views_[view].breaks.empty()) return false;
    views_[view].breaks.clear();
    return true;
}

bool ViewSet::set_crop(u32 view, const ViewCrop& c) {
    if (view >= views_.size()) return false;
    if (c.enabled && !c.valid()) return false;
    views_[view].crop = c;
    apply_crop(views_[view].content, views_[view].crop);
    return true;
}

void ViewSet::set_angle(ProjectionAngle a) {
    if (a == angle_) return;
    angle_ = a;
    for (u32 i = 0; i < views_.size(); ++i)
        if (views_[i].kind == ViewKind::Base) relayout_children(i, gap_);
}

u32 ViewSet::add_base(const SourceModel& m, StandardView dir, f64 x, f64 y,
                      f64 scale) {
    DrawingView v;
    v.kind = ViewKind::Base;
    v.direction = dir;
    v.label = gview::view_name(dir);
    v.x = x;
    v.y = y;
    v.scale = scale > 0.0 ? scale : 1.0;
    v.scale_from_parent = false;
    v.source = m.id;
    v.source_revision = m.revision;
    v.content = project_body(m.body, m.volume, dir, v.scale, v.show_hidden);
    v.generated = true;
    views_.push_back(std::move(v));
    return static_cast<u32>(views_.size() - 1);
}

void ViewSet::lay_out_child(DrawingView& child, const DrawingView& parent,
                            f64 gap) const {
    f64 rx = 0.0, uy = 0.0;
    projection_offset(parent.direction, child.direction, angle_, rx, uy);

    const f64 step_x = (parent.content.width + child.content.width) * 0.5 + gap;
    const f64 step_y = (parent.content.height + child.content.height) * 0.5 + gap;

    child.x = parent.x + rx * step_x;
    child.y = parent.y + uy * step_y;
}

u32 ViewSet::project(const SourceModel& m, u32 parent, StandardView dir,
                     f64 gap) {
    if (parent >= views_.size()) return kNoView;
    gap_ = gap;

    DrawingView v;
    v.kind = (dir == StandardView::Iso || dir == StandardView::IsoNE ||
              dir == StandardView::IsoNW || dir == StandardView::IsoSE ||
              dir == StandardView::IsoSW)
                 ? ViewKind::Isometric
                 : ViewKind::Projected;
    v.direction = dir;
    v.label = gview::view_name(dir);
    v.parent = parent;
    v.scale = views_[parent].scale;
    v.scale_from_parent = true;
    v.alignment = alignment_for(views_[parent].direction, dir);
    v.source = m.id;
    v.source_revision = m.revision;
    v.content = project_body(m.body, m.volume, dir, v.scale, v.show_hidden);
    v.generated = true;

    lay_out_child(v, views_[parent], gap);
    views_.push_back(std::move(v));
    return static_cast<u32>(views_.size() - 1);
}

std::vector<u32> ViewSet::add_layout(const SourceModel& m, Layout l, f64 x,
                                     f64 y, f64 scale, f64 gap) {
    std::vector<u32> out;
    const std::vector<StandardView> dirs = layout_views(l);
    if (dirs.empty()) return out;

    const u32 base = add_base(m, dirs.front(), x, y, scale);
    out.push_back(base);
    for (std::size_t i = 1; i < dirs.size(); ++i)
        out.push_back(project(m, base, dirs[i], gap));
    return out;
}

DrawingView* ViewSet::view(u32 i) {
    return i < views_.size() ? &views_[i] : nullptr;
}
const DrawingView* ViewSet::view(u32 i) const {
    return i < views_.size() ? &views_[i] : nullptr;
}

bool ViewSet::is_stale(u32 i, const SourceModel& m) const {
    if (i >= views_.size()) return false;
    const DrawingView& v = views_[i];
    return v.source == m.id && v.source_revision != m.revision;
}

std::vector<u32> ViewSet::stale(const SourceModel& m) const {
    std::vector<u32> out;
    for (u32 i = 0; i < views_.size(); ++i)
        if (is_stale(i, m)) out.push_back(i);
    return out;
}

void ViewSet::regenerate_one(DrawingView& v, const SourceModel& m) {
    v.content = project_body(m.body, m.volume, v.direction, v.scale,
                             v.show_hidden);

    for (const ViewBreak& b : v.breaks) apply_break(v.content, b);
    apply_crop(v.content, v.crop);

    if (v.clipped && v.clip_radius > 0.0) {
        auto clip = [&](std::vector<Edge2>& es) {
            std::vector<Edge2> keep;
            for (const Edge2& e : es) {
                f64 ax = e.x0 - v.clip_x, ay = e.y0 - v.clip_y;
                f64 bx = e.x1 - v.clip_x, by = e.y1 - v.clip_y;
                const f64 dx = bx - ax, dy = by - ay;
                const f64 aa = dx * dx + dy * dy;
                if (aa < 1e-24) continue;
                const f64 r2 = v.clip_radius * v.clip_radius;

                // Solve |a + t d|^2 = r^2 for the entry and exit parameters,
                // then intersect that range with [0,1].
                const f64 b = 2.0 * (ax * dx + ay * dy);
                const f64 c = ax * ax + ay * ay - r2;
                const f64 disc = b * b - 4.0 * aa * c;
                if (disc < 0.0) continue;            // misses the circle
                const f64 sq = std::sqrt(disc);
                f64 t0 = (-b - sq) / (2.0 * aa);
                f64 t1 = (-b + sq) / (2.0 * aa);
                t0 = std::max(t0, 0.0);
                t1 = std::min(t1, 1.0);
                if (t1 - t0 < 1e-12) continue;

                Edge2 o = e;
                o.x0 = e.x0 + (e.x1 - e.x0) * t0;
                o.y0 = e.y0 + (e.y1 - e.y0) * t0;
                o.x1 = e.x0 + (e.x1 - e.x0) * t1;
                o.y1 = e.y0 + (e.y1 - e.y0) * t1;
                keep.push_back(o);
            }
            es.swap(keep);
        };
        clip(v.content.visible);
        clip(v.content.hidden);
        clip(v.content.silhouette);

        v.content.width = v.content.height = v.clip_radius * 2.0;
        auto recentre = [&](std::vector<Edge2>& es) {
            for (Edge2& e : es) {
                e.x0 -= v.clip_x; e.x1 -= v.clip_x;
                e.y0 -= v.clip_y; e.y1 -= v.clip_y;
            }
        };
        recentre(v.content.visible);
        recentre(v.content.hidden);
        recentre(v.content.silhouette);
    }
    v.generated = true;
}

std::size_t ViewSet::regenerate(const SourceModel& m) {
    std::size_t n = 0;
    for (DrawingView& v : views_) {
        if (v.source != m.id) continue;
        if (v.kind == ViewKind::Section) { v.source_revision = m.revision; ++n; continue; }
        regenerate_one(v, m);
        v.source_revision = m.revision;
        ++n;
    }
    for (u32 i = 0; i < views_.size(); ++i)
        if (views_[i].kind == ViewKind::Base) relayout_children(i, gap_);
    return n;
}

void ViewSet::relayout_children(u32 parent, f64 gap) {
    if (parent >= views_.size()) return;
    for (DrawingView& v : views_) {
        if (v.parent != parent) continue;
        if (v.alignment == Alignment::None && v.kind != ViewKind::Isometric)
            continue;   // deliberately unaligned: leave it where it was put
        lay_out_child(v, views_[parent], gap);
    }
}

bool ViewSet::move(u32 i, f64 to_x, f64 to_y) {
    if (i >= views_.size()) return false;
    DrawingView& v = views_[i];

    f64 nx = to_x, ny = to_y;
    if (v.alignment == Alignment::Vertical) nx = v.x;
    if (v.alignment == Alignment::Horizontal) ny = v.y;

    const f64 dx = nx - v.x, dy = ny - v.y;
    v.x = nx;
    v.y = ny;

    for (DrawingView& c : views_)
        if (c.parent == i) {
            c.x += dx;
            c.y += dy;
        }
    return true;
}

bool ViewSet::unalign(u32 i) {
    if (i >= views_.size()) return false;
    if (views_[i].alignment == Alignment::None) return false;
    views_[i].alignment = Alignment::None;
    return true;
}

void to_sheet_shapes(const ViewSet& s, const gcm::WorkingVolume& paper,
                     std::vector<gsnap::Shape>& shapes,
                     std::vector<std::string>& layers,
                     const SheetLayers& names) {
    auto emit = [&](const std::vector<Edge2>& es, const DrawingView& v,
                    const std::string& layer) {
        for (const Edge2& e : es) {
            gsnap::Shape sh;
            sh.kind = gsnap::ShapeKind::Segment;
            sh.a = gcm::to_lattice(paper, v.x + e.x0, v.y + e.y0);
            sh.b = gcm::to_lattice(paper, v.x + e.x1, v.y + e.y1);
            shapes.push_back(sh);
            layers.push_back(layer);
        }
    };

    for (u32 id = 0; id < s.all().size(); ++id) {
        const DrawingView& v = s.all()[id];
        if (!v.generated) continue;
        std::vector<Edge2> plain;
        if (v.show_silhouettes) {
            plain.reserve(v.content.visible.size());
            for (const Edge2& e : v.content.visible) {
                bool outline = false;
                for (const Edge2& o : v.content.silhouette)
                    if (&o == &e || (o.x0 == e.x0 && o.y0 == e.y0 &&
                                     o.x1 == e.x1 && o.y1 == e.y1))
                        outline = true;
                if (!outline) plain.push_back(e);
            }
            emit(v.content.silhouette, v, names.silhouette);
        } else {
            plain = v.content.visible;
        }
        emit(plain, v, names.visible);
        if (v.show_hidden) emit(v.content.hidden, v, names.hidden);

        const std::vector<CutFace>& cut = s.cut_faces(id);
        for (const CutFace& cf : cut) {
            for (std::size_t i = 0; i < cf.loop.size(); ++i) {
                const auto& a = cf.loop[i];
                const auto& b = cf.loop[(i + 1) % cf.loop.size()];
                gsnap::Shape sh;
                sh.kind = gsnap::ShapeKind::Segment;
                sh.a = gcm::to_lattice(paper, v.x + a.first, v.y + a.second);
                sh.b = gcm::to_lattice(paper, v.x + b.first, v.y + b.second);
                shapes.push_back(sh);
                layers.push_back(names.cut);
            }
        }

        if (const SectionLine* line = s.section_line(id)) {
            if (const DrawingView* parent = s.view(v.parent)) {
                gsnap::Shape sh;
                sh.kind = gsnap::ShapeKind::Segment;
                sh.a = gcm::to_lattice(paper, parent->x + line->x0,
                                       parent->y + line->y0);
                sh.b = gcm::to_lattice(paper, parent->x + line->x1,
                                       parent->y + line->y1);
                shapes.push_back(sh);
                layers.push_back(names.annotation);
            }
        }
        if (const DetailCircle* c = s.detail_circle(id)) {
            if (const DrawingView* parent = s.view(v.parent)) {
                gsnap::Shape sh;
                sh.kind = gsnap::ShapeKind::Circle;
                sh.a = gcm::to_lattice(paper, parent->x + c->x,
                                       parent->y + c->y);
                sh.radius = static_cast<gcm::i64>(
                    std::llround(c->radius * paper.scale()));
                shapes.push_back(sh);
                layers.push_back(names.annotation);
            }
        }
    }
}

}
