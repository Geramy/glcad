#include "test.hpp"
#include "ui/shell.hpp"
#include "ui/snapshot.hpp"
#include <filesystem>
#include <fstream>

using namespace gcapp;
namespace {
bool click_style_control(Shell& s,const std::string& label) {
    s.build_frame();
    for(const auto& item:s.accessible_ui()) if(item.label==label &&
        (item.role==gui::Context::Role::Button || item.role==gui::Context::Role::Toggle || item.role==gui::Context::Role::Field)) {
        const auto box=item.bounds;const auto x=box.x+box.w*.5f,y=box.y+box.h*.5f;
        s.pointer_move(x,y);s.build_frame();s.pointer_down(x,y);s.build_frame();s.pointer_up(x,y);s.build_frame();return true;
    }
    return false;
}
}
TEST(text_style_bar_edits_selected_note_and_shows_all_controls) {
    Shell s;s.set_dpr(1);s.set_viewport(1600,1000);std::string error;CHECK(s.load_font(&error));
    auto& c=s.canvas();c.set_display_unit(gunit::Length::Metre);
    c.add_text(gcm::to_lattice(c.volume(),0,0),"Door schedule",.35);
    c.set_selected_texts({0});s.build_frame();
    for(const auto* label:{"Text font","Text height","Bold","Italic","Text color","Background","Highlight"}) {
        bool found=false;for(const auto& item:s.accessible_ui()) found|=item.label==label;
        CHECK_MSG(found,"missing text style control: %s",label);
    }
    CHECK(click_style_control(s,"Bold"));CHECK(c.text_property(c.texts()[0],"BOLD")=="1");
    CHECK(click_style_control(s,"Italic"));CHECK(c.text_property(c.texts()[0],"ITALIC")=="1");
    CHECK(click_style_control(s,"Text color"));CHECK(click_style_control(s,"Text palette #FF0000"));
    CHECK(c.text_property(c.texts()[0],"COLOR")=="#FF0000");
    CHECK(click_style_control(s,"Background"));CHECK(click_style_control(s,"Text palette #FFFFFF"));
    CHECK(c.text_property(c.texts()[0],"BACKGROUND")=="#FFFFFF");
    CHECK(click_style_control(s,"Highlight"));CHECK(click_style_control(s,"Text palette #FFFF00"));
    CHECK(c.text_property(c.texts()[0],"HIGHLIGHT")=="#FFFF00");
    if(const char* dir=std::getenv("GLCAD_UI_REVIEW_DIR")) {
        std::filesystem::create_directories(dir);s.pointer_move(0,0);s.build_frame();
        CHECK(s.save_file((std::filesystem::path(dir)/"text-style-bar.glcad").string()));
        const auto png=snapshot_png(s,1600,1000);CHECK(!png.empty());
        std::ofstream out(std::filesystem::path(dir)/"text-style-bar.png",std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()),png.size());
    }
    s.set_viewport(720,900);s.build_frame();
    for(const auto& item:s.accessible_ui()) if(item.label=="Text font" || item.label=="Highlight")
        CHECK(item.bounds.x>=0 && item.bounds.right()<=720);
}
