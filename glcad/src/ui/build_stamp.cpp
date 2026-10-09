#include "ui/build_stamp.hpp"

// Included only here so the per-build stamp does not force other rebuilds.
#include "build_stamp.h"

namespace gcapp {

const char* build_stamp() noexcept {
    return GLCAD_BUILD_HASH ", built " GLCAD_BUILD_TIME;
}

}
