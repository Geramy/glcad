#include "engine/sysvar.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <limits>

#include "engine/command.hpp"

namespace gcmd {

void SysVars::put(SysVar v) {
    v.name = to_upper(v.name);
    index_[v.name] = vars_.size();
    vars_.push_back(std::move(v));
}

void SysVars::add_standard() {
    auto i = [&](const char* n, i64 v, const char* d) {
        put({n, VarType::Int, v, 0.0, "", d, false});
    };
    auto r = [&](const char* n, f64 v, const char* d) {
        put({n, VarType::Real, 0, v, "", d, false});
    };
    auto t = [&](const char* n, const char* v, const char* d) {
        put({n, VarType::Text, 0, 0.0, v, d, false});
    };

    i("OSMODE", Osnap_Endpoint | Osnap_Midpoint | Osnap_Center | Osnap_Intersection,
      "Running object snap bit flags");
    i("ORTHOMODE", 0, "Orthogonal drawing mode");
    i("SNAPMODE", 0, "Snap mode on/off");
    i("GRIDMODE", 1, "Grid display on/off");
    i("AUTOSNAP", 63, "AutoSnap marker, tooltip and magnet flags");
    i("POLARMODE", 0, "Polar tracking behaviour flags");
    r("POLARANG", 45.0, "Polar tracking increment, degrees");
    r("SNAPANG", 0.0, "Snap and grid rotation angle");
    r("APERTURE", 10.0, "Object snap target height, pixels");
    r("PICKBOX", 3.0, "Object selection target height, pixels");
    // Numbered as AutoCAD does: low digit is the mark, high the surround.
    i("PDMODE", 2, "Point display style");
    r("PDSIZE", 0.0, "Point display size: 0 is 4 pixels, negative is a fraction of the view");
    i("PICKFIRST", 1, "Select before command");
    i("PICKADD", 0, "Selection adds rather than replaces");

    i("LUNITS", 2, "Linear units: 1 scientific, 2 decimal, 3 engineering, "
                   "4 architectural, 5 fractional");
    i("LUPREC", 4, "Linear unit precision");
    i("AUNITS", 0, "Angular units: 0 decimal degrees");
    i("AUPREC", 2, "Angular precision");
    i("INSUNITS", 4, "Insertion units: 4 millimetres, 1 inches, 6 metres");
    r("LTSCALE", 1.0, "Global linetype scale");
    r("CELTSCALE", 1.0, "Current object linetype scale");
    i("PSLTSCALE", 1, "Paper space linetype scaling");
    r("DIMSCALE", 1.0, "Overall dimension scale");
    r("TEXTSIZE", 2.5, "Default text height");
    r("OFFSETDIST", 0.5, "Offset distance in drawing units");
    i("ARRAYROWS", 2, "Rectangular array rows");
    i("ARRAYCOLS", 3, "Rectangular array columns");
    i("ARRAYCOUNT", 6, "Polar and path array item count");
    r("ARRAYANGLE", 360, "Polar array sweep in degrees");
    r("GRIDUNIT", 0, "Grid and snap spacing; zero follows zoom");
    r("FILLETRAD", 0.0, "Current fillet radius");
    r("CHAMFERA", 0.0, "First chamfer distance");
    r("CHAMFERB", 0.0, "Second chamfer distance");

    t("CLAYER", "0", "Current layer");
    t("CECOLOR", "BYLAYER", "Current object colour");
    t("CELTYPE", "BYLAYER", "Current linetype");
    i("CELWEIGHT", -1, "Current lineweight (-1 BYLAYER)");
    t("TEXTSTYLE", "Standard", "Current text style");
    t("DIMSTYLE", "Standard", "Current dimension style");
    t("CANNOSCALE", "1:1", "Current space annotation scale");
    r("CANNOSCALEVALUE", 1.0, "Current annotation paper/drawing ratio");
    find("CANNOSCALEVALUE")->read_only=true;
    t("MLEADERSTYLE", "Leader", "Current leader annotation style");
    t("CMLSTYLE", "Standard", "Current multiline style");
    t("HPNAME", "ANSI31", "Default hatch pattern");
    r("HPSCALE", 1.0, "Hatch pattern scale");
    r("HPANG", 0.0, "Hatch pattern angle");

    i("DYNMODE", 3, "Dynamic input mode");
    i("SHORTCUTMENU", 11, "Right-click menu behaviour");
    i("HIGHLIGHT", 1, "Highlight selected objects");
    i("BLIPMODE", 0, "Marker blips");
    i("UCSICON", 3, "UCS icon display");
    i("LWDISPLAY", 0, "Display lineweights");

    put({"KERNELUNITS", VarType::Text, 0, 0.0, "metre",
         "Engine storage unit; always metric", true});
    put({"LATTICEBITS", VarType::Int, 32, 0.0, "",
         "Lattice resolution bits per axis", true});
    put({"SCALARBACKEND", VarType::Text, 0, 0.0, "DF64",
         "Active GPU scalar backend", true});
}

SysVar* SysVars::find(std::string_view name) {
    const auto it = index_.find(to_upper(name));
    return it == index_.end() ? nullptr : &vars_[it->second];
}
const SysVar* SysVars::find(std::string_view name) const {
    const auto it = index_.find(to_upper(name));
    return it == index_.end() ? nullptr : &vars_[it->second];
}

i64 SysVars::get_int(std::string_view n, i64 fb) const {
    const SysVar* v = find(n);
    return v && v->type == VarType::Int ? v->ival : fb;
}
f64 SysVars::get_real(std::string_view n, f64 fb) const {
    const SysVar* v = find(n);
    return v && v->type == VarType::Real ? v->rval : fb;
}
std::string SysVars::get_text(std::string_view n, std::string_view fb) const {
    const SysVar* v = find(n);
    return v && v->type == VarType::Text ? v->sval : std::string(fb);
}

bool SysVars::set_value(std::string_view name, std::string_view input, std::string* error) {
    auto* var=find(name);
    auto fail=[&](const std::string& why){if(error) *error=why;return false;};
    if(!var) return fail("unknown system variable");
    if(var->read_only) return fail("read-only");
    std::string value(input);const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return fail("a value is required");
    value=value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
    if(var->type==VarType::Text) {var->sval=value;return true;}
    errno=0;char* end=nullptr;const double number=std::strtod(value.c_str(),&end);
    if(errno==ERANGE || end!=value.c_str()+value.size() || !std::isfinite(number)) return fail("enter a finite number");
    if(var->type==VarType::Int && (std::floor(number)!=number || number>=double(std::numeric_limits<i64>::max()) || number<double(std::numeric_limits<i64>::min()))) return fail("enter an integer");
    const auto& n=var->name;
    auto is=[&](std::initializer_list<const char*> names){for(auto p:names) if(n==p) return true;return false;};
    if(is({"OFFSETDIST","TEXTSIZE","LTSCALE","CELTSCALE","DIMSCALE","HPSCALE","POLARANG","ARRAYANGLE"}) && number<=0) return fail("must be positive");
    if(is({"FILLETRAD","CHAMFERA","CHAMFERB","GRIDUNIT"}) && number<0) return fail("must be nonnegative");
    if(is({"ARRAYROWS","ARRAYCOLS"}) && (number<1 || number>1000)) return fail("must be between 1 and 1000");
    if(n=="ARRAYCOUNT" && (number<2 || number>10000)) return fail("must be between 2 and 10000");
    if(is({"APERTURE","PICKBOX"}) && (number<1 || number>50)) return fail("must be between 1 and 50 pixels");
    if(is({"POLARANG","ARRAYANGLE"}) && number>360) return fail("must not exceed 360 degrees");
    if(is({"LUPREC","AUPREC"}) && (number<0 || number>8)) return fail("must be between 0 and 8");
    if(is({"ORTHOMODE","SNAPMODE","GRIDMODE","PICKFIRST","HIGHLIGHT","BLIPMODE","LWDISPLAY","PSLTSCALE"}) && number!=0 && number!=1) return fail("must be 0 or 1");
    if(n=="PICKADD" && (number<0 || number>2)) return fail("must be 0, 1 or 2");
    if(n=="DYNMODE" && (number<-3 || number>3)) return fail("must be between -3 and 3");
    if(n=="LUNITS" && (number<1 || number>5)) return fail("must be between 1 and 5");
    if(n=="AUNITS" && (number<0 || number>4)) return fail("must be between 0 and 4");
    if(var->type==VarType::Int) var->ival=static_cast<i64>(number);else var->rval=number;
    return true;
}

bool SysVars::set_int(std::string_view n, i64 v) {
    SysVar* s = find(n);
    if (!s || s->read_only || s->type != VarType::Int) return false;
    s->ival = v;
    return true;
}
bool SysVars::set_real(std::string_view n, f64 v) {
    SysVar* s = find(n);
    if (!s || s->read_only || s->type != VarType::Real) return false;
    s->rval = v;
    return true;
}
bool SysVars::set_text(std::string_view n, std::string_view v) {
    SysVar* s = find(n);
    if (!s || s->read_only || s->type != VarType::Text) return false;
    s->sval = v;
    return true;
}

bool SysVars::osnap_enabled(i64 bit) const {
    const i64 m = get_int("OSMODE");
    if (m & Osnap_Off) return false;
    return (m & bit) != 0;
}

void SysVars::set_osnap(i64 bit, bool on) {
    i64 m = get_int("OSMODE");
    m = on ? (m | bit) : (m & ~bit);
    set_int("OSMODE", m);
}

}
