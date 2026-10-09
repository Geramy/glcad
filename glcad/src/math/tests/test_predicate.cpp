#include "test.hpp"

#include <random>

#include "math/predicate.hpp"

using namespace gcm;
using namespace gcm::predicate;

static __int128 orient2d_oracle(const Lattice2& a, const Lattice2& b, const Lattice2& c) {
    const __int128 adx = static_cast<__int128>(a.x) - c.x;
    const __int128 ady = static_cast<__int128>(a.y) - c.y;
    const __int128 bdx = static_cast<__int128>(b.x) - c.x;
    const __int128 bdy = static_cast<__int128>(b.y) - c.y;
    return adx * bdy - ady * bdx;
}

static Sign sign_of_i128(__int128 v) {
    return v < 0 ? Sign::Negative : (v > 0 ? Sign::Positive : Sign::Zero);
}

// Consecutive Fibonacci: det is exactly +/-1 while products are ~2^63.
TEST(predicate_orient2d_survives_cassini_degeneracy) {
    const Lattice2 a{0, 0};
    const Lattice2 b{2971215073LL, 1836311903LL};   // F(47), F(46)
    const Lattice2 c{4807526976LL, 2971215073LL};   // F(48), F(47)

    const __int128 truth = orient2d_oracle(a, b, c);
    CHECK_MSG(truth == 1, "Cassini setup wrong, oracle det = %lld",
              static_cast<long long>(truth));

    CHECK(orient2d(a, b, c) == Sign::Positive);
    CHECK(orient2d_exact(a, b, c) == Sign::Positive);

    CHECK_MSG(orient2d_naive_f64(a, b, c) != Sign::Positive,
              "naive f64 unexpectedly correct; test no longer proves anything");
    CHECK_MSG(orient2d_naive_f32(a, b, c) != Sign::Positive,
              "naive f32 unexpectedly correct; test no longer proves anything");
}

TEST(predicate_orient2d_matches_oracle_random) {
    std::mt19937_64 rng(101);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 32), i64{1} << 32);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const Lattice2 a{dist(rng), dist(rng)};
        const Lattice2 b{dist(rng), dist(rng)};
        const Lattice2 c{dist(rng), dist(rng)};
        CHECK(orient2d(a, b, c) == sign_of_i128(orient2d_oracle(a, b, c)));
    }
}

TEST(predicate_orient2d_matches_oracle_near_degenerate) {
    std::mt19937_64 rng(202);
    std::uniform_int_distribution<i64> big(-(i64{1} << 30), i64{1} << 30);
    std::uniform_int_distribution<i64> tiny(-2, 2);

    stats().reset();
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const i64 dx = big(rng), dy = big(rng);
        const Lattice2 a{0, 0};
        const Lattice2 b{dx, dy};
        const Lattice2 c{2 * dx + tiny(rng), 2 * dy + tiny(rng)};
        CHECK(orient2d(a, b, c) == sign_of_i128(orient2d_oracle(a, b, c)));
    }
    std::printf("\n      [near-degenerate exact fallback rate: %.2f%%]  ",
                stats().fallback_rate() * 100.0);
}

TEST(predicate_orient2d_exact_collinearity) {
    const Lattice2 a{0, 0};
    const Lattice2 b{1000000007LL, 3LL};
    const Lattice2 c{2000000014LL, 6LL};
    CHECK(orient2d(a, b, c) == Sign::Zero);
    CHECK(orient2d_exact(a, b, c) == Sign::Zero);
    CHECK(collinear(a, b, c));
}

TEST(predicate_orient2d_antisymmetry) {
    std::mt19937_64 rng(303);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 31), i64{1} << 31);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const Lattice2 a{dist(rng), dist(rng)};
        const Lattice2 b{dist(rng), dist(rng)};
        const Lattice2 c{dist(rng), dist(rng)};
        const Sign s1 = orient2d(a, b, c);
        const Sign s2 = orient2d(b, a, c);
        CHECK(to_int(s1) == -to_int(s2));
    }
}

TEST(predicate_filter_is_the_fast_path) {
    std::mt19937_64 rng(404);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 32), i64{1} << 32);
    stats().reset();
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const Lattice2 a{dist(rng), dist(rng)};
        const Lattice2 b{dist(rng), dist(rng)};
        const Lattice2 c{dist(rng), dist(rng)};
        (void)orient2d(a, b, c);
    }
    const f64 rate = stats().fallback_rate();
    CHECK_MSG(rate < 0.001, "exact fallback rate %.4f%% too high on random input",
              rate * 100.0);
}

TEST(predicate_orient3d_matches_exact_random) {
    std::mt19937_64 rng(505);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 20), i64{1} << 20);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const Lattice3 a{dist(rng), dist(rng), dist(rng)};
        const Lattice3 b{dist(rng), dist(rng), dist(rng)};
        const Lattice3 c{dist(rng), dist(rng), dist(rng)};
        const Lattice3 d{dist(rng), dist(rng), dist(rng)};
        CHECK(orient3d(a, b, c, d) == orient3d_exact(a, b, c, d));
    }
}

TEST(predicate_orient3d_exact_coplanarity) {
    const Lattice3 a{0, 0, 0};
    const Lattice3 b{1000000, 0, 0};
    const Lattice3 c{0, 1000000, 0};
    const Lattice3 d{777777, 333333, 0};
    CHECK(orient3d(a, b, c, d) == Sign::Zero);
    CHECK(coplanar(a, b, c, d));

    const Lattice3 d2{777777, 333333, 1};
    CHECK(orient3d(a, b, c, d2) != Sign::Zero);
}

TEST(predicate_orient3d_antisymmetry) {
    std::mt19937_64 rng(606);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 20), i64{1} << 20);
    for (int i = 0; i < (int)::t::iters(50000); ++i) {
        const Lattice3 a{dist(rng), dist(rng), dist(rng)};
        const Lattice3 b{dist(rng), dist(rng), dist(rng)};
        const Lattice3 c{dist(rng), dist(rng), dist(rng)};
        const Lattice3 d{dist(rng), dist(rng), dist(rng)};
        CHECK(to_int(orient3d(a, b, c, d)) == -to_int(orient3d(b, a, c, d)));
    }
}

TEST(predicate_segment_intersection_is_exact) {
    const Lattice2 a{0, 0}, b{100, 100};
    const Lattice2 c{0, 100}, d{100, 0};
    CHECK(segments_properly_intersect(a, b, c, d));

    const Lattice2 e{100, 100}, f{200, 0};
    CHECK(!segments_properly_intersect(a, b, e, f));

    const Lattice2 g{0, 10}, h{100, 110};
    CHECK(!segments_properly_intersect(a, b, g, h));
}


TEST(predicate_all_filter_scalars_agree_exactly) {
    using PF64  = Predicates<F64>;
    using PDF64 = Predicates<DF64>;

    std::mt19937_64 rng(7171);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 32), i64{1} << 32);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const Lattice2 a{dist(rng), dist(rng)};
        const Lattice2 b{dist(rng), dist(rng)};
        const Lattice2 c{dist(rng), dist(rng)};
        const Sign truth = sign_of_i128(orient2d_oracle(a, b, c));
        CHECK(PF64::orient2d(a, b, c)  == truth);
        CHECK(PDF64::orient2d(a, b, c) == truth);
    }
}

TEST(predicate_df64_filter_survives_cassini) {
    const Lattice2 a{0, 0};
    const Lattice2 b{2971215073LL, 1836311903LL};
    const Lattice2 c{4807526976LL, 2971215073LL};
    CHECK(Predicates<DF64>::orient2d(a, b, c) == Sign::Positive);
    CHECK(Predicates<F64>::orient2d(a, b, c)  == Sign::Positive);
}

TEST(predicate_limb_width_does_not_change_results) {
    using P64 = Predicates<F64, u64>;
    using P32 = Predicates<F64, u32>;

    std::mt19937_64 rng(8181);
    std::uniform_int_distribution<i64> big(-(i64{1} << 30), i64{1} << 30);
    std::uniform_int_distribution<i64> tiny(-2, 2);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const i64 dx = big(rng), dy = big(rng);
        const Lattice2 a{0, 0};
        const Lattice2 b{dx, dy};
        const Lattice2 c{2 * dx + tiny(rng), 2 * dy + tiny(rng)};   // near-degenerate
        CHECK(P64::orient2d(a, b, c) == P32::orient2d(a, b, c));
        CHECK(P64::orient2d_exact(a, b, c) == P32::orient2d_exact(a, b, c));
    }
}

TEST(predicate_orient3d_filters_agree) {
    std::mt19937_64 rng(9191);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 20), i64{1} << 20);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const Lattice3 a{dist(rng), dist(rng), dist(rng)};
        const Lattice3 b{dist(rng), dist(rng), dist(rng)};
        const Lattice3 c{dist(rng), dist(rng), dist(rng)};
        const Lattice3 d{dist(rng), dist(rng), dist(rng)};
        const Sign truth = Predicates<F64>::orient3d_exact(a, b, c, d);
        CHECK(Predicates<F64>::orient3d(a, b, c, d)  == truth);
        CHECK(Predicates<DF64>::orient3d(a, b, c, d) == truth);
    }
}

TEST(predicate_filter_capacity_is_checked_at_compile_time) {
    // An F32 filter cannot hold 32-bit lattice differences exactly.
    CHECK(!Predicates<F32>::supports(32));
    CHECK(Predicates<F32>::supports(20));
    CHECK(Predicates<DF64>::supports(32));
    CHECK(Predicates<DF64>::supports(47));
    CHECK(!Predicates<DF64>::supports(48));
    CHECK(Predicates<F64>::supports(52));

    WorkingVolume v;
    CHECK(PredicatesFor<AppleM>::supports(v));
    CHECK(PredicatesFor<Cpu>::supports(v));
}
