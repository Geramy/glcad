#pragma once

#include <string>
#include <vector>

#include "ui/context.hpp"

namespace gui {

enum class Icon : u16 {
    None,
    Line, Polyline, Rect, Circle, Arc, Ellipse, Spline, Point, Hatch,
    Move, Copy, Rotate, Scale, Mirror, Offset, Trim, Extend, Fillet, Erase,
    Array, Explode,
    Text, Dimension, Leader, Table,
    Extrude, Revolve, Sweep, Loft, Shell, Union, Subtract, Intersect, Box,
    Pan, Zoom, Orbit, ZoomExtents, Layers, Grid, Ortho, Snap,
    ChevronLeft, ChevronRight, ChevronUp, ChevronDown, Close, Menu, Plus, Minus,
    Lock, Unlock, Eye, EyeOff, Snowflake, Pin, Undo, Redo, Settings, Search,
    Chamfer, Split, Section, Pattern, NewSketch, Finish, Measure, DeleteBody,
    Assistant, Call, Mic, Send, Attach, CallEnd,
    CheckPlan,
};

u32 icon_codepoint(Icon i) noexcept;
const char* icon_symbol_name(Icon i) noexcept;
std::string icon_utf8(Icon i);
const std::vector<u32>& icon_codepoints();

void draw_icon(Context& c, Icon icon, const Rect& r, Color color,
               f32 weight = 1.6f);

const char* icon_name(Icon i) noexcept;

}
