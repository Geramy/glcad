#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/shell.hpp"

#include <string>
#include <vector>

namespace gcapp {

namespace T = gtheme::token;

void Shell::note_edits() {
    publish_sysvars();
    if(viewport_drag_index_>=0 || canvas_.selection_dragging() || canvas_.grip_dragging()) return;
    if (layout_drag_ || restoring_history_) return;
    if(canvas_.edit_serial()!=seen_canvas_)
        canvas_.detect_wall_regions(drawing_discipline_ != DrawingDiscipline::Mechanical);
    refresh_symbol_legends();
    ensure_space_ids();
    const gcm::u64 c = canvas_.edit_serial(), m = modeller_.edit_serial();

    static const bool verify = std::getenv("GLCAD_VERIFY_METADATA") != nullptr;
    const gcm::u64 gate = metadata_gate_;
    bool could_have_moved = seen_metadata_.empty() || gate != seen_metadata_gate_;
    std::vector<gcm::u8> lightweight;
    if (could_have_moved || verify) lightweight = metadata_bytes(true);
    if (could_have_moved && std::getenv("GLCAD_TRACE_GATE"))
        std::fprintf(stderr, "gate opened by %s\n", metadata_gate_reason_);
    if (verify && !could_have_moved && !seen_lightweight_metadata_.empty() &&
        lightweight != seen_lightweight_metadata_) {
        log_.push_back("METADATA CHANGED WITHOUT TICKING THE GATE -- "
                       "an undo step would have been lost");
        std::fputs("GLCAD_VERIFY_METADATA: metadata changed without ticking "
                   "the gate -- an undo step would have been lost\n", stderr);
        could_have_moved = true;
    }
    if (!could_have_moved && c == seen_canvas_ && m == seen_solid_) {
        if (verify) seen_lightweight_metadata_ = std::move(lightweight);
        sketch_transaction_ = false;
        return;
    }
    seen_metadata_gate_ = gate;
    const bool metadata_moved = could_have_moved &&
                                (seen_metadata_.empty() ||
                                 lightweight != seen_lightweight_metadata_ ||
                                 cached_block_generation_ != seen_block_generation_);
    auto state = metadata_moved ? metadata_bytes() : std::vector<gcm::u8>{};
    const auto first = undo_journal_.size();
    if (m != seen_solid_)
        undo_journal_.push_back({Edit::Kind::Solid, static_cast<int>(m - seen_solid_), {}, 0});
    const auto marks=canvas_.undo_mark_serial();
    if(c!=seen_canvas_ && marks==seen_canvas_marks_ && doc_.in_transaction()) {
        doc_.abort();mirrored_=~gcm::u64{0};
    }
    if (marks > seen_canvas_marks_)
        undo_journal_.push_back({Edit::Kind::Canvas, static_cast<int>(marks - seen_canvas_marks_), {}, 0});
    if (metadata_moved && !seen_metadata_.empty() && state != seen_metadata_) {
        if (sketch_transaction_ && first > 0 && undo_journal_[first - 1].kind == Edit::Kind::Sketches) {
            undo_journal_[first - 1].metadata = seen_metadata_;
        } else {
            if (undo_journal_.size() == first)
                undo_journal_.push_back({Edit::Kind::Metadata, 1, {}, 0});
            undo_journal_[first].metadata = seen_metadata_;
        }
        ++metadata_serial_;
    }
    for (std::size_t i = first; i < undo_journal_.size(); ++i)
        undo_journal_[i].space_id = undo_journal_[i].kind == Edit::Kind::Metadata && command_origin_space_
                                       ? command_origin_space_ : active_space_id();
    gcm::u32 gesture_space = 0;
    for (std::size_t i = first; i < undo_journal_.size(); ++i)
        if (undo_journal_[i].kind == Edit::Kind::Metadata) {
            gesture_space = command_origin_space_ ? command_origin_space_ : active_space_id();
            break;
        }
    if (undo_journal_.size() != first) redo_journal_.clear();
    if (metadata_moved) seen_metadata_ = std::move(state);
    if (could_have_moved || verify) seen_lightweight_metadata_ = std::move(lightweight);
    seen_block_generation_ = cached_block_generation_;
    seen_solid_ = m;
    seen_canvas_ = c;
    seen_canvas_marks_ = marks;
    sketch_transaction_ = false;
    while (undo_journal_.size() > 512) undo_journal_.erase(undo_journal_.begin());
    // Close the document transaction at the journal's Edit boundary, always.
    sync_document(true, gesture_space);
}

void Shell::journal_sketches() {
    flush_edits();
    store_active_sketch();
    undo_journal_.push_back({Edit::Kind::Sketches, 1, sketches_, active_sketch_});
    undo_journal_.back().spaces = space_states_;
    sketch_transaction_ = true;
    redo_journal_.clear();
}

void Shell::clear_journal() {
    undo_journal_.clear();
    redo_journal_.clear();
    seen_canvas_ = canvas_.edit_serial();
    seen_canvas_marks_ = canvas_.undo_mark_serial();
    seen_solid_ = modeller_.edit_serial();
    seen_metadata_ = metadata_bytes();
    seen_lightweight_metadata_ = metadata_bytes(true);
    seen_block_generation_ = cached_block_generation_;
    seen_metadata_gate_ = metadata_gate_;
    settings_image_.clear();
    force_document_resync();
    sync_document();
    doc_.forget_history();
}

void Shell::delete_sketch(int index) {
    if (index < 0 || index >= static_cast<int>(sketches_.size())) return;
    journal_sketches();
    if (sketches_.size() == 1 || index == 0) {
        sketches_[0].shapes.clear();
        sketches_[0].segments.clear();
        sketches_[0].texts.clear();
        space_states_[sketches_[0].space_id] = Canvas::SpaceState{};
        if (active_sketch_ == 0 && !in_paper_space_) load_active_space();
        log_.push_back("cleared " + sketches_[0].name);
        mesh_dirty_ = true;
        return;
    }
    const std::string name = sketches_[static_cast<std::size_t>(index)].name;
    const bool was_active = index == active_sketch_;
    for (auto& layout:layouts_) for (auto& vp:layout.viewports) {
        if (vp.source_space==static_cast<gcm::u32>(index)) vp.source_space=0;
        else if (vp.source_space>static_cast<gcm::u32>(index)) --vp.source_space;
    }
    sketches_.erase(sketches_.begin() + index);
    if (active_sketch_ > index) --active_sketch_;
    if (was_active) {
        active_sketch_ = 0;
        if (!in_paper_space_) load_active_space();
        if (mode_ == Mode::Sketch) edit_sketch(0);
    }
    clear_solid_selection();
    hover_target_ = Modeller::Target{};
    rebuild_sketch_segments();
    mesh_dirty_ = true;
    log_.push_back("deleted " + name);
}

void Shell::delete_selected() {
    if(text_entry_) return;
    if(canvas_.selection_count()>0 || has_solid_selection()) {run_command("ERASE");return;}
    log_.push_back("nothing selected to delete");
}

bool Shell::selection_anchor(gview::Point3& out) const {
    if(modeller_.edge_kept() && modeller_.edge_pick_point(out)) return true;
    if(sel_target_.valid) {out=sel_target_.has_pick_point?sel_target_.pick_point:sel_target_.centroid;return true;}
    const auto& target=modeller_.target();
    if(target.valid) {out=target.has_pick_point?target.pick_point:target.centroid;return true;}
    return false;
}

void Shell::pan_selection(double dx,double dy) {
    gview::Point3 anchor;
    if(selection_anchor(anchor)) camera_.pan(dx,dy,anchor);
    else camera_.pan(dx,dy);
}

void Shell::zoom_selection(double x,double y,double factor) {
    gview::Point3 anchor;
    if(selection_anchor(anchor)) camera_.zoom_at(x,y,factor,anchor);
    else camera_.zoom_at(x,y,factor);
}

gview::Point3 Shell::focus_pivot() const {
    gview::Point3 selected;
    if(selection_anchor(selected)) return selected;
    const auto centre_of = [&](const gtopo::Body& b) {
        const auto bb = b.bounds(vol_);
        return gview::Point3::from((bb.lo.x.to_f64() + bb.hi.x.to_f64()) * 0.5,
                                   (bb.lo.y.to_f64() + bb.hi.y.to_f64()) * 0.5,
                                   (bb.lo.z.to_f64() + bb.hi.z.to_f64()) * 0.5);
    };
    const auto& bodies = modeller_.bodies();
    if (sel_body_ >= 0 && sel_body_ < static_cast<int>(bodies.size())) return centre_of(bodies[std::size_t(sel_body_)]);
    if (sel_target_.valid && sel_target_.on_body && sel_target_.body < bodies.size()) return centre_of(bodies[sel_target_.body]);
    if (sel_target_.valid) return sel_target_.centroid;
    const auto extent = [&](bool layer_only, gview::Point3& out) {
        double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
        bool any = false;
        for (const auto& sh : canvas_.shapes()) {
            if (layer_only && sh.layer != layers_.current()) continue;
            for (const auto& l : {sh.a, sh.b}) {
                double u, v;
                gcm::to_world(vol_, l, u, v);
                lo_x = std::min(lo_x, u); hi_x = std::max(hi_x, u);
                lo_y = std::min(lo_y, v); hi_y = std::max(hi_y, v);
                any = true;
            }
        }
        if (!any) return false;
        double wx, wy, wz;
        sketch_plane().to_world((lo_x + hi_x) * 0.5, (lo_y + hi_y) * 0.5, wx, wy, wz);
        out = gview::Point3::from(wx, wy, wz);
        return true;
    };
    gview::Point3 p;
    if (extent(true, p)) return p;
    if (extent(false, p)) return p;
    return orbit_pivot();
}

gcm::u64 Shell::document_serial() const noexcept {
    return canvas_.edit_serial() + modeller_.edit_serial() + sketch_edits_ + metadata_serial_ +
           static_cast<gcm::u64>(sketches_.size()) * 1000003u;
}
}
