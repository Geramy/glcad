#pragma once

#include <vector>

#include "engine/source.hpp"

namespace gci {

struct GestureSample {
    f64  x, y;
    f64  t;
    bool down;
};

using Gesture = std::vector<GestureSample>;

class ApplePencilLike : public SourceBase<ApplePencilLike> {
public:
    explicit ApplePencilLike(Gesture g) : g_(std::move(g)) {}

    DeviceCaps caps_impl() const {
        DeviceCaps c;
        c.device_class     = DeviceClass::Stylus;
        c.has_pressure     = true;
        c.has_tilt         = true;
        c.has_barrel_roll  = true;
        c.has_hover        = true;
        c.sample_rate_hz   = 240.0;
        return c;
    }
    const char* name_impl() const { return "Apple Pencil Pro (240 Hz)"; }

    void poll_impl(std::vector<InputEvent>& out) {
        for (const auto& s : g_) {
            InputEvent e;
            e.device_id     = 1;
            e.phase         = s.down ? Phase::Move : Phase::Hover;
            e.x = s.x; e.y = s.y; e.timestamp = s.t;
            e.pressure      = s.down ? 0.62f : 0.0f;
            e.tilt_altitude = 1.1f;
            e.tilt_azimuth  = 0.4f;
            e.barrel_roll   = 0.2f;
            out.push_back(e);
        }
        g_.clear();
    }

private:
    Gesture g_;
};

class WindowsInkLike : public SourceBase<WindowsInkLike> {
public:
    explicit WindowsInkLike(Gesture g) : g_(std::move(g)) {}

    DeviceCaps caps_impl() const {
        DeviceCaps c;
        c.device_class   = DeviceClass::Stylus;
        c.has_pressure   = true;
        c.has_tilt       = true;
        c.has_barrel_roll = false;
        c.has_hover      = true;
        c.sample_rate_hz = 133.0;
        return c;
    }
    const char* name_impl() const { return "Windows Ink / Surface Pen (133 Hz)"; }

    void poll_impl(std::vector<InputEvent>& out) {
        for (const auto& s : g_) {
            InputEvent e;
            e.device_id  = 2;
            e.phase      = s.down ? Phase::Move : Phase::Hover;
            e.x = s.x; e.y = s.y; e.timestamp = s.t;
            e.pressure   = s.down ? 0.55f : 0.0f;
            e.tilt_altitude = 1.2f;
            e.barrel_roll   = 9.9f;   // garbage: caps say unsupported, base must zero it
            out.push_back(e);
        }
        g_.clear();
    }

private:
    Gesture g_;
};

class LibinputTabletLike : public SourceBase<LibinputTabletLike> {
public:
    explicit LibinputTabletLike(Gesture g) : g_(std::move(g)) {}

    DeviceCaps caps_impl() const {
        DeviceCaps c;
        c.device_class   = DeviceClass::Stylus;
        c.has_pressure   = true;
        c.has_tilt       = true;
        c.has_hover      = false;
        c.sample_rate_hz = 200.0;
        return c;
    }
    const char* name_impl() const { return "Wacom via libinput (200 Hz)"; }

    void poll_impl(std::vector<InputEvent>& out) {
        for (const auto& s : g_) {
            if (!s.down) continue;
            InputEvent e;
            e.device_id  = 3;
            e.phase      = Phase::Move;
            e.x = s.x; e.y = s.y; e.timestamp = s.t;
            e.pressure   = 0.71f;
            e.tilt_altitude = 1.0f;
            out.push_back(e);
        }
        g_.clear();
    }

private:
    Gesture g_;
};

class MouseLike : public SourceBase<MouseLike> {
public:
    explicit MouseLike(Gesture g) : g_(std::move(g)) {}

    DeviceCaps caps_impl() const {
        DeviceCaps c;
        c.device_class   = DeviceClass::Mouse;
        c.sample_rate_hz = 125.0;
        return c;
    }
    const char* name_impl() const { return "Mouse (125 Hz)"; }

    void poll_impl(std::vector<InputEvent>& out) {
        for (const auto& s : g_) {
            InputEvent e;
            e.device_id = 4;
            e.phase     = Phase::Move;
            e.x = s.x; e.y = s.y; e.timestamp = s.t;
            e.buttons   = s.down ? Button_Primary : Button_None;
            e.pressure  = 0.33f;   // garbage: caps say unsupported
            out.push_back(e);
        }
        g_.clear();
    }

private:
    Gesture g_;
};

class TouchscreenLike : public SourceBase<TouchscreenLike> {
public:
    explicit TouchscreenLike(Gesture g) : g_(std::move(g)) {}

    DeviceCaps caps_impl() const {
        DeviceCaps c;
        c.device_class     = DeviceClass::Touch;
        c.has_contact_size = true;
        c.sample_rate_hz   = 120.0;
        return c;
    }
    const char* name_impl() const { return "Touchscreen (120 Hz)"; }

    void poll_impl(std::vector<InputEvent>& out) {
        for (const auto& s : g_) {
            if (!s.down) continue;
            InputEvent e;
            e.device_id    = 5;
            e.phase        = Phase::Move;
            e.x = s.x; e.y = s.y; e.timestamp = s.t;
            e.contact_size = 8.0f;
            out.push_back(e);
        }
        g_.clear();
    }

private:
    Gesture g_;
};

}
