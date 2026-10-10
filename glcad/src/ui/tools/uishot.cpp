#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ui/shell.hpp"
#include "ui/frame_types.hpp"
#include "ui/snapshot.hpp"
#include "ui/symbols.hpp"

using namespace gcapp;

namespace {

}

int main(int argc, char** argv) {
    std::string round_edge;
    int width = 2400, height = 1500;
    double dpr = 2.0;
    std::string path = "ui.png";
    std::string theme_name;
    std::string view_name;
    double extrude = 0.0;
    double solid = 0.0;
    bool drawing = false;
    bool tablet = false;
    std::string mode_name;
    std::vector<std::pair<double, double>> clicks;
    std::string import_path;
    std::vector<std::string> commands;
    bool symbol_legend = false;
    int wheel = 0;
    double lift = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) path = argv[++i];
        else if (a == "-w" && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (a == "-h" && i + 1 < argc) height = std::atoi(argv[++i]);
        else if (a == "--dpr" && i + 1 < argc) dpr = std::atof(argv[++i]);
        else if (a == "--theme" && i + 1 < argc) theme_name = argv[++i];
        else if (a == "--view" && i + 1 < argc) view_name = argv[++i];
        else if (a == "--extrude" && i + 1 < argc) extrude = std::atof(argv[++i]);
        else if (a == "--solid" && i + 1 < argc) solid = std::atof(argv[++i]);
        else if (a == "--drawing") drawing = true;
        else if (a == "--tablet") tablet = true;
        else if (a == "--mode" && i + 1 < argc) mode_name = argv[++i];
        else if (a == "--lift" && i + 1 < argc) lift = std::atof(argv[++i]);
        else if (a == "--command" && i+1<argc) commands.push_back(argv[++i]);
        else if (a == "--symbol-legend") symbol_legend = true;
        else if (a == "--import" && i + 1 < argc) import_path = argv[++i];
        else if (a == "--wheel" && i + 1 < argc) wheel = std::atoi(argv[++i]);
        else if (a == "--round-edge" && i + 1 < argc) round_edge = argv[++i];
        else if (a == "--click" && i + 1 < argc) {
            double cx = 0, cy = 0;
            if (std::sscanf(argv[++i], "%lf,%lf", &cx, &cy) == 2) clicks.push_back({cx, cy});
        }
    }

    Shell shell;
    std::string error;
    if (!theme_name.empty()) shell.themes().activate(theme_name);
    auto replay_clicks = [&] {
        for (const auto& [cx, cy] : clicks) {
            shell.build_frame();
            shell.pointer_move(cx, cy);
            shell.build_frame();
            shell.pointer_down(cx, cy);
            shell.build_frame();
            shell.pointer_up(cx, cy);
            shell.build_frame();
            shell.pointer_move(cx, cy);
            shell.build_frame();
        }
    };
    // Set before the viewport: px() needs the scale.
    shell.set_dpr(dpr);
    if (!shell.load_font(&error))
        std::fprintf(stderr, "uishot: %s\n", error.c_str());
    shell.set_tablet_mode(tablet);
    if (tablet) shell.scale_ui(1.65);
    shell.set_viewport(width, height);
    if (std::getenv("GLCAD_CONFIG_DIR")) shell.load_app_config();
    if (std::getenv("GLCAD_PROJECT_ASK")) shell.enable_project_ask();
    static int g_fake_frame = 0;
    const bool fake_call = std::getenv("GLCAD_FAKE_CALL") != nullptr;
    if (fake_call) {
        shell.audio_record_begin = [] { return true; };
        shell.audio_record_end = [] { return std::vector<gcm::u8>{}; };
        shell.audio_record_level = [] {
            const float t = float(g_fake_frame);
            const float env = 0.5f + 0.5f * std::sin(t * 0.06f);
            const float osc = std::fabs(std::sin(t * 0.9f));
            return std::min(1.0f, env * osc * 0.9f + 0.05f);
        };
    }

    if (!import_path.empty()) {
        shell.import_file(import_path);
        shell.command("ZOOMEXTENTS");
    }
    if (import_path.empty() && !std::getenv("GLCAD_PROJECT_ASK")) {
    shell.canvas().set_display_unit(gunit::Length::Metre);
    shell.canvas().set_tool(Tool::Line);
    shell.canvas().type_point("0,0");
    shell.canvas().type_point("8,0");
    shell.canvas().type_point("8,5");
    shell.canvas().type_point("0,5");
    shell.canvas().type_point("0,0");
    shell.canvas().cancel();
    shell.canvas().set_tool(Tool::Circle);
    shell.canvas().type_point("4,2.5");
    shell.canvas().type_point("6,2.5");
    shell.canvas().cancel();
    shell.canvas().set_tool(Tool::Line);
    }

    if (!view_name.empty() && view_name != "plan") {
        using gview::StandardView;
        static const std::pair<const char*, StandardView> views[] = {
            {"iso", StandardView::Iso},     {"front", StandardView::Front},
            {"back", StandardView::Back},   {"left", StandardView::Left},
            {"right", StandardView::Right}, {"top", StandardView::Top},
            {"bottom", StandardView::Bottom}};
        for (const auto& [name, v] : views)
            if (view_name == name) shell.set_lens(Lens::ThreeD), shell.camera().look(v);
    }

    if(symbol_legend) {
        auto& canvas=shell.canvas();canvas.clear();canvas.cancel();canvas.set_tool(Tool::Select);
        std::size_t i=0;
        for(const auto& item:symbol_catalog()) {
            canvas.place_symbol(item.id,gcm::to_lattice(canvas.volume(),double(i%3)*2,double(i/3)*2),.8);
            ++i;
        }
        canvas.place_symbol("north",gcm::to_lattice(canvas.volume(),6,0),.8);
        canvas.update_symbol_legend(gcm::to_lattice(canvas.volume(),8,4));
        shell.build_frame();
        shell.command("ZOOMEXTENTS");
    }
    for(const auto& command:commands) shell.command(command);
    if (drawing) {
        shell.build_frame();
        shell.canvas().set_tool(Tool::Line);
        shell.canvas().type_point("-6,-2");
        double px, py;
        shell.camera().project(gview::Point3::from(-1.5, 1.0, 0.0), px, py);
        shell.pointer_move(px, py);
    }

    if (lift != 0.0) {
        shell.canvas().clear();
        shell.canvas().set_tool(Tool::Line);
        for (const char* p : {"-6,-3", "6,-3", "6,3", "-6,3", "-6,-3"}) shell.canvas().type_point(p);
        shell.canvas().cancel();
        shell.canvas().set_tool(Tool::Line);
        shell.canvas().type_point("1,-3");
        shell.canvas().type_point("1,3");
        shell.canvas().cancel();
        shell.canvas().set_tool(Tool::Select);
        shell.build_frame();
        shell.command("LIFT " + std::to_string(lift));
        shell.command("ZOOMEXTENTS");
        shell.build_frame();
    }

    if (solid != 0.0) {
        shell.build_frame();
        shell.modeller().arm(Modeller::Op::Extrude);
        double px, py;
        shell.camera().project(gview::Point3::from(4.0, 2.5, 0.0), px, py);
        shell.pointer_move(px, py);
        shell.pointer_down(px, py);
        shell.pointer_up(px, py);
        const std::string typed = std::to_string(solid);
        for (char ch : typed) shell.key_char(std::string(1, ch));
        shell.key_special(0);
        shell.key_special(1);
        shell.key_special(1);
        shell.build_frame();
        shell.command("ZOOMEXTENTS");
        shell.build_frame();
        shell.camera().project(gview::Point3::from(4.0, 2.5, solid), px, py);
        shell.pointer_move(px, py);
    }

    if (!round_edge.empty()) {
        double ex = 0, ey = 0, ez = 0;
        std::sscanf(round_edge.c_str(), "%lf,%lf,%lf", &ex, &ey, &ez);
        shell.set_mode(Mode::Solid);
        shell.build_frame();
        shell.activate_tool(ToolId::FilletEdge);
        shell.build_frame();
        double px, py;
        shell.camera().project(gview::Point3::from(ex, ey, ez), px, py);
        shell.pencil_tap(px, py);
        shell.modeller().edge_limit_wait(10);
        shell.build_frame();
        const double limit = shell.modeller().edge_limit();
        if (limit > 0) shell.modeller().set_typed(gunit::format_length(shell.modeller().floor_to_display(limit * .35), gunit::Format{shell.canvas().display_unit()}));
        shell.build_frame();
    }

    if (extrude != 0.0) {
        shell.build_frame();
        shell.modeller().arm(Modeller::Op::Extrude);
        double px, py;
        shell.camera().project(gview::Point3::from(4.0, 2.5, 0.0), px, py);
        shell.pointer_move(px, py);
        shell.pointer_down(px, py);
        shell.pointer_up(px, py);
        shell.pointer_down(px, py);
        double tx, ty;
        shell.camera().project(gview::Point3::from(4.0, 2.5, extrude), tx, ty);
        shell.pointer_move(tx, ty);
    }

    if (std::getenv("UISHOT_SELECT_PROBE")) {
        shell.build_frame();
        int probes = 0, hits = 0;
        for (std::size_t i = 0; i < shell.canvas().shapes().size() && probes < 8; ++i) {
            const auto& sh = shell.canvas().shapes()[i];
            if (sh.kind != gsnap::ShapeKind::Segment) continue;
            double ax, ay, bx, by;
            gcm::to_world(shell.canvas().volume(), sh.a, ax, ay);
            gcm::to_world(shell.canvas().volume(), sh.b, bx, by);
            const double wx = (ax + bx) * 0.5, wy = (ay + by) * 0.5;
            double px, py;
            if (!shell.camera().project(gview::Point3::from(wx, wy, 0.0), px, py)) continue;
            if (px < 20 || py < 160 || px > 1300 || py > 900) continue;
            ++probes;
            shell.pointer_move(px, py);
            shell.build_frame();
            shell.pointer_down(px, py);
            shell.build_frame();
            shell.pointer_up(px, py);
            shell.build_frame();
            const bool hit = !shell.canvas().selection().empty();
            if (hit) ++hits;
            std::fprintf(stderr, "probe %d shape %zu at screen (%.0f, %.0f): selection=%zu hover_valid=%d\n",
                         probes, i, px, py, shell.canvas().selection().size(), int(shell.hovered_target().valid));
            shell.command("SELECTNONE");
            shell.build_frame();
        }
        std::fprintf(stderr, "select probe: %d/%d hits\n", hits, probes);
    }
    for (int i = 0; i < std::abs(wheel); ++i) {
        shell.pointer_move(width * 0.45, height * 0.5);
        shell.scroll(0, wheel > 0 ? 1.0 : -1.0, false);
        shell.build_frame();
    }
    if (const char* pa = std::getenv("UISHOT_PROBE_AT")) {
        double qx = 0, qy = 0;
        if (std::sscanf(pa, "%lf,%lf", &qx, &qy) == 2) {
            shell.build_frame();
            const std::size_t si = shell.canvas().pick(qx, qy);
            const gcm::u32 ri = shell.canvas().pick_reference(qx, qy);
            std::fprintf(stderr, "probe-at (%g,%g): shape=%zd ref=%d", qx, qy, (ssize_t)(si == Canvas::npos ? -1 : (ssize_t)si), int(ri == Canvas::npos32 ? -1 : (int)ri));
            if (ri != Canvas::npos32) std::fprintf(stderr, " name='%s'", shell.canvas().blocks().references()[ri].name.c_str());
            if (si != Canvas::npos) std::fprintf(stderr, " layer='%s'", shell.canvas().shapes()[si].layer.c_str());
            std::fprintf(stderr, "\n");
        }
    }
    if (mode_name == "solid") shell.set_mode(Mode::Solid);
    else if (mode_name == "drawing") shell.set_mode(Mode::Drawing);
    else if (mode_name == "sketch") shell.set_mode(Mode::Sketch);
    replay_clicks();
    if (fake_call) {
        shell.command("ASSISTANT");
        shell.build_frame();
        const double cx = 120, cy = 382;
        shell.pointer_move(cx, cy); shell.build_frame();
        shell.pointer_down(cx, cy); shell.build_frame();
        shell.pointer_up(cx, cy);   shell.build_frame();
        for (g_fake_frame = 0; g_fake_frame < 400; ++g_fake_frame) shell.build_frame();
    }
    shell.build_frame();
    if (std::getenv("UISHOT_DUMP_UI")) {
        double x0 = 0, y0 = 0;
        std::sscanf(std::getenv("UISHOT_DUMP_UI"), "%lf,%lf", &x0, &y0);
        for (const auto& u : shell.ui())
            if (u.rect[0] + u.rect[2] > x0 && u.rect[1] + u.rect[3] > y0 && u.rect[0] < x0 + 400 && u.rect[1] < y0 + 400)
                std::fprintf(stderr, "ui kind=%g rect=(%g,%g %gx%g) rgba=(%.2f,%.2f,%.2f,%.2f) clip=(%g,%g %gx%g)\n",
                             u.params[2], u.rect[0], u.rect[1], u.rect[2], u.rect[3], u.color[0], u.color[1], u.color[2], u.color[3],
                             u.clip[0], u.clip[1], u.clip[2], u.clip[3]);
    }

    const std::vector<gcm::u8> png = snapshot_png(shell, width, height);
    std::FILE* f = png.empty() ? nullptr : std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(png.data(), 1, png.size(), f);
        std::fclose(f);
    }
    if (!f) {
        std::fprintf(stderr, "uishot: could not write %s\n", path.c_str());
        return 1;
    }
    std::printf("%s  %dx%d  ui=%zu text=%zu lines=%zu\n", path.c_str(), width,
                height, shell.ui().size(), shell.text().size(), shell.static_lines().size());
    return 0;
}
