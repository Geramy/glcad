#include "ui/shell.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace gcapp {
namespace {
std::string upper(std::string value) {
    for(auto& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
std::string color_value(const gdoc::Color& color) {
    if(color.kind==gdoc::Color::Kind::ByLayer)return "BYLAYER";
    if(color.kind==gdoc::Color::Kind::Index)return std::to_string(color.index);
    char text[8];std::snprintf(text,sizeof(text),"#%02X%02X%02X",color.r,color.g,color.b);return text;
}
std::string decimal(double number) {std::ostringstream out;out.precision(10);out<<number;return out.str();}
}

void Canvas::set_selected_hatches(std::vector<std::size_t> which) {
    clear_selection();
    for(auto i:which) if(i<hatches_.size() && std::find(sel_hatches_.begin(),sel_hatches_.end(),i)==sel_hatches_.end())
        sel_hatches_.push_back(i);
}

std::size_t Canvas::pick_hatch(double px,double py) const {
    double x,y;view_.world_of(px,py,x,y);const auto p=gcm::to_lattice(vol_,x,y);
    for(std::size_t i=hatches_.size();i-->0;) {
        const auto& hatch=hatches_[i];if(!hatch.valid || hatch.orphaned || layer_hidden(hatch.layer))continue;
        if(hatch.cached.solid) {
            if(gdoc::point_in_region(p,hatch.cached.fill_loops))return i;
        } else {
            const auto boundary=hatch_boundary(hatch);
            const auto loops=gdoc::extract_loops(segments_of(boundary));
            if(loops.ok() && gdoc::point_in_region(p,loops.loops))return i;
        }
    }
    return npos;
}

bool Canvas::set_hatch_property(const std::vector<std::size_t>& which,
                                const std::string& property,const std::string& value) {
    const auto key=upper(property),v=upper(value);
    if(which.empty() || std::any_of(which.begin(),which.end(),[&](auto i){return i>=hatches_.size();}))return false;
    double number=0;gdoc::Color color;const gdoc::HatchPattern* pattern=nullptr;
    if(key=="PATTERN") {pattern=patterns_.find(value);if(!pattern)return false;}
    else if(key=="SCALE" || key=="ANGLE") {
        std::istringstream input(value);if(!(input>>number) || !std::isfinite(number))return false;
        input>>std::ws;if(!input.eof() || (key=="SCALE" && number<=0))return false;
        if(key=="ANGLE")number=std::remainder(number,360.);
    } else if(key=="COLOR") {
        if(v=="BYLAYER")color=gdoc::Color::by_layer();
        else if(v.size()==7 && v[0]=='#' && std::all_of(v.begin()+1,v.end(),[](unsigned char c){return std::isxdigit(c);})) {
            const auto rgb=std::stoul(v.substr(1),nullptr,16);color=gdoc::Color::rgb(gcm::u8(rgb>>16),gcm::u8(rgb>>8),gcm::u8(rgb));
        } else {
            int index=0;std::istringstream input(value);if(!(input>>index))return false;input>>std::ws;
            if(!input.eof() || index<1 || index>255)return false;color=gdoc::Color::aci(index);
        }
    } else return false;
    mark();
    for(auto i:which) {
        auto& hatch=hatches_[i];
        if(key=="PATTERN")hatch.style.name=pattern->name;
        else if(key=="SCALE")hatch.style.scale=number;
        else if(key=="ANGLE")hatch.style.angle=number;
        else hatch.attrs.color=color;
        if(key!="COLOR")hatch.valid=false;
    }
    refresh_hatches();++style_serial_;static_key_=~gcm::u64{0};
    message_="Hatch "+property+" updated";return true;
}

void Shell::draw_hatch_style_bar(gui::Rect& screen) {
    const auto selected=canvas_.selected_hatches();
    if(selected.empty() || selected.front()>=canvas_.hatches().size() || read_only_)return;
    const auto& first=canvas_.hatches()[selected.front()];
    const auto same=[&](auto property){for(auto i:selected)if(i<canvas_.hatches().size() && property(canvas_.hatches()[i])!=property(first))return false;return true;};
    const auto apply=[&](const char* property,const std::string& value) {
        if(canvas_.set_hatch_property(selected,property,value)) {invalidate_frame_caches();log_line(canvas_.message());}
        else log_line("Hatch: invalid "+std::string(property)+" value");
    };
    const float widths[]={88,180,170,100,100};
    const float pad=float(px(12)),gap=float(px(8)),row_h=float(px(54));
    const float available=std::max(float(px(100)),screen.w-2*pad);
    int rows=1;float used=0;
    for(float w:widths){const auto width=std::min(float(px(w)),available);if(used && used+width>available){++rows;used=0;}used+=width+gap;}
    const auto bar=screen.cut_top(rows*row_h+float(px(8)));const auto& theme=themes_.active();
    ui_ctx_.draw().rect(bar,theme.get(gtheme::token::ui_surface));
    ui_ctx_.draw().line({bar.x,bar.bottom()},{bar.right(),bar.bottom()},1,theme.get(gtheme::token::ui_border));
    ui_ctx_.push_id("hatch-style-bar");float x=bar.x+pad,y=bar.y+float(px(4));
    const auto cell=[&](int i,const char* label) {
        const auto width=std::min(float(px(widths[i])),available);
        if(x>bar.x+pad && x+width>bar.right()-pad){x=bar.x+pad;y+=row_h;}
        ui_ctx_.draw().text({x,y,width,float(px(18))},label,ui_ctx_.style().font_size_small,theme.get(gtheme::token::ui_text_dim),gui::TextAlign::Left);
        gui::Rect result{x,y+float(px(19)),width,float(px(30))};x+=width+gap;return result;
    };
    gui::label(ui_ctx_,cell(0,"Hatch style"),std::to_string(selected.size())+" selected",gui::TextAlign::Left,true);
    std::vector<std::string> patterns;for(const auto& p:canvas_.patterns().all())patterns.push_back(p.name);
    if(patterns.empty())patterns.push_back(first.style.name);
    if(!same([](const auto& h){return h.style.name;}))patterns.insert(patterns.begin(),"Mixed");
    int pick=patterns.front()=="Mixed"?0:int(std::find(patterns.begin(),patterns.end(),first.style.name)-patterns.begin());
    if(pick>=int(patterns.size())){patterns.push_back(first.style.name);pick=int(patterns.size()-1);}
    if(gui::dropdown(ui_ctx_,cell(1,"Pattern"),"Hatch pattern",patterns,pick)>=0 && patterns[pick]!="Mixed")apply("PATTERN",patterns[pick]);
    static constexpr int indices[]={1,2,3,4,5,6,7,8,9,20,30,40,60,80,100,120,140,160,180,200,220,240,250,252,254};
    std::vector<std::string> colors={"By layer"},values={"BYLAYER"};
    for(auto index:indices){gcm::u8 r,g,b;gdoc::aci_rgb(index,r,g,b);char label[32];std::snprintf(label,sizeof(label),"%d  #%02X%02X%02X",index,r,g,b);colors.emplace_back(label);values.push_back(std::to_string(index));}
    const auto current=color_value(first.attrs.color);auto found=std::find(values.begin(),values.end(),current);
    if(found==values.end()){values.push_back(current);colors.push_back(current);found=values.end()-1;}
    int color=int(found-values.begin());
    if(!same([](const auto& h){return color_value(h.attrs.color);})){values.push_back("Mixed");colors.push_back("Mixed");color=int(values.size()-1);}
    if(gui::dropdown(ui_ctx_,cell(2,"Hatch color"),"Hatch color",colors,color)>=0 && values[color]!="Mixed")apply("COLOR",values[color]);
    for(auto entry:{std::pair{3,"SCALE"},std::pair{4,"ANGLE"}}) {
        const bool scale=entry.first==3;const char* label=scale?"Hatch scale":"Hatch angle";
        auto& value=scale?hatch_style_scale_:hatch_style_angle_;
        if(!ui_ctx_.is_focused(ui_ctx_.id(label))) {
            const bool equal=scale?same([](const auto& h){return h.style.scale;}):same([](const auto& h){return h.style.angle;});
            value=equal?decimal(scale?first.style.scale:first.style.angle):"Mixed";
        }
        if(gui::numeric_field(ui_ctx_,cell(entry.first,scale?"Scale":"Angle"),label,value,scale?"":"deg").committed)apply(entry.second,value);
    }
    ui_ctx_.pop_id();
}
}
