#include "test.hpp"

#include <chrono>
#include <cmath>
#include <thread>
#include "geometry/boolean.hpp"
#include "ui/boolean_preview.hpp"
#include "ui/scenario.hpp"
#include "ui/frame_demand.hpp"
#include "ui/shell.hpp"

using namespace gcapp;

namespace {
Shell scenario_shell() {
    Shell s;
    s.set_dpr(1.0);
    s.set_viewport(1600, 1000);
    return s;
}
}

TEST(scenario_box_circle_extrude_runs_the_animated_preview) {
    Shell s = scenario_shell();

    gcm::WorkingVolume vol;
    vol.extent = 500.0;
    vol.resolution_bits = 32;

    ScenarioConfig cfg;
    cfg.paced = false;
    cfg.seconds = 1.0;
    cfg.fps = 30;
    cfg.distance = 2.0;
    CHECK(run_box_circle_extrude(s, cfg));

    CHECK(s.modeller().bodies().size() == 1);
    if (s.modeller().bodies().size() == 1) {
        const double v = gtopo::volume(s.modeller().bodies()[0], vol);
        const double want = 48.0 + std::acos(-1.0) * 1.0 * 1.0 * cfg.distance;
        CHECK_MSG(std::fabs(v - want) < 1e-3, "volume %g, expected %g", v, want);
    }
}

TEST(scenario_shallow_side_face_cut_commits_a_closed_solid) {
    Shell s = scenario_shell();
    ScenarioConfig cfg;
    cfg.paced=false; cfg.side_face=true;
    cfg.seconds=.1; cfg.fps=30; cfg.distance=-.1218;
    CHECK(run_box_circle_extrude(s,cfg));
    CHECK(s.modeller().bodies().size()==1);
    if (s.modeller().bodies().size()!=1) return;
    const auto& body=s.modeller().bodies()[0];
    CHECK(body.analyse().closed && body.analyse().manifold);
    gcm::WorkingVolume vol; vol.extent=500; vol.resolution_bits=32;
    CHECK(std::fabs(gtopo::volume(body,vol)-(48+std::acos(-1.0)*cfg.distance))<1e-3);
}

TEST(scenario_preview_is_the_live_union_while_dragging) {
    Shell s = scenario_shell();

    s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    for (const char* p : {"0,0", "4,0", "4,4", "0,4", "0,0"}) s.canvas().type_point(p);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.build_frame();

    s.activate_tool(ToolId::Extrude);
    double wx, wy, wz, px, py;
    s.sketch_plane().to_world(2.0, 2.0, wx, wy, wz);
    CHECK(s.camera().project(gview::Point3::from(wx, wy, wz), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (char c : std::string("3")) s.key_char(std::string(1, c));
    s.key_special(0);
    s.build_frame();
    CHECK(s.modeller().bodies().size() == 1);
    const std::size_t mesh_box = s.mesh().size();

    s.command("SKETCH");
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    s.build_frame();
    CHECK(s.sketches().size() == 2);
    if (s.sketches().size() == 2) CHECK(s.sketches()[1].on_face);

    s.canvas().set_tool(Tool::Circle);
    s.canvas().type_point("0,0");
    s.canvas().type_point("1,0");
    s.canvas().set_tool(Tool::Select);
    s.build_frame();

    s.set_lens(Lens::ThreeD);
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    s.pointer_down(px, py);
    s.build_frame();
    CHECK(s.modeller().stage() == Modeller::Stage::Placed);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    s.pointer_down(px, py);
    s.build_frame();
    CHECK(s.modeller().stage() == Modeller::Stage::Dragging);

    const gview::Point3 c = s.modeller().target().centroid;
    const gview::Vec3 n = s.modeller().target().normal;
    for (double d : {0.5, 2.0}) {
        gview::Point3 p = c + n * gcm::F64::from(d);
        double qx, qy;
        CHECK(s.camera().project(p, qx, qy));
        s.pointer_move(qx, qy);
        s.build_frame();
        CHECK_MSG(std::fabs(s.modeller().distance() - d) < 1e-6,
                  "drag says %g, wanted %g", s.modeller().distance(), d);
        CHECK_MSG(s.mesh().size() > mesh_box,
                  "preview mesh %zu not bigger than the box mesh %zu",
                  s.mesh().size(), mesh_box);
    }
    s.pointer_up(px, py);
    s.build_frame();
}

TEST(scenario_gpu_preview_state_is_valid_during_cut_drag) {
    Shell s = scenario_shell();

    gcm::WorkingVolume vol;
    vol.extent = 500.0;
    vol.resolution_bits = 32;

    s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    for (const char* p : {"0,0", "4,0", "4,4", "0,4", "0,0"}) s.canvas().type_point(p);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
    s.build_frame();

    s.activate_tool(ToolId::Extrude);
    double wx, wy, wz, px, py;
    s.sketch_plane().to_world(2.0, 2.0, wx, wy, wz);
    CHECK(s.camera().project(gview::Point3::from(wx, wy, wz), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (char c : std::string("3")) s.key_char(std::string(1, c));
    s.key_special(0);
    s.build_frame();
    CHECK(s.modeller().bodies().size() == 1);

    s.command("SKETCH");
    s.build_frame();
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    s.build_frame();
    CHECK(s.sketches().size() == 2);
    if (s.sketches().size() == 2) CHECK(s.sketches()[1].on_face);

    s.canvas().set_tool(Tool::Circle);
    s.canvas().type_point("0,0");
    s.canvas().type_point("1,0");
    s.canvas().set_tool(Tool::Select);
    s.build_frame();

    s.set_lens(Lens::ThreeD);
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    CHECK(s.camera().project(gview::Point3::from(2.0, 2.0, 3.0), px, py));
    s.build_frame();
    s.pointer_move(px, py);
    s.build_frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    s.pointer_down(px, py);
    s.build_frame();
    CHECK(s.modeller().stage() == Modeller::Stage::Placed);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    s.pointer_down(px, py);
    s.build_frame();
    CHECK(s.modeller().stage() == Modeller::Stage::Dragging);

    const gview::Point3 c = s.modeller().target().centroid;
    const gview::Vec3 n = s.modeller().target().normal;
    gview::Point3 p = c - n * gcm::F64::from(1.0);
    double qx, qy;
    CHECK(s.camera().project(p, qx, qy));
    s.pointer_move(qx, qy);
    s.build_frame();
    if (s.modeller().distance() >= 0) {
        gview::Point3 p2 = c + n * gcm::F64::from(1.0);
        CHECK(s.camera().project(p2, qx, qy));
        s.pointer_move(qx, qy);
        s.build_frame();
    }
    CHECK(s.modeller().stage() == Modeller::Stage::Dragging);

    const auto& gpu = s.modeller().gpu_preview();
    CHECK_MSG(!gpu.body.empty(), "body triangles empty");
    CHECK_MSG(!gpu.tool.empty(), "tool triangles empty");
    CHECK(gpu.body.size() % 3 == 0);
    CHECK(gpu.tool.size() % 3 == 0);
    CHECK_MSG(gpu.target_body_count > 0, "target body range not identified");
    CHECK_MSG(gpu.operation == gbool::Operation::Subtract, "expected a cut");
    CHECK_MSG(gpu.body_first > 0, "body_first is 0");

    s.pointer_up(px, py);
    s.build_frame();
}

TEST(bench_boolean_interaction) {
    if(!std::getenv("GLCAD_BENCH_BOOLEAN"))return;
    auto s=scenario_shell();ScenarioConfig cfg;
    cfg.paced=false;cfg.seconds=1;cfg.fps=60;cfg.distance=1;
    std::size_t frames=0;double preview_ms=0;
    cfg.render=[&] {
        const auto start=std::chrono::steady_clock::now();
        const auto p=static_cast<const Shell&>(s).modeller().gpu_preview();
        preview_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++frames;
    };
    const auto start=std::chrono::steady_clock::now();
    CHECK(run_box_circle_extrude(s,cfg));
    const auto total=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("BOOLEAN BENCH scenario %.3f ms, operand preparation %.3f ms/frame, faces %u\n",total,preview_ms/frames,s.modeller().bodies()[0].face_count());
    s.command("CANCEL");s.set_mode(Mode::Solid);s.build_frame();
    const int count=std::getenv("GLCAD_BENCH_HOVER_COUNT")?std::atoi(std::getenv("GLCAD_BENCH_HOVER_COUNT")):100;
    double move=0,build=0;
    for(int i=0;i<count;++i) {
        const auto a=std::chrono::steady_clock::now();
        s.pointer_move(750+(i%100),450+(i%20));
        const auto b=std::chrono::steady_clock::now();s.build_frame();const auto c=std::chrono::steady_clock::now();
        move+=std::chrono::duration<double,std::milli>(b-a).count();build+=std::chrono::duration<double,std::milli>(c-b).count();
    }
    std::printf("BOOLEAN BENCH hover %d: move %.3f ms, frame %.3f ms\n",count,move/count,build/count);
    const int commits=std::getenv("GLCAD_BENCH_COMMIT_COUNT")?std::atoi(std::getenv("GLCAD_BENCH_COMMIT_COUNT")):1;
    for(int i=0;i<commits;++i)for(double distance:{-.5,.5,-.75,.75}) {
        const auto a=std::chrono::steady_clock::now();
        const bool ok=s.set_feature_value(0,int(s.modeller().history(0).size()-1),distance);
        const auto b=std::chrono::steady_clock::now();
        std::printf("BOOLEAN BENCH CPU commit %.2f: %.3f ms, ok=%d\n",distance,std::chrono::duration<double,std::milli>(b-a).count(),ok);
        CHECK(ok);
    }
}

TEST(frame_demand_settles_bursts_and_sleeps_until_input_or_work) {
    FrameDemand demand;
    CHECK(demand.consume()); CHECK(demand.consume()); CHECK(!demand.consume());
    for (int i=0;i<1000;++i) demand.request();
    CHECK(demand.consume()); CHECK(demand.consume());
    for (int i=0;i<1000;++i) CHECK(!demand.consume());
    for (int i=0;i<10;++i) CHECK(demand.consume(true));
    CHECK(!demand.pending());
    demand.request(); CHECK(demand.consume());
    demand.request(); CHECK(demand.consume()); CHECK(demand.consume());
    CHECK(!demand.pending());
}

TEST(scenario_walls_zoom_keeps_mesh_resident) {
    Shell s = scenario_shell();
    ScenarioConfig cfg;
    cfg.paced = false;
    cfg.seconds = .1;
    cfg.fps = 30;
    CHECK(run_walls_zoom(s, cfg));
}

TEST(scenario_draw_inside_closed_regions_replays_real_pointer_edits) {
    auto shell = scenario_shell();
    ScenarioConfig cfg; cfg.paced = false;
    CHECK(run_draw_inside_regions(shell,cfg));
    CHECK(shell.canvas().shapes().size() == 8u * 64u + 6u);
    CHECK(shell.canvas().hatches().size() == 8);
    for (const auto& hatch : shell.canvas().hatches()) CHECK(hatch.valid && !hatch.orphaned);
    const auto before = shell.canvas().shapes().size();
    CHECK(run_draw_inside_regions(shell,cfg));
    CHECK(shell.canvas().shapes().size() == before + 6);
}
