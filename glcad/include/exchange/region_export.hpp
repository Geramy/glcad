#pragma once

#include "exchange/dxf.hpp"

namespace gexchange {
bool prepare_region_boundaries(gdxf::Document& document, std::string& error);
}
