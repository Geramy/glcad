#include "test.hpp"

#include <cmath>
#include <string>

#include "geometry/footprint.hpp"

using ggeo::plan::calc;
using ggeo::plan::footprint;
using ggeo::plan::FootprintSpec;

namespace {
const double kSqFt = 0.09290304;
double shoelace(const std::vector<std::pair<double, double>>& p) {
    double a2 = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& u = p[i];
        const auto& v = p[(i + 1) % p.size()];
        a2 += u.first * v.second - v.first * u.second;
    }
    return std::fabs(a2) * 0.5;
}
}

TEST(plan_calc_speaks_units_and_dimensions) {
    std::string err;
    const auto q = calc("sqrt(2800 ft2 / 1.4)", err);
    CHECK_MSG(q.has_value(), "calc failed: %s", err.c_str());
    CHECK(q->dim == 1);
    CHECK_MSG(std::fabs(q->v - std::sqrt(2800.0 * kSqFt / 1.4)) < 1e-9,
              "got %.6f m", q->v);

    const auto a = calc("40 ft * 70 ft", err);
    CHECK(a && a->dim == 2);
    CHECK(std::fabs(a->v - 2800.0 * kSqFt) < 1e-9);

    const auto s = calc("(3 + 4.5) * 2", err);
    CHECK(s && s->dim == 0 && std::fabs(s->v - 15.0) < 1e-12);

    CHECK(!calc("3 ft + 4 ft2", err).has_value());
    CHECK(!calc("sqrt(4 m)", err).has_value());
    CHECK(!calc("1 +", err).has_value());
    CHECK(!calc(std::string(500, '(') + "1" + std::string(500, ')'), err).has_value());
    CHECK(calc("((((((((((1))))))))))", err).has_value());
}

TEST(plan_footprint_rectangle_hits_the_area_on_the_grid) {
    FootprintSpec spec;
    spec.area = 2800.0 * kSqFt;
    std::string err;
    const auto fp = footprint(spec, err);
    CHECK_MSG(fp.has_value(), "%s", err.c_str());
    CHECK(fp->pts.size() == 4);
    CHECK_MSG(std::fabs(fp->area - spec.area) / spec.area < 0.02,
              "area %.2f m2 vs target %.2f m2", fp->area, spec.area);
    for (const auto& p : fp->pts) {
        CHECK(std::fabs(std::remainder(p.first, spec.grid)) < 1e-6);
        CHECK(std::fabs(std::remainder(p.second, spec.grid)) < 1e-6);
    }
    CHECK(std::fabs(shoelace(fp->pts) - fp->area) < 1e-9);
}

TEST(plan_footprint_l_t_u_close_and_meet_the_target) {
    for (const char* style : {"l", "t", "u"}) {
        FootprintSpec spec;
        spec.area = 260.0;
        spec.style = style;
        std::string err;
        const auto fp = footprint(spec, err);
        CHECK_MSG(fp.has_value(), "%s: %s", style, err.c_str());
        CHECK_MSG(fp->pts.size() >= 6, "%s made only %d points", style,
                  int(fp->pts.size()));
        CHECK_MSG(std::fabs(shoelace(fp->pts) - fp->area) < 1e-9,
                  "%s: polygon disagrees with its reported area", style);
        CHECK_MSG(std::fabs(fp->area - spec.area) / spec.area < 0.05,
                  "%s: area %.2f vs %.2f", style, fp->area, spec.area);
    }
}

TEST(plan_footprint_respects_the_lot_and_its_setbacks) {
    FootprintSpec spec;
    spec.area = 2800.0 * kSqFt;
    spec.lot_w = 120.0 * 0.3048;
    spec.lot_d = 150.0 * 0.3048;
    spec.front_setback = 25.0 * 0.3048;
    spec.side_setback = 10.0 * 0.3048;
    spec.rear_setback = 20.0 * 0.3048;
    std::string err;
    const auto fp = footprint(spec, err);
    CHECK_MSG(fp.has_value(), "%s", err.c_str());
    for (const auto& p : fp->pts) {
        CHECK_MSG(p.first >= spec.side_setback - 1e-6 &&
                      p.first <= spec.lot_w - spec.side_setback + 1e-6,
                  "x=%.3f breaches a side setback", p.first);
        CHECK_MSG(p.second >= spec.front_setback - 1e-6 &&
                      p.second <= spec.lot_d - spec.rear_setback + 1e-6,
                  "y=%.3f breaches front/rear setback", p.second);
    }
    FootprintSpec tight = spec;
    tight.lot_w = 15.0;
    tight.lot_d = 15.0;
    CHECK(!footprint(tight, err).has_value());
}
