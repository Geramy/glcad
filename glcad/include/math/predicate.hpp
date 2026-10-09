#pragma once

#include "math/bigint.hpp"
#include "math/config.hpp"
#include "math/fixed.hpp"
#include "math/platform.hpp"
#include "math/scalar.hpp"

namespace gcm::predicate {

struct Stats {
    u64 calls{0};
    u64 exact_fallbacks{0};

    void reset() noexcept { calls = 0; exact_fallbacks = 0; }
    f64 fallback_rate() const noexcept {
        return calls ? static_cast<f64>(exact_fallbacks) / static_cast<f64>(calls) : 0.0;
    }
};

inline Stats& stats() noexcept {
    static thread_local Stats s;
    return s;
}

template <FilterScalar Filter, class Limb = u64>
struct Predicates {
    using FilterScalarType = Filter;
    using LimbType         = Limb;

    // Shewchuk's static filter bounds, from the scalar's declared epsilon.
    static constexpr f64 eps       = Filter::filter_eps;
    static constexpr f64 ccw_bound = (3.0 + 16.0 * eps) * eps;
    static constexpr f64 o3d_bound = (7.0 + 56.0 * eps) * eps;

    // Differences must be exact in the filter type: L-bit lattice gives L+1 bits.
    static constexpr int max_lattice_bits = Filter::mantissa_bits - 1;

    static constexpr bool supports(int resolution_bits) noexcept {
        return resolution_bits <= max_lattice_bits;
    }
    static bool supports(const WorkingVolume& v) noexcept {
        return supports(v.resolution_bits);
    }

    using Det2 = SInt<128, Limb>;   // orient2d: products of 2 differences
    using Det3 = SInt<256, Limb>;   // orient3d: products of 3, summed 6 times

    static Sign orient2d_exact(const Lattice2& a, const Lattice2& b,
                               const Lattice2& c) noexcept {
        const i64 adx = a.x - c.x, ady = a.y - c.y;
        const i64 bdx = b.x - c.x, bdy = b.y - c.y;
        return sub(mul_i64_i64<128, Limb>(adx, bdy),
                   mul_i64_i64<128, Limb>(ady, bdx)).sign();
    }

    static Sign orient2d(const Lattice2& a, const Lattice2& b,
                         const Lattice2& c) noexcept {
        Stats& st = stats();
        ++st.calls;

        const Filter adx = Filter::from(static_cast<f64>(a.x - c.x));
        const Filter ady = Filter::from(static_cast<f64>(a.y - c.y));
        const Filter bdx = Filter::from(static_cast<f64>(b.x - c.x));
        const Filter bdy = Filter::from(static_cast<f64>(b.y - c.y));

        const Filter left  = adx * bdy;
        const Filter right = ady * bdx;
        const Filter det   = left - right;
        const Filter err   = Filter::from(ccw_bound) *
                             (Filter::abs(left) + Filter::abs(right));

        if (det > err)              return Sign::Positive;
        if (Filter::neg(det) > err) return Sign::Negative;

        ++st.exact_fallbacks;
        return orient2d_exact(a, b, c);
    }

    static Sign orient3d_exact(const Lattice3& a, const Lattice3& b,
                               const Lattice3& c, const Lattice3& d) noexcept {
        const i64 adx = a.x - d.x, ady = a.y - d.y, adz = a.z - d.z;
        const i64 bdx = b.x - d.x, bdy = b.y - d.y, bdz = b.z - d.z;
        const i64 cdx = c.x - d.x, cdy = c.y - d.y, cdz = c.z - d.z;

        const Det3 m1 = sub(mul_i64_i64<256, Limb>(bdy, cdz),
                            mul_i64_i64<256, Limb>(bdz, cdy));
        const Det3 m2 = sub(mul_i64_i64<256, Limb>(bdx, cdz),
                            mul_i64_i64<256, Limb>(bdz, cdx));
        const Det3 m3 = sub(mul_i64_i64<256, Limb>(bdx, cdy),
                            mul_i64_i64<256, Limb>(bdy, cdx));

        Det3 det = mul_i64(m1, adx);
        det = sub(det, mul_i64(m2, ady));
        det = add(det, mul_i64(m3, adz));
        return det.sign();
    }

    static Sign orient3d(const Lattice3& a, const Lattice3& b,
                         const Lattice3& c, const Lattice3& d) noexcept {
        Stats& st = stats();
        ++st.calls;

        const Filter adx = Filter::from(static_cast<f64>(a.x - d.x));
        const Filter ady = Filter::from(static_cast<f64>(a.y - d.y));
        const Filter adz = Filter::from(static_cast<f64>(a.z - d.z));
        const Filter bdx = Filter::from(static_cast<f64>(b.x - d.x));
        const Filter bdy = Filter::from(static_cast<f64>(b.y - d.y));
        const Filter bdz = Filter::from(static_cast<f64>(b.z - d.z));
        const Filter cdx = Filter::from(static_cast<f64>(c.x - d.x));
        const Filter cdy = Filter::from(static_cast<f64>(c.y - d.y));
        const Filter cdz = Filter::from(static_cast<f64>(c.z - d.z));

        const Filter bdxcdy = bdx * cdy, cdxbdy = cdx * bdy;
        const Filter cdxady = cdx * ady, adxcdy = adx * cdy;
        const Filter adxbdy = adx * bdy, bdxady = bdx * ady;

        const Filter det = adz * (bdxcdy - cdxbdy) +
                           bdz * (cdxady - adxcdy) +
                           cdz * (adxbdy - bdxady);

        const Filter permanent =
            (Filter::abs(bdxcdy) + Filter::abs(cdxbdy)) * Filter::abs(adz) +
            (Filter::abs(cdxady) + Filter::abs(adxcdy)) * Filter::abs(bdz) +
            (Filter::abs(adxbdy) + Filter::abs(bdxady)) * Filter::abs(cdz);
        const Filter err = Filter::from(o3d_bound) * permanent;

        if (det > err)              return Sign::Positive;
        if (Filter::neg(det) > err) return Sign::Negative;

        ++st.exact_fallbacks;
        return orient3d_exact(a, b, c, d);
    }

    static bool collinear(const Lattice2& a, const Lattice2& b,
                          const Lattice2& c) noexcept {
        return orient2d(a, b, c) == Sign::Zero;
    }
    static bool coplanar(const Lattice3& a, const Lattice3& b,
                         const Lattice3& c, const Lattice3& d) noexcept {
        return orient3d(a, b, c, d) == Sign::Zero;
    }
    static bool segments_properly_intersect(const Lattice2& a, const Lattice2& b,
                                            const Lattice2& c, const Lattice2& d) noexcept {
        const Sign d1 = orient2d(a, b, c), d2 = orient2d(a, b, d);
        const Sign d3 = orient2d(c, d, a), d4 = orient2d(c, d, b);
        if (d1 == Sign::Zero || d2 == Sign::Zero ||
            d3 == Sign::Zero || d4 == Sign::Zero)
            return false;   // touching or collinear is not a *proper* crossing
        return d1 != d2 && d3 != d4;
    }
};

template <class P>
using PredicatesFor = Predicates<typename PlatformTraits<P>::PreferredScalar>;

static_assert(std::is_same_v<PredicatesFor<AppleM>::FilterScalarType, DF64>,
              "Apple M-series must not use an f64 filter -- the hardware has none");
static_assert(std::is_same_v<PredicatesFor<Cpu>::FilterScalarType, F64>);
static_assert(PredicatesFor<AppleM>::supports(32),
              "DF64 filter must cover the default 32-bit lattice");
static_assert(!Predicates<F32>::supports(32),
              "an F32 filter cannot represent 32-bit lattice differences exactly");

using Host = PredicatesFor<Cpu>;

inline Sign orient2d(const Lattice2& a, const Lattice2& b, const Lattice2& c) noexcept {
    return Host::orient2d(a, b, c);
}
inline Sign orient2d_exact(const Lattice2& a, const Lattice2& b, const Lattice2& c) noexcept {
    return Host::orient2d_exact(a, b, c);
}
inline Sign orient3d(const Lattice3& a, const Lattice3& b,
                     const Lattice3& c, const Lattice3& d) noexcept {
    return Host::orient3d(a, b, c, d);
}
inline Sign orient3d_exact(const Lattice3& a, const Lattice3& b,
                           const Lattice3& c, const Lattice3& d) noexcept {
    return Host::orient3d_exact(a, b, c, d);
}
inline bool collinear(const Lattice2& a, const Lattice2& b, const Lattice2& c) noexcept {
    return Host::collinear(a, b, c);
}
inline bool coplanar(const Lattice3& a, const Lattice3& b,
                     const Lattice3& c, const Lattice3& d) noexcept {
    return Host::coplanar(a, b, c, d);
}
inline bool segments_properly_intersect(const Lattice2& a, const Lattice2& b,
                                        const Lattice2& c, const Lattice2& d) noexcept {
    return Host::segments_properly_intersect(a, b, c, d);
}

inline Sign orient2d_naive_f64(const Lattice2& a, const Lattice2& b, const Lattice2& c) noexcept {
    const f64 det = (static_cast<f64>(a.x) - static_cast<f64>(c.x)) *
                        (static_cast<f64>(b.y) - static_cast<f64>(c.y)) -
                    (static_cast<f64>(a.y) - static_cast<f64>(c.y)) *
                        (static_cast<f64>(b.x) - static_cast<f64>(c.x));
    return det < 0.0 ? Sign::Negative : (det > 0.0 ? Sign::Positive : Sign::Zero);
}

inline Sign orient2d_naive_f32(const Lattice2& a, const Lattice2& b, const Lattice2& c) noexcept {
    const f32 det = (static_cast<f32>(a.x) - static_cast<f32>(c.x)) *
                        (static_cast<f32>(b.y) - static_cast<f32>(c.y)) -
                    (static_cast<f32>(a.y) - static_cast<f32>(c.y)) *
                        (static_cast<f32>(b.x) - static_cast<f32>(c.x));
    return det < 0.0f ? Sign::Negative : (det > 0.0f ? Sign::Positive : Sign::Zero);
}

}
