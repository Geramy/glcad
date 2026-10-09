#include "geometry/region.hpp"
#include "test.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
gcm::WorkingVolume volume() {
  gcm::WorkingVolume v;
  v.extent = 100;
  v.resolution_bits = 40;
  return v;
}
std::vector<gsnap::Shape> rectangle(double x0, double y0, double x1,
                                    double y1) {
  const auto v = volume();
  const gcm::Lattice2 points[] = {
      gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
      gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
  std::vector<gsnap::Shape> out;
  for (int i = 0; i < 4; ++i) {
    gsnap::Shape s;
    s.a = points[i];
    s.b = points[(i + 1) % 4];
    out.push_back(s);
  }
  return out;
}
gsnap::Shape circle(double x, double y, double radius) {
  gsnap::Shape s;
  s.kind = gsnap::ShapeKind::Circle;
  s.a = gcm::to_lattice(volume(), x, y);
  s.radius = gcm::i64(std::llround(radius * volume().scale()));
  return s;
}
}
TEST(
    region_assembles_reversed_edges_and_rejects_open_crossed_or_branched_boundaries) {
  auto pieces = rectangle(0, 0, 4, 3);
  std::swap(pieces[0].a, pieces[0].b);
  std::reverse(pieces.begin(), pieces.end());
  auto r = gregion::from_shapes(pieces);
  CHECK_MSG(r.ok, "%s", r.error.c_str());
  CHECK(r.region.loops.size() == 1);
  CHECK(std::abs(r.region.area(volume()) - 12) < 1e-7);
  CHECK(r.region.contains(gcm::to_lattice(volume(), 2, 1)));
  CHECK(!r.region.contains(gcm::to_lattice(volume(), 5, 1)));
  pieces.pop_back();
  CHECK(!gregion::from_shapes(pieces).ok);
  auto crossed = rectangle(0, 0, 4, 3);
  std::swap(crossed[0].b, crossed[2].b);
  CHECK(!gregion::from_shapes(crossed).ok);
  auto branch = rectangle(0, 0, 4, 3);
  branch.push_back(branch.front());
  CHECK(!gregion::from_shapes(branch).ok);
}
TEST(
    region_line_booleans_preserve_holes_empty_results_and_disconnected_components) {
  const auto a = gregion::from_shapes(rectangle(0, 0, 4, 4));
  const auto b = gregion::from_shapes(rectangle(2, 0, 6, 4));
  for (auto op : {gregion::Operation::Union, gregion::Operation::Intersect,
                  gregion::Operation::Subtract}) {
    const auto r = gregion::boolean_op(a.region, b.region, op);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(std::abs(r.region.area(volume()) -
                   (op == gregion::Operation::Union ? 24 : 8)) < 1e-6);
  }
  const auto hole = gregion::from_shapes(rectangle(1, 1, 3, 3));
  const auto cut =
      gregion::boolean_op(a.region, hole.region, gregion::Operation::Subtract);
  CHECK_MSG(cut.ok, "%s", cut.error.c_str());
  CHECK(cut.region.loops.size() == 2);
  CHECK(std::abs(cut.region.area(volume()) - 12) < 1e-6);
  CHECK(!cut.region.contains(gcm::to_lattice(volume(), 2, 2)));
  CHECK(cut.region.contains(gcm::to_lattice(volume(), .5, .5)));
  const auto other = gregion::from_shapes(rectangle(8, 0, 10, 2));
  const auto joined =
      gregion::boolean_op(cut.region, other.region, gregion::Operation::Union);
  CHECK_MSG(joined.ok, "%s", joined.error.c_str());
  CHECK(joined.region.loops.size() == 3);
  CHECK(std::abs(joined.region.area(volume()) - 16) < 1e-6);
  const auto empty =
      gregion::boolean_op(a.region, a.region, gregion::Operation::Subtract);
  CHECK(empty.ok);
  CHECK(empty.region.empty());
  const auto same =
      gregion::boolean_op(a.region, a.region, gregion::Operation::Union);
  CHECK(same.ok);
  CHECK(std::abs(same.region.area(volume()) - 16) < 1e-6);
}
TEST(region_rotated_wall_booleans_keep_split_edges_and_exactly_shared_vertices) {
  for (double angle : {.17, .7, 1.11}) {
    const auto rotate = [&](std::vector<gsnap::Shape> edges) {
      for (auto &edge : edges) for (auto *p : {&edge.a, &edge.b}) {
        double x, y; gcm::to_world(volume(), *p, x, y);
        *p = gcm::to_lattice(volume(), x * std::cos(angle) - y * std::sin(angle),
                            x * std::sin(angle) + y * std::cos(angle));
      }
      return gregion::from_shapes(edges);
    };
    const auto a = rotate(rectangle(0, -.1, 6, .1));
    const auto b = rotate(rectangle(5.9, -.1, 6.1, 4));
    CHECK(a.ok); CHECK(b.ok);
    for (auto op : {gregion::Operation::Union, gregion::Operation::Intersect,
                    gregion::Operation::Subtract}) {
      const auto result = gregion::boolean_op(a.region, b.region, op);
      CHECK_MSG(result.ok, "angle=%g operation=%d: %s", angle, int(op), result.error.c_str());
      if (!result.ok) continue;
      const double expected = op == gregion::Operation::Union ? 2.0 :
                              op == gregion::Operation::Intersect ? .02 : 1.18;
      CHECK(std::abs(result.region.area(volume()) - expected) < 1e-7);
      for (const auto &loop : result.region.loops) for (std::size_t i = 0; i < loop.size(); ++i) {
        const auto end = gregion::endpoints(loop[i]).second;
        const auto start = gregion::endpoints(loop[(i + 1) % loop.size()]).first;
        CHECK(end.x == start.x && end.y == start.y);
      }
    }
  }
}

TEST(region_circle_booleans_keep_analytic_arcs_and_area) {
  const double pi = std::acos(-1.0);
  auto a = gregion::from_shapes({circle(0, 0, 2)}),
       b = gregion::from_shapes({circle(2, 0, 2)});
  CHECK(a.ok);
  CHECK(std::abs(a.region.area(volume()) - 4 * pi) < 1e-6);
  const auto overlap =
      gregion::boolean_op(a.region, b.region, gregion::Operation::Intersect);
  CHECK_MSG(overlap.ok, "%s", overlap.error.c_str());
  CHECK(overlap.region.loops.size() == 1);
  const double lens = 8 * pi / 3 - 2 * std::sqrt(3.0);
  CHECK(std::abs(overlap.region.area(volume()) - lens) < 1e-6);
  for (const auto &loop : overlap.region.loops)
    for (const auto &edge : loop)
      CHECK(edge.shape.kind == gsnap::ShapeKind::Arc);
  const auto ring = gregion::boolean_op(
      a.region, gregion::from_shapes({circle(0, 0, 1)}).region,
      gregion::Operation::Subtract);
  CHECK_MSG(ring.ok, "%s", ring.error.c_str());
  CHECK(ring.region.loops.size() == 2);
  CHECK(std::abs(ring.region.area(volume()) - 3 * pi) < 1e-6);
  const auto half = gregion::boolean_op(
      a.region, gregion::from_shapes(rectangle(0, -3, 3, 3)).region,
      gregion::Operation::Intersect);
  CHECK_MSG(half.ok, "%s", half.error.c_str());
  CHECK(std::abs(half.region.area(volume()) - 2 * pi) < 1e-6);
}
TEST(region_booleans_handle_shared_edges_tangent_components_and_thin_material) {
  const auto a = gregion::from_shapes(rectangle(0, 0, 4, 4));
  const auto touching = gregion::from_shapes(rectangle(4, 0, 8, 4));
  const auto merged =
      gregion::boolean_op(a.region, touching.region, gregion::Operation::Union);
  CHECK_MSG(merged.ok, "%s", merged.error.c_str());
  CHECK(merged.region.loops.size() == 1);
  CHECK(std::abs(merged.region.area(volume()) - 32) < 1e-6);
  const auto corner = gregion::from_shapes(rectangle(4, 4, 6, 6));
  const auto joined =
      gregion::boolean_op(a.region, corner.region, gregion::Operation::Union);
  CHECK_MSG(joined.ok, "%s", joined.error.c_str());
  CHECK(std::abs(joined.region.area(volume()) - 20) < 1e-6);
  const auto tangent =
      gregion::boolean_op(gregion::from_shapes({circle(0, 0, 1)}).region,
                          gregion::from_shapes({circle(2, 0, 1)}).region,
                          gregion::Operation::Union);
  CHECK_MSG(tangent.ok, "%s", tangent.error.c_str());
  CHECK(std::abs(tangent.region.area(volume()) - 2 * std::acos(-1.0)) < 1e-6);
  const auto thin = gregion::from_shapes(rectangle(0, 0, 80, 1e-7));
  CHECK(thin.ok);
  CHECK(std::abs(thin.region.area(volume()) - 8e-6) < 1e-8);
  auto rings = rectangle(0, 0, 5, 5), inner = rectangle(1, 1, 4, 4);
  std::reverse(inner.begin(), inner.end());
  for (auto &edge : inner)
    std::swap(edge.a, edge.b);
  rings.insert(rings.end(), inner.begin(), inner.end());
  const auto reversed = gregion::from_shapes(rings);
  CHECK(reversed.ok);
  CHECK(std::abs(reversed.region.area(volume()) - 16) < 1e-6);
}
TEST(region_rectangle_boolean_areas_match_independent_overlap_formula) {
  for (int x = -3; x <= 5; ++x)
    for (int y = -3; y <= 5; ++y) {
      const auto a = gregion::from_shapes(rectangle(0, 0, 4, 3));
      const auto b = gregion::from_shapes(rectangle(x, y, x + 2, y + 2));
      const double overlap = std::max(0, std::min(4, x + 2) - std::max(0, x)) *
                             std::max(0, std::min(3, y + 2) - std::max(0, y));
      for (auto op : {gregion::Operation::Union, gregion::Operation::Subtract,
                      gregion::Operation::Intersect}) {
        const double expected = op == gregion::Operation::Union ? 16 - overlap
                                : op == gregion::Operation::Subtract
                                    ? 12 - overlap
                                    : overlap;
        const auto result = gregion::boolean_op(a.region, b.region, op);
        CHECK_MSG(result.ok, "offset %d,%d: %s", x, y, result.error.c_str());
        CHECK_MSG(std::abs(result.region.area(volume()) - expected) < 1e-6,
                  "offset %d,%d: area %.12g expected %.12g", x, y,
                  result.region.area(volume()), expected);
      }
    }
}
TEST(region_curved_results_can_be_used_as_boolean_operands_again) {
  const auto a = gregion::from_shapes({circle(0, 0, 2)});
  for (double distance : {.25, 1.2, 2., 3.9, 4.}) {
    const auto b = gregion::from_shapes({circle(distance, 0, 2)});
    const auto difference =
        gregion::boolean_op(a.region, b.region, gregion::Operation::Subtract);
    const auto common =
        gregion::boolean_op(a.region, b.region, gregion::Operation::Intersect);
    CHECK_MSG(difference.ok, "%s", difference.error.c_str());
    CHECK_MSG(common.ok, "%s", common.error.c_str());
    const auto restored = gregion::boolean_op(difference.region, common.region,
                                              gregion::Operation::Union);
    CHECK_MSG(restored.ok, "distance %.12g: %s", distance,
              restored.error.c_str());
    CHECK(std::abs(restored.region.area(volume()) - 4 * std::acos(-1.0)) <
          1e-6);
  }
}

TEST(
    region_creation_rejects_curves_outside_the_lattice_or_with_nonfinite_angles) {
  auto invalid = circle(0, 0, 1);
  invalid.a.x = std::numeric_limits<gcm::i64>::max();
  CHECK(!gregion::from_shapes({invalid}).ok);
  invalid = circle(0, 0, 1);
  invalid.kind = gsnap::ShapeKind::Arc;
  invalid.start_angle = -std::numeric_limits<double>::max();
  invalid.end_angle = std::numeric_limits<double>::max();
  CHECK(!gregion::from_shapes({invalid}).ok);
  CHECK(!gregion::from_shapes({}).ok);
  auto boundary = rectangle(0, 0, 2, 2);
  boundary[0].a.x = std::numeric_limits<gcm::i64>::max();
  CHECK(!gregion::from_shapes(boundary).ok);
}

TEST(
    region_typed_boundaries_validate_boolean_contacts_and_reject_corrupt_topology) {
  const auto members = [](const gregion::Region &region) {
    std::vector<gsnap::Shape> out;
    for (gcm::u32 l = 0; l < region.loops.size(); ++l)
      for (const auto &edge : region.loops[l]) {
        auto s = edge.shape;
        s.poly = 1;
        s.poly_index = out.size();
        s.region = true;
        s.region_loop = l;
        s.region_reversed = edge.reversed;
        out.push_back(s);
      }
    return out;
  };
  for (double distance : {0.25, 2.0, 4.0, 5.0}) {
    const auto a = gregion::from_shapes({circle(0, 0, 2)}).region;
    const auto b = gregion::from_shapes({circle(distance, 0, 2)}).region;
    for (auto op : {gregion::Operation::Union, gregion::Operation::Subtract,
                    gregion::Operation::Intersect}) {
      const auto result = gregion::boolean_op(a, b, op);
      CHECK(result.ok);
      if (result.region.empty())
        continue;
      auto sh = members(result.region);
      std::reverse(sh.begin(), sh.end());
      const auto restored = gregion::from_members(sh);
      CHECK_MSG(restored.ok, "%g: %s", distance, restored.error.c_str());
      if (restored.ok)
        CHECK(std::abs(restored.region.area(volume()) -
                       result.region.area(volume())) < 1e-10);
    }
  }
  auto boundary = members(gregion::from_shapes(rectangle(0, 0, 2, 2)).region);
  auto broken = boundary;
  broken[0].b.x += 100;
  CHECK(!gregion::from_members(broken).ok);
  broken = boundary;
  broken[0].poly_index = 1;
  CHECK(!gregion::from_members(broken).ok);
  broken = boundary;
  broken[0].region_loop = 2;
  CHECK(!gregion::from_members(broken).ok);
  broken = boundary;
  broken[0].region_reversed = true;
  CHECK(!gregion::from_members(broken).ok);
  broken = boundary;
  broken[0].region = false;
  CHECK(!gregion::validate_members(broken).empty());
}
