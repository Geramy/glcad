#include "ui/scenario.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "geometry/boolean.hpp"
#include "ui/shell.hpp"
#include "ui/tools.hpp"

namespace gcapp {

namespace {

gcm::WorkingVolume scenario_vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

void say(const char* msg) {
    std::printf("scenario: %s\n", msg);
    std::fflush(stdout);
}

// Wait out the 0.4 s double-click guard in Shell::pointer_down, even unpaced.
void click_guard() {
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
}

void type_point(Shell& s, const char* p) { s.canvas().type_point(p); }

}

bool run_box_circle_extrude(Shell& s, const ScenarioConfig& cfg) {
    const gcm::WorkingVolume vol = scenario_vol();

    s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    for (const char* p : {"0,0", "4,0", "4,4", "0,4", "0,0"}) type_point(s, p);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.build_frame();
    s.command("ZOOMEXTENTS");
    s.build_frame();

    s.activate_tool(ToolId::Extrude);
    double wx, wy, wz, px, py;
    s.sketch_plane().to_world(2.0, 2.0, wx, wy, wz);
    if (!s.camera().project(gview::Point3::from(wx, wy, wz), px, py)) { say("box: cannot project"); return false; }
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (char c : std::string("3")) s.key_char(std::string(1, c));
    s.key_special(0);
    s.build_frame();
    if (s.modeller().bodies().size() != 1) { say("box: no body after extrude"); return false; }
    say("box extruded 4x4x3");

    const auto face_point = cfg.side_face ? gview::Point3::from(4,2,1.5) : gview::Point3::from(2,2,3);
    if (cfg.side_face) s.camera().look(gview::StandardView::Right);
    s.command("SKETCH");
    s.build_frame();
    if (!s.camera().project(face_point, px, py)) { say("face: cannot project"); return false; }
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    s.build_frame();
    if (s.sketches().size() != 2 || !s.sketches()[1].on_face) { say("face: no sketch on the face"); return false; }

    s.canvas().set_tool(Tool::Circle);
    type_point(s, "0,0");
    type_point(s, "1,0");
    s.canvas().set_tool(Tool::Select);
    s.build_frame();
    say(cfg.side_face ? "circle sketched on the side face" : "circle sketched on the top face");

    s.set_lens(Lens::ThreeD);
    s.camera().look(gview::StandardView::Iso);
    s.build_frame();

    s.activate_tool(ToolId::Extrude);
    if (!s.camera().project(face_point, px, py)) { say("pull: cannot project"); return false; }
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    click_guard();
    s.pointer_down(px, py);
    s.build_frame();
    if (s.modeller().stage() != Modeller::Stage::Placed) { say("pull: arrow not placed"); return false; }
    click_guard();
    s.pointer_down(px, py);
    s.build_frame();
    if (s.modeller().stage() != Modeller::Stage::Dragging) { say("pull: not dragging"); return false; }

    const int steps = std::max(1, static_cast<int>(cfg.fps * cfg.seconds));
    const double frame_seconds = 1.0 / std::max(1, cfg.fps);
    const gview::Point3 c = s.modeller().target().centroid;
    const gview::Vec3 n = s.modeller().target().normal;
    double qx = px, qy = py;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 1; i <= steps; ++i) {
        const auto frame_start = std::chrono::steady_clock::now();
        const double d = cfg.distance * double(i) / double(steps);
        gview::Point3 p = c + n * gcm::F64::from(d);
        if (!s.camera().project(p, qx, qy)) { say("pull: cannot project step"); return false; }
        s.pointer_move(qx, qy);
        s.build_frame();
        if (cfg.render) cfg.render();
        if (cfg.paced) {
            const auto target = frame_start + std::chrono::duration<double>(frame_seconds);
            const auto now = std::chrono::steady_clock::now();
            if (target > now) std::this_thread::sleep_for(target - now);
        }
        if (i % std::max(1, steps / 10) == 0) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "preview %d/%d, d=%.4g m", i, steps, d);
            say(buf);
        }
    }
    const double anim_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    s.pointer_up(qx, qy);
    s.build_frame();
    if (cfg.render) cfg.render();

    if (s.modeller().bodies().size() != 1) { say("commit: expected one fused body"); return false; }
    const double v = gtopo::volume(s.modeller().bodies()[0], vol);
    const double want = 48.0 + std::acos(-1.0) * 1.0 * 1.0 * cfg.distance;
    if (std::fabs(v - want) > 1e-3) {
        std::printf("scenario: committed volume %g, expected %g\n", v, want);
        std::fflush(stdout);
        return false;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "committed: %d preview frames in %.2f s, volume %g = 48 + %.4g",
                  steps, anim_s, v, std::acos(-1.0) * cfg.distance);
    say(buf);
    return true;
}

bool run_walls_zoom(Shell& s, const ScenarioConfig& cfg) {
    s.canvas().set_display_unit(gunit::Length::Metre);
    for (int row = 0; row < 8; ++row) for (int column = 0; column < 8; ++column) {
        const int x = column * 5, y = row * 5;
        s.canvas().set_tool(Tool::Line);
        for (auto point : {std::pair{x,y}, {x+4,y}, {x+4,y+4}, {x,y+4}, {x,y}})
            s.canvas().type_point(std::to_string(point.first) + "," + std::to_string(point.second));
        s.canvas().cancel();
    }
    s.canvas().set_tool(Tool::Select);
    s.command("WALLS 200mm 2.7m");
    s.set_lens(Lens::ThreeD);
    s.camera().look(gview::StandardView::Iso);
    s.command("ZOOMEXTENTS");
    const auto render = [&] { if (cfg.render) cfg.render(); else s.build_frame(); };
    for (int i = 0; i < 4; ++i) render();
    if (static_cast<const Shell&>(s).modeller().bodies().size() != 64) { say("walls: expected 64 joined room solids"); return false; }
    const auto revision = s.mesh_version();
    double x, y;
    if (!s.camera().project(gview::Point3::from(20,20,0), x, y)) return false;
    const int steps = std::max(10, int(cfg.seconds * cfg.fps));
    std::vector<double> frame_ms;
    frame_ms.reserve(steps);
    for (int i = 0; i < steps; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const auto distance = s.camera().distance();
        s.pinch(x, y, i % 2 ? 1.0 / 1.05 - 1.0 : .05);
        if (s.camera().distance() == distance) { say("walls: zoom did not move the camera"); return false; }
        render();
        frame_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        if (s.mesh_version() != revision) { say("walls: zoom rebuilt unchanged geometry"); return false; }
    }
    std::sort(frame_ms.begin(), frame_ms.end());
    std::printf("scenario: walls zoom: 256 wall segments / 64 joined solids, %zu mesh vertices, %d frames, median %.3f ms, p95 %.3f ms; mesh unchanged\n",
                s.mesh().size(), steps, frame_ms[frame_ms.size()/2], frame_ms[frame_ms.size()*95/100]);
    std::fflush(stdout);
    return true;
}

bool run_draw_inside_regions(Shell& s, const ScenarioConfig& cfg) {
    s.set_mode(Mode::Sketch);
    auto& canvas = s.canvas();
    canvas.set_display_unit(gunit::Length::Metre);
    if (canvas.shapes().empty()) {
        std::vector<gsnap::Shape> shapes;
        const auto add = [&](double ax, double ay, double bx, double by) {
            gsnap::Shape shape;
            shape.a = gcm::to_lattice(canvas.volume(), ax, ay);
            shape.b = gcm::to_lattice(canvas.volume(), bx, by);
            shapes.push_back(shape);
        };
        for (int room = 0; room < 8; ++room) {
            const double x = (room % 4) * 6., y = (room / 4) * 6.;
            for (int i = 0; i < 16; ++i) {
                const double a = i * .25, b = (i + 1) * .25;
                add(x+a,y,x+b,y); add(x+4,y+a,x+4,y+b);
                add(x+4-a,y+4,x+4-b,y+4); add(x,y+4-a,x,y+4-b);
            }
        }
        canvas.load_shapes(std::move(shapes));
        for (int room = 0; room < 8; ++room)
            if (!canvas.background_at_world(gcm::to_lattice(canvas.volume(), (room%4)*6.+2, (room/4)*6.+2),
                                             gdoc::Color::rgb(90,140,190), .01, 35)) {
                say("draw regions: background fixture did not close"); return false;
            }
    }
    canvas.clear_selection();
    s.set_modifiers(false,false,false,false);
    s.camera().look(gview::StandardView::Top);
    s.camera().set_target(gview::Point3::from(2,2,0));
    s.fit_width(12);
    const auto render = [&] { if (cfg.render) cfg.render(); else s.build_frame(); };
    for (int i = 0; i < 6; ++i) render();
    const auto before = canvas.shapes().size();
    std::vector<double> moves, clicks, renders;
    const auto timed = [](auto&& work, std::vector<double>& samples) {
        const auto start = std::chrono::steady_clock::now();
        work();
        samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    };
    for (int line = 0; line < 6; ++line) {
        s.activate_tool(ToolId::Line);
        for (double x : {1.5,3.}) {
            double px = 0, py = 0;
            if (!s.camera().project(gview::Point3::from(x,1.5+line*.2,0),px,py)) {
                say("draw regions: cannot project an interior point"); return false;
            }
            timed([&] { s.pointer_move(px,py); }, moves);
            timed(render, renders);
            timed([&] { s.pointer_down(px,py); s.pointer_up(px,py); }, clicks);
            timed(render, renders);
            if (cfg.paced && cfg.fps > 0)
                std::this_thread::sleep_for(std::chrono::duration<double>(1. / cfg.fps));
        }
        canvas.cancel();
    }
    const auto report = [](const char* stage, std::vector<double> samples) {
        std::sort(samples.begin(),samples.end());
        std::printf("scenario: draw-inside-regions %s: samples=%zu median=%.3fms p95=%.3fms max=%.3fms\n",
            stage,samples.size(),samples[samples.size()/2],samples[(samples.size()-1)*95/100],samples.back());
    };
    report("pointer move",moves); report("pointer down/up",clicks); report("render",renders);
    std::printf("scenario: draw-inside-regions shapes %zu -> %zu, hatches %zu, render=%s\n",
        before,canvas.shapes().size(),canvas.hatches().size(),cfg.render ? "native" : "headless");
    std::fflush(stdout);
    canvas.set_tool(Tool::Select);
    if (canvas.shapes().size() != before + 6) {
        say("draw regions: expected six new line segments"); return false;
    }
    return true;
}

}
