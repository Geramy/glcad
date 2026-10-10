#pragma once

#include <vector>

#include "math/config.hpp"
#include "ui/icons.hpp"

namespace gcapp {

enum class Mode : gcm::u8 { Sketch, Solid, Drawing };

enum class ToolId : gcm::u16 {
    Select, Line, Polyline, Rect, Circle, Arc, Ellipse, Spline, Point, Polygon,
    Move, Copy, Rotate, Mirror, Offset, Trim, Extend, Fillet, Chamfer,
    Scale, Stretch, Break,
    ArrayRect, ArrayPolar,
    DimLinear, DimAligned, DimAngular, DimRadius, DimDiameter, Hatch, Text,
    FinishSketch,
    NewSketch,
    Extrude, Revolve, Sweep, Loft, PressPull,
    Box, Cylinder, Sphere,
    FilletEdge, ChamferEdge, ShellBody, OffsetFace, Split, DeleteBody,
    Hole, Draft, Slice,
    Union, Subtract, Intersect,
    MeasureVolume, MeasureDistance, Section,
    PatternBody, MirrorBody, Mate,
    GroupSel, Ungroup,
    PlaceLayout, RevCloud,
    Assistant,
    Count
};

enum class ToolGroup : gcm::u8 {
    SketchSelect, Draw, Modify, Annotate, Finish,
    SolidSelect, Sketch, Create, Refine, Combine, Measure, Transform,
    DrawingSelect, Layout, Markup,
};

struct ToolDef {
    ToolId      id;
    const char* label;
    gui::Icon   icon;
    const char* command;
    ToolGroup   group;
    bool        wired;
};

struct ToolGroupDef {
    ToolGroup   group;
    Mode        mode;
    const char* caption;
};

const std::vector<ToolDef>&      tool_defs();
std::vector<const ToolDef*>      tools_for_mode(Mode mode);
struct ShapeCommand { const char* label; const char* command; };
bool                             is_shape_tool(ToolId id) noexcept;
std::vector<const ToolDef*>      shape_tools(Mode mode);
const std::vector<ShapeCommand>& shape_commands(Mode mode);
const ToolDef&                   shapes_def(Mode mode);
std::vector<const ToolDef*>      toolbar_tools(Mode mode);
const std::vector<ToolGroupDef>& tool_groups();
const ToolDef*                   tool_def(ToolId id) noexcept;
Mode                             tool_mode(ToolId id) noexcept;
bool                             tool_in_mode(ToolId id, Mode m) noexcept;
const char*                      mode_name(Mode m) noexcept;

struct ToolbarGroupLayout {
    const ToolGroupDef* group;
    std::vector<const ToolDef*> tools;
    gui::Rect bounds;
};

struct ToolbarLayout {
    gui::Rect modes, settings, assistant, plan_check;
    std::vector<ToolbarGroupLayout> groups;
    float height{0};
};

ToolbarLayout layout_toolbar(Mode mode, const gui::Rect& area, float scale);

}
