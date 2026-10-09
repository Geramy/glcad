#include "test.hpp"

#include <cmath>
#include <random>

#include "math/scalar.hpp"
#include "math/tagged.hpp"
#include "math/vec.hpp"

using namespace gcm;

static f64 rel_err(f64 got, f64 want) {
    if (want == 0.0) return std::fabs(got);
    return std::fabs((got - want) / want);
}

TEST(scalar_df64_roundtrip) {
    std::mt19937_64 rng(1);
    std::uniform_real_distribution<f64> dist(-1e6, 1e6);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const f64 v = dist(rng);
        CHECK(rel_err(DF64::from(v).to_f64(), v) < 1e-14);
    }
}

TEST(scalar_df64_add_beats_f32_by_20_bits) {
    // Relative to max(|a|,|b|): error relative to the sum is unbounded.
    std::mt19937_64 rng(2);
    std::uniform_real_distribution<f64> dist(-1e3, 1e3);
    f64 worst_df = 0.0, worst_f32 = 0.0;
    for (int i = 0; i < (int)::t::iters(20000); ++i) {
        const f64 a = dist(rng), b = dist(rng);
        const f64 scale = std::max(std::fabs(a), std::fabs(b));
        worst_df  = std::max(worst_df,
            std::fabs((DF64::from(a) + DF64::from(b)).to_f64() - (a + b)) / scale);
        worst_f32 = std::max(worst_f32,
            std::fabs((F32::from(a) + F32::from(b)).to_f64() - (a + b)) / scale);
    }
    CHECK_MSG(worst_df < 1e-13, "df64 add worst scaled err %g", worst_df);
    CHECK_MSG(worst_df * 1e5 < worst_f32, "df64 %g not >=1e5x better than f32 %g",
              worst_df, worst_f32);
}

TEST(scalar_df64_mul_accuracy) {
    std::mt19937_64 rng(3);
    std::uniform_real_distribution<f64> dist(-1e3, 1e3);
    f64 worst = 0.0;
    for (int i = 0; i < (int)::t::iters(20000); ++i) {
        const f64 a = dist(rng), b = dist(rng);
        worst = std::max(worst, rel_err((DF64::from(a) * DF64::from(b)).to_f64(), a * b));
    }
    CHECK_MSG(worst < 1e-13, "df64 mul worst rel err %g", worst);
}

TEST(scalar_df64_div_accuracy) {
    std::mt19937_64 rng(4);
    std::uniform_real_distribution<f64> dist(1.0, 1e3);
    f64 worst = 0.0;
    for (int i = 0; i < (int)::t::iters(20000); ++i) {
        const f64 a = dist(rng), b = dist(rng);
        worst = std::max(worst, rel_err((DF64::from(a) / DF64::from(b)).to_f64(), a / b));
    }
    CHECK_MSG(worst < 1e-12, "df64 div worst rel err %g", worst);
}

TEST(scalar_df64_sqrt_accuracy) {
    std::mt19937_64 rng(5);
    std::uniform_real_distribution<f64> dist(1e-3, 1e6);
    f64 worst = 0.0;
    for (int i = 0; i < (int)::t::iters(20000); ++i) {
        const f64 a = dist(rng);
        worst = std::max(worst, rel_err(DF64::sqrt(DF64::from(a)).to_f64(), std::sqrt(a)));
    }
    CHECK_MSG(worst < 1e-12, "df64 sqrt worst rel err %g", worst);
}

TEST(scalar_df64_accumulation_survives_where_f32_collapses) {
    const int   n    = 1000000;
    const f64   want = 0.1 * static_cast<f64>(n);

    f32 acc32 = 0.0f;
    for (int i = 0; i < n; ++i) acc32 += 0.1f;

    DF64 accdf = DF64::from(0.0);
    const DF64 tenth = DF64::from(0.1);
    for (int i = 0; i < n; ++i) accdf += tenth;

    const f64 e32 = rel_err(static_cast<f64>(acc32), want);
    const f64 edf = rel_err(accdf.to_f64(), want);

    CHECK_MSG(edf < 1e-9, "df64 accumulation rel err %g", edf);
    CHECK_MSG(edf * 1000.0 < e32, "df64 %g not >=1000x better than f32 %g", edf, e32);
}

TEST(scalar_ordering_and_equality) {
    CHECK(DF64::from(1.0) < DF64::from(2.0));
    CHECK(DF64::from(2.0) > DF64::from(1.0));
    CHECK(DF64::from(1.5) == DF64::from(1.5));
    CHECK(F64::from(-1.0) < F64::from(0.0));
    CHECK(F32::from(3.0) >= F32::from(3.0));
    CHECK(-DF64::from(2.0) == DF64::from(-2.0));
}

TEST(scalar_crtp_algebra_is_generic) {
    auto quadratic = []<Scalar S>(f64 a, f64 b, f64 c, f64 x) {
        const S sa = S::from(a), sb = S::from(b), sc = S::from(c), sx = S::from(x);
        return (sa * sx * sx + sb * sx + sc).to_f64();
    };
    const f64 want = 3.0 * 2.5 * 2.5 + -4.0 * 2.5 + 7.0;
    CHECK(std::fabs(quadratic.operator()<F64> (3.0, -4.0, 7.0, 2.5) - want) < 1e-12);
    CHECK(std::fabs(quadratic.operator()<DF64>(3.0, -4.0, 7.0, 2.5) - want) < 1e-10);
    CHECK(std::fabs(quadratic.operator()<F32> (3.0, -4.0, 7.0, 2.5) - want) < 1e-4);
}

TEST(scalar_tagged_classes_compose) {
    const Auth<F64> a = Auth<F64>::from(2.0);
    const Auth<F64> b = Auth<F64>::from(3.0);
    CHECK(std::fabs((a * b).to_f64() - 6.0) < 1e-12);

    const Disp<F64> d = demote(a);
    CHECK(std::fabs(d.to_f64() - 2.0) < 1e-12);

    static_assert(!std::is_same_v<Auth<F64>, Disp<F64>>);
    static_assert(!std::is_convertible_v<Disp<F64>, Auth<F64>>);
}

TEST(vec_generic_over_scalar) {
    const Vec3<DF64> a = Vec3<DF64>::from(1.0, 2.0, 3.0);
    const Vec3<DF64> b = Vec3<DF64>::from(4.0, 5.0, 6.0);
    CHECK(std::fabs(dot(a, b).to_f64() - 32.0) < 1e-10);

    const Vec3<DF64> c = cross(a, b);
    CHECK(std::fabs(c.x.to_f64() - (-3.0)) < 1e-10);
    CHECK(std::fabs(c.y.to_f64() - ( 6.0)) < 1e-10);
    CHECK(std::fabs(c.z.to_f64() - (-3.0)) < 1e-10);

    const Vec2<F64> p = Vec2<F64>::from(3.0, 4.0);
    CHECK(std::fabs(length(p).to_f64() - 5.0) < 1e-12);
}
