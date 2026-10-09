#include "test.hpp"

#include <cmath>
#include <random>

#include "math/fixed.hpp"
#include "math/scalar.hpp"

using namespace gcm;

TEST(fixed_roundtrip_within_half_cell) {
    WorkingVolume v;                 // 10 km half-extent, 32-bit lattice
    std::mt19937_64 rng(11);
    std::uniform_real_distribution<f64> dist(-9000.0, 9000.0);
    const f64 tol = v.half_cell() * 1.000001;

    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const f64 x = dist(rng), y = dist(rng), z = dist(rng);
        const Lattice3 q = to_lattice(v, x, y, z);
        f64 rx, ry, rz;
        to_world(v, q, rx, ry, rz);
        CHECK(std::fabs(rx - x) <= tol);
        CHECK(std::fabs(ry - y) <= tol);
        CHECK(std::fabs(rz - z) <= tol);
    }
}

TEST(fixed_resolution_is_sane_for_mechanical_work) {
    WorkingVolume part;              // a 1 m part
    part.extent = 0.5;
    CHECK_MSG(part.half_cell() < 1e-9, "half cell %g m over a 1 m volume",
              part.half_cell());

    WorkingVolume site;              // a 10 km site
    CHECK_MSG(site.half_cell() < 2e-6, "half cell %g m over a 10 km volume",
              site.half_cell());
}

TEST(fixed_snapping_agrees_outside_the_ambiguity_band) {
    WorkingVolume v;
    const f64 band = ambiguity_band(DF64::mantissa_bits, v.max_coord());

    std::mt19937_64 rng(13);
    std::uniform_real_distribution<f64> dist(-1000.0, 1000.0);

    int escalated = 0, disagreed_outside_band = 0;
    const int n = (int)::t::iters(200000);

    for (int i = 0; i < n; ++i) {
        const f64 x = dist(rng);
        const f64 lattice_value = (x - v.cx) * v.scale();

        const f64 weak   = lattice_value * (1.0 + std::ldexp(1.0, -DF64::mantissa_bits));
        const f64 strong = lattice_value;

        if (in_ambiguity_band(lattice_value, band)) {
            ++escalated;          // goes to the exact tier on BOTH backends
            continue;
        }
        if (std::llround(weak) != std::llround(strong)) ++disagreed_outside_band;
    }

    CHECK_MSG(disagreed_outside_band == 0,
              "%d/%d points disagreed OUTSIDE the ambiguity band -- the band is "
              "too narrow and dual precision is unsafe",
              disagreed_outside_band, n);

    CHECK_MSG(escalated > 0, "ambiguity band never triggered across %d samples", n);
    std::printf("\n      [escalated to exact tier: %d/%d = %.3f%%]  ",
                escalated, n, 100.0 * escalated / n);
}

TEST(fixed_ambiguity_band_catches_boundary_cases) {
    const i64 maxc = i64{1} << 32;
    const f64 band_df64 = ambiguity_band(DF64::mantissa_bits, maxc);
    const f64 band_f64  = ambiguity_band(F64::mantissa_bits, maxc);

    CHECK(band_df64 > band_f64);

    CHECK(in_ambiguity_band(1000.5, 0.01));
    CHECK(!in_ambiguity_band(1000.2, 0.01));
}
