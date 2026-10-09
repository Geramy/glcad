#pragma once

#include "math/config.hpp"

namespace gci {

using namespace gcm;

enum class DeviceClass : u8 { Stylus, Touch, Mouse, Trackpad, Keyboard };

enum class Phase : u8 { Hover, Down, Move, Up, Cancel };

enum Button : u32 {
    Button_None    = 0,
    Button_Primary = 1u << 0,
    Button_Barrel  = 1u << 1,
    Button_Eraser  = 1u << 2,
    Button_Middle  = 1u << 3,
    Button_Squeeze = 1u << 4,
};

enum Modifier : u32 {
    Mod_None  = 0,
    Mod_Shift = 1u << 0,
    Mod_Ctrl  = 1u << 1,
    Mod_Alt   = 1u << 2,
    Mod_Cmd   = 1u << 3,
};

struct InputEvent {
    u32         device_id{0};
    DeviceClass device_class{DeviceClass::Mouse};
    Phase       phase{Phase::Move};

    f64 x{0.0}, y{0.0};
    f64 timestamp{0.0};       // DEVICE time in seconds, never frame time

    f32 pressure{0.0f};
    f32 tilt_altitude{1.5707963267948966f};  // radians; pi/2 == perpendicular
    f32 tilt_azimuth{0.0f};
    f32 barrel_roll{0.0f};
    f32 contact_size{0.0f};

    u32 buttons{Button_None};
    u32 modifiers{Mod_None};

    // Must be consumed for path gestures (strokes, lasso).
    bool is_coalesced{false};

    // Rendering only: never commits geometry, resolves a snap, or enters the log.
    bool is_predicted{false};
};

struct DeviceCaps {
    DeviceClass device_class{DeviceClass::Mouse};
    bool has_pressure{false};
    bool has_tilt{false};
    bool has_barrel_roll{false};
    bool has_hover{false};
    bool has_contact_size{false};
    f64  sample_rate_hz{60.0};

    f64 magnetism_px() const noexcept {
        switch (device_class) {
            case DeviceClass::Stylus:   return 14.0;
            case DeviceClass::Touch:    return 22.0;
            case DeviceClass::Trackpad: return 10.0;
            default:                    return 8.0;
        }
    }

    f64 samples_per_frame(f64 display_hz = 120.0) const noexcept {
        return sample_rate_hz / display_hz;
    }

    bool needs_coalescing(f64 display_hz = 120.0) const noexcept {
        return samples_per_frame(display_hz) >= 1.5;
    }
};

}
