#pragma once

#include <array>
#include <cmath>

#include "geometry/body.hpp"
#include "math/bigint.hpp"

namespace gtopo::detail {

// Sum before converting to float to avoid cancellation in thin polygons.
inline std::array<gcm::SInt<256>, 3> loop_area(const Body& body, LoopId loop) {
    std::array<gcm::SInt<256>, 3> area{};
    const auto begin = body.loop_begin(loop), end = body.loop_end(loop);
    if (end - begin < 3) return area;
    const auto origin = body.vertex_point[body.he_vertex[begin]];
    for (auto h = begin; h < end; ++h) {
        const auto p = body.vertex_point[body.he_vertex[h]];
        const auto q = body.vertex_point[body.he_vertex[body.he_next[h]]];
        const gcm::i64 u[3] = {p.x-origin.x, p.y-origin.y, p.z-origin.z};
        const gcm::i64 v[3] = {q.x-origin.x, q.y-origin.y, q.z-origin.z};
        for (int k = 0; k < 3; ++k) {
            const int i = (k+1)%3, j = (k+2)%3;
            area[k] = gcm::add(area[k], gcm::sub(gcm::mul_i64_i64<256>(u[i],v[j]),
                                               gcm::mul_i64_i64<256>(u[j],v[i])));
        }
    }
    return area;
}

inline double approximate_area(gcm::SInt<256> area) {
    const bool negative = area.is_negative();
    if (negative) area = gcm::negate(area);
    double value = 0;
    for (int k = 3; k >= 0; --k) value = std::ldexp(value,64) + double(area.l[k]);
    return negative ? -value : value;
}

}
