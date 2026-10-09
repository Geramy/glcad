#include "test.hpp"

#include <random>

#include "math/bigint.hpp"

using namespace gcm;

template <class L>
static __int128 to_i128(const SInt<128, L>& v) {
    if constexpr (sizeof(L) == 4)
        return (static_cast<__int128>(static_cast<i64>(
                    (static_cast<u64>(v.l[3]) << 32) | v.l[2])) << 64) |
               static_cast<__int128>((static_cast<u64>(v.l[1]) << 32) | v.l[0]);
    else
        return (static_cast<__int128>(static_cast<i64>(v.l[1])) << 64) |
               static_cast<__int128>(v.l[0]);
}

TEST(bigint_umul64_matches_oracle) {
    std::mt19937_64 rng(2024);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const u64 a = rng();
        const u64 b = rng();
        u64 lo, hi;
        umul64(a, b, lo, hi);
        const unsigned __int128 want =
            static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
        CHECK(lo == static_cast<u64>(want));
        CHECK(hi == static_cast<u64>(want >> 64));
    }
}

TEST(bigint_mul_i64_i64_matches_oracle) {
    std::mt19937_64 rng(55);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 40), i64{1} << 40);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const i64 a = dist(rng);
        const i64 b = dist(rng);
        const SInt<128> got = mul_i64_i64<128>(a, b);
        const __int128 want = static_cast<__int128>(a) * static_cast<__int128>(b);
        CHECK(to_i128(got) == want);
    }
}

TEST(bigint_add_sub_match_oracle) {
    std::mt19937_64 rng(88);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 45), i64{1} << 45);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const i64 a = dist(rng), b = dist(rng), c = dist(rng), d = dist(rng);
        const SInt<128> x = mul_i64_i64<128>(a, b);
        const SInt<128> y = mul_i64_i64<128>(c, d);
        const __int128 wx = static_cast<__int128>(a) * b;
        const __int128 wy = static_cast<__int128>(c) * d;
        CHECK(to_i128(add(x, y)) == wx + wy);
        CHECK(to_i128(sub(x, y)) == wx - wy);
        CHECK(to_i128(negate(x)) == -wx);
    }
}

TEST(bigint_sign_matches_oracle) {
    std::mt19937_64 rng(4);
    std::uniform_int_distribution<i64> dist(-1000, 1000);
    for (int i = 0; i < (int)::t::iters(100000); ++i) {
        const i64 a = dist(rng), b = dist(rng), c = dist(rng), d = dist(rng);
        const SInt<128> diff = sub(mul_i64_i64<128>(a, b), mul_i64_i64<128>(c, d));
        const __int128 want = static_cast<__int128>(a) * b - static_cast<__int128>(c) * d;
        const Sign expect = want < 0 ? Sign::Negative
                                     : (want > 0 ? Sign::Positive : Sign::Zero);
        CHECK(diff.sign() == expect);
    }
}

TEST(bigint_handles_i64_min) {
    // abs_to_u64 must not overflow on the most negative value.
    CHECK(abs_to_u64(INT64_MIN) == (u64{1} << 63));
    const SInt<128> v = mul_i64_i64<128>(INT64_MIN, 1);
    CHECK(to_i128(v) == static_cast<__int128>(INT64_MIN));
}

TEST(bigint_wide_256_sums) {
    std::mt19937_64 rng(1717);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 40), i64{1} << 40);
    for (int i = 0; i < (int)::t::iters(50000); ++i) {
        const i64 a = dist(rng), b = dist(rng), c = dist(rng);
        SInt<256> m = mul_i64_i64<256>(a, b);
        m = mul_i64(m, c);
        const __int128 partial = static_cast<__int128>(a) * b;
        if (partial > -(__int128{1} << 60) && partial < (__int128{1} << 60) &&
            c > -(i64{1} << 60) && c < (i64{1} << 60)) {
            const __int128 want = partial * c;
            const __int128 got  = (static_cast<__int128>(static_cast<i64>(m.l[1])) << 64) |
                                  static_cast<__int128>(m.l[0]);
            CHECK(got == want);
        }
    }
}


TEST(bigint_u32_and_u64_limbs_agree_exactly) {
    std::mt19937_64 rng(9090);
    std::uniform_int_distribution<i64> dist(-(i64{1} << 40), i64{1} << 40);
    for (int i = 0; i < (int)::t::iters(200000); ++i) {
        const i64 a = dist(rng), b = dist(rng), c = dist(rng), d = dist(rng);

        const auto w64 = sub(mul_i64_i64<128, u64>(a, b), mul_i64_i64<128, u64>(c, d));
        const auto w32 = sub(mul_i64_i64<128, u32>(a, b), mul_i64_i64<128, u32>(c, d));

        const __int128 want = static_cast<__int128>(a) * b - static_cast<__int128>(c) * d;
        CHECK(to_i128(w64) == want);
        CHECK(to_i128(w32) == want);
        CHECK(w64.sign() == w32.sign());
    }
}
