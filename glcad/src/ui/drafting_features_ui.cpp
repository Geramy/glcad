#include "ui/shell.hpp"
#include "ui/widgets.hpp"
#include "ui/ai_tools.hpp"
#include "ui/symbols.hpp"
#include <charconv>
#include <cmath>
#include <sstream>

namespace gcapp {
namespace {
bool read_color(const std::string &text, gdoc::Color &color) {
    if (text.size() != 7 || text[0] != '#')
        return false;
    unsigned rgb = 0;
    const auto r = std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
    if (r.ec != std::errc{} || r.ptr != text.data() + 7)
        return false;
    color = gdoc::Color::rgb(gcm::u8(rgb >> 16), gcm::u8(rgb >> 8), gcm::u8(rgb));
    return true;
}
bool read_point(const std::string &text, gunit::Length unit, const gcm::WorkingVolume &vol,
                gcm::Lattice2 &point) {
    auto comma = text.find(',');
    if (comma == std::string::npos)
        return false;
    auto x = gunit::parse_length(text.substr(0, comma), unit),
         y = gunit::parse_length(text.substr(comma + 1), unit);
    if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y))
        return false;
    point = gcm::to_lattice(vol, *x, *y);
    return true;
}
}
void Shell::set_drawing_discipline(DrawingDiscipline discipline) {
    if (discipline != DrawingDiscipline::General && discipline != DrawingDiscipline::Architectural &&
        discipline != DrawingDiscipline::Mechanical) return;
    if (drawing_discipline_ != discipline) {
        touch_metadata("drawing discipline");
        drawing_discipline_ = discipline;
        library_scroll_=0;
        if(discipline==DrawingDiscipline::Mechanical) {library_folders_.erase("Architecture");library_folders_.insert("Drawing tools");}
        else {library_folders_.insert("Architecture");library_folders_.erase("Drawing tools");}
        plan_panel_serial_ = ~gcm::u64{0};
    }
    project_type_ = discipline == DrawingDiscipline::Architectural ? "architecture" :
                    discipline == DrawingDiscipline::Mechanical ? "mechanical" : "general";
    project_ask_done_ = true;
}
void Shell::set_project_type(std::string type) {
    set_drawing_discipline(type == "architecture" || type == "architectural" ? DrawingDiscipline::Architectural :
                          type == "mechanical" ? DrawingDiscipline::Mechanical : DrawingDiscipline::General);
}
bool Shell::run_drafting_feature_command(const std::string &name, const std::string &arguments) {
    if(run_block_library_command(name,arguments))return true;
    if (name == "DISCIPLINE") {
        std::string value = arguments;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (value == "architectural" || value == "architecture" || value == "arch") set_drawing_discipline(DrawingDiscipline::Architectural);
        else if (value == "mechanical" || value == "mech") set_drawing_discipline(DrawingDiscipline::Mechanical);
        else if (value == "general") set_drawing_discipline(DrawingDiscipline::General);
        else if (!value.empty()) { log_.push_back("DISCIPLINE: ARCHITECTURAL, MECHANICAL or GENERAL"); return true; }
        log_.push_back("Drawing discipline: " + project_type_ + ". Independent of 2D/3D mode.");
        return true;
    }
    if (name == "SKETCHLIBRARY") { background_context_seed_=false; drafting_feature_panel_ = 3; return true; }
    if(name=="SYMBOLS") {background_context_seed_=false;drafting_feature_panel_=3;library_folders_.erase("Architecture");library_folders_.insert("Symbols");library_scroll_=0;return true;}
    if(name=="SYMBOL" || name=="ARROW" || name=="SYMBOLLEGEND") {
        if(mode_!=Mode::Sketch) {log_.push_back("Open a 2D sketch first");return true;}
        if(read_only()) {log_.push_back("The active document is read-only");return true;}
        gai::Json args;
        const auto execute=[this](const std::string& tool,gai::Json values) {
            gai::ToolCall call;call.function.name=tool;call.function.arguments=values.dump();
            ai_tools::execute(*this,call,ai_tools::Origin::Library);mesh_dirty_=true;
        };
        if(name=="SYMBOLLEGEND") {
            const auto& refs=canvas_.blocks().references();
            if(std::any_of(refs.begin(),refs.end(),[&](const auto& ref){return is_symbol_legend(canvas_.blocks(),ref.name);})) {
                execute("symbol_legend",gai::Json::object());return true;
            }
            if(used_symbols(canvas_).empty()) {log_.push_back("Place a symbol or block before generating a legend");return true;}
        } else if(name=="SYMBOL") {
            std::istringstream in(arguments);std::string id,size=symbol_size_;in>>id;in>>size;
            const auto& entries=symbol_catalog();
            const auto length=gunit::parse_length(size,canvas_.display_unit());
            if(std::none_of(entries.begin(),entries.end(),[&](const auto& item){return id==item.id;}) || !length || !std::isfinite(*length) || *length<=0 || *length>10000) {
                log_.push_back("Choose a symbol in the library and enter a positive size");drafting_feature_panel_=5;return true;
            }
            args["symbol"]=id;args["size"]=size;
        } else {
            std::string direction=arguments;
            std::transform(direction.begin(),direction.end(),direction.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(!direction.empty() && direction!="both") {log_.push_back("ARROW or ARROW BOTH; pick tail then tip");return true;}
            args["direction"]=direction.empty()?"forward":"both";
        }
        begin_pick(name=="ARROW"?2:1,name=="ARROW"?"Pick the arrow tail, then its tip":name=="SYMBOL"?"Pick the symbol position":"Pick the legend's top-left position",[this,name,args,execute](const auto& points) mutable {
            const auto point=[&](gcm::Lattice2 p) {double x,y;gcm::to_world(vol_,p,x,y);std::ostringstream text;text.precision(15);text<<x<<"m,"<<y<<"m";return text.str();};
            if(name=="ARROW") {args["from"]=point(points[0]);args["to"]=point(points[1]);}
            else args["at"]=point(points[0]);
            execute(name=="ARROW"?"draw_arrow":name=="SYMBOL"?"draw_symbol":"symbol_legend",args);
        });
        return true;
    }
    if (name == "WALL" || name == "DOOR" || name == "WINDOW" || name == "DOORFRAME" || name == "HINGE") {
        if (mode_ != Mode::Sketch) { log_.push_back("Open a 2D sketch first"); return true; }
        begin_pick(name=="HINGE"?1:2, name=="HINGE"?"Pick the hinge position":"Pick two snapped endpoints for " + name, [this, name](const auto& points) {
            const auto point = [&](gcm::Lattice2 p) {
                double x,y; gcm::to_world(vol_,p,x,y);
                std::ostringstream text; text.precision(15); text<<x<<"m,"<<y<<"m"; return text.str();
            };
            gai::ToolCall call;
            call.function.name = name == "WALL" ? "draw_wall" : name == "WINDOW" ? "draw_window" : "draw_door";
            gai::Json args;
            if(name!="WALL") args["snap_to_wall"]="true";
            if (name == "WALL" || name == "WINDOW") {
                args["from"]=point(points[0]); args["to"]=point(points[1]);
            } else {
                args["hinge"]=point(points[0]);
                const auto latch=points.size()>1?points[1]:gcm::Lattice2{points[0].x+gcm::i64(std::llround(.1/vol_.inv_scale())),points[0].y};
                args["latch"]=point(latch);
                args["framing"]="none";
                if(name=="DOORFRAME" || name=="HINGE") args["component"]=name=="DOORFRAME"?"frame":"hinge";
            }
            call.function.arguments=args.dump();
            ai_tools::execute(*this,call,ai_tools::Origin::Library); mesh_dirty_=true;
        });
        return true;
    }
    if (name != "BACKGROUND" && name != "BACKGROUNDOFF" && name != "DOORFLOW" && name != "DOORSWING" && name != "DOORHINGE")
        return false;
    if (mode_ != Mode::Sketch) {
        log_.push_back("Open a 2D sketch first");
        return true;
    }
    if (name == "DOORFLOW" || name == "DOORSWING" || name == "DOORHINGE") {
        if (arguments.empty()) {
            if (canvas_.has_selected_door())
                drafting_feature_panel_ = 2;
            else
                log_.push_back("Select a door swing first");
        } else {
            std::string direction = arguments;
            std::transform(direction.begin(), direction.end(), direction.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (name == "DOORFLOW") canvas_.set_door_flow(direction);
            else if (direction == "reverse") canvas_.edit_selected_doors({}, name == "DOORSWING", name == "DOORHINGE");
            else { log_.push_back(name + ": use REVERSE"); return true; }
            log_.push_back(canvas_.message());
            mesh_dirty_ = true;
        }
        return true;
    }
    if (arguments.empty()) {
        if (name == "BACKGROUND")
            drafting_feature_panel_ = 1;
        else
            begin_pick(1, "Click the background to remove", [this](const auto &points) {
                canvas_.remove_background_at(points[0]);
                log_.push_back(canvas_.message());
            });
        return true;
    }
    std::istringstream stream(arguments);
    std::string at, color, gap = "10mm", extra;
    int opacity = 35;
    stream >> at;
    gcm::Lattice2 point;
    if (!read_point(at, canvas_.display_unit(), vol_, point)) {
        log_.push_back("Area background: enter x,y in drawing units");
        return true;
    }
    if (name == "BACKGROUNDOFF") {
        canvas_.remove_background_at(point);
        log_.push_back(canvas_.message());
        return true;
    }
    stream >> color;
    if (stream >> gap) {
        if (stream >> extra) {
            const auto parsed = std::from_chars(extra.data(), extra.data() + extra.size(), opacity);
            if (parsed.ec != std::errc{} || parsed.ptr != extra.data() + extra.size()) {
                log_.push_back("Opacity must be an integer percentage");
                return true;
            }
        }
    }
    if (stream >> extra) {
        log_.push_back("Too many area background arguments");
        return true;
    }
    gdoc::Color rgb;
    const auto tolerance = gunit::parse_length(gap, canvas_.display_unit());
    if (!read_color(color, rgb) || !tolerance) {
        log_.push_back("BACKGROUND x,y #RRGGBB [gap, e.g. 10mm] [opacity 0–100]");
        return true;
    }
    canvas_.background_at_world(point, rgb, *tolerance, opacity);
    log_.push_back(canvas_.message());
    mesh_dirty_ = true;
    return true;
}

void Shell::draw_plan_check_button(const gui::Rect& button, const gui::Rect& bar) {
    const auto& theme=themes_.active();
    const auto& style=ui_ctx_.style();
    const auto id=ui_ctx_.id("plan-check-toggle");
    const auto hit=ui_ctx_.interact(id,button);
    const bool open=drafting_feature_panel_==4;
    ui_ctx_.register_focusable(id,button,"Check plan");
    ui_ctx_.announce(id,gui::Context::Role::Button,"Check plan",open?"expanded":"collapsed",button,open);
    if(open || hit.hovered || hit.focused)
        ui_ctx_.draw().round_rect(button,style.radius_small,
            theme.get(open?gtheme::token::ui_accent:gtheme::token::ui_hover));
    gui::draw_icon(ui_ctx_,gui::Icon::CheckPlan,button,
        theme.get(open?gtheme::token::ui_accent_text:gtheme::token::ui_text),float(px(1.6)));
    if(hit.hovered || hit.focused) {
        const float width=ui_ctx_.measure("Check plan",style.font_size_small)+float(px(16));
        const gui::Rect tip{std::clamp(button.centre().x-width*.5f,bar.x,std::max(bar.x,bar.right()-width)),
            bar.bottom()+float(px(6)),width,float(px(28))};
        ui_ctx_.begin_overlay(tip);
        ui_ctx_.draw().round_rect(tip,style.radius_small,theme.get(gtheme::token::ui_surface_raised));
        ui_ctx_.draw().border(tip,style.radius_small,1,theme.get(gtheme::token::ui_border));
        ui_ctx_.draw().text(tip,"Check plan",style.font_size_small,theme.get(gtheme::token::ui_text),gui::TextAlign::Centre);
        ui_ctx_.end_overlay();
    }
    if(hit.clicked || ui_ctx_.activated(id)) {
        drafting_feature_panel_=open?0:4;
        plan_panel_serial_=~gcm::u64{0};
        plan_panel_scroll_=0;
    }
}

void Shell::draw_discipline_picker(const gui::Rect& r) {
    int discipline = int(drawing_discipline_);
    if (gui::dropdown(ui_ctx_, r, "Drawing discipline", {"General", "Architectural", "Mechanical"}, discipline) >= 0)
        run_command(std::string("DISCIPLINE ") + (discipline == 1 ? "ARCHITECTURAL" : discipline == 2 ? "MECHANICAL" : "GENERAL"));
}

void Shell::draw_canvas_options(const gui::Rect& button, const gui::Rect& row) {
    const auto& theme=themes_.active();
    const auto& st=ui_ctx_.style();
    const float unit=float(px(1));
    const auto id=ui_ctx_.id("drawing-options");
    const auto hit=ui_ctx_.interact(id,button);
    ui_ctx_.announce(id,gui::Context::Role::Button,"Drawing options",canvas_options_open_?"expanded":"collapsed",button,canvas_options_open_);
    if(canvas_options_open_||hit.hovered||hit.held) ui_ctx_.draw().round_rect(button.deflate(gui::Insets::all(2*unit)),6*unit,
        theme.get(canvas_options_open_?gtheme::token::ui_accent:gtheme::token::ui_hover));
    const auto options_ink=theme.get(canvas_options_open_?gtheme::token::ui_accent_text:gtheme::token::ui_text);
    gui::draw_icon(ui_ctx_,gui::Icon::Options,button,options_ink,1.3f);
    gui::draw_menu_marker(ui_ctx_,button.deflate(gui::Insets::all(2*unit)),options_ink,unit);
    menu_markers_.push_back({"Drawing options",button});
    if(hit.clicked) canvas_options_open_=!canvas_options_open_;
    if(!canvas_options_open_) return;
    const float line=(tablet_mode_?44:34)*unit, pad=10*unit, label_w=96*unit;
    const int rows=mode_==Mode::Sketch?3:2;
    const float width=std::min(300*unit,row.w-2*pad);
    const gui::Rect panel{std::clamp(button.right()-width,row.x+pad,row.right()-pad-width),row.bottom()+4*unit,width,rows*line+2*pad};
    ui_ctx_.begin_overlay(panel);
    ui_ctx_.push_id("drawing-options-panel");
    ui_ctx_.draw().shadow(panel,6*unit,12*unit,theme.get(gtheme::token::ui_shadow));
    ui_ctx_.draw().round_rect(panel,8*unit,theme.get(gtheme::token::ui_surface_raised));
    ui_ctx_.draw().border(panel,8*unit,1,theme.get(gtheme::token::ui_border));
    gui::Rect cursor=panel.deflate(gui::Insets::all(pad));
    const auto labelled=[&](const char* text) {
        gui::Rect r=cursor.cut_top(line);
        ui_ctx_.draw().text(r.cut_left(label_w),text,st.font_size,theme.get(gtheme::token::ui_text),gui::TextAlign::Left);
        return r.deflate(gui::Insets::xy(0,(line-std::min(line,36*unit))*.5f));
    };
    static const std::vector<std::string> modes={"Wire","Flat","Smooth"};
    int pick=display_mode_;
    if(gui::dropdown(ui_ctx_,labelled("Display"),"Display mode",modes,pick)>=0) {
        display_mode_=std::clamp(pick,0,2);
        if(display_mode_==1) modeller_.set_smoothing(false,modeller_.smoothing_angle());
        if(display_mode_==2) modeller_.set_smoothing(true,modeller_.smoothing_angle());
        mesh_dirty_=true;
        log_.push_back("display: "+modes[std::size_t(display_mode_)]);
    }
    draw_discipline_picker(labelled("Discipline"));
    if(mode_==Mode::Sketch) {
        gui::Rect r=cursor.cut_top(line).deflate(gui::Insets::xy(0,(line-std::min(line,36*unit))*.5f));
        if(gui::button(ui_ctx_,r,"Object library")) {
            background_context_seed_=false;
            drafting_feature_panel_=drafting_feature_panel_==3?0:3;
            canvas_options_open_=false;
        }
    }
    if(input_.key_escape||(input_.pressed&&!panel.contains(input_.mouse)&&!button.contains(input_.mouse)&&!ui_ctx_.covered_by_overlay(input_.mouse,1)))
        canvas_options_open_=false;
    ui_ctx_.pop_id();
    ui_ctx_.end_overlay();
}

void Shell::draw_drafting_features(const gui::Rect &canvas) {
    if (mode_ != Mode::Sketch) return;
    if(input_.key_escape) {drafting_feature_panel_=0;background_context_seed_=false;library_drag_command_.clear();}
    const auto& palette = themes_.active();
    if(drafting_feature_panel_==3 || drafting_feature_panel_==5) {draw_object_library(canvas);return;}
    if (drafting_feature_panel_ == 4) {
        gui::Rect panel{canvas.x+float(px(12)),canvas.y+float(px(50)),
            std::min(float(px(390)),canvas.w-float(px(24))),
            std::min(float(px(400)),canvas.h-float(px(62)))};
        ui_ctx_.begin_overlay(panel);
        ui_ctx_.draw().round_rect(panel,float(px(8)),palette.get(gtheme::token::ui_surface_raised));
        gui::Rect row{panel.x+float(px(10)),panel.y+float(px(10)),panel.w-float(px(20)),float(px(30))};
        {
            if(plan_panel_serial_!=canvas_.edit_serial()) {
                plan_panel_findings_=plan_check();plan_panel_serial_=canvas_.edit_serial();
            }
            const int visible=std::max(1,int((panel.h-px(20))/px(36)));
            if(panel.contains(input_.mouse)) plan_panel_scroll_-=int(input_.wheel);
            plan_panel_scroll_=std::clamp(plan_panel_scroll_,0,std::max(0,int(plan_panel_findings_.size())-visible));
            int finding_index=0;
            for(const auto& f:plan_panel_findings_) {
                if(f.located) {
                    double x,y,sx,sy; gcm::to_world(vol_,f.location,x,y); canvas_.view().screen_of(x,y,sx,sy);
                    gui::Rect marker{float(sx)-float(px(6)),float(sy)-float(px(6)),float(px(12)),float(px(12))};
                    ui_ctx_.draw().border(marker,0,2,palette.get(gtheme::token::ui_text));
                }
                if(finding_index++<plan_panel_scroll_ || row.bottom()>panel.bottom()-float(px(8))) continue;
                ui_ctx_.push_id(std::to_string(finding_index));
                const bool chosen=gui::button(ui_ctx_,row,f.text.c_str());
                ui_ctx_.pop_id();
                if(chosen) log_.push_back(f.text);
                if(chosen && f.located) {
                    double x,y;gcm::to_world(vol_,f.location,x,y);double sx,sy;canvas_.view().screen_of(x,y,sx,sy);
                    camera_.pan(canvas.x+canvas.w*.5-sx,canvas.y+canvas.h*.5-sy);sync_view_from_camera();
                }
                row.y+=float(px(36));
            }
        }
        ui_ctx_.end_overlay(); return;
    }
    if (!drafting_feature_panel_) return;
    if (input_.key_escape) {
        drafting_feature_panel_ = 0;
        return;
    }
    const auto &theme = themes_.active();
    const auto &style = ui_ctx_.style();
    const float width = std::min(float(px(340)), canvas.w - float(px(16)));
    gui::Rect box{canvas.x + (canvas.w - width) * .5f, canvas.y + float(px(56)), width,
                  float(px(drafting_feature_panel_ == 1 ? 340 : 418))};
    ui_ctx_.begin_overlay(box);
    ui_ctx_.push_id("drafting-features");
    ui_ctx_.draw().round_rect(box, float(px(8)), theme.get(gtheme::token::ui_surface_raised));
    const float pad = float(px(14)), row = float(px(34));
    float y = box.y + pad;
    const auto next = [&]() {
        gui::Rect r{box.x + pad, y, box.w - 2 * pad, row};
        y += row + float(px(5));
        return r;
    };
    ui_ctx_.draw().text(next(), drafting_feature_panel_ == 1 ? "Area background" : "Edit doors",
                        style.font_size, theme.get(gtheme::token::ui_text), gui::TextAlign::Left);
    if (drafting_feature_panel_ == 2) {
        gui::label(ui_ctx_, next(), std::to_string(canvas_.selected_door_count()) + " doors selected");
        for (const auto &item :
             {std::pair{"One way — toward swing", "forward"},
              std::pair{"One way — away from swing", "reverse"},
              std::pair{"Both directions", "both"}, std::pair{"Remove arrow", "off"}})
            if (gui::button(ui_ctx_, next(), item.first)) {
                drafting_feature_panel_ = 0;
                run_command(std::string("DOORFLOW ") + item.second);
            }
        if (gui::button(ui_ctx_, next(), "Reverse swing side")) {
            drafting_feature_panel_ = 0;
            run_command("DOORSWING reverse");
        }
        if (gui::button(ui_ctx_, next(), "Swap hinge / latch")) {
            drafting_feature_panel_ = 0;
            run_command("DOORHINGE reverse");
        }
    } else {
        const auto field = [&](const char *label, std::string &value) {
            auto r = next();
            auto name = r;
            name.w = float(px(130));
            r.x += name.w;
            r.w -= name.w;
            ui_ctx_.draw().text(name, label, style.font_size, theme.get(gtheme::token::ui_text),
                                gui::TextAlign::Left);
            gui::text_field(ui_ctx_, r, label, value);
        };
        field("Color #RRGGBB", background_color_);
        field("Opacity %", background_opacity_);
        field("Close gaps up to", background_gap_);
        const auto swatches = next();
        const char *colors[] = {"#74A9CF", "#A1D99B", "#FDD49E", "#BCBDDC", "#FCAE91", "#D9D9D9"};
        for (int i = 0; i < 6; ++i) {
            gui::Rect r{swatches.x + i * swatches.w / 6, swatches.y, swatches.w / 6 - 3,
                        swatches.h};
            gdoc::Color c;
            read_color(colors[i], c);
            gui::color_swatch(ui_ctx_, r, {c.r / 255.f, c.g / 255.f, c.b / 255.f, 1},
                              background_color_ == colors[i]);
            if (ui_ctx_.interact(ui_ctx_.id(colors[i]), r).clicked)
                background_color_ = colors[i];
        }
        if (gui::button(ui_ctx_, next(), "Pick area / update color")) {
            gdoc::Color color;
            auto gap = gunit::parse_length(background_gap_, canvas_.display_unit());
            int opacity = 0;
            auto result =
                std::from_chars(background_opacity_.data(),
                                background_opacity_.data() + background_opacity_.size(), opacity);
            if (!read_color(background_color_, color) || !gap || !std::isfinite(*gap) || *gap < 0 ||
                *gap > 1 || result.ec != std::errc{} ||
                result.ptr != background_opacity_.data() + background_opacity_.size() ||
                opacity < 0 || opacity > 100)
                log_.push_back("Enter a hex color, a gap from 0–1 m, and opacity from 0–100%");
            else {
                drafting_feature_panel_ = 0;
                if(background_context_seed_) {
                    background_context_seed_=false;
                    canvas_.background_at_world(background_seed_,color,*gap,opacity);
                    log_.push_back(canvas_.message());
                } else begin_pick(1, "Click inside the area to color",
                           [this, color, gap = *gap, opacity](const auto &points) {
                               canvas_.background_at_world(points[0], color, gap, opacity);
                               log_.push_back(canvas_.message());
                           });
            }
        }
        if (gui::button(ui_ctx_, next(), "Remove background")) {
            drafting_feature_panel_ = 0;
            run_command("BACKGROUNDOFF");
        }
    }
    if (gui::button(ui_ctx_, next(), "Cancel")) {
        drafting_feature_panel_ = 0;background_context_seed_=false;
    }
    ui_ctx_.pop_id();
    ui_ctx_.end_overlay();
}
}
