#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "math/config.hpp"
#include "engine/units.hpp"

namespace gcmd {

using gcm::f64;

enum class CoordForm : gcm::u8 {
    Invalid,
    AbsoluteCartesian,
    RelativeCartesian,
    AbsolutePolar,
    RelativePolar,
    DistanceOnly,
};

struct Coordinate {
    CoordForm form{CoordForm::Invalid};
    f64 x{0.0}, y{0.0}, z{0.0};
    f64 distance{0.0};
    f64 angle{0.0};
    bool has_z{false};
    bool world_override{false};

    bool valid() const noexcept { return form != CoordForm::Invalid; }
    bool relative() const noexcept {
        return form == CoordForm::RelativeCartesian ||
               form == CoordForm::RelativePolar;
    }

    void resolve(f64 last_x, f64 last_y, f64 last_z, f64 direction,
                 f64& out_x, f64& out_y, f64& out_z) const;
};

std::optional<Coordinate> parse_coordinate(std::string_view text,
                                           gunit::Length unit = gunit::Length::Millimetre);

std::string format_coordinate(f64 x, f64 y, gunit::Length unit,
                              int decimals = 3);
std::string format_relative(f64 dx, f64 dy, gunit::Length unit,
                            int decimals = 3);
std::string format_polar(f64 distance, f64 angle_rad, gunit::Length unit,
                         int decimals = 3);

}
