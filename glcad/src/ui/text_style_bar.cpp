#include "ui/shell.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace gcapp {
namespace {
const char* color_caption(const std::string& property) {
    return property=="COLOR" ? "Text color" : property=="BACKGROUND" ? "Background" : "Highlight";
}
gui::Color preview_color(const std::string& value, gui::Color fallback) {
    unsigned r,g,b;
    if(value.size()==7 && value[0]=='#' && std::sscanf(value.c_str()+1,"%2x%2x%2x",&r,&g,&b)==3)
        return {r/255.f,g/255.f,b/255.f,1};
    return fallback;
}
std::string hex_color(int index) {
    gcm::u8 r,g,b;gdoc::aci_rgb(index,r,g,b);
    char text[8];std::snprintf(text,sizeof(text),"#%02X%02X%02X",r,g,b);return text;
}
}

void Shell::draw_text_style_bar(gui::Rect& screen) {
    const auto selected=canvas_.selected_texts();
    if(selected.empty() || selected.front()>=canvas_.texts().size() || read_only_) {
        text_color_property_.clear();return;
    }
    const auto first=canvas_.texts()[selected.front()];
    const auto value=[&](const std::string& key) {
        const auto initial=canvas_.text_property(first,key);
        for(auto i:selected) if(i<canvas_.texts().size() && canvas_.text_property(canvas_.texts()[i],key)!=initial)
            return std::string("Mixed");
        return initial;
    };
    const float widths[]={84,180,106,44,44,112,112,112};
    const float gap=float(px(8)),pad=float(px(12)),row_h=float(px(54));
    const float available=std::max(float(px(100)),screen.w-2*pad);
    int rows=1;float used=0;
    for(float w:widths) {const float width=std::min(float(px(w)),available);if(used && used+width>available){++rows;used=0;}used+=width+gap;}
    const auto bar=screen.cut_top(rows*row_h+float(px(8)));
    const auto& theme=themes_.active();
    ui_ctx_.draw().rect(bar,theme.get(gtheme::token::ui_surface));
    ui_ctx_.draw().line({bar.x,bar.bottom()},{bar.right(),bar.bottom()},1,theme.get(gtheme::token::ui_border));
    ui_ctx_.push_id("text-style-bar");
    float x=bar.x+pad,y=bar.y+float(px(4));
    const auto cell=[&](int index,const char* caption) {
        const float w=std::min(float(px(widths[index])),available);
        if(x>bar.x+pad && x+w>bar.right()-pad){x=bar.x+pad;y+=row_h;}
        gui::Rect label{x,y,w,float(px(18))};
        ui_ctx_.draw().text(label,caption,ui_ctx_.style().font_size_small,theme.get(gtheme::token::ui_text_dim),gui::TextAlign::Left);
        gui::Rect control{x,y+float(px(19)),w,float(px(30))};x+=w+gap;return control;
    };
    auto title=cell(0,"Text style");
    gui::label(ui_ctx_,title,std::to_string(selected.size())+" selected",gui::TextAlign::Left,true);
    auto font_rect=cell(1,"Font");
    if(text_style_fonts_.empty() || text_style_font_count_!=fonts_.size()) {
        text_style_fonts_={"Default"};text_style_font_count_=fonts_.size();
        std::set<std::string> names;
        for(const auto& font:fonts_.all()) if(font.kind==gtext::FontKind::TrueType || font.kind==gtext::FontKind::OpenType)
            names.insert(font.name);
        text_style_fonts_.insert(text_style_fonts_.end(),names.begin(),names.end());
    }
    auto fonts=text_style_fonts_;
    auto font=value("FONT");
    if(std::find(fonts.begin(),fonts.end(),font)==fonts.end())fonts.push_back(font);
    int choice=int(std::find(fonts.begin(),fonts.end(),font)-fonts.begin());
    if(gui::dropdown(ui_ctx_,font_rect,"Text font",fonts,choice)>=0 && fonts[choice]!="Mixed")
        set_selection_property("FONT",fonts[choice]);
    auto height=cell(2,"Text height");
    if(!ui_ctx_.is_focused(ui_ctx_.id("Text height"))) {
        bool mixed=false;for(auto i:selected) if(i<canvas_.texts().size())mixed|=canvas_.texts()[i].height!=first.height;
        text_style_height_=mixed?"Mixed":gunit::format_length(first.height,gunit::Format{canvas_.display_unit()});
    }
    if(gui::numeric_field(ui_ctx_,height,"Text height",text_style_height_).committed)
        set_selection_property("HEIGHT",text_style_height_);
    for(auto item:{std::pair{3,"BOLD"},std::pair{4,"ITALIC"}}) {
        const auto rect=cell(item.first,item.first==3?"Bold":"Italic");
        const auto state=value(item.second);
        const auto id=ui_ctx_.id(item.second);const auto in=ui_ctx_.interact(id,rect);
        ui_ctx_.register_focusable(id,rect,item.first==3?"Bold":"Italic");
        ui_ctx_.announce(id,gui::Context::Role::Toggle,item.first==3?"Bold":"Italic",state,rect,state=="1");
        const bool on=state=="1";
        ui_ctx_.draw().round_rect(rect,float(px(4)),theme.get(on?gtheme::token::ui_accent:gtheme::token::ui_surface_raised));
        ui_ctx_.draw().border(rect,float(px(4)),in.focused?2:1,theme.get(in.focused?gtheme::token::ui_accent:gtheme::token::ui_border));
        ui_ctx_.draw().text(rect,state=="Mixed"?"—":item.first==3?"B":"I",ui_ctx_.style().font_size,
            theme.get(on?gtheme::token::ui_accent_text:gtheme::token::ui_text),gui::TextAlign::Centre);
        if(in.clicked || ui_ctx_.activated(id))set_selection_property(item.second,on?"0":"1");
    }
    for(auto item:{std::pair{5,"COLOR"},std::pair{6,"BACKGROUND"},std::pair{7,"HIGHLIGHT"}}) {
        auto rect=cell(item.first,color_caption(item.second));
        const auto current=value(item.second);const auto id=ui_ctx_.id(item.second);
        const auto in=ui_ctx_.interact(id,rect);ui_ctx_.register_focusable(id,rect,color_caption(item.second));
        ui_ctx_.announce(id,gui::Context::Role::Button,color_caption(item.second),current,rect);
        ui_ctx_.draw().round_rect(rect,float(px(4)),theme.get(gtheme::token::ui_surface_raised));
        ui_ctx_.draw().border(rect,float(px(4)),in.focused?2:1,theme.get(in.focused?gtheme::token::ui_accent:gtheme::token::ui_border));
        auto chip=gui::Rect{rect.x+float(px(7)),rect.y+float(px(7)),float(px(16)),float(px(16))};
        gui::color_swatch(ui_ctx_,chip,preview_color(current,theme.get(gtheme::token::ui_text)));
        auto label=rect;label.x+=float(px(29));label.w-=float(px(32));
        gui::label(ui_ctx_,label,current=="BYLAYER"?"By layer":current=="NONE"?"None":current);
        if(in.clicked || ui_ctx_.activated(id)) {
            text_color_property_=item.second;text_color_anchor_=rect;
            text_color_scroll_={};
            text_style_custom_color_=current.size()==7 && current[0]=='#'?current:"#000000";
        }
    }
    ui_ctx_.pop_id();
}

void Shell::draw_text_style_colors() {
    if(text_color_property_.empty() || canvas_.selected_texts().empty())return;
    if(input_.key_escape){text_color_property_.clear();return;}
    const auto& theme=themes_.active();
    const auto screen=ui_ctx_.viewport();
    const float pad=float(px(12)),gap=float(px(6));
    const float width=std::min(float(px(238)),screen.w),height=std::min(float(px(342)),screen.h);
    const float cell=std::max(1.f,(width-2*pad-4*gap)/5);
    gui::Rect panel{std::clamp(text_color_anchor_.x,screen.x,std::max(screen.x,screen.right()-width)),
        std::clamp(text_color_anchor_.bottom()+gap,screen.y,std::max(screen.y,screen.bottom()-height)),width,height};
    text_color_panel_=panel;
    ui_ctx_.begin_overlay(panel);ui_ctx_.push_id("text-style-colors");
    ui_ctx_.draw().shadow(panel,float(px(6)),float(px(8)),theme.get(gtheme::token::ui_shadow));
    auto surface=theme.get(gtheme::token::ui_surface_raised);surface.a=1;
    ui_ctx_.draw().round_rect(panel,float(px(6)),surface);
    ui_ctx_.draw().border(panel,float(px(6)),1,theme.get(gtheme::token::ui_border));
    const auto content=gui::begin_scroll(ui_ctx_,panel,text_color_scroll_);
    gui::label(ui_ctx_,{content.x+pad,content.y+pad,panel.w-2*pad,float(px(24))},color_caption(text_color_property_));
    const auto apply=[&](const std::string& color){if(set_selection_property(text_color_property_,color))text_color_property_.clear();};
    const auto reset=text_color_property_=="COLOR"?"By layer":"None";
    if(gui::button(ui_ctx_,{content.x+pad,content.y+float(px(40)),panel.w-2*pad,float(px(28))},reset))apply(text_color_property_=="COLOR"?"BYLAYER":"NONE");
    static constexpr int indices[]={1,2,3,4,5,6,7,8,9,20,30,40,60,80,100,120,140,160,180,200,220,240,250,252,254};
    for(int i=0;i<25;++i) {
        const auto color=hex_color(indices[i]);
        gui::Rect tile{content.x+pad+(i%5)*(cell+gap),content.y+float(px(76))+(i/5)*(cell+gap),cell,cell};
        const auto id=ui_ctx_.id("text-color-"+std::to_string(indices[i]));const auto in=ui_ctx_.interact(id,tile);
        ui_ctx_.register_focusable(id,tile,color);ui_ctx_.announce(id,gui::Context::Role::Button,"Text palette "+color,color,tile);
        gui::color_swatch(ui_ctx_,tile.deflate(gui::Insets::all(float(px(4)))),preview_color(color,{}),in.hovered||in.focused);
        if(in.clicked || ui_ctx_.activated(id)){apply(color);break;}
    }
    const float extent=float(px(76))+5*(cell+gap)+float(px(46));
    gui::Rect custom{content.x+pad,content.y+extent-float(px(42)),panel.w-2*pad-float(px(66)),float(px(30))};
    const auto result=gui::numeric_field(ui_ctx_,custom,"Custom text color",text_style_custom_color_);
    gui::Rect button{custom.right()+gap,custom.y,float(px(60)),custom.h};
    if(result.committed || gui::button(ui_ctx_,button,"Apply"))apply(text_style_custom_color_);
    gui::end_scroll(ui_ctx_,panel,text_color_scroll_,extent);
    ui_ctx_.pop_id();ui_ctx_.end_overlay();
}
}
