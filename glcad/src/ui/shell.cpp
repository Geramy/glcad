#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <cctype>
#include <cstdlib>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include "exchange/dxf.hpp"
#include "exchange/dwg.hpp"
#include "exchange/dwg_write.hpp"
#include "exchange/plot_style.hpp"
#include "exchange/dwf.hpp"
#include "exchange/step.hpp"
#include "exchange/xt.hpp"
#include "engine/document_file.hpp"
#include <filesystem>

#include "geometry/mate.hpp"
#include "geometry/recognise.hpp"
#include "geometry/measure.hpp"

#include "ui/build_stamp.hpp"
#include "ui/doc_entities.hpp"
#include "ui/solids_file.hpp"
#include "exchange/pdf.hpp"
#include <chrono>
#include <random>

#include "ui/shell.hpp"
#include "ui/ai_tools.hpp"
#include "ai/encode.hpp"
#include "ui/snapshot.hpp"
#include "ui/resource_path.hpp"
#include "math/parallel.hpp"

#include "engine/tcp.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui/frame_types.hpp"

#ifdef _WIN32
#include <direct.h>
static int glcad_make_dir(const char* p) { return _mkdir(p); }
#else
#include <sys/stat.h>
static int glcad_make_dir(const char* p) { return ::mkdir(p, 0755); }
#endif

namespace gcapp {

namespace T = gtheme::token;

std::string uppercase(std::string t) {
    for (char& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
    return t;
}

std::string file_title(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string scale_display(int den) {
    for (const auto& sc : gplot::standard_scales())
        if (sc.paper == 1.0 && int(sc.drawing + 0.5) == den &&
            std::abs(sc.drawing - double(den)) < 1e-9)
            return sc.name;
    return "1:" + std::to_string(den);
}

std::string recovery_identity() {
    std::random_device random;
    return std::to_string(random()) + "-" + std::to_string(random());
}

namespace {
bool interactive_role(gui::Context::Role role) {
    using Role=gui::Context::Role;
    switch(role) {
        case Role::Button: case Role::Toggle: case Role::Checkbox: case Role::Field:
        case Role::Tab: case Role::Row: case Role::TreeItem: case Role::Slider: return true;
        default: return false;
    }
}


gcm::WorkingVolume working_volume() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
}

Shell::Shell() : vol_(working_volume()), modeller_(vol_) {
#ifdef GLCAD_WITH_PGLITE
    const char* runtime_override = std::getenv("GLCAD_PGLITE_RUNTIME");
    if (!runtime_override || !*runtime_override) {
        const std::string runtime = resource_fallback("pglite");
        if (!runtime.empty()) {
#ifdef _WIN32
            _putenv_s("GLCAD_PGLITE_RUNTIME", runtime.c_str());
#else
            setenv("GLCAD_PGLITE_RUNTIME", runtime.c_str(), 1);
#endif
        }
    }
#endif

    new_tab();
    recovery_id_ = recovery_identity();

    gio::register_tcp_transport();

    themes_.set_system_pair("GLCAD Light", "GLCAD Dark");
    themes_.set_follow_system(true);
    themes_.sync_with_system();

    listen_to_document();
    commands_.add_standard();
    register_all_commands(command_table_);
    for(const char* name:{"BLOCKSPALETTE","CONTENT"})if(!commands_.find(name)) {
        gcmd::Command command;command.name=name;command.description="Browse block collections";
        command.group="Organise";command.modes=gcmd::Modes::Both;commands_.add(std::move(command));
    }
    for (const auto& d : tool_defs()) {
        if (commands_.find(d.command)) continue;
        gcmd::Command c;
        c.name = d.command;
        c.description = d.label;
        c.group = tool_mode(d.id) == Mode::Sketch ? "Draw" : "3D";
        c.modes = gcmd::Modes::Both;
        commands_.add(std::move(c));
    }
    for (const auto& m : app_menus())
        for (const auto& it : m.items) {
            if (it.separator() || commands_.find(it.command)) continue;
            gcmd::Command c;
            c.name = it.command;
            c.description = it.label;
            c.group = m.title;
            c.modes = gcmd::Modes::Both;
            commands_.add(std::move(c));
        }
    for (const char* gone : {"TABLE", "TOLERANCE", "FIELD", "WIPEOUT",
                             "DELCONSTRAINT", "CONSTRAINTBAR"})
        commands_.remove(gone);
    for(const char* name:{"LEADEREDIT","LEADERSTYLE","MLEADERSTYLE","PDFIMPORT"}) {gcmd::Command c;c.name=name;c.description="Edit leader annotations";c.modes=gcmd::Modes::Both;commands_.add(std::move(c));}
    vars_.add_standard();
    // Seed from app defaults; zeros would overwrite every setting on frame one.
    publish_sysvars();
    cli_ = std::make_unique<gcmd::CommandLine>(commands_, vars_);

    gdoc::Layer w;  w.name = "A-WALL";  w.color = gdoc::Color::rgb(230, 120, 90);
    gdoc::Layer d;  d.name = "A-DOOR";  d.color = gdoc::Color::rgb(120, 200, 140);
    gdoc::Layer g;  g.name = "A-GLAZ";  g.color = gdoc::Color::rgb(110, 180, 240);
    gdoc::Layer dim; dim.name = "A-ANNO-DIMS"; dim.color = gdoc::Color::rgb(230, 200, 100);
    layers_.add(w); layers_.add(d); layers_.add(g); layers_.add(dim);

    SketchDoc ground;
    ground.name = "Sketch 1";
    ground.plane = gtopo::SketchPlane::world_xy();
    sketches_.push_back(ground);
    active_sketch_ = 0;

    camera_.look_plan();
    camera_.set_target(ggeo::d3::Point<gcm::F64>::from(0, 0, 0));
    camera_.set_viewport(vw_, vh_);
    fit_width(default_width_m_);

    gui::PanelState layers;
    layers.id = "layers";
    layers.title = "Layers";
    layers.side = gui::Side::Right;
    // Device pixels, not logical.
    layers.extent = float(px(280));
    layers.order = 0;
    layers.preferred = float(px(380));
    dock_.add(layers);


    gui::PanelState sk;
    sk.id = "sketches";
    sk.title = "Sketches";
    sk.side = gui::Side::Right;
    sk.extent = float(px(280));
    sk.order = 2;
    sk.preferred = float(px(120));
    dock_.add(sk);

    gui::PanelState dr;
    dr.id = "drawing";
    dr.title = "Drawing";
    dr.side = gui::Side::Right;
    dr.extent = float(px(280));
    dr.order = 1;
    dr.preferred = float(px(230));
    dr.open = false;
    dock_.add(dr);

    gui::PanelState pr;
    pr.id = "properties";
    pr.title = "Properties";
    pr.side = gui::Side::Right;
    pr.extent = float(px(280));
    pr.order = 1;
    pr.preferred = float(px(330));
    pr.open = false;
    dock_.add(pr);

    gui::PanelState ob;
    ob.id = "objects";
    ob.title = "Objects";
    ob.side = gui::Side::Right;
    ob.extent = float(px(280));
    ob.order = 3;
    ob.preferred = float(px(120));
    dock_.add(ob);

    gui::PanelState as;
    as.id = "assistant";
    as.title = "Assistant";
    as.side = gui::Side::Left;
    as.extent = float(px(320));
    as.order = 5;
    as.preferred = 0.0f;
    as.open = false;
    dock_.add(as);


    canvas_.set_tool(Tool::Select);
    log_.push_back(std::string("GLCAD ") + build_stamp());
    {
        std::error_code ec;
        if (std::filesystem::exists(recoverable_path(), ec))
            log_.push_back("an unsaved drawing from the last session is waiting -- "
                           "type RECOVER to open it");
    }
    log_.push_back("GLCAD ready. Type a command, or press L for LINE.");
    clear_journal();
    saved_serial_ = document_serial();
}

Shell::~Shell() {
    if(mcp_server_) mcp_server_->stop();
    ai_->abort = true;
    {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        ai_->awaiting_tools = false;
    }
    ai_->cv.notify_all();
    if (ai_->worker.joinable()) ai_->worker.join();
    if (ai_->tts_worker.joinable()) ai_->tts_worker.join();
    if (ai_->status_worker.joinable()) ai_->status_worker.join();
    ai_->discovery.stop();
}



std::vector<Shell::PlanFinding> Shell::plan_check() const {
    gplan::Plan p;
    p.shapes = &canvas_.shapes();
    p.architectural = drawing_discipline_ != DrawingDiscipline::Mechanical;
    p.metres_per_unit = canvas_.volume().inv_scale();
    p.dimensions = canvas_.dimensions().size();
    p.labels = canvas_.texts().size();
    p.hatches = canvas_.hatches().size();
    for (const auto& h : canvas_.hatches()) p.orphan_hatches += h.orphaned ? 1 : 0;
    const auto rules = !p.architectural ? gplan::Rules{} : gplan::load_rules(
        (std::filesystem::path(gfile::app_config_path()).parent_path() / "library" /
         "plan-rules.txt")
            .string());
    return gplan::check(p, rules);
}

void Shell::rebuild_scene() {
    sync_document(false);
    scene_.clear();
    if (display_mode_ == 0 || wireframe_) return;

    const auto& ranges = modeller_.mesh_ranges();
    const auto add = [&](const Modeller::MeshRange& r, gdoc::EntityId id) {
        grender::DrawItem item;
        item.id = id;
        item.kind = grender::DrawKind::Mesh;
        item.first = r.first;
        item.count = r.count;
        item.material.translucent = r.translucent;
        for (std::size_t i = r.first; i < r.first + r.count && i < mesh_.size(); ++i)
            item.bounds.add(mesh_[i].pos[0], mesh_[i].pos[1], mesh_[i].pos[2]);
        scene_.put(item);
    };

    for (const auto& r : ranges) {
        if (!r.key) {
            add(r, preview_scene_id(r.body));
            continue;
        }
        const gdoc::EntityId id = document_body_id(r.key);
        if (doc_.find(id)) add(r, id);
    }
}

void Shell::listen_to_document() {
    doc_.on_change([this](const gdoc::Transaction& t) {
        ++document_revision_;
        last_change_label_ = t.label;
        touch_metadata("transaction");
    });
}

bool Shell::load_font(std::string* error) {
    fonts_.scan_system();
    canvas_.set_font_registry(&fonts_);

    std::string icons = GLCAD_ICON_FONT;
    struct stat icon_st{};
    if (::stat(icons.c_str(), &icon_st) != 0) {
        const std::string alt = resource_fallback(
            "fonts/" + std::filesystem::path(icons).filename().string());
        if (!alt.empty()) icons = alt;
    }
    const auto& want = gui::icon_codepoints();
    const auto try_face = [&](const std::string& path) {
        if (!font_.load_with_icons_files(path, icons, want, 48, 6, error))
            return false;
        if (!font_.covers_ascii()) return false;
        const gtext::Glyph* probe = font_.glyph('M');
        if (!probe || probe->advance < 0.05f || probe->advance > 3.0f) return false;
        icons_loaded_ = font_.glyph(gui::icon_codepoint(gui::Icon::Line)) != nullptr;
        ui_ctx_.set_measure([this](std::string_view text, gcm::f32 size) {
            gtext::TextStyle ts;
            ts.size = size;
            return gtext::layout_text(font_, std::string(text), ts).width;
        });
        canvas_.set_text_font(&font_);
        return true;
    };

    for (const char* preferred : {"Helvetica", "HelveticaNeue", "Arial", "SFNS",
                                  "Geneva", "Verdana",
                                  "DejaVu Sans", "DejaVuSans", "Liberation Sans",
                                  "LiberationSans", "Noto Sans", "NotoSans",
                                  "Ubuntu", "FreeSans", "Cantarell",
                                  "Segoe UI", "Tahoma"})
        if (const auto* e = fonts_.find(preferred))
            if (try_face(e->path)) return true;

    for (const auto& e : fonts_.all()) {
        if (e.kind != gtext::FontKind::TrueType && e.kind != gtext::FontKind::OpenType)
            continue;
        if (try_face(e.path)) {
            log_.push_back("text face (fallback): " + e.path);
            return true;
        }
    }
    const auto bundled=(std::filesystem::path(icons).parent_path()/"DejaVuSans.ttf").string();
    if(try_face(bundled)) return true;
    const auto installed=resource_fallback("fonts/DejaVuSans.ttf");
    if(!installed.empty() && installed!=bundled && try_face(installed)) return true;
    if (error) *error = "no usable text font found";
    return false;
}

void Shell::set_dpr(double scale) {
    modeller_.set_edge_pick_px(9.0 * scale);
    if (scale <= 0.0 || scale == dpr_) return;
    const double ratio = scale / dpr_;
    dpr_ = scale;
    for (const char* id : {"layers", "sketches", "objects", "settings", "properties"})
        if (gui::PanelState* p = dock_.find(id)) {
            p->extent = float(p->extent * ratio);
            p->min_extent = float(p->min_extent * ratio);
            p->max_extent = float(p->max_extent * ratio);
            p->preferred = float(p->preferred * ratio);
        }
    scale_ui(ratio);
}

void Shell::scale_ui(double factor) {
    if (factor <= 0.0 || factor == 1.0) return;
    gui::Style& st = ui_ctx_.style();
    const float f = float(factor);
    st.unit *= f;
    st.row_height *= f;
    st.row_height_dense *= f;
    st.control_height *= f;
    st.icon_button *= f;
    st.icon_size *= f;
    st.rail_width *= f;
    st.app_bar_height *= f;
    st.font_size *= f;
    st.font_size_small *= f;
    st.font_size_title *= f;
    st.spacing *= f;
    st.panel_gap *= f;
    st.shadow_spread *= f;
    st.min_touch_target *= f;
}

std::array<double, 8> Shell::camera_pose() const noexcept {
    const auto t = camera_.target();
    return {t.x.to_f64(), t.y.to_f64(), t.z.to_f64(), camera_.distance(), camera_.azimuth(),
            camera_.elevation(), double(camera_.projection()), camera_.fov()};
}

void Shell::set_viewport(double w, double h) {
    if (refit_bounds_ && (w != refit_w_ || h != refit_h_)) {
        if (camera_pose() == refit_pose_) pending_extent_fit_ = refit_bounds_;
        else refit_bounds_.reset();
    }
    vw_ = w; vh_ = h;
    canvas_.view().set_viewport(w, h);
    camera_.set_viewport(w, h);
    if (!navigated_) fit_width(default_width_m_);
    if (!layout_loaded_ && w > 0.0 && h > 0.0) {
        layout_loaded_ = true;
        load_layout();
    }
    sync_view_from_camera();
}

void Shell::set_safe_insets(double left, double top, double right, double bottom) {
    safe_insets_={float(std::max(0.0,left)),float(std::max(0.0,top)),float(std::max(0.0,right)),float(std::max(0.0,bottom))};
}

std::string Shell::layout_path() const {
    // Only for a shell that loaded the config, so tests never touch the real file.
    if (!config_loaded_) return {};
    namespace fs = std::filesystem;
    return (fs::path(gfile::app_config_path()).parent_path() / "layout.txt").string();
}

void Shell::load_layout() {
    std::string path = layout_path();
    if (path.empty()) return;
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0)
        if (const char* home = std::getenv("HOME"); home && *home)
            path = std::string(home) + "/.glcad/layout.txt";
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::string text;
        char buf[512];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        std::fclose(f);
        dock_.restore(text, float(vw_), float(vh_));
    }
}

void Shell::save_layout() const {
    const std::string path = layout_path();
    if (path.empty()) return;
    const std::string dir = path.substr(0, path.rfind('/'));
    glcad_make_dir(dir.c_str());
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        const std::string text = dock_.save(float(vw_), float(vh_));
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

void Shell::fit_width(double metres) {
    if (metres <= 0.0 || vw_ <= 0.0 || vh_ <= 0.0) return;
    const double half_h = (metres * 0.5) * (vh_ / vw_);
    camera_.set_distance(half_h / std::tan(camera_.fov() * 0.5));
    sync_view_from_camera();
}

const gtopo::SketchPlane& Shell::sketch_plane() const noexcept {
    static const gtopo::SketchPlane ground = gtopo::SketchPlane::world_xy();
    if (in_paper_space_ || active_sketch_ < 0 || active_sketch_ >= static_cast<int>(sketches_.size()))
        return ground;
    return sketches_[active_sketch_].plane;
}

bool Shell::looking_at_sketch() const noexcept {
    const auto& pl = sketch_plane();
    return camera_.looks_along(pl.nx, pl.ny, pl.nz);
}

void Shell::sync_view_from_camera() {
    const auto& pl0 = sketch_plane();
    camera_.set_projection(camera_.looks_along(pl0.nx, pl0.ny, pl0.nz)
                               ? gview::Projection::Orthographic
                               : gview::Projection::Perspective);
    const gview::Point3 t = camera_.target();
    const auto& pl = sketch_plane();
    double u, v;
    pl.to_plane(t.x.to_f64(), t.y.to_f64(), t.z.to_f64(), u, v);
    const double half = camera_.ortho_half_height();
    const double scale = half > 0.0 ? vh_ / (2.0 * half) : 40.0;
    canvas_.view().set_mapping(u, v, scale);
}

bool Shell::to_plane(double x, double y, double& sx, double& sy) const {
    const auto content_unscale = [&](double& px, double& py) {
        if (mode_ != Mode::Drawing || drawing_k_ == 1.0 || drawing_k_ <= 1e-12) return;
        double den, ccx, ccy;
        if (!plot_den(den, ccx, ccy)) return;
        double cx, cy, sox, soy;
        canvas_.view().screen_of(ccx, ccy, cx, cy);
        canvas_.view().screen_of(ccx + plot_dx_, ccy + plot_dy_, sox, soy);
        px = cx + (px - sox) / drawing_k_;
        py = cy + (py - soy) / drawing_k_;
    };
    if (looking_at_sketch()) { sx = x; sy = y; content_unscale(sx, sy); return true; }
    const auto& pl = sketch_plane();
    gview::Point3 hit;
    if (!camera_.plane_hit(x, y, gview::Point3::from(pl.ox, pl.oy, pl.oz),
                           gview::Vec3::from(pl.nx, pl.ny, pl.nz), hit))
        return false;
    double u, v;
    pl.to_plane(hit.x.to_f64(), hit.y.to_f64(), hit.z.to_f64(), u, v);
    canvas_.view().screen_of(u, v, sx, sy);
    content_unscale(sx, sy);
    return true;
}

FrameUniforms Shell::frame_uniforms() const {
    FrameUniforms u{};
    const gview::Mat4 vp = camera_.view_projection();
    vp.to_f32(u.view_proj);
    vp.inverse().to_f32(u.inv_view_proj);
    const gview::Point3 e = camera_.eye();
    u.eye[0] = float(e.x.to_f64()); u.eye[1] = float(e.y.to_f64());
    u.eye[2] = float(e.z.to_f64()); u.eye[3] = 1.0f;
    u.viewport[0] = float(vw_);
    u.viewport[1] = float(vh_);
    u.viewport[2] = gtext::Font::sdf_distance_range;
    u.viewport[3] = looking_at_sketch() ? 1.0f : 0.0f;

    const auto& t = themes_.active();
    const auto put = [](float* dst, gtheme::Color c) {
        dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = c.a;
    };
    put(u.background, t.get(T::canvas_background));
    put(u.grid_minor, t.get(T::canvas_grid_minor));
    put(u.grid_major, t.get(T::canvas_grid_major));
    put(u.axis_x,     t.get(T::canvas_axis_x));
    put(u.axis_y,     t.get(T::canvas_axis_y));

    double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
    for (const auto& sh : canvas_.shapes()) {
        double ax, ay, bx, by;
        gcm::to_world(vol_, sh.a, ax, ay);
        gcm::to_world(vol_, sh.b, bx, by);
        const double r = double(sh.radius) / vol_.scale();
        lo_x = std::min({lo_x, ax - r, bx}); hi_x = std::max({hi_x, ax + r, bx});
        lo_y = std::min({lo_y, ay - r, by}); hi_y = std::max({hi_y, ay + r, by});
    }
    if (lo_x > hi_x) { lo_x = -5.0; lo_y = -5.0; hi_x = 5.0; hi_y = 5.0; }
    const double mx = std::max(1.0, (hi_x - lo_x) * 0.15);
    const double my = std::max(1.0, (hi_y - lo_y) * 0.15);
    u.plane[0] = float(lo_x - mx); u.plane[1] = float(lo_y - my);
    u.plane[2] = float(hi_x + mx); u.plane[3] = float(hi_y + my);
    u.plane_style[0] = 0.12f;
    u.plane_style[1] = float(px(18));
    u.plane_style[2] = grid_visible_ && !plot_preview_ ? 1.0f : 0.0f;
    {
        const double target = 64.0 / std::max(canvas_.view().scale(), 1e-12);
        u.plane_style[3] = float(canvas_.grid_spacing() > 0.0
                                     ? canvas_.grid_spacing()
                                     : std::pow(10.0, std::floor(std::log10(target))));
    }
    {
        const auto& pl = sketch_plane();
        const float o[4] = {float(pl.ox), float(pl.oy), float(pl.oz), 1.0f};
        const float uu[4] = {float(pl.ux), float(pl.uy), float(pl.uz), 0.0f};
        const float vv[4] = {float(pl.vx), float(pl.vy), float(pl.vz), 0.0f};
        const float nn[4] = {float(pl.nx), float(pl.ny), float(pl.nz), 0.0f};
        std::copy(o, o + 4, u.plane_origin);
        std::copy(uu, uu + 4, u.plane_u);
        std::copy(vv, vv + 4, u.plane_v);
        std::copy(nn, nn + 4, u.plane_n);
        u.plane_n[3]=float(canvas_.grid_angle);
        u.plane_u[3]=(vars_.get_int("UCSICON",3)&1)?1.f:0.f;
    }
    u.section[0] = float(section_nx_);
    u.section[1] = float(section_ny_);
    u.section[2] = float(section_nz_);
    u.section[3] = float(section_d_);
    return u;
}

void Shell::set_lens(Lens l) noexcept {
    history_.record(camera_);
    const auto& pl = sketch_plane();
    if (l == Lens::TwoD) camera_.look_along_normal(pl.nx, pl.ny, pl.nz);
    else if (looking_at_sketch()) camera_.look(gview::StandardView::Iso);
    sync_view_from_camera();
}
void Shell::toggle_lens() noexcept {
    set_lens(lens() == Lens::TwoD ? Lens::ThreeD : Lens::TwoD);
}

void Shell::pointer_move(double x, double y) {
    if (plot_preview_) {
        if (plot_preview_->dragging) {
            plot_preview_->pan_x += x - input_.mouse.x;
            plot_preview_->pan_y += y - input_.mouse.y;
        }
        input_.mouse = {float(x), float(y)};
        return;
    }
    const bool dragging = right_down_ || middle_down_ || cube_drag_.active || (ui_wants_mouse_ && input_.down) ||
                          modeller_.stage() == Modeller::Stage::Dragging || layout_drag_ ||
                          canvas_.grip_dragging() || canvas_.selection_dragging() ||
                          canvas_.band().active;
    const bool navigating = right_down_ || middle_down_ || cube_drag_.active;
    if (dragging && !navigating) touch_metadata("drag");
    if (hover_done_ && !dragging) {
        // Update the raw position now; wheel zoom anchors on input_.mouse.
        input_.mouse_delta = {float(x - input_.mouse.x), float(y - input_.mouse.y)};
        input_.mouse = {float(x), float(y)};
        hover_pending_ = true;
        hover_x_ = x;
        hover_y_ = y;
        return;
    }
    hover_done_ = true;
    pointer_move_impl(x, y);
}

Shell::CursorKind Shell::cursor() {
    if (plot_preview_) return CursorKind::Arrow;
    const gui::Vec2 m=input_.mouse;
    if(middle_down_ || right_down_ || cube_drag_.active || canvas_.grip_dragging() ||
       canvas_.dragging_selection() || modeller_.stage()==Modeller::Stage::Dragging)
        return CursorKind::Grab;
    if(!canvas_rect_.contains(m) || ui_ctx_.wants_mouse())return CursorKind::Arrow;
    if(text_placing_ || text_entry_)return CursorKind::Text;
    if(canvas_.tool()!=Tool::Select || pick_.active() || hatch_pending_ || picking_sketch_plane_)
        return CursorKind::Cross;
    if(mode_==Mode::Solid)
        return hover_target_.valid || (modeller_.edge_hovered() && !modeller_.edge_kept()) ? CursorKind::Pick : CursorKind::Arrow;

    double sx=0,sy=0;
    if(!to_plane(m.x,m.y,sx,sy))return hover_target_.valid?CursorKind::Pick:CursorKind::Arrow;
    const auto selected=[](const auto& items,auto index) {
        return std::find(items.begin(),items.end(),index)!=items.end();
    };
    const auto text=canvas_.pick_text(sx,sy);
    if(text!=Canvas::npos)return selected(canvas_.selected_texts(),text)?CursorKind::Move:CursorKind::Pick;
    const auto dimension=canvas_.pick_dim(sx,sy);
    if(dimension!=Canvas::npos)return selected(canvas_.selected_dimensions(),dimension)?CursorKind::Move:CursorKind::Pick;
    const auto shape=canvas_.pick(sx,sy);
    if(shape!=Canvas::npos)return selected(canvas_.selection(),shape)?CursorKind::Move:CursorKind::Pick;
    const auto reference=canvas_.pick_reference(sx,sy);
    if(reference!=Canvas::npos32)return selected(canvas_.selected_references(),reference)?CursorKind::Move:CursorKind::Pick;
    if(canvas_.pick_hatch(sx,sy)!=Canvas::npos || hover_target_.valid ||
       (modeller_.edge_hovered() && !modeller_.edge_kept()))return CursorKind::Pick;
    return CursorKind::Arrow;
}

void Shell::pointer_move_impl(double x, double y) {
    input_.mouse_delta = {float(x - input_.mouse.x), float(y - input_.mouse.y)};
    input_.mouse = {float(x), float(y)};
    sync_view_from_camera();

    if(!library_drag_command_.empty())return;
    if(viewport_drag_index_>=0) { move_viewport_drag(x,y); return; }

    if (right_down_) {
        pan_selection(input_.mouse_delta.x, input_.mouse_delta.y);
        sync_view_from_camera();
        return;
    }
    if (middle_down_) {
        if (input_.shift) {
            pan_selection(input_.mouse_delta.x, input_.mouse_delta.y);
        } else {
            camera_.orbit_about(focus_pivot(),
                                -input_.mouse_delta.x * 0.008,
                                input_.mouse_delta.y * 0.008);
        }
        sync_view_from_camera();
        return;
    }

    if (!product_edit_allowed_) return;

    bool over_control=false;
    for (const auto& item:ui_ctx_.accessibility_tree())
        if (interactive_role(item.role) && item.bounds.contains(input_.mouse)) { over_control=true; break; }
    if ((ui_wants_mouse_ || over_control)) {
        canvas_.clear_hover();
        hover_target_=Modeller::Target{};
        modeller_.clear_target_hover();
        region_probe_pending_=mode_!=Mode::Drawing && !over_control &&
            canvas_rect_.contains(input_.mouse) && canvas_.tool()==Tool::Select;
        probe_x_=x; probe_y_=y;
        return;
    }

    if (cube_drag_.active) {
        gview::ViewCube::update_drag(camera_, cube_drag_, x, y);
        sync_view_from_camera();
        return;
    }

    if (layout_drag_) {
        double wx, wy;
        canvas_.view().world_of(x, y, wx, wy);
        double dx = layout_dx0_ + (wx - layout_from_x_);
        double dy = layout_dy0_ + (wy - layout_from_y_);
        double den = 1.0, ccx = 0.0, ccy = 0.0, pcx, pcy, pw, ph;
        if (plot_den(den, ccx, ccy) && den > 1e-12 && paper_rect(pcx, pcy, pw, ph)) {
            const double k = 1.0 / den;
            const double tol = 8.0 / std::max(canvas_.view().scale(), 1e-9);
            const double ex = (layout_hi_x_ - layout_lo_x_) * 0.5 * k;
            const double ey = (layout_hi_y_ - layout_lo_y_) * 0.5 * k;
            const auto snap1 = [&](double& d, double half, double extent) {
                if (std::fabs(d) < tol) { d = 0.0; return; }
                if (std::fabs(d - (half - extent)) < tol) d = half - extent;
                else if (std::fabs(d + (half - extent)) < tol) d = extent - half;
            };
            snap1(dx, pw * 0.5, ex);
            snap1(dy, ph * 0.5, ey);
        }
        plot_dx_ = dx;
        plot_dy_ = dy;
        navigated_ = true;
        return;
    }

    if (modeller_.op() != Modeller::Op::None) {
        if (modeller_.stage() == Modeller::Stage::Dragging) {
            modeller_.drag(camera_, x, y);
            mesh_dirty_ = true;
        } else {
            if (modeller_.op() == Modeller::Op::Extrude && modeller_.stage() == Modeller::Stage::Placed &&
                sel_target_.valid && canvas_.tool() == Tool::Select && !modeller_.arrow_under(camera_, x, y) &&
                modeller_.hover_edge(camera_, x, y))
                return;
            modeller_.hover(camera_, x, y);
        }
        return;
    }

    double sx, sy;
    if (!to_plane(x, y, sx, sy)) {
        hover_target_ = Modeller::Target{};
        if (mode_ == Mode::Drawing) return;
        if (canvas_.tool() == Tool::Select && modeller_.hover_edge(camera_, x, y)) return;
        if (canvas_.tool() == Tool::Select) {
            region_probe_pending_ = true;
            probe_x_ = x; probe_y_ = y;
        }
        return;
    }
    if (canvas_.grip_dragging())            canvas_.update_grip_drag(sx, sy);
    else if (canvas_.selection_dragging())  canvas_.update_selection_drag(sx, sy);
    else if (canvas_.band().active)         canvas_.update_band(sx, sy);
    else                                    canvas_.hover(sx, sy);

    hover_target_ = Modeller::Target{};
    if (mode_ == Mode::Drawing) return;
    if (canvas_.tool() == Tool::Select && !canvas_.grip_dragging() && !canvas_.selection_dragging() &&
        !canvas_.band().active && !pick_.active() && !hatch_pending_ && !picking_sketch_plane_ &&
        modeller_.hover_edge(camera_, x, y)) {
        region_probe_pending_ = true;
        probe_x_ = x; probe_y_ = y;
        return;
    }
    if (canvas_.tool() == Tool::Select && !canvas_.grip_dragging() &&
        !canvas_.selection_dragging() && !canvas_.band().active &&
        (mode_ == Mode::Solid || picking_sketch_plane_ || canvas_.pick(sx, sy) == Canvas::npos)) {
        region_probe_pending_ = true;
        probe_x_ = x; probe_y_ = y;
    }
}

void Shell::resolve_region_probe() {
    region_probe_pending_ = false;
    if (mode_ == Mode::Drawing) return;
    const double x = probe_x_, y = probe_y_;
    if (canvas_.tool() != Tool::Select || ui_ctx_.covered_by_overlay({float(x),float(y)})) return;
    if (modeller_.op()!=Modeller::Op::None) {
        if (modeller_.stage()!=Modeller::Stage::Dragging) modeller_.hover(camera_,x,y);
        return;
    }
    double sx, sy;
    if (!to_plane(x, y, sx, sy)) {
        Modeller::Target t;
        if (modeller_.target_at(camera_, x, y, t, mode_ == Mode::Solid) && t.on_body) hover_target_ = t;
        return;
    }
    if (canvas_.grip_dragging() || canvas_.selection_dragging() || canvas_.band().active ||
        pick_.active() || hatch_pending_)
        return;
    if (mode_ == Mode::Solid || picking_sketch_plane_ || canvas_.pick(sx, sy) == Canvas::npos) {
        Modeller::Target t;
        if (!modeller_.target_at(camera_, x, y, t, mode_ == Mode::Solid)) return;
        if (modeller_.edge_hovered()) {
            if (t.source < 0 || !t.on_body || t.from_face) return;
            modeller_.clear_edge_hover();
        }
        hover_target_ = t;
    }
}

void Shell::middle_down(double x, double y) {
    if (plot_preview_) return;
    navigated_ = true;
    input_.mouse = {float(x), float(y)};
    input_.mouse_delta = {0.0f, 0.0f};
    middle_down_ = true;
    history_.record(camera_);
}

void Shell::middle_up(double, double) {
    middle_down_ = false;
}

void Shell::right_up(double, double) {
    right_down_ = false;
}

gview::Point3 Shell::orbit_pivot() const {
    if (!canvas_.selection().empty()) {
        double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
        bool any = false;
        for (std::size_t i : canvas_.selection()) {
            if (i >= canvas_.shapes().size()) continue;
            const gsnap::Shape& s = canvas_.shapes()[i];
            double ax, ay, bx, by;
            gcm::to_world(vol_, s.a, ax, ay);
            gcm::to_world(vol_, s.b, bx, by);
            for (auto [px_, py_] : {std::pair{ax, ay}, std::pair{bx, by}}) {
                if (!any) { lo_x = hi_x = px_; lo_y = hi_y = py_; any = true; }
                lo_x = std::min(lo_x, px_); hi_x = std::max(hi_x, px_);
                lo_y = std::min(lo_y, py_); hi_y = std::max(hi_y, py_);
            }
        }
        if (any)
            return gview::Point3::from((lo_x + hi_x) * 0.5, (lo_y + hi_y) * 0.5,
                                       0.0);
    }

    if (!modeller_.bodies().empty()) {
        auto box = ggeo::d3::AABB<gcm::F64>::empty();
        for (const gtopo::Body& b : modeller_.bodies()) {
            const auto own = b.bounds(vol_);
            box.expand(own.lo);
            box.expand(own.hi);
        }
        if (box.lo.x <= box.hi.x)
            return gview::Point3::from((box.lo.x.to_f64() + box.hi.x.to_f64()) * 0.5,
                                       (box.lo.y.to_f64() + box.hi.y.to_f64()) * 0.5,
                                       (box.lo.z.to_f64() + box.hi.z.to_f64()) * 0.5);
    }
    return camera_.target();
}

void Shell::pointer_down(double x, double y) {
    touch_metadata("pointer_down");
    input_.mouse = {float(x), float(y)};
    input_.pressed = true;
    input_.down = true;
    sync_view_from_camera();

    if (!text_color_property_.empty()) {
        if (!text_color_panel_.contains(input_.mouse) && !text_color_anchor_.contains(input_.mouse)) {
            text_color_property_.clear();input_.pressed=false;input_.down=false;
        }
        return;
    }
    if (!layer_color_target_.empty()) {
        if (!layer_color_panel_.contains(input_.mouse) && !layer_color_anchor_.contains(input_.mouse)) {
            layer_color_target_.clear();
            input_.pressed = false;
            input_.down = false;
        }
        return;
    }
    if (ctx_menu_open_) {
        if (!ctx_menu_bounds_.contains(input_.mouse)) {
            ctx_menu_open_ = false;
            input_.pressed = false;
            input_.down = false;
        }
        return;
    }

    if (!product_edit_allowed_) return;

    if (plot_preview_) { plot_preview_->dragging = y > plot_preview_->toolbar_height; return; }
    if (ui_wants_mouse_) return;
    for (const auto& item:ui_ctx_.accessibility_tree())
        if (interactive_role(item.role) && item.bounds.contains(input_.mouse)) return;

    input_.pressed=false;
    ui_ctx_.clear_focus();

    if(mode_==Mode::Sketch && canvas_.tool()==Tool::Select && !pick_.active() && !hatch_pending_ &&
       !picking_sketch_plane_ && !text_placing_ &&
       (modeller_.op()==Modeller::Op::None || (modeller_.op()==Modeller::Op::Extrude && modeller_.stage()==Modeller::Stage::Placed))) {
        canvas_.adopt_wall_sections();
        double sx,sy;
        if(to_plane(x,y,sx,sy) && canvas_.pick(sx,sy)==Canvas::npos &&
           canvas_.pick_text(sx,sy)==Canvas::npos && canvas_.pick_dim(sx,sy)==Canvas::npos)
        if(const auto section=canvas_.pick_wall_section(sx,sy);section!=Canvas::npos32) {
            modeller_.arm(Modeller::Op::None);clear_solid_selection();
            if(input_.shift || input_.cmd || input_.ctrl)canvas_.select_at(sx,sy,true);
            else {
                if(std::find(canvas_.selected_references().begin(),canvas_.selected_references().end(),section)==canvas_.selected_references().end())
                    canvas_.select_reference(section);
                canvas_.begin_selection_drag(sx,sy);
            }
            mesh_dirty_=true;return;
        }
    }

    if (region_probe_pending_ && modeller_.op() == Modeller::Op::None) {
        probe_x_ = x; probe_y_ = y;
        resolve_region_probe();
    }

    const auto layout = cube_layout();
    if (gview::ViewCube::begin_drag(camera_, layout, x, y, cube_drag_)) {
        history_.record(camera_);
        navigated_ = true;
        return;
    }

    if(begin_viewport_drag(x,y)) return;

    if (mode_ == Mode::Drawing && active_tool_id_ == ToolId::PlaceLayout) {
        {
            layout_drag_ = true;
            canvas_.view().world_of(x, y, layout_from_x_, layout_from_y_);
            layout_dx0_ = plot_dx_;
            layout_dy0_ = plot_dy_;
            layout_lo_x_ = 1e300; layout_lo_y_ = 1e300;
            layout_hi_x_ = -1e300; layout_hi_y_ = -1e300;
            for (const auto& sh : canvas_.shapes()) {
                double ax, ay;
                gcm::to_world(vol_, sh.a, ax, ay);
                layout_lo_x_ = std::min(layout_lo_x_, ax); layout_hi_x_ = std::max(layout_hi_x_, ax);
                layout_lo_y_ = std::min(layout_lo_y_, ay); layout_hi_y_ = std::max(layout_hi_y_, ay);
            }
            return;
        }
    }

    const bool additive=input_.cmd || input_.ctrl || input_.shift;
    const bool selecting=canvas_.tool()==Tool::Select && !pick_.active() && !hatch_pending_ &&
        !picking_sketch_plane_ && !text_placing_ &&
        (modeller_.op()==Modeller::Op::None || modeller_.edge_kept() ||
         (modeller_.op()==Modeller::Op::Extrude && modeller_.stage()==Modeller::Stage::Placed));
    if(additive && selecting) {
        last_click_time_=-1;
        if(modeller_.op()==Modeller::Op::Extrude) modeller_.arm(Modeller::Op::None);
        if(mode_!=Mode::Drawing && modeller_.toggle_edge_at(camera_,x,y)) {mesh_dirty_=true;return;}
        Modeller::Target hit;
        double sx=0,sy=0;
        const bool drawing_hit=mode_!=Mode::Solid && to_plane(x,y,sx,sy) &&
            (canvas_.pick(sx,sy)!=Canvas::npos || canvas_.pick_text(sx,sy)!=Canvas::npos ||
             canvas_.pick_dim(sx,sy)!=Canvas::npos || canvas_.pick_reference(sx,sy)!=Canvas::npos32 ||
             canvas_.pick_hatch(sx,sy)!=Canvas::npos);
        if(mode_!=Mode::Drawing && !drawing_hit && modeller_.target_at(camera_,x,y,hit,mode_==Mode::Solid)) {
            if(selected_targets_.empty() && sel_target_.valid) selected_targets_.push_back(sel_target_);
            const auto same=[&](const Modeller::Target& target) {
                if(target.on_body!=hit.on_body || target.source!=hit.source) return false;
                if(hit.on_body) return target.body==hit.body && (target.face==hit.face ||
                    std::find(target.surface_faces.begin(),target.surface_faces.end(),hit.face)!=target.surface_faces.end());
                return target.profile.outer.size()==hit.profile.outer.size() &&
                    std::equal(target.profile.outer.begin(),target.profile.outer.end(),hit.profile.outer.begin(),
                        [](const auto& a,const auto& b) {return a.x==b.x && a.y==b.y;});
            };
            const auto found=std::find_if(selected_targets_.begin(),selected_targets_.end(),same);
            if(found==selected_targets_.end()) selected_targets_.push_back(hit);
            else selected_targets_.erase(found);
            sel_target_=selected_targets_.empty() ? Modeller::Target{} : selected_targets_.back();
            mesh_dirty_=true;
            return;
        }
        if(mode_!=Mode::Solid && to_plane(x,y,sx,sy)) {
            if(!drawing_hit) canvas_.begin_band(sx,sy);
            else if(input_.cmd || input_.ctrl) canvas_.select_edge_at(sx,sy);
            else canvas_.select_at(sx,sy,true);
            mesh_dirty_=true;
        }
        return;
    }

    if (mode_ != Mode::Drawing && canvas_.tool() == Tool::Select && !picking_sketch_plane_ && !text_placing_) {
        const double t = now_seconds();
        const bool dbl = last_click_time_ >= 0.0 && t - last_click_time_ < 0.4 &&
                         std::hypot(x - last_click_x_, y - last_click_y_) < px(6);
        if (dbl && selected_targets_.size()+selected_bodies_.size()<=1) {
            Modeller::Target hit;
            if (modeller_.target_at(camera_, x, y, hit, mode_ == Mode::Solid) && hit.valid) {
                last_click_time_ = -1.0;
                if (modeller_.op() == Modeller::Op::Extrude) modeller_.arm(Modeller::Op::None);
                clear_solid_selection();
                if (hit.source >= 0) edit_sketch(hit.source);
                else if (hit.on_body) begin_sketch_on_face(hit.body, hit.face);
                return;
            }
        }
    }
    if (mode_ != Mode::Drawing && canvas_.tool() == Tool::Select && !pick_.active() && !hatch_pending_ && !picking_sketch_plane_ &&
        (modeller_.op() == Modeller::Op::None || modeller_.edge_kept() ||
         (modeller_.op() == Modeller::Op::Extrude && modeller_.stage() == Modeller::Stage::Placed && modeller_.edge_hovered() &&
          !modeller_.arrow_under(camera_, x, y)))) {
        if (modeller_.op() == Modeller::Op::Extrude && modeller_.edge_hovered() && !modeller_.edge_kept()) {
            modeller_.arm(Modeller::Op::None);
            clear_solid_selection();
        }
        if (modeller_.edge_kept()) {
            const bool was_applied = modeller_.edge_applied();
            (void)was_applied;
            modeller_.finish_edge();
            if (modeller_.hover_edge(camera_, x, y)) {
                modeller_.begin_edge();
                clear_solid_selection();
                log_.push_back(modeller_.message());
                return;
            }
            mesh_dirty_ = true;
        } else if (modeller_.edge_hovered()) {
            modeller_.begin_edge();
            clear_solid_selection();
            sel_body_ = -1;
            canvas_.clear_selection();
            log_.push_back(modeller_.message());
            return;
        }
    }
    if (mode_ != Mode::Drawing && modeller_.op() == Modeller::Op::Extrude && modeller_.stage() == Modeller::Stage::Placed &&
        sel_target_.valid && !modeller_.arrow_under(camera_, x, y)) {
        Modeller::Target t;
        if (modeller_.target_at(camera_, x, y, t, mode_ == Mode::Solid)) {
            select_target(t);
            return;
        }
        modeller_.arm(Modeller::Op::None);
        clear_solid_selection();
        hover_target_ = Modeller::Target{};
        mesh_dirty_ = true;
    }
    if (modeller_.op() != Modeller::Op::None) {
        if (modeller_.press(camera_, x, y)) mesh_dirty_ = true;
        if (modeller_.take_committed()) after_commit();
        else if (!modeller_.message().empty()) log_.push_back(modeller_.message());
        return;
    }

    if (picking_sketch_plane_) {
        picking_sketch_plane_=false;
        Modeller::Target hit;
        if (modeller_.target_at(camera_,x,y,hit,true) && hit.on_body)
            begin_sketch_on_face(hit.body,hit.face);
        else begin_sketch_on_ground();
        return;
    }

    double sx, sy;
    if (!to_plane(x, y, sx, sy)) {
        if (hover_target_.valid && hover_target_.on_body) select_target(hover_target_);
        return;
    }

    // Order matters: grip, then existing selection, then empty space.
    if (text_placing_) {
        double u, v;
        canvas_.view().world_of(sx, sy, u, v);
        text_pos_ = gcm::to_lattice(vol_, u, v);
        text_placing_ = false;
        text_entry_ = true;
        text_buffer_.clear();
        log_.push_back("type the text, Enter to place it, Esc to cancel");
        return;
    }

    if (pick_.active()) {
        double u, v;
        canvas_.view().world_of(sx, sy, u, v);
        pick_.pts.push_back(gcm::to_lattice(vol_, u, v));
        if (!pick_.finish_on_enter && static_cast<int>(pick_.pts.size()) >= pick_.wanted) {
            auto done = std::move(pick_.done);
            const std::vector<gcm::Lattice2> pts = pick_.pts;
            pick_ = PointPick{};
            if (done) done(pts);
            mesh_dirty_ = true;
        } else {
            log_.push_back(pick_.prompt + " (" + std::to_string(pick_.pts.size()) + " of " + std::to_string(pick_.wanted) + ")");
        }
        return;
    }
    if (hatch_pending_) {
        hatch_pending_ = false;
        canvas_.hatch_at(sx, sy,canvas_.hatch_style().name);
        log_.push_back(canvas_.message());
        mesh_dirty_ = true;
        return;
    }

    if (canvas_.tool() == Tool::Select) {
        const double t = now_seconds();
        const bool dbl = last_click_time_ >= 0.0 && t - last_click_time_ < 0.4 &&
                         std::hypot(x - last_click_x_, y - last_click_y_) < px(6);
        last_click_time_ = t; last_click_x_ = x; last_click_y_ = y;
        if (mode_ != Mode::Drawing && dbl && hover_target_.valid) {
            last_click_time_ = -1.0;
            if (hover_target_.source >= 0) edit_sketch(hover_target_.source);
            else if (hover_target_.on_body) begin_sketch_on_face(hover_target_.body, hover_target_.face);
            return;
        }
        if (mode_ == Mode::Solid) {
            if (hover_target_.valid) select_target(hover_target_);
            else clear_solid_selection();
            return;
        }
        if (!(input_.shift || input_.cmd || input_.ctrl) && canvas_.begin_grip_drag(sx, sy)) return;
        if (!(input_.shift || input_.cmd || input_.ctrl) && canvas_.begin_selection_drag(sx, sy)) return;
        if(canvas_.pick(sx,sy)==Canvas::npos &&
           canvas_.pick_text(sx,sy)==Canvas::npos && canvas_.pick_dim(sx,sy)==Canvas::npos &&
           canvas_.pick_reference(sx,sy)==Canvas::npos32 && canvas_.pick_hatch(sx,sy)!=Canvas::npos) {
            canvas_.select_at(sx,sy,input_.shift || input_.cmd || input_.ctrl);
            clear_solid_selection();sel_body_=-1;return;
        }
        if (mode_ != Mode::Drawing && hover_target_.valid && canvas_.pick(sx, sy) == Canvas::npos) {
            select_target(hover_target_);
            return;
        }
        clear_solid_selection();
        sel_body_ = -1;
        if (!(input_.shift || input_.cmd || input_.ctrl) && canvas_.select_and_drag(sx, sy)) return;
        canvas_.begin_band(sx, sy);
        return;
    }
    canvas_.click(sx, sy, input_.shift || input_.cmd || input_.ctrl);
    last_edit_was_solid_ = false;
    mesh_dirty_ = true;
}

void Shell::settle_pointer(double x, double y) {
    pointer_move(x, y);
    if (!building_frame_ && (std::fabs(frame_mouse_.x - float(x)) > 0.5f || std::fabs(frame_mouse_.y - float(y)) > 0.5f))
        build_frame();
}

void Shell::finger_tap(double x, double y) {
    settle_pointer(x, y);
    if (tap_region_at(x, y)) return;
    const gui::Vec2 at{float(x), float(y)};
    if (canvas_rect_.contains(at) && mode_ == Mode::Sketch &&
        canvas_.tool() != Tool::Select && !text_placing_ &&
        !placement_chip_rect_.contains(at) && !shapes_menu_rect_.contains(at)) {
        return;
    }
    pointer_down(x, y);
    pointer_up(x, y);
}

bool Shell::region_tap_target(double x, double y, Modeller::Target& t) {
    const Tool tool = canvas_.tool();
    const bool shape_tool = tool == Tool::Line || tool == Tool::Rect || tool == Tool::Circle ||
                            tool == Tool::Arc || tool == Tool::Polyline || tool == Tool::Ellipse ||
                            tool == Tool::Spline;
    double sx, sy;
    return mode_ == Mode::Sketch && shape_tool && !canvas_.pending() && !text_placing_ && !hatch_pending_ &&
           !pick_.active() && !picking_sketch_plane_ && modeller_.op() == Modeller::Op::None &&
           canvas_rect_.contains({float(x), float(y)}) && !ui_ctx_.covered_by_overlay({float(x), float(y)}) &&
           to_plane(x, y, sx, sy) && canvas_.pick(sx, sy) == Canvas::npos &&
           modeller_.target_at(camera_, x, y, t, false) && t.valid && !t.on_body;
}

bool Shell::tap_region_at(double x, double y) {
    Modeller::Target t;
    if (!region_tap_target(x, y, t)) return false;
    return_to_select();
    hover_target_ = t;
    select_target(t);
    return true;
}

void Shell::pencil_tap(double x, double y) {
    settle_pointer(x, y);
    if (tap_region_at(x, y)) return;
    pointer_down(x, y);
    pointer_up(x, y);
}

void Shell::pointer_input(PointerPhase phase, double x, double y, bool secondary) {
    switch (phase) {
        case PointerPhase::Down: {
            settle_pointer(x, y);
            pointer_secondary_ = secondary;
            if (secondary) { right_down(x, y); return; }
            Modeller::Target t;
            if (region_tap_target(x, y, t)) {
                pointer_deferred_ = true;
                pointer_down_x_ = x; pointer_down_y_ = y;
                return;
            }
            pointer_down(x, y);
            return;
        }
        case PointerPhase::Move:
            if (pointer_deferred_ && std::hypot(x - pointer_down_x_, y - pointer_down_y_) > px(4)) {
                pointer_deferred_ = false;
                pointer_down(pointer_down_x_, pointer_down_y_);
            }
            pointer_move(x, y);
            return;
        case PointerPhase::Up:
            if (pointer_secondary_) { pointer_secondary_ = false; right_up(x, y); return; }
            if (pointer_deferred_) {
                pointer_deferred_ = false;
                if (tap_region_at(pointer_down_x_, pointer_down_y_)) return;
                pointer_down(pointer_down_x_, pointer_down_y_);
            }
            pointer_up(x, y);
            return;
    }
}

void Shell::apply_gesture(const TouchResult& r) {
    const double fx = r.from_pencil ? r.x : r.raw_x, fy = r.from_pencil ? r.y : r.raw_y;
    switch (r.gesture) {
        case Gesture::Press:
            pointer_move(fx, fy);
            break;
        case Gesture::Tap:
            if (r.from_pencil) pencil_tap(r.x, r.y);
            else finger_tap(r.raw_x, r.raw_y);
            break;
        case Gesture::Drag:
            if (r.from_pencil) {
                if (r.began) { settle_pointer(r.x, r.y); pointer_down(r.x, r.y); }
                pointer_move(r.x, r.y);
                if (r.ended) pencil_release(r.x, r.y);
            } else {
                if (r.began) middle_down(r.raw_x, r.raw_y);
                pointer_move(r.raw_x, r.raw_y);
                if (r.ended) middle_up(r.raw_x, r.raw_y);
            }
            break;
        case Gesture::Pan:
            if (r.dx != 0.0 || r.dy != 0.0) scroll(r.dx, r.dy, true);
            if (std::fabs(r.scale - 1.0) > 1e-5) pinch(r.centre_x, r.centre_y, r.scale - 1.0);
            break;
        case Gesture::Zoom:
            pinch(r.centre_x, r.centre_y, r.scale - 1.0);
            break;
        case Gesture::LongPress:
            right_down(fx, fy);
            right_up(fx, fy);
            break;
        case Gesture::TwoFingerTap:
            key_special(1);
            break;
        default:
            break;
    }
}

void Shell::pencil_release(double x, double y) {
    pointer_move(x, y);
    pointer_up(x, y);
    if (mode_ == Mode::Sketch && canvas_.tool() != Tool::Select && canvas_.pending()) {
        canvas_.click(x, y, false);
        canvas_.cancel();
        log_.push_back(canvas_.message());
    }
}

void Shell::pointer_up(double x, double y) {
    if (!product_edit_allowed_) {
        input_.mouse={float(x),float(y)};
        input_.released=true; input_.down=false;
        return;
    }
    touch_metadata("pointer_up");
    if (plot_preview_) { plot_preview_->dragging = false; input_.mouse = {float(x), float(y)}; input_.released = true; input_.down = false; return; }
    input_.released = true;
    input_.down = false;
    input_.mouse={float(x),float(y)};
    if(!library_drag_command_.empty()) {finish_library_drag(x,y);return;}
    if(viewport_drag_index_>=0) { move_viewport_drag(x,y); viewport_drag_index_=-1; flush_edits(); return; }
    if (canvas_.end_grip_drag()) mesh_dirty_ = true;

    if (layout_drag_) {
        layout_drag_ = false;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "drawing placed %+.0f, %+.0f mm on the sheet",
                      plot_dx_ * 1000.0, plot_dy_ * 1000.0);
        log_.push_back(buf);
        return;
    }

    if (cube_drag_.active) {
        const auto layout = cube_layout();
        if (gview::ViewCube::end_drag(cube_drag_)) {
            const auto part = gview::ViewCube::pick(camera_, layout, x, y);
            if (part.valid()) gview::ViewCube::apply(camera_, part);
        }
        history_.record(camera_);
        sync_view_from_camera();
        return;
    }

    if (modeller_.release()) {
        after_commit();
        return;
    }

    if (canvas_.selection_dragging()) {
        canvas_.end_selection_drag();
        mesh_dirty_ = true;
        return;
    }

    if (canvas_.band().active) {
        double sx, sy;
        if (!to_plane(x, y, sx, sy)) { canvas_.cancel_band(); return; }
        {
            const auto& b = canvas_.band();
            double u0, v0, u1, v1;
            canvas_.view().world_of(b.x0, b.y0, u0, v0);
            canvas_.view().world_of(b.x1, b.y1, u1, v1);
            last_band_lo_ = gcm::to_lattice(vol_, std::min(u0, u1), std::min(v0, v1));
            last_band_hi_ = gcm::to_lattice(vol_, std::max(u0, u1), std::max(v0, v1));
            have_last_band_ = true;
        }
        if (canvas_.end_band(input_.shift || input_.cmd || input_.ctrl) == Canvas::npos) {
            if(input_.cmd || input_.ctrl) canvas_.select_edge_at(sx,sy);
            else canvas_.click(sx,sy,input_.shift);
        }
        mesh_dirty_ = true;
        return;
    }
}
void Shell::right_down(double x, double y) {
    if (!text_color_property_.empty()) { text_color_property_.clear(); return; }
    if (!layer_color_target_.empty()) { layer_color_target_.clear(); return; }
    if (plot_preview_) return;
    input_.mouse = {float(x), float(y)};
    input_.mouse_delta = {0.0f, 0.0f};
    if (input_.shift) {
        right_down_ = true;
        navigated_ = true;
        history_.record(camera_);
        return;
    }
    input_.right_pressed = true;
    canvas_.cancel_grip_drag();
    canvas_.cancel_selection_drag();
    canvas_.cancel_band();
    canvas_.cancel();
    cli_->cancel();
}

void Shell::scroll(double dx, double dy, bool precise) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || (!precise && dy==0)) return;
    // Navigation is not an edit; it must not open the journal gate.
    if (ui_wants_mouse_) touch_metadata("scroll over ui");
    if (plot_preview_) {
        if (precise) { plot_preview_->pan_x += dx; plot_preview_->pan_y += dy; }
        else plot_preview_->zoom = std::clamp(plot_preview_->zoom * (dy > 0 ? 1.1 : 1 / 1.1), .25, 8.0);
        return;
    }
    if (ui_ctx_.covered_by_overlay(input_.mouse, 0)) {
        input_.wheel += float(precise ? dy / 40.0 : dy);
        return;
    }
    if (bar_.input.empty() && command_history_rect_.contains(input_.mouse)) {
        input_.wheel += float(precise ? dy / 40.0 : dy);
        return;
    }
    for (const auto& p : dock_.panels()) {
        const auto sl = dock_.slot(p.id);
        if (sl.visible && sl.frame.contains(input_.mouse)) {
            input_.wheel += float(precise ? dy / 40.0 : dy);
            return;
        }
    }
    navigated_ = true;
    if (precise) pan_selection(dx, dy);
    else zoom_selection(input_.mouse.x, input_.mouse.y, dy > 0 ? 0.9091 : 1.1);
    sync_view_from_camera();
}

void Shell::pinch(double x, double y, double mag) {
    if (!std::isfinite(mag) || mag<=-1.0 || !std::isfinite(x) || !std::isfinite(y)) return;
    if (ui_ctx_.covered_by_overlay({float(x), float(y)}, 0)) return;
    navigated_ = true;
    zoom_selection(x, y, std::clamp(1.0 / (1.0 + mag),0.5,2.0));
    sync_view_from_camera();
}

void Shell::set_modifiers(bool s, bool c, bool a, bool m) {
    input_.shift = s; input_.ctrl = c; input_.alt = a; input_.cmd = m;
}

bool Shell::set_feature_value(int body, int feature, double value) {
    if (!require_edit_access()) return false;
    if (body < 0 || feature < 0) return false;
    // Flush first so this value gets its own undo step.
    flush_edits();
    build_frame_sources();
    const bool ok = modeller_.set_feature_value(std::size_t(body), std::size_t(feature), value);
    log_.push_back(ok ? modeller_.message() : "could not rebuild: " + modeller_.last_rebuild_message());
    if (ok) { last_edit_was_solid_ = true; mesh_dirty_ = true; clear_solid_selection(); }
    return ok;
}

bool Shell::remove_feature(int body, int feature) {
    if (!require_edit_access()) return false;
    if (body < 0 || feature < 0) return false;
    flush_edits();
    build_frame_sources();
    const bool ok = modeller_.remove_feature(std::size_t(body), std::size_t(feature));
    log_.push_back(modeller_.message());
    if (ok) {
        last_edit_was_solid_ = true; mesh_dirty_ = true;
        clear_solid_selection();
        sel_feature_body_ = -1; sel_feature_ = -1; feature_typed_.clear();
        if (sel_body_ >= static_cast<int>(modeller_.bodies().size())) sel_body_ = -1;
    }
    return ok;
}

void Shell::key_char(const std::string& s) {
    if (!product_edit_allowed_) return;
    touch_metadata("key_char");
    if (plot_preview_) return;
    if(s==" " && input_.shift && !text_entry_ && !ui_ctx_.focus_is_field() &&
       canvas_.tool()==Tool::Select && mode_==Mode::Sketch && !pick_.active()) {
        double x,y;
        if(to_plane(input_.mouse.x,input_.mouse.y,x,y)) {
            canvas_.cycle_selection(x,y);log_.push_back(canvas_.message());
        }
        return;
    }
    if (ui_ctx_.focus_is_field() && !ui_ctx_.is_focused(ui_ctx_.id("command-input"))) { input_.typed+=s; return; }

    if (sel_feature_ >= 0 && !text_entry_ && s.size() == 1 &&
        (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '.' || s[0] == '-')) {
        feature_typed_ += s;
        return;
    }
    if (text_entry_) {
        text_buffer_ += s;
        return;
    }

    if ((modeller_.stage() == Modeller::Stage::Placed ||
         modeller_.stage() == Modeller::Stage::Dragging ||
         modeller_.stage() == Modeller::Stage::TypeValue) &&
        s.size() == 1 && (std::isdigit(static_cast<unsigned char>(s[0])) ||
                          s[0] == '.' || s[0] == '-' || s[0] == ' ' || !modeller_.typed().empty())) {
        modeller_.type(s[0]);
        return;
    }

    if (!ui_ctx_.wants_keys() &&
        canvas_.dynamic_input().visible && !s.empty() &&
        (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '.' ||
         s[0] == '-' || s[0] == '<')) {
        canvas_.dynamic_type(s);
        return;
    }
    input_.typed += s;
}

void Shell::return_to_select() {
    library_drag_command_.clear();
    library_drag_preview_.clear();
    cli_->cancel();
    pending_args_.clear();
    pending_hint_.clear();
    pick_ = PointPick{};
    hatch_pending_ = false;
    picking_sketch_plane_ = false;
    bar_.input.clear();
    bar_.prompt.clear();
    bar_.keywords.clear();
    bar_.completions.clear();
    bar_.highlight = 0;
    bar_.submitted = false;
    bar_.chosen_completion = -1;
    input_.typed.clear();
    input_.key_enter = false;
    canvas_.cancel_grip_drag();
    canvas_.cancel();
    canvas_.dynamic_cancel();
    canvas_.cancel_selection_drag();
    canvas_.cancel_band();
    if (canvas_.tool() != Tool::Select) canvas_.set_tool(Tool::Select);
    if (modeller_.stage() != Modeller::Stage::Idle) modeller_.cancel();
    if (modeller_.op() != Modeller::Op::None) modeller_.arm(Modeller::Op::None);
    modeller_.clear_edge_hover();
    active_tool_id_ = ToolId::Select;
    text_entry_ = false;
    text_placing_ = false;
    text_editing_ = static_cast<std::size_t>(-1);
    text_buffer_.clear();
    mesh_dirty_ = true;
}

bool Shell::focus_accessible(gui::Id id) {
    for (const auto& item:ui_ctx_.accessibility_tree()) {
        if (item.id!=id || item.disabled || item.bounds.empty()) continue;
        if (item.role!=gui::Context::Role::Field && item.role!=gui::Context::Role::Button &&
            item.role!=gui::Context::Role::Toggle && item.role!=gui::Context::Role::Checkbox &&
            item.role!=gui::Context::Role::Slider) return false;
        ui_ctx_.set_focus(id);
        return true;
    }
    return false;
}

bool Shell::replace_focused_text(gui::Id id, const std::string& utf8) {
    if (!id || !ui_ctx_.is_focused(id)) return false;
    for (const auto& item:ui_ctx_.accessibility_tree()) {
        if (item.id!=id || item.disabled || item.role!=gui::Context::Role::Field) continue;
        input_.replacement_id=id;
        input_.replacement_text=utf8;
        input_.typed.clear();
        touch_metadata("native text edit");
        return true;
    }
    return false;
}

void Shell::key_special(int code) {
    if (!product_edit_allowed_) {
        if (code==0) input_.key_enter=true;
        if (code==3) input_.key_tab=true;
        return;
    }
    touch_metadata("key_special");
    if (plot_preview_) { if (code == 1) close_plot_preview(); return; }
    if (code == 3 && (input_.ctrl || input_.cmd) && tab_count() > 1) {
        run_command(input_.shift ? "PREVTAB" : "NEXTTAB");
        return;
    }
    const auto value_focus=std::find(value_focus_order_.begin(),value_focus_order_.end(),ui_ctx_.focused());
    const bool in_values=value_focus!=value_focus_order_.end();
    const bool idle_command=ui_ctx_.is_focused(ui_ctx_.id("command-input")) && bar_.input.empty();
    if (code==3 && ui_ctx_.overlay_rects().empty() && !value_focus_order_.empty() &&
        (!ui_ctx_.wants_keys() || idle_command || in_values)) {
        const auto n=value_focus_order_.size();
        const auto next=!in_values ? (input_.shift ? n-1 : 0) :
            (std::size_t(value_focus-value_focus_order_.begin())+(input_.shift ? n-1 : 1))%n;
        ui_ctx_.set_focus(value_focus_order_[next]);
        return;
    }
    if (code==0 && in_values) { input_.key_enter=true; return; }
    switch (code) {
        case 5:  input_.key_left = true;  return;
        case 6:  input_.key_right = true; return;
        case 7:  input_.key_up = true;    return;
        case 8:  input_.key_down = true;  return;
        case 9:  input_.key_home = true;  return;
        case 10: input_.key_end = true;   return;
        default: break;
    }

    if (ui_ctx_.focus_is_field() && !ui_ctx_.is_focused(ui_ctx_.id("command-input")) && code!=1) {
        if (code==0) input_.key_enter=true;
        else if (code==2) input_.key_backspace=true;
        else if (code==3) input_.key_tab=true;
        else if (code==4) input_.key_delete=true;
        return;
    }

    const bool dynamic = !ui_ctx_.wants_keys() &&
                         !canvas_.dynamic_buffer().empty();

    if (text_entry_) {
        if (code == 0) {
            if (text_editing_ != static_cast<std::size_t>(-1)) {
                canvas_.set_text(text_editing_, text_buffer_);
                log_.push_back(text_buffer_.empty() ? "note emptied" : "note edited");
                text_editing_ = static_cast<std::size_t>(-1);
            } else {
                canvas_.add_text(text_pos_, text_buffer_, text_height_, 0.0, 1, 2,
                                 text_width_, text_styles_.current());
                log_.push_back(text_buffer_.empty() ? "no text" : "text placed");
            }
            text_entry_ = false;
            text_buffer_.clear();
            last_edit_was_solid_ = false;
            active_tool_id_ = ToolId::Select;
        } else if (code == 1) {
            return_to_select();
        } else if (code == 2) {
            if (!text_buffer_.empty()) text_buffer_.pop_back();
        }
        return;
    }
    if (text_placing_ && code == 1) { return_to_select(); return; }
    if (code == 4) {
        // By announced role, not wants_keys: the command bar holds default focus.
        if (ui_ctx_.focus_is_field() && !idle_command) { input_.key_delete = true; return; }
        delete_selected();
        return;
    }
    if (sel_feature_ >= 0 && !text_entry_) {
        if (code == 0 && !feature_typed_.empty()) {
            const double v = gunit::parse_length(feature_typed_, canvas_.display_unit()).value_or(0.0);
            feature_typed_.clear();
            if (std::fabs(v) > 1e-12) set_feature_value(sel_feature_body_, sel_feature_, v);
            return;
        }
        if (code == 2 && !feature_typed_.empty()) { feature_typed_.pop_back(); return; }
        if (code == 1) { sel_feature_body_ = -1; sel_feature_ = -1; feature_typed_.clear(); input_.key_escape = true; return_to_select(); return; }
    }

    switch (code) {
        case 0:
            if (pick_.active() && pick_.finish_on_enter) {
                if (static_cast<int>(pick_.pts.size()) < pick_.wanted) {
                    log_.push_back("pick at least " + std::to_string(pick_.wanted) + " points");
                    return;
                }
                auto done=std::move(pick_.done);auto pts=std::move(pick_.pts);pick_=PointPick{};
                if(done) done(pts);
                return;
            }
            if (modeller_.commit_typed()) {
                after_commit();
                return;
            }
            if (dynamic && canvas_.dynamic_commit()) {
                mesh_dirty_ = true;
                return;
            }
            input_.key_enter = true;
            break;
        case 1:
            if (!text_color_property_.empty()) { text_color_property_.clear(); return; }
            if (!layer_color_target_.empty()) { layer_color_target_.clear(); return; }
            if (ctx_menu_open_) { ctx_menu_open_ = false; return; }
            if(viewport_drag_index_>=0) { cancel_viewport_drag(); break; }
            if (settings_open_) { settings_open_ = false; return; }
            if (!pending_args_.empty()) {
                log_.push_back(pending_args_ + ": cancelled");
                pending_args_.clear();
                pending_hint_.clear();
                input_.key_escape = true;
                break;
            }
            if (pick_.active()) { pick_ = PointPick{}; log_.push_back("cancelled"); input_.key_escape = true; break; }
            if (hatch_pending_) { hatch_pending_ = false; input_.key_escape = true; break; }
            if (picking_sketch_plane_) { picking_sketch_plane_ = false; input_.key_escape = true; break; }
            if (modeller_.edge_kept()) {
                clear_solid_selection();
                modeller_.finish_edge();
                active_tool_id_ = ToolId::Select;
                mesh_dirty_ = true;
                input_.key_escape = true;
                break;
            }
            if (modeller_.stage() != Modeller::Stage::Idle) {
                const bool was_selection = sel_target_.valid && modeller_.op() == Modeller::Op::Extrude;
                modeller_.cancel();
                if (was_selection) { modeller_.arm(Modeller::Op::None); clear_solid_selection(); }
                if (modeller_.op() == Modeller::Op::None) active_tool_id_ = ToolId::Select;
                mesh_dirty_ = true;
                input_.key_escape = true;
                break;
            }
            canvas_.cancel_selection_drag();
            canvas_.cancel_band();
            canvas_.dynamic_cancel();
            clear_solid_selection();
            input_.key_escape = true;
            break;
        case 2:
            if (!modeller_.typed().empty()) {
                modeller_.backspace();
                return;
            }
            if (dynamic) {
                canvas_.dynamic_backspace();
                return;
            }
            if ((input_.cmd || input_.ctrl) && (!ui_ctx_.focus_is_field() || idle_command)) {
                delete_selected();
                return;
            }
            input_.key_backspace = true;
            break;
        case 3:
            input_.key_tab = true;
            break;
        default: break;
    }
    if (code == 1) { return_to_select(); log_line("cancelled"); }
}


void Shell::rebuild_mesh() {
    sync_document(false);
    const auto& solid_mesh = modeller_.mesh();
    std::vector<gcm::u64> state{
        modeller_.mesh_version(), grender::palette_revision(),
        gcm::u64(display_mode_), gcm::u64(wireframe_),
        static_fills_.empty() ? 0 : static_fills_version_, gcm::u64(static_fills_.size()),
        gcm::u64(modeller_.stage()), gcm::u64(modeller_.op())};
    const auto scalar = [&](double value) {
        gcm::u64 bits; std::memcpy(&bits, &value, sizeof(bits)); state.push_back(bits);
    };
    state.push_back(modeller_.mesh_ranges().size());
    for(const auto& range:modeller_.mesh_ranges()) {
        state.push_back(range.key);
        state.push_back(!range.key || doc_.find(document_body_id(range.key))!=nullptr);
    }
    const auto target = [&](const Modeller::Target& t) {
        state.push_back(t.valid);
        if (!t.valid) return;
        state.push_back(t.body); state.push_back(t.face);
        state.push_back(t.surface_faces.size());
        for (auto face : t.surface_faces) state.push_back(face);
        scalar(t.normal.x.to_f64()); scalar(t.normal.y.to_f64()); scalar(t.normal.z.to_f64());
        const auto profile = [&](const auto& loop) {
            state.push_back(loop.size());
            for (const auto& p : loop) { state.push_back(gcm::u64(p.x)); state.push_back(gcm::u64(p.y)); }
        };
        profile(t.profile.outer); state.push_back(t.profile.holes.size());
        for (const auto& loop : t.profile.holes) profile(loop);
        state.push_back(t.outline.size());
        for (const auto& loop : t.outline) {
            state.push_back(loop.size());
            for (const auto& p : loop) {
                scalar(p.x.to_f64()); scalar(p.y.to_f64()); scalar(p.z.to_f64());
            }
        }
    };
    if (modeller_.stage() == Modeller::Stage::Idle) {
        target(sel_target_); target(hover_target_);
    } else {
        target(modeller_.target()); target(modeller_.hovered());
    }
    state.push_back(selected_targets_.size());
    for(const auto& selected:selected_targets_) target(selected);
    if (state == mesh_state_) return;
    mesh_state_ = std::move(state);
    ++mesh_version_;
    if (display_mode_ == 0) {
        mesh_.clear();
        std::vector<float> seg;
        modeller_.wireframe(seg);
        wire_lines_.clear();
        wire_lines_.reserve(seg.size() / 3);
        const gtheme::Color gc = themes_.active().get(T::geometry);
        for (std::size_t i = 0; i + 5 < seg.size(); i += 6) {
            wire_lines_.push_back({{seg[i], seg[i + 1], seg[i + 2]}, {gc.r, gc.g, gc.b, 1.0f}});
            wire_lines_.push_back({{seg[i + 3], seg[i + 4], seg[i + 5]}, {gc.r, gc.g, gc.b, 1.0f}});
        }
    } else {
        wire_lines_.clear();
        mesh_ = solid_mesh;
    }
    if (wireframe_) mesh_.clear();
    rebuild_scene();
    if(!static_fills_.empty()) {
        const auto first=mesh_.size();
        mesh_.insert(mesh_.end(),static_fills_.begin(),static_fills_.end());
        grender::DrawItem fill;fill.id={0x617265615f66696cull,1};fill.kind=grender::DrawKind::Overlay;
        fill.first=first;fill.count=mesh_.size()-first;fill.material.translucent=true;fill.material.depth_bias=-1;
        for(std::size_t i=first;i<mesh_.size();++i)fill.bounds.add(mesh_[i].pos[0],mesh_[i].pos[1],mesh_[i].pos[2]);
        scene_.put(fill);
    }

    const auto& theme = themes_.active();
    const gtheme::Color hc = theme.get(T::geometry_highlight);
    const gtheme::Color ac = theme.get(T::ui_accent);
    const float hover[4] = {hc.r, hc.g, hc.b, 0.22f};
    const float sel[4] = {ac.r, ac.g, ac.b, 0.35f};
    const auto op=modeller_.op();
    if (modeller_.preview().active || modeller_.value_preview_valid() ||
        (modeller_.stage()==Modeller::Stage::PickSecond && modeller_.hovered().valid &&
         (op==Modeller::Op::Union || op==Modeller::Op::Subtract || op==Modeller::Op::Intersect))) return;
    const std::size_t overlay_first=mesh_.size();
    if(selected_targets_.size()>1 || modeller_.edge_kept())
        for(const auto& selected:selected_targets_) modeller_.fill(selected,sel,mesh_);
    if (modeller_.stage() == Modeller::Stage::Idle) {
        if (sel_target_.valid && selected_targets_.size()<=1) modeller_.fill(sel_target_, sel, mesh_);
        if (hover_target_.valid) modeller_.fill(hover_target_, hover, mesh_);
    } else {
        if (modeller_.hovered().valid) modeller_.fill(modeller_.hovered(), hover, mesh_);
        if (modeller_.target().valid) modeller_.fill(modeller_.target(), sel, mesh_);
    }
    if (mesh_.size()>overlay_first) {
        grender::DrawItem overlay;
        overlay.id={0x686f7665725f7569ull,1};
        overlay.kind=grender::DrawKind::Overlay;
        overlay.first=overlay_first; overlay.count=mesh_.size()-overlay_first;
        overlay.material.translucent=true;
        overlay.material.depth_bias=-1;
        for (std::size_t i=overlay_first;i<mesh_.size();++i)
            overlay.bounds.add(mesh_[i].pos[0],mesh_[i].pos[1],mesh_[i].pos[2]);
        scene_.put(overlay);
    }
}

void Shell::build_frame_sources() {
    static const std::vector<gdoc::Segment2> none;
    std::vector<Modeller::SketchSource> sources;
    for (int i = 0; i < static_cast<int>(sketches_.size()); ++i) {
        const bool hidden = (i != active_sketch_ || mode_ != Mode::Sketch) && canvas_.layer_hidden(sketches_[i].layer);
        const bool live = i == active_sketch_;
        Modeller::SketchSource src;
        src.plane = sketches_[i].plane;
        src.segments = hidden ? &none : (live ? &sketch_segs_ : &sketches_[i].segments);
        src.version = hidden ? 0 : (live ? sketch_segs_key_ : sketches_[i].segments_version);
        sources.push_back(std::move(src));
    }
    modeller_.set_sketches(std::move(sources));
    for (int i=0; i<int(sketches_.size()); ++i) modeller_.set_sketch_visible(i,sketch_visible(i));
}

void Shell::rebuild_sketch_segments() {
    const gcm::u64 key = canvas_.edit_serial() * 7919ull + static_cast<gcm::u64>(active_sketch_ + 1);
    if (key == sketch_segs_key_) return;
    sketch_segs_key_ = key;
    sketch_segs_.clear();
    grender::d2::DrawList tmp;
    canvas_.build_geometry(tmp);
    tmp.flatten_arcs_chordal(Canvas::kModelChordTolerance);
    for (const auto& l : tmp.lines()) {
        if (l.role != grender::Role::Geometry &&
            l.role != grender::Role::Selected) continue;
        sketch_segs_.push_back({gcm::to_lattice(vol_, l.x0, l.y0),
                                gcm::to_lattice(vol_, l.x1, l.y1)});
    }
}

gui::Rect Shell::canvas_fit_bounds() const noexcept {
    auto rect = canvas_rect_;
    const float top = std::min(float(px(136)), std::max(0.f, rect.h - float(px(64))));
    rect.y += top;
    rect.h -= top;
    return rect;
}

gview::CubeLayout Shell::cube_layout() const {
    gview::CubeLayout l;
    l.size = px(110);
    l.margin = px(16);
    l.x = canvas_rect_.right() - l.size - l.margin;
    l.y = canvas_rect_.y + l.margin;
    return l;
}

void Shell::emit_text(const gui::Rect& r, const std::string& s, float size,
                      gui::Color c, gui::TextAlign align, const gui::Rect& clip) {
    if (s.empty() || !font_.loaded()) return;

    gtext::TextStyle st;
    st.size = size;
    st.valign = gtext::VAlign::Baseline;
    const gtext::Layout lay = gtext::layout_text(font_, s, st);
    if (lay.empty()) return;

    float ox = r.x;
    if (align == gui::TextAlign::Centre) ox = r.x + (r.w - lay.width) * 0.5f;
    else if (align == gui::TextAlign::Right) ox = r.x + r.w - lay.width;

    float oy = r.y + r.h * 0.5f + size * font_.metrics().cap_height * 0.5f;

    if (lay.glyphs.size() == 1) {
        const gtext::Glyph* g = lay.glyphs.front().glyph;
        const bool is_icon = g && g->codepoint >= 0xE000u && g->codepoint <= 0xF8FFu;
        if (is_icon)
            oy = r.y + r.h * 0.5f + lay.glyphs.front().y + lay.glyphs.front().h * 0.5f;
    }

    const float aw = float(font_.atlas_width());
    const float ah = float(font_.atlas_height());

    for (const auto& g : lay.glyphs) {
        const float x0 = ox + g.x, y0 = oy - g.y - g.h;
        const float x1 = x0 + g.w, y1 = y0 + g.h;
        const float u0 = g.glyph->ax / aw, v0 = g.glyph->ay / ah;
        const float u1 = (g.glyph->ax + g.glyph->aw) / aw;
        const float v1 = (g.glyph->ay + g.glyph->ah) / ah;

        const float col[4] = {c.r, c.g, c.b, c.a};
        const float cl[4]  = {clip.x, clip.y, clip.w, clip.h};
        const float px[6][2] = {{x0,y0},{x1,y0},{x1,y1},{x0,y0},{x1,y1},{x0,y1}};
        const float uv[6][2] = {{u0,v0},{u1,v0},{u1,v1},{u0,v0},{u1,v1},{u0,v1}};
        for (int k = 0; k < 6; ++k) {
            TextVertex v{};
            v.pos[0] = px[k][0]; v.pos[1] = px[k][1];
            v.uv[0]  = uv[k][0]; v.uv[1]  = uv[k][1];
            std::memcpy(v.color, col, sizeof(col));
            std::memcpy(v.clip, cl, sizeof(cl));
            text_.push_back(v);
        }
    }
}

void Shell::emit_cad_text_mesh(const std::vector<gclip::Vertex>& mesh, gui::Color color, const gui::Rect& clip,
        const std::function<bool(double,double,double&,double&)>& screen) {
    for(std::size_t i=0;i<mesh.size();i+=3) {
        TextVertex triangle[3]{};bool visible=true;
        for(int k=0;k<3;++k) {
            const auto& p=mesh[i+k];double sx,sy;
            if(!screen(p.x,p.y,sx,sy)){visible=false;break;}
            triangle[k]={{float(sx),float(sy)},{float(p.u),float(p.v)},
                {color.r,color.g,color.b,color.a},{clip.x,clip.y,clip.w,clip.h}};
        }
        if(visible)text_.insert(text_.end(),std::begin(triangle),std::end(triangle));
    }
}

void Shell::emit_cad_text(const Canvas::Label& lab, gui::Color color, const gui::Rect& clip,
        const std::function<bool(double,double,double&,double&)>& screen) {
    if(lab.stroke_font || lab.outline_font || lab.box_width>0) return;
    if(!font_.loaded() || lab.text.empty() || lab.height<=0) return;
    const auto mesh=Canvas::label_mesh(lab,&font_,canvas_.volume());
    emit_cad_text_mesh(mesh,color,clip,screen);
}

void Shell::build_frame() {
    if (building_frame_) return;
    {
        struct Building { bool& flag; ~Building() { flag = false; } } building{building_frame_};
        building_frame_ = true;
        build_frame_body();
    }
    auto asks = std::move(deferred_file_asks_);
    deferred_file_asks_.clear();
    for (auto& ask : asks)
        if (file_hook_) file_hook_(ask.first, std::move(ask.second));
        else ask.second({});
}

void Shell::request_file(FileAsk kind, std::function<void(std::string)> done) {
    if (!file_hook_) { done({}); return; }
    if (building_frame_) { deferred_file_asks_.emplace_back(kind, std::move(done)); return; }
    file_hook_(kind, std::move(done));
}

void Shell::build_frame_body() {
    process_mcp_requests();
    menu_markers_.clear();
    if (plot_preview_) { build_plot_preview_frame(); return; }
    images_.clear();
    canvas_.set_field_values(in_paper_space_ && active_layout() ? sheet_fields(*active_layout()) : std::map<std::string,std::string>{});
    const bool viewport_sheet = mode_ == Mode::Drawing && active_layout() && (in_paper_space_ || !active_layout()->direct_model_view || !active_layout()->viewports.empty() || !active_layout()->plot_style_data.empty());
    hover_done_ = false;
    if (hover_pending_) {
        hover_pending_ = false;
        pointer_move_impl(hover_x_, hover_y_);
    }
    apply_sysvars();
    if (canvas_.edit_serial() != dims_synced_) {
        dims_synced_ = canvas_.edit_serial();
        canvas_.prune_constraints();
        canvas_.resolve_constraints();
        canvas_.refresh_dimensions();
    }
    if(now_seconds()-lock_check_time_>=1.0) { lock_check_time_=now_seconds(); refresh_document_lock(); }
    autosave_tick();
    ai_join_worker();
    process_ai_tools();
    ai_call_tick();
    if (!ai_->busy) {
        std::string spoken;
        bool from_call = false;
        {
            std::lock_guard<std::mutex> lock(ai_->mutex);
            spoken.swap(ai_->voice_prompt);
            from_call = ai_->voice_from_call;
            ai_->voice_from_call = false;
        }
        if (!spoken.empty()) {
            ai_speak_once_ = from_call;
            ai_send(std::move(spoken));
        }
    }
    ui_.clear();
    text_.clear();
    tris_.clear();
    apply_theme_palette();

    layout_ui();

    gtopo::SketchPlane pl = sketch_plane();
    const gtopo::SketchPlane pl_paper = pl;
    drawing_k_ = 1.0;
    canvas_.set_annotative_fixed(0.0);
    if (mode_ == Mode::Drawing && !in_paper_space_) {
        double den = 1.0, ccx = 0.0, ccy = 0.0;
        if (plot_den(den, ccx, ccy) && den > 1e-12) {
            const double k = 1.0 / den;
            drawing_k_ = k;
            canvas_.set_annotative_fixed(den);
            pl.ox += (pl.ux * ccx + pl.vx * ccy) * (1.0 - k);
            pl.oy += (pl.uy * ccx + pl.vy * ccy) * (1.0 - k);
            pl.oz += (pl.uz * ccx + pl.vz * ccy) * (1.0 - k);
            pl.ux *= k; pl.uy *= k; pl.uz *= k;
            pl.vx *= k; pl.vy *= k; pl.vz *= k;
            pl.ox += pl_paper.ux * plot_dx_ + pl_paper.vx * plot_dy_;
            pl.oy += pl_paper.uy * plot_dx_ + pl_paper.vy * plot_dy_;
            pl.oz += pl_paper.uz * plot_dx_ + pl_paper.vz * plot_dy_;
        }
    }
    canvas_.build_dynamic_only(draw_dynamic_);
    const grender::d2::DrawList& static_draw = canvas_.static_draw();
    const auto lift_arc = [&](const grender::d2::ArcItem& a) {
        ArcInstance inst{};
        const grender::Color c = a.color.a<0 ? grender::color_for(a.role) : a.color;
        double wx, wy, wz;
        pl.to_world(a.cx, a.cy, wx, wy, wz);
        inst.centre[0] = float(wx); inst.centre[1] = float(wy); inst.centre[2] = float(wz);
        inst.centre[3] = float(a.a0);
        const double Ux = a.ux * a.major, Uy = a.uy * a.major;
        const double Vx = -a.uy * a.minor, Vy = a.ux * a.minor;
        inst.major[0] = float(pl.ux * Ux + pl.vx * Uy);
        inst.major[1] = float(pl.uy * Ux + pl.vy * Uy);
        inst.major[2] = float(pl.uz * Ux + pl.vz * Uy);
        inst.major[3] = float(a.a1);
        inst.minor[0] = float(pl.ux * Vx + pl.vx * Vy);
        inst.minor[1] = float(pl.uy * Vx + pl.vy * Vy);
        inst.minor[2] = float(pl.uz * Vx + pl.vz * Vy);
        inst.minor[3] = a.width_px;
        inst.color[0] = c.r; inst.color[1] = c.g; inst.color[2] = c.b; inst.color[3] = c.a;
        return inst;
    };
    const auto lift = [&](const std::vector<grender::d2::Vertex>& in,
                          std::vector<LineVertex>& out) {
        out.clear();
        out.reserve(in.size());
        for (const auto& v : in) {
            double wx, wy, wz;
            pl.to_world(v.x, v.y, wx, wy, wz);
            out.push_back({{float(wx), float(wy), float(wz)}, {v.r, v.g, v.b, v.a}});
        }
    };
    {
        gcm::u64 key = canvas_.draw_version() * 1000003ull + static_cast<gcm::u64>(active_sketch_ + 1);
        for (const auto& sk : sketches_) {
            key = (key * 31ull) ^ static_cast<gcm::u64>(sk.segments.size());
            key = key * 31ull + gcm::u64(sketch_visible(int(&sk-sketches_.data())));
        }
        for(double value:{pl.ox,pl.oy,pl.oz,pl.ux,pl.uy,pl.uz,pl.vx,pl.vy,pl.vz,pl.nx,pl.ny,pl.nz}) {
            gcm::u64 bits;std::memcpy(&bits,&value,sizeof(bits));key=(key*1000003ull)^bits;
        }
        key ^= grender::palette_revision() * 0x9E3779B97F4A7C15ull;
        // Content scale is part of the key: origin-centred scaling never moves the origin.
        key ^= static_cast<gcm::u64>(std::llround(drawing_k_ * 1048576.0)) * 0x9e3779b97f4a7c15ull;
        key ^= static_cast<gcm::u64>(std::llround(plot_dx_ * 1e6)) * 0x2545F4914F6CDD1Dull;
        key ^= static_cast<gcm::u64>(std::llround(plot_dy_ * 1e6)) * 0xDA942042E4DD58B5ull;
        if (!viewport_sheet && key != lifted_key_) {
            lifted_key_ = key;
            previous_static_fills_.swap(static_fills_);
            static_lines_.clear(); static_arcs_.clear(); static_points_.clear(); static_fills_.clear();
            if (in_paper_space_ || sketch_visible(active_sketch_)) {
                const auto ink=themes_.active().get(T::geometry);
                const auto& triangles=static_draw.triangles();
                const std::array<double,16> transform{pl.ox,pl.oy,pl.oz,pl.ux,pl.uy,pl.uz,
                    pl.vx,pl.vy,pl.vz,pl.nx,pl.ny,pl.nz,ink.r,ink.g,ink.b,ink.a};
                static_assert(sizeof(grender::d2::TriangleItem)==6*sizeof(double)+4*sizeof(float));
                const bool same_triangles=triangles.size()==active_fill_triangles_.size() &&
                    (triangles.empty() || std::memcmp(triangles.data(),active_fill_triangles_.data(),
                                                     triangles.size()*sizeof(grender::d2::TriangleItem))==0);
                if(!same_triangles || transform!=active_fill_transform_ ||
                   active_fill_palette_revision_!=grender::palette_revision()) {
                    active_fill_vertices_.clear();active_fill_vertices_.reserve(triangles.size()*3);
                    for(const auto& triangle:triangles)for(int k=0;k<3;++k) {
                        double x,y,z;pl.to_world(triangle.xy[2*k],triangle.xy[2*k+1],x,y,z);
                        MeshVertex vertex{};vertex.pos[0]=float(x);vertex.pos[1]=float(y);vertex.pos[2]=float(z);
                        const auto color=triangle.color.a<0 ? grender::Color{ink.r,ink.g,ink.b,ink.a} : triangle.color;
                        vertex.color[0]=color.r;vertex.color[1]=color.g;
                        vertex.color[2]=color.b;vertex.color[3]=color.a;
                        active_fill_vertices_.push_back(vertex);
                    }
                    active_fill_triangles_=triangles;active_fill_transform_=transform;
                    active_fill_palette_revision_=grender::palette_revision();
                }
                static_fills_.insert(static_fills_.end(),active_fill_vertices_.begin(),active_fill_vertices_.end());
                const auto& items = static_draw.lines();
                static_lines_.resize(items.size() * 2);
                gcm::parallel_for(items.size(), 20000, [&](std::size_t lo, std::size_t hi, unsigned) {
                    for (std::size_t i = lo; i < hi; ++i) {
                        const auto& l = items[i];
                        const grender::Color c = l.color.a<0 ? grender::color_for(l.role) : l.color;
                        double wx, wy, wz;
                        pl.to_world(l.x0, l.y0, wx, wy, wz);
                        static_lines_[2 * i] = {{float(wx), float(wy), float(wz)}, {c.r, c.g, c.b, c.a}, l.width_px};
                        pl.to_world(l.x1, l.y1, wx, wy, wz);
                        static_lines_[2 * i + 1] = {{float(wx), float(wy), float(wz)}, {c.r, c.g, c.b, c.a}, l.width_px};
                    }
                });
                const auto& arcs = static_draw.arcs();
                static_arcs_.resize(arcs.size());
                gcm::parallel_for(arcs.size(), 5000, [&](std::size_t lo, std::size_t hi, unsigned) {
                    for (std::size_t i = lo; i < hi; ++i) static_arcs_[i] = lift_arc(arcs[i]);
                });
                static_points_.clear();
                static_points_.reserve(static_draw.points().size());
                for (const auto& p : static_draw.points()) {
                    const grender::Color c = grender::color_for(p.role);
                    double wx, wy, wz;
                    pl.to_world(p.x, p.y, wx, wy, wz);
                    static_points_.push_back({{float(wx), float(wy), float(wz)}, {c.r, c.g, c.b, c.a}});
                }
            }
            const gtheme::Color gc = themes_.active().get(T::geometry);
            const float dim[4] = {gc.r, gc.g, gc.b, 0.45f};
            std::vector<LineVertex> keep;
            keep.swap(lines_);
            lines_.clear();
            for (int i = 0; i < static_cast<int>(sketches_.size()); ++i)
                if (i != active_sketch_ && sketch_visible(i)) lift_sketch(sketches_[i], dim);
            static_lines_.insert(static_lines_.end(), lines_.begin(), lines_.end());
            lines_.swap(keep);
            static_assert(sizeof(MeshVertex)==10*sizeof(float));
            const bool same_fills=static_fills_.size()==previous_static_fills_.size() &&
                (static_fills_.empty() || std::memcmp(static_fills_.data(),previous_static_fills_.data(),
                                                    static_fills_.size()*sizeof(MeshVertex))==0);
            if(!same_fills)++static_fills_version_;
        }
    }
    lines_.clear();
    points_ = static_points_;
    arcs_.clear();
    if (in_paper_space_ || sketch_visible(active_sketch_))
        for (const auto& a : draw_dynamic_.arcs()) arcs_.push_back(lift_arc(a));
    draw_dynamic_.pack(lines2d_, points2d_);
    if (!in_paper_space_ && !sketch_visible(active_sketch_)) { lines2d_.clear(); points2d_.clear(); }
    {
        std::vector<LineVertex> dl, dp;
        lift(lines2d_, dl);
        for (std::size_t i=0;i<draw_dynamic_.lines().size() && 2*i+1<dl.size();++i)
            dl[2*i].width_px=dl[2*i+1].width_px=draw_dynamic_.lines()[i].width_px;
        lift(points2d_, dp);
        lines_.insert(lines_.end(), dl.begin(), dl.end());
        points_.insert(points_.end(), dp.begin(), dp.end());
    }
    if (mode_ == Mode::Drawing) points_.clear();
    sketch_lines_ = lines_.size();
    if (mode_ == Mode::Drawing) {
        double cx, cy, w, h;
        if (paper_rect(cx, cy, w, h)) {
            const gtheme::Color bg = themes_.active().get(T::canvas_background);
            const bool dark_bg = (bg.r + bg.g + bg.b) < 1.5f;
            const float ir = dark_bg ? 0.82f : 0.13f;
            const float ig = dark_bg ? 0.84f : 0.16f;
            const float ib = dark_bg ? 0.88f : 0.20f;
            const auto sheet_line = [&](double x0, double y0, double x1, double y1,
                                        float r, float g, float b, float a2) {
                double wx, wy, wz;
                pl_paper.to_world(x0, y0, wx, wy, wz);
                lines_.push_back({{float(wx), float(wy), float(wz)}, {r, g, b, a2}});
                pl_paper.to_world(x1, y1, wx, wy, wz);
                lines_.push_back({{float(wx), float(wy), float(wz)}, {r, g, b, a2}});
            };
            const double d = 1.5 / std::max(canvas_.view().scale(), 1e-9);
            const auto rect = [&](double inset, float r, float g, float b, float a2) {
                const double x0 = cx - w / 2 + inset, x1 = cx + w / 2 - inset;
                const double y0 = cy - h / 2 + inset, y1 = cy + h / 2 - inset;
                sheet_line(x0, y0, x1, y0, r, g, b, a2);
                sheet_line(x1, y0, x1, y1, r, g, b, a2);
                sheet_line(x1, y1, x0, y1, r, g, b, a2);
                sheet_line(x0, y1, x0, y0, r, g, b, a2);
            };
            if(!viewport_sheet) rect(-d, ir, ig, ib, 1.0f);
            rect(0.0, ir, ig, ib, 1.0f);
            if(!viewport_sheet) rect(d, ir, ig, ib, 1.0f);
            for (int k = 1; !viewport_sheet && k <= 2; ++k) {
                const double o = d * 2.0 * k;
                sheet_line(cx + w / 2 + o, cy - h / 2 + o, cx + w / 2 + o, cy + h / 2 + o,
                           0.45f, 0.48f, 0.52f, 0.7f);
                sheet_line(cx - w / 2 + o, cy + h / 2 + o, cx + w / 2 + o, cy + h / 2 + o,
                           0.45f, 0.48f, 0.52f, 0.7f);
            }
        }
    }
    if (viewport_sheet) {
        if(!static_fills_.empty())++static_fills_version_;
        static_lines_.clear(); static_arcs_.clear(); static_points_.clear(); static_fills_.clear();
        lifted_key_=~gcm::u64{0};
        if(sketch_lines_) lines_.erase(lines_.begin(),lines_.begin()+std::min(sketch_lines_,lines_.size()));
        arcs_.clear(); points_.clear();
        draw_viewport_sheet();
    }
    if(!viewport_sheet)build_underlays();
    else canvas_.set_underlay_snap_shapes({});
    rebuild_sketch_segments();
    build_frame_sources();
    if (region_probe_pending_) resolve_region_probe();

    if (!viewport_sheet) {
        const auto& theme = themes_.active();
        const gtheme::Color hc = theme.get(T::geometry_highlight);
        const gtheme::Color ac = theme.get(T::ui_accent);
        const float h[4] = {hc.r, hc.g, hc.b, 1.0f};
        const float a[4] = {ac.r, ac.g, ac.b, 1.0f};
        if (display_mode_ == 0)
            lines_.insert(lines_.end(), wire_lines_.begin(), wire_lines_.end());
        modeller_.overlay(camera_, h, a, lines_);
        {
            const auto outline = [&](const Modeller::Target& t, const float* c) {
                for (const auto& loop : t.outline)
                    for (std::size_t i = 0; i < loop.size(); ++i) {
                        const auto& p = loop[i];
                        const auto& q = loop[(i + 1) % loop.size()];
                        lines_.push_back({{float(p.x.to_f64()), float(p.y.to_f64()), float(p.z.to_f64())}, {c[0], c[1], c[2], 1.0f}});
                        lines_.push_back({{float(q.x.to_f64()), float(q.y.to_f64()), float(q.z.to_f64())}, {c[0], c[1], c[2], 1.0f}});
                    }
            };
            if (hover_target_.valid) outline(hover_target_, h);
            if(selected_targets_.empty()) {if(sel_target_.valid) outline(sel_target_,a);}
            else for(const auto& selected:selected_targets_) outline(selected,a);
            if (hover_body_ >= 0) outline_body(std::size_t(hover_body_), h);
            if(selected_bodies_.empty()) {if(sel_body_>=0) outline_body(std::size_t(sel_body_),a);}
            else for(const auto body:selected_bodies_) outline_body(body,a);
        }


    }

    {
        const auto& theme = themes_.active();
        const gview::Mat4 vp = camera_.view_projection();
        const auto screen = [&](double x,double y,double& sx,double& sy) {
            double wx,wy,wz; pl.to_world(x,y,wx,wy,wz); return camera_.project(vp,wx,wy,wz,sx,sy);
        };
        const auto color_of = [&](const Canvas::Label& lab) {
            gtheme::Color c = theme.get(T::geometry);
            if (lab.color.a >= 0.0f) c = gtheme::Color{lab.color.r, lab.color.g, lab.color.b, lab.color.a};
            return gui::Color{c.r,c.g,c.b,c.a};
        };
        if (!viewport_sheet) {
            const auto& labels = canvas_.labels();
            if (static_label_mesh_revision_ != canvas_.labels_revision() ||
                static_label_mesh_font_loaded_ != font_.loaded() ||
                static_label_meshes_.size() != labels.size()) {
                static_label_meshes_.clear();
                static_label_meshes_.resize(labels.size());
                if (font_.loaded())
                    for (std::size_t i = 0; i < labels.size(); ++i)
                        if (!labels[i].stroke_font && !labels[i].outline_font && labels[i].box_width<=0)
                            static_label_meshes_[i] = Canvas::label_mesh(labels[i], &font_, canvas_.volume());
                static_label_mesh_revision_ = canvas_.labels_revision();
                static_label_mesh_font_loaded_ = font_.loaded();
            }
            for (std::size_t i = 0; i < labels.size(); ++i) {
                const auto& lab = labels[i];
                if (lab.stroke_font || lab.outline_font || lab.box_width>0) continue;
                emit_cad_text_mesh(static_label_meshes_[i], color_of(lab), canvas_rect_, screen);
            }
            for (const auto& lab : canvas_.dynamic_labels()) {
                if (lab.stroke_font || lab.outline_font || lab.box_width>0) continue;
                emit_cad_text(lab, color_of(lab), canvas_rect_, screen);
            }
        }
    }

    emit_ui_commands();

    rebuild_mesh();
    mesh_dirty_ = false;
}

void Shell::emit_ui_commands() {
    ui_layers_ = UiLayers{};
    using Kind = UiLayers::Kind;
    ui_layers_.append(Kind::Surface, 0, ui_.size());
    ui_layers_.append(Kind::Triangle, 0, tris_.size());
    ui_layers_.append(Kind::Text, 0, text_.size());
    ui_layers_.ui[1]   = ui_.size();
    ui_layers_.tris[1] = tris_.size();
    ui_layers_.text[1] = text_.size();
    {
        gui::Rect clip{0, 0, float(vw_), float(vh_)};
        std::vector<gui::Rect> clip_stack;
        const std::vector<std::size_t>& layer_starts = ui_ctx_.overlay_layer_starts();
        int layer = 1;
        std::size_t cmd_index = 0;
        for (const auto& cmd : ui_ctx_.draw().commands()) {
            struct RecordRun {
                UiLayers& layers;
                const std::vector<UiInstance>& surfaces;
                const std::vector<TextVertex>& triangles;
                const std::vector<TextVertex>& glyphs;
                std::size_t u, t, x;
                ~RecordRun() {
                    layers.append(Kind::Surface, u, surfaces.size()-u);
                    layers.append(Kind::Triangle, t, triangles.size()-t);
                    layers.append(Kind::Text, x, glyphs.size()-x);
                }
            } record{ui_layers_, ui_, tris_, text_, ui_.size(), tris_.size(), text_.size()};
            while (layer < UiLayers::kCount - 1 &&
                   std::size_t(layer - 1) < layer_starts.size() &&
                   cmd_index >= layer_starts[std::size_t(layer - 1)]) {
                ++layer;
                ui_layers_.ui[layer]   = ui_.size();
                ui_layers_.tris[layer] = tris_.size();
                ui_layers_.text[layer] = text_.size();
            }
            ++cmd_index;
            switch (cmd.prim) {
                case gui::Prim::Clip:
                    clip_stack.push_back(clip);
                    clip = cmd.rect;
                    continue;
                case gui::Prim::Unclip:
                    if (!clip_stack.empty()) { clip = clip_stack.back(); clip_stack.pop_back(); }
                    continue;
                case gui::Prim::Text: {
                    gui::Rect own{cmd.rect.x - 2.0f, cmd.rect.y - 2.0f,
                                  cmd.rect.w + 4.0f, cmd.rect.h + 4.0f};
                    const float ix0 = std::max(clip.x, own.x);
                    const float iy0 = std::max(clip.y, own.y);
                    const float ix1 = std::min(clip.right(), own.right());
                    const float iy1 = std::min(clip.bottom(), own.bottom());
                    const gui::Rect contained{ix0, iy0, std::max(0.0f, ix1 - ix0),
                                              std::max(0.0f, iy1 - iy0)};
                    emit_text(cmd.rect, cmd.text, cmd.font_size, cmd.color, cmd.align,
                              contained);
                    continue;
                }
                case gui::Prim::Triangle: {
                    for (const gui::Vec2& v : {cmd.a, cmd.b, cmd.c}) {
                        TextVertex t{};
                        t.pos[0] = v.x; t.pos[1] = v.y;
                        t.color[0] = cmd.color.r; t.color[1] = cmd.color.g;
                        t.color[2] = cmd.color.b; t.color[3] = cmd.color.a;
                        t.clip[0] = clip.x; t.clip[1] = clip.y; t.clip[2] = clip.w; t.clip[3] = clip.h;
                        tris_.push_back(t);
                    }
                    continue;
                }
                case gui::Prim::Icon:
                    continue;
                case gui::Prim::Line: {
                    const float dx = cmd.b.x - cmd.a.x, dy = cmd.b.y - cmd.a.y;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len < 0.01f) continue;
                    if (std::fabs(dx) > 0.5f && std::fabs(dy) > 0.5f) {
                        const float nx = -dy / len * cmd.thickness * 0.5f;
                        const float ny = dx / len * cmd.thickness * 0.5f;
                        const gui::Vec2 p0{cmd.a.x + nx, cmd.a.y + ny}, p1{cmd.b.x + nx, cmd.b.y + ny};
                        const gui::Vec2 p2{cmd.b.x - nx, cmd.b.y - ny}, p3{cmd.a.x - nx, cmd.a.y - ny};
                        for (const gui::Vec2& v : {p0, p1, p2, p0, p2, p3}) {
                            TextVertex t{};
                            t.pos[0] = v.x; t.pos[1] = v.y;
                            t.color[0] = cmd.color.r; t.color[1] = cmd.color.g;
                            t.color[2] = cmd.color.b; t.color[3] = cmd.color.a;
                            t.clip[0] = clip.x; t.clip[1] = clip.y; t.clip[2] = clip.w; t.clip[3] = clip.h;
                            tris_.push_back(t);
                        }
                        continue;
                    }
                    UiInstance u{};
                    if (std::fabs(dx) >= std::fabs(dy))
                        u = {{std::min(cmd.a.x, cmd.b.x), (cmd.a.y + cmd.b.y) * 0.5f - cmd.thickness * 0.5f,
                              std::fabs(dx), cmd.thickness},
                             {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a},
                             {0, 0, 0, 0}, {clip.x, clip.y, clip.w, clip.h}};
                    else
                        u = {{(cmd.a.x + cmd.b.x) * 0.5f - cmd.thickness * 0.5f, std::min(cmd.a.y, cmd.b.y),
                              cmd.thickness, std::fabs(dy)},
                             {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a},
                             {0, 0, 0, 0}, {clip.x, clip.y, clip.w, clip.h}};
                    ui_.push_back(u);
                    continue;
                }
                default: break;
            }

            const float kind = cmd.prim == gui::Prim::Border ? 1.0f
                             : cmd.prim == gui::Prim::Shadow ? 2.0f : 0.0f;
            UiInstance u{};
            u.rect[0] = cmd.rect.x; u.rect[1] = cmd.rect.y;
            u.rect[2] = cmd.rect.w; u.rect[3] = cmd.rect.h;
            u.color[0] = cmd.color.r; u.color[1] = cmd.color.g;
            u.color[2] = cmd.color.b; u.color[3] = cmd.color.a;
            u.params[0] = cmd.radius; u.params[1] = cmd.thickness;
            u.params[2] = kind; u.params[3] = cmd.prim == gui::Prim::Shadow ? cmd.thickness : 0.0f;
            u.clip[0] = clip.x; u.clip[1] = clip.y; u.clip[2] = clip.w; u.clip[3] = clip.h;
            ui_.push_back(u);
        }
        for (int k = layer + 1; k <= UiLayers::kCount; ++k) {
            ui_layers_.ui[k]   = ui_.size();
            ui_layers_.tris[k] = tris_.size();
            ui_layers_.text[k] = text_.size();
        }
    }

}

void Shell::apply_theme_palette() {
    // Rebuilt every frame: a theme edited in place keeps its name.
    const auto rp = themes_.active().role_palette();
    auto c = [](const gtheme::Color& v) { return grender::Color{v.r, v.g, v.b, v.a}; };

    grender::Palette p;
    p.set(grender::Role::Geometry, c(rp.geometry));
    p.set(grender::Role::Preview, c(rp.preview));
    p.set(grender::Role::Construction, c(rp.construction));
    p.set(grender::Role::Endpoint, c(rp.endpoint));
    p.set(grender::Role::Snap, c(rp.snap));
    p.set(grender::Role::Selected, c(rp.selected));
    p.set(grender::Role::Highlight, c(rp.highlight));
    p.set(grender::Role::Error, c(rp.error));
    p.set(grender::Role::Annotation, c(rp.annotation));
    grender::set_palette(p);

    const auto bg = themes_.active().get(T::canvas_background);
    canvas_.set_ground(grender::Color{bg.r, bg.g, bg.b, 1.0f});
}

std::string Shell::title() const {
    return canvas_.status();
}

}

namespace gcapp {

void Shell::layout_ui() {
    report_modeller_error();
    const auto& theme = themes_.active();
    if (gui::PanelState* dp = dock_.find("drawing"))
        dp->open = (mode_ == Mode::Drawing);
    canvas_.set_current_layer(layers_.current());
    modeller_.set_current_layer(layers_.current());
    modeller_.set_display_unit(canvas_.display_unit());
    {
        std::string layer_hint;
        int sketch_hint = -1;
        if (!canvas_.selection().empty()) {
            const std::size_t i = canvas_.selection().front();
            if (i < canvas_.shapes().size()) {
                layer_hint = canvas_.shapes()[i].layer;
                if (layer_hint.empty()) layer_hint = layers_.current();
            }
            if (mode_ == Mode::Sketch) sketch_hint = active_sketch_;
        } else if (!canvas_.selected_references().empty()) {
            layer_hint = canvas_.blocks().references()[canvas_.selected_references().front()].layer;
        } else if (sel_target_.valid && sel_target_.source >= 0) {
            sketch_hint = sel_target_.source;
            if (sketch_hint >= 0 && sketch_hint < int(sketches_.size()))
                layer_hint = sketches_[std::size_t(sketch_hint)].layer;
        }
        if (layer_hint != sel_layer_hint_) {
            sel_layer_hint_ = layer_hint;
            hint_scroll_layers_ = !layer_hint.empty();
        }
        if (sketch_hint != sel_sketch_hint_) {
            sel_sketch_hint_ = sketch_hint;
            hint_scroll_sketches_ = sketch_hint >= 0;
        }
    }
    {
        std::vector<std::string> hidden;
        for (const auto& l : layers_.all()) if (!l.visible()) hidden.push_back(l.name);
        modeller_.set_hidden_layers(hidden);
        canvas_.set_hidden_layers(std::move(hidden));

        std::vector<std::pair<std::string, grender::Color>> colors;
        for (const auto& l : layers_.all()) {
            gcm::u8 cr = l.color.r, cg = l.color.g, cb = l.color.b;
            if (l.color.kind == gdoc::Color::Kind::Index) {
                if (l.color.index == 7) continue;
                gdoc::aci_rgb(l.color.index, cr, cg, cb);
            } else if (l.color.kind != gdoc::Color::Kind::True) {
                continue;
            }
            colors.push_back({l.name, grender::Color{cr / 255.0f, cg / 255.0f,
                                                     cb / 255.0f, 1.0f}});
        }
        canvas_.set_layer_colors(std::move(colors));
        std::map<std::string,int> transparencies;
        for(const auto& layer:layers_.all()) if(layer.transparency)transparencies.emplace(layer.name,layer.transparency);
        canvas_.set_layer_transparencies(std::move(transparencies));
        canvas_.set_appearance_layers(layers_);

        canvas_.set_linetypes(linetypes_.all());
        std::vector<std::pair<std::string, std::string>> layer_lt;
        for (const auto& l : layers_.all())
            if (l.linetype != "Continuous") layer_lt.push_back({l.name, l.linetype});
        canvas_.set_layer_linetypes(std::move(layer_lt));
        canvas_.set_linetype_scale(linetype_scale_);
    }

    input_.mouse = input_.mouse;
    ui_ctx_.begin({0, 0, float(vw_), float(vh_)}, input_, theme);
    value_focus_order_.clear();
    ui_ctx_.clipboard = clipboard_set_hook;
    if (chat_copy_pending_ && ui_ctx_.focus_is_field()) {
        chat_copy_pending_ = false;
        ui_ctx_.copy_requested = true;
    }

    ui_ctx_.set_icons_available(icons_loaded_);
    sync_view_from_camera();

    const bool frame_layout=layout_frame_pending_;
    layout_frame_pending_=false;
    const gui::Style& st = ui_ctx_.style();
    const float pad = float(px(10));
    gui::Rect screen{0, 0, float(vw_), float(vh_)};
    if (safe_insets_.horizontal() > 0 || safe_insets_.vertical() > 0) {
        const gui::Rect inner = screen.deflate(safe_insets_);
        const auto surface = theme.get(T::ui_surface);
        ui_ctx_.draw().rect({0, 0, screen.w, inner.y}, surface);
        ui_ctx_.draw().rect({0, inner.bottom(), screen.w, screen.bottom() - inner.bottom()}, surface);
        ui_ctx_.draw().rect({0, inner.y, inner.x, inner.h}, surface);
        ui_ctx_.draw().rect({inner.right(), inner.y, screen.right() - inner.right(), inner.h}, surface);
        screen = inner;
    }

    if (!product_edit_allowed_) {
        draw_product_viewer();
    } else {

    gui::Rect status = screen.cut_bottom(float(px(32)));
    draw_header(screen);
    toolbar_top_ = screen.y;
    draw_toolbar(screen);
    draw_text_style_bar(screen);
    draw_hatch_style_bar(screen);
    ui_ctx_.draw().rect(status, theme.get(T::ui_surface));
    ui_ctx_.draw().line({status.x, status.y}, {status.right(), status.y}, 1.0f,
                        theme.get(T::ui_border));
    {
        gui::Rect cursor = status.deflate(gui::Insets::xy(pad, float(px(5))));

        bool ortho = canvas_.polar().ortho;
        bool polar = canvas_.polar().enabled;
        if (gui::status_toggle(ui_ctx_, cursor, "ORTHO", ortho))
            canvas_.polar().ortho = ortho;
        if (gui::status_toggle(ui_ctx_, cursor, "POLAR", polar))
            canvas_.polar().enabled = polar;

        bool osnap = canvas_.snaps().settings.enabled;
        if (gui::status_toggle(ui_ctx_, cursor, "OSNAP", osnap))
            canvas_.snaps().settings.enabled = osnap;
        bool gsnap_on = canvas_.grid_snap();
        if (gui::status_toggle(ui_ctx_, cursor, "SNAP", gsnap_on))
            canvas_.set_grid_snap(gsnap_on);
        if (gui::status_toggle(ui_ctx_, cursor, "GRID", grid_visible_)) {}

        bool plan = looking_at_sketch();
        if (gui::status_toggle(ui_ctx_, cursor, "PLAN", plan))
            set_lens(plan ? Lens::TwoD : Lens::ThreeD);

        cursor.cut_left(float(px(12)));

        char buf[160];
        double cx, cy;
        gcm::to_world(vol_, canvas_.snap().point, cx, cy);
        canvas_.world_to_ucs(cx,cy,cx,cy);
        const int mode=int(vars_.get_int("LUNITS",2));
        gunit::Format fmt;fmt.unit=canvas_.display_unit();fmt.precision=int(vars_.get_int("LUPREC",4));fmt.show_unit=false;
        fmt.style=mode==1?gunit::Style::Scientific:mode==3?gunit::Style::Engineering:mode==4?gunit::Style::Architectural:mode==5?gunit::Style::Fractional:gunit::Style::Decimal;
        gui::status_text(ui_ctx_,cursor,gunit::format_length(cx,fmt)+", "+gunit::format_length(cy,fmt),float(px(160)));

        std::snprintf(buf, sizeof(buf), "snap: %s",
                      canvas_.snap().valid ? canvas_.snap().kind : "-");
        gui::status_text(ui_ctx_, cursor, buf, float(px(130)));

        if (canvas_.track().active) {
            gunit::AngleFormat angle;angle.precision=int(vars_.get_int("AUPREC",2));
            const auto mode=vars_.get_int("AUNITS",0);angle.unit=mode==2?gunit::Angle::Gradian:mode==3?gunit::Angle::Radian:gunit::Angle::Degree;
            gui::status_text(ui_ctx_,cursor,gunit::format_angle(canvas_.track().angle_deg*3.141592653589793/180,angle)+"  "+gunit::format_length(canvas_.track().length,fmt),float(px(150)));
        }

        std::snprintf(buf, sizeof(buf), "%zu segments", canvas_.segment_count());
        gui::status_text(ui_ctx_, cursor, buf, float(px(130)));
    }

    gui::Rect cmd_row = screen.cut_bottom(float(px(48)));
    {
        gui::Rect bar = cmd_row.deflate(gui::Insets::xy(pad, float(px(4))));
        bar_.prompt = pick_.active() ? pick_.prompt
                    : !pending_args_.empty() ? pending_args_ + " " + pending_hint_
                    : cli_->prompt().active ? cli_->prompt().text
                    : modeller_.op() != Modeller::Op::None ? std::string(modeller_.prompt())
                    : std::string{};
        bar_.keywords = cli_->prompt().keywords;

        bar_.completions.clear();
        if (!bar_.input.empty())
            for (const auto& m : commands_.complete(
                     bar_.input,
                     lens() == Lens::TwoD ? gcmd::Modes::TwoD : gcmd::Modes::ThreeD, 6))
                bar_.completions.push_back(m.cmd->name + "   " + m.cmd->description);

        gui::command_bar(ui_ctx_, bar, bar_);

        if (bar_.chosen_completion >= 0 &&
            bar_.chosen_completion < static_cast<int>(bar_.completions.size())) {
            const std::string& row = bar_.completions[static_cast<std::size_t>(bar_.chosen_completion)];
            const std::string name = row.substr(0, row.find(' '));
            bar_.input.clear();
            bar_.highlight = 0;
            run_command(name);
        } else if (bar_.submitted && !bar_.input.empty()) {
            const std::string entered = bar_.input;
            bar_.input.clear();
            if (!pending_args_.empty() &&
                (std::isdigit(static_cast<unsigned char>(entered[0])) || entered[0] == '.' || entered[0] == '-')) {
                const std::string cmd = pending_args_;
                pending_args_.clear();
                run_command(cmd + " " + entered);
            } else {
                pending_args_.clear();
                run_command(entered);
            }
        } else if (bar_.cancelled) {
            pending_args_.clear();
            bar_.input.clear();
            canvas_.cancel();
        }
    }

    if (canvas_.band().active) {
        const auto& b = canvas_.band();
        float lo_x = 1e30f, lo_y = 1e30f, hi_x = -1e30f, hi_y = -1e30f;
        for (auto [cx, cy] : {std::pair{b.x0, b.y0}, std::pair{b.x1, b.y0}, std::pair{b.x1, b.y1}, std::pair{b.x0, b.y1}}) {
            double u, v, wx, wy, wz, sx, sy;
            canvas_.view().world_of(cx, cy, u, v);
            sketch_plane().to_world(u, v, wx, wy, wz);
            if (!camera_.project(gview::Point3::from(wx, wy, wz), sx, sy)) continue;
            lo_x = std::min(lo_x, float(sx)); hi_x = std::max(hi_x, float(sx));
            lo_y = std::min(lo_y, float(sy)); hi_y = std::max(hi_y, float(sy));
        }
        const float x = lo_x, y = lo_y, w = hi_x - lo_x, h = hi_y - lo_y;
        if (w > 1.0f && h > 1.0f) {
            gui::Color c = theme.get(b.crossing ? T::select_crossing
                                                : T::select_window);
            gui::Color fill = c;
            fill.a = 0.18f;
            ui_ctx_.draw().rect({x, y, w, h}, fill);
            ui_ctx_.draw().border({x, y, w, h}, 0.0f, float(px(1)), c);
        }
    }

    screen = dock_.layout(ui_ctx_, screen);
    canvas_rect_ = screen;
    if(pending_extent_fit_ && screen.w>0 && screen.h>0) {
        sync_view_from_camera();
        const auto fit = canvas_fit_bounds();
        camera_.zoom_to_region(*pending_extent_fit_,fit.x,fit.y,fit.w,fit.h);
        refit_bounds_ = pending_extent_fit_;
        pending_extent_fit_.reset();
        sync_view_from_camera();
        refit_pose_ = camera_pose();
        refit_w_ = vw_; refit_h_ = vh_;
    }
    if(frame_layout && in_paper_space_) zoom_to_paper();
    if (dock_.take_changed()) layout_dirty_ = true;
    if (layout_dirty_ && !input_.down) { layout_dirty_ = false; save_layout(); }

    if (const auto sl = dock_.slot("layers"); sl.visible && !sl.collapsed) {
        {
            float rows = float(layers_.all().size());
            for (const auto& ref : canvas_.blocks().references())
                if (std::find(expanded_layers_.begin(), expanded_layers_.end(), ref.layer) !=
                    expanded_layers_.end())
                    rows += 0.8f;
            dock_.set_content_height("layers", rows * (float(px(40)) + float(px(4))));
        }
        if (sl.title.w > 0.0f) {
            gui::Rect plus = sl.title;
            plus.x = plus.right() - float(px(56));
            plus.w = float(px(24));
            plus.y += (plus.h - float(px(24))) * 0.5f;
            plus.h = float(px(24));
            const auto pin = ui_ctx_.interact(ui_ctx_.id("layers-add"), plus);
            if (pin.hovered) {
                gui::Color hover_c = theme.get(T::ui_accent);
                hover_c.a = st.hover_opacity;
                ui_ctx_.draw().round_rect(plus, float(px(12)), hover_c);
            }
            ui_ctx_.draw().text(plus, "+", st.font_size * 1.2f,
                                theme.get(pin.hovered ? T::ui_accent : T::ui_text_dim), gui::TextAlign::Centre);
            if (pin.clicked) add_layer();
        }
        dock_.begin_content(ui_ctx_, "layers");
        gui::Stack s(ui_ctx_, sl.content);
        std::vector<const gdoc::Layer*> ordered;
        ordered.reserve(layers_.all().size());
        for (const auto& l : layers_.all()) ordered.push_back(&l);
        std::sort(ordered.begin(), ordered.end(), [](const gdoc::Layer* a, const gdoc::Layer* b) {
            const std::string& x = a->name; const std::string& y = b->name;
            const std::size_t n = std::min(x.size(), y.size());
            for (std::size_t i = 0; i < n; ++i) {
                const int cx = std::tolower(static_cast<unsigned char>(x[i]));
                const int cy = std::tolower(static_cast<unsigned char>(y[i]));
                if (cx != cy) return cx < cy;
            }
            return x.size() < y.size();
        });
        if (hint_scroll_layers_ && !sel_layer_hint_.empty()) {
            hint_scroll_layers_ = false;
            for (std::size_t k = 0; k < ordered.size(); ++k)
                if (ordered[k]->name == sel_layer_hint_) {
                    dock_.set_scroll("layers",
                                     std::max(0.0f, float(k) * (float(px(40)) + float(px(4))) -
                                                    sl.content.h * 0.4f));
                    break;
                }
        }
        for (const gdoc::Layer* lp : ordered) {
            const auto& l = *lp;
            gui::Rect r = s.next(float(px(40)));
            if (r.h < float(px(32))) break;

            ui_ctx_.push_id(l.name);
            std::vector<gcm::u32> kids;
            for (gcm::u32 ri = 0; ri < gcm::u32(canvas_.blocks().references().size()); ++ri)
                if (canvas_.blocks().references()[ri].layer == l.name) kids.push_back(ri);
            const bool expanded = std::find(expanded_layers_.begin(), expanded_layers_.end(),
                                            l.name) != expanded_layers_.end();
            if (!kids.empty()) {
                gui::Rect tri{r.x, r.y, float(px(16)), r.h};
                const auto tin = ui_ctx_.interact(ui_ctx_.id("disclose"), tri);
                ui_ctx_.draw().text(tri, expanded ? "\xe2\x96\xbe" : "\xe2\x96\xb8",
                                    st.font_size_small,
                                    theme.get(tin.hovered ? T::ui_text : T::ui_text_dim),
                                    gui::TextAlign::Centre);
                if (tin.clicked) {
                    if (expanded)
                        expanded_layers_.erase(std::find(expanded_layers_.begin(),
                                                         expanded_layers_.end(), l.name));
                    else
                        expanded_layers_.push_back(l.name);
                }
                r.x += float(px(16));
                r.w -= float(px(16));
            }
            const auto row_in = ui_ctx_.interact(ui_ctx_.id("row"), r);
            if (l.name == layers_.current())
                ui_ctx_.draw().round_rect(r, 3.0f, theme.get(T::ui_active));
            else if (l.name == sel_layer_hint_) {
                gui::Color a = theme.get(T::ui_accent);
                a.a = 0.10f;
                ui_ctx_.draw().round_rect(r, 3.0f, a);
                ui_ctx_.draw().border(r, 3.0f, 1.5f, theme.get(T::ui_accent));
            }
            else if (row_in.hovered) {
                gui::Color layer = theme.get(T::ui_accent);
                layer.a = st.hover_opacity;
                ui_ctx_.draw().round_rect(r, 3.0f, layer);
            }
            if (row_in.clicked && l.name != layers_.current()) {
                layers_.set_current(l.name);
                log_.push_back("current layer: " + l.name);
            }

            gui::Rect cur = r.deflate(gui::Insets::xy(float(px(8)), 0.0f));
            const float iw = float(px(32));

            auto icon_toggle = [&](gui::Icon on_icon, gui::Icon off_icon,
                                   bool& value, const char* id) {
                gui::Rect b = cur.cut_left(iw);
                ui_ctx_.push_id(id);
                const auto in = ui_ctx_.interact(ui_ctx_.id("t"), b);
                if (in.hovered)
                    ui_ctx_.draw().round_rect(b, 3.0f, theme.get(T::ui_hover));
                gui::draw_icon(ui_ctx_, value ? on_icon : off_icon, b,
                               theme.get(value ? T::ui_text : T::ui_text_dim), 1.3f);
                ui_ctx_.pop_id();
                if (in.clicked) { value = !value; return true; }
                return false;
            };

            bool on = l.on, frozen = l.frozen, locked = l.locked;
            bool changed = false;
            changed |= icon_toggle(gui::Icon::Eye, gui::Icon::EyeOff, on, "on");
            changed |= icon_toggle(gui::Icon::Snowflake, gui::Icon::Snowflake,
                                   frozen, "fz");
            changed |= icon_toggle(gui::Icon::Lock, gui::Icon::Unlock, locked, "lk");
            if (changed) {
                if (gdoc::Layer* t = layers_.find(l.name)) {
                    t->on = on; t->frozen = frozen; t->locked = locked;
                }
            }

            cur.cut_left(float(px(8)));
            gui::Rect sw = cur.cut_left(float(px(24)));
            const auto swatch_id = ui_ctx_.id("layer-color");
            const auto swatch_input = ui_ctx_.interact(swatch_id, sw);
            const std::string swatch_label = "Layer color: " + l.name;
            ui_ctx_.announce(swatch_id, gui::Context::Role::Button, swatch_label, "", sw, false, read_only_);
            ui_ctx_.register_focusable(swatch_id, sw, swatch_label);
            if (!read_only_ && (swatch_input.clicked || ui_ctx_.activated(swatch_id))) {
                layer_color_target_ = l.name;
                layer_color_anchor_ = sw;
                layer_color_tab_ = active_tab_;
            }
            {
                const float chip = float(px(14));
                sw = {sw.x + (sw.w - chip) * 0.5f, sw.y + (sw.h - chip) * 0.5f, chip, chip};
            }
            {
                gcm::u8 cr = l.color.r, cg = l.color.g, cb = l.color.b;
                if (l.color.kind == gdoc::Color::Kind::Index)
                    gdoc::aci_rgb(l.color.index, cr, cg, cb);
                gui::color_swatch(ui_ctx_, sw,
                                  gui::Color::rgb(cr / 255.0f, cg / 255.0f, cb / 255.0f));
            }
            cur.cut_left(float(px(8)));
            gui::Rect del = cur.cut_right(float(px(24)));
            {
                const auto din = ui_ctx_.interact(ui_ctx_.id("del"), del);
                if (din.hovered) {
                    gui::Color hover_c = theme.get(T::ui_accent);
                    hover_c.a = st.hover_opacity;
                    ui_ctx_.draw().round_rect(del, float(px(12)), hover_c);
                }
                ui_ctx_.draw().text(del, "\xc3\x97", st.font_size,
                                    theme.get(din.hovered ? T::ui_text : T::ui_text_dim), gui::TextAlign::Centre);
                if (din.clicked) { delete_layer(l.name); ui_ctx_.pop_id(); break; }
            }
            gui::label(ui_ctx_, cur, l.name, gui::TextAlign::Left,
                       !l.on || l.frozen);
            if (expanded)
                for (gcm::u32 ri : kids) {
                    gui::Rect kid = s.next(float(px(30)));
                    if (kid.h < float(px(24))) break;
                    kid.x += float(px(28));
                    kid.w -= float(px(28));
                    ui_ctx_.push_id("kid" + std::to_string(ri));
                    const auto kin = ui_ctx_.interact(ui_ctx_.id("row"), kid);
                    const auto& sel = canvas_.selected_references();
                    const bool is_sel = std::find(sel.begin(), sel.end(), ri) != sel.end();
                    if (is_sel) {
                        gui::Color a2 = theme.get(T::ui_accent);
                        a2.a = 0.14f;
                        ui_ctx_.draw().round_rect(kid, 3.0f, a2);
                    } else if (kin.hovered) {
                        ui_ctx_.draw().round_rect(kid, 3.0f, theme.get(T::ui_hover));
                    }
                    const auto& ref = canvas_.blocks().references()[ri];
                    ui_ctx_.draw().text(kid.deflate(gui::Insets::xy(float(px(8)), 0)),
                                        "\xe2\x97\x87 " + ref.name, st.font_size_small,
                                        theme.get(T::ui_text_dim), gui::TextAlign::Left);
                    if (kin.clicked) canvas_.select_reference(ri);
                    ui_ctx_.pop_id();
                }
            ui_ctx_.pop_id();
            s.gap(float(px(4)));
        }
        dock_.end_content(ui_ctx_, "layers");
    }


    draw_sketches_panel();
    draw_objects_panel();
    draw_drawing_panel();
    draw_settings_panel();
    draw_properties_panel();
    draw_assistant_panel();
    draw_project_ask();

    ui_ctx_.draw().push_clip(canvas_rect_);
    for (auto [a,b]:modeller_.edge_highlight()) {
        if(!camera_.clip_segment(a,b)) continue;
        double ax,ay,bx,by;
        if (!camera_.project(a,ax,ay) || !camera_.project(b,bx,by)) continue;
        const gui::Vec2 start{float(ax),float(ay)},end{float(bx),float(by)};
        ui_ctx_.draw().line(start,end,float(px(8)),{0.15f,0.55f,1.0f,0.30f});
        ui_ctx_.draw().line(start,end,float(px(2)),{0.15f,0.55f,1.0f,0.65f});
    }
    draw_pull_arrow(canvas_rect_);
    draw_edge_value(canvas_rect_);
    draw_dynamic_input(canvas_rect_);
    draw_text_entry();
    draw_view_cube(canvas_rect_);
    draw_unit_chip(canvas_rect_);
    draw_placement_chip(canvas_rect_);
    ui_ctx_.draw().pop_clip();
    if (log_.size() > 1000) log_.erase(log_.begin(), log_.end() - 1000);
    command_history_rect_ = {};
    if (!log_.empty() && bar_.input.empty()) {
        const float keyword_height=bar_.keywords.empty() ? 0.0f : ui_ctx_.style().control_height+4.0f;
        const float lh = float(px(18));
        const float toggle_size=float(px(tablet_mode_?44:28));
        const float left=std::max(screen.x,canvas_rect_.x)+pad;
        const gui::Rect toggle{left,cmd_row.y-keyword_height-toggle_size-float(px(8)),toggle_size,toggle_size};
        {
            const char* label=history_hidden_?"Show command history":"Hide command history";
            const auto id=ui_ctx_.id(label);
            const auto hit=ui_ctx_.interact(id,toggle);
            ui_ctx_.announce(id,gui::Context::Role::Button,label,history_hidden_?"collapsed":"expanded",toggle);
            auto fill=theme.get(hit.hovered||hit.held?T::ui_hover:T::ui_surface);fill.a=1;
            ui_ctx_.draw().round_rect(toggle,float(px(6)),fill);
            ui_ctx_.draw().border(toggle,float(px(6)),1.0f,theme.get(T::ui_border));
            gui::draw_icon(ui_ctx_,history_hidden_?gui::Icon::ChevronUp:gui::Icon::ChevronDown,toggle,theme.get(T::ui_text),1.2f);
            if(hit.clicked) set_command_history_hidden(!history_hidden_);
        }
        const float log_x=toggle.right()+float(px(10));
        const float log_width=std::max(float(px(80)),std::min(float(px(760)),canvas_rect_.right()-pad-log_x));
        if (history_source_ != log_ || history_width_ != log_width ||
            history_font_size_ != ui_ctx_.style().font_size) {
            const int old_rows = int(history_rows_.size());
            history_source_ = log_; history_width_ = log_width;
            history_font_size_ = ui_ctx_.style().font_size;
            history_rows_.clear();
            for (const auto& entry:log_) {
                std::string_view remaining=entry;
                while (!remaining.empty()) {
                    std::size_t count=ui_ctx_.fit(remaining,history_font_size_,log_width);
                    if (count==0) count=1;
                    if (count<remaining.size()) {
                        const auto space=remaining.substr(0,count).find_last_of(' ');
                        if (space!=std::string_view::npos && space>0) count=space;
                    }
                    history_rows_.emplace_back(remaining.substr(0,count));remaining.remove_prefix(count);
                    while (!remaining.empty() && remaining.front()==' ') remaining.remove_prefix(1);
                }
            }
            if (history_scroll_ > 0) history_scroll_ += int(history_rows_.size()) - old_rows;
        }
        if (!history_hidden_) {
            const std::size_t max_rows=std::max<std::size_t>(1,std::size_t(canvas_rect_.h*0.3f/lh));
            const std::size_t visible=std::min(max_rows,history_rows_.size());
            const float log_h=lh*visible;
            gui::Rect log_area{log_x,toggle.bottom()-log_h,log_width,log_h};
            const gui::Rect back=log_area.deflate(gui::Insets::xy(-float(px(10)), -float(px(6))));
            command_history_rect_=back;
            const bool hovered=back.contains(input_.mouse) && !ui_ctx_.covered_by_overlay(input_.mouse,0);
            if (hovered && input_.wheel) history_scroll_ += int(std::round(input_.wheel*3));
            history_scroll_=std::clamp(history_scroll_,0,int(history_rows_.size()-visible));
            ui_ctx_.push_id("command_history");
            if (hovered) {
                auto card=theme.get(T::ui_surface);card.a=1;
                ui_ctx_.draw().round_rect(back,float(px(8)),card);
                ui_ctx_.draw().border(back,float(px(8)),1.0f,theme.get(T::ui_border));
            }
            const std::size_t first=history_rows_.size()-visible-history_scroll_;
            for (std::size_t i=0;i<visible;++i) {
                gui::Rect row=log_area;row.y+=lh*i;row.h=lh;
                gui::label(ui_ctx_,row,history_rows_[first+i],gui::TextAlign::Left,true);
                ui_ctx_.announce(ui_ctx_.id(std::to_string(i)),gui::Context::Role::Label,history_rows_[first+i],"",row);
            }
            ui_ctx_.pop_id();
        }
    }

    draw_osnap_ring(canvas_rect_);
    draw_context_menu(canvas_rect_);
    draw_drafting_features(canvas_rect_);
    draw_layer_color_picker();
    draw_text_style_colors();
    }

    ui_ctx_.end();
    ui_wants_mouse_ = ui_ctx_.wants_mouse();
    frame_mouse_ = input_.mouse;
    note_edits();

    input_.pressed = false;
    input_.released = false;
    input_.right_pressed = false;
    input_.typed.clear();
    input_.replacement_id=0; input_.replacement_text.clear();
    input_.wheel = 0.0f;
    input_.key_enter = input_.key_escape = input_.key_backspace = input_.key_tab = false;
    input_.key_delete = false;
    input_.key_left = input_.key_right = input_.key_up = input_.key_down = false;
    input_.key_home = input_.key_end = false;
}


namespace {
std::string object_action_title(const Canvas& canvas, const SelectionSummary& selection) {
    if(selection.block_refs>0 && selection.objects==selection.block_refs && !selection.solid_items &&
       std::all_of(canvas.selected_references().begin(),canvas.selected_references().end(),[&](auto i) {
           return i<canvas.blocks().references().size() && Canvas::is_wall_section(canvas.blocks().references()[i]);
       }))return selection.block_refs==1?"Wall section":std::to_string(selection.block_refs)+" wall sections";
    if((selection.objects>0 && selection.solid_items>0) ||
       (selection.solid_items>1 && selection.solid_items!=selection.edges &&
        selection.solid_items!=selection.faces &&
        (selection.edges>0 || selection.faces>0 || selection.region)))
        return std::to_string(selection.objects+selection.solid_items)+" selected objects";
    if (selection.shapes && !selection.texts && !selection.dimensions && !selection.hatches && !selection.block_refs) {
        std::string type;
        bool shared = true;
        for (auto i : canvas.selection()) {
            if (i >= canvas.shapes().size()) continue;
            const auto found = canvas.shapes()[i].attrs.user.find("sketch_object");
            if (found == canvas.shapes()[i].attrs.user.end()) { shared = false; break; }
            if (type.empty()) type = found->second;
            else if (type != found->second) { shared = false; break; }
        }
        if (shared && !type.empty()) { type.front() = char(std::toupper(static_cast<unsigned char>(type.front()))); return type + " geometry"; }
    }
    if(selection.objects>0 && selection.objects!=selection.shapes)
        return std::to_string(selection.objects)+(selection.objects==1 ? " selected object" : " selected objects");
    if (selection.shapes) {
        return std::to_string(selection.shapes) + (selection.shapes == 1 ? " sketch edge" : " sketch edges");
    }
    if(selection.edges) return std::to_string(selection.edges)+(selection.edges==1 ? " solid edge" : " solid edges");
    if(selection.faces) return std::to_string(selection.faces)+(selection.faces==1 ? " solid face" : " solid faces");
    if (selection.bodies) return std::to_string(selection.bodies) + (selection.bodies == 1 ? " solid" : " solids");
    if (selection.edges) return "Solid edge";
    if (selection.faces) return "Solid face";
    if (selection.region) return "Closed area";
    if (selection.block_refs) return "Block selection";
    return "Drawing actions";
}
bool object_action_row(gui::Context& ui, const gui::Rect& row, std::string_view label,
                       gcm::u32 icon, bool destructive, float scale) {
    const auto id = ui.id(label);
    const auto in = ui.interact(id, row);
    ui.register_focusable(id, row, label);
    ui.announce(id, gui::Context::Role::Button, label, "", row);
    if (in.hovered) ui.draw().round_rect(row, 4 * scale, ui.theme().get(T::ui_hover));
    if (ui.is_focused(id)) ui.draw().border(row.deflate(gui::Insets::all(scale)), 4 * scale, 2 * scale, ui.theme().get(T::ui_accent));
    const auto color = ui.theme().get(destructive ? T::status_error : T::ui_text);
    const float slot = icon ? 24 * scale : 0;
    if (icon) {
        std::string glyph;
        glyph.push_back(static_cast<char>(0xE0 | (icon >> 12)));
        glyph.push_back(static_cast<char>(0x80 | ((icon >> 6) & 0x3F)));
        glyph.push_back(static_cast<char>(0x80 | (icon & 0x3F)));
        ui.draw().text({row.x + 8 * scale, row.y, slot, row.h}, glyph, 18 * scale, color, gui::TextAlign::Centre);
    }
    gui::Rect text{row.x + 12 * scale + slot, row.y, row.w - 24 * scale - slot, row.h};
    ui.draw().text(text, std::string(label), ui.style().font_size, color, gui::TextAlign::Left);
    return in.clicked || ui.activated(id);
}
gcm::u32 context_action_icon(std::string_view command) {
    using gui::Icon;
    static const std::pair<const char*, Icon> icons[] = {{"MOVE",Icon::Move},{"COPY",Icon::Copy},
        {"ROTATE",Icon::Rotate},{"MIRROR",Icon::Mirror},{"SCALE",Icon::Scale},{"ERASE",Icon::Erase},
        {"EXTRUDE",Icon::Extrude},{"OFFSET",Icon::Offset},{"HATCH",Icon::Hatch},{"BACKGROUND",Icon::Hatch},
        {"GROUPSEL",Icon::Layers},{"EXPLODE",Icon::Explode},{"UNION",Icon::Union},{"SUBTRACT",Icon::Subtract},
        {"INTERSECT",Icon::Intersect},{"DIST",Icon::Measure},{"MATE",Icon::Move},{"DOORFLOW",Icon::Rotate},
        {"TEXTSMALLER",Icon::Text},{"TEXTBIGGER",Icon::Text},{"REVOLVE",Icon::Revolve},
        {"SKETCHONFACE",Icon::NewSketch},{"EDITSKETCH",Icon::NewSketch},{"MASSPROP",Icon::Measure},
        {"MIRROR3D",Icon::Mirror},{"FILLETEDGE",Icon::Fillet},{"CHAMFEREDGE",Icon::Chamfer}};
    for (const auto& [name, icon] : icons) if (command == name) return gui::icon_codepoint(icon);
    return 0;
}
}

SelectionSummary Shell::selection_summary() const {
    SelectionSummary sum;
    sum.objects=int(canvas_.selection_count());
    sum.texts=int(canvas_.selected_texts().size());
    sum.dimensions=int(canvas_.selected_dimensions().size());
    sum.sketch_mode=mode_==Mode::Sketch;
    sum.planar_face=sel_target_.valid && sel_target_.on_body && sel_target_.source<0 && sel_target_.surface_faces.empty();
    sum.can_revolve=sel_target_.valid && (!sel_target_.on_body || sel_target_.source>=0);
    sum.editable_sketch=sel_target_.valid && sel_target_.source>=0 && sel_target_.source!=active_sketch_;
    sum.hatches=int(canvas_.selected_hatches().size());
    sum.door = canvas_.has_selected_door();
    sum.shapes = static_cast<int>(canvas_.selection().size());
    sum.block_refs = static_cast<int>(canvas_.selected_references().size());
    sum.edges = modeller_.edge_kept() ? int(std::max<std::size_t>(1,modeller_.selected_edges().size())) : 0;
    std::vector<std::size_t> owners=selected_bodies_;
    if(sel_body_>=0) owners.push_back(std::size_t(sel_body_));
    const auto add=[&](const Modeller::Target& target) {
        if(!target.valid) return;
        if(target.on_body && target.body<modeller_.bodies().size()) {++sum.faces;owners.push_back(target.body);}
        else if(!target.on_body) sum.region=true;
    };
    if(selected_targets_.empty()) add(sel_target_);
    else for(const auto& target:selected_targets_) add(target);
    for(const auto& edge:modeller_.selected_edges()) owners.push_back(edge.body);
    std::sort(owners.begin(),owners.end());
    owners.erase(std::unique(owners.begin(),owners.end()),owners.end());
    sum.bodies=int(owners.size());
    sum.solid_items=int(selected_targets_.size()+selected_bodies_.size())+sum.edges;
    if(selected_targets_.empty() && sel_target_.valid) ++sum.solid_items;
    if(selected_bodies_.empty() && sel_body_>=0) ++sum.solid_items;
    sum.two_bodies = sum.bodies >= 2 ||
                     (sum.bodies >= 1 && modeller_.bodies().size() >= 2);
    return sum;
}

void Shell::draw_osnap_ring(const gui::Rect& canvas) {
    const bool placing = canvas_.tool() != Tool::Select || pick_.wanted > 0;

    bool summon = false;
    if (placing && canvas.contains(input_.mouse)) {
        if (input_.right_pressed && input_.shift) {
            summon = true;
            input_.right_pressed = false;
        } else if (tablet_mode_ && gui::arm_long_press(osnap_ring_, input_)) {
            summon = true;
        }
    }

    if (summon) {
        osnap_ring_items_.clear();
        for (const char* name : canvas_.osnap_ring(input_.mouse.x, input_.mouse.y))
            osnap_ring_items_.push_back(name);
        if (osnap_ring_items_.empty()) {
            log_line("no snaps under the cursor");
        } else {
            osnap_ring_.open = true;
            osnap_ring_.centre = input_.mouse;
        }
    }
    if (!osnap_ring_.open) return;

    std::vector<gui::RingItem> items;
    items.reserve(osnap_ring_items_.size());
    for (std::size_t i = 0; i < osnap_ring_items_.size(); ++i)
        items.push_back({0, osnap_ring_items_[i], true, gcm::u32(i)});

    const gcm::i32 chosen = gui::ring(ui_ctx_, osnap_ring_, items);
    if (chosen >= 0 && std::size_t(chosen) < osnap_ring_items_.size()) {
        if (canvas_.force_snap(osnap_ring_items_[std::size_t(chosen)]))
            log_line(canvas_.message());
        osnap_ring_.open = false;
    }
    if (input_.key_escape) osnap_ring_.open = false;
}

void Shell::select_context_target(double x,double y) {
    sync_view_from_camera();
    if(mode_==Mode::Sketch)canvas_.adopt_wall_sections();
    double sx=0,sy=0;
    const bool drawing_hit=mode_!=Mode::Solid && to_plane(x,y,sx,sy) &&
        (canvas_.pick(sx,sy)!=Canvas::npos || canvas_.pick_text(sx,sy)!=Canvas::npos ||
         canvas_.pick_dim(sx,sy)!=Canvas::npos || canvas_.pick_reference(sx,sy)!=Canvas::npos32 ||
         canvas_.pick_hatch(sx,sy)!=Canvas::npos);
    if(drawing_hit) {
        const bool preserve=canvas_.context_selected_at(sx,sy);
        canvas_.select_context_at(sx,sy);
        if(!preserve) {modeller_.arm(Modeller::Op::None);clear_solid_selection();}
        return;
    }
    if(mode_==Mode::Drawing)return;
    Modeller::Target hit;
    const bool target_hit=modeller_.target_at(camera_,x,y,hit,mode_==Mode::Solid);
    if(target_hit) {
        const auto same=[&](const Modeller::Target& target) {
            if(!target.valid || target.on_body!=hit.on_body || target.source!=hit.source)return false;
            if(hit.on_body)return target.body==hit.body && (target.face==hit.face ||
                std::find(target.surface_faces.begin(),target.surface_faces.end(),hit.face)!=target.surface_faces.end());
            return target.profile.outer.size()==hit.profile.outer.size() &&
                std::equal(target.profile.outer.begin(),target.profile.outer.end(),hit.profile.outer.begin(),
                    [](const auto& a,const auto& b){return a.x==b.x && a.y==b.y;});
        };
        if((hit.on_body && (sel_body_==int(hit.body) ||
            std::find(selected_bodies_.begin(),selected_bodies_.end(),hit.body)!=selected_bodies_.end())) ||
           same(sel_target_) || std::any_of(selected_targets_.begin(),selected_targets_.end(),same))return;
    }
    bool preserve=false;
    if(modeller_.select_context_edge_at(camera_,x,y,preserve)) {
        if(!preserve) {clear_solid_selection();canvas_.clear_selection();}
        mesh_dirty_=true;return;
    }
    if(target_hit) {
        modeller_.arm(Modeller::Op::None);
        select_target(hit);
    }
}

void Shell::execute_selection_action(const ActionDef& action) {
    if(action.handler!=SelectionAction::Command && action.handler!=SelectionAction::Volume && !require_edit_access())return;
    switch(action.handler) {
    case SelectionAction::Command: run_command(action.command);break;
    case SelectionAction::TextSmaller:
    case SelectionAction::TextBigger:
        canvas_.scale_selected_text(action.handler==SelectionAction::TextSmaller?1/1.25:1.25);
        log_line(canvas_.message());break;
    case SelectionAction::Extrude:
        if(modeller_.stage()!=Modeller::Stage::Placed)modeller_.place(Modeller::Op::Extrude,sel_target_);
        log_line(modeller_.prompt());break;
    case SelectionAction::Revolve: {
        const auto target=sel_target_;clear_solid_selection();
        modeller_.place(Modeller::Op::Revolve,target);active_tool_id_=ToolId::Revolve;
        log_line(modeller_.prompt());break;
    }
    case SelectionAction::SketchOnFace:
        begin_sketch_on_face(sel_target_.body,sel_target_.face);clear_solid_selection();break;
    case SelectionAction::EditSketch:
        edit_sketch(sel_target_.source);clear_solid_selection();break;
    case SelectionAction::Volume:
        log_line(modeller_.describe(sel_target_.valid && sel_target_.on_body?sel_target_.body:std::size_t(sel_body_)));break;
    case SelectionAction::MirrorBody:
        modeller_.preselect_body(Modeller::Op::MirrorBody,std::size_t(sel_body_));
        clear_solid_selection();active_tool_id_=ToolId::MirrorBody;log_line(modeller_.prompt());break;
    case SelectionAction::HatchRegion: {
        double sx,sy;canvas_.view().screen_of(sel_target_.centroid.x.to_f64(),sel_target_.centroid.y.to_f64(),sx,sy);
        canvas_.hatch_at(sx,sy,canvas_.hatch_style().name);log_line(canvas_.message());
        clear_solid_selection();mesh_dirty_=true;break;
    }
    }
}

void Shell::draw_context_menu(const gui::Rect& canvas) {
    if(!ctx_menu_open_ && !input_.right_pressed)return;
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();

    if(input_.right_pressed && canvas.contains(input_.mouse) && vars_.get_int("SHORTCUTMENU",11)!=0)
        select_context_target(input_.mouse.x,input_.mouse.y);
    const SelectionSummary sum=selection_summary();
    const auto acts=SelectionActionFactory::build(sum);
    const auto destructive=[](const ActionDef* action) { return std::string_view(action->command)=="ERASE"; };

    if (input_.right_pressed && vars_.get_int("SHORTCUTMENU",11)!=0 && canvas.contains(input_.mouse) && !acts.empty()) {
        ctx_menu_open_ = true;
        ctx_menu_at_ = input_.mouse;
        ctx_menu_scroll_ = 0;
        input_.right_pressed = false;
    }
    if (!ctx_menu_open_) return;
    if (acts.empty() || input_.key_escape) { ctx_menu_open_ = false; return; }

    const float row_h = float(px(34)), pad = float(px(8)), heading_h = float(px(46));
    const gui::Rect bounds{pad,pad,std::max(1.f,float(vw_)-2*pad),std::max(1.f,float(vh_)-2*pad)};
    const std::string title = object_action_title(canvas_, sum);
    float w = std::max(float(px(252)), ui_ctx_.measure(title, st.font_size) + float(px(48)));
    for (const auto* a : acts)
        w = std::max(w, ui_ctx_.measure(a->label, st.font_size) + float(px(62)));
    w = std::min(w, bounds.w);
    const bool erase = !acts.empty() && destructive(acts.back());
    const float content_height = row_h * float(acts.size()) + (erase ? float(px(9)) : 0);
    const float h = std::min(heading_h + content_height + pad, bounds.h);
    gui::Rect list{ctx_menu_at_.x, ctx_menu_at_.y, w, h};
    list.x = std::clamp(list.x, bounds.x, std::max(bounds.x, bounds.right() - list.w));
    list.y = std::clamp(list.y, bounds.y, std::max(bounds.y, bounds.bottom() - list.h));
    ctx_menu_bounds_ = list;
    gui::Rect body{list.x + pad, list.y + heading_h, list.w - 2 * pad, list.h - heading_h - pad};
    if (list.contains(input_.mouse)) ctx_menu_scroll_ -= input_.wheel * row_h;
    ctx_menu_scroll_ = std::clamp(ctx_menu_scroll_, 0.f, std::max(0.f, content_height - body.h));

    ui_ctx_.begin_overlay(list);
    ui_ctx_.push_id("context-actions");
    ui_ctx_.interact(ui_ctx_.id("surface"), list);
    if (content_height > body.h && body.h > 0) {
        const gui::Rect track{list.right()-float(px(16)),body.y,float(px(12)),body.h};
        const float thumb_h=std::min(body.h,std::max(float(px(20)),body.h*body.h/content_height));
        const auto id=ui_ctx_.id("scroll");
        const auto state=ui_ctx_.interact(id,track);
        const float travel=body.h-thumb_h, range=content_height-body.h;
        auto& grab=ui_ctx_.state_f32(id,0);
        const float top=track.y+(travel>0?travel*ctx_menu_scroll_/range:0);
        if(state.held && input_.pressed)grab=std::clamp(input_.mouse.y-top,0.f,thumb_h);
        if(state.held && travel>0)ctx_menu_scroll_=std::clamp((input_.mouse.y-track.y-grab)/travel*range,0.f,range);
        body.w-=float(px(12));
    }
    ui_ctx_.draw().shadow(list, float(px(6)), float(px(10)), theme.get(T::ui_shadow));
    auto surface = theme.get(T::ui_surface_raised); surface.a = 1;
    ui_ctx_.draw().round_rect(list, float(px(6)), surface);
    ui_ctx_.draw().border(list, float(px(6)), 1.0f, theme.get(T::ui_border));
    gui::Rect heading{list.x + float(px(14)), list.y + float(px(8)), list.w - float(px(28)), float(px(30))};
    ui_ctx_.draw().text(heading, title, st.font_size, theme.get(T::ui_text), gui::TextAlign::Left);
    ui_ctx_.announce(ui_ctx_.id("menu"), gui::Context::Role::Panel, "Selection actions", title, list);
    ui_ctx_.draw().line({list.x + pad, body.y - float(px(5))}, {list.right() - pad, body.y - float(px(5))}, 1, theme.get(T::ui_border));
    ui_ctx_.draw().push_clip(body);
    for (std::size_t i = 0; i < acts.size(); ++i) {
        const bool danger = destructive(acts[i]);
        gui::Rect row{body.x, body.y + row_h * float(i) - ctx_menu_scroll_ + (danger ? float(px(9)) : 0), body.w, row_h};
        if (row.bottom() <= body.y || row.y >= body.bottom()) continue;
        if (danger) ui_ctx_.draw().line({row.x, row.y - float(px(5))}, {row.right(), row.y - float(px(5))}, 1, theme.get(T::ui_border));
        if (object_action_row(ui_ctx_, row.intersect(body), acts[i]->label, context_action_icon(acts[i]->command), danger, float(px(1)))) {
            ctx_menu_open_ = false;
            if(std::string_view(acts[i]->command)=="BACKGROUND") {
                double sx,sy,x,y;
                if(to_plane(ctx_menu_at_.x,ctx_menu_at_.y,sx,sy)) {
                    canvas_.view().world_of(sx,sy,x,y);
                    background_seed_=gcm::to_lattice(vol_,x,y); background_context_seed_=true;
                    canvas_.clear_selection();
                }
            }
            ui_ctx_.draw().pop_clip();
            ui_ctx_.pop_id();
            ui_ctx_.end_overlay();
            execute_selection_action(*acts[i]);
            return;
        }
    }
    ui_ctx_.draw().pop_clip();
    if (content_height > body.h && body.h > 0) {
        const float thumb_h = std::min(body.h,std::max(float(px(20)), body.h * body.h / content_height));
        const float y = body.y + (body.h - thumb_h) * ctx_menu_scroll_ / (content_height - body.h);
        ui_ctx_.draw().round_rect({list.right() - float(px(5)), y, float(px(3)), thumb_h}, float(px(1.5)), theme.get(T::ui_text_dim));
    }
    if (input_.pressed && !list.contains(input_.mouse)) ctx_menu_open_ = false;
    ui_ctx_.pop_id();
    ui_ctx_.end_overlay();
}

void Shell::draw_unit_chip(const gui::Rect& canvas) {
    static const gunit::Length cycle[] = {gunit::Length::Millimetre, gunit::Length::Centimetre,
                                          gunit::Length::Metre, gunit::Length::Inch,
                                          gunit::Length::Foot};
    static const std::vector<std::string> names = {"mm", "cm", "m", "in", "ft"};
    gcm::i32 sel = 0;
    for (gcm::i32 i = 0; i < 5; ++i)
        if (cycle[i] == canvas_.display_unit()) sel = i;
    gui::Rect chip{canvas.x + float(px(12)), canvas.y + float(px(12)), float(px(64)), float(px(30))};
    ui_ctx_.push_id("unit-chip");
    if (gui::dropdown(ui_ctx_, chip, "unit", names, sel) >= 0)
        adopt_display_unit(cycle[std::clamp(sel, 0, 4)]);
    ui_ctx_.pop_id();
}

void Shell::draw_view_cube(const gui::Rect&) {
    const auto& theme = themes_.active();
    const auto layout = cube_layout();
    const float cx = float(layout.x + layout.size * 0.5);
    const float cy = float(layout.y + layout.size * 0.5);
    const float half = float(layout.size) * 0.27f;

    const gview::Vec3 R = camera_.right(), U = camera_.up(), F = camera_.forward();
    const auto proj = [&](double x, double y, double z) {
        const double sx = x * R.x.to_f64() + y * R.y.to_f64() + z * R.z.to_f64();
        const double sy = x * U.x.to_f64() + y * U.y.to_f64() + z * U.z.to_f64();
        return gui::Vec2{cx + float(sx) * half, cy - float(sy) * half};
    };
    const auto hovered = gview::ViewCube::pick(camera_, layout, input_.mouse.x, input_.mouse.y);

    struct Face { int nx, ny, nz; const char* label; };
    static const Face faces[] = {
        {1, 0, 0, "RIGHT"}, {-1, 0, 0, "LEFT"}, {0, 1, 0, "BACK"},
        {0, -1, 0, "FRONT"}, {0, 0, 1, "TOP"}, {0, 0, -1, "BOTTOM"}};

    std::vector<std::pair<double, int>> order;
    for (int i = 0; i < 6; ++i) {
        const double facing = -(faces[i].nx * F.x.to_f64() + faces[i].ny * F.y.to_f64() +
                                faces[i].nz * F.z.to_f64());
        if (facing > 0.02) order.push_back({facing, i});
    }
    std::sort(order.begin(), order.end());

    {
        std::vector<gui::Vec2> pts;
        for (const auto& [facing, i] : order) {
            const auto& f = faces[i];
            const double n[3] = {double(f.nx), double(f.ny), double(f.nz)};
            double u[3], v[3];
            if (f.nz != 0) { u[0] = 1; u[1] = 0; u[2] = 0; v[0] = 0; v[1] = 1; v[2] = 0; }
            else if (f.nx != 0) { u[0] = 0; u[1] = 1; u[2] = 0; v[0] = 0; v[1] = 0; v[2] = 1; }
            else { u[0] = 1; u[1] = 0; u[2] = 0; v[0] = 0; v[1] = 0; v[2] = 1; }
            for (auto [su, sv] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{1, 1}, std::pair{-1, 1}})
                pts.push_back(proj(n[0] + su * u[0] + sv * v[0], n[1] + su * u[1] + sv * v[1],
                                   n[2] + su * u[2] + sv * v[2]));
        }
        std::sort(pts.begin(), pts.end(), [](const gui::Vec2& a, const gui::Vec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        const auto cross = [](const gui::Vec2& o, const gui::Vec2& a, const gui::Vec2& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<gui::Vec2> hull;
        for (const auto& p : pts) {
            while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0) hull.pop_back();
            hull.push_back(p);
        }
        const std::size_t lower = hull.size() + 1;
        for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
            while (hull.size() >= lower && cross(hull[hull.size() - 2], hull.back(), *it) <= 0) hull.pop_back();
            hull.push_back(*it);
        }
        if (!hull.empty()) hull.pop_back();
        if (hull.size() >= 3) {
            gui::Color shade = theme.get(T::ui_shadow);
            const gui::Vec2 centre{cx, cy};
            for (int layer = 3; layer >= 1; --layer) {
                const float grow = 1.0f + 0.06f * layer;
                const float dy = float(px(3 + 2 * layer));
                std::vector<gui::Vec2> poly;
                for (const auto& p : hull)
                    poly.push_back({centre.x + (p.x - centre.x) * grow, centre.y + (p.y - centre.y) * grow + dy});
                gui::Color c = shade;
                c.a = 0.10f;
                ui_ctx_.draw().polygon(poly, c);
            }
        }
    }

    const gui::Color ink = theme.get(T::ui_text);
    const gui::Color border = theme.get(T::ui_border);
    const gui::Color accent = theme.get(T::ui_accent);
    const gui::Color surface = theme.get(T::ui_surface_raised).mix(ink, 0.07f);

    for (const auto& [facing, i] : order) {
        const Face& f = faces[i];
        const double n[3] = {double(f.nx), double(f.ny), double(f.nz)};
        double u[3], v[3];
        if (f.nz != 0) { u[0] = 1; u[1] = 0; u[2] = 0; v[0] = 0; v[1] = 1; v[2] = 0; }
        else if (f.nx != 0) { u[0] = 0; u[1] = 1; u[2] = 0; v[0] = 0; v[1] = 0; v[2] = 1; }
        else { u[0] = 1; u[1] = 0; u[2] = 0; v[0] = 0; v[1] = 0; v[2] = 1; }
        std::vector<gui::Vec2> quad;
        for (auto [su, sv] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{1, 1}, std::pair{-1, 1}})
            quad.push_back(proj(n[0] + su * u[0] + sv * v[0], n[1] + su * u[1] + sv * v[1],
                                n[2] + su * u[2] + sv * v[2]));

        const bool hot = hovered.valid() && hovered.region == gview::CubeRegion::Face &&
                         hovered.nx == f.nx && hovered.ny == f.ny && hovered.nz == f.nz;
        gui::Color fill = hot ? accent : surface.mix(ink, float(0.18 * (1.0 - facing)));
        fill.a = 1.0f;
        ui_ctx_.draw().polygon(quad, fill);
        for (std::size_t k = 0; k < 4; ++k)
            ui_ctx_.draw().line(quad[k], quad[(k + 1) % 4], 1.0f, border);

        if (facing > 0.45) {
            const gui::Vec2 c = proj(n[0], n[1], n[2]);
            const float w = float(px(44)), h = float(px(16));
            ui_ctx_.draw().text({c.x - w * 0.5f, c.y - h * 0.5f, w, h}, f.label,
                                float(px(9)) * float(std::min(1.0, 0.55 + facing * 0.5)),
                                hot ? theme.get(T::ui_accent_text) : ink, gui::TextAlign::Centre);
        }
    }

    if (hovered.valid() && hovered.region != gview::CubeRegion::Face) {
        double hx, hy;
        if (gview::ViewCube::part_screen_position(camera_, layout, hovered, hx, hy)) {
            const float r = float(px(hovered.region == gview::CubeRegion::Edge ? 5 : 6));
            ui_ctx_.draw().round_rect({float(hx) - r, float(hy) - r, r * 2, r * 2}, r, accent);
        }
    }

    gui::Rect caption{float(layout.x), float(layout.y + layout.size) - float(px(2)),
                      float(layout.size), float(px(16))};
    gui::label(ui_ctx_, caption, gview::view_name(camera_.nearest_standard()),
               gui::TextAlign::Centre, true);
}

void Shell::after_commit() {
    last_click_time_=-1;
    hover_target_=Modeller::Target{};
    log_.push_back(modeller_.message());
    modeller_.take_committed();
    if (modeller_.edge_kept() && modeller_.edge_applied()) {
        last_edit_was_solid_ = true;
        mesh_dirty_ = true;
        return;
    }

    if (const Modeller::SectionOut sec = modeller_.take_section(); sec.valid) {
        journal_sketches();
        SketchDoc sk;
        sk.name = "Section " + std::to_string(sketches_.size() + 1);
        sk.layer = layers_.current();
        sk.plane = gtopo::SketchPlane::from_normal(sec.ox, sec.oy, sec.oz, sec.nx, sec.ny, sec.nz);
        for (std::size_t i = 0; i + 5 < sec.segments.size(); i += 6) {
            double ax, ay, bx, by;
            sk.plane.to_plane(sec.segments[i], sec.segments[i + 1], sec.segments[i + 2], ax, ay);
            sk.plane.to_plane(sec.segments[i + 3], sec.segments[i + 4], sec.segments[i + 5], bx, by);
            gsnap::Shape sh;
            sh.kind = gsnap::ShapeKind::Segment;
            sh.a = gcm::to_lattice(vol_, ax, ay);
            sh.b = gcm::to_lattice(vol_, bx, by);
            sk.shapes.push_back(sh);
        }
        tessellate_sketch(sk);
        sketches_.push_back(std::move(sk));
        log_.push_back("section drawn as " + sketches_.back().name);
    }
    last_edit_was_solid_ = true;
    mesh_dirty_ = true;
    modeller_.arm(Modeller::Op::None);
    active_tool_id_ = ToolId::Select;
    clear_solid_selection();
    if (mode_ == Mode::Sketch) set_mode(Mode::Solid);
}

void Shell::clear_solid_selection() {
    selected_targets_.clear(); selected_bodies_.clear();
    sel_target_=Modeller::Target{}; sel_body_=-1;
    mesh_dirty_=true;
}

bool Shell::has_solid_selection() const {
    if(sel_body_>=0 || !selected_bodies_.empty()) return true;
    if(sel_target_.valid && sel_target_.on_body) return true;
    return modeller_.edge_kept() || std::any_of(selected_targets_.begin(),selected_targets_.end(),
        [](const auto& target) {return target.valid && target.on_body;});
}

void Shell::select_body(std::size_t body, bool additive) {
    if(body>=modeller_.bodies().size()) return;
    if(!additive) {clear_solid_selection();modeller_.arm(Modeller::Op::None);canvas_.clear_selection();}
    else if(modeller_.op()==Modeller::Op::Extrude) modeller_.arm(Modeller::Op::None);
    if(selected_bodies_.empty() && sel_body_>=0) selected_bodies_.push_back(std::size_t(sel_body_));
    const auto found=std::find(selected_bodies_.begin(),selected_bodies_.end(),body);
    if(found==selected_bodies_.end()) selected_bodies_.push_back(body);
    else selected_bodies_.erase(found);
    sel_body_=selected_bodies_.empty() ? -1 : int(selected_bodies_.back());
    mesh_dirty_=true;
}

bool Shell::erase_selected_solids() {
    if(!require_edit_access()) return false;
    auto bodies=selected_bodies_;
    if(sel_body_>=0) bodies.push_back(std::size_t(sel_body_));
    if(sel_target_.valid && sel_target_.on_body) bodies.push_back(sel_target_.body);
    for(const auto& target:selected_targets_) if(target.valid && target.on_body) bodies.push_back(target.body);
    for(const auto& edge:modeller_.selected_edges()) bodies.push_back(edge.body);
    if(!modeller_.erase_many(std::move(bodies))) return false;
    clear_solid_selection(); hover_target_={};
    last_edit_was_solid_=true;
    log_.push_back(modeller_.message());
    return true;
}

void Shell::select_target(const Modeller::Target& t) {
    if (mode_ == Mode::Drawing) return;
    selected_targets_.assign(1,t);
    selected_bodies_.clear();
    sel_target_ = t;
    mesh_dirty_=true;
    if(!t.surface_faces.empty()) modeller_.arm(Modeller::Op::None);
    sel_body_ = -1;
    canvas_.clear_selection();
    if (t.valid && t.surface_faces.empty() && (modeller_.op() == Modeller::Op::None || modeller_.op() == Modeller::Op::Extrude)) {
        modeller_.place(Modeller::Op::Extrude, t);
        mesh_dirty_ = true;
    }
}

bool Shell::round_edge(double r) {
    if (!require_edit_access()) return false;
    if (!modeller_.edge_kept()) { log_.push_back("hover an edge and click it first"); return false; }
    const bool ok = modeller_.apply_edge(Modeller::Op::FilletEdge, r);
    if (modeller_.take_committed()) after_commit(); else log_.push_back(modeller_.message());
    active_tool_id_ = ToolId::FilletEdge;
    mesh_dirty_ = true;
    return ok;
}

bool Shell::bevel_edge(double d) {
    if (!require_edit_access()) return false;
    if (!modeller_.edge_kept()) { log_.push_back("hover an edge and click it first"); return false; }
    const bool ok = modeller_.apply_edge(Modeller::Op::ChamferEdge, d);
    if (modeller_.take_committed()) after_commit(); else log_.push_back(modeller_.message());
    active_tool_id_ = ToolId::ChamferEdge;
    mesh_dirty_ = true;
    return ok;
}

void Shell::report_modeller_error() {
    const auto& error=modeller_.value_error();
    if (error==reported_value_error_) return;
    reported_value_error_=error;
    if (!error.empty()) {
        if (log_.empty() || log_.back()!=error) log_line(error);
        mesh_dirty_=true;
    }
}

void Shell::draw_operation_value(const gui::Rect& canvas,
                                  const std::string& label, const std::string& placeholder,
                                  bool edge_slider) {
    ui_ctx_.push_id("operation_value");
    const auto id=ui_ctx_.id(label);
    value_focus_order_.push_back(id);
    const float field_h=float(px(edge_slider && tablet_mode_ ? 44 : 32));
    const float slider_h=float(px(tablet_mode_ ? 44 : 28)),ends_h=float(px(18));
    const float width=float(px(edge_slider ? (tablet_mode_ ? 320 : 264) : 224));
    const float height=edge_slider ? float(px(22+10+6))+field_h+slider_h+ends_h : float(px(64));
    float left=INFINITY,top=INFINITY,right=-INFINITY,bottom=-INFINITY;
    const auto include=[&](const gview::Point3& point) {
        double x,y;
        if (!camera_.project(point,x,y)) return;
        left=std::min(left,float(x)); right=std::max(right,float(x));
        top=std::min(top,float(y)); bottom=std::max(bottom,float(y));
    };
    const auto& target=modeller_.target();
    const auto body=modeller_.edge_kept() ? modeller_.edge_body_index() : target.body;
    gview::Point3 edge_anchor;
    const bool at_edge=modeller_.edge_kept() && modeller_.edge_pick_point(edge_anchor);
    if(at_edge) include(edge_anchor);
    else if (target.on_body && body<modeller_.bodies().size()) {
        const auto bounds=modeller_.bodies()[body].bounds(vol_);
        for (double x:{bounds.lo.x.to_f64(),bounds.hi.x.to_f64()})
            for (double y:{bounds.lo.y.to_f64(),bounds.hi.y.to_f64()})
                for (double z:{bounds.lo.z.to_f64(),bounds.hi.z.to_f64()})
                    include(gview::Point3::from(x,y,z));
    } else for (const auto& loop:target.outline) for (const auto& point:loop) include(point);
    const float gap=float(px(16));
    float x=canvas.right()-width-gap,y=canvas.y+float(px(144));
    if (std::isfinite(left)) {
        y=at_edge ? top+gap : top;
        if (right+gap+width<=canvas.right()) x=right+gap;
        else if (left-gap-width>=canvas.x) x=left-gap-width;
        else if (bottom+gap+height<=canvas.bottom()) { x=left; y=bottom+gap; }
        else if (top-gap-height>=canvas.y) { x=left; y=top-gap-height; }
    }
    const gui::Rect card{std::clamp(x,canvas.x,std::max(canvas.x,canvas.right()-width)),
                     std::clamp(y,canvas.y,std::max(canvas.y,canvas.bottom()-height)),width,height};
    const auto& theme=themes_.active();
    ui_ctx_.announce(ui_ctx_.id("operation-card"),gui::Context::Role::Panel,"Operation value",label,card);
    ui_ctx_.draw().round_rect(card,float(px(6)),theme.get(T::ui_surface_raised));
    gui::Rect caption=card; caption.h=float(px(22));
    gui::label(ui_ctx_,caption,label,gui::TextAlign::Left,true);
    gui::Rect field{card.x+float(px(4)),caption.bottom(),edge_slider ? card.w-float(px(4+6+56+4)) : float(px(158)),field_h};
    std::string text=modeller_.typed();
    if (gui::text_field(ui_ctx_,field,label,text,placeholder)) {
        modeller_.set_typed(std::move(text));
        mesh_dirty_=true;
    }
    gui::Rect ok{field.right()+float(px(6)),field.y,float(px(edge_slider ? 56 : 48)),field.h};
    if (modeller_.value_preview_valid()) value_focus_order_.push_back(ui_ctx_.id("OK"));
    const bool enter=ui_ctx_.is_focused(id) && input_.key_enter;
    if (gui::button(ui_ctx_,ok,"OK",modeller_.value_preview_valid(),true) || enter) {
        input_.key_enter=false;
        if (modeller_.commit_typed()) {
            after_commit();
            ui_ctx_.clear_focus();
        }
        mesh_dirty_=true;
    }
    if (edge_slider) draw_edge_slider(card,field.bottom()+float(px(6)),slider_h);
    report_modeller_error();
    ui_ctx_.pop_id();
}

void Shell::draw_edge_slider(const gui::Rect& card, float top, float height) {
    const bool round=modeller_.op()!=Modeller::Op::ChamferEdge;
    const gui::Rect row{card.x+float(px(10)),top,card.w-float(px(20)),height};
    const gui::Rect ends{row.x,row.bottom(),row.w,float(px(16))};
    const double limit=modeller_.edge_limit();
    if (limit<0.0) {
        gui::label(ui_ctx_,row,"Measuring the largest size...",gui::TextAlign::Left,true);
        round_slider_drag_=false;
        return;
    }
    if (limit==0.0) {
        gui::label(ui_ctx_,row,round ? "This edge can't be rounded" : "This edge can't be bevelled",gui::TextAlign::Left,true);
        round_slider_drag_=false;
        return;
    }
    const gunit::Format fmt{canvas_.display_unit()};
    const auto text_of=[&](double v) {
        const double f=modeller_.floor_to_display(std::clamp(v,0.0,limit));
        return f>0.0 ? gunit::format_length(f,fmt) : std::string();
    };
    double current=round_slider_drag_ ? round_slider_value_ : 0.0;
    if (!round_slider_drag_) {
        const auto parsed=gunit::parse_length(modeller_.typed(),canvas_.display_unit());
        if (parsed && std::isfinite(*parsed) && *parsed>0) current=*parsed;
        else if (modeller_.edge_applied()) current=modeller_.edge_value();
    }
    float v=float(std::clamp(current,0.0,limit));
    const char* name=round ? "Round radius slider" : "Chamfer amount slider";
    if (gui::slider(ui_ctx_,row,name,v,0.0f,float(limit))) {
        round_slider_drag_=true;
        round_slider_value_=double(v);
        const double t=now_seconds();
        if (round_slider_last_preview_<0.0 || t-round_slider_last_preview_>=0.05) {
            round_slider_last_preview_=t;
            modeller_.set_typed(text_of(round_slider_value_));
            mesh_dirty_=true;
        }
    }
    if (round_slider_drag_ && !input_.down) {
        round_slider_drag_=false;
        round_slider_last_preview_=-1.0;
        const std::string text=text_of(round_slider_value_);
        modeller_.set_typed(text);
        if (!text.empty() && modeller_.commit_typed()) {
            after_commit();
            ui_ctx_.clear_focus();
        }
        mesh_dirty_=true;
    }
    gui::label(ui_ctx_,ends,"0",gui::TextAlign::Left,true);
    gui::label(ui_ctx_,ends,"max "+gunit::format_length(limit,fmt),gui::TextAlign::Right,true);
}

void Shell::draw_edge_value(const gui::Rect& canvas) {
    if (modeller_.stage()!=Modeller::Stage::TypeValue) return;
    std::string label,placeholder;
    if (modeller_.edge_kept()) {
        label=modeller_.op()==Modeller::Op::ChamferEdge ? "Chamfer amount" : "Round radius";
        placeholder=modeller_.edge_applied() ? gunit::format_length(modeller_.edge_value(),gunit::Format{canvas_.display_unit()}) : "Enter size";
    } else {
        if (!modeller_.target().valid) return;
        label=modeller_.op()==Modeller::Op::PatternBody ? "Count and spacing" : "Wall thickness";
        placeholder=modeller_.op()==Modeller::Op::PatternBody ? "4 2.5" : "Enter size";
    }
    draw_operation_value(canvas,label,placeholder,modeller_.edge_kept());
}

void Shell::draw_pull_arrow(const gui::Rect& canvas) {
    gview::Point3 base, tip;
    if (!modeller_.arrow_points(camera_, base, tip)) return;
    double bx, by, tx, ty;
    if (!camera_.project(base, bx, by) || !camera_.project(tip, tx, ty)) return;
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();

    const gview::Vec3 n = modeller_.target().normal;
    const double ax = std::fabs(n.x.to_f64()), ay = std::fabs(n.y.to_f64()), az = std::fabs(n.z.to_f64());
    gui::Color col = theme.get(T::ui_accent);
    if (ax > 0.98) col = gui::Color::rgb(0.93f, 0.33f, 0.31f);
    else if (ay > 0.98) col = gui::Color::rgb(0.45f, 0.80f, 0.35f);
    else if (az > 0.98) col = gui::Color::rgb(0.30f, 0.56f, 0.98f);
    const bool held = modeller_.stage() == Modeller::Stage::Dragging;
    gui::Color body = col;
    if (held) body = col.mix(gui::Color::rgb(1, 1, 1), 0.25f);

    const float dx = float(tx - bx), dy = float(ty - by);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) return;
    const float ux = dx / len, uy = dy / len;
    const float px_ = -uy, py_ = ux;
    const float shaft = float(px(3));
    const float head_l = float(px(16)), head_w = float(px(8));
    const float grip = float(px(5));
    const float sx0 = float(bx), sy0 = float(by);
    const float sx1 = float(tx) - ux * head_l, sy1 = float(ty) - uy * head_l;
    ui_ctx_.draw().polygon({{sx0 + px_ * shaft, sy0 + py_ * shaft}, {sx1 + px_ * shaft, sy1 + py_ * shaft},
                            {sx1 - px_ * shaft, sy1 - py_ * shaft}, {sx0 - px_ * shaft, sy0 - py_ * shaft}}, body);
    ui_ctx_.draw().triangle({float(tx), float(ty)}, {sx1 + px_ * head_w, sy1 + py_ * head_w},
                            {sx1 - px_ * head_w, sy1 - py_ * head_w}, body);
    ui_ctx_.draw().round_rect({sx0 - grip, sy0 - grip, grip * 2, grip * 2}, grip, body);
    ui_ctx_.draw().border({sx0 - grip, sy0 - grip, grip * 2, grip * 2}, grip, 1.0f, theme.get(T::ui_surface));

    draw_operation_value(canvas,
                         modeller_.target().on_body && modeller_.target().source<0 ? "Face offset" : "Extrusion distance",
                         gunit::format_length(modeller_.distance(),gunit::Format{canvas_.display_unit()}));
}

void Shell::set_mode(Mode m) {
    if (m == Mode::Solid && in_paper_space_) leave_paper_space();
    if (mode_ == m) return;
    if (m == Mode::Drawing) {
        layer_before_drawing_ = layers_.current();
        if (!layers_.find("Markup")) {
            gdoc::Layer mk;
            mk.name = "Markup";
            mk.color = gdoc::Color::rgb(214, 48, 38);
            layers_.add(mk);
        }
        layers_.set_current("Markup");
    } else if (mode_ == Mode::Drawing && !layer_before_drawing_.empty() &&
               layers_.find(layer_before_drawing_)) {
        layers_.set_current(layer_before_drawing_);
    }
    canvas_.set_current_layer(layers_.current());
    mode_ = m;
    modeller_.arm(Modeller::Op::None);
    canvas_.set_tool(Tool::Select);
    canvas_.clear_selection();
    clear_solid_selection();
    hover_target_ = Modeller::Target{};
    region_probe_pending_ = false;
    last_click_time_ = -1.0;
    picking_sketch_plane_ = false;
    active_tool_id_ = ToolId::Select;
    layout_drag_ = false;
}

void Shell::activate_tool(ToolId id) {
    if (!require_edit_access()) return;
    const ToolDef* d = tool_def(id);
    if (!d) return;
    if (!d->wired) {
        log_.push_back(std::string(d->label) + ": not connected yet");
        return;
    }
    picking_sketch_plane_ = false;
    if (!tool_in_mode(id, mode_) && id != ToolId::Select) set_mode(tool_mode(id));
    run_command(d->command);
    active_tool_id_ = id;
}

int Shell::sketch_parent(int index) const {
    if (index < 0 || std::size_t(index) >= sketches_.size()) return -1;
    for (std::size_t b=0; b<modeller_.bodies().size(); ++b)
        for (const auto& feature:modeller_.history(b))
            if (feature.sketch == index) return int(b);
    const auto key=sketches_[index].parent_body_key;
    if (key) for (std::size_t b=0; b<modeller_.bodies().size(); ++b)
        if (modeller_.body_name(b)==key) return int(b);
    return -1;
}

bool Shell::sketch_visible(int index) const {
    if (index < 0 || std::size_t(index) >= sketches_.size()) return false;
    if (index == active_sketch_ && mode_ == Mode::Sketch) return true;
    const auto& sketch=sketches_[index];
    if (sketch.visibility==SketchVisibility::Hidden || canvas_.layer_hidden(sketch.layer)) return false;
    const int parent=sketch_parent(index);
    if (parent>=0 && modeller_.body_hidden(std::size_t(parent))) return false;
    if (sketch.visibility==SketchVisibility::Shown) return true;
    return !modeller_.sketch_consumed(index);
}

void Shell::set_sketch_visibility(int index, SketchVisibility visibility) {
    if (index<0 || std::size_t(index)>=sketches_.size() || sketches_[index].visibility==visibility) return;
    journal_sketches();
    sketches_[index].visibility=visibility;
    touch_metadata("sketch visibility");
    hover_target_={}; clear_solid_selection();
    canvas_.clear_selection();
    mesh_dirty_=true;
    build_frame_sources();
    if (!sketch_visible(index) && (modeller_.target().source==index || modeller_.hovered().source==index)) {
        modeller_.arm(Modeller::Op::None);
        active_tool_id_=ToolId::Select;
    }
    flush_edits();
}

void Shell::store_active_sketch() {
    ensure_space_ids();
    space_states_[active_space_id()] = canvas_.capture_space();
    // In paper space the canvas holds the sheet; storing it would clobber the model.
    if (in_paper_space_) {
        if (Layout* l = active_layout()) {
            l->shapes = canvas_.shapes();
            l->notes = canvas_.texts();
        }
        return;
    }
    if (active_sketch_ < 0 || active_sketch_ >= static_cast<int>(sketches_.size())) return;
    SketchDoc& sk = sketches_[active_sketch_];
    sk.shapes = canvas_.shapes();
    sk.texts = canvas_.texts();
    tessellate_sketch(sk);
}

void Shell::tessellate_sketch(SketchDoc& sk) const {
    ++sk.segments_version;
    sk.segments.clear();
    for (const auto& sh : sk.shapes) {
        using K = gsnap::ShapeKind;
        if (sh.kind == K::Circle || sh.kind == K::Arc) {
            double cx, cy;
            gcm::to_world(vol_, sh.a, cx, cy);
            const double r = double(sh.radius) / vol_.scale();
            const double a0 = sh.kind == K::Arc ? sh.start_angle : 0.0;
            const double a1 = sh.kind == K::Arc ? sh.end_angle : 2.0 * 3.14159265358979323846;
            const int n = 48;
            for (int i = 0; i < n; ++i) {
                const double t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
                sk.segments.push_back({gcm::to_lattice(vol_, cx + r * std::cos(t0), cy + r * std::sin(t0)),
                                       gcm::to_lattice(vol_, cx + r * std::cos(t1), cy + r * std::sin(t1))});
            }
        } else if (sh.kind == K::Segment) {
            sk.segments.push_back({sh.a, sh.b});
        }
    }
}

void Shell::lift_sketch(const SketchDoc& sk, const float color[4]) {
    for (const auto& seg : sk.segments) {
        double ax, ay, bx, by, wx, wy, wz;
        gcm::to_world(vol_, seg.a, ax, ay);
        gcm::to_world(vol_, seg.b, bx, by);
        sk.plane.to_world(ax, ay, wx, wy, wz);
        lines_.push_back({{float(wx), float(wy), float(wz)}, {color[0], color[1], color[2], color[3]}});
        sk.plane.to_world(bx, by, wx, wy, wz);
        lines_.push_back({{float(wx), float(wy), float(wz)}, {color[0], color[1], color[2], color[3]}});
    }
    const auto state=space_states_.find(sk.space_id);
    const bool has_fill=(state!=space_states_.end()&&!state->second.content.hatches.empty()) || !sk.texts.empty();
    const bool has_flow=std::any_of(sk.shapes.begin(),sk.shapes.end(),[](const auto& shape){return shape.attrs.user.count("door_flow");});
    if(has_fill||has_flow) {
        Canvas source=canvas_for_space(sk.space_id);
        std::vector<std::string> hidden;
        std::vector<std::pair<std::string,grender::Color>> colors;
        for(const auto& layer:layers_.all()) {
            if(!layer.visible())hidden.push_back(layer.name);
            colors.emplace_back(layer.name,canvas_.color_of_layer(layer.name));
        }
        source.set_hidden_layers(std::move(hidden));source.set_layer_colors(std::move(colors));
        source.set_appearance_layers(layers_);
        const auto ink=themes_.active().get(T::geometry);
        if(has_fill)for(const auto& triangle:source.static_draw().triangles())for(int k=0;k<3;++k) {
            double x,y,z;sk.plane.to_world(triangle.xy[2*k],triangle.xy[2*k+1],x,y,z);
            MeshVertex vertex{};vertex.pos[0]=float(x);vertex.pos[1]=float(y);vertex.pos[2]=float(z);
            const auto fill=triangle.color.a<0?grender::Color{ink.r,ink.g,ink.b,ink.a}:triangle.color;
            vertex.color[0]=fill.r;vertex.color[1]=fill.g;
            vertex.color[2]=fill.b;vertex.color[3]=fill.a*color[3];
            static_fills_.push_back(vertex);
        }
        if(has_flow)for(const auto& arrow:source.door_flow_shapes()) {
            auto ink=canvas_.color_of_shape(arrow);if(ink.a<0)ink=grender::color_for(grender::Role::Geometry);
            for(auto point:{arrow.a,arrow.b}) {
                double u,v,x,y,z;gcm::to_world(vol_,point,u,v);sk.plane.to_world(u,v,x,y,z);
                lines_.push_back({{float(x),float(y),float(z)},{ink.r,ink.g,ink.b,ink.a*color[3]},2});
            }
        }
    }

}

bool Shell::begin_sketch_on_face(std::size_t body, gcm::u32 face) {
    leave_paper_space();
    const auto& bodies = modeller_.bodies();
    if (body >= bodies.size() || face >= bodies[body].face_count()) return false;
    if (!modeller_.face_is_planar(body, face)) {
        log_.push_back("that face is curved; pick a flat one to sketch on");
        return false;
    }
    journal_sketches();

    SketchDoc sk;
    sk.name = "Sketch " + std::to_string(sketches_.size() + 1);
    sk.plane = gtopo::plane_of_face(bodies[body], face, vol_);
    sk.layer = layers_.current();
    sk.on_face = true;
    sk.body = body;
    sk.parent_body_key = modeller_.body_name(body);
    sk.face = face;

    history_.record(camera_);
    camera_.look_along_normal(sk.plane.nx, sk.plane.ny, sk.plane.nz);
    camera_.set_target(gview::Point3::from(sk.plane.ox, sk.plane.oy, sk.plane.oz));
    const gview::Vec3 r = camera_.right();
    // Derive up from the plane normal; camera.up() would tilt top-face sketches.
    const double along=r.x.to_f64()*sk.plane.nx+r.y.to_f64()*sk.plane.ny+r.z.to_f64()*sk.plane.nz;
    sk.plane.ux=r.x.to_f64()-along*sk.plane.nx;
    sk.plane.uy=r.y.to_f64()-along*sk.plane.ny;
    sk.plane.uz=r.z.to_f64()-along*sk.plane.nz;
    const double length=std::sqrt(sk.plane.ux*sk.plane.ux+sk.plane.uy*sk.plane.uy+sk.plane.uz*sk.plane.uz);
    sk.plane.ux/=length;sk.plane.uy/=length;sk.plane.uz/=length;
    sk.plane.vx=sk.plane.ny*sk.plane.uz-sk.plane.nz*sk.plane.uy;
    sk.plane.vy=sk.plane.nz*sk.plane.ux-sk.plane.nx*sk.plane.uz;
    sk.plane.vz=sk.plane.nx*sk.plane.uy-sk.plane.ny*sk.plane.ux;

    sketches_.push_back(std::move(sk));
    active_sketch_ = static_cast<int>(sketches_.size()) - 1;
    load_active_space();
    set_mode(Mode::Sketch);
    sync_view_from_camera();
    log_.push_back("sketching on the face");
    return true;
}

void Shell::begin_sketch_on_ground() {
    edit_sketch(0);
}

void Shell::edit_sketch(int index) {
    leave_paper_space();
    flush_edits();
    if (index < 0 || index >= static_cast<int>(sketches_.size())) return;
    if (index != active_sketch_) {
        store_active_sketch();
        active_sketch_ = index;
        load_active_space();
    }
    const auto& pl = sketches_[index].plane;
    history_.record(camera_);
    camera_.look_along_normal(pl.nx, pl.ny, pl.nz);
    camera_.set_target(gview::Point3::from(pl.ox, pl.oy, pl.oz));
    set_mode(Mode::Sketch);
    sync_view_from_camera();
}

void Shell::finish_sketch() {
    store_active_sketch();
    rebuild_sketch_segments();
    build_frame_sources();
    if (const std::size_t n = modeller_.regenerate_from_sketch(active_sketch_); n > 0) {
        log_.push_back("rebuilt " + std::to_string(n) + " solid(s) from " + sketches_[std::size_t(active_sketch_)].name);
        mesh_dirty_ = true;
    } else if (!modeller_.last_rebuild_message().empty()) {
        log_.push_back(modeller_.last_rebuild_message());
    }
    set_mode(Mode::Solid);
    if (looking_at_sketch()) {
        history_.record(camera_);
        camera_.look(gview::StandardView::Iso);
        sync_view_from_camera();
    }
}

void Shell::switch_mode(Mode m) {
    mode_menu_open_=false;
    tablet_popup_=0;
    if(m==mode_) return;
    if(m==Mode::Sketch) edit_sketch(active_sketch_>=0?active_sketch_:0);
    else if(m==Mode::Solid) finish_sketch();
    else enter_drawing_mode();
}

void Shell::draw_mode_switch(const gui::Rect& button, const gui::Rect& bar, bool compact) {
    const auto& theme=themes_.active();
    const auto& st=ui_ctx_.style();
    const float unit=float(px(1));
    struct Choice {Mode mode;const char* name;gui::Icon icon;};
    static constexpr Choice choices[]={{Mode::Sketch,"Sketch",gui::Icon::NewSketch},
        {Mode::Solid,"Solid",gui::Icon::Box},{Mode::Drawing,"Drawing",gui::Icon::Table}};
    const Choice* current=&choices[0];
    for(const auto& c:choices) if(c.mode==mode_) current=&c;
    ui_ctx_.push_id("mode-switch");
    const auto id=ui_ctx_.id("button");
    const auto hit=ui_ctx_.interact(id,button);
    ui_ctx_.announce(id,gui::Context::Role::Button,std::string("Mode: ")+current->name,
        mode_menu_open_?"expanded":"collapsed",button,mode_menu_open_);
    const gui::Rect shape=button.deflate(gui::Insets::xy(0,std::max(0.f,(button.h-36*unit)*.5f)));
    const float radius=shape.h*.5f;
    ui_ctx_.draw().round_rect(shape,radius,theme.get(mode_menu_open_?T::ui_active:hit.hovered||hit.held?T::ui_hover:T::ui_surface_raised));
    ui_ctx_.draw().border(shape,radius,1,theme.get(T::ui_border));
    gui::Rect inner=shape.deflate(gui::Insets::xy(8*unit,0));
    const float glyph=std::min(shape.h,28*unit);
    gui::draw_icon(ui_ctx_,current->icon,inner.cut_left(glyph),theme.get(T::ui_accent),1.4f);
    gui::draw_icon(ui_ctx_,gui::Icon::ChevronDown,inner.cut_right(glyph*.75f),theme.get(T::ui_text),1.2f);
    if(!compact) ui_ctx_.draw().text(inner.deflate(gui::Insets::xy(4*unit,0)),current->name,st.font_size,
        theme.get(T::ui_text),gui::TextAlign::Left);
    if(hit.clicked) {mode_menu_open_=!mode_menu_open_;tablet_popup_=0;}
    if(mode_menu_open_) {
        const float row=std::max(button.h,40*unit);
        float width=0;
        for(const auto& c:choices) width=std::max(width,ui_ctx_.measure(c.name,st.font_size));
        width=std::min(width+row+40*unit,bar.w);
        const gui::Rect drop{std::clamp(button.x,bar.x,bar.right()-width),shape.bottom()+4*unit,width,row*3+8*unit};
        ui_ctx_.begin_overlay(drop);
        ui_ctx_.draw().shadow(drop,6*unit,12*unit,theme.get(T::ui_shadow));
        ui_ctx_.draw().round_rect(drop,6*unit,theme.get(T::ui_surface_raised));
        ui_ctx_.draw().border(drop,6*unit,1,theme.get(T::ui_border));
        const Choice* picked=nullptr;
        for(int i=0;i<3;++i) {
            const auto& c=choices[i];
            const gui::Rect r{drop.x+4*unit,drop.y+4*unit+row*float(i),drop.w-8*unit,row};
            const auto rid=ui_ctx_.id(c.name);
            const auto rin=ui_ctx_.interact(rid,r);
            const bool on=c.mode==mode_;
            ui_ctx_.announce(rid,gui::Context::Role::Button,c.name,"",r,on);
            if(on||rin.hovered||rin.held) ui_ctx_.draw().round_rect(r,4*unit,theme.get(on?T::ui_accent:T::ui_hover));
            gui::Rect content=r.deflate(gui::Insets::xy(8*unit,0));
            const auto ink=theme.get(on?T::ui_accent_text:T::ui_text);
            gui::draw_icon(ui_ctx_,c.icon,content.cut_left(std::min(row,32*unit)),ink,1.4f);
            ui_ctx_.draw().text(content.deflate(gui::Insets::xy(8*unit,0)),c.name,st.font_size,ink,gui::TextAlign::Left);
            if(rin.clicked) picked=&c;
        }
        if(input_.key_escape||(input_.pressed&&!drop.contains(input_.mouse)&&!button.contains(input_.mouse)))mode_menu_open_=false;
        ui_ctx_.end_overlay();
        if(picked) switch_mode(picked->mode);
    }
    ui_ctx_.pop_id();
}

void Shell::draw_tablet_toolbar(gui::Rect& screen) {
    const auto& theme=themes_.active();
    const auto& st=ui_ctx_.style();
    const float unit=float(px(1)), button=44*unit, gap=4*unit;
    const bool utilities=!header_utilities_;
    const float utility_width=utilities?(mode_==Mode::Sketch?3:2)*(button+gap):0;
    const float mode_full=std::max(button*2.5f,ui_ctx_.measure("Drawing",st.font_size)+80*unit);
    const bool compact_mode=screen.w<1200*unit;
    const float mode_width=compact_mode?button+20*unit:mode_full;
    const auto tools=toolbar_tools(mode_);
    const float inner=screen.w-2*gap;
    const float lead=mode_width+gap, first_cap=std::max(0.0f,inner-lead-2*(button+gap)-utility_width);
    struct Placed {const ToolDef* tool;int row;float x;bool divider;};
    std::vector<Placed> placed;
    {
        int row=0;float x=0,cap=first_cap;
        for(const auto* t:tools) {
            bool divider=x>0&&!placed.empty()&&placed.back().tool->group!=t->group;
            if(x+button+(divider?gap:0)>cap&&(x>0||row==0)) {++row;x=0;cap=inner;divider=false;}
            placed.push_back({t,row,x+(divider?gap:0),divider});
            x+=button+(divider?gap:0)+gap;
        }
    }
    const int rows=placed.empty()?1:placed.back().row+1;
    const auto bar=screen.cut_top(rows*button+(rows+1)*gap);
    ui_ctx_.draw().rect(bar,theme.get(T::ui_surface));
    ui_ctx_.draw().line({bar.x,bar.bottom()},{bar.right(),bar.bottom()},1,theme.get(T::ui_border));
    ui_ctx_.push_id("tablet-toolbar");
    const auto tap=[&](gui::Rect r,const std::string& label,bool selected,gui::Icon icon=gui::Icon::None,bool enabled=true) {
        const auto id=ui_ctx_.id(label);
        const auto hit=ui_ctx_.interact(id,r,enabled);
        ui_ctx_.announce(id,gui::Context::Role::Button,label,"",r,selected,!enabled);
        if(selected||hit.hovered||hit.held) ui_ctx_.draw().round_rect(r,6*unit,
            theme.get(selected?T::ui_accent:T::ui_hover));
        auto ink=theme.get(selected?T::ui_accent_text:T::ui_text); if(!enabled) ink.a=.38f;
        if(icon!=gui::Icon::None) gui::draw_icon(ui_ctx_,icon,r,ink,1.3f);
        else ui_ctx_.draw().text(r.deflate(gui::Insets::xy(4*unit,0)),label,
            std::min(st.font_size,(r.w-8*unit)/std::max(1.0f,float(label.size())*.56f)),ink,gui::TextAlign::Centre);
        return hit.clicked;
    };
    gui::Rect top{bar.x+gap,bar.y+gap,bar.w-2*gap,button};
    if(utilities) {
        const gui::Rect settings=top.cut_right(button);
        top.cut_right(gap);
        const gui::Rect assistant=top.cut_right(button);
        top.cut_right(gap);
        if(mode_==Mode::Sketch) {draw_plan_check_button(top.cut_right(button),bar);top.cut_right(gap);}
        if(tap(settings,"Settings",settings_open_,gui::Icon::Settings)) settings_open_=!settings_open_;
        if(tap(assistant,"Assistant panel",false,gui::Icon::Assistant)) run_command("ASSISTANT");
    }
    const gui::Rect redo=top.cut_right(button);
    top.cut_right(gap);
    const gui::Rect undo=top.cut_right(button);
    top.cut_right(gap);
    ui_ctx_.draw().line({undo.x-gap*.5f,undo.y+8*unit},{undo.x-gap*.5f,undo.bottom()-8*unit},1,theme.get(T::ui_border));
    if(tap(undo,"Undo",false,gui::Icon::Undo)) run_command("UNDO");
    if(tap(redo,"Redo",false,gui::Icon::Redo)) run_command("REDO");
    const gui::Rect mode_button=top.cut_left(mode_width);
    for(const auto& p:placed) {
        const auto* tool=p.tool;
        const float x0=p.row==0?bar.x+gap+lead:bar.x+gap;
        const float y=bar.y+gap+p.row*(button+gap);
        if(p.divider) ui_ctx_.draw().line({x0+p.x-gap*.5f,y+8*unit},{x0+p.x-gap*.5f,y+button-8*unit},1,theme.get(T::ui_border));
        const gui::Rect r{x0+p.x,y,button,button};
        const bool shapes=tool->id==ToolId::Count;
        if(shapes) shapes_anchor_=r;
        const bool on=shapes?is_shape_tool(active_tool_id_)||shapes_open_:tool->id==active_tool_id_;
        if(tap(r,tool->label,on,tool->icon,tool->wired)) {
            if(shapes) shapes_open_=!shapes_open_;
            else {activate_tool(tool->id);shapes_open_=false;}
            mode_menu_open_=false;
        }
        if(shapes) {
            gui::draw_menu_marker(ui_ctx_,r,theme.get(on?T::ui_accent_text:T::ui_text),unit);
            menu_markers_.push_back({tool->label,r});
        }
    }
    draw_mode_switch(mode_button,bar,compact_mode);
    ui_ctx_.pop_id();
    draw_shapes_menu(button);
}

void Shell::draw_shapes_menu(float row_h) {
    shapes_menu_rect_={};
    if(!shapes_open_) return;
    const auto& theme=themes_.active();
    const auto& st=ui_ctx_.style();
    struct Entry {std::string label;ToolId tool;const char* command;bool active;};
    std::vector<Entry> entries;
    for(const auto* t:shape_tools(mode_)) entries.push_back({t->label,t->id,nullptr,t->id==active_tool_id_});
    for(const auto& c:shape_commands(mode_)) entries.push_back({c.label,ToolId::Count,c.command,false});
    const auto anchor=shapes_anchor_;
    const float width=float(px(220));
    gui::Rect drop{std::clamp(anchor.x,0.0f,std::max(0.0f,float(vw_)-width)),anchor.bottom()+float(px(4)),width,row_h*float(entries.size())};
    shapes_menu_rect_=drop;
    ui_ctx_.push_id("shapes-menu");
    ui_ctx_.begin_overlay(drop);
    ui_ctx_.draw().shadow(drop,float(px(6)),float(px(10)),theme.get(T::ui_shadow));
    ui_ctx_.draw().round_rect(drop,float(px(6)),theme.get(T::ui_surface_raised));
    ui_ctx_.draw().border(drop,float(px(6)),1,theme.get(T::ui_border));
    for(std::size_t i=0;i<entries.size();++i) {
        const auto& e=entries[i];
        gui::Rect item{drop.x,drop.y+float(i)*row_h,drop.w,row_h};
        const auto id=ui_ctx_.id(e.label);
        const auto hit=ui_ctx_.interact(id,item);
        ui_ctx_.announce(id,gui::Context::Role::Button,e.label,"",item,e.active,false);
        if(e.active||hit.hovered||hit.held) ui_ctx_.draw().round_rect(item.deflate(gui::Insets::all(float(px(2)))),float(px(4)),
            theme.get(e.active?T::ui_accent:T::ui_hover));
        if(const auto* def=tool_def(e.tool)) gui::draw_icon(ui_ctx_,def->icon,{item.x+float(px(4)),item.y,row_h,row_h},
            theme.get(e.active?T::ui_accent_text:T::ui_text),1.3f);
        ui_ctx_.draw().text({item.x+row_h+float(px(8)),item.y,item.w-row_h-float(px(12)),item.h},e.label,st.font_size,
            theme.get(e.active?T::ui_accent_text:T::ui_text),gui::TextAlign::Left);
        if(hit.clicked) {
            shapes_open_=false;
            if(e.command) run_command(e.command);
            else activate_tool(e.tool);
        }
    }
    if(input_.key_escape||(input_.pressed&&!drop.contains(input_.mouse)&&!anchor.contains(input_.mouse))) shapes_open_=false;
    ui_ctx_.end_overlay();
    ui_ctx_.pop_id();
}

void Shell::draw_placement_chip(const gui::Rect& canvas) {
    placement_chip_rect_={};
    const Tool ct=active_tool_id_==ToolId::Rect?Tool::Rect:active_tool_id_==ToolId::Circle?Tool::Circle:
                  active_tool_id_==ToolId::Arc?Tool::Arc:Tool::Select;
    const auto modes=Canvas::tool_modes(ct);
    if(modes.empty()||mode_==Mode::Solid) return;
    const auto& theme=themes_.active();
    const auto& st=ui_ctx_.style();
    const float h=float(px(tablet_mode_?44:30)),pad=float(px(14));
    float total=0;
    for(const auto& m:modes) total+=ui_ctx_.measure(m,st.font_size)+2*pad;
    gui::Rect chip{canvas.x+(canvas.w-total)*.5f,canvas.y+float(px(12)),total,h};
    chip.x=std::max(chip.x,canvas.x+float(px(88)));
    placement_chip_rect_=chip;
    ui_ctx_.push_id("placement-chip");
    ui_ctx_.draw().shadow(chip,h*.5f,float(px(8)),theme.get(T::ui_shadow));
    ui_ctx_.draw().round_rect(chip,h*.5f,theme.get(T::ui_surface_raised));
    ui_ctx_.draw().border(chip,h*.5f,1,theme.get(T::ui_border));
    const int current=canvas_.tool_mode(ct);
    float x=chip.x;
    for(std::size_t i=0;i<modes.size();++i) {
        const float w=ui_ctx_.measure(modes[i],st.font_size)+2*pad;
        const gui::Rect seg{x,chip.y,w,h};
        x+=w;
        const auto id=ui_ctx_.id(modes[i]);
        const auto hit=ui_ctx_.interact(id,seg);
        const bool on=int(i)==current;
        ui_ctx_.announce(id,gui::Context::Role::Button,modes[i],"",seg,on,false);
        if(on||hit.hovered) ui_ctx_.draw().round_rect(seg.deflate(gui::Insets::all(float(px(3)))),(h-float(px(6)))*.5f,
            theme.get(on?T::ui_accent:T::ui_hover));
        ui_ctx_.draw().text(seg,modes[i],st.font_size,theme.get(on?T::ui_accent_text:T::ui_text),gui::TextAlign::Centre);
        if(hit.clicked&&!on) {
            canvas_.set_tool_mode(ct,int(i));
            activate_tool(active_tool_id_);
            if(const auto* def=tool_def(active_tool_id_)) log_.push_back(std::string(def->label)+": "+modes[i]);
        }
    }
    ui_ctx_.pop_id();
}

void Shell::draw_toolbar(gui::Rect& screen) {
    if(tablet_mode_) {draw_tablet_toolbar(screen);return;}
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    const auto layout = layout_toolbar(mode_, screen, float(px(1)));
    gui::Rect bar = screen.cut_top(layout.height);
    ui_ctx_.draw().rect(bar, theme.get(T::ui_surface));
    ui_ctx_.draw().line({bar.x, bar.bottom()}, {bar.right(), bar.bottom()}, 1.0f,
                        theme.get(T::ui_border));

    const float btn = float(px(40));
    const float cap_h = float(px(16));

    {
        if(mode_==Mode::Sketch)draw_plan_check_button(layout.plan_check,bar);
        const gui::Rect gear = layout.settings;
        const auto gin = ui_ctx_.interact(ui_ctx_.id("settings-gear"), gear);
        if (gin.hovered) ui_ctx_.draw().round_rect(gear, st.radius_small, theme.get(T::ui_hover));
        gui::draw_icon(ui_ctx_, gui::Icon::Settings, gear,
                       theme.get(settings_open_ ? T::ui_accent : T::ui_text), 1.0f);
        if (gin.clicked) settings_open_ = !settings_open_;

        const gui::Rect chat = layout.assistant;
        const auto cin = ui_ctx_.interact(ui_ctx_.id("assistant-toggle"), chat);
        const bool open = [&] {
            const gui::PanelState* p = dock_.find("assistant");
            return p && p->open && !p->collapsed;
        }();
        if (cin.hovered) ui_ctx_.draw().round_rect(chat, st.radius_small, theme.get(T::ui_hover));
        gui::draw_icon(ui_ctx_, gui::Icon::Assistant, chat,
                       theme.get(open || ai_->busy ? T::ui_accent : T::ui_text), 1.0f);
        if (cin.clicked) {
            if (open) dock_.collapse("assistant", true);
            else run_command("ASSISTANT");
        }
    }
    ui_ctx_.push_id("toolbar");
    for (const auto& fragment : layout.groups) {
        gui::Rect cursor = fragment.bounds;
        ui_ctx_.push_id(fragment.group->caption);
        for (const auto* def : fragment.tools) {
            const auto& d = *def;
            gui::Rect b = cursor.cut_left(btn);
            b.h = btn;
            ui_ctx_.push_id(d.label);
            const bool shapes = d.id == ToolId::Count;
            if (shapes) shapes_anchor_ = b;
            const auto tid = ui_ctx_.id("t");
            const auto in = ui_ctx_.interact(tid, b, d.wired);
            const bool on = d.wired && (shapes ? is_shape_tool(active_tool_id_) || shapes_open_ :
                            active_tool_id_ == d.id &&
                            (d.id != ToolId::Select || (!picking_sketch_plane_ && modeller_.op() == Modeller::Op::None)));
            ui_ctx_.announce(tid, gui::Context::Role::Button, d.label, "", b, on, !d.wired);
            gui::Color ink = theme.get(on ? T::ui_accent_text : T::ui_text);
            if (!d.wired) ink.a = 0.38f;
            if (on) ui_ctx_.draw().round_rect(b, float(px(8)), theme.get(T::ui_accent));
            else if (in.hovered && d.wired) {
                gui::Color layer = theme.get(T::ui_accent);
                layer.a = st.hover_opacity;
                ui_ctx_.draw().round_rect(b, float(px(8)), layer);
            }
            gui::Rect ib{b.centre().x - float(px(20)), b.centre().y - float(px(20)),
                         float(px(40)), float(px(40))};
            gui::draw_icon(ui_ctx_, d.icon, ib, ink, 1.6f);
            if (shapes) {
                gui::draw_menu_marker(ui_ctx_, b, ink, float(px(1)));
                menu_markers_.push_back({d.label, b});
            }
            if (in.hovered) {
                std::string tip = d.label;
                if (!d.wired) tip += " (not connected yet)";
                const float w = ui_ctx_.measure(tip, st.font_size_small) + float(px(16));
                gui::Rect tr{std::clamp(b.centre().x - w * 0.5f, bar.x, std::max(bar.x, bar.right() - w)),
                             bar.bottom() + float(px(6)), w, float(px(28))};
                ui_ctx_.draw().shadow(tr, float(px(4)), float(px(10)), theme.get(T::ui_shadow));
                ui_ctx_.draw().round_rect(tr, float(px(4)), theme.get(T::ui_surface_raised));
                ui_ctx_.draw().border(tr, float(px(4)), 1.0f, theme.get(T::ui_border));
                ui_ctx_.draw().text(tr, tip, st.font_size_small, theme.get(T::ui_text), gui::TextAlign::Centre);
            }
            if (in.clicked && d.wired) {
                if (shapes) shapes_open_ = !shapes_open_;
                else { activate_tool(d.id); shapes_open_ = false; }
            }
            ui_ctx_.pop_id();
            cursor.cut_left(float(px(2)));
        }
        gui::Rect cap{fragment.bounds.x, fragment.bounds.y + btn, fragment.bounds.w, cap_h};
        ui_ctx_.draw().text(cap, fragment.group->caption, st.font_size_small * 0.92f, theme.get(T::ui_text_dim),
                            gui::TextAlign::Centre);
        const float rule_x = fragment.bounds.right() + float(px(8));
        if (rule_x < bar.right() - float(px(12)))
            ui_ctx_.draw().line({rule_x, cap.y - btn + float(px(6))}, {rule_x, cap.bottom() - float(px(6))},
                                1.0f, theme.get(T::ui_border));
        ui_ctx_.pop_id();
    }
    ui_ctx_.pop_id();
    draw_mode_switch(layout.modes, bar, false);
    draw_shapes_menu(float(px(30)));
}


double Shell::now_seconds() const {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

void Shell::draw_dynamic_input(const gui::Rect& canvas) {
    const Canvas::DynamicInput di = canvas_.dynamic_input();
    if (!di.visible || di.fields.empty() || mode_ != Mode::Sketch) return;
    const auto& theme = themes_.active();
    const float row_h = float(px(30));
    const float width=float(px(224));
    gui::Rect card{canvas.right()-width-float(px(16)),canvas.y+float(px(144)),width,
                   row_h*float(di.fields.size())+float(px(42))};
    card.x=std::clamp(card.x,canvas.x,std::max(canvas.x,canvas.right()-card.w));
    card.y=std::clamp(card.y,canvas.y,std::max(canvas.y,canvas.bottom()-card.h));
    ui_ctx_.push_id("drawing_values");
    ui_ctx_.draw().shadow(card, float(px(6)), float(px(10)), theme.get(T::ui_shadow));
    ui_ctx_.draw().round_rect(card, float(px(6)), theme.get(T::ui_surface_raised));
    ui_ctx_.draw().border(card, float(px(6)), 1.0f, theme.get(T::ui_border));

    float y = card.y + float(px(4));
    for (std::size_t i=0;i<di.fields.size();++i) {
        const auto& f=di.fields[i];
        gui::Rect row{card.x+float(px(8)),y,card.w-float(px(16)),row_h};
        gui::Rect lab=row; lab.w=float(px(28));
        gui::label(ui_ctx_,lab,f.label,gui::TextAlign::Left,true);
        gui::Rect val=row; val.x+=float(px(28)); val.w-=float(px(28));
        std::string text=f.active ? canvas_.dynamic_buffer() : "";
        const std::string label="Drawing "+f.label;
        value_focus_order_.push_back(ui_ctx_.id(label));
        if (gui::text_field(ui_ctx_,val,label,text,f.value)) canvas_.dynamic_set(int(i),std::move(text));
        if (ui_ctx_.is_focused(ui_ctx_.id(label)) && input_.key_enter) {
            input_.key_enter=false;
            if (canvas_.dynamic_buffer().empty()) canvas_.dynamic_set(int(i),f.value);
            if (canvas_.dynamic_commit()) { ui_ctx_.clear_focus(); mesh_dirty_=true; }
        }
        y+=row_h;
    }
    gui::Rect ok{card.right()-float(px(64)),y+float(px(4)),float(px(56)),float(px(28))};
    value_focus_order_.push_back(ui_ctx_.id("OK"));
    if (gui::button(ui_ctx_,ok,"OK",true,true)) {
        if (canvas_.dynamic_buffer().empty()) canvas_.dynamic_set(0,di.fields[0].value);
        if (canvas_.dynamic_commit()) { ui_ctx_.clear_focus(); mesh_dirty_=true; }
    }
    ui_ctx_.pop_id();
}
}
