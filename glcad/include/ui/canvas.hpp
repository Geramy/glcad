#pragma once
#include "ui/drafting_bounds.hpp"

#include <cmath>
#include "engine/layer.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include "render/stroke.hpp"
#include <string>
#include <vector>

#include "engine/coordinate.hpp"
#include "engine/block.hpp"
#include "engine/hatch.hpp"
#include "engine/underlay.hpp"
#include "engine/linetype.hpp"
#include "geometry/edit.hpp"
#include "geometry/region.hpp"
#include "geometry/dimension.hpp"
#include "geometry/d2.hpp"
#include "geometry/nurbs.hpp"
#include "render/drawlist.hpp"
#include "render/mtext.hpp"
#include "render/registry.hpp"
#include "geometry/snap.hpp"
#include "geometry/system.hpp"

namespace gcapp {

using Scalar = gcm::F64;
using Point  = ggeo::d2::Point<Scalar>;
using Vec    = ggeo::d2::Vec<Scalar>;
using Lattice = ggeo::d2::LatticePoint;

enum class Tool {
    Select, Line, Rect, Circle, Arc, Polyline,
    Ellipse, Point, Ray, Xline, Spline, Multiline,
    Move, Copy, Rotate, Mirror, Offset, Trim, Extend, Fillet, Chamfer,
    ArrayRect, ArrayPolar,
    DimLinear, DimAligned, DimAngular, DimRadial, DimDiameter,
};

bool tool_uses_selection(Tool t) noexcept;
bool tool_is_dimension(Tool t) noexcept;

const char* tool_name(Tool t) noexcept;

struct Polar {
    bool   enabled{true};
    bool   ortho{false};
    double increment_deg{45.0};
    double tolerance_deg{4.0};
    std::vector<double> extra;

    std::vector<double> angles() const;
};

struct Track {
    bool   active{false};
    double angle_deg{0.0};
    double length{0.0};           // metres
    double gx{0.0}, gy{0.0};      // world
};

struct Snap {
    bool        valid{false};
    Lattice     point{};
    const char* kind{"-"};
};

class View {
public:
    void   set_viewport(double w, double h) noexcept { vw_ = w; vh_ = h; }
    void   set_mapping(double ox, double oy, double scale) noexcept {
        ox_ = ox; oy_ = oy; if (scale > 0.0) scale_ = scale;
    }
    void   pan(double dx_px, double dy_px) noexcept;
    void   zoom_at(double px, double py, double factor) noexcept;
    void   world_of(double px, double py, double& wx, double& wy) const noexcept;
    void   screen_of(double wx, double wy, double& px, double& py) const noexcept;
    double scale() const noexcept { return scale_; }
    double ox() const noexcept { return ox_; }
    double oy() const noexcept { return oy_; }
    double vw() const noexcept { return vw_; }
    double vh() const noexcept { return vh_; }

private:
    double ox_{0.0}, oy_{0.0};
    double scale_{40.0};
    double vw_{1440.0}, vh_{900.0};
};

class Canvas {
public:
    Canvas();

    View&       view() noexcept { return view_; }
    const View& view() const noexcept { return view_; }

    void set_tool(Tool t) noexcept;
    Tool tool() const noexcept { return tool_; }

    void hover(double px, double py);
    Polar&       polar() noexcept { return polar_; }
    const Polar& polar() const noexcept { return polar_; }
    Track        track() const noexcept { return track_; }
    void         toggle_ortho() noexcept { polar_.ortho = !polar_.ortho; }
    void         toggle_polar() noexcept { polar_.enabled = !polar_.enabled; }
    void         cycle_polar_increment() noexcept;
    void click(double px, double py, bool additive = false);
    void cancel() noexcept;
    void undo();
    void redo();
    void clear();
    void load_shapes(std::vector<gsnap::Shape> shapes, bool clear_history = true);
    void modify_shapes(const std::function<void(std::vector<gsnap::Shape>&)>& edit) {
        mark();
        edit(shapes_);
        gregion::reconcile_joints(shapes_);
        update_door_openings(undo_stack_.back().shapes);
        assign_keys();
        selection_.clear();
        align_dirty_ = true;
    }
    void assign_keys() noexcept {
        for (auto& s : shapes_) if (s.key == 0) s.key = ++last_key_;
    }
    std::size_t index_of_key(gcm::u32 key) const noexcept {
        if (key == 0) return npos;
        for (std::size_t i = 0; i < shapes_.size(); ++i)
            if (shapes_[i].key == key) return i;
        return npos;
    }
    std::vector<gsnap::Shape> boundary_at_world(gcm::Lattice2 seed) const;
    std::vector<gdoc::Segment2> boundary_segments() const;
    std::vector<gsnap::Shape> hatch_boundary(const gdoc::AssociativeHatch& hatch) const;

    struct StoredConstraint {
        gsolve::Kind kind{gsolve::Kind::Coincident};
        double       value{0.0};
        gcm::u32     a{0}, b{0};        // shape keys; b is 0 for a unary one
        bool         driving{true};
    };
    const std::vector<StoredConstraint>& constraints() const noexcept {
        return constraints_;
    }
    bool resolve_constraints();
    std::size_t prune_constraints();
    void load_constraints(std::vector<StoredConstraint> c) {
        constraints_ = std::move(c);
    }

    gcm::u32 begin_polyline() noexcept {
        poly_run_ = ++last_poly_;
        poly_next_ = 0;
        return poly_run_;
    }
    void end_polyline() noexcept { poly_run_ = 0; poly_next_ = 0; }
    void refresh_dimensions();
    gcm::u32 next_polyline_id() noexcept { return ++last_poly_; }
    bool revision_cloud(Lattice a, Lattice b, double chord = 0, gcm::u32 replace = 0);
    bool is_revision_cloud(std::size_t index) const;
    bool is_region(std::size_t index) const noexcept { return index < shapes_.size() && shapes_[index].region && shapes_[index].poly; }
    gregion::Region region_at(std::size_t index) const;
    bool create_region(const std::vector<gsnap::Shape>& boundaries, const std::vector<std::size_t>& replace = {});
    bool combine_regions(gregion::Operation operation);
private:
    void expand_region_selection(std::vector<std::size_t>& which) const;
    void store_region(const gregion::Region& region, const std::vector<std::size_t>& replace,
                      const gdoc::Attributes& attributes, const std::string& layer, gcm::u32 identity = 0);
public:
    bool building_polyline() const noexcept { return poly_run_ != 0; }
    gcm::u32 stamp_polyline(std::vector<gsnap::Shape>& sh, std::size_t from) {
        const gcm::u32 id = ++last_poly_;
        gcm::u32 k = 0;
        for (std::size_t i = from; i < sh.size(); ++i) {
            sh[i].poly = id;
            sh[i].poly_index = k++;
        }
        return id;
    }
    std::vector<std::size_t> polyline_pieces(std::size_t i) const {
        std::vector<std::size_t> out;
        if (i >= shapes_.size()) return out;
        const gcm::u32 id = shapes_[i].poly;
        if (id == 0) { out.push_back(i); return out; }
        for (std::size_t k = 0; k < shapes_.size(); ++k)
            if (shapes_[k].poly == id) out.push_back(k);
        std::sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) {
            return shapes_[a].poly_index < shapes_[b].poly_index;
        });
        return out;
    }
    bool polyline_closed(const std::vector<std::size_t>& pieces) const noexcept {
        if (pieces.size() < 2) return false;
        const gsnap::Shape& f = shapes_[pieces.front()];
        const gsnap::Shape& l = shapes_[pieces.back()];
        const auto endpoint=[&](const gsnap::Shape& s,bool end) {
            if(s.kind!=gsnap::ShapeKind::Arc) return end?s.b:s.a;
            double x,y;gcm::to_world(vol_,s.a,x,y);
            const double angle=end?s.end_angle:s.start_angle,r=s.radius*vol_.inv_scale();
            return gcm::to_lattice(vol_,x+r*std::cos(angle),y+r*std::sin(angle));
        };
        const auto a=endpoint(f,false),b=endpoint(l,true);
        return std::hypot(double(a.x-b.x),double(a.y-b.y))<=4;

    }
    void note_polyline_ids() noexcept {
        for (const auto& sh : shapes_) {
            last_poly_ = std::max(last_poly_, sh.poly);
            last_key_  = std::max(last_key_, sh.key);
        }
    }

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    std::size_t pick(double px, double py) const;
    void        select_at(double px, double py, bool add);
    bool        select_context_at(double px, double py);
    bool        context_selected_at(double px, double py) const;
    void        select_all();
    std::size_t cycle_selection(double px, double py, bool add = false);
    std::size_t select_previous();
    std::size_t select_last();
    std::size_t selection_count() const;
    void remember_selection();
    void        clear_selection() { remember_selection(); selection_.clear(); sel_refs_.clear(); sel_texts_.clear(); sel_dims_.clear(); sel_hatches_.clear(); rebuild_grips(); }
    void        set_selection(std::vector<std::size_t> which) {
        remember_selection();
        sel_dims_.clear(); sel_hatches_.clear();
        expand_region_selection(which);
        selection_ = std::move(which);
        sel_refs_.clear();
        sel_texts_.clear();
        rebuild_grips();
    }
    bool        is_selected(std::size_t i) const noexcept;
    const std::vector<std::size_t>& selection() const noexcept { return selection_; }
    const std::vector<std::size_t>& selected_dimensions() const noexcept { return sel_dims_; }
    void set_selected_dimensions(std::vector<std::size_t> which) {
        clear_selection();
        for (auto i:which) if (i<dims_.size() && std::find(sel_dims_.begin(),sel_dims_.end(),i)==sel_dims_.end()) sel_dims_.push_back(i);
        rebuild_grips();
    }
    void modify_dimensions(const std::function<void(std::vector<gdim::Dimension>&)>& edit) {
        mark(); edit(dims_); ++style_serial_;
    }
    void modify_references(const std::function<void(std::vector<gdoc::BlockRef>&)>& edit) {
        auto refs=blocks_.references(); mark(); edit(refs); blocks_.set_references(std::move(refs));
        sync_wall_sections();
    }
    const std::vector<gcm::u32>& selected_references() const noexcept { return sel_refs_; }
    static constexpr gcm::u32 npos32 = 0xFFFFFFFFu;
    gcm::u32 pick_reference(double px, double py) const;
    void        erase_selected();

    std::size_t select_area(double px0, double py0, double px1, double py1,
                            bool add);

    std::size_t select_fence(const std::vector<std::pair<double, double>>& screen_pts,
                             bool add);
    std::size_t select_lasso(const std::vector<std::pair<double, double>>& screen_pts,
                             bool crossing, bool add);

    struct Filter {
        bool        by_kind{false};
        gsnap::ShapeKind kind{gsnap::ShapeKind::Segment};
        bool        by_layer{false};
        std::string layer;
        bool        by_length{false};
        double      min_length{0.0}, max_length{1e300};
        bool        by_radius{false};
        double      min_radius{0.0}, max_radius{1e300};

        bool matches(const gsnap::Shape& s, const gcm::WorkingVolume& v,
                     std::string_view layer_of) const;
        bool any() const noexcept {
            return by_kind || by_layer || by_length || by_radius;
        }
    };

    std::size_t select_filtered(const Filter& f, bool add);
    std::vector<std::size_t> find(const Filter& f) const;
    bool        crossing_direction(double px0, double px1) const noexcept {
        return px1 < px0;
    }

    struct Band {
        bool   active{false};
        double x0{0}, y0{0};      // screen px
        double x1{0}, y1{0};      // screen px
        bool   crossing{false};
    };
    void        begin_band(double px, double py);
    void        update_band(double px, double py);
    std::size_t end_band(bool add);
    void        cancel_band() noexcept { band_ = Band{}; }
    const Band& band() const noexcept { return band_; }

    bool  begin_selection_drag(double px, double py);
    void  update_selection_drag(double px, double py);
    bool  end_selection_drag();
    void  cancel_selection_drag();
    bool  selection_dragging() const noexcept { return dragging_selection_; }

    bool type_point(std::string_view text);
    void set_display_unit(gunit::Length u) noexcept { unit_ = u; }
    gunit::Length display_unit() const noexcept { return unit_; }

    std::string cursor_readout() const;

    double pickbox_pixels{8};
    bool dynamic_input_enabled{true};
    bool highlight_selection{true}, snap_marker{true}, blip_mode{false};
    std::vector<Lattice> blips;
    int selection_add_mode{0};
    double grid_angle{0};
    gdoc::Attributes current_attributes;
    double& second_chamfer_distance() noexcept { return chamfer_second_; }
    double& offset_distance() noexcept { return offset_distance_; }
    double& corner_radius() noexcept { return corner_radius_; }
    double& chamfer_distance() noexcept { return chamfer_distance_; }
    int&    array_columns() noexcept { return array_columns_; }
    int&    array_rows() noexcept { return array_rows_; }
    int&    array_count() noexcept { return array_count_; }
    double& array_angle() noexcept { return array_angle_; }

    const std::vector<gdim::Dimension>& dimensions() const noexcept { return dims_; }
    void load_dimensions(std::vector<gdim::Dimension> d) { dims_ = std::move(d); }
    void add_dimensions(const std::vector<gdim::Dimension>& d) {
        if (d.empty()) return;
        mark();
        dims_.insert(dims_.end(), d.begin(), d.end());
    }
    gdim::StyleTable&       dim_styles() noexcept { return dim_styles_; }
    const gdim::StyleTable& dim_styles() const noexcept { return dim_styles_; }

    struct Label {
        double      x{0.0}, y{0.0};
        double      height{0.0};    // metres
        double      angle{0.0};     // radians
        std::string text;
        int         halign{1};
        int         valign{2};
        grender::Color color{grender::d2::kByRole};
        double width_factor{1}, oblique{0}, y_factor{1}, tracking{1};
        std::shared_ptr<gtext::StrokeFont> stroke_font;
        std::string layer{"0"};
        gdoc::Attributes attrs;
        std::vector<gclip::Boundary> clips;
        const gtext::Font* outline_font{nullptr};
        std::shared_ptr<gtext::Font> owned_font;
        bool bold{false};
        grender::Color background{0,0,0,0}, highlight{0,0,0,0};
        double box_width{0}, box_height{0}, text_advance{0};
    };
    const std::vector<Label>& labels() const noexcept { return labels_cache_; }
    const std::vector<Label>& dynamic_labels() const noexcept { return labels_dyn_; }
    gcm::u64 labels_revision() const noexcept { return labels_revision_; }

    struct TextItem {
        Lattice     pos{};
        std::string text;
        double      height{0.25};   // metres
        double      angle{0.0};     // radians
        int         halign{1};      // 0 left, 1 centre, 2 right -- of `pos`
        int         valign{2};      // 0 baseline, 1 bottom, 2 middle, 3 top
        double      width{0.0};
        std::string style;
        std::string field;
        gcm::u64 selection_id{0};
        std::string layer;
        gdoc::Attributes attrs;
    };
    void add_text(Lattice pos, std::string text, double height, double angle = 0.0,
                  int halign = 1, int valign = 2, double width = 0.0,
                  std::string style = {}, std::string layer = {}, gdoc::Attributes attrs = {});
    bool set_text(std::size_t i, std::string text) {
        if (i >= texts_.size()) return false;
        mark();
        texts_[i].text = std::move(text);
        texts_[i].field.clear();
        ++texts_serial_;
        return true;
    }
    bool set_text_width(std::size_t i, double width) {
        if (i >= texts_.size()) return false;
        mark();
        texts_[i].width = width;
        ++texts_serial_;
        return true;
    }
    const std::vector<TextItem>& texts() const noexcept { return texts_; }
    std::string text_property(const TextItem& text, const std::string& key) const;
    void set_font_registry(const gtext::FontRegistry* fonts) noexcept { font_registry_=fonts; }
    static std::vector<grender::d2::TriangleItem> label_fills(const Label& label, const gcm::WorkingVolume& volume);
    void modify_texts(const std::function<void(std::vector<TextItem>&)>& edit) {
        mark(); edit(texts_); ++texts_serial_;
    }
    std::string resolved_text(const TextItem& t) const {
        const auto it=field_values_.find(t.field);
        return it==field_values_.end() ? t.text : it->second;
    }
    void set_field_values(std::map<std::string,std::string> values) {
        if (values!=field_values_) { field_values_=std::move(values); ++texts_serial_; }
    }
    bool set_text_field(std::size_t i, const std::string& field) {
        if (i>=texts_.size()) return false;
        mark(); texts_[i].field=field; ++texts_serial_; return true;
    }
    const std::vector<std::size_t>& selected_texts() const noexcept { return sel_texts_; }
    void set_selected_texts(std::vector<std::size_t> which) {
        remember_selection();
        sel_dims_.clear(); sel_hatches_.clear();
        sel_texts_ = std::move(which);
        selection_.clear();
        sel_refs_.clear();
        rebuild_grips();
    }
    std::size_t pick_text(double px, double py) const;
    std::size_t pick_dim(double px, double py) const;
    void select_reference(gcm::u32 index);
    void add_centre_mark(Lattice centre, double r);
    bool select_and_drag(double px, double py);
    void scale_selected_text(double factor);
    void rescale_document(double factor);
    bool dragging_selection() const noexcept { return dragging_selection_; }
    gcm::u64 edit_serial() const noexcept { return edit_serial_; }
    gcm::u64 undo_mark_serial() const noexcept { return undo_mark_serial_; }
    gcm::u64 style_serial() const noexcept { return style_serial_; }
    gcm::u64 texts_serial() const noexcept { return texts_serial_; }
    void bump_edit_serial() noexcept { ++edit_serial_; }

    void set_current_layer(std::string name) { current_layer_ = std::move(name); }
    const std::string& current_layer() const noexcept { return current_layer_; }
    void set_hidden_layers(std::vector<std::string> names);
    void set_linetypes(std::vector<gdoc::Linetype> t) {
        if (t.size() != linetypes_.size()) ++style_serial_;
        else for (std::size_t i = 0; i < t.size(); ++i)
            if (t[i].name != linetypes_[i].name ||
                t[i].pattern.size() != linetypes_[i].pattern.size()) { ++style_serial_; break; }
        linetypes_ = std::move(t);
    }
    void set_layer_linetypes(std::vector<std::pair<std::string, std::string>> l) {
        if (l != layer_linetypes_) { layer_linetypes_ = std::move(l); ++style_serial_; }
    }
    void set_linetype_scale(double s) noexcept {
        if (s > 0.0 && s != linetype_scale_) { linetype_scale_ = s; ++style_serial_; }
    }
    double linetype_scale() const noexcept { return linetype_scale_; }

    void set_lineweight_display(bool enabled, std::map<std::string,gdoc::Lineweight> weights) {
        if (enabled!=lineweight_display_ || weights!=layer_weights_) {
            lineweight_display_=enabled; layer_weights_=std::move(weights); ++style_serial_;
        }
    }
    float stroke_width(const gsnap::Shape& shape) const {
        if (!lineweight_display_) return 1.0f;
        auto weight=shape.attrs.lineweight;
        if (weight==gdoc::Lineweight::ByLayer || weight==gdoc::Lineweight::ByBlock) {
            const auto it=layer_weights_.find(shape.layer);
            weight=it==layer_weights_.end() ? gdoc::Lineweight::Default : it->second;
        }
        return float(std::max(1.0,gdoc::lineweight_mm(weight)*96.0/25.4));
    }
    void set_layer_colors(std::vector<std::pair<std::string, grender::Color>> c) {
        if (c.size() != layer_colors_.size()) ++style_serial_;
        else for (std::size_t i = 0; i < c.size(); ++i) {
            const auto& [an, ac] = c[i];
            const auto& [bn, bc] = layer_colors_[i];
            if (an != bn || ac.r != bc.r || ac.g != bc.g || ac.b != bc.b || ac.a != bc.a) {
                ++style_serial_;
                break;
            }
        }
        layer_colors_ = std::move(c);
    }
    grender::Color color_of_layer(const std::string& layer) const noexcept {
        for (const auto& c : layer_colors_)
            if (c.first == layer) return legible(c.second);
        return grender::d2::kByRole;
    }

    void set_ground(grender::Color c) noexcept {
        if (c.r == ground_.r && c.g == ground_.g && c.b == ground_.b) return;
        ground_ = c;
        ++style_serial_;
    }
    grender::Color ground() const noexcept { return ground_; }

    grender::Color legible(grender::Color c) const noexcept {
        if (c.a < 0.0f) return c;
        const auto value = [](const grender::Color& x) {
            return std::max(x.r, std::max(x.g, x.b));
        };
        const auto chroma = [](const grender::Color& x) {
            return std::max(x.r, std::max(x.g, x.b)) - std::min(x.r, std::min(x.g, x.b));
        };
        if (chroma(c) > 0.12f) return c;
        if (std::fabs(value(c) - value(ground_)) > 0.25f) return c;
        return grender::Color{1.0f - c.r, 1.0f - c.g, 1.0f - c.b, c.a};
    }
    void set_layer_transparencies(std::map<std::string,int> values);
    void set_appearance_layers(const gdoc::LayerTable& layers);
    int transparency_of_shape(const gsnap::Shape& shape) const noexcept {
        const auto it=layer_transparencies_.find(shape.layer);
        return gdoc::transparency_percent(shape.attrs,it==layer_transparencies_.end()?0:it->second);
    }
    grender::Color color_of_shape(const gsnap::Shape& s) const noexcept {
        const gdoc::Color& c = s.attrs.color;
        grender::Color out = grender::d2::kByRole;
        if (c.kind == gdoc::Color::Kind::True) {
            out = legible(grender::Color{float(c.r) / 255.0f, float(c.g) / 255.0f,
                                         float(c.b) / 255.0f, 1.0f});
        } else if (c.kind == gdoc::Color::Kind::Index && c.index != 7) {
            gcm::u8 r = 0, g = 0, b = 0;
            gdoc::aci_rgb(c.index, r, g, b);
            out = legible(grender::Color{float(r) / 255.0f, float(g) / 255.0f,
                                         float(b) / 255.0f, 1.0f});
        } else {
            out = color_of_layer(s.layer);
        }
        const int transparency=transparency_of_shape(s);
        if(transparency>0) {
            if(out.a<0)out=grender::color_for(grender::Role::Geometry);
            out.a=1.0f-float(transparency)/100.0f;
        }
        return out;
    }
    bool shape_visible(const gsnap::Shape& shape) const noexcept {
        return shape.attrs.visible && !layer_hidden(shape.layer) && transparency_of_shape(shape)<100;
    }
    bool layer_hidden(const std::string& layer) const noexcept {
        for (const auto& h : hidden_layers_) if (h == layer) return true;
        return false;
    }
    void load_texts(std::vector<TextItem> t) { texts_ = std::move(t); ++texts_serial_; }
    void set_text_font(const gtext::Font* f) noexcept {
        if (f != text_font_) { text_font_ = f; ++texts_serial_; }
    }
    const gtext::Font* text_font() const noexcept { return text_font_; }
    void set_text_styles(const gtext::TextStyleTable& styles) {
        if (text_styles_.all() != styles.all() || text_styles_.current() != styles.current()) {
            text_styles_ = styles; ++texts_serial_;
        }
    }
    static std::vector<std::array<double,4>> label_strokes(const Label& label, const gcm::WorkingVolume& volume = {});
    static std::vector<gclip::Vertex> label_mesh(const Label& label, const gtext::Font* font, const gcm::WorkingVolume& volume);
    std::vector<Label> note_labels(const TextItem& t, double annotation_scale = 0) const;
    std::vector<Label> reference_labels(const gdoc::BlockRef& ref, double annotation_scale = 0) const;
    std::vector<gdoc::BlockTable::UnderlayInstance> underlay_instances() const;
    bool labels_bounds(const std::vector<Label>& labels, double& lx, double& ly, double& hx, double& hy) const;
    bool text_is_annotative(const TextItem& t) const {
        const auto* style=text_styles_.find(t.style.empty()?text_styles_.current():t.style);
        return style&&style->annotative;
    }
    void text_bounds(const TextItem& t, double& lx, double& ly, double& hx, double& hy) const;

    gdoc::BlockTable&       blocks() noexcept { return blocks_; }
    const gdoc::BlockTable& blocks() const noexcept { return blocks_; }
    gdoc::UnderlayTable&    underlays() noexcept { return underlays_; }
    const gdoc::UnderlayTable& underlays() const noexcept { return underlays_; }
    void set_underlay_snap_shapes(std::vector<gsnap::Shape> shapes);

    bool define_block(std::string_view name, double px, double py);
    bool insert_block(std::string_view name, double px, double py,
                      double scale = 1.0, double rotation = 0.0);
    bool explode_block(std::size_t reference);
    bool place_symbol(std::string_view id, gcm::Lattice2 at, double size = 1.0, double rotation = 0.0);
    bool update_symbol_legend(gcm::Lattice2 at, double size = 1.0);
    bool refresh_symbol_legends(bool force = false);
    bool take_symbol_legend_changes() noexcept {
        const bool changed=legend_metadata_dirty_;legend_metadata_dirty_=false;return changed;
    }

    bool set_attribute(std::size_t reference, std::string_view tag,
                       std::string_view value);
    std::vector<std::pair<std::string, std::string>> attributes_of(
        std::size_t reference) const;

    struct DynamicField {
        std::string label;
        std::string value;
        bool        editable{true};
        bool        active{false};
    };
    struct DynamicInput {
        bool                      visible{false};
        double                    x{0.0}, y{0.0};
        std::vector<DynamicField> fields;
    };
    DynamicInput dynamic_input() const;

    void dynamic_type(std::string_view text);
    void dynamic_set(int field, std::string text);
    void dynamic_backspace();
    void dynamic_tab();
    bool dynamic_commit();
    void dynamic_cancel();
    const std::string& dynamic_buffer() const noexcept { return dyn_buffer_; }
    int  dynamic_field_index() const noexcept { return dyn_field_; }

    gdoc::MultilineTable& multilines() noexcept { return multilines_; }
    const gdoc::MultilineTable& multilines() const noexcept { return multilines_; }
    void set_multiline_style(std::string_view name);
    const std::string& multiline_style() const noexcept { return mline_style_; }
    void set_multiline_justify(gdoc::Justify j) noexcept { mline_justify_ = j; }
    gdoc::Justify multiline_justify() const noexcept { return mline_justify_; }
    double& multiline_scale() noexcept { return mline_scale_; }

    const std::vector<ggeo::NurbsCurve2<Scalar>>& splines() const noexcept {
        return splines_;
    }
    static constexpr double kModelChordTolerance = 1e-5;
    double& spline_tolerance() noexcept { return spline_tolerance_; }
    double  spline_tolerance() const noexcept { return spline_tolerance_; }
    void add_splines(const std::vector<ggeo::NurbsCurve2<Scalar>>& s) {
        if (s.empty()) return;
        mark();
        splines_.insert(splines_.end(), s.begin(), s.end());
    }
    void load_splines(std::vector<ggeo::NurbsCurve2<Scalar>> s) { splines_ = std::move(s); }

    std::vector<Tool> tool_ring() const;

    bool tool_available(Tool t) const;
    static std::vector<std::string> tool_modes(Tool t);
    int  tool_mode(Tool t) const noexcept;
    void set_tool_mode(Tool t, int mode);

    std::vector<const char*> osnap_ring(double px, double py) const;
    bool force_snap(std::string_view kind);

    const std::vector<gedit::Grip>& grips() const noexcept { return grips_; }
    bool  grip_dragging() const noexcept { return dragging_grip_; }
    std::vector<gedit::GripOption> grip_options(double px, double py) const;
    bool  apply_grip_option(double px, double py, gedit::GripOp op, Lattice to);
    bool  begin_grip_drag(double px, double py);
    void  update_grip_drag(double px, double py);
    bool  end_grip_drag();
    void  cancel_grip_drag();

    bool hatch_at(double px, double py, std::string_view pattern = "ANSI31");
    bool background_at_world(gcm::Lattice2 seed, gdoc::Color color,
                             double gap_metres = .01, int opacity = 35);
    bool remove_background_at(gcm::Lattice2 seed);
    std::vector<gsnap::Shape> background_boundary(const gdoc::AssociativeHatch& h, bool local = false) const;
    bool has_selected_door() const;
    std::size_t selected_door_count() const;
    bool edit_selected_doors(std::string_view flow = {}, bool reverse_swing = false,
                             bool reverse_hinge = false);
    bool set_door_flow(std::string_view direction);
    std::vector<gsnap::Shape> door_flow_shapes(std::size_t only = std::size_t(-1)) const;
    std::size_t detect_wall_regions(bool infer_generic = true);
    void group_object(std::size_t first, const std::string& type);
    void select_edge_at(double px, double py);
    bool corner_selection(Tool tool);
    bool dimension_selection();
    bool add_wall(gcm::Lattice2 a, gcm::Lattice2 b, double thickness);
    static bool is_wall_section(const gdoc::BlockRef& ref) { return ref.attrs.user.contains("wall_section"); }
    static bool is_joined_wall(const gsnap::Shape& shape) { return shape.attrs.user.contains("wall_joined"); }
    bool sync_wall_sections();
    void adopt_wall_sections();
    gcm::u32 pick_wall_section(double px, double py) const;
private:
    void add_wall_section(const std::vector<gsnap::Shape>& boundary);
public:
    std::vector<std::size_t> wall_centerline_indices() const;
    std::vector<gdoc::Segment2> wall_material_segments() const;
    std::size_t walls_from_lines(double thickness);
    void group_door(std::size_t first_shape);
    void track_door_opening(gcm::u32 door_key, const std::vector<gsnap::Shape>& uncut,
                            bool automatic);
    bool update_door_openings(const std::vector<gsnap::Shape>& before);
    bool hatch_at_world(gcm::Lattice2 seed, std::string_view pattern = "ANSI31",
                        double scale = 1.0, double angle_deg = 0.0);
    bool ensure_wall_hatch(gcm::Lattice2 seed, double scale, double angle_deg);
    std::size_t refresh_wall_hatches(const std::vector<DraftingBounds>& dirty = {});
    bool add_hatch(const std::vector<gsnap::Shape>& boundary, gcm::Lattice2 seed,
                   std::string_view pattern = "ANSI31", double scale = 1.0, double angle_deg = 0.0,
                   std::string layer = {}, gdoc::Attributes attrs = {});
    bool gradient_at_point(double px, double py);
    void load_hatches(std::vector<gdoc::AssociativeHatch> h) {
        hatches_ = std::move(h); sel_hatches_.clear();
        for (auto& x : hatches_) { x.valid = false; x.orphaned = false; }
    }
    const std::vector<gdoc::AssociativeHatch>& hatches() const noexcept {
        return hatches_;
    }
    const std::vector<std::size_t>& selected_hatches() const noexcept { return sel_hatches_; }
    void set_selected_hatches(std::vector<std::size_t> which);
    std::size_t pick_hatch(double px, double py) const;
    bool set_hatch_property(const std::vector<std::size_t>& which,
                            const std::string& property, const std::string& value);
    gdoc::PatternTable& patterns() noexcept { return patterns_; }
    gdoc::HatchStyle&   hatch_style() noexcept { return hatch_style_; }
    gdoc::Gradient&     gradient() noexcept { return gradient_; }
    std::size_t         refresh_hatches(const std::vector<DraftingBounds>& dirty = {});
    struct DraftingWork {gcm::u64 opening_edges{0},opening_doors{0},background_traces{0},background_segments{0},hatch_updates{0};};
    const DraftingWork& drafting_work() const noexcept {return drafting_work_;}

    struct CleanupReport { int merged{0}, removed{0}, cut{0}, caps{0}; };
    struct OpeningCut { Lattice from{}, to{}; double thickness{0}; gcm::u64 wall_group{0}; };
    CleanupReport merge_overlaps(double tol_m, bool dedupe, bool openings, std::optional<OpeningCut> opening = {});

    bool        apply_constraint(gsolve::Kind k, double value = 0.0);
    gsolve::Dof dof() const;

    std::size_t auto_constrain();
    std::vector<gsolve::Constraint> suggest_constraints() const;
    gsolve::InferenceSettings&       inference() noexcept { return inference_; }
    const gsolve::InferenceSettings& inference() const noexcept { return inference_; }

    const std::string& message() const noexcept { return message_; }

    std::size_t segment_count() const noexcept { return shapes_.size(); }
    const std::vector<gsnap::Shape>& shapes() const noexcept { return shapes_; }
    Snap        snap() const noexcept { return snap_; }
    bool        pending() const noexcept { return pending_; }
    void clear_hover() noexcept { snap_ = Snap{}; track_ = Track{}; }
    Lattice     cursor() const noexcept { return cursor_; }
    const gcm::WorkingVolume& volume() const noexcept { return vol_; }

    bool grid_snap() const noexcept { return grid_snap_; }
    void set_grid_snap(bool on) noexcept { grid_snap_ = on; }
    void set_ucs(double ox, double oy, double angle_rad) noexcept {
        ucs_ox_ = ox;
        ucs_oy_ = oy;
        ucs_angle_ = angle_rad;
    }
    bool ucs_active() const noexcept {
        return ucs_ox_ != 0.0 || ucs_oy_ != 0.0 || ucs_angle_ != 0.0;
    }
    double ucs_angle() const noexcept { return ucs_angle_; }
    void ucs_to_world(double x, double y, double& wx, double& wy) const noexcept {
        const double c = std::cos(ucs_angle_), s2 = std::sin(ucs_angle_);
        wx = ucs_ox_ + x * c - y * s2;
        wy = ucs_oy_ + x * s2 + y * c;
    }
    void world_to_ucs(double wx, double wy, double& x, double& y) const noexcept {
        const double c = std::cos(-ucs_angle_), s2 = std::sin(-ucs_angle_);
        const double dx = wx - ucs_ox_, dy = wy - ucs_oy_;
        x = dx * c - dy * s2;
        y = dx * s2 + dy * c;
    }

    int&    point_mode() noexcept { return point_mode_; }
    int     point_mode() const noexcept { return point_mode_; }
    double& point_size() noexcept { return point_size_; }
    double  point_size() const noexcept { return point_size_; }

    double grid_spacing() const noexcept { return grid_spacing_; }
    void   set_grid_spacing(double s) noexcept {
        if (s >= 0.0) { grid_spacing_ = s; ++style_serial_; }
    }
    gsnap::Engine&       snaps() noexcept { return snap_engine_; }
    const gsnap::Engine& snaps() const noexcept { return snap_engine_; }
    void acquire_track();

    struct Alignment {
        bool    active{false};
        bool    horizontal{false};
        bool    vertical{false};
        Lattice source_h{};
        Lattice source_v{};
    };
    const Alignment& alignment() const noexcept { return align_; }
    void clear_track() noexcept { snap_engine_.release_all(); }

    void        build(grender::d2::DrawList& out) const;
    std::string status() const;

private:
    Band        band_{};
    bool        dragging_selection_{false};
    double      drag_from_x_{0}, drag_from_y_{0};   // world
    std::vector<gsnap::Shape> drag_original_;

public:
    struct Snapshot {
        std::vector<gsnap::Shape>    shapes;
        std::vector<gdim::Dimension> dims;
        std::vector<std::size_t>     selection;
        std::vector<gdoc::BlockRef>  refs;
        std::vector<gcm::u32>        sel_refs;
        std::vector<TextItem>        texts;
        std::vector<std::size_t>     sel_texts;
        std::vector<gdoc::AssociativeHatch>  hatches;
        std::vector<ggeo::NurbsCurve2<Scalar>> splines;
        std::vector<StoredConstraint>          constraints;
        std::vector<std::size_t> sel_dims;
        std::vector<std::size_t> sel_hatches;
    };

    struct SpaceState {
        Snapshot content;
        std::vector<Snapshot> undo, redo;
        gdoc::UnderlayTable underlays;
        std::vector<gcm::u64> previous_selection;
        std::map<gcm::u32,gcm::u64> shape_selection_ids;
        std::string annotation_scale{"1:1"};
        double annotation_factor{1};
    };
    SpaceState capture_space() const;
    void restore_space(const SpaceState& state);
private:
    Snap find_grid_snap(double wx, double wy) const;
    void sync_snap_engine() const;
    void place(Lattice p);
    void add_segment(Lattice a, Lattice b);
    void add_circle(Lattice centre, gcm::i64 radius);
    void add_arc(Lattice centre, gcm::i64 radius, double a0, double a1);
    void mark();
    gcm::u64 edit_serial_{0};
    gcm::u64 undo_mark_serial_{0};
    std::string current_layer_{"0"};
    std::vector<std::string> hidden_layers_;
    std::vector<gdoc::Linetype> linetypes_;
    std::vector<std::pair<std::string, std::string>> layer_linetypes_;
    double linetype_scale_{1.0};
    gcm::u64 style_serial_{0};
    mutable bool solid_only_{false};
    gcm::u32 last_poly_{0}, poly_run_{0}, poly_next_{0};
    // Never reset while a document is open; a reused handle repoints references.
    gcm::u32 last_key_{0};
    const gtext::Font* text_font_{nullptr};
    std::vector<StoredConstraint> constraints_;
    gcm::u32 snap_key_{0};
    gdim::Dimension::Anchor snap_anchor_{gdim::Dimension::Anchor::None};
    std::vector<std::pair<gcm::u32, gdim::Dimension::Anchor>> arc_pt_refs_;
    bool lineweight_display_{false};
    std::map<std::string,gdoc::Lineweight> layer_weights_;
    std::vector<std::pair<std::string, grender::Color>> layer_colors_;
    grender::Color ground_{0.086f, 0.094f, 0.106f, 1.0f};
    std::map<std::string,int> layer_transparencies_;
    gdoc::LayerTable appearance_layers_;
    mutable grender::d2::DrawList static_cache_;
    mutable std::vector<Label>    labels_cache_;
    mutable grender::d2::DrawList label_geometry_cache_;
    mutable std::vector<std::shared_ptr<const std::vector<gcm::u8>>> label_font_sources_;
    mutable std::array<double,3> label_cache_volume_{};
    mutable int label_cache_resolution_{-1};
    mutable gcm::u64              static_key_{0};
    mutable gcm::u64              labels_revision_{0};
    mutable gcm::u64              snap_sync_key_{~0ull};
    gcm::u64 underlay_snap_serial_{};
    std::vector<gsnap::Shape> underlay_snap_shapes_;
    mutable std::vector<std::size_t> snap_shape_indices_;
    std::size_t hover_shape_{npos};
    mutable double tess_scale_{64.0};
    gcm::u64 block_geometry_key() const noexcept;
    gcm::u64 static_key() const noexcept;
public:
    void build_split(grender::d2::DrawList& static_out, grender::d2::DrawList& dynamic_out) const;
    // No dash patterns: a hatch boundary traced from dashes would leak.
    void build_geometry(grender::d2::DrawList& out) const;
    void build_dynamic_only(grender::d2::DrawList& dynamic_out) const;
    const grender::d2::DrawList& static_draw() const noexcept { return static_cache_; }
    gcm::u64 draw_version() const noexcept { return static_key_; }
private:
    bool drag_cloud_grip(const gedit::Grip& grip, Lattice to);
    void build_dynamic(grender::d2::DrawList& out) const;
    void push_shape(gsnap::Shape s) {
        if (s.layer.empty()) { s.layer = current_layer_; if(!s.key) s.attrs=current_attributes; }
        if (s.key == 0) s.key = ++last_key_;
        if (poly_run_ != 0) {
            s.poly = poly_run_;
            s.poly_index = poly_next_++;
        }
        shapes_.push_back(std::move(s));
    }
    void emit_shape(grender::d2::DrawList& out, const gsnap::Shape& s,
                    grender::Role role) const;
    const gdoc::Linetype* linetype_of(const gsnap::Shape& s) const noexcept {
        if (solid_only_) return nullptr;
        const std::string& own = s.attrs.linetype;
        const std::string* name = &own;
        if (own.empty() || own == "ByLayer" || own == "ByBlock") {
            name = nullptr;
            for (const auto& l : layer_linetypes_)
                if (l.first == s.layer) { name = &l.second; break; }
            if (!name) return nullptr;
        }
        if (*name == "Continuous") return nullptr;
        for (const auto& lt : linetypes_)
            if (lt.name == *name) return lt.solid() ? nullptr : &lt;
        return nullptr;
    }
    void emit_dimensions(grender::d2::DrawList& out) const;
    void emit_note(const TextItem& t, double x, double y, std::vector<Label>& labels, double annotation_scale = 0) const;
    gtext::TextStyleTable text_styles_;
    mutable std::map<std::string,std::shared_ptr<gtext::StrokeFont>> stroke_fonts_;
    gdim::Style annotative(const gdim::Style& st) const;
public:
    gdim::Style styled_for(const gdim::Style& st, double feature_scale) const;
private:
public:
    void ensure_selection_ids();
    gcm::u64 mint_selection_id() { return ++next_selection_id_; }
    void set_annotative_fixed(double s) noexcept { annotative_fixed_ = s; }
    const std::string& annotation_scale_name() const noexcept { return annotation_scale_name_; }
    double annotation_factor() const noexcept { return annotation_factor_; }
    void set_annotation_scale(std::string name, double factor) {
        if(!std::isfinite(factor) || factor<=0) return;
        if(annotation_scale_name_!=name || annotation_factor_!=factor) {
            annotation_scale_name_=std::move(name);annotation_factor_=factor;++style_serial_;
        }
    }
    double effective_annotation_factor() const noexcept;
    double annotation_factor_for(const gdim::Style& st) const noexcept;
private:
    double auto_annotation_factor() const noexcept;
    mutable double auto_annotation_{1.0};
    mutable gcm::u64 auto_annotation_serial_{~gcm::u64{0}};
    double annotative_fixed_{0.0};
    std::string annotation_scale_name_{"1:1"};
    double annotation_factor_{1};
    void emit_hatches(grender::d2::DrawList& out) const;
    void emit_blocks(grender::d2::DrawList& out) const;
    void emit_block(grender::d2::DrawList& out, const gdoc::BlockRef& r, grender::Role role) const;
    void rebuild_grips();
    void rebuild_spline();
    void rebuild_multiline();
    std::size_t mline_first_{0};
    std::vector<gdoc::Segment2> segments_of(const std::vector<gsnap::Shape>& shapes) const;

    void run_edit_tool();
    void run_dimension_tool();
    std::size_t picks_needed() const noexcept;

    gsolve::System build_system(std::vector<gcm::u32>& entity_of_shape) const;
    void           write_back(const gsolve::System& sys,
                              const std::vector<gcm::u32>& entity_of_shape);

    gcm::WorkingVolume                       vol_{};
    std::vector<gsnap::Shape> shapes_;
    std::vector<gdim::Dimension> dims_;
    mutable std::vector<std::array<double, 4>> ref_bounds_;
    mutable gcm::u64 ref_bounds_key_{0};
    enum class SelectionKind { Shape, Text, Reference, Dimension };
    struct SelectionObject {
        SelectionKind kind;
        std::size_t index;
        std::vector<std::array<double,4>> lines;
        std::vector<std::array<double,4>> filled_boxes;
        std::vector<std::array<std::pair<double,double>,3>> filled_triangles;
        std::vector<gregion::Region> filled_regions;
    };
    std::vector<std::array<double,4>> selection_lines(const gsnap::Shape& shape) const;
    std::vector<SelectionObject> selection_objects() const;
    void include_selection(const SelectionObject& object, bool toggle = false);
    std::size_t select_polygon(const std::vector<std::pair<double,double>>& points,
                               bool fence, bool crossing, bool add);

    bool selected_at(double px, double py) const;
    std::size_t select_hit(double px, double py, bool add, bool cycle, bool edge = false);
    std::vector<gcm::u64> previous_selection_;
    gcm::u64 next_selection_id_{0};
    std::map<gcm::u32,gcm::u64> shape_selection_ids_;
    std::vector<gcm::u64> cycle_candidates_;
    std::size_t cycle_position_{0};
    double cycle_x_{-1e30}, cycle_y_{-1e30};
    gcm::u64 selection_id(SelectionKind kind, std::size_t index) const;
    std::vector<std::size_t>  selection_;
    std::vector<gcm::u32>    sel_refs_;
    std::vector<gdoc::BlockRef> drag_refs_original_;
    std::vector<std::size_t> sel_texts_;
    std::vector<std::size_t> sel_dims_;
    std::map<std::string,std::string> field_values_;
    std::vector<TextItem>    drag_texts_original_;
    bool                     drag_engaged_{false};
    bool                     drag_only_doors_{false};
    std::optional<gcm::Lattice2> drag_last_delta_;
    int                      dragging_dim_{-1};
    int                      dragging_dim_point_{3};
    gdim::Dimension          drag_dim_orig_;
    double                   drag_px_{0.0}, drag_py_{0.0};
    std::vector<Snapshot>     undo_stack_, redo_stack_;

    std::vector<Lattice> chain_;
    std::vector<Lattice> arc_pts_;
    std::vector<std::size_t> picked_;

    std::string   dyn_buffer_;
    int           dyn_field_{0};
    Lattice       dyn_reference_{};
    bool          dyn_reference_valid_{false};

    gdoc::MultilineTable multilines_{};
    std::string          mline_style_{"STANDARD"};
    gdoc::Justify        mline_justify_{gdoc::Justify::Zero};
    double               mline_scale_{1.0};
    std::vector<Lattice> mline_pts_;

    std::vector<ggeo::NurbsCurve2<Scalar>> splines_;
    std::vector<Lattice>                   spline_pts_;
    double                                 spline_tolerance_{0.002};

    gdim::StyleTable dim_styles_{};
    mutable std::vector<Label> labels_;
    mutable std::vector<Label> labels_dyn_;
    const gtext::FontRegistry* font_registry_{nullptr};
    mutable std::map<std::string,std::shared_ptr<gtext::Font>> note_fonts_;
    std::vector<TextItem>      texts_;
    gcm::u64                   texts_serial_{0};

    double offset_distance_{0.5};
    double corner_radius_{0.25};
    double chamfer_distance_{0.25};
    double chamfer_second_{0.25};
    int    array_columns_{3};
    int    array_rows_{2};
    int    array_count_{6};
    double array_angle_{6.28318530717958647692};
    std::string message_;
    gunit::Length unit_{gunit::Length::Millimetre};
    gsolve::InferenceSettings inference_{};

    std::vector<gedit::Grip> grips_;
    gedit::Grip              hot_grip_{};
    bool                     dragging_grip_{false};

    gdoc::BlockTable     blocks_{};
    gcm::u64 legend_sync_edit_{~gcm::u64{0}}, legend_sync_blocks_{~gcm::u64{0}};
    bool legend_metadata_dirty_{false};
    gdoc::UnderlayTable  underlays_{};

    gdoc::PatternTable                  patterns_{};
    gdoc::HatchStyle                    hatch_style_{};
    gdoc::Gradient                      gradient_{};
    std::vector<gdoc::AssociativeHatch> hatches_;
    std::vector<std::size_t> sel_hatches_;
    mutable std::vector<gdoc::Segment2> background_graph_segments_;
    mutable std::vector<gdoc::Loop> background_graph_loops_;
    mutable long double background_graph_gap_{-1};
    mutable DraftingWork drafting_work_;
    Lattice last_point_{};
    bool has_last_point_{false};
    bool typed_exact_{false};

    mutable gsnap::Engine snap_engine_;
    bool                  grid_snap_{true};
    double                grid_spacing_{0.0};
    double                ucs_ox_{0.0}, ucs_oy_{0.0}, ucs_angle_{0.0};
    int                   point_mode_{2};      // PDMODE: a cross
    double                point_size_{0.0};    // PDSIZE: 0 = 4 px
    View    view_{};
    std::vector<Lattice> align_points_;
    bool                 align_dirty_{true};
    Alignment            align_{};
    void rebuild_align_points();
    bool find_alignment(double wx, double wy, Lattice& out);

    Polar   polar_{};
    Track   track_{};
    Tool    tool_{Tool::Line};
    int     rect_mode_{0}, circle_mode_{0}, arc_mode_{0};
    bool    pending_{false};
    Lattice anchor_{}, cursor_{};
    Snap    snap_{};
};

class Shell;

int run(int argc, char** argv);

void open_from_argv(Shell& shell, int argc, char** argv);

}
