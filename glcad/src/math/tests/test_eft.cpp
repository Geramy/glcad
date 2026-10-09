#include "test.hpp"

#include <cmath>
#include <random>

#include "math/eft.hpp"

using namespace gcm;

// f32 transforms are checked against f64, a wider exact type.

TEST(eft_two_sum_f32_is_exact) {
    std::mt19937_64 rng(12345);
    std::uniform_real_distribution<float> dist(-1e6f, 1e6f);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const f32 a = dist(rng);
        const f32 b = dist(rng);
        const auto r = eft::two_sum(a, b);
        const f64 exact = static_cast<f64>(a) + static_cast<f64>(b);
        const f64 got   = static_cast<f64>(r.hi) + static_cast<f64>(r.lo);
        CHECK(exact == got);
    }
}

TEST(eft_two_sum_f32_extreme_magnitudes) {
    const f32 pairs[][2] = {
        {1e30f, 1e-30f}, {1.0f, 1e-20f}, {-1e20f, 1e20f},
        {0.0f, 0.0f},    {-0.0f, 0.0f},  {1e-38f, 1e38f},
    };
    for (const auto& p : pairs) {
        const auto r = eft::two_sum(p[0], p[1]);
        const f64 exact = static_cast<f64>(p[0]) + static_cast<f64>(p[1]);
        const f64 got   = static_cast<f64>(r.hi) + static_cast<f64>(r.lo);
        CHECK(exact == got);
    }
}

TEST(eft_quick_two_sum_respects_precondition) {
    std::mt19937_64 rng(999);
    std::uniform_real_distribution<float> dist(-1e6f, 1e6f);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        f32 a = dist(rng);
        f32 b = dist(rng);
        if (std::fabs(a) < std::fabs(b)) { const f32 tmp = a; a = b; b = tmp; }
        const auto r = eft::quick_two_sum(a, b);
        const f64 exact = static_cast<f64>(a) + static_cast<f64>(b);
        const f64 got   = static_cast<f64>(r.hi) + static_cast<f64>(r.lo);
        CHECK(exact == got);
    }
}

TEST(eft_two_prod_f32_is_exact) {
    std::mt19937_64 rng(777);
    std::uniform_real_distribution<float> dist(-1e4f, 1e4f);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const f32 a = dist(rng);
        const f32 b = dist(rng);
        const auto r = eft::two_prod(a, b);
        const f64 exact = static_cast<f64>(a) * static_cast<f64>(b);
        const f64 got   = static_cast<f64>(r.hi) + static_cast<f64>(r.lo);
        CHECK(exact == got);
    }
}

TEST(eft_two_diff_f32_is_exact) {
    std::mt19937_64 rng(31337);
    std::uniform_real_distribution<float> dist(-1e6f, 1e6f);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const f32 a = dist(rng);
        const f32 b = dist(rng);
        const auto r = eft::two_diff(a, b);
        const f64 exact = static_cast<f64>(a) - static_cast<f64>(b);
        const f64 got   = static_cast<f64>(r.hi) + static_cast<f64>(r.lo);
        CHECK(exact == got);
    }
}

TEST(eft_split_reconstructs_exactly) {
    std::mt19937_64 rng(4242);
    std::uniform_real_distribution<float> dist(-1e5f, 1e5f);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const f32 a = dist(rng);
        const auto r = eft::split_f32(a);
        CHECK(static_cast<f64>(r.hi) + static_cast<f64>(r.lo) == static_cast<f64>(a));
    }
}
