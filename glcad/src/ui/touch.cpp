#include "ui/touch.hpp"

#include <algorithm>
#include <cmath>

namespace gcapp {
namespace {

constexpr f64 kPi = 3.14159265358979323846;

f64 distance(f64 ax, f64 ay, f64 bx, f64 by) {
    return std::hypot(bx - ax, by - ay);
}

f64 angle_delta(f64 from, f64 to) {
    f64 d = to - from;
    while (d > kPi) d -= 2.0 * kPi;
    while (d < -kPi) d += 2.0 * kPi;
    return d;
}

}

const char* gesture_name(Gesture g) noexcept {
    switch (g) {
        case Gesture::None:         return "none";
        case Gesture::Tap:          return "tap";
        case Gesture::Press:        return "press";
        case Gesture::Drag:         return "drag";
        case Gesture::Pan:          return "pan";
        case Gesture::Zoom:         return "zoom";
        case Gesture::Rotate:       return "rotate";
        case Gesture::LongPress:    return "long press";
        case Gesture::TwoFingerTap: return "two-finger tap";
    }
    return "";
}

void TouchState::reset() {
    points_.clear();
    current_ = Gesture::None;
    committed_ = false;
    long_press_fired_ = false;
    peak_ = 0;
}

TouchState::Tracked* TouchState::find(gcm::u64 id) {
    for (Tracked& t : points_)
        if (t.id == id) return &t;
    return nullptr;
}

void TouchState::cursor_for(f64 x, f64 y, TouchKind kind, f64& out_x,
                            f64& out_y) const {
    if (kind == TouchKind::Pencil && !settings.offset_pencil) {
        out_x = x;
        out_y = y;
        return;
    }
    const f64 sign = settings.left_handed ? -1.0 : 1.0;
    out_x = x + settings.finger_offset_x * sign;
    out_y = y + settings.finger_offset_y;
}

TouchResult TouchState::update(const std::vector<TouchPoint>& touches, f64 now) {
    TouchResult out;

    std::vector<Tracked> kept;
    for (const TouchPoint& t : touches) {
        if (!t.down) continue;
        if (Tracked* existing = find(t.id)) {
            Tracked u = *existing;
            u.x = t.x;
            u.y = t.y;
            if (distance(u.start_x, u.start_y, u.x, u.y) > settings.tap_slop)
                u.moved = true;
            kept.push_back(u);
        } else {
            Tracked u;
            u.id = t.id;
            u.start_x = u.x = t.x;
            u.start_y = u.y = t.y;
            u.started_at = now;
            u.kind = t.kind;
            kept.push_back(u);
        }
    }

    const std::size_t was = points_.size();
    const std::vector<Tracked> previous = points_;
    points_ = kept;
    const std::size_t is = points_.size();
    peak_ = std::max(peak_, is);

    if (is == 0) {
        if (was == 0) return out;

        out.ended = true;
        const Tracked& last = previous.front();
        out.raw_x = last.x;
        out.raw_y = last.y;
        cursor_for(last.x, last.y, last.kind, out.x, out.y);
        out.from_pencil = last.kind == TouchKind::Pencil;

        const f64 held = now - last.started_at;
        if (peak_ >= 2 && !committed_) {
            out.gesture = Gesture::TwoFingerTap;
        } else if (long_press_fired_) {
            out.gesture = Gesture::None;
        } else if (!last.moved && held <= settings.tap_seconds && peak_ == 1) {
            out.gesture = Gesture::Tap;
        } else if (current_ == Gesture::Drag) {
            // Report the lift as a Drag so the platform closes its grab or stroke.
            out.gesture = Gesture::Drag;
        } else {
            out.gesture = Gesture::None;
        }
        current_ = Gesture::None;
        committed_ = false;
        long_press_fired_ = false;
        peak_ = 0;
        return out;
    }

    if (is == 1) {
        const Tracked& t = points_.front();
        out.raw_x = t.x;
        out.raw_y = t.y;
        cursor_for(t.x, t.y, t.kind, out.x, out.y);
        out.from_pencil = t.kind == TouchKind::Pencil;
        out.began = was == 0;

        if (peak_ >= 2) {
            out.gesture = Gesture::None;
            current_ = Gesture::None;
            return out;
        }

        const f64 held = now - t.started_at;
        const f64 wander = distance(t.start_x, t.start_y, t.x, t.y);

        if (!t.moved && !long_press_fired_ &&
            held >= settings.long_press_seconds &&
            wander <= settings.long_press_slop) {
            long_press_fired_ = true;
            out.gesture = Gesture::LongPress;
            current_ = Gesture::LongPress;
            return out;
        }
        if (long_press_fired_) {
            out.gesture = Gesture::None;
            return out;
        }

        if (t.moved) {
            out.gesture = Gesture::Drag;
            out.began = current_ != Gesture::Drag;
            if (const Tracked* before = [&]() -> const Tracked* {
                    for (const Tracked& p : previous)
                        if (p.id == t.id) return &p;
                    return nullptr;
                }()) {
                out.dx = t.x - before->x;
                out.dy = t.y - before->y;
            }
            current_ = Gesture::Drag;
            return out;
        }

        out.gesture = Gesture::Press;
        current_ = Gesture::Press;
        return out;
    }

    const Tracked& a = points_[0];
    const Tracked& b = points_[1];
    const f64 cx = (a.x + b.x) * 0.5, cy = (a.y + b.y) * 0.5;
    const f64 span = distance(a.x, a.y, b.x, b.y);
    const f64 angle = std::atan2(b.y - a.y, b.x - a.x);

    out.raw_x = cx;
    out.raw_y = cy;
    out.x = cx;
    out.y = cy;
    out.centre_x = cx;
    out.centre_y = cy;

    if (was < 2) {
        last_distance_ = span;
        last_angle_ = angle;
        last_centre_x_ = cx;
        last_centre_y_ = cy;
        committed_ = false;
        out.began = true;
        // End an in-flight drag explicitly or the platform's grab stays held.
        if (current_ == Gesture::Drag) {
            const Tracked* first = nullptr;
            for (const Tracked& t : previous)
                if (t.moved) { first = &t; break; }
            out.gesture = Gesture::Drag;
            out.ended = true;
            if (first) {
                out.raw_x = first->x;
                out.raw_y = first->y;
                cursor_for(first->x, first->y, first->kind, out.x, out.y);
                out.from_pencil = first->kind == TouchKind::Pencil;
            }
        } else {
            out.gesture = Gesture::None;
        }
        current_ = Gesture::None;
        long_press_fired_ = false;
        return out;
    }

    const f64 pan = distance(last_centre_x_, last_centre_y_, cx, cy);
    const f64 spread = std::fabs(span - last_distance_);
    const f64 turn = std::fabs(angle_delta(last_angle_, angle)) * span * 0.5;

    if (!committed_) {
        const f64 best = std::max({pan, spread, turn});
        if (best < settings.two_finger_slop) {
            out.gesture = Gesture::None;
            last_centre_x_ = cx;
            last_centre_y_ = cy;
            last_distance_ = span;
            last_angle_ = angle;
            return out;
        }
        current_ = Gesture::Pan;
        committed_ = true;
        out.began = true;
    }

    out.gesture = current_;
    out.dx = cx - last_centre_x_;
    out.dy = cy - last_centre_y_;
    out.scale = last_distance_ > 1e-6 ? span / last_distance_ : 1.0;
    out.rotation = angle_delta(last_angle_, angle);

    last_centre_x_ = cx;
    last_centre_y_ = cy;
    last_distance_ = span;
    last_angle_ = angle;
    return out;
}

}
