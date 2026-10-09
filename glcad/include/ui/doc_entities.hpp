#pragma once

#include <vector>

#include "engine/entity.hpp"
#include "engine/hatch.hpp"
#include "geometry/dimension.hpp"
#include "geometry/nurbs.hpp"
#include "ui/canvas.hpp"

namespace gcapp {

void append_entities(const std::vector<gdim::Dimension>& dims,
                     const std::vector<gdoc::AssociativeHatch>& hatches,
                     const std::vector<ggeo::NurbsCurve2<Scalar>>& splines,
                     const std::vector<Canvas::StoredConstraint>& constraints,
                     std::vector<gdoc::Entity>& out);

void read_entities(const std::vector<gdoc::Entity>& in,
                   std::vector<gdim::Dimension>& dims,
                   std::vector<gdoc::AssociativeHatch>& hatches,
                   std::vector<ggeo::NurbsCurve2<Scalar>>& splines,
                   std::vector<Canvas::StoredConstraint>& constraints);

void append_text_entities(const std::vector<Canvas::TextItem>& texts,
                          gdoc::SpaceId space, std::vector<gdoc::Entity>& out);
void read_text_entities(const std::vector<gdoc::Entity>& in, gdoc::SpaceId space,
                        std::vector<Canvas::TextItem>& texts);

}
