#include "geometry/loft.hpp"

#include <algorithm>
#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"

namespace gtopo {

using gcm::F64;
using gcm::Lattice3;
using P3 = ggeo::d3::Point<F64>;

const char* loft_error_message(LoftError e) noexcept {
    switch (e) {
        case LoftError::None:                return "ok";
        case LoftError::TooFewSections:      return "a loft needs at least two sections";
        case LoftError::VertexCountMismatch:
            return "every section must have the same vertex count; "
                   "corresponding vertices are what get joined";
        case LoftError::InvalidProfile:      return "a section profile is not valid";
    }
    return "";
}

LoftError check_sections(const std::vector<Section>& s) {
    if (s.size() < 2) return LoftError::TooFewSections;

    const std::size_t n = s[0].profile.outer.size();
    const std::size_t holes = s[0].profile.holes.size();
    for (const auto& sec : s) {
        Profile p = sec.profile;
        p.normalize();
        if (!p.valid()) return LoftError::InvalidProfile;
        if (p.outer.size() != n) return LoftError::VertexCountMismatch;
        if (p.holes.size() != holes) return LoftError::VertexCountMismatch;
        for (std::size_t h = 0; h < holes; ++h)
            if (p.holes[h].size() != s[0].profile.holes[h].size())
                return LoftError::VertexCountMismatch;
    }
    return LoftError::None;
}

Body loft(const std::vector<Section>& sections, const gcm::WorkingVolume& vol,
          bool cap_ends) {
    Builder b;
    if (check_sections(sections) != LoftError::None) return b.build();

    std::vector<Profile> profiles;
    profiles.reserve(sections.size());
    for (const auto& s : sections) {
        Profile p = s.profile;
        p.normalize();
        profiles.push_back(std::move(p));
    }

    const std::size_t loops = 1 + profiles[0].holes.size();

    // grid[loop][section][vertex]
    std::vector<std::vector<std::vector<VertexId>>> grid(loops);
    for (auto& g : grid) g.resize(sections.size());

    for (std::size_t si = 0; si < sections.size(); ++si) {
        const auto& frame = sections[si].frame;
        std::vector<const Loop2*> ls{&profiles[si].outer};
        for (const auto& h : profiles[si].holes) ls.push_back(&h);

        for (std::size_t li = 0; li < ls.size(); ++li) {
            grid[li][si].reserve(ls[li]->size());
            for (const auto& q : *ls[li]) {
                f64 x, y;
                gcm::to_world(vol, q, x, y);
                const P3 w = frame.to_world(F64::from(x), F64::from(y));
                grid[li][si].push_back(b.add_vertex(gcm::to_lattice(
                    vol, w.x.to_f64(), w.y.to_f64(), w.z.to_f64())));
            }
        }
    }

    for (std::size_t li = 0; li < loops; ++li) {
        const std::size_t n = grid[li][0].size();
        for (std::size_t si = 0; si + 1 < sections.size(); ++si)
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                b.begin_face();
                b.add_loop({grid[li][si][i], grid[li][si][j],
                            grid[li][si + 1][j], grid[li][si + 1][i]});
                b.end_face();
            }
    }

    if (cap_ends) {
        b.begin_face();
        {
            std::vector<VertexId> o = grid[0][0];
            std::reverse(o.begin(), o.end());
            b.add_loop(o);
            for (std::size_t li = 1; li < loops; ++li) {
                std::vector<VertexId> h = grid[li][0];
                std::reverse(h.begin(), h.end());
                b.add_loop(h);
            }
        }
        b.end_face();

        const std::size_t last = sections.size() - 1;
        b.begin_face();
        b.add_loop(grid[0][last]);
        for (std::size_t li = 1; li < loops; ++li) b.add_loop(grid[li][last]);
        b.end_face();
    }

    return b.build();
}

namespace {

Loop2 offset_loop(const Loop2& loop, f64 distance, f64 sign,
                  const gcm::WorkingVolume& vol) {
    const std::size_t n = loop.size();
    Loop2 out;
    if (n < 3) return out;

    struct Line { f64 px, py, dx, dy; };
    std::vector<Line> edges(n);

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        f64 ax, ay, bx, by;
        gcm::to_world(vol, loop[i], ax, ay);
        gcm::to_world(vol, loop[j], bx, by);
        f64 dx = bx - ax, dy = by - ay;
        const f64 len = std::hypot(dx, dy);
        if (len < 1e-12) return {};
        dx /= len; dy /= len;
        const f64 nx = -dy * distance * sign, ny = dx * distance * sign;
        edges[i] = {ax + nx, ay + ny, dx, dy};
    }

    std::vector<gcm::Lattice2> pts;
    pts.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t h = (i + n - 1) % n;
        const Line& a = edges[h];
        const Line& b = edges[i];
        const f64 den = a.dx * b.dy - a.dy * b.dx;
        if (std::fabs(den) < 1e-9) {
            out.push_back(gcm::to_lattice(vol, b.px, b.py));
            continue;
        }
        const f64 t = ((b.px - a.px) * b.dy - (b.py - a.py) * b.dx) / den;
        pts.push_back(gcm::to_lattice(vol, a.px + a.dx * t, a.py + a.dy * t));
    }

    // Test for reversed edges: area and winding checks miss an over-deep inset.
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        f64 ax, ay, bx, by;
        gcm::to_world(vol, pts[i], ax, ay);
        gcm::to_world(vol, pts[j], bx, by);
        const f64 dot = (bx - ax) * edges[i].dx + (by - ay) * edges[i].dy;
        if (dot < 0.0) return {};
    }

    out = std::move(pts);
    return out;
}

}

Profile offset_profile(const Profile& p_in, f64 distance,
                       const gcm::WorkingVolume& vol) {
    Profile p = p_in;
    p.normalize();
    Profile out;
    if (!p.valid()) return out;

    out.outer = offset_loop(p.outer, distance, 1.0, vol);
    if (out.outer.size() < 3) return Profile{};

    for (const auto& h : p.holes) {
        Loop2 o = offset_loop(h, distance, 1.0, vol);
        if (o.size() < 3) return Profile{};
        out.holes.push_back(std::move(o));
    }

    if (signed_area_sign(out.outer) != gcm::Sign::Positive) return Profile{};
    for (const auto& h : out.holes)
        if (signed_area_sign(h) != gcm::Sign::Negative) return Profile{};

    out.normalize();
    return out;
}

Body shell_extrusion(const Profile& p_in, const gcm::WorkingVolume& vol,
                     f64 height, f64 thickness, bool open_top, bool open_bottom) {
    Builder empty;
    if (thickness <= 0.0 || height <= 0.0) return empty.build();

    Profile outer = p_in;
    outer.normalize();
    if (!outer.valid()) return empty.build();

    const Profile inner = offset_profile(outer, thickness, vol);
    if (!inner.valid()) return empty.build();

    const Body out_body = extrude_z(outer, vol, height);
    if (!open_top && !open_bottom) {
        Profile in = inner;
        auto frame = ggeo::d3::Frame<F64>::world_xy();
        frame.origin = ggeo::d3::Point<F64>::from(0, 0, thickness);
        const Body void_body = extrude(in, vol, frame, height - 2.0 * thickness);
        const auto r = subtract(out_body, void_body, vol);
        return r.ok() ? r.body : empty.build();
    }

    const f64 z0 = open_bottom ? -thickness : thickness;
    const f64 z1 = open_top ? height + thickness : height - thickness;
    if (z1 <= z0) return empty.build();

    auto frame = ggeo::d3::Frame<F64>::world_xy();
    frame.origin = ggeo::d3::Point<F64>::from(0, 0, z0);
    const Body void_body = extrude(inner, vol, frame, z1 - z0);

    const auto r = subtract(out_body, void_body, vol);
    return r.ok() ? r.body : empty.build();
}

}
