#include "ui/icons.hpp"

namespace gui {

u32 icon_codepoint(Icon i) noexcept {
    switch (i) {
        case Icon::Line: return 0xF825;   // line_start
        case Icon::Polyline: return 0xEBBB;   // polyline
        case Icon::Rect: return 0xEB54;   // rectangle
        case Icon::Circle: return 0xEF4A;   // circle
        case Icon::Arc: return 0xF757;   // line_curve
        case Icon::Ellipse: return 0xE40C;   // panorama_fish_eye
        case Icon::Spline: return 0xE155;   // gesture
        case Icon::Point: return 0xE061;   // fiber_manual_record
        case Icon::Hatch: return 0xE421;   // texture
        case Icon::Move: return 0xE89F;   // open_with
        case Icon::Copy: return 0xE14D;   // content_copy
        case Icon::Rotate: return 0xE418;   // rotate_90_degrees_ccw
        case Icon::Scale: return 0xF1CE;   // open_in_full
        case Icon::Mirror: return 0xE3E8;   // flip
        case Icon::Offset: return 0xE91A;   // line_weight
        case Icon::Trim: return 0xE14E;   // content_cut
        case Icon::Extend: return 0xF830;   // expand_content
        case Icon::Fillet: return 0xE920;   // rounded_corner
        case Icon::Erase: return 0xE92E;   // delete
        case Icon::Array: return 0xE9B0;   // grid_view
        case Icon::Explode: return 0xE0B6;   // call_split
        case Icon::Text: return 0xE262;   // text_fields
        case Icon::Dimension: return 0xEA49;   // square_foot
        case Icon::Leader: return 0xE569;   // near_me
        case Icon::Table: return 0xE265;   // table_chart
        case Icon::Extrude: return 0xF09B;   // upload
        case Icon::Revolve: return 0xE84D;   // 3d_rotation
        case Icon::Sweep: return 0xEACD;   // route
        case Icon::Loft: return 0xF22B;   // stacked_line_chart
        case Icon::Shell: return 0xE3C2;   // crop_free
        case Icon::Union: return 0xF84F;   // join_full
        case Icon::Subtract: return 0xEB7D;   // difference
        case Icon::Intersect: return 0xEAF4;   // join_inner
        case Icon::Box: return 0xF720;   // deployed_code
        case Icon::Pan: return 0xE925;   // pan_tool
        case Icon::Zoom: return 0xE8FF;   // zoom_in
        case Icon::Orbit: return 0xE577;   // 360
        case Icon::ZoomExtents: return 0xEA10;   // fit_screen
        case Icon::Layers: return 0xE53B;   // layers
        case Icon::Grid: return 0xE3EC;   // grid_on
        case Icon::Ortho: return 0xE41C;   // straighten
        case Icon::Snap: return 0xE3B4;   // center_focus_strong
        case Icon::ChevronLeft: return 0xE5CB;   // chevron_left
        case Icon::ChevronRight: return 0xE5CC;   // chevron_right
        case Icon::ChevronUp: return 0xE5CE;   // expand_less
        case Icon::ChevronDown: return 0xE5CF;   // expand_more
        case Icon::Close: return 0xE5CD;   // close
        case Icon::Menu: return 0xE5D2;   // menu
        case Icon::Plus: return 0xE145;   // add
        case Icon::Minus: return 0xE15B;   // remove
        case Icon::Lock: return 0xE899;   // lock
        case Icon::Unlock: return 0xE898;   // lock_open
        case Icon::Eye: return 0xE8F4;   // visibility
        case Icon::EyeOff: return 0xE8F5;   // visibility_off
        case Icon::Snowflake: return 0xEB3B;   // ac_unit
        case Icon::Pin: return 0xF10D;   // push_pin
        case Icon::Undo: return 0xE166;   // undo
        case Icon::Redo: return 0xE15A;   // redo
        case Icon::Settings: return 0xE8B8;   // settings
        case Icon::Search: return 0xEF7A;   // search
        case Icon::Chamfer: return 0xE86B;   // change_history
        case Icon::Split: return 0xE949;   // vertical_split
        case Icon::Section: return 0xE947;   // horizontal_split
        case Icon::Pattern: return 0xE5C3;   // apps
        case Icon::NewSketch: return 0xE746;   // draw
        case Icon::Finish: return 0xE668;   // check
        case Icon::Measure: return 0xF6AF;   // measuring_tape
        case Icon::DeleteBody: return 0xE92B;   // delete_forever
        case Icon::Assistant: return 0xE0BF;   // forum
        case Icon::Call: return 0xE0B0;   // call
        case Icon::Mic: return 0xE029;   // mic
        case Icon::Send: return 0xE163;   // send
        case Icon::Attach: return 0xE226;   // attach_file
        case Icon::CallEnd: return 0xE0B1;   // call_end
        case Icon::Cursor: return 0xF82F;   // arrow_selector_tool
        case Icon::Shapes: return 0xE72C;   // category
        case Icon::Polygon: return 0xEB39;   // hexagon
        case Icon::Stretch: return 0xF69B;   // arrow_range
        case Icon::BreakLine: return 0xE16F;   // link_off
        case Icon::PolarArray: return 0xEA0F;   // workspaces
        case Icon::Group: return 0xE162;   // select_all
        case Icon::Diameter: return 0xF08C;   // block
        case Icon::PushPull: return 0xEA16;   // height
        case Icon::Hole: return 0xE39E;   // adjust
        case Icon::CheckPlan: return 0xF385;   // document_search
        case Icon::Options: return 0xE429;   // tune
        default: return 0;
    }
}

const char* icon_symbol_name(Icon i) noexcept {
    switch (i) {
        case Icon::Line: return "line_start";
        case Icon::Polyline: return "polyline";
        case Icon::Rect: return "rectangle";
        case Icon::Circle: return "circle";
        case Icon::Arc: return "line_curve";
        case Icon::Ellipse: return "panorama_fish_eye";
        case Icon::Spline: return "gesture";
        case Icon::Point: return "fiber_manual_record";
        case Icon::Hatch: return "texture";
        case Icon::Move: return "open_with";
        case Icon::Copy: return "content_copy";
        case Icon::Rotate: return "rotate_90_degrees_ccw";
        case Icon::Scale: return "open_in_full";
        case Icon::Mirror: return "flip";
        case Icon::Offset: return "line_weight";
        case Icon::Trim: return "content_cut";
        case Icon::Extend: return "expand_content";
        case Icon::Fillet: return "rounded_corner";
        case Icon::Erase: return "delete";
        case Icon::Array: return "grid_view";
        case Icon::Explode: return "call_split";
        case Icon::Text: return "text_fields";
        case Icon::Dimension: return "square_foot";
        case Icon::Leader: return "near_me";
        case Icon::Table: return "table_chart";
        case Icon::Extrude: return "upload";
        case Icon::Revolve: return "3d_rotation";
        case Icon::Sweep: return "route";
        case Icon::Loft: return "stacked_line_chart";
        case Icon::Shell: return "crop_free";
        case Icon::Union: return "join_full";
        case Icon::Subtract: return "difference";
        case Icon::Intersect: return "join_inner";
        case Icon::Box: return "deployed_code";
        case Icon::Pan: return "pan_tool";
        case Icon::Zoom: return "zoom_in";
        case Icon::Orbit: return "360";
        case Icon::ZoomExtents: return "fit_screen";
        case Icon::Layers: return "layers";
        case Icon::Grid: return "grid_on";
        case Icon::Ortho: return "straighten";
        case Icon::Snap: return "center_focus_strong";
        case Icon::ChevronLeft: return "chevron_left";
        case Icon::ChevronRight: return "chevron_right";
        case Icon::ChevronUp: return "expand_less";
        case Icon::ChevronDown: return "expand_more";
        case Icon::Close: return "close";
        case Icon::Menu: return "menu";
        case Icon::Plus: return "add";
        case Icon::Minus: return "remove";
        case Icon::Lock: return "lock";
        case Icon::Unlock: return "lock_open";
        case Icon::Eye: return "visibility";
        case Icon::EyeOff: return "visibility_off";
        case Icon::Snowflake: return "ac_unit";
        case Icon::Pin: return "push_pin";
        case Icon::Undo: return "undo";
        case Icon::Redo: return "redo";
        case Icon::Settings: return "settings";
        case Icon::Search: return "search";
        case Icon::Chamfer: return "change_history";
        case Icon::Split: return "vertical_split";
        case Icon::Section: return "horizontal_split";
        case Icon::Pattern: return "apps";
        case Icon::NewSketch: return "draw";
        case Icon::Finish: return "check";
        case Icon::Measure: return "measuring_tape";
        case Icon::DeleteBody: return "delete_forever";
        case Icon::Assistant: return "forum";
        case Icon::Call: return "call";
        case Icon::Mic: return "mic";
        case Icon::Send: return "send";
        case Icon::Attach: return "attach_file";
        case Icon::CallEnd: return "call_end";
        case Icon::Cursor: return "arrow_selector_tool";
        case Icon::Shapes: return "category";
        case Icon::Polygon: return "hexagon";
        case Icon::Stretch: return "arrow_range";
        case Icon::BreakLine: return "link_off";
        case Icon::PolarArray: return "workspaces";
        case Icon::Group: return "select_all";
        case Icon::Diameter: return "block";
        case Icon::PushPull: return "height";
        case Icon::Hole: return "adjust";
        case Icon::CheckPlan: return "document_search";
        case Icon::Options: return "tune";
        default: return "";
    }
}

std::string icon_utf8(Icon i) {
    const u32 cp = icon_codepoint(i);
    if (cp == 0) return {};
    std::string s;
    s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    return s;
}

const std::vector<u32>& icon_codepoints() {
    static const std::vector<u32> all = {
        0xF825, 0xEBBB, 0xEB54, 0xEF4A, 0xF757, 0xE40C, 0xE155, 0xE061, 0xE421, 0xE89F, 0xE14D, 0xE418, 0xF1CE, 0xE3E8, 0xE91A, 0xE14E, 0xF830, 0xE920, 0xE92E, 0xE9B0, 0xE0B6, 0xE262, 0xEA49, 0xE569, 0xE265, 0xF09B, 0xE84D, 0xEACD, 0xF22B, 0xE3C2, 0xF84F, 0xEB7D, 0xEAF4, 0xF720, 0xE925, 0xE8FF, 0xE577, 0xEA10, 0xE53B, 0xE3EC, 0xE41C, 0xE3B4, 0xE5CB, 0xE5CC, 0xE5CE, 0xE5CF, 0xE5CD, 0xE5D2, 0xE145, 0xE15B, 0xE899, 0xE898, 0xE8F4, 0xE8F5, 0xEB3B, 0xF10D, 0xE166, 0xE15A, 0xE8B8, 0xEF7A, 0xE86B, 0xE949, 0xE947, 0xE5C3, 0xE746, 0xE668, 0xF6AF, 0xE92B, 0xE0BF, 0xE0B0, 0xE029, 0xE163, 0xE226, 0xE0B1, 0xF82F, 0xE72C, 0xEB39, 0xF69B, 0xE16F, 0xEA0F, 0xE162, 0xF08C, 0xEA16, 0xE39E, 0xF385, 0xE429,
    };
    return all;
}

}
