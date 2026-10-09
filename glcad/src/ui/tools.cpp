#include "ui/tools.hpp"

#include <algorithm>

namespace gcapp {

using gui::Icon;

ToolbarLayout layout_toolbar(Mode mode, const gui::Rect& area, float scale) {
    ToolbarLayout out;
    const float left = area.x + 12 * scale;
    const float right = area.right() - 12 * scale;
    const float width = std::max(0.0f, right - left);
    const float button = std::min(40 * scale, width);
    const float gap = 2 * scale;
    const float pitch = button + gap;
    const float row_height = 72 * scale;
    int row = 0;
    auto y = [&] { return area.y + 4 * scale + row * row_height; };
    out.settings = {right - button, y(), button, 40 * scale};
    out.assistant = {out.settings.x - button - 8 * scale, y(), button, 40 * scale};
    float utilities_left = out.assistant.x;
    if (mode == Mode::Sketch) {
        out.plan_check = {utilities_left - button - 8 * scale, y(), button, 40 * scale};
        utilities_left = out.plan_check.x;
    }
    if (utilities_left - left < (148 + 16) * scale) ++row;
    out.modes = {left, y(), std::min(148 * scale, width), 40 * scale};
    float x = out.modes.right() + 16 * scale;
    auto row_right = [&] { return row == 0 ? utilities_left - 8 * scale : right; };
    auto next_row = [&] { ++row; x = left; };
    for (const auto& group : tool_groups()) {
        if (group.mode != mode) continue;
        std::vector<const ToolDef*> defs;
        for (const auto& tool : tool_defs())
            if (tool.group == group.group) defs.push_back(&tool);
        const float group_width = defs.size() * pitch - gap;
        if (group_width <= width && x + group_width > row_right() && x + 3 * pitch > row_right()) next_row();
        std::size_t at = 0;
        while (at < defs.size()) {
            if (x + button > row_right()) next_row();
            const float start = x;
            ToolbarGroupLayout fragment{&group, {}, {}};
            do {
                fragment.tools.push_back(defs[at++]);
                x += pitch;
            } while (at < defs.size() && x + button <= row_right());
            fragment.bounds = {start, y(), x - gap - start, 56 * scale};
            out.groups.push_back(std::move(fragment));
            if (at < defs.size()) next_row();
        }
        x += 14 * scale;
    }
    out.height = (row + 1) * row_height;
    return out;
}

const std::vector<ToolGroupDef>& tool_groups() {
    static const std::vector<ToolGroupDef> g = {
        {ToolGroup::SketchSelect, Mode::Sketch, "Select"},
        {ToolGroup::Draw,         Mode::Sketch, "Draw"},
        {ToolGroup::Modify,       Mode::Sketch, "Modify"},
        {ToolGroup::Annotate,     Mode::Sketch, "Annotate"},
        {ToolGroup::Finish,       Mode::Sketch, "Done"},
        {ToolGroup::SolidSelect,  Mode::Solid,  "Select"},
        {ToolGroup::Sketch,       Mode::Solid,  "Sketch"},
        {ToolGroup::Create,       Mode::Solid,  "Create"},
        {ToolGroup::Refine,       Mode::Solid,  "Refine"},
        {ToolGroup::Combine,      Mode::Solid,  "Combine"},
        {ToolGroup::Measure,      Mode::Solid,  "Measure"},
        {ToolGroup::Transform,    Mode::Solid,  "Transform"},
        {ToolGroup::DrawingSelect,Mode::Drawing,"Select"},
        {ToolGroup::Layout,       Mode::Drawing,"Layout"},
        {ToolGroup::Markup,       Mode::Drawing,"Markup"},
    };
    return g;
}

const std::vector<ToolDef>& tool_defs() {
    static const std::vector<ToolDef> t = {
        {ToolId::Select,      "Select",       Icon::Point,      "SELECT",      ToolGroup::SketchSelect, true},
        {ToolId::Line,        "Line",         Icon::Line,       "LINE",        ToolGroup::Draw,     true},
        {ToolId::Polyline,    "Polyline",     Icon::Polyline,   "PLINE",       ToolGroup::Draw,     true},
        {ToolId::Rect,        "Rectangle",    Icon::Rect,       "RECTANG",     ToolGroup::Draw,     true},
        {ToolId::Circle,      "Circle",       Icon::Circle,     "CIRCLE",      ToolGroup::Draw,     true},
        {ToolId::Arc,         "Arc",          Icon::Arc,        "ARC",         ToolGroup::Draw,     true},
        {ToolId::Ellipse,     "Ellipse",      Icon::Ellipse,    "ELLIPSE",     ToolGroup::Draw,     true},
        {ToolId::Spline,      "Spline",       Icon::Spline,     "SPLINE",      ToolGroup::Draw,     true},
        {ToolId::Polygon,     "Polygon",      Icon::Rect,       "POLYGON",     ToolGroup::Draw,     true},
        {ToolId::Point,       "Point",        Icon::Point,      "POINT",       ToolGroup::Draw,     true},
        {ToolId::Hatch,       "Hatch",        Icon::Hatch,      "HATCH",       ToolGroup::Draw,     true},
        {ToolId::Move,        "Move",         Icon::Move,       "MOVE",        ToolGroup::Modify,   true},
        {ToolId::Copy,        "Copy",         Icon::Copy,       "COPY",        ToolGroup::Modify,   true},
        {ToolId::Rotate,      "Rotate",       Icon::Rotate,     "ROTATE",      ToolGroup::Modify,   true},
        {ToolId::Mirror,      "Mirror",       Icon::Mirror,     "MIRROR",      ToolGroup::Modify,   true},
        {ToolId::Offset,      "Offset",       Icon::Offset,     "OFFSET",      ToolGroup::Modify,   true},
        {ToolId::Trim,        "Trim",         Icon::Trim,       "TRIM",        ToolGroup::Modify,   true},
        {ToolId::Extend,      "Extend",       Icon::Extend,     "EXTEND",      ToolGroup::Modify,   true},
        {ToolId::Fillet,      "Fillet",       Icon::Fillet,     "FILLET",      ToolGroup::Modify,   true},
        {ToolId::Chamfer,     "Chamfer",      Icon::Chamfer,    "CHAMFER",     ToolGroup::Modify,   true},
        {ToolId::Scale,       "Scale",        Icon::Scale,      "SCALE",       ToolGroup::Modify,   true},
        {ToolId::Stretch,     "Stretch",      Icon::Move,       "STRETCH",     ToolGroup::Modify,   true},
        {ToolId::Break,       "Break",        Icon::Trim,       "BREAK",       ToolGroup::Modify,   true},
        {ToolId::ArrayRect,   "Array",        Icon::Array,      "ARRAY",       ToolGroup::Modify,   true},
        {ToolId::ArrayPolar,  "Polar array",  Icon::Array,      "ARRAYPOLAR",  ToolGroup::Modify,   true},
        {ToolId::GroupSel,    "Group",        Icon::Array,      "GROUPSEL",    ToolGroup::Modify,   true},
        {ToolId::Ungroup,     "Ungroup",      Icon::Explode,    "EXPLODE",     ToolGroup::Modify,   true},
        {ToolId::DimLinear,   "Linear",       Icon::Dimension,  "DIMLINEAR",   ToolGroup::Annotate, true},
        {ToolId::DimAligned,  "Aligned",      Icon::Ortho,      "DIMALIGNED",  ToolGroup::Annotate, true},
        {ToolId::MeasureDistance, "Measure",  Icon::Measure,    "DIST",        ToolGroup::Annotate, true},
        {ToolId::DimRadius,   "Radius",       Icon::Circle,     "DIMRADIUS",   ToolGroup::Annotate, true},
        {ToolId::DimDiameter, "Diameter",     Icon::Point,      "DIMDIAMETER", ToolGroup::Annotate, true},
        {ToolId::DimAngular,  "Angular",      Icon::Arc,        "DIMANGULAR",  ToolGroup::Annotate, true},
        {ToolId::Hatch,       "Hatch",        Icon::Hatch,      "BHATCH",      ToolGroup::Annotate, true},
        {ToolId::Text,        "Text",         Icon::Text,       "TEXT",        ToolGroup::Annotate, true},
        {ToolId::FinishSketch,"Finish",       Icon::Finish,     "FINISH",      ToolGroup::Finish,   true},

        {ToolId::Select,      "Select",       Icon::Point,      "SELECT",      ToolGroup::DrawingSelect, true},
        {ToolId::PlaceLayout, "Place",        Icon::Move,       "PLACELAYOUT", ToolGroup::Layout,   true},
        {ToolId::Line,        "Line",         Icon::Line,       "LINE",        ToolGroup::Markup,   true},
        {ToolId::Rect,        "Rectangle",    Icon::Rect,       "RECTANG",     ToolGroup::Markup,   true},
        {ToolId::RevCloud,    "Cloud",        Icon::Spline,     "REVCLOUD",    ToolGroup::Markup,   true},
        {ToolId::Text,        "Note",         Icon::Text,       "TEXT",        ToolGroup::Markup,   true},
        {ToolId::DimLinear,   "Linear",       Icon::Dimension,  "DIMLINEAR",   ToolGroup::Markup,   true},
        {ToolId::DimAligned,  "Aligned",      Icon::Ortho,      "DIMALIGNED",  ToolGroup::Markup,   true},
        {ToolId::MeasureDistance, "Measure",  Icon::Measure,    "DIST",        ToolGroup::Markup,   true},

        {ToolId::Select,      "Select",       Icon::Point,      "SELECT",      ToolGroup::SolidSelect, true},
        {ToolId::NewSketch,   "New sketch",   Icon::NewSketch,  "SKETCH",      ToolGroup::Sketch,   true},
        {ToolId::Box,         "Box",          Icon::Box,        "BOX",         ToolGroup::Create,   true},
        {ToolId::Cylinder,    "Cylinder",     Icon::Circle,     "CYLINDER",    ToolGroup::Create,   true},
        {ToolId::Sphere,      "Sphere",       Icon::Circle,     "SPHERE",      ToolGroup::Create,   true},
        {ToolId::Extrude,     "Extrude",      Icon::Extrude,    "EXTRUDE",     ToolGroup::Create,   true},
        {ToolId::PressPull,   "Push/Pull",    Icon::Extrude,    "PRESSPULL",   ToolGroup::Create,   true},
        {ToolId::Revolve,     "Revolve",      Icon::Revolve,    "REVOLVE",     ToolGroup::Create,   true},
        {ToolId::Sweep,       "Sweep",        Icon::Sweep,      "SWEEP",       ToolGroup::Create,   true},
        {ToolId::Loft,        "Loft",         Icon::Loft,       "LOFT",        ToolGroup::Create,   true},
        {ToolId::FilletEdge,  "Fillet",       Icon::Fillet,     "FILLETEDGE",  ToolGroup::Refine,   true},
        {ToolId::ChamferEdge, "Chamfer",      Icon::Chamfer,    "CHAMFEREDGE", ToolGroup::Refine,   true},
        {ToolId::ShellBody,   "Shell",        Icon::Shell,      "SHELL",       ToolGroup::Refine,   true},
        {ToolId::OffsetFace,  "Offset face",  Icon::Offset,     "OFFSETFACE",  ToolGroup::Refine,   true},
        {ToolId::Hole,        "Hole",         Icon::Circle,     "HOLE",        ToolGroup::Refine,   true},
        {ToolId::Draft,       "Draft",        Icon::Chamfer,    "DRAFT",       ToolGroup::Refine,   true},
        {ToolId::Split,       "Split",        Icon::Split,      "SPLIT",       ToolGroup::Refine,   true},
        {ToolId::Slice,       "Slice",        Icon::Section,    "SLICE",       ToolGroup::Refine,   true},
        {ToolId::DeleteBody,  "Delete",       Icon::DeleteBody, "DELETEBODY",  ToolGroup::Refine,   true},
        {ToolId::Union,       "Union",        Icon::Union,      "UNION",       ToolGroup::Combine,  true},
        {ToolId::Subtract,    "Subtract",     Icon::Subtract,   "SUBTRACT",    ToolGroup::Combine,  true},
        {ToolId::Intersect,   "Intersect",    Icon::Intersect,  "INTERSECT",   ToolGroup::Combine,  true},
        {ToolId::MeasureVolume,"Volume",      Icon::Measure,    "MASSPROP",    ToolGroup::Measure,  true},
        {ToolId::MeasureDistance,"Distance",  Icon::Dimension,  "DIST",        ToolGroup::Measure,  true},
        {ToolId::Section,     "Section",      Icon::Section,    "SECTION",     ToolGroup::Measure,  true},
        {ToolId::Mate,        "Mate",         Icon::Snap,       "MATE",        ToolGroup::Transform, true},
        {ToolId::PatternBody, "Pattern",      Icon::Pattern,    "PATTERN3D",   ToolGroup::Transform, true},
        {ToolId::MirrorBody,  "Mirror",       Icon::Mirror,     "MIRROR3D",    ToolGroup::Transform, true},
    };
    return t;
}

const ToolDef* tool_def(ToolId id) noexcept {
    for (const auto& d : tool_defs()) if (d.id == id) return &d;
    return nullptr;
}

Mode tool_mode(ToolId id) noexcept {
    const ToolDef* d = tool_def(id);
    if (!d) return Mode::Sketch;
    for (const auto& g : tool_groups()) if (g.group == d->group) return g.mode;
    return Mode::Sketch;
}

// Don't switch modes when the current mode also lists this tool.
bool tool_in_mode(ToolId id, Mode m) noexcept {
    for (const auto& d : tool_defs())
        if (d.id == id)
            for (const auto& g : tool_groups())
                if (g.group == d.group && g.mode == m) return true;
    return false;
}

const char* mode_name(Mode m) noexcept { return m == Mode::Sketch ? "Sketch" : "Solid"; }

}
