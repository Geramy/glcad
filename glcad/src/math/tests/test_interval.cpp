#include "test.hpp"

#include <cmath>

#include "math/interval.hpp"
#include "math/predicate.hpp"

using namespace gcm;
using I = Interval<F64>;

TEST(interval_contains_its_value) {
    const I a = I::around(0.1);
    CHECK(a.contains(0.1));
    CHECK(a.lo <= 0.1 && a.hi >= 0.1);
    CHECK(a.width() > 0.0);

    const I e = I::exact(3.0);
    CHECK(e.is_point());
    CHECK(e.width() == 0.0);
}

TEST(interval_arithmetic_encloses_the_true_answer) {
    for (double a : {-3.7, -1.0, 0.0, 0.25, 1e6}) {
        for (double b : {-2.2, -0.5, 0.0, 7.0, 1e-9}) {
            const I ia = I::around(a), ib = I::around(b);
            CHECK_MSG((ia + ib).contains(a + b), "%g + %g", a, b);
            CHECK_MSG((ia - ib).contains(a - b), "%g - %g", a, b);
            CHECK_MSG((ia * ib).contains(a * b), "%g * %g", a, b);
            if (std::fabs(b) > 1e-12)
                CHECK_MSG((ia / ib).contains(a / b), "%g / %g", a, b);
        }
    }
}

TEST(interval_multiplication_uses_all_four_corners) {
    const I a(-2.0, 3.0);
    const I b(-5.0, 1.0);
    const I p = a * b;
    CHECK_MSG(p.lo <= -15.0 + 1e-9, "lo is %g, should reach -15", p.lo);
    CHECK_MSG(p.hi >= 10.0 - 1e-9, "hi is %g, should reach 10", p.hi);
    for (double x : {-2.0, -0.5, 0.0, 1.7, 3.0})
        for (double y : {-5.0, -1.0, 0.0, 0.5, 1.0})
            CHECK(p.contains(x * y));
}

TEST(interval_division_by_a_straddling_interval_is_unbounded) {
    const I q = I(1.0, 2.0) / I(-1.0, 1.0);
    CHECK(std::isinf(q.lo) && std::isinf(q.hi));
    CHECK(q.contains(1e300));
    CHECK(q.contains(-1e300));
}

TEST(interval_sign_is_a_proof_or_a_shrug) {
    CHECK(I(1.0, 2.0).sign() == Sign::Positive);
    CHECK(I(-5.0, -1.0).sign() == Sign::Negative);
    CHECK(I(-1.0, 1.0).sign() == Sign::Zero);
    CHECK(I(0.0, 1.0).sign() == Sign::Zero);       // touches zero: undecided
    CHECK(I(0.0, 0.0).sign() == Sign::Zero);

    CHECK(I(1.0, 2.0).decided());
    CHECK(!I(-1.0, 1.0).decided());
}

TEST(interval_abs_and_sqrt) {
    CHECK(abs(I(-3.0, -1.0)).lo == 1.0);
    CHECK(abs(I(-3.0, 2.0)).lo == 0.0);
    CHECK(abs(I(-3.0, 2.0)).hi == 3.0);

    const I r = sqrt(I::around(2.0));
    CHECK(r.contains(std::sqrt(2.0)));
    CHECK(sqrt(I(-4.0, -1.0)).empty());
    CHECK(sqrt(I(-1.0, 4.0)).lo == 0.0);
}

namespace {
using Pred = predicate::Predicates<F64>;
Lattice2 p2(i64 x, i64 y) { return {x, y}; }
Lattice3 p3(i64 x, i64 y, i64 z) { return {x, y, z}; }
}

TEST(interval_orient2d_agrees_with_the_exact_predicate_when_it_decides) {
    struct Case { i64 ax, ay, bx, by, cx, cy; };
    const Case cases[] = {
        {0, 0, 1000, 0, 0, 1000},
        {0, 0, 1000, 0, 0, -1000},
        {0, 0, 1000, 1000, 2000, 2000},        // exactly collinear
        {0, 0, 1, 1, 2, 2},
        {0, 0, 1000000, 0, 500000, 1},         // one unit off the line
        {-10000000, -10000000, 10000000, 10000000, 0, 1},
    };

    for (const Case& c : cases) {
        const Sign got = orient2d_interval<F64>(p2(c.ax, c.ay), p2(c.bx, c.by),
                                                p2(c.cx, c.cy));
        const Sign truth = Pred::orient2d_exact(p2(c.ax, c.ay), p2(c.bx, c.by),
                                                p2(c.cx, c.cy));
        if (got == Sign::Zero) continue;          // it declined, which is legal
        CHECK_MSG(got == truth, "interval said %d, exact said %d", int(got),
                  int(truth));
    }
}

TEST(interval_orient2d_declines_on_the_genuinely_collinear) {
    CHECK(orient2d_interval<F64>(p2(0, 0), p2(1, 1), p2(2, 2)) == Sign::Zero);
    CHECK(orient2d_interval<F64>(p2(0, 0), p2(5, 0), p2(9, 0)) == Sign::Zero);
}

TEST(interval_orient2d_decides_the_easy_cases) {
    int decided = 0;
    const int n = static_cast<int>(200 * t::scale()) + 1;
    for (int i = 1; i <= n; ++i) {
        if (orient2d_interval<F64>(p2(0, 0), p2(1000000, 0),
                                   p2(i * 137, 1000000 + i)) != Sign::Zero)
            ++decided;
    }
    CHECK_MSG(decided == n, "only %d of %d clear cases were decided", decided, n);
}

TEST(interval_orient2d_never_contradicts_the_exact_tier) {
    const int n = static_cast<int>(300 * t::scale()) + 1;
    for (int i = -n; i <= n; ++i)
        for (int j = -3; j <= 3; ++j) {
            const Lattice2 a = p2(0, 0), b = p2(1000, 0), c = p2(i, j);
            const Sign got = orient2d_interval<F64>(a, b, c);
            if (got == Sign::Zero) continue;
            CHECK_MSG(got == Pred::orient2d_exact(a, b, c),
                      "disagreed at (%d, %d)", i, j);
        }
}

TEST(interval_orient3d_agrees_with_the_exact_predicate) {
    struct Case { i64 p[12]; };
    const Case cases[] = {
        {{0,0,0, 1000,0,0, 0,1000,0, 0,0,1000}},
        {{0,0,0, 1000,0,0, 0,1000,0, 0,0,-1000}},
        {{0,0,0, 1000,0,0, 0,1000,0, 1000,1000,0}},   // coplanar
        {{0,0,0, 1000,0,0, 0,1000,0, 300,300,1}},
    };
    for (const Case& c : cases) {
        const Lattice3 a = p3(c.p[0], c.p[1], c.p[2]);
        const Lattice3 b = p3(c.p[3], c.p[4], c.p[5]);
        const Lattice3 d = p3(c.p[6], c.p[7], c.p[8]);
        const Lattice3 e = p3(c.p[9], c.p[10], c.p[11]);
        const Sign got = orient3d_interval<F64>(a, b, d, e);
        const Sign truth = Pred::orient3d_exact(a, b, d, e);
        if (got == Sign::Zero) continue;
        CHECK(got == truth);
    }
}

TEST(interval_widening_never_narrows) {
    const I a = I::around(1.0 / 3.0);
    const I b = a + I::exact(0.0);
    CHECK(b.width() >= a.width());
    const I c = a * I::exact(1.0);
    CHECK(c.contains(1.0 / 3.0));
}
