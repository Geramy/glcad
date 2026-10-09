#pragma once

#include <vector>

#include "math/config.hpp"

namespace gcapp {

class Shell;

std::vector<gcm::u8> snapshot_png(Shell& shell, int width, int height);

}
