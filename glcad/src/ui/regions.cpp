#include "ui/canvas.hpp"
#include <map>
#include <set>

namespace gcapp {
gregion::Region Canvas::region_at(std::size_t index) const {
  if (!is_region(index))
    return {};
  std::map<gcm::u32, gregion::Loop> loops;
  for (auto i : polyline_pieces(index))
    loops[shapes_[i].region_loop].push_back(
        {shapes_[i], shapes_[i].region_reversed});
  gregion::Region out;
  for (auto &[id, loop] : loops)
    out.loops.push_back(std::move(loop));
  return out;
}

void Canvas::expand_region_selection(std::vector<std::size_t> &which) const {
  std::set<gcm::u32> compounds;
  std::set<std::size_t> members(which.begin(), which.end());
  for (auto i : which)
    if (is_region(i))
      compounds.insert(shapes_[i].poly);
  if (compounds.empty())
    return;
  for (std::size_t i = 0; i < shapes_.size(); ++i)
    if (shapes_[i].region && compounds.contains(shapes_[i].poly) &&
        members.insert(i).second)
      which.push_back(i);
}

void Canvas::store_region(const gregion::Region &region,
                          const std::vector<std::size_t> &replace,
                          const gdoc::Attributes &attributes,
                          const std::string &layer, gcm::u32 identity) {
  auto saved_attributes = attributes;
  saved_attributes.user.erase("revision_cloud");
  const auto saved_layer = layer;
  std::set<gcm::u32> removed;
  for (auto i : replace)
    if (i < shapes_.size())
      removed.insert(shapes_[i].key);
  mark();
  clear_selection();
  shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(),
                               [&](const auto &shape) {
                                 return removed.contains(shape.key);
                               }),
                shapes_.end());
  const auto id = identity ? identity : next_polyline_id();
  gcm::u32 order = 0;
  for (gcm::u32 l = 0; l < region.loops.size(); ++l)
    for (const auto &edge : region.loops[l]) {
      auto shape = edge.shape;
      shape.key = 0;
      shape.id = 0;
      shape.poly = id;
      shape.poly_index = order++;
      shape.region = true;
      shape.region_loop = l;
      shape.region_reversed = edge.reversed;
      shape.attrs = saved_attributes;
      shape.layer = saved_layer;
      selection_.push_back(shapes_.size());
      shapes_.push_back(std::move(shape));
    }
  assign_keys();
  note_polyline_ids();
  prune_constraints();
  refresh_dimensions();
  align_dirty_ = true;
  rebuild_grips();
  refresh_hatches();
}

bool Canvas::create_region(const std::vector<gsnap::Shape> &boundaries,
                           const std::vector<std::size_t> &replace) {
  const auto result = gregion::from_shapes(boundaries);
  if (!result.ok) {
    message_ = "REGION: " + result.error;
    return false;
  }
  const auto attributes =
      boundaries.empty() ? current_attributes : boundaries.front().attrs;
  const auto layer =
      boundaries.empty() ? current_layer_ : boundaries.front().layer;
  store_region(result.region, replace, attributes,
               layer.empty() ? current_layer_ : layer);
  message_ = "created region with " +
             std::to_string(result.region.loops.size()) + " boundary loop(s)";
  return true;
}

bool Canvas::combine_regions(gregion::Operation operation) {
  std::vector<std::size_t> roots, replace;
  std::set<gcm::u32> seen;
  if (!sel_texts_.empty() || !sel_dims_.empty() || !sel_refs_.empty()) {
    message_ = "select only region entities";
    return false;
  }
  for (auto i : selection_) {
    if (!is_region(i)) {
      message_ = "select only region entities";
      return false;
    }
    if (seen.insert(shapes_[i].poly).second) {
      roots.push_back(i);
      const auto members = polyline_pieces(i);
      replace.insert(replace.end(), members.begin(), members.end());
    }
  }
  if (roots.size() < 2) {
    message_ = "select at least two region entities; the first is the "
               "subtraction base";
    return false;
  }
  for (auto root : roots) {
    std::vector<gsnap::Shape> members;
    for (auto i : polyline_pieces(root))
      members.push_back(shapes_[i]);
    const auto valid = gregion::from_members(members);
    if (!valid.ok) {
      message_ = "invalid region: " + valid.error;
      return false;
    }
  }
  auto result = region_at(roots.front());
  for (std::size_t n = 1; n < roots.size(); ++n) {
    auto combined = gregion::boolean_op(result, region_at(roots[n]), operation);
    if (!combined.ok) {
      message_ = "region boolean: " + combined.error;
      return false;
    }
    result = std::move(combined.region);
  }
  const auto first = shapes_[roots.front()];
  store_region(result, replace, first.attrs, first.layer, first.poly);
  message_ = result.empty() ? "region boolean removed all material"
                            : "region boolean complete";
  return true;
}
}
