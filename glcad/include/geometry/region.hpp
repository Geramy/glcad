#pragma once

#include "geometry/snap.hpp"
#include <string>
#include <vector>

namespace gregion {
struct Edge {
  gsnap::Shape shape;
  bool reversed{false};
};
using Loop = std::vector<Edge>;
struct Region {
  std::vector<Loop> loops;
  bool empty() const noexcept { return loops.empty(); }
  double area(const gcm::WorkingVolume &volume) const;
  bool contains(gcm::Lattice2 point) const;
};
enum class Operation { Union, Subtract, Intersect };
struct Result {
  bool ok{false};
  std::string error;
  Region region;
};
Result from_shapes(const std::vector<gsnap::Shape> &shapes);
Result from_members(const std::vector<gsnap::Shape> &shapes);
void reconcile_joints(std::vector<gsnap::Shape> &shapes);
std::string validate_members(const std::vector<gsnap::Shape> &shapes);
Result boolean_op(const Region &a, const Region &b, Operation operation);
std::pair<gcm::Lattice2, gcm::Lattice2> endpoints(const Edge &edge);
}
