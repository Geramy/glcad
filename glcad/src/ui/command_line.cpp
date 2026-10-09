#include "ui/shell.hpp"
#include "ui/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "geometry/mate.hpp"
#include "geometry/measure.hpp"
#include "geometry/recognise.hpp"
#include "ui/build_stamp.hpp"

namespace gcapp {

namespace T = gtheme::token;

void Shell::run_command(const std::string& text_in) {
    if (!product_edit_allowed_) {
        const auto token=gcmd::to_upper(text_in.substr(0,text_in.find(' ')));
        static const char* allowed[]={"OPEN","SAVE","SAVEAS","QSAVE","EXPORT","PRINT","PLOT",
            "PREVIEW","ZOOM","ZOOMEXTENTS","ZOOMALL","PAN","ABOUT","HELP",
            "NEWTAB","NEXTTAB","PREVTAB","CLOSETAB","CLOSE","RECOVER"};
        if (std::none_of(std::begin(allowed),std::end(allowed),[&](const char* n){return token==n;})) {
            require_edit_access(); return;
        }
    }
    touch_metadata("command");
    cancel_viewport_drag();
    const bool outer = command_depth_++ == 0;
    if (outer) { flush_edits(); command_origin_space_ = active_space_id(); }
    struct Finish {
        Shell& shell;
        ~Finish() {
            if (--shell.command_depth_ == 0) {
                shell.apply_sysvars();
                shell.publish_sysvars();
                shell.flush_edits();
                if (!shell.restoring_history_) shell.sync_document();
                shell.command_origin_space_ = 0;
            }
        }
    } finish{*this};
    std::string text = text_in;
    command_arg_.clear();
    if (const std::size_t sp = text.find(' '); sp != std::string::npos) {
        command_arg_ = text.substr(sp + 1);
        while (!command_arg_.empty() && command_arg_.front() == ' ') command_arg_.erase(0, 1);
        text = text.substr(0, sp);
    }
    if (plot_preview_ && gcmd::to_upper(text) != "PREVIEW" && gcmd::to_upper(text) != "PRINT" &&
        gcmd::to_upper(text) != "ABOUT" && gcmd::to_upper(text) != "HELP") {
        log_.push_back("Close plot preview to edit the drawing");
        return;
    }
    if(gcmd::to_upper(text)=="SELECT" && !command_arg_.empty()) {
        auto keyword=gcmd::to_upper(command_arg_);
        const auto end=keyword.find(' ');
        const auto mode=keyword.substr(0,end);
        const std::string rest=end==std::string::npos?"":command_arg_.substr(end+1);
        if(mode=="P"||mode=="PREVIOUS") text="SELECTPREVIOUS";
        else if(mode=="L"||mode=="LAST") text="SELECTLAST";
        else if(mode=="ALL") text="SELECTALL";
        else if(mode=="WP"||mode=="WPOLYGON") text="WPOLYGON";
        else if(mode=="CP"||mode=="CPOLYGON") text="CPOLYGON";
        else if(mode=="F"||mode=="FENCE") text="FENCE";
        command_arg_=rest;
    }
    if(vars_.find(text) && !commands_.find(text)) {
        command_arg_=gcmd::to_upper(text)+(command_arg_.empty()?"":" "+command_arg_);
        run_extra_command("SETVAR");return;
    }
    if(vars_.get_int("PICKFIRST",1)==0 && text!="SELECT" && text!="SETVAR") canvas_.clear_selection();
    for (char c : text) cli_->type(c);
    const auto res = cli_->submit();
    if (!res.message.empty()) log_.push_back(res.message);
    if (log_.size() > 6) log_.erase(log_.begin());

    if (!res.command) return;
    const std::string& n = res.command->name;
    if (run_drafting_feature_command(n, command_arg_)) return;

    if (const CommandHandler handler = command_table_.find(n)) {
        CommandContext ctx(*this, command_arg_);
        handler(ctx);
        mesh_dirty_ = true;
        return;
    }

    struct ToolBinding { const char* name; Tool tool; int rail; };
    static const ToolBinding kTools[] = {
        {"LINE", Tool::Line, 1},        {"RECTANG", Tool::Rect, 2},
        {"CIRCLE", Tool::Circle, 3},    {"ARC", Tool::Arc, 4},
        {"PLINE", Tool::Polyline, 5},   {"SELECT", Tool::Select, 0},
        {"ELLIPSE", Tool::Ellipse, 0},  {"POINT", Tool::Point, 0},
        {"RAY", Tool::Ray, 0},          {"XLINE", Tool::Xline, 0},
        {"SPLINE", Tool::Spline, 0},   {"MLINE", Tool::Multiline, 0},
        {"MOVE", Tool::Move, 0},        {"COPY", Tool::Copy, 0},
        {"ROTATE", Tool::Rotate, 0},    {"MIRROR", Tool::Mirror, 0},
        {"OFFSET", Tool::Offset, 0},    {"TRIM", Tool::Trim, 0},
        {"EXTEND", Tool::Extend, 0},    {"FILLET", Tool::Fillet, 0},
        {"CHAMFER", Tool::Chamfer, 0},
        {"DIMLINEAR", Tool::DimLinear, 0},
        {"DIMALIGNED", Tool::DimAligned, 0},
        {"DIMANGULAR", Tool::DimAngular, 0},
        {"DIMRADIUS", Tool::DimRadial, 0},
        {"DIMDIAMETER", Tool::DimDiameter, 0},
    };
    for (const auto& b : kTools)
        if (n == b.name) {
            if(!command_arg_.empty() && (b.tool==Tool::Offset || b.tool==Tool::Fillet || b.tool==Tool::Chamfer)) {
                const char* setting=b.tool==Tool::Offset?"OFFSETDIST":b.tool==Tool::Fillet?"FILLETRAD":"CHAMFERA";
                const auto distance=gunit::parse_length(command_arg_,canvas_.display_unit());
                if(!distance || !set_drafting_var(setting,std::to_string(*distance))) return;
            }
            if(canvas_.corner_selection(b.tool)) {
                log_.push_back(canvas_.message());mesh_dirty_=true;return;
            }
            canvas_.set_tool(b.tool);
            if (b.tool == Tool::Select) modeller_.arm(Modeller::Op::None);
            for (const auto& d : tool_defs())
                if (n == d.command) { active_tool_id_ = d.id; break; }
            log_.push_back(std::string(b.name) + ": pick points, Esc to cancel");
            return;
        }

    struct ConBinding { const char* name; gsolve::Kind kind; };
    static const ConBinding kCons[] = {
        {"GCHORIZONTAL", gsolve::Kind::Horizontal},
        {"GCVERTICAL", gsolve::Kind::Vertical},
        {"GCPARALLEL", gsolve::Kind::Parallel},
        {"GCPERPENDICULAR", gsolve::Kind::Perpendicular},
        {"GCCOLLINEAR", gsolve::Kind::Collinear},
        {"GCTANGENT", gsolve::Kind::Tangent},
        {"GCCONCENTRIC", gsolve::Kind::Concentric},
        {"GCEQUAL", gsolve::Kind::Equal},
        {"GCCOINCIDENT", gsolve::Kind::Coincident},
        {"GCFIX", gsolve::Kind::FixPoint},
    };
    for (const auto& c : kCons)
        if (n == c.name) {
            canvas_.apply_constraint(c.kind);
            log_.push_back(canvas_.message());
            mesh_dirty_ = true;
            return;
        }

    bool handled_by_table = false;
    for (const char* known : {"QSELECT", "BHATCH", "HATCH", "GRADIENT", "AUTOCONSTRAIN", "ARRAY",
                              "ERASE", "UNDO", "REDO", "ORTHO", "POLAR", "EXTRUDE", "OFFSETFACE", "REVOLVE",
                              "UNION", "SUBTRACT", "INTERSECT", "DELETEBODY", "MASSPROP", "SKETCH", "FINISH",
                              "SWEEP", "SHELL", "SPLIT", "SECTION", "DIST", "PATTERN3D", "MIRROR3D", "MATE",
                              "FILLETEDGE", "CHAMFEREDGE", "LOFT", "ZOOM", "VIEW", "ZOOMPREVIOUS", "ZOOMNEXT",
                              "3DWALK", "3DFLY", "PARAMETERS", "OSNAP", "SNAP", "GRID", "PLAN", "ORBIT",
                              "NAVSWHEEL", "PAN", "REGEN", "REDRAW", "DSETTINGS", "OPTIONS", "UNITS",
                              "CONSTRAINTSETTINGS", "THEME", "LAYON", "LAYFRZ", "LAYISO", "LAYER", "PROPERTIES",
                              "PRESSPULL", "SLICE", "INTERFERE", "BOX", "CYLINDER", "AREA", "ID", "LIST",
                              "MEASUREGEOM", "NEW", "FILTER", "SELECTALL", "ISO", "ZOOMEXTENTS", "ABOUT",
                              "OPEN", "SAVE", "SAVEAS", "IMPORT", "EXPORT", "DWGUNITS", "GROUPSEL", "UNGROUP",
                              "DIMARC", "DIMCENTER", "DIMJOGGED", "LIFT", "WALLS", "TEXT", "MTEXT", "PLOT",
                              "NEWTAB", "CLOSETAB", "NEXTTAB", "PREVTAB"})
        if (n == known) handled_by_table = true;

    if (n == "QSELECT" || n == "FILTER") {
        Canvas::Filter f;
        std::string a = command_arg_;
        std::vector<std::string> words;
        {
            std::size_t at = 0;
            while (at < a.size()) {
                std::size_t sp = a.find(' ', at);
                if (sp == std::string::npos) sp = a.size();
                if (sp > at) words.push_back(a.substr(at, sp - at));
                at = sp + 1;
            }
        }
        const auto lower = [](std::string t) {
            for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        std::string unknown;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::string key = lower(words[i]);
            const auto next = [&](std::size_t k) {
                return i + k < words.size() ? words[i + k] : std::string{};
            };
            if (key == "layer" && !next(1).empty()) {
                f.by_layer = true;
                f.layer = next(1);
                i += 1;
            } else if (key == "kind" && !next(1).empty()) {
                const std::string k = lower(next(1));
                f.by_kind = true;
                if (k == "line" || k == "segment") f.kind = gsnap::ShapeKind::Segment;
                else if (k == "circle") f.kind = gsnap::ShapeKind::Circle;
                else if (k == "arc") f.kind = gsnap::ShapeKind::Arc;
                else if (k == "ellipse") f.kind = gsnap::ShapeKind::Ellipse;
                else if (k == "point") f.kind = gsnap::ShapeKind::Point;
                else if (k == "block") f.kind = gsnap::ShapeKind::Block;
                else if (k == "ray") f.kind = gsnap::ShapeKind::Ray;
                else if (k == "xline") f.kind = gsnap::ShapeKind::Xline;
                else { f.by_kind = false; unknown += " " + next(1); }
                i += 1;
            } else if ((key == "length" || key == "radius") && !next(1).empty()) {
                double lo = 0.0, hi = 1e300;
                try { lo = std::stod(next(1)); } catch (...) {}
                std::size_t used = 1;
                if (!next(2).empty()) {
                    try { hi = std::stod(next(2)); used = 2; } catch (...) { hi = 1e300; }
                }
                if (key == "length") { f.by_length = true; f.min_length = lo; f.max_length = hi; }
                else { f.by_radius = true; f.min_radius = lo; f.max_radius = hi; }
                i += used;
            } else {
                unknown += " " + words[i];
            }
        }
        if (!f.any()) {
            f.by_kind = true;
            f.kind = gsnap::ShapeKind::Segment;
        }
        const std::size_t n_sel = canvas_.select_filtered(f, false);
        std::string msg = "selected " + std::to_string(n_sel) + " object(s)";
        if (f.by_layer) msg += " on layer " + f.layer;
        if (f.by_kind) msg += " of one kind";
        if (f.by_length) msg += " by length";
        if (f.by_radius) msg += " by radius";
        if (!unknown.empty()) msg += "  (ignored:" + unknown + ")";
        log_.push_back(msg);
        return;
    }

    if (n == "HATCH" || n == "BHATCH") {
        if (sel_target_.valid && sel_target_.source == active_sketch_ && mode_ == Mode::Sketch) {
            double sx, sy;
            canvas_.view().screen_of(sel_target_.centroid.x.to_f64(), sel_target_.centroid.y.to_f64(), sx, sy);
            canvas_.hatch_at(sx, sy,canvas_.hatch_style().name);
            log_.push_back(canvas_.message());
            clear_solid_selection();
            mesh_dirty_ = true;
            return;
        }
        if (mode_ != Mode::Sketch) { log_.push_back("HATCH: open a sketch first"); return; }
        hatch_pending_ = true;
        canvas_.set_tool(Tool::Select);
        log_.push_back("HATCH: click inside the area to hatch");
        return;
    }
    if (n == "GRADIENT") {
        canvas_.gradient_at_point(input_.mouse.x, input_.mouse.y);
        log_.push_back(canvas_.message());
        return;
    }
    if (n == "ARRAY") { canvas_.set_tool(Tool::ArrayRect); return; }
    if (n == "ERASE") {
        const bool solids=has_solid_selection();
        if(solids) erase_selected_solids();
        if(canvas_.selection_count()>0 || !solids) {canvas_.erase_selected();log_.push_back(canvas_.message());}
    }
    else if (n == "PLANCHECK") {
        drafting_feature_panel_=4;plan_panel_serial_=~gcm::u64{0};
        const auto findings = plan_check();
        std::size_t errors = 0, warnings = 0;
        for (const auto& f : findings) {
            const char* tag = f.level == PlanFinding::Level::Error ? "ERROR"
                            : f.level == PlanFinding::Level::Warning ? "warn" : "note";
            errors += f.level == PlanFinding::Level::Error;
            warnings += f.level == PlanFinding::Level::Warning;
            log_.push_back(std::string("plan check [") + tag + "] " + f.text);
        }
        log_.push_back("plan check: " + std::to_string(errors) + " error(s), " +
                       std::to_string(warnings) + " warning(s)");
    }
    else if (n == "UNDO" || n == "REDO") {
        clear_solid_selection();
        modeller_.finish_edge();
        if (document_undo_) {
            flush_edits();
            const bool redo = n == "REDO";
            if (!undo_through_document(redo))
                log_.push_back(redo ? "nothing to redo" : "nothing to undo");
            else
                log_.push_back(redo ? "redo: " + doc_.undo_label()
                                    : "undo: " + last_change_label());
            return;
        }
        flush_edits();
        std::vector<Edit>& from = n == "UNDO" ? undo_journal_ : redo_journal_;
        std::vector<Edit>& to = n == "UNDO" ? redo_journal_ : undo_journal_;
        if (from.empty()) {
            log_.push_back(n == "UNDO" ? "nothing to undo" : "nothing to redo");
        } else {
            restoring_history_ = true;
            Edit e = from.back();
            from.pop_back();
            const auto current_metadata = e.metadata.empty() ? std::vector<gcm::u8>{} : metadata_bytes();
            switch (e.kind) {
                case Edit::Kind::Metadata:
                    activate_space(e.space_id);
                    to.push_back(e);
                    log_.push_back(n == "UNDO" ? "undo: document settings" : "redo: document settings");
                    break;
                case Edit::Kind::Canvas:
                    activate_space(e.space_id);
                    for (int i = 0; i < e.steps; ++i) { if (n == "UNDO") canvas_.undo(); else canvas_.redo(); }
                    to.push_back(e);
                    log_.push_back(n == "UNDO" ? "undo: drawing" : "redo: drawing");
                    break;
                case Edit::Kind::Solid:
                    for (int i = 0; i < e.steps; ++i) { if (n == "UNDO") modeller_.undo(); else modeller_.redo(); }
                    to.push_back(e);
                    mesh_dirty_ = true;
                    clear_solid_selection();
                    sel_body_ = -1;
                    log_.push_back(n == "UNDO" ? "undo: solid" : "redo: solid");
                    break;
                case Edit::Kind::Sketches: {
                    store_active_sketch();
                    Edit back{Edit::Kind::Sketches, 1, sketches_, active_sketch_};
                    back.spaces = space_states_;
                    space_states_ = e.spaces;
                    sketches_ = e.sketches;
                    active_sketch_ = std::min(e.active_sketch, static_cast<int>(sketches_.size()) - 1);
                    if (active_sketch_ < 0) active_sketch_ = 0;
                    in_paper_space_ = false;
                    load_active_space();
                    rebuild_sketch_segments();
                    to.push_back(std::move(back));
                    mesh_dirty_ = true;
                    log_.push_back(n == "UNDO" ? "undo: sketches" : "redo: sketches");
                    break;
                }
            }
            store_active_sketch();
            if (!e.metadata.empty()) {
                if (!restore_metadata(e.metadata)) log_.push_back("could not restore document settings");
                to.back().metadata = current_metadata;
                if (e.kind == Edit::Kind::Metadata) activate_space(e.space_id);
                ++metadata_serial_;
            }
            restoring_history_ = false;
            seen_metadata_ = metadata_bytes();
            seen_lightweight_metadata_ = metadata_bytes(true);
            seen_block_generation_ = cached_block_generation_;
            seen_canvas_ = canvas_.edit_serial();
            seen_canvas_marks_ = canvas_.undo_mark_serial();
            seen_solid_ = modeller_.edit_serial();
        }
    }
    else if (n == "ORTHO") canvas_.toggle_ortho();
    else if (n == "POLAR") canvas_.toggle_polar();
    else if (n == "OFFSETFACE" && !command_arg_.empty() && sel_target_.valid &&
             sel_target_.on_body) {
        run_command("FACEOFFSET " + command_arg_);
    }
    else if (n == "EXTRUDE" && !command_arg_.empty() &&
             (uppercase(command_arg_).rfind("THROUGH", 0) == 0 ||
              uppercase(command_arg_).rfind("SYM", 0) == 0)) {
        if (!modeller_.hovered().valid && !sel_target_.valid) {
            log_.push_back("EXTRUDE: point at a face or a closed region first");
        } else {
            const bool through = uppercase(command_arg_).rfind("THROUGH", 0) == 0;
            double d = 0.0;
            if (through) {
                d = -modeller_.through_distance();
            } else {
                std::istringstream in(command_arg_);
                std::string skip;
                in >> skip;
                if (!(in >> d)) d = 0.0;
            }
            if (std::fabs(d) < 1e-12) {
                log_.push_back("EXTRUDE SYMMETRIC <distance>");
            } else if (through) {
                log_.push_back(modeller_.commit_distance(d)
                                   ? "cut through" : "EXTRUDE: nothing to cut");
                after_commit();
            } else {
                const bool a = modeller_.commit_distance(d * 0.5);
                const bool b = modeller_.commit_distance(-d * 0.5);
                log_.push_back(a || b ? "extruded symmetrically"
                                      : "EXTRUDE: nothing to extrude");
                after_commit();
            }
        }
    }
    else if (n == "EXTRUDE" || n == "OFFSETFACE") {
        modeller_.arm(Modeller::Op::Extrude);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = n == "EXTRUDE" ? ToolId::Extrude : ToolId::OffsetFace;
        log_.push_back(modeller_.prompt());
    }
    else if (n == "REVOLVE") {
        modeller_.arm(Modeller::Op::Revolve);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = ToolId::Revolve;
        log_.push_back(modeller_.prompt());
    }
    else if (n == "UNION" || n == "SUBTRACT" || n == "INTERSECT") {
        if(std::any_of(canvas_.selection().begin(),canvas_.selection().end(),[&](auto i){return canvas_.is_region(i);})) {
            canvas_.combine_regions(n=="UNION"?gregion::Operation::Union:n=="SUBTRACT"?gregion::Operation::Subtract:gregion::Operation::Intersect);
            log_.push_back(canvas_.message());return;
        }
        modeller_.arm(n == "UNION" ? Modeller::Op::Union
                    : n == "SUBTRACT" ? Modeller::Op::Subtract : Modeller::Op::Intersect);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = n == "UNION" ? ToolId::Union
                        : n == "SUBTRACT" ? ToolId::Subtract : ToolId::Intersect;
        log_.push_back(modeller_.prompt());
    }
    else if (n == "DIST" && mode_ != Mode::Solid) {
        modeller_.arm(Modeller::Op::None);
        if (canvas_.dimension_selection()) {
            active_tool_id_ = ToolId::Select;
            log_.push_back(canvas_.message());
            return;
        }
        canvas_.set_tool(Tool::DimAligned);
        active_tool_id_ = ToolId::MeasureDistance;
        log_.push_back("measure: click two lines for their direct gap, or pick points");
    }
    else if (n == "SWEEP" || n == "SHELL" || n == "SPLIT" || n == "SECTION" || n == "DIST" ||
             n == "PATTERN3D" || n == "MIRROR3D") {
        static const struct { const char* name; Modeller::Op op; ToolId id; } ops[] = {
            {"SWEEP", Modeller::Op::Sweep, ToolId::Sweep},
            {"SHELL", Modeller::Op::ShellBody, ToolId::ShellBody},
            {"SPLIT", Modeller::Op::Split, ToolId::Split},
            {"SECTION", Modeller::Op::Section, ToolId::Section},
            {"DIST", Modeller::Op::MeasureDistance, ToolId::MeasureDistance},
            {"PATTERN3D", Modeller::Op::PatternBody, ToolId::PatternBody},
            {"MIRROR3D", Modeller::Op::MirrorBody, ToolId::MirrorBody},
        };
        for (const auto& o : ops)
            if (n == o.name) {
                modeller_.arm(o.op);
                canvas_.set_tool(Tool::Select);
                active_tool_id_ = o.id;
                log_.push_back(modeller_.prompt());
            }
    }
    else if (n == "MATE") {
        std::string a = command_arg_;
        for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
        const std::size_t sp = a.find(' ');
        const std::string word = sp == std::string::npos ? a : a.substr(0, sp);
        double value = 0.0;
        if (sp != std::string::npos) { try { value = std::stod(a.substr(sp + 1)); } catch (...) {} }
        gtopo::MateKind k = gtopo::MateKind::Coincident;
        if (word == "flush") k = gtopo::MateKind::Flush;
        else if (word == "parallel") k = gtopo::MateKind::Parallel;
        else if (word == "perpendicular") k = gtopo::MateKind::Perpendicular;
        else if (word == "concentric") k = gtopo::MateKind::Concentric;
        else if (word == "tangent") k = gtopo::MateKind::Tangent;
        else if (word == "collinear") k = gtopo::MateKind::Collinear;
        else if (word == "distance") k = gtopo::MateKind::Distance;
        else if (word == "angle") { k = gtopo::MateKind::Angle;
                                    value *= 3.14159265358979323846 / 180.0; }
        modeller_.set_mate_kind(k, value);
        modeller_.arm(Modeller::Op::Mate);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = ToolId::Mate;
        log_.push_back(std::string("mate (") + gtopo::mate_kind_name(k) + "): " +
                       modeller_.prompt());
    }
    else if (n == "FILLETEDGE" || n == "CHAMFEREDGE" || n == "LOFT") {
        const auto op=n=="FILLETEDGE" ? Modeller::Op::FilletEdge : n=="CHAMFEREDGE" ? Modeller::Op::ChamferEdge : Modeller::Op::Loft;
        if(n!="LOFT" && modeller_.edge_kept()) modeller_.set_edge_operation(op);
        else modeller_.arm(op);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = n == "FILLETEDGE" ? ToolId::FilletEdge
                        : n == "CHAMFEREDGE" ? ToolId::ChamferEdge : ToolId::Loft;
        log_.push_back(modeller_.prompt());
    }
    else if (n == "DELETEBODY") {
        if(!erase_selected_solids()) log_.push_back("select a solid first");
    }
    else if (n == "MASSPROP") {
        if (!sel_target_.valid || !sel_target_.on_body) {
            log_.push_back("select a solid first");
        } else {
            const auto& b = modeller_.bodies()[std::size_t(sel_target_.body)];
            double density = 1000.0;
            {
                const auto& mats = modeller_.materials();
                const gcm::u32 mi = modeller_.material_of(std::size_t(sel_target_.body));
                if (mi < mats.size() && mats[mi].density > 0.0) density = mats[mi].density;
            }
            if (!command_arg_.empty()) {
                try { density = std::stod(command_arg_); } catch (...) {}
            }
            const gtopo::MassProperties m = gtopo::mass_properties(b, vol_, density);
            if (!m.ok) { log_.push_back(std::string("MASSPROP: ") + m.reason); }
            else {
                char buf[512];
                const auto u = canvas_.display_unit();
                const double k = gunit::from_metres(1.0, u);
                std::snprintf(buf, sizeof(buf),
                    "volume %.6g %s^3, surface %.6g %s^2, mass %.6g kg at density %.6g kg/m^3",
                    m.volume * k * k * k, gunit::suffix(u),
                    m.area * k * k, gunit::suffix(u), m.mass, m.density);
                log_.push_back(buf);
                std::snprintf(buf, sizeof(buf),
                    "centre of mass %.6g, %.6g, %.6g %s",
                    m.centroid[0] * k, m.centroid[1] * k, m.centroid[2] * k,
                    gunit::suffix(u));
                log_.push_back(buf);
                std::snprintf(buf, sizeof(buf),
                    "principal moments %.6g, %.6g, %.6g kg m^2 -- about the centre of mass",
                    m.principal[0], m.principal[1], m.principal[2]);
                log_.push_back(buf);
                std::snprintf(buf, sizeof(buf),
                    "bounding box %.6g x %.6g x %.6g %s",
                    (m.hi[0] - m.lo[0]) * k, (m.hi[1] - m.lo[1]) * k,
                    (m.hi[2] - m.lo[2]) * k, gunit::suffix(u));
                log_.push_back(buf);
            }
        }
    }
    else if (n == "SKETCH") {
        picking_sketch_plane_ = true;
        modeller_.arm(Modeller::Op::None);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = ToolId::NewSketch;
        log_.push_back("pick a face to sketch on, or the ground");
    }
    else if (n == "FINISH") { finish_sketch(); }
    else if (n == "ZOOM") canvas_.view().zoom_at(vw_ * 0.5, vh_ * 0.5, 1.25);
    else if (n == "VIEW") {
        std::string first = command_arg_, rest;
        if (const std::size_t sp = command_arg_.find(' '); sp != std::string::npos) {
            first = command_arg_.substr(0, sp);
            rest = command_arg_.substr(sp + 1);
        }
        std::string key = first;
        for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));

        if (command_arg_.empty()) {
            std::string names;
            for (const auto& v : views_.all()) names += (names.empty() ? "" : ", ") + v.name;
            log_.push_back("views: " + names + " -- VIEW <name> restores one");
        } else if (key == "SAVE" && !rest.empty()) {
            views_.capture(camera_, rest, "captured from the viewport");
            log_.push_back("view saved: " + rest);
        } else if (key == "DELETE" && !rest.empty()) {
            log_.push_back(views_.remove(rest) ? "view deleted: " + rest
                                               : "VIEW: cannot delete " + rest);
        } else if (views_.find(first)) {
            history_.record(camera_);
            if (views_.restore(camera_, first)) {
                sync_view_from_camera();
                log_.push_back("view " + first);
            } else {
                log_.push_back("VIEW: could not restore " + first);
            }
        } else {
            views_.capture(camera_, first, "captured from the viewport");
            log_.push_back("view saved: " + first);
        }
    }
    else if (n == "ZOOMPREVIOUS") {
        if (history_.back(camera_)) log_.push_back("previous view");
        else log_.push_back("no previous view");
    }
    else if (n == "ZOOMNEXT") {
        if (history_.forward(camera_)) log_.push_back("next view");
        else log_.push_back("no next view");
    }
    else if (n == "3DWALK" || n == "3DFLY") {
        walker_.settings.mode =
            n == "3DFLY" ? gview::WalkMode::Fly : gview::WalkMode::Walk;
        walker_.begin(camera_);
        set_lens(Lens::ThreeD);
        log_.push_back(n == "3DFLY" ? "fly mode" : "walk mode");
    }
    else if (n == "PARAMETERS") {
        std::string first = command_arg_, rest;
        if (const std::size_t sp = command_arg_.find(' '); sp != std::string::npos) {
            first = command_arg_.substr(0, sp);
            rest = command_arg_.substr(sp + 1);
            while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
        }
        std::string key = first;
        for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));

        if (first.empty()) {
            const gunit::EvalReport r = params_.evaluate(canvas_.display_unit());
            if (params_.size() == 0) {
                log_.push_back("no parameters -- PARAMETERS <name> <value> defines one");
            } else {
                for (const auto& p : params_.all()) {
                    char buf[192];
                    std::snprintf(buf, sizeof(buf), "%s = %s -> %.6g%s%s",
                                  p.name.c_str(), p.expression.c_str(), p.value,
                                  p.ok ? "" : "  ERROR: ", p.ok ? "" : p.error.c_str());
                    log_.push_back(buf);
                }
                if (!r.ok())
                    log_.push_back(std::to_string(r.failed) + " parameter(s) could not "
                                   "be evaluated" +
                                   (r.cycles.empty() ? "" : "; a cycle: " + r.cycles.front()));
            }
            log_.push_back(canvas_.dof().describe());
        } else if (key == "DELETE" && !rest.empty()) {
            const auto users = params_.dependents(rest);
            if (!users.empty()) {
                std::string said;
                for (const auto& u : users) said += (said.empty() ? "" : ", ") + u;
                log_.push_back("PARAMETERS: " + rest + " is used by " + said);
            } else {
                log_.push_back(params_.remove(rest) ? "deleted " + rest
                                                    : "no parameter called " + rest);
            }
        } else if (rest.empty()) {
            if (const gunit::Parameter* p = params_.find(first)) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "%s = %s -> %.6g", p->name.c_str(),
                              p->expression.c_str(), p->value);
                log_.push_back(buf);
            } else {
                log_.push_back("PARAMETERS " + first + " <value>: defines it");
            }
        } else {
            gunit::Parameter p;
            if (const gunit::Parameter* existing = params_.find(first)) p = *existing;
            p.name = first;
            p.expression = rest;
            params_.add(p);
            const gunit::EvalReport r = params_.evaluate(canvas_.display_unit());
            const gunit::Parameter* made = params_.find(first);
            if (made && made->ok) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "%s = %s -> %.6g", made->name.c_str(),
                              made->expression.c_str(), made->value);
                log_.push_back(buf);
            } else {
                log_.push_back("PARAMETERS: " +
                               (made && !made->error.empty() ? made->error
                                : r.cycles.empty() ? std::string("could not evaluate ") + first
                                                   : "a cycle: " + r.cycles.front()));
            }
        }
    }
    else if (n == "END" || n == "ENDP" || n == "MID" || n == "CEN" || n == "CENTRE" ||
             n == "NODE" || n == "NOD" || n == "QUA" || n == "INT" || n == "INS" ||
             n == "PER" || n == "TAN" || n == "NEA" || n == "APP" || n == "EXT" ||
             n == "PAR" || n == "GEO") {
        static const struct { const char* typed; const char* kind; } kOverrides[] = {
            {"END", "endpoint"},   {"ENDP", "endpoint"}, {"MID", "midpoint"},
            {"CEN", "center"},     {"CENTRE", "center"}, {"NODE", "node"},
            {"NOD", "node"},       {"QUA", "quadrant"},  {"INT", "intersection"},
            {"INS", "insertion"},  {"PER", "perpendicular"}, {"TAN", "tangent"},
            {"NEA", "nearest"},    {"APP", "apparent int"},  {"EXT", "extension"},
            {"PAR", "parallel"},   {"GEO", "geo center"},
        };
        for (const auto& o : kOverrides)
            if (n == o.typed) {
                if (canvas_.force_snap(o.kind)) log_.push_back(canvas_.message());
                else log_.push_back(std::string("no ") + o.kind + " snap here");
                break;
            }
    }
    else if (n == "OSNAP") {
        auto& set = canvas_.snaps().settings;
        static const struct { const char* name; gsnap::Kind k; } kModes[] = {
            {"end", gsnap::Endpoint},        {"endpoint", gsnap::Endpoint},
            {"mid", gsnap::Midpoint},        {"midpoint", gsnap::Midpoint},
            {"cen", gsnap::Center},          {"center", gsnap::Center},
            {"centre", gsnap::Center},       {"node", gsnap::Node},
            {"qua", gsnap::Quadrant},        {"quadrant", gsnap::Quadrant},
            {"int", gsnap::Intersection},    {"intersection", gsnap::Intersection},
            {"ins", gsnap::Insertion},       {"insertion", gsnap::Insertion},
            {"per", gsnap::Perpendicular},   {"perpendicular", gsnap::Perpendicular},
            {"tan", gsnap::Tangent},         {"tangent", gsnap::Tangent},
            {"nea", gsnap::Nearest},         {"nearest", gsnap::Nearest},
            {"app", gsnap::ApparentIntersection},
            {"ext", gsnap::Extension},       {"extension", gsnap::Extension},
            {"par", gsnap::Parallel},        {"parallel", gsnap::Parallel},
            {"geo", gsnap::GeometricCenter},
        };
        std::string a = command_arg_;
        for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (a.empty()) {
            set.enabled = !set.enabled;
            log_.push_back(set.enabled ? "object snap on" : "object snap off");
            publish_sysvars();
        } else if (a == "all") {
            set.running = gsnap::Endpoint | gsnap::Midpoint | gsnap::Center |
                          gsnap::Node | gsnap::Quadrant | gsnap::Intersection |
                          gsnap::Insertion | gsnap::Perpendicular | gsnap::Tangent |
                          gsnap::Nearest | gsnap::ApparentIntersection |
                          gsnap::Extension | gsnap::GeometricCenter;
            set.enabled = true;
            log_.push_back("object snap: every mode");
            publish_sysvars();
        } else if (a == "none" || a == "off") {
            set.running = gsnap::None;
            log_.push_back("object snap: no modes");
            publish_sysvars();
        } else {
            gcm::u32 want = 0;
            std::vector<std::string> unknown;
            std::size_t at = 0;
            while (at < a.size()) {
                std::size_t sp = a.find_first_of(" ,", at);
                if (sp == std::string::npos) sp = a.size();
                const std::string word = a.substr(at, sp - at);
                at = sp + 1;
                if (word.empty()) continue;
                bool found = false;
                for (const auto& m : kModes)
                    if (word == m.name) { want |= m.k; found = true; break; }
                if (!found) unknown.push_back(word);
            }
            if (want) {
                set.running = want;
                set.enabled = true;
            }
            std::string msg = "object snap: ";
            bool first = true;
            for (const auto& m : kModes) {
                if ((set.running & m.k) == 0) continue;
                if (std::strlen(m.name) <= 3) continue;
                msg += (first ? "" : ", ");
                msg += m.name;
                first = false;
            }
            if (first) msg += "none";
            for (const auto& u : unknown) msg += "  (unknown: " + u + ")";
            log_.push_back(msg);
            publish_sysvars();
        }
    }
    else if (n == "SNAP") {
        if (!command_arg_.empty()) {
            const auto d = gunit::parse_length(command_arg_, canvas_.display_unit());
            if (!d) {
                log_.push_back("SNAP: " + command_arg_ + " is not a distance");
            } else {
                canvas_.set_grid_spacing(*d);
                canvas_.set_grid_snap(*d > 0.0 || canvas_.grid_snap());
                log_.push_back(*d > 0.0
                                   ? "snap spacing " + gunit::format_length(
                                         *d, gunit::Format{canvas_.display_unit()})
                                   : "snap spacing follows the zoom");
            }
        } else {
            canvas_.set_grid_snap(!canvas_.grid_snap());
            log_.push_back(canvas_.grid_snap() ? "grid snap on" : "grid snap off");
        }
    }
    else if (n == "GRID") {
        if (!command_arg_.empty()) {
            const auto d = gunit::parse_length(command_arg_, canvas_.display_unit());
            if (!d) {
                log_.push_back("GRID: " + command_arg_ + " is not a distance");
            } else {
                canvas_.set_grid_spacing(*d);
                grid_visible_ = true;
                log_.push_back(*d > 0.0
                                   ? "grid " + gunit::format_length(
                                         *d, gunit::Format{canvas_.display_unit()})
                                   : "grid follows the zoom");
            }
        } else {
            grid_visible_ = !grid_visible_;
            log_.push_back(grid_visible_ ? "grid on" : "grid off");
        }
    }
    else if (n == "PLAN") set_lens(Lens::TwoD);
    else if (n == "ORBIT" || n == "NAVSWHEEL") {
        set_lens(Lens::ThreeD);
        log_.push_back("middle-drag orbits about the work; Shift+middle or Shift+right pans");
    }
    else if (n == "PAN") log_.push_back("Shift+middle, Shift+right-drag, or two fingers, pans the view");
    else if (n == "REGEN" || n == "REDRAW") { canvas_.blips.clear(); mesh_dirty_ = true; log_.push_back("regenerated"); }
    else if (n == "DSETTINGS" || n == "OPTIONS" || n == "UNITS" || n == "CONSTRAINTSETTINGS") {
        settings_open_=true;settings_tab_=n=="DSETTINGS" || n=="CONSTRAINTSETTINGS"?3:0;
        log_.push_back("settings opened");
    }
    else if (n == "THEME") {
        themes_.set_follow_system(false);
        themes_.activate(themes_.active().name == "GLCAD Dark" ? "GLCAD Light" : "GLCAD Dark");
        log_.push_back("theme: " + themes_.active().name);
    }
    else if (n == "LAYON") { layers_.all_on(); log_.push_back("all layers on"); }
    else if (n == "LAYFRZ") {
        if (gdoc::Layer* l = layers_.find(layers_.current())) { l->frozen = true; log_.push_back(l->name + " frozen"); }
    }
    else if (n == "LAYISO") { layers_.isolate(layers_.current()); log_.push_back(layers_.current() + " isolated"); }
    else if (n == "LAYER") log_.push_back("the Layers panel is on the right");
    else if (n == "PROPERTIES") {
        if (!command_arg_.empty()) {
            const auto split=command_arg_.find(' ');
            if (split!=std::string::npos) set_selection_property(command_arg_.substr(0,split),command_arg_.substr(split+1));
        }
        if (gui::PanelState* p = dock_.find("properties")) { p->open = true; p->collapsed = false; }
        log_.push_back(canvas_.selection().empty()
                           ? "Properties: select an object to see its properties"
                           : "Properties");
    }
    else if (n == "PRESSPULL") run_command("EXTRUDE");
    else if (n == "SLICE") run_command("SPLIT");
    else if (n == "INTERFERE") {
        if (modeller_.bodies().size() < 2) {
            log_.push_back("INTERFERE: two solids are needed");
        } else {
            gtopo::Assembly assembly;
            for (std::size_t i = 0; i < modeller_.bodies().size(); ++i) {
                gtopo::Instance in;
                in.body = &modeller_.bodies()[i];
                in.grounded = i == 0;
                in.name = "Solid " + std::to_string(i + 1);
                assembly.add(in);
            }
            const auto hits = assembly.interference(vol_);
            if (hits.empty()) {
                log_.push_back("no interference between " +
                               std::to_string(modeller_.bodies().size()) + " solids");
            } else {
                char buf[160];
                for (const auto& h : hits) {
                    if (h.touching)
                        std::snprintf(buf, sizeof(buf),
                                      "Solid %u and Solid %u touch (no overlapping volume)",
                                      h.a + 1, h.b + 1);
                    else
                        std::snprintf(buf, sizeof(buf),
                                      "Solid %u and Solid %u overlap by %.6g m3",
                                      h.a + 1, h.b + 1, h.volume);
                    log_.push_back(buf);
                }
                log_.push_back(std::to_string(hits.size()) + " interference(s)");
            }
        }
    }
    else if (n == "BOX" || n == "CYLINDER") {
        gtopo::Profile p;
        const auto& pl = sketch_plane();
        if (n == "BOX") {
            for (auto [x, y] : {std::pair{-1.0, -1.0}, std::pair{1.0, -1.0}, std::pair{1.0, 1.0}, std::pair{-1.0, 1.0}})
                p.outer.push_back(gcm::to_lattice(vol_, x, y));
        } else {
            for (int i = 0; i < 48; ++i) {
                const double a = 2.0 * 3.14159265358979323846 * i / 48.0;
                p.outer.push_back(gcm::to_lattice(vol_, std::cos(a), std::sin(a)));
            }
        }
        p.normalize();
        ggeo::d3::Frame<gcm::F64> f;
        f.origin = gview::Point3::from(pl.ox, pl.oy, pl.oz);
        f.u = gview::Vec3::from(pl.ux, pl.uy, pl.uz);
        f.v = gview::Vec3::from(pl.vx, pl.vy, pl.vz);
        f.n = gview::Vec3::from(pl.nx, pl.ny, pl.nz);
        gtopo::Body prim = gtopo::extrude(p, vol_, f, 2.0);
        if (n == "CYLINDER") {
            const gtopo::Surface side = gtopo::Surface::cylinder(
                pl.ox, pl.oy, pl.oz, pl.nx, pl.ny, pl.nz, 1.0);
            if (prim.face_surface.size() < prim.face_count())
                prim.face_surface.resize(prim.face_count());
            for (gcm::u32 fi = 0; fi < prim.face_count(); ++fi) {
                const gtopo::Plane fp = gtopo::face_plane(prim, fi, vol_);
                const double along = fp.nx * pl.nx + fp.ny * pl.ny + fp.nz * pl.nz;
                if (std::fabs(along) < 0.5) prim.face_surface[fi] = side;
            }
        }
        modeller_.add_body(std::move(prim), n == "BOX" ? "box 2 x 2 x 2" : "cylinder r 1, h 2");
        log_.push_back(modeller_.message());
        last_edit_was_solid_ = true;
        if (mode_ == Mode::Sketch) set_mode(Mode::Solid);
    }
    else if (n == "AREA") {
        if (sel_target_.valid) {
            double a = 0.0;
            const auto& o = sel_target_.profile.outer;
            for (std::size_t i = 0; i < o.size(); ++i) {
                double x0, y0, x1, y1;
                gcm::to_world(vol_, o[i], x0, y0);
                gcm::to_world(vol_, o[(i + 1) % o.size()], x1, y1);
                a += x0 * y1 - x1 * y0;
            }
            char buf[96];
            const auto u = canvas_.display_unit();
            const double k = gunit::from_metres(1.0, u);
            std::snprintf(buf, sizeof(buf), "area %.6g %s^2",
                          std::fabs(a) * 0.5 * k * k, gunit::suffix(u));
            log_.push_back(buf);
            if (sel_target_.on_body &&
                sel_target_.body < gcm::i32(modeller_.bodies().size())) {
                const auto& b = modeller_.bodies()[std::size_t(sel_target_.body)];
                const gtopo::Measurement ar =
                    gtopo::measure_face_area(b, sel_target_.face, vol_);
                const gtopo::Measurement pe =
                    gtopo::measure_face_perimeter(b, sel_target_.face, vol_);
                if (ar.ok && pe.ok) {
                    std::snprintf(buf, sizeof(buf),
                                  "face: %.6g %s, perimeter %.6g %s (exact)",
                                  ar.value, ar.units.c_str(), pe.value, pe.units.c_str());
                    log_.push_back(buf);
                }
            }
        } else if (!canvas_.selection().empty() && canvas_.is_region(canvas_.selection().front())) {
            const auto region=canvas_.region_at(canvas_.selection().front());
            const auto unit=canvas_.display_unit();const double k=gunit::from_metres(1.0,unit);
            double perimeter=0;for(const auto& loop:region.loops)for(const auto& edge:loop)perimeter+=gedit::length_of(edge.shape,vol_);
            char text[160];std::snprintf(text,sizeof(text),"region area %.9g %s^2, perimeter %.9g %s",region.area(vol_)*k*k,gunit::suffix(unit),perimeter*k,gunit::suffix(unit));
            log_.push_back(text);
        } else if (!canvas_.selection().empty()) {
            const std::vector<std::size_t> run =
                canvas_.polyline_pieces(canvas_.selection().front());
            if (run.size() >= 3 && canvas_.polyline_closed(run)) {
                double a = 0.0, perim = 0.0;
                for (const std::size_t i : run) {
                    const gsnap::Shape& sh = canvas_.shapes()[i];
                    double x0, y0, x1, y1;
                    gcm::to_world(vol_, sh.a, x0, y0);
                    gcm::to_world(vol_, sh.b, x1, y1);
                    a += x0 * y1 - x1 * y0;
                    perim += sh.kind == gsnap::ShapeKind::Arc
                                 ? std::fabs(sh.end_angle - sh.start_angle) *
                                       double(sh.radius) * vol_.inv_scale()
                                 : std::hypot(x1 - x0, y1 - y0);
                }
                bool any_arc = false;
                for (const std::size_t i : run)
                    if (canvas_.shapes()[i].kind == gsnap::ShapeKind::Arc) any_arc = true;
                const auto u = canvas_.display_unit();
                const double k = gunit::from_metres(1.0, u);
                char buf[160];
                std::snprintf(buf, sizeof(buf), "area %.6g %s^2, perimeter %.6g %s%s",
                              std::fabs(a) * 0.5 * k * k, gunit::suffix(u),
                              perim * k, gunit::suffix(u),
                              any_arc ? "  (area to the chords)" : "");
                log_.push_back(buf);
            } else {
                log_.push_back(run.size() >= 3
                                   ? "AREA: that polyline is not closed"
                                   : "select a closed polyline, a region or a face");
            }
        } else {
            log_.push_back("select a region or a face first");
        }
    }
    else if (n == "ID") {
        double cx, cy;
        gcm::to_world(vol_, canvas_.snap().point, cx, cy);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.4f, %.4f", cx, cy);
        log_.push_back(buf);
    }
    else if (n == "LIST") {
        if (sel_target_.valid && sel_target_.on_body) log_.push_back(modeller_.describe(sel_target_.body));
        else if (canvas_.selection_count()==1 && !canvas_.selection().empty() && canvas_.is_region(canvas_.selection().front())) {
            const auto i=canvas_.selection().front();
            const auto region=canvas_.region_at(i);
            log_.push_back("Region  layer " + canvas_.shapes()[i].layer + "  " + std::to_string(region.loops.size()) + " loop(s)");
            run_command("AREA");
        }
        else if (canvas_.selection().size() == 1 &&
                 canvas_.selection().front() < canvas_.shapes().size()) {
            const gsnap::Shape& sh = canvas_.shapes()[canvas_.selection().front()];
            const gunit::Format fmt{canvas_.display_unit()};
            double ax, ay, bx, by;
            gcm::to_world(vol_, sh.a, ax, ay);
            gcm::to_world(vol_, sh.b, bx, by);
            std::string line = std::string(gsnap::shape_kind_name(sh.kind)) +
                               "  layer " + (sh.layer.empty() ? layers_.current() : sh.layer);
            const gdoc::Color& c = sh.attrs.color;
            line += "  colour " + (c.kind == gdoc::Color::Kind::ByLayer ? std::string("ByLayer")
                     : c.kind == gdoc::Color::Kind::True
                         ? std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b)
                         : std::to_string(c.index));
            line += "  linetype " + sh.attrs.linetype;
            line += "  lineweight " + (sh.attrs.lineweight == gdoc::Lineweight::ByLayer
                                           ? std::string("ByLayer")
                                       : sh.attrs.lineweight == gdoc::Lineweight::Default
                                           ? std::string("Default")
                                           : std::to_string(gdoc::lineweight_mm(sh.attrs.lineweight)) + " mm");
            if (sh.attrs.transparency > 0)
                line += "  transparency " + std::to_string(sh.attrs.transparency) + "%";
            log_.push_back(line);
            if (sh.is_linear() && !sh.is_infinite())
                log_.push_back("  from " + gunit::format_length(ax, fmt) + ", " + gunit::format_length(ay, fmt) +
                               "  to " + gunit::format_length(bx, fmt) + ", " + gunit::format_length(by, fmt) +
                               "  length " + gunit::format_length(std::hypot(bx - ax, by - ay), fmt));
            else if (sh.is_curve())
                log_.push_back("  centre " + gunit::format_length(ax, fmt) + ", " + gunit::format_length(ay, fmt) +
                               "  radius " + gunit::format_length(double(sh.radius) * vol_.inv_scale(), fmt));
        }
        else log_.push_back(std::to_string(canvas_.selection().size()) + " selected, " +
                            std::to_string(canvas_.shapes().size()) + " objects in the sketch, " +
                            std::to_string(modeller_.bodies().size()) + " solids");
    }
    else if (n == "MEASUREGEOM") {
        bool said = false;
        if (sel_target_.valid && sel_target_.on_body &&
            sel_target_.body < gcm::i32(modeller_.bodies().size())) {
            const auto& b = modeller_.bodies()[std::size_t(sel_target_.body)];
            const auto ar = gtopo::measure_face_area(b, sel_target_.face, vol_);
            const auto pe = gtopo::measure_face_perimeter(b, sel_target_.face, vol_);
            char buf[192];
            if (ar.ok && pe.ok) {
                std::snprintf(buf, sizeof(buf), "face: area %.6g %s, perimeter %.6g %s",
                              ar.value, ar.units.c_str(), pe.value, pe.units.c_str());
                log_.push_back(buf);
                said = true;
            }
            const gtopo::Body& body = b;
            gcm::u32 reported = 0;
            for (gcm::u32 f = 0; f < body.face_count() && reported < 3; ++f) {
                if (f == sel_target_.face) continue;
                const auto an = gtopo::measure_angle_between_faces(body, sel_target_.face, f, vol_);
                if (!an.ok) continue;
                const double deg = an.value * 180.0 / 3.14159265358979323846;
                if (deg < 0.5 || deg > 179.5) continue;
                std::snprintf(buf, sizeof(buf), "  to face %u: %.4g degrees", f, deg);
                log_.push_back(buf);
                ++reported;
                said = true;
            }
        }
        if (modeller_.bodies().size() >= 2) {
            const auto cl = gtopo::measure_clearance(modeller_.bodies()[0],
                                                     modeller_.bodies()[1], vol_);
            if (cl.ok) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "clearance between the first two solids: %.6g %s",
                              cl.value, cl.units.c_str());
                log_.push_back(buf);
                said = true;
            }
        }
        if (!said) run_command("DIST");
    }
    else if (n == "RECOGNISE" || n == "RECOGNIZE") {
        if (modeller_.bodies().empty()) {
            log_.push_back("RECOGNISE: no solid");
        } else {
            const std::size_t which =
                (sel_target_.valid && sel_target_.on_body &&
                 sel_target_.body < gcm::i32(modeller_.bodies().size()))
                    ? std::size_t(sel_target_.body) : 0;
            const gtopo::RecognitionResult r =
                gtopo::recognise(modeller_.bodies()[which], vol_);
            if (!r.ok) {
                log_.push_back(std::string("RECOGNISE: ") + r.reason);
            } else if (r.features.empty()) {
                log_.push_back("no features recognised in " +
                               std::to_string(r.faces_total) + " faces");
            } else {
                char buf[192];
                for (const auto& f : r.features) {
                    std::snprintf(buf, sizeof(buf), "%s (%.0f%% sure): %s",
                                  gtopo::feature_kind_name(f.kind),
                                  f.confidence * 100.0, f.why.c_str());
                    log_.push_back(buf);
                }
                std::snprintf(buf, sizeof(buf),
                              "%zu feature(s); %zu of %zu faces explained",
                              r.features.size(), r.faces_explained, r.faces_total);
                log_.push_back(buf);

                if (uppercase(command_arg_).rfind("ATTACH", 0) == 0) {
                    gtopo::Body b = modeller_.bodies()[which];
                    if (b.face_surface.size() < b.face_count())
                        b.face_surface.resize(b.face_count());
                    std::size_t attached = 0, declined = 0;
                    for (const auto& f : r.features) {
                        if (f.radius <= 0.0) continue;
                        if (!f.certain()) { ++declined; continue; }
                        if (f.kind != gtopo::FeatureKind::Hole &&
                            f.kind != gtopo::FeatureKind::Boss &&
                            f.kind != gtopo::FeatureKind::Fillet)
                            continue;
                        const gtopo::Surface surf = gtopo::Surface::cylinder(
                            f.origin[0], f.origin[1], f.origin[2],
                            f.axis[0], f.axis[1], f.axis[2], f.radius);
                        for (const gtopo::FaceId fi : f.primary_faces)
                            if (fi < b.face_surface.size() &&
                                b.face_surface[fi].kind == gtopo::SurfaceKind::Plane) {
                                b.face_surface[fi] = surf;
                                ++attached;
                            }
                    }
                    if (attached) {
                        modeller_.replace_body(which, std::move(b));
                        mesh_dirty_ = true;
                    }
                    std::snprintf(buf, sizeof(buf),
                                  "attached %zu exact surface(s)%s", attached,
                                  declined ? ("; " + std::to_string(declined) +
                                              " hypothesis(es) were not certain enough")
                                                 .c_str()
                                           : "");
                    log_.push_back(buf);
                }
            }
        }
    }
    else if (n == "AUDIT") {
        std::size_t problems = 0;
        char buf[192];
        for (std::size_t i = 0; i < modeller_.bodies().size(); ++i) {
            const gtopo::Topology t = modeller_.bodies()[i].analyse();
            if (t.closed && t.manifold) continue;
            std::snprintf(buf, sizeof(buf),
                          "solid %zu: %s%s (V%u E%u F%u, genus %lld)", i,
                          t.closed ? "" : "not closed ",
                          t.manifold ? "" : "not manifold",
                          t.vertices, t.edges, t.faces,
                          static_cast<long long>(t.genus));
            log_.push_back(buf);
            ++problems;
        }
        std::size_t orphans = 0;
        for (const auto& h : canvas_.hatches()) if (h.orphaned) ++orphans;
        if (orphans) {
            std::snprintf(buf, sizeof(buf),
                          "%zu hatch(es) have lost their boundary", orphans);
            log_.push_back(buf);
            problems += orphans;
        }
        std::size_t lost_dims = 0;
        for (const auto& d : canvas_.dimensions()) if (d.orphaned) ++lost_dims;
        if (lost_dims) {
            std::snprintf(buf, sizeof(buf),
                          "%zu dimension(s) measure geometry this drawing no "
                          "longer has", lost_dims);
            log_.push_back(buf);
            problems += lost_dims;
        }
        std::size_t missing_defs = 0;
        for (const auto& r : canvas_.blocks().references())
            if (!canvas_.blocks().find(r.name)) ++missing_defs;
        if (missing_defs) {
            std::snprintf(buf, sizeof(buf),
                          "%zu block insertion(s) name a definition this drawing "
                          "does not have", missing_defs);
            log_.push_back(buf);
            problems += missing_defs;
        }
        std::size_t off_layer = 0;
        for (const auto& sh : canvas_.shapes())
            if (!sh.layer.empty() && !layers_.find(sh.layer)) ++off_layer;
        if (off_layer) {
            std::snprintf(buf, sizeof(buf),
                          "%zu object(s) are on a layer the drawing does not define",
                          off_layer);
            log_.push_back(buf);
            problems += off_layer;
        }
        log_.push_back(problems ? "AUDIT: " + std::to_string(problems) + " problem(s) found"
                                : "AUDIT: no problems found");
    }
    else if (n == "NEW") {
        reset_document();
        log_.push_back("new drawing");
    }
    else if (n == "NEWTAB") { new_tab(); log_.push_back("new tab: " + tab_title_); }
    else if (n == "CLOSETAB") { close_tab(active_tab_); }
    else if (n == "NEXTTAB") { if (tab_count() > 1) switch_tab((active_tab_ + 1) % tab_count()); }
    else if (n == "PREVTAB") { if (tab_count() > 1) switch_tab((active_tab_ + tab_count() - 1) % tab_count()); }
    else if (n == "SELECTALL") { canvas_.select_all(); log_.push_back(std::to_string(canvas_.selection_count()) + " selected"); }
    else if (n == "ISO") { set_lens(Lens::ThreeD); camera_.look(gview::StandardView::Iso); sync_view_from_camera(); }
    else if (n == "ZOOMEXTENTS") {
        double lo_x = 1e300, lo_y = 1e300, lo_z = 1e300, hi_x = -1e300, hi_y = -1e300, hi_z = -1e300;
        bool any = false;
        for (const auto& b : modeller_.bodies()) {
            if(in_paper_space_ || modeller_.body_hidden(std::size_t(&b-modeller_.bodies().data()))) continue;
            const auto bb = b.bounds(vol_);
            lo_x = std::min(lo_x, bb.lo.x.to_f64()); hi_x = std::max(hi_x, bb.hi.x.to_f64());
            lo_y = std::min(lo_y, bb.lo.y.to_f64()); hi_y = std::max(hi_y, bb.hi.y.to_f64());
            lo_z = std::min(lo_z, bb.lo.z.to_f64()); hi_z = std::max(hi_z, bb.hi.z.to_f64());
            any = true;
        }
        const auto see2 = [&](double ax, double ay) {
            double wx, wy, wz;
            sketch_plane().to_world(ax, ay, wx, wy, wz);
            lo_x = std::min(lo_x, wx); hi_x = std::max(hi_x, wx);
            lo_y = std::min(lo_y, wy); hi_y = std::max(hi_y, wy);
            lo_z = std::min(lo_z, wz); hi_z = std::max(hi_z, wz);
            any = true;
        };
        const auto see_rect=[&](double lx,double ly,double hx,double hy) {
            see2(lx,ly);see2(lx,hy);see2(hx,ly);see2(hx,hy);
        };
        const auto see_shape=[&](const gsnap::Shape& sh) {
            if(canvas_.layer_hidden(sh.layer) || sh.is_infinite()) return;
            double ax,ay,bx,by;gcm::to_world(vol_,sh.a,ax,ay);
            if(sh.is_curve()) {
                const double r=double(sh.radius)*vol_.inv_scale();
                see_rect(ax-r,ay-r,ax+r,ay+r);
            } else if(sh.is_elliptical()) {
                gcm::to_world(vol_,sh.b,bx,by);
                const double major=std::hypot(bx-ax,by-ay),minor=double(sh.radius)*vol_.inv_scale();
                const double c=major>0?(bx-ax)/major:1,s=major>0?(by-ay)/major:0;
                const double rx=std::hypot(major*c,minor*s),ry=std::hypot(major*s,minor*c);
                see_rect(ax-rx,ay-ry,ax+rx,ay+ry);
            } else {
                see2(ax,ay);
                if(sh.kind==gsnap::ShapeKind::Segment){gcm::to_world(vol_,sh.b,bx,by);see2(bx,by);}
            }
        };
        for(const auto& sh:canvas_.shapes())see_shape(sh);
        for(const auto& spline:canvas_.splines())
            for(const auto& point:spline.tessellate(1e-4))see2(point.x.to_f64(),point.y.to_f64());
        for (const auto& ref : canvas_.blocks().references())
            if(!canvas_.layer_hidden(ref.layer))
                for(const auto& sh:canvas_.blocks().expand(ref,vol_,8,&layers_))see_shape(sh);
        for (const auto& ref : canvas_.blocks().references()) {
            if(canvas_.layer_hidden(ref.layer))continue;
            const auto labels=canvas_.reference_labels(ref);
            if(labels.empty())continue;
            double lx=0,ly=0,hx=0,hy=0;
            if(canvas_.labels_bounds(labels,lx,ly,hx,hy))see_rect(lx,ly,hx,hy);
        }
        for (const auto& t : canvas_.texts()) {
            if (canvas_.layer_hidden(t.layer)) continue;
            double lx=0,ly=0,hx=0,hy=0;
            if (canvas_.labels_bounds(canvas_.note_labels(t),lx,ly,hx,hy)) {
                see_rect(lx,ly,hx,hy);
                continue;
            }
            double ax, ay;
            gcm::to_world(vol_, t.pos, ax, ay);
            see2(ax, ay);
        }
        for(const auto& instance:canvas_.underlay_instances()) {
            const auto& u=instance.underlay;const auto& transform=instance.transform;
            if(!u.visible||!u.found||u.fade>=100)continue;
            const auto placed=[&](gdoc::Point2 p) {
                double x,y;u.to_drawing(p,vol_,x,y);
                return gclip::Vertex{transform[0]*x+transform[1]*y+transform[2],transform[3]*x+transform[4]*y+transform[5]};
            };
            std::array<gclip::Vertex,4> corners{placed(u.extent_lo),placed({u.extent_hi.x,u.extent_lo.y}),placed(u.extent_hi),placed({u.extent_lo.x,u.extent_hi.y})};
            std::vector<gclip::Vertex> quad;for(int k:{0,1,2,0,2,3})quad.push_back(corners[k]);
            auto clips=instance.clips;
            if(u.has_clip()) {
                gclip::Boundary mask;mask.inverted=u.clip_inverted;
                for(auto p:u.clip){const auto q=placed(p);mask.points.push_back(gcm::to_lattice(vol_,q.x,q.y));}
                clips.push_back(std::move(mask));
                if(u.clip_hole.size()>=3) {
                    gclip::Boundary hole;hole.inverted=true;
                    for(auto p:u.clip_hole){const auto q=placed(p);hole.points.push_back(gcm::to_lattice(vol_,q.x,q.y));}
                    clips.push_back(std::move(hole));
                }
            }
            for(const auto& p:gclip::triangles(quad,clips,vol_).vertices)see2(p.x,p.y);
        }

        for(const auto& d:canvas_.dimensions())if(!canvas_.layer_hidden(d.layer))for(auto p:d.kind==gdim::Kind::Leader?d.leader_grips():std::vector<gcm::Lattice2>{d.p1,d.p2,d.p3}){double x,y;gcm::to_world(vol_,p,x,y);see2(x,y);}
        if (any) {
            history_.record(camera_);
            auto bounds=ggeo::d3::AABB<gcm::F64>::empty();
            bounds.expand(gview::Point3::from(lo_x,lo_y,lo_z));
            bounds.expand(gview::Point3::from(hi_x,hi_y,hi_z));
            pending_extent_fit_=bounds;
            sync_view_from_camera();
            const auto fit = canvas_fit_bounds();
            camera_.zoom_to_region(bounds,fit.x,fit.y,fit.w,fit.h);
            sync_view_from_camera();
            navigated_ = true;
        }
    }
    else if (n == "ABOUT") {
        if (product_hook_) product_hook_();
        else log_.push_back(std::string("GLCAD ") + build_stamp() + " -- an exact-arithmetic CAD. Sketch, then pull.");
    }
    else if (n == "TEXTEDIT" || n == "DDEDIT") {
        const auto& sel = canvas_.selected_texts();
        if (sel.empty()) {
            log_.push_back(n + ": select a note first");
        } else if (sel.front() < canvas_.texts().size()) {
            const auto& note = canvas_.texts()[sel.front()];
            text_editing_ = sel.front();
            text_entry_ = true;
            text_placing_ = false;
            text_buffer_ = note.text;
            text_pos_ = note.pos;
            text_width_ = note.width;
            log_.push_back("edit the note, Enter to keep it, Esc to leave it as it was");
        }
    }
    else if (n == "FIND") {
        if (command_arg_.empty()) {
            log_.push_back("FIND <text>: selects the notes that contain it");
        } else {
            const auto lower = [](std::string t) {
                for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
                return t;
            };
            const std::string want = lower(command_arg_);
            std::vector<std::size_t> hits;
            for (std::size_t i = 0; i < canvas_.texts().size(); ++i) {
                const std::string plain =
                    gtext::parse_mtext(canvas_.texts()[i].text).plain();
                if (lower(plain).find(want) != std::string::npos) hits.push_back(i);
            }
            std::size_t overrides = 0;
            for (const auto& d : canvas_.dimensions())
                if (d.has_override() && lower(d.text_override).find(want) != std::string::npos)
                    ++overrides;
            canvas_.set_selected_texts(hits);
            std::string said = std::to_string(hits.size()) + " note(s) contain \"" +
                               command_arg_ + "\"";
            if (overrides)
                said += ", and " + std::to_string(overrides) + " dimension override(s)";
            log_.push_back(said);
        }
    }
    else if (n == "TEXT" || n == "MTEXT") {
        text_width_ = 0.0;
        if (n == "MTEXT" && !command_arg_.empty())
            text_width_ = gunit::parse_length(command_arg_, canvas_.display_unit())
                              .value_or(0.0);
        text_placing_ = true;
        text_entry_ = false;
        text_editing_ = static_cast<std::size_t>(-1);
        text_buffer_.clear();
        modeller_.arm(Modeller::Op::None);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = ToolId::Text;
        if (!in_paper_space_ && mode_ != Mode::Sketch) edit_sketch(active_sketch_ >= 0 ? active_sketch_ : 0), active_tool_id_ = ToolId::Text;
        log_.push_back("click where the text starts, then type it and press Enter");
    }
    else if (n == "LIFT") {
        const double h = command_arg_.empty()
                             ? 0.0
                             : gunit::parse_length(command_arg_, canvas_.display_unit()).value_or(0.0);
        if (std::fabs(h) < 1e-9) {
            pending_args_ = "LIFT";
            pending_hint_ = "height";
            log_.push_back("LIFT: type the height, e.g. 2.7");
        }
        else {
            store_active_sketch();
            rebuild_sketch_segments();
            build_frame_sources();
            if (modeller_.lift_all(active_sketch_, h) > 0) after_commit();
            else log_.push_back(modeller_.message());
        }
    }
    else if (n == "WALLS") {
        double t = 0.0, h = 0.0;
        bool walls_ok = false, make_solids = false;
        {
            const std::size_t sp = command_arg_.find_last_of(' ');
            if (sp != std::string::npos) {
                const auto ta = gunit::parse_length(command_arg_.substr(0, sp), canvas_.display_unit());
                const auto tb = gunit::parse_length(command_arg_.substr(sp + 1), canvas_.display_unit());
                if (ta && tb) { t = *ta; h = *tb; walls_ok = true; make_solids = true; }
            }
            if (!walls_ok) if (const auto thickness = gunit::parse_length(command_arg_, canvas_.display_unit())) {
                t = *thickness; walls_ok = true;
            }
        }
        if (!walls_ok || !std::isfinite(t) || t <= 0.0 ||
            (make_solids && (!std::isfinite(h) || std::fabs(h) < 1e-9))) {
            pending_args_ = "WALLS";
            pending_hint_ = "thickness [3D height]";
            log_.push_back("WALLS: thickness creates 2D walls (e.g. 200mm); add a height for 3D (200mm 2.7m)");
        }
        else if (!make_solids) {
            if (!in_paper_space_ && mode_ != Mode::Sketch)
                edit_sketch(active_sketch_ >= 0 ? active_sketch_ : 0);
            const auto converted = canvas_.walls_from_lines(t);
            const auto message = canvas_.message();
            if (converted) {
                canvas_.set_tool(Tool::Select);
                active_tool_id_ = ToolId::Select;
            }
            log_.push_back(message);
        }
        else {
            store_active_sketch();
            rebuild_sketch_segments();
            build_frame_sources();
            std::vector<gdoc::Segment2> centerlines;
            for(auto i:canvas_.wall_centerline_indices())centerlines.push_back({canvas_.shapes()[i].a,canvas_.shapes()[i].b});
            const auto material=canvas_.wall_material_segments();
            if (modeller_.walls(active_sketch_, t, h, &centerlines, &material) > 0) after_commit();
            else log_.push_back(modeller_.message());
        }
    }
    else if (n == "ASKV") {
        if (command_arg_.empty()) log_.push_back("ASKV <question> -- asks with a screenshot of the view attached");
        else ai_send(command_arg_, /*with_view=*/true);
    }
    else if (n == "ASK") {
        if (command_arg_.empty()) {
            if (gui::PanelState* p = dock_.find("assistant")) p->open = true;
            dock_.collapse("assistant", false);
            log_.push_back("ASK <question>, or type into the Assistant panel");
        }
        else ai_send(command_arg_);
    }
    else if (n == "ASSISTANT") {
        if (gui::PanelState* p = dock_.find("assistant")) p->open = true;
        dock_.collapse("assistant", false);
        ai_ensure_discovery();
    }
    else if (n == "AISERVER") {
        if (command_arg_.empty()) log_.push_back("AISERVER <url> [name] -- e.g. AISERVER http://192.168.1.20:8000 studio");
        else {
            std::istringstream in(command_arg_);
            std::string url, name;
            in >> url >> name;
            const auto c = gai::Client::from_url(url);
            if (!c) log_.push_back("AISERVER: could not read that URL");
            else {
                gfile::AiServerRecord rec;
                rec.url = c->url();
                rec.name = name;
                rec.manual = true;
                bool known = false;
                for (const auto& srv : app_config_.ai_servers) known = known || srv.url == rec.url;
                if (!known) app_config_.ai_servers.push_back(rec);
                if (app_config_.ai_active_url.empty()) app_config_.ai_active_url = rec.url;
                save_app_config();
                log_.push_back("AI server saved: " + rec.url);
            }
        }
    }
    else if (n == "AIUSE") {
        const auto c = gai::Client::from_url(command_arg_);
        if (!c) log_.push_back("AIUSE <url> -- which server the assistant talks to");
        else {
            app_config_.ai_active_url = c->url();
            save_app_config();
            log_.push_back("assistant now talks to " + c->url());
        }
    }
    else if (n == "AIMODEL") {
        app_config_.ai_model = command_arg_;
        save_app_config();
        log_.push_back(command_arg_.empty() ? "assistant model: first available"
                                            : "assistant model: " + command_arg_);
    }
    else if (n == "SPEAK") {
        if (command_arg_.empty()) log_.push_back("SPEAK <text> -- reads it through the AI server");
        else if (const auto client = ai_client(); !client) log_.push_back("SPEAK: no AI server (AISERVER <url> adds one)");
        else if (!audio_play_hook) log_.push_back("SPEAK: audio playback is not wired on this platform yet");
        else if (!ai_->busy) {
            ai_join_worker();
            ai_->busy = true;
            ai_->worker = gcm::BigStackThread([st = ai_.get(), play = audio_play_hook,
                                       c = *client, text = command_arg_] {
                gai::SpeechRequest req;
                req.input = text;
                if (const auto m = c.models(); m.ok())
                    req.model = pick_model_by_label(m->data, "tts", "kokoro");
                const auto r = c.speech(req);
                if (r.ok()) play(*r, "audio/wav");
                std::lock_guard<std::mutex> lock(st->mutex);
                if (!r.ok()) st->chat.push_back({"assistant", "(SPEAK: " + r.error.message + ")", {}});
                st->busy = false;
            });
        }
    }
    else if (n == "PLACELAYOUT") {
        if (mode_ != Mode::Drawing) enter_drawing_mode();
        modeller_.arm(Modeller::Op::None);
        canvas_.set_tool(Tool::Select);
        active_tool_id_ = ToolId::PlaceLayout;
        log_.push_back("PLACE: drag the drawing on the sheet; it snaps to the centre and edges");
    }
    else if (n == "PLOT") {
        if (!command_arg_.empty()) export_file(command_arg_);
        else if (file_hook_) ask_file(FileAsk::ExportPdf);
        else log_.push_back("PLOT <file.pdf>");
    }
    else if (n == "OPEN" || n == "SAVE" || n == "SAVEAS" || n == "IMPORT" || n == "EXPORT") {
        if (n == "SAVE" && command_arg_.empty() && !document_path_.empty()) save_file(document_path_);
        else if (!command_arg_.empty()) {
            const std::string path = command_arg_;
            if (n == "OPEN") open_file(path);
            else if (n == "SAVE" || n == "SAVEAS") save_file(path);
            else if (n == "IMPORT") import_file(path);
            else export_file(path);
        } else {
            ask_file(n == "OPEN" ? FileAsk::Open : n == "IMPORT" ? FileAsk::Import
                   : n == "EXPORT" ? FileAsk::Export : FileAsk::Save);
        }
    }
    else if (!handled_by_table && !run_extra_command(n)) {
        log_.push_back(n + ": not available in this build");
    }
    mesh_dirty_ = true;
}
}
