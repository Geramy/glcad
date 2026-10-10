#include "test.hpp"

#include <cmath>

#include "ui/touch.hpp"

using namespace gcapp;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

TouchPoint finger(gcm::u64 id, f64 x, f64 y) {
    TouchPoint t;
    t.id = id;
    t.x = x;
    t.y = y;
    t.kind = TouchKind::Finger;
    t.down = true;
    return t;
}

TouchPoint pencil(gcm::u64 id, f64 x, f64 y) {
    TouchPoint t = finger(id, x, y);
    t.kind = TouchKind::Pencil;
    return t;
}

}

TEST(touch_the_cursor_is_lifted_out_from_under_the_finger) {
    TouchState t;
    f64 x = 0, y = 0;
    t.cursor_for(100.0, 200.0, TouchKind::Finger, x, y);
    CHECK_MSG(x != 100.0 || y != 200.0, "the cursor sat under the finger");
    CHECK(std::fabs(x - (100.0 + t.settings.finger_offset_x)) < 1e-9);
    CHECK(std::fabs(y - (200.0 + t.settings.finger_offset_y)) < 1e-9);
    CHECK(x < 100.0 && y < 200.0);
}

TEST(touch_a_left_handed_user_gets_the_mirror) {
    TouchState t;
    t.settings.left_handed = true;
    f64 x = 0, y = 0;
    t.cursor_for(100.0, 200.0, TouchKind::Finger, x, y);
    CHECK_MSG(x > 100.0, "the offset did not mirror");
    CHECK_MSG(y < 200.0, "the vertical offset should not mirror");
}

TEST(touch_a_pencil_draws_where_its_tip_is) {
    TouchState t;
    f64 x = 0, y = 0;
    t.cursor_for(100.0, 200.0, TouchKind::Pencil, x, y);
    CHECK_MSG(std::fabs(x - 100.0) < 1e-9 && std::fabs(y - 200.0) < 1e-9,
              "the pencil was offset to %g,%g", x, y);

    t.settings.offset_pencil = true;
    t.cursor_for(100.0, 200.0, TouchKind::Pencil, x, y);
    CHECK(std::fabs(x - 100.0) > 1.0);
}

TEST(touch_a_quick_touch_is_a_tap) {
    TouchState t;
    CHECK(t.update({finger(1, 50, 50)}, 0.0).gesture == Gesture::Press);
    const TouchResult up = t.update({}, 0.10);
    CHECK_MSG(up.gesture == Gesture::Tap, "got %s", gesture_name(up.gesture));
    CHECK(up.ended);
    CHECK(t.active() == 0);
}

TEST(touch_a_slow_touch_is_not_a_tap) {
    TouchState t;
    t.update({finger(1, 50, 50)}, 0.0);
    const TouchResult up = t.update({}, 5.0);
    CHECK_MSG(up.gesture != Gesture::Tap, "a five-second hold was a tap");
}

TEST(touch_holding_still_reports_a_press_before_it_commits) {
    TouchState t;
    const TouchResult down = t.update({finger(1, 50, 50)}, 0.0);
    CHECK(down.gesture == Gesture::Press);
    CHECK(down.began);
    CHECK(down.x != down.raw_x);
    CHECK(t.update({finger(1, 51, 50)}, 0.05).gesture == Gesture::Press);
}

TEST(touch_a_finger_that_wanders_becomes_a_drag) {
    TouchState t;
    t.update({finger(1, 50, 50)}, 0.0);
    const TouchResult moved = t.update({finger(1, 90, 50)}, 0.05);
    CHECK_MSG(moved.gesture == Gesture::Drag, "got %s",
              gesture_name(moved.gesture));
    CHECK_MSG(std::fabs(moved.dx - 40.0) < 1e-9,
              "the delta is %g, not from the last frame", moved.dx);

    const TouchResult more = t.update({finger(1, 100, 55)}, 0.08);
    CHECK(more.gesture == Gesture::Drag);
    CHECK(std::fabs(more.dx - 10.0) < 1e-9);
    CHECK(std::fabs(more.dy - 5.0) < 1e-9);
}

TEST(touch_a_tiny_wobble_is_still_a_tap) {
    TouchState t;
    t.update({finger(1, 50, 50)}, 0.0);
    t.update({finger(1, 53, 52)}, 0.05);
    const TouchResult up = t.update({}, 0.12);
    CHECK_MSG(up.gesture == Gesture::Tap, "a 3-point wobble broke the tap");
}

TEST(touch_a_long_press_summons_the_ring_once) {
    TouchState t;
    t.update({finger(1, 50, 50)}, 0.0);
    CHECK(t.update({finger(1, 50, 50)}, 0.30).gesture == Gesture::Press);

    const TouchResult fired = t.update({finger(1, 50, 50)}, 0.50);
    CHECK_MSG(fired.gesture == Gesture::LongPress, "got %s",
              gesture_name(fired.gesture));
    CHECK_MSG(t.update({finger(1, 50, 50)}, 0.60).gesture == Gesture::None,
              "the long press fired twice");
    CHECK(t.update({}, 0.70).gesture == Gesture::None);
}

TEST(touch_a_long_press_that_moves_is_a_drag_instead) {
    TouchState t;
    t.update({finger(1, 50, 50)}, 0.0);
    t.update({finger(1, 90, 50)}, 0.10);
    const TouchResult late = t.update({finger(1, 92, 50)}, 0.90);
    CHECK_MSG(late.gesture == Gesture::Drag,
              "a moving finger fired a long press");
}

TEST(touch_a_second_finger_cancels_a_long_press) {
    TouchState t;
    t.update({finger(1, 50, 50)}, 0.0);
    CHECK(t.update({finger(1, 50, 50)}, 0.50).gesture == Gesture::LongPress);
    t.update({finger(1, 50, 50), finger(2, 150, 50)}, 0.55);
    const TouchResult back = t.update({finger(1, 50, 50)}, 0.60);
    CHECK(back.gesture == Gesture::None);
}

TEST(touch_a_two_finger_gesture_waits_before_it_picks_a_verb) {
    TouchState t;
    t.update({finger(1, 100, 100)}, 0.0);
    const TouchResult landed =
        t.update({finger(1, 100, 100), finger(2, 200, 100)}, 0.05);
    CHECK_MSG(landed.gesture == Gesture::None,
              "the second finger committed to %s on its first frame",
              gesture_name(landed.gesture));
    CHECK(landed.began);

    const TouchResult tiny =
        t.update({finger(1, 101, 100), finger(2, 201, 100)}, 0.06);
    CHECK(tiny.gesture == Gesture::None);
}





TEST(touch_a_rotation_through_the_wrap_point_is_small) {
    TouchState t;
    t.update({finger(1, 200, 100)}, 0.0);
    t.update({finger(1, 200, 100), finger(2, 100, 99)}, 0.05);
    const TouchResult across =
        t.update({finger(1, 200, 100), finger(2, 100, 101)}, 0.10);
    if (across.gesture == Gesture::Rotate)
        CHECK_MSG(std::fabs(across.rotation) < 0.5,
                  "crossing the wrap point rotated by %g radians",
                  across.rotation);
}

TEST(touch_two_fingers_up_without_moving_is_the_tablets_right_click) {
    TouchState t;
    t.update({finger(1, 100, 100)}, 0.0);
    t.update({finger(1, 100, 100), finger(2, 200, 100)}, 0.02);
    t.update({finger(1, 100, 100), finger(2, 200, 100)}, 0.05);
    const TouchResult up = t.update({}, 0.12);
    CHECK_MSG(up.gesture == Gesture::TwoFingerTap, "got %s",
              gesture_name(up.gesture));
}

TEST(touch_lifting_one_finger_of_a_pan_does_not_start_drawing) {
    TouchState t;
    t.update({finger(1, 100, 100)}, 0.0);
    t.update({finger(1, 100, 100), finger(2, 200, 100)}, 0.05);
    t.update({finger(1, 140, 130), finger(2, 240, 130)}, 0.10);

    const TouchResult one = t.update({finger(1, 180, 160)}, 0.15);
    CHECK_MSG(one.gesture == Gesture::None,
              "lifting a finger mid-pan started a %s", gesture_name(one.gesture));

    const TouchResult up = t.update({}, 0.20);
    CHECK(up.gesture == Gesture::None || up.gesture == Gesture::TwoFingerTap);
    CHECK(t.active() == 0);
}

TEST(touch_a_new_gesture_starts_clean_after_the_last_one) {
    TouchState t;
    t.update({finger(1, 100, 100)}, 0.0);
    t.update({finger(1, 100, 100), finger(2, 200, 100)}, 0.05);
    t.update({finger(1, 140, 130), finger(2, 240, 130)}, 0.10);
    t.update({}, 0.20);

    CHECK(t.update({finger(3, 50, 50)}, 0.30).gesture == Gesture::Press);
    CHECK_MSG(t.update({}, 0.35).gesture == Gesture::Tap,
              "the previous gesture leaked into the next one");
}

TEST(touch_reset_clears_everything) {
    TouchState t;
    t.update({finger(1, 100, 100)}, 0.0);
    t.update({finger(1, 100, 100), finger(2, 200, 100)}, 0.05);
    CHECK(t.active() == 2);
    t.reset();
    CHECK(t.active() == 0);
    CHECK(t.current() == Gesture::None);
    CHECK(t.update({finger(9, 10, 10)}, 1.0).began);
}

TEST(touch_a_pencil_drag_reports_itself_as_one) {
    TouchState t;
    t.update({pencil(1, 50, 50)}, 0.0);
    const TouchResult drag = t.update({pencil(1, 90, 50)}, 0.05);
    CHECK(drag.gesture == Gesture::Drag);
    CHECK_MSG(drag.from_pencil, "a pencil stroke did not say so");
    CHECK_MSG(std::fabs(drag.x - 90.0) < 1e-9,
              "the pencil cursor was offset to %g", drag.x);
}

TEST(touch_gesture_names_exist) {
    for (Gesture g : {Gesture::None, Gesture::Tap, Gesture::Press, Gesture::Drag,
                      Gesture::Pan, Gesture::Zoom, Gesture::Rotate,
                      Gesture::LongPress, Gesture::TwoFingerTap})
        CHECK(gesture_name(g)[0] != '\0');
}

TEST(touch_an_empty_frame_before_anything_happens_is_harmless) {
    TouchState t;
    const TouchResult r = t.update({}, 0.0);
    CHECK(r.gesture == Gesture::None);
    CHECK(!r.began && !r.ended);
}

TEST(touch_a_drag_begins_when_it_moves_and_ends_when_it_lifts) {
    TouchState t;
    f64 now = 0.0;
    auto r = t.update({pencil(1, 100, 100)}, now);
    CHECK(r.gesture == Gesture::Press);
    now += 0.05;
    r = t.update({pencil(1, 140, 100)}, now);
    CHECK_MSG(r.gesture == Gesture::Drag, "moving past the slop was '%s'", gesture_name(r.gesture));
    CHECK_MSG(r.began, "the first drag frame did not begin the drag");
    now += 0.05;
    r = t.update({pencil(1, 180, 100)}, now);
    CHECK(r.gesture == Gesture::Drag);
    CHECK_MSG(!r.began, "a continuing drag began twice");
    now += 0.05;
    r = t.update({}, now);
    CHECK_MSG(r.gesture == Gesture::Drag && r.ended, "the lift was '%s' ended=%d",
              gesture_name(r.gesture), int(r.ended));
    CHECK_MSG(r.from_pencil, "the lift forgot whose drag it was");
}

TEST(touch_a_finger_drag_lift_closes_the_camera_gesture) {
    TouchState t;
    f64 now = 0.0;
    t.update({finger(7, 300, 300)}, now);
    now += 0.05;
    auto r = t.update({finger(7, 360, 320)}, now);
    CHECK(r.gesture == Gesture::Drag && r.began && !r.from_pencil);
    now += 0.05;
    r = t.update({}, now);
    CHECK_MSG(r.gesture == Gesture::Drag && r.ended && !r.from_pencil,
              "the finger lift was '%s'", gesture_name(r.gesture));
}

TEST(touch_two_fingers_pan_and_zoom_in_the_same_motion) {
    TouchState t;
    f64 now = 0.0;
    t.update({finger(1, 100, 100), finger(2, 200, 100)}, now);
    now += 0.05;
    auto r = t.update({finger(1, 120, 140), finger(2, 260, 140)}, now);
    CHECK_MSG(r.gesture == Gesture::Pan, "got %s", gesture_name(r.gesture));
    CHECK_MSG(r.dx > 0.0 && r.dy > 0.0, "the slide did not pan (%g, %g)", r.dx, r.dy);
    CHECK_MSG(r.scale > 1.0, "the spread did not zoom (scale %g)", r.scale);
    now += 0.05;
    r = t.update({finger(1, 110, 140), finger(2, 270, 140)}, now);
    CHECK_MSG(r.scale > 1.0, "a further spread stopped zooming");
    CHECK_MSG(std::fabs(r.dx) < 1e-9, "a symmetric spread invented a pan of %g", r.dx);
}

TEST(touch_a_second_finger_ends_the_drag_it_interrupts) {
    TouchState t;
    f64 now = 0.0;
    t.update({finger(1, 100, 100)}, now);
    now += 0.05;
    auto r = t.update({finger(1, 160, 120)}, now);
    CHECK(r.gesture == Gesture::Drag && r.began);
    now += 0.05;
    r = t.update({finger(1, 160, 120), finger(2, 300, 300)}, now);
    CHECK_MSG(r.gesture == Gesture::Drag && r.ended,
              "the interrupted drag reported '%s' ended=%d", gesture_name(r.gesture), int(r.ended));
    CHECK_MSG(!r.from_pencil, "whose drag was it?");
}

TEST(touch_finger_taps_tolerate_finger_wobble_and_hold_in_points) {
    TouchState t;
    t.settings.scale = 2.0;
    t.update({finger(1, 100, 100)}, 0.0);
    t.update({finger(1, 130, 110)}, 0.15);
    const TouchResult up = t.update({}, 0.35);
    CHECK_MSG(up.gesture == Gesture::Tap, "a 16 pt, 0.35 s finger tap was %s", gesture_name(up.gesture));

    TouchState p;
    p.settings.scale = 2.0;
    p.update({pencil(1, 100, 100)}, 0.0);
    CHECK(p.update({pencil(1, 130, 110)}, 0.05).gesture == Gesture::Drag);
}

TEST(touch_thresholds_follow_the_points_scale) {
    TouchState t;
    t.settings.scale = 2.0;
    t.update({finger(1, 100, 100)}, 0.0);
    CHECK(t.update({finger(1, 135, 100)}, 0.05).gesture == Gesture::Press);
    CHECK(t.update({finger(1, 139, 100)}, 0.10).gesture == Gesture::Press);
    CHECK(t.update({finger(1, 145, 100)}, 0.14).gesture == Gesture::Drag);
    double x, y;
    t.cursor_for(100, 100, TouchKind::Finger, x, y);
    CHECK(std::fabs(x - (100 + t.settings.finger_offset_x * 2.0)) < 1e-9);
}
