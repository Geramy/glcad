#include "geometry/region.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace gregion {
namespace {
using Real = long double;
using Shape = gsnap::Shape;
constexpr Real pi = 3.141592653589793238462643383279502884L;
constexpr Real tau = 2 * pi;
struct XY {
  Real x, y;
};
XY operator+(XY a, XY b) { return {a.x + b.x, a.y + b.y}; }
XY operator-(XY a, XY b) { return {a.x - b.x, a.y - b.y}; }
XY operator*(XY a, Real t) { return {a.x * t, a.y * t}; }
Real dot(XY a, XY b) { return a.x * b.x + a.y * b.y; }
Real cross(XY a, XY b) { return a.x * b.y - a.y * b.x; }
Real length(XY a) { return std::hypot(a.x, a.y); }
XY point(gcm::Lattice2 p) { return {Real(p.x), Real(p.y)}; }
gcm::Lattice2 lattice(XY p) {
  return {gcm::i64(std::llround(p.x)), gcm::i64(std::llround(p.y))};
}
Real angle(Real a) {
  a = std::fmod(a, tau);
  return a < 0 ? a + tau : a;
}
Real sweep(const Shape &s) {
  if (s.kind == gsnap::ShapeKind::Circle)
    return tau;
  Real a = Real(s.end_angle) - s.start_angle;
  if (a >= tau - 1e-12L)
    return tau;
  return angle(a);
}
XY at(const Shape &s, Real t) {
  if (s.kind == gsnap::ShapeKind::Segment)
    return point(s.a) + (point(s.b) - point(s.a)) * t;
  const Real a =
      (s.kind == gsnap::ShapeKind::Circle ? 0 : Real(s.start_angle)) +
      sweep(s) * t;
  return point(s.a) + XY{std::cos(a), std::sin(a)} * Real(s.radius);
}
Real parameter(const Shape &s, XY p) {
  if (s.kind == gsnap::ShapeKind::Segment) {
    const auto d = point(s.b) - point(s.a);
    const auto n = dot(d, d);
    return n > 0 ? dot(p - point(s.a), d) / n : 0;
  }
  const auto d = p - point(s.a);
  const Real start =
      s.kind == gsnap::ShapeKind::Circle ? 0 : Real(s.start_angle);
  return angle(std::atan2(d.y, d.x) - start) / sweep(s);
}
bool on(const Shape &s, XY p, Real tolerance = 4) {
  // Arc angles wrap: test endpoint proximity before rejecting a parameter < 0.
  if (length(at(s, 0) - p) <= tolerance || length(at(s, 1) - p) <= tolerance)
    return true;
  const auto t = parameter(s, p);
  if (t < -1e-12L || t > 1 + 1e-12L)
    return false;
  return length(at(s, std::clamp(t, Real(0), Real(1))) - p) <= tolerance;
}

std::vector<XY> intersections(const Shape &a, const Shape &b) {
  std::vector<XY> result;
  // Margin matches the four-unit tolerance used by on().
  const auto bounds = [](const Shape &s) {
    if (s.kind == gsnap::ShapeKind::Segment)
      return std::array<Real, 4>{Real(std::min(s.a.x, s.b.x)),
          Real(std::min(s.a.y, s.b.y)), Real(std::max(s.a.x, s.b.x)),
          Real(std::max(s.a.y, s.b.y))};
    return std::array<Real, 4>{Real(s.a.x) - s.radius, Real(s.a.y) - s.radius,
        Real(s.a.x) + s.radius, Real(s.a.y) + s.radius};
  };
  const auto ab = bounds(a), bb = bounds(b);
  if (ab[2] + 8 < bb[0] || bb[2] + 8 < ab[0] ||
      ab[3] + 8 < bb[1] || bb[3] + 8 < ab[1])
    return result;
  const auto keep = [&](XY p) {
    if (on(a, p) && on(b, p))
      result.push_back(p);
  };
  const bool line_a = a.kind == gsnap::ShapeKind::Segment,
             line_b = b.kind == gsnap::ShapeKind::Segment;
  if (line_a && line_b) {
    const auto p = point(a.a), q = point(b.a), u = point(a.b) - p,
               v = point(b.b) - q;
    const Real denominator = cross(u, v);
    if (denominator != 0)
      keep(p + u * (cross(q - p, v) / denominator));
  } else if (line_a || line_b) {
    const auto &line = line_a ? a : b;
    const auto &circle = line_a ? b : a;
    const auto u = point(line.b) - point(line.a),
               m = point(line.a) - point(circle.a);
    const Real aa = dot(u, u), bb = 2 * dot(m, u),
               cc = dot(m, m) - Real(circle.radius) * circle.radius;
    Real disc = bb * bb - 4 * aa * cc;
    if (aa > 0 && disc >= 0) {
      disc = std::sqrt(disc);
      keep(point(line.a) + u * ((-bb - disc) / (2 * aa)));
      if (disc > 0)
        keep(point(line.a) + u * ((-bb + disc) / (2 * aa)));
    }
  } else {
    const auto d = point(b.a) - point(a.a);
    const auto distance = length(d);
    const Real ra = a.radius, rb = b.radius;
    if (distance > 0 && distance <= ra + rb && distance >= std::abs(ra - rb)) {
      const Real x = (ra * ra - rb * rb + distance * distance) / (2 * distance);
      const Real h = std::sqrt(std::max(Real(0), ra * ra - x * x));
      const auto u = d * (1 / distance), base = point(a.a) + u * x,
                 n = XY{-u.y, u.x};
      keep(base + n * h);
      if (h > 0)
        keep(base - n * h);
    }
  }
  for (auto p : {at(a, 0), at(a, 1), at(b, 0), at(b, 1)})
    keep(p);
  return result;
}
bool inside(const Region &r, XY p) {
  bool result = false;
  for (const auto &loop : r.loops)
    for (const auto &edge : loop) {
      const auto &s = edge.shape;
      if (s.kind == gsnap::ShapeKind::Segment) {
        const auto a = point(s.a), b = point(s.b);
        if ((a.y > p.y) != (b.y > p.y) &&
            a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y) > p.x)
          result = !result;
      } else {
        std::vector<Real> cuts{0, 1};
        for (Real a : {pi / 2, 3 * pi / 2}) {
          const Real t = angle(a - (s.kind == gsnap::ShapeKind::Circle
                                        ? 0
                                        : Real(s.start_angle))) /
                         sweep(s);
          if (t > 1e-12L && t < 1 - 1e-12L)
            cuts.push_back(t);
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t i = 1; i < cuts.size(); ++i) {
          // Lattice nodes, not the unsnapped sin(2*pi): avoids a gap at the seam.
          const auto a = point(lattice(at(s, cuts[i - 1])));
          const auto b = point(lattice(at(s, cuts[i])));
          if ((a.y > p.y) == (b.y > p.y))
            continue;
          const Real dy = p.y - s.a.y;
          const Real dx =
              std::sqrt(std::max(Real(0), Real(s.radius) * s.radius - dy * dy));
          const bool right = at(s, (cuts[i - 1] + cuts[i]) / 2).x >= s.a.x;
          if (Real(s.a.x) + (right ? dx : -dx) > p.x)
            result = !result;
        }
      }
    }
  return result;
}
Real signed_area(const Loop &loop) {
  Real area = 0;
  const auto origin = at(loop.front().shape, 0);
  for (const auto &e : loop) {
    const auto &s = e.shape;
    const auto a = at(s, 0) - origin, b = at(s, 1) - origin;
    Real integral = cross(a, b);
    if (s.is_curve()) {
      const Real delta = sweep(s);
      integral += Real(s.radius) * s.radius * (delta - std::sin(delta));
    }
    area += (e.reversed ? -integral : integral) / 2;
  }
  return area;
}
bool near(XY a, XY b) { return length(a - b) <= 4; }
XY tangent(const Edge &edge, bool end) {
  const auto &s = edge.shape;
  XY d = point(s.b) - point(s.a);
  if (s.is_curve()) {
    const Real t = (end != edge.reversed) ? 1 : 0;
    const Real a =
        (s.kind == gsnap::ShapeKind::Circle ? 0 : Real(s.start_angle)) +
        sweep(s) * t;
    d = {-std::sin(a), std::cos(a)};
  }
  return edge.reversed ? d * Real(-1) : d;
}
Result assemble(std::vector<Edge> edges, bool directed) {
  Result result;
  if (edges.size() > 4096) {
    result.error = "region exceeds 4096 boundary edges";
    return result;
  }
  while (!edges.empty()) {
    Loop loop;
    loop.push_back(edges.back());
    edges.pop_back();
    const auto first = loop.front();
    const XY start = at(first.shape, first.reversed ? 1 : 0);
    XY end = at(first.shape, first.reversed ? 0 : 1);
    while (!near(end, start)) {
      std::size_t found = edges.size();
      bool reverse = false;
      Real best_turn = -pi - 1;
      for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto &edge = edges[i];
        const auto a = at(edge.shape, edge.reversed ? 1 : 0),
                   b = at(edge.shape, edge.reversed ? 0 : 1);
        const bool forwards = near(end, a),
                   backwards = !directed && near(end, b);
        if (!forwards && !backwards)
          continue;
        if (found != edges.size() && !directed) {
          result.error = "region boundary branches or touches itself";
          return result;
        }
        if (directed) {
          const auto incoming = tangent(loop.back(), true),
                     outgoing = tangent(edge, false);
          const Real turn =
              std::atan2(cross(incoming, outgoing), dot(incoming, outgoing));
          if (turn <= best_turn)
            continue;
          best_turn = turn;
        }
        found = i;
        reverse = backwards && !forwards;
      }
      if (found == edges.size()) {
        result.error = "region boundary is open";
        return result;
      }
      auto next = edges[found];
      if (reverse)
        next.reversed = !next.reversed;
      loop.push_back(next);
      edges.erase(edges.begin() + found);
      end = at(next.shape, next.reversed ? 0 : 1);
    }
    // Share one lattice point so exact tracing keeps tolerance-matched loops closed.
    for (std::size_t i = 0; i < loop.size(); ++i) {
      auto &a = loop[i];
      auto &b = loop[(i + 1) % loop.size()];
      if (a.shape.kind != gsnap::ShapeKind::Segment ||
          b.shape.kind != gsnap::ShapeKind::Segment)
        continue;
      auto &from = a.reversed ? a.shape.a : a.shape.b;
      auto &to = b.reversed ? b.shape.b : b.shape.a;
      const auto joint = lattice((point(from) + point(to)) * .5L);
      from = to = joint;
    }
    if (std::abs(signed_area(loop)) < 1) {
      if (directed)
        continue;
      result.error = "region boundary has zero area";
      return result;
    }
    result.region.loops.push_back(std::move(loop));
  }
  result.ok = true;
  return result;
}
Shape portion(const Shape &s, Real lo, Real hi) {
  Shape out = s;
  out.key = out.id = out.poly = out.poly_index = 0;
  if (s.kind == gsnap::ShapeKind::Segment) {
    out.a = lattice(at(s, lo));
    out.b = lattice(at(s, hi));
  } else {
    out.kind = gsnap::ShapeKind::Arc;
    const auto start = s.kind == gsnap::ShapeKind::Circle ? 0 : s.start_angle;
    out.start_angle = double(start + sweep(s) * lo);
    out.end_angle = double(start + sweep(s) * hi);
  }
  return out;
}
std::string invalid_edge(const Shape &s) {
  const auto safe = [](gcm::Lattice2 p) {
    const Real limit = Real(std::numeric_limits<gcm::i64>::max() - 4096);
    return std::abs(Real(p.x)) <= limit && std::abs(Real(p.y)) <= limit;
  };
  if (!safe(s.a) || (s.kind == gsnap::ShapeKind::Segment && !safe(s.b)))
    return "region boundary exceeds lattice range";
  if (s.kind != gsnap::ShapeKind::Segment && !s.is_curve())
    return "regions require line or circular boundaries";
  if (s.is_curve() &&
      (s.radius <= 0 || !std::isfinite(s.start_angle) ||
       !std::isfinite(s.end_angle) ||
       !std::isfinite(s.end_angle - s.start_angle) || sweep(s) < 1e-12L ||
       std::abs(Real(s.a.x)) + Real(s.radius) >
           Real(std::numeric_limits<gcm::i64>::max() - 4096) ||
       std::abs(Real(s.a.y)) + Real(s.radius) >
           Real(std::numeric_limits<gcm::i64>::max() - 4096)))
    return "invalid circular region boundary";
  if (s.kind == gsnap::ShapeKind::Segment && near(point(s.a), point(s.b)))
    return "region edge is below drawing resolution";
  return {};
}
}
std::pair<gcm::Lattice2, gcm::Lattice2> endpoints(const Edge &e) {
  return {lattice(at(e.shape, e.reversed ? 1 : 0)),
          lattice(at(e.shape, e.reversed ? 0 : 1))};
}
bool Region::contains(gcm::Lattice2 p) const { return inside(*this, point(p)); }
double Region::area(const gcm::WorkingVolume &v) const {
  Real result = 0;
  for (const auto &loop : loops) {
    if (loop.empty())
      continue;
    const auto &s = loop.front().shape;
    const auto p = at(s, .5),
               d = s.kind == gsnap::ShapeKind::Segment
                       ? point(s.b) - point(s.a)
                       : XY{-std::sin((s.kind == gsnap::ShapeKind::Circle
                                           ? 0
                                           : s.start_angle) +
                                      sweep(s) / 2),
                            std::cos((s.kind == gsnap::ShapeKind::Circle
                                          ? 0
                                          : s.start_angle) +
                                     sweep(s) / 2)};
    const Real delta =
        std::max(Real(.125), std::max(std::abs(p.x), std::abs(p.y)) *
                                 std::numeric_limits<Real>::epsilon() * 8);
    const auto n = XY{-d.y, d.x} * (delta / length(d));
    const bool left = inside(*this, p + n);
    const bool interior_left = signed_area(loop) > 0;
    result += (left == (!loop.front().reversed) ? (interior_left ? 1 : -1)
                                                : (interior_left ? -1 : 1)) *
              std::abs(signed_area(loop));
  }
  return double(result * Real(v.inv_scale()) * v.inv_scale());
}
Result from_shapes(const std::vector<gsnap::Shape> &shapes) {
  if (shapes.empty())
    return {false, "select closed boundaries for a region", {}};
  std::vector<Edge> edges;
  for (auto s : shapes) {
    if (const auto error = invalid_edge(s); !error.empty())
      return {false, error, {}};
    edges.push_back({std::move(s), false});
  }
  auto result = assemble(std::move(edges), false);
  if (!result.ok)
    return result;
  for (std::size_t l = 0; l < result.region.loops.size(); ++l)
    for (std::size_t i = 0; i < result.region.loops[l].size(); ++i)
      for (std::size_t m = l; m < result.region.loops.size(); ++m)
        for (std::size_t j = m == l ? i + 1 : 0;
             j < result.region.loops[m].size(); ++j) {
          const auto &a = result.region.loops[l][i];
          const auto &b = result.region.loops[m][j];
          for (auto p : intersections(a.shape, b.shape)) {
            const bool adjacent =
                l == m && (j == i + 1 ||
                           (i == 0 && j + 1 == result.region.loops[l].size()));
            if (!adjacent ||
                (!near(p, at(a.shape, 0)) && !near(p, at(a.shape, 1))) ||
                (!near(p, at(b.shape, 0)) && !near(p, at(b.shape, 1))))
              return {false, "region boundaries cross or touch", {}};
          }
        }
  return result;
}
Result from_members(const std::vector<Shape> &shapes) {
  if (shapes.empty() || shapes.size() > 4096)
    return {false, "invalid region boundary count", {}};
  std::vector<const Shape *> ordered;
  for (const auto &sh : shapes) {
    if (!sh.region || !sh.poly || sh.poly != shapes.front().poly)
      return {false, "mixed region membership", {}};
    if (const auto error = invalid_edge(sh); !error.empty())
      return {false, error, {}};
    ordered.push_back(&sh);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](auto a, auto b) { return a->poly_index < b->poly_index; });
  Region region;
  for (std::size_t n = 0; n < ordered.size(); ++n) {
    const auto &sh = *ordered[n];
    if (sh.poly_index != n || sh.region_loop > region.loops.size())
      return {false, "invalid region boundary order", {}};
    if (sh.region_loop == region.loops.size())
      region.loops.emplace_back();
    if (sh.region_loop + 1 != region.loops.size())
      return {false, "interleaved region loops", {}};
    region.loops.back().push_back({sh, sh.region_reversed});
  }
  for (const auto &loop : region.loops) {
    for (std::size_t i = 0; i < loop.size(); ++i) {
      const auto end = endpoints(loop[i]).second;
      const auto start = endpoints(loop[(i + 1) % loop.size()]).first;
      if (!near(point(end), point(start)))
        return {false, "open region boundary", {}};
    }
    if (std::abs(signed_area(loop)) < 1)
      return {false, "region boundary has zero area", {}};
  }
  for (std::size_t i = 0; i < ordered.size(); ++i)
    for (std::size_t j = i + 1; j < ordered.size(); ++j) {
      const auto &a = *ordered[i];
      const auto &b = *ordered[j];
      for (auto p : intersections(a, b))
        if ((!near(p, at(a, 0)) && !near(p, at(a, 1))) ||
            (!near(p, at(b, 0)) && !near(p, at(b, 1))))
          return {false, "region boundaries cross or overlap", {}};
      const bool lines = a.kind == gsnap::ShapeKind::Segment &&
                         b.kind == gsnap::ShapeKind::Segment;
      const bool same_support =
          lines
              ? cross(point(a.b) - point(a.a), point(b.b) - point(b.a)) == 0 &&
                    cross(point(b.a) - point(a.a), point(a.b) - point(a.a)) == 0
              : a.is_curve() && b.is_curve() && a.a.x == b.a.x &&
                    a.a.y == b.a.y && a.radius == b.radius;
      if (same_support && (on(a, at(b, .5)) || on(b, at(a, .5))))
        return {false, "overlapping region boundaries", {}};
    }
  return {true, {}, std::move(region)};
}
void reconcile_joints(std::vector<Shape> &shapes) {
  std::map<gcm::u32, std::vector<Shape *>> groups;
  for (auto &shape : shapes)
    if (shape.region && shape.poly)
      groups[shape.poly].push_back(&shape);
  for (auto &[id, group] : groups) {
    std::sort(group.begin(), group.end(),
              [](auto a, auto b) { return a->poly_index < b->poly_index; });
    bool complete = true;
    for (std::size_t n = 0; n < group.size(); ++n)
      if (group[n]->poly_index != n || !invalid_edge(*group[n]).empty())
        complete = false;
    if (!complete)
      continue;
    for (std::size_t from = 0; from < group.size();) {
      auto to = from + 1;
      while (to < group.size() &&
             group[to]->region_loop == group[from]->region_loop)
        ++to;
      const auto set_angle = [](Shape &sh, bool end, XY p) {
        const auto d = p - point(sh.a);
        Real value = std::atan2(d.y, d.x);
        auto &target = end ? sh.end_angle : sh.start_angle;
        value += tau * std::round((Real(target) - value) / tau);
        target = double(value);
      };
      if (to - from > 1)
        for (auto n = from; n < to; ++n) {
          auto &a = *group[n];
          auto &b = *group[n + 1 < to ? n + 1 : from];
          if (a.kind != gsnap::ShapeKind::Arc ||
              b.kind != gsnap::ShapeKind::Arc)
            continue;
          const auto pa = at(a, a.region_reversed ? 0 : 1),
                     pb = at(b, b.region_reversed ? 1 : 0);
          if (near(pa, pb))
            continue;
          const auto expected = (pa + pb) * .5L;
          XY joint = expected;
          if (a.a.x != b.a.x || a.a.y != b.a.y || a.radius != b.radius) {
            auto ca = a, cb = b;
            ca.kind = cb.kind = gsnap::ShapeKind::Circle;
            const auto candidates = intersections(ca, cb);
            if (candidates.empty())
              continue;
            joint = *std::min_element(
                candidates.begin(), candidates.end(), [&](XY x, XY y) {
                  return length(x - expected) < length(y - expected);
                });
          }
          set_angle(a, !a.region_reversed, joint);
          set_angle(b, b.region_reversed, joint);
        }
      for (auto n = from; n < to; ++n) {
        auto &s = *group[n];
        if (s.kind != gsnap::ShapeKind::Segment)
          continue;
        const auto &prev = *group[n > from ? n - 1 : to - 1];
        const auto &next = *group[n + 1 < to ? n + 1 : from];
        if (prev.is_curve())
          (s.region_reversed ? s.b : s.a) =
              endpoints({prev, prev.region_reversed}).second;
        if (next.is_curve())
          (s.region_reversed ? s.a : s.b) =
              endpoints({next, next.region_reversed}).first;
      }
      from = to;
    }
  }
}
std::string validate_members(const std::vector<Shape> &shapes) {
  std::map<gcm::u32, std::vector<Shape>> groups;
  for (const auto &sh : shapes) {
    if (sh.region && !sh.poly)
      return "region without compound identity";
    if (sh.region)
      groups[sh.poly];
  }
  for (const auto &sh : shapes)
    if (auto it = groups.find(sh.poly); it != groups.end())
      it->second.push_back(sh);
  for (const auto &[id, group] : groups) {
    const auto result = from_members(group);
    if (!result.ok)
      return result.error;
  }
  return {};
}
Result boolean_op(const Region &a, const Region &b, Operation operation) {
  const auto evaluate = [&](XY p) {
    const bool x = inside(a, p), y = inside(b, p);
    return operation == Operation::Union       ? x || y
           : operation == Operation::Intersect ? x && y
                                               : x && !y;
  };
  std::vector<Shape> shapes;
  for (const auto *r : {&a, &b})
    for (const auto &loop : r->loops)
      for (const auto &e : loop)
        shapes.push_back(e.shape);
  if (shapes.size() > 4096)
    return {false, "region boolean exceeds 4096 boundary edges", {}};
  std::vector<Edge> boundary;
  for (std::size_t i = 0; i < shapes.size(); ++i) {
    const auto &s = shapes[i];
    std::vector<Real> cuts{0, 1};
    for (std::size_t j = 0; j < shapes.size(); ++j)
      if (i != j)
        for (auto p : intersections(s, shapes[j])) {
          auto t = parameter(s, p);
          if (t > 1e-12L && t < 1 - 1e-12L)
            cuts.push_back(t);
        }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(
        std::unique(cuts.begin(), cuts.end(),
                    [](Real x, Real y) { return std::abs(x - y) < 1e-12L; }),
        cuts.end());
    for (std::size_t k = 1; k < cuts.size(); ++k) {
      auto fragment = portion(s, cuts[k - 1], cuts[k]);
      // Classify on the original support: rounded endpoints can cross the probe.
      const Real midpoint = (cuts[k - 1] + cuts[k]) * .5L;
      const auto p = at(s, midpoint);
      XY tangent =
          fragment.kind == gsnap::ShapeKind::Segment
              ? point(s.b) - point(s.a)
              : XY{-std::sin((s.kind == gsnap::ShapeKind::Circle ? 0 : s.start_angle) + sweep(s) * midpoint),
                   std::cos((s.kind == gsnap::ShapeKind::Circle ? 0 : s.start_angle) + sweep(s) * midpoint)};
      const Real magnitude = length(tangent);
      if (magnitude == 0 || (fragment.kind == gsnap::ShapeKind::Segment &&
          fragment.a.x == fragment.b.x && fragment.a.y == fragment.b.y))
        continue;
      const Real delta =
          std::max(Real(.125), std::max(std::abs(p.x), std::abs(p.y)) *
                                   std::numeric_limits<Real>::epsilon() * 8);
      const auto normal = XY{-tangent.y, tangent.x} * (delta / magnitude);
      const bool left = evaluate(p + normal), right = evaluate(p - normal);
      if (left == right)
        continue;
      Edge edge{std::move(fragment), !left};
      const auto endpoints_of_edge = endpoints(edge);
      const bool duplicate =
          std::any_of(boundary.begin(), boundary.end(), [&](const Edge &old) {
            const auto ends = endpoints(old);
            return near(point(ends.first), point(endpoints_of_edge.first)) &&
                   near(point(ends.second), point(endpoints_of_edge.second)) &&
                   near(at(old.shape, .5), p);
          });
      if (!duplicate)
        boundary.push_back(std::move(edge));
    }
  }
  return assemble(std::move(boundary), true);
}
}
