#include "ui/shell.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace gcapp {

namespace T = gtheme::token;

bool Shell::document_blank() const noexcept {
    if (document_dirty()) return false;
    if (!document_path_.empty()) return false;
    if (!canvas_.shapes().empty() || !canvas_.texts().empty()) return false;
    if (!modeller_.bodies().empty()) return false;
    if (!layouts_.empty() || !page_setups_.empty() || params_.size() != 0) return false;
    if (!canvas_.dimensions().empty() || !canvas_.hatches().empty() ||
        !canvas_.splines().empty() || canvas_.blocks().size() != 0) return false;
    for (const auto& sk : sketches_)
        if (!sk.shapes.empty() || !sk.texts.empty()) return false;
    return true;
}

void Shell::reset_document() {
    pending_extent_fit_.reset();
    refit_bounds_.reset();
    drawing_discipline_ = DrawingDiscipline::General;
    project_type_ = "general";
    project_location_.clear();
    project_ask_done_ = false;
    project_ask_step_ = 0;
    cancel_viewport_drag();
    // Leave paper space first or set_mode stores the old sheet in the new document.
    in_paper_space_ = false;
    space_states_.clear(); last_space_id_ = 0;
    layers_ = gdoc::LayerTable{};
    doc_ = gdoc::Document{};
    layouts_.clear();
    page_setups_.clear();
    active_layout_ = -1;
    plot_paper_ = "A3";
    plot_landscape_ = true;
    plot_scale_den_ = 0;
    plot_dx_ = plot_dy_ = 0.0;
    linetypes_ = gdoc::LinetypeTable{};
    text_styles_ = gtext::TextStyleTable{};
    annotation_scales_=gdim::ScaleList{};
    params_ = gunit::ParameterTable{};
    pmi_.clear();
    views_ = gview::ViewTable{};
    history_ = gview::ViewHistory{};
    groups_.clear();
    text_height_ = 0.25;
    text_width_ = 0.0;
    linetype_scale_ = 1.0;
    precision_ = 3;
    section_nx_ = section_ny_ = section_nz_ = section_d_ = 0.0;
    document_lock_.reset();
    document_stamp_.clear(); external_change_=false;
    read_only_ = false;
    lock_holder_.clear();
    recovery_base_ = gfile::Contents{};
    recovery_written_ = false;
    recovery_id_ = recovery_identity();
    recovered_from_.clear();
    autosave_at_ = 0.0;
    autosave_serial_ = 0;
    views_need_refresh_ = false;
    const auto unit = canvas_.display_unit();
    const auto snaps = canvas_.snaps().settings;
    const auto polar = canvas_.polar();
    const bool grid = canvas_.grid_snap();
    canvas_ = Canvas{};
    canvas_.set_display_unit(unit);
    canvas_.snaps().settings = snaps;
    canvas_.polar() = polar;
    canvas_.set_grid_snap(grid);
    canvas_.view().set_viewport(vw_, vh_);
    canvas_.set_text_font(&font_);
    canvas_.set_font_registry(&fonts_);
    vars_ = gcmd::SysVars{};
    vars_.add_standard();
    publish_sysvars();
    modeller_ = Modeller(vol_);
    sketches_.assign(1, SketchDoc{});
    sketches_[0].name = "Sketch 1";
    sketches_[0].plane = gtopo::SketchPlane::world_xy();
    sketches_[0].on_face = false;
    active_sketch_ = 0;
    document_path_.clear();
    sel_body_ = -1;
    hover_body_ = -1;
    clear_solid_selection();
    hover_target_ = {};
    text_placing_ = false;
    text_entry_ = false;
    set_mode(Mode::Solid);
    set_mode(Mode::Sketch);
    navigated_ = false;
    camera_.look_plan();
    camera_.set_target(gview::Point3::from(0, 0, 0));
    fit_width(default_width_m_);
    invalidate_frame_caches();
    saved_serial_ = document_serial();
    clear_journal();
}

// Cache keys can collide across tabs; drop every per-document cache.
void Shell::invalidate_frame_caches() {
    sheet_preview_key_=~gcm::u64{0};
    cached_block_revision_ = ~0ull;
    plot_cache_.valid = false;
    dims_synced_ = ~0ull;
    text_editing_ = static_cast<std::size_t>(-1);
    sel_feature_body_ = sel_feature_ = -1;
    lifted_key_ = ~0ull;
    static_label_mesh_revision_ = ~0ull;
    static_label_meshes_.clear();
    sketch_segs_key_ = ~0ull;
    static_lines_.clear();
    static_points_.clear();
    static_arcs_.clear();
    mesh_dirty_ = true;
    hover_pending_ = false;
    hover_done_ = false;
}

void Shell::swap_with(TabState& t) {
    cached_block_revision_ = ~0ull;
    std::swap(canvas_, t.canvas);
    std::swap(camera_, t.camera);
    std::swap(layers_, t.layers);
    std::swap(doc_, t.doc);
    std::swap(modeller_, t.modeller);
    std::swap(sketches_, t.sketches);
    std::swap(space_states_, t.space_states);
    std::swap(last_space_id_, t.last_space_id);
    std::swap(active_sketch_, t.active_sketch);
    std::swap(mode_, t.mode);
    std::swap(drawing_discipline_, t.drawing_discipline);
    std::swap(project_type_, t.project_type);
    std::swap(project_location_, t.project_location);
    std::swap(project_ask_done_, t.project_ask_done);
    std::swap(project_ask_step_, t.project_ask_step);
    std::swap(active_tool_id_, t.tool);
    std::swap(document_path_, t.path);
    std::swap(tab_title_, t.title);
    std::swap(navigated_, t.navigated);
    std::swap(last_edit_was_solid_, t.last_edit_was_solid);
    std::swap(sel_body_, t.sel_body);
    std::swap(sel_target_, t.sel_target);
    std::swap(selected_targets_, t.selected_targets);
    std::swap(selected_bodies_, t.selected_bodies);
    std::swap(saved_serial_, t.saved_serial);
    std::swap(sketch_edits_, t.sketch_edits);
    std::swap(undo_journal_, t.undo_journal);
    std::swap(redo_journal_, t.redo_journal);
    std::swap(seen_canvas_, t.seen_canvas);
    std::swap(seen_canvas_marks_, t.seen_canvas_marks);
    std::swap(seen_solid_, t.seen_solid);
    std::swap(seen_metadata_, t.seen_metadata);
    std::swap(metadata_serial_, t.metadata_serial);
    std::swap(layouts_, t.layouts);
    std::swap(page_setups_, t.page_setups);
    std::swap(active_layout_, t.active_layout);
    std::swap(in_paper_space_, t.in_paper_space);
    std::swap(plot_paper_, t.plot_paper);
    std::swap(plot_landscape_, t.plot_landscape);
    std::swap(plot_scale_den_, t.plot_scale_den);
    std::swap(plot_dx_, t.plot_dx);
    std::swap(plot_dy_, t.plot_dy);
    std::swap(linetypes_, t.linetypes);
    std::swap(text_styles_, t.text_styles);
    drafting_seen_.clear(); drafting_text_seen_.clear();
    std::swap(params_, t.params);
    std::swap(pmi_, t.pmi);
    std::swap(views_, t.views);
    std::swap(vars_, t.vars);
    std::swap(groups_, t.groups);
    std::swap(text_height_, t.text_height);
    std::swap(text_width_, t.text_width);
    std::swap(linetype_scale_, t.linetype_scale);
    std::swap(precision_, t.precision);
    std::swap(section_nx_, t.section_nx);
    std::swap(section_ny_, t.section_ny);
    std::swap(section_nz_, t.section_nz);
    std::swap(section_d_, t.section_d);
    std::swap(document_stamp_, t.document_stamp);
    std::swap(external_change_, t.external_change);
    std::swap(document_lock_, t.document_lock);
    std::swap(read_only_, t.read_only);
    std::swap(lock_holder_, t.lock_holder);
    std::swap(recovery_base_, t.recovery_base);
    std::swap(recovery_written_, t.recovery_written);
    std::swap(recovery_id_, t.recovery_id);
    std::swap(recovered_from_, t.recovered_from);
    std::swap(autosave_at_, t.autosave_at);
    std::swap(autosave_serial_, t.autosave_serial);
    std::swap(views_need_refresh_, t.views_need_refresh);
    std::swap(history_, t.view_history);
    publish_sysvars();
}

std::vector<Shell::TabInfo> Shell::tabs() const {
    std::vector<TabInfo> out;
    for (int i = 0; i < tab_count(); ++i) {
        TabInfo info;
        const bool live = i == active_tab_;
        info.title = live ? tab_title_ : tabs_[static_cast<std::size_t>(i)].title;
        info.path = live ? document_path_ : tabs_[static_cast<std::size_t>(i)].path;
        info.active = live;
        info.dirty = live ? document_dirty()
                          : (tabs_[static_cast<std::size_t>(i)].canvas.edit_serial() +
                             tabs_[static_cast<std::size_t>(i)].modeller.edit_serial() +
                             tabs_[static_cast<std::size_t>(i)].sketch_edits +
                             tabs_[static_cast<std::size_t>(i)].metadata_serial +
                             static_cast<gcm::u64>(tabs_[static_cast<std::size_t>(i)].sketches.size()) * 1000003u)
                            != tabs_[static_cast<std::size_t>(i)].saved_serial;
        out.push_back(std::move(info));
    }
    return out;
}

int Shell::new_tab() {
    if (tabs_.empty()) {
        tabs_.emplace_back(vol_);
        active_tab_ = 0;
        tab_title_ = "Untitled " + std::to_string(untitled_counter_++);
        return 0;
    }
    flush_edits();
    const int previous_tab = active_tab_;
    swap_with(tabs_[static_cast<std::size_t>(active_tab_)]);
    tabs_.emplace_back(vol_);
    active_tab_ = tab_count() - 1;
    const TabState& prev = tabs_[static_cast<std::size_t>(previous_tab)];
    canvas_.set_display_unit(prev.canvas.display_unit());
    canvas_.snaps().settings = prev.canvas.snaps().settings;
    canvas_.set_grid_snap(prev.canvas.grid_snap());
    canvas_.polar() = prev.canvas.polar();
    layers_ = gdoc::LayerTable{};
    sketches_.clear();
    reset_document();
    tab_title_ = "Untitled " + std::to_string(untitled_counter_++);
    hover_body_ = -1;
    hover_target_ = {};
    // A fresh camera needs the viewport or hover lands offset from the pointer.
    canvas_.view().set_viewport(vw_, vh_);
    camera_.set_viewport(vw_, vh_);
    fit_width(default_width_m_);
    sync_view_from_camera();
    invalidate_frame_caches();
    return active_tab_;
}

void Shell::switch_tab(int index) {
    pending_extent_fit_.reset();
    refit_bounds_.reset();
    if (index < 0 || index >= tab_count() || index == active_tab_) return;
    flush_edits();
    swap_with(tabs_[static_cast<std::size_t>(active_tab_)]);
    swap_with(tabs_[static_cast<std::size_t>(index)]);
    active_tab_ = index;
    reveal_layout_tab(selected_layout());
    hover_body_ = -1;
    hover_target_ = {};
    text_placing_ = false;
    text_entry_ = false;
    picking_sketch_plane_ = false;
    invalidate_frame_caches();
    canvas_.view().set_viewport(vw_, vh_);
    camera_.set_viewport(vw_, vh_);
    rebuild_sketch_segments();
    sync_view_from_camera();
    log_.push_back("tab: " + tab_title_);
}

void Shell::close_tab(int index) {
    if (index < 0 || index >= tab_count()) return;
    if (tab_count() == 1) {
        reset_document();
        tab_title_ = "Untitled " + std::to_string(untitled_counter_++);
        log_.push_back("closed " + tab_title_);
        return;
    }
    if (tabs()[static_cast<std::size_t>(index)].dirty)
        log_.push_back("closed " + tabs()[static_cast<std::size_t>(index)].title + " with unsaved changes");
    if (index == active_tab_) {
        const int to = index > 0 ? index - 1 : 1;
        switch_tab(to);
    }
    tabs_.erase(tabs_.begin() + index);
    if (active_tab_ > index) --active_tab_;
}

void Shell::draw_document_tabs(gui::Rect& cursor, float reserve) {
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    const float unit = float(px(1));
    const float h = cursor.h;
    const float close_w = float(px(20));
    const float plus_w = (tablet_mode_ ? 44 : 28) * unit;
    const float max_w = 180 * unit;
    ui_ctx_.push_id("tabs");
    const auto infos = tabs();
    int want_switch = -1, want_close = -1;
    for (int i = 0; i < static_cast<int>(infos.size()); ++i) {
        const TabInfo& info = infos[static_cast<std::size_t>(i)];
        float w = ui_ctx_.measure(info.title, st.font_size) + close_w + float(px(28));
        w = std::min(w, max_w);
        if (cursor.w < w + plus_w + reserve) break;
        gui::Rect r = cursor.cut_left(w);
        r.y = cursor.y; r.h = h;
        ui_ctx_.push_id("t" + std::to_string(i));
        const auto in = ui_ctx_.interact(ui_ctx_.id("tab"), r);
        ui_ctx_.announce(ui_ctx_.id("tab"), gui::Context::Role::Button, info.title, "", r, info.active);
        const float rad = float(px(8));
        if (info.active) {
            ui_ctx_.draw().round_rect(r, rad, theme.get(T::ui_surface_raised));
            ui_ctx_.draw().rect({r.x + rad, r.bottom() - float(px(2)), r.w - 2.0f * rad, float(px(2))},
                                theme.get(T::ui_accent));
        } else if (in.hovered) {
            gui::Color layer = theme.get(T::ui_accent);
            layer.a = st.hover_opacity;
            ui_ctx_.draw().round_rect(r, rad, layer);
        }
        gui::Rect label = r;
        label.x += float(px(12));
        label.w -= float(px(12)) + close_w + float(px(4));
        ui_ctx_.draw().text(label, info.dirty ? info.title + " \xe2\x80\xa2" : info.title, st.font_size,
                            theme.get(info.active ? T::ui_text : T::ui_text_dim), gui::TextAlign::Left);
        gui::Rect x{r.right() - close_w - float(px(4)), r.y + (h - close_w) * 0.5f, close_w, close_w};
        const auto xin = ui_ctx_.interact(ui_ctx_.id("close"), x);
        if (info.active || in.hovered || xin.hovered) {
            if (xin.hovered) {
                gui::Color layer = theme.get(T::ui_accent);
                layer.a = st.hover_opacity;
                ui_ctx_.draw().round_rect(x, close_w * 0.5f, layer);
            }
            ui_ctx_.draw().text(x, "\xc3\x97", st.font_size, theme.get(T::ui_text_dim), gui::TextAlign::Centre);
        }
        if (xin.clicked) want_close = i;
        else if (in.clicked) want_switch = i;
        ui_ctx_.pop_id();
        cursor.cut_left(float(px(2)));
    }
    {
        gui::Rect p = cursor.cut_left(plus_w);
        p.y = cursor.y; p.h = h;
        const auto id = ui_ctx_.id("new");
        const auto in = ui_ctx_.interact(id, p);
        ui_ctx_.announce(id, gui::Context::Role::Button, "New tab", "", p);
        if (in.hovered) {
            gui::Color layer = theme.get(T::ui_accent);
            layer.a = st.hover_opacity;
            ui_ctx_.draw().round_rect(p, float(px(8)), layer);
        }
        gui::draw_icon(ui_ctx_, gui::Icon::Plus, p, theme.get(T::ui_text), 1.2f);
        if (in.clicked) run_command("NEWTAB");
    }
    ui_ctx_.pop_id();
    if (want_close >= 0) close_tab(want_close);
    else if (want_switch >= 0) switch_tab(want_switch);
}

void Shell::draw_tab_bar(gui::Rect& screen) {
    const auto& theme = themes_.active();
    gui::Rect bar = screen.cut_top(float(px(36)));
    ui_ctx_.draw().rect(bar, theme.get(T::ui_surface));
    ui_ctx_.draw().line({bar.x, bar.bottom()}, {bar.right(), bar.bottom()}, 1.0f, theme.get(T::ui_border));
    gui::Rect cursor = bar.deflate(gui::Insets::xy(float(px(8)), float(px(4))));
    draw_document_tabs(cursor, 0);
}

float Shell::document_pickers_width() const {
    const gui::Style& st = ui_ctx_.style();
    const float unit = float(px(1));
    const auto& info = tabs()[std::size_t(std::clamp(active_tab_, 0, int(tabs().size()) - 1))];
    const float doc = std::clamp(ui_ctx_.measure(info.title, st.font_size) + 84 * unit, 130 * unit, 240 * unit);
    const std::string layout = selected_layout() < 0 ? "Model" : layouts_[std::size_t(selected_layout())].name;
    const float sheet = std::clamp(ui_ctx_.measure(layout, st.font_size) + 84 * unit, 120 * unit, 200 * unit);
    return doc + sheet + 6 * unit;
}

void Shell::draw_document_pickers(const gui::Rect& area, const gui::Rect& row) {
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    const float unit = float(px(1)), line = (tablet_mode_ ? 44 : 32) * unit, gap = 6 * unit;
    const auto infos = tabs();
    const int active = std::clamp(active_tab_, 0, int(infos.size()) - 1);
    const std::string doc_title = infos[std::size_t(active)].title + (infos[std::size_t(active)].dirty ? " \xe2\x80\xa2" : "");
    const std::string sheet = selected_layout() < 0 ? std::string("Model") : layouts_[std::size_t(selected_layout())].name;
    gui::Rect cursor = area;
    const float doc_w = std::min(cursor.w * .58f, std::clamp(ui_ctx_.measure(doc_title, st.font_size) + 84 * unit, 130 * unit, 240 * unit));
    const gui::Rect doc_button = cursor.cut_left(doc_w);
    cursor.cut_left(gap);
    const gui::Rect sheet_button = cursor.cut_left(std::min(cursor.w, std::clamp(ui_ctx_.measure(sheet, st.font_size) + 84 * unit, 120 * unit, 200 * unit)));
    ui_ctx_.push_id("document-pickers");
    const auto pill = [&](const gui::Rect& r, const std::string& label, const std::string& text, gui::Icon icon, int which) {
        const auto id = ui_ctx_.id(label.substr(0, label.find(':')));
        const auto in = ui_ctx_.interact(id, r);
        const bool open = header_menu_ == which;
        ui_ctx_.announce(id, gui::Context::Role::Button, label, open ? "expanded" : "collapsed", r, open);
        const gui::Rect shape = r.deflate(gui::Insets::xy(0, std::max(3 * unit, (r.h - 32 * unit) * .5f)));
        ui_ctx_.draw().round_rect(shape, 8 * unit, theme.get(open ? T::ui_active : in.hovered || in.held ? T::ui_hover : T::ui_surface_raised));
        ui_ctx_.draw().border(shape, 8 * unit, 1, theme.get(T::ui_border));
        gui::Rect inner = shape.deflate(gui::Insets::xy(6 * unit, 0));
        gui::draw_icon(ui_ctx_, icon, inner.cut_left(24 * unit), theme.get(T::ui_text_dim), 1.2f);
        gui::draw_icon(ui_ctx_, gui::Icon::ChevronDown, inner.cut_right(20 * unit), theme.get(T::ui_text), 1.1f);
        ui_ctx_.draw().text(inner.deflate(gui::Insets::xy(6 * unit, 0)), text, st.font_size, theme.get(T::ui_text), gui::TextAlign::Left);
        if (in.clicked) { header_menu_ = open ? 0 : which; header_menu_page_ = 0; }
    };
    pill(doc_button, "Document: " + infos[std::size_t(active)].title, doc_title, gui::Icon::Copy, 1);
    pill(sheet_button, "Layout: " + sheet, sheet, gui::Icon::Table, 2);
    if (header_menu_ == 1 || header_menu_ == 2) {
        struct Entry { std::string label; std::function<void()> action; bool selected = false; bool enabled = true; std::function<void()> close; bool rule = false; };
        std::vector<Entry> entries;
        if (header_menu_ == 1) {
            for (int i = 0; i < int(infos.size()); ++i) {
                const auto& info = infos[std::size_t(i)];
                entries.push_back({info.title + (info.dirty ? " \xe2\x80\xa2" : ""), [this, i] { switch_tab(i); }, i == active, true,
                                   [this, i] { close_tab(i); }});
            }
            entries.push_back({"", {}, false, false, {}, true});
            entries.push_back({"New document", [this] { run_command("NEWTAB"); }});
            entries.push_back({"Open\xe2\x80\xa6", [this] { run_command("OPEN"); }});
        } else {
            entries.push_back({"Model", [this] { select_layout(-1); }, selected_layout() < 0});
            for (int i = 0; i < int(layouts_.size()); ++i)
                entries.push_back({layouts_[std::size_t(i)].name, [this, i] { select_layout(i); }, selected_layout() == i});
            entries.push_back({"", {}, false, false, {}, true});
            const int at = selected_layout();
            entries.push_back({"Previous layout", [this, at] { select_layout(at - 1); }, false, at >= 0});
            entries.push_back({"Next layout", [this, at] { select_layout(at + 1); }, false, at + 1 < int(layouts_.size())});
            entries.push_back({"New layout", [this] { run_command("LAYOUT NEW"); select_layout(active_layout_); }});
        }
        const gui::Rect anchor = header_menu_ == 1 ? doc_button : sheet_button;
        float width = 220 * unit;
        for (const auto& e : entries) width = std::max(width, ui_ctx_.measure(e.label, st.font_size) + 72 * unit);
        width = std::min(width, row.w - 2 * gap);
        const float top = row.bottom() + 4 * unit;
        const int capacity = std::max(3, int((float(vh_) - top - 8 * unit) / line) - 1);
        const int pages = std::max(1, (int(entries.size()) + capacity - 1) / capacity);
        header_menu_page_ = std::clamp(header_menu_page_, 0, pages - 1);
        const int first = header_menu_page_ * capacity, last = std::min(int(entries.size()), first + capacity);
        float height = 8 * unit + (pages > 1 ? line : 0);
        for (int i = first; i < last; ++i) height += entries[std::size_t(i)].rule ? 9 * unit : line;
        const gui::Rect drop{std::clamp(anchor.x, row.x + gap, row.right() - gap - width), top, width, height};
        ui_ctx_.begin_overlay(drop);
        ui_ctx_.push_id(header_menu_ == 1 ? "document-menu" : "layout-menu");
        ui_ctx_.draw().shadow(drop, 6 * unit, 12 * unit, theme.get(T::ui_shadow));
        ui_ctx_.draw().round_rect(drop, 6 * unit, theme.get(T::ui_surface_raised));
        ui_ctx_.draw().border(drop, 6 * unit, 1, theme.get(T::ui_border));
        std::function<void()> chosen;
        const auto item = [&](gui::Rect r, const std::string& label, bool selected, bool enabled) {
            const auto id = ui_ctx_.id(label);
            const auto in = ui_ctx_.interact(id, r, enabled);
            ui_ctx_.announce(id, gui::Context::Role::Button, label, "", r, selected, !enabled);
            if (selected || in.hovered || in.held) ui_ctx_.draw().round_rect(r, 4 * unit, theme.get(selected ? T::ui_accent : T::ui_hover));
            auto ink = theme.get(selected ? T::ui_accent_text : T::ui_text);
            if (!enabled) ink.a = .38f;
            ui_ctx_.draw().text(r.deflate(gui::Insets::xy(12 * unit, 0)), label, st.font_size, ink, gui::TextAlign::Left);
            return in.clicked;
        };
        float y = drop.y + 4 * unit;
        for (int i = first; i < last; ++i) {
            const auto& e = entries[std::size_t(i)];
            if (e.rule) {
                ui_ctx_.draw().line({drop.x + 8 * unit, y + 4 * unit}, {drop.right() - 8 * unit, y + 4 * unit}, 1, theme.get(T::ui_border));
                y += 9 * unit;
                continue;
            }
            gui::Rect r{drop.x + 4 * unit, y, drop.w - 8 * unit, line};
            ui_ctx_.push_id(std::to_string(i));
            if (e.close) {
                const gui::Rect x = r.cut_right(line);
                const std::string close_label = "Close " + infos[std::size_t(i)].title;
                const auto xid = ui_ctx_.id("close");
                const auto xin = ui_ctx_.interact(xid, x);
                ui_ctx_.announce(xid, gui::Context::Role::Button, close_label, "", x);
                if (xin.hovered || xin.held) ui_ctx_.draw().round_rect(x.deflate(gui::Insets::all(4 * unit)), 4 * unit, theme.get(T::ui_hover));
                gui::draw_icon(ui_ctx_, gui::Icon::Close, x, theme.get(T::ui_text_dim), 1.0f);
                if (xin.clicked) chosen = e.close;
            }
            if (item(r, e.label, e.selected, e.enabled) && !chosen) chosen = e.action;
            ui_ctx_.pop_id();
            y += line;
        }
        if (pages > 1) {
            gui::Rect nav{drop.x + 4 * unit, drop.bottom() - line - 4 * unit, drop.w - 8 * unit, line};
            if (item(nav.cut_left(nav.w * .5f), "Previous page", false, header_menu_page_ > 0)) --header_menu_page_;
            if (item(nav, "Next page", false, header_menu_page_ + 1 < pages)) ++header_menu_page_;
        }
        const bool dismiss = input_.key_escape ||
            (input_.pressed && !drop.contains(input_.mouse) && !anchor.contains(input_.mouse));
        ui_ctx_.pop_id();
        ui_ctx_.end_overlay();
        if (chosen) { header_menu_ = 0; chosen(); }
        else if (dismiss) header_menu_ = 0;
    }
    ui_ctx_.pop_id();
}
}
