#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

namespace gcapp {

struct SelectionSummary {
    int  texts{0}, dimensions{0};
    bool sketch_mode{true}, planar_face{false}, can_revolve{false}, editable_sketch{false};
    int  hatches{0};
    int  objects{0};
    int  solid_items{0};
    int  shapes{0};
    int  block_refs{0};
    int  faces{0};
    int  edges{0};
    int  bodies{0};
    bool two_bodies{false};
    bool region{false};
    bool door{false};
};

enum class SelectionAction {
    Command, TextSmaller, TextBigger, Extrude, Revolve, SketchOnFace,
    EditSketch, Volume, MirrorBody, HatchRegion
};

struct ActionDef {
    const char* label;
    const char* command;
    bool (*applies)(const SelectionSummary&);
    SelectionAction handler{SelectionAction::Command};
};

inline const std::vector<ActionDef>& selection_actions() {
    static const std::vector<ActionDef> table = {
        {"Move",      "MOVE",      +[](const SelectionSummary& s) { return (s.objects>0 || s.shapes>0 || s.block_refs>0) && !s.hatches && !s.bodies; }},
        {"Copy",      "COPY",      +[](const SelectionSummary& s) { return (s.objects>0 || s.shapes>0 || s.block_refs>0) && !s.hatches && !s.bodies; }},
        {"Rotate",    "ROTATE",    +[](const SelectionSummary& s) { return (s.objects>0 || s.shapes>0 || s.block_refs>0) && !s.hatches && !s.bodies; }},
        {"Mirror",    "MIRROR",    +[](const SelectionSummary& s) { return (s.objects>0 || s.shapes>0 || s.block_refs>0) && !s.hatches && !s.bodies; }},
        {"Group",     "GROUPSEL",  +[](const SelectionSummary& s) { return s.shapes > 1; }},
        {"Save selection to library…", "LIBRARYADD", +[](const SelectionSummary& s) { return s.sketch_mode && s.objects>0 && !s.solid_items; }},
        {"Ungroup",   "EXPLODE",   +[](const SelectionSummary& s) { return s.block_refs > 0; }},
        {"Scale",     "SCALE",     +[](const SelectionSummary& s) { return (s.objects>0 || s.shapes>0 || s.block_refs>0) && !s.hatches && !s.bodies; }},
        {"Properties…", "PROPERTIES", +[](const SelectionSummary& s) { return s.objects>0 || s.shapes>0 || s.block_refs>0; }},
        {"Erase",     "ERASE",     +[](const SelectionSummary& s) { return s.objects > 0 || s.shapes > 0 || s.bodies > 0 || s.block_refs > 0; }},
        {"Round edges…", "FILLETEDGE", +[](const SelectionSummary& s) { return s.edges>0 && !s.faces && !s.shapes; }},
        {"Bevel edges…", "CHAMFEREDGE", +[](const SelectionSummary& s) { return s.edges>0 && !s.faces && !s.shapes; }},
        {"Extrude",   "EXTRUDE",   +[](const SelectionSummary& s) { return s.region || s.planar_face; }, SelectionAction::Extrude},
        {"Offset",    "OFFSET",    +[](const SelectionSummary& s) { return s.region || s.shapes>0; }},
        {"Hatch",     "HATCH",     +[](const SelectionSummary& s) { return s.region && s.sketch_mode; }, SelectionAction::HatchRegion},
        {"Area background…", "BACKGROUND", +[](const SelectionSummary& s) { return s.sketch_mode && (s.shapes>0 || s.region || (!s.objects && !s.solid_items && !s.bodies)); }},
        {"Smaller text", "TEXTSMALLER", +[](const SelectionSummary& s) { return s.texts>0; }, SelectionAction::TextSmaller},
        {"Larger text", "TEXTBIGGER", +[](const SelectionSummary& s) { return s.texts>0; }, SelectionAction::TextBigger},
        {"Revolve", "REVOLVE", +[](const SelectionSummary& s) { return s.can_revolve; }, SelectionAction::Revolve},
        {"Sketch on face", "SKETCHONFACE", +[](const SelectionSummary& s) { return s.planar_face; }, SelectionAction::SketchOnFace},
        {"Edit sketch", "EDITSKETCH", +[](const SelectionSummary& s) { return s.editable_sketch; }, SelectionAction::EditSketch},
        {"Volume", "MASSPROP", +[](const SelectionSummary& s) { return s.bodies==1 && !s.edges; }, SelectionAction::Volume},
        {"Mirror solid", "MIRROR3D", +[](const SelectionSummary& s) { return s.bodies==1 && !s.faces && !s.edges; }, SelectionAction::MirrorBody},
        {"Edit doors…", "DOORFLOW", +[](const SelectionSummary& s) { return s.door; }},
        {"Union",     "UNION",     +[](const SelectionSummary& s) { return s.two_bodies; }},
        {"Subtract",  "SUBTRACT",  +[](const SelectionSummary& s) { return s.two_bodies; }},
        {"Intersect", "INTERSECT", +[](const SelectionSummary& s) { return s.two_bodies; }},
        {"Mate",      "MATE",      +[](const SelectionSummary& s) { return s.two_bodies; }},
        {"Measure distance", "DIST",
         +[](const SelectionSummary& s) { return s.shapes + s.faces + s.edges + s.bodies >= 2; }},
    };
    return table;
}

struct SelectionActionFactory {
    static std::vector<const ActionDef*> build(const SelectionSummary& selection) {
        std::vector<const ActionDef*> result;
        const bool mixed=selection.solid_items>0 && selection.objects>0;
        const bool bulk=selection.solid_items>1;
        const bool only_edges=selection.edges>0 && selection.solid_items==selection.edges && !mixed;
        for(const auto& action:selection_actions()) {
            const std::string_view command=action.command;
            if((mixed || bulk) && command!="ERASE" &&
               !(only_edges && (command=="FILLETEDGE" || command=="CHAMFEREDGE")))continue;
            if(action.applies(selection))result.push_back(&action);
        }
        std::stable_partition(result.begin(),result.end(),[](const auto* action) {
            return std::string_view(action->command)!="ERASE";
        });
        return result;
    }
};

}
