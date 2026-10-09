#include "test.hpp"

#include <cmath>

#include "engine/coordinate.hpp"

using namespace gcmd;

namespace {
constexpr f64 kPi = 3.14159265358979323846;
const gunit::Length mm = gunit::Length::Millimetre;

void at(const Coordinate& c, f64 lx, f64 ly, f64 dir, f64& x, f64& y) {
    f64 z;
    c.resolve(lx, ly, 0.0, dir, x, y, z);
}
}

TEST(coord_absolute_cartesian) {
    const auto c = parse_coordinate("12mm,8mm", mm);
    CHECK(c.has_value());
    CHECK(c->form == CoordForm::AbsoluteCartesian);
    CHECK(!c->relative());
    CHECK(std::fabs(c->x - 0.012) < 1e-12);
    CHECK(std::fabs(c->y - 0.008) < 1e-12);

    f64 x, y;
    at(*c, 5.0, 5.0, 0.0, x, y);
    CHECK_MSG(std::fabs(x - 0.012) < 1e-12, "%.9f", x);
    CHECK(std::fabs(y - 0.008) < 1e-12);
}

TEST(coord_relative_cartesian) {
    const auto c = parse_coordinate("@12,8", mm);
    CHECK(c.has_value());
    CHECK(c->form == CoordForm::RelativeCartesian);
    CHECK(c->relative());

    f64 x, y;
    at(*c, 1.0, 2.0, 0.0, x, y);
    CHECK(std::fabs(x - 1.012) < 1e-12);
    CHECK(std::fabs(y - 2.008) < 1e-12);
}

TEST(coord_relative_polar_draws_the_wall) {
    const auto c = parse_coordinate("@3600mm<0", mm);
    CHECK(c.has_value());
    CHECK(c->form == CoordForm::RelativePolar);
    CHECK(std::fabs(c->distance - 3.6) < 1e-12);
    CHECK(std::fabs(c->angle) < 1e-12);

    f64 x, y;
    at(*c, 0.0, 0.0, 0.0, x, y);
    CHECK_MSG(std::fabs(x - 3.6) < 1e-9, "%.9f", x);
    CHECK(std::fabs(y) < 1e-9);
}

TEST(coord_polar_angles_are_degrees_on_input) {
    const auto c = parse_coordinate("@15<30", mm);
    CHECK(c.has_value());
    CHECK_MSG(std::fabs(c->angle - kPi / 6.0) < 1e-9, "%.9f rad", c->angle);

    f64 x, y;
    at(*c, 0.0, 0.0, 0.0, x, y);
    CHECK(std::fabs(x - 0.015 * std::cos(kPi / 6.0)) < 1e-12);
    CHECK(std::fabs(y - 0.015 * std::sin(kPi / 6.0)) < 1e-12);
}

TEST(coord_absolute_polar) {
    const auto c = parse_coordinate("100<90", mm);
    CHECK(c.has_value());
    CHECK(c->form == CoordForm::AbsolutePolar);
    f64 x, y;
    at(*c, 7.0, 7.0, 0.0, x, y);
    CHECK_MSG(std::fabs(x) < 1e-9, "%.9f", x);
    CHECK(std::fabs(y - 0.1) < 1e-9);
}

TEST(coord_bare_number_is_a_distance_along_the_lock) {
    const auto c = parse_coordinate("3600", mm);
    CHECK(c.has_value());
    CHECK(c->form == CoordForm::DistanceOnly);

    f64 x, y;
    at(*c, 1.0, 1.0, kPi * 0.5, x, y);      // locked at 90 degrees
    CHECK_MSG(std::fabs(x - 1.0) < 1e-9, "%.9f", x);
    CHECK_MSG(std::fabs(y - 4.6) < 1e-9, "%.9f", y);
}

TEST(coord_three_dimensional_forms) {
    const auto c = parse_coordinate("12,8,3", mm);
    CHECK(c.has_value());
    CHECK(c->has_z);
    CHECK(std::fabs(c->z - 0.003) < 1e-12);

    const auto p = parse_coordinate("@15<30,4", mm);
    CHECK(p.has_value());
    CHECK(p->form == CoordForm::RelativePolar);
    CHECK(p->has_z);
    CHECK(std::fabs(p->z - 0.004) < 1e-12);
}

TEST(coord_world_override_escapes_relative) {
    const auto c = parse_coordinate("*@12,8", mm);
    CHECK(c.has_value());
    CHECK(c->world_override);
    f64 x, y;
    at(*c, 99.0, 99.0, 0.0, x, y);
    CHECK_MSG(std::fabs(x - 0.012) < 1e-12, "the override did not take: %.9f", x);
}

TEST(coord_units_in_the_text_override_the_default) {
    const auto c = parse_coordinate("1in,0", mm);
    CHECK(c.has_value());
    CHECK_MSG(std::fabs(c->x - 0.0254) < 1e-9, "%.9f", c->x);
}

TEST(coord_arithmetic_is_accepted) {
    const auto c = parse_coordinate("(2+3)*4,10", mm);
    CHECK_MSG(c.has_value(), "arithmetic was rejected");
    CHECK_MSG(std::fabs(c->x - 0.020) < 1e-9, "%.9f", c->x);
}

TEST(coord_whitespace_is_tolerated) {
    const auto a = parse_coordinate("  @ 12 , 8 ", mm);
    CHECK(a.has_value());
    CHECK(a->relative());
    CHECK(std::fabs(a->x - 0.012) < 1e-12);
}

TEST(coord_rejects_nonsense) {
    CHECK(!parse_coordinate("", mm).has_value());
    CHECK(!parse_coordinate("@", mm).has_value());
    CHECK(!parse_coordinate("12,", mm).has_value());
    CHECK(!parse_coordinate("<45", mm).has_value());
    CHECK(!parse_coordinate("12,8,3,9", mm).has_value());
    CHECK(!parse_coordinate("hello", mm).has_value());
    CHECK(!parse_coordinate("12,hello", mm).has_value());
}

TEST(coord_formatting_round_trips) {
    const std::string abs = format_coordinate(0.012, 0.008, mm);
    CHECK_MSG(abs == "12mm,8mm", "got '%s'", abs.c_str());
    const auto back = parse_coordinate(abs, mm);
    CHECK(back.has_value());
    CHECK(std::fabs(back->x - 0.012) < 1e-9);

    const std::string rel = format_relative(0.012, -0.008, mm);
    CHECK_MSG(rel == "@12mm,-8mm", "got '%s'", rel.c_str());
    CHECK(parse_coordinate(rel, mm)->relative());

    const std::string pol = format_polar(3.6, 0.0, mm);
    CHECK_MSG(pol == "@3600mm<0", "got '%s'", pol.c_str());
    const auto pback = parse_coordinate(pol, mm);
    CHECK(pback.has_value());
    CHECK(std::fabs(pback->distance - 3.6) < 1e-9);
}

TEST(coord_negative_angles_normalise_on_output) {
    const std::string s = format_polar(1.0, -kPi * 0.5, gunit::Length::Metre);
    CHECK_MSG(s == "@1m<270", "got '%s'", s.c_str());
}

TEST(coord_invalid_coordinate_resolves_to_the_last_point) {
    Coordinate c;
    f64 x, y, z;
    c.resolve(5.0, 6.0, 7.0, 0.0, x, y, z);
    CHECK(std::fabs(x - 5.0) < 1e-12);
    CHECK(std::fabs(y - 6.0) < 1e-12);
    CHECK(std::fabs(z - 7.0) < 1e-12);
}
