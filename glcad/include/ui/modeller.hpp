#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/units.hpp"
#include "ui/frame_types.hpp"
#include "engine/hatch.hpp"
#include "geometry/mate.hpp"
#include "geometry/body.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/profile.hpp"
#include "geometry/sketch.hpp"
#include "geometry/triangulate.hpp"
#include "render/camera.hpp"
#include "ui/boolean_preview.hpp"

namespace gcapp {

class Modeller {
public:
    enum class Op : gcm::u8 {
        None, Extrude, Revolve, Union, Subtract, Intersect, FilletEdge, ChamferEdge, Loft,
        Mate,
        Sweep, ShellBody, Split, Section, MeasureDistance, PatternBody, MirrorBody
    };

    enum class Stage : gcm::u8 {
        Idle, PickTarget, Placed, Dragging, PickAxis, PickSecond, TypeValue
    };

    struct SketchSource {
        gtopo::SketchPlane                 plane;
        const std::vector<gdoc::Segment2>* segments{nullptr};
        gcm::u64                           version{0};
        bool                               visible{true};
    };

    struct Target {
        bool        valid{false};
        std::vector<gcm::u32> surface_faces;
        gview::Point3 pick_point;
        bool has_pick_point{false};
        bool        on_body{false};
        std::size_t body{0};
        gcm::u32    face{0};

        gtopo::Profile              profile;
        ggeo::d3::Frame<gcm::F64>   frame;
        gview::Point3               centroid;
        gview::Vec3                 normal;
        int                         source{-1};
        bool                        from_face{false};
        std::vector<std::vector<gview::Point3>> outline;
    };

    explicit Modeller(const gcm::WorkingVolume& vol);

    void  arm(Op op);
    void  cancel();
    Op    op() const noexcept { return op_; }
    Stage stage() const noexcept { return stage_; }
    const char* prompt() const noexcept;

    void set_sketches(std::vector<SketchSource> sources) {
        sources_ = std::move(sources);
        loop_cache_.resize(sources_.size());
        consumed_cache_.resize(sources_.size());
    }
    bool sketch_consumed(int source) const;
    void set_sketch_visible(int source, bool visible) { sources_[source].visible=visible; }
    const gdoc::BoundaryResult& loops_of(int si) const;
    const std::vector<SketchSource>& sketches() const noexcept { return sources_; }

    void hover(const gview::Camera& cam, double px, double py);
    bool press(const gview::Camera& cam, double px, double py);
    void drag(const gview::Camera& cam, double px, double py);
    void hover(const gview::Camera& cam, double px, double py,
               const std::vector<gdoc::Segment2>& sketch) {
        set_sketches({{gtopo::SketchPlane::world_xy(), &sketch}});
        hover(cam, px, py);
    }
    bool press(const gview::Camera& cam, double px, double py,
               const std::vector<gdoc::Segment2>& sketch) {
        set_sketches({{gtopo::SketchPlane::world_xy(), &sketch}});
        return press(cam, px, py);
    }
    bool release();

    void        type(char c);
    void        backspace();
    void        set_typed(std::string text);
    bool        value_preview_valid() const noexcept { return typed_preview_ || !value_preview_.empty(); }
    const std::string& value_error() const noexcept { return value_error_; }
    bool        commit_typed();
    bool        commit_distance(double distance);
    double      through_distance() const;
    const std::string& typed() const noexcept { return typed_; }
    double      distance() const noexcept { return distance_; }

    const Target& hovered() const noexcept { return hover_; }
    const Target& target() const noexcept { return target_; }

    struct Preview {
        bool   active{false};
        bool   cut{false};
        bool   on_body{false};
    };
    const Preview& preview() const noexcept { return preview_; }
    gbool::PreviewState gpu_preview() const;
    void preview_triangles(const float color[4], std::vector<MeshVertex>& out);
    std::pair<std::size_t, std::size_t> preview_range() const noexcept {
        return {preview_range_first_, preview_range_count_};
    }

    bool target_at(const gview::Camera& cam, double px, double py, Target& out,
                   bool prefer_result = false) const {
        return find_target(cam, px, py, out, prefer_result);
    }
    bool target_at(const gview::Camera& cam, double px, double py,
                   const std::vector<gdoc::Segment2>& sketch, Target& out) {
        set_sketches({{gtopo::SketchPlane::world_xy(), &sketch}});
        return find_target(cam, px, py, out);
    }
    std::string describe(std::size_t body) const;
    bool axis_hovered() const noexcept { return axis_valid_; }
    bool edge_hovered() const noexcept { return edge_valid_; }
    void clear_target_hover() noexcept { hover_=Target{}; }

    bool edge_pick_point(gview::Point3& out) const { if (!edge_valid_) return false; out=edge_pick_point_; return true; }
    bool hover_edge(const gview::Camera& cam, double px, double py) { return pick_edge(cam, px, py); }
    void clear_edge_hover() noexcept { if (stage_ != Stage::TypeValue) edge_valid_ = false; }
    void set_edge_pick_px(double px) noexcept { edge_pick_px_ = px; }
    void begin_edge();
    struct EdgeSelection {
        std::size_t body{0};
        gcm::u32 halfedge{0};
        gview::Point3 a, b, pick;
    };
    bool toggle_edge_at(const gview::Camera& cam, double px, double py);
    bool select_context_edge_at(const gview::Camera& cam,double px,double py,bool& preserved);
    const std::vector<EdgeSelection>& selected_edges() const noexcept { return selected_edges_; }
    bool edge_kept() const noexcept { return stage_ == Stage::TypeValue && edge_valid_; }
    void set_edge_operation(Op op) { if(edge_kept() && (op==Op::FilletEdge || op==Op::ChamferEdge)) {op_=op;clear_preview();typed_.clear();} }
    bool apply_edge(Op op, double value);
    bool edge_applied() const noexcept { return edge_applied_; }
    double edge_value() const noexcept { return edge_value_; }
    std::size_t edge_body_index() const noexcept { return edge_body_; }
    double edge_length() const;
    bool edge_midpoint(gview::Point3& out) const;
    std::vector<std::pair<gview::Point3,gview::Point3>> edge_highlight() const;
    void finish_edge();

    struct SectionOut {
        bool   valid{false};
        double ox, oy, oz, nx, ny, nz;
        std::vector<double> segments;
    };
    SectionOut take_section() { SectionOut o = section_; section_ = SectionOut{}; return o; }
    bool measured() const noexcept { return measure_valid_; }
    double measured_distance() const noexcept { return measure_value_; }
    void place(Op op, const Target& t);
    void fill(const Target& t, const float color[4], std::vector<MeshVertex>& out) const;
    bool erase(std::size_t body);
    bool erase_many(std::vector<std::size_t> bodies);
    void preselect_body(Op op, std::size_t body);
    void add_body(gtopo::Body b, const char* what);
    void set_bodies(std::vector<gtopo::Body> bodies);
    std::vector<std::vector<gtopo::FaceId>> merge_loaded_faces();

    std::size_t lift_all(int source, double height);
    std::size_t walls(int source, double thickness, double height,
                      const std::vector<gdoc::Segment2>* centerlines = nullptr,
                      const std::vector<gdoc::Segment2>* material = nullptr);

    const std::vector<gtopo::Body>& bodies() const noexcept { return bodies_; }
    bool face_is_planar(std::size_t body, gcm::u32 face) const;
    void set_mate_kind(gtopo::MateKind k, double value = 0.0) noexcept {
        mate_kind_ = k;
        mate_value_ = value;
    }
    gtopo::MateKind mate_kind() const noexcept { return mate_kind_; }

    // Stable names, never indices: indices renumber on the next edit.
    struct StoredMate {
        gtopo::MateKind kind{gtopo::MateKind::Coincident};
        gcm::u32 body_a{0}, face_a{0};
        gcm::u32 body_b{0}, face_b{0};
        double   value{0.0};
        bool     suppressed{false};
    };
    const std::vector<StoredMate>& mates() const noexcept { return mates_; }
    void load_mates(std::vector<StoredMate> m) { mates_ = std::move(m); }
    std::size_t prune_mates();
    std::size_t resolve_mates();
    bool replace_body(std::size_t index, gtopo::Body b) {
        if (index >= bodies_.size()) return false;
        snapshot();
        sync_face_ids();
        if (index < face_ids_.size()) {
            std::vector<gcm::u32> survivors(b.face_count(), gtopo::kInvalid);
            const gcm::u32 kept = std::min(bodies_[index].face_count(), b.face_count());
            for (gcm::u32 f = 0; f < kept; ++f) survivors[f] = f;
            face_ids_[index].follow(bodies_[index], b, survivors);
        }
        bodies_[index] = std::move(b);
        invalidate_mesh();
        return true;
    }

    gcm::u32 body_name(std::size_t body) const {
        sync_face_ids();
        return body < body_key_.size() ? body_key_[body] : 0;
    }
    std::size_t body_of_name(gcm::u32 key) const {
        sync_face_ids();
        for (std::size_t i = 0; i < body_key_.size(); ++i)
            if (body_key_[i] == key) return i;
        return static_cast<std::size_t>(-1);
    }

    gcm::u32 face_name(std::size_t body, gcm::u32 face) const {
        sync_face_ids();
        return body < face_ids_.size() ? face_ids_[body].face_id(face) : 0;
    }
    gcm::u32 face_of_name(std::size_t body, gcm::u32 name) const {
        sync_face_ids();
        return body < face_ids_.size() ? face_ids_[body].face_of(name) : gtopo::kInvalid;
    }
    const std::vector<gtopo::IdMap>& face_ids() const noexcept {
        sync_face_ids();
        return face_ids_;
    }

private:
    void sync_face_ids() const {
        if (face_ids_.size() != bodies_.size()) face_ids_.resize(bodies_.size());
        for (std::size_t i = 0; i < bodies_.size(); ++i)
            if (face_ids_[i].size() != bodies_[i].face_count())
                face_ids_[i].seed(bodies_[i]);
        if (body_key_.size() < bodies_.size()) body_key_.resize(bodies_.size(), 0);
        if (body_key_.size() > bodies_.size()) body_key_.resize(bodies_.size());
        for (auto& k : body_key_) if (k == 0) k = ++last_body_key_;
    }
public:

    struct Material {
        std::string name{"Default"};
        float r{0.62f}, g{0.66f}, b{0.72f}, a{1.0f};
        float roughness{0.6f};
        float metallic{0.0f};
        double density{0.0};        // kg/m3, for mass properties; 0 = unknown
    };
    const std::vector<Material>& materials() const noexcept { return materials_; }
    gcm::u32 add_material(Material m);
    gcm::u32 material_of(std::size_t body) const {
        return body < body_material_.size() ? body_material_[body] : 0;
    }
    bool set_material(std::size_t body, std::string_view name);

    struct Feature {
        std::string label;
        int         sketch{-1}; // -1 if none
        Op                          op{Op::None};
        double                      value{0.0};
        bool                        regenerable{false};
        bool                        on_body{false};
        gtopo::Profile              profile;
        ggeo::d3::Frame<gcm::F64>   frame;
        bool                        has_seed{false};
        double                      seed_u{0.0}, seed_v{0.0};   // sketch plane coords
        double                      axis_x{0.0}, axis_y{0.0}, axis_dx{0.0}, axis_dy{1.0};
        gview::Point3               anchor{};
        bool                        suppressed{false};
    };
    const std::vector<Feature>& history(std::size_t body) const {
        static const std::vector<Feature> none;
        return body < history_.size() ? history_[body] : none;
    }
    void set_history(std::vector<std::vector<Feature>> h) {
        history_ = std::move(h);
        history_.resize(bodies_.size());
        consumed_cache_.clear();
    }
    const std::vector<std::vector<Feature>>& histories() const noexcept { return history_; }

    struct Metadata {
        std::vector<Material> materials;
        std::vector<std::string> body_layers;
        std::vector<gcm::u32> body_material, body_keys;
        gcm::u32 last_body_key{0};
        std::vector<std::vector<gcm::u32>> face_ids;
        std::vector<gcm::u32> next_face_ids;
        std::vector<StoredMate> mates;
        std::vector<std::vector<gcm::u8>> suppressed;
        std::vector<std::size_t> rollback;
    };
    Metadata metadata() const;
    bool load_metadata(const Metadata& m);

    void set_current_layer(std::string name) { current_layer_ = std::move(name); }
    void set_display_unit(gunit::Length u) noexcept { display_unit_ = u; }
    void set_smoothing(bool on, double angle_deg) noexcept {
        if (on != smooth_shading_ || angle_deg != smooth_angle_deg_) {
            smooth_shading_ = on;
            smooth_angle_deg_ = angle_deg;
            invalidate_mesh();
        }
    }
    bool   smoothing_on() const noexcept { return smooth_shading_; }
    double smoothing_angle() const noexcept { return smooth_angle_deg_; }
    void set_hidden_layers(std::vector<std::string> names) { if (names != hidden_layers_) { hidden_layers_ = std::move(names); invalidate_mesh(); } }
    const std::string& body_layer(std::size_t body) const {
        static const std::string zero = "0";
        return body < body_layer_.size() ? body_layer_[body] : zero;
    }
    void set_body_layer(std::size_t body, std::string name) {
        if (body_layer_.size() < bodies_.size()) body_layer_.resize(bodies_.size(), current_layer_);
        if (body < body_layer_.size()) { body_layer_[body] = std::move(name); invalidate_mesh(); }
    }
    bool body_hidden(std::size_t body) const {
        const std::string& l = body_layer(body);
        for (const auto& h : hidden_layers_) if (h == l) return true;
        return false;
    }

    bool set_feature_value(std::size_t body, std::size_t feature, double value);
    bool remove_feature(std::size_t body, std::size_t feature);
    std::size_t regenerate_from_sketch(int sketch);

    bool rollback(std::size_t body, std::size_t through, std::string& why);
    std::size_t rolled_back_to(std::size_t body) const {
        return body < rollback_.size() ? rollback_[body] : kAllFeatures;
    }
    bool suppress(std::size_t body, std::size_t feature, bool on, std::string& why);
    bool reorder(std::size_t body, std::size_t from, std::size_t to, std::string& why);
    static constexpr std::size_t kAllFeatures = static_cast<std::size_t>(-1);
    bool rebuild(std::size_t body, std::string& why);
    const std::string& last_rebuild_message() const noexcept { return rebuild_message_; }

    bool arrow_points(const gview::Camera& cam, gview::Point3& base, gview::Point3& tip) const {
        if (stage_ != Stage::Placed && stage_ != Stage::Dragging) return false;
        arrow(cam, base, tip);
        return true;
    }
    bool arrow_under(const gview::Camera& cam, double px, double py) const {
        return stage_ == Stage::Placed && arrow_hit(cam, px, py);
    }
    gcm::u64 edit_serial() const noexcept { return edit_serial_; }
    bool undo();
    bool redo();
    bool can_undo() const noexcept { return !undo_.empty(); }
    void clear();
    const std::string& message() const noexcept { return message_; }
    bool take_committed() noexcept { const bool c = committed_; committed_ = false; return c; }

    bool mesh_dirty() const noexcept { return mesh_dirty_; }
    gcm::u64 mesh_version() const noexcept { return mesh_version_; }
    const std::vector<MeshVertex>& mesh();

    struct MeshRange {
        std::size_t body{0};
        gcm::u32    key{0};
        std::size_t first{0}, count{0};
        bool        translucent{false};
    };
    const std::vector<MeshRange>& mesh_ranges() const noexcept { return mesh_ranges_; }
    void wireframe(std::vector<float>& out) const;
    void overlay(const gview::Camera& cam, const float highlight[4],
                 const float accent[4], std::vector<LineVertex>& lines) const;
    bool gizmo_label(const gview::Camera& cam, double& px, double& py,
                     std::string& text) const;

private:
    bool find_target(const gview::Camera& cam, double px, double py, Target& out,
                     bool prefer_result = false) const;
    bool target_from_face(std::size_t body, gcm::u32 face, Target& out) const;
    bool target_from_region(int source, double u, double v, Target& out) const;
    bool target_from_boundary(int source, const gdoc::BoundaryResult& region, Target& out) const;
    bool region_used(const Target& target) const;
    std::size_t extrusion_cap_feature(const Target& target) const;
    bool face_region_at(std::size_t body, gcm::u32 face,
                               const gview::Point3& hit, Target& out) const;
    bool pick_face(const gview::Camera& cam, double px, double py,
                   std::size_t& body, gcm::u32& face, double& t) const;
    bool face_under(const gview::Point3& p, const gtopo::SketchPlane& plane,
                    std::size_t& body, gcm::u32& face) const;
    bool pick_axis(const gview::Camera& cam, double px, double py);
    bool pick_edge(const gview::Camera& cam, double px, double py);
    void commit_revolve();
    void commit_combine(std::size_t second);
    void commit_edge(double value, bool replace_applied = false);
    void commit_loft(const Target& second);
    void commit_mate(const Target& second);
    void commit_sweep(int source, const gdoc::Segment2& from);
    void commit_shell(double thickness);
    void commit_split(const Target& plane_of);
    void commit_section(const Target& plane_of);
    void commit_pattern(int count, double spacing);
    void commit_mirror(const Target& plane_of);
    gtopo::Body transformed(const gtopo::Body& b, bool reflect,
                            const std::function<void(double&, double&, double&)>& map) const;
public:
    bool plane_of(const Target& t, double& ox, double& oy, double& oz,
                  double& nx, double& ny, double& nz) const;
private:
    bool pick_any_line(const gview::Camera& cam, double px, double py, int& source,
                       gdoc::Segment2& seg) const;
    bool hit_point(const gview::Camera& cam, double px, double py, gview::Point3& out) const;

    gtopo::Body make_body(double distance) const;
    void        commit(double distance);
    void        snapshot();
    void clear_preview() noexcept {
        preview_ = {};
        typed_preview_ = false;
        value_preview_.clear();
        value_error_.clear();
        preview_range_first_ = preview_range_count_ = 0;
        mesh_dirty_ = true;
    }
    void invalidate_mesh() noexcept { clear_preview(); tri_cache_.clear(); selection_cache_.clear(); volume_cache_.clear(); consumed_cache_.clear(); }
    const gtopo::TriMesh& tri(std::size_t body) const;

    void arrow(const gview::Camera& cam, gview::Point3& base, gview::Point3& tip) const;
    bool arrow_hit(const gview::Camera& cam, double px, double py) const;
    double axis_param(const gview::Camera& cam, double px, double py) const;

    gcm::WorkingVolume        vol_;
    bool                     typed_preview_{false};
    struct BodyDraft {
        std::size_t body{0};
        gtopo::Body shape;
    };
    std::vector<BodyDraft>   value_preview_;
    bool edge_drafts(Op op, double value, bool replace_applied,
                     std::vector<BodyDraft>& out, std::string& error) const;
    const gtopo::Body& displayed_body(std::size_t body) const;
    bool                     value_preview_replaces_{true};
    std::string              value_error_;
    std::vector<gtopo::Body>  bodies_;
    mutable std::vector<gtopo::IdMap> face_ids_;
    mutable std::vector<gcm::u32> body_key_;
    mutable gcm::u32 last_body_key_{0};
    std::vector<StoredMate> mates_;
    std::vector<gcm::u32> body_material_;
    std::vector<Material> materials_{Material{}};
    std::vector<std::size_t> rollback_;
    gtopo::MateKind           mate_kind_{gtopo::MateKind::Coincident};
    double                    mate_value_{0.0};
    std::vector<std::vector<Feature>> history_;
    struct Snap {
        std::vector<gtopo::Body>          bodies;
        std::vector<std::vector<Feature>> history;
        std::vector<std::string>          layers;
        Metadata metadata;
    };
    std::vector<std::string> body_layer_;
    std::string              current_layer_{"0"};
    std::vector<std::string> hidden_layers_;
    std::vector<Snap> undo_, redo_;
    std::string rebuild_message_;
    bool replay(const std::vector<Feature>& feats, gtopo::Body& out, std::string& why) const;
    gtopo::Body body_for(const gtopo::Profile& profile, const ggeo::d3::Frame<gcm::F64>& frame, double distance) const;
    Preview preview_;
    std::size_t preview_range_first_{0}, preview_range_count_{0};
    void note(std::size_t body, std::string label, int sketch = -1) {
        if (history_.size() < bodies_.size()) history_.resize(bodies_.size());
        if (body_layer_.size() < bodies_.size()) body_layer_.resize(bodies_.size(), current_layer_);
        if (body < history_.size()) history_[body].push_back({std::move(label), sketch});
    }
    gcm::u64 edit_serial_{0};

    Op      op_{Op::None};
    Stage   stage_{Stage::Idle};
    Target  hover_, target_;
    std::vector<SketchSource> sources_;
    bool          axis_valid_{false};
    gview::Point3 axis_a_, axis_b_;
    std::size_t   first_body_{0};
    struct LoopCache {
        const void*            segments{nullptr};
        gcm::u64             version{~0ull};
        std::size_t          count{0};
        gdoc::BoundaryResult loops;
        bool                 filled{false};
    };
    mutable std::vector<LoopCache> loop_cache_;
    struct ConsumedCache {
        const void* segments{nullptr};
        gcm::u64 version{0};
        std::size_t count{0};
        std::array<double,12> plane{};
        bool filled{false}, consumed{false};
    };
    mutable std::vector<ConsumedCache> consumed_cache_;
    bool sketch_consumed_uncached(int source) const;

    bool          edge_valid_{false};
    std::vector<EdgeSelection> selected_edges_;
    std::size_t   edge_body_{0};
    gcm::u32      edge_he_{0};
    gview::Point3 edge_a_, edge_b_;
    bool          edge_applied_{false};
    double        edge_pick_px_{10.0};
    double        edge_value_{0.0};
    SectionOut    section_;
    bool          measure_valid_{false};
    bool          measure_first_{false};
    gview::Point3 measure_a_, measure_b_;
    double        measure_value_{0.0};
    bool          path_valid_{false};
    int           path_source_{-1};
    gdoc::Segment2 path_seg_{};
    bool          committed_{false};
    double  distance_{0.0};
    double  grab_offset_{0.0};
    std::string typed_;
    gunit::Length display_unit_{gunit::Length::Metre};
    bool   smooth_shading_{true};
    double smooth_angle_deg_{30.0};
    std::string message_;
    std::string combine_note_;

    struct SelectionTopology {
        std::vector<gcm::u32> group_of_face;
        std::vector<std::vector<gcm::u32>> groups;
    };
    const SelectionTopology& selection_topology(std::size_t body) const;
    mutable std::vector<SelectionTopology> selection_cache_;
    gview::Point3 edge_pick_point_;
    mutable std::vector<gtopo::TriMesh> tri_cache_;
    mutable std::vector<std::optional<double>> volume_cache_;
    std::vector<MeshVertex>             mesh_;
    std::vector<MeshRange>              mesh_ranges_;
    bool                                mesh_dirty_{true};
    gcm::u64                            mesh_version_{0};
};

}
