#include "geometry/solid_edit.hpp"
#include "exchange/views.hpp"
#include "ui/shell.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <sstream>
#include <unordered_set>

#include "engine/document_file.hpp"
#include "exchange/page.hpp"
#include "exchange/plot_style.hpp"
#include "engine/script.hpp"
#include "engine/lisp.hpp"
#include "geometry/edit.hpp"
#include "geometry/clip.hpp"
#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/nurbs.hpp"
#include "geometry/revolve.hpp"

namespace gcapp {

namespace {
constexpr double kPi = 3.14159265358979323846;

}

void Shell::begin_pick(int n, std::string prompt,
                       std::function<void(const std::vector<gcm::Lattice2>&)> done) {
    pick_ = PointPick{};
    pick_.wanted = n;
    pick_.prompt = std::move(prompt);
    pick_.done = std::move(done);
    canvas_.set_tool(Tool::Select);
    log_.push_back(pick_.prompt);
}

std::vector<std::size_t> Shell::selected_indices() const {
    std::vector<std::size_t> out;
    for (std::size_t i : canvas_.selection())
        if (i < canvas_.shapes().size()) out.push_back(i);
    return out;
}

void LispDelete::operator()(gcmd::Interpreter* p) const noexcept { delete p; }
void ScriptDelete::operator()(gcmd::ScriptRunner* p) const noexcept { delete p; }

void Shell::ensure_lisp() {
    if (lisp_) return;
    lisp_.reset(new gcmd::Interpreter());
    lisp_->define("ssget", [this](gcmd::Interpreter&, const gcmd::LispList& args) {
        gcmd::LispList out;
        if (args.size() >= 2 && args[0].text == "X") {
            for (const auto& sh : canvas_.shapes())
                if (sh.layer == args[1].text)
                    out.push_back(gcmd::LispValue::num(gcm::i64(sh.key)));
        } else {
            for (std::size_t i : canvas_.selection())
                if (i < canvas_.shapes().size())
                    out.push_back(gcmd::LispValue::num(gcm::i64(canvas_.shapes()[i].key)));
        }
        return gcmd::LispValue::list(std::move(out));
    });
    lisp_->define("entget", [this](gcmd::Interpreter&, const gcmd::LispList& args) {
        if (args.empty()) return gcmd::LispValue::nil();
        const std::size_t i = canvas_.index_of_key(gcm::u32(args[0].as_int()));
        if (i == Canvas::npos) return gcmd::LispValue::nil();
        const gsnap::Shape& sh = canvas_.shapes()[i];
        double ax, ay, bx, by;
        gcm::to_world(vol_, sh.a, ax, ay);
        gcm::to_world(vol_, sh.b, bx, by);
        const auto pair = [](gcm::i64 code, gcmd::LispValue v) {
            gcmd::LispList p{gcmd::LispValue::num(code), std::move(v)};
            return gcmd::LispValue::list(std::move(p));
        };
        const auto point = [](double x, double y) {
            gcmd::LispList p{gcmd::LispValue::num(x), gcmd::LispValue::num(y)};
            return gcmd::LispValue::list(std::move(p));
        };
        gcmd::LispList out;
        out.push_back(pair(-1, gcmd::LispValue::num(gcm::i64(sh.key))));
        out.push_back(pair(0, gcmd::LispValue::str(sh.region ? "REGION" : gsnap::shape_kind_name(sh.kind))));
        out.push_back(pair(8, gcmd::LispValue::str(sh.layer.empty() ? "0" : sh.layer)));
        out.push_back(pair(10, point(ax, ay)));
        if (sh.kind == gsnap::ShapeKind::Segment) out.push_back(pair(11, point(bx, by)));
        if (sh.is_curve())
            out.push_back(pair(40, gcmd::LispValue::num(double(sh.radius) * vol_.inv_scale())));
        return gcmd::LispValue::list(std::move(out));
    });
    lisp_->define("entmod", [this](gcmd::Interpreter&, const gcmd::LispList& args) {
        if (args.size() < 3) return gcmd::LispValue::nil();
        const std::size_t i = canvas_.index_of_key(gcm::u32(args[0].as_int()));
        if (i == Canvas::npos) return gcmd::LispValue::nil();
        const gcm::i64 code = args[1].as_int();
        const gcmd::LispValue v = args[2];
        bool ok = false;
        canvas_.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            if (i >= sh.size()) return;
            for (auto member : canvas_.polyline_pieces(i)) {
                if (code == 8) { sh[member].layer = v.text; ok = true; }
                else if (code == 62) { sh[member].attrs.color = gdoc::Color::aci(gcm::i32(v.as_int())); ok = true; }
                else if (code == 6) { sh[member].attrs.linetype = v.text; ok = true; }
            }
        });
        return ok ? args[0] : gcmd::LispValue::nil();
    });
    lisp_->define("entmake", [this](gcmd::Interpreter&, const gcmd::LispList& args) {
        if (args.size() < 5) return gcmd::LispValue::nil();
        std::string kind = args[0].text;
        for (char& c : kind) c = char(std::toupper(static_cast<unsigned char>(c)));
        gsnap::Shape sh;
        if (kind == "CIRCLE") {
            sh.kind = gsnap::ShapeKind::Circle;
            sh.a = gcm::to_lattice(vol_, args[1].as_real(), args[2].as_real());
            sh.b = sh.a;
            sh.radius = gcm::i64(std::llround(args[3].as_real() * vol_.scale()));
        } else {
            sh.kind = gsnap::ShapeKind::Segment;
            sh.a = gcm::to_lattice(vol_, args[1].as_real(), args[2].as_real());
            sh.b = gcm::to_lattice(vol_, args[3].as_real(), args[4].as_real());
        }
        sh.layer = args.size() >= 6 && !args[5].text.empty() ? args[5].text
                                                             : canvas_.current_layer();
        canvas_.modify_shapes([&](std::vector<gsnap::Shape>& out) { out.push_back(sh); });
        const gcm::u32 key = canvas_.shapes().empty() ? 0 : canvas_.shapes().back().key;
        return gcmd::LispValue::num(gcm::i64(key));
    });
    lisp_->define("entdel", [this](gcmd::Interpreter&, const gcmd::LispList& args) {
        if (args.empty()) return gcmd::LispValue::nil();
        const std::size_t i = canvas_.index_of_key(gcm::u32(args[0].as_int()));
        if (i == Canvas::npos) return gcmd::LispValue::nil();
        auto members = canvas_.polyline_pieces(i);
        std::sort(members.rbegin(),members.rend());
        canvas_.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            for(auto member:members) sh.erase(sh.begin()+static_cast<long>(member));
        });
        return gcmd::LispValue::truth();
    });
}

void Shell::sync_lisp_vars(bool into_lisp) {
    if (!lisp_) return;
    auto& v = lisp_->variables();
    const auto lower = [](std::string t) {
        for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    if (into_lisp) {
        for (const gcmd::SysVar& sv : vars_.all()) {
            switch (sv.type) {
                case gcmd::VarType::Int:  v[lower(sv.name)] = gcmd::LispValue::num(sv.ival); break;
                case gcmd::VarType::Real: v[lower(sv.name)] = gcmd::LispValue::num(sv.rval); break;
                case gcmd::VarType::Text: v[lower(sv.name)] = gcmd::LispValue::str(sv.sval); break;
            }
        }
        return;
    }
    for (const gcmd::SysVar& sv : vars_.all()) {
        const auto it = v.find(lower(sv.name));
        if (it == v.end()) continue;
        switch (sv.type) {
            case gcmd::VarType::Int:
                if (it->second.numeric() && it->second.as_int() != sv.ival)
                    run_command("SETVAR " + sv.name + " " + std::to_string(it->second.as_int()));
                break;
            case gcmd::VarType::Real:
                if (it->second.numeric() && it->second.as_real() != sv.rval)
                    run_command("SETVAR " + sv.name + " " + std::to_string(it->second.as_real()));
                break;
            case gcmd::VarType::Text:
                if (it->second.type == gcmd::LispType::Str && it->second.text != sv.sval)
                    run_command("SETVAR " + sv.name + " " + it->second.text);
                break;
        }
    }
}

void Shell::run_script(gcmd::Script sc) {
    if (!script_) {
        script_.reset(new gcmd::ScriptRunner(*cli_, commands_));
        script_->on_echo = [this](const std::string& text) { log_.push_back(text); };
        script_->on_input = [this](const std::string& text) {
            if (text.empty()) { canvas_.cancel(); return; }
            const char c = text[0];
            const bool looks_like_a_point =
                (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.' ||
                 c == '@') && text.find(',') != std::string::npos;
            if (looks_like_a_point && canvas_.tool() != Tool::Select) {
                canvas_.type_point(text.c_str());
                return;
            }
            if (!pending_args_.empty() &&
                (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-')) {
                const std::string cmd = pending_args_;
                pending_args_.clear();
                run_command(cmd + " " + text);
                return;
            }
            run_command(text);
        };
        script_->on_delay = [this](int ms) {
            log_.push_back("delay " + std::to_string(ms) + " ms");
        };
    }
    const std::string name = sc.name;
    script_->load(std::move(sc));
    const gcmd::RunReport r = script_->run();
    report_script(r);
    if (r.state == gcmd::RunState::Paused)
        log_.push_back("script " + name + " is paused -- RESUME continues it");
}

void Shell::report_script(const gcmd::RunReport& r) {
    for (const auto& line : script_->log()) log_.push_back(line);
    char buf[192];
    std::snprintf(buf, sizeof(buf), "script: %zu step(s) run, %zu failed%s%s",
                  r.executed, r.failed,
                  r.message.empty() ? "" : " -- ", r.message.c_str());
    log_.push_back(buf);
}

bool Shell::run_extra_command(const std::string& n) {
    const std::string& arg = command_arg_;
    std::vector<double> nums;
    const bool have_nums = parse_numbers(arg, nums);
    const auto need_selection = [&](const char* what) {
        if (!canvas_.selection().empty()) return true;
        log_.push_back(std::string(what) + ": select objects first");
        return false;
    };
    const bool region_selected=std::any_of(canvas_.selection().begin(),canvas_.selection().end(),[&](auto i){return canvas_.is_region(i);});
    if(region_selected && n=="STRETCH") {
        log_.push_back(n+": use region booleans, or EXPLODE to edit individual boundaries");return true;
    }
    const auto world = [&](gcm::Lattice2 p, double& x, double& y) { gcm::to_world(vol_, p, x, y); };
    const auto lat = [&](double x, double y) { return gcm::to_lattice(vol_, x, y); };
    const auto ask = [&](const char* cmd, const char* hint, const char* usage) {
        pending_args_ = cmd;
        pending_hint_ = hint;
        log_.push_back(usage);
    };

    if (n == "STRETCH") {
        if (!need_selection("STRETCH")) return true;
        if (nums.size() < 2) { ask("STRETCH", "dx dy", "STRETCH: type the displacement, e.g. 1 0 (the crossing window is the last selection window)"); return true; }
        const std::vector<std::size_t> sel = selected_indices();
        gedit::Window w;
        if (have_last_band_) w = gedit::make_window(last_band_lo_, last_band_hi_);
        else {
            gcm::Lattice2 lo{INT64_MAX, INT64_MAX}, hi{INT64_MIN, INT64_MIN};
            for (std::size_t i : sel) for (const auto& q : {canvas_.shapes()[i].a, canvas_.shapes()[i].b}) {
                lo.x = std::min(lo.x, q.x); lo.y = std::min(lo.y, q.y); hi.x = std::max(hi.x, q.x); hi.y = std::max(hi.y, q.y);
            }
            w = gedit::make_window(lo, hi);
        }
        const gcm::i64 dx = static_cast<gcm::i64>(std::llround(nums[0] * vol_.scale()));
        const gcm::i64 dy = static_cast<gcm::i64>(std::llround(nums[1] * vol_.scale()));
        std::size_t moved = 0;
        canvas_.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            std::vector<gsnap::Shape> chosen;
            for (std::size_t i : sel) chosen.push_back(sh[i]);
            const gedit::StretchResult r = gedit::stretch(chosen, w, dx, dy);
            for (std::size_t k = 0; k < sel.size() && k < r.shapes.size(); ++k) sh[sel[k]] = r.shapes[k];
            moved = r.moved + r.stretched;
        });
        log_.push_back("stretched " + std::to_string(moved) + " object(s)");
        return true;
    }


    if (n == "SPHERE" || n == "CONE" || n == "TORUS" || n == "WEDGE") {
        const char* usage = n == "SPHERE" ? "SPHERE: type the radius, e.g. 1"
                          : n == "CONE"   ? "CONE: type the base radius and height, e.g. 1 2"
                          : n == "TORUS"  ? "TORUS: type the ring radius and tube radius, e.g. 2 0.5"
                                          : "WEDGE: type the length, width and height, e.g. 2 1 1";
        const std::size_t want = n == "SPHERE" ? 1 : n == "WEDGE" ? 3 : 2;
        if (nums.size() < want) { ask(n.c_str(), n == "SPHERE" ? "radius" : n == "CONE" ? "radius height" : n == "TORUS" ? "ring tube" : "length width height", usage); return true; }
        for (double v : nums) if (v <= 0.0) { log_.push_back(n + ": positive values, please"); return true; }
        const auto& pl = sketch_plane();
        ggeo::d3::Frame<gcm::F64> frame;
        frame.origin = gview::Point3::from(pl.ox, pl.oy, pl.oz);
        frame.u = gview::Vec3::from(pl.ux, pl.uy, pl.uz);
        frame.v = gview::Vec3::from(pl.vx, pl.vy, pl.vz);
        frame.n = gview::Vec3::from(pl.nx, pl.ny, pl.nz);
        gtopo::Profile prof;
        gtopo::Body made;
        const int seg = 32;
        if (n == "SPHERE") {
            const double r = nums[0];
            for (int i = 0; i <= seg / 2; ++i) {
                const double a = -kPi * 0.5 + kPi * i / (seg / 2);
                prof.outer.push_back(lat(r * std::cos(a), r * std::sin(a)));
            }
            prof.normalize();
            made = gtopo::revolve(prof, vol_, frame, ggeo::d2::Point<gcm::F64>::from(0, 0), ggeo::d2::Vec<gcm::F64>::from(0, 1), 2.0 * kPi, seg);
        } else if (n == "CONE") {
            const double r = nums[0], h = nums[1];
            prof.outer = {lat(0, 0), lat(r, 0), lat(0, h)};
            prof.normalize();
            made = gtopo::revolve(prof, vol_, frame, ggeo::d2::Point<gcm::F64>::from(0, 0), ggeo::d2::Vec<gcm::F64>::from(0, 1), 2.0 * kPi, seg);
        } else if (n == "TORUS") {
            const double R = nums[0], r = nums[1];
            if (r >= R) { log_.push_back("TORUS: the tube radius must be smaller than the ring radius"); return true; }
            for (int i = 0; i < 24; ++i) {
                const double a = 2.0 * kPi * i / 24;
                prof.outer.push_back(lat(R + r * std::cos(a), r * std::sin(a)));
            }
            prof.normalize();
            made = gtopo::revolve(prof, vol_, frame, ggeo::d2::Point<gcm::F64>::from(0, 0), ggeo::d2::Vec<gcm::F64>::from(0, 1), 2.0 * kPi, seg);
        } else {
            const double l = nums[0], w = nums[1], h = nums[2];
            ggeo::d3::Frame<gcm::F64> side;
            side.origin = frame.origin;
            side.u = frame.u;
            side.v = frame.n;
            side.n = ggeo::d3::Vec<gcm::F64>::from(
                frame.v.x.to_f64() * -1.0, frame.v.y.to_f64() * -1.0, frame.v.z.to_f64() * -1.0);
            prof.outer = {lat(0, 0), lat(l, 0), lat(0, h)};
            prof.normalize();
            made = gtopo::extrude(prof, vol_, side, w);
            (void)w;
        }
        if (made.face_count() == 0) { log_.push_back(n + ": could not build it"); return true; }
        if (gtopo::volume(made, vol_) < 0.0 && n != "WEDGE")
            made = gtopo::revolve(prof, vol_, frame, ggeo::d2::Point<gcm::F64>::from(0, 0),
                                  ggeo::d2::Vec<gcm::F64>::from(0, -1), 2.0 * kPi, seg);
        if (gtopo::volume(made, vol_) < 0.0) { log_.push_back(n + ": could not build it right way out"); return true; }
        modeller_.add_body(std::move(made), (n + " placed at the sketch origin").c_str());
        after_commit();
        return true;
    }
    if (n == "VSCURRENT") {
        const std::string u = uppercase(arg);
        if (u.empty()) { log_.push_back(std::string("VSCURRENT: ") + (wireframe_ ? "wireframe" : "shaded") + " -- WIREFRAME or SHADED"); return true; }
        wireframe_ = u.rfind("W", 0) == 0 || u == "2DWIREFRAME";
        mesh_dirty_ = true;
        log_.push_back(std::string("visual style: ") + (wireframe_ ? "wireframe" : "shaded"));
        return true;
    }
    if (n == "UCS") {
        std::string key = uppercase(arg);
        if (key.rfind("WORLD", 0) == 0 || key.rfind("W", 0) == 0) {
            canvas_.set_ucs(0.0, 0.0, 0.0);
            log_.push_back("UCS: world");
            return true;
        }
        if (key.rfind("OBJECT", 0) == 0 || key.rfind("OB", 0) == 0) {
            if (canvas_.selection().empty()) {
                log_.push_back("UCS OBJECT: select a line first");
                return true;
            }
            const gsnap::Shape& l = canvas_.shapes()[canvas_.selection().front()];
            if (l.kind != gsnap::ShapeKind::Segment) {
                log_.push_back("UCS OBJECT: select a line");
                return true;
            }
            double ax, ay, bx, by;
            world(l.a, ax, ay);
            world(l.b, bx, by);
            canvas_.set_ucs(ax, ay, std::atan2(by - ay, bx - ax));
            char buf[160];
            std::snprintf(buf, sizeof(buf), "UCS along the line: origin %s, %.4g degrees",
                          gunit::format_length(ax, gunit::Format{canvas_.display_unit()}).c_str(),
                          canvas_.ucs_angle() * 180.0 / kPi);
            log_.push_back(buf);
            return true;
        }
        if (have_nums && nums.size() >= 2) {
            const double deg = nums.size() >= 3 ? nums[2] : 0.0;
            canvas_.set_ucs(nums[0], nums[1], deg * kPi / 180.0);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "UCS: origin %.6g, %.6g at %.4g degrees",
                          nums[0], nums[1], deg);
            log_.push_back(buf);
            return true;
        }
        if (!canvas_.ucs_active()) {
            log_.push_back("UCS: world -- UCS <x> <y> [angle], UCS OBJECT, UCS WORLD");
            return true;
        }
        double ox = 0.0, oy = 0.0;
        canvas_.ucs_to_world(0.0, 0.0, ox, oy);
        char buf[176];
        std::snprintf(buf, sizeof(buf), "UCS: origin %.6g, %.6g at %.4g degrees",
                      ox, oy, canvas_.ucs_angle() * 180.0 / kPi);
        log_.push_back(buf);
        return true;
    }

    if (n == "DWGUNITS") {
        const auto u = gunit::length_from_suffix(arg);
        if (arg.empty() || !u) {
            log_.push_back("DWGUNITS <mm|cm|m|in|ft>: the units the imported file was drawn in");
            return true;
        }
        const double f = gunit::metres_per(*u);
        canvas_.rescale_document(f);
        log_.push_back("import rescaled: 1 drawing unit = 1 " + std::string(gunit::name(*u)));
        run_command("ZOOMEXTENTS");
        return true;
    }
    if (n == "GROUPSEL") {
        if (!need_selection("GROUPSEL")) return true;
        int nn = 1;
        while (canvas_.blocks().find("Group-" + std::to_string(nn))) ++nn;
        const std::string name = "Group-" + std::to_string(nn);
        double sx = 0, sy = 0;
        std::size_t cnt = 0;
        for (std::size_t i : selected_indices()) {
            double x, y;
            world(canvas_.shapes()[i].a, x, y);
            sx += x; sy += y; ++cnt;
        }
        if (!cnt) { log_.push_back("GROUPSEL: nothing groupable selected"); return true; }
        double px2, py2;
        canvas_.view().screen_of(sx / double(cnt), sy / double(cnt), px2, py2);
        log_.push_back(canvas_.define_block(name, px2, py2) ? "grouped as " + name
                                                            : "GROUP: " + canvas_.message());
        return true;
    }
    if (n == "UNGROUP") { run_command("EXPLODE"); return true; }
    if (n == "XATTACH" || n == "XREF") {
        std::istringstream input(arg);std::string action;input>>std::quoted(action, '"', '\0');
        std::string key=action;for(auto& c:key)c=char(std::toupper(static_cast<unsigned char>(c)));
        if(n=="XREF"&&(key.empty()||key=="LIST")) {
            if(canvas_.blocks().xrefs().empty())log_.push_back("no references attached -- XATTACH <file> attaches one");
            for(const auto& x:canvas_.blocks().xrefs()) {
                const char* state=x.state==gdoc::XrefState::Loaded?"loaded":x.state==gdoc::XrefState::Unloaded?"unloaded":x.state==gdoc::XrefState::NotFound?"NOT FOUND":"unresolved";
                log_.push_back(x.name+"  "+(x.type==gdoc::XrefType::Overlay?"overlay":"attach")+"  "+state+"  "+x.path);
            }
            return true;
        }
        if(n=="XREF"&&(key=="RELOAD"||key=="UNLOAD"||key=="DETACH"||key=="PATH")) {
            std::string target;input>>std::quoted(target, '"', '\0');
            if(target.empty()&&key=="RELOAD") {
                std::size_t done=0;const auto references=canvas_.blocks().xrefs();
                for(const auto& x:references)if(!x.nested&&x.state!=gdoc::XrefState::Unloaded&&reload_xref(x))++done;
                log_.push_back("reloaded "+std::to_string(done)+" reference(s)");return true;
            }
            const auto* found=std::as_const(canvas_.blocks()).xref(target);
            if(!found){log_.push_back("XREF: no reference called "+target);return true;}
            auto reference=*found;
            if(reference.nested&&(key=="PATH"||key=="DETACH")) {
                log_.push_back("XREF: nested paths and attachments belong to their parent drawing");return true;
            }
            if(key=="UNLOAD"||key=="DETACH") {
                const auto layer="XREF|"+target;
                if(std::any_of(canvas_.shapes().begin(),canvas_.shapes().end(),[&](const auto& shape){return shape.layer==layer;}))
                    canvas_.modify_shapes([&](auto& shapes){std::erase_if(shapes,[&](const auto& shape){return shape.layer==layer;});});
            }
            if(key=="UNLOAD") {
                canvas_.blocks().unload(target);canvas_.clear_selection();
                log_.push_back("unloaded "+target+" (the reference and its cache are kept)");
            } else if(key=="DETACH") {
                canvas_.blocks().detach(target);
                for(auto& [id,state]:space_states_)std::erase_if(state.content.refs,[&](const auto& ref){return ref.name==target;});
                canvas_.clear_selection();log_.push_back("detached "+target);
            } else {
                if(key=="PATH") {
                    std::string path;input>>std::quoted(path, '"', '\0');
                    if(path.empty()){log_.push_back("XREF PATH <name> <file>");return true;}
                    reference.path=path;reference.resolved_path.clear();
                }
                if(reload_xref(reference))log_.push_back((key=="PATH"?"repaired ":"reloaded ")+target);
            }
            invalidate_frame_caches();return true;
        }
        auto type=gdoc::XrefType::Attach;
        std::string path=action;
        if(key=="OVERLAY"){type=gdoc::XrefType::Overlay;input>>std::quoted(path, '"', '\0');}
        else if(!arg.empty()&&arg.front()!='"')path=arg;
        if(path.empty()){ask("XATTACH","file","XATTACH: type the drawing to attach as a reference");return true;}
        const auto name=std::filesystem::path(path).filename().string();
        gdoc::Xref reference;
        if(const auto* existing=std::as_const(canvas_.blocks()).xref(name))reference=*existing;
        reference.name=name;reference.path=path;reference.resolved_path.clear();reference.type=type;
        if(reload_xref(reference))log_.push_back("attached "+name+" -- XREF RELOAD replaces its source contents");
        return true;
    }
    if (n == "GROUP") {
        if (arg.empty()) {
            std::string names;
            for (const auto& [k, v] : groups_) names += (names.empty() ? "" : ", ") + k + " (" + std::to_string(v.size()) + ")";
            log_.push_back(names.empty() ? "GROUP <name>: names the selection; GROUP <name> again selects it" : "groups: " + names);
            return true;
        }
        const auto it = groups_.find(arg);
        if (it != groups_.end() && canvas_.selection().empty()) {
            canvas_.clear_selection();
            std::size_t found = 0;
            for (std::size_t i = 0; i < canvas_.shapes().size(); ++i) {
                const auto& s = canvas_.shapes()[i];
                for (const auto& g : it->second)
                    if (g.kind == s.kind && g.a.x == s.a.x && g.a.y == s.a.y && g.b.x == s.b.x && g.b.y == s.b.y && g.radius == s.radius) {
                        double x, y, px, py;
                        world(s.a, x, y);
                        canvas_.view().screen_of(x, y, px, py);
                        canvas_.select_at(px, py, true);
                        ++found;
                        break;
                    }
            }
            log_.push_back("group " + arg + ": " + std::to_string(found) + " selected");
            return true;
        }
        if (!need_selection("GROUP")) return true;
        std::vector<gsnap::Shape> members;
        for (std::size_t i : selected_indices()) members.push_back(canvas_.shapes()[i]);
        groups_[arg] = std::move(members);
        log_.push_back("group " + arg + ": " + std::to_string(groups_[arg].size()) + " object(s)");
        return true;
    }
    if (n == "LAYOUT") {
        // Flush the live sheet before changing its index.
        struct RestorePaper {
            Shell& shell;
            bool active;
            ~RestorePaper() { if (active && shell.active_layout()) shell.enter_paper_space(); }
        } restore{*this, in_paper_space_ && !arg.empty()};
        if (restore.active) leave_paper_space();
        std::string what = arg, rest;
        if (const std::size_t sp = arg.find(' '); sp != std::string::npos) {
            what = arg.substr(0, sp);
            rest = arg.substr(sp + 1);
        }
        for (char& c : what) c = char(std::toupper(static_cast<unsigned char>(c)));

        if (arg.empty()) {
            if (layouts_.empty()) {
                log_.push_back("no layouts yet -- LAYOUT NEW <name> makes one");
                return true;
            }
            std::string names;
            for (std::size_t i = 0; i < layouts_.size(); ++i)
                names += (names.empty() ? "" : ", ") + layouts_[i].name +
                         (int(i) == active_layout_ ? " (current)" : "");
            log_.push_back("layouts: " + names);
            return true;
        }
        if (what == "NEW") {
            layout_store();
            Layout l;
            l.name = rest.empty() ? "Layout" + std::to_string(layouts_.size() + 1) : rest;
            if(rest.empty()) for(std::size_t number=1;std::any_of(layouts_.begin(),layouts_.end(),[&](const auto& e){return e.name==l.name;});++number) l.name="Layout"+std::to_string(number);
            if(uppercase(l.name)=="MODEL") { log_.push_back("LAYOUT: Model is reserved for model space"); return true; }
            for (const auto& e : layouts_)
                if (e.name == l.name) { log_.push_back("LAYOUT: " + l.name + " already exists"); return true; }
            l.paper = plot_paper_;
            if(const auto* current=active_layout()) { l.imported_paper=current->imported_paper; l.custom_paper=current->custom_paper; }
            l.landscape = plot_landscape_;
            l.scale_den = plot_scale_den_;
            layouts_.push_back(std::move(l));
            layout_load(int(layouts_.size()) - 1);
            if (mode_ != Mode::Drawing) enter_drawing_mode();
            log_.push_back("layout " + layouts_.back().name + " on " + plot_paper_);
            return true;
        }
        if (what == "DELETE" || what == "RENAME") {
            const std::string target = what == "DELETE" ? rest
                                     : (rest.find(' ') == std::string::npos ? rest
                                        : rest.substr(0, rest.find(' ')));
            int at = -1;
            for (std::size_t i = 0; i < layouts_.size(); ++i)
                if (layouts_[i].name == target) at = int(i);
            if (at < 0) { log_.push_back("LAYOUT: no layout called " + target); return true; }
            if (what == "DELETE") {
                layouts_.erase(layouts_.begin() + at);
                if (layouts_.empty()) active_layout_ = -1;
                else layout_load(std::min(at, int(layouts_.size()) - 1));
                log_.push_back("deleted layout " + target);
            } else {
                const std::size_t sp = rest.find(' ');
                if (sp == std::string::npos) { log_.push_back("LAYOUT RENAME <old> <new>"); return true; }
                const auto name=rest.substr(sp+1);
                if(name.empty() || uppercase(name)=="MODEL" || std::any_of(layouts_.begin(),layouts_.end(),[&](const auto& l){return &l!=&layouts_[std::size_t(at)] && l.name==name;})) {
                    log_.push_back("LAYOUT: choose a unique sheet name other than Model"); return true;
                }
                layouts_[std::size_t(at)].name = name;
                log_.push_back("renamed to " + layouts_[std::size_t(at)].name);
            }
            return true;
        }
        for (std::size_t i = 0; i < layouts_.size(); ++i)
            if (layouts_[i].name == arg) {
                layout_store();
                layout_load(int(i));
                if (mode_ != Mode::Drawing) enter_drawing_mode();
                log_.push_back("layout " + arg);
                return true;
            }
        log_.push_back("LAYOUT: no layout called " + arg + " -- LAYOUT lists them");
        return true;
    }
    if (n == "MVIEW" || n == "VPORTS") {
        Layout* l = active_layout();
        if (!l) { log_.push_back(n + ": make a layout first (LAYOUT NEW)"); return true; }
        const auto help = [&] {
            log_.push_back("MVIEW <scale> [x0 y0 x1 y1] | LIST | TILE | CLEAR | "
                           "LOCK <#> ON/OFF | SCALE <#> <denominator> | CENTER <#> <x> <y> | "
                           "ROTATE <#> <degrees> | CLIP <#> <x0> <y0> <x1> <y1> | "
                           "POLYGON <#> <x0> <y0> <x1> <y1> <x2> <y2> ... | FREEZE/THAW <#> <layer> | ON/OFF/DELETE <#>; paper coordinates are mm");
        };
        std::istringstream input(arg);
        std::string action; input >> action;
        for (char& c : action) c = char(std::toupper(static_cast<unsigned char>(c)));
        const auto ended = [&] { input >> std::ws; return input.eof(); };
        const auto number = [&](double& v) { return bool(input >> v) && std::isfinite(v); };
        const auto paper_size=layout_paper(active_layout());
        const gplot::PaperSize* paper=&paper_size;
        if (!paper) { log_.push_back(n + ": unknown paper " + plot_paper_); return true; }
        const gplot::PaperSize ps = plot_landscape_ ? *paper : gplot::rotated(*paper);
        const auto tiled = [&](std::size_t count, std::size_t i) {
            const std::size_t cols = count <= 1 ? 1 : (count <= 4 ? 2 : 3);
            const std::size_t rows = (count + cols - 1) / cols;
            const double gap = 0.005;
            const double cw = (ps.printable_width() - gap * double(cols - 1)) / double(cols);
            const double ch = (ps.printable_height() - gap * double(rows - 1)) / double(rows);
            gplot::Viewport v;
            v.lo = {ps.margin_left + double(i % cols) * (cw + gap),
                    ps.margin_bottom + ps.printable_height() - double(i / cols + 1) * ch - double(i / cols) * gap};
            v.hi = {v.lo.x + cw, v.lo.y + ch};
            return v;
        };
        if (action == "HELP" || action == "?") { help(); return true; }
        if (action == "CLEAR" || (action == "DELETE" && ended())) {
            if (!ended()) { help(); return true; }
            l->viewports.clear();
            log_.push_back("viewports cleared -- the sheet plots the whole drawing again");
            return true;
        }
        if (action == "TILE") {
            if (!ended()) { help(); return true; }
            for (std::size_t i=0; i<l->viewports.size(); ++i) {
                const auto box=tiled(l->viewports.size(),i);
                l->viewports[i].lo=box.lo; l->viewports[i].hi=box.hi; l->viewports[i].clip_polygon.clear();
            }
            log_.push_back("viewports tiled"); return true;
        }
        if (action == "LIST" || (arg.empty() && !l->viewports.empty())) {
            if (!arg.empty() && !ended()) { help(); return true; }
            log_.push_back("viewports on " + l->name + " (paper coordinates in mm):");
            for (std::size_t i=0; i<l->viewports.size(); ++i) {
                const auto& v=l->viewports[i];
                std::ostringstream row;
                row << "#" << i+1 << " 1:" << v.scale << (v.locked ? " locked" : " unlocked")
                    << (v.on ? " on" : " off") << " clip " << v.lo.x*1000 << "," << v.lo.y*1000
                    << " to " << v.hi.x*1000 << "," << v.hi.y*1000 << "; frozen:";
                for (const auto& layer : v.frozen_layers) row << " [" << layer << "]";
                log_.push_back(row.str());
            }
            help(); return true;
        }
        if (action == "LOCK" || action == "SCALE" || action == "CENTER" || action == "ROTATE" ||
            action == "CLIP" || action == "POLYGON" || action == "FREEZE" || action == "THAW" || action == "ON" ||
            action == "OFF" || action == "DELETE") {
            int index=0;
            if (!(input >> index) || index<1 || std::size_t(index)>l->viewports.size()) {
                log_.push_back("MVIEW: invalid viewport number -- MVIEW LIST lists them"); return true;
            }
            auto v=l->viewports[std::size_t(index-1)];
            if (action == "LOCK") {
                std::string value; input >> value;
                for (char& c:value) c=char(std::toupper(static_cast<unsigned char>(c)));
                if ((value!="ON" && value!="OFF") || !ended()) { help(); return true; }
                v.locked=value=="ON";
            } else if (action == "SCALE" || action == "CENTER" || action == "ROTATE") {
                double x=0,y=0;
                if (!number(x) || (action=="CENTER" && !number(y)) || !ended() ||
                    (action=="SCALE" && x<=0)) { help(); return true; }
                if (v.locked) { log_.push_back("MVIEW: viewport is locked -- unlock it before changing the view"); return true; }
                if (action=="SCALE") v.scale=x;
                else if (action=="ROTATE") v.rotation=std::remainder(x,360.0)*kPi/180.0;
                else { v.centre_x=gunit::to_metres(x,canvas_.display_unit()); v.centre_y=gunit::to_metres(y,canvas_.display_unit()); }
            } else if (action == "POLYGON") {
                std::vector<gplot::PaperPoint> points;
                double x,y;
                while(!ended()) { if(!number(x)||!number(y)||points.size()>=256) { help(); return true; } points.push_back({x*.001,y*.001}); }
                if(points.size()<3) { help(); return true; }
                auto lo=points[0],hi=points[0];
                for(auto p:points) { lo.x=std::min(lo.x,p.x); lo.y=std::min(lo.y,p.y); hi.x=std::max(hi.x,p.x); hi.y=std::max(hi.y,p.y); }
                if(!gplot::PaperClip{lo,hi,points}.valid()) { log_.push_back("MVIEW: clip polygon must be simple, with at least three distinct corners"); return true; }
                v.to_model((lo.x+hi.x)/2,(lo.y+hi.y)/2,v.centre_x,v.centre_y);
                v.lo=lo; v.hi=hi; v.clip_polygon=std::move(points);
            } else if (action == "CLIP") {
                v.clip_polygon.clear();
                double x0,y0,x1,y1;
                if (!number(x0)||!number(y0)||!number(x1)||!number(y1)||!ended()||x0>=x1||y0>=y1) { help(); return true; }
                v.to_model(x0*0.0005+x1*0.0005,y0*0.0005+y1*0.0005,v.centre_x,v.centre_y);
                v.lo={x0*0.001,y0*0.001}; v.hi={x1*0.001,y1*0.001};
            } else if (action == "FREEZE" || action == "THAW") {
                std::string layer; std::getline(input >> std::ws,layer);
                while (!layer.empty() && std::isspace(static_cast<unsigned char>(layer.back()))) layer.pop_back();
                if (!layers_.find(layer)) { log_.push_back("MVIEW: unknown layer " + layer); return true; }
                auto it=std::find(v.frozen_layers.begin(),v.frozen_layers.end(),layer);
                if (action=="FREEZE" && it==v.frozen_layers.end()) v.frozen_layers.push_back(layer);
                else if (action=="THAW" && it!=v.frozen_layers.end()) v.frozen_layers.erase(it);
            } else {
                if (!ended()) { help(); return true; }
                if (action=="DELETE") {
                    l->viewports.erase(l->viewports.begin()+index-1);
                    log_.push_back("viewport " + std::to_string(index) + " deleted"); return true;
                }
                v.on=action=="ON";
            }
            if (!std::isfinite(v.centre_x) || !std::isfinite(v.centre_y)) { help(); return true; }
            l->viewports[std::size_t(index-1)]=std::move(v);
            log_.push_back("viewport " + std::to_string(index) + ": " + action);
            return true;
        }
        input.clear(); input.str(arg);
        double den=0;
        if (!arg.empty() && (!number(den) || den<=0)) { help(); return true; }
        bool explicit_box=false;
        gplot::PaperPoint requested_lo, requested_hi;
        if (!arg.empty() && !ended()) {
            double x0,y0,x1,y1;
            if (!number(x0)||!number(y0)||!number(x1)||!number(y1)||!ended()||x0>=x1||y0>=y1) { help(); return true; }
            requested_lo={x0*0.001,y0*0.001}; requested_hi={x1*0.001,y1*0.001}; explicit_box=true;
        }
        if (!explicit_box) {
            for (std::size_t i=0; i<l->viewports.size(); ++i) {
                const auto box=tiled(l->viewports.size(),i); const auto& v=l->viewports[i];
                if (v.locked || std::fabs(v.lo.x-box.lo.x)>1e-9 || std::fabs(v.lo.y-box.lo.y)>1e-9 ||
                    std::fabs(v.hi.x-box.hi.x)>1e-9 || std::fabs(v.hi.y-box.hi.y)>1e-9) {
                    log_.push_back("MVIEW: existing windows have custom bounds or locks; use MVIEW <scale> <x0> <y0> <x1> <y1> (mm)");
                    return true;
                }
            }
        }

        gplot::Viewport v;
        v.source_space = static_cast<gcm::u32>(active_sketch_);
        v.scale = den > 0.0 ? den : (plot_scale_den_ > 0 ? double(plot_scale_den_) : 1.0);
        const PlotContent& pc = plot_content();
        if (pc.have_box) {
            v.centre_x = (pc.lo_x + pc.hi_x) * 0.5;
            v.centre_y = (pc.lo_y + pc.hi_y) * 0.5;
        }
        l->viewports.push_back(v);

        const std::size_t n_vp = l->viewports.size();
        if (explicit_box) {
            l->viewports.back().lo=requested_lo; l->viewports.back().hi=requested_hi;
        } else for (std::size_t i=0; i<n_vp; ++i) {
            const auto box=tiled(n_vp,i);
            l->viewports[i].lo=box.lo; l->viewports[i].hi=box.hi; l->viewports[i].clip_polygon.clear();
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "viewport %zu on %s at 1:%.6g",
                      n_vp, l->name.c_str(), v.scale);
        log_.push_back(buf);
        return true;
    }
    if (n == "SECTIONPLANE" || n == "SECTIONOFF") {
        if (n == "SECTIONOFF" || uppercase(arg) == "OFF") {
            set_section_plane(0.0, 0.0, 0.0, 0.0);
            log_.push_back("section off");
            return true;
        }
        std::istringstream in(arg);
        std::string what;
        in >> what;
        std::string key = what;
        for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
        double at = 0.0;
        if (!(in >> at)) { try { at = std::stod(what); key = "Z"; } catch (...) {} }

        double nx = 0.0, ny = 0.0, nz = 1.0;
        if (key == "X")      { nx = 1.0; ny = 0.0; nz = 0.0; }
        else if (key == "Y") { nx = 0.0; ny = 1.0; nz = 0.0; }
        else if (key == "Z" || key.empty()) { nx = 0.0; ny = 0.0; nz = 1.0; }
        else if (key == "FACE") {
            if (!sel_target_.valid || !sel_target_.on_body) {
                log_.push_back("SECTIONPLANE FACE: pick a face first");
                return true;
            }
            double ox, oy, oz;
            if (!modeller_.plane_of(sel_target_, ox, oy, oz, nx, ny, nz)) {
                log_.push_back("SECTIONPLANE FACE: that is not a planar face");
                return true;
            }
            at = nx * ox + ny * oy + nz * oz + at;
            set_section_plane(nx, ny, nz, at);
            log_.push_back("section on the picked face");
            return true;
        } else {
            log_.push_back("SECTIONPLANE X|Y|Z <at> | FACE [offset] | OFF");
            return true;
        }
        set_section_plane(nx, ny, nz, at);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "section: %s = %s", key.empty() ? "Z" : key.c_str(),
                      gunit::format_length(at, gunit::Format{canvas_.display_unit()}).c_str());
        log_.push_back(buf);
        return true;
    }
    if (n == "PRINT") {
        if(uppercase(arg)=="SET" || uppercase(arg).starts_with("SET ")) {
            if(open_plot_preview(true)) print_plot(arg.size()>3?arg.substr(4):std::string{});
        } else print_plot(arg);
        return true;
    }
    if (n == "PREVIEW") {
        if (uppercase(arg) == "CLOSE") close_plot_preview();
        else if(uppercase(arg)=="SET") open_plot_preview(true);
        else if(uppercase(arg)=="NEXT") set_plot_preview_page(plot_preview_page()+1);
        else if(uppercase(arg)=="PREVIOUS") set_plot_preview_page(plot_preview_page()>1?plot_preview_page()-1:0);
        else if(uppercase(arg).starts_with("PAGE ")) {
            std::istringstream input(arg.substr(5));std::size_t page=0;
            if(input>>page) {input>>std::ws;if(input.eof())set_plot_preview_page(page);else log_.push_back("PREVIEW PAGE: enter one page number");}
            else log_.push_back("PREVIEW PAGE: enter one page number");
        }
        else if (uppercase(arg).rfind("SAVE ", 0) == 0) {
            std::istringstream input(arg.substr(5)); std::string path;
            input >> std::quoted(path);
            log_.push_back(save_plot_preview(path) ? "saved preview PDF: " + path : "could not save preview PDF");
        } else if(arg.empty()) open_plot_preview();
        else log_.push_back("PREVIEW [SET | PAGE n | NEXT | PREVIOUS | SAVE path | CLOSE]");
        return true;
    }
    if (n == "SHEETSET") return sheet_set_command(arg);
    if (n == "PUBLISH") {
        if (arg.rfind("SET ", 0) == 0 || arg.rfind("set ", 0) == 0) {
            std::istringstream input(arg.substr(4));
            std::string path;
            input >> std::quoted(path);
            publish_sheet_set(path);
            return true;
        }
        if (layouts_.empty()) {
            log_.push_back("PUBLISH: no sheets -- LAYOUT NEW makes one");
            return true;
        }
        std::string stem = arg;
        if (stem.empty())
            stem = document_path_.empty() ? std::string("sheets") : document_path_;
        if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos &&
            dot > stem.find_last_of("/\\") + 1)
            stem = stem.substr(0, dot);

        const int was = active_layout_;
        std::size_t done = 0, failed = 0;
        std::string wrote;
        for (int i = 0; i < int(layouts_.size()); ++i) {
            layout_store();
            layout_load(i);
            const std::string path = stem + "-" + layouts_[std::size_t(i)].name + ".pdf";
            if (export_file(path)) {
                ++done;
                wrote += (wrote.empty() ? "" : ", ") + layouts_[std::size_t(i)].name;
            } else {
                ++failed;
            }
        }
        layout_store();
        if (was >= 0) layout_load(was);
        log_.push_back("published " + std::to_string(done) + " sheet(s)" +
                       (failed ? ", " + std::to_string(failed) + " failed" : "") +
                       (wrote.empty() ? "" : ": " + wrote));
        return true;
    }
    if(n=="PDFIMPORT") {
        if(arg.empty()){if(file_hook_)ask_file(FileAsk::Import);else log_.push_back("PDFIMPORT [VECTOR|IMAGE] [PAGE <number>] <file.pdf>");return true;}
        std::istringstream args(arg);std::string mode,path;int page=1;args>>mode;
        bool raster=mode=="IMAGE"||mode=="image";
        if(raster||mode=="VECTOR"||mode=="vector")args>>mode;
        if(mode=="PAGE"||mode=="page"){args>>page>>std::ws;std::getline(args,path);}
        else {std::string rest;std::getline(args,rest);path=mode+rest;}
        if(path.size()>1&&path.front()=='"'&&path.back()=='"')path=path.substr(1,path.size()-2);
        if(raster)attach_underlay(path,page,false);else import_pdf(path,page);
        return true;
    }
    if (n == "UNDERLAY") {
        std::istringstream in(arg);
        std::string what, rest;
        in >> what;
        std::getline(in, rest);
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
        std::string key = what;
        for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));

        if(key=="REMOVE")key="DETACH";
        if (arg.empty() || key == "LIST") {
            if (canvas_.underlays().size() == 0) {
                log_.push_back("no underlays -- UNDERLAY <file> attaches one");
                return true;
            }
            for (const auto& u : canvas_.underlays().all()) {
                char buf[224];
                std::snprintf(buf, sizeof(buf), "%s  fade %d  scale %.6g  %s  %s",
                              u.name.c_str(), u.fade, u.scale,
                              u.visible ? "shown" : "hidden", u.path.c_str());
                log_.push_back(buf);
            }
            return true;
        }
        if (key=="FADE"||key=="SCALE"||key=="HIDE"||key=="SHOW"||key=="DETACH"||key=="MOVE"||key=="ROTATE"||key=="CONTRAST"||key=="MONO"||key=="PLOT"||key=="CLIP"||key=="CLEARCLIP"||key=="RELOAD"||key=="UNIT") {
            std::istringstream args(rest);std::string name;args>>std::quoted(name);
            auto* u=canvas_.underlays().find(name);if(!u){log_.push_back("UNDERLAY: no underlay called "+name);return true;}
            auto invalid=[&](){log_.push_back("UNDERLAY: invalid "+key+" value");return true;};
            if(key=="HIDE"||key=="SHOW")u->visible=key=="SHOW";
            else if(key=="DETACH")canvas_.underlays().detach(name);
            else if(key=="CLEARCLIP"){u->clip.clear();u->clip_hole.clear();u->clip_inverted=false;u->clip_enabled=true;}
            else if(key=="RELOAD"||key=="UNIT") {
                double unit=0;
                if(key=="UNIT"&&(!(args>>unit)||!std::isfinite(unit)||unit<=0))return invalid();
                std::string error;std::vector<std::string> warnings;
                if(!gplot::resolve_underlay(*u,vol_,error,unit,&warnings)){log_.push_back("UNDERLAY: "+error);return true;}
                image_cache_.clear();
                for(const auto& warning:warnings)log_.push_back("UNDERLAY: "+warning);
            }
            else if(key=="MOVE") {double x,y;if(!(args>>x>>y)||!std::isfinite(x)||!std::isfinite(y))return invalid();u->insertion=gcm::to_lattice(vol_,gunit::to_metres(x,canvas_.display_unit()),gunit::to_metres(y,canvas_.display_unit()));}
            else if(key=="CLIP") {
                std::vector<gdoc::Point2> points;double x,y;
                while(args>>x){if(!(args>>y)||!std::isfinite(x)||!std::isfinite(y))return invalid();points.push_back(gcm::to_lattice(vol_,gunit::to_metres(x,canvas_.display_unit()),gunit::to_metres(y,canvas_.display_unit())));}
                if(points.size()<3||!gclip::valid_boundary(points))return invalid();u->clip=std::move(points);u->clip_hole.clear();u->clip_inverted=false;u->clip_enabled=true;
            } else {
                double value;if(!(args>>value)||!std::isfinite(value))return invalid();
                if(key=="SCALE"){if(value<=0)return invalid();u->scale=value;}
                else if(key=="ROTATE")u->rotation=value*3.141592653589793/180;
                else if(key=="FADE"||key=="CONTRAST"){if(value<0||value>100||value!=std::floor(value))return invalid();if(key=="FADE")u->fade=int(value);else u->contrast=int(value);}
                else {if(value!=0&&value!=1)return invalid();if(key=="MONO")u->monochrome=value!=0;else u->plottable=value!=0;}
            }
            log_.push_back(name+": "+key+" updated");invalidate_frame_caches();return true;
        }

        std::string path=arg;int page=1;
        if(key=="PAGE") {std::istringstream args(rest);args>>page>>std::ws;std::getline(args,path);}
        if(path.size()>1&&path.front()=='"'&&path.back()=='"')path=path.substr(1,path.size()-2);
        attach_underlay(path,page);
        return true;
    }
    if (n == "TEMPLATE") {
        std::istringstream in(arg);
        std::string what, path;
        in >> what;
        std::getline(in, path);
        while (!path.empty() && path.front() == ' ') path.erase(0, 1);
        std::string key = what;
        for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));

        if (key == "SAVE") {
            if (path.empty()) { log_.push_back("TEMPLATE SAVE <file.glcad>"); return true; }
            namespace fs = std::filesystem;
            const auto same_file = [&](const std::string& other) {
                if (other.empty()) return false;
                std::error_code ec;
                if (fs::equivalent(path, other, ec)) return true;
                return fs::path(path).lexically_normal() == fs::path(other).lexically_normal();
            };
            bool open_drawing = same_file(document_path_);
            for (const auto& tab : tabs_) open_drawing |= same_file(tab.path);
            if (open_drawing) {
                log_.push_back("TEMPLATE SAVE: choose a different file from an open drawing");
                return true;
            }
            std::string holder;
            auto lease=DocumentLock::acquire(path,holder);
            if (!lease) {
                log_.push_back("TEMPLATE SAVE: the destination is locked by another session");
                return true;
            }
            auto c = document_contents();
            if (!prepare_template(c)) return true;
            const gfile::Error e = gfile::save_file(path, c);
            log_.push_back(e.ok ? "template saved: " + path
                                : "TEMPLATE: " + e.message);
            return true;
        }
        if (key == "OPEN" || key == "NEW") {
            if (path.empty()) { log_.push_back("TEMPLATE NEW <file.glcad>"); return true; }
            gfile::Contents c;
            const gfile::Error e = gfile::load_file(path, c);
            if (!e.ok) { log_.push_back("TEMPLATE: " + e.message); return true; }
            if (!prepare_template(c) || !load_contents(c, {})) return true;
            saved_serial_ = ~gcm::u64{0};
            log_.push_back("template " + path + ": " +
                           std::to_string(layers_.all().size()) + " layer(s)");
            return true;
        }
        log_.push_back("TEMPLATE SAVE <file> | TEMPLATE NEW <file>");
        return true;
    }
    if (n == "MATELIST" || n == "MATEUPDATE" || n == "MATEDELETE") {
        const auto& mates = modeller_.mates();
        if (n == "MATELIST") {
            if (mates.empty()) {
                log_.push_back("no mates -- MATE asserts one between two picked faces");
                return true;
            }
            for (std::size_t i = 0; i < mates.size(); ++i) {
                const auto& m = mates[i];
                const std::size_t a = modeller_.body_of_name(m.body_a);
                const std::size_t b = modeller_.body_of_name(m.body_b);
                char buf[192];
                std::snprintf(buf, sizeof(buf), "%zu %s: solid %s to solid %s%s", i,
                              gtopo::mate_kind_name(m.kind),
                              a == std::size_t(-1) ? "?" : std::to_string(a + 1).c_str(),
                              b == std::size_t(-1) ? "?" : std::to_string(b + 1).c_str(),
                              m.suppressed ? " (suppressed)" : "");
                log_.push_back(buf);
            }
            return true;
        }
        if (n == "MATEDELETE") {
            if (!have_nums) { log_.push_back("MATEDELETE <index> -- MATELIST numbers them"); return true; }
            std::vector<Modeller::StoredMate> kept;
            const std::size_t drop = std::size_t(nums[0]);
            for (std::size_t i = 0; i < mates.size(); ++i)
                if (i != drop) kept.push_back(mates[i]);
            const bool removed = kept.size() != mates.size();
            modeller_.load_mates(std::move(kept));
            log_.push_back(removed ? "mate removed" : "MATEDELETE: no mate " +
                                                          std::to_string(drop));
            return true;
        }
        const std::size_t gone = modeller_.prune_mates();
        const std::size_t held = modeller_.resolve_mates();
        mesh_dirty_ = true;
        if (gone)
            log_.push_back(std::to_string(gone) + " mate(s) dropped: their parts or "
                           "faces are gone");
        log_.push_back(held ? modeller_.message()
                            : "no mates held -- MATELIST says what there is");
        return true;
    }
    if (n == "HOLE") {
        if (!sel_target_.valid || !sel_target_.on_body ||
            sel_target_.body >= gcm::i32(modeller_.bodies().size())) {
            log_.push_back("HOLE: pick a face first");
            return true;
        }
        if (!have_nums || nums[0] <= 0.0) {
            ask("HOLE", "diameter [depth]", "HOLE: type the diameter, e.g. 0.012");
            return true;
        }
        const double radius = nums[0] * 0.5;
        const std::size_t body = std::size_t(sel_target_.body);
        const gtopo::Body& b = modeller_.bodies()[body];

        const gview::Vec3 nrm = sel_target_.normal;
        const double nx = -nrm.x.to_f64(), ny = -nrm.y.to_f64(), nz = -nrm.z.to_f64();
        const gview::Point3 c = sel_target_.centroid;

        double depth = nums.size() >= 2 && nums[1] > 0.0 ? nums[1] : 0.0;
        if (depth <= 0.0) {
            double lo = 1e300, hi = -1e300;
            for (const auto& p : b.vertex_point) {
                double x, y, z;
                gcm::to_world(vol_, p, x, y, z);
                const double t = x * nx + y * ny + z * nz;
                lo = std::min(lo, t);
                hi = std::max(hi, t);
            }
            const double start = c.x.to_f64() * nx + c.y.to_f64() * ny + c.z.to_f64() * nz;
            depth = (hi - start) + std::max(radius, 1e-3);
        }

        ggeo::d3::Frame<gcm::F64> frame;
        frame.origin = gview::Point3::from(c.x.to_f64() - nx * std::max(radius, 1e-3),
                                           c.y.to_f64() - ny * std::max(radius, 1e-3),
                                           c.z.to_f64() - nz * std::max(radius, 1e-3));
        gview::Vec3 seed = gview::Vec3::from(1, 0, 0);
        if (std::fabs(nx) > 0.9) seed = gview::Vec3::from(0, 1, 0);
        const double ux = seed.y.to_f64() * nz - seed.z.to_f64() * ny;
        const double uy = seed.z.to_f64() * nx - seed.x.to_f64() * nz;
        const double uz = seed.x.to_f64() * ny - seed.y.to_f64() * nx;
        const double ul = std::sqrt(ux * ux + uy * uy + uz * uz);
        if (ul < 1e-12) { log_.push_back("HOLE: that face has no usable normal"); return true; }
        frame.u = gview::Vec3::from(ux / ul, uy / ul, uz / ul);
        frame.n = gview::Vec3::from(nx, ny, nz);
        // v = n x u keeps the frame right-handed; the other sign builds inside out.
        frame.v = gview::Vec3::from(
            ny * frame.u.z.to_f64() - nz * frame.u.y.to_f64(),
            nz * frame.u.x.to_f64() - nx * frame.u.z.to_f64(),
            nx * frame.u.y.to_f64() - ny * frame.u.x.to_f64());

        gtopo::Profile p;
        for (int i = 0; i < 48; ++i) {
            const double a = 2.0 * kPi * i / 48.0;
            p.outer.push_back(lat(radius * std::cos(a), radius * std::sin(a)));
        }
        p.normalize();
        gtopo::Body drill = gtopo::extrude(p, vol_, frame,
                                           depth + std::max(radius, 1e-3));
        if (drill.face_count() == 0) { log_.push_back("HOLE: could not form the drill"); return true; }
        {
            const gtopo::Surface wall = gtopo::Surface::cylinder(
                frame.origin.x.to_f64(), frame.origin.y.to_f64(), frame.origin.z.to_f64(),
                nx, ny, nz, radius);
            if (drill.face_surface.size() < drill.face_count())
                drill.face_surface.resize(drill.face_count());
            for (gcm::u32 f = 0; f < drill.face_count(); ++f) {
                const gtopo::Plane fp = gtopo::face_plane(drill, f, vol_);
                if (std::fabs(fp.nx * nx + fp.ny * ny + fp.nz * nz) < 0.5)
                    drill.face_surface[f] = wall;
            }
        }
        const gtopo::CurvedBoolResult cut =
            gtopo::boolean_curved(b, drill, gtopo::BoolOp::Subtract, vol_);
        gtopo::Body made;
        if (cut.ok() && cut.body.face_count() > 0) made = cut.body;
        else {
            const gtopo::BoolResult r = gtopo::subtract(b, drill, vol_);
            if (!r.ok()) { log_.push_back("HOLE: " + std::string(r.message())); return true; }
            made = r.body;
        }
        modeller_.replace_body(body, gtopo::merge_coplanar_faces(made, vol_));
        mesh_dirty_ = true;
        char buf[176];
        std::snprintf(buf, sizeof(buf), "hole %s diameter, %s",
                      gunit::format_length(nums[0], gunit::Format{canvas_.display_unit()}).c_str(),
                      nums.size() >= 2 && nums[1] > 0.0
                          ? gunit::format_length(nums[1], gunit::Format{canvas_.display_unit()}).c_str()
                          : "through");
        log_.push_back(buf);
        return true;
    }
    if (n == "HEAL") {
        if (modeller_.bodies().empty()) { log_.push_back("HEAL: no solid"); return true; }
        const std::size_t body =
            (sel_target_.valid && sel_target_.on_body &&
             sel_target_.body < gcm::i32(modeller_.bodies().size()))
                ? std::size_t(sel_target_.body) : 0;
        const gtopo::Body& before = modeller_.bodies()[body];
        const gtopo::Topology t0 = before.analyse();
        const double v0 = gtopo::volume(before, vol_);

        gtopo::Body healed = gtopo::merge_coplanar_faces(before, vol_);
        const gtopo::Topology t1 = healed.analyse();
        const double v1 = gtopo::volume(healed, vol_);

        const bool sound = (t1.closed || !t0.closed) && (t1.manifold || !t0.manifold) &&
                           std::fabs(v1 - v0) < std::max(1e-9, std::fabs(v0) * 1e-9);
        char buf[256];
        if (!sound) {
            std::snprintf(buf, sizeof(buf),
                          "HEAL: declined -- the repair changed the solid "
                          "(volume %.9g -> %.9g, closed %d -> %d)",
                          v0, v1, int(t0.closed), int(t1.closed));
            log_.push_back(buf);
            return true;
        }
        const gcm::u32 removed = t0.faces > t1.faces ? t0.faces - t1.faces : 0;
        modeller_.replace_body(body, std::move(healed));
        mesh_dirty_ = true;
        std::snprintf(buf, sizeof(buf),
                      "healed: %u face(s) merged, now V%u E%u F%u, %s%s",
                      removed, t1.vertices, t1.edges, t1.faces,
                      t1.closed ? "closed" : "NOT CLOSED",
                      t1.manifold ? " and manifold" : " and NOT MANIFOLD");
        log_.push_back(buf);
        if (!t1.closed || !t1.manifold)
            log_.push_back("what remains needs stitching or gap filling, which "
                           "this build cannot do -- the boolean will refuse it");
        return true;
    }
    if (n == "PROJECT" || n == "PROJECTGEOM") {
        if (modeller_.bodies().empty()) { log_.push_back("PROJECT: no solid"); return true; }
        const std::size_t body =
            (sel_target_.valid && sel_target_.on_body &&
             sel_target_.body < gcm::i32(modeller_.bodies().size()))
                ? std::size_t(sel_target_.body) : 0;
        const gtopo::Body& b = modeller_.bodies()[body];
        const gtopo::SketchPlane& pl = sketch_plane();

        const bool one_face = sel_target_.valid && sel_target_.on_body &&
                              !command_arg_.empty() && uppercase(command_arg_) == "FACE";
        std::size_t made = 0;
        canvas_.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            const std::size_t first = sh.size();
            for (gcm::u32 f = 0; f < b.face_count(); ++f) {
                if (one_face && f != sel_target_.face) continue;
                for (gcm::u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li) {
                    const gcm::u32 lb = b.loop_begin(li), le = b.loop_end(li);
                    for (gcm::u32 h = lb; h < le; ++h) {
                        const gcm::u32 va = b.he_vertex[h];
                        const gcm::u32 vb = b.he_vertex[h + 1 < le ? h + 1 : lb];
                        if (va > vb) continue;
                        double ax, ay, az, bx, by, bz;
                        gcm::to_world(vol_, b.vertex_point[va], ax, ay, az);
                        gcm::to_world(vol_, b.vertex_point[vb], bx, by, bz);
                        double pu0, pv0, pu1, pv1;
                        pl.to_plane(ax, ay, az, pu0, pv0);
                        pl.to_plane(bx, by, bz, pu1, pv1);
                        if (std::hypot(pu1 - pu0, pv1 - pv0) < 1e-9) continue;
                        gsnap::Shape seg;
                        seg.kind = gsnap::ShapeKind::Segment;
                        seg.a = lat(pu0, pv0);
                        seg.b = lat(pu1, pv1);
                        seg.layer = canvas_.current_layer();
                        sh.push_back(std::move(seg));
                        ++made;
                    }
                }
            }
            (void)first;
        });
        if (made) run_command("OVERKILL");
        log_.push_back("projected " + std::to_string(made) + " edge(s) onto the sketch");
        return true;
    }
    if (n == "DATUM") {
        std::istringstream in(arg);
        std::string what;
        in >> what;
        std::string key = what;
        for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
        double offset = 0.0;
        if (!(in >> offset)) {
            try { offset = std::stod(what); key.clear(); } catch (...) {}
        }

        gtopo::SketchPlane pl;
        std::string named;
        if (key == "XY" || key.empty()) {
            pl = active_sketch_ >= 0 && key.empty() && !sketches_.empty()
                     ? sketches_[std::size_t(active_sketch_)].plane
                     : gtopo::SketchPlane::world_xy();
            named = key.empty() ? "the current sketch" : "XY";
        } else if (key == "XZ") {
            pl = gtopo::SketchPlane::from_normal(0, 0, 0, 0, -1, 0);
            named = "XZ";
        } else if (key == "YZ") {
            pl = gtopo::SketchPlane::from_normal(0, 0, 0, 1, 0, 0);
            named = "YZ";
        } else if (key == "FACE") {
            if (!sel_target_.valid || !sel_target_.on_body) {
                log_.push_back("DATUM FACE: pick a face first");
                return true;
            }
            double ox, oy, oz, nx, ny, nz;
            if (!modeller_.plane_of(sel_target_, ox, oy, oz, nx, ny, nz)) {
                log_.push_back("DATUM FACE: that is not a planar face");
                return true;
            }
            pl = gtopo::SketchPlane::from_normal(ox, oy, oz, nx, ny, nz);
            named = "the picked face";
        } else {
            log_.push_back("DATUM [offset] | DATUM XY|XZ|YZ [offset] | DATUM FACE <offset>");
            return true;
        }

        pl.ox += pl.nx * offset;
        pl.oy += pl.ny * offset;
        pl.oz += pl.nz * offset;

        leave_paper_space();
        journal_sketches();
        SketchDoc sk;
        sk.name = "Datum " + std::to_string(sketches_.size() + 1);
        sk.plane = pl;
        sk.layer = layers_.current();
        sketches_.push_back(std::move(sk));
        edit_sketch(int(sketches_.size()) - 1);
        char buf[176];
        std::snprintf(buf, sizeof(buf), "%s: parallel to %s, %s away",
                      sketches_.back().name.c_str(), named.c_str(),
                      gunit::format_length(std::fabs(offset),
                                           gunit::Format{canvas_.display_unit()}).c_str());
        log_.push_back(buf);
        return true;
    }
    if (n == "ROLLBACK" || n == "SUPPRESS" || n == "UNSUPPRESS" || n == "FEATUREMOVE") {
        if (modeller_.bodies().empty()) { log_.push_back(n + ": no solid"); return true; }
        const std::size_t body =
            (sel_target_.valid && sel_target_.on_body &&
             sel_target_.body < gcm::i32(modeller_.bodies().size()))
                ? std::size_t(sel_target_.body) : 0;
        const auto& hist = modeller_.history(body);
        if (hist.empty()) { log_.push_back(n + ": that solid has no history"); return true; }

        std::string why;
        if (n == "ROLLBACK") {
            if (!have_nums) {
                std::string said;
                for (std::size_t i = 0; i < hist.size(); ++i)
                    said += (said.empty() ? "" : ", ") + std::to_string(i) + " " +
                            hist[i].label + (hist[i].suppressed ? " (suppressed)" : "");
                log_.push_back("features: " + said);
                const std::size_t at = modeller_.rolled_back_to(body);
                log_.push_back(at == Modeller::kAllFeatures
                                   ? "built to the end -- ROLLBACK <n> stops earlier"
                                   : "rolled back to " + std::to_string(at) +
                                         " -- ROLLBACK " + std::to_string(hist.size()) +
                                         " rolls forward");
                return true;
            }
            if (!modeller_.rollback(body, std::size_t(std::max(0.0, nums[0])), why))
                log_.push_back("ROLLBACK: " + why);
            else
                log_.push_back(modeller_.last_rebuild_message());
            mesh_dirty_ = true;
            return true;
        }
        if (n == "FEATUREMOVE") {
            if (nums.size() < 2) { log_.push_back("FEATUREMOVE <from> <to>"); return true; }
            if (!modeller_.reorder(body, std::size_t(nums[0]), std::size_t(nums[1]), why))
                log_.push_back("FEATUREMOVE: " + why +
                               " -- a fillet cannot precede the edge it rounds");
            else
                log_.push_back(modeller_.last_rebuild_message());
            mesh_dirty_ = true;
            return true;
        }
        if (!have_nums) { log_.push_back(n + " <feature>: ROLLBACK lists them"); return true; }
        if (!modeller_.suppress(body, std::size_t(nums[0]), n == "SUPPRESS", why))
            log_.push_back(n + ": " + why);
        else
            log_.push_back(modeller_.last_rebuild_message());
        mesh_dirty_ = true;
        return true;
    }
    if (n == "MATERIAL") {
        std::istringstream in(arg);
        std::string what;
        in >> what;
        std::string key = what;
        for (char& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));

        if (what.empty()) {
            std::string names;
            for (const auto& m : modeller_.materials()) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s%s (%.0f kg/m3)",
                              names.empty() ? "" : ", ", m.name.c_str(), m.density);
                names += buf;
            }
            log_.push_back("materials: " + names);
            log_.push_back("MATERIAL <name> assigns it; MATERIAL NEW <name> "
                           "<r> <g> <b> [density] defines one");
            return true;
        }
        if (key == "NEW") {
            Modeller::Material m;
            in >> m.name;
            double r = 0.62, g = 0.66, b = 0.72, density = 0.0;
            in >> r >> g >> b >> density;
            if (m.name.empty()) { log_.push_back("MATERIAL NEW <name> <r> <g> <b> [density]"); return true; }
            const double k = (r > 1.0 || g > 1.0 || b > 1.0) ? 1.0 / 255.0 : 1.0;
            m.r = float(std::clamp(r * k, 0.0, 1.0));
            m.g = float(std::clamp(g * k, 0.0, 1.0));
            m.b = float(std::clamp(b * k, 0.0, 1.0));
            m.density = density;
            modeller_.add_material(m);
            mesh_dirty_ = true;
            log_.push_back("material " + m.name);
            return true;
        }
        if (modeller_.bodies().empty()) { log_.push_back("MATERIAL: no solid"); return true; }
        const std::size_t body =
            (sel_target_.valid && sel_target_.on_body &&
             sel_target_.body < gcm::i32(modeller_.bodies().size()))
                ? std::size_t(sel_target_.body) : 0;
        if (!modeller_.set_material(body, what)) {
            log_.push_back("MATERIAL: no material called " + what);
            return true;
        }
        mesh_dirty_ = true;
        log_.push_back("solid " + std::to_string(body + 1) + " is " + what);
        return true;
    }
    if (n == "PMIADD" || n == "PMILIST" || n == "PMICHECK" || n == "PMIDRIVE") {
        if (modeller_.bodies().empty()) { log_.push_back(n + ": no solid"); return true; }
        const std::size_t body =
            (sel_target_.valid && sel_target_.on_body &&
             sel_target_.body < gcm::i32(modeller_.bodies().size()))
                ? std::size_t(sel_target_.body) : 0;
        if (pmi_.size() < modeller_.bodies().size()) pmi_.resize(modeller_.bodies().size());
        gtopo::PmiSet& set = pmi_[body];
        const gtopo::Body& b = modeller_.bodies()[body];

        if (n == "PMILIST") {
            if (set.size() == 0) {
                log_.push_back("no model dimensions on solid " + std::to_string(body + 1) +
                               " -- PMIADD attaches one to the picked face");
                return true;
            }
            for (gcm::u32 i = 0; i < gcm::u32(set.size()); ++i) {
                const gtopo::PmiDimension* d = set.at(i);
                if (!d) continue;
                const gtopo::PmiMeasurement m = gtopo::measure(b, *d, vol_);
                char buf[224];
                std::snprintf(buf, sizeof(buf), "%u %s %s: %.6g%s%s", i,
                              gtopo::pmi_kind_name(d->kind), d->label.c_str(),
                              m.ok ? m.value : d->nominal,
                              d->driving ? " (driving)" : "",
                              m.ok ? "" : " -- reference lost");
                log_.push_back(buf);
            }
            return true;
        }

        if (n == "PMICHECK") {
            const gtopo::PmiSet::Report r = set.inspect(b, vol_);
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "%zu dimension(s): %zu out of tolerance, %zu with a lost reference",
                          r.measurements.size(), r.out_of_tolerance, r.broken);
            log_.push_back(buf);
            for (const auto& c : set.conflicts(b, vol_))
                log_.push_back("over-driven: " + c.reason);
            if (r.passes() && r.measurements.size()) log_.push_back("passes");
            return true;
        }

        if (n == "PMIDRIVE") {
            std::istringstream in(arg);
            gcm::u32 which = 0;
            double value = 0.0;
            if (!(in >> which) || !(in >> value)) {
                log_.push_back("PMIDRIVE <index> <value> -- PMILIST numbers them");
                return true;
            }
            const gtopo::PmiDimension* d = set.at(which);
            if (!d) { log_.push_back("PMIDRIVE: no dimension " + std::to_string(which)); return true; }
            if (!d->driving) {
                log_.push_back("PMIDRIVE: that one is a reference dimension -- it "
                               "measures the model and must not move it");
                return true;
            }
            const gtopo::DriveResult r = gtopo::drive(b, *d, value, vol_);
            if (!r.ok) { log_.push_back("PMIDRIVE: " + r.reason); return true; }
            modeller_.replace_body(body, r.body);
            if (gtopo::PmiDimension* live = set.at(which)) *live = r.updated;
            set.reattach(modeller_.bodies()[body], vol_);
            mesh_dirty_ = true;
            char buf[176];
            std::snprintf(buf, sizeof(buf),
                          "%s: %.6g -> %.6g, volume %.6g -> %.6g m3",
                          d->label.c_str(), r.before, r.after,
                          r.volume_before, r.volume_after);
            log_.push_back(buf);
            return true;
        }

        if (!sel_target_.valid || !sel_target_.on_body) {
            log_.push_back("PMIADD: pick a face first");
            return true;
        }
        std::istringstream in(arg);
        std::string kind_word;
        in >> kind_word;
        for (char& c : kind_word) c = char(std::tolower(static_cast<unsigned char>(c)));
        double nominal = 0.0, plus = 0.0, minus = 0.0;
        in >> nominal >> plus >> minus;

        gtopo::PmiDimension d;
        d.kind = kind_word == "diameter"  ? gtopo::PmiKind::Diameter
               : kind_word == "radius"    ? gtopo::PmiKind::Radius
               : kind_word == "angle"     ? gtopo::PmiKind::Angle
               : kind_word == "thickness" ? gtopo::PmiKind::Thickness
                                          : gtopo::PmiKind::Distance;
        d.a = gtopo::make_ref(b, gtopo::PmiTarget::Face, sel_target_.face, vol_);
        if (d.kind != gtopo::PmiKind::Diameter && d.kind != gtopo::PmiKind::Radius) {
            if (!hover_target_.valid || !hover_target_.on_body ||
                std::size_t(hover_target_.body) != body ||
                hover_target_.face == sel_target_.face) {
                log_.push_back("PMIADD: that kind needs two faces -- hover the "
                               "second one before running it");
                return true;
            }
            d.b = gtopo::make_ref(b, gtopo::PmiTarget::Face, hover_target_.face, vol_);
        }
        const gtopo::PmiMeasurement m = gtopo::measure(b, d, vol_);
        if (!m.ok) { log_.push_back("PMIADD: " + m.reason); return true; }
        d.nominal = nominal > 0.0 ? nominal : m.value;
        d.tolerance.plus = plus;
        d.tolerance.minus = minus > 0.0 ? minus : plus;
        d.driving = nominal > 0.0;
        d.label = std::string(gtopo::pmi_kind_name(d.kind)) + " " +
                  std::to_string(set.size() + 1);
        const gcm::u32 at = set.add(d);
        char buf[192];
        std::snprintf(buf, sizeof(buf), "%s %u: %.6g%s%s", d.label.c_str(), at,
                      m.value, d.driving ? " (driving)" : " (reference)",
                      d.tolerance.none() ? "" : " with tolerance");
        log_.push_back(buf);
        return true;
    }
    if(n=="SCALELISTEDIT") {
        std::istringstream input(arg);std::string action,name;input>>action;action=uppercase(action);
        if(action.empty()||action=="LIST") {
            if(action.empty()) {settings_open_=true;settings_tab_=3;}
            for(const auto& scale:annotation_scales_.all())
                log_.push_back(scale.name+" ("+std::to_string(scale.factor())+")");
        } else if(action=="ADD") {
            double paper,drawing;std::string extra;input>>std::quoted(name)>>paper>>drawing;
            if(!input || (input>>extra) || name.empty() || name=="1:1" ||
               !std::isfinite(paper)||!std::isfinite(drawing)||paper<=0||drawing<=0||
               drawing/paper<1e-6||drawing/paper>1e6) {
                log_.push_back("SCALELISTEDIT ADD <name> <paper units> <drawing units>; 1:1 is reserved");return true;
            }
            if(annotation_scales_.find(name)) {log_.push_back("scale already exists; delete an unused scale before redefining it");return true;}
            annotation_scales_.add({name,paper,drawing});log_.push_back("annotation scale added: "+name);
        } else if(action=="DELETE") {
            input>>std::quoted(name);bool used=canvas_.annotation_scale_name()==name;
            for(const auto& [id,state]:space_states_) if(id!=active_space_id()) used|=state.annotation_scale==name;
            if(used) log_.push_back("scale is in use by a drawing/layout space");
            else log_.push_back(annotation_scales_.remove(name)?"annotation scale removed":"scale not found or protected");
        } else log_.push_back("SCALELISTEDIT LIST | ADD <name> <paper> <drawing> | DELETE <name>");
        return true;
    }
    if (n == "FENCE" || n == "LASSO" || n=="WPOLYGON" || n=="CPOLYGON") {
        const bool fence=n=="FENCE",crossing=n!="WPOLYGON";
        const auto select=[this,fence,crossing](const std::vector<std::pair<double,double>>& pts) {
            if(fence) canvas_.select_fence(pts,false);else canvas_.select_lasso(pts,crossing,false);
            log_.push_back(canvas_.message());
        };
        if(!arg.empty()) {
            std::istringstream in(arg);std::vector<double> coords;double value;
            while(in>>value) coords.push_back(value);
            if(!in.eof() || coords.size()<(fence?4u:6u) || coords.size()%2 ||
               std::any_of(coords.begin(),coords.end(),[](double x){return !std::isfinite(x);})) {
                log_.push_back(n+": supply at least "+(fence?"two":"three")+" finite x y coordinate pairs");return true;
            }
            std::vector<std::pair<double,double>> pts;
            for(std::size_t i=0;i<coords.size();i+=2) {
                double x,y;canvas_.view().screen_of(coords[i],coords[i+1],x,y);pts.emplace_back(x,y);
            }
            select(pts);return true;
        }
        begin_pick(fence?2:3,n+": pick vertices, Enter finishes, Escape cancels",[this,select](const auto& points) {
            std::vector<std::pair<double,double>> pts;
            for(auto p:points) {
                double x,y,px,py;gcm::to_world(vol_,p,x,y);canvas_.view().screen_of(x,y,px,py);pts.emplace_back(px,py);
            }
            select(pts);
        });
        pick_.finish_on_enter=true;
        return true;
    }
    if (n == "PLOTSTYLE") {
        Layout* l=active_layout();
        if(!l) { log_.push_back("PLOTSTYLE: make a sheet first (LAYOUT NEW)"); return true; }
        const auto split=arg.find(' '); const auto action=uppercase(arg.substr(0,split));
        auto rest=split==std::string::npos ? std::string{} : arg.substr(split+1);
        if(rest.size()>1 && rest.front()=='"' && rest.back()=='"') rest=rest.substr(1,rest.size()-2);
        if(action=="NEW"||action=="EDIT") {
            gplot::PlotStyleTable table;
            std::string error;
            if(action=="NEW") {
                auto mode=uppercase(rest);
                if(mode!="CTB"&&mode!="STB") { log_.push_back("PLOTSTYLE NEW CTB|STB"); return true; }
                table=gplot::PlotStyleTable(mode=="CTB"?gplot::StyleMode::ColorDependent:gplot::StyleMode::Named);
                if(mode=="CTB") for(int i=1;i<=255;++i) table.by_index(i).name="Color_"+std::to_string(i);
            } else {
                auto parsed=gplot::read_plot_style(l->plot_style_data);
                if(!parsed.ok) { log_.push_back("PLOTSTYLE EDIT: load or create a table first"); return true; }
                table=std::move(parsed.table);
                std::istringstream input(rest); std::string name,property,value;
                input>>std::quoted(name)>>property; std::getline(input>>std::ws,value); property=uppercase(property);
                gplot::PlotStyle* style=nullptr;
                if(table.mode()==gplot::StyleMode::ColorDependent) {
                    char* end=nullptr; auto aci=std::strtol(name.c_str(),&end,10);
                    if(name.empty()||*end||aci<1||aci>255) { log_.push_back("PLOTSTYLE EDIT: CTB index must be 1..255"); return true; }
                    style=&table.by_index(int(aci));
                } else if(!name.empty()) style=&table.by_name(name);
                if(!style) { log_.push_back("PLOTSTYLE EDIT: specify a style name"); return true; }
                double number=0; std::istringstream numeric(value); bool is_number=bool(numeric>>number)&&std::isfinite(number); numeric>>std::ws; is_number=is_number&&numeric.eof();
                const bool object=uppercase(value)=="OBJECT";
                if(property=="COLOR") {
                    int r,g,b; std::istringstream rgb(value); bool valid=bool(rgb>>r>>g>>b); rgb>>std::ws;
                    if(!object&&(!valid||!rgb.eof()||r<0||r>255||g<0||g>255||b<0||b>255)) error="COLOR OBJECT|<r> <g> <b>";
                    else { style->use_object_color=object; if(!object) { style->r=r; style->g=g; style->b=b; } }
                } else if(property=="WEIGHT" && (object||(is_number&&number>0&&number<=100))) { style->use_object_lineweight=object; if(!object) style->width_mm=number; }
                else if(property=="SCREEN"&&is_number&&number>=0&&number<=100) style->screening=number;
                else if(property=="LINETYPE"&&(object||(is_number&&number==std::floor(number)&&number>=0&&number<=31))) { style->pattern_id=object?31:int(number); style->use_object_linetype=style->pattern_id==31; }
                else if(property=="FILL"&&is_number&&number==std::floor(number)&&number>=64&&number<=73) style->fill_style=int(number);
                else if(property=="CAP"&&is_number&&number==std::floor(number)&&number>=0&&number<=4) { style->source_end_style=int(number); style->endcap=number==4?gplot::LinecapStyle::Butt:gplot::LinecapStyle(int(number)); }
                else if(property=="JOIN"&&is_number&&number==std::floor(number)&&(number>=0&&number<=3||number==5)) { style->source_join_style=int(number); style->join=number==5?gplot::LinejoinStyle::Miter:gplot::LinejoinStyle(int(number)); }
                else if(property=="SIZE"&&is_number&&number>0&&number<=10000) style->pattern_size=number;
                else if(property=="SCALE"&&is_number&&number>0&&number<=10000) table.pattern_scale=number;
                else if(property=="GRAYSCALE"&&(uppercase(value)=="ON"||uppercase(value)=="OFF")) style->grayscale=uppercase(value)=="ON";
                else if(property=="ADAPTIVE"&&(uppercase(value)=="ON"||uppercase(value)=="OFF")) style->adaptive_linetype=uppercase(value)=="ON";
                else error="properties: COLOR, WEIGHT (mm), SCREEN (0..100), LINETYPE (0..31), FILL (64..73), CAP (0..4), JOIN (0..3/5), SIZE, SCALE, GRAYSCALE ON/OFF, ADAPTIVE ON/OFF";
            }
            auto bytes=error.empty()?gplot::write_plot_style(table,&error):std::vector<gcm::u8>{};
            if(bytes.empty()) { log_.push_back("PLOTSTYLE: "+error); return true; }
            l->plot_style_data=std::move(bytes); l->style_mode=table.mode(); l->monochrome=false;
            if(action=="NEW") l->plot_style_name=table.mode()==gplot::StyleMode::Named?"Drawing.stb":"Drawing.ctb";
            invalidate_frame_caches(); log_.push_back("Plot style table updated: "+l->plot_style_name); return true;
        }
        if(action=="LOAD") {
            std::error_code ec; auto size=std::filesystem::file_size(rest,ec);
            if(ec||size>4*1024*1024) { log_.push_back("PLOTSTYLE: cannot read table or file exceeds 4 MiB"); return true; }
            std::ifstream input(rest,std::ios::binary);
            std::vector<gcm::u8> data((std::istreambuf_iterator<char>(input)),{});
            auto parsed=gplot::read_plot_style(data);
            if(!parsed.ok) { log_.push_back(parsed.error); return true; }
            l->plot_style_name=std::filesystem::path(rest).filename().string();
            l->plot_style_data=std::move(data); l->style_mode=parsed.table.mode(); l->monochrome=false;
            invalidate_frame_caches(); log_.push_back("Plot style loaded: "+l->plot_style_name);
            for(const auto& warning:parsed.warnings) log_.push_back(warning);
            return true;
        }
        if(action=="SAVE") {
            if(l->plot_style_data.empty()||rest.empty()) { log_.push_back("PLOTSTYLE SAVE <path>: load a table first"); return true; }
            std::ofstream output(rest,std::ios::binary);
            output.write(reinterpret_cast<const char*>(l->plot_style_data.data()),std::streamsize(l->plot_style_data.size())); output.close();
            log_.push_back(output ? "Plot style saved: "+rest : "PLOTSTYLE: could not save "+rest); return true;
        }
        if(action=="LAYER" || action=="SET") {
            auto parsed=gplot::read_plot_style(l->plot_style_data);
            if(!parsed.ok||parsed.table.mode()!=gplot::StyleMode::Named) { log_.push_back("PLOTSTYLE: load a named STB table first"); return true; }
            std::string name=rest; gdoc::Layer* layer=nullptr;
            if(action=="LAYER") {
                const auto sp=rest.find(' ');
                if(sp==std::string::npos) { log_.push_back("PLOTSTYLE LAYER <layer> <style>"); return true; }
                layer=layers_.find(rest.substr(0,sp)); name=rest.substr(sp+1);
                if(!layer) { log_.push_back("PLOTSTYLE: unknown layer"); return true; }
            }
            if(!parsed.table.find(name)) { log_.push_back("PLOTSTYLE: unknown named style"); return true; }
            if(layer) layer->plot_style=name;
            else {
                const auto selected=canvas_.selection();
                if(selected.empty()) { log_.push_back("PLOTSTYLE SET: select geometry first"); return true; }
                canvas_.modify_shapes([&](auto& shapes) { for(auto i:selected) if(i<shapes.size()) shapes[i].attrs.user["plot_style"]=name; });
                canvas_.set_selection(selected);
            }
            invalidate_frame_caches(); log_.push_back("Plot style assigned: "+name); return true;
        }
        if(action.empty()||action=="LIST") {
            log_.push_back("Plot style: "+(l->plot_style_name.empty() ? std::string(l->monochrome?"monochrome":"colour") : l->plot_style_name));
            if(!l->plot_style_data.empty()) {
                auto parsed=gplot::read_plot_style(l->plot_style_data);
                if(parsed.ok && parsed.table.mode()==gplot::StyleMode::Named)
                    for(const auto& style:parsed.table.all()) log_.push_back(style.name);
            }
            log_.push_back("PLOTSTYLE NEW CTB|STB | EDIT <index|name> <property> <value> | LOAD|SAVE <file> | MONO|COLOUR | LAYER <layer> <style> | SET <style>"); return true;
        }
        if(action=="MONO"||action=="MONOCHROME"||action=="COLOUR"||action=="COLOR") {
            l->monochrome=action.starts_with("MONO"); l->plot_style_data.clear(); l->plot_style_name.clear();
            l->style_mode=gplot::StyleMode::ColorDependent; invalidate_frame_caches();
            log_.push_back(l->monochrome ? "Plot style: monochrome" : "Plot style: colour"); return true;
        }
        log_.push_back("PLOTSTYLE: LOAD|SAVE <file> | LIST | MONO|COLOUR | LAYER <layer> <style> | SET <style>"); return true;
    }
    if (n == "PSPACE" || n == "MSPACE" || n == "MODEL" || n == "PAPER") {
        if (n == "PSPACE" || n == "PAPER") enter_paper_space();
        else leave_paper_space();
        return true;
    }
    if (n == "TITLEBLOCK") {
        if (!active_layout()) {
            log_.push_back("TITLEBLOCK: make a layout first (LAYOUT NEW)");
            return true;
        }
        if (uppercase(arg).rfind("FIELD ",0)==0) {
            auto field=arg.substr(6);
            for (char& c:field) c=char(std::toupper(static_cast<unsigned char>(c)));
            if (!in_paper_space_ || canvas_.selected_texts().empty() || !sheet_fields(*active_layout()).count(field)) {
                log_.push_back("TITLEBLOCK FIELD DOCUMENT|SHEET|PAPER|SCALE|PAPERSCALE: select sheet text first"); return true;
            }
            for (auto i:canvas_.selected_texts()) canvas_.set_text_field(i,field);
            return true;
        }
        const auto paper_size=layout_paper(active_layout());
        const gplot::PaperSize* paper=&paper_size;
        if (!paper) { log_.push_back("TITLEBLOCK: unknown paper " + plot_paper_); return true; }
        const double w = plot_landscape_ ? paper->width : paper->height;
        const double h = plot_landscape_ ? paper->height : paper->width;
        const double m = 0.010;
        const double panel_w = std::min(0.180, w * 0.5);
        const double panel_h = std::min(0.060, h * 0.35);

        const bool was_paper = in_paper_space_;
        if (was_paper) leave_paper_space();
        Layout& l = *active_layout();
        l.shapes.clear();
        l.notes.clear();
        const auto line = [&](double x0, double y0, double x1, double y1) {
            gsnap::Shape sh;
            sh.kind = gsnap::ShapeKind::Segment;
            sh.a = gcm::to_lattice(vol_, x0, y0);
            sh.b = gcm::to_lattice(vol_, x1, y1);
            sh.layer = "Titleblock";
            l.shapes.push_back(std::move(sh));
        };
        line(m, m, w - m, m);
        line(w - m, m, w - m, h - m);
        line(w - m, h - m, m, h - m);
        line(m, h - m, m, m);
        const double px0 = w - m - panel_w, py0 = m, px1 = w - m, py1 = m + panel_h;
        line(px0, py0, px1, py0);
        line(px0, py1, px1, py1);
        line(px0, py0, px0, py1);
        line(px0, py0 + panel_h / 3.0, px1, py0 + panel_h / 3.0);
        line(px0, py0 + panel_h * 2.0 / 3.0, px1, py0 + panel_h * 2.0 / 3.0);

        const auto note = [&](double x, double y, const std::string& text, double height) {
            Canvas::TextItem t;
            t.pos = gcm::to_lattice(vol_, x, y);
            t.text = text;
            t.height = height;
            t.halign = 0;
            t.valign = 1;
            l.notes.push_back(std::move(t));
        };
        const double pad = 0.003;
        note(px0 + pad, py0 + panel_h * 2.0 / 3.0 + pad,
             document_path_.empty() ? std::string("UNTITLED") : file_title(document_path_),
             0.0035);
        note(px0 + pad, py0 + panel_h / 3.0 + pad, l.name, 0.0025);
        note(px0 + pad, py0 + pad,
             plot_paper_ + std::string("  ") +
                 (plot_scale_den_ > 0 ? scale_display(plot_scale_den_)
                                      : std::string("SCALE AS SHOWN")),
             0.0025);

        l.notes[0].field="DOCUMENT"; l.notes[1].field="SHEET"; l.notes[2].field="PAPERSCALE";
        Canvas keyed;
        keyed.load_shapes(l.shapes);
        l.shapes=keyed.shapes();

        if (!layers_.find("Titleblock")) {
            gdoc::Layer tb;
            tb.name = "Titleblock";
            layers_.add(tb);
        }
        log_.push_back("titleblock on " + l.name + ": " +
                       std::to_string(l.shapes.size()) + " lines, " +
                       std::to_string(l.notes.size()) + " fields");
        if (was_paper) enter_paper_space();
        return true;
    }
    if (n == "RECOVER") { recover_autosave(); return true; }
    if (n == "SETVAR") {
        std::string name = command_arg_, value;
        if (const std::size_t sp = command_arg_.find(' '); sp != std::string::npos) {
            name = command_arg_.substr(0, sp);
            value = command_arg_.substr(sp + 1);
        }
        for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
        gcmd::SysVar* v = vars_.find(name);
        if (name.empty()) { log_.push_back("SETVAR <name> [value]"); return true; }
        if (!v) { log_.push_back(name + " is not a system variable"); return true; }
        if (value.empty()) {
            log_.push_back(v->name + " = " +
                           (v->type == gcmd::VarType::Text ? v->sval
                            : v->type == gcmd::VarType::Int ? std::to_string(v->ival)
                                                            : std::to_string(v->rval)));
            return true;
        }
        if (v->read_only) { log_.push_back(v->name + " is read-only"); return true; }
        set_drafting_var(name,value);
        return true;
    }
    if (n == "LTSCALE") {
        if (command_arg_.empty()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "LTSCALE %.6g", linetype_scale_);
            log_.push_back(buf);
            return true;
        }
        double v = 0.0;
        try { v = std::stod(command_arg_); } catch (...) {}
        if (v <= 0.0) { log_.push_back("LTSCALE takes a positive number"); return true; }
        linetype_scale_ = v;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "LTSCALE %.6g", linetype_scale_);
        log_.push_back(buf);
        return true;
    }
    if (n == "FACEOFFSET" || n == "MOVEFACE" || n == "ALIGNFACE" || n == "THICKEN") {
        if (!sel_target_.valid || !sel_target_.on_body ||
            sel_target_.body >= gcm::i32(modeller_.bodies().size())) {
            log_.push_back(n + ": pick a face of a solid first");
            return true;
        }
        const auto& b = modeller_.bodies()[std::size_t(sel_target_.body)];
        gtopo::EditResult r;
        std::string what;

        if (n == "FACEOFFSET" || n == "THICKEN") {
            if (!have_nums || std::fabs(nums[0]) < 1e-12) {
                ask(n.c_str(), "distance",
                    n == "THICKEN" ? "THICKEN: type the wall thickness, e.g. 0.02"
                                   : "OFFSETFACE: type the distance, e.g. 0.5");
                return true;
            }
            const double d = n == "THICKEN" ? std::fabs(nums[0]) : nums[0];
            r = gtopo::offset_face(b, sel_target_.face, d, vol_);
            what = (n == "THICKEN" ? "thickened " : "offset ") +
                   gunit::format_length(std::fabs(d), gunit::Format{canvas_.display_unit()});
        } else if (n == "MOVEFACE") {
            if (nums.size() < 3) {
                ask("MOVEFACE", "dx dy dz", "MOVEFACE: type the move, e.g. 0 0 0.5");
                return true;
            }
            const gtopo::Vec3 delta{gcm::F64(nums[0]), gcm::F64(nums[1]),
                                    gcm::F64(nums[2])};
            r = gtopo::move_face(b, sel_target_.face, delta, vol_);
            what = "moved the face";
        } else {
            if (!have_nums) {
                ask("ALIGNFACE", "face",
                    "ALIGNFACE: type the face number to become parallel to");
                return true;
            }
            const gcm::u32 other = static_cast<gcm::u32>(nums[0]);
            if (other >= b.face_count()) {
                log_.push_back("ALIGNFACE: this solid has " +
                               std::to_string(b.face_count()) + " faces");
                return true;
            }
            double cx = 0.0, cy = 0.0, cz = 0.0;
            std::size_t n_pts = 0;
            for (gcm::u32 li = b.face_loop_begin(sel_target_.face);
                 li < b.face_loop_end(sel_target_.face); ++li)
                for (gcm::u32 h = b.loop_begin(li); h < b.loop_end(li); ++h) {
                    double x, y, z;
                    gcm::to_world(vol_, b.vertex_point[b.he_vertex[h]], x, y, z);
                    cx += x; cy += y; cz += z; ++n_pts;
                }
            if (n_pts == 0) { log_.push_back("ALIGNFACE: that face has no corners"); return true; }
            cx /= double(n_pts); cy /= double(n_pts); cz /= double(n_pts);

            gtopo::Plane target = gtopo::face_plane(b, other, vol_);
            target.d = target.nx * cx + target.ny * cy + target.nz * cz;
            r = gtopo::retarget_face(b, sel_target_.face, target, vol_);
            what = "aligned to face " + std::to_string(other);
        }

        if (!r.ok) { log_.push_back(n + ": " + r.reason); return true; }
        modeller_.replace_body(std::size_t(sel_target_.body), r.body);
        mesh_dirty_ = true;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s, volume %.6g -> %.6g m3",
                      what.c_str(), r.volume_before, r.volume_after);
        log_.push_back(buf);
        return true;
    }
    if (n == "DRAFT" || n == "OFFSETFACE2") {
        if (!sel_target_.valid || !sel_target_.on_body ||
            sel_target_.body >= gcm::i32(modeller_.bodies().size())) {
            log_.push_back("DRAFT: pick a face of a solid first");
            return true;
        }
        double angle = 3.0;
        if (!command_arg_.empty()) { try { angle = std::stod(command_arg_); } catch (...) {} }
        const auto& b = modeller_.bodies()[std::size_t(sel_target_.body)];
        gtopo::Plane neutral{};
        neutral.nx = 0.0; neutral.ny = 0.0; neutral.nz = 1.0; neutral.d = 0.0;
        const gtopo::EditResult r =
            gtopo::draft_face(b, sel_target_.face, neutral, angle, vol_);
        if (!r.ok) { log_.push_back("DRAFT: " + r.reason); return true; }
        modeller_.replace_body(std::size_t(sel_target_.body), r.body);
        mesh_dirty_ = true;
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "drafted %.3g degrees, volume %.6g -> %.6g m3",
                      angle, r.volume_before, r.volume_after);
        log_.push_back(buf);
        return true;
    }
    if (n == "VIEWBASE") {
        if (modeller_.bodies().empty()) {
            log_.push_back("VIEWBASE: no solid to project");
            return true;
        }
        const std::size_t which =
            (sel_target_.valid && sel_target_.on_body &&
             sel_target_.body < gcm::i32(modeller_.bodies().size()))
                ? std::size_t(sel_target_.body) : 0;
        double scale = 1.0;
        gdraft::Layout layout = gdraft::Layout::ThreeView;
        {
            std::string a = command_arg_;
            for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (a.rfind("one", 0) == 0) layout = gdraft::Layout::OneView;
            else if (a.rfind("two", 0) == 0) layout = gdraft::Layout::TwoView;
            else if (a.rfind("four", 0) == 0) layout = gdraft::Layout::FourView;
            const std::size_t sp = a.find(' ');
            const std::string num = sp == std::string::npos ? a : a.substr(sp + 1);
            if (!num.empty()) { try { scale = std::stod(num); } catch (...) {} }
            if (scale <= 0.0) scale = 1.0;
        }

        gdraft::SourceModel m;
        m.id = 1;
        m.body = modeller_.bodies()[which];
        m.volume = vol_;
        gdraft::ViewSet set(gdraft::ProjectionAngle::Third);
        double cx = 0.0, cy = 0.0;
        canvas_.view().world_of(canvas_.view().vw() * 0.5, canvas_.view().vh() * 0.5, cx, cy);
        const auto ids = set.add_layout(m, layout, cx, cy, scale);

        std::vector<gsnap::Shape> shapes;
        std::vector<std::string> layers;
        gdraft::to_sheet_shapes(set, vol_, shapes, layers);
        if (shapes.empty()) {
            log_.push_back("VIEWBASE: the projection produced nothing");
            return true;
        }
        for (const auto& name : layers)
            if (!layers_.find(name)) {
                gdoc::Layer l;
                l.name = name;
                layers_.add(std::move(l));
            }
        if (Layout* l = active_layout()) {
            l->view_body = int(which);
            l->view_scale = scale;
            l->view_layout = layout;
            l->sections.clear();
            l->details.clear();
            const std::size_t made = refresh_views(*l);
            plot_cache_.valid = false;
            log_.push_back("projected " + std::to_string(made) + " view(s) onto " +
                           l->name + ", " + std::to_string(l->view_shapes.size()) +
                           " edges at 1:" + std::to_string(int(std::lround(1.0 / scale))));
            return true;
        }
        canvas_.modify_shapes([&](std::vector<gsnap::Shape>& out) {
            for (std::size_t i = 0; i < shapes.size(); ++i) {
                shapes[i].layer = i < layers.size() ? layers[i] : std::string("0");
                out.push_back(shapes[i]);
            }
        });
        log_.push_back("projected " + std::to_string(ids.size()) + " view(s), " +
                       std::to_string(shapes.size()) + " edges at 1:" +
                       std::to_string(int(std::lround(1.0 / scale))) +
                       " -- as loose geometry, because no sheet is current");
        return true;
    }
    if (n == "VIEWSECTION" || n == "VIEWDETAIL" || n == "VIEWUPDATE") {
        Layout* l = active_layout();
        if (!l) { log_.push_back(n + ": make a sheet first (LAYOUT NEW)"); return true; }
        if (l->view_body < 0) {
            log_.push_back(n + ": project a view onto this sheet first (VIEWBASE)");
            return true;
        }
        if (n == "VIEWUPDATE") {
            const std::size_t made = refresh_views(*l);
            plot_cache_.valid = false;
            log_.push_back("regenerated " + std::to_string(made) + " view(s), " +
                           std::to_string(l->view_shapes.size()) + " edges");
            return true;
        }
        if (n == "VIEWSECTION") {
            gdraft::SectionLine line;
            std::vector<double> v;
            if (parse_numbers(arg, v) && v.size() >= 4) {
                line.x0 = v[0]; line.y0 = v[1]; line.x1 = v[2]; line.y1 = v[3];
            } else {
                line.x0 = 0.0; line.y0 = -0.05; line.x1 = 0.0; line.y1 = 0.05;
            }
            line.label = std::string(1, char('A' + int(l->sections.size() % 26)));
            l->sections.push_back({0, line});
            const std::size_t made = refresh_views(*l);
            plot_cache_.valid = false;
            log_.push_back("section " + line.label + "-" + line.label + " on " + l->name +
                           " (" + std::to_string(made) + " views, " +
                           std::to_string(l->view_shapes.size()) + " edges)");
            return true;
        }
        gdraft::DetailCircle circle;
        double dscale = 2.0;
        {
            std::vector<double> v;
            if (parse_numbers(arg, v)) {
                if (v.size() >= 3) { circle.x = v[0]; circle.y = v[1]; circle.radius = v[2]; }
                if (v.size() >= 4 && v[3] > 0.0) dscale = v[3];
            }
        }
        circle.label = std::string(1, char('A' + int(l->details.size() % 26)));
        l->details.push_back({0, circle, dscale});
        const std::size_t made = refresh_views(*l);
        plot_cache_.valid = false;
        log_.push_back("detail " + circle.label + " on " + l->name + " at " +
                       std::to_string(int(std::lround(dscale))) + ":1 (" +
                       std::to_string(made) + " views, " +
                       std::to_string(l->view_shapes.size()) + " edges)");
        return true;
    }
    if (n == "LINETYPE") {
        gdoc::Layer* l = layers_.find(layers_.current());
        if (arg.empty()) {
            std::string names;
            for (const auto& lt : linetypes_.all()) names += (names.empty() ? "" : ", ") + lt.name;
            log_.push_back("LINETYPE: layer " + layers_.current() + " is " +
                           (l ? l->linetype : std::string("Continuous")));
            log_.push_back("  loaded: " + names);
            log_.push_back("  LINETYPE <name> sets it; LINETYPE LOAD <file.lin> adds more");
            return true;
        }
        if (arg.size() > 5 && (arg.compare(0, 5, "LOAD ") == 0 || arg.compare(0, 5, "load ") == 0)) {
            const std::string path = arg.substr(5);
            std::ifstream f(path);
            if (!f) { log_.push_back("LINETYPE: could not open " + path); return true; }
            std::ostringstream ss;
            ss << f.rdbuf();
            const std::size_t added = linetypes_.load_lin(ss.str());
            log_.push_back("loaded " + std::to_string(added) + " linetype(s) from " + path);
            return true;
        }
        if (arg != "ByLayer" && !linetypes_.find(arg)) {
            log_.push_back("LINETYPE: no linetype called " + arg + " -- LINETYPE lists them");
            return true;
        }
        if (!canvas_.selection().empty()) {
            const std::vector<std::size_t> sel = selected_indices();
            canvas_.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) sh[i].attrs.linetype = arg;
            });
            log_.push_back(std::to_string(sel.size()) + " object(s): linetype " + arg);
            return true;
        }
        if (!l) { log_.push_back("LINETYPE: no current layer"); return true; }
        l->linetype = arg;
        log_.push_back("layer " + layers_.current() + ": linetype " + arg);
        return true;
    }
    if (n == "STYLE") {
        std::istringstream in(arg);
        std::string first;
        in >> first;

        if(uppercase(first)=="ANNOTATIVE") {
            std::string name,value,extra;in>>std::quoted(name)>>value;
            const auto* existing=text_styles_.find(name);value=uppercase(value);
            if(!existing || (value!="ON"&&value!="OFF") || (in>>extra)) {
                log_.push_back("STYLE ANNOTATIVE <existing style> ON|OFF");return true;
            }
            auto style=*existing;style.annotative=value=="ON";text_styles_.add(style);canvas_.set_text_styles(text_styles_);
            log_.push_back("text style "+name+" annotative "+value);return true;
        }
        if(first=="FONT") {
            std::string name,path;in>>name;std::getline(in,path);
            const auto begin=path.find_first_not_of(" ");path=begin==std::string::npos?"":path.substr(begin);
            if(path.size()>1 && path.front()=='"' && path.back()=='"') path=path.substr(1,path.size()-2);
            const auto* existing=text_styles_.find(name);
            gtext::StrokeFont font;std::string error;
            if(!existing || !font.load_file(path,&error)) {
                log_.push_back("STYLE FONT: use an existing style and a readable SHX/SHP font: "+error);return true;
            }
            auto st=*existing;st.font=path;text_styles_.add(st);canvas_.set_text_styles(text_styles_);
            log_.push_back("text style "+name+" font "+path);return true;
        }
        if (first.empty()) {
            std::string names;
            for (const auto& st : text_styles_.all()) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s%s %.4g", names.empty() ? "" : ", ",
                              st.name.c_str(), double(st.height));
                names += buf;
            }
            log_.push_back("text styles: " + names + " (current " +
                           text_styles_.current() + ", height " +
                           std::to_string(text_height_) + ")");
            return true;
        }
        if (have_nums && nums.size() == 1 && first.find_first_not_of("0123456789.-+eE") ==
                                                 std::string::npos) {
            if (nums[0] <= 0.0) { log_.push_back("STYLE: a height is positive"); return true; }
            text_height_ = nums[0];
            log_.push_back("text height " + std::to_string(text_height_));
            return true;
        }

        double height = 0.0, width = 1.0, oblique = 0.0;
        const bool given = bool(in >> height);
        in >> width;
        in >> oblique;
        if (!given) {
            if (const gtext::TextStyleDef* st = text_styles_.find(first)) {
                text_styles_.set_current(first);
                if (st->height_fixed()) text_height_ = double(st->height);
                log_.push_back("text style " + first);
            } else {
                log_.push_back("STYLE: no style called " + first +
                               " -- STYLE <name> <height> makes one");
            }
            return true;
        }
        if(!std::isfinite(height) || height<0 || !std::isfinite(width) || width<=0 ||
           !std::isfinite(oblique) || std::abs(oblique)>=85) {
            log_.push_back("STYLE: height must be nonnegative, width positive, and oblique between -85 and 85 degrees"); return true;
        }
        gtext::TextStyleDef st;
        if (const gtext::TextStyleDef* existing = text_styles_.find(first)) st = *existing;
        st.name = first;
        st.height = float(height);
        st.width_factor = float(width > 0.0 ? width : 1.0);
        st.oblique = float(oblique);
        text_styles_.add(st);
        text_styles_.set_current(first);
        canvas_.set_text_styles(text_styles_);
        if (st.height_fixed()) text_height_ = double(st.height);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "text style %s: height %.4g, width %.3g, oblique %.3g",
                      first.c_str(), height, width, oblique);
        log_.push_back(buf);
        return true;
    }

    if(n=="LEADEREDIT") {
        const auto selected=canvas_.selected_dimensions();
        if(selected.empty()) {log_.push_back("LEADEREDIT: select a leader");return true;}
        std::istringstream in(arg);std::string op;in>>op;op=uppercase(op);
        std::string value;std::getline(in,value);if(!value.empty()&&value.front()==' ') value.erase(0,1);
        if(op=="TEXT" || op=="STYLE") {
            if(op=="STYLE"&&!canvas_.dim_styles().find(value)) {log_.push_back("LEADEREDIT: unknown style");return true;}
            canvas_.modify_dimensions([&](auto& dims){for(auto i:selected) if(i<dims.size()&&dims[i].kind==gdim::Kind::Leader) {
                if(op=="TEXT") dims[i].text_override=value;else dims[i].style=value;
            }});
        } else if(op=="VERTEX") {
            std::istringstream vertex(value);std::size_t index=0;vertex>>index;
            std::string position;std::getline(vertex,position);
            const auto coordinate=gcmd::parse_coordinate(position,canvas_.display_unit());
            if(!index||!coordinate||!coordinate->valid()) {log_.push_back("LEADEREDIT VERTEX <one-based grip number> x,y");return true;}
            for(auto i:selected)if(i<canvas_.dimensions().size()&&canvas_.dimensions()[i].kind==gdim::Kind::Leader&&index>canvas_.dimensions()[i].leader_grips().size()) {
                log_.push_back("LEADEREDIT: vertex is outside the leader");return true;
            }
            double x,y,z;coordinate->resolve(0,0,0,0,x,y,z);if(!coordinate->world_override)canvas_.ucs_to_world(x,y,x,y);
            const auto point=gcm::to_lattice(vol_,x,y);
            canvas_.modify_dimensions([&](auto& dims){for(auto i:selected)if(i<dims.size()&&dims[i].kind==gdim::Kind::Leader)dims[i].move_leader_grip(index-1,point);});
        } else if(op=="ARROW" || op=="BEND" || op=="TEXTPOINT") {
            const auto coordinate=gcmd::parse_coordinate(value,canvas_.display_unit());
            if(!coordinate || !coordinate->valid()) {log_.push_back("LEADEREDIT: enter x,y");return true;}
            double x,y,z;coordinate->resolve(0,0,0,0,x,y,z);if(!coordinate->world_override) canvas_.ucs_to_world(x,y,x,y);
            const auto point=gcm::to_lattice(vol_,x,y);
            canvas_.modify_dimensions([&](auto& dims){for(auto i:selected) if(i<dims.size()&&dims[i].kind==gdim::Kind::Leader) {
                auto& d=dims[i];d.move_leader_point(op=="ARROW"?d.p1:op=="BEND"?d.p2:d.p3,point);
            }});
        } else {log_.push_back("LEADEREDIT TEXT <text> | STYLE <name> | ARROW|BEND|TEXTPOINT x,y | VERTEX <number> x,y");return true;}
        log_.push_back("leader updated");return true;
    }
    if(n=="MLEADERSTYLE" || n=="LEADERSTYLE") {
        const auto current=canvas_.dim_styles().current();
        run_command("DIMSTYLE "+arg);
        if(!arg.empty()) {std::istringstream in(arg);std::string name;in>>name;if(canvas_.dim_styles().find(name)) vars_.set_text("MLEADERSTYLE",name);}
        canvas_.dim_styles().set_current(current);return true;
    }
    if (n == "LEADER" || n == "MLEADER") {
        const std::string text = arg;
        std::string name=vars_.get_text("MLEADERSTYLE","Leader");
        if(!canvas_.dim_styles().find(name)) {
            gdim::Style st;st.name=name;st.dimtxt=text_height_*1000;st.dimasz=text_height_*800;st.dimgap=text_height_*500;
            canvas_.dim_styles().add(st);
        }
        begin_pick(2,n+": click the arrow point, then the text point",[this,text,name,world](const auto& p) {
            double ax,ay,bx,by;world(p[0],ax,ay);world(p[1],bx,by);
            if(std::hypot(bx-ax,by-ay)<1e-9) {log_.push_back("leader needs two distinct points");return;}
            const auto* style=canvas_.dim_styles().find(name);
            const double landing=style?style->scaled(style->dimtxt)*.002:text_height_*2;
            gdim::Dimension d;d.kind=gdim::Kind::Leader;d.style=name;d.layer=canvas_.current_layer();
            d.p1=p[0];d.p3=p[1];d.p2=gcm::to_lattice(vol_,bx+(bx>=ax?-landing:landing),by);
            if(d.p2.x==d.p1.x && d.p2.y==d.p1.y) d.p2=d.p3;
            d.text_override=text;canvas_.add_dimensions({d});log_.push_back("leader added");
        });return true;
    }

    if (n == "PLOTSCALE") {
        int den = 0;
        gplot::NamedScale parsed;
        if (arg.empty() || uppercase(arg) == "FIT") den = 0;
        else if (gplot::parse_scale(arg, parsed) && parsed.paper == 1.0 && parsed.drawing >= 1.0)
            den = int(parsed.drawing + 0.5);           // drafter forms: 1:50, 1/4"=1'-0", 1"=20'
        else if (std::sscanf(arg.c_str(), "1:%d", &den) == 1 || std::sscanf(arg.c_str(), "%d", &den) == 1) {}
        else { log_.push_back("PLOTSCALE 1:<n>, a named scale like 1/4\"=1'-0\", or FIT"); return true; }
        plot_scale_den_ = std::max(0, den);
        log_.push_back(plot_scale_den_ > 0 ? "plot scale " + (parsed.name.empty() ? "1:" + std::to_string(plot_scale_den_) : parsed.name)
                                           : "plot scale: fit to paper");
        return true;
    }
    if (n == "PAGESETUP") {
        std::istringstream in(arg);
        std::string first, second;
        in >> first >> second;
        const std::string key = uppercase(first);

        if (first.empty()) {
            std::string said = "PAGESETUP: " + plot_paper_ +
                               (plot_landscape_ ? " landscape" : " portrait");
            if (const Layout* l = active_layout())
                said += l->area == gplot::PlotArea::Window ? ", window" : ", extents";
            log_.push_back(said);
            if (!page_setups_.empty()) {
                std::string names;
                for (const auto& p : page_setups_)
                    names += (names.empty() ? "" : ", ") + p.name;
                log_.push_back("  saved setups: " + names);
            }
            log_.push_back("  PAGESETUP <paper> [landscape|portrait] | AREA "
                           "EXTENTS|WINDOW | SAVE <name> | <name>");
            return true;
        }

        if (key == "AREA") {
            Layout* l = active_layout();
            if (!l) { log_.push_back("PAGESETUP AREA: make a layout first"); return true; }
            const std::string which = uppercase(second);
            if (which == "EXTENTS" || which.empty()) {
                l->area = gplot::PlotArea::Extents;
                log_.push_back("plot area: the whole drawing");
            } else if (which == "WINDOW") {
                std::vector<double> corners;
                {
                    std::istringstream rest(arg);
                    std::string skip;
                    rest >> skip >> skip;
                    double value = 0.0;
                    while (rest >> value) corners.push_back(value);
                }
                if (corners.size() >= 4) {
                    l->win_lo_x = std::min(corners[0], corners[2]);
                    l->win_lo_y = std::min(corners[1], corners[3]);
                    l->win_hi_x = std::max(corners[0], corners[2]);
                    l->win_hi_y = std::max(corners[1], corners[3]);
                    l->area = gplot::PlotArea::Window;
                    plot_cache_.valid = false;
                    log_.push_back("plot area: the window given");
                    return true;
                }
                begin_pick(2, "PAGESETUP: pick the two corners of the window",
                           [this](const std::vector<gcm::Lattice2>& p) {
                    Layout* la = active_layout();
                    if (!la) return;
                    double ax, ay, bx, by;
                    gcm::to_world(vol_, p[0], ax, ay);
                    gcm::to_world(vol_, p[1], bx, by);
                    la->win_lo_x = std::min(ax, bx);
                    la->win_lo_y = std::min(ay, by);
                    la->win_hi_x = std::max(ax, bx);
                    la->win_hi_y = std::max(ay, by);
                    la->area = gplot::PlotArea::Window;
                    plot_cache_.valid = false;
                    log_.push_back("plot area: the window picked");
                });
            } else {
                log_.push_back("PAGESETUP AREA: EXTENTS or WINDOW");
            }
            return true;
        }

        if (key == "SAVE") {
            if (second.empty()) { log_.push_back("PAGESETUP SAVE <name>"); return true; }
            PageSetupNamed p;
            p.name = second;
            p.paper = plot_paper_;
            p.landscape = plot_landscape_;
            p.scale_den = plot_scale_den_;
            if (const Layout* l = active_layout()) p.area = l->area;
            bool replaced = false;
            for (auto& e : page_setups_)
                if (e.name == p.name) { e = p; replaced = true; }
            if (!replaced) page_setups_.push_back(p);
            log_.push_back((replaced ? "page setup updated: " : "page setup saved: ") + p.name);
            return true;
        }
        for (const auto& p : page_setups_)
            if (p.name == first) {
                plot_paper_ = p.paper;
                plot_landscape_ = p.landscape;
                plot_scale_den_ = p.scale_den;
                if (Layout* l = active_layout()) l->area = p.area;
                layout_store();
                plot_cache_.valid = false;
                log_.push_back("page setup " + p.name + ": " + plot_paper_ +
                               (plot_landscape_ ? " landscape" : " portrait"));
                return true;
            }

        if (!gplot::find_paper(key)) {
            log_.push_back("PAGESETUP: no paper or saved setup called " + first +
                           " (A4, A3, A2, A1, A0, Letter, Tabloid...)");
            return true;
        }
        plot_paper_ = key;
        if (!second.empty()) plot_landscape_ = uppercase(second).rfind("L", 0) == 0;
        layout_store();
        plot_cache_.valid = false;
        log_.push_back("page: " + plot_paper_ + (plot_landscape_ ? " landscape" : " portrait"));
        return true;
    }

    if (n == "SCRIPT" || n == "RSCRIPT") {
        std::string path = n == "RSCRIPT" ? last_script_ : arg;
        if (path.empty()) { log_.push_back(n == "RSCRIPT" ? "RSCRIPT: no script has run yet" : "SCRIPT <file.scr>: runs the commands in the file"); return true; }
        std::ifstream f(path);
        if (!f) { log_.push_back("SCRIPT: could not open " + path); return true; }
        std::ostringstream ss;
        ss << f.rdbuf();
        last_script_ = path;
        run_script(gcmd::parse_scr(ss.str(), path));
        return true;
    }
    if (n == "DELAY") {
        log_.push_back(script_ && script_->state() == gcmd::RunState::Paused
                           ? "DELAY: the script is paused -- RESUME continues it"
                           : "DELAY: only meaningful inside a script");
        return true;
    }
    if (n == "RESUME") {
        if (!script_ || script_->state() != gcmd::RunState::Paused) {
            log_.push_back("RESUME: no script is paused");
            return true;
        }
        script_->resume();
        const gcmd::RunReport r = script_->run();
        report_script(r);
        return true;
    }
    if (n == "APPLOAD" || n == "LISP") {
        if (arg.empty()) {
            log_.push_back(n == "LISP" ? "LISP <expression>: evaluates it"
                                       : "APPLOAD <file.lsp>: loads and runs it");
            return true;
        }
        std::string text = arg;
        if (n == "APPLOAD") {
            std::ifstream f(arg);
            if (!f) { log_.push_back("APPLOAD: could not open " + arg); return true; }
            std::ostringstream ss;
            ss << f.rdbuf();
            text = ss.str();
        }
        ensure_lisp();
        sync_lisp_vars(true);
        lisp_->clear_script();
        const gcmd::LispValue result = lisp_->run(text);
        for (const auto& line : lisp_->output()) log_.push_back(line);
        if (!lisp_->error().message.empty()) {
            log_.push_back("LISP: " + lisp_->error().message);
            return true;
        }
        if (!lisp_->script().empty()) {
            run_script(lisp_->script());
            lisp_->clear_script();
        } else {
            log_.push_back(result.print());
        }
        sync_lisp_vars(false);
        return true;
    }

    return false;
}

}
