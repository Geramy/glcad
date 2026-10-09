#include "test.hpp"

#include <cmath>
#include <cstring>
#include <random>

#include "math/soft_f64.hpp"

using namespace gcm;

static u64 bits_of(f64 x) { u64 b; std::memcpy(&b, &x, 8); return b; }
static f64 f64_of(u64 b) { f64 x; std::memcpy(&x, &b, 8); return x; }

// Any NaN equals any NaN; otherwise the encodings must match exactly.
static bool same(u64 got, u64 want) {
    const bool gn = SoftF64::is_nan(got), wn = SoftF64::is_nan(want);
    if (gn || wn) return gn && wn;
    return got == want;
}

static void report(const char* op, f64 a, f64 b, u64 got, u64 want) {
    std::printf("\n      %s(%.17g, %.17g)\n        got  %016llx (%.17g)"
                "\n        want %016llx (%.17g)\n",
                op, a, b, (unsigned long long)got, f64_of(got),
                (unsigned long long)want, f64_of(want));
}

static std::vector<f64> corpus() {
    std::vector<f64> v = {
        0.0, -0.0, 1.0, -1.0, 2.0, 0.5, 3.0, -3.0,
        1e308, -1e308, 1e-308, 5e-324, -5e-324,          // subnormal extremes
        2.2250738585072014e-308,                          // min normal
        1.7976931348623157e308,                           // max normal
        4.9406564584124654e-324,                          // min subnormal
        1.0000000000000002, 0.9999999999999999,
        std::numeric_limits<f64>::infinity(),
        -std::numeric_limits<f64>::infinity(),
        std::numeric_limits<f64>::quiet_NaN(),
        3.141592653589793, 2.718281828459045,
        1e16, 1e17, 123456789.123456789,
    };
    for (int e = -60; e <= 60; e += 7) v.push_back(std::ldexp(1.0, e));
    for (int e = -60; e <= 60; e += 11) v.push_back(std::ldexp(1.0, e) * 1.5);
    for (int i = 1; i <= 20; ++i) v.push_back(std::ldexp(f64(i), -1074));  // subnormals
    return v;
}

TEST(softf64_roundtrip_is_lossless) {
    for (f64 x : corpus()) {
        if (std::isnan(x)) continue;
        CHECK(bits_of(SoftF64::from(x).to_f64()) == bits_of(x));
    }
}

TEST(softf64_add_is_bit_identical_to_hardware) {
    const auto c = corpus();
    int failures = 0;
    for (f64 a : c)
        for (f64 b : c) {
            const u64 got  = SoftF64::add(SoftF64::from(a), SoftF64::from(b)).b;
            const u64 want = bits_of(a + b);
            if (!same(got, want)) {
                if (++failures <= 3) report("add", a, b, got, want);
                ++::t::failures();
            }
            ++::t::checks();
        }
}

TEST(softf64_mul_is_bit_identical_to_hardware) {
    const auto c = corpus();
    int failures = 0;
    for (f64 a : c)
        for (f64 b : c) {
            const u64 got  = SoftF64::mul(SoftF64::from(a), SoftF64::from(b)).b;
            const u64 want = bits_of(a * b);
            if (!same(got, want)) {
                if (++failures <= 3) report("mul", a, b, got, want);
                ++::t::failures();
            }
            ++::t::checks();
        }
}

TEST(softf64_div_is_bit_identical_to_hardware) {
    const auto c = corpus();
    int failures = 0;
    for (f64 a : c)
        for (f64 b : c) {
            const u64 got  = SoftF64::div(SoftF64::from(a), SoftF64::from(b)).b;
            const u64 want = bits_of(a / b);
            if (!same(got, want)) {
                if (++failures <= 3) report("div", a, b, got, want);
                ++::t::failures();
            }
            ++::t::checks();
        }
}

TEST(softf64_random_add_mul_bit_identical) {
    std::mt19937_64 rng(20260828);
    int fa = 0, fm = 0;
    for (int i = 0; i < (int)::t::iters(300000); ++i) {
        u64 ba = rng(), bb = rng();
        f64 a = f64_of(ba), b = f64_of(bb);
        if (!std::isfinite(a) || !std::isfinite(b)) continue;

        const u64 ga = SoftF64::add(SoftF64::from(a), SoftF64::from(b)).b;
        if (!same(ga, bits_of(a + b))) {
            if (++fa <= 3) report("add", a, b, ga, bits_of(a + b));
            ++::t::failures();
        }
        ++::t::checks();

        const u64 gm = SoftF64::mul(SoftF64::from(a), SoftF64::from(b)).b;
        if (!same(gm, bits_of(a * b))) {
            if (++fm <= 3) report("mul", a, b, gm, bits_of(a * b));
            ++::t::failures();
        }
        ++::t::checks();
    }
}

TEST(softf64_random_same_magnitude_cancellation) {
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<f64> d(1.0, 2.0);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const f64 a = d(rng);
        const f64 b = a * (1.0 + std::ldexp(1.0, -(int)(rng() % 53)));
        const u64 got = SoftF64::sub(SoftF64::from(a), SoftF64::from(b)).b;
        CHECK(same(got, bits_of(a - b)));
    }
}

TEST(softf64_random_div_bit_identical) {
    std::mt19937_64 rng(31337);
    std::uniform_real_distribution<f64> d(-1e6, 1e6);
    int f = 0;
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const f64 a = d(rng), b = d(rng);
        if (b == 0.0) continue;
        const u64 got = SoftF64::div(SoftF64::from(a), SoftF64::from(b)).b;
        if (!same(got, bits_of(a / b))) {
            if (++f <= 3) report("div", a, b, got, bits_of(a / b));
            ++::t::failures();
        }
        ++::t::checks();
    }
}

TEST(softf64_comparison_matches_hardware) {
    const auto c = corpus();
    for (f64 a : c)
        for (f64 b : c) {
            if (std::isnan(a) || std::isnan(b)) continue;
            const int got = SoftF64::cmp(SoftF64::from(a), SoftF64::from(b));
            const int want = (a < b) ? -1 : ((a > b) ? 1 : 0);
            CHECK(got == want);
        }
}

TEST(softf64_sqrt_is_bit_identical_to_hardware) {
    for (f64 x : corpus()) {
        if (std::isnan(x) || x < 0.0) continue;
        const u64 got = SoftF64::sqrt(SoftF64::from(x)).b;
        if (!same(got, bits_of(std::sqrt(x)))) {
            report("sqrt", x, 0.0, got, bits_of(std::sqrt(x)));
            ++::t::failures();
        }
        ++::t::checks();
    }
    std::mt19937_64 rng(4242);
    std::uniform_real_distribution<f64> d(1e-100, 1e100);
    int f = 0;
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const f64 x = d(rng);
        const u64 got = SoftF64::sqrt(SoftF64::from(x)).b;
        if (!same(got, bits_of(std::sqrt(x)))) {
            if (++f <= 3) report("sqrt", x, 0.0, got, bits_of(std::sqrt(x)));
            ++::t::failures();
        }
        ++::t::checks();
    }
}

TEST(softf64_satisfies_the_scalar_concept) {
    static_assert(Scalar<SoftF64>);
    static_assert(FilterScalar<SoftF64>);

    const SoftF64 a = SoftF64::from(3.0);
    const SoftF64 b = SoftF64::from(4.0);
    CHECK(std::fabs((a * a + b * b).to_f64() - 25.0) < 1e-12);
    CHECK(a < b);
    CHECK(SoftF64::sqrt(SoftF64::from(25.0)).to_f64() == 5.0);
}
