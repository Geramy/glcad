#include "test.hpp"

#include <cmath>
#include <string>

#include "render/views.hpp"
#include "render/viewcube.hpp"

using namespace gview;

namespace {
constexpr f64 kPi = 3.14159265358979323846;
f64 dist3(Point3 a, Point3 b) {
    return std::sqrt(std::pow(a.x.to_f64() - b.x.to_f64(), 2) +
                     std::pow(a.y.to_f64() - b.y.to_f64(), 2) +
                     std::pow(a.z.to_f64() - b.z.to_f64(), 2));
}
}

TEST(view_state_round_trips_a_camera_exactly) {
    Camera c;
    c.set_target(Point3::from(3, -4, 5));
    c.set_distance(42.0);
    c.set_orbit(0.7, -0.3);
    c.set_projection(Projection::Orthographic);

    const ViewState s = ViewState::from(c);

    Camera other;
    s.apply(other);
    CHECK(std::fabs(other.distance() - 42.0) < 1e-12);
    CHECK(std::fabs(other.azimuth() - 0.7) < 1e-12);
    CHECK(std::fabs(other.elevation() + 0.3) < 1e-12);
    CHECK(other.projection() == Projection::Orthographic);
    CHECK(dist3(other.target(), c.target()) < 1e-9);
    CHECK(ViewState::from(other).same_as(s));
}

TEST(view_state_stores_parameters_not_a_matrix) {
    Camera c;
    c.set_orbit(0.5, 0.4);
    ViewState s = ViewState::from(c);
    s.projection = Projection::Orthographic;

    Camera other;
    s.apply(other);
    CHECK(other.projection() == Projection::Orthographic);
    CHECK(std::fabs(other.azimuth() - 0.5) < 1e-12);
}

TEST(view_table_ships_the_standards) {
    ViewTable t;
    CHECK(t.size() >= 7);
    CHECK(t.find("Top") != nullptr);
    CHECK(t.find("Front") != nullptr);
    CHECK(t.find("SW Isometric") != nullptr);
    CHECK(t.find("Nonexistent") == nullptr);
}

TEST(view_capture_and_restore) {
    ViewTable t;
    Camera c;
    c.set_target(Point3::from(10, 20, 30));
    c.set_distance(7.5);
    c.set_orbit(1.1, 0.2);

    t.capture(c, "SECTION A", "through the stair core");
    CHECK(t.find("SECTION A") != nullptr);
    CHECK(t.find("SECTION A")->description == "through the stair core");

    Camera other;
    CHECK(t.restore(other, "SECTION A"));
    CHECK(std::fabs(other.distance() - 7.5) < 1e-12);
    CHECK(dist3(other.target(), c.target()) < 1e-9);

    CHECK(!t.restore(other, "NOT A VIEW"));
}

TEST(view_a_locked_view_cannot_be_deleted) {
    ViewTable t;
    CHECK_MSG(!t.remove("Top"), "a standard view was deleted");

    Camera c;
    t.capture(c, "MINE");
    CHECK(t.remove("MINE"));
    CHECK(t.find("MINE") == nullptr);
    CHECK(!t.remove("MINE"));
}

TEST(view_rename_refuses_a_clash) {
    ViewTable t;
    Camera c;
    t.capture(c, "A");
    t.capture(c, "B");
    CHECK(!t.rename("A", "B"));
    CHECK(!t.rename("A", ""));
    CHECK(!t.rename("MISSING", "C"));
    CHECK(t.rename("A", "C"));
    CHECK(t.find("C") != nullptr);
    CHECK(t.find("A") == nullptr);
}

TEST(view_capture_replaces_by_name) {
    ViewTable t;
    Camera c;
    c.set_distance(5.0);
    t.capture(c, "WORKING");
    const std::size_t before = t.size();

    c.set_distance(50.0);
    t.capture(c, "WORKING");
    CHECK(t.size() == before);
    CHECK(std::fabs(t.find("WORKING")->state.distance - 50.0) < 1e-12);
}

TEST(view_history_goes_back_and_forward) {
    ViewHistory h;
    Camera c;

    c.set_distance(10.0);
    h.record(c);
    c.set_distance(5.0);
    h.record(c);
    c.set_distance(2.0);
    h.record(c);
    CHECK(h.size() == 3);

    CHECK(h.can_go_back());
    CHECK(!h.can_go_forward());
    CHECK(h.back(c));
    CHECK_MSG(std::fabs(c.distance() - 5.0) < 1e-12, "went back to %.3f",
              c.distance());
    CHECK(h.back(c));
    CHECK(std::fabs(c.distance() - 10.0) < 1e-12);
    CHECK(!h.can_go_back());
    CHECK(!h.back(c));

    CHECK(h.forward(c));
    CHECK(std::fabs(c.distance() - 5.0) < 1e-12);
}

TEST(view_history_collapses_repeats) {
    ViewHistory h;
    Camera c;
    c.set_distance(10.0);
    for (int i = 0; i < 20; ++i) h.record(c);
    CHECK_MSG(h.size() == 1, "%zu entries for one position", h.size());
}

TEST(view_going_back_is_not_itself_a_new_view) {
    ViewHistory h;
    Camera c;
    c.set_distance(10.0);
    h.record(c);
    c.set_distance(5.0);
    h.record(c);
    c.set_distance(2.0);
    h.record(c);

    h.back(c);
    h.record(c);
    CHECK(h.can_go_back());
    h.back(c);
    CHECK_MSG(std::fabs(c.distance() - 10.0) < 1e-12,
              "landed at %.3f rather than the oldest view", c.distance());
}

TEST(view_a_new_view_discards_the_forward_entries) {
    ViewHistory h;
    Camera c;
    c.set_distance(10.0);
    h.record(c);
    c.set_distance(5.0);
    h.record(c);
    h.back(c);
    CHECK(h.can_go_forward());

    c.set_distance(99.0);
    h.record(c);
    CHECK_MSG(!h.can_go_forward(), "a stale forward entry survived");
    CHECK(h.size() == 2);
}

TEST(view_history_is_bounded) {
    ViewHistory h(8);
    Camera c;
    for (int i = 1; i <= 40; ++i) {
        c.set_distance(static_cast<f64>(i));
        h.record(c);
    }
    CHECK_MSG(h.size() == 8, "%zu entries past the limit", h.size());
}

TEST(view_history_clear) {
    ViewHistory h;
    Camera c;
    h.record(c);
    c.set_distance(3.0);
    h.record(c);
    h.clear();
    CHECK(h.size() == 0);
    CHECK(!h.can_go_back());
    CHECK(!h.back(c));
}

TEST(walk_moves_the_eye_through_the_model) {
    Camera c;
    c.set_target(Point3::from(0, 0, 1.7));
    c.set_distance(5.0);
    c.set_orbit(0.0, 0.0);

    Walker w;
    w.begin(c);
    CHECK(w.active());

    const Point3 before = w.eye(c);
    WalkInput in;
    in.forward = true;
    in.dt = 1.0;
    w.settings.speed = 2.0;
    w.step(c, in);

    const Point3 after = w.eye(c);
    CHECK_MSG(dist3(before, after) > 1.0, "the eye moved only %.4f",
              dist3(before, after));
}

TEST(walk_keeps_the_eye_at_head_height) {
    Camera c;
    c.set_orbit(0.3, -0.6);
    Walker w;
    w.settings.eye_height = 1.7;
    w.settings.ground_z = 0.0;
    w.begin(c);

    WalkInput in;
    in.forward = true;
    in.dt = 0.5;
    for (int i = 0; i < 10; ++i) w.step(c, in);

    const Point3 e = w.eye(c);
    CHECK_MSG(std::fabs(e.z.to_f64() - 1.7) < 1e-6,
              "the eye drifted to z = %.6f", e.z.to_f64());
}

TEST(walk_looking_down_does_not_drive_you_into_the_floor) {
    Camera c;
    c.set_orbit(0.0, -1.2);
    Walker w;
    w.begin(c);
    set_eye(c, Point3::from(0, 0, w.settings.eye_height));

    const Point3 before = w.eye(c);
    WalkInput in;
    in.forward = true;
    in.dt = 1.0;
    w.step(c, in);
    const Point3 after = w.eye(c);

    CHECK_MSG(std::fabs(after.z.to_f64() - before.z.to_f64()) < 1e-6,
              "walking while looking down changed height by %.6f",
              after.z.to_f64() - before.z.to_f64());
    CHECK(std::hypot(after.x.to_f64() - before.x.to_f64(),
                     after.y.to_f64() - before.y.to_f64()) > 0.1);
}

TEST(fly_moves_in_all_three_axes) {
    Camera c;
    c.set_orbit(0.0, 0.0);
    Walker w;
    w.settings.mode = WalkMode::Fly;
    w.begin(c);

    const Point3 before = w.eye(c);
    WalkInput in;
    in.up = true;
    in.dt = 1.0;
    w.settings.speed = 3.0;
    w.step(c, in);

    const Point3 after = w.eye(c);
    CHECK_MSG(after.z.to_f64() > before.z.to_f64() + 1.0,
              "flying up changed height by %.6f",
              after.z.to_f64() - before.z.to_f64());
}

TEST(walk_looking_turns_about_the_eye_not_the_target) {
    Camera c;
    c.set_orbit(0.0, 0.0);
    Walker w;
    w.begin(c);

    const Point3 before = w.eye(c);
    WalkInput in;
    in.look_dx = 100.0;
    in.dt = 0.016;
    w.step(c, in);

    CHECK_MSG(std::fabs(c.azimuth()) > 1e-6, "looking did not turn the camera");
    CHECK_MSG(dist3(before, w.eye(c)) < 0.05,
              "the eye moved %.6f while merely looking", dist3(before, w.eye(c)));
}

TEST(walk_pitch_is_clamped_short_of_straight_up) {
    Camera c;
    Walker w;
    w.begin(c);
    WalkInput in;
    in.look_dy = 10000.0;
    w.step(c, in);
    CHECK_MSG(std::fabs(c.elevation()) < kPi * 0.5,
              "the camera flipped over the pole (%.6f)", c.elevation());
}

TEST(walk_does_nothing_until_it_is_begun) {
    Camera c;
    const f64 before = c.azimuth();
    Walker w;
    WalkInput in;
    in.forward = true;
    in.look_dx = 50.0;
    w.step(c, in);
    CHECK(!w.active());
    CHECK(std::fabs(c.azimuth() - before) < 1e-12);
}

TEST(walk_fast_modifier_moves_further) {
    auto travel = [](bool fast) {
        Camera c;
        c.set_orbit(0.0, 0.0);
        Walker w;
        w.begin(c);
        const Point3 before = w.eye(c);
        WalkInput in;
        in.forward = true;
        in.fast = fast;
        in.dt = 1.0;
        w.step(c, in);
        return dist3(before, w.eye(c));
    };
    CHECK_MSG(travel(true) > travel(false) * 2.0, "the fast modifier did little");
}

TEST(cube_drag_turns_the_model) {
    Camera c;
    c.set_orbit(0.0, 0.0);
    const CubeLayout layout = CubeLayout::top_right(1000, 800, 110, 16);

    ViewCube::Drag d;
    CHECK_MSG(ViewCube::begin_drag(c, layout, layout.x + 20, layout.y + 20, d),
              "a press inside the cube did not start a drag");
    CHECK(d.active);

    ViewCube::update_drag(c, d, layout.x + 120, layout.y + 20);
    CHECK_MSG(std::fabs(c.azimuth()) > 0.1, "dragging did not orbit (%.6f)",
              c.azimuth());
}

TEST(cube_a_short_gesture_is_a_click_not_a_drag) {
    Camera c;
    const CubeLayout layout = CubeLayout::top_right(1000, 800, 110, 16);

    ViewCube::Drag d;
    ViewCube::begin_drag(c, layout, layout.x + 40, layout.y + 40, d);
    ViewCube::update_drag(c, d, layout.x + 41, layout.y + 40);
    CHECK_MSG(ViewCube::end_drag(d), "a one-pixel move was treated as a drag");
    CHECK(!d.active);

    ViewCube::Drag e;
    ViewCube::begin_drag(c, layout, layout.x + 40, layout.y + 40, e);
    ViewCube::update_drag(c, e, layout.x + 140, layout.y + 40);
    CHECK_MSG(!ViewCube::end_drag(e), "a hundred-pixel drag was treated as a click");
}

TEST(cube_a_press_outside_the_widget_starts_nothing) {
    Camera c;
    const CubeLayout layout = CubeLayout::top_right(1000, 800, 110, 16);
    ViewCube::Drag d;
    CHECK(!ViewCube::begin_drag(c, layout, 10, 700, d));
    CHECK(!d.active);
    const f64 before = c.azimuth();
    ViewCube::update_drag(c, d, 500, 500);
    CHECK(std::fabs(c.azimuth() - before) < 1e-12);
}
