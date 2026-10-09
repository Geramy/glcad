#include "ui/shell.hpp"

#include <cmath>
#include <string>

namespace gcapp {

namespace T = gtheme::token;

float Shell::menu_bar_width() const {
    const auto& st=ui_ctx_.style();
    const float unit=float(px(1)), row_h=(tablet_mode_?44:32)*unit;
    float total=16*unit;
    for(const auto& menu:app_menus()) total+=std::max(row_h,ui_ctx_.measure(menu.title,st.font_size)+24*unit);
    return total;
}

void Shell::draw_menu_bar(const gui::Rect& bar, const gui::Rect& row) {
    const auto& theme=themes_.active();
    const auto& st=ui_ctx_.style();
    const float unit=float(px(1)), row_h=bar.h, pad=8*unit;
    const auto& menus=app_menus();
    const float more_w=std::max(row_h,ui_ctx_.measure("More menus",st.font_size)+24*unit);
    float total=0;
    for(const auto& menu:menus) total+=std::max(row_h,ui_ctx_.measure(menu.title,st.font_size)+24*unit);
    const bool overflow=total>bar.w-2*pad;
    const gui::Rect more{std::max(bar.x+pad,bar.right()-pad-more_w),bar.y,std::min(more_w,bar.w-2*pad),bar.h};
    std::vector<gui::Rect> titles(menus.size(),more);
    ui_ctx_.push_id("menubar");
    const auto item=[&](gui::Rect r,const std::string& label,bool selected,bool enabled=true) {
        const auto id=ui_ctx_.id(label);
        const auto in=ui_ctx_.interact(id,r,enabled);
        ui_ctx_.announce(id,gui::Context::Role::Button,label,selected?"expanded":"",r,selected,!enabled);
        if(selected||in.hovered) ui_ctx_.draw().round_rect(r.deflate(gui::Insets::xy(0,4*unit)),4*unit,
            theme.get(selected?T::ui_active:T::ui_hover));
        auto ink=theme.get(T::ui_text);if(!enabled)ink.a=.38f;
        ui_ctx_.draw().text(r.deflate(gui::Insets::xy(8*unit,0)),label,st.font_size,ink,gui::TextAlign::Left);
        return in;
    };
    float x=bar.x+pad;
    int visible=0;
    for(int i=0;i<int(menus.size());++i) {
        const float width=std::max(row_h,ui_ctx_.measure(menus[i].title,st.font_size)+24*unit);
        if(x+width>(overflow?more.x:bar.right()-pad)) break;
        titles[i]={x,bar.y,width,bar.h};
        const auto in=item(titles[i],menus[i].title,open_menu_==i);
        if(in.clicked) {open_menu_=open_menu_==i?-1:i;menu_overflow_open_=false;menu_item_page_=0;}
        else if(!tablet_mode_&&open_menu_>=0&&in.hovered&&open_menu_!=i) {open_menu_=i;menu_item_page_=0;}
        x+=width;++visible;
    }
    if(overflow) {
        const auto in=item(more,"More menus",menu_overflow_open_||open_menu_>=visible);
        if(in.clicked) {menu_overflow_open_=!menu_overflow_open_;open_menu_=-1;menu_overflow_page_=0;}
    } else menu_overflow_open_=false;
    const float available=std::max(row_h,float(vh_)-bar.bottom()-pad);
    const int capacity=std::max(1,int((available-8*unit)/row_h)-1);
    const auto panel=[&](gui::Rect anchor,float width,float height) {
        width=std::min(width,row.w-2*pad);
        return gui::Rect{std::clamp(anchor.x,row.x+pad,row.right()-pad-width),bar.bottom(),width,height};
    };
    const auto begin=[&](const gui::Rect& drop) {
        ui_ctx_.begin_overlay(drop);
        ui_ctx_.draw().shadow(drop,4*unit,16*unit,theme.get(T::ui_shadow));
        ui_ctx_.draw().round_rect(drop,4*unit,theme.get(T::ui_surface_raised));
        ui_ctx_.draw().border(drop,4*unit,1,theme.get(T::ui_border));
    };
    const auto navigation=[&](const gui::Rect& drop,int& page,int pages) {
        if(pages<=1) return;
        gui::Rect nav{drop.x+4*unit,drop.bottom()-row_h-4*unit,drop.w-8*unit,row_h};
        if(item(nav.cut_left(nav.w*.5f),"Previous page",false,page>0).clicked)--page;
        if(item(nav,"Next page",false,page+1<pages).clicked)++page;
    };
    if(menu_overflow_open_) {
        const int count=int(menus.size())-visible,pages=std::max(1,(count+capacity-1)/capacity);
        menu_overflow_page_=std::clamp(menu_overflow_page_,0,pages-1);
        const int first=visible+menu_overflow_page_*capacity,last=std::min(int(menus.size()),first+capacity);
        const auto drop=panel(more,220*unit,8*unit+(last-first+(pages>1?1:0))*row_h);
        begin(drop);
        ui_ctx_.push_id("overflow");
        for(int i=first;i<last;++i) {
            const gui::Rect r{drop.x+4*unit,drop.y+4*unit+(i-first)*row_h,drop.w-8*unit,row_h};
            if(item(r,menus[i].title,false).clicked) {open_menu_=i;menu_item_page_=0;menu_overflow_open_=false;}
        }
        navigation(drop,menu_overflow_page_,pages);
        ui_ctx_.pop_id();
        if(input_.key_escape||(input_.pressed&&!bar.contains(input_.mouse)&&!drop.contains(input_.mouse)))menu_overflow_open_=false;
        ui_ctx_.end_overlay();
    } else if(open_menu_>=0&&open_menu_<int(menus.size())) {
        const auto& menu=menus[open_menu_];
        const int pages=std::max(1,(int(menu.items.size())+capacity-1)/capacity);
        menu_item_page_=std::clamp(menu_item_page_,0,pages-1);
        const int first=menu_item_page_*capacity,last=std::min(int(menu.items.size()),first+capacity);
        float width=180*unit,height=8*unit+(pages>1?row_h:0);
        for(const auto& entry:menu.items) if(!entry.separator()) width=std::max(width,
            ui_ctx_.measure(entry.label,st.font_size)+ui_ctx_.measure(entry.shortcut,st.font_size_small)+56*unit);
        for(int i=first;i<last;++i)height+=menu.items[i].separator()?9*unit:row_h;
        const auto drop=panel(titles[open_menu_],width,height);
        begin(drop);
        ui_ctx_.push_id(menu.title);
        float y=drop.y+4*unit;
        for(int i=first;i<last;++i) {
            const auto& entry=menu.items[i];
            if(entry.separator()) {ui_ctx_.draw().line({drop.x+pad,y+4*unit},{drop.right()-pad,y+4*unit},1,theme.get(T::ui_border));y+=9*unit;continue;}
            const gui::Rect r{drop.x+4*unit,y,drop.w-8*unit,row_h};
            ui_ctx_.push_id(std::to_string(i));
            if(item(r,entry.label,false).clicked) {open_menu_=-1;run_command(entry.command);}
            if(!tablet_mode_&&!entry.shortcut.empty()) ui_ctx_.draw().text(r.deflate(gui::Insets::xy(12*unit,0)),entry.shortcut,
                st.font_size_small,theme.get(T::ui_text_dim),gui::TextAlign::Right);
            ui_ctx_.pop_id();y+=row_h;
        }
        navigation(drop,menu_item_page_,pages);
        ui_ctx_.pop_id();
        if(input_.key_escape||(input_.pressed&&!bar.contains(input_.mouse)&&!drop.contains(input_.mouse)))open_menu_=-1;
        ui_ctx_.end_overlay();
    }
    ui_ctx_.pop_id();
}

void Shell::draw_header(gui::Rect& screen) {
    const auto& theme=themes_.active();
    const float unit=float(px(1)), row_h=(tablet_mode_?44:32)*unit, pad=8*unit, gap=6*unit;
    const auto background=[&](const gui::Rect& r) {
        ui_ctx_.draw().rect(r,theme.get(T::ui_surface));
        ui_ctx_.draw().line({r.x,r.bottom()},{r.right(),r.bottom()},1,theme.get(T::ui_border));
    };
    header_utilities_=false;
    gui::Rect strip{};
    if(!native_menus_) {
        const gui::Rect row=screen.cut_top(row_h);
        background(row);
        gui::Rect inner=row;
        gui::Rect options{};
        if(!tablet_mode_) {inner.cut_right(pad-gap);options=inner.cut_right(row_h);inner.cut_right(gap);}
        if(tablet_mode_) {
            header_utilities_=true;
            const float button=row_h;
            inner.cut_right(pad-gap);
            const auto tap=[&](gui::Rect r,const char* label,bool selected,gui::Icon icon) {
                const auto id=ui_ctx_.id(label);
                const auto hit=ui_ctx_.interact(id,r);
                ui_ctx_.announce(id,gui::Context::Role::Button,label,"",r,selected);
                if(selected||hit.hovered||hit.held) ui_ctx_.draw().round_rect(r.deflate(gui::Insets::all(2*unit)),6*unit,
                    theme.get(selected?T::ui_accent:T::ui_hover));
                gui::draw_icon(ui_ctx_,icon,r,theme.get(selected?T::ui_accent_text:T::ui_text),1.3f);
                return hit.clicked;
            };
            ui_ctx_.push_id("header");
            const gui::Rect settings=inner.cut_right(button);
            const gui::Rect assistant=inner.cut_right(button);
            if(tap(settings,"Settings",settings_open_,gui::Icon::Settings)) settings_open_=!settings_open_;
            if(tap(assistant,"Assistant panel",false,gui::Icon::Assistant)) run_command("ASSISTANT");
            options=inner.cut_right(button);
            if(mode_==Mode::Sketch) draw_plan_check_button(inner.cut_right(button),row);
            ui_ctx_.pop_id();
            inner.cut_right(gap);
        }
        const float menus=menu_bar_width(), pickers=document_pickers_width();
        const float menu_room=std::min(menus,inner.w-pickers-gap);
        if(menu_room>=std::min(menus,420*unit)) {
            gui::Rect menu_area=inner.cut_left(menu_room);
            strip=inner.cut_right(pickers);
            draw_document_pickers(strip,row);
            draw_menu_bar(menu_area,row);
            draw_canvas_options(options,row);
            return;
        }
        draw_menu_bar(inner,row);
        draw_canvas_options(options,row);
    }
    const gui::Rect row=screen.cut_top(tablet_mode_?44*unit:32*unit);
    background(row);
    gui::Rect strip_area=row.deflate(gui::Insets::xy(pad,0));
    gui::Rect options{};
    if(native_menus_) {options=strip_area.cut_right(row.h);strip_area.cut_right(gap);}
    draw_document_pickers(strip_area,row);
    if(native_menus_) draw_canvas_options(options,row);
}

void Shell::draw_text_entry() {
    if (!text_entry_) return;
    double u, v, wx, wy, wz, sx, sy;
    gcm::to_world(vol_, text_pos_, u, v);
    sketch_plane().to_world(u, v, wx, wy, wz);
    if (!camera_.project(gview::Point3::from(wx, wy, wz), sx, sy)) return;
    const float h = float(text_height_ * canvas_.view().scale());
    const auto& theme = themes_.active();
    const gtheme::Color c = theme.get(T::ui_accent);
    emit_text({float(sx), float(sy) - h, float(px(600)), h}, text_buffer_ + "|", std::max(h, float(px(10))),
              gui::Color{c.r, c.g, c.b, 1.0f}, gui::TextAlign::Left, gui::Rect{0, 0, float(vw_), float(vh_)});
    gui::Rect field{canvas_rect_.x+float(px(12)),canvas_rect_.bottom()-float(px(62)),
                    std::min(float(px(360)),canvas_rect_.w-float(px(90))),float(px(44))};
    const auto id=ui_ctx_.id("Note text");
    if (!ui_ctx_.focus_is_field() || ui_ctx_.is_focused(ui_ctx_.id("command-input"))) ui_ctx_.set_focus(id);
    gui::text_field(ui_ctx_,field,"Note text",text_buffer_);
    gui::Rect ok{field.right()+float(px(6)),field.y,float(px(56)),field.h};
    if (gui::button(ui_ctx_,ok,"Place text",true) || (ui_ctx_.is_focused(id) && input_.key_enter)) {
        input_.key_enter=false;
        ui_ctx_.clear_focus();
        key_special(0);
    }
}
}
