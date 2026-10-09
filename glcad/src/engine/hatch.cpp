#include "engine/hatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <limits>
#include <unordered_map>

#include "math/bigint.hpp"
#include "math/predicate.hpp"

namespace gdoc {

using P = gcm::predicate::Host;
using gcm::Sign;

namespace {

struct PointKey {
    i64 x, y;
    bool operator==(const PointKey& o) const noexcept { return x == o.x && y == o.y; }
};
struct PointHash {
    std::size_t operator()(const PointKey& k) const noexcept {
        return std::hash<i64>{}(k.x) * 1000003u ^ std::hash<i64>{}(k.y);
    }
};

}

i64 loop_area_sign(const Loop& loop) {
    if (loop.size() < 3) return 0;
    gcm::SInt<256> acc;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Point2& a = loop[i];
        const Point2& b = loop[(i + 1) % loop.size()];
        acc = gcm::add(acc, gcm::mul_i64_i64<256>(a.x, b.y));
        acc = gcm::sub(acc, gcm::mul_i64_i64<256>(a.y, b.x));
    }
    const Sign s = acc.sign();
    return s == Sign::Positive ? 1 : (s == Sign::Negative ? -1 : 0);
}

// Half-open [y0, y1) crossings count each vertex once, with no tolerance.
bool point_in_loop(Point2 p, const Loop& loop) {
    if (loop.size() < 3) return false;
    bool inside = false;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Point2& a = loop[i];
        const Point2& b = loop[(i + 1) % loop.size()];
        if ((a.y > p.y) == (b.y > p.y)) continue;

        const Sign s = P::orient2d(a, b, p);
        if (s == Sign::Zero) return true;                 // exactly on the edge
        const bool upward = b.y > a.y;
        if ((s == Sign::Positive) == upward) inside = !inside;
    }
    return inside;
}

bool point_in_region(Point2 p, const std::vector<Loop>& loops) {
    int crossings = 0;
    for (const auto& l : loops)
        if (point_in_loop(p, l)) ++crossings;
    return (crossings % 2) == 1;      // even-odd: islands punch holes
}

std::string BoundaryResult::message(const gcm::WorkingVolume& v) const {
    switch (error) {
        case BoundaryError::None: return "Boundary closed";
        case BoundaryError::TooFewSegments:
            return "Boundary needs at least three segments";
        case BoundaryError::NoEnclosingLoop:
            return "No enclosing boundary around that point";
        case BoundaryError::NotClosed: {
            f64 x, y;
            gcm::to_world(v, gap_at, x, y);
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "Boundary not closed \xe2\x80\x94 gap near (%.4f, %.4f)", x, y);
            return buf;
        }
    }
    return "";
}

namespace {

using Wide = __int128;

Wide cross(Point2 o, Point2 a, Point2 b) {
    return Wide(a.x - o.x) * Wide(b.y - o.y) - Wide(a.y - o.y) * Wide(b.x - o.x);
}

int half_plane(i64 dx, i64 dy) { return (dy > 0 || (dy == 0 && dx > 0)) ? 0 : 1; }

bool angle_less(i64 ax, i64 ay, i64 bx, i64 by) {
    const int ha = half_plane(ax, ay), hb = half_plane(bx, by);
    if (ha != hb) return ha < hb;
    return Wide(ax) * Wide(by) - Wide(ay) * Wide(bx) > 0;
}

std::vector<Segment2> split_at_touches(const std::vector<Segment2>& segments) {
    std::vector<Point2> points;
    points.reserve(segments.size() * 2);
    for (const auto& s : segments) { points.push_back(s.a); points.push_back(s.b); }
    std::sort(points.begin(), points.end(), [](Point2 a, Point2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    points.erase(std::unique(points.begin(), points.end(), [](Point2 a, Point2 b) { return a.x == b.x && a.y == b.y; }), points.end());
    std::map<i64, std::vector<i64>> by_y, by_x;
    for (const auto& p : points) { by_y[p.y].push_back(p.x); by_x[p.x].push_back(p.y); }
    for (auto& [y, xs] : by_y) std::sort(xs.begin(), xs.end());

    std::vector<Segment2> out;
    out.reserve(segments.size());
    std::vector<Point2> cuts;
    for (const auto& s : segments) {
        if (s.a.x == s.b.x && s.a.y == s.b.y) continue;
        cuts.clear();
        if (s.a.y == s.b.y) {
            const auto& xs = by_y[s.a.y];
            const i64 lo = std::min(s.a.x, s.b.x), hi = std::max(s.a.x, s.b.x);
            for (auto it = std::upper_bound(xs.begin(), xs.end(), lo); it != xs.end() && *it < hi; ++it)
                cuts.push_back({*it, s.a.y});
        } else if (s.a.x == s.b.x) {
            const auto& ys = by_x[s.a.x];
            const i64 lo = std::min(s.a.y, s.b.y), hi = std::max(s.a.y, s.b.y);
            for (auto it = std::upper_bound(ys.begin(), ys.end(), lo); it != ys.end() && *it < hi; ++it)
                cuts.push_back({s.a.x, *it});
        } else {
            const i64 lox = std::min(s.a.x, s.b.x), hix = std::max(s.a.x, s.b.x);
            const i64 loy = std::min(s.a.y, s.b.y), hiy = std::max(s.a.y, s.b.y);
            auto it = std::upper_bound(points.begin(), points.end(), Point2{lox, std::numeric_limits<i64>::max()},
                                       [](Point2 a, Point2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
            for (; it != points.end() && it->x < hix; ++it)
                if (it->y > loy && it->y < hiy && cross(s.a, s.b, *it) == 0) cuts.push_back(*it);
        }
        if (cuts.empty()) { out.push_back(s); continue; }
        const i64 dx = s.b.x - s.a.x, dy = s.b.y - s.a.y;
        std::sort(cuts.begin(), cuts.end(), [&](Point2 p, Point2 q) {
            return Wide(p.x - s.a.x) * dx + Wide(p.y - s.a.y) * dy < Wide(q.x - s.a.x) * dx + Wide(q.y - s.a.y) * dy;
        });
        Point2 from = s.a;
        for (const auto& c : cuts) { out.push_back({from, c}); from = c; }
        out.push_back({from, s.b});
    }

    for (auto& s : out)
        if (s.b.x < s.a.x || (s.b.x == s.a.x && s.b.y < s.a.y)) std::swap(s.a, s.b);
    std::sort(out.begin(), out.end(), [](const Segment2& p, const Segment2& q) {
        if (p.a.x != q.a.x) return p.a.x < q.a.x;
        if (p.a.y != q.a.y) return p.a.y < q.a.y;
        if (p.b.x != q.b.x) return p.b.x < q.b.x;
        return p.b.y < q.b.y;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const Segment2& p, const Segment2& q) {
        return p.a.x == q.a.x && p.a.y == q.a.y && p.b.x == q.b.x && p.b.y == q.b.y;
    }), out.end());
    return out;
}

}

BoundaryResult extract_loops(const std::vector<Segment2>& input) {
    BoundaryResult r;
    if (input.size() < 3) { r.error = BoundaryError::TooFewSegments; return r; }
    const std::vector<Segment2> segments = split_at_touches(input);

    std::unordered_map<PointKey, std::vector<std::size_t>, PointHash> adj;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        adj[{segments[i].a.x, segments[i].a.y}].push_back(i);
        adj[{segments[i].b.x, segments[i].b.y}].push_back(i);
    }

    std::vector<bool> dead(segments.size(), false);
    bool  saw_free_end = false;
    Point2 first_gap{};
    {
        std::unordered_map<PointKey, std::size_t, PointHash> live;
        live.reserve(adj.size());
        for (const auto& [k, uses] : adj) live[k] = uses.size();
        std::vector<PointKey> work;
        for (const auto& [k, n] : live)
            if (n < 2) work.push_back(k);
        while (!work.empty()) {
            const PointKey k = work.back();
            work.pop_back();
            const auto it = live.find(k);
            if (it == live.end() || it->second >= 2 || it->second == 0) continue;
            if (!saw_free_end) { saw_free_end = true; first_gap = Point2{k.x, k.y}; }
            for (const std::size_t si : adj[k]) {
                if (dead[si]) continue;
                dead[si] = true;
                for (const Point2 p : {segments[si].a, segments[si].b}) {
                    const auto pit = live.find(PointKey{p.x, p.y});
                    if (pit == live.end() || pit->second == 0) continue;
                    if (--pit->second < 2) work.push_back(PointKey{p.x, p.y});
                }
            }
        }
    }

    struct Half { Point2 from, to; std::size_t twin{0}, next{0}; bool done{false}; };
    std::vector<Half> half;
    half.reserve(segments.size() * 2);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (dead[i]) continue;
        const std::size_t h = half.size();
        half.push_back({segments[i].a, segments[i].b, h + 1});
        half.push_back({segments[i].b, segments[i].a, h});
    }
    std::unordered_map<PointKey, std::vector<std::size_t>, PointHash> out_of;
    for (std::size_t h = 0; h < half.size(); ++h) out_of[{half[h].from.x, half[h].from.y}].push_back(h);
    for (auto& [k, list] : out_of) {
        std::sort(list.begin(), list.end(), [&](std::size_t a, std::size_t b) {
            return angle_less(half[a].to.x - half[a].from.x, half[a].to.y - half[a].from.y,
                              half[b].to.x - half[b].from.x, half[b].to.y - half[b].from.y);
        });
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::size_t into = half[list[i]].twin;
            half[into].next = list[(i + list.size() - 1) % list.size()];
        }
    }

    for (std::size_t start = 0; start < half.size(); ++start) {
        if (half[start].done) continue;
        Loop loop;
        std::size_t h = start;
        bool closed = false;
        for (std::size_t guard = 0; guard <= half.size(); ++guard) {
            if (half[h].done) { closed = h == start; break; }
            half[h].done = true;
            loop.push_back(half[h].from);
            h = half[h].next;
        }
        if (closed && loop.size() >= 3 && loop_area_sign(loop) > 0) r.loops.push_back(std::move(loop));
    }

    if (r.loops.empty()) {
        r.error = BoundaryError::NotClosed;
        if (saw_free_end) r.gap_at = first_gap;
        return r;
    }
    std::vector<long double> area(r.loops.size());
    for (std::size_t i = 0; i < r.loops.size(); ++i) {
        long double a = 0;
        const auto& l = r.loops[i];
        for (std::size_t j = 0; j < l.size(); ++j) {
            const auto p = l[j], q = l[(j + 1) % l.size()];
            a += static_cast<long double>(p.x) * q.y - static_cast<long double>(q.x) * p.y;
        }
        area[i] = a;
    }
    std::vector<std::size_t> order(r.loops.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return area[a] > area[b]; });
    std::vector<Loop> sorted;
    sorted.reserve(order.size());
    for (auto i : order) sorted.push_back(std::move(r.loops[i]));
    r.loops = std::move(sorted);
    return r;
}

BoundaryResult trace_region(const std::vector<Segment2>& segments, Point2 seed) {
    const BoundaryResult all = extract_loops(segments);
    if (!all.ok()) return all;
    return region_containing(all, seed);
}

BoundaryResult region_containing(const BoundaryResult& all, Point2 seed) {
    if (!all.ok()) return all;

    BoundaryResult r;

    std::vector<std::size_t> containing;
    for (std::size_t i = 0; i < all.loops.size(); ++i)
        if (point_in_loop(seed, all.loops[i])) containing.push_back(i);

    if (containing.empty()) {
        r.error = BoundaryError::NoEnclosingLoop;
        r.gap_at = seed;
        return r;
    }

    const auto magnitude = [](const Loop& l) {
        long double a = 0;
        for (std::size_t j = 0; j < l.size(); ++j) {
            const auto p = l[j], q = l[(j + 1) % l.size()];
            a += static_cast<long double>(p.x) * q.y - static_cast<long double>(q.x) * p.y;
        }
        return a < 0 ? -a : a;
    };
    std::size_t best = containing[0];
    for (std::size_t ci : containing)
        if (magnitude(all.loops[ci]) < magnitude(all.loops[best])) best = ci;
    r.loops.push_back(all.loops[best]);

    const Loop& outer = all.loops[best];
    i64 minx = outer[0].x, maxx = outer[0].x, miny = outer[0].y, maxy = outer[0].y;
    std::unordered_map<PointKey, bool, PointHash> corners;
    for (const auto& p : outer) {
        corners[{p.x, p.y}] = true;
        minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
    }
    const long double best_area = magnitude(outer);
    std::vector<std::size_t> inside;
    for (std::size_t i = 0; i < all.loops.size(); ++i) {
        if (i == best || all.loops[i].empty()) continue;
        const auto& l = all.loops[i];
        if (l[0].x < minx || l[0].x > maxx || l[0].y < miny || l[0].y > maxy) continue;
        if (magnitude(l) >= best_area) continue;
        bool touching = false;
        for (const auto& p : l)
            if (corners.count({p.x, p.y})) { touching = true; break; }
        if (!touching && point_in_loop(l[0], outer)) inside.push_back(i);
    }
    if (inside.empty()) return r;

    std::unordered_map<PointKey, std::size_t, PointHash> owner;
    std::vector<std::size_t> parent(inside.size());
    for (std::size_t k = 0; k < inside.size(); ++k) parent[k] = k;
    const auto root = [&](std::size_t k) { while (parent[k] != k) k = parent[k] = parent[parent[k]]; return k; };
    for (std::size_t k = 0; k < inside.size(); ++k)
        for (const auto& p : all.loops[inside[k]]) {
            const auto [it, fresh] = owner.try_emplace(PointKey{p.x, p.y}, k);
            if (!fresh) parent[root(k)] = root(it->second);
        }
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t k = 0; k < inside.size(); ++k) groups[root(k)].push_back(inside[k]);

    std::vector<Loop> islands;
    for (const auto& [g, members] : groups) {
        if (members.size() == 1) { islands.push_back(all.loops[members[0]]); continue; }
        std::map<std::pair<std::pair<i64, i64>, std::pair<i64, i64>>, int> edges;
        for (const auto i : members) {
            const auto& l = all.loops[i];
            for (std::size_t j = 0; j < l.size(); ++j) {
                const auto a = l[j], b = l[(j + 1) % l.size()];
                const auto reverse = edges.find({{b.x, b.y}, {a.x, a.y}});
                if (reverse != edges.end()) edges.erase(reverse);
                else edges[{{a.x, a.y}, {b.x, b.y}}] = 1;
            }
        }
        std::multimap<std::pair<i64, i64>, std::pair<i64, i64>> from;
        for (const auto& [e, n] : edges) from.emplace(e.first, e.second);
        while (!from.empty()) {
            Loop loop;
            auto it = from.begin();
            const auto start = it->first;
            auto at = it->second;
            loop.push_back({start.first, start.second});
            from.erase(it);
            for (std::size_t guard = 0; guard <= edges.size() && at != start; ++guard) {
                loop.push_back({at.first, at.second});
                const auto next = from.find(at);
                if (next == from.end()) break;
                at = next->second;
                from.erase(next);
            }
            if (at == start && loop.size() >= 3 && loop_area_sign(loop) > 0) islands.push_back(std::move(loop));
        }
    }
    std::sort(islands.begin(), islands.end(), [&](const Loop& a, const Loop& b) { return magnitude(a) > magnitude(b); });
    std::vector<Loop> kept;
    for (auto& island : islands) {
        bool nested = false;
        for (const auto& k : kept)
            if (point_in_loop(island[0], k)) { nested = true; break; }
        if (!nested) kept.push_back(std::move(island));
    }
    for (auto& k : kept) r.loops.push_back(std::move(k));
    return r;
}

PatternTable::PatternTable() {
    add({"SOLID", "Solid fill", {}});
    add({"ANSI31", "ANSI iron, brick, stone masonry", {{45.0, 0, 0, 0, 0.125, {}}}});
    add({"ANSI32", "ANSI steel", {{45.0, 0, 0, 0, 0.375, {}},
                                  {45.0, 0.176776695, 0, 0, 0.375, {}}}});
    add({"ANSI33", "ANSI bronze, brass, copper",
         {{45.0, 0, 0, 0, 0.25, {}}, {45.0, 0.176776695, 0, 0, 0.25, {0.125, -0.0625}}}});
    add({"ANSI34", "ANSI plastic, rubber",
         {{45.0, 0, 0, 0, 0.75, {}},
          {45.0, 0.176776695, 0, 0, 0.75, {}},
          {45.0, 0.353553391, 0, 0, 0.75, {}},
          {45.0, 0.530330086, 0, 0, 0.75, {}}}});
    add({"ANSI35", "ANSI fire brick, refractory",
         {{45.0, 0, 0, 0, 0.25, {}},
          {45.0, 0.176776695, 0, 0, 0.25, {0.3125, -0.0625, 0, -0.0625}}}});
    add({"ANSI36", "ANSI marble, slate, glass",
         {{45.0, 0, 0, 0.21875, 0.125, {0.3125, -0.0625, 0, -0.0625}}}});
    add({"ANSI37", "ANSI lead, zinc, magnesium",
         {{45.0, 0, 0, 0, 0.125, {}}, {135.0, 0, 0, 0, 0.125, {}}}});
    add({"ANSI38", "ANSI aluminum",
         {{45.0, 0, 0, 0, 0.125, {}},
          {135.0, 0, 0, 0.25, 0.125, {0.3125, -0.1875}}}});
    add({"NET", "Horizontal and vertical grid",
         {{0.0, 0, 0, 0, 0.125, {}}, {90.0, 0, 0, 0, 0.125, {}}}});
    add({"LINE", "Parallel horizontal lines", {{0.0, 0, 0, 0, 0.125, {}}}});
    add({"DOTS", "Dotted fill", {{0.0, 0, 0, 0.03125, 0.0625, {0.0, -0.0625}}}});
    add({"EARTH", "Earth or ground", {{0.0, 0, 0, 0.25, 0.25, {}},
                                      {90.0, 0, 0, 0.25, 0.25, {}}}});
    add({"CONCRETE", "Concrete", {{45.0, 0, 0, 0.25, 0.25, {0.125, -0.125}}}});
    add({"INSUL", "Insulation batting", {{0.0, 0, 0, 0.125, 0.125, {0.125, -0.125}}}});
}

const HatchPattern* PatternTable::find(std::string_view name) const {
    for (const auto& p : pats_)
        if (p.name.size() == name.size() &&
            std::equal(p.name.begin(), p.name.end(), name.begin(),
                       [](char a, char b) {
                           return std::toupper((unsigned char)a) ==
                                  std::toupper((unsigned char)b);
                       }))
            return &p;
    return nullptr;
}

HatchPattern& PatternTable::add(HatchPattern p) {
    for (auto& e : pats_)
        if (e.name == p.name) { e = std::move(p); return e; }
    pats_.push_back(std::move(p));
    return pats_.back();
}

std::size_t PatternTable::load_pat(std::string_view text) {
    std::size_t i = 0, loaded = 0;
    HatchPattern cur;
    bool have = false;

    auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
        while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
        return s;
    };
    auto flush = [&] {
        if (have && !cur.name.empty()) { add(cur); ++loaded; }
        have = false;
        cur = HatchPattern{};
    };

    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string_view::npos) e = text.size();
        std::string_view line = trim(text.substr(i, e - i));
        i = e + 1;
        if (line.empty() || line.front() == ';') continue;

        if (line.front() == '*') {
            flush();
            line.remove_prefix(1);
            const std::size_t c = line.find(',');
            cur.name = std::string(trim(c == std::string_view::npos ? line : line.substr(0, c)));
            cur.description = c == std::string_view::npos
                                  ? std::string{}
                                  : std::string(trim(line.substr(c + 1)));
            have = true;
            continue;
        }
        if (!have) continue;

        std::vector<f64> nums;
        std::size_t p = 0;
        while (p < line.size()) {
            std::size_t q = line.find(',', p);
            if (q == std::string_view::npos) q = line.size();
            const std::string_view tok = trim(line.substr(p, q - p));
            if (!tok.empty()) nums.push_back(std::atof(std::string(tok).c_str()));
            p = q + 1;
        }
        if (nums.size() >= 5) {
            PatternLine pl;
            pl.angle = nums[0];
            pl.x = nums[1];
            pl.y = nums[2];
            pl.dx = nums[3];
            pl.dy = nums[4];
            for (std::size_t k = 5; k < nums.size(); ++k) pl.dashes.push_back(nums[k]);
            cur.lines.push_back(pl);
        }
    }
    flush();
    return loaded;
}

Hatch generate_hatch(const std::vector<Loop>& loops, const HatchPattern& pattern,
                     const HatchStyle& style, const gcm::WorkingVolume& vol) {
    Hatch h;
    if (loops.empty()) return h;

    if (pattern.solid()) {
        h.solid = true;
        h.fill_loops = loops;
        // Triangulation expects a CCW outer boundary and CW islands.
        for(std::size_t i=0;i<h.fill_loops.size();++i)
            if((loop_area_sign(h.fill_loops[i])>0)!=(i==0))
                std::reverse(h.fill_loops[i].begin(),h.fill_loops[i].end());
        return h;
    }

    const auto fail=[&](const char* reason) {h.lines.clear();h.error=reason;return h;};
    if(!(style.scale>0) || !std::isfinite(style.scale) || !std::isfinite(style.angle))
        return fail("Hatch scale and angle must be finite, with a positive scale");
    constexpr std::size_t max_work=16000000,max_segments=250000;
    std::size_t work=0;
    constexpr const char* dense="Hatch pattern is too dense; increase its scale or dash spacing";

    f64 minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
    for (const auto& l : loops)
        for (const auto& p : l) {
            f64 x, y;
            gcm::to_world(vol, p, x, y);
            minx = std::min(minx, x); maxx = std::max(maxx, x);
            miny = std::min(miny, y); maxy = std::max(maxy, y);
        }
    if (minx > maxx) return h;

    const f64 cx = (minx + maxx) * 0.5, cy = (miny + maxy) * 0.5;

    constexpr f64 kPi = 3.14159265358979323846;

    for (const auto& fam : pattern.lines) {
        if(!std::isfinite(fam.angle) || !std::isfinite(fam.dx) || !std::isfinite(fam.dy))
            return fail("Hatch pattern contains non-finite coordinates");
        if(!fam.dashes.empty() && std::all_of(fam.dashes.begin(),fam.dashes.end(),
                                             [](f64 d){return std::isfinite(d) && d<0;}))continue;
        const f64 ang = (fam.angle + style.angle) * kPi / 180.0;
        const f64 ux = std::cos(ang), uy = std::sin(ang);
        const f64 nx = -uy, ny = ux;

        f64 spacing = std::hypot(fam.dx, fam.dy) * style.scale;
        if (spacing < 1e-9) spacing = 0.125 * style.scale;

        if (!(spacing > 0) || !std::isfinite(spacing)) return fail(dense);
        struct Edge { f64 a, b, c, d; };
        std::vector<Edge> edges;
        f64 across_lo=1e300,across_hi=-1e300;
        for(const auto& loop:loops)for(std::size_t i=0;i<loop.size();++i) {
            f64 ax,ay,bx,by;
            gcm::to_world(vol,loop[i],ax,ay);
            gcm::to_world(vol,loop[(i+1)%loop.size()],bx,by);
            const f64 a=(ax-cx)*ux+(ay-cy)*uy, b=(ax-cx)*nx+(ay-cy)*ny;
            const f64 c=(bx-cx)*ux+(by-cy)*uy, d=(bx-cx)*nx+(by-cy)*ny;
            edges.push_back({a,b,c,d});
            across_lo=std::min(across_lo,b);across_hi=std::max(across_hi,b);
        }
        f64 cycle=0;
        for(f64 dash:fam.dashes)cycle+=std::abs(dash)*style.scale;
        if(!std::isfinite(cycle))return fail("Hatch dash cycle must be finite");
        const auto emit=[&](f64 lo,f64 hi,f64 off) {
            if(h.lines.size()>=max_segments) {h.lines.clear();h.error=dense;return;}
            const auto a=gcm::to_lattice(vol,cx+nx*off+ux*lo,cy+ny*off+uy*lo);
            const auto b=gcm::to_lattice(vol,cx+nx*off+ux*hi,cy+ny*off+uy*hi);
            h.lines.push_back({a,b});
        };
        std::vector<f64> crossings;crossings.reserve(edges.size());
        std::vector<std::pair<f64,f64>> coincident;
        const f64 roundoff=vol.inv_scale()+32*std::numeric_limits<f64>::epsilon()*
            (1+std::hypot(maxx-minx,maxy-miny));
        const auto emit_span=[&](f64 lo,f64 hi,f64 off) {
            if(!(hi>lo))return;
            if(fam.dashes.empty() || !(cycle>0)) {emit(lo,hi,off);return;}
            // Integer counters: float accumulation stalls at very small periods.
            const auto first_cycle=std::floor(lo/cycle),last_cycle=std::floor(hi/cycle);
            const auto cycles=last_cycle-first_cycle+1;
            if(!std::isfinite(cycles) || std::abs(first_cycle)>9e15 || std::abs(last_cycle)>9e15 ||
               cycles>double((max_work-work)/fam.dashes.size())) {
                h.lines.clear();h.error=dense;return;
            }
            work+=std::size_t(cycles)*fam.dashes.size();
            for(gcm::i64 index=gcm::i64(first_cycle);index<=gcm::i64(last_cycle);++index) {
                f64 start=double(index)*cycle;
                for(f64 dash:fam.dashes) {
                    const f64 end=start+std::abs(dash)*style.scale;
                    if(dash>=0 && end>=lo && start<=hi) {
                        const auto a=std::max(lo,start),b=std::min(hi,end);
                        if(b>a || (dash==0 && start>=lo && start<hi))emit(a,b,off);
                        if(h.error!=nullptr)return;
                    }
                    start=end;
                }
            }
        };
        const auto first=std::ceil(across_lo/spacing),last=std::floor(across_hi/spacing);
        const auto count=last-first+1;
        if(!std::isfinite(count) || std::abs(first)>9e15 || std::abs(last)>9e15 ||
           count>double((max_work-work)/std::max<std::size_t>(1,edges.size())))return fail(dense);
        if(count<=0)continue;
        work+=std::size_t(count)*edges.size();
        for(gcm::i64 k=gcm::i64(first);k<=gcm::i64(last);++k) {
            const f64 off=double(k)*spacing;
            crossings.clear();coincident.clear();
            for(const auto& e:edges) {
                // Within one lattice cell of a parallel boundary counts as coincident.
                const auto b=std::abs(e.b-off)<=roundoff?off:e.b;
                const auto d=std::abs(e.d-off)<=roundoff?off:e.d;
                if(b==off && d==off) {
                    coincident.emplace_back(std::min(e.a,e.c),std::max(e.a,e.c));
                    continue;
                }
                // Half-open crossings count a shared vertex once; pairs give even-odd.
                if((b>off)==(d>off))continue;
                crossings.push_back(e.a+(off-b)*(e.c-e.a)/(d-b));
            }
            std::sort(crossings.begin(),crossings.end());
            std::sort(coincident.begin(),coincident.end());
            for(std::size_t i=0;i+1<crossings.size();i+=2) {
                f64 lo=crossings[i];const f64 hi=crossings[i+1];
                for(const auto& edge:coincident) {
                    if(work>=max_work)return fail(dense);
                    ++work;
                    if(edge.second<=lo || edge.first>=hi)continue;
                    emit_span(lo,std::min(hi,edge.first),off);
                    if(h.error!=nullptr)return h;
                    lo=std::max(lo,edge.second);
                    if(lo>=hi)break;
                }
                emit_span(lo,hi,off);
                if(h.error!=nullptr)return h;
            }
        }
    }
    return h;
}


gcm::u64 boundary_fingerprint(const std::vector<Segment2>& segments) {
    // Combined by addition only so the hash is order-independent; mix at the end.
    gcm::u64 sum = 0, weighted = 0;
    for (const Segment2& s : segments) {
        gcm::i64 x0 = s.a.x, y0 = s.a.y, x1 = s.b.x, y1 = s.b.y;
        if (x1 < x0 || (x1 == x0 && y1 < y0)) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }
        gcm::u64 h = 1469598103934665603ull;
        for (gcm::i64 v : {x0, y0, x1, y1}) {
            h ^= static_cast<gcm::u64>(v);
            h *= 1099511628211ull;
        }
        sum += h;
        weighted += h * 0x9E3779B97F4A7C15ull;
    }
    gcm::u64 out = sum ^ (weighted >> 29);
    out *= 0xFF51AFD7ED558CCDull;
    out ^= out >> 33;
    return out;
}

HatchUpdate update_hatch(AssociativeHatch& h, const std::vector<Segment2>& segments,
                         const PatternTable& patterns,
                         const gcm::WorkingVolume& vol) {
    HatchUpdate out;

    if (!h.style.associative && h.valid) {
        out.reason = "not associative";
        return out;
    }

    std::vector<Segment2> boundary;
    boundary.reserve(h.boundary_shapes.size());
    for (gcm::u32 i : h.boundary_shapes)
        if (i < segments.size()) boundary.push_back(segments[i]);

    if (boundary.size() < 3) {
        h.orphaned = true;
        h.valid = false;
        out.orphaned = true;
        out.reason = "the boundary is gone";
        return out;
    }

    const gcm::u64 fingerprint = boundary_fingerprint(boundary);
    if (h.valid && fingerprint == h.boundary_hash) {
        out.reason = "unchanged";
        return out;
    }

    const BoundaryResult loops =
        h.seed_valid ? trace_region(boundary, h.seed) : extract_loops(boundary);
    if (!loops.ok() || loops.loops.empty()) {
        h.orphaned = true;
        h.valid = false;
        out.orphaned = true;
        out.reason = "the boundary no longer closes";
        return out;
    }

    const HatchPattern* pattern = patterns.find(h.style.name);
    if (!pattern) {
        out.reason = "unknown pattern";
        return out;
    }

    h.cached = generate_hatch(loops.loops, *pattern, h.style, vol);
    if(h.cached.error!=nullptr) {
        h.valid=false;h.orphaned=false;out.reason=h.cached.error;return out;
    }
    h.boundary_hash = fingerprint;
    h.valid = true;
    h.orphaned = false;
    out.regenerated = true;
    out.reason = "regenerated";
    return out;
}

const char* gradient_kind_name(GradientKind k) noexcept {
    switch (k) {
        case GradientKind::Linear:                return "linear";
        case GradientKind::Cylindrical:           return "cylindrical";
        case GradientKind::InvertedCylindrical:   return "inverted cylindrical";
        case GradientKind::Spherical:             return "spherical";
        case GradientKind::InvertedSpherical:     return "inverted spherical";
        case GradientKind::HemiSpherical:         return "hemispherical";
        case GradientKind::InvertedHemiSpherical: return "inverted hemispherical";
        case GradientKind::Curved:                return "curved";
        case GradientKind::InvertedCurved:        return "inverted curved";
    }
    return "?";
}

namespace {

struct Extent {
    f64 lo_x{0}, lo_y{0}, hi_x{0}, hi_y{0};
    bool empty{true};
};

Extent region_extent(const std::vector<Loop>& region, const gcm::WorkingVolume& v) {
    Extent e;
    for (const Loop& l : region)
        for (const Point2& p : l) {
            f64 x, y;
            gcm::to_world(v, p, x, y);
            if (e.empty) {
                e.lo_x = e.hi_x = x;
                e.lo_y = e.hi_y = y;
                e.empty = false;
                continue;
            }
            e.lo_x = std::min(e.lo_x, x);
            e.hi_x = std::max(e.hi_x, x);
            e.lo_y = std::min(e.lo_y, y);
            e.hi_y = std::max(e.hi_y, y);
        }
    return e;
}

f64 clamp01(f64 t) { return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t); }

}

f64 gradient_at(const Gradient& g, Point2 p, const std::vector<Loop>& region,
                const gcm::WorkingVolume& vol) {
    const Extent e = region_extent(region, vol);
    if (e.empty) return 0.0;

    f64 px, py;
    gcm::to_world(vol, p, px, py);

    const f64 cx = g.centred ? (e.lo_x + e.hi_x) * 0.5 : e.lo_x;
    const f64 cy = g.centred ? (e.lo_y + e.hi_y) * 0.5 : e.lo_y;
    const f64 w = std::max(1e-12, e.hi_x - e.lo_x);
    const f64 h = std::max(1e-12, e.hi_y - e.lo_y);
    const f64 half = std::max(w, h) * 0.5;

    const f64 rad = g.angle * 3.14159265358979323846 / 180.0;
    const f64 ca = std::cos(rad), sa = std::sin(rad);
    const f64 lx = (px - cx) * ca + (py - cy) * sa;
    const f64 ly = -(px - cx) * sa + (py - cy) * ca;

    switch (g.kind) {
        case GradientKind::Linear:
            return clamp01((lx + w * 0.5) / w);

        case GradientKind::Cylindrical:
            return clamp01(1.0 - std::fabs(lx) / (w * 0.5));
        case GradientKind::InvertedCylindrical:
            return clamp01(std::fabs(lx) / (w * 0.5));

        case GradientKind::Spherical:
            return clamp01(1.0 - std::hypot(lx, ly) / half);
        case GradientKind::InvertedSpherical:
            return clamp01(std::hypot(lx, ly) / half);

        case GradientKind::HemiSpherical:
            return clamp01(1.0 - std::hypot(lx, ly * 2.0) / half);
        case GradientKind::InvertedHemiSpherical:
            return clamp01(std::hypot(lx, ly * 2.0) / half);

        case GradientKind::Curved: {
            const f64 t = clamp01((lx + w * 0.5) / w);
            return t * t;
        }
        case GradientKind::InvertedCurved: {
            const f64 t = clamp01((lx + w * 0.5) / w);
            return 1.0 - (1.0 - t) * (1.0 - t);
        }
    }
    return 0.0;
}

Color gradient_color(const Gradient& g, f64 t) {
    t = clamp01(t);

    auto resolve = [](const Color& c, gcm::u8& r, gcm::u8& gg, gcm::u8& b) {
        if (c.kind == Color::Kind::True) {
            r = c.r;
            gg = c.g;
            b = c.b;
            return;
        }
        r = gg = b = 128;
    };

    gcm::u8 r0, g0, b0, r1, g1, b1;
    resolve(g.from, r0, g0, b0);
    if (g.two_colour) {
        resolve(g.to, r1, g1, b1);
    } else {
        const f64 k = clamp01(g.tint);
        r1 = static_cast<gcm::u8>(r0 + (255 - r0) * k);
        g1 = static_cast<gcm::u8>(g0 + (255 - g0) * k);
        b1 = static_cast<gcm::u8>(b0 + (255 - b0) * k);
    }

    return Color::rgb(static_cast<gcm::u8>(r0 + (r1 - r0) * t),
                      static_cast<gcm::u8>(g0 + (g1 - g0) * t),
                      static_cast<gcm::u8>(b0 + (b1 - b0) * t));
}

std::vector<GradientBand> gradient_bands(const Gradient& g,
                                         const std::vector<Loop>& region,
                                         int bands, const gcm::WorkingVolume& vol) {
    std::vector<GradientBand> out;
    if (region.empty() || bands < 1) return out;

    out.reserve(static_cast<std::size_t>(bands));
    for (int i = 0; i < bands; ++i) {
        const f64 t = bands == 1 ? 0.5 : static_cast<f64>(i) / (bands - 1);
        GradientBand band;
        band.loops = region;
        band.t = t;
        band.color = gradient_color(g, t);
        out.push_back(std::move(band));
    }
    return out;
}

}
