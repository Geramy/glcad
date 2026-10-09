#pragma once

#include <cmath>

#include "math/config.hpp"

namespace gcm {

struct Lattice2 { i64 x{}, y{}; };
struct Lattice3 { i64 x{}, y{}, z{}; };

// Default 32 bits: 2.3 um over a 10 km volume, 15 bits of DF64 margin.
struct WorkingVolume {
    f64 cx{0.0}, cy{0.0}, cz{0.0};
    f64 extent{10000.0};
    int resolution_bits{32};

    f64 scale() const noexcept {
        return static_cast<f64>(i64{1} << resolution_bits) / extent;
    }
    f64 inv_scale() const noexcept { return extent / static_cast<f64>(i64{1} << resolution_bits); }

    f64 half_cell() const noexcept { return 0.5 * inv_scale(); }

    i64 max_coord() const noexcept { return i64{1} << resolution_bits; }
};

inline i64 snap_axis(f64 world, f64 centre, f64 scale) noexcept {
    return static_cast<i64>(std::llround((world - centre) * scale));
}

inline Lattice2 to_lattice(const WorkingVolume& v, f64 x, f64 y) noexcept {
    const f64 s = v.scale();
    return {snap_axis(x, v.cx, s), snap_axis(y, v.cy, s)};
}

inline Lattice3 to_lattice(const WorkingVolume& v, f64 x, f64 y, f64 z) noexcept {
    const f64 s = v.scale();
    return {snap_axis(x, v.cx, s), snap_axis(y, v.cy, s), snap_axis(z, v.cz, s)};
}

inline void to_world(const WorkingVolume& v, const Lattice2& p, f64& x, f64& y) noexcept {
    const f64 s = v.inv_scale();
    x = v.cx + static_cast<f64>(p.x) * s;
    y = v.cy + static_cast<f64>(p.y) * s;
}

inline void to_world(const WorkingVolume& v, const Lattice3& p, f64& x, f64& y, f64& z) noexcept {
    const f64 s = v.inv_scale();
    x = v.cx + static_cast<f64>(p.x) * s;
    y = v.cy + static_cast<f64>(p.y) * s;
    z = v.cz + static_cast<f64>(p.z) * s;
}

// Band sized from the weakest backend so every backend escalates identically.
inline bool in_ambiguity_band(f64 lattice_value, f64 band) noexcept {
    // llround flips at half-integers, not integers.
    const f64 frac = lattice_value - std::floor(lattice_value);
    return std::fabs(frac - 0.5) < band;
}

inline f64 ambiguity_band(int mantissa_bits, i64 max_coord) noexcept {
    const f64 relative = std::ldexp(1.0, -mantissa_bits);
    return 4.0 * relative * static_cast<f64>(max_coord);   // 4x safety factor
}

}
