#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "math/config.hpp"

namespace gcapp {

using gcm::f64;
using gcm::u8;

enum class TouchKind : u8 { Finger, Pencil, Unknown };

struct TouchPoint {
    gcm::u64  id{0};
    f64       x{0}, y{0};     // points
    TouchKind kind{TouchKind::Finger};
    f64       pressure{0.0};  // 0 when the device does not report it
    bool      down{false};
};

enum class Gesture : u8 {
    None,
    Tap,
    Press,
    Drag,
    Pan,
    Zoom,
    Rotate,
    LongPress,
    TwoFingerTap,
};

const char* gesture_name(Gesture g) noexcept;

struct TouchSettings {
    f64 tap_slop{10.0};
    f64 tap_seconds{0.25};

    f64 long_press_seconds{0.45};
    f64 long_press_slop{12.0};

    f64 finger_offset_x{-22.0};
    f64 finger_offset_y{-28.0};
    bool left_handed{false};

    bool offset_pencil{false};

    f64 two_finger_slop{6.0};
    f64 dominance{1.35};
};

struct TouchResult {
    Gesture gesture{Gesture::None};

    f64 x{0}, y{0};
    f64 raw_x{0}, raw_y{0};

    f64 dx{0}, dy{0};          // points
    f64 scale{1.0};            // multiplier
    f64 rotation{0.0};         // radians
    f64 centre_x{0}, centre_y{0};

    bool began{false};
    bool ended{false};
    bool from_pencil{false};
};

class TouchState {
public:
    TouchSettings settings;

    TouchResult update(const std::vector<TouchPoint>& touches, f64 now);

    void reset();

    Gesture current() const noexcept { return current_; }
    std::size_t active() const noexcept { return points_.size(); }

    void cursor_for(f64 x, f64 y, TouchKind kind, f64& out_x, f64& out_y) const;

private:
    struct Tracked {
        gcm::u64  id{0};
        f64       start_x{0}, start_y{0};
        f64       x{0}, y{0};
        f64       started_at{0};
        TouchKind kind{TouchKind::Finger};
        bool      moved{false};
    };

    Tracked* find(gcm::u64 id);

    std::vector<Tracked> points_;
    Gesture              current_{Gesture::None};
    bool                 committed_{false};
    bool                 long_press_fired_{false};
    f64                  last_distance_{0};
    f64                  last_angle_{0};
    f64                  last_centre_x_{0}, last_centre_y_{0};
    std::size_t          peak_{0};
};

}
