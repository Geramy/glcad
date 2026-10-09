#include "ui/shell.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/document_file.hpp"
#include "exchange/dxf.hpp"
#include "exchange/dwg.hpp"
#include "exchange/dwg_write.hpp"
#include "exchange/dwf.hpp"
#include "exchange/mesh_export.hpp"
#include "exchange/pdf.hpp"
#include "exchange/plot_style.hpp"
#include "exchange/step.hpp"
#include "exchange/xt.hpp"
#include "geometry/measure.hpp"
#include "ui/doc_entities.hpp"
#include "ui/solids_file.hpp"

namespace gcapp {

namespace T = gtheme::token;

std::string lower_ext(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}
namespace {
bool slurp(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
bool spit(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}
}

void open_from_argv(Shell& shell, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg.empty() || arg.front() == '-') continue;
        shell.open_file(arg);
        shell.command("ZOOMEXTENTS");
        return;
    }
}

std::vector<Shell::FileFilter> Shell::file_filters(FileAsk kind) {
    if(kind==FileAsk::LoadLibrary||kind==FileAsk::SaveLibrary)
        return {{"Block collections",{"gllib","glcad","dwg","dxf"}}, {"GLCAD library",{"gllib"}},
            {"GLCAD document",{"glcad"}},{"DWG block library",{"dwg"}},{"DXF block library",{"dxf"}}};
    if (kind == FileAsk::ExportPdf) return {{"PDF", {"pdf"}}};
    if (kind == FileAsk::Attach) {
        return {{"Images", {"png", "jpg", "jpeg", "webp"}},
                {"Documents", {"pdf"}}};
    }
    const bool reading = kind == FileAsk::Open || kind == FileAsk::Import;
    std::vector<FileFilter> f;
    if (reading) {
        f.push_back({"All supported", {"glcad", "gllib", "dxf", "dwg", "pdf", "step", "stp", "x_t", "xmt_txt"}});
        f.push_back({"Drafting files", {"dxf", "dwg", "pdf"}});
        f.push_back({"3D CAD files", {"step", "stp", "x_t", "xmt_txt"}});
        f.push_back({"Meshes", {"stl", "obj"}});
    } else {
        f.push_back({"All supported", {"glcad", "dxf", "step", "stp", "x_t", "pdf", "dwfx", "stl", "3mf", "obj"}});
        f.push_back({"Drafting files", {"dxf", "pdf", "dwfx"}});
        f.push_back({"3D CAD files", {"step", "stp", "x_t", "xmt_txt"}});
        f.push_back({"Meshes", {"stl", "3mf", "obj"}});
    }
    f.push_back({"GLCAD document", {"glcad"}});
    f.push_back({"DXF", {"dxf"}});
    f.push_back({"DWG", {"dwg"}});
    f.push_back({"STEP", {"step", "stp"}});
    f.push_back({"Parasolid", {"x_t", "xmt_txt"}});
    f.push_back({"PDF", {"pdf"}});
    return f;
}

void Shell::ask_file(FileAsk kind) {
    if (!file_hook_) {
        if(kind==FileAsk::LoadLibrary||kind==FileAsk::SaveLibrary) {
            pending_args_=kind==FileAsk::LoadLibrary?"LIBRARYLOAD":"LIBRARYSAVE";
            pending_hint_="symbols.gllib";
            log_line(pending_args_+": type the collection path");
        } else log_.push_back("type the path, e.g. OPEN part.dxf");
        return;
    }
    const std::string origin=recovery_id_;
    request_file(kind, [this, kind, origin](std::string path) {
        if (path.empty()) return;
        if (kind!=FileAsk::Open && origin!=recovery_id_) {
            log_.push_back("The active document changed while choosing a file. Select the intended tab and try again.");
            return;
        }
        if (kind==FileAsk::Import && !require_edit_access()) return;
        switch (kind) {
            case FileAsk::Open:   open_file(path); break;
            case FileAsk::Save:   save_file(path); break;
            case FileAsk::Import: import_file(path); break;
            case FileAsk::Export: export_file(path); break;
            case FileAsk::ExportPdf: export_file(lower_ext(path)=="pdf" ? path : path+".pdf"); break;
            case FileAsk::Attach: break;
            case FileAsk::LoadLibrary: load_block_library(path); break;
            case FileAsk::SaveLibrary: save_block_library(std::filesystem::path(path).extension().empty()?path+".gllib":path); break;
        }
    });
}

namespace {
struct ImportShift {
    gcm::i64 lo_x{0}, lo_y{0}, hi_x{0}, hi_y{0};
    bool     any{false};
    gcm::i64 dx{0}, dy{0};
    void see(const gcm::Lattice2& p) {
        if (!any) { lo_x = hi_x = p.x; lo_y = hi_y = p.y; any = true; return; }
        lo_x = std::min(lo_x, p.x); hi_x = std::max(hi_x, p.x);
        lo_y = std::min(lo_y, p.y); hi_y = std::max(hi_y, p.y);
    }
    void see(const gsnap::Shape& sh) {
        see(sh.a);
        if (sh.is_curve()) { see({sh.a.x - sh.radius, sh.a.y - sh.radius}); see({sh.a.x + sh.radius, sh.a.y + sh.radius}); }
        else see(sh.b);
    }
    void settle() { if (any) { dx = -(lo_x / 2 + hi_x / 2); dy = -(lo_y / 2 + hi_y / 2); } }
    void apply(gcm::Lattice2& p) const { p.x += dx; p.y += dy; }
    void apply(gsnap::Shape& sh) const { apply(sh.a); apply(sh.b); }
    void apply(gdim::Dimension& d) const { d.transform_points([&](auto& point){apply(point);}); }
};
}

bool Shell::import_file(const std::string& path, bool recenter) {
    if(lower_ext(path)=="gllib")return load_block_library(path);
    touch_metadata("file");
    if(lower_ext(path)=="pdf")return import_pdf(path);
    std::string ext = lower_ext(path);
    const bool blank = document_blank();
    const bool centre = recenter && blank;
    const auto merge_layers = [&](const gdoc::LayerTable& incoming) {
        for (const auto& layer : incoming.all())
            if (blank || !layers_.find(layer.name)) layers_.add(layer);
    };
    const auto merge_text_style = [&](gtext::TextStyleDef style) {
        if(!style.font.empty() && std::filesystem::exists(std::filesystem::path(path).parent_path()/style.font))
            style.font=(std::filesystem::path(path).parent_path()/style.font).string();
        if(blank || !text_styles_.find(style.name))text_styles_.add(std::move(style));
    };
    const auto merge_dim_style = [&](const gdim::Style& style) {
        if(blank || !canvas_.dim_styles().find(style.name))canvas_.dim_styles().add(style);
    };
    if (ext == "dwg" || ext == "dxf") {
        std::string head;
        if (std::ifstream f(path, std::ios::binary); f) {
            head.resize(4096);
            f.read(head.data(), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<std::size_t>(f.gcount()));
        }
        if (head.rfind("AC10", 0) == 0) ext = "dwg";
        else if (head.find("SECTION") != std::string::npos || head.rfind("999", 0) == 0 || head.find("\n  0\n") != std::string::npos) ext = "dxf";
    }
    if (ext == "dxf") {
        std::string text;
        if (!slurp(path, text)) { log_.push_back("could not read " + path); return false; }
        const gdxf::ReadResult r = gdxf::read(text, vol_);
        if (!r.ok) { log_.push_back("DXF: " + r.error); return false; }
        ImportShift shift;
        if (centre) {
            for (const auto& e : r.document.entities) shift.see(e.shape);
            for (const auto& t : r.document.texts) shift.see(t.pos);
            for (const auto& ref : r.document.blocks.references()) shift.see(ref.insertion);
            shift.settle();
        }
        std::vector<gsnap::Shape> shapes = canvas_.shapes();
        for (const auto& e : r.document.entities) { shapes.push_back(e.shape); shapes.back().layer = e.layer; shift.apply(shapes.back()); }
        merge_layers(r.document.layers);
        for (const auto& lt : r.document.linetypes.all())
            if (blank || !linetypes_.find(lt.name)) linetypes_.add(lt);
        for (const auto& st : r.document.text_styles)merge_text_style(st);
        canvas_.set_text_styles(text_styles_);
        for (auto ds : r.document.dim_styles) {
            ds.unit = r.document.units == gdxf::Units::Millimetres ? gunit::Length::Millimetre
                    : r.document.units == gdxf::Units::Centimetres ? gunit::Length::Centimetre
                    : r.document.units == gdxf::Units::Inches ? gunit::Length::Inch
                    : r.document.units == gdxf::Units::Feet ? gunit::Length::Foot : gunit::Length::Metre;
            merge_dim_style(ds);
        }
        canvas_.load_shapes(std::move(shapes));
        for (const auto& def : r.document.blocks.all())
            if (!canvas_.blocks().find(def.name)) canvas_.blocks().define(def);
        for (gdoc::BlockRef ref : r.document.blocks.references()) { shift.apply(ref.insertion); canvas_.blocks().insert(ref); }
        for (const auto& t : r.document.texts) {
            gcm::Lattice2 pos = t.pos;
            shift.apply(pos);
            canvas_.add_text(pos, t.text, t.height, t.angle, int(t.halign), int(t.valign), t.width, t.style, t.layer, t.attrs);
        }
        {
            std::vector<gdim::Dimension> dims = r.document.dimensions;
            for (auto& d : dims) shift.apply(d);
            canvas_.add_dimensions(dims);
        }
        {
            std::vector<ggeo::NurbsCurve2<gcm::F64>> splines = r.document.splines;
            if (shift.any) {
                double ox, oy, sx, sy;
                gcm::to_world(vol_, gcm::Lattice2{0, 0}, ox, oy);
                gcm::to_world(vol_, gcm::Lattice2{shift.dx, shift.dy}, sx, sy);
                for (auto& c : splines)
                    for (auto& cp : c.control)
                        cp.p = ggeo::d2::Point<gcm::F64>::from(cp.p.x.to_f64() + (sx - ox), cp.p.y.to_f64() + (sy - oy));
            }
            canvas_.add_splines(splines);
        }
        std::size_t hatched = 0;
        for (const auto& h : r.document.hatches) {
            std::vector<gsnap::Shape> boundary = h.boundary;
            for (auto& b : boundary) shift.apply(b);
            gcm::Lattice2 seed = h.seed;
            shift.apply(seed);
            if (canvas_.add_hatch(boundary, seed, h.solid ? "SOLID" : h.pattern, h.scale, h.angle, h.layer, h.attrs)) ++hatched;
        }
        if (!r.unknown_kinds.empty()) {
            std::string what;
            for (const auto& [k, n] : r.unknown_kinds) what += (what.empty() ? "" : ", ") + std::to_string(n) + " " + k;
            log_.push_back("DXF: not imported: " + what);
        }
        if (r.junk_lines) log_.push_back("DXF: skipped " + std::to_string(r.junk_lines) + " unreadable line(s)");
        log_.push_back("imported " + std::to_string(r.document.entities.size()) + " entities" +
                       (r.document.texts.empty() ? std::string{} : ", " + std::to_string(r.document.texts.size()) + " notes") +
                       (r.document.dimensions.empty() ? std::string{} : ", " + std::to_string(r.document.dimensions.size()) + " dimensions") +
                       (r.document.splines.empty() ? std::string{} : ", " + std::to_string(r.document.splines.size()) + " splines") +
                       (hatched ? ", " + std::to_string(hatched) + " hatches" : std::string{}) + " from DXF" +
                       (r.unknown_entities ? " (" + std::to_string(r.unknown_entities) + " unknown skipped)" : ""));
        last_edit_was_solid_ = false;
        if (shift.any) { log_.push_back("centred on the origin"); run_command("ZOOMEXTENTS"); }
        return true;
    }
    if (ext == "dwg") {
        gdwg::ReadResult r = gdwg::read_file(path, vol_);
        if (!r.ok) { log_.push_back("DWG: " + r.error); return false; }
        leave_paper_space();
        const bool empty_model=canvas_.shapes().empty() && canvas_.texts().empty() && canvas_.blocks().references().empty();
        double model_factor=1;
        {
            struct Candidate { double factor; const char* name; gunit::Length unit; };
            static const Candidate kUnits[] = {
                {0.001, "mm", gunit::Length::Millimetre}, {0.01, "cm", gunit::Length::Centimetre},
                {0.0254, "in", gunit::Length::Inch}, {0.3048, "ft", gunit::Length::Foot},
                {1.0, "m", gunit::Length::Metre}};
            const int header_pick = r.insunits == 4 ? 0 : r.insunits == 5 ? 1
                                  : r.insunits == 1 ? 2 : r.insunits == 2 ? 3
                                  : r.insunits == 6 ? 4 : -1;
            int votes[5] = {0, 0, 0, 0, 0};
            for (const auto& d : r.dimensions) {
                if (!d.dim.has_override()) continue;
                const auto want = gunit::parse_length(d.dim.text_override, gunit::Length::Metre);
                if (!want || *want <= 0.0) continue;
                const double raw = gdim::measure(d.dim, vol_);
                if (raw <= 1e-12) continue;
                const double ratio = *want / raw;
                for (int u = 0; u < 5; ++u)
                    if (std::fabs(ratio / kUnits[u].factor - 1.0) < 0.03) ++votes[u];
            }
            int dim_pick = -1;
            for (int u = 0; u < 5; ++u)
                if (votes[u] > 0 && (dim_pick < 0 || votes[u] > votes[dim_pick])) dim_pick = u;
            int pick = header_pick;
            std::string how = "header $INSUNITS";
            if (dim_pick >= 0 && dim_pick != header_pick) {
                pick = dim_pick;
                how = header_pick >= 0
                          ? std::string("the file's dimensions (header said ") +
                                kUnits[header_pick].name + ")"
                          : "the file's dimensions";
            }
            if (pick >= 0) {
                model_factor=kUnits[pick].factor;
                canvas_.set_display_unit(kUnits[pick].unit);
                log_.push_back(std::string("units: ") + kUnits[pick].name + ", from " + how +
                               " -- DWGUNITS overrides");
            } else {
                log_.push_back("the file does not state its units; if distances read wrong, "
                               "DWGUNITS ft (or in, mm, cm) rescales the import");
            }
        }
        const auto paper_reference=[&](const std::string& name) {
            for(const auto& layout:r.layouts)
                for(const auto& ref:layout.references)if(ref.name==name)return true;
            return false;
        };
        if(model_factor!=1) {
            const auto point=[&](gcm::Lattice2& p) { double x,y; gcm::to_world(vol_,p,x,y); p=gcm::to_lattice(vol_,x*model_factor,y*model_factor); };
            const auto shape=[&](gsnap::Shape& sh) { point(sh.a); point(sh.b); sh.radius=gcm::i64(std::llround(double(sh.radius)*model_factor)); };
            const auto rescale_ref=[&](gdoc::BlockRef& ref) {
                point(ref.insertion);
                for(auto& note:ref.notes) { point(note.pos); note.height*=model_factor; note.width*=model_factor; }
            };
            for(auto& e:r.entities) shape(e.shape);
            for(auto& t:r.texts) { point(t.pos); t.height*=model_factor;t.width*=model_factor; }
            for(auto& d:r.dimensions) { d.dim.transform_points(point); d.dim.radius=gcm::i64(std::llround(double(d.dim.radius)*model_factor)); }
            for(auto& h:r.hatches) { point(h.seed); h.scale*=model_factor; for(auto& edge:h.boundary) shape(edge); }
            for(auto& curve:r.splines) for(auto& cp:curve.control) cp.p=ggeo::d2::Point<gcm::F64>::from(cp.p.x.to_f64()*model_factor,cp.p.y.to_f64()*model_factor);
            for(auto& u:r.underlays){point(u.insertion);u.scale*=model_factor;}
            for(auto def:r.blocks.all()) {
                point(def.base); for(auto& sh:def.geometry) shape(sh);
                for(auto& u:def.underlays){point(u.insertion);u.scale*=model_factor;}
                for(auto& ref:def.nested_refs)rescale_ref(ref);
                for(auto& note:def.notes) { point(note.pos); note.height*=model_factor; note.width*=model_factor; }
                for(auto& attr:def.attributes) { point(attr.position); attr.height*=model_factor; }
                r.blocks.define(std::move(def));
            }
            auto refs=r.blocks.references(); for(auto& ref:refs) rescale_ref(ref); r.blocks.set_references(std::move(refs));
            for(auto x:r.blocks.xrefs()) { if(!paper_reference(x.name))point(x.insertion); for(auto& p:x.clip) point(p); r.blocks.attach(std::move(x)); }
        }
        ImportShift shift;
        if (centre) {
            for (const auto& e : r.entities) shift.see(e.shape);
            for (const auto& t : r.texts) shift.see(t.pos);
            for(const auto& u:r.underlays)shift.see(u.insertion);
            for (const auto& ref : r.blocks.references()) shift.see(ref.insertion);
            for(const auto& curve:r.splines) for(const auto& cp:curve.control) shift.see(gcm::to_lattice(vol_,cp.p.x.to_f64(),cp.p.y.to_f64()));
            shift.settle();
        }
        const auto prepare_underlay=[&](gdoc::Underlay& u) {
            auto source=std::filesystem::path(u.path);
            if(source.is_relative())source=std::filesystem::path(path).parent_path()/source;
            u.path=source.lexically_normal().string();
            std::string error;std::vector<std::string> warnings;
            u.found=gplot::resolve_underlay(u,vol_,error,0,&warnings);
            for(const auto& warning:warnings)log_.push_back("DWG underlay: "+warning);
            if(!u.found)log_.push_back("DWG underlay retained: "+u.path+" ("+error+")");
        };
        for(auto& u:r.underlays) {
            prepare_underlay(u);shift.apply(u.insertion);
            const auto name=u.name;
            for(std::size_t suffix=2;canvas_.underlays().find(u.name);++suffix)
                u.name=name+" ("+std::to_string(suffix)+")";
            canvas_.underlays().attach(std::move(u));
        }
        for(auto def:r.blocks.all()) {
            for(auto& u:def.underlays)prepare_underlay(u);
            r.blocks.define(std::move(def));
        }
        std::vector<gsnap::Shape> shapes = canvas_.shapes();
        const auto first_imported_shape=shapes.size();
        for (const auto& e : r.entities) { shapes.push_back(e.shape); shapes.back().key=0; shapes.back().layer = e.layer; shift.apply(shapes.back()); }
        merge_layers(r.layers);
        for(const auto& style:r.text_styles)merge_text_style(style);
        for(const auto& style:r.dim_styles)merge_dim_style(style);
        canvas_.set_text_styles(text_styles_);
        canvas_.load_shapes(std::move(shapes));
        std::unordered_map<gcm::u32,gcm::u32> imported_shape_keys;
        for(std::size_t i=0;i<r.entities.size();++i)
            imported_shape_keys[r.entities[i].shape.key]=canvas_.shapes()[first_imported_shape+i].key;
        for(auto& rec:r.dimensions)for(auto* ref:{&rec.dim.o1,&rec.dim.o2})if(ref->bound()) {
            const auto key=imported_shape_keys.find(ref->shape);
            if(key==imported_shape_keys.end())*ref={};else ref->shape=key->second;
        }
        for (const auto& def : r.blocks.all())
            if (!canvas_.blocks().find(def.name)) canvas_.blocks().define(def);
        for (gdoc::BlockRef ref : r.blocks.references()) { shift.apply(ref.insertion); canvas_.blocks().insert(ref); }
        for (auto x : r.blocks.xrefs()) {
            if(!x.nested&&!paper_reference(x.name))shift.apply(x.insertion);
            canvas_.blocks().attach(std::move(x));
        }
        for (const auto& t : r.texts) {
            gcm::Lattice2 pos = t.pos;
            shift.apply(pos);
            canvas_.add_text(pos, t.text, t.height, t.angle, int(t.halign), int(t.valign),t.width,t.style,t.layer,t.attrs);
        }
        {
            std::vector<gdim::Dimension> dims;
            for (const auto& d : r.dimensions) { dims.push_back(d.dim); shift.apply(dims.back()); }
            canvas_.add_dimensions(dims);
        }
        {
            std::vector<ggeo::NurbsCurve2<gcm::F64>> splines = r.splines;
            if (shift.any) {
                double ox, oy, sx, sy;
                gcm::to_world(vol_, gcm::Lattice2{0, 0}, ox, oy);
                gcm::to_world(vol_, gcm::Lattice2{shift.dx, shift.dy}, sx, sy);
                for (auto& c : splines)
                    for (auto& cp : c.control)
                        cp.p = ggeo::d2::Point<gcm::F64>::from(cp.p.x.to_f64() + (sx - ox), cp.p.y.to_f64() + (sy - oy));
            }
            canvas_.add_splines(splines);
        }
        std::size_t hatched = 0;
        for (const auto& h : r.hatches) {
            std::vector<gsnap::Shape> boundary = h.boundary;
            for (auto& b : boundary) shift.apply(b);
            gcm::Lattice2 seed = h.seed;
            shift.apply(seed);
            if (canvas_.add_hatch(boundary, seed, h.solid ? "SOLID" : h.pattern, h.scale, h.angle, h.layer)) ++hatched;
        }
        log_.push_back("imported " + std::to_string(r.entities.size()) + " entities, " +
                       std::to_string(r.blocks.references().size()) + " block insertions, " +
                       std::to_string(r.blocks.xrefs().size()) + " external references, " +
                       std::to_string(r.texts.size()) + " notes" +
                       (r.dimensions.empty() ? std::string{} : ", " + std::to_string(r.dimensions.size()) + " dimensions") +
                       (hatched ? ", " + std::to_string(hatched) + " hatches" : std::string{}) +
                       " from DWG (" + std::string(gdwg::version_name(r.header.version)) + ")" +
                       (r.unknown_objects ? " (" + std::to_string(r.unknown_objects) + " objects not understood)" : ""));
        for (const auto& warning:r.skipped) log_.push_back("DWG: "+warning);
        if (shift.any) { log_.push_back("centred on the origin"); run_command("ZOOMEXTENTS"); }
        store_active_sketch(); layout_store();
        if(!r.layouts.empty()) {
            if(empty_model && layouts_.size()==1 && layouts_[0].shapes.empty() && layouts_[0].notes.empty() && layouts_[0].viewports.empty()) {
                space_states_.erase(layouts_[0].space_id); layouts_.clear(); active_layout_=-1;
            }
            const auto first=layouts_.size();
            double sx=0,sy=0; if(shift.any) { double ox,oy; gcm::to_world(vol_,{0,0},ox,oy); gcm::to_world(vol_,{shift.dx,shift.dy},sx,sy); sx-=ox; sy-=oy; }
            for(const auto& imported:r.layouts) {
                Layout layout; layout.name=imported.name; layout.paper=imported.paper_name;
                for(std::size_t suffix=2;std::any_of(layouts_.begin(),layouts_.end(),[&](const auto& l){return l.name==layout.name;});++suffix) layout.name=imported.name+" ("+std::to_string(suffix)+")";
                layout.custom_paper=true; layout.direct_model_view=false; layout.landscape=true;
                layout.imported_paper={layout.paper,imported.width,imported.height,imported.margin_left,imported.margin_bottom,imported.margin_right,imported.margin_top};
                layout.plot_style_name=imported.plot_style_table; layout.viewports=imported.viewports;
                for(auto& vp:layout.viewports) { vp.source_space=gcm::u32(std::max(0,active_sketch_)); vp.centre_x=vp.centre_x*model_factor+sx; vp.centre_y=vp.centre_y*model_factor+sy; vp.scale*=model_factor; }
                Canvas paper; paper.blocks()=canvas_.blocks(); paper.blocks().set_references(imported.references); paper.set_display_unit(canvas_.display_unit());
                std::vector<gsnap::Shape> geometry; for(const auto& e:imported.entities) geometry.push_back(e.shape);
                paper.load_shapes(std::move(geometry));
                for(auto u:imported.underlays){prepare_underlay(u);paper.underlays().attach(std::move(u));}
                for(const auto& t:imported.texts) paper.add_text(t.pos,t.text,t.height,t.angle,t.halign,t.valign,t.width,t.style,t.layer,t.attrs);
                paper.add_dimensions(imported.dimensions); paper.add_splines(imported.splines);
                for(const auto& h:imported.hatches) paper.add_hatch(h.boundary,h.seed,h.solid?"SOLID":h.pattern,h.scale,h.angle,h.layer,h.attrs);
                layout.shapes=paper.shapes(); layout.notes=paper.texts(); layouts_.push_back(std::move(layout)); ensure_space_ids();
                space_states_[layouts_.back().space_id]=paper.capture_space();
            }
            layout_load(int(first));
            if(r.active_layout>=0 && std::size_t(r.active_layout)<r.layouts.size()) select_layout(int(first)+r.active_layout);
            log_.push_back("imported "+std::to_string(r.layouts.size())+" paper layouts");
        }
        return true;
    }
    if (ext == "step" || ext == "stp") {
        std::string text;
        if (!slurp(path, text)) { log_.push_back("could not read " + path); return false; }
        const gstep::ReadResult r = gstep::read(text, vol_);
        if (!r.ok) { log_.push_back("STEP: " + r.error); return false; }
        for (const auto& b : r.bodies) modeller_.add_body(b, "imported");
        log_.push_back("imported " + std::to_string(r.bodies.size()) + " solid(s) from STEP");
        last_edit_was_solid_ = true;
        mesh_dirty_ = true;
        if (mode_ == Mode::Sketch && !r.bodies.empty()) set_mode(Mode::Solid);
        return true;
    }
    if (ext == "x_t" || ext == "xmt_txt") {
        const gxt::ReadResult r = gxt::read_file(path, vol_);
        if (!r.ok) { log_.push_back("Parasolid: " + r.error); return false; }
        for (const auto& b : r.bodies) modeller_.add_body(b, "imported");
        log_.push_back("imported " + std::to_string(r.bodies.size()) + " solid(s) from Parasolid (" + r.schema + ")" +
                       (r.skipped.empty() ? "" : ", " + std::to_string(r.skipped.size()) + " face(s) not rebuilt"));
        last_edit_was_solid_ = true;
        mesh_dirty_ = true;
        if (mode_ == Mode::Sketch && !r.bodies.empty()) set_mode(Mode::Solid);
        return true;
    }
    if (ext == "glcad") return open_file(path);
    log_.push_back("cannot import ." + ext + " (DXF, DWG, PDF, STEP, X_T, GLCAD)");
    return false;
}

std::map<std::string,std::string> Shell::sheet_fields(const Layout& l) const {
    const bool live=&l==active_layout();
    const auto paper=live ? plot_paper_ : l.paper;
    const auto den=live ? plot_scale_den_ : l.scale_den;
    const auto scale=den>0 ? scale_display(den) : "SCALE AS SHOWN";
    return {{"DOCUMENT",document_path_.empty() ? "UNTITLED" : file_title(document_path_)},
            {"SHEET",l.name},{"PAPER",paper},{"SCALE",scale},{"PAPERSCALE",paper+"  "+scale}};
}

gplot::PlotResult Shell::plot_sheet(gplot::PdfWriter& out,bool preview) const {
    const_cast<Shell*>(this)->store_active_sketch();
    gplot::Sheet sheet;
    sheet.setup.paper=layout_paper(active_layout());
    sheet.setup.orientation = plot_landscape_ ? gplot::Orientation::Landscape : gplot::Orientation::Portrait;
    sheet.setup.area = gplot::PlotArea::Extents;
    if (const Layout* la = active_layout()) {
        sheet.setup.area = la->area;
        if (la->area == gplot::PlotArea::Window) {
            sheet.setup.window_lo = gcm::to_lattice(vol_, la->win_lo_x, la->win_lo_y);
            sheet.setup.window_hi = gcm::to_lattice(vol_, la->win_hi_x, la->win_hi_y);
        }
    }
    sheet.setup.fit_to_paper = plot_scale_den_ <= 0;
    sheet.setup.offset_x = plot_dx_;
    sheet.setup.offset_y = plot_dy_;
    if (plot_scale_den_ > 0) {
        sheet.setup.scale_numerator = 1.0;
        sheet.setup.scale_denominator = double(plot_scale_den_);
    }
    sheet.setup.scale_family = canvas_.display_unit() == gunit::Length::Inch ||
                               canvas_.display_unit() == gunit::Length::Foot
                                   ? gplot::ScaleFamily::Imperial : gplot::ScaleFamily::Metric;
    const bool mono =
        active_layout() &&
        active_layout()->monochrome;
    gplot::PlotStyleTable styles =
        mono ? gplot::PlotStyleTable::monochrome() : gplot::PlotStyleTable{};
    if (const auto* layout=active_layout(); layout && !layout->plot_style_data.empty()) {
        auto parsed=gplot::read_plot_style(layout->plot_style_data);
        if(!parsed.ok) return {false,parsed.error};
        styles=std::move(parsed.table);
    }
    gplot::PlotJob job;
    job.sheet = &sheet;
    job.styles = &styles;
    job.layers = &layers_;
    job.linetypes = &linetypes_;
    job.linetype_scale = linetype_scale_;
    job.paper_linetype_scale=vars_.get_int("PSLTSCALE",1)!=0;
    job.volume = vol_;
    const PlotContent& pc = plot_content();
    job.content.push_back(pc.base);
    job.content.push_back(pc.marks);
    if(preview&&active_sketch_>=0&&std::size_t(active_sketch_)<sketches_.size())job.content[0].images=underlay_content(canvas_for_space(sketches_[active_sketch_].space_id),true);
    if (const Layout* l = active_layout()) {
        sheet.viewports = l->viewports;
        if (!l->direct_model_view || !sheet.viewports.empty()) {
            job.content.clear();
            for (std::size_t i=0;i<sheet.viewports.size();++i) {
                auto& vp=sheet.viewports[i];
                if (vp.source_space>=sketches_.size()) { vp.on=false; continue; }
                const Canvas source=canvas_for_space(sketches_[vp.source_space].space_id);
                auto content=build_plot_content(source,vp.scale);
                if(preview)content.base.images=underlay_content(source,true);
                vp.source_space=static_cast<gcm::u32>(i);
                content.base.space=content.marks.space=vp.source_space;
                job.content.push_back(std::move(content.base));
                job.content.push_back(std::move(content.marks));
            }
        }
        job.sheet_shapes = l->shapes;
        for (const auto& sh : l->shapes)
            job.sheet_layers.push_back(sh.layer.empty() ? "0" : sh.layer);
        for (std::size_t i = 0; i < l->view_shapes.size(); ++i) {
            job.sheet_shapes.push_back(l->view_shapes[i]);
            job.sheet_layers.push_back(i < l->view_layers.size() ? l->view_layers[i]
                                                                 : std::string("0"));
        }
        auto source=canvas_for_space(l->space_id);
        source.set_field_values(sheet_fields(*l));
        for (const auto& t : l->notes) for(const auto& lab:source.note_labels(t)) {
            for(const auto& triangle:Canvas::label_fills(lab,vol_)) {
                gplot::PlotJob::Fill fill;fill.layer="Titleblock";fill.attrs=lab.attrs;
                if(triangle.color.a>=0)fill.attrs.color=gdoc::Color::rgb(gcm::u8(std::lround(triangle.color.r*255)),gcm::u8(std::lround(triangle.color.g*255)),gcm::u8(std::lround(triangle.color.b*255)));
                for(int k=0;k<3;++k)fill.points.emplace_back(triangle.xy[2*k],triangle.xy[2*k+1]);
                job.sheet_fills.push_back(std::move(fill));
            }
            if(lab.box_width>0)continue;
            if(lab.stroke_font) {
                for(auto line:Canvas::label_strokes(lab,vol_)) {
                    gsnap::Shape sh;sh.a=gcm::to_lattice(vol_,line[0],line[1]);sh.b=gcm::to_lattice(vol_,line[2],line[3]);
                    job.sheet_shapes.push_back(sh);job.sheet_layers.push_back("Titleblock");
                }
                continue;
            }
            gplot::PlotJob::Note n;
            n.x=lab.x;n.y=lab.y;n.text=lab.text;n.height=lab.height;n.rotation=lab.angle;n.layer="Titleblock";
            n.invisible=bool(lab.outline_font);
            n.width_factor=lab.width_factor;n.oblique=lab.oblique;n.y_factor=lab.y_factor;n.attrs=lab.attrs;
            job.sheet_notes.push_back(std::move(n));
        }
    }
    if(const Layout* l=active_layout())job.sheet_images=underlay_content(canvas_for_space(l->space_id),preview);
    if (const Layout* l = active_layout()) append_sheet_annotations(*l, job);
    return gplot::plot(job, out);
}

bool Shell::export_file(const std::string& path) {
    store_active_sketch(); layout_store();
    const std::string ext = lower_ext(path);
    const Canvas model = in_paper_space_ && active_sketch_>=0
        ? canvas_for_space(sketches_[std::size_t(active_sketch_)].space_id) : canvas_;
    if(ext=="dwg"||ext=="dxf") {
        bool underlays=model.underlays().size()!=0;
        for(const auto& layout:layouts_)underlays=underlays||canvas_for_space(layout.space_id).underlays().size()!=0;
        if(underlays&&ext=="dxf"){log_.push_back("DXF export cannot preserve underlay references yet; save GLCAD or DWG");return false;}
    }
    const auto build_dxf = [&](const Canvas& source) {
        gdxf::Document d;
        d.layers = layers_;d.underlays=source.underlays().all();
        if(const auto* layout=active_layout()) d.plot_style_table=layout->plot_style_name;
        d.linetypes = linetypes_;
        d.text_styles = text_styles_.all();
        for (const auto& sh : source.shapes())
            d.entities.push_back({sh, sh.layer.empty() ? layers_.current() : sh.layer, ""});
        for (const auto& arrow : source.door_flow_shapes()) d.entities.push_back({arrow,arrow.layer,""});
        for (const auto& t : source.texts()) {
            gdxf::Text out;
            out.pos = gcm::Lattice2{t.pos.x, t.pos.y};
            out.text = source.resolved_text(t);
            out.height = t.height;
            out.angle = t.angle;
            out.layer = t.layer.empty() ? "0" : t.layer;
            out.halign = t.halign;
            out.valign = t.valign;
            out.width = t.width;
            out.style = t.style;
            out.attrs=t.attrs;
            d.texts.push_back(std::move(out));
        }
        d.dimensions = source.dimensions();
        d.dim_styles = source.dim_styles().all();
        if(auto* style=source.dim_styles().find(source.dim_styles().current())) { d.has_dim_style=true; d.dim_style=*style; }
        d.blocks = source.blocks();
        d.splines = source.splines();
        for (const auto& h : source.hatches()) {
            if (h.orphaned) continue;
            gdxf::HatchRec rec;
            rec.attrs = h.attrs;
            rec.pattern = h.style.name;
            rec.solid = h.style.name == "SOLID";
            rec.scale = h.style.scale;
            rec.angle = h.style.angle;
            rec.seed = h.seed;
            rec.layer = h.layer.empty() ? "0" : h.layer;
            rec.boundary=source.hatch_boundary(h);
            if (!rec.boundary.empty()) d.hatches.push_back(std::move(rec));
        }
        gdxf::WriteOptions o;
        o.units = source.display_unit() == gunit::Length::Millimetre ? gdxf::Units::Millimetres
                : source.display_unit() == gunit::Length::Inch ? gdxf::Units::Inches
                : source.display_unit() == gunit::Length::Foot ? gdxf::Units::Feet
                : source.display_unit() == gunit::Length::Centimetre ? gdxf::Units::Centimetres
                                                                       : gdxf::Units::Metres;
        return std::make_pair(std::move(d), o);
    };
    const auto has_regions=[](const gdxf::Document& d) {
        for(const auto& e:d.entities)if(e.shape.region)return true;
        for(const auto& layout:d.layouts)for(const auto& e:layout.entities)if(e.shape.region)return true;
        for(const auto& block:d.blocks.all())if(block.source_owner!="glcad.wall")
            for(const auto& shape:block.geometry)if(shape.region)return true;
        return false;
    };
    if (ext == "dxf") {
        auto [d, o] = build_dxf(model);
        const auto bytes=gdxf::write(d,vol_,o);
        if(bytes.empty()){log_.push_back("DXF export cannot represent this document");return false;}
        if (!spit(path, bytes)) { log_.push_back("could not write " + path); return false; }
        const auto insertions=std::count_if(d.blocks.references().begin(),d.blocks.references().end(),[&](const auto& ref) {
            const auto* block=d.blocks.find(ref.name);
            return !Canvas::is_wall_section(ref) && (!block || block->source_owner!="glcad.wall");
        });
        std::string msg = "exported " + std::to_string(d.entities.size()) +
                          " entities, " +
                          std::to_string(insertions) +
                          " insertions, " + std::to_string(d.texts.size()) +
                          " notes";
        if (!d.dimensions.empty())
            msg += ", " + std::to_string(d.dimensions.size()) + " dimensions";
        if (!d.hatches.empty())
            msg += ", " + std::to_string(d.hatches.size()) + " hatches";
        if (!d.splines.empty())
            msg += ", " + std::to_string(d.splines.size()) + " splines";
        const std::size_t skipped = model.hatches().size() - d.hatches.size();
        if (skipped)
            msg += " (" + std::to_string(skipped) + " hatch(es) had no boundary left)";
        log_.push_back(msg + " to DXF" + (has_regions(d) ? " (regions exported as editable boundaries)" : ""));
        return true;
    }
    if (ext == "dwg") {
        auto [d, options]=build_dxf(model);
        d.active_layout=selected_layout();
        for(const auto& layout:layouts_) {
            auto sheet=canvas_for_space(layout.space_id);
            sheet.set_field_values(sheet_fields(layout));
            auto [contents,ignored]=build_dxf(sheet);
            gdxf::Layout out; out.name=layout.name; out.paper_name=layout.paper; out.plot_style_table=layout.plot_style_name;
            {
                const auto paper=layout_paper(&layout);
                const auto p=layout.landscape?paper:gplot::rotated(paper);
                out.width=p.width; out.height=p.height;
                out.margin_left=p.margin_left; out.margin_bottom=p.margin_bottom; out.margin_right=p.margin_right; out.margin_top=p.margin_top;
            }
            out.entities=std::move(contents.entities); out.texts=std::move(contents.texts);out.underlays=std::move(contents.underlays);
            out.dimensions=std::move(contents.dimensions); out.splines=std::move(contents.splines); out.hatches=std::move(contents.hatches);
            out.references=contents.blocks.references(); out.viewports=layout.viewports;
            if(out.viewports.empty() && layout.direct_model_view) {
                const auto content=build_plot_content(model,1);
                if(content.have_box) {
                    double lx=content.lo_x,ly=content.lo_y,hx=content.hi_x,hy=content.hi_y;
                    if(layout.area==gplot::PlotArea::Window) { lx=layout.win_lo_x; ly=layout.win_lo_y; hx=layout.win_hi_x; hy=layout.win_hi_y; }
                    gplot::PageSetup setup; setup.paper=layout_paper(&layout); setup.orientation=layout.landscape?gplot::Orientation::Landscape:gplot::Orientation::Portrait;
                    setup.scale_family=model.display_unit()==gunit::Length::Inch || model.display_unit()==gunit::Length::Foot?gplot::ScaleFamily::Imperial:gplot::ScaleFamily::Metric;
                    double den=layout.scale_den;
                    if(den<=0) {
                        den=setup.fitted_ratio(hx-lx,hy-ly);
                        if(const auto* scale=gplot::nearest_standard_scale(den,setup.scale_family)) den=scale->drawing/scale->paper;
                    }
                    gplot::Viewport vp; vp.source_space=gcm::u32(std::max(0,active_sketch_)); vp.lo={out.margin_left,out.margin_bottom}; vp.hi={out.width-out.margin_right,out.height-out.margin_top};
                    vp.scale=den>0?den:1; vp.centre_x=(lx+hx)*.5-layout.dx*vp.scale; vp.centre_y=(ly+hy)*.5-layout.dy*vp.scale;
                    out.viewports.push_back(vp);
                }
            }
            for(const auto& vp:out.viewports) if(vp.source_space!=gcm::u32(std::max(0,active_sketch_))) {
                log_.push_back("DWG export: a viewport references another model sketch; export that sketch separately"); return false;
            }
            for(const auto& sh:layout.view_shapes) out.entities.push_back({sh,sh.layer,""});
            d.layouts.push_back(std::move(out));
        }
        gdwg::WriteOptions o; o.units=options.units; o.quantize_true_color=true;
        std::string error;
        if (!gdwg::write_file(path,d,vol_,o,&error)) { log_.push_back(error); return false; }
        log_.push_back("Exported AutoCAD 2000 DWG: "+path+" (true colours use the nearest ACI palette colour"+
            (has_regions(d) ? "; regions exported as editable boundaries)" : ")"));
        return true;
    }
    if (ext == "step" || ext == "stp") {
        const auto& bodies = modeller_.bodies();
        if (bodies.empty()) { log_.push_back("no solid to export"); return false; }
        const std::size_t which = (sel_body_ >= 0 && sel_body_ < static_cast<int>(bodies.size()))
                                  ? std::size_t(sel_body_)
                                  : (sel_target_.valid && sel_target_.on_body) ? sel_target_.body : 0;
        if (!spit(path, gstep::write(bodies[which], vol_))) { log_.push_back("could not write " + path); return false; }
        log_.push_back(bodies.size() > 1
                       ? "exported solid " + std::to_string(which + 1) + " of " + std::to_string(bodies.size()) + " to STEP (one solid per file)"
                       : "exported the solid to STEP");
        return true;
    }
    if (ext == "x_t" || ext == "xmt_txt") {
        const auto& bodies = modeller_.bodies();
        if (bodies.empty()) { log_.push_back("no solid to export"); return false; }
        if (!spit(path, gxt::write(bodies, vol_))) { log_.push_back("could not write " + path); return false; }
        log_.push_back("exported " + std::to_string(bodies.size()) + " solid(s) to Parasolid X_T");
        return true;
    }
    if (ext == "pdf") {
        gplot::PdfWriter out;
        out.set_title(document_path_.empty() ? "GLCAD drawing" : document_path_);
        out.set_creator("GLCAD");
        const gplot::PlotResult r = plot_sheet(out);
        if (!r.ok) { log_.push_back("PDF: " + r.message); return false; }
        if (!out.write_file(path)) { log_.push_back("could not write " + path); return false; }
        log_.push_back("plotted " + std::to_string(r.paths) + " paths to PDF at " + r.scale_name +
                       (r.texts ? " with " + std::to_string(r.texts) + " notes" : std::string{}));
        return true;
    }
    if (ext == "dwf" || ext == "dwfx") {
        gplot::PdfWriter page;
        const auto r=plot_sheet(page);
        gplot::DwfWriter out;
        out.set_font(font_.source_bytes());
        const auto [width, height] = page.page_size(0);
        out.begin_page(active_layout() ? active_layout()->name : "Layout1", width, height);
        for (const auto& p:page.paths_of_page(0)) { out.declare_layer(p.layer); out.add(p); }
        for (const auto& t:page.texts_of_page(0)) { out.declare_layer(t.layer); out.add(t); }
        for (const auto& image:page.images_of_page(0)) out.add(image);
        out.end_page();
        if (!r.ok) { log_.push_back("DWF: " + r.message); return false; }
        if (!out.write_file(path)) { log_.push_back("could not write " + path); return false; }
        log_.push_back("published " + std::to_string(r.paths) + " paths to DWFx: " + path);
        return true;
    }
    if (ext == "stl" || ext == "3mf") {
        const auto& bodies = modeller_.bodies();
        const auto r = ext == "stl" ? gmesh::write_stl(bodies, vol_) : gmesh::write_3mf(bodies, vol_);
        if (!r.ok) { log_.push_back(r.error); return false; }
        if (!spit(path, std::string(r.bytes.begin(), r.bytes.end()))) { log_.push_back("could not write " + path); return false; }
        log_.push_back("exported " + std::to_string(r.triangles) + " triangle(s) from " +
                       std::to_string(r.objects) + " solid(s) to " + path + " (millimetres)");
        return true;
    }
    if (ext == "obj") {
        const auto& bodies = modeller_.bodies();
        if (bodies.empty()) { log_.push_back("no solid to export"); return false; }
        std::string text = "# GLCAD, units: millimetres\n";
        std::size_t base = 1, tris = 0;
        const auto objects = gmesh::mesh(bodies, vol_);
        for (const auto& o : objects) {
            text += "o " + o.name + "\n";
            for (std::size_t i = 0; i < o.vertex_count(); ++i) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "v %.10g %.10g %.10g\n",
                              o.vertices[3 * i], o.vertices[3 * i + 1], o.vertices[3 * i + 2]);
                text += buf;
            }
            for (std::size_t t = 0; t + 2 < o.triangles.size(); t += 3) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "f %zu %zu %zu\n", base + o.triangles[t],
                              base + o.triangles[t + 1], base + o.triangles[t + 2]);
                text += buf;
                ++tris;
            }
            base += o.vertex_count();
        }
        if (!spit(path, text)) { log_.push_back("could not write " + path); return false; }
        log_.push_back("exported " + std::to_string(tris) + " triangle(s) from " +
                       std::to_string(objects.size()) + " solid(s) to " + path + " (millimetres)");
        return true;
    }
    if (ext == "glcad") return save_file(path);
    log_.push_back("cannot export ." + ext + " (DXF, DWF, STEP, STL, 3MF, OBJ, PDF, GLCAD)");
    return false;
}

static std::string document_stamp(const std::string& path) {
    std::error_code ec;
    const auto size=std::filesystem::file_size(path,ec); if(ec) return "missing";
    const auto time=std::filesystem::last_write_time(path,ec); if(ec) return "unreadable";
    return std::to_string(size)+":"+std::to_string(static_cast<long long>(time.time_since_epoch().count()));
}

bool Shell::save_file(const std::string& path) {
    if (lower_ext(path) != "glcad") return export_file(path);
    const bool same_target=!document_path_.empty() && DocumentLock::identity(path)==DocumentLock::identity(document_path_);
    if(same_target) refresh_document_lock();
    std::unique_ptr<DocumentLock> destination_lock;
    std::string holder;
    if(!same_target) destination_lock=DocumentLock::acquire(path,holder);
    if((same_target && read_only_) || (!same_target && !destination_lock)) {
        log_.push_back("save refused: "+(same_target?lock_holder_:holder)+" -- SAVEAS writes your copy elsewhere");
        return false;
    }
    cached_block_revision_ = ~0ull;
    flush_edits();
    const std::string previous_recovery = autosave_path();
    const gfile::Contents c = document_contents();
    const gfile::Error e = gfile::save_file(DocumentLock::identity(path), c);
    if (!e.ok) { log_.push_back("save failed: " + e.message); return false; }
    if(destination_lock) document_lock_=std::move(destination_lock);
    document_stamp_=document_stamp(path); external_change_=false;
    document_path_ = path;
    tab_title_ = file_title(path);
    invalidate_frame_caches();
    saved_serial_ = document_serial();
    read_only_ = false;
    lock_holder_.clear();
    {
        std::error_code ec;
        std::filesystem::remove(previous_recovery, ec);
        std::filesystem::remove(autosave_path(), ec);
        if (!recovered_from_.empty()) std::filesystem::remove(recovered_from_, ec);
        recovered_from_.clear();
        recovery_base_ = gfile::Contents{};
        recovery_written_ = false;
    }
    log_.push_back("saved " + path + ": " + std::to_string(c.shape_count()) + " objects, " +
                   std::to_string(modeller_.bodies().size()) + " solid(s)");
    return true;
}

bool Shell::prepare_template(gfile::Contents& c) {
    Shell staged;
    if (!staged.load_contents(c, {})) {
        log_.push_back("TEMPLATE: invalid document content");
        return false;
    }
    staged.store_active_sketch();
    const auto materials = staged.modeller_.materials();
    staged.modeller_ = Modeller(staged.vol_);
    auto model = staged.modeller_.metadata();
    model.materials = materials;
    staged.modeller_.load_metadata(model);
    staged.pmi_.clear();
    staged.groups_.clear();
    staged.views_ = gview::ViewTable{};
    staged.section_nx_ = staged.section_ny_ = staged.section_nz_ = staged.section_d_ = 0;

    staged.canvas_.blocks() = gfile::template_block_definitions(staged.canvas_.blocks());
    for (const auto& sk : staged.sketches_) staged.space_states_.erase(sk.space_id);
    staged.sketches_.assign(1, SketchDoc{});
    staged.sketches_[0].name = "Sketch 1";
    staged.sketches_[0].plane = gtopo::SketchPlane::world_xy();
    staged.active_sketch_ = 0;
    staged.in_paper_space_ = false;
    staged.ensure_space_ids();
    staged.space_states_[staged.sketches_[0].space_id] = Canvas::SpaceState{};
    for (auto& l : staged.layouts_) {
        for (auto& vp : l.viewports) vp.source_space = 0;
        l.view_body = -1; l.views = gdraft::ViewSet{}; l.view_shapes.clear(); l.view_layers.clear();
        l.sections.clear(); l.details.clear();
        auto& state = staged.space_states_[l.space_id];
        auto& refs = state.content.refs;
        refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const auto& r) {
            return staged.canvas_.blocks().find(r.name) == nullptr;
        }), refs.end());
        state.undo.clear(); state.redo.clear();
        state.content.selection.clear(); state.content.sel_refs.clear(); state.content.sel_texts.clear();
    }
    staged.load_active_space();
    staged.cached_block_revision_ = ~0ull;
    c = staged.document_contents();
    return true;
}

gfile::Contents Shell::document_contents() const {
    const_cast<Shell*>(this)->store_active_sketch();
    gfile::Contents c;
    c.volume = vol_;
    c.units = gunit::name(canvas_.display_unit());
    c.precision = precision_;
    c.layers = layers_;
    for (std::size_t i = 0; i < sketches_.size(); ++i) {
        const SketchDoc& sk = sketches_[i];
        gdoc::Space sp;
        sp.id = static_cast<gdoc::SpaceId>(i);
        sp.kind = i == 0 ? gdoc::SpaceKind::Model : gdoc::SpaceKind::Sketch;
        sp.name = sk.name;
        sp.frame.origin = gview::Point3::from(sk.plane.ox, sk.plane.oy, sk.plane.oz);
        sp.frame.u = gview::Vec3::from(sk.plane.ux, sk.plane.uy, sk.plane.uz);
        sp.frame.v = gview::Vec3::from(sk.plane.vx, sk.plane.vy, sk.plane.vz);
        sp.frame.n = gview::Vec3::from(sk.plane.nx, sk.plane.ny, sk.plane.nz);
        c.spaces.push_back(sp);
        gfile::Contents::SpaceGeometry g;
        g.space = static_cast<gcm::u32>(i);
        g.shapes = (static_cast<int>(i) == active_sketch_ && !in_paper_space_)
                       ? canvas_.shapes()
                       : sk.shapes;
        g.layers.clear();
        for (const auto& sh : g.shapes) g.layers.push_back(sh.layer.empty() ? layers_.current() : sh.layer);
        c.geometry.push_back(std::move(g));
    }
    if (!modeller_.bodies().empty()) {
        c.foreign.push_back({kSolidsFrame, encode_solids(modeller_.bodies())});
        const std::string h = encode_feature_history(modeller_.histories());
        if (!h.empty()) c.foreign.push_back({kFeaturesFrame, std::vector<gcm::u8>(h.begin(), h.end())});
    }
    {
        std::string t;
        for (std::size_t b = 0; b < modeller_.bodies().size(); ++b) t += "B " + std::to_string(b) + " " + modeller_.body_layer(b) + "\n";
        for (std::size_t k = 0; k < sketches_.size(); ++k) t += "S " + std::to_string(k) + " " + sketches_[k].layer + "\n";
        for (std::size_t k=0; k<sketches_.size(); ++k)
            t += "V " + std::to_string(k) + " " + std::to_string(int(sketches_[k].visibility)) + " " + std::to_string(sketches_[k].parent_body_key) + "\n";
        for (std::size_t k=0; k<sketches_.size(); ++k)
            if (sketches_[k].on_face)
                t += "F " + std::to_string(k) + " " + std::to_string(sketches_[k].body) + " " + std::to_string(sketches_[k].face) + "\n";
        if (!t.empty()) c.foreign.push_back({kLayersFrame, std::vector<gcm::u8>(t.begin(), t.end())});
    }
    {
        std::string t;
        for (std::size_t i = 0; i < sketches_.size(); ++i) {
            const auto& texts =
                (static_cast<int>(i) == active_sketch_ && !in_paper_space_) ? canvas_.texts()
                : sketches_[i].texts;
            for (const auto& x : texts) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "%zu %lld %lld %.17g %.17g %d %d %.17g ", i,
                              static_cast<long long>(x.pos.x), static_cast<long long>(x.pos.y),
                              x.height, x.angle, x.halign, x.valign, x.width);
                t += buf;
                for (char ch : x.text) t += ch == '\n' ? ' ' : ch;
                t += '\n';
            }
        }
        if (!t.empty()) c.foreign.push_back({kTextsFrame, std::vector<gcm::u8>(t.begin(), t.end())});
    }
    c.blocks = canvas_.blocks();
    const auto& ground = space_states_.at(sketches_[0].space_id).content;
    c.blocks.set_references(ground.refs);
    append_entities(ground.dims, ground.hatches, ground.splines, ground.constraints, c.entities);

    gfile::Contents paper;
    for (std::size_t i = 0; i < layouts_.size(); ++i) {
        const auto space = static_cast<gcm::u32>(i);
        gfile::Contents::SpaceGeometry g;
        g.space = space;
        g.shapes = layouts_[i].shapes;
        for (const auto& sh : g.shapes) g.layers.push_back(sh.layer);
        paper.geometry.push_back(std::move(g));
        append_text_entities(layouts_[i].notes, space, paper.entities);
    }
    if (!layouts_.empty()) c.foreign.push_back({kPaperContentFrame, gfile::encode(paper)});
    gfile::Contents notes;
    for (std::size_t i = 0; i < sketches_.size(); ++i)
        append_text_entities(sketches_[i].texts, static_cast<gcm::u32>(i), notes.entities);
    c.foreign.push_back({kModelNotesFrame, gfile::encode(notes)});

    if (!layouts_.empty()) {
        std::string t;
        for (std::size_t i = 0; i < layouts_.size(); ++i) {
            const Layout& l = layouts_[i];
            const bool live = int(i) == active_layout_;
            char buf[512];
            std::snprintf(buf, sizeof(buf), "L %d %.17g %.17g %.17g %s|%s\n",
                          live ? plot_scale_den_ : l.scale_den,
                          double(live ? (plot_landscape_ ? 1 : 0) : (l.landscape ? 1 : 0)),
                          live ? plot_dx_ : l.dx, live ? plot_dy_ : l.dy,
                          (live ? plot_paper_ : l.paper).c_str(), l.name.c_str());
            t += buf;
            for (const auto& sh : l.shapes) {
                std::snprintf(buf, sizeof(buf), "S %lld %lld %lld %lld %s\n",
                              static_cast<long long>(sh.a.x), static_cast<long long>(sh.a.y),
                              static_cast<long long>(sh.b.x), static_cast<long long>(sh.b.y),
                              sh.layer.empty() ? "0" : sh.layer.c_str());
                t += buf;
            }
            std::snprintf(buf, sizeof(buf), "A %d %.17g %.17g %.17g %.17g %d\n",
                          static_cast<int>(l.area), l.win_lo_x, l.win_lo_y,
                          l.win_hi_x, l.win_hi_y, l.monochrome ? 1 : 0);
            t += buf;
            if (l.view_body >= 0) {
                std::snprintf(buf, sizeof(buf), "W %d %.17g %d\n", l.view_body,
                              l.view_scale, static_cast<int>(l.view_layout));
                t += buf;
                for (const auto& sd : l.sections) {
                    std::snprintf(buf, sizeof(buf), "C %u %.17g %.17g %.17g %.17g %s\n",
                                  sd.parent, sd.line.x0, sd.line.y0, sd.line.x1,
                                  sd.line.y1, sd.line.label.c_str());
                    t += buf;
                }
                for (const auto& dd : l.details) {
                    std::snprintf(buf, sizeof(buf), "D %u %.17g %.17g %.17g %.17g %s\n",
                                  dd.parent, dd.circle.x, dd.circle.y, dd.circle.radius,
                                  dd.scale, dd.circle.label.c_str());
                    t += buf;
                }
            }
            for (const auto& vp : l.viewports) {
                std::snprintf(buf, sizeof(buf), "V %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
                              vp.lo.x, vp.lo.y, vp.hi.x, vp.hi.y,
                              vp.centre_x, vp.centre_y, vp.scale);
                t += buf;
            }
            for (const auto& nt : l.notes) {
                std::snprintf(buf, sizeof(buf), "T %lld %lld %.17g ",
                              static_cast<long long>(nt.pos.x),
                              static_cast<long long>(nt.pos.y), nt.height);
                t += buf;
                for (char ch : nt.text) t += ch == '\n' ? ' ' : ch;
                t += '\n';
            }
        }
        for (const auto& p : page_setups_) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "P %d %d %d %s|%s\n", p.scale_den,
                          p.landscape ? 1 : 0, static_cast<int>(p.area),
                          p.paper.c_str(), p.name.c_str());
            t += buf;
        }
        c.foreign.push_back({kLayoutsFrame, std::vector<gcm::u8>(t.begin(), t.end())});
    }
    c.linetypes = linetypes_;
    c.multilines = canvas_.multilines();
    c.foreign.push_back({kMetadataFrame, metadata_bytes()});
    return c;
}

void Shell::refresh_document_lock() {
    if(document_path_.empty()||lower_ext(document_path_)!="glcad") return;
    if(!document_stamp_.empty()&&document_stamp_!=document_stamp(document_path_)) {
        if(!external_change_) log_.push_back("Drawing changed on disk; reopen it to load the changes, or SAVEAS to keep your edits separately");
        external_change_=true;
    }
    if(external_change_) { read_only_=true; lock_holder_="the drawing has changed on disk"; return; }
    if(document_lock_ && document_lock_->owns(document_path_)) return;
    document_lock_.reset();
    document_lock_=DocumentLock::acquire(document_path_,lock_holder_);
    read_only_=!document_lock_;
    if(document_lock_) { std::ofstream probe(document_path_,std::ios::app); if(!probe) { read_only_=true; lock_holder_="the drawing is not writable"; } }
}

bool Shell::open_file(const std::string& path) {
    if(lower_ext(path)=="gllib")return load_block_library(path);
    touch_metadata("file");
    if(lower_ext(path)!="glcad") return import_file(path);
    std::string holder;
    auto lease=DocumentLock::acquire(path,holder);
    gfile::Contents c;
    const auto before=document_stamp(path);
    const gfile::Error e=gfile::load_file(path,c);
    if(!e.ok) { log_.push_back("open failed: "+e.message); return false; }
    bool protected_file=!lease;
    { std::ofstream probe(path,std::ios::app); if(!probe) protected_file=true; }
    if(!load_contents(c,path,protected_file,std::move(holder))) return false;
    document_lock_=std::move(lease);
    document_stamp_=before;
    if(before!=document_stamp(path)) { external_change_=true; read_only_=true; lock_holder_="the drawing changed while opening"; }
    return true;
}

bool Shell::load_contents(const gfile::Contents& c, const std::string& path,
                          bool opened_read_only, std::string opened_lock_holder) {
    const auto valid_regions = gfile::validate_geometry(c);
    if (!valid_regions.ok) { log_.push_back("open failed: " + valid_regions.message); return false; }
    for (const auto& frame : c.foreign) {
        if (frame.kind != kPaperContentFrame && frame.kind != kModelNotesFrame) continue;
        gfile::Contents content;
        auto error = gfile::decode(frame.payload, content);
        if (error.ok) error = gfile::validate_geometry(content);
        if (!error.ok) {
            log_.push_back("open failed: invalid sheet/note content: " + error.message);
            return false;
        }
    }
    for (const auto& frame : c.foreign)
        if (frame.kind == kMetadataFrame && !restore_metadata(frame.payload, true, &c)) {
            log_.push_back("open failed: invalid document metadata");
            return false;
        }
    flush_edits();
    if (!document_blank()) new_tab();
    else reset_document();
    read_only_ = opened_read_only;
    lock_holder_ = std::move(opened_lock_holder);
    document_stamp_=path.empty()?std::string{}:document_stamp(path); external_change_=false;
    precision_ = c.precision;
    auto unit = gunit::length_from_suffix(c.units);
    for (int i = int(gunit::Length::Metre); i <= int(gunit::Length::Mil); ++i)
        if (c.units == gunit::name(gunit::Length(i))) unit = gunit::Length(i);
    if (unit) canvas_.set_display_unit(*unit);
    layers_ = c.layers;
    linetypes_ = c.linetypes;
    canvas_.multilines() = c.multilines;
    sketches_.clear();
    for (std::size_t i = 0; i < c.spaces.size(); ++i) {
        SketchDoc sk;
        sk.name = c.spaces[i].name.empty() ? "Sketch " + std::to_string(i + 1) : c.spaces[i].name;
        const auto& f = c.spaces[i].frame;
        sk.plane.ox = f.origin.x.to_f64(); sk.plane.oy = f.origin.y.to_f64(); sk.plane.oz = f.origin.z.to_f64();
        sk.plane.ux = f.u.x.to_f64(); sk.plane.uy = f.u.y.to_f64(); sk.plane.uz = f.u.z.to_f64();
        sk.plane.vx = f.v.x.to_f64(); sk.plane.vy = f.v.y.to_f64(); sk.plane.vz = f.v.z.to_f64();
        sk.plane.nx = f.n.x.to_f64(); sk.plane.ny = f.n.y.to_f64(); sk.plane.nz = f.n.z.to_f64();
        for (const auto& g : c.geometry)
            if (g.space == i) {
                sk.shapes = g.shapes;
                for (std::size_t k = 0; k < sk.shapes.size() && k < g.layers.size(); ++k)
                    sk.shapes[k].layer = g.layers[k];
            }
        tessellate_sketch(sk);
        sketches_.push_back(std::move(sk));
    }
    if (sketches_.empty()) {
        SketchDoc ground;
        ground.name = "Sketch 1";
        ground.plane = gtopo::SketchPlane::world_xy();
        sketches_.push_back(ground);
    }
    canvas_.blocks() = c.blocks;
    cached_block_revision_ = ~0ull;
    {
        std::vector<gdim::Dimension> dims;
        std::vector<gdoc::AssociativeHatch> hatches;
        std::vector<ggeo::NurbsCurve2<Scalar>> splines;
        std::vector<Canvas::StoredConstraint> constraints;
        read_entities(c.entities, dims, hatches, splines, constraints);
        canvas_.load_dimensions(std::move(dims));
        canvas_.load_hatches(std::move(hatches));
        canvas_.load_splines(std::move(splines));
        canvas_.load_constraints(std::move(constraints));
    }
    std::vector<gtopo::Body> solids;
    for (const auto& f : c.foreign) {
        if (f.kind == kSolidsFrame && !decode_solids(f.payload, solids))
            log_.push_back("the solids in this file could not be read");
        if (f.kind == kLayoutsFrame) {
            layouts_.clear();
            page_setups_.clear();
            active_layout_ = -1;
            const std::string t(f.payload.begin(), f.payload.end());
            std::istringstream in(t);
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) continue;
                if (line[0] == 'L') {
                    Layout l;
                    int den = 0;
                    double landscape = 1.0, dx = 0.0, dy = 0.0;
                    int consumed = 0;
                    if (std::sscanf(line.c_str(), "L %d %lf %lf %lf %n",
                                    &den, &landscape, &dx, &dy, &consumed) < 4)
                        continue;
                    l.scale_den = den;
                    l.landscape = landscape != 0.0;
                    l.dx = dx;
                    l.dy = dy;
                    const std::string rest = line.substr(std::size_t(consumed));
                    const std::size_t bar = rest.find('|');
                    if (bar == std::string::npos) continue;
                    l.paper = rest.substr(0, bar);
                    l.name = rest.substr(bar + 1);
                    layouts_.push_back(std::move(l));
                } else if (line[0] == 'S' && !layouts_.empty()) {
                    long long ax, ay, bx, by;
                    int consumed = 0;
                    if (std::sscanf(line.c_str(), "S %lld %lld %lld %lld %n",
                                    &ax, &ay, &bx, &by, &consumed) < 4) continue;
                    gsnap::Shape sh;
                    sh.kind = gsnap::ShapeKind::Segment;
                    sh.a.x = static_cast<decltype(sh.a.x)>(ax);
                    sh.a.y = static_cast<decltype(sh.a.y)>(ay);
                    sh.b.x = static_cast<decltype(sh.b.x)>(bx);
                    sh.b.y = static_cast<decltype(sh.b.y)>(by);
                    sh.layer = line.substr(std::size_t(consumed));
                    layouts_.back().shapes.push_back(std::move(sh));
                } else if (line[0] == 'W' && !layouts_.empty()) {
                    int body = -1, lay = 0;
                    double sc = 1.0;
                    if (std::sscanf(line.c_str(), "W %d %lf %d", &body, &sc, &lay) < 3) continue;
                    Layout& l = layouts_.back();
                    l.view_body = body;
                    l.view_scale = sc;
                    l.view_layout = static_cast<gdraft::Layout>(lay);
                } else if (line[0] == 'C' && !layouts_.empty()) {
                    Layout::SectionDef sd;
                    char label[32] = {0};
                    if (std::sscanf(line.c_str(), "C %u %lf %lf %lf %lf %31s", &sd.parent,
                                    &sd.line.x0, &sd.line.y0, &sd.line.x1, &sd.line.y1,
                                    label) < 5) continue;
                    if (label[0]) sd.line.label = label;
                    layouts_.back().sections.push_back(std::move(sd));
                } else if (line[0] == 'D' && !layouts_.empty()) {
                    Layout::DetailDef dd;
                    char label[32] = {0};
                    if (std::sscanf(line.c_str(), "D %u %lf %lf %lf %lf %31s", &dd.parent,
                                    &dd.circle.x, &dd.circle.y, &dd.circle.radius,
                                    &dd.scale, label) < 5) continue;
                    if (label[0]) dd.circle.label = label;
                    layouts_.back().details.push_back(std::move(dd));
                } else if (line[0] == 'A' && !layouts_.empty()) {
                    int area = 1, mono = 0;
                    double lx = 0, ly = 0, hx = 0, hy = 0;
                    if (std::sscanf(line.c_str(), "A %d %lf %lf %lf %lf %d",
                                    &area, &lx, &ly, &hx, &hy, &mono) < 5) continue;
                    Layout& l = layouts_.back();
                    l.area = static_cast<gplot::PlotArea>(area);
                    l.monochrome = mono != 0;
                    l.win_lo_x = lx; l.win_lo_y = ly;
                    l.win_hi_x = hx; l.win_hi_y = hy;
                } else if (line[0] == 'P') {
                    PageSetupNamed p;
                    int den = 0, land = 1, area = 1, consumed = 0;
                    if (std::sscanf(line.c_str(), "P %d %d %d %n",
                                    &den, &land, &area, &consumed) < 3) continue;
                    p.scale_den = den;
                    p.landscape = land != 0;
                    p.area = static_cast<gplot::PlotArea>(area);
                    const std::string rest = line.substr(std::size_t(consumed));
                    const std::size_t bar = rest.find('|');
                    if (bar == std::string::npos) continue;
                    p.paper = rest.substr(0, bar);
                    p.name = rest.substr(bar + 1);
                    page_setups_.push_back(std::move(p));
                } else if (line[0] == 'V' && !layouts_.empty()) {
                    gplot::Viewport vp;
                    if (std::sscanf(line.c_str(), "V %lf %lf %lf %lf %lf %lf %lf",
                                    &vp.lo.x, &vp.lo.y, &vp.hi.x, &vp.hi.y,
                                    &vp.centre_x, &vp.centre_y, &vp.scale) < 7)
                        continue;
                    layouts_.back().viewports.push_back(vp);
                } else if (line[0] == 'T' && !layouts_.empty()) {
                    long long tx, ty;
                    double h = 0.0;
                    int consumed = 0;
                    if (std::sscanf(line.c_str(), "T %lld %lld %lf %n",
                                    &tx, &ty, &h, &consumed) < 3) continue;
                    Canvas::TextItem nt;
                    nt.pos.x = static_cast<decltype(nt.pos.x)>(tx);
                    nt.pos.y = static_cast<decltype(nt.pos.y)>(ty);
                    nt.height = h;
                    nt.halign = 0;
                    nt.valign = 1;
                    nt.text = line.substr(std::size_t(consumed));
                    layouts_.back().notes.push_back(std::move(nt));
                }
            }
            if (!layouts_.empty()) layout_load(0);
            views_need_refresh_ = true;
            continue;
        }
        if (f.kind == kTextsFrame) {
            const std::string t(f.payload.begin(), f.payload.end());
            std::size_t pos = 0;
            while (pos < t.size()) {
                const std::size_t nl = t.find('\n', pos);
                const std::string line = t.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                pos = nl == std::string::npos ? t.size() : nl + 1;
                std::size_t sk; long long lx, ly; double h, a, w = 0.0;
                int ha = 1, va = 2, consumed = 0;
                if (std::sscanf(line.c_str(), "%zu %lld %lld %lf %lf %d %d %lf %n",
                                &sk, &lx, &ly, &h, &a, &ha, &va, &w, &consumed) < 8) {
                    consumed = 0;
                    ha = 1; va = 2; w = 0.0;
                    if (std::sscanf(line.c_str(), "%zu %lld %lld %lf %lf %n",
                                    &sk, &lx, &ly, &h, &a, &consumed) < 5) continue;
                }
                if (sk >= sketches_.size()) continue;
                Canvas::TextItem x;
                x.pos.x = static_cast<decltype(x.pos.x)>(lx);
                x.pos.y = static_cast<decltype(x.pos.y)>(ly);
                x.height = h; x.angle = a;
                x.halign = ha; x.valign = va; x.width = w;
                x.text = line.substr(static_cast<std::size_t>(consumed));
                sketches_[sk].texts.push_back(std::move(x));
            }
        }
    }
    for (const auto& frame : c.foreign) {
        if (frame.kind != kPaperContentFrame && frame.kind != kModelNotesFrame) continue;
        gfile::Contents content;
        if (!gfile::decode(frame.payload, content).ok) {
            log_.push_back("the full sheet/note content could not be read");
            continue;
        }
        if (frame.kind == kModelNotesFrame) {
            for (std::size_t i = 0; i < sketches_.size(); ++i) {
                sketches_[i].texts.clear();
                read_text_entities(content.entities, static_cast<gcm::u32>(i), sketches_[i].texts);
            }
        } else {
            for (std::size_t i = 0; i < layouts_.size(); ++i) {
                layouts_[i].shapes.clear();
                layouts_[i].notes.clear();
                for (const auto& g : content.geometry)
                    if (g.space == i) layouts_[i].shapes = g.shapes;
                read_text_entities(content.entities, static_cast<gcm::u32>(i), layouts_[i].notes);
            }
        }
    }
    modeller_.set_bodies(std::move(solids));
    if (views_need_refresh_) {
        views_need_refresh_ = false;
        for (Layout& l : layouts_) refresh_views(l);
    }
    for (const auto& f : c.foreign) {
        if (f.kind != kLayersFrame) continue;
        const std::string t(f.payload.begin(), f.payload.end());
        std::istringstream in(t);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream l(line);
            char kind; std::size_t index; std::string layer;
            if (!(l >> kind >> index)) continue;
            if (kind=='F') {
                std::size_t body=0; gcm::u32 face=0;
                if (l >> body >> face && index<sketches_.size() &&
                    body<modeller_.bodies().size() && face<modeller_.bodies()[body].face_count()) {
                    sketches_[index].on_face=true;
                    sketches_[index].body=body;
                    sketches_[index].face=face;
                }
                continue;
            }
            if (kind=='V') {
                unsigned visibility=0; gcm::u32 parent=0;
                if (l >> visibility >> parent && index<sketches_.size() && visibility<=2) {
                    sketches_[index].visibility=SketchVisibility(visibility);
                    sketches_[index].parent_body_key=parent;
                }
                continue;
            }
            std::getline(l, layer);
            while (!layer.empty() && layer.front() == ' ') layer.erase(0, 1);
            if (kind == 'B') modeller_.set_body_layer(index, layer);
            else if (kind == 'S' && index < sketches_.size()) sketches_[index].layer = layer;
        }
    }
    for (const auto& f : c.foreign) {
        if (f.kind != kFeaturesFrame) continue;
        auto hist = decode_feature_history(f.payload, modeller_.bodies().size());
        modeller_.set_history(std::move(hist));
    }
    active_sketch_ = 0;
    canvas_.load_shapes(sketches_[0].shapes);
    canvas_.load_texts(sketches_[0].texts);
    for (const auto& frame : c.foreign)
        if (frame.kind == kMetadataFrame && !restore_metadata(frame.payload)) {
            log_.push_back("open failed: invalid document metadata");
            return false;
        }
    const auto face_remaps = modeller_.merge_loaded_faces();
    for (auto& sketch : sketches_)
        if (sketch.on_face && sketch.body < face_remaps.size() && sketch.face < face_remaps[sketch.body].size())
            sketch.face = face_remaps[sketch.body][sketch.face];
    document_path_ = path;
    if (!path.empty()) tab_title_ = file_title(path);
    resolve_pending_underlays(path);
    saved_serial_ = document_serial();
    clear_journal();
    set_mode(Mode::Solid);
    set_mode(Mode::Sketch);
    camera_.look_plan();
    run_command("ZOOMEXTENTS");
    log_.push_back("opened " + path + ": " + std::to_string(c.shape_count()) + " objects in " +
                   std::to_string(sketches_.size()) + " sketch(es), " +
                   std::to_string(modeller_.bodies().size()) + " solid(s)");
    return true;
}
}
