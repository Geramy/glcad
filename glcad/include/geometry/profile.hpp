#pragma once

#include <vector>

#include "math/bigint.hpp"
#include "math/fixed.hpp"
#include "math/predicate.hpp"

namespace gtopo {

using Loop2 = std::vector<gcm::Lattice2>;

// Twice the signed area, exact (256-bit accumulation).
gcm::Sign signed_area_sign(const Loop2& loop);
gcm::SInt<256> signed_area_2x(const Loop2& loop);

struct Profile {
    Loop2              outer;
    std::vector<Loop2> holes;

    // Outer CCW, holes CW.
    void normalize();

    bool valid() const;
    std::size_t total_vertices() const;
};

}
