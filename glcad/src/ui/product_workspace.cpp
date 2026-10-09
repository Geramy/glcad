#include "ui/shell.hpp"
#include "ui/widgets.hpp"

namespace gcapp {
void Shell::set_product_edit_access(bool allowed) {
    if (product_edit_allowed_==allowed) return;
    if (!allowed) {
        cancel_viewport_drag();
        canvas_.cancel_grip_drag();
        return_to_select();
        prepare_suspend();
        pending_args_.clear(); cli_->cancel(); bar_.input.clear();
        ui_ctx_.clear_focus();
        input_.pressed=input_.released=input_.down=false;
        input_.key_enter=false; input_.typed.clear();
        input_.replacement_id=0; input_.replacement_text.clear();
        hover_target_={}; region_probe_pending_=false;
    }
    product_edit_allowed_=allowed;
    invalidate_frame_caches();
}

bool Shell::require_edit_access() {
    if (product_edit_allowed_) return true;
    log_.push_back("Editing requires an active subscription. Open Account to restore or subscribe; your files remain available.");
    return false;
}

void Shell::draw_product_viewer() {
    namespace T=gtheme::token;
    const auto& theme=themes_.active();
    const auto& style=ui_ctx_.style();
    gui::Rect screen{0,0,float(vw_),float(vh_)};
    auto top=screen.cut_top(float(px(56)));
    ui_ctx_.draw().rect(top,theme.get(T::ui_surface));
    auto row=top.deflate(gui::Insets::all(float(px(6))));
    const float width=std::min(float(px(102)),row.w/5);
    const auto button=[&](const char* label) {
        return gui::button(ui_ctx_,row.cut_left(width).deflate(gui::Insets::all(float(px(2)))),label);
    };
    if(button("Open")) ask_file(FileAsk::Open);
    if(button("Save copy")) ask_file(FileAsk::Save);
    if(button("Export")) ask_file(FileAsk::Export);
    if(button("Fit view")) run_command("ZOOMEXTENTS");
    if(button("Account") && product_hook_) product_hook_();
    draw_tab_bar(screen);
    auto status=screen.cut_bottom(float(px(42)));
    ui_ctx_.draw().rect(status,theme.get(T::ui_surface));
    ui_ctx_.draw().text(status.deflate(gui::Insets::all(float(px(8)))),
        "Viewer • Pinch to zoom; two fingers to pan or orbit. Your files stay available.",
        style.font_size_small,theme.get(T::ui_text));
    canvas_rect_=screen;
}
}
