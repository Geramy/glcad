#include "test.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "math/predicate.hpp"

#if defined(GLCAD_HAVE_SLANG)
#include "slang_predicates.h"
#endif

using namespace gcm;

namespace {

using P = predicate::Predicates<F64, u64>;

struct Orient2dInput {
    std::int32_t ax, ay, bx, by, cx, cy;
};
struct Orient3dInput {
    std::int32_t ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz;
};

Lattice2 lat(std::int32_t x, std::int32_t y) {
    Lattice2 p;
    p.x = x;
    p.y = y;
    return p;
}
Lattice3 lat3(std::int32_t x, std::int32_t y, std::int32_t z) {
    Lattice3 p;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

int sign_of(Sign s) {
    return s == Sign::Positive ? 1
         : (s == Sign::Negative ? -1 : 0);
}

const char* name_of(int s) {
    return s > 0 ? "positive" : (s < 0 ? "negative" : "zero");
}

}

#if !defined(GLCAD_HAVE_SLANG)

TEST(slang_conformance_did_not_run) {
    CHECK_MSG(false,
              "this build has no Slang compiler, so the GPU mirror was never "
              "checked against the CPU. Configure with -DGLCAD_SLANGC=<path> "
              "to run it.");
}

#else

TEST(slang_the_input_structures_match_the_shader) {
    CHECK(sizeof(Orient2dInput) == 6 * sizeof(std::int32_t));
    CHECK(sizeof(Orient3dInput) == 12 * sizeof(std::int32_t));
    CHECK(slang_orient2d_input_size() == sizeof(Orient2dInput));
    CHECK(slang_orient3d_input_size() == sizeof(Orient3dInput));
}

namespace {

struct MulInput {
    std::uint32_t a_lo, a_hi, b_lo, b_hi;
};
struct MulOutput {
    std::uint32_t l0, l1, l2, l3;
};

MulInput mul_in(std::int64_t a, std::int64_t b) {
    MulInput v;
    v.a_lo = static_cast<std::uint32_t>(static_cast<std::uint64_t>(a));
    v.a_hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(a) >> 32);
    v.b_lo = static_cast<std::uint32_t>(static_cast<std::uint64_t>(b));
    v.b_hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(b) >> 32);
    return v;
}

void reference_product(std::int64_t a, std::int64_t b, std::uint32_t out[4]) {
    const auto p = mul_i64_i64<128, std::uint32_t>(a, b);
    for (int i = 0; i < 4; ++i) out[i] = p.l[static_cast<std::size_t>(i)];
}

}

TEST(slang_the_multiply_layouts_match) {
    CHECK(sizeof(MulInput) == slang_mul_input_size());
    CHECK(sizeof(MulOutput) == slang_mul_output_size());
}

TEST(slang_the_128_bit_product_matches_limb_for_limb) {
    std::vector<MulInput> in;
    std::vector<std::pair<std::int64_t, std::int64_t>> pairs;

    auto add = [&](std::int64_t a, std::int64_t b) {
        pairs.push_back({a, b});
        in.push_back(mul_in(a, b));
    };

    add(0xFFFFFFFF, 0xFFFFFFFF);
    add(0xFFFFFFFFll, 0xFFFFFFFFFFll);
    add(0xFFFFFFFFFFFFFFFFll, 0xFFFFFFFFll);
    add(0x7FFFFFFFFFFFFFFFll, 0x7FFFFFFFFFFFFFFFll);
    add(0xFFFF0000FFFF0000ll, 0x0000FFFF0000FFFFll);
    add(0x80000000, 0x80000000);
    add(0xFFFF, 0xFFFF);
    add(0, 12345);
    add(1, -1);
    add(-1, -1);
    add(-0x100000000ll, 0x100000000ll);
    add(0x123456789ABCDEFll, -0xFEDCBA987654321ll);

    std::mt19937_64 rng(0xC0FFEEull);
    for (int i = 0; i < 4000; ++i) {
        const std::int64_t a = static_cast<std::int64_t>(rng());
        const std::int64_t b = static_cast<std::int64_t>(rng());
        add(a, b);
    }

    std::vector<MulOutput> got(in.size());
    slang_mul_batch(in.data(), got.data(), in.size());

    std::size_t disagreements = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        std::uint32_t want[4];
        reference_product(pairs[i].first, pairs[i].second, want);
        const std::uint32_t g[4] = {got[i].l0, got[i].l1, got[i].l2, got[i].l3};
        for (int k = 0; k < 4; ++k) {
            if (g[k] == want[k]) continue;
            if (disagreements < 4)
                CHECK_MSG(false,
                          "%lld x %lld: limb %d is %08x, the CPU says %08x",
                          static_cast<long long>(pairs[i].first),
                          static_cast<long long>(pairs[i].second), k, g[k],
                          want[k]);
            ++disagreements;
            break;
        }
    }
    CHECK_MSG(disagreements == 0, "%zu of %zu products disagreed",
              disagreements, in.size());
}

TEST(slang_orient2d_agrees_on_the_obvious_cases) {
    std::vector<Orient2dInput> in = {
        {0, 0, 10, 0, 0, 10},         // counter-clockwise
        {0, 0, 0, 10, 10, 0},         // clockwise
        {0, 0, 10, 10, 20, 20},       // collinear
        {0, 0, 1, 0, 2, 0},           // collinear, on an axis
        {-5, -5, 5, 5, 0, 0},         // collinear through the origin
    };
    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient2d_batch(in.data(), got.data(), in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient2d(lat(in[i].ax, in[i].ay),
                                             lat(in[i].bx, in[i].by),
                                             lat(in[i].cx, in[i].cy)));
        CHECK_MSG(got[i] == want, "case %zu: the CPU says %s and the GPU says %s",
                  i, name_of(want), name_of(got[i]));
    }
}

TEST(slang_orient2d_agrees_on_random_input) {
    // Full lattice range, so the 32x32 cross terms carry.
    std::mt19937 rng(20260829u);
    std::uniform_int_distribution<std::int32_t> coord(-2147483647, 2147483647);

    std::vector<Orient2dInput> in;
    for (int i = 0; i < 4000; ++i)
        in.push_back({coord(rng), coord(rng), coord(rng), coord(rng),
                      coord(rng), coord(rng)});

    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient2d_batch(in.data(), got.data(), in.size());

    std::size_t disagreements = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient2d(lat(in[i].ax, in[i].ay),
                                             lat(in[i].bx, in[i].by),
                                             lat(in[i].cx, in[i].cy)));
        if (got[i] != want) {
            if (disagreements < 3)
                CHECK_MSG(false,
                          "(%d,%d) (%d,%d) (%d,%d): CPU %s, GPU %s",
                          in[i].ax, in[i].ay, in[i].bx, in[i].by, in[i].cx,
                          in[i].cy, name_of(want), name_of(got[i]));
            ++disagreements;
        }
    }
    CHECK_MSG(disagreements == 0, "%zu of %zu disagreed", disagreements,
              in.size());
}

TEST(slang_orient2d_agrees_on_the_hard_cases) {
    std::mt19937 rng(7u);
    std::uniform_int_distribution<std::int32_t> big(-2000000000, 2000000000);
    std::uniform_int_distribution<std::int32_t> nudge(-2, 2);

    std::vector<Orient2dInput> in;
    for (int i = 0; i < 3000; ++i) {
        const std::int32_t ax = big(rng), ay = big(rng);
        const std::int32_t dx = nudge(rng) == 0 ? 1 : nudge(rng);
        const std::int32_t dy = nudge(rng);
        in.push_back({ax, ay, ax + dx * 1000, ay + dy * 1000,
                      ax + dx * 2000 + nudge(rng), ay + dy * 2000 + nudge(rng)});
    }

    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient2d_batch(in.data(), got.data(), in.size());

    std::size_t disagreements = 0, zeros = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient2d(lat(in[i].ax, in[i].ay),
                                             lat(in[i].bx, in[i].by),
                                             lat(in[i].cx, in[i].cy)));
        if (want == 0) ++zeros;
        if (got[i] != want) ++disagreements;
    }
    CHECK_MSG(disagreements == 0, "%zu of %zu near-degenerate cases disagreed",
              disagreements, in.size());
    CHECK_MSG(zeros > 0, "no case was actually degenerate");
}

TEST(slang_orient2d_agrees_at_the_extremes) {
    const std::int32_t big = 2000000000;
    std::vector<Orient2dInput> in = {
        {big, big, -big, big, big, -big},
        {-big, -big, big, -big, -big, big},
        {big, -big, -big, -big, 0, big},
        {big, big, big - 1, big - 1, big - 2, big - 2},
        {0, 0, big, big, big - 1, big},
    };
    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient2d_batch(in.data(), got.data(), in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient2d(lat(in[i].ax, in[i].ay),
                                             lat(in[i].bx, in[i].by),
                                             lat(in[i].cx, in[i].cy)));
        CHECK_MSG(got[i] == want,
                  "extreme case %zu: CPU %s, GPU %s", i, name_of(want),
                  name_of(got[i]));
    }
}

TEST(slang_orient3d_agrees_on_the_obvious_cases) {
    std::vector<Orient3dInput> in = {
        {0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0, 10},     // a tetrahedron
        {0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0, -10},    // the other way up
        {0, 0, 0, 10, 0, 0, 0, 10, 0, 5, 5, 0},      // coplanar
        {0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0},        // collinear, so coplanar
    };
    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient3d_batch(in.data(), got.data(), in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient3d_exact(
            lat3(in[i].ax, in[i].ay, in[i].az),
            lat3(in[i].bx, in[i].by, in[i].bz),
            lat3(in[i].cx, in[i].cy, in[i].cz),
            lat3(in[i].dx, in[i].dy, in[i].dz)));
        CHECK_MSG(got[i] == want, "case %zu: CPU %s, GPU %s", i, name_of(want),
                  name_of(got[i]));
    }
}

TEST(slang_orient3d_agrees_on_random_input) {
    // Full lattice range, so the 32x32 cross terms carry.
    std::mt19937 rng(31337u);
    std::uniform_int_distribution<std::int32_t> coord(-2147483647, 2147483647);

    std::vector<Orient3dInput> in;
    for (int i = 0; i < 3000; ++i)
        in.push_back({coord(rng), coord(rng), coord(rng),
                      coord(rng), coord(rng), coord(rng),
                      coord(rng), coord(rng), coord(rng),
                      coord(rng), coord(rng), coord(rng)});

    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient3d_batch(in.data(), got.data(), in.size());

    std::size_t disagreements = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient3d_exact(
            lat3(in[i].ax, in[i].ay, in[i].az),
            lat3(in[i].bx, in[i].by, in[i].bz),
            lat3(in[i].cx, in[i].cy, in[i].cz),
            lat3(in[i].dx, in[i].dy, in[i].dz)));
        if (got[i] != want) {
            if (disagreements < 3)
                CHECK_MSG(false, "case %zu: CPU %s, GPU %s", i, name_of(want),
                          name_of(got[i]));
            ++disagreements;
        }
    }
    CHECK_MSG(disagreements == 0, "%zu of %zu disagreed", disagreements,
              in.size());
}

TEST(slang_orient3d_agrees_on_coplanar_input) {
    std::mt19937 rng(999u);
    std::uniform_int_distribution<std::int32_t> coord(-1000000000, 1000000000);

    std::vector<Orient3dInput> in;
    for (int i = 0; i < 2000; ++i) {
        const std::int32_t ax = coord(rng), ay = coord(rng);
        const std::int32_t bx = coord(rng), by = coord(rng);
        const std::int32_t cx = coord(rng), cy = coord(rng);
        const std::int32_t dx = coord(rng), dy = coord(rng);
        const std::int32_t z = coord(rng);
        in.push_back({ax, ay, z, bx, by, z, cx, cy, z, dx, dy, z});
    }

    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient3d_batch(in.data(), got.data(), in.size());

    std::size_t wrong = 0;
    for (std::size_t i = 0; i < in.size(); ++i)
        if (got[i] != 0) ++wrong;
    CHECK_MSG(wrong == 0, "%zu of %zu coplanar cases came out non-zero", wrong,
              in.size());
}

TEST(slang_the_multiply_carries_between_its_cross_terms) {
    // a_lo*b_hi + a_hi*b_lo carries into bit 32; adding before shifting loses it.
    const std::int32_t lo = -2147483647, hi = 2147483647;
    std::vector<Orient3dInput> in;

    in.push_back({hi, hi, hi, lo, hi, hi, hi, lo, hi, lo, lo, lo});
    in.push_back({lo, lo, lo, hi, lo, lo, lo, hi, lo, hi, hi, hi});
    in.push_back({hi, lo, hi, lo, hi, lo, hi, hi, lo, lo, lo, hi});
    in.push_back({hi, hi, lo, lo, lo, hi, hi, lo, hi, lo, hi, lo});
    in.push_back({hi, 0, 0, lo, 1, 0, 0, hi, 1, 0, 0, lo});

    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient3d_batch(in.data(), got.data(), in.size());

    std::size_t disagreements = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient3d_exact(
            lat3(in[i].ax, in[i].ay, in[i].az),
            lat3(in[i].bx, in[i].by, in[i].bz),
            lat3(in[i].cx, in[i].cy, in[i].cz),
            lat3(in[i].dx, in[i].dy, in[i].dz)));
        if (got[i] != want) {
            CHECK_MSG(false, "carry case %zu: CPU %s, GPU %s", i, name_of(want),
                      name_of(got[i]));
            ++disagreements;
        }
    }
    CHECK_MSG(disagreements == 0, "%zu of %zu carry cases disagreed",
              disagreements, in.size());
}

TEST(slang_orient3d_agrees_at_the_extremes) {
    const std::int32_t big = 2000000000;
    std::vector<Orient3dInput> in = {
        {big, big, big, -big, big, big, big, -big, big, big, big, -big},
        {-big, -big, -big, big, -big, -big, -big, big, -big, -big, -big, big},
        {big, 0, 0, 0, big, 0, 0, 0, big, -big, -big, -big},
        {big, big, big, big - 1, big, big, big, big - 1, big, big, big, big - 1},
    };
    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient3d_batch(in.data(), got.data(), in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        const int want = sign_of(P::orient3d_exact(
            lat3(in[i].ax, in[i].ay, in[i].az),
            lat3(in[i].bx, in[i].by, in[i].bz),
            lat3(in[i].cx, in[i].cy, in[i].cz),
            lat3(in[i].dx, in[i].dy, in[i].dz)));
        CHECK_MSG(got[i] == want, "extreme case %zu: CPU %s, GPU %s", i,
                  name_of(want), name_of(got[i]));
    }
}

TEST(slang_orient2d_is_antisymmetric) {
    std::mt19937 rng(4242u);
    std::uniform_int_distribution<std::int32_t> coord(-2147483647, 2147483647);

    std::vector<Orient2dInput> forward, swapped;
    for (int i = 0; i < 1500; ++i) {
        const Orient2dInput v{coord(rng), coord(rng), coord(rng),
                              coord(rng), coord(rng), coord(rng)};
        forward.push_back(v);
        swapped.push_back({v.bx, v.by, v.ax, v.ay, v.cx, v.cy});
    }

    std::vector<std::int32_t> a(forward.size(), 99), b(swapped.size(), 99);
    slang_orient2d_batch(forward.data(), a.data(), forward.size());
    slang_orient2d_batch(swapped.data(), b.data(), swapped.size());

    std::size_t wrong = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != -b[i]) ++wrong;
    CHECK_MSG(wrong == 0, "%zu of %zu were not antisymmetric", wrong, a.size());
}

TEST(slang_orient3d_is_antisymmetric) {
    std::mt19937 rng(24u);
    std::uniform_int_distribution<std::int32_t> coord(-1000000000, 1000000000);

    std::vector<Orient3dInput> forward, swapped;
    for (int i = 0; i < 1200; ++i) {
        const Orient3dInput v{coord(rng), coord(rng), coord(rng),
                              coord(rng), coord(rng), coord(rng),
                              coord(rng), coord(rng), coord(rng),
                              coord(rng), coord(rng), coord(rng)};
        forward.push_back(v);
        swapped.push_back({v.bx, v.by, v.bz, v.ax, v.ay, v.az,
                           v.cx, v.cy, v.cz, v.dx, v.dy, v.dz});
    }

    std::vector<std::int32_t> a(forward.size(), 99), b(swapped.size(), 99);
    slang_orient3d_batch(forward.data(), a.data(), forward.size());
    slang_orient3d_batch(swapped.data(), b.data(), swapped.size());

    std::size_t wrong = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != -b[i]) ++wrong;
    CHECK_MSG(wrong == 0, "%zu of %zu were not antisymmetric", wrong, a.size());
}

TEST(slang_a_degenerate_triangle_is_zero_on_both_sides) {
    std::vector<Orient2dInput> in = {
        {5, 7, 5, 7, 100, -200},
        {0, 0, 0, 0, 0, 0},
        {-3, -3, 9, 9, 9, 9},
    };
    std::vector<std::int32_t> got(in.size(), 99);
    slang_orient2d_batch(in.data(), got.data(), in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
        CHECK_MSG(got[i] == 0, "case %zu came out %s", i, name_of(got[i]));
}

#endif   // GLCAD_HAVE_SLANG
