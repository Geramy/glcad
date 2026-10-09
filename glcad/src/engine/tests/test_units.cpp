#include "test.hpp"

#include <cmath>

#include "engine/units.hpp"

using namespace gunit;

static bool near(f64 a, f64 b, f64 tol = 1e-12) { return std::fabs(a - b) < tol; }

TEST(unit_core_is_metric_and_conversions_are_exact) {
    CHECK(metres_per(Length::Inch) == 0.0254);
    CHECK(metres_per(Length::Foot) == 0.3048);
    CHECK(near(to_metres(12.0, Length::Inch), 0.3048));
    CHECK(near(to_metres(1.0, Length::Foot), 0.3048));
    CHECK(near(from_metres(1.0, Length::Millimetre), 1000.0));
    CHECK(near(from_metres(0.0254, Length::Inch), 1.0));
}

TEST(unit_roundtrip_every_unit) {
    for (auto u : {Length::Metre, Length::Millimetre, Length::Centimetre,
                   Length::Kilometre, Length::Micrometre, Length::Inch,
                   Length::Foot, Length::Yard, Length::Mile, Length::Mil}) {
        for (f64 v : {0.0, 1.0, 3.14159, 1234.5, -42.0})
            CHECK(near(from_metres(to_metres(v, u), u), v, 1e-9));
    }
}

TEST(unit_parse_plain_number_uses_the_document_unit) {
    CHECK(near(*parse_length("12.7", Length::Millimetre), 0.0127));
    CHECK(near(*parse_length("12.7", Length::Inch), 0.32258));
    CHECK(near(*parse_length(".5", Length::Metre), 0.5));
}

TEST(unit_parse_suffix_overrides_the_document_unit) {
    CHECK(near(*parse_length("120mm", Length::Inch), 0.120));
    CHECK(near(*parse_length(".5in", Length::Millimetre), 0.0127));
    CHECK(near(*parse_length("1m", Length::Millimetre), 1.0));
    CHECK(near(*parse_length("2cm", Length::Foot), 0.02));
    CHECK(near(*parse_length("1km", Length::Metre), 1000.0));
}

TEST(unit_parse_fractions) {
    CHECK(near(*parse_length("1/2\"", Length::Millimetre), 0.0127));
    CHECK(near(*parse_length("3/4in", Length::Metre), 0.01905));
}

TEST(unit_parse_feet_and_inches) {
    CHECK(near(*parse_length("3'6\"", Length::Millimetre), 1.0668));
    CHECK(near(*parse_length("3'", Length::Millimetre), 0.9144));
    CHECK(near(*parse_length("3'-6\"", Length::Millimetre), 1.0668));
    CHECK(near(*parse_length("3'-6 1/2\"", Length::Millimetre), 1.0795));
}

TEST(unit_parse_expressions) {
    CHECK(near(*parse_length("25.4/2", Length::Millimetre), 0.0127));
    CHECK(near(*parse_length("10+2", Length::Millimetre), 0.012));
    CHECK(near(*parse_length("(2+3)*4", Length::Millimetre), 0.020));
    CHECK(near(*parse_length("100/4", Length::Millimetre), 0.025));
    CHECK(near(*parse_length("-5", Length::Metre), -5.0));
}

TEST(unit_parse_rejects_garbage) {
    CHECK(!parse_length("", Length::Metre).has_value());
    CHECK(!parse_length("abc", Length::Metre).has_value());
    CHECK(!parse_length("12 34 xyz", Length::Metre).has_value());
    CHECK(!parse_length("1/0", Length::Metre).has_value());
}

TEST(unit_format_decimal) {
    Format f;
    f.unit = Length::Millimetre;
    f.style = Style::Decimal;
    f.precision = 2;
    CHECK(format_length(0.0127, f) == "12.7mm");

    f.show_unit = false;
    CHECK(format_length(1.0, f) == "1000");
}

TEST(unit_format_architectural) {
    Format f;
    f.style = Style::Architectural;
    f.precision = 4;                    // 1/16"
    CHECK(format_length(1.0668, f) == "3'-6\"");
    CHECK(format_length(1.0795, f) == "3'-6 1/2\"");
    CHECK(format_length(-1.0668, f) == "-3'-6\"");
}

TEST(unit_format_engineering) {
    Format f;
    f.style = Style::Engineering;
    f.precision = 2;
    CHECK(format_length(1.0668, f) == "3'-6.00\"");
}

TEST(unit_format_fractional_reduces) {
    Format f;
    f.unit = Length::Inch;
    f.style = Style::Fractional;
    f.precision = 4;                    // sixteenths
    f.show_unit = false;
    CHECK(format_length(to_metres(6.5, Length::Inch), f) == "6 1/2");
    CHECK(format_length(to_metres(6.25, Length::Inch), f) == "6 1/4");
    CHECK(format_length(to_metres(6.0, Length::Inch), f) == "6");
}

TEST(unit_display_never_changes_stored_geometry) {
    const f64 stored = 1.0668;

    Format mm; mm.unit = Length::Millimetre; mm.precision = 1;
    Format arch; arch.style = Style::Architectural; arch.precision = 4;

    CHECK(format_length(stored, mm) == "1066.8mm");
    CHECK(format_length(stored, arch) == "3'-6\"");

    CHECK(near(*parse_length("1066.8mm", Length::Metre), stored, 1e-9));
    CHECK(near(*parse_length("3'6\"", Length::Metre), stored, 1e-9));
}

TEST(unit_angles) {
    CHECK(near(to_radians(180.0, Angle::Degree), 3.14159265358979323846, 1e-12));
    CHECK(near(from_radians(3.14159265358979323846, Angle::Degree), 180.0, 1e-12));
    CHECK(near(*parse_angle("90", Angle::Degree), 1.5707963267948966, 1e-12));
    CHECK(near(*parse_angle("45deg", Angle::Radian), 0.7853981633974483, 1e-12));

    AngleFormat af; af.precision = 1;
    CHECK(format_angle(1.5707963267948966, af) == "90.0\xc2\xb0");
}
