#include "geometry/profile.hpp"

#include <algorithm>

namespace gtopo {

gcm::SInt<256> signed_area_2x(const Loop2& loop) {
    gcm::SInt<256> acc;
    const std::size_t n = loop.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto& a = loop[i];
        const auto& b = loop[(i + 1) % n];
        acc = gcm::add(acc, gcm::mul_i64_i64<256>(a.x, b.y));
        acc = gcm::sub(acc, gcm::mul_i64_i64<256>(a.y, b.x));
    }
    return acc;
}

gcm::Sign signed_area_sign(const Loop2& loop) {
    if (loop.size() < 3) return gcm::Sign::Zero;
    return signed_area_2x(loop).sign();
}

void Profile::normalize() {
    if (signed_area_sign(outer) == gcm::Sign::Negative)
        std::reverse(outer.begin(), outer.end());
    for (auto& h : holes)
        if (signed_area_sign(h) == gcm::Sign::Positive)
            std::reverse(h.begin(), h.end());
}

bool Profile::valid() const {
    if (outer.size() < 3) return false;
    if (signed_area_sign(outer) != gcm::Sign::Positive) return false;
    for (const auto& h : holes) {
        if (h.size() < 3) return false;
        if (signed_area_sign(h) != gcm::Sign::Negative) return false;
    }
    return true;
}

std::size_t Profile::total_vertices() const {
    std::size_t n = outer.size();
    for (const auto& h : holes) n += h.size();
    return n;
}

}
