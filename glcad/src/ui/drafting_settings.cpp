#include "ui/shell.hpp"
#include "ui/widgets.hpp"
#include <cmath>
#include <sstream>

namespace gcapp {
namespace {
constexpr double pi=3.14159265358979323846;
std::string color_name(const gdoc::Color& c) {
    if(c.kind==gdoc::Color::Kind::ByLayer) return "BYLAYER";
    if(c.kind==gdoc::Color::Kind::ByBlock) return "BYBLOCK";
    return std::to_string(c.index);
}
}

void Shell::sync_drafting_vars(bool publish) {
    const auto number=[&](const char* name,double current,const auto& setter) {
        auto* var=vars_.find(name);if(!var) return;
        double value=var->type==gcmd::VarType::Int?double(var->ival):var->rval;
        if(publish) {
            if(var->type==gcmd::VarType::Int) var->ival=gcm::i64(current);else var->rval=current;
            drafting_seen_[name]=current;
        } else if(!drafting_seen_.contains(name) || drafting_seen_[name]!=value) {
            drafting_seen_[name]=value;setter(value);
        }
    };
    const auto text=[&](const char* name,const std::string& current,const auto& setter) {
        auto* var=vars_.find(name);if(!var) return;
        if(publish) {var->sval=current;drafting_text_seen_[name]=current;}
        else if(!drafting_text_seen_.contains(name)||drafting_text_seen_[name]!=var->sval) {
            drafting_text_seen_[name]=var->sval;setter(var->sval);
        }
    };
    number("PICKADD",canvas_.selection_add_mode,[&](double v){canvas_.selection_add_mode=int(v);});
    number("BLIPMODE",canvas_.blip_mode,[&](double v){canvas_.blip_mode=v!=0;if(!v) canvas_.blips.clear();});
    number("INSUNITS",canvas_.display_unit()==gunit::Length::Inch?1:canvas_.display_unit()==gunit::Length::Foot?2:canvas_.display_unit()==gunit::Length::Millimetre?4:canvas_.display_unit()==gunit::Length::Centimetre?5:6,[&](double v){
        if(v==1) canvas_.set_display_unit(gunit::Length::Inch);else if(v==2) canvas_.set_display_unit(gunit::Length::Foot);
        else if(v==4) canvas_.set_display_unit(gunit::Length::Millimetre);else if(v==5) canvas_.set_display_unit(gunit::Length::Centimetre);else if(v==6) canvas_.set_display_unit(gunit::Length::Metre);
    });
    number("SNAPANG",canvas_.grid_angle*180/pi,[&](double v){canvas_.grid_angle=v*pi/180;});
    number("HIGHLIGHT",canvas_.highlight_selection,[&](double v){if(canvas_.highlight_selection!=(v!=0)) {canvas_.highlight_selection=v!=0;canvas_.bump_edit_serial();}});
    number("AUTOSNAP",double(vars_.get_int("AUTOSNAP")),[&](double v){canvas_.snap_marker=(int(v)&1)!=0;canvas_.snaps().settings.tracking=(int(v)&16)!=0;});
    number("OFFSETDIST",canvas_.offset_distance(),[&](double v){canvas_.offset_distance()=v;});
    number("ARRAYROWS",canvas_.array_rows(),[&](double v){canvas_.array_rows()=int(v);});
    number("ARRAYCOLS",canvas_.array_columns(),[&](double v){canvas_.array_columns()=int(v);});
    number("ARRAYCOUNT",canvas_.array_count(),[&](double v){canvas_.array_count()=int(v);});
    number("ARRAYANGLE",canvas_.array_angle()*180/pi,[&](double v){canvas_.array_angle()=v*pi/180;});
    number("CHAMFERB",canvas_.second_chamfer_distance(),[&](double v){canvas_.second_chamfer_distance()=v;});
    number("ORTHOMODE",canvas_.polar().ortho,[&](double v){canvas_.polar().ortho=v!=0;});
    number("POLARANG",canvas_.polar().increment_deg,[&](double v){canvas_.polar().increment_deg=v;});
    number("SNAPMODE",canvas_.grid_snap(),[&](double v){canvas_.set_grid_snap(v!=0);});
    number("GRIDMODE",grid_visible_,[&](double v){grid_visible_=v!=0;});
    number("GRIDUNIT",canvas_.grid_spacing(),[&](double v){canvas_.set_grid_spacing(v);});
    number("APERTURE",canvas_.snaps().settings.aperture_px,[&](double v){canvas_.snaps().settings.aperture_px=v;});
    number("PICKBOX",canvas_.pickbox_pixels,[&](double v){canvas_.pickbox_pixels=v;});
    number("DYNMODE",double(vars_.get_int("DYNMODE")),[&](double v){canvas_.dynamic_input_enabled=v>0;});
    number("CELTSCALE",canvas_.current_attributes.linetype_scale,[&](double v){canvas_.current_attributes.linetype_scale=v;});
    auto weight=canvas_.current_attributes.lineweight;
    number("CELWEIGHT",weight==gdoc::Lineweight::ByLayer?-1:weight==gdoc::Lineweight::ByBlock?-2:weight==gdoc::Lineweight::Default?-3:int(weight),[&](double v){
        canvas_.current_attributes.lineweight=v==-1?gdoc::Lineweight::ByLayer:v==-2?gdoc::Lineweight::ByBlock:v==-3?gdoc::Lineweight::Default:gdoc::nearest_lineweight(v/100);
    });
    text("CANNOSCALE",canvas_.annotation_scale_name(),[&](const std::string& v){
        if(const auto* scale=annotation_scales_.find(v)) canvas_.set_annotation_scale(v,scale->factor());
    });
    number("CANNOSCALEVALUE",1.0/canvas_.annotation_factor(),[](double){});
    text("CLAYER",layers_.current(),[&](const std::string& v){if(layers_.find(v)){layers_.set_current(v);canvas_.set_current_layer(v);}});
    text("CELTYPE",canvas_.current_attributes.linetype,[&](const std::string& v){
        const auto n=gcmd::to_upper(v);canvas_.current_attributes.linetype=n=="BYLAYER"?"ByLayer":n=="BYBLOCK"?"ByBlock":v;
    });
    text("CECOLOR",color_name(canvas_.current_attributes.color),[&](const std::string& v){
        const auto n=gcmd::to_upper(v);
        if(n=="BYLAYER") canvas_.current_attributes.color=gdoc::Color::by_layer();
        else if(n=="BYBLOCK") canvas_.current_attributes.color=gdoc::Color::by_block();
        else {int index=gdoc::aci_of_name(v);if(!index) index=std::atoi(v.c_str());canvas_.current_attributes.color=gdoc::Color::aci(index);}
    });
    text("TEXTSTYLE",text_styles_.current(),[&](const std::string& v){if(auto* st=text_styles_.find(v)){text_styles_.set_current(v);if(st->height_fixed()) text_height_=st->height;}});
    text("DIMSTYLE",canvas_.dim_styles().current(),[&](const std::string& v){canvas_.dim_styles().set_current(v);});
    text("CMLSTYLE",canvas_.multiline_style(),[&](const std::string& v){canvas_.set_multiline_style(v);});
    auto& hatch=canvas_.hatch_style();
    text("HPNAME",hatch.name,[&](const std::string& v){hatch.name=v;});
    number("HPSCALE",hatch.scale,[&](double v){hatch.scale=v;});
    number("HPANG",hatch.angle,[&](double v){hatch.angle=v;});
    const auto* dim=canvas_.dim_styles().find(canvas_.dim_styles().current());
    number("DIMSCALE",dim?dim->dimscale:1,[&](double v){if(dim && dim->dimscale!=v){auto st=*dim;st.dimscale=v;canvas_.dim_styles().add(st);canvas_.bump_edit_serial();}});
}

bool Shell::set_drafting_var(const std::string& name,const std::string& value) {
    std::string error;
    const auto n=gcmd::to_upper(name),v=gcmd::to_upper(value);
    if(n=="CANNOSCALE" && !annotation_scales_.find(value)) error="unknown scale; use SCALELISTEDIT ADD to define it";
    if(n=="CLAYER" && !layers_.find(value)) error="unknown layer";
    if(n=="TEXTSTYLE" && !text_styles_.find(value)) error="unknown text style";
    if(n=="DIMSTYLE" && !canvas_.dim_styles().find(value)) error="unknown dimension style";
    if(n=="CMLSTYLE" && !canvas_.multilines().find(value)) error="unknown multiline style";
    if(n=="CELTYPE" && v!="BYLAYER" && v!="BYBLOCK" && !linetypes_.find(value)) error="unknown linetype";
    if(n=="HPNAME" && !canvas_.patterns().find(value)) error="unknown hatch pattern";
    if(n=="CECOLOR" && v!="BYLAYER" && v!="BYBLOCK" && !gdoc::aci_of_name(value)) {
        char* end=nullptr;auto index=std::strtol(value.c_str(),&end,10);
        if(!end || *end || index<1 || index>255) error="use BYLAYER, BYBLOCK, a color name, or an index from 1 to 255";
    }
    if(n=="AUNITS" && (v=="1" || v=="4")) error="DMS and survey bearing formats are not supported; use 0 (degrees), 2 (gradians), or 3 (radians)";
    if(n=="POLARMODE" && v!="0") error="only absolute polar tracking (0) is supported";
    if(n=="INSUNITS" && v!="0" && v!="1" && v!="2" && v!="4" && v!="5" && v!="6") error="supported insertion units are 0, 1, 2, 4, 5 and 6";
    if(error.empty() && !vars_.set_value(n,value,&error)) {}
    if(!error.empty()) {log_.push_back(n+": "+error);return false;}
    drafting_fields_.erase(n);
    apply_sysvars();publish_sysvars();log_.push_back(n+" set to "+value);return true;
}

void Shell::draw_settings_drafting(gui::Stack& stack) {
    const auto& theme=themes_.active();const auto& style=ui_ctx_.style();
    auto title=[&](const char* label){stack.gap(float(px(8)));ui_ctx_.draw().text(stack.next(float(px(24))),label,style.font_size,theme.get(gtheme::token::ui_text),gui::TextAlign::Left);};
    auto field=[&](const char* name,const char* label,bool distance=false) {
        const auto* var=vars_.find(name);if(!var) return;
        auto row=stack.next(float(px(36)));auto caption=row.cut_left(row.w*.56f);
        ui_ctx_.draw().text(caption,label,style.font_size,theme.get(gtheme::token::ui_text_dim),gui::TextAlign::Left);
        auto [it,inserted]=drafting_fields_.try_emplace(name);
        if(inserted) {
            double value=var->type==gcmd::VarType::Int?double(var->ival):var->rval;
            if(distance) value/=gunit::metres_per(canvas_.display_unit());
            std::ostringstream ss;ss<<value;it->second=ss.str();
        }
        auto result=gui::numeric_field(ui_ctx_,row,name,it->second,distance?gunit::suffix(canvas_.display_unit()):"");
        if(result.committed) {
            std::string value=result.text;
            if(distance) {auto parsed=gunit::parse_length(value,canvas_.display_unit());if(parsed)value=std::to_string(*parsed);}
            set_drafting_var(name,value);drafting_fields_.erase(name);
        }
    };
    auto toggle=[&](const char* name,const char* label) {
        bool on=vars_.get_int(name)!=0;
        if(gui::toggle(ui_ctx_,stack.next(float(px(34))),label,on)) set_drafting_var(name,on?"1":"0");
    };
    title("Editing defaults");
    field("OFFSETDIST","Offset distance",true);field("FILLETRAD","Fillet radius",true);
    field("CHAMFERA","Chamfer first distance",true);field("CHAMFERB","Chamfer second distance",true);
    field("ARRAYROWS","Array rows");field("ARRAYCOLS","Array columns");field("ARRAYCOUNT","Polar / path items");field("ARRAYANGLE","Polar sweep (degrees)");
    title("Drafting aids");toggle("ORTHOMODE","Orthogonal drawing");toggle("SNAPMODE","Grid snap");toggle("GRIDMODE","Show grid");
    field("GRIDUNIT","Grid spacing (0 = automatic)",true);field("SNAPANG","Grid rotation (degrees)");field("POLARANG","Polar increment (degrees)");
    field("LUPREC","Coordinate decimal places");field("AUPREC","Angle decimal places");field("APERTURE","Object snap aperture (pixels)");field("PICKBOX","Selection aperture (pixels)");toggle("DYNMODE","Dynamic input");
    title("Annotation and strokes");
    {
        std::vector<std::string> names;int selected=0;
        for(const auto& scale:annotation_scales_.all()) {
            if(scale.name==canvas_.annotation_scale_name()) selected=int(names.size());
            names.push_back(scale.name);
        }
        ui_ctx_.draw().text(stack.next(float(px(22))),"Annotation scale",style.font_size,theme.get(gtheme::token::ui_text_dim),gui::TextAlign::Left);
        if(gui::dropdown(ui_ctx_,stack.next(float(px(34))),"annotation-scale",names,selected)>=0)
            set_drafting_var("CANNOSCALE",names[std::size_t(selected)]);
    }
    field("TEXTSIZE","Text height",true);field("DIMSCALE","Dimension scale");field("HPSCALE","Hatch scale");field("HPANG","Hatch angle (degrees)");field("LTSCALE","Linetype scale");toggle("LWDISPLAY","Display lineweights");
}
}
