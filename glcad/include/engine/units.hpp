#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "math/config.hpp"

namespace gunit {

using gcm::f64;
using gcm::i32;

enum class Length {
    Metre, Millimetre, Centimetre, Kilometre, Micrometre,
    Inch, Foot, Yard, Mile, Mil,
};

constexpr f64 metres_per(Length u) noexcept {
    switch (u) {
        case Length::Metre:      return 1.0;
        case Length::Millimetre: return 1e-3;
        case Length::Centimetre: return 1e-2;
        case Length::Kilometre:  return 1e3;
        case Length::Micrometre: return 1e-6;
        case Length::Inch:       return 0.0254;
        case Length::Foot:       return 0.3048;
        case Length::Yard:       return 0.9144;
        case Length::Mile:       return 1609.344;
        case Length::Mil:        return 0.0000254;
    }
    return 1.0;
}

constexpr f64 to_metres(f64 v, Length u) noexcept { return v * metres_per(u); }
constexpr f64 from_metres(f64 m, Length u) noexcept { return m / metres_per(u); }

const char* suffix(Length u) noexcept;
const char* name(Length u) noexcept;
std::optional<Length> length_from_suffix(std::string_view s) noexcept;

enum class Angle { Radian, Degree, Gradian };

constexpr f64 radians_per(Angle u) noexcept {
    switch (u) {
        case Angle::Radian:  return 1.0;
        case Angle::Degree:  return 3.14159265358979323846 / 180.0;
        case Angle::Gradian: return 3.14159265358979323846 / 200.0;
    }
    return 1.0;
}
constexpr f64 to_radians(f64 v, Angle u) noexcept { return v * radians_per(u); }
constexpr f64 from_radians(f64 r, Angle u) noexcept { return r / radians_per(u); }

enum class Style {
    Decimal,
    Scientific,
    Engineering,
    Architectural,
    Fractional,
};

struct Format {
    Length unit{Length::Millimetre};
    Style  style{Style::Decimal};
    i32    precision{4};        // decimals, or 2^n fraction denominator (4 -> 1/16)
    bool   show_unit{true};
    bool   suppress_trailing_zeros{true};
};

std::string format_length(f64 metres, const Format& f);

struct AngleFormat {
    Angle unit{Angle::Degree};
    i32   precision{2};
    bool  show_unit{true};
};
std::string format_angle(f64 radians, const AngleFormat& f);

std::optional<f64> parse_length(std::string_view text, Length default_unit);
std::optional<f64> parse_angle(std::string_view text, Angle default_unit);

}
