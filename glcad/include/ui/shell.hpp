#pragma once
#include "ui/block_library.hpp"

#include <cstdlib>

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "math/thread.hpp"
#include "ui/canvas.hpp"
#include "ui/mcp.hpp"
#include "ui/document_lock.hpp"
#include "ui/modeller.hpp"
#include "ui/tools.hpp"
#include "ui/menus.hpp"
#include "ui/actions.hpp"
#include <functional>
#include "geometry/sketch.hpp"
#include "geometry/annotation.hpp"
#include "geometry/boolean.hpp"
#include "ui/frame_types.hpp"
#include "ui/touch.hpp"
#include "engine/document_file.hpp"
#include "engine/commandline.hpp"
#include "ui/commands.hpp"
#include "render/scene.hpp"

namespace gcmd {
class Interpreter;
class ScriptRunner;
struct Script;
struct RunReport;
}

namespace gcapp {
enum class DrawingDiscipline { General, Architectural, Mechanical };
struct LispDelete {
    void operator()(gcmd::Interpreter* p) const noexcept;
};
struct ScriptDelete {
    void operator()(gcmd::ScriptRunner* p) const noexcept;
};
}
#include "render/layout.hpp"
#include "render/registry.hpp"
#include "render/theme.hpp"
#include "engine/entity.hpp"
#include "engine/hatch.hpp"
#include "geometry/extrude.hpp"
#include "geometry/triangulate.hpp"
#include "ui/dock.hpp"
#include "ui/icons.hpp"
#include "ui/widgets.hpp"
#include "render/viewcube.hpp"
#include "render/views.hpp"

#include "exchange/pdf.hpp"
#include "exchange/views.hpp"
#include "geometry/pmi.hpp"
#include "engine/parameter.hpp"
#include "engine/appconfig.hpp"
#include "plancheck/plancheck.hpp"
#include "ai/client.hpp"
#include "ai/discovery.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace gcapp {

std::string file_title(const std::string& path);
std::string scale_display(int den);
std::string lower_ext(const std::string& path);
std::string uppercase(std::string t);
std::string recovery_identity();
std::string pick_model_by_label(const std::vector<gai::ModelInfo>& models,
                                const char* label, const char* fallback);
bool parse_numbers(const std::string& text, std::vector<double>& out);

enum class Lens { TwoD, ThreeD };

struct MeshVertex;
struct UiInstance;
struct TextVertex;

class Shell {
    friend class CommandContext;

public:
    ~Shell();
    // Never move a Shell while an assistant worker is running.
    Shell(Shell&&) noexcept = default;
    Shell& operator=(Shell&&) noexcept = default;
    enum class FileAsk : gcm::u8 { Open, Save, Import, Export, Attach, ExportPdf, LoadLibrary, SaveLibrary };
    using FileHook = std::function<void(FileAsk, std::function<void(std::string)>)>;

    Shell();

    void set_product_edit_access(bool allowed);
    bool product_edit_access() const noexcept { return product_edit_allowed_; }
    const std::string& document_identity() const noexcept { return recovery_id_; }
    void set_product_hook(std::function<void()> hook) { product_hook_=std::move(hook); }
private:
    bool product_edit_allowed_{true};
    std::function<void()> product_hook_;
    void draw_product_viewer();
    bool require_edit_access();
public:

    bool load_font(std::string* error);
    bool icons_loaded() const noexcept { return icons_loaded_; }
    const gtext::Font& font() const noexcept { return font_; }

    void set_viewport(double w, double h);
    void set_safe_insets(double left, double top, double right, double bottom);
    gui::Insets safe_insets() const noexcept { return safe_insets_; }
    float toolbar_top() const noexcept { return toolbar_top_; }
    const std::vector<std::pair<std::string, gui::Rect>>& menu_markers() const noexcept { return menu_markers_; }
    double viewport_width() const noexcept { return vw_; }
    gui::Rect canvas_bounds() const noexcept { return canvas_rect_; }
    gui::Rect canvas_fit_bounds() const noexcept;
    double viewport_height() const noexcept { return vh_; }
    void fit_width(double metres);

    void set_dpr(double scale);
    void scale_ui(double factor);
    bool wants_frame() const noexcept { return mesh_dirty_ || ai_->busy || ai_recording_ || (mcp_server_ && mcp_server_->pending()); }
    uint32_t graphics_msaa() const noexcept { return graphics_msaa_; }
    double dpr() const noexcept { return dpr_; }

    FrameUniforms frame_uniforms() const;

    enum class CursorKind { Arrow, Pick, Move, Grab, Cross, Text };
    CursorKind cursor();

    void pointer_move(double x, double y);
    void pointer_down(double x, double y);
    void pointer_up(double x, double y);
    void pencil_tap(double x, double y);
    void settle_pointer(double x, double y);
    void pencil_release(double x, double y);
    void finger_tap(double x, double y);
    void apply_gesture(const TouchResult& r);
    enum class PointerPhase { Down, Move, Up };
    void pointer_input(PointerPhase phase, double x, double y, bool secondary);
    bool tap_region_at(double x, double y);
    void right_down(double x, double y);

    void middle_down(double x, double y);
    void middle_up(double x, double y);
    void right_up(double x, double y);
    bool navigating() const noexcept { return middle_down_; }
    void scroll(double dx, double dy, bool precise);
    void pinch(double x, double y, double magnification);
    void key_char(const std::string& utf8);
    void key_special(int code);          // 0 enter, 1 escape, 2 backspace, 3 tab, 4 delete
    void set_modifiers(bool shift, bool ctrl, bool alt, bool cmd);

    void build_frame();
    bool building_frame() const noexcept { return building_frame_; }
    void build_underlays();
    std::vector<gplot::PdfImage> underlay_content(const Canvas& source, bool preview=false) const;
    bool attach_underlay(const std::string& path, int page=1, bool preserve_reference=true);
    bool import_pdf(const std::string& path, int page=1);
    const std::vector<ImageDraw>& images() const noexcept { return images_; }
    const std::vector<UiInstance>&  ui() const noexcept { return ui_; }
    const std::vector<TextVertex>&  text() const noexcept { return text_; }
    const std::vector<TextVertex>&  tris() const noexcept { return tris_; }
    const UiLayers&                 ui_layers() const noexcept { return ui_layers_; }
    const std::vector<MeshVertex>&  mesh() const noexcept { return mesh_; }
    gcm::u64 mesh_version() const noexcept { return mesh_version_; }
    const std::vector<LineVertex>& static_lines() const noexcept { return static_lines_; }
    const std::vector<ArcInstance>& static_arcs() const noexcept { return static_arcs_; }
    const std::vector<ArcInstance>& canvas_arcs() const noexcept { return arcs_; }
    gcm::u64                       static_lines_version() const noexcept { return lifted_key_; }
    const std::vector<LineVertex>& canvas_lines() const noexcept { return lines_; }
    std::size_t total_line_count() const noexcept { return static_lines_.size() + lines_.size(); }
    std::size_t sketch_line_count() const noexcept { return sketch_lines_; }
    const std::vector<LineVertex>& canvas_points() const noexcept { return points_; }

    Lens              lens() const noexcept {
        return looking_at_sketch() ? Lens::TwoD : Lens::ThreeD;
    }
    bool              looking_at_sketch() const noexcept;
    const gtopo::SketchPlane& sketch_plane() const noexcept;
    std::size_t sketch_count() const noexcept { return sketches_.size(); }
    void              set_lens(Lens l) noexcept;
    void              toggle_lens() noexcept;
    Canvas&           canvas() noexcept { touch_metadata("canvas()"); return canvas_; }
    const Canvas&     canvas() const noexcept { return canvas_; }
    gview::Camera&    camera() noexcept { return camera_; }
    const gview::Camera& camera() const noexcept { return camera_; }
    Modeller&         modeller() noexcept { touch_metadata("modeller()"); return modeller_; }
    const Modeller&   modeller() const noexcept { return modeller_; }
    gdoc::LayerTable& layers() noexcept { touch_metadata("layers()"); return layers_; }
    const gdoc::LayerTable& layers() const noexcept { return layers_; }
    const gdoc::LinetypeTable& linetypes() const noexcept { return linetypes_; }
    const gtext::TextStyleTable& text_styles() const noexcept { return text_styles_; }
    struct Layout {
        gcm::u32 space_id{0};
        std::string name{"Layout1"};
        std::string paper{"A3"};
        gplot::PaperSize imported_paper{};
        bool custom_paper{false};
        bool direct_model_view{true};
        bool        landscape{true};
        int         scale_den{0};          // 0 = fit to paper
        double      dx{0.0}, dy{0.0};      // sheet metres
        std::vector<gsnap::Shape>     shapes;
        std::vector<Canvas::TextItem> notes;
        std::vector<gplot::Viewport> viewports;
        gplot::PlotArea area{gplot::PlotArea::Extents};
        double win_lo_x{0.0}, win_lo_y{0.0}, win_hi_x{0.0}, win_hi_y{0.0};

        gdraft::ViewSet views;
        int             view_body{-1};        // which solid; -1 = none
        double          view_scale{1.0};
        gdraft::Layout  view_layout{gdraft::Layout::ThreeView};
        std::vector<gsnap::Shape>  view_shapes;
        std::vector<std::string>   view_layers;
        struct SectionDef { gcm::u32 parent{0}; gdraft::SectionLine line; };
        struct DetailDef  { gcm::u32 parent{0}; gdraft::DetailCircle circle; double scale{2.0}; };
        std::vector<SectionDef> sections;
        std::vector<DetailDef>  details;
        gplot::StyleMode style_mode{gplot::StyleMode::ColorDependent};
        bool             monochrome{false};
        std::string plot_style_name;
        std::vector<gcm::u8> plot_style_data;
    };
    const std::vector<Layout>& layouts() const noexcept { return layouts_; }
    int selected_layout() const noexcept { return in_paper_space_ ? active_layout_ : -1; }
    bool select_layout(int index); // -1 selects Model within the current file
    bool in_paper_space() const noexcept { return in_paper_space_; }
    bool read_only() const noexcept { return read_only_; }
    const std::string& lock_holder() const noexcept { return lock_holder_; }
    const std::string& plot_paper() const noexcept { return plot_paper_; }
    double text_height() const noexcept { return text_height_; }
    gui::Dock&        dock() noexcept { return dock_; }

    Mode   mode() const noexcept { return mode_; }
    void   set_mode(Mode m);
    ToolId active_tool() const noexcept { return active_tool_id_; }
    void   activate_tool(ToolId id);

    enum class SketchVisibility { Automatic, Shown, Hidden };
    struct SketchDoc {
        gcm::u32 space_id{0};
        std::string                 name;
        gtopo::SketchPlane          plane;
        std::vector<gsnap::Shape>   shapes;      // plane coordinates
        std::vector<Canvas::TextItem> texts;
        std::vector<gdoc::Segment2> segments;
        gcm::u64                    segments_version{0};
        SketchVisibility            visibility{SketchVisibility::Automatic};
        gcm::u32                    parent_body_key{0};
        bool                        on_face{false};
        std::size_t                 body{0};
        gcm::u32                    face{0};
        std::string                 layer{"0"};
    };
    const std::vector<SketchDoc>& sketches() const noexcept { return sketches_; }
    bool sketch_visible(int index) const;
    int sketch_parent(int index) const;
    void set_sketch_visibility(int index, SketchVisibility visibility);
    int  active_sketch() const noexcept { return active_sketch_; }
    bool begin_sketch_on_face(std::size_t body, gcm::u32 face);
    void begin_sketch_on_ground();
    void edit_sketch(int index);
    void finish_sketch();
    const Modeller::Target& hovered_target() const noexcept { return hover_target_; }
    const Modeller::Target& selected_target() const noexcept { return sel_target_; }
    SelectionSummary selection_summary() const;
    bool has_solid_selection() const;
    bool erase_selected_solids();
    void clear_solid_selection();
    void select_body(std::size_t body, bool additive = false);
    bool context_menu_open() const noexcept { return ctx_menu_open_; }
    gtheme::ThemeSet& themes() noexcept { touch_metadata("themes()"); return themes_; }
    std::string       title() const;

    bool replace_focused_text(gui::Id id, const std::string& utf8);
    bool focus_accessible(gui::Id id);
    bool ui_wants_mouse() const noexcept { return ui_wants_mouse_; }
    const std::vector<gui::Context::Accessible>& accessible_ui() const noexcept { return ui_ctx_.accessibility_tree(); }

private:
    void   layout_ui();
    void emit_cad_text(const Canvas::Label& label, gui::Color color, const gui::Rect& clip,
        const std::function<bool(double,double,double&,double&)>& screen);
    void emit_cad_text_mesh(const std::vector<gclip::Vertex>& mesh, gui::Color color, const gui::Rect& clip,
        const std::function<bool(double,double,double&,double&)>& screen);
    void   emit_text(const gui::Rect& r, const std::string& s, float size,
                     gui::Color c, gui::TextAlign align, const gui::Rect& clip);
    void   rebuild_mesh();
    void   pointer_move_impl(double x, double y);
    void   return_to_select();
    void   touch_metadata(const char* why = "?") noexcept {
        ++metadata_gate_;
        metadata_gate_reason_ = why;
    }
public:
    void add_layer();
    bool delete_layer(const std::string& name);
    bool paper_rect(double& cx, double& cy, double& w, double& h) const;
    void zoom_to_paper();
    void ensure_paper_in_view();
    bool plot_den(double& den, double& cx, double& cy) const;
    struct PlotContent {
        gcm::u64 key{0};
        bool     valid{false};
        bool     have_box{false};
        double   lo_x{0}, lo_y{0}, hi_x{0}, hi_y{0};
        double   den{0.0};
        std::string scale_name;
        gplot::PlotJob::SpaceContent base;
        gplot::PlotJob::SpaceContent marks;
    };
    const PlotContent& plot_content() const;
    gplot::PlotResult plot_sheet(gplot::PdfWriter& out, bool preview=false) const;
    std::map<std::string,std::string> sheet_fields(const Layout& layout) const;
    bool set_selection_property(const std::string& property, const std::string& value);
    double plot_offset_x() const noexcept { return plot_dx_; }
    double plot_offset_y() const noexcept { return plot_dy_; }

    bool set_mcp_enabled(bool enabled, int port = -1);
    bool mcp_running() const noexcept { return mcp_server_ && mcp_server_->port()!=0; }
    std::string mcp_url() const;
    void process_mcp_requests();
    void load_app_config();
    void save_app_config() const;
    bool save_app_config_checked() const;
    void adopt_display_unit(gunit::Length u);
    gfile::AppConfig&       app_config() noexcept { touch_metadata("app_config()"); return app_config_; }
    const gfile::AppConfig& app_config() const noexcept { return app_config_; }

    void set_audio_devices(std::vector<std::string> outputs, std::vector<std::string> inputs) {
        audio_outputs_ = std::move(outputs);
        audio_inputs_ = std::move(inputs);
    }
    std::function<void(std::vector<gcm::u8>, std::string)> audio_play_hook;
    std::function<bool()>                              audio_stream_begin;
    std::function<void(const gcm::u8*, std::size_t)>   audio_stream_feed;
    std::function<void()>                              audio_stream_end;
    std::function<bool()>                    audio_record_begin;
    std::function<std::vector<gcm::u8>()>    audio_record_end;
    std::function<float()>                   audio_record_level;
    std::function<std::vector<gcm::u8>()>    audio_record_take;
    std::function<bool()>                    audio_playing;
    std::function<void(const std::string&)>  clipboard_set_hook;
    void copy_selection() noexcept { chat_copy_pending_ = true; }

    void run_command(const std::string& text);

    using PlanFinding = gplan::Finding;
    std::vector<PlanFinding> plan_check() const;

    void ai_send(std::string prompt, bool with_view = false);
    void ai_stash_screenshot(std::vector<gcm::u8> png);
    void ai_queue_tools(std::vector<gai::ToolCall> calls);
    struct AiChatLine {
        std::string role;
        std::string text;
        std::string thinking;
        std::vector<gcm::u8> audio;
        std::string          audio_mime;
    };
    void ai_replay_line(std::size_t index);

    void ai_note_selection() noexcept {
        ai_selected_via_tool_ = true;
        ai_drew_since_select_ = false;
    }
    void ai_note_drawing() noexcept { ai_drew_since_select_ = true; }
    void ai_note_erase() noexcept { ai_selected_via_tool_ = false; }
    bool ai_selection_stale() const noexcept {
        return ai_selected_via_tool_ && ai_drew_since_select_;
    }
    std::vector<AiChatLine> ai_transcript() const;
    bool ai_busy() const noexcept { return ai_->busy; }

    static void split_thinking(const std::string& raw, std::string& visible,
                               std::string& thinking);
    static std::vector<std::string> reply_choices(const std::string& text);
    double drawing_k_{1.0};
    double plot_dx_{0.0}, plot_dy_{0.0};
    bool   layout_drag_{false};
    double layout_from_x_{0.0}, layout_from_y_{0.0};
    double layout_dx0_{0.0}, layout_dy0_{0.0};
    double layout_lo_x_{0.0}, layout_lo_y_{0.0}, layout_hi_x_{0.0}, layout_hi_y_{0.0};
    std::string layer_before_drawing_;
    mutable PlotContent plot_cache_;
    mutable gplot::PdfWriter sheet_preview_;
    mutable gcm::u64 sheet_preview_key_{~gcm::u64{0}};
    void draw_viewport_sheet();
    std::string property_selection_key_;
    std::map<std::string,std::string> property_inputs_;
    PlotContent build_plot_content(const Canvas& source, double annotation_scale) const;

    // Behind a pointer: workers capture it, and a Shell must stay movable.
    gfile::AppConfig  app_config_;
    std::unique_ptr<mcp::HttpServer> mcp_server_;
    bool mcp_processing_{false};
    std::string mcp_error_, mcp_port_input_;
    void draw_settings_mcp(gui::Stack& stack);
    struct AiState {
        gai::Discovery           discovery;
        std::atomic<bool>        busy{false};
        std::atomic<bool>        abort{false};
        // Big stack: recursive streamed-JSON parsing overflowed std::thread's 512 KB.
        gcm::BigStackThread      worker;
        mutable std::mutex       mutex;
        std::condition_variable  cv;
        std::vector<AiChatLine>  chat;
        std::string              stream;
        std::string              stream_thinking;
        std::string              error;
        std::vector<gai::ModelInfo> models;
        std::string              status;
        std::vector<gai::ToolCall>  pending_calls;
        std::vector<std::string>    call_results;
        std::vector<std::string>    pending_activity;
        std::size_t                 call_at{0};
        bool                        awaiting_tools{false};
        int                         confirm_state{0};   // 0 none, 1 asking, 2 run, 3 deny
        std::string                 voice_prompt;
        bool                        voice_from_call{false};
        std::vector<gcm::u8>        voice_clip;
        std::thread                 tts_worker;
        std::atomic<bool>           tts_busy{false};
        std::thread                 status_worker;
        std::atomic<bool>           status_busy{false};
        std::vector<gcm::u8>        shot;
    };
    std::unique_ptr<AiState> ai_ = std::make_unique<AiState>();
    std::string       ai_input_;
    float             ai_scroll_{0.0f};
    int        chat_sel_a_{-1}, chat_sel_b_{-1};
    float      chat_sel_a_x_{0.0f}, chat_sel_b_x_{0.0f};
    int        chat_press_row_{-1}, chat_hover_row_{-1};
    gui::Vec2  chat_press_pos_{};
    double     chat_press_time_{0.0};
    bool       chat_dragging_{false};
    bool       chat_long_done_{false};
    bool       chat_copy_pending_{false};
    bool       ai_selected_via_tool_{false};
    bool       ai_drew_since_select_{false};
public:
    void enable_project_ask() noexcept { project_ask_enabled_ = true; }
    const std::string& project_type() const noexcept { return project_type_; }
    void set_project_type(std::string t);
    DrawingDiscipline drawing_discipline() const noexcept { return drawing_discipline_; }
    void set_drawing_discipline(DrawingDiscipline discipline);
private:
    DrawingDiscipline drawing_discipline_{DrawingDiscipline::General};
    std::string project_type_{"general"};
    std::string project_location_;
    bool        project_ask_enabled_{false};
    bool        project_ask_done_{false};
    int         project_ask_step_{0};     // 0 discipline, 1 location
    void draw_project_ask();
public:
    std::string       ai_effort_override_;
    bool              ai_recording_{false};
    bool              ai_call_{false};
    bool              ai_speak_once_{false};
    std::vector<gcm::u8> ai_attach_;
    std::string          ai_attach_name_, ai_attach_mime_;
    std::array<float, 256> ai_wave_{};
    std::size_t           ai_wave_at_{0};
    double            ai_status_at_{-1e9};
    double            ai_settings_seen_at_{-1e9};
    std::vector<std::string> audio_outputs_, audio_inputs_;
    void draw_assistant_panel();
    void process_ai_tools();
    void apply_sysvars();
    void sync_drafting_vars(bool publish);
    bool set_drafting_var(const std::string& name, const std::string& value);
    void draw_settings_drafting(gui::Stack& stack);
    std::map<std::string,double> drafting_seen_;
    std::map<std::string,std::string> drafting_text_seen_, drafting_fields_;
    void publish_sysvars();
    double var_filletrad_{-1e30}, var_chamfera_{-1e30}, var_textsize_{-1e30};
    double var_ltscale_{-1e30}, var_osmode_{-1e30};
    bool in_ai_tools_{false};
    void ai_speak_reply(const std::string& text);
    void draw_settings_ai(gui::Stack& s);
    int  ai_settings_tab_{0};
    int         ai_page_{0};              // 0 list, 1 add, 2 server detail
    std::string ai_page_url_;
    std::string ai_new_url_, ai_new_name_, ai_new_key_;
    gfile::AiServerRecord* ai_record(const std::string& url);
    std::optional<gai::Client> ai_server_client(const std::string& url) const;
    void ai_ensure_discovery();
    void ai_join_worker();
    void ai_refresh_status();
    void ai_stop_and_send_voice();
    bool ai_configured() const;
    void ai_transcribe_clip(std::vector<gcm::u8> bytes, bool speak_back);
    std::optional<std::vector<gcm::u8>> ai_speak_text(const gai::Client& c,
                                                      const std::string& text,
                                                      std::string& error);
    void ai_call_tick();
    bool   call_heard_{false};
    double call_started_{0.0}, call_last_voice_{0.0}, call_quiet_drop_{0.0};
    std::optional<gai::Client> ai_client() const;
    void enter_drawing_mode();
private:
    void   apply_theme_palette();
    double px(double logical) const noexcept { return logical * dpr_; }

    void   sync_view_from_camera();
    bool   to_plane(double x, double y, double& sx, double& sy) const;

    Canvas               canvas_;
    gview::Camera        camera_;
    gview::ViewTable     views_;
    gview::ViewHistory   history_;
    gview::Walker        walker_;
    gview::ViewCube::Drag cube_drag_{};

    gtheme::ThemeSet     themes_;
    gtext::Font          font_;
    bool                 icons_loaded_{false};
    gtext::FontRegistry  fonts_;

    gcmd::Registry       commands_;
    CommandTable         command_table_;
    gcmd::SysVars        vars_;
    std::unique_ptr<gcmd::CommandLine> cli_;
    gui::CommandBarState bar_;
    std::vector<std::string> log_;
    std::vector<std::string> history_source_, history_rows_;
    gui::Rect command_history_rect_{};
    float history_width_{0}, history_font_size_{0};
    int history_scroll_{0};
    bool history_hidden_{false};
    gui::Insets safe_insets_{};
    std::string reported_value_error_;
    void report_modeller_error();

    gview::Point3 orbit_pivot() const;

    bool           middle_down_{false};
    bool           right_down_{false};
    gview::Point3  focus_pivot() const;
    bool selection_anchor(gview::Point3& out) const;
    void pan_selection(double dx, double dy);
    void zoom_selection(double x, double y, double factor);

    gui::Context   ui_ctx_;
    std::vector<gui::Id> value_focus_order_;
    gui::InputState input_{};

    gcm::WorkingVolume   vol_;
    gdoc::LayerTable     layers_;
    gdoc::Document       doc_;
    grender::Scene       scene_;
    void                 rebuild_scene();
    gcm::u64             mirror_key() const noexcept;
    static gdoc::EntityId document_body_id(gcm::u32 key) noexcept;
    static gdoc::EntityId preview_scene_id(std::size_t body) noexcept;
    gcm::u64             mirrored_{~0ull};
    bool                 document_undo_{std::getenv("GLCAD_DOCUMENT_UNDO") != nullptr};
    gcm::u64             document_revision_{0};
    std::string          last_change_label_;
    void                 listen_to_document();
    Modeller             modeller_;
    bool                 mesh_dirty_{true};
    std::vector<gdoc::Segment2> sketch_segs_;
    gcm::u64                    sketch_segs_key_{0};
    bool                 last_edit_was_solid_{false};
    gui::Rect            canvas_rect_{0, 0, 1440, 900};
    std::optional<ggeo::d3::AABB<gcm::F64>> pending_extent_fit_;
    std::optional<ggeo::d3::AABB<gcm::F64>> refit_bounds_;
    std::array<double, 8> refit_pose_{};
    double refit_w_{0}, refit_h_{0};
    std::array<double, 8> camera_pose() const noexcept;

    Modeller::Target     hover_target_;
    Modeller::Target     sel_target_;
    std::vector<Modeller::Target> selected_targets_;
    std::vector<std::size_t> selected_bodies_;
    void draw_unit_chip(const gui::Rect& canvas);
    void                 draw_context_menu(const gui::Rect& canvas);
    void                 execute_selection_action(const ActionDef& action);
    void                 select_context_target(double x,double y);
    void draw_drafting_features(const gui::Rect& canvas);
    bool run_drafting_feature_command(const std::string& name, const std::string& arguments);
    int drafting_feature_panel_{0};
    std::string symbol_size_{"500mm"};
    int symbol_scroll_{0};
    void draw_object_library(const gui::Rect& canvas);
    bool run_block_library_command(const std::string& name, const std::string& arguments);
    void persist_block_library();
    BlockLibrary block_library_;
    bool library_save_selection_open_{false};
    std::string library_symbol_name_{"My symbol"}, library_symbol_folder_{"My symbols"};
    void draw_plan_check_button(const gui::Rect& button, const gui::Rect& bar);
    void finish_library_drag(double x,double y);
    void place_library_object(const std::string& command,double x,double y);
    std::set<std::string> library_folders_{"Architecture", "Architecture/Doors and windows"};
    float library_scroll_{0};
    std::string library_size_{"900mm"};
    std::string library_drag_command_;
    gui::Vec2 library_drag_start_{};
    gui::Rect library_panel_{};
    std::vector<gsnap::Shape> library_drag_preview_;
    gcm::u64 plan_panel_serial_{~gcm::u64{0}};
    std::vector<PlanFinding> plan_panel_findings_;
    int plan_panel_scroll_{0};
    bool background_context_seed_{false};
    gcm::Lattice2 background_seed_{};
    std::string background_color_{"#74A9CF"}, background_gap_{"10mm"}, background_opacity_{"35"};
    bool                 ctx_menu_open_{false};
    gui::Vec2            ctx_menu_at_{};
    gui::Rect            ctx_menu_bounds_{};
    float                ctx_menu_scroll_{0};
    void                 draw_view_cube(const gui::Rect& canvas);
    void                 draw_gizmo_cube();
    gview::CubeLayout    cube_layout() const;
    void                 rebuild_sketch_segments();
    void                 build_frame_sources();
    void                 resolve_region_probe();

    std::vector<UiInstance>          ui_;
    std::vector<TextVertex>          text_, tris_;
    std::vector<std::vector<gclip::Vertex>> static_label_meshes_;
    gcm::u64                              static_label_mesh_revision_{0};
    bool                                  static_label_mesh_font_loaded_{false};
    std::vector<ImageDraw> images_;
    std::unordered_map<std::string,std::shared_ptr<const gplot::RasterImage>> image_cache_;
    UiLayers                         ui_layers_{};
    std::vector<MeshVertex>          mesh_;
    std::vector<gcm::u64>            mesh_state_;
    gcm::u64                        mesh_version_{0};
    std::vector<LineVertex>          lines_, points_;
    std::size_t                      sketch_lines_{0};
    std::vector<grender::d2::Vertex> lines2d_, points2d_;
    grender::d2::DrawList            draw_dynamic_;
    std::vector<MeshVertex>          static_fills_, previous_static_fills_;
    gcm::u64                         static_fills_version_{0};
    std::vector<grender::d2::TriangleItem> active_fill_triangles_;
    std::vector<MeshVertex>          active_fill_vertices_;
    std::array<double,16>            active_fill_transform_{};
    gcm::u64                         active_fill_palette_revision_{0};
    std::vector<LineVertex>          static_lines_, static_points_;
    gcm::u64                         lifted_key_{0};
    bool   hover_done_{false}, hover_pending_{false};
    bool   pointer_deferred_{false}, pointer_secondary_{false};
    gui::Vec2 frame_mouse_{-1e9f, -1e9f};
    double pointer_down_x_{0}, pointer_down_y_{0};
    bool   region_tap_target(double x, double y, Modeller::Target& t);
    bool   region_probe_pending_{false};
    double probe_x_{0.0}, probe_y_{0.0};
    double hover_x_{0}, hover_y_{0};
    std::vector<ArcInstance>         static_arcs_, arcs_;
    grender::d2::DrawList            draw_;

    double vw_{1440}, vh_{900}, dpr_{2.0};
    bool   ui_wants_mouse_{false};
    gui::Dock dock_;
    Mode    mode_{Mode::Sketch};
    ToolId  active_tool_id_{ToolId::Select};
    std::vector<SketchDoc> sketches_;
    int     active_sketch_{0};
    bool    picking_sketch_plane_{false};
    void    draw_toolbar(gui::Rect& screen);
    void    draw_hatch_style_bar(gui::Rect& screen);
    std::string hatch_style_scale_, hatch_style_angle_;
    void    draw_text_style_bar(gui::Rect& screen);
    void    draw_text_style_colors();
    std::string text_style_height_, text_color_property_, text_style_custom_color_;
    gui::Rect text_color_anchor_{}, text_color_panel_{};
    std::vector<std::string> text_style_fonts_;
    std::size_t text_style_font_count_{0};
    gui::ScrollState text_color_scroll_{};
    void    draw_tablet_toolbar(gui::Rect& screen);
    void    draw_shapes_menu(float row_h);
    void    draw_placement_chip(const gui::Rect& canvas);
    void    draw_mode_switch(const gui::Rect& button, const gui::Rect& bar, bool compact);
    void    draw_canvas_options(const gui::Rect& button, const gui::Rect& row);
    void    draw_discipline_picker(const gui::Rect& r);
    bool    canvas_options_open_{false};
    void    switch_mode(Mode m);
    bool    mode_menu_open_{false};
    void    after_commit();
    double  last_click_time_{-1.0};
    double  last_click_x_{0.0}, last_click_y_{0.0};
    double  now_seconds() const;
    void    draw_dynamic_input(const gui::Rect& canvas);
    void    draw_sketches_panel();
    void    draw_objects_panel();
    int     hover_body_{-1};
    int     sel_body_{-1};
    void    outline_body(std::size_t body, const float color[4]);
    void    draw_settings_panel();
    void    draw_properties_panel();
    void    draw_layer_color_picker();
    std::string layer_color_target_;
    gui::Rect layer_color_anchor_{};
    gui::Rect layer_color_panel_{};
    int layer_color_tab_{-1};
    gdoc::LinetypeTable linetypes_;
    gtext::TextStyleTable text_styles_;
    gdim::ScaleList annotation_scales_;
    double var_pdmode_{-1.0}, var_pdsize_{-1.0};
    gunit::ParameterTable params_;
    double section_nx_{0.0}, section_ny_{0.0}, section_nz_{0.0}, section_d_{0.0};
public:
    void set_section_plane(double nx, double ny, double nz, double d) noexcept {
        section_nx_ = nx; section_ny_ = ny; section_nz_ = nz; section_d_ = d;
    }
    std::vector<gtopo::PmiSet>& model_dimensions() noexcept { return pmi_; }
    const std::vector<gtopo::PmiSet>& model_dimensions() const noexcept { return pmi_; }
    bool sectioned() const noexcept {
        return section_nx_ != 0.0 || section_ny_ != 0.0 || section_nz_ != 0.0;
    }
private:
    std::vector<gtopo::PmiSet> pmi_;
    int     unit_choice_{0};
    int     precision_{3};
    bool    grid_visible_{true};
    bool    navigated_{false};
    bool    layout_loaded_{false};
    FileHook    file_hook_;
    bool        building_frame_{false};
    std::vector<std::pair<FileAsk, std::function<void(std::string)>>> deferred_file_asks_;
    void build_frame_body();
    void request_file(FileAsk kind, std::function<void(std::string)> done);
    bool        text_placing_{false};
    bool        text_entry_{false};
    gcm::Lattice2 text_pos_{};
    std::string text_buffer_;
    double      text_height_{0.25};
    void        draw_text_entry();
    std::string document_path_;
    std::string command_arg_;
    bool        native_menus_{false};
    int         open_menu_{-1};
    bool        tablet_mode_{false};
    int         tablet_popup_{0}, tablet_popup_page_{0};
    bool        shapes_open_{false};
    gui::Rect   shapes_anchor_{}, shapes_menu_rect_{}, placement_chip_rect_{};
    std::vector<std::pair<std::string, gui::Rect>> menu_markers_;
    bool        menu_overflow_open_{false};
    int         menu_overflow_page_{0}, menu_item_page_{0};
    void        draw_menu_bar(const gui::Rect& bar, const gui::Rect& row);
    float       menu_bar_width() const;
    void        draw_header(gui::Rect& screen);
    bool        header_utilities_{false};
    float       toolbar_top_{0};
    void        ask_file(FileAsk kind);
    bool    layout_dirty_{false};
    std::string layout_path() const;
    void    load_layout();
    void    save_layout() const;
    double  default_width_m_{10.0};
public:
    const std::vector<std::string>& log() const noexcept { return log_; }
    void log_line(std::string text) {
        log_.push_back(std::move(text));
        if (log_.size() > 1000) log_.erase(log_.begin(), log_.end() - 1000);
    }
    gcmd::Registry& commands() noexcept { return commands_; }
    const CommandTable& command_table() const noexcept { return command_table_; }
    bool grid_visible() const noexcept { return grid_visible_; }
    const std::string& bar_input() const noexcept { return bar_.input; }
    const std::string& bar_prompt() const noexcept { return bar_.prompt; }
    void command(const std::string& text) { run_command(text); }

    void set_file_hook(FileHook h) { file_hook_ = std::move(h); }
    bool open_file(const std::string& path);
    bool save_file(const std::string& path);
    bool import_file(const std::string& path, bool recenter = true);
    bool reload_xref(gdoc::Xref reference);
    bool export_file(const std::string& path);
    bool load_block_library(const std::string& path);
    bool save_block_library(const std::string& path) const;
    bool save_selection_to_library(const std::string& name, const std::string& folder, gcm::Lattice2 base);
    bool add_drawing_blocks_to_library();
    bool insert_library_block(std::size_t entry, gcm::Lattice2 position);
    const BlockLibrary& block_library() const noexcept { return block_library_; }
    struct PublishSheet {
        std::string drawing;
        std::string layout;
        std::string name;
    };
    const std::vector<PublishSheet>& publish_sheets() const noexcept { return publish_sheets_; }
    bool publish_sheet_set(const std::string& path);
    bool open_plot_preview(bool sheet_set = false);
    bool set_plot_preview_page(std::size_t page);
    std::size_t plot_preview_page() const noexcept { return plot_preview_ ? plot_preview_->page : 0; }
    std::size_t plot_preview_pages() const noexcept { return plot_preview_ ? plot_preview_->document.page_count() : 0; }
    void close_plot_preview();
    bool plot_preview_open() const noexcept { return bool(plot_preview_); }
    bool save_plot_preview(const std::string& path) const;
    using PrintHook = std::function<bool(const std::string&, const std::string&, std::string&)>;
    void set_print_hook(PrintHook hook) { print_hook_ = std::move(hook); }
    bool print_plot(const std::string& printer = {});
    bool save_sheet_set(const std::string& path);
    bool load_sheet_set(const std::string& path);
private:
    std::vector<PublishSheet> publish_sheets_;
    struct PlotPreview {
        std::string path;
        gplot::PdfWriter document;
        std::size_t page{1};
        std::vector<std::string> names;
        std::shared_ptr<const gplot::RasterImage> image;
        double zoom{1}, pan_x{0}, pan_y{0}, toolbar_height{56};
        bool dragging{false};
        ~PlotPreview();
    };
    std::shared_ptr<PlotPreview> plot_preview_;
    PrintHook print_hook_;
    bool prepare_sheet_set(gplot::PdfWriter& pdf);
    void build_plot_preview_frame();
    void emit_ui_commands();
    bool sheet_set_command(const std::string& arg);
public:

    const std::string& document_path() const noexcept { return document_path_; }
    struct FileFilter {
        std::string              label;
        std::vector<std::string> extensions;
    };
    static std::vector<FileFilter> file_filters(FileAsk kind);

    void set_native_menus(bool on) noexcept { native_menus_ = on; }
    bool native_menus() const noexcept { return native_menus_; }
    void set_tablet_mode(bool on) {
        tablet_mode_=on; tablet_popup_=0;
        invalidate_frame_caches();
    }
    int  open_menu() const noexcept { return open_menu_; }
    bool command_history_hidden() const noexcept { return history_hidden_; }
    void set_command_history_hidden(bool hidden);
    void    store_active_sketch();
    void    tessellate_sketch(SketchDoc& sk) const;
    void    lift_sketch(const SketchDoc& sk, const float color[4]);

    struct TabInfo {
        std::string title;
        std::string path;
        bool        active{false};
        bool        dirty{false};
    };
    gcm::u64 document_serial() const noexcept;
    std::string autosave_path() const;
    bool        prepare_suspend();
    bool        autosave_now();
    void        autosave_tick();
    bool        recover_autosave();
    bool     document_dirty() const noexcept { return document_serial() != saved_serial_; }

    const grender::Scene& scene() const noexcept { return scene_; }

    gdoc::Document&       document()       noexcept { return doc_; }
    const gdoc::Document& document() const noexcept { return doc_; }

    gcm::u64 document_revision() const noexcept { return document_revision_; }
    gcm::u64 metadata_encodes() const noexcept { return metadata_encodes_; }
    void     sync_document(bool commit = true, gcm::u32 gesture_space = 0);
    bool     refresh_symbol_legends();
    void     force_document_resync() noexcept { mirrored_ = ~0ull; }
    void     apply_document(gdoc::SpaceId space = gdoc::kNoSpace);
    void     restore_sketches_from_document();
    std::vector<gcm::u8> document_settings_image() const { return metadata_bytes(); }
    bool     restore_document_settings(const std::vector<gcm::u8>& bytes) {
        return restore_metadata(bytes);
    }
    bool     undo_through_document(bool redo);
    bool     document_undo() const noexcept { return document_undo_; }
    void     set_document_undo(bool on) noexcept { document_undo_ = on; }
    std::vector<gsnap::Shape>     shapes_from_document(gcm::u32 shell_space) const;
    std::vector<Canvas::TextItem> notes_from_document(gcm::u32 shell_space) const;
    std::vector<gdoc::BlockRef>   inserts_from_document(gcm::u32 shell_space) const;
    std::vector<gdim::Dimension>  dimensions_from_document(gcm::u32 shell_space) const;
    std::vector<gdoc::AssociativeHatch> hatches_from_document(gcm::u32 shell_space) const;
    std::vector<Canvas::StoredConstraint> constraints_from_document(gcm::u32 shell_space) const;
    std::vector<ggeo::NurbsCurve2<Scalar>> splines_from_document(gcm::u32 shell_space) const;
    gdoc::SpaceId                 document_space_of(gcm::u32 shell_space) const noexcept;
    gcm::u32 active_space_id() const;
    std::size_t sketch_segment_count() const noexcept { return sketch_segs_.size(); }
    bool     document_changed_since(gcm::u64 mark) const noexcept {
        return document_revision_ != mark;
    }
    const std::string& last_change_label() const noexcept { return last_change_label_; }
    gcm::u64 saved_serial() const noexcept { return saved_serial_; }
    int  tab_count() const noexcept { return static_cast<int>(tabs_.size()); }
    int  active_tab() const noexcept { return active_tab_; }
    std::vector<TabInfo> tabs() const;
    const std::string& tab_title() const noexcept { return tab_title_; }
    int  new_tab();
    void switch_tab(int index);
    void close_tab(int index);
    bool document_blank() const noexcept;

    void delete_selected();
    void delete_sketch(int index);
    std::size_t undo_depth() const noexcept { return undo_journal_.size(); }

private:
    struct Edit {
        enum class Kind : gcm::u8 { Canvas, Solid, Sketches, Metadata };
        Kind                   kind{Kind::Canvas};
        int                    steps{1};
        std::vector<SketchDoc> sketches;
        int                    active_sketch{0};
        std::vector<gcm::u8> metadata;
        gcm::u32 space_id{0};
        std::map<gcm::u32, Canvas::SpaceState> spaces;
    };
    std::vector<Edit> undo_journal_, redo_journal_;
    gcm::u64  seen_canvas_{0}, seen_canvas_marks_{0}, seen_solid_{0};
    void      note_edits();
    void      flush_edits() { touch_metadata("flush"); note_edits(); touch_metadata("flush"); }
    void      journal_sketches();
    void      clear_journal();
    bool      hatch_pending_{false};
    std::string pending_args_, pending_hint_;
public:
    struct PageSetupNamed {
        std::string name;
        std::string paper{"A3"};
        bool        landscape{true};
        int         scale_den{0};
        gplot::PlotArea area{gplot::PlotArea::Extents};
    };
private:
    struct TabState {
        explicit TabState(const gcm::WorkingVolume& v) : modeller(v) {}
        Canvas                  canvas;
        gview::Camera           camera;
        gdoc::LayerTable        layers;
        gdoc::Document          doc;
        Modeller                modeller;
        std::vector<SketchDoc>  sketches;
        std::map<gcm::u32, Canvas::SpaceState> space_states;
        gcm::u32 last_space_id{0};
        int                     active_sketch{0};
        Mode                    mode{Mode::Sketch};
        DrawingDiscipline       drawing_discipline{DrawingDiscipline::General};
        std::string             project_type{"general"}, project_location;
        bool                    project_ask_done{false};
        int                     project_ask_step{0};
        ToolId                  tool{ToolId::Select};
        std::string             path;
        std::string             title;
        bool                    navigated{false};
        bool                    last_edit_was_solid{false};
        int                     sel_body{-1};
        Modeller::Target        sel_target;
        std::vector<Modeller::Target> selected_targets;
        std::vector<std::size_t> selected_bodies;
        gcm::u64                saved_serial{0};
        gcm::u64                sketch_edits{0};
        std::vector<Edit>       undo_journal, redo_journal;
        gcm::u64                seen_canvas{0}, seen_canvas_marks{0}, seen_solid{0};
        std::vector<gcm::u8> seen_metadata;
        gcm::u64 metadata_serial{0};
        std::vector<Layout> layouts;
        std::vector<PageSetupNamed> page_setups;
        int active_layout{-1};
        bool in_paper_space{false};
        std::string plot_paper{"A3"};
        bool plot_landscape{true};
        int plot_scale_den{0};
        double plot_dx{0.0}, plot_dy{0.0};
        gdoc::LinetypeTable linetypes;
        gtext::TextStyleTable text_styles;
        gunit::ParameterTable params;
        std::vector<gtopo::PmiSet> pmi;
        gview::ViewTable views;
        gview::ViewHistory view_history;
        gcmd::SysVars vars;
        std::map<std::string, std::vector<gsnap::Shape>> groups;
        double text_height{0.25}, text_width{0.0}, linetype_scale{1.0};
        int precision{3};
        double section_nx{0.0}, section_ny{0.0}, section_nz{0.0}, section_d{0.0};
        bool read_only{false};
        std::unique_ptr<DocumentLock> document_lock;
        std::string document_stamp;
        bool external_change{false};
        std::string lock_holder;
        gfile::Contents recovery_base;
        bool recovery_written{false};
        std::string recovery_id, recovered_from;
        double autosave_at{0.0};
        gcm::u64 autosave_serial{0};
        bool views_need_refresh{false};
    };
    gcm::u64    saved_serial_{0};
    double      linetype_scale_{1.0};
    double      autosave_at_{0.0};
    gcm::u64    autosave_serial_{0};
    gcm::u64    sketch_edits_{0};
    std::vector<TabState> tabs_;
    int         active_tab_{0};
    int         untitled_counter_{1};
    std::string tab_title_;
    void        swap_with(TabState& t);
    void        invalidate_frame_caches();
    void        reset_document();
    void        draw_tab_bar(gui::Rect& screen);
    void        draw_document_tabs(gui::Rect& cursor, float reserve);
    void        draw_document_pickers(const gui::Rect& area, const gui::Rect& row);
    float       document_pickers_width() const;
    int         header_menu_{0}, header_menu_page_{0};
    void reveal_layout_tab(int index);
    gplot::PaperSize layout_paper(const Layout* layout) const;
    std::size_t layout_tab_start_{0};
    bool layout_frame_pending_{false};
    void        draw_drawing_panel();
    void        draw_osnap_ring(const gui::Rect& canvas);
    gui::RingState osnap_ring_{};
    gcm::u64 dims_synced_{0};
    gfile::Contents recovery_base_;
    bool            recovery_written_{false};
    std::string     recovery_id_, recovered_from_;
    std::string     recoverable_path() const;
    bool            read_only_{false};
    std::unique_ptr<DocumentLock> document_lock_;
    std::string document_stamp_;
    bool external_change_{false};
    int viewport_drag_index_{-1}, viewport_drag_corner_{-1};
    gplot::Viewport viewport_drag_original_;
    gplot::PaperPoint viewport_drag_start_;
    bool begin_viewport_drag(double x,double y);
    void move_viewport_drag(double x,double y);
    void cancel_viewport_drag();
    double lock_check_time_{0};
    void refresh_document_lock();
    std::string     lock_holder_;
    bool            views_need_refresh_{false};
    std::unique_ptr<gcmd::Interpreter, LispDelete>  lisp_;
    std::unique_ptr<gcmd::ScriptRunner, ScriptDelete> script_;
    void ensure_lisp();
    void sync_lisp_vars(bool into_lisp);
    void run_script(gcmd::Script s);
    void report_script(const gcmd::RunReport& r);
    bool                          in_paper_space_{false};
    std::map<gcm::u32, Canvas::SpaceState> space_states_;
    gcm::u32 last_space_id_{0};
    bool restoring_history_{false}, sketch_transaction_{false};
    void ensure_space_ids();
    void load_active_space();
    Canvas canvas_for_space(gcm::u32 id) const;
    void append_sheet_annotations(const Layout& layout, gplot::PlotJob& job) const;
    bool activate_space(gcm::u32 id);
    void enter_paper_space();
    void leave_paper_space();
    gfile::Contents document_contents() const;
    bool load_xref(gdoc::Xref reference, std::vector<std::string>& source_chain);
    void resolve_pending_underlays(const std::string& document_path);
    bool load_contents(const gfile::Contents& c, const std::string& path,
                       bool read_only = false, std::string lock_holder = {});
    bool prepare_template(gfile::Contents& c);
    std::vector<gcm::u8> metadata_bytes(bool lightweight = false) const;
    bool restore_metadata(const std::vector<gcm::u8>& bytes, bool validate_only = false, const gfile::Contents* document = nullptr);
    mutable std::vector<gcm::u8> cached_block_state_;
    mutable gcm::u64 cached_block_revision_{~0ull}, cached_block_generation_{0};
    std::vector<gcm::u8> seen_lightweight_metadata_;
    gcm::u64 seen_block_generation_{~0ull};
    std::vector<gcm::u8> seen_metadata_;
    gcm::u64 metadata_serial_{0};

    gcm::u64 metadata_gate_{1};
    mutable gcm::u64 metadata_encodes_{0};
    mutable std::vector<gcm::u8> settings_image_;
    mutable gcm::u64             settings_image_gate_{~0ull};
    const std::vector<gcm::u8>&  settings_image() const;
    const char* metadata_gate_reason_{"start"};
    gcm::u64 seen_metadata_gate_{0};
    int command_depth_{0};
    gcm::u32 command_origin_space_{0};

    std::size_t text_editing_{static_cast<std::size_t>(-1)};
    double text_width_{0.0};
    std::vector<std::string> osnap_ring_items_;
    int         plot_scale_den_{0};   // 0 = fit to paper
    int display_mode_{2};               // 0 wire, 1 flat, 2 smooth
    std::vector<LineVertex> wire_lines_;
    std::vector<std::string> expanded_layers_;
    std::string sel_layer_hint_;
    int  sel_sketch_hint_{-1};
    bool hint_scroll_layers_{false};
    bool hint_scroll_sketches_{false};
    int  settings_tab_{0};              // 0 General, 1 Graphics
    gui::ScrollState settings_scroll_{};
    int              settings_scroll_page_{-1};
    bool             config_loaded_{false};
    std::string      ai_add_error_;
    int  smooth_angle_choice_{1};
    int  curve_quality_{1};
    int  msaa_choice_{0};
    uint32_t graphics_msaa_{1};
    bool        settings_open_{false};
    void        draw_pull_arrow(const gui::Rect& canvas);

    struct PointPick {
        int wanted{0};
        bool finish_on_enter{false};
        std::string prompt;
        std::vector<gcm::Lattice2> pts;
        std::function<void(const std::vector<gcm::Lattice2>&)> done;
        bool active() const noexcept { return wanted > 0; }
    };
    PointPick   pick_;
    void        begin_pick(int n, std::string prompt, std::function<void(const std::vector<gcm::Lattice2>&)> done);
    bool        run_extra_command(const std::string& n);
    std::vector<std::size_t> selected_indices() const;
    gcm::Lattice2 last_band_lo_{}, last_band_hi_{};
    bool        have_last_band_{false};
    std::map<std::string, std::vector<gsnap::Shape>> groups_;
    std::string last_script_;
    bool        wireframe_{false};
    std::string plot_paper_{"A3"};
    bool        plot_landscape_{true};

    std::vector<Layout> layouts_;
    int                 active_layout_{-1};   // -1 while none has been made
    void layout_load(int index);
    void layout_store();
    std::size_t refresh_views(Layout& l);
    std::vector<PageSetupNamed> page_setups_;
    Layout* active_layout() noexcept {
        return active_layout_ >= 0 && active_layout_ < int(layouts_.size())
                   ? &layouts_[std::size_t(active_layout_)]
                   : nullptr;
    }
    const Layout* active_layout() const noexcept {
        return active_layout_ >= 0 && std::size_t(active_layout_) < layouts_.size()
            ? &layouts_[std::size_t(active_layout_)] : nullptr;
    }
    void        select_target(const Modeller::Target& t);
    void        draw_edge_value(const gui::Rect& canvas);
    void        draw_operation_value(const gui::Rect& canvas,
                                     const std::string& label, const std::string& placeholder,
                                     bool edge_slider = false);
    void        draw_edge_slider(const gui::Rect& card, float top, float height);
    bool        round_slider_drag_{false};
    double      round_slider_value_{0.0};
    double      round_slider_last_preview_{-1.0};
    ToolId      tool_modes_open_{ToolId::Select};
    int         sel_feature_body_{-1};
    int         sel_feature_{-1};
    std::string feature_typed_;
public:
    bool        set_feature_value(int body, int feature, double value);
    bool        remove_feature(int body, int feature);
    void        select_feature(int body, int feature) { sel_feature_body_ = body; sel_feature_ = feature; feature_typed_.clear(); }
    int         selected_feature_body() const noexcept { return sel_feature_body_; }
    int         selected_feature() const noexcept { return sel_feature_; }
    bool        round_edge(double r);
    bool        bevel_edge(double d);
private:
};

}
