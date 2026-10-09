#include "ui/shell.hpp"
#include "render/mtext.hpp"
#include "ui/ai_tools.hpp"
#include "ui/frame_types.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace gcapp {

namespace T = gtheme::token;

void Shell::draw_drawing_panel() {
    const auto sl = dock_.slot("drawing");
    if (!sl.visible || sl.collapsed) return;
    dock_.begin_content(ui_ctx_, "drawing");
    gui::Stack s(ui_ctx_, sl.content);
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    ui_ctx_.push_id("drawing-panel");

    gui::label(ui_ctx_, s.next(float(px(18))), "Sheets", gui::TextAlign::Left, true);
    {
        for (std::size_t i = 0; i < layouts_.size(); ++i) {
            gui::Rect row = s.next(float(px(28)));
            gui::Rect close = row;
            close.x = row.right() - float(px(24));
            close.w = float(px(24));
            row.w -= float(px(28));
            ui_ctx_.push_id("sheet" + std::to_string(i));
            if (gui::button(ui_ctx_, row, layouts_[i].name, true,
                            int(i) == selected_layout())) select_layout(int(i));
            if (gui::button(ui_ctx_, close, "\xc3\x97", layouts_.size() > 0)) {
                run_command("LAYOUT DELETE " + layouts_[i].name);
                ui_ctx_.pop_id();
                break;
            }
            ui_ctx_.pop_id();
        }
        gui::Rect add = s.next(float(px(30)));
        if (gui::button(ui_ctx_, add, layouts_.empty() ? "New sheet" : "+ Sheet", true))
            run_command("LAYOUT NEW");
        if (active_layout_ >= 0) {
            gui::Rect tb = s.next(float(px(30)));
            if (gui::button(ui_ctx_, tb, "Titleblock", true)) run_command("TITLEBLOCK");
        }
    }
    s.gap(float(px(10)));

    gui::label(ui_ctx_, s.next(float(px(18))), "Paper", gui::TextAlign::Left, true);
    {
        std::vector<std::string> papers = [] {
            std::vector<std::string> names;
            for (const auto& ps : gplot::standard_papers()) names.push_back(ps.name);
            return names;
        }();
        if(std::find(papers.begin(),papers.end(),plot_paper_)==papers.end()) papers.push_back(plot_paper_);
        gcm::i32 sel = 0;
        for (std::size_t i = 0; i < papers.size(); ++i) if (papers[i] == plot_paper_) sel = gcm::i32(i);
        gui::Rect dd = s.next(float(px(32)));
        if (gui::dropdown(ui_ctx_, dd, "paper", papers, sel) >= 0) {
            plot_paper_ = papers[std::size_t(sel)];
            layout_store();
        }
    }
    s.gap(float(px(6)));
    {
        gui::Rect row = s.next(float(px(30)));
        gui::Rect l = row; l.w = row.w * 0.5f - 2.0f;
        gui::Rect r2 = row; r2.x = l.right() + 4.0f; r2.w = l.w;
        if (gui::button(ui_ctx_, l, "Landscape", true, plot_landscape_)) { plot_landscape_ = true; layout_store(); }
        if (gui::button(ui_ctx_, r2, "Portrait", true, !plot_landscape_)) { plot_landscape_ = false; layout_store(); }
    }
    s.gap(float(px(8)));
    gui::label(ui_ctx_, s.next(float(px(18))), "Scale", gui::TextAlign::Left, true);
    {
        std::vector<std::string> names{"Fit to paper"};
        std::vector<int> dens{0};
        for (const auto& sc : gplot::standard_scales()) {
            if (sc.paper != 1.0 || sc.drawing < 1.0) continue;
            names.push_back(sc.name);
            dens.push_back(int(sc.drawing + 0.5));
        }
        gcm::i32 sel = 0;
        for (std::size_t i = 0; i < dens.size(); ++i)
            if (dens[i] == plot_scale_den_) sel = gcm::i32(i);
        gui::Rect dd = s.next(float(px(32)));
        if (gui::dropdown(ui_ctx_, dd, "scale", names, sel) >= 0) {
            plot_scale_den_ = dens[std::size_t(sel)];
            layout_store();
        }
    }
    if (const auto* layout=active_layout(); layout && !layout->viewports.empty()) {
        s.gap(float(px(10)));
        gui::label(ui_ctx_,s.next(float(px(18))),"Viewports",gui::TextAlign::Left,true);
        for (std::size_t i=0;i<layout->viewports.size();++i) {
            const auto viewport=layout->viewports[i];
            const auto number=std::to_string(i+1);
            ui_ctx_.push_id("viewport"+number);
            auto row=s.next(float(px(28)));
            auto lock=row.cut_right(float(px(68)));
            auto show=row.cut_right(float(px(50)));
            char scale_label[96]; std::snprintf(scale_label,sizeof(scale_label),"#%zu 1:%.6g",i+1,viewport.scale);
            gui::label(ui_ctx_,row,scale_label,gui::TextAlign::Left,true);
            if (gui::button(ui_ctx_,lock,viewport.locked ? "Unlock" : "Lock",true))
                run_command("MVIEW LOCK "+number+(viewport.locked ? " OFF" : " ON"));
            if (gui::button(ui_ctx_,show,viewport.on ? "Hide" : "Show",true))
                run_command("MVIEW "+std::string(viewport.on ? "OFF " : "ON ")+number);
            std::vector<std::string> names{"Freeze / thaw layer..."};
            for (const auto& layer:layers_.all()) names.push_back((viewport.layer_visible(layer.name) ? "Freeze: " : "Thaw: ")+layer.name);
            gcm::i32 pick=0;
            if (gui::dropdown(ui_ctx_,s.next(float(px(28))),"vp-layer",names,pick)>0)
                run_command("MVIEW "+std::string(viewport.layer_visible(layers_.all()[std::size_t(pick-1)].name) ? "FREEZE " : "THAW ")+number+" "+layers_.all()[std::size_t(pick-1)].name);
            ui_ctx_.pop_id();
        }
        if (gui::button(ui_ctx_,s.next(float(px(28))),"Viewport command help",true)) run_command("MVIEW HELP");
    }
    s.gap(float(px(12)));
    {
        gui::Rect row = s.next(float(px(36)));
        if (gui::button(ui_ctx_, row, "Export PDFâ¦", true)) run_command("PLOT");
    }
    s.gap(float(px(6)));
    gui::label(ui_ctx_, s.next(float(px(16))),
               plot_scale_den_ > 0 ? "prints at exactly " + scale_display(plot_scale_den_)
                                   : "fits the drawing, reports the scale", gui::TextAlign::Left, true);
    ui_ctx_.pop_id();
    dock_.end_content(ui_ctx_, "drawing");
}

bool Shell::set_selection_property(const std::string& property, const std::string& value) {
    const auto key=uppercase(property);
    const auto& texts=canvas_.selected_texts(); const auto& dims=canvas_.selected_dimensions();
    const auto& refs=canvas_.selected_references();
    const auto invalid=[&] { log_.push_back("Properties: invalid value or property for the selection"); return false; };
    double number=0;
    const bool length=key=="HEIGHT" || key=="WIDTH" || key=="X" || key=="Y";
    const bool numeric=length || key=="ROTATION" || key=="SCALE" || key=="SCALEX" || key=="SCALEY";
    if (numeric) {
        if (length) {
            const auto parsed=gunit::parse_length(value,canvas_.display_unit());
            if (!parsed) return invalid(); number=*parsed;
        } else {
            std::istringstream input(value); if (!(input>>number)) return invalid();
            input>>std::ws; if (!input.eof()) return invalid();
        }
        if (!std::isfinite(number) || (key=="HEIGHT" && number<=0) || (key=="WIDTH" && number<0) ||
            ((key=="SCALE" || key=="SCALEX" || key=="SCALEY") && number==0)) return invalid();
        if (key=="ROTATION") number=std::remainder(number,360.0)*3.14159265358979323846/180.0;
    }
    if (!texts.empty()) {
        const bool format=key=="FONT" || key=="COLOR" || key=="BOLD" || key=="ITALIC";
        const bool fill=key=="BACKGROUND" || key=="HIGHLIGHT";
        if (key!="HEIGHT" && key!="WIDTH" && key!="ROTATION" && key!="STYLE" && key!="TEXT" && !format && !fill) return invalid();
        if (key=="STYLE" && !text_styles_.find(value)) return invalid();
        const bool off=uppercase(value)=="NONE" || uppercase(value)=="OFF" || uppercase(value)=="BYLAYER";
        unsigned rgb=0;
        if((key=="COLOR" || fill) && !off) {
            if(value.size()!=7 || value[0]!='#' || !std::all_of(value.begin()+1,value.end(),[](unsigned char c){return std::isxdigit(c);}))return invalid();
            rgb=unsigned(std::stoul(value.substr(1),nullptr,16));
        }
        const auto enabled=uppercase(value);
        if((key=="BOLD" || key=="ITALIC") && enabled!="1" && enabled!="0" && enabled!="ON" && enabled!="OFF" && enabled!="TRUE" && enabled!="FALSE")return invalid();
        std::string font_name=value;
        if(key=="FONT" && uppercase(value)!="DEFAULT") {
            if(fonts_.all().empty())fonts_.scan_system();
            const auto* font=fonts_.find(value);if(!font || (font->kind!=gtext::FontKind::TrueType && font->kind!=gtext::FontKind::OpenType))return invalid();
            gtext::Font candidate;if(!candidate.load_file(font->path) || !candidate.covers_ascii())return invalid();
            font_name=font->name;
        }
        canvas_.set_font_registry(&fonts_);
        canvas_.modify_texts([&](auto& items) { for (auto i:texts) if (i<items.size()) {
            auto& t=items[i];
            if (key=="HEIGHT") t.height=number; else if (key=="WIDTH") t.width=number;
            else if (key=="ROTATION") t.angle=number; else if (key=="STYLE") t.style=value;
            else if(fill) {
                const auto name=key=="BACKGROUND" ? "text_background" : "text_highlight";
                if(off)t.attrs.user.erase(name);else t.attrs.user[name]=value;
            } else if(format) {
                auto content=gtext::parse_mtext(t.text);
                for(auto& run:content.runs) {
                    if(key=="FONT")run.format.font=uppercase(value)=="DEFAULT" ? "" : font_name;
                    else if(key=="BOLD")run.format.bold=enabled=="1" || enabled=="ON" || enabled=="TRUE";
                    else if(key=="ITALIC")run.format.italic=enabled=="1" || enabled=="ON" || enabled=="TRUE";
                    else {run.format.has_color=!off;run.format.true_color=!off;run.format.r=gcm::u8(rgb>>16);run.format.g=gcm::u8(rgb>>8);run.format.b=gcm::u8(rgb);}
                }
                t.text=gtext::format_mtext(content);
            } else { t.text=value; t.field.clear(); }
        }});
    } else if (!dims.empty()) {
        if (key!="STYLE" && key!="TEXT" && key!="LAYER") return invalid();
        if (key=="STYLE" && !canvas_.dim_styles().find(value)) return invalid();
        if (key=="LAYER" && !layers_.find(value)) return invalid();
        canvas_.modify_dimensions([&](auto& items) { for (auto i:dims) if (i<items.size()) {
            if (key=="STYLE") items[i].style=value;
            else if (key=="TEXT") items[i].text_override=value;
            else items[i].layer=value;
        }});
    } else if (!refs.empty()) {
        if (key!="SCALE" && key!="SCALEX" && key!="SCALEY" && key!="ROTATION" && key!="X" && key!="Y" && key!="LAYER") return invalid();
        if (key=="LAYER" && !layers_.find(value)) return invalid();
        canvas_.modify_references([&](auto& items) { for (auto i:refs) if (i<items.size()) {
            auto& r=items[i];
            if (key=="SCALE" || key=="SCALEX") r.scale_x=number;
            if (key=="SCALE" || key=="SCALEY") r.scale_y=number;
            if (key=="ROTATION") r.rotation=number;
            if (key=="LAYER") r.layer=value;
            if (key=="X" || key=="Y") {
                double x,y; gcm::to_world(vol_,r.insertion,x,y);
                if (key=="X") x=number; else y=number;
                r.insertion=gcm::to_lattice(vol_,x,y);
            }
        }});
    } else return invalid();
    invalidate_frame_caches(); log_.push_back("Properties: "+key+" updated"); return true;
}

void Shell::draw_properties_panel() {
    const auto sl = dock_.slot("properties");
    if (!sl.visible || sl.collapsed) return;
    dock_.begin_content(ui_ctx_, "properties");
    gui::Stack s(ui_ctx_, sl.content);
    const auto& theme = themes_.active();
    ui_ctx_.push_id("properties-panel");

    const auto& text_selection=canvas_.selected_texts();
    const auto& dim_selection=canvas_.selected_dimensions();
    const auto& ref_selection=canvas_.selected_references();
    if (!text_selection.empty() || !dim_selection.empty() || !ref_selection.empty()) {
        const bool text=!text_selection.empty(), dim=!dim_selection.empty();
        std::string selection_key=std::to_string(document_serial())+":"+std::to_string(active_space_id());
        for (auto i:text_selection) selection_key+="t"+std::to_string(i);
        for (auto i:dim_selection) selection_key+="d"+std::to_string(i);
        for (auto i:ref_selection) selection_key+="b"+std::to_string(i);
        if (selection_key!=property_selection_key_) { property_selection_key_=selection_key; property_inputs_.clear(); }
        const std::size_t count=text ? text_selection.size() : dim ? dim_selection.size() : ref_selection.size();
        const bool walls=!text && !dim && std::all_of(ref_selection.begin(),ref_selection.end(),[&](auto i) {
            return i<canvas_.blocks().references().size() && Canvas::is_wall_section(canvas_.blocks().references()[i]);
        });
        gui::label(ui_ctx_,s.next(float(px(22))),std::to_string(count)+(text ? " text item(s)" : dim ? " annotation(s)" : walls ? " wall section(s)" : " block insertion(s)"),gui::TextAlign::Left,true);
        const auto num=[](double n) { std::ostringstream out; out.precision(15); out<<n; return out.str(); };
        const std::vector<std::string> properties=text ? std::vector<std::string>{"HEIGHT","WIDTH","ROTATION","STYLE","TEXT"}
            : dim ? std::vector<std::string>{"STYLE","TEXT","LAYER"}
                  : std::vector<std::string>{"X","Y","ROTATION","SCALEX","SCALEY","LAYER"};
        for (const auto& property:properties) {
            std::vector<std::string> values;
            if (text) { for (auto i:text_selection) if (i<canvas_.texts().size()) {
                const auto& t=canvas_.texts()[i];
                values.push_back(property=="HEIGHT" ? num(gunit::from_metres(t.height,canvas_.display_unit()))
                    : property=="WIDTH" ? num(gunit::from_metres(t.width,canvas_.display_unit()))
                    : property=="ROTATION" ? num(t.angle*180/3.14159265358979323846)
                    : property=="STYLE" ? (t.style.empty() ? text_styles_.current() : t.style) : t.text);
            }
            } else if (dim) { for (auto i:dim_selection) if (i<canvas_.dimensions().size()) {
                const auto& d=canvas_.dimensions()[i]; values.push_back(property=="STYLE" ? d.style : property=="TEXT" ? d.text_override : d.layer);
            }
            } else { for (auto i:ref_selection) if (i<canvas_.blocks().references().size()) {
                const auto& r=canvas_.blocks().references()[i]; double x,y; gcm::to_world(vol_,r.insertion,x,y);
                values.push_back(property=="SCALEX" ? num(r.scale_x) : property=="SCALEY" ? num(r.scale_y)
                    : property=="ROTATION" ? num(r.rotation*180/3.14159265358979323846)
                    : property=="X" ? num(gunit::from_metres(x,canvas_.display_unit()))
                    : property=="Y" ? num(gunit::from_metres(y,canvas_.display_unit())) : r.layer);
            }
            }
            if (values.empty()) continue;
            const bool same=std::all_of(values.begin(),values.end(),[&](const auto& v) { return v==values[0]; });
            gui::label(ui_ctx_,s.next(float(px(18))),property+(same ? "" : "  varies"),gui::TextAlign::Left,true);
            if (property=="STYLE" || property=="LAYER") {
                std::vector<std::string> choices;
                if (!same) choices.push_back("varies");
                if (property=="LAYER") for (const auto& l:layers_.all()) choices.push_back(l.name);
                else if (text) for (const auto& st:text_styles_.all()) choices.push_back(st.name);
                else for (const auto& st:canvas_.dim_styles().all()) choices.push_back(st.name);
                gcm::i32 pick=0;
                if (same) for (std::size_t i=0;i<choices.size();++i) if (choices[i]==values[0]) pick=gcm::i32(i);
                if (gui::dropdown(ui_ctx_,s.next(float(px(30))),property,choices,pick)>=0 && choices[std::size_t(pick)]!="varies")
                    run_command("PROPERTIES "+property+" "+choices[std::size_t(pick)]);
            } else {
                auto [it,inserted]=property_inputs_.try_emplace(property,same ? values[0] : "");
                if (property=="TEXT") {
                    if (gui::text_field(ui_ctx_,s.next(float(px(30))),property,it->second,same ? "Text" : "varies"))
                        run_command("PROPERTIES TEXT "+it->second);
                } else {
                    const auto result=gui::numeric_field(ui_ctx_,s.next(float(px(30))),property,it->second,
                        property=="ROTATION" ? "deg" : (property=="SCALEX" || property=="SCALEY") ? "" : gunit::suffix(canvas_.display_unit()));
                    if (result.committed) run_command("PROPERTIES "+property+" "+result.text);
                }
            }
        }
        ui_ctx_.pop_id(); dock_.end_content(ui_ctx_,"properties"); return;
    }

    const std::vector<std::size_t> sel = selected_indices();
    if (sel.empty()) {
        gui::label(ui_ctx_, s.next(float(px(20))), "Nothing selected",
                   gui::TextAlign::Left, true);
        gui::label(ui_ctx_, s.next(float(px(18))),
                   "Select objects to see and change their properties.",
                   gui::TextAlign::Left, true);
        ui_ctx_.pop_id();
        dock_.end_content(ui_ctx_, "properties");
        return;
    }

    const auto& shapes = canvas_.shapes();
    const gsnap::Shape& first = shapes[sel.front()];
    bool same_layer = true, same_color = true, same_lt = true, same_lw = true,
         same_tr = true, same_kind = true;
    for (std::size_t i : sel) {
        const gsnap::Shape& sh = shapes[i];
        if (sh.layer != first.layer)                       same_layer = false;
        if (sh.attrs.color != first.attrs.color)           same_color = false;
        if (sh.attrs.linetype != first.attrs.linetype)     same_lt = false;
        if (sh.attrs.lineweight != first.attrs.lineweight) same_lw = false;
        if (sh.attrs.transparency != first.attrs.transparency) same_tr = false;
        if (sh.kind != first.kind)                         same_kind = false;
    }

    {
        const std::string what =
            canvas_.selection_count()==1 && first.region ? "Region" :
            sel.size() == 1
                ? std::string(gsnap::shape_kind_name(first.kind))
                : std::to_string(sel.size()) + " objects" +
                      (same_kind ? std::string(" (") + gsnap::shape_kind_name(first.kind) + ")"
                                 : std::string());
        gui::label(ui_ctx_, s.next(float(px(22))), what, gui::TextAlign::Left, true);
    }
    s.gap(float(px(6)));

    const auto varies = [](bool same) { return same ? 0 : 1; };
    const auto apply = [&](auto&& fn) {
        canvas_.modify_shapes([&](std::vector<gsnap::Shape>& sh) {
            for (std::size_t i : sel) fn(sh[i]);
        });
    };

    gui::label(ui_ctx_, s.next(float(px(16))), "Layer", gui::TextAlign::Left, true);
    {
        std::vector<std::string> names;
        for (const auto& l : layers_.all()) names.push_back(l.name);
        std::sort(names.begin(), names.end());
        gcm::i32 pick = -1;
        std::vector<std::string> shown = names;
        if (!same_layer) { shown.insert(shown.begin(), "varies"); pick = 0; }
        else for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == (first.layer.empty() ? layers_.current() : first.layer))
                pick = gcm::i32(i);
        if (gui::dropdown(ui_ctx_, s.next(float(px(30))), "prop-layer", shown, pick) >= 0) {
            const std::string chosen = shown[std::size_t(pick)];
            if (chosen != "varies") apply([&](gsnap::Shape& sh) { sh.layer = chosen; });
        }
    }
    s.gap(float(px(6)));

    gui::label(ui_ctx_, s.next(float(px(16))), "Colour", gui::TextAlign::Left, true);
    {
        static const char* kNames[] = {"ByLayer", "Red", "Yellow", "Green",
                                       "Cyan", "Blue", "Magenta", "White"};
        std::vector<std::string> shown;
        gcm::i32 pick = -1;
        const gdoc::Color& c0 = first.attrs.color;
        if (!same_color) { shown.push_back("varies"); pick = 0; }
        else if (c0.kind == gdoc::Color::Kind::True) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u,%u,%u", c0.r, c0.g, c0.b);
            shown.push_back(buf);
            pick = 0;
        } else if (c0.kind == gdoc::Color::Kind::Index && (c0.index < 1 || c0.index > 7)) {
            shown.push_back("Colour " + std::to_string(c0.index));
            pick = 0;
        }
        const std::size_t offset = shown.size();
        for (const char* n2 : kNames) shown.push_back(n2);
        if (pick < 0) {
            pick = c0.kind == gdoc::Color::Kind::ByLayer
                       ? gcm::i32(offset)
                       : gcm::i32(offset) + c0.index;
        }
        gui::Rect row = s.next(float(px(30)));
        gui::Rect sw = row; sw.w = float(px(30));
        gui::Rect dd = row; dd.x = sw.right() + 4.0f; dd.w = row.w - sw.w - 4.0f;
        const grender::Color shown_col = canvas_.color_of_shape(first);
        gui::Color swatch = theme.get(T::ui_text);
        if (shown_col.a >= 0.0f)
            swatch = gui::Color{shown_col.r, shown_col.g, shown_col.b, shown_col.a};
        gui::color_swatch(ui_ctx_, sw.deflate(gui::Insets::all(3.0f)), swatch);
        if (gui::dropdown(ui_ctx_, dd, "prop-color", shown, pick) >= 0) {
            const std::size_t at = std::size_t(pick);
            if (at >= offset) {
                const std::size_t which = at - offset;
                const gdoc::Color chosen = which == 0 ? gdoc::Color::by_layer()
                                                      : gdoc::Color::aci(gcm::i32(which));
                apply([&](gsnap::Shape& sh) { sh.attrs.color = chosen; });
            }
        }
    }
    s.gap(float(px(6)));

    gui::label(ui_ctx_, s.next(float(px(16))), "Linetype", gui::TextAlign::Left, true);
    {
        std::vector<std::string> shown;
        gcm::i32 pick = -1;
        if (!same_lt) { shown.push_back("varies"); pick = 0; }
        const std::size_t offset = shown.size();
        shown.push_back("ByLayer");
        for (const auto& lt : linetypes_.all())
            if (lt.name != "ByLayer") shown.push_back(lt.name);
        if (pick < 0) {
            pick = gcm::i32(offset);
            for (std::size_t i = offset; i < shown.size(); ++i)
                if (shown[i] == first.attrs.linetype) pick = gcm::i32(i);
        }
        if (gui::dropdown(ui_ctx_, s.next(float(px(30))), "prop-linetype", shown, pick) >= 0) {
            const std::string chosen = shown[std::size_t(pick)];
            if (chosen != "varies")
                apply([&](gsnap::Shape& sh) { sh.attrs.linetype = chosen; });
        }
    }
    s.gap(float(px(6)));

    gui::label(ui_ctx_, s.next(float(px(16))), "Lineweight", gui::TextAlign::Left, true);
    {
        static const gdoc::Lineweight kLadder[] = {
            gdoc::Lineweight::ByLayer, gdoc::Lineweight::Default,
            gdoc::Lineweight::W009, gdoc::Lineweight::W013, gdoc::Lineweight::W015,
            gdoc::Lineweight::W018, gdoc::Lineweight::W020, gdoc::Lineweight::W025,
            gdoc::Lineweight::W030, gdoc::Lineweight::W035, gdoc::Lineweight::W040,
            gdoc::Lineweight::W050, gdoc::Lineweight::W053, gdoc::Lineweight::W070,
            gdoc::Lineweight::W100, gdoc::Lineweight::W140, gdoc::Lineweight::W200,
        };
        std::vector<std::string> shown;
        gcm::i32 pick = -1;
        if (!same_lw) { shown.push_back("varies"); pick = 0; }
        const std::size_t offset = shown.size();
        for (const gdoc::Lineweight w : kLadder) {
            if (w == gdoc::Lineweight::ByLayer)      shown.push_back("ByLayer");
            else if (w == gdoc::Lineweight::Default) shown.push_back("Default");
            else {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%.2f mm", gdoc::lineweight_mm(w));
                shown.push_back(buf);
            }
        }
        if (pick < 0)
            for (std::size_t i = 0; i < std::size(kLadder); ++i)
                if (kLadder[i] == first.attrs.lineweight) pick = gcm::i32(offset + i);
        if (pick < 0) pick = gcm::i32(offset);
        if (gui::dropdown(ui_ctx_, s.next(float(px(30))), "prop-lweight", shown, pick) >= 0) {
            const std::size_t at = std::size_t(pick);
            if (at >= offset && at - offset < std::size(kLadder)) {
                const gdoc::Lineweight chosen = kLadder[at - offset];
                apply([&](gsnap::Shape& sh) { sh.attrs.lineweight = chosen; });
            }
        }
    }
    s.gap(float(px(6)));

    {
        const std::vector<std::string> modes{"ByLayer","ByBlock","Explicit","varies"};
        gcm::i32 mode=!same_tr?3:first.attrs.transparency==-1?0:first.attrs.transparency==-2?1:2;
        if(gui::dropdown(ui_ctx_,s.next(float(px(30))),"prop-transparency-mode",modes,mode)>=0 && mode<3) {
            const int value=mode==0?-1:mode==1?-2:std::max(0,first.attrs.transparency);
            apply([&](gsnap::Shape& shape){shape.attrs.transparency=value;});
        }
        const int shown_tr = same_tr ? std::max(first.attrs.transparency, 0) : 0;
        gui::label(ui_ctx_, s.next(float(px(16))),
                   same_tr ? "Transparency  " + std::to_string(shown_tr) + "%"
                           : "Transparency  varies",
                   gui::TextAlign::Left, true);
        float v = float(shown_tr);
        if (gui::slider(ui_ctx_, s.next(float(px(26))), "prop-transparency", v, 0.0f, 90.0f)) {
            const int t = int(v + 0.5f);
            apply([&](gsnap::Shape& sh) { sh.attrs.transparency = t; });
        }
    }
    s.gap(float(px(10)));

    if (canvas_.selection_count()==1 && first.region) {
        gui::separator(ui_ctx_,s.next(float(px(8))));
        const auto region=canvas_.region_at(sel.front());
        const double k=gunit::from_metres(1,canvas_.display_unit());
        double perimeter=0;for(const auto& loop:region.loops)for(const auto& edge:loop)perimeter+=gedit::length_of(edge.shape,vol_);
        char text[160];
        std::snprintf(text,sizeof(text),"Area  %.9g %s^2",region.area(vol_)*k*k,gunit::suffix(canvas_.display_unit()));
        gui::label(ui_ctx_,s.next(float(px(18))),text);
        std::snprintf(text,sizeof(text),"Perimeter  %.9g %s",perimeter*k,gunit::suffix(canvas_.display_unit()));
        gui::label(ui_ctx_,s.next(float(px(18))),text);
        gui::label(ui_ctx_,s.next(float(px(18))),"Loops  "+std::to_string(region.loops.size()));
    } else if (sel.size() == 1) {
        gui::separator(ui_ctx_, s.next(float(px(8))));
        gui::label(ui_ctx_, s.next(float(px(18))), "Geometry", gui::TextAlign::Left, true);
        double ax, ay, bx, by;
        gcm::to_world(vol_, first.a, ax, ay);
        gcm::to_world(vol_, first.b, bx, by);
        const auto say = [&](const std::string& text) {
            gui::label(ui_ctx_, s.next(float(px(18))), text, gui::TextAlign::Left, true);
        };
        const gunit::Format fmt{canvas_.display_unit()};
        const auto len = [&](double v) { return gunit::format_length(v, fmt); };
        if (first.is_linear() && !first.is_infinite()) {
            say("start   " + len(ax) + ", " + len(ay));
            say("end     " + len(bx) + ", " + len(by));
            say("length  " + len(std::hypot(bx - ax, by - ay)));
            const double ang = std::atan2(by - ay, bx - ax) * 180.0 / 3.14159265358979323846;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "angle   %.4g deg", ang < 0 ? ang + 360.0 : ang);
            say(buf);
        } else if (first.is_curve()) {
            const double r = double(first.radius) * vol_.inv_scale();
            say("centre  " + len(ax) + ", " + len(ay));
            say("radius  " + len(r));
            say("diameter " + len(r * 2.0));
            if (first.kind == gsnap::ShapeKind::Circle) {
                say("circumference " + len(2.0 * 3.14159265358979323846 * r));
            } else {
                double sweep = first.end_angle - first.start_angle;
                while (sweep < 0) sweep += 2.0 * 3.14159265358979323846;
                char buf[48];
                std::snprintf(buf, sizeof(buf), "sweep   %.4g deg",
                              sweep * 180.0 / 3.14159265358979323846);
                say(buf);
                say("arc length " + len(sweep * r));
            }
        } else if (first.kind == gsnap::ShapeKind::Point) {
            say("at      " + len(ax) + ", " + len(ay));
        }
    }

    ui_ctx_.pop_id();
    dock_.end_content(ui_ctx_, "properties");
}

void Shell::draw_assistant_panel() {
    const auto sl = dock_.slot("assistant");
    if (!sl.visible || sl.collapsed) return;
    ai_ensure_discovery();
    dock_.begin_content(ui_ctx_, "assistant");
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    ui_ctx_.push_id("assistant-panel");
    gui::Rect inner = sl.content;

    const bool configured = ai_configured();

    const bool can_record = bool(audio_record_begin) && bool(audio_record_end);
    if (can_record) {
        gui::Rect call_bar = inner.cut_top(float(px(40)));
        ui_ctx_.draw().round_rect(call_bar.deflate(gui::Insets::all(2.0f)), float(px(8)),
                                  theme.get(ai_recording_ && ai_call_ ? T::ui_active
                                                                      : T::ui_surface_raised));
        gui::Rect cbtn = call_bar.cut_left(float(px(110)));
        ui_ctx_.push_id("call-bar");
        const bool in_call = ai_recording_ && ai_call_;
        if (gui::button(ui_ctx_, cbtn.deflate(gui::Insets::all(4.0f)),
                        gui::icon_utf8(in_call ? gui::Icon::CallEnd : gui::Icon::Call) +
                            std::string(in_call ? " End call" : " Call"),
                        configured, in_call)) {
            if (!in_call) {
                ai_call_ = true;
                call_heard_ = false;
                call_quiet_drop_ = now_seconds();
                ai_recording_ = audio_record_begin();
                if (!ai_recording_) { ai_call_ = false; log_line("could not start the microphone"); }
            } else {
                const bool pending = call_heard_;
                ai_recording_ = false;
                ai_call_ = false;
                call_heard_ = false;
                auto tail = audio_record_end ? audio_record_end() : std::vector<gcm::u8>{};
                if (pending) ai_transcribe_clip(std::move(tail), true);
            }
        }
        ui_ctx_.pop_id();
        if (in_call) {
            if (audio_record_level) {
                ai_wave_[ai_wave_at_ % ai_wave_.size()] = audio_record_level();
                ++ai_wave_at_;
            }
            gui::Rect wave = call_bar.deflate(gui::Insets::all(6.0f));
            const gui::Color col = theme.get(T::ui_accent);
            const float mid = wave.y + wave.h * 0.5f;
            const std::size_t n = ai_wave_.size();
            const int cols = std::max(2, int(wave.w));
            float prev_top = mid, prev_bot = mid;
            for (int x = 0; x < cols; ++x) {
                const float t = float(x) / float(cols - 1);
                const std::size_t idx =
                    (ai_wave_at_ + std::size_t(t * float(n - 1))) % n;
                const float v = std::min(1.0f, ai_wave_[idx] * 3.5f);
                const float half = std::max(0.75f, wave.h * 0.5f * v);
                const float px_x = wave.x + float(x);
                const float top = mid - half, bot = mid + half;
                if (x > 0) {
                    ui_ctx_.draw().line({px_x - 1.0f, prev_top}, {px_x, top}, 1.4f, col);
                    ui_ctx_.draw().line({px_x - 1.0f, prev_bot}, {px_x, bot}, 1.4f, col);
                }
                prev_top = top; prev_bot = bot;
            }
            gui::Color base = col; base.a = 0.3f;
            ui_ctx_.draw().line({wave.x, mid}, {wave.right(), mid}, 1.0f, base);
        } else {
            ui_ctx_.draw().text(call_bar.deflate(gui::Insets::all(6.0f)),
                                !configured ? "no AI server"
                                : app_config_.ai_voice ? "voice replies on" : "",
                                st.font_size_small, theme.get(T::ui_text_dim),
                                gui::TextAlign::Right);
        }
    }

    if (!configured) {
        gui::Rect cta = inner.cut_top(float(px(34)));
        ui_ctx_.push_id("ai-cta");
        if (gui::button(ui_ctx_, cta.deflate(gui::Insets::all(2.0f)),
                        "No AI server -- open Settings > AI", true)) {
            settings_open_ = true;
            settings_tab_ = 2;
            ai_settings_tab_ = 0;
            ai_page_ = 0;
        }
        ui_ctx_.pop_id();
    }

    const float in_line_h = st.font_size + 6.0f;
    const float in_text_w = inner.w - 3.0f * float(px(42)) - 4.0f -
                            st.padding.l * 2.0f;
    int in_lines = gui::text_area_lines(ui_ctx_, ai_input_, st.font_size,
                                        std::max(40.0f, in_text_w));
    in_lines = std::clamp(in_lines, 1, 5);
    gui::Rect input_row = inner;
    input_row.h = std::max(float(px(40)), in_line_h * float(in_lines) + float(px(16)));
    input_row.y = inner.bottom() - input_row.h - float(px(4));
    gui::Rect list = inner;
    list.h = std::max(0.0f, input_row.y - inner.y - float(px(6)));
    if (!ai_attach_.empty()) {
        gui::Rect chip = list.cut_bottom(float(px(24)));
        ui_ctx_.draw().round_rect(chip.deflate(gui::Insets::all(2.0f)), float(px(6)),
                                  theme.get(T::ui_surface_raised));
        ui_ctx_.push_id("attach-chip");
        const auto cin = ui_ctx_.interact(ui_ctx_.id("clear"), chip);
        ui_ctx_.draw().text(chip.deflate(gui::Insets::all(4.0f)),
                            gui::icon_utf8(gui::Icon::Attach) + " " + ai_attach_name_ +
                                "  (click to remove)",
                            st.font_size_small, theme.get(T::ui_text_dim), gui::TextAlign::Left);
        if (cin.clicked) { ai_attach_.clear(); ai_attach_name_.clear(); }
        ui_ctx_.pop_id();
    }

    {
        gui::Rect lever = list.cut_bottom(float(px(32)));
        const std::string def = app_config_.ai_reasoning_effort.empty()
                                    ? "low" : app_config_.ai_reasoning_effort;
        gui::Rect dd = lever;
        gui::Rect lab = dd.cut_left(std::max(0.0f, dd.w - float(px(150))));
        ui_ctx_.draw().text(lab.deflate(gui::Insets::xy(float(px(10)), 0)), "thinking",
                            st.font_size_small, theme.get(T::ui_text_dim),
                            gui::TextAlign::Right);
        const std::vector<std::string> efforts = {
            "(default: " + def + ")", "low", "medium", "high"};
        gcm::i32 pick = 0;
        for (std::size_t i = 1; i < efforts.size(); ++i)
            if (efforts[i] == ai_effort_override_) pick = gcm::i32(i);
        if (gui::dropdown(ui_ctx_, dd.deflate(gui::Insets::all(2.0f)), "chat-effort",
                          efforts, pick) >= 0)
            ai_effort_override_ = pick <= 0 ? std::string{}
                                            : efforts[std::size_t(std::clamp(pick, 1, 3))];
    }

    const auto wrapped = [&](const std::string& text, float width) {
        std::vector<std::string> lines;
        std::string line;
        std::string word;
        const auto flush_word = [&]() {
            if (word.empty()) return;
            const std::string trial = line.empty() ? word : line + " " + word;
            if (ui_ctx_.measure(trial, st.font_size) <= width || line.empty()) line = trial;
            else { lines.push_back(line); line = word; }
            word.clear();
        };
        for (const char c : text) {
            if (c == '\n') { flush_word(); lines.push_back(line); line.clear(); }
            else if (c == ' ') flush_word();
            else word.push_back(c);
        }
        flush_word();
        if (!line.empty()) lines.push_back(line);
        return lines;
    };

    std::string confirm_ask;
    {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        if (ai_->awaiting_tools && ai_->confirm_state == 1 &&
            ai_->call_at < ai_->pending_calls.size())
            confirm_ask = ai_tools::describe(ai_->pending_calls[ai_->call_at]);
    }
    gui::Rect confirm_area{};
    if (!confirm_ask.empty())
        confirm_area = list.cut_bottom(float(px(64)));

    std::vector<std::string> choices;
    if (!ai_->busy && confirm_ask.empty()) {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        if (!ai_->chat.empty() && ai_->chat.back().role == "assistant")
            choices = reply_choices(ai_->chat.back().text);
    }
    gui::Rect choice_area{};
    if (!choices.empty())
        choice_area = list.cut_bottom(float(px(6)) +
                                      float(px(30)) * float(choices.size()));
    std::string picked_choice;

    struct Row { bool dim{false}; std::string text; int line{-1}; int think{-1}; };
    std::vector<Row> rows;
    {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        const float w = list.w - float(px(44));
        for (std::size_t li = 0; li < ai_->chat.size(); ++li) {
            const auto& line = ai_->chat[li];
            const bool user = line.role == "user";
            const bool said = user || line.role == "assistant";
            if (!line.thinking.empty()) {
                ui_ctx_.push_id("think");
                const bool open = ui_ctx_.state_bool(
                    ui_ctx_.id(std::to_string(li)), app_config_.ai_show_thinking);
                ui_ctx_.pop_id();
                const auto tlines = wrapped(line.thinking, w);
                constexpr std::size_t kPreview = 4;
                const bool fits_anyway = tlines.size() <= kPreview;
                rows.push_back({true,
                                std::string(open || fits_anyway ? "\xe2\x96\xbe" : "\xe2\x96\xb8") +
                                    " thinking" +
                                    (open || fits_anyway
                                         ? ""
                                         : " (" + std::to_string(tlines.size()) + " lines)"),
                                -1, int(li)});
                const std::size_t shown =
                    (open || fits_anyway) ? tlines.size() : kPreview;
                for (std::size_t ti = 0; ti < shown; ++ti)
                    rows.push_back({true, "  " + tlines[ti]});
                if (shown < tlines.size())
                    rows.push_back({true, "  \xe2\x80\xa6", -1, int(li)});
            }
            bool first = true;
            for (const auto& l : wrapped(user ? "you: " + line.text : line.text, w)) {
                Row r{!said, l};
                if (first && said && !line.text.empty()) r.line = int(li);
                first = false;
                rows.push_back(std::move(r));
            }
            if (said) rows.push_back({false, ""});
        }
        if (ai_->busy && (!ai_->stream.empty() || !ai_->stream_thinking.empty())) {
            std::string visible, inline_think;
            split_thinking(ai_->stream, visible, inline_think);
            const std::string thinking = ai_->stream_thinking + inline_think;
            if (!thinking.empty()) {
                const auto tl = wrapped(thinking, w);
                const std::size_t from = tl.size() > 4 ? tl.size() - 4 : 0;
                rows.push_back({true, "\xe2\x96\xbe thinking\xe2\x80\xa6"});
                for (std::size_t ti = from; ti < tl.size(); ++ti)
                    rows.push_back({true, "  " + tl[ti]});
            }
            for (const auto& l : wrapped(visible, w)) rows.push_back({false, l});
        } else if (ai_->busy) {
            rows.push_back({true, "  ..."});
        }
    }
    ui_ctx_.push_clip(list);
    ui_ctx_.push_id("transcript");
    const float row_h = st.font_size + float(px(7));
    const float total_h = row_h * float(rows.size());
    const float max_scroll = std::max(0.0f, total_h - list.h);
    if (list.contains(ui_ctx_.input().mouse) && ui_ctx_.input().wheel != 0.0f)
        ai_scroll_ += ui_ctx_.input().wheel * 40.0f;
    ai_scroll_ = std::clamp(ai_scroll_, 0.0f, max_scroll);
    if (max_scroll > 0.0f) {
        const float th = std::max(24.0f, list.h * (list.h / total_h));
        const float ty = list.y + (list.h - th) * (1.0f - ai_scroll_ / max_scroll);
        gui::Color bar = theme.get(T::ui_text_dim);
        bar.a = 0.35f;
        ui_ctx_.draw().round_rect({list.right() - 5.0f, ty, 3.0f, th}, 1.5f, bar);
    }
    float y = list.bottom() - row_h + ai_scroll_;
    chat_hover_row_ = -1;
    const int sel_lo = (chat_sel_a_ >= 0 && chat_sel_b_ >= 0)
                           ? std::min(chat_sel_a_, chat_sel_b_) : -1;
    const int sel_hi = (chat_sel_a_ >= 0 && chat_sel_b_ >= 0)
                           ? std::max(chat_sel_a_, chat_sel_b_) : -1;
    for (auto it = rows.rbegin(); it != rows.rend() && y > list.y - row_h; ++it, y -= row_h) {
        if (it->text.empty()) continue;
        if (y >= list.bottom()) continue;
        gui::Rect r{list.x + float(px(10)), y, list.w - float(px(44)), row_h};
        const int idx = int(rows.size()) - 1 - int(it - rows.rbegin());
        if (r.contains(ui_ctx_.input().mouse)) chat_hover_row_ = idx;
        if (idx >= sel_lo && idx <= sel_hi && sel_lo >= 0) {
            gui::Color selc = theme.get(T::ui_accent);
            selc.a = 0.20f;
            gui::Rect band = r;
            if (sel_lo == sel_hi) {
                const float x0 = std::min(chat_sel_a_x_, chat_sel_b_x_);
                const float x1 = std::max(chat_sel_a_x_, chat_sel_b_x_);
                const std::size_t a =
                    ui_ctx_.caret_at(it->text, st.font_size, x0 - r.x);
                const std::size_t b =
                    ui_ctx_.caret_at(it->text, st.font_size, x1 - r.x);
                const float bx = ui_ctx_.measure(it->text.substr(0, std::min(a, b)),
                                                 st.font_size);
                const float bw = ui_ctx_.measure(it->text.substr(0, std::max(a, b)),
                                                 st.font_size) - bx;
                band.x = r.x + bx;
                band.w = std::max(2.0f, bw);
            }
            ui_ctx_.draw().rect(band, selc);
        }
        if (it->think >= 0) {
            ui_ctx_.push_id("think");
            const auto tin = ui_ctx_.interact(
                ui_ctx_.id("row" + std::to_string(it->think)), r);
            if (tin.hovered)
                ui_ctx_.draw().round_rect(r, float(px(4)), theme.get(T::ui_hover));
            if (tin.clicked) {
                bool& open = ui_ctx_.state_bool(
                    ui_ctx_.id(std::to_string(it->think)), app_config_.ai_show_thinking);
                open = !open;
            }
            ui_ctx_.pop_id();
        }
        ui_ctx_.draw().text(r, it->text, st.font_size,
                            theme.get(it->dim ? T::ui_text_dim : T::ui_text),
                            gui::TextAlign::Left);
        if (it->line >= 0) {
            const gui::Rect pb{list.right() - float(px(30)), y, float(px(24)), row_h};
            ui_ctx_.push_id(std::to_string(it->line));
            const auto pin = ui_ctx_.interact(ui_ctx_.id("play"), pb);
            if (pin.hovered)
                ui_ctx_.draw().round_rect(pb, float(px(6)), theme.get(T::ui_hover));
            const gui::Color pc = theme.get(pin.hovered ? T::ui_accent : T::ui_text_dim);
            const float cx = pb.x + pb.w * 0.5f - 3.0f, cy = pb.y + row_h * 0.5f;
            ui_ctx_.draw().triangle({cx, cy - 4.5f}, {cx, cy + 4.5f}, {cx + 8.0f, cy}, pc);
            if (pin.clicked) ai_replay_line(std::size_t(it->line));
            ui_ctx_.pop_id();
        }
    }
    if (!rows.empty()) {
        const auto& in = ui_ctx_.input();
        const bool in_text_area = list.contains(in.mouse) &&
                                  in.mouse.x < list.right() - float(px(44));
        const int max_row = int(rows.size()) - 1;
        if (in.pressed) {
            if (chat_sel_a_ >= 0 && !chat_dragging_)
                chat_sel_a_ = chat_sel_b_ = -1;
            chat_press_row_ = (in_text_area && chat_hover_row_ >= 0)
                                  ? chat_hover_row_ : -1;
            chat_press_pos_ = in.mouse;
            chat_press_time_ = in.time;
            chat_long_done_ = false;
        }
        if (in.down && chat_press_row_ >= 0) {
            const float moved = std::hypot(in.mouse.x - chat_press_pos_.x,
                                           in.mouse.y - chat_press_pos_.y);
            if (!chat_dragging_ && moved > 4.0f) {
                chat_dragging_ = true;
                chat_sel_a_ = chat_press_row_;
                chat_sel_a_x_ = chat_press_pos_.x;
            }
            if (chat_dragging_ && chat_hover_row_ >= 0) {
                chat_sel_b_ = chat_hover_row_;
                chat_sel_b_x_ = in.mouse.x;
            }
            if (!chat_dragging_ && !chat_long_done_ &&
                in.time - chat_press_time_ > 0.55) {
                chat_long_done_ = true;
                int li = rows[std::size_t(chat_press_row_)].think;
                for (int k = chat_press_row_; k >= 0 && li < 0; --k)
                    if (rows[std::size_t(k)].line >= 0) li = rows[std::size_t(k)].line;
                std::string whole;
                if (li >= 0) {
                    std::lock_guard<std::mutex> lock(ai_->mutex);
                    if (std::size_t(li) < ai_->chat.size())
                        whole = ai_->chat[std::size_t(li)].text;
                }
                if (!whole.empty()) {
                    if (clipboard_set_hook) {
                        clipboard_set_hook(whole);
                        log_.push_back("message copied");
                    } else {
                        log_.push_back("no clipboard on this platform yet");
                    }
                    int first = chat_press_row_, last = chat_press_row_;
                    while (first > 0 && rows[std::size_t(first)].line < 0 &&
                           !rows[std::size_t(first)].text.empty())
                        --first;
                    while (last + 1 <= max_row &&
                           !rows[std::size_t(last + 1)].text.empty() &&
                           rows[std::size_t(last + 1)].line < 0)
                        ++last;
                    chat_sel_a_ = first;
                    chat_sel_b_ = last;
                    chat_sel_a_x_ = list.x;
                    chat_sel_b_x_ = list.right();
                }
            }
        }
        if (in.released) {
            chat_press_row_ = -1;
            chat_dragging_ = false;
        }
        if (chat_copy_pending_) {
            chat_copy_pending_ = false;
            if (chat_sel_a_ >= 0 && chat_sel_b_ >= 0) {
                const int lo = std::clamp(std::min(chat_sel_a_, chat_sel_b_), 0, max_row);
                const int hi = std::clamp(std::max(chat_sel_a_, chat_sel_b_), 0, max_row);
                std::string text;
                if (lo == hi) {
                    const auto& t = rows[std::size_t(lo)].text;
                    const float ox = list.x + float(px(10));
                    std::size_t a = ui_ctx_.caret_at(
                        t, st.font_size, std::min(chat_sel_a_x_, chat_sel_b_x_) - ox);
                    std::size_t b = ui_ctx_.caret_at(
                        t, st.font_size, std::max(chat_sel_a_x_, chat_sel_b_x_) - ox);
                    if (b < a) std::swap(a, b);
                    text = b > a ? t.substr(a, b - a) : t;
                } else {
                    for (int k = lo; k <= hi; ++k) {
                        if (rows[std::size_t(k)].text.empty()) continue;
                        if (!text.empty()) text += "\n";
                        text += rows[std::size_t(k)].text;
                    }
                }
                if (!text.empty()) {
                    if (clipboard_set_hook) {
                        clipboard_set_hook(text);
                        log_.push_back("copied");
                    } else {
                        log_.push_back("no clipboard on this platform yet");
                    }
                }
            }
        }
    }
    ui_ctx_.pop_id();
    ui_ctx_.pop_clip();

    if (!choices.empty()) {
        gui::Rect area = choice_area;
        area.cut_top(float(px(6)));
        ui_ctx_.push_id("ai-choices");
        for (std::size_t k = 0; k < choices.size(); ++k) {
            gui::Rect row = area.cut_top(float(px(30)));
            const gui::Rect btn = row.deflate(gui::Insets::all(2.0f));
            std::string label = std::string(1, char('a' + k)) + ")  " + choices[k];
            const std::size_t fits =
                ui_ctx_.fit(label, st.font_size, btn.w - float(px(16)));
            if (fits < label.size()) label = label.substr(0, fits) + "â¦";
            ui_ctx_.push_id(std::to_string(k));
            if (gui::button(ui_ctx_, btn, label, configured)) picked_choice = choices[k];
            ui_ctx_.pop_id();
        }
        ui_ctx_.pop_id();
    }

    if (!confirm_ask.empty()) {
        gui::Rect crow = confirm_area;
        crow.h = float(px(28));
        ui_ctx_.draw().round_rect(crow, float(px(6)), theme.get(T::ui_active));
        std::string ask = "AI wants: " + confirm_ask;
        const gui::Rect crow_in = crow.deflate(gui::Insets::xy(float(px(8)), 2.0f));
        const std::size_t fits = ui_ctx_.fit(ask, st.font_size_small, crow_in.w);
        if (fits < ask.size()) ask = ask.substr(0, fits) + "\xe2\x80\xa6";
        ui_ctx_.draw().text(crow_in, ask, st.font_size_small, theme.get(T::ui_text),
                            gui::TextAlign::Left);
        gui::Rect brow{confirm_area.x, crow.bottom() + float(px(2)),
                       confirm_area.w, float(px(28))};
        gui::Rect right = brow;
        gui::Rect left = right.cut_left(right.w * 0.5f);
        std::lock_guard<std::mutex> lock(ai_->mutex);
        if (gui::button(ui_ctx_, left.deflate(gui::Insets::all(2.0f)), "Run it", true))
            ai_->confirm_state = 2;
        if (gui::button(ui_ctx_, right.deflate(gui::Insets::all(2.0f)), "Deny", true))
            ai_->confirm_state = 3;
    }

    gui::Rect send = input_row;
    const auto bottom_square = [&](gui::Rect col) {
        gui::Rect b = col;
        b.y = col.bottom() - float(px(40));
        b.h = float(px(40));
        return b;
    };
    gui::Rect upload = bottom_square(send.cut_right(float(px(42))));
    gui::Rect micb = bottom_square(send.cut_right(float(px(42))));
    gui::Rect sendb = bottom_square(send.cut_right(float(px(42))));
    ui_ctx_.push_id("ai-upload");
    if (gui::button(ui_ctx_, upload.deflate(gui::Insets::all(2.0f)),
                    gui::icon_utf8(gui::Icon::Attach), configured, !ai_attach_.empty())) {
        if (file_hook_)
            request_file(FileAsk::Attach, [this](std::string path) {
                if (path.empty()) return;
                std::FILE* f = std::fopen(path.c_str(), "rb");
                if (!f) return;
                std::fseek(f, 0, SEEK_END);
                const long n = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                std::vector<gcm::u8> bytes(std::size_t(std::max(0L, n)));
                const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
                std::fclose(f);
                bytes.resize(got);
                const std::string low = lower_ext(path);
                const std::string mime = low == "png" ? "image/png"
                                       : low == "jpg" || low == "jpeg" ? "image/jpeg"
                                       : low == "webp" ? "image/webp"
                                       : low == "pdf" ? "application/pdf" : "";
                if (mime.empty() || bytes.empty()) {
                    log_.push_back("attach: images (png/jpg/webp) ride to the model; that file cannot yet");
                    return;
                }
                if (mime == "application/pdf") {
                    log_.push_back("attach: PDF pages need rasterising first -- images work today");
                    return;
                }
                ai_attach_ = std::move(bytes);
                ai_attach_mime_ = mime;
                const auto slash = path.find_last_of('/');
                ai_attach_name_ = slash == std::string::npos ? path : path.substr(slash + 1);
            });
        else log_.push_back("attach: no file dialog on this platform yet");
    }
    ui_ctx_.pop_id();
    const bool can_mic = bool(audio_record_begin) && bool(audio_record_end);
    ui_ctx_.push_id("ai-mic");
    if (gui::button(ui_ctx_, micb.deflate(gui::Insets::all(2.0f)),
                    gui::icon_utf8(gui::Icon::Mic), configured && can_mic,
                    ai_recording_ && !ai_call_) && can_mic) {
        if (!ai_recording_) { ai_call_ = false; ai_recording_ = audio_record_begin(); }
        else if (!ai_call_) ai_stop_and_send_voice();
    }
    ui_ctx_.pop_id();
    gui::text_area(ui_ctx_, send.deflate(gui::Insets::all(2.0f)), "ai-input", ai_input_,
                   "message the assistant...");
    const bool entered = ui_ctx_.is_focused(ui_ctx_.id("ai-input")) &&
                         ui_ctx_.input().key_enter && !ui_ctx_.input().shift;
    bool send_clicked = false;
    if (ai_->busy) {
        ui_ctx_.push_id("ai-stop");
        if (gui::button(ui_ctx_, sendb.deflate(gui::Insets::all(2.0f)), "", true, true))
            ai_->abort = true;
        const gui::Vec2 c = sendb.centre();
        ui_ctx_.draw().rect({c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f},
                            theme.get(T::ui_accent_text));
        ui_ctx_.pop_id();
    } else {
        ui_ctx_.push_id("ai-send");
        send_clicked =
            gui::button(ui_ctx_, sendb.deflate(gui::Insets::all(2.0f)),
                        gui::icon_utf8(gui::Icon::Send), configured, configured);
        ui_ctx_.pop_id();
    }
    if ((entered || send_clicked) && configured && !ai_->busy && !ai_input_.empty()) {
        std::string prompt;
        prompt.swap(ai_input_);
        ai_send(std::move(prompt));
    }
    // Sent after drawing: ai_send rewrites the transcript this frame reads.
    if (!picked_choice.empty() && configured && !ai_->busy)
        ai_send(std::move(picked_choice));
    ui_ctx_.pop_id();
    dock_.end_content(ui_ctx_, "assistant");
}

void Shell::add_layer() {
    for (int n = 1; n < 1000; ++n) {
        const std::string name = "Layer " + std::to_string(n);
        if (!layers_.find(name)) {
            gdoc::Layer l;
            l.name = name;
            layers_.add(l);
            layers_.set_current(name);
            log_.push_back("new layer: " + name + " (current)");
            return;
        }
    }
}

bool Shell::delete_layer(const std::string& name) {
    if (name == "0") { log_.push_back("layer 0 cannot be deleted"); return false; }
    std::size_t used = 0;
    for (const auto& sh : canvas_.shapes()) if (sh.layer == name) ++used;
    for (const auto& ref : canvas_.blocks().references()) if (ref.layer == name) ++used;
    for (const auto& sk : sketches_) if (sk.layer == name) ++used;
    for (std::size_t b = 0; b < modeller_.bodies().size(); ++b) if (modeller_.body_layer(b) == name) ++used;
    if (used) {
        log_.push_back("layer " + name + " is in use by " + std::to_string(used) + " object(s)");
        return false;
    }
    if (layers_.current() == name) layers_.set_current("0");
    if (!layers_.remove(name)) { log_.push_back("layer " + name + " cannot be deleted"); return false; }
    log_.push_back("deleted layer " + name);
    return true;
}

void Shell::draw_sketches_panel() {
    const auto sl = dock_.slot("sketches");
    if (!sl.visible || sl.collapsed) return;
    dock_.begin_content(ui_ctx_, "sketches");
    gui::Stack s(ui_ctx_, sl.content);
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    ui_ctx_.push_id("sketches");
    if (hint_scroll_sketches_ && sel_sketch_hint_ >= 0) {
        hint_scroll_sketches_ = false;
        dock_.set_scroll("sketches",
                         std::max(0.0f, float(sel_sketch_hint_) * float(px(40)) -
                                        sl.content.h * 0.4f));
    }
    for (int i = 0; i < static_cast<int>(sketches_.size()); ++i) {
        if (sketch_parent(i)>=0) continue;
        const SketchDoc& sk = sketches_[i];
        gui::Rect row = s.next(float(px(40)));
        ui_ctx_.push_id(std::to_string(i));
        const auto in = ui_ctx_.interact(ui_ctx_.id("row"), {row.x,row.y,row.w-float(px(132)),row.h});
        const bool active = (i == active_sketch_ && mode_ == Mode::Sketch);
        if (!active && i == sel_sketch_hint_) {
            gui::Color a = theme.get(T::ui_accent);
            a.a = 0.10f;
            ui_ctx_.draw().round_rect(row, float(px(6)), a);
            ui_ctx_.draw().border(row, float(px(6)), 1.5f, theme.get(T::ui_accent));
        }
        if (active) {
            gui::Color a = theme.get(T::ui_accent);
            a.a = 0.16f;
            ui_ctx_.draw().round_rect(row, float(px(6)), a);
        } else if (in.hovered) {
            gui::Color layer = theme.get(T::ui_accent);
            layer.a = st.hover_opacity;
            ui_ctx_.draw().round_rect(row, float(px(6)), layer);
        }
        gui::Rect name{row.x + float(px(12)), row.y + float(px(4)), row.w - float(px(144)), float(px(16))};
        ui_ctx_.draw().text(name, sk.name + (sk.on_face ? "  (on a face)" : ""), st.font_size,
                            theme.get(T::ui_text), gui::TextAlign::Left);
        gui::Rect visibility{row.right()-float(px(128)),row.y+float(px(8)),float(px(48)),float(px(24))};
        if (gui::button(ui_ctx_,visibility,sketch_visible(i) ? "Hide" : "Show",true))
            set_sketch_visibility(i,sketch_visible(i) ? SketchVisibility::Hidden : SketchVisibility::Shown);
        gui::Rect automatic{row.right()-float(px(78)),visibility.y,float(px(42)),visibility.h};
        if (gui::button(ui_ctx_,automatic,"Auto",true)) set_sketch_visibility(i,SketchVisibility::Automatic);
        gui::Rect x{row.right() - float(px(32)), row.y + (row.h - float(px(24))) * 0.5f, float(px(24)), float(px(24))};
        const auto xin = ui_ctx_.interact(ui_ctx_.id("delete"), x);
        if (xin.hovered) {
            gui::Color layer = theme.get(T::ui_accent);
            layer.a = st.hover_opacity;
            ui_ctx_.draw().round_rect(x, float(px(12)), layer);
        }
        ui_ctx_.draw().text(x, "\xc3\x97", st.font_size, theme.get(xin.hovered ? T::ui_text : T::ui_text_dim), gui::TextAlign::Centre);
        if (xin.clicked) { delete_sketch(i); ui_ctx_.pop_id(); break; }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "at (%.3g, %.3g, %.3g)  normal (%.2g, %.2g, %.2g)  %zu objects  \xc2\xb7  layer %s",
                      sk.plane.ox, sk.plane.oy, sk.plane.oz, sk.plane.nx, sk.plane.ny, sk.plane.nz,
                      i == active_sketch_ ? canvas_.shapes().size() : sk.shapes.size(), sk.layer.c_str());
        gui::Rect sub{name.x, name.bottom(), name.w, float(px(14))};
        std::string line = buf;
        const float small = st.font_size_small * 0.9f;
        if (ui_ctx_.measure(line, small) > sub.w) {
            while (!line.empty() && ui_ctx_.measure(line + "\xe2\x80\xa6", small) > sub.w) line.pop_back();
            line += "\xe2\x80\xa6";
        }
        ui_ctx_.draw().text(sub, line, small, theme.get(T::ui_text_dim), gui::TextAlign::Left);
        if (in.clicked) edit_sketch(i);
        ui_ctx_.pop_id();
        s.gap(float(px(2)));
    }
    ui_ctx_.pop_id();
    dock_.end_content(ui_ctx_, "sketches");
}

void Shell::draw_settings_ai(gui::Stack& s) {
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    ai_ensure_discovery();

    if (now_seconds() - ai_settings_seen_at_ > 1.0)
        ai_status_at_ = 0.0;
    ai_settings_seen_at_ = now_seconds();
    if (now_seconds() - ai_status_at_ > 5.0) {
        ai_status_at_ = now_seconds();
        ai_refresh_status();
    }
    std::string status, active = app_config_.ai_active_url;
    std::vector<std::string> models, tts_models, stt_models, img_models, embed_models;
    {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        status = ai_->status;
        bool any_labels = false;
        for (const auto& m : ai_->models) any_labels = any_labels || m.labelled();
        const auto named = [](const std::string& id, const char* hint) {
            std::string low = id;
            for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
            return low.find(hint) != std::string::npos;
        };
        for (const auto& m : ai_->models) {
            if (!any_labels || m.chats()) models.push_back(m.id);
            if (any_labels ? m.speaks()
                           : (named(m.id, "kokoro") || named(m.id, "tts")))
                tts_models.push_back(m.id);
            if (any_labels ? m.transcribes()
                           : (named(m.id, "whisper") || named(m.id, "moonshine")))
                stt_models.push_back(m.id);
            if (any_labels ? m.makes_images()
                           : (named(m.id, "flux") || named(m.id, "sdxl") ||
                              named(m.id, "stable")))
                img_models.push_back(m.id);
            if (any_labels ? m.embeds() : named(m.id, "embed"))
                embed_models.push_back(m.id);
        }
    }

    {
        gui::Rect tabs = s.next(float(px(34)));
        static const char* names[3] = {"Servers", "Models", "Voice"};
        ui_ctx_.push_id("ai-tabs");
        for (int i = 0; i < 3; ++i) {
            gui::Rect b = tabs;
            b.w = tabs.w / 5.0f;
            b.x += b.w * float(i);
            ui_ctx_.push_id(names[i]);
            const auto tin = ui_ctx_.interact(ui_ctx_.id("tab"), b);
            if (ai_settings_tab_ == i)
                ui_ctx_.draw().round_rect(b.deflate(gui::Insets::all(2.0f)), float(px(6)),
                                          theme.get(T::ui_active));
            else if (tin.hovered)
                ui_ctx_.draw().round_rect(b.deflate(gui::Insets::all(2.0f)), float(px(6)),
                                          theme.get(T::ui_hover));
            ui_ctx_.draw().text(b, names[i], st.font_size,
                                theme.get(ai_settings_tab_ == i ? T::ui_text : T::ui_text_dim),
                                gui::TextAlign::Centre);
            if (tin.clicked) ai_settings_tab_ = i;
            ui_ctx_.pop_id();
        }
        ui_ctx_.pop_id();
        s.gap(float(px(6)));
    }

    const auto label_row = [&](const std::string& text) {
        gui::Rect lab = s.next(float(px(22)));
        ui_ctx_.draw().text(lab, text, st.font_size, theme.get(T::ui_text_dim),
                            gui::TextAlign::Left);
    };

    const auto page_header = [&](const std::string& title) {
        gui::Rect row = s.next(float(px(34)));
        gui::Rect back = row.cut_left(float(px(34)));
        ui_ctx_.push_id("ai-back");
        const auto bin = ui_ctx_.interact(ui_ctx_.id("back"), back);
        if (bin.hovered) ui_ctx_.draw().round_rect(back.deflate(gui::Insets::all(2.0f)),
                                                   float(px(6)), theme.get(T::ui_hover));
        gui::draw_icon(ui_ctx_, gui::Icon::ChevronLeft, back, theme.get(T::ui_text), 1.0f);
        ui_ctx_.pop_id();
        ui_ctx_.draw().text(row, title, st.font_size, theme.get(T::ui_text), gui::TextAlign::Left);
        s.gap(float(px(6)));
        return bin.clicked;
    };

    if (ai_settings_tab_ == 0 && ai_page_ == 0) {
        label_row("Active: " + (active.empty() ? std::string("(first found)") : active) +
                  (status.empty() ? "" : "  --  " + status));
        s.gap(float(px(6)));
        const auto found = ai_->discovery.snapshot();
        const auto server_row = [&](const std::string& name, const std::string& url,
                                    const char* tag) {
            gui::Rect row = s.next(float(px(38)));
            ui_ctx_.push_id(url);
            const std::string mark = url == active ? "\xe2\x97\x8f " : "";
            if (gui::button(ui_ctx_, row.deflate(gui::Insets::all(2.0f)),
                            mark + (name.empty() ? url : name + "  " + url) +
                                (tag[0] ? std::string("   [") + tag + "]" : ""),
                            true, url == active)) {
                ai_page_ = 2;
                ai_page_url_ = url;
                ai_status_at_ = -1e9;
            }
            ui_ctx_.pop_id();
        };
        label_row("Your servers");
        if (app_config_.ai_servers.empty()) label_row("  none yet -- add one below");
        ui_ctx_.push_id("ai-saved");
        for (const auto& srv : app_config_.ai_servers) server_row(srv.name, srv.url, "");
        ui_ctx_.pop_id();
        s.gap(float(px(6)));
        label_row("Found on this network");
        bool any_new = false;
        ui_ctx_.push_id("ai-found");
        for (const auto& d : found) {
            bool known = false;
            for (const auto& srv : app_config_.ai_servers) known = known || srv.url == d.url;
            if (known) continue;
            any_new = true;
            server_row(d.hostname, d.url, "found");
        }
        ui_ctx_.pop_id();
        if (!any_new) label_row(found.empty() ? "  listening for Lemonade beacons..."
                                              : "  every found server is already yours");
        s.gap(float(px(10)));
        gui::Rect add = s.next(float(px(38)));
        ui_ctx_.push_id("ai-add-open");
        if (gui::button(ui_ctx_, add.deflate(gui::Insets::all(2.0f)), "+  Add server", true)) {
            ai_page_ = 1;
            ai_new_url_.clear();
            ai_new_name_.clear();
            ai_new_key_.clear();
            ai_add_error_.clear();
        }
        ui_ctx_.pop_id();
    }

    if (ai_settings_tab_ == 0 && ai_page_ == 1) {
        if (page_header("Add server")) ai_page_ = 0;
        label_row("Hostname");
        gui::Rect f = s.next(float(px(38)));
        gui::text_field(ui_ctx_, f.deflate(gui::Insets::all(2.0f)), "ai-new-url",
                        ai_new_url_, "http://host:port  or  https://api.example.com");
        s.gap(float(px(6)));
        label_row("Name (optional)");
        f = s.next(float(px(38)));
        gui::text_field(ui_ctx_, f.deflate(gui::Insets::all(2.0f)), "ai-new-name",
                        ai_new_name_, "studio");
        s.gap(float(px(6)));
        label_row("API key (optional)");
        f = s.next(float(px(38)));
        gui::text_field(ui_ctx_, f.deflate(gui::Insets::all(2.0f)), "ai-new-key",
                        ai_new_key_, "sent as a bearer token");
        if (!ai_add_error_.empty()) {
            s.gap(float(px(6)));
            gui::Rect err = s.next(float(px(22)));
            ui_ctx_.draw().text(err, ai_add_error_, st.font_size,
                                theme.get(T::ui_accent), gui::TextAlign::Left);
        }
        s.gap(float(px(10)));
        gui::Rect row = s.next(float(px(38)));
        gui::Rect save = row;
        gui::Rect cancel = save.cut_right(save.w * 0.5f);
        ui_ctx_.push_id("ai-add-actions");
        if (gui::button(ui_ctx_, save.deflate(gui::Insets::all(2.0f)), "Save", true, true)) {
            if (ai_new_url_.empty()) {
                ai_add_error_ = "enter the server's address: http://host:port, or https://api.example.com for a hosted service";
            } else if (const auto c = gai::Client::from_url(ai_new_url_)) {
                gfile::AiServerRecord rec;
                rec.name = ai_new_name_;
                rec.url = c->url();
                rec.api_key = ai_new_key_;
                bool known = false;
                for (const auto& srv : app_config_.ai_servers) known = known || srv.url == rec.url;
                if (!known) app_config_.ai_servers.push_back(rec);
                if (app_config_.ai_active_url.empty()) app_config_.ai_active_url = rec.url;
                if (!save_app_config_checked()) {
                    ai_add_error_ = "could not write the config file -- check disk permissions";
                } else {
                    ai_add_error_.clear();
                    ai_page_ = 0;
                    ai_status_at_ = -1e9;
                }
            } else {
                ai_add_error_ = "that address did not read -- use http://host:port or https://host";
            }
        }
        if (gui::button(ui_ctx_, cancel.deflate(gui::Insets::all(2.0f)), "Cancel", true))
            ai_page_ = 0;
        ui_ctx_.pop_id();
    }

    if (ai_settings_tab_ == 0 && ai_page_ == 2) {
        gfile::AiServerRecord* rec = ai_record(ai_page_url_);
        if (page_header(rec && !rec->name.empty() ? rec->name : ai_page_url_)) ai_page_ = 0;
        label_row(ai_page_url_);
        label_row(status.empty() ? "checking..." : status);
        s.gap(float(px(8)));

        gui::Rect row = s.next(float(px(38)));
        ui_ctx_.push_id("ai-detail");
        const bool is_active = ai_page_url_ == active;
        if (gui::button(ui_ctx_, row.deflate(gui::Insets::all(2.0f)),
                        is_active ? "This is the active server" : "Use this server",
                        true, is_active) && !is_active) {
            app_config_.ai_active_url = ai_page_url_;
            save_app_config();
            ai_status_at_ = -1e9;
        }

        s.gap(float(px(8)));
        label_row("Default model on this server");
        if (!models.empty()) {
            std::vector<std::string> rows2{"(inherit the global pick)"};
            rows2.insert(rows2.end(), models.begin(), models.end());
            gcm::i32 pick = 0;
            if (rec)
                for (std::size_t i = 0; i < models.size(); ++i)
                    if (models[i] == rec->default_model) pick = gcm::i32(i + 1);
            gui::Rect dd = s.next(float(px(38)));
            if (gui::dropdown(ui_ctx_, dd, "server-model", rows2, pick) >= 0) {
                if (!rec) {
                    gfile::AiServerRecord fresh;
                    fresh.url = ai_page_url_;
                    app_config_.ai_servers.push_back(fresh);
                    rec = &app_config_.ai_servers.back();
                }
                rec->default_model = pick <= 0 ? std::string{} : models[std::size_t(pick - 1)];
                save_app_config();
            }
        } else {
            label_row("  connect to list its models");
        }

        // Re-fetch the pointer: the vector may have grown.
        const auto adopt = [&]() -> gfile::AiServerRecord* {
            if (gfile::AiServerRecord* r = ai_record(ai_page_url_)) return r;
            gfile::AiServerRecord fresh;
            fresh.url = ai_page_url_;
            for (const auto& d : ai_->discovery.snapshot())
                if (d.url == ai_page_url_) fresh.name = d.hostname;
            app_config_.ai_servers.push_back(fresh);
            return &app_config_.ai_servers.back();
        };

        s.gap(float(px(8)));
        label_row("Voice -- the model that speaks the replies");
        if (!tts_models.empty()) {
            std::vector<std::string> rows2{"(auto: first speech model)"};
            rows2.insert(rows2.end(), tts_models.begin(), tts_models.end());
            gcm::i32 pick = 0;
            if (rec)
                for (std::size_t i = 0; i < tts_models.size(); ++i)
                    if (tts_models[i] == rec->default_tts) pick = gcm::i32(i + 1);
            gui::Rect dd = s.next(float(px(38)));
            if (gui::dropdown(ui_ctx_, dd, "server-tts", rows2, pick) >= 0) {
                rec = adopt();
                rec->default_tts = pick <= 0 ? std::string{} : tts_models[std::size_t(pick - 1)];
                save_app_config();
            }
        } else {
            label_row("  connect to list its speech models");
        }
        s.gap(float(px(4)));
        label_row("Voice name (for models with several voices)");
        {
            std::string vname = rec ? rec->tts_voice : std::string{};
            gui::Rect f = s.next(float(px(38)));
            if (gui::text_field(ui_ctx_, f.deflate(gui::Insets::all(2.0f)), "server-tts-voice",
                                vname, "af_heart (the model's default)")) {
                rec = adopt();
                rec->tts_voice = vname;
                save_app_config();
            }
        }
        s.gap(float(px(8)));
        label_row("Transcription -- the model that hears you");
        if (!stt_models.empty()) {
            std::vector<std::string> rows2{"(auto: first transcription model)"};
            rows2.insert(rows2.end(), stt_models.begin(), stt_models.end());
            gcm::i32 pick = 0;
            if (rec)
                for (std::size_t i = 0; i < stt_models.size(); ++i)
                    if (stt_models[i] == rec->default_stt) pick = gcm::i32(i + 1);
            gui::Rect dd = s.next(float(px(38)));
            if (gui::dropdown(ui_ctx_, dd, "server-stt", rows2, pick) >= 0) {
                rec = adopt();
                rec->default_stt = pick <= 0 ? std::string{} : stt_models[std::size_t(pick - 1)];
                save_app_config();
            }
        } else {
            label_row("  connect to list its transcription models");
        }

        s.gap(float(px(8)));
        label_row("API key");
        {
            std::string key = rec ? rec->api_key : std::string{};
            gui::Rect f = s.next(float(px(38)));
            if (gui::text_field(ui_ctx_, f.deflate(gui::Insets::all(2.0f)), "server-key",
                                key, "none")) {
                if (!rec) {
                    gfile::AiServerRecord fresh;
                    fresh.url = ai_page_url_;
                    app_config_.ai_servers.push_back(fresh);
                    rec = &app_config_.ai_servers.back();
                }
                rec->api_key = key;
                save_app_config();
            }
        }

        if (rec) {
            s.gap(float(px(10)));
            row = s.next(float(px(38)));
            if (gui::button(ui_ctx_, row.deflate(gui::Insets::all(2.0f)), "Remove this server",
                            true)) {
                for (std::size_t i = 0; i < app_config_.ai_servers.size(); ++i)
                    if (app_config_.ai_servers[i].url == ai_page_url_) {
                        app_config_.ai_servers.erase(app_config_.ai_servers.begin() + long(i));
                        break;
                    }
                if (active == ai_page_url_) app_config_.ai_active_url.clear();
                save_app_config();
                ai_page_ = 0;
            }
        } else {
            s.gap(float(px(10)));
            row = s.next(float(px(38)));
            if (gui::button(ui_ctx_, row.deflate(gui::Insets::all(2.0f)),
                            "Save to my servers", true)) {
                gfile::AiServerRecord fresh;
                fresh.url = ai_page_url_;
                for (const auto& d : ai_->discovery.snapshot())
                    if (d.url == ai_page_url_) fresh.name = d.hostname;
                app_config_.ai_servers.push_back(fresh);
                save_app_config();
            }
        }
        ui_ctx_.pop_id();
    }

    if (ai_settings_tab_ == 1) {
        std::string url = active;
        if (url.empty()) {
            const auto found = ai_->discovery.snapshot();
            if (!found.empty()) url = found.front().url;
            else if (!app_config_.ai_servers.empty()) url = app_config_.ai_servers.front().url;
        }
        gfile::AiServerRecord* rec = ai_record(url);
        label_row(url.empty()
                      ? "No server -- add one under Servers"
                      : "Models on " + (rec && !rec->name.empty() ? rec->name : url) +
                            (status.empty() ? "" : "  --  " + status));
        const auto adopt_url = [&]() -> gfile::AiServerRecord* {
            if (gfile::AiServerRecord* r = ai_record(url)) return r;
            gfile::AiServerRecord fresh;
            fresh.url = url;
            for (const auto& d : ai_->discovery.snapshot())
                if (d.url == url) fresh.name = d.hostname;
            app_config_.ai_servers.push_back(fresh);
            return &app_config_.ai_servers.back();
        };
        const auto role_row = [&](const char* label, const char* id,
                                  const std::vector<std::string>& list,
                                  std::string gfile::AiServerRecord::* field,
                                  const std::string& fallback = {}) {
            s.gap(float(px(6)));
            label_row(label);
            if (url.empty() || list.empty()) {
                label_row(url.empty() ? "  waiting for a server"
                                      : "  none on this server");
                return;
            }
            std::vector<std::string> rows2{"(auto: first suitable)"};
            rows2.insert(rows2.end(), list.begin(), list.end());
            gcm::i32 pick = 0;
            const std::string& want =
                (rec && !(rec->*field).empty()) ? rec->*field : fallback;
            for (std::size_t i = 0; i < list.size(); ++i)
                if (list[i] == want) pick = gcm::i32(i + 1);
            const bool missing = !want.empty() && pick == 0;
            if (missing) {
                rows2.push_back(want + "  (saved -- not on this server now)");
                pick = gcm::i32(rows2.size() - 1);
            }
            gui::Rect dd = s.next(float(px(38)));
            if (gui::dropdown(ui_ctx_, dd, id, rows2, pick) >= 0 &&
                !(missing && pick == gcm::i32(rows2.size() - 1))) {
                rec = adopt_url();
                rec->*field = pick <= 0 ? std::string{}
                                        : list[std::size_t(std::min<gcm::i32>(
                                              pick, gcm::i32(list.size())) - 1)];
                save_app_config();
            }
        };
        role_row("Completion -- chat and tool calling", "role-chat",
                 models, &gfile::AiServerRecord::default_model,
                 app_config_.ai_model);
        role_row("Speech (TTS) -- the assistant's voice", "role-tts",
                 tts_models, &gfile::AiServerRecord::default_tts);
        role_row("Transcription (STT) -- hears you", "role-stt",
                 stt_models, &gfile::AiServerRecord::default_stt);
        role_row("Image generation", "role-img",
                 img_models, &gfile::AiServerRecord::default_img);
        role_row("Embeddings -- the standards library search", "role-embed",
                 embed_models, &gfile::AiServerRecord::default_embed);
        s.gap(float(px(8)));
        gui::Rect row = s.next(float(px(34)));
        bool think = app_config_.ai_show_thinking;
        if (gui::toggle(ui_ctx_, row.deflate(gui::Insets::all(2.0f)), "Show thinking", think)) {
            app_config_.ai_show_thinking = think;
            save_app_config();
        }

        s.gap(float(px(8)));
        label_row("Reasoning effort -- how long the model may think");
        {
            static const std::vector<std::string> efforts = {"low", "medium", "high"};
            gcm::i32 pick = 0;
            for (std::size_t i = 0; i < efforts.size(); ++i)
                if (efforts[i] == app_config_.ai_reasoning_effort) pick = gcm::i32(i);
            gui::Rect dd2 = s.next(float(px(38)));
            if (gui::dropdown(ui_ctx_, dd2, "reasoning-effort", efforts, pick) >= 0) {
                app_config_.ai_reasoning_effort = efforts[std::size_t(std::clamp(pick, 0, 2))];
                save_app_config();
            }
        }
    }

    if (ai_settings_tab_ == 2) {
        gui::Rect row = s.next(float(px(34)));
        bool v = app_config_.ai_voice;
        if (gui::toggle(ui_ctx_, row.deflate(gui::Insets::all(2.0f)), "Speak replies", v)) {
            app_config_.ai_voice = v;
            save_app_config();
        }
        const auto device_row = [&](const char* label, const char* id,
                                    const std::vector<std::string>& devices, std::string& chosen) {
            s.gap(float(px(8)));
            label_row(label);
            std::vector<std::string> rows2{"System default"};
            rows2.insert(rows2.end(), devices.begin(), devices.end());
            gcm::i32 pick = 0;
            for (std::size_t i = 0; i < devices.size(); ++i)
                if (devices[i] == chosen) pick = gcm::i32(i + 1);
            gui::Rect dd = s.next(float(px(38)));
            if (gui::dropdown(ui_ctx_, dd, id, rows2, pick) >= 0) {
                chosen = pick <= 0 ? std::string{} : devices[std::size_t(pick - 1)];
                save_app_config();
            }
        };
        device_row("Speakers", "audio-out", audio_outputs_, app_config_.audio_output);
        device_row("Microphone", "audio-in", audio_inputs_, app_config_.audio_input);
    }
}

void Shell::draw_project_ask() {
    if (!project_ask_enabled_ || project_ask_done_ || settings_open_) return;
    if (!document_blank()) { project_ask_done_ = true; return; }
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    const gui::Rect screen{0, 0, float(vw_), float(vh_)};

    std::vector<const std::string*> suggest;
    if (project_ask_step_ == 1) {
        std::string low = project_location_;
        for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const auto& r : app_config_.recent_locations) {
            if (suggest.size() >= 4) break;
            if (r == project_location_) continue;
            std::string rl = r;
            for (char& c : rl) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (low.empty() || rl.find(low) != std::string::npos)
                suggest.push_back(&r);
        }
    }

    const float cw = std::min(float(vw_) - float(px(80)), float(px(440)));
    const float ch = float(px(240)) + float(px(30)) * float(suggest.size());
    const gui::Rect card{(float(vw_) - cw) * 0.5f, (float(vh_) - ch) * 0.5f, cw, ch};

    ui_ctx_.begin_overlay(screen);
    gui::Color scrim{0.0f, 0.0f, 0.0f, 0.25f};
    ui_ctx_.draw().rect(screen, scrim);
    ui_ctx_.draw().shadow(card, float(px(12)), float(px(20)), theme.get(T::ui_shadow));
    ui_ctx_.draw().round_rect(card, float(px(12)), theme.get(T::ui_surface_raised));
    ui_ctx_.draw().border(card, float(px(12)), 1.0f, theme.get(T::ui_border));

    ui_ctx_.push_id("project-ask");
    gui::Rect inner = card.deflate(gui::Insets::all(float(px(16))));
    gui::Rect head = inner.cut_top(float(px(32)));
    ui_ctx_.draw().text(head,
                        project_ask_step_ == 0 ? "What are you making?"
                                               : "Where is the project?",
                        st.font_size_title, theme.get(T::ui_text), gui::TextAlign::Left);
    {
        gui::Rect close{head.right() - float(px(28)), head.y, float(px(28)), float(px(28))};
        const auto xin = ui_ctx_.interact(ui_ctx_.id("skip"), close);
        if (xin.hovered)
            ui_ctx_.draw().round_rect(close, float(px(14)), theme.get(T::ui_hover));
        ui_ctx_.draw().text(close, "\xc3\x97", st.font_size * 1.2f,
                            theme.get(xin.hovered ? T::ui_text : T::ui_text_dim),
                            gui::TextAlign::Centre);
        if (xin.clicked) project_ask_done_ = true;
    }
    inner.cut_top(float(px(8)));

    if (project_ask_step_ == 0) {
        gui::Rect sub = inner.cut_top(float(px(24)));
        ui_ctx_.draw().text(sub, "The assistant drafts to the rules of the discipline.",
                            st.font_size, theme.get(T::ui_text_dim), gui::TextAlign::Left);
        inner.cut_top(float(px(8)));
        struct Choice { const char* label; const char* type; };
        static const Choice kChoices[3] = {
            {"Architectural -- plans, walls, rooms, codes", "architecture"},
            {"Mechanical -- parts, assemblies, fabrication", "mechanical"},
            {"General -- schematics and other drawings", "general"},
        };
        for (const auto& c : kChoices) {
            gui::Rect row = inner.cut_top(float(px(40)));
            ui_ctx_.push_id(c.type);
            if (gui::button(ui_ctx_, row.deflate(gui::Insets::all(3.0f)), c.label, true)) {
                set_project_type(c.type);
                if (project_type_ == "architecture") { project_ask_step_ = 1; project_ask_done_ = false; }
                else project_ask_done_ = true;
            }
            ui_ctx_.pop_id();
        }
    } else {
        gui::Rect sub = inner.cut_top(float(px(40)));
        ui_ctx_.draw().text(sub,
                            "City, county, state and ZIP pick the building codes. The "
                            "assistant fetches them into the standards library now.",
                            st.font_size, theme.get(T::ui_text_dim), gui::TextAlign::Left);
        inner.cut_top(float(px(8)));
        gui::Rect field = inner.cut_top(float(px(40)));
        const auto previous_location = project_location_;
        gui::text_field(ui_ctx_, field.deflate(gui::Insets::all(2.0f)), "project-location",
                        project_location_, "Fresno, Fresno County, CA 93650, USA");
        if (project_location_ != previous_location) touch_metadata("project location");
        // Id taken outside the rows' scope; inside it the label hashes differently.
        const gui::Id loc_field_id = ui_ctx_.id("project-location");
        ui_ctx_.push_id("loc-suggest");
        for (const std::string* r : suggest) {
            gui::Rect row = inner.cut_top(float(px(30)));
            ui_ctx_.push_id(*r);
            const auto rin = ui_ctx_.interact(ui_ctx_.id("row"), row);
            if (rin.hovered)
                ui_ctx_.draw().round_rect(row.deflate(gui::Insets::all(2.0f)),
                                          float(px(6)), theme.get(T::ui_hover));
            ui_ctx_.draw().text(row.deflate(gui::Insets::xy(float(px(12)), 0)), *r,
                                st.font_size, theme.get(T::ui_text_dim),
                                gui::TextAlign::Left);
            if (rin.clicked) {
                project_location_ = *r;
                touch_metadata("project location");
                ui_ctx_.state_i32(loc_field_id) = gcm::i32(project_location_.size());
            }
            ui_ctx_.pop_id();
        }
        ui_ctx_.pop_id();
        inner.cut_top(float(px(10)));
        gui::Rect row = inner.cut_top(float(px(38)));
        gui::Rect go = row;
        gui::Rect skip = go.cut_right(go.w * 0.4f);
        const bool entered = ui_ctx_.is_focused(ui_ctx_.id("project-location")) &&
                             ui_ctx_.input().key_enter;
        ui_ctx_.push_id("loc-actions");
        if ((gui::button(ui_ctx_, go.deflate(gui::Insets::all(2.0f)), "Continue", true, true) ||
             entered)) {
            project_ask_done_ = true;
            if (!project_location_.empty()) {
                auto& rec = app_config_.recent_locations;
                rec.erase(std::remove(rec.begin(), rec.end(), project_location_),
                          rec.end());
                rec.insert(rec.begin(), project_location_);
                if (rec.size() > 8) rec.resize(8);
                save_app_config();
            }
            if (!project_location_.empty() && ai_configured()) {
                ai_send("PROJECT SETUP. This architecture project is located in: " +
                        project_location_ +
                        ". Identify the governing jurisdiction (city, county, "
                        "state), check the library with search_standards, and "
                        "where its residential code and local amendments are "
                        "missing, find them online and archive the relevant "
                        "parts (fetch_url with save_title, or save_standard). "
                        "Then extract the numeric thresholds (entrance and "
                        "egress door widths) into save_plan_rules, citing the "
                        "section, so the plan checker enforces this "
                        "jurisdiction. Finish with one short summary of what "
                        "governs this project.");
            }
        }
        if (gui::button(ui_ctx_, skip.deflate(gui::Insets::all(2.0f)), "Skip", true))
            project_ask_done_ = true;
        ui_ctx_.pop_id();
    }
    ui_ctx_.pop_id();
    ui_ctx_.end_overlay();
}

void Shell::draw_settings_panel() {
    if (!settings_open_) return;
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    const gui::Rect screen{0, 0, float(vw_), float(vh_)};
    const float cw = std::min(float(vw_) - float(px(80)), float(px(720)));
    const float ch = float(vh_) - float(px(72));
    const gui::Rect card{(float(vw_) - cw) * 0.5f, (float(vh_) - ch) * 0.5f, cw, ch};

    ui_ctx_.begin_overlay(screen);
    gui::Color scrim{0.0f, 0.0f, 0.0f, 0.35f};
    ui_ctx_.draw().rect(screen, scrim);
    ui_ctx_.draw().shadow(card, float(px(12)), float(px(20)), theme.get(T::ui_shadow));
    ui_ctx_.draw().round_rect(card, float(px(12)), theme.get(T::ui_surface_raised));
    ui_ctx_.draw().border(card, float(px(12)), 1.0f, theme.get(T::ui_border));

    ui_ctx_.push_id("settings");
    gui::Rect inner = card.deflate(gui::Insets::all(float(px(16))));
    gui::Rect head = inner;
    head.h = float(px(32));
    ui_ctx_.draw().text(head, "Settings", st.font_size_title * 1.15f, theme.get(T::ui_text), gui::TextAlign::Left);
    {
        gui::Rect close{head.right() - float(px(28)), head.y, float(px(28)), float(px(28))};
        const auto xin = ui_ctx_.interact(ui_ctx_.id("close"), close);
        if (xin.hovered) ui_ctx_.draw().round_rect(close, float(px(14)), theme.get(T::ui_hover));
        ui_ctx_.draw().text(close, "\xc3\x97", st.font_size * 1.2f,
                            theme.get(xin.hovered ? T::ui_text : T::ui_text_dim), gui::TextAlign::Centre);
        if (xin.clicked) settings_open_ = false;
    }
    inner.cut_top(float(px(36)));
    {
        gui::Rect tabs = inner;
        tabs.h = float(px(32));
        ui_ctx_.push_id("tabs");
        const char* names[5] = {"General", "Graphics", "AI", "Drafting", "MCP"};
        for (int i = 0; i < 5; ++i) {
            gui::Rect b = tabs;
            b.w = tabs.w / 5.0f;
            b.x += b.w * float(i);
            ui_ctx_.push_id(names[i]);
            const auto tin = ui_ctx_.interact(ui_ctx_.id("tab"), b);
            if (settings_tab_ == i)
                ui_ctx_.draw().round_rect(b.deflate(gui::Insets::all(2.0f)), float(px(6)),
                                          theme.get(T::ui_active));
            else if (tin.hovered)
                ui_ctx_.draw().round_rect(b.deflate(gui::Insets::all(2.0f)), float(px(6)),
                                          theme.get(T::ui_hover));
            ui_ctx_.draw().text(b, names[i], st.font_size,
                                theme.get(settings_tab_ == i ? T::ui_text : T::ui_text_dim),
                                gui::TextAlign::Centre);
            ui_ctx_.announce(ui_ctx_.id("tab"),gui::Context::Role::Tab,names[i],"",b,settings_tab_==i);
            if (tin.clicked) settings_tab_ = i;
            ui_ctx_.pop_id();
        }
        ui_ctx_.pop_id();
        inner.cut_top(float(px(40)));
    }
    const int page_key = settings_tab_ * 100 + ai_settings_tab_ * 10 + ai_page_;
    if (page_key != settings_scroll_page_) {
        settings_scroll_page_ = page_key;
        drafting_fields_.clear();
        settings_scroll_ = gui::ScrollState{};
    }
    ui_ctx_.push_id("settings-scroll");
    const gui::Rect page_view = inner;
    const gui::Rect page_content = gui::begin_scroll(ui_ctx_, page_view, settings_scroll_);
    gui::Rect page_run = page_content;
    page_run.h = 100000.0f;
    gui::Stack s(ui_ctx_, page_run);

    if (settings_tab_ == 2) draw_settings_ai(s);
    if (settings_tab_ == 3) draw_settings_drafting(s);
    if (settings_tab_ == 4) draw_settings_mcp(s);

    if (settings_tab_ == 1) {
        gui::Rect lab2 = s.next(float(px(22)));
        ui_ctx_.draw().text(lab2, "Object auto-smoothing", st.font_size,
                            theme.get(T::ui_text_dim), gui::TextAlign::Left);
        gui::Rect row2 = s.next(float(px(38)));
        bool smooth = modeller_.smoothing_on();
        gui::Rect half = row2.cut_left(row2.w * 0.5f);
        if (gui::toggle(ui_ctx_, half.deflate(gui::Insets::all(2.0f)), "Smooth", smooth)) {
            modeller_.set_smoothing(smooth, modeller_.smoothing_angle());
            mesh_dirty_ = true;
            app_config_.smoothing = smooth ? 1 : 0;
            save_app_config();
        }
        static const std::vector<std::string> angles = {"15\xc2\xb0", "30\xc2\xb0", "45\xc2\xb0", "60\xc2\xb0"};
        static const double angle_v[4] = {15.0, 30.0, 45.0, 60.0};
        for (int i = 0; i < 4; ++i)
            if (std::fabs(angle_v[i] - modeller_.smoothing_angle()) < 1e-9) smooth_angle_choice_ = i;
        if (gui::dropdown(ui_ctx_, row2.deflate(gui::Insets::all(2.0f)), "smooth-angle", angles,
                          smooth_angle_choice_) >= 0) {
            modeller_.set_smoothing(modeller_.smoothing_on(),
                                    angle_v[std::clamp(smooth_angle_choice_, 0, 3)]);
            mesh_dirty_ = true;
            app_config_.smoothing_angle = modeller_.smoothing_angle();
            save_app_config();
        }

        s.gap(float(px(8)));
        lab2 = s.next(float(px(22)));
        ui_ctx_.draw().text(lab2, "Curve quality", st.font_size,
                            theme.get(T::ui_text_dim), gui::TextAlign::Left);
        static const std::vector<std::string> curves = {"Draft", "Standard", "Fine"};
        static const double curve_tol[3] = {0.005, 0.002, 0.0005};
        gui::Rect dd2 = s.next(float(px(38)));
        if (gui::dropdown(ui_ctx_, dd2, "curve-quality", curves, curve_quality_) >= 0) {
            canvas_.spline_tolerance() = curve_tol[std::clamp(curve_quality_, 0, 2)];
            app_config_.curve_quality = curve_quality_;
            save_app_config();
        }

        s.gap(float(px(8)));
        lab2 = s.next(float(px(22)));
        ui_ctx_.draw().text(lab2, "Anti-aliasing", st.font_size,
                            theme.get(T::ui_text_dim), gui::TextAlign::Left);
        static const std::vector<std::string> aa = {"Off", "2x", "4x", "8x", "16x"};
        static const uint32_t aa_v[5] = {1, 2, 4, 8, 16};
        dd2 = s.next(float(px(38)));
        if (gui::dropdown(ui_ctx_, dd2, "aa", aa, msaa_choice_) >= 0) {
            graphics_msaa_ = aa_v[std::clamp(msaa_choice_, 0, 4)];
            app_config_.msaa = static_cast<int>(graphics_msaa_);
            save_app_config();
        }
    }

    if (settings_tab_ == 0) {
    static const gunit::Length units[] = {gunit::Length::Metre, gunit::Length::Millimetre,
                                          gunit::Length::Centimetre, gunit::Length::Inch,
                                          gunit::Length::Foot};
    std::vector<std::string> names;
    for (gunit::Length u : units) names.push_back(gunit::name(u));
    for (int i = 0; i < 5; ++i) if (units[i] == canvas_.display_unit()) unit_choice_ = i;
    gui::Rect lab = s.next(float(px(22)));
    ui_ctx_.draw().text(lab, "Units", st.font_size, theme.get(T::ui_text_dim), gui::TextAlign::Left);
    gui::Rect dd = s.next(float(px(38)));
    if (gui::dropdown(ui_ctx_, dd, "units", names, unit_choice_) >= 0)
        adopt_display_unit(units[std::clamp(unit_choice_, 0, 4)]);

    s.gap(float(px(8)));
    lab = s.next(float(px(22)));
    ui_ctx_.draw().text(lab, "Default units (new documents)", st.font_size,
                        theme.get(T::ui_text_dim), gui::TextAlign::Left);
    {
        static const char* suffixes[5] = {"m", "mm", "cm", "in", "ft"};
        gcm::i32 def_choice = 0;
        for (int i = 0; i < 5; ++i) if (app_config_.default_unit == suffixes[i]) def_choice = i;
        gui::Rect ddd = s.next(float(px(38)));
        if (gui::dropdown(ui_ctx_, ddd, "default-units", names, def_choice) >= 0) {
            app_config_.default_unit = suffixes[std::clamp(def_choice, 0, 4)];
            save_app_config();
        }
    }



    s.gap(float(px(8)));
    lab = s.next(float(px(22)));
    ui_ctx_.draw().text(lab, "Dimension display", st.font_size, theme.get(T::ui_text_dim), gui::TextAlign::Left);
    static const std::vector<std::string> precisions = {"0", "0.0", "0.00", "0.000", "0.0000"};
    dd = s.next(float(px(38)));
    if (gui::dropdown(ui_ctx_, dd, "precision", precisions, precision_) >= 0) {
        app_config_.dim_precision = precision_;
        save_app_config();
    }

    s.gap(float(px(8)));
    lab = s.next(float(px(22)));
    ui_ctx_.draw().text(lab, "Pointer input", st.font_size, theme.get(T::ui_text_dim), gui::TextAlign::Left);
    gui::Rect row = s.next(float(px(38)));
    bool polar = canvas_.polar().enabled, ortho = canvas_.polar().ortho;
    gui::Rect b1 = row.cut_left(row.w * 0.5f);
    if (gui::toggle(ui_ctx_, b1.deflate(gui::Insets::all(2.0f)), "Polar tracking", polar)) {
        canvas_.polar().enabled = polar;
        app_config_.polar = polar ? 1 : 0;
        save_app_config();
    }
    if (gui::toggle(ui_ctx_, row.deflate(gui::Insets::all(2.0f)), "Ortho", ortho)) {
        canvas_.polar().ortho = ortho;
        app_config_.ortho = ortho ? 1 : 0;
        save_app_config();
    }

    s.gap(float(px(8)));
    lab = s.next(float(px(22)));
    ui_ctx_.draw().text(lab, "Theme", st.font_size, theme.get(T::ui_text_dim), gui::TextAlign::Left);
    row = s.next(float(px(38)));
    ui_ctx_.push_id("theme");
    for (const char* name : {"GLCAD Light", "GLCAD Dark"}) {
        gui::Rect b = row.cut_left(row.w * (name[6] == 'L' ? 0.5f : 1.0f));
        ui_ctx_.push_id(name);
        if (gui::button(ui_ctx_, b.deflate(gui::Insets::all(2.0f)), name + 6)) {
            themes_.set_follow_system(false);
            themes_.activate(name);
            app_config_.theme = name;
            save_app_config();
        }
        ui_ctx_.pop_id();
    }
    ui_ctx_.pop_id();
    }
    gui::end_scroll(ui_ctx_, page_view, settings_scroll_,
                    100000.0f - s.remaining().h);
    ui_ctx_.pop_id();
    if (ui_ctx_.input().pressed && !card.contains(ui_ctx_.input().mouse))
        settings_open_ = false;
    ui_ctx_.pop_id();
    ui_ctx_.end_overlay();
}


void Shell::draw_layer_color_picker() {
    if (layer_color_target_.empty()) return;
    const auto slot = dock_.slot("layers");
    const auto* layer = layers_.find(layer_color_target_);
    if (!layer || read_only_ || active_tab_ != layer_color_tab_ || !slot.visible || slot.collapsed || input_.key_escape) {
        layer_color_target_.clear(); return;
    }
    const auto& theme = themes_.active();
    const float gap = float(px(6)), cell = float(px(40)), pad = float(px(12));
    const float width = 2 * pad + 5 * cell + 4 * gap;
    const float height = float(px(78)) + 5 * cell + 4 * gap;
    const auto screen = ui_ctx_.viewport();
    gui::Rect panel{std::clamp(layer_color_anchor_.right() - width, screen.x, std::max(screen.x, screen.right() - width)),
                    std::clamp(layer_color_anchor_.bottom() + gap, screen.y, std::max(screen.y, screen.bottom() - height)), width, height};
    layer_color_panel_ = panel;
    if (input_.pressed && !panel.contains(input_.mouse) && !layer_color_anchor_.contains(input_.mouse)) {
        layer_color_target_.clear(); return;
    }
    ui_ctx_.begin_overlay(panel);
    ui_ctx_.push_id("layer-color-picker");
    ui_ctx_.draw().shadow(panel, float(px(8)), float(px(8)), theme.get(T::ui_shadow));
    ui_ctx_.draw().round_rect(panel, float(px(8)), theme.get(T::ui_surface_raised));
    ui_ctx_.draw().border(panel, float(px(8)), 1, theme.get(T::ui_border));
    gui::Rect heading{panel.x + pad, panel.y + pad, panel.w - 2 * pad, float(px(24))};
    gui::label(ui_ctx_, heading, "Layer color: " + layer_color_target_);
    heading.y += float(px(23)); heading.h = float(px(18));
    gui::label(ui_ctx_, heading, "25 CAD indexed colors", gui::TextAlign::Left, true);
    static constexpr int colors[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 20, 30, 40, 60,
                                    80, 100, 120, 140, 160, 180, 200, 220, 240, 250, 252, 254};
    for (int i = 0; i < 25; ++i) {
        const int index = colors[i];
        gui::Rect tile{panel.x + pad + (i % 5) * (cell + gap), panel.y + float(px(58)) + (i / 5) * (cell + gap), cell, cell};
        const std::string label = "ACI " + std::to_string(index);
        const auto id = ui_ctx_.id(label);
        const auto in = ui_ctx_.interact(id, tile);
        ui_ctx_.register_focusable(id, tile, label);
        const bool selected = layer->color.kind == gdoc::Color::Kind::Index && layer->color.index == index;
        ui_ctx_.announce(id, gui::Context::Role::Button, label, "", tile, selected);
        if (in.hovered) ui_ctx_.draw().round_rect(tile, float(px(3)), theme.get(T::ui_hover));
        gcm::u8 r, g, b; gdoc::aci_rgb(index, r, g, b);
        auto color = gui::Color::rgb(r / 255.f, g / 255.f, b / 255.f);
        if (index == 7) color = theme.get(T::ui_text);
        gui::Rect chip{tile.x + float(px(4)), tile.y + float(px(2)), tile.w - float(px(8)), float(px(23))};
        gui::color_swatch(ui_ctx_, chip, color, selected);
        gui::Rect number{tile.x, tile.bottom() - float(px(15)), tile.w, float(px(15))};
        ui_ctx_.draw().text(number, std::to_string(index), ui_ctx_.style().font_size_small, theme.get(T::ui_text), gui::TextAlign::Centre);
        if (in.clicked || ui_ctx_.activated(id)) {
            if (!selected) {
                flush_edits();
                if (auto* target = layers_.find(layer_color_target_)) target->color = gdoc::Color::aci(index);
                touch_metadata("layer color");
                mesh_dirty_ = true;
                log_.push_back("Layer " + layer_color_target_ + ": ACI " + std::to_string(index));
            }
            layer_color_target_.clear();
            break;
        }
    }
    ui_ctx_.pop_id();
    ui_ctx_.end_overlay();
}

void Shell::outline_body(std::size_t bi, const float color[4]) {
    const auto& bodies = modeller_.bodies();
    if (bi >= bodies.size()) return;
    const gtopo::Body& b = bodies[bi];
    for (gcm::u32 h = 0; h < b.halfedge_count(); ++h) {
        if (b.he_twin[h] != gtopo::kInvalid && b.he_twin[h] < h) continue;
        double ax, ay, az, bx, by, bz;
        gcm::to_world(vol_, b.vertex_point[b.he_vertex[h]], ax, ay, az);
        gcm::to_world(vol_, b.vertex_point[b.he_vertex[b.he_next[h]]], bx, by, bz);
        lines_.push_back({{float(ax), float(ay), float(az)}, {color[0], color[1], color[2], 1.0f}});
        lines_.push_back({{float(bx), float(by), float(bz)}, {color[0], color[1], color[2], 1.0f}});
    }
}

void Shell::draw_objects_panel() {
    const auto sl = dock_.slot("objects");
    if (!sl.visible || sl.collapsed) return;
    dock_.begin_content(ui_ctx_, "objects");
    gui::Stack s(ui_ctx_, sl.content);
    const auto& theme = themes_.active();
    const gui::Style& st = ui_ctx_.style();
    const auto& bodies = modeller_.bodies();
    hover_body_ = -1;
    if (bodies.empty()) {
        gui::Rect r = s.next(float(px(32)));
        ui_ctx_.draw().text(r.deflate(gui::Insets::xy(float(px(12)), 0)), "No solids yet. Select a closed sketch and extrude it.",
                            st.font_size_small, theme.get(T::ui_text_dim), gui::TextAlign::Left);
    }
    ui_ctx_.push_id("objects");
    for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
        gui::Rect row = s.next(float(px(40)));
        ui_ctx_.push_id(std::to_string(i));
        const auto in = ui_ctx_.interact(ui_ctx_.id("row"), row);
        if (in.hovered) hover_body_ = i;
        if (i == sel_body_ || std::find(selected_bodies_.begin(),selected_bodies_.end(),std::size_t(i))!=selected_bodies_.end()) {
            gui::Color a = theme.get(T::ui_accent);
            a.a = 0.16f;
            ui_ctx_.draw().round_rect(row, float(px(6)), a);
        } else if (in.hovered) {
            gui::Color layer = theme.get(T::ui_accent);
            layer.a = st.hover_opacity;
            ui_ctx_.draw().round_rect(row, float(px(6)), layer);
        }
        gui::Rect ic{row.x + float(px(8)), row.y, float(px(32)), row.h};
        gui::draw_icon(ui_ctx_, gui::Icon::Box, ic, theme.get(T::ui_text), 1.4f);
        gui::Rect name{row.x + float(px(44)), row.y + float(px(4)), row.w - float(px(84)), float(px(16))};
        ui_ctx_.draw().text(name, "Solid " + std::to_string(i + 1), st.font_size, theme.get(T::ui_text), gui::TextAlign::Left);
        gui::Rect x{row.right() - float(px(32)), row.y + (row.h - float(px(24))) * 0.5f, float(px(24)), float(px(24))};
        const auto xin = ui_ctx_.interact(ui_ctx_.id("delete"), x);
        if (xin.hovered) {
            gui::Color layer = theme.get(T::ui_accent);
            layer.a = st.hover_opacity;
            ui_ctx_.draw().round_rect(x, float(px(12)), layer);
        }
        ui_ctx_.draw().text(x, "\xc3\x97", st.font_size, theme.get(xin.hovered ? T::ui_text : T::ui_text_dim), gui::TextAlign::Centre);
        if (xin.clicked) {
            select_body(std::size_t(i));
            delete_selected();
            ui_ctx_.pop_id();
            break;
        }
        gui::Rect sub{name.x, name.bottom(), name.w, float(px(14))};
        ui_ctx_.draw().text(sub, modeller_.describe(std::size_t(i)) + "  \xc2\xb7  layer " + modeller_.body_layer(std::size_t(i)),
                            st.font_size_small * 0.9f, theme.get(T::ui_text_dim), gui::TextAlign::Left);
        for (int sketch=0; sketch<int(sketches_.size()); ++sketch) {
            if (sketch_parent(sketch)!=i) continue;
            ui_ctx_.push_id("sketch"+std::to_string(sketch));
            const auto child=s.next(float(px(28)));
            gui::Rect name{child.x+float(px(28)),child.y,child.w-float(px(128)),child.h};
            if (gui::button(ui_ctx_,name,sketches_[sketch].name,true)) edit_sketch(sketch);
            gui::Rect visibility{child.right()-float(px(98)),child.y,float(px(50)),child.h};
            if (gui::button(ui_ctx_,visibility,sketch_visible(sketch) ? "Hide" : "Show",true))
                set_sketch_visibility(sketch,sketch_visible(sketch) ? SketchVisibility::Hidden : SketchVisibility::Shown);
            gui::Rect automatic{child.right()-float(px(46)),child.y,float(px(44)),child.h};
            if (gui::button(ui_ctx_,automatic,"Auto",true)) set_sketch_visibility(sketch,SketchVisibility::Automatic);
            ui_ctx_.pop_id();
        }
        const auto& hist = modeller_.history(std::size_t(i));
        for (int k = 0; k < static_cast<int>(hist.size()); ++k) {
            const auto& f = hist[std::size_t(k)];
            gui::Rect fr = s.next(float(px(22)));
            ui_ctx_.push_id("f" + std::to_string(k));
            const auto fin = ui_ctx_.interact(ui_ctx_.id("row"), fr);
            const bool picked = sel_feature_body_ == i && sel_feature_ == k;
            if (picked) {
                gui::Color a = theme.get(T::ui_accent);
                a.a = 0.22f;
                ui_ctx_.draw().round_rect(fr, float(px(4)), a);
            } else if (fin.hovered) {
                gui::Color layer = theme.get(T::ui_accent);
                layer.a = st.hover_opacity;
                ui_ctx_.draw().round_rect(fr, float(px(4)), layer);
            }
            std::string line = f.label;
            if (picked && !feature_typed_.empty()) {
                const std::size_t sp = line.find(' ');
                line = (sp == std::string::npos ? line : line.substr(0, sp)) + " " + feature_typed_ + "|";
            }
            if (f.sketch >= 0 && f.sketch < static_cast<int>(sketches_.size()))
                line += "  \xe2\x80\x94  " + sketches_[std::size_t(f.sketch)].name;
            gui::Rect tick{fr.x + float(px(44)), fr.y, float(px(12)), fr.h};
            ui_ctx_.draw().text(tick, "\xe2\x86\xb3", st.font_size_small, theme.get(T::ui_text_dim), gui::TextAlign::Left);
            gui::Rect txt{fr.x + float(px(58)), fr.y, fr.w - float(px(96)), fr.h};
            ui_ctx_.draw().text(txt, line, st.font_size_small,
                                theme.get(picked ? T::ui_text : f.regenerable ? T::ui_text_dim : T::ui_text_dim), gui::TextAlign::Left);
            gui::Rect x{fr.right() - float(px(30)), fr.y + (fr.h - float(px(20))) * 0.5f, float(px(20)), float(px(20))};
            const auto xin = ui_ctx_.interact(ui_ctx_.id("delete"), x);
            if (xin.hovered) {
                gui::Color layer = theme.get(T::ui_accent);
                layer.a = st.hover_opacity;
                ui_ctx_.draw().round_rect(x, float(px(10)), layer);
            }
            if (fin.hovered || xin.hovered || picked)
                ui_ctx_.draw().text(x, "\xc3\x97", st.font_size_small, theme.get(xin.hovered ? T::ui_text : T::ui_text_dim), gui::TextAlign::Centre);
            ui_ctx_.pop_id();
            if (xin.clicked) { remove_feature(i, k); ui_ctx_.pop_id(); goto next_body; }
            if (fin.clicked) select_feature(i, k);
        }
        if (in.clicked) {
            select_body(std::size_t(i),input_.cmd || input_.ctrl || input_.shift);
            sel_feature_body_ = -1; sel_feature_ = -1; feature_typed_.clear();
        }
        ui_ctx_.pop_id();
        s.gap(float(px(2)));
        continue;
    next_body:
        break;
    }
    ui_ctx_.pop_id();
    dock_.end_content(ui_ctx_, "objects");
}
}
