#include "engine/command.hpp"

namespace gcmd {

namespace {

Prompt pt(const char* t, std::vector<std::string> kw = {}) {
    return Prompt{t, ArgKind::Point, std::move(kw), false};
}
Prompt dist(const char* t, std::vector<std::string> kw = {}) {
    return Prompt{t, ArgKind::Distance, std::move(kw), false};
}
Prompt sel(const char* t) { return Prompt{t, ArgKind::Selection, {}, false}; }

}

void Registry::add_standard() {
    add({"LINE", {"L"}, "Draw straight line segments", "Draw", Modes::Both,
         {pt("Specify first point"),
          pt("Specify next point", {"Close", "Undo"})}});
    add({"PLINE", {"PL"}, "Draw a polyline", "Draw", Modes::Both,
         {pt("Specify start point"),
          pt("Specify next point", {"Arc", "Halfwidth", "Length", "Undo", "Width"})}});
    add({"CIRCLE", {"C"}, "Draw a circle", "Draw", Modes::Both,
         {pt("Specify centre point", {"3P", "2P", "Ttr"}),
          dist("Specify radius", {"Diameter"})}});
    add({"ARC", {"A"}, "Draw an arc", "Draw", Modes::Both,
         {pt("Specify start point", {"Centre"}),
          pt("Specify second point", {"Centre", "End"}),
          pt("Specify end point")}});
    add({"RECTANG", {"REC"}, "Draw a rectangle", "Draw", Modes::Both,
         {pt("Specify first corner", {"Chamfer", "Elevation", "Fillet", "Thickness", "Width"}),
          pt("Specify other corner", {"Area", "Dimensions", "Rotation"})}});
    add({"POLYGON", {"POL"}, "Draw a regular polygon", "Draw", Modes::Both,
         {Prompt{"Number of sides", ArgKind::Integer, {}, false},
          pt("Specify centre", {"Edge"}),
          dist("Specify radius", {"Inscribed", "Circumscribed"})}});
    add({"ELLIPSE", {"EL"}, "Draw an ellipse", "Draw", Modes::Both,
         {pt("Specify axis endpoint", {"Arc", "Centre"}), pt("Specify other endpoint"),
          dist("Specify half length of other axis", {"Rotation"})}});
    add({"SPLINE", {"SPL"}, "Draw a spline", "Draw", Modes::Both,
         {pt("Specify first point", {"Method", "Knots", "Object"})}});
    add({"POINT", {"PO"}, "Place a point", "Draw", Modes::Both, {pt("Specify a point")}});
    add({"XLINE", {"XL"}, "Draw an infinite construction line", "Draw", Modes::Both,
         {pt("Specify a point", {"Hor", "Ver", "Ang", "Bisect", "Offset"})}});
    add({"RAY", {}, "Draw a ray", "Draw", Modes::Both, {pt("Specify start point")}});
    add({"DONUT", {"DO"}, "Draw a filled ring", "Draw", Modes::TwoD,
         {dist("Inside diameter"), dist("Outside diameter"), pt("Centre")}});
    add({"HATCH", {"H", "BH"}, "Fill an enclosed area with a pattern", "Draw",
         Modes::TwoD,
         {pt("Pick internal point", {"Select", "Undo", "seTtings"})}});
    add({"GRADIENT", {"GD"}, "Fill an area with a gradient", "Draw", Modes::TwoD,
         {pt("Pick internal point", {"Select", "seTtings"})}});
    add({"BOUNDARY", {"BO"}, "Create a region or polyline from an enclosed area",
         "Draw", Modes::TwoD, {pt("Pick internal point")}});
    add({"SKETCHLIBRARY", {}, "Open the 2D sketch object library", "Draw", Modes::TwoD, {}});
    add({"SYMBOLS", {}, "Open the 2D symbol library", "Draw", Modes::TwoD, {}});
    add({"SYMBOL", {}, "Place a symbol: SYMBOL north|section|elevation|datum|direction|both_directions [size]", "Draw", Modes::TwoD, {}});
    add({"ARROW", {}, "Place a standalone arrow: pick tail then tip; ARROW BOTH for two directions", "Draw", Modes::TwoD, {}});
    add({"SYMBOLLEGEND", {}, "Generate or update a movable symbol legend from used blocks", "Draw", Modes::TwoD, {}});
    add({"DISCIPLINE", {}, "Set drawing purpose: ARCHITECTURAL, MECHANICAL or GENERAL (independent of 2D/3D)", "Draw", Modes::Both, {}});
    add({"WALL", {}, "Place a joined wall between two snapped points", "Draw", Modes::TwoD, {}});
    add({"DOOR", {}, "Place a swing door between hinge and latch", "Draw", Modes::TwoD, {}});
    add({"WINDOW", {}, "Place a window between two jambs", "Draw", Modes::TwoD, {}});
    add({"DOORFRAME", {}, "Place a door frame between two jambs", "Draw", Modes::TwoD, {}});
    add({"HINGE", {}, "Place a door hinge", "Draw", Modes::TwoD, {}});
    add({"BACKGROUND", {}, "Color an area: BACKGROUND x,y #RRGGBB [gap] [opacity percent]", "Draw", Modes::TwoD, {}});
    add({"BACKGROUNDOFF", {}, "Remove an area background: BACKGROUNDOFF x,y", "Draw", Modes::TwoD, {}});
    add({"DOORSWING", {}, "Reverse selected door swing sides: DOORSWING reverse", "Modify", Modes::TwoD, {}});
    add({"DOORHINGE", {}, "Swap selected door hinges and latches: DOORHINGE reverse", "Modify", Modes::TwoD, {}});
    add({"DOORFLOW", {}, "Edit selected doors: DOORFLOW forward|reverse|both|off", "Modify", Modes::TwoD, {}});
    add({"REGION", {"REG"}, "Convert closed loops to regions", "Draw", Modes::Both,
         {sel("Select objects")}});
    add({"REVCLOUD", {}, "Draw a revision cloud", "Draw", Modes::TwoD,
         {pt("Specify start point", {"Arc length", "Object", "Style"})}});
    add({"WIPEOUT", {}, "Mask an area", "Draw", Modes::TwoD,
         {pt("Specify first point", {"Frames", "Polyline"})}});

    add({"ERASE", {"E"}, "Delete objects", "Modify", Modes::Both, {sel("Select objects")}});
    add({"COPY", {"CO", "CP"}, "Copy objects", "Modify", Modes::Both,
         {sel("Select objects"), pt("Specify base point", {"Displacement", "mOde"}),
          pt("Specify second point", {"Array", "Exit", "Undo"})}});
    add({"MOVE", {"M"}, "Move objects", "Modify", Modes::Both,
         {sel("Select objects"), pt("Specify base point"), pt("Specify second point")}});
    add({"ROTATE", {"RO"}, "Rotate objects", "Modify", Modes::Both,
         {sel("Select objects"), pt("Specify base point"),
          Prompt{"Specify rotation angle", ArgKind::Angle, {"Copy", "Reference"}, false}}});
    add({"SCALE", {"SC"}, "Scale objects", "Modify", Modes::Both,
         {sel("Select objects"), pt("Specify base point"),
          dist("Specify scale factor", {"Copy", "Reference"})}});
    add({"MIRROR", {"MI"}, "Mirror objects", "Modify", Modes::Both,
         {sel("Select objects"), pt("First point of mirror line"),
          pt("Second point of mirror line")}});
    add({"OFFSET", {"O"}, "Offset a curve at a distance", "Modify", Modes::Both,
         {dist("Specify offset distance", {"Through", "Erase", "Layer"}),
          sel("Select object to offset"), pt("Specify side to offset")}});
    add({"ARRAY", {"AR"}, "Array objects", "Modify", Modes::Both,
         {sel("Select objects"),
          Prompt{"Enter array type", ArgKind::Keyword,
                 {"Rectangular", "PAth", "POlar"}, false}}});
    add({"TRIM", {"TR"}, "Trim objects to an edge", "Modify", Modes::Both,
         {sel("Select object to trim")}});
    add({"EXTEND", {"EX"}, "Extend objects to a boundary", "Modify", Modes::Both,
         {sel("Select object to extend")}});
    add({"MATERIAL", {"MAT"}, "What a solid is made of", "3D", Modes::ThreeD, {}});
    add({"MATELIST", {}, "What the assembly is mated by", "3D", Modes::ThreeD, {}});
    add({"MATEUPDATE", {}, "Re-solve the assembly against its mates", "3D",
         Modes::ThreeD, {}});
    add({"MATEDELETE", {}, "Remove a mate", "3D", Modes::ThreeD, {}});
    add({"OFFSETFACE", {}, "Push or pull a face", "3D", Modes::ThreeD,
         {sel("Select a face")}});
    add({"HOLE", {}, "Drill a hole in the picked face", "3D", Modes::ThreeD,
         {sel("Pick a face"), dist("Diameter")}});
    add({"HEAL", {}, "Repair an imported solid enough to be used", "3D",
         Modes::ThreeD, {}});
    add({"PROJECT", {"PROJECTGEOM"}, "Drop a solid's edges onto the sketch", "3D",
         Modes::Both, {}});
    add({"DATUM", {}, "A plane to sketch on that is not a face", "3D", Modes::Both, {}});
    add({"SECTIONPLANE", {"SECPLANE"}, "Cut the view open without cutting the model",
         "3D", Modes::ThreeD, {}});
    add({"SECTIONOFF", {}, "Put the section away", "3D", Modes::ThreeD, {}});
    add({"ROLLBACK", {}, "Build the solid only as far as a feature", "3D",
         Modes::ThreeD, {}});
    add({"SUPPRESS", {}, "Leave a feature out of the rebuild", "3D", Modes::ThreeD, {}});
    add({"UNSUPPRESS", {}, "Put a suppressed feature back", "3D", Modes::ThreeD, {}});
    add({"FEATUREMOVE", {}, "Reorder the feature history", "3D", Modes::ThreeD, {}});
    add({"PMIADD", {}, "Attach a dimension to the model", "3D", Modes::ThreeD,
         {sel("Pick a face")}});
    add({"PMILIST", {}, "List the model's dimensions", "3D", Modes::ThreeD, {}});
    add({"PMICHECK", {}, "Inspect the model against its dimensions", "3D",
         Modes::ThreeD, {}});
    add({"PMIDRIVE", {}, "Drive a face from a dimension", "3D", Modes::ThreeD, {}});
    add({"GRIPEDIT", {}, "What a grip can do besides being dragged", "Modify",
         Modes::TwoD, {sel("Select objects")}});
    add({"SCALELISTEDIT", {}, "List, add, or delete named annotation scales", "Format", Modes::TwoD, {}});
    add({"FENCE", {}, "Select everything a line crosses", "Modify", Modes::TwoD, {}});
    add({"WPOLYGON", {"WP"}, "Select objects wholly inside a polygon", "Modify", Modes::TwoD, {}});
    add({"CPOLYGON", {}, "Select objects inside or crossing a polygon", "Modify", Modes::TwoD, {}});
    add({"SELECTPREVIOUS", {"PREVIOUS"}, "Recall the previous selection", "Modify", Modes::TwoD, {}});
    add({"SELECTLAST", {"LAST"}, "Select the last visible object", "Modify", Modes::TwoD, {}});
    add({"SELECTCYCLE", {}, "Cycle overlapping objects at a point (Shift+Space)", "Modify", Modes::TwoD, {}});
    add({"LASSO", {}, "Select everything inside an outline", "Modify", Modes::TwoD, {}});
    add({"ARRAYRECT", {}, "Repeat the selection in rows and columns", "Modify",
         Modes::TwoD, {sel("Select objects")}});
    add({"ARRAYPOLAR", {"ARPOL"}, "Repeat the selection around a centre", "Modify",
         Modes::TwoD, {sel("Select objects")}});
    add({"ARRAYPATH", {}, "Repeat the selection along a path", "Modify", Modes::TwoD,
         {sel("Select objects")}});
    add({"FILLETALL", {}, "Round every corner of a polyline", "Modify", Modes::TwoD,
         {sel("Select a polyline")}});
    add({"CHAMFERALL", {}, "Chamfer every corner of a polyline", "Modify", Modes::TwoD,
         {sel("Select a polyline")}});
    add({"FILLET", {"F"}, "Round a corner, or blend an edge in 3D", "Modify",
         Modes::Both,
         {sel("Select first object"),
          sel("Select second object")}});
    add({"CHAMFER", {"CHA"}, "Bevel a corner or edge", "Modify", Modes::Both,
         {sel("Select first line"), sel("Select second line")}});
    add({"STRETCH", {"S"}, "Stretch objects by a crossing window", "Modify",
         Modes::TwoD,
         {sel("Select objects with a crossing window"), pt("Base point"),
          pt("Second point")}});
    add({"LENGTHEN", {"LEN"}, "Change the length of an object", "Modify", Modes::TwoD,
         {sel("Select an object")}});
    add({"BREAK", {"BR"}, "Break an object", "Modify", Modes::TwoD,
         {sel("Select object"), pt("Specify second break point", {"First point"})}});
    add({"JOIN", {"J"}, "Join collinear or contiguous objects", "Modify", Modes::Both,
         {sel("Select source object")}});
    add({"EXPLODE", {"X"}, "Break a compound object into parts", "Modify", Modes::Both,
         {sel("Select objects")}});
    add({"PEDIT", {"PE"}, "Edit a polyline", "Modify", Modes::TwoD, {sel("Select polyline")}});
    add({"ALIGN", {"AL"}, "Align objects to others", "Modify", Modes::Both,
         {sel("Select objects")}});
    add({"OVERKILL", {}, "Remove duplicate and overlapping geometry", "Modify",
         Modes::Both, {sel("Select objects")}});
    add({"BLEND", {}, "Blend between two curves", "Modify", Modes::Both,
         {sel("Select first object"), sel("Select second object")}});
    add({"REVERSE", {}, "Reverse object direction", "Modify", Modes::Both,
         {sel("Select objects")}});

    add({"EXTRUDE", {"EXT"}, "Extrude a profile into a solid", "3D", Modes::ThreeD,
         {sel("Select objects to extrude"),
          dist("Specify height of extrusion", {"Direction", "Path", "Taper angle"})}});
    add({"REVOLVE", {"REV"}, "Revolve a profile about an axis", "3D", Modes::ThreeD,
         {sel("Select objects to revolve"), pt("Specify axis start point"),
          pt("Specify axis endpoint"),
          Prompt{"Specify angle of revolution", ArgKind::Angle, {"STart angle"}, false}}});
    add({"SWEEP", {"SW"}, "Sweep a profile along a path", "3D", Modes::ThreeD,
         {sel("Select objects to sweep"), sel("Select sweep path")}});
    add({"LOFT", {"LOF"}, "Loft between cross sections", "3D", Modes::ThreeD,
         {sel("Select cross sections in order")}});
    add({"PRESSPULL", {"PP"}, "Push or pull a bounded area or face", "3D", Modes::ThreeD,
         {sel("Select object or bounded area"), dist("Specify extrusion height")}});
    add({"SHELL", {"SH"}, "Hollow a solid to a wall thickness", "3D", Modes::ThreeD,
         {sel("Select a solid"), sel("Remove faces"), dist("Enter shell offset distance")}});
    add({"UNION", {"UNI"}, "Combine solids or regions", "Modify", Modes::Both, {sel("Select solids or regions")}});
    add({"SUBTRACT", {"SU"}, "Subtract solids or regions", "Modify", Modes::Both,
         {sel("Select solids to subtract from"), sel("Select solids to subtract")}});
    add({"INTERSECT", {"IN"}, "Keep the common area or volume", "Modify", Modes::Both,
         {sel("Select solids")}});
    add({"SLICE", {"SL"}, "Cut a solid with a plane", "3D", Modes::ThreeD,
         {sel("Select objects to slice"), pt("Specify start point of slicing plane")}});
    add({"BOX", {}, "Create a box solid", "3D", Modes::ThreeD,
         {pt("Specify first corner", {"Centre"}), pt("Specify other corner",
          {"Cube", "Length"}), dist("Specify height")}});
    add({"CYLINDER", {"CYL"}, "Create a cylinder", "3D", Modes::ThreeD,
         {pt("Specify centre of base"), dist("Specify base radius"), dist("Specify height")}});
    add({"SPHERE", {}, "Create a sphere", "3D", Modes::ThreeD,
         {pt("Specify centre point"), dist("Specify radius")}});
    add({"CONE", {}, "Create a cone", "3D", Modes::ThreeD,
         {pt("Specify centre of base"), dist("Specify base radius"), dist("Specify height")}});
    add({"WEDGE", {"WE"}, "Create a wedge", "3D", Modes::ThreeD,
         {pt("Specify first corner"), pt("Specify other corner"), dist("Specify height")}});
    add({"TORUS", {"TOR"}, "Create a torus", "3D", Modes::ThreeD,
         {pt("Specify centre point"), dist("Specify radius"), dist("Specify tube radius")}});
    add({"DRAFT", {}, "Apply draft to faces", "3D", Modes::ThreeD,
         {sel("Select faces"), Prompt{"Draft angle", ArgKind::Angle, {}, false}}});
    add({"FACEOFFSET", {}, "Move a face along its own normal by a typed distance",
         "3D", Modes::ThreeD, {sel("Select a face"), dist("Distance")}});
    add({"MOVEFACE", {}, "Move a face by a vector", "3D", Modes::ThreeD,
         {sel("Select a face"), dist("dx dy dz")}});
    add({"ALIGNFACE", {}, "Make a face parallel to another", "3D", Modes::ThreeD,
         {sel("Select a face")}});
    add({"THICKEN", {}, "Thicken a face into material", "3D", Modes::ThreeD,
         {sel("Select a face"), dist("Thickness")}});
    add({"THICKEN", {}, "Thicken a surface into a solid", "3D", Modes::ThreeD,
         {sel("Select surfaces"), dist("Specify thickness")}});
    add({"SECTION", {"SEC"}, "Section a solid", "3D", Modes::ThreeD,
         {sel("Select objects"), pt("Specify first point on section plane")}});
    add({"INTERFERE", {"INF"}, "Check for interference between solids", "3D",
         Modes::ThreeD, {sel("Select first set of objects")}});
    add({"MASSPROP", {}, "Report mass properties", "3D", Modes::ThreeD,
         {sel("Select objects")}});

    add({"LAYER", {"LA"}, "Manage layers", "Organise", Modes::Both, {}});
    add({"LAYISO", {}, "Isolate the selected object's layer", "Organise", Modes::Both,
         {sel("Select objects")}});
    add({"LAYON", {}, "Turn all layers on", "Organise", Modes::Both, {}});
    add({"LAYFRZ", {}, "Freeze a layer", "Organise", Modes::Both, {sel("Select objects")}});
    add({"LINETYPE", {"LT"}, "Manage linetypes", "Organise", Modes::Both, {}});
    add({"LTSCALE", {"LTS"}, "Set the global linetype scale", "Organise", Modes::Both,
         {dist("Enter new linetype scale factor")}});
    add({"PROPERTIES", {"PR", "CH", "MO"}, "Open the properties panel", "Organise",
         Modes::Both, {}});
    add({"TEXTEDIT", {"DDEDIT", "ED"}, "Edit a note's text", "Annotate", Modes::Both,
         {sel("Select a note")}});
    add({"FIND", {}, "Find text in the drawing", "Annotate", Modes::Both, {}});
    add({"MATCHPROP", {"MA"}, "Copy properties between objects", "Organise", Modes::Both,
         {sel("Select source object"), sel("Select destination objects")}});
    add({"CHPROP", {}, "Change a property of the selection", "Organise", Modes::Both,
         {sel("Select objects")}});
    add({"COLOR", {"COLOUR", "COL"}, "Set the selection's colour", "Organise", Modes::Both,
         {sel("Select objects")}});
    add({"LWEIGHT", {"LW"}, "Set the selection's lineweight", "Organise", Modes::Both,
         {sel("Select objects")}});
    add({"TRANSPARENCY", {}, "Set the selection's transparency", "Organise", Modes::Both,
         {sel("Select objects")}});
    add({"BLOCK", {"B"}, "Create a block definition", "Organise", Modes::Both, {}});
    add({"INSERT", {"I"}, "Insert a block", "Organise", Modes::Both, {pt("Specify insertion point")}});
    add({"WBLOCK", {"W"}, "Write a block to a file", "Organise", Modes::Both, {}});
    add({"BEDIT", {"BE"}, "Make a block dynamic and give it parameters", "Organise",
         Modes::TwoD, {}});
    add({"BPARAM", {}, "Set a dynamic block's parameter on an insertion", "Organise",
         Modes::TwoD, {sel("Select an insertion")}});
    add({"ATTDEF", {"ATT"}, "Define a block attribute", "Organise", Modes::TwoD, {}});
    add({"ATTEDIT", {"ATE"}, "Edit an insertion's attributes", "Organise", Modes::TwoD, {}});
    add({"BATTMAN", {}, "List the blocks' attributes", "Organise", Modes::TwoD, {}});
    add({"ATTEXT", {}, "Extract the attributes as a schedule", "Organise", Modes::TwoD, {}});
    add({"XREF", {"XR"}, "Manage external references", "Organise", Modes::Both, {}});
    add({"XCLIP", {}, "Clip an external reference", "Organise", Modes::Both, {}});
    add({"XATTACH", {"XA"}, "Attach an external reference", "Organise", Modes::Both, {}});
    add({"GROUP", {"G"}, "Group objects", "Organise", Modes::Both, {sel("Select objects")}});
    add({"GROUPSEL", {}, "Group the current selection as a block", "Organise", Modes::Both, {}});
    add({"UNGROUP", {}, "Break a group back into parts", "Organise", Modes::Both, {}});
    add({"PURGE", {"PU"}, "Remove unused named objects", "Organise", Modes::Both, {}});
    add({"RECOVER", {}, "Open the drawing recovered from an unclean exit", "File", Modes::Both, {}});
    add({"VIEWBASE", {"BASEVIEW"}, "Project the solid into drawing views", "View", Modes::Both, {}});
    add({"DRAFT", {}, "Tilt a face by a draft angle", "3D", Modes::ThreeD, {}});
    add({"AUDIT", {}, "Check the drawing for structural problems", "File", Modes::Both, {}});
    add({"SETVAR", {}, "Read or write a system variable", "Tools", Modes::Both, {}});
    add({"RECOGNISE", {"RECOGNIZE"}, "Find the features hiding in an imported solid", "3D", Modes::ThreeD, {}});

    add({"TEXT", {"DT"}, "Place single-line text", "Annotate", Modes::Both,
         {pt("Specify start point", {"Justify", "Style"}), dist("Specify height"),
          Prompt{"Specify rotation angle", ArgKind::Angle, {}, false},
          Prompt{"Enter text", ArgKind::Text, {}, false}}});
    add({"MTEXT", {"T", "MT"}, "Place multiline text", "Annotate", Modes::Both,
         {pt("Specify first corner"), pt("Specify opposite corner")}});
    add({"STYLE", {"ST"}, "Manage text styles", "Annotate", Modes::Both, {}});
    add({"DIMSTYLE", {"D"}, "Manage dimension styles", "Annotate", Modes::Both, {}});
    add({"DIMLINEAR", {"DLI"}, "Linear dimension", "Annotate", Modes::Both,
         {pt("Specify first extension line origin"), pt("Specify second origin"),
          pt("Specify dimension line location")}});
    add({"DIMALIGNED", {"DAL"}, "Aligned dimension", "Annotate", Modes::Both,
         {pt("Specify first extension line origin"), pt("Specify second origin")}});
    add({"DIMANGULAR", {"DAN"}, "Angular dimension", "Annotate", Modes::Both,
         {sel("Select arc, circle or line")}});
    add({"DIMRADIUS", {"DRA"}, "Radial dimension", "Annotate", Modes::Both,
         {sel("Select arc or circle")}});
    add({"DIMDIAMETER", {"DDI"}, "Diameter dimension", "Annotate", Modes::Both,
         {sel("Select arc or circle")}});
    // Promptless: the shell runs its own pick flow for these.
    add({"DIMARC", {}, "Arc length dimension", "Annotate", Modes::Both, {}});
    add({"DIMCENTER", {"DCE"}, "Centre mark", "Annotate", Modes::Both, {}});
    add({"DIMJOGGED", {"DJO"}, "Jogged radial dimension", "Annotate", Modes::Both, {}});
    add({"DIMCONTINUE", {"DCO"}, "Continue a dimension", "Annotate", Modes::Both, {}});
    add({"DIMBASELINE", {"DBA"}, "Baseline dimension", "Annotate", Modes::Both, {}});
    add({"DIMORDINATE", {"DOR"}, "Ordinate dimension", "Annotate", Modes::Both,
         {pt("Specify feature location")}});
    add({"MLEADER", {"MLD"}, "Place a multileader", "Annotate", Modes::Both,
         {pt("Specify leader arrowhead location")}});
    add({"LEADER", {"LE"}, "Place a leader", "Annotate", Modes::Both,
         {pt("Specify leader start point")}});
    add({"TABLE", {"TB"}, "Insert a table", "Annotate", Modes::TwoD, {}});
    add({"TOLERANCE", {"TOL"}, "Place a GD&T feature control frame", "Annotate",
         Modes::Both, {}});
    add({"FIELD", {}, "Insert a field", "Annotate", Modes::Both, {}});

    add({"DIST", {"DI"}, "Measure a distance", "Inquire", Modes::Both,
         {pt("Specify first point"), pt("Specify second point")}});
    add({"AREA", {"AA"}, "Measure an area", "Inquire", Modes::Both,
         {pt("Specify first corner point", {"Object", "Add", "Subtract"})}});
    add({"ID", {}, "Report a coordinate", "Inquire", Modes::Both, {pt("Specify point")}});
    add({"LIST", {"LI"}, "List object data", "Inquire", Modes::Both, {sel("Select objects")}});
    add({"MEASUREGEOM", {"MEA"}, "Measure geometry", "Inquire", Modes::Both, {}});

    add({"ZOOM", {"Z"}, "Zoom the view", "View", Modes::Both,
         {Prompt{"Specify corner, or enter an option", ArgKind::Keyword,
                 {"All", "Centre", "Dynamic", "Extents", "Previous", "Scale",
                  "Window", "Object"}, true}},
         true, true});
    add({"PAN", {"P"}, "Pan the view", "View", Modes::Both, {}, true, true});
    add({"REGEN", {"RE"}, "Regenerate the drawing", "View", Modes::Both, {}});
    add({"REDRAW", {"R"}, "Redraw the viewport", "View", Modes::Both, {}});
    add({"VIEW", {"V"}, "Manage named views", "View", Modes::Both, {}});
    add({"ORBIT", {"3DO"}, "Orbit the 3D view", "View", Modes::ThreeD, {}, true, true});
    add({"VSCURRENT", {"VS"}, "Set the visual style", "View", Modes::ThreeD,
         {Prompt{"Enter an option", ArgKind::Keyword,
                 {"2Dwireframe", "Wireframe", "Hidden", "Realistic", "Conceptual",
                  "Shaded", "Shaded with edges", "X-ray"}, false}}});
    add({"UCS", {}, "Manage the user coordinate system", "View", Modes::Both, {}});
    add({"PLAN", {}, "View the XY plane of a UCS", "View", Modes::Both, {}});

    add({"OSNAP", {"OS"}, "Set running object snaps", "Settings", Modes::Both, {}});
    add({"END", {"ENDP"}, "Snap the next point to an endpoint", "Settings", Modes::Both, {}});
    add({"MID", {}, "Snap the next point to a midpoint", "Settings", Modes::Both, {}});
    add({"CEN", {"CENTRE"}, "Snap the next point to a centre", "Settings", Modes::Both, {}});
    add({"NOD", {"NODE"}, "Snap the next point to a node", "Settings", Modes::Both, {}});
    add({"QUA", {}, "Snap the next point to a quadrant", "Settings", Modes::Both, {}});
    add({"INT", {}, "Snap the next point to an intersection", "Settings", Modes::Both, {}});
    add({"INS", {}, "Snap the next point to an insertion point", "Settings", Modes::Both, {}});
    add({"PER", {}, "Snap the next point perpendicular", "Settings", Modes::Both, {}});
    add({"TAN", {}, "Snap the next point tangent", "Settings", Modes::Both, {}});
    add({"NEA", {}, "Snap the next point to the nearest point on an object", "Settings", Modes::Both, {}});
    add({"APP", {}, "Snap the next point to an apparent intersection", "Settings", Modes::Both, {}});
    add({"EXT", {}, "Snap the next point along an extension", "Settings", Modes::Both, {}});
    add({"PAR", {}, "Snap the next point parallel", "Settings", Modes::Both, {}});
    add({"GEO", {}, "Snap the next point to a geometric centre", "Settings", Modes::Both, {}});
    add({"ORTHO", {}, "Toggle orthogonal mode", "Settings", Modes::Both, {}});
    add({"POLAR", {}, "Toggle polar tracking", "Settings", Modes::Both, {}});
    add({"GRID", {}, "Toggle the grid", "Settings", Modes::Both, {}});
    add({"SNAP", {"SN"}, "Toggle snap mode", "Settings", Modes::Both, {}});
    add({"DSETTINGS", {"DS"}, "Drafting settings", "Settings", Modes::Both, {}});
    add({"UNITS", {"UN"}, "Set drawing units", "Settings", Modes::Both, {}});
    add({"OPTIONS", {"OP"}, "Application options", "Settings", Modes::Both, {}});
    add({"THEME", {}, "Edit the colour theme", "Settings", Modes::Both, {}});

    add({"NEW", {}, "New drawing", "File", Modes::Both, {}});
    add({"OPEN", {}, "Open a document", "File", Modes::Both, {}});
    add({"SAVE", {}, "Save the document", "File", Modes::Both, {}});
    add({"SAVEAS", {}, "Save under a new name", "File", Modes::Both, {}});
    add({"IMPORT", {"IMP"}, "Import a file", "File", Modes::Both, {}});
    add({"EXPORT", {"EXP"}, "Export a file", "File", Modes::Both, {}});
    add({"PLOT", {}, "Export the drawing to PDF", "File", Modes::Both, {}});
    add({"PRINT", {}, "Print the plotted drawing", "File", Modes::Both, {}});
    add({"LAYOUT", {"LO"}, "Make, switch or remove a sheet", "File", Modes::Both, {}});
    add({"MVIEW", {"MV"}, "Add a viewport to the current sheet", "File", Modes::Both, {}});
    add({"VIEWSECTION", {}, "Cut a section from a drawing view", "File", Modes::Both, {}});
    add({"VIEWDETAIL", {}, "Blow up part of a drawing view", "File", Modes::Both, {}});
    add({"VIEWUPDATE", {}, "Regenerate the sheet's drawing views", "File", Modes::Both, {}});
    add({"PSPACE", {"PS", "PAPER"}, "Draw on the sheet itself", "File", Modes::Both, {}});
    add({"PLOTSTYLE", {}, "Plot this sheet monochrome or in colour", "File", Modes::Both, {}});
    add({"MSPACE", {"MS", "MODEL"}, "Go back to the model", "File", Modes::Both, {}});
    add({"TITLEBLOCK", {"TBLK"}, "Draw a titleblock on the current sheet", "File",
         Modes::Both, {}});
    add({"PREVIEW", {"PRE"}, "Preview the plot", "File", Modes::Both, {}});
    add({"PAGESETUP", {}, "Configure the page", "File", Modes::Both, {}});
    add({"PLOTSCALE", {}, "Set the plot scale", "File", Modes::Both, {}});
    add({"PLACELAYOUT", {}, "Slide the drawing on the sheet", "File", Modes::Both, {}});

    add({"ASK", {}, "Ask the AI assistant", "AI", Modes::Both, {}});
    add({"ASKV", {}, "Ask the AI assistant, with a screenshot of the view", "AI", Modes::Both, {}});
    add({"ASSISTANT", {}, "Open the assistant panel", "AI", Modes::Both, {}});
    add({"AISERVER", {}, "Add an AI server by URL", "AI", Modes::Both, {}});
    add({"AIUSE", {}, "Pick which AI server to use", "AI", Modes::Both, {}});
    add({"AIMODEL", {}, "Pick the assistant's model", "AI", Modes::Both, {}});
    add({"SPEAK", {}, "Read text aloud through the AI server", "AI", Modes::Both, {}});
    add({"DWGUNITS", {}, "Rescale a drawing imported in the wrong unit", "File", Modes::Both, {}});
    add({"PUBLISH", {}, "Batch publish sheets", "File", Modes::Both, {}});
    add({"SHEETSET", {}, "Manage sheets across drawings", "File", Modes::Both, {}});

    add({"GEOMCONSTRAINT", {"GCON"}, "Apply a geometric constraint", "Parametric",
         Modes::TwoD,
         {Prompt{"Enter constraint type", ArgKind::Keyword,
                 {"Horizontal", "Vertical", "Perpendicular", "PArallel", "Tangent",
                  "SMooth", "Coincident", "CONcentric", "COLlinear", "Symmetric",
                  "Equal", "Fix"}, false}}});
    add({"GCHORIZONTAL", {}, "Constrain a line horizontal", "Parametric", Modes::TwoD,
         {sel("Select an object or point")}});
    add({"GCVERTICAL", {}, "Constrain a line vertical", "Parametric", Modes::TwoD,
         {sel("Select an object or point")}});
    add({"GCPARALLEL", {}, "Constrain two lines parallel", "Parametric", Modes::TwoD,
         {sel("Select first object"), sel("Select second object")}});
    add({"GCPERPENDICULAR", {}, "Constrain two lines perpendicular", "Parametric",
         Modes::TwoD, {sel("Select first object"), sel("Select second object")}});
    add({"GCCOLLINEAR", {}, "Constrain two lines collinear", "Parametric", Modes::TwoD,
         {sel("Select first object"), sel("Select second object")}});
    add({"GCTANGENT", {}, "Constrain a line tangent to a curve", "Parametric",
         Modes::TwoD, {sel("Select first object"), sel("Select second object")}});
    add({"GCCONCENTRIC", {}, "Constrain two curves concentric", "Parametric",
         Modes::TwoD, {sel("Select first object"), sel("Select second object")}});
    add({"GCEQUAL", {}, "Constrain two objects equal", "Parametric", Modes::TwoD,
         {sel("Select first object"), sel("Select second object")}});
    add({"GCCOINCIDENT", {}, "Make two points coincident", "Parametric", Modes::TwoD,
         {sel("Select first point"), sel("Select second point")}});
    add({"GCSYMMETRIC", {}, "Constrain two objects symmetric about a line",
         "Parametric", Modes::TwoD,
         {sel("Select first object"), sel("Select second object"),
          sel("Select symmetry line")}});
    add({"GCFIX", {}, "Pin a point where it is", "Parametric", Modes::TwoD,
         {sel("Select a point")}});
    add({"DIMCONSTRAINT", {"DCON"}, "Apply a dimensional constraint", "Parametric",
         Modes::TwoD,
         {Prompt{"Enter constraint type", ArgKind::Keyword,
                 {"Linear", "Horizontal", "Vertical", "Aligned", "ANgular",
                  "Radial", "Diameter"}, false}}});
    add({"DELCONSTRAINT", {}, "Remove constraints from a selection", "Parametric",
         Modes::TwoD, {sel("Select objects")}});
    add({"CONSTRAINTBAR", {}, "Show or hide constraint bars", "Parametric",
         Modes::TwoD, {}});
    add({"PARAMETERS", {"PAR"}, "Manage parameters and expressions", "Parametric",
         Modes::TwoD, {}});
    add({"AUTOCONSTRAIN", {"ACON"}, "Infer and apply geometric constraints",
         "Parametric", Modes::TwoD, {sel("Select objects")}});
    add({"CONSTRAINTSETTINGS", {"CSETTINGS"}, "Configure inference tolerances",
         "Parametric", Modes::TwoD, {}});

    add({"UNDO", {"U"}, "Undo the last operation", "Edit", Modes::Both, {}});
    add({"REDO", {}, "Redo", "Edit", Modes::Both, {}});
    add({"SELECT", {}, "Select objects", "Edit", Modes::Both, {sel("Select objects")}});
    add({"QSELECT", {}, "Select by property", "Edit", Modes::Both, {}});
    add({"LISP", {}, "Evaluate a LISP expression", "Tools", Modes::Both, {}});
    add({"SCRIPT", {"SCR"}, "Run a script file", "Tools", Modes::Both, {}});
    add({"RSCRIPT", {}, "Repeat the last script", "Tools", Modes::Both, {}});
    add({"BOM", {}, "A bill of materials from the model", "Inquire", Modes::Both, {}});
    add({"BLOCKLIB", {"ADCENTER"}, "Insert from a folder of drawings", "Organise",
         Modes::TwoD, {}});
    add({"DIVIDE", {"DIV"}, "Points at equal divisions along an object", "Draw",
         Modes::TwoD, {sel("Select objects")}});
    add({"MEASURE", {"ME"}, "Points every so far along an object", "Draw",
         Modes::TwoD, {sel("Select objects")}});
    add({"UNDERLAY", {}, "Attach a PDF, scan or DWF to trace over", "Organise",
         Modes::TwoD, {}});
    add({"TEMPLATE", {}, "Save or start from a firm's standard", "File", Modes::Both, {}});
    add({"PGP", {"REINIT"}, "Load command aliases from a PGP file", "Tools",
         Modes::Both, {}});
    add({"APPLOAD", {"AP"}, "Load an application or LISP file", "Tools",
         Modes::Both, {}});
    add({"DELAY", {}, "Pause a running script", "Tools", Modes::Both,
         {Prompt{"Delay in milliseconds", ArgKind::Integer, {}, false}}});
    add({"RESUME", {}, "Resume a paused script", "Tools", Modes::Both, {}});

    add({"ZOOMPREVIOUS", {"ZP"}, "Go back to the previous view", "View",
         Modes::Both, {}});
    add({"ZOOMNEXT", {}, "Go forward again", "View", Modes::Both, {}});
    add({"3DWALK", {}, "Walk through the model", "View", Modes::ThreeD, {}});
    add({"3DFLY", {}, "Fly through the model", "View", Modes::ThreeD, {}});
    add({"NAVSWHEEL", {}, "Steering wheel navigation", "View", Modes::ThreeD, {}});
    add({"FILTER", {"FI"}, "Selection filter", "Edit", Modes::Both, {}});
}

}
