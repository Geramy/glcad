#include "ui/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/document_file.hpp"
#include "geometry/clip.hpp"
#include "ui/shell.hpp"

namespace gcapp {

namespace {

std::vector<std::size_t> selected_indices(Canvas& canvas) {
    std::vector<std::size_t> out;
    for (std::size_t i : canvas.selection())
        if (i < canvas.shapes().size()) out.push_back(i);
    return out;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

void change_properties(CommandContext& c, const std::string& n, const char* implied) {
    const std::string& arg = c.arg();
    Canvas& canvas = c.canvas();

    std::vector<std::string> words;
    {
        std::size_t at = 0;
        while (at < arg.size()) {
            std::size_t sp = arg.find(' ', at);
            if (sp == std::string::npos) sp = arg.size();
            if (sp > at) words.push_back(arg.substr(at, sp - at));
            at = sp + 1;
        }
    }
    if (implied) words.insert(words.begin(), implied);

    if (words.size() < 2) {
        c.log(n + ": say which property and what to set it to, "
                  "e.g. CHPROP color 1, CHPROP lineweight 0.5");
        return;
    }
    const auto lower = [](std::string t) {
        for (char& ch : t) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        return t;
    };

    const std::vector<std::size_t> sel = selected_indices(canvas);
    std::string said;
    for (std::size_t w = 0; w + 1 < words.size(); w += 2) {
        const std::string key = lower(words[w]);
        const std::string val = words[w + 1];
        const std::string lval = lower(val);

        if (key == "layer") {
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) sh[i].layer = val;
            });
            said += "  layer " + val;
        } else if (key == "color" || key == "colour") {
            gdoc::Color col = gdoc::Color::by_layer();
            if (lval == "bylayer") col = gdoc::Color::by_layer();
            else if (lval == "byblock") col = gdoc::Color::by_block();
            else if (val.find(',') != std::string::npos) {
                int r = 0, g = 0, b = 0;
                if (std::sscanf(val.c_str(), "%d,%d,%d", &r, &g, &b) == 3)
                    col = gdoc::Color::rgb(gcm::u8(std::clamp(r, 0, 255)),
                                           gcm::u8(std::clamp(g, 0, 255)),
                                           gcm::u8(std::clamp(b, 0, 255)));
            } else if (const gcm::i32 named = gdoc::aci_of_name(lval); named > 0) {
                col = gdoc::Color::aci(named);
            } else {
                int idx = 0;
                if (std::sscanf(val.c_str(), "%d", &idx) == 1 && idx >= 1 && idx <= 255)
                    col = gdoc::Color::aci(idx);
                else { c.log(n + ": " + val + " is not a colour"); return; }
            }
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) sh[i].attrs.color = col;
            });
            said += "  colour " + val;
        } else if (key == "linetype" || key == "lt") {
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) sh[i].attrs.linetype = val;
            });
            said += "  linetype " + val;
        } else if (key == "lineweight" || key == "lw") {
            gdoc::Lineweight lw = gdoc::Lineweight::ByLayer;
            if (lval == "bylayer") lw = gdoc::Lineweight::ByLayer;
            else if (lval == "byblock") lw = gdoc::Lineweight::ByBlock;
            else if (lval == "default") lw = gdoc::Lineweight::Default;
            else {
                double mm = 0.0;
                try { mm = std::stod(val); } catch (...) {
                    c.log(n + ": " + val + " is not a lineweight"); return;
                }
                lw = gdoc::nearest_lineweight(mm);
            }
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) sh[i].attrs.lineweight = lw;
            });
            said += "  lineweight " + val;
        } else if (key == "transparency" || key == "tr") {
            int t = -1;
            if (lval == "bylayer") t = -1;
            else if(lval=="byblock")t=-2;
            else if (std::sscanf(val.c_str(), "%d", &t) != 1) {
                c.log(n + ": transparency is a percentage, 0 to 90"); return;
            }
            t = t < -2 ? -1 : std::min(t, 90);
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) sh[i].attrs.transparency = t;
            });
            said += "  transparency " + val;
        } else {
            c.log(n + ": no property called " + words[w]);
            return;
        }
    }
    c.log(std::to_string(sel.size()) + " object(s):" + said);
}

struct Chprop : CommandBase<Chprop> {
    static constexpr auto name = "CHPROP";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const { change_properties(c, name, nullptr); }
};

struct Color : CommandBase<Color> {
    static constexpr auto name = "COLOR";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const { change_properties(c, name, "color"); }
};

struct Lweight : CommandBase<Lweight> {
    static constexpr auto name = "LWEIGHT";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const { change_properties(c, name, "lineweight"); }
};

struct Transparency : CommandBase<Transparency> {
    static constexpr auto name = "TRANSPARENCY";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const { change_properties(c, name, "transparency"); }
};

struct Matchprop : CommandBase<Matchprop> {
    static constexpr auto name = "MATCHPROP";
    static constexpr bool wants_selection = true;

    void execute(CommandContext& c) const {
        const std::vector<std::size_t> sel = selected_indices(c.canvas());
        c.pick(1, "MATCHPROP: click the source object",
               [&shell = c.shell(), sel](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            Canvas& canvas = k.canvas();
            double px, py, x, y;
            k.world(p[0], x, y);
            canvas.view().screen_of(x, y, px, py);
            const std::size_t src = canvas.pick(px, py);
            if (src == Canvas::npos) { k.log("MATCHPROP: nothing there"); return; }
            const std::string layer = canvas.shapes()[src].layer;
            const gdoc::Attributes from = canvas.shapes()[src].attrs;
            canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
                for (std::size_t i : sel) {
                    sh[i].layer = layer;
                    sh[i].attrs.color          = from.color;
                    sh[i].attrs.linetype       = from.linetype;
                    sh[i].attrs.lineweight     = from.lineweight;
                    sh[i].attrs.linetype_scale = from.linetype_scale;
                    sh[i].attrs.transparency   = from.transparency;
                }
            });
            k.log("matched: layer " + (layer.empty() ? std::string("0") : layer) +
                  " and its properties onto " + std::to_string(sel.size()) + " object(s)");
        });
    }
};

struct Block : CommandBase<Block> {
    static constexpr auto name = "BLOCK";

    void execute(CommandContext& c) const {
        if (!c.has_arg()) { c.ask("BLOCK", "name", "BLOCK: type a name for the block of the selected objects"); return; }
        if (!c.need_selection("BLOCK")) return;
        const std::string blockname = c.arg();
        c.pick(1, "BLOCK: click the base point",
               [&shell = c.shell(), blockname](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            Canvas& canvas = k.canvas();
            double x, y, px, py;
            k.world(p[0], x, y);
            canvas.view().screen_of(x, y, px, py);
            k.log(canvas.define_block(blockname, px, py) ? "block " + blockname + " defined" : "BLOCK: " + canvas.message());
        });
    }
};

struct Insert : CommandBase<Insert> {
    static constexpr auto name = "INSERT";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        if (!c.has_arg()) {
            std::string names;
            for (const auto& b : canvas.blocks().all()) names += (names.empty() ? "" : ", ") + b.name;
            c.log(names.empty() ? "INSERT: no blocks defined yet (BLOCK makes one)" : "INSERT <name>: " + names);
            return;
        }
        std::istringstream in(c.arg());
        std::string blockname;
        double scale = 1.0, rotation_deg = 0.0;
        in >> blockname;
        if (!(in >> scale) || scale == 0.0) scale = 1.0;
        if (!(in >> rotation_deg)) rotation_deg = 0.0;
        if (!canvas.blocks().find(blockname)) { c.log("INSERT: no block named " + blockname); return; }
        const double rotation = rotation_deg * 3.14159265358979323846 / 180.0;
        c.pick(1, "INSERT: click the insertion point",
               [&shell = c.shell(), blockname, scale, rotation, rotation_deg](const std::vector<gcm::Lattice2>& p) {
            CommandContext k(shell, {});
            Canvas& cv = k.canvas();
            double x, y, px, py;
            k.world(p[0], x, y);
            cv.view().screen_of(x, y, px, py);
            if (!cv.insert_block(blockname, px, py, scale, rotation)) {
                k.log("INSERT: " + cv.message());
                return;
            }
            char buf[160];
            std::snprintf(buf, sizeof(buf), "inserted %s at %.4g:1, %.4g degrees",
                          blockname.c_str(), scale, rotation_deg);
            k.log(buf);
        });
    }
};

struct Wblock : CommandBase<Wblock> {
    static constexpr auto name = "WBLOCK";

    void execute(CommandContext& c) const {
        if (!c.has_arg()) { c.ask("WBLOCK", "file.glcad", "WBLOCK: type the file to write the selection to"); return; }
        if (!c.need_selection("WBLOCK")) return;
        Canvas& canvas = c.canvas();
        gdoc::LayerTable& layers = c.shell().layers();
        gfile::Contents contents;
        contents.volume = c.vol();
        contents.layers = layers;
        gdoc::Space sp;
        sp.id = 0;
        sp.kind = gdoc::SpaceKind::Model;
        sp.name = "Model";
        sp.frame = ggeo::d3::Frame<gcm::F64>::world_xy();
        contents.spaces.push_back(sp);
        gfile::Contents::SpaceGeometry g;
        g.space = 0;
        for (std::size_t i : selected_indices(canvas)) {
            g.shapes.push_back(canvas.shapes()[i]);
            g.layers.push_back(canvas.shapes()[i].layer.empty() ? layers.current() : canvas.shapes()[i].layer);
        }
        contents.geometry.push_back(std::move(g));
        const std::string path = c.arg().find('.') == std::string::npos ? c.arg() + ".glcad" : c.arg();
        const gfile::Error e = gfile::save_file(path, contents);
        c.log(e.ok ? "wrote " + std::to_string(canvas.selection().size()) + " object(s) to " + path : "WBLOCK: " + e.message);
    }
};

struct Bedit : CommandBase<Bedit> {
    static constexpr auto name = "BEDIT";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        std::istringstream in(c.arg());
        std::string block, kind, pname;
        in >> block >> kind >> pname;
        if (block.empty()) {
            std::string said;
            for (const auto& d : canvas.blocks().all())
                if (canvas.blocks().is_dynamic(d.name)) {
                    const gdoc::DynamicBlock* dyn = canvas.blocks().dynamic(d.name);
                    said += (said.empty() ? "" : "; ") + d.name + ":";
                    if (dyn)
                        for (const auto& p : dyn->parameters)
                            said += " " + p.name + "(" +
                                    gdoc::parameter_kind_name(p.kind) + ")";
                }
            c.log(said.empty() ? "no dynamic blocks -- BEDIT <block> <kind> <name>"
                               : said);
            return;
        }
        if (!canvas.blocks().find(block)) {
            c.log("BEDIT: no block named " + block);
            return;
        }
        gdoc::DynamicBlock& dyn = canvas.blocks().make_dynamic(block);
        for (char& ch : kind) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (kind.empty()) {
            c.log(block + " is dynamic: " + std::to_string(dyn.parameters.size()) +
                  " parameter(s), " + std::to_string(dyn.actions.size()) + " action(s)");
            return;
        }
        if (pname.empty()) { c.log("BEDIT " + block + " " + kind + " <name> ..."); return; }

        if (kind == "LINEAR" || kind == "POLAR" || kind == "ROTATION") {
            gdoc::BlockParameter p;
            p.name = pname;
            p.kind = kind == "LINEAR" ? gdoc::ParameterKind::Linear
                   : kind == "POLAR"  ? gdoc::ParameterKind::Polar
                                      : gdoc::ParameterKind::Rotation;
            in >> p.minimum >> p.maximum >> p.increment;
            p.value = p.minimum;
            dyn.parameters.push_back(p);
            c.log(block + "." + pname + ": " +
                  gdoc::parameter_kind_name(p.kind));
            return;
        }
        if (kind == "FLIP" || kind == "VISIBILITY") {
            gdoc::BlockParameter p;
            p.name = pname;
            p.kind = kind == "FLIP" ? gdoc::ParameterKind::Flip
                                    : gdoc::ParameterKind::Visibility;
            std::string choice;
            while (in >> choice) p.choices.push_back(choice);
            dyn.parameters.push_back(p);
            c.log(block + "." + pname + ": " +
                  gdoc::parameter_kind_name(p.kind));
            return;
        }
        if (kind == "LOOKUP") {
            gdoc::BlockParameter p;
            p.name = pname;
            p.kind = gdoc::ParameterKind::Lookup;
            std::string choice;
            while (in >> choice) p.choices.push_back(choice);
            if (p.choices.empty()) { c.log("BEDIT ... LOOKUP <name> <choices...>"); return; }
            dyn.parameters.push_back(p);
            c.log(block + "." + pname + ": " + std::to_string(p.choices.size()) +
                  " choice(s)");
            return;
        }
        if (kind == "STRETCH" || kind == "MOVE" || kind == "SCALE") {
            gdoc::BlockAction a;
            a.name = pname;
            a.kind = kind == "STRETCH" ? gdoc::ActionKind::Stretch
                   : kind == "MOVE"    ? gdoc::ActionKind::Move
                                       : gdoc::ActionKind::Scale;
            in >> a.parameter >> a.dx >> a.dy;
            if (a.parameter.empty() || !dyn.parameter(a.parameter)) {
                c.log("BEDIT: no parameter called " + a.parameter +
                      " -- an action without one drives nothing");
                return;
            }
            if (const gdoc::BlockDef* def = canvas.blocks().find(block))
                for (gcm::u32 i = 0; i < gcm::u32(def->geometry.size()); ++i)
                    a.geometry.push_back(i);
            dyn.actions.push_back(a);
            c.log(block + ": " + pname + " " +
                  gdoc::action_kind_name(a.kind) + " driven by " + a.parameter);
            return;
        }
        c.log("BEDIT: LINEAR, POLAR, ROTATION, FLIP, VISIBILITY, LOOKUP, "
              "STRETCH, MOVE or SCALE");
    }
};

struct Bparam : CommandBase<Bparam> {
    static constexpr auto name = "BPARAM";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        if (canvas.selected_references().empty()) {
            c.log("BPARAM: select an insertion first");
            return;
        }
        std::istringstream in(c.arg());
        std::string pname, value;
        in >> pname >> value;
        const gcm::u32 which = canvas.selected_references().front();
        const gdoc::BlockRef* r = canvas.blocks().reference_mut(which);
        if (!r) return;
        gdoc::DynamicBlock* dyn = canvas.blocks().dynamic(r->name);
        if (!dyn) { c.log("BPARAM: " + r->name + " is not dynamic"); return; }
        if (pname.empty()) {
            std::string said;
            for (const auto& p : dyn->parameters) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s%s = %.6g", said.empty() ? "" : ", ",
                              p.name.c_str(), p.value);
                said += buf;
            }
            c.log(said.empty() ? r->name + " has no parameters" : said);
            return;
        }
        const gdoc::BlockParameter* p = dyn->parameter(pname);
        if (!p) { c.log("BPARAM: no parameter called " + pname); return; }
        bool ok = false;
        if (p->kind == gdoc::ParameterKind::Lookup ||
            p->kind == gdoc::ParameterKind::Visibility ||
            p->kind == gdoc::ParameterKind::Flip) {
            ok = dyn->set_choice(pname, value);
            if (!ok) c.log("BPARAM: " + value + " is not one of its choices");
        } else {
            double v = 0.0;
            try { v = std::stod(value); } catch (...) {
                c.log("BPARAM: " + value + " is not a number");
                return;
            }
            ok = dyn->set_parameter(pname, v);
            if (ok && dyn->parameter(pname) && dyn->parameter(pname)->value != v)
                c.log("BPARAM: clamped to " +
                      std::to_string(dyn->parameter(pname)->value));
        }
        if (ok) {
            c.invalidate_frame_caches();
            c.log(r->name + "." + pname + " = " + value);
        }
    }
};

struct Attdef : CommandBase<Attdef> {
    static constexpr auto name = "ATTDEF";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        std::istringstream in(c.arg());
        std::string block, tag, prompt, fallback;
        in >> block >> tag;
        in >> prompt;
        std::getline(in, fallback);
        while (!fallback.empty() && fallback.front() == ' ') fallback.erase(0, 1);
        if (block.empty() || tag.empty()) {
            c.log("ATTDEF <block> <tag> [prompt] [default]");
            return;
        }
        gdoc::BlockDef* def = const_cast<gdoc::BlockDef*>(canvas.blocks().find(block));
        if (!def) { c.log("ATTDEF: no block named " + block); return; }
        for (char& ch : tag) ch = char(std::toupper(static_cast<unsigned char>(ch)));
        gdoc::AttributeDef a;
        a.tag = tag;
        a.prompt = prompt.empty() ? tag : prompt;
        a.default_value = fallback;
        a.height = c.shell().text_height();
        bool replaced = false;
        for (auto& existing : def->attributes)
            if (existing.tag == tag) { existing = a; replaced = true; }
        if (!replaced) def->attributes.push_back(a);
        std::size_t filled = 0;
        for (gcm::u32 i = 0; i < gcm::u32(canvas.blocks().references().size()); ++i)
            if (gdoc::BlockRef* r = canvas.blocks().reference_mut(i))
                if (r->name == block && !r->attribute(tag)) {
                    r->set_attribute(tag, a.default_value);
                    ++filled;
                }
        c.log((replaced ? "attribute updated: " : "attribute added: ") +
              block + "." + tag +
              (filled ? " (" + std::to_string(filled) + " insertion(s) filled)" : ""));
    }
};

void edit_attributes(CommandContext& c) {
    Canvas& canvas = c.canvas();
    if (canvas.selected_references().empty()) {
        std::string said;
        for (const auto& def : canvas.blocks().all()) {
            if (def.attributes.empty()) continue;
            std::string tags;
            for (const auto& a : def.attributes)
                tags += (tags.empty() ? "" : ", ") + a.tag;
            said += (said.empty() ? "" : "; ") + def.name + ": " + tags;
        }
        c.log(said.empty()
                  ? "no block defines any attributes (ATTDEF adds one)"
                  : said);
        c.log("select an insertion, then ATTEDIT <tag> <value>");
        return;
    }
    std::istringstream in(c.arg());
    std::string tag, value;
    in >> tag;
    std::getline(in, value);
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    for (char& ch : tag) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    if (tag.empty()) {
        const gcm::u32 which = canvas.selected_references().front();
        if (const gdoc::BlockRef* r = canvas.blocks().reference_mut(which)) {
            std::string said = r->name + ":";
            for (const auto& a : r->attributes) said += " " + a.tag + "=" + a.value;
            c.log(r->attributes.empty() ? r->name + " has no attributes" : said);
        }
        return;
    }
    std::size_t changed = 0;
    for (gcm::u32 which : canvas.selected_references())
        if (gdoc::BlockRef* r = canvas.blocks().reference_mut(which)) {
            r->set_attribute(tag, value);
            ++changed;
        }
    c.invalidate_frame_caches();
    c.log(std::to_string(changed) + " insertion(s): " + tag + " = " + value);
}

struct Attedit : CommandBase<Attedit> {
    static constexpr auto name = "ATTEDIT";
    void execute(CommandContext& c) const { edit_attributes(c); }
};

struct Battman : CommandBase<Battman> {
    static constexpr auto name = "BATTMAN";
    void execute(CommandContext& c) const { edit_attributes(c); }
};

struct Attext : CommandBase<Attext> {
    static constexpr auto name = "ATTEXT";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        const auto& refs = canvas.blocks().references();
        if (refs.empty()) { c.log("ATTEXT: nothing is inserted"); return; }
        std::vector<std::string> columns{"BLOCK", "X", "Y", "LAYER"};
        for (const auto& r : refs)
            for (const auto& a : r.attributes)
                if (std::find(columns.begin(), columns.end(), a.tag) == columns.end())
                    columns.push_back(a.tag);
        const auto quote = [](const std::string& t) {
            if (t.find_first_of(",\"\n") == std::string::npos) return t;
            std::string out = "\"";
            for (char ch : t) { if (ch == '"') out += '"'; out += ch; }
            return out + "\"";
        };
        std::string csv;
        for (std::size_t i = 0; i < columns.size(); ++i)
            csv += (i ? "," : "") + columns[i];
        csv += "\n";
        const gunit::Format fmt{canvas.display_unit()};
        for (const auto& r : refs) {
            double x, y;
            gcm::to_world(c.vol(), r.insertion, x, y);
            csv += quote(r.name) + "," + gunit::format_length(x, fmt) + "," +
                   gunit::format_length(y, fmt) + "," + quote(r.layer);
            for (std::size_t col = 4; col < columns.size(); ++col) {
                const auto value = r.attribute(columns[col]);
                csv += "," + quote(value ? *value : std::string());
            }
            csv += "\n";
        }
        const std::string path = c.arg().empty() ? std::string("attributes.csv") : c.arg();
        std::ofstream f(path);
        if (!f) { c.log("ATTEXT: could not write " + path); return; }
        f << csv;
        c.log("extracted " + std::to_string(refs.size()) + " insertion(s), " +
              std::to_string(columns.size()) + " column(s) to " + path);
    }
};

struct Xclip : CommandBase<Xclip> {
    static constexpr auto name = "XCLIP";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        const auto& vol = c.vol();
        std::istringstream in(c.arg());std::string refname,mode;in>>std::quoted(refname)>>mode;mode=upper(mode);
        const auto usage=[&]{c.log("XCLIP name RECT x0 y0 x1 y1 | POLYGON x0 y0 x1 y1 ... | ON | OFF | INVERT | CLEAR");};
        if(refname.empty()){usage();return;}
        const auto* found=static_cast<const gdoc::BlockTable&>(canvas.blocks()).xref(refname);
        if(!found){c.log("XCLIP: no reference called "+refname);return;}
        const auto cached_layer="XREF|"+refname;
        const bool legacy_cache=std::any_of(canvas.shapes().begin(),canvas.shapes().end(),[&](const auto& shape){return shape.layer==cached_layer;});
        if(legacy_cache||(found->state==gdoc::XrefState::Loaded&&!canvas.blocks().find(refname))) {
            c.log("XCLIP: clipping this reference's imported cache is not supported yet");return;
        }
        auto reference=*found;
        if(mode=="ON"||mode=="OFF")reference.clip_enabled=mode=="ON";
        else if(mode=="INVERT")reference.clip_inverted=!reference.clip_inverted;
        else if(mode=="CLEAR"){reference.clip.clear();reference.clip_inverted=false;}
        else if(mode=="RECT"||mode=="POLYGON") {
            std::vector<double> values;double value;while(in>>value){if(!std::isfinite(value)){usage();return;}values.push_back(value);}
            if(!in.eof()||values.size()%2||(mode=="RECT"?values.size()!=4:values.size()<6)||values.size()>8192){usage();return;}
            gdoc::BlockRef placement;placement.name=refname;placement.insertion=reference.insertion;placement.scale_x=placement.scale_y=reference.scale;placement.rotation=reference.rotation;
            for(const auto& ref:canvas.blocks().references())if(ref.name==refname){placement=ref;break;}
            if(placement.scale_x==0||placement.scale_y==0){usage();return;}
            std::vector<gcm::Lattice2> boundary;
            for(std::size_t i=0;i<values.size();i+=2) {
                const auto x=gunit::to_metres(values[i],canvas.display_unit()),y=gunit::to_metres(values[i+1],canvas.display_unit());
                if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x*vol.scale())>9e17||std::abs(y*vol.scale())>9e17){usage();return;}
                boundary.push_back(gcm::to_lattice(vol,x,y));
            }
            if(mode=="RECT"){const auto a=boundary[0],b=boundary[1];boundary={a,{b.x,a.y},b,{a.x,b.y}};}
            const auto* def=canvas.blocks().find(refname);const auto base=def?def->base:gcm::Lattice2{};
            const double cs=std::cos(placement.rotation),sn=std::sin(placement.rotation);
            for(auto& p:boundary){const double dx=double(p.x)-placement.insertion.x,dy=double(p.y)-placement.insertion.y;
                const double x=base.x+(cs*dx+sn*dy)/placement.scale_x,y=base.y+(-sn*dx+cs*dy)/placement.scale_y;
                if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>9e17||std::abs(y)>9e17){usage();return;}
                p={gcm::i64(std::llround(x)),gcm::i64(std::llround(y))};}
            if(!gclip::valid_boundary(boundary)){c.log("XCLIP: boundary must be a simple closed polygon");return;}
            reference.clip=std::move(boundary);reference.clip_enabled=true;reference.clip_inverted=false;
        } else {usage();return;}
        if(mode!="RECT"&&mode!="POLYGON"){std::string extra;if(in>>extra){usage();return;}}
        canvas.blocks().attach(std::move(reference));
        canvas.clear_selection();c.log("clipping updated: "+refname);
    }
};

struct Purge : CommandBase<Purge> {
    static constexpr auto name = "PURGE";

    void execute(CommandContext& c) const {
        Canvas& canvas = c.canvas();
        gdoc::LayerTable& layers = c.shell().layers();
        std::size_t layers_gone = 0, blocks_gone = 0;
        std::unordered_set<std::string> used;
        for (const auto& s : canvas.shapes()) used.insert(s.layer.empty() ? "0" : s.layer);
        for (const auto& sk : c.shell().sketches()) for (const auto& s : sk.shapes) used.insert(s.layer.empty() ? "0" : s.layer);
        std::vector<std::string> names;
        for (const auto& l : layers.all()) names.push_back(l.name);
        for (const auto& layername : names)
            if (layername != "0" && layername != layers.current() && !used.count(layername) && layers.remove(layername)) ++layers_gone;
        std::unordered_set<std::string> inserted;
        for (const auto& r : canvas.blocks().references()) inserted.insert(r.name);
        for (const auto& d : canvas.blocks().all())
            for (const auto& nested : canvas.blocks().nested_insertions(d)) inserted.insert(nested.name);
        std::vector<std::string> bnames;
        for (const auto& b : canvas.blocks().all()) bnames.push_back(b.name);
        for (const auto& blockname : bnames)
            if (!inserted.count(blockname) && canvas.blocks().remove(blockname)) ++blocks_gone;
        c.log("purged " + std::to_string(layers_gone) + " layer(s), " + std::to_string(blocks_gone) + " block(s)");
    }
};

struct Blocklib : CommandBase<Blocklib> {
    static constexpr auto name = "BLOCKLIB";

    void execute(CommandContext& c) const {
        namespace fs = std::filesystem;
        Canvas& canvas = c.canvas();
        std::istringstream in(c.arg());
        std::string folder, want;
        in >> folder >> want;
        if (folder.empty()) {
            c.log("BLOCKLIB <folder> lists it; BLOCKLIB <folder> <name> inserts one");
            return;
        }
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) {
            c.log("BLOCKLIB: " + folder + " is not a folder");
            return;
        }
        std::vector<std::string> found;
        for (const auto& e : fs::directory_iterator(folder, ec)) {
            if (!e.is_regular_file(ec)) continue;
            std::string ext = e.path().extension().string();
            for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (ext != ".glcad" && ext != ".dxf" && ext != ".dwg") continue;
            found.push_back(e.path().stem().string());
        }
        std::sort(found.begin(), found.end());
        if (want.empty()) {
            std::string names;
            for (const auto& f2 : found) names += (names.empty() ? "" : ", ") + f2;
            c.log(found.empty() ? "no drawings in " + folder
                                : std::to_string(found.size()) + " in " + folder +
                                      ": " + names);
            return;
        }
        if (std::find(found.begin(), found.end(), want) == found.end()) {
            c.log("BLOCKLIB: no drawing called " + want + " in " + folder);
            return;
        }
        std::string path;
        for (const auto& e : fs::directory_iterator(folder, ec))
            if (e.path().stem().string() == want) { path = e.path().string(); break; }
        const std::size_t before = canvas.shapes().size();
        if (!c.shell().import_file(path)) { c.log("BLOCKLIB: could not read " + path); return; }
        std::vector<gsnap::Shape> brought;
        canvas.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            for (std::size_t i = before; i < sh.size(); ++i) brought.push_back(sh[i]);
            if (sh.size() > before) sh.resize(before);
        });
        if (brought.empty()) { c.log("BLOCKLIB: " + want + " is empty"); return; }
        gdoc::BlockDef def;
        def.name = want;
        def.geometry = std::move(brought);
        canvas.blocks().define(std::move(def));
        gdoc::BlockRef ref;
        ref.name = want;
        ref.layer = canvas.current_layer();
        canvas.blocks().insert(ref);
        c.invalidate_frame_caches();
        c.log("block " + want + " from the library, inserted at the origin");
    }
};

}

void register_organise_commands(CommandTable& t) {
    t.add<Chprop>();
    t.add<Color>();
    t.add<Lweight>();
    t.add<Transparency>();
    t.add<Matchprop>();
    t.add<Block>();
    t.add<Insert>();
    t.add<Wblock>();
    t.add<Bedit>();
    t.add<Bparam>();
    t.add<Attdef>();
    t.add<Attedit>();
    t.add<Battman>();
    t.add<Attext>();
    t.add<Xclip>();
    t.add<Purge>();
    t.add<Blocklib>();
}

}
