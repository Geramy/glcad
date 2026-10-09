#include "ui/shell.hpp"
#include "ui/doc_entities.hpp"
#include "ui/solids_file.hpp"
#include "engine/block_state.hpp"
#include "exchange/plot_style.hpp"
#include <cmath>

namespace gcapp {
struct SpaceMetadataRecord {
    gcm::u32 id{0}, index{0};
    bool paper{false};
    std::vector<gcm::u8> annotations;
    std::vector<gdoc::Underlay> underlays;
};
}

namespace gfile::state {
#define GLCAD_FIELDS(Type, ...) template<> struct Fields<Type> { \
    template<class A, class V> static void visit(A& a, V& v) { a(__VA_ARGS__); } };
GLCAD_FIELDS(gdim::AnnotationScale, v.name, v.paper, v.drawing)
GLCAD_FIELDS(gplot::PaperSize, v.name, v.width, v.height, v.margin_left, v.margin_bottom, v.margin_right, v.margin_top)
GLCAD_FIELDS(gtext::TextStyleDef, v.name, v.font, v.big_font, v.height, v.width_factor,
             v.oblique, v.backwards, v.upside_down, v.vertical, v.annotative)
GLCAD_FIELDS(gdim::Style, v.name, v.dimexe, v.dimexo, v.dimgap, v.dimasz, v.arrow,
             v.dimse1, v.dimse2, v.dimsd1, v.dimsd2, v.dimtxt, v.text_pos, v.dimtih,
             v.dimtoh, v.dimtvp, v.unit, v.format, v.dimdec, v.dimlfac, v.dimscale,
             v.dimzin_leading, v.dimzin_trailing, v.prefix, v.suffix, v.dimtol, v.dimtp, v.dimtm)
GLCAD_FIELDS(gunit::Parameter, v.name, v.kind, v.expression, v.value, v.description, v.ok, v.error)
GLCAD_FIELDS(gcapp::Modeller::Material, v.name, v.r, v.g, v.b, v.a, v.roughness, v.metallic, v.density)
GLCAD_FIELDS(gcapp::Modeller::StoredMate, v.kind, v.body_a, v.face_a, v.body_b, v.face_b, v.value, v.suppressed)
GLCAD_FIELDS(gcapp::Modeller::Metadata, v.materials, v.body_layers, v.body_material, v.body_keys, v.last_body_key,
             v.face_ids, v.next_face_ids, v.mates, v.suppressed, v.rollback)
GLCAD_FIELDS(gtopo::PmiRef, v.target, v.index, v.fingerprint)
GLCAD_FIELDS(gtopo::Tolerance, v.plus, v.minus)
GLCAD_FIELDS(gtopo::PmiDimension, v.label, v.kind, v.a, v.b, v.nominal, v.tolerance, v.driving, v.moves_b)
GLCAD_FIELDS(gcm::F64, v.v)
GLCAD_FIELDS(gview::Point3, v.x, v.y, v.z)
GLCAD_FIELDS(gview::ViewState, v.target, v.distance, v.azimuth, v.elevation, v.projection, v.fov)
GLCAD_FIELDS(gview::NamedView, v.name, v.state, v.description, v.locked)
GLCAD_FIELDS(gplot::PaperPoint, v.x, v.y)
GLCAD_FIELDS(gplot::Viewport, v.lo, v.hi, v.source_space, v.centre_x, v.centre_y,
             v.scale, v.rotation, v.locked, v.on, v.frozen_layers)
GLCAD_FIELDS(gdraft::SectionLine, v.x0, v.y0, v.x1, v.y1, v.label)
GLCAD_FIELDS(gdraft::DetailCircle, v.x, v.y, v.radius, v.label)
GLCAD_FIELDS(gcapp::Shell::Layout::SectionDef, v.parent, v.line)
GLCAD_FIELDS(gcapp::Shell::Layout::DetailDef, v.parent, v.circle, v.scale)
GLCAD_FIELDS(gcapp::Shell::Layout, v.name, v.paper, v.landscape, v.scale_den, v.dx, v.dy,
             v.viewports, v.area, v.win_lo_x, v.win_lo_y, v.win_hi_x, v.win_hi_y,
             v.view_body, v.view_scale, v.view_layout, v.sections, v.details, v.style_mode, v.monochrome)
GLCAD_FIELDS(gcapp::Shell::PageSetupNamed, v.name, v.paper, v.landscape, v.scale_den, v.area)
GLCAD_FIELDS(gcmd::SysVar, v.name, v.type, v.ival, v.rval, v.sval, v.description, v.read_only)
GLCAD_FIELDS(gcapp::SpaceMetadataRecord, v.id, v.index, v.paper, v.annotations, v.underlays)
#undef GLCAD_FIELDS
}

namespace gcapp {
namespace {
struct Metadata {
    gcm::u8 drawing_discipline{0};
    std::string project_location;
    std::vector<SpaceMetadataRecord> spaces;
    std::vector<gdim::AnnotationScale> annotation_scales;
    std::vector<std::string> space_annotation_scales;
    std::vector<std::string> plot_style_names;
    std::vector<std::vector<gcm::u8>> plot_style_data;
    std::vector<std::vector<std::vector<gplot::PaperPoint>>> viewport_clips;
    std::vector<gplot::PaperSize> imported_papers;
    std::vector<gcm::u8> paper_flags;
    gcm::u32 last_space_id{0};
    std::vector<gcm::u8> tables, blocks, groups;
    std::vector<std::string> group_names;
    std::vector<gview::NamedView> views;
    std::string current_layer;
    double section_nx{0}, section_ny{0}, section_nz{0}, section_d{0};
    std::vector<gtext::TextStyleDef> text_styles;
    std::vector<gdim::Style> dim_styles;
    std::string text_style, dim_style;
    std::vector<gunit::Parameter> parameters;
    Modeller::Metadata model;
    std::vector<std::vector<gtopo::PmiDimension>> pmi;
    std::vector<gdoc::Underlay> underlays;
    std::vector<Shell::Layout> layouts;
    std::vector<Shell::PageSetupNamed> page_setups;
    std::vector<gcmd::SysVar> vars;
    double text_height{0.25}, text_width{0}, linetype_scale{1};
    int precision{3};
    std::string paper{"A3"};
    bool landscape{true};
    int scale_den{0};
    double dx{0}, dy{0};
    template<class A> void fields(A& a) {
        a(tables, blocks, groups, group_names, views, current_layer,
          section_nx, section_ny, section_nz, section_d, text_styles, dim_styles, text_style, dim_style, parameters,
          model, pmi, underlays, layouts, page_setups, vars, text_height, text_width,
          linetype_scale, precision, paper, landscape, scale_den, dx, dy);
    }
};
}

const std::vector<gcm::u8>& Shell::settings_image() const {
    if (settings_image_gate_ != metadata_gate_ || settings_image_.empty()) {
        settings_image_ = metadata_bytes();
        settings_image_gate_ = metadata_gate_;
    }
    return settings_image_;
}

std::vector<gcm::u8> Shell::metadata_bytes(bool lightweight) const {
    ++metadata_encodes_;
    const_cast<Shell*>(this)->ensure_space_ids();
    Metadata m;
    gfile::Contents tables;
    tables.layers = layers_; tables.linetypes = linetypes_; tables.multilines = canvas_.multilines();
    if (cached_block_revision_ != canvas_.blocks().revision()) {
        gfile::Contents content; content.blocks = canvas_.blocks();
        content.blocks.set_references({});
        auto bytes = gfile::encode(content);
        if (bytes != cached_block_state_) {
            cached_block_state_ = std::move(bytes);
            ++cached_block_generation_;
        }
        cached_block_revision_ = canvas_.blocks().revision();
    }
    if (!lightweight) m.blocks = cached_block_state_;
    gfile::Contents group_content;
    for (const auto& [name, shapes] : groups_) {
        m.group_names.push_back(name);
        gfile::Contents::SpaceGeometry g;
        g.space = static_cast<gcm::u32>(group_content.geometry.size()); g.shapes = shapes;
        for (const auto& sh : shapes) g.layers.push_back(sh.layer);
        group_content.geometry.push_back(std::move(g));
    }
    m.groups = gfile::encode(group_content); m.views = views_.all();
    m.current_layer = layers_.current();
    m.section_nx = section_nx_; m.section_ny = section_ny_; m.section_nz = section_nz_; m.section_d = section_d_;
    for (std::size_t i = 0; i < layouts_.size(); ++i) {
        const bool live = in_paper_space_ && int(i) == active_layout_;
        const auto& shapes = live ? canvas_.shapes() : layouts_[i].shapes;
        gfile::Contents::SpaceGeometry g;
        g.space = static_cast<gcm::u32>(i); g.shapes = shapes;
        for (const auto& s : shapes) g.layers.push_back(s.layer.empty() ? "0" : s.layer);
        tables.geometry.push_back(std::move(g));
        append_text_entities(live ? canvas_.texts() : layouts_[i].notes,
                             static_cast<gcm::u32>(i), tables.entities);
    }
    m.tables = gfile::encode(tables);
    m.text_styles = text_styles_.all(); m.text_style = text_styles_.current();
    m.dim_styles = canvas_.dim_styles().all(); m.dim_style = canvas_.dim_styles().current();
    m.parameters = params_.all(); m.model = modeller_.metadata();
    for (const auto& p : pmi_) m.pmi.push_back(p.all());
    while (!m.pmi.empty() && m.pmi.back().empty()) m.pmi.pop_back();
    const auto append_space = [&](gcm::u32 id, std::size_t index, bool paper) {
        SpaceMetadataRecord rec; rec.id = id; rec.index = static_cast<gcm::u32>(index); rec.paper = paper;
        gfile::Contents content;
        if (id == active_space_id()) {
            append_entities(canvas_.dimensions(), canvas_.hatches(), canvas_.splines(), canvas_.constraints(), content.entities);
            content.blocks.set_references(canvas_.blocks().references());
            rec.underlays = canvas_.underlays().all();
        } else if (const auto it = space_states_.find(id); it != space_states_.end()) {
            const auto& c = it->second.content;
            append_entities(c.dims, c.hatches, c.splines, c.constraints, content.entities);
            content.blocks.set_references(c.refs); rec.underlays = it->second.underlays.all();
        }
        std::string annotation="1:1";
        if(id==active_space_id()) annotation=canvas_.annotation_scale_name();
        else if(auto it=space_states_.find(id);it!=space_states_.end()) annotation=it->second.annotation_scale;
        m.space_annotation_scales.push_back(annotation);
        rec.annotations = gfile::encode(content);
        m.spaces.push_back(std::move(rec));
    };
    for (std::size_t i = 0; i < sketches_.size(); ++i) append_space(sketches_[i].space_id, i, false);
    for (std::size_t i = 0; i < layouts_.size(); ++i) append_space(layouts_[i].space_id, i, true);
    m.last_space_id = last_space_id_;
    m.layouts = layouts_; m.page_setups = page_setups_; m.vars = vars_.all();
    std::erase_if(m.vars,[](const auto& var){return var.name=="INSUNITS" || var.name=="CANNOSCALE" || var.name=="CANNOSCALEVALUE";});
    if (active_layout_ >= 0 && std::size_t(active_layout_) < m.layouts.size()) {
        auto& l = m.layouts[std::size_t(active_layout_)];
        l.paper = plot_paper_; l.landscape = plot_landscape_; l.scale_den = plot_scale_den_;
        l.dx = plot_dx_; l.dy = plot_dy_;
    }
    m.text_height = text_height_; m.text_width = text_width_; m.linetype_scale = linetype_scale_;
    m.precision = precision_;
    if (layouts_.empty()) {
        m.paper = plot_paper_; m.landscape = plot_landscape_; m.scale_den = plot_scale_den_;
        m.dx = plot_dx_; m.dy = plot_dy_;
    }
    gfile::state::Writer out;
    for (const auto& layout:layouts_) { m.plot_style_names.push_back(layout.plot_style_name); m.plot_style_data.push_back(layout.plot_style_data); }
    for(const auto& layout:layouts_) { std::vector<std::vector<gplot::PaperPoint>> clips; for(const auto& vp:layout.viewports) clips.push_back(vp.clip_polygon); m.viewport_clips.push_back(std::move(clips)); }
    for(const auto& layout:layouts_) { m.imported_papers.push_back(layout.imported_paper); m.paper_flags.push_back((layout.custom_paper?1:0)|(layout.direct_model_view?2:0)); }
    m.annotation_scales=annotation_scales_.all();
    out(std::uint32_t{8}); m.fields(out);
    out(m.spaces, m.last_space_id, m.plot_style_names, m.plot_style_data, m.viewport_clips, m.imported_papers, m.paper_flags, m.annotation_scales, m.space_annotation_scales);
    out(gfile::state::underlay_details(m.underlays));
    for(const auto& space:m.spaces)out(gfile::state::underlay_details(space.underlays));
    out(gcm::u8(drawing_discipline_), project_location_);
    return std::move(out.bytes);
}

bool Shell::restore_metadata(const std::vector<gcm::u8>& bytes, bool validate_only, const gfile::Contents* document) {
    Metadata m; gfile::Contents tables, blocks, groups;
    std::uint32_t version;
    std::vector<gfile::Contents> annotations;
    try {
        gfile::state::Reader in{bytes};
        in(version);
        if (version < 1 || version > 8) return false;
        m.fields(in);
        if (version >= 2) in(m.spaces, m.last_space_id);
        if(version>=3) {
            in(m.plot_style_names,m.plot_style_data);
            if(m.plot_style_names.size()!=m.layouts.size()||m.plot_style_data.size()!=m.layouts.size()) return false;
            for(std::size_t i=0;i<m.layouts.size();++i) {
                if(!m.plot_style_data[i].empty()&&!gplot::read_plot_style(m.plot_style_data[i]).ok) return false;
                m.layouts[i].plot_style_name=std::move(m.plot_style_names[i]);
                m.layouts[i].plot_style_data=std::move(m.plot_style_data[i]);
            }
        }
        if(version>=4) {
            in(m.viewport_clips);
            if(m.viewport_clips.size()!=m.layouts.size()) return false;
            for(std::size_t i=0;i<m.layouts.size();++i) {
                if(m.viewport_clips[i].size()!=m.layouts[i].viewports.size()) return false;
                for(std::size_t j=0;j<m.viewport_clips[i].size();++j) {
                    auto& vp=m.layouts[i].viewports[j]; vp.clip_polygon=std::move(m.viewport_clips[i][j]);
                    if(!gplot::PaperClip{vp.lo,vp.hi,vp.clip_polygon}.valid()) return false;
                }
            }
        }
        if(version>=5) {
            in(m.imported_papers,m.paper_flags);
            if(m.imported_papers.size()!=m.layouts.size() || m.paper_flags.size()!=m.layouts.size()) return false;
            for(std::size_t i=0;i<m.layouts.size();++i) {
                auto& l=m.layouts[i]; l.imported_paper=std::move(m.imported_papers[i]); l.custom_paper=m.paper_flags[i]&1; l.direct_model_view=m.paper_flags[i]&2;
                if(m.paper_flags[i]>3 || (l.custom_paper && (!std::isfinite(l.imported_paper.width) || !std::isfinite(l.imported_paper.height) || l.imported_paper.width<=0 || l.imported_paper.height<=0))) return false;
            }
        }
        if(version>=6) {
            in(m.annotation_scales,m.space_annotation_scales);
            if(m.space_annotation_scales.size()!=m.spaces.size()) return false;
            std::vector<std::string> names;
            for(const auto& scale:m.annotation_scales) {
                if(scale.name.empty()||!std::isfinite(scale.paper)||!std::isfinite(scale.drawing)||scale.paper<=0||scale.drawing<=0||
                   scale.factor()<1e-6||scale.factor()>1e6||std::find(names.begin(),names.end(),scale.name)!=names.end()) return false;
                if(scale.name=="1:1"&&scale.factor()!=1) return false;
                names.push_back(scale.name);
            }
            if(std::find(names.begin(),names.end(),"1:1")==names.end()) return false;
            for(const auto& name:m.space_annotation_scales) if(std::find(names.begin(),names.end(),name)==names.end()) return false;
        }
        if(version>=7) {
            std::vector<gfile::state::UnderlayDetails> details;in(details);
            if(!gfile::state::apply_underlay_details(m.underlays,details))return false;
            for(auto& space:m.spaces) {
                in(details);if(!gfile::state::apply_underlay_details(space.underlays,details))return false;
            }
        }
        if(version>=8) {
            in(m.drawing_discipline,m.project_location);
            if(m.drawing_discipline>gcm::u8(DrawingDiscipline::Mechanical)) return false;
        }
        in.finish();
        if (!gfile::decode(m.tables, tables).ok || !gfile::decode(m.blocks, blocks).ok || !gfile::decode(m.groups, groups).ok) return false;
        if (m.layouts.size() != tables.geometry.size() || m.group_names.size() != groups.geometry.size() || m.model.materials.empty()) return false;
        if (version >= 2) {
            const auto model_count = document ? std::max<std::size_t>(1, document->spaces.size()) : sketches_.size();
            if (m.spaces.size() != model_count + m.layouts.size()) return false;
            std::vector<gcm::u32> ids;
            for (std::size_t i = 0; i < m.spaces.size(); ++i) {
                const auto& rec = m.spaces[i];
                if (!rec.id || rec.id > m.last_space_id || rec.paper != (i >= model_count) ||
                    rec.index != (rec.paper ? i - model_count : i)) return false;
                ids.push_back(rec.id);
                gfile::Contents c;
                if (!gfile::decode(rec.annotations, c).ok) return false;
                annotations.push_back(std::move(c));
            }
            std::sort(ids.begin(), ids.end());
            if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return false;
        }
        if (validate_only) {
            if (!document) return true;
            Modeller probe(vol_);
            std::vector<gtopo::Body> bodies;
            for (const auto& f : document->foreign)
                if (f.kind == kSolidsFrame && !decode_solids(f.payload, bodies)) return false;
            probe.set_bodies(std::move(bodies));
            for (const auto& f : document->foreign)
                if (f.kind == kFeaturesFrame)
                    probe.set_history(decode_feature_history(f.payload, probe.bodies().size()));
            return probe.load_metadata(m.model);
        }
        if (!modeller_.load_metadata(m.model)) return false;
    } catch (const std::exception&) { return false; }
    store_active_sketch();
    const auto selected_space = active_space_id();
    const std::string selected_layout = active_layout() ? active_layout()->name : std::string{};
    drawing_discipline_ = DrawingDiscipline(m.drawing_discipline);
    project_type_ = drawing_discipline_ == DrawingDiscipline::Architectural ? "architecture" :
                    drawing_discipline_ == DrawingDiscipline::Mechanical ? "mechanical" : "general";
    project_location_ = std::move(m.project_location);
    project_ask_done_ = version >= 8;
    plan_panel_serial_ = ~gcm::u64{0};
    layers_ = std::move(tables.layers); linetypes_ = std::move(tables.linetypes);
    canvas_.blocks() = std::move(blocks.blocks);
    cached_block_revision_ = ~gcm::u64{0};
    canvas_.multilines() = std::move(tables.multilines);
    layers_.set_current(m.current_layer);
    canvas_.set_current_layer(layers_.current()); modeller_.set_current_layer(layers_.current());
    groups_.clear();
    for (std::size_t i = 0; i < m.group_names.size(); ++i) {
        auto& g = groups.geometry[i];
        for (std::size_t j = 0; j < g.shapes.size(); ++j) g.shapes[j].layer = g.layers[j];
        groups_[m.group_names[i]] = std::move(g.shapes);
    }
    views_ = gview::ViewTable{};
    const auto standard_views = views_.all();
    for (const auto& v : standard_views) views_.remove(v.name);
    for (auto& v : m.views) views_.add(std::move(v));
    section_nx_ = m.section_nx; section_ny_ = m.section_ny; section_nz_ = m.section_nz; section_d_ = m.section_d;
    annotation_scales_=gdim::ScaleList{};
    if(version>=6) {
        const auto defaults=annotation_scales_.all();
        for(const auto& scale:defaults) annotation_scales_.remove(scale.name);
        for(const auto& scale:m.annotation_scales) annotation_scales_.add(scale);
    }
    text_styles_ = gtext::TextStyleTable{};
    for (auto& s : m.text_styles) text_styles_.add(std::move(s));
    text_styles_.set_current(m.text_style);
    canvas_.dim_styles() = gdim::StyleTable{};
    for (auto& s : m.dim_styles) canvas_.dim_styles().add(std::move(s));
    canvas_.dim_styles().set_current(m.dim_style);
    params_ = gunit::ParameterTable{};
    for (auto& p : m.parameters) params_.add(std::move(p));
    pmi_.clear();
    for (auto& dims : m.pmi) {
        gtopo::PmiSet set; for (auto& d : dims) set.add(std::move(d));
        pmi_.push_back(std::move(set));
    }
    canvas_.underlays() = gdoc::UnderlayTable{};
    for (auto& u : m.underlays) canvas_.underlays().attach(std::move(u));
    layouts_ = std::move(m.layouts); page_setups_ = std::move(m.page_setups);
    for (std::size_t i = 0; i < layouts_.size(); ++i) {
        layouts_[i].shapes = std::move(tables.geometry[i].shapes);
        for (std::size_t j = 0; j < layouts_[i].shapes.size(); ++j)
            layouts_[i].shapes[j].layer = tables.geometry[i].layers[j];
        read_text_entities(tables.entities, static_cast<gcm::u32>(i), layouts_[i].notes);
        refresh_views(layouts_[i]);
    }
    active_layout_ = layouts_.empty() ? -1 : 0;
    for (std::size_t i = 0; i < layouts_.size(); ++i)
        if (layouts_[i].name == selected_layout) active_layout_ = int(i);
    plot_paper_ = m.paper; plot_landscape_ = m.landscape; plot_scale_den_ = m.scale_den;
    plot_dx_ = m.dx; plot_dy_ = m.dy;
    if (active_layout_ >= 0) layout_load(active_layout_);
    if (version >= 2) {
        last_space_id_ = m.last_space_id;
        for (std::size_t i = 0; i < m.spaces.size(); ++i) {
            const auto& rec = m.spaces[i];
            if (rec.paper) layouts_[rec.index].space_id = rec.id;
            else sketches_[rec.index].space_id = rec.id;
            auto& state = space_states_[rec.id];
            state.annotation_scale=version>=6?m.space_annotation_scales[i]:"1:1";
            state.annotation_factor=annotation_scales_.find(state.annotation_scale)->factor();
            state.content.dims.clear(); state.content.hatches.clear();
            state.content.splines.clear(); state.content.constraints.clear();
            read_entities(annotations[i].entities, state.content.dims, state.content.hatches,
                          state.content.splines, state.content.constraints);
            state.content.refs = annotations[i].blocks.references();
            state.underlays = gdoc::UnderlayTable{};
            for (const auto& u : rec.underlays) state.underlays.attach(u);
        }
        for (std::size_t i = 0; i < layouts_.size(); ++i)
            if (layouts_[i].space_id == selected_space) { layout_load(int(i)); break; }
        if (in_paper_space_ && layouts_.empty()) in_paper_space_ = false;
        load_active_space();
    } else {
        ensure_space_ids();
        store_active_sketch();
    }
    vars_ = gcmd::SysVars{}; vars_.add_standard();
    for (const auto& v : m.vars) {
        if (auto* live = vars_.find(v.name)) *live = v;
    }
    text_height_ = m.text_height; text_width_ = m.text_width;
    linetype_scale_ = m.linetype_scale; precision_ = m.precision;
    var_filletrad_ = var_chamfera_ = var_textsize_ = var_ltscale_ = var_osmode_ = -1e30;
    var_pdmode_ = var_pdsize_ = -1e30;
    vars_.set_text("TEXTSTYLE",text_styles_.current());vars_.set_text("DIMSTYLE",canvas_.dim_styles().current());
    vars_.set_text("CLAYER",layers_.current());
    vars_.set_text("CANNOSCALE",canvas_.annotation_scale_name());
    if(const auto* style=canvas_.dim_styles().find(canvas_.dim_styles().current())) vars_.set_real("DIMSCALE",style->dimscale);
    drafting_seen_.clear();drafting_text_seen_.clear();
    apply_sysvars(); publish_sysvars();
    canvas_.set_linetypes(linetypes_.all());
    invalidate_frame_caches();
    return true;
}
}
