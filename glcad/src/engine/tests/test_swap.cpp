#include "test.hpp"

#include <cmath>

#include "engine/router.hpp"
#include "engine/sources.hpp"

using namespace gci;

static Gesture stroke() {
    Gesture g;
    for (int i = 0; i < 8; ++i)                       // approach, not touching
        g.push_back({100.0 + i, 200.0, 0.001 * i, false});
    for (int i = 0; i < 40; ++i)                      // drawing
        g.push_back({108.0 + i * 2.5, 200.0 + i * 1.25, 0.01 + 0.001 * i, true});
    return g;
}

template <class Source>
static std::vector<InputEvent> run(Source src) {
    std::vector<InputEvent> out;
    src.poll(out);
    return out;
}

template <class Source>
static std::vector<Intent> intents_of(Source src, bool tool_active) {
    RouterState s;
    s.tool_active = tool_active;
    std::vector<Intent> r;
    for (const auto& e : run(std::move(src)))
        r.push_back(Router<Mode3D>::route(e, s));
    return r;
}

TEST(input_backends_are_swappable_for_the_drawing_path) {
    auto only_draw = [](const std::vector<InputEvent>& ev,
                        const std::vector<Intent>& in) {
        std::vector<std::pair<f64, f64>> pts;
        for (std::size_t i = 0; i < ev.size(); ++i)
            if (in[i] == Intent::Draw) pts.emplace_back(ev[i].x, ev[i].y);
        return pts;
    };

    const auto pencil_ev = run(ApplePencilLike(stroke()));
    const auto ink_ev    = run(WindowsInkLike(stroke()));
    const auto wacom_ev  = run(LibinputTabletLike(stroke()));

    const auto pencil = only_draw(pencil_ev, intents_of(ApplePencilLike(stroke()), true));
    const auto ink    = only_draw(ink_ev,    intents_of(WindowsInkLike(stroke()), true));
    const auto wacom  = only_draw(wacom_ev,  intents_of(LibinputTabletLike(stroke()), true));

    CHECK_MSG(pencil.size() == 40, "pencil produced %zu draw points", pencil.size());
    CHECK(pencil == ink);
    CHECK(pencil == wacom);
}

TEST(input_normalization_fills_missing_capabilities) {
    for (const auto& e : run(MouseLike(stroke()))) {
        if (e.buttons & Button_Primary) CHECK(e.pressure == 1.0f);
        CHECK(std::fabs(e.tilt_altitude - 1.5707963267948966f) < 1e-6f);
        CHECK(e.barrel_roll == 0.0f);
    }
}

TEST(input_normalization_discards_values_the_device_cannot_report) {
    // WindowsInkLike emits garbage barrel_roll while declaring it unsupported.
    for (const auto& e : run(WindowsInkLike(stroke())))
        CHECK(e.barrel_roll == 0.0f);

    // MouseLike emits a bogus pressure while declaring no pressure support.
    for (const auto& e : run(MouseLike(stroke())))
        CHECK(e.pressure == 0.0f || e.pressure == 1.0f);
}

TEST(input_hover_only_from_devices_that_have_it) {
    auto count_hover = [](const std::vector<InputEvent>& ev) {
        int n = 0;
        for (const auto& e : ev) if (e.phase == Phase::Hover) ++n;
        return n;
    };
    CHECK(count_hover(run(ApplePencilLike(stroke()))) == 8);
    CHECK(count_hover(run(WindowsInkLike(stroke())))  == 8);
    CHECK(count_hover(run(LibinputTabletLike(stroke()))) == 0);  // opaque tablet
}

TEST(input_law_stylus_creates_fingers_navigate) {
    RouterState s;
    s.tool_active = true;

    for (const auto& e : run(ApplePencilLike(stroke()))) {
        const Intent i = Router<Mode3D>::route(e, s);
        CHECK(i != Intent::Pan);
        CHECK(i != Intent::Orbit);
        CHECK(i != Intent::Zoom);
    }

    RouterState t;
    t.active_touches = 1;
    t.tool_active    = true;
    for (const auto& e : run(TouchscreenLike(stroke()))) {
        const Intent i = Router<Mode3D>::route(e, t);
        CHECK(i != Intent::Draw);
    }
}

TEST(input_mode_policy_is_the_only_2d_3d_difference) {
    RouterState s;
    s.active_touches = 1;
    s.over_geometry  = false;

    InputEvent e;
    e.device_class = DeviceClass::Touch;
    e.phase        = Phase::Move;

    CHECK(Router<Mode3D>::route(e, s) == Intent::Orbit);
    CHECK(Router<Mode2D>::route(e, s) == Intent::Pan);

    InputEvent pen;
    pen.device_class = DeviceClass::Stylus;
    pen.phase        = Phase::Move;
    RouterState d;
    d.tool_active = true;
    CHECK(Router<Mode3D>::route(pen, d) == Router<Mode2D>::route(pen, d));

    InputEvent two;
    two.device_class = DeviceClass::Touch;
    two.phase        = Phase::Move;
    RouterState t2;
    t2.active_touches = 2;
    CHECK(Router<Mode3D>::route(two, t2) == Router<Mode2D>::route(two, t2));
}

TEST(input_predicted_samples_may_never_commit) {
    InputEvent e;
    e.is_predicted = true;
    CHECK(!may_commit(e));

    e.is_predicted = false;
    CHECK(may_commit(e));

    Gesture g{{1.0, 2.0, 0.0, true}};
    ApplePencilLike src(g);
    std::vector<InputEvent> out;
    src.poll(out);
    for (const auto& ev : out) CHECK(!(ev.is_predicted && ev.is_coalesced));
}

TEST(input_high_rate_devices_are_identified) {
    // Apple Pencil: 240 Hz on a 120 Hz display is 2 samples per frame.
    CHECK(std::fabs(ApplePencilLike(Gesture{}).caps().samples_per_frame(120.0) - 2.0) < 1e-9);
    CHECK(ApplePencilLike(Gesture{}).caps().needs_coalescing(120.0));
    CHECK(LibinputTabletLike(Gesture{}).caps().needs_coalescing(120.0));

    // A 125 Hz mouse delivers ~1 sample per 120 Hz frame.
    CHECK(MouseLike(Gesture{}).caps().samples_per_frame(120.0) < 1.1);
    CHECK(!MouseLike(Gesture{}).caps().needs_coalescing(120.0));

    // Windows Ink at 133 Hz needs coalescing only against a 60 Hz display.
    CHECK(!WindowsInkLike(Gesture{}).caps().needs_coalescing(120.0));
    CHECK(WindowsInkLike(Gesture{}).caps().needs_coalescing(60.0));
}

TEST(input_magnetism_scales_with_device_class) {
    CHECK(TouchscreenLike(Gesture{}).caps().magnetism_px() >
          ApplePencilLike(Gesture{}).caps().magnetism_px());
    CHECK(ApplePencilLike(Gesture{}).caps().magnetism_px() >
          MouseLike(Gesture{}).caps().magnetism_px());
}

TEST(input_events_are_ordered_by_device_timestamp) {
    const auto ev = run(ApplePencilLike(stroke()));
    for (std::size_t i = 1; i < ev.size(); ++i)
        CHECK(ev[i].timestamp >= ev[i - 1].timestamp);
}
