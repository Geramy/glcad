#pragma once

#include <algorithm>
#include <vector>

#include "engine/event.hpp"

namespace gci {

template <class D>
class SourceBase {
public:
    const D& self() const noexcept { return static_cast<const D&>(*this); }
    D&       self() noexcept { return static_cast<D&>(*this); }

    DeviceCaps  caps() const { return self().caps_impl(); }
    const char* name() const { return self().name_impl(); }

    std::size_t poll(std::vector<InputEvent>& out) {
        raw_.clear();
        self().poll_impl(raw_);

        const DeviceCaps c = caps();
        for (InputEvent& e : raw_) {
            normalize(e, c);
            if (e.phase == Phase::Hover && !c.has_hover) continue;
            out.push_back(e);
        }
        // Device timestamps are the ordering key, not arrival order.
        std::stable_sort(out.begin(), out.end(),
                         [](const InputEvent& a, const InputEvent& b) {
                             return a.timestamp < b.timestamp;
                         });
        return raw_.size();
    }

private:
    // No pressure sensor: report full pressure in contact, not zero.
    static void normalize(InputEvent& e, const DeviceCaps& c) {
        e.device_class = c.device_class;

        if (!c.has_pressure) {
            e.pressure = (e.phase == Phase::Down || e.phase == Phase::Move) ? 1.0f : 0.0f;
        } else {
            e.pressure = std::clamp(e.pressure, 0.0f, 1.0f);
        }

        if (!c.has_tilt) {
            e.tilt_altitude = 1.5707963267948966f;   // perpendicular
            e.tilt_azimuth  = 0.0f;
        }
        if (!c.has_barrel_roll) e.barrel_roll = 0.0f;
        if (!c.has_contact_size) e.contact_size = 0.0f;

        if (e.is_predicted) e.is_coalesced = false;
    }

    std::vector<InputEvent> raw_;
};

}
