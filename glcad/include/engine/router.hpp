#pragma once

#include "engine/event.hpp"

namespace gci {

enum class Intent : u8 {
    None,
    Hover,
    Draw,
    Pick,
    Pan,
    Orbit,
    Zoom,
    ContextMenu,
    ToolCycle,
    RollManipulator,
};

const char* intent_name(Intent i) noexcept;

struct Mode3D {
    static constexpr Intent one_finger_on_empty() noexcept { return Intent::Orbit; }
    static constexpr const char* name() noexcept { return "3D"; }
};

struct Mode2D {
    static constexpr Intent one_finger_on_empty() noexcept { return Intent::Pan; }
    static constexpr const char* name() noexcept { return "2D"; }
};

struct RouterState {
    int  active_touches{0};
    bool over_geometry{false};
    bool tool_active{false};
};

template <class ModePolicy>
class Router {
public:
    using Mode = ModePolicy;

    static Intent route(const InputEvent& e, const RouterState& s) noexcept {
        switch (e.device_class) {
            case DeviceClass::Stylus:   return route_stylus(e, s);
            case DeviceClass::Touch:    return route_touch(e, s);
            case DeviceClass::Trackpad: return route_trackpad(e, s);
            case DeviceClass::Mouse:    return route_mouse(e, s);
            default:                    return Intent::None;
        }
    }

private:
    static Intent route_stylus(const InputEvent& e, const RouterState& s) noexcept {
        if (e.buttons & Button_Squeeze) return Intent::ContextMenu;
        if (e.buttons & Button_Barrel)  return Intent::ContextMenu;

        switch (e.phase) {
            case Phase::Hover: return Intent::Hover;
            case Phase::Down:
            case Phase::Move:  return s.tool_active ? Intent::Draw : Intent::Pick;
            default:           return Intent::None;
        }
    }

    static Intent route_touch(const InputEvent& e, const RouterState& s) noexcept {
        if (e.phase == Phase::Hover) return Intent::None;

        if (s.active_touches >= 2) {
            return (s.active_touches >= 3) ? Intent::Pan : Intent::Zoom;
        }
        if (s.active_touches == 1) {
            return s.over_geometry ? Intent::Pan : ModePolicy::one_finger_on_empty();
        }
        return Intent::None;
    }

    static Intent route_trackpad(const InputEvent& e, const RouterState& s) noexcept {
        if (e.modifiers & Mod_Alt) return Intent::Orbit;
        if (e.phase == Phase::Move && e.buttons == Button_None) return Intent::Pan;
        return route_mouse(e, s);
    }

    static Intent route_mouse(const InputEvent& e, const RouterState& s) noexcept {
        if (e.buttons & Button_Barrel) return Intent::ContextMenu;
        if (e.buttons & Button_Middle) {
            return (ModePolicy::one_finger_on_empty() == Intent::Orbit &&
                    !(e.modifiers & Mod_Shift))
                       ? Intent::Orbit
                       : Intent::Pan;
        }
        switch (e.phase) {
            case Phase::Hover: return Intent::Hover;
            case Phase::Down:
            case Phase::Move:
                if (!(e.buttons & Button_Primary) && e.phase == Phase::Move)
                    return Intent::Hover;
                return s.tool_active ? Intent::Draw : Intent::Pick;
            default: return Intent::None;
        }
    }
};

constexpr bool may_commit(const InputEvent& e) noexcept { return !e.is_predicted; }

inline const char* intent_name(Intent i) noexcept {
    switch (i) {
        case Intent::Hover:           return "Hover";
        case Intent::Draw:            return "Draw";
        case Intent::Pick:            return "Pick";
        case Intent::Pan:             return "Pan";
        case Intent::Orbit:           return "Orbit";
        case Intent::Zoom:            return "Zoom";
        case Intent::ContextMenu:     return "ContextMenu";
        case Intent::ToolCycle:       return "ToolCycle";
        case Intent::RollManipulator: return "RollManipulator";
        default:                      return "None";
    }
}

}
