#include "ui/ai_tools.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <memory>

#include "engine/appconfig.hpp"
#include "engine/tls.hpp"
#include "geometry/footprint.hpp"
#include "plancheck/plancheck.hpp"
#include "rag/library.hpp"
#include "render/mtext.hpp"
#include "ui/shell.hpp"
#include "ui/snapshot.hpp"
#include "ui/symbols.hpp"

namespace gcapp::ai_tools {

namespace {

using gai::Json;

Json obj_schema(std::initializer_list<std::pair<const char*, const char*>> props,
                std::initializer_list<const char*> required = {}) {
    Json s = Json::object();
    s["type"] = "object";
    s["properties"] = Json::object();
    for (const auto& [name, desc] : props) {
        s["properties"][name]["type"] = "string";
        s["properties"][name]["description"] = desc;
    }
    Json req = Json::array();
    for (const char* r : required) req.push_back(r);
    if (!req.empty()) s["required"] = std::move(req);
    return s;
}

constexpr const char* kPoint = "a point as \"x,y\" in the current display units";

struct Args {
    Json v = Json::object();
    bool ok{false};
    std::string str(const char* key, const std::string& fallback = {}) const {
        const auto it = v.find(key);
        if (it == v.end()) return fallback;
        if (it->is_string()) return it->get<std::string>();
        if (it->is_number()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.10g", it->get<double>());
            return buf;
        }
        return fallback;
    }
    double num(const char* key, double fallback) const {
        const auto it = v.find(key);
        if (it == v.end()) return fallback;
        if (it->is_number()) return it->get<double>();
        if (it->is_string()) { try { return std::stod(it->get<std::string>()); } catch (...) {} }
        return fallback;
    }
};

std::string point_text(const Args& a, const char* key) {
    const auto it = a.v.find(key);
    if (it == a.v.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    char buf[64];
    if (it->is_array() && it->size() == 2 && (*it)[0].is_number() &&
        (*it)[1].is_number()) {
        std::snprintf(buf, sizeof(buf), "%.10g,%.10g",
                      (*it)[0].get<double>(), (*it)[1].get<double>());
        return buf;
    }
    if (it->is_object() && it->contains("x") && it->contains("y") &&
        (*it)["x"].is_number() && (*it)["y"].is_number()) {
        std::snprintf(buf, sizeof(buf), "%.10g,%.10g",
                      (*it)["x"].get<double>(), (*it)["y"].get<double>());
        return buf;
    }
    return {};
}

Args read_args(const gai::ToolCall& call) {
    Args a;
    const auto v = gai::parse_json(call.arguments().empty() ? "{}" : call.arguments());
    if (v && v->is_object()) { a.v = *v; a.ok = true; }
    return a;
}

Json error(const std::string& what) {
    Json e = Json::object();
    e["error"] = what;
    return e;
}

bool parse_point(Shell& shell, const std::string& text, double& x, double& y) {
    const auto comma = text.find(',');
    if (comma == std::string::npos) return false;
    const auto unit = shell.canvas().display_unit();
    const auto px = gunit::parse_length(text.substr(0, comma), unit);
    const auto py = gunit::parse_length(text.substr(comma + 1), unit);
    if (!px || !py) return false;
    x = *px;
    y = *py;
    return true;
}

double display(Shell& shell, double metres) {
    return gunit::from_metres(metres, shell.canvas().display_unit());
}

std::string fmt_length(Shell& shell, double metres) {
    const auto unit = shell.canvas().display_unit();
    gunit::Format f;
    f.unit = unit;
    f.style = unit == gunit::Length::Foot ? gunit::Style::Architectural
                                          : gunit::Style::Decimal;
    f.precision = unit == gunit::Length::Foot                                ? 4
                : unit == gunit::Length::Millimetre                          ? 1
                : unit == gunit::Length::Inch || unit == gunit::Length::Centimetre ? 2
                                                                             : 4;
    f.show_unit = false;
    const std::string body = gunit::format_length(metres, f);
    if (unit == gunit::Length::Inch) return body + "\"";
    if (unit == gunit::Length::Foot) return body;
    return body + " " + gunit::suffix(unit);
}

Json area_json(Shell& shell, double square_metres) {
    const auto unit = shell.canvas().display_unit();
    const double per = gunit::metres_per(unit);
    Json o = Json::object();
    o["area"] = square_metres / (per * per);
    o["area_units"] = std::string(gunit::suffix(unit)) + "2";
    o["area_m2"] = square_metres;
    o["area_sqft"] = square_metres / 0.09290304;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.4g %s2 (%.4g sqft)", square_metres / (per * per),
                  gunit::suffix(unit), square_metres / 0.09290304);
    o["area_text"] = buf;
    return o;
}

std::string fmt_point(Shell& shell, double x, double y) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g,%.6g", display(shell, x), display(shell, y));
    return buf;
}

bool pick_points(Shell& shell, Tool tool, const std::vector<std::string>& points,
                 std::string& err) {
    shell.canvas().set_tool(tool);
    for (const auto& p : points)
        if (!shell.canvas().type_point(p)) {
            shell.canvas().cancel();
            shell.canvas().set_tool(Tool::Select);
            err = "could not read point \"" + p + "\"";
            return false;
        }
    shell.canvas().cancel();
    shell.canvas().set_tool(Tool::Select);
    return true;
}

bool drawing_extents(Shell& shell, double& lo_x, double& lo_y, double& hi_x, double& hi_y) {
    lo_x = 1e300; lo_y = 1e300; hi_x = -1e300; hi_y = -1e300;
    for (const auto& sh : shell.canvas().shapes()) {
        double ax, ay, bx, by;
        gcm::to_world(shell.canvas().volume(), sh.a, ax, ay);
        gcm::to_world(shell.canvas().volume(), sh.b, bx, by);
        const double r = sh.is_curve() ? double(sh.radius) * shell.canvas().volume().inv_scale() : 0.0;
        lo_x = std::min({lo_x, ax - r, sh.is_curve() ? ax : bx});
        hi_x = std::max({hi_x, ax + r, sh.is_curve() ? ax : bx});
        lo_y = std::min({lo_y, ay - r, sh.is_curve() ? ay : by});
        hi_y = std::max({hi_y, ay + r, sh.is_curve() ? ay : by});
    }
    return lo_x < hi_x && lo_y < hi_y;
}

bool cell_box(Shell& shell, const std::string& cell, int cols, int rows,
              double& x0, double& y0, double& x1, double& y1) {
    if (cell.size() < 2) return false;
    const int col = std::toupper(static_cast<unsigned char>(cell[0])) - 'A';
    int row = 0;
    try { row = std::stoi(cell.substr(1)) - 1; } catch (...) { return false; }
    if (col < 0 || col >= cols || row < 0 || row >= rows) return false;
    double lo_x, lo_y, hi_x, hi_y;
    if (!drawing_extents(shell, lo_x, lo_y, hi_x, hi_y)) return false;
    const double cw = (hi_x - lo_x) / cols, ch = (hi_y - lo_y) / rows;
    x0 = lo_x + cw * col;
    x1 = x0 + cw;
    y0 = lo_y + ch * row;      // row 1 is the BOTTOM row, like a chart
    y1 = y0 + ch;
    return true;
}

Json shape_json(Shell& shell, std::size_t i) {
    const auto& sh = shell.canvas().shapes()[i];
    Json o = Json::object();
    o["id"] = static_cast<gcm::i64>(i);
    o["layer"] = sh.layer.empty() ? "0" : sh.layer;
    if(const auto type=sh.attrs.user.find("sketch_object");type!=sh.attrs.user.end())
        o["object_type"]=type->second;
    if(sh.poly) o["compound_id"]=sh.poly;
    o["region_boundary"]=sh.region;
    double ax, ay, bx, by;
    gcm::to_world(shell.canvas().volume(), sh.a, ax, ay);
    gcm::to_world(shell.canvas().volume(), sh.b, bx, by);
    switch (sh.kind) {
        case gsnap::ShapeKind::Segment: {
            o["kind"] = "line";
            o["from"] = fmt_point(shell, ax, ay);
            o["to"] = fmt_point(shell, bx, by);
            o["length"] = display(shell, std::hypot(bx - ax, by - ay));
            o["length_text"] = fmt_length(shell, std::hypot(bx - ax, by - ay));
            break;
        }
        case gsnap::ShapeKind::Circle:
        case gsnap::ShapeKind::Arc: {
            o["kind"] = sh.kind == gsnap::ShapeKind::Circle ? "circle" : "arc";
            o["centre"] = fmt_point(shell, ax, ay);
            const double r = double(sh.radius) * shell.canvas().volume().inv_scale();
            o["radius"] = display(shell, r);
            o["radius_text"] = fmt_length(shell, r);
            o["diameter_text"] = fmt_length(shell, r * 2.0);
            if (sh.kind == gsnap::ShapeKind::Arc) {
                o["start_angle_deg"] = sh.start_angle * 180.0 / 3.14159265358979323846;
                o["end_angle_deg"] = sh.end_angle * 180.0 / 3.14159265358979323846;
            }
            break;
        }
        default:
            o["kind"] = "other";
            o["at"] = fmt_point(shell, ax, ay);
    }
    return o;
}

struct LayerGuard {
    Shell& shell;
    std::string previous;
    LayerGuard(Shell& s, const std::string& layer) : shell(s) {
        previous = s.layers().current();
        if (!s.layers().find(layer)) {
            gdoc::Layer l;
            l.name = layer;
            s.layers().add(l);
        }
        s.layers().set_current(layer);
        s.canvas().set_current_layer(layer);
    }
    ~LayerGuard() {
        shell.layers().set_current(previous);
        shell.canvas().set_current_layer(previous);
    }
};

std::string run_draw_wall(Shell& shell, const Args& a, Json& out, std::string& err) {
    double ax, ay, bx, by;
    if (!parse_point(shell, point_text(a, "from"), ax, ay) ||
        !parse_point(shell, point_text(a, "to"), bx, by))
        return error("points read as \"x,y\"").dump();
    const double len = std::hypot(bx - ax, by - ay);
    if (len < 1e-9) return error("the wall's ends coincide").dump();

    const std::string type = a.str("type", "exterior");
    double t = type == "interior" ? 0.115 : 0.165;
    if (const auto given =
            gunit::parse_length(a.str("thickness", "0"), shell.canvas().display_unit());
        given && *given > 0.0)
        t = *given;

    LayerGuard layer(shell, "A-WALL");
    const auto wall_serial=shell.canvas().edit_serial();
    if (!shell.canvas().add_wall(gcm::to_lattice(shell.canvas().volume(), ax, ay),
                                  gcm::to_lattice(shell.canvas().volume(), bx, by), t))
        return error(shell.canvas().message()).dump();
    const double cx = (ax + bx) * 0.5, cy = (ay + by) * 0.5;
    const bool hatched = wall_serial==shell.canvas().edit_serial() || shell.canvas().ensure_wall_hatch(
        gcm::to_lattice(shell.canvas().volume(), cx, cy),
        std::max(t * 4.0, 0.25), type == "interior" ? 135.0 : 45.0);

    shell.canvas().set_tool(Tool::Select);
    out["drawn"] = "wall";
    out["type"] = type;
    out["thickness"] = display(shell, t);
    out["length"] = display(shell, len);
    if (!hatched) out["note"] = "core hatch did not take; the faces are drawn";
    return out.dump();
}

int default_jacks(double opening_w) { return opening_w >= 1.8 ? 2 : 1; }

void draw_jamb_framing(Shell& shell, double ex, double ey,
                       double ox, double oy,
                       double nx, double ny,
                       double t, int jacks, std::string& err) {
    const double stud = 0.038;   // a two-by's actual thickness
    const auto P = [&](double x, double y) { return fmt_point(shell, x, y); };
    LayerGuard layer(shell, "A-WALL");
    const auto first=shell.canvas().shapes().size();
    double off = 0.0;
    for (int i = 0; i < jacks + 1; ++i) {
        const double x0 = ex + ox * off, y0 = ey + oy * off;
        const double x1 = ex + ox * (off + stud), y1 = ey + oy * (off + stud);
        const double hx = nx * t * 0.5, hy = ny * t * 0.5;
        pick_points(shell, Tool::Line, {P(x0 + hx, y0 + hy), P(x1 + hx, y1 + hy)}, err);
        pick_points(shell, Tool::Line, {P(x0 - hx, y0 - hy), P(x1 - hx, y1 - hy)}, err);
        pick_points(shell, Tool::Line, {P(x0 + hx, y0 + hy), P(x0 - hx, y0 - hy)}, err);
        pick_points(shell, Tool::Line, {P(x1 + hx, y1 + hy), P(x1 - hx, y1 - hy)}, err);
        off += stud;
    }
    shell.canvas().group_object(first,"framing");
}

bool snap_opening(Shell& shell,double& ax,double& ay,double& bx,double& by,double& thickness,gcm::u64* wall_group=nullptr) {
    struct Face {double x,y,ux,uy,len,d;gcm::u64 group;};
    std::vector<Face> faces;
    const auto& volume=shell.canvas().volume();
    for(const auto& shape:shell.canvas().shapes()) {
        if(shape.layer!="A-WALL" || shape.kind!=gsnap::ShapeKind::Segment) continue;
        const auto type=shape.attrs.user.find("sketch_object");
        if(type!=shape.attrs.user.end() && type->second!="wall")continue;
        double x,y,qx,qy;gcm::to_world(volume,shape.a,x,y);gcm::to_world(volume,shape.b,qx,qy);
        const double len=std::hypot(qx-x,qy-y);if(len<.35) continue;
        const double ux=(qx-x)/len,uy=(qy-y)/len;
        const double ta=(ax-x)*ux+(ay-y)*uy,tb=(bx-x)*ux+(by-y)*uy;
        if(ta<-.025 || ta>len+.025 || tb<-.025 || tb>len+.025) continue;
        const double da=(ax-x)*(-uy)+(ay-y)*ux,db=(bx-x)*(-uy)+(by-y)*ux;
        if(std::abs(da)>.5 || std::abs(db-da)>.025) continue;
        faces.push_back({x,y,ux,uy,len,std::abs(da),shape.poly});
    }
    std::sort(faces.begin(),faces.end(),[](auto a,auto b){return a.d<b.d;});
    for(const auto& a:faces) for(const auto& b:faces) {
        if(a.group && b.group && a.group!=b.group)continue;
        if(std::abs(a.ux*b.uy-a.uy*b.ux)>1e-6) continue;
        const double offset=(b.x-a.x)*(-a.uy)+(b.y-a.y)*a.ux;
        if(std::abs(offset)<.05 || std::abs(offset)>.5 || a.d>.25) continue;
        const double da=(ax-a.x)*(-a.uy)+(ay-a.y)*a.ux;
        const double db=(bx-a.x)*(-a.uy)+(by-a.y)*a.ux;
        const double width=std::hypot(bx-ax,by-ay),direction=(bx-ax)*a.ux+(by-ay)*a.uy>=0?1:-1;
        ax+=(offset*.5-da)*(-a.uy);ay+=(offset*.5-da)*a.ux;
        bx=ax+direction*a.ux*width;by=ay+direction*a.uy*width;
        thickness=std::abs(offset);if(wall_group)*wall_group=a.group==b.group?a.group:0;return true;
    }
    return false;
}

std::string run_draw_door(Shell& shell, const Args& a, Json& out, std::string& err) {
    double hx_, hy_, lx, ly;
    if (!parse_point(shell, point_text(a, "hinge"), hx_, hy_) ||
        !parse_point(shell, point_text(a, "latch"), lx, ly))
        return error("hinge and latch read as \"x,y\"").dump();
    double host_thickness=.165;gcm::u64 host_group=0;
    const auto host_option=a.v.find("snap_to_wall");
    std::string host_mode=host_option!=a.v.end() && host_option->is_boolean()
        ? (host_option->get<bool>() ? "true" : "false") : a.str("snap_to_wall","auto");
    if(host_mode!="auto" && host_mode!="true" && host_mode!="false")
        return error("Door: snap_to_wall must be true, false, or omitted for automatic host detection").dump();
    const bool snapped=host_mode!="false" && snap_opening(shell,hx_,hy_,lx,ly,host_thickness,&host_group);
    if(host_mode=="true" && !snapped)
        return error("Door: could not identify two parallel wall faces across both jambs; pick an unobstructed wall span or disable snap_to_wall for a standalone symbol").dump();
    const double w = std::hypot(lx - hx_, ly - hy_);
    if (w < 1e-9) return error("hinge and latch coincide").dump();
    const bool left = a.str("swing", "left") != "right";
    const double ux = (lx - hx_) / w, uy = (ly - hy_) / w;
    const double px_ = left ? -uy : uy, py_ = left ? ux : -ux;
    const auto P = [&](double x, double y) {
        char point[128];
        std::snprintf(point,sizeof(point),"%.17g,%.17g",display(shell,x),display(shell,y));
        return std::string(point);
    };
    const std::string kind = a.str("kind", "swing");
    const auto walls_before_door=shell.canvas().shapes();
    const auto first_door_shape=shell.canvas().shapes().size();
    const double host_hx=hx_,host_hy=hy_,host_lx=lx,host_ly=ly;

    double t = host_thickness;
    if (const auto given = gunit::parse_length(a.str("wall_thickness", "0"),
                                               shell.canvas().display_unit());
        given && *given > 0.0 && !snapped)
        t = *given;

    const auto component=a.str("component","door");
    if(component=="frame" || component=="hinge") {
        LayerGuard layer(shell,"A-DOOR");
        if(component=="hinge") {
            if(!pick_points(shell,Tool::Circle,{P(hx_,hy_),P(hx_+.012,hy_)},err)) return error(err).dump();
        } else {
            for(auto end:{std::pair{hx_,hy_},std::pair{lx,ly}})
                if(!pick_points(shell,Tool::Line,{P(end.first-px_*t*.5,end.second-py_*t*.5),
                    P(end.first+px_*t*.5,end.second+py_*t*.5)},err)) return error(err).dump();
        }
        shell.canvas().group_object(first_door_shape,component);
        out["drawn"]=component;return out.dump();
    }
    const bool swing=kind!="rollup" && kind!="overhead" && kind!="garage";
    if(snapped && swing) {
        hx_+=px_*host_thickness*.5;hy_+=py_*host_thickness*.5;
        lx+=px_*host_thickness*.5;ly+=py_*host_thickness*.5;
        std::vector<gsnap::Shape> candidate;
        for(const auto& wall:shell.canvas().shapes())if(wall.layer=="A-WALL")candidate.push_back(wall);
        gsnap::Shape arc;arc.kind=gsnap::ShapeKind::Arc;arc.layer="A-DOOR";
        arc.a=gcm::to_lattice(shell.canvas().volume(),hx_,hy_);
        arc.radius=gcm::i64(std::llround(w/shell.canvas().volume().inv_scale()));
        arc.start_angle=left?std::atan2(uy,ux):std::atan2(py_,px_);
        arc.end_angle=left?std::atan2(py_,px_):std::atan2(uy,ux);
        candidate.push_back(arc);
        gplan::Plan plan;plan.shapes=&candidate;plan.metres_per_unit=shell.canvas().volume().inv_scale();
        if(!gplan::door_swing_collisions(plan).empty())
            return error("Door: its swing or latch intersects a wall. Move the opening to a clear span or reverse the swing; the requested width was not changed").dump();
    }
    if (kind == "rollup" || kind == "overhead" || kind == "garage") {
        LayerGuard layer(shell, "A-DOOR");
        const double d = 0.05;
        const double hx = px_ * d * 0.5, hy = py_ * d * 0.5;
        if (!pick_points(shell, Tool::Line, {P(hx_ + hx, hy_ + hy), P(lx + hx, ly + hy)}, err) ||
            !pick_points(shell, Tool::Line, {P(hx_ - hx, hy_ - hy), P(lx - hx, ly - hy)}, err) ||
            !pick_points(shell, Tool::Line, {P(hx_ + hx, hy_ + hy), P(hx_ - hx, hy_ - hy)}, err) ||
            !pick_points(shell, Tool::Line, {P(lx + hx, ly + hy), P(lx - hx, ly - hy)}, err))
            return error(err).dump();
        const double run = std::min(1.2, w * 0.35);
        const double inset = 0.05;
        pick_points(shell, Tool::Line,
                    {P(hx_ + ux * inset, hy_ + uy * inset),
                     P(hx_ + ux * inset + px_ * run, hy_ + uy * inset + py_ * run)}, err);
        pick_points(shell, Tool::Line,
                    {P(lx - ux * inset, ly - uy * inset),
                     P(lx - ux * inset + px_ * run, ly - uy * inset + py_ * run)}, err);
        shell.canvas().group_object(first_door_shape,"rollup");
        out["drawn"] = "rollup door";
    } else {
        const double tx = hx_ + px_ * w, ty = hy_ + py_ * w;
        LayerGuard layer(shell, "A-DOOR");
        if (!pick_points(shell, Tool::Line, {P(hx_, hy_), P(tx, ty)}, err))
            return error(err).dump();
        shell.canvas().set_tool_mode(Tool::Arc, 0);
        const bool ccw_from_latch = left;
        if (!pick_points(shell, Tool::Arc,
                         ccw_from_latch
                             ? std::vector<std::string>{P(hx_, hy_), P(lx, ly), P(tx, ty)}
                             : std::vector<std::string>{P(hx_, hy_), P(tx, ty), P(lx, ly)},
                         err))
            return error(err).dump();
        shell.canvas().group_door(first_door_shape);
        out["drawn"] = "door";
        out["swing"] = left ? "left" : "right";
    }

    shell.canvas().modify_shapes([&](auto& shapes) {
        for(std::size_t i=first_door_shape;i<shapes.size();++i)
            shapes[i].attrs.user["opening_host"]=snapped?"wall":"none";
    });

    const int jacks = int(a.num("jacks", default_jacks(w)));
    if (a.str("framing", "wood") != "none" && jacks > 0) {
        draw_jamb_framing(shell, host_hx, host_hy, -ux, -uy, px_, py_, t, jacks, err);
        draw_jamb_framing(shell, host_lx, host_ly, ux, uy, px_, py_, t, jacks, err);
        out["framing"] = std::to_string(jacks) + " jack(s) + king each side" +
                         (jacks > 1 ? " (doubled)" : "");
    }
    out["width"] = display(shell, w);
    out["hinge"]=fmt_point(shell,hx_,hy_);out["latch"]=fmt_point(shell,lx,ly);
    std::optional<Canvas::OpeningCut> opening;
    if(snapped)opening=Canvas::OpeningCut{gcm::to_lattice(shell.canvas().volume(),host_hx,host_hy),
        gcm::to_lattice(shell.canvas().volume(),host_lx,host_ly),host_thickness,host_group};
    gcm::u32 door_key=0;
    for(std::size_t i=first_door_shape;i<shell.canvas().shapes().size();++i)
        if(shell.canvas().shapes()[i].kind==gsnap::ShapeKind::Arc)door_key=shell.canvas().shapes()[i].key;
    const auto cleanup=snapped ? shell.canvas().merge_overlaps(.002,false,true,opening) : Canvas::CleanupReport{};
    shell.canvas().track_door_opening(door_key,walls_before_door,host_mode!="false");
    out["wall_faces_cut"]=cleanup.cut;out["snapped_to_wall"]=snapped;
    out["note"] = cleanup.cut>0?"Wall faces cut at the opening":"No wall faces intersected this opening";
    shell.canvas().set_tool(Tool::Select);
    return out.dump();
}

std::string run_draw_window(Shell& shell, const Args& a, Json& out, std::string& err) {
    double ax, ay, bx, by;
    if (!parse_point(shell, point_text(a, "from"), ax, ay) ||
        !parse_point(shell, point_text(a, "to"), bx, by))
        return error("from and to read as \"x,y\"").dump();
    double host_thickness=.165;gcm::u64 host_group=0;
    const auto host_option=a.v.find("snap_to_wall");
    const std::string host_mode=host_option!=a.v.end() && host_option->is_boolean()
        ? (host_option->get<bool>() ? "true" : "false") : a.str("snap_to_wall","auto");
    if(host_mode!="auto" && host_mode!="true" && host_mode!="false")
        return error("Window: snap_to_wall must be true, false, or omitted for automatic host detection").dump();
    const bool snapped=host_mode!="false" && snap_opening(shell,ax,ay,bx,by,host_thickness,&host_group);
    if(host_mode=="true" && !snapped)
        return error("Window: could not identify two parallel wall faces across both jambs").dump();
    const double w = std::hypot(bx - ax, by - ay);
    if (w < 1e-9) return error("the window's ends coincide").dump();
    const double ux = (bx - ax) / w, uy = (by - ay) / w;
    const double nx = -uy, ny = ux;
    double t = host_thickness;
    if (const auto given = gunit::parse_length(a.str("wall_thickness", "0"),
                                               shell.canvas().display_unit());
        given && *given > 0.0 && !snapped)
        t = *given;
    const auto P = [&](double x, double y) {
        char point[128];
        std::snprintf(point,sizeof(point),"%.17g,%.17g",display(shell,x),display(shell,y));
        return std::string(point);
    };

    const auto first_window_shape=shell.canvas().shapes().size();
    {
        LayerGuard layer(shell, "A-GLAZ");
        const double hx = nx * t * 0.5, hy = ny * t * 0.5;
        if (!pick_points(shell, Tool::Line, {P(ax + hx, ay + hy), P(bx + hx, by + hy)}, err) ||
            !pick_points(shell, Tool::Line, {P(ax - hx, ay - hy), P(bx - hx, by - hy)}, err) ||
            !pick_points(shell, Tool::Line, {P(ax + hx, ay + hy), P(ax - hx, ay - hy)}, err) ||
            !pick_points(shell, Tool::Line, {P(bx + hx, by + hy), P(bx - hx, by - hy)}, err))
            return error(err).dump();
        const double g = t * 0.15;
        const double gx = nx * g, gy = ny * g;
        if (!pick_points(shell, Tool::Line, {P(ax + gx, ay + gy), P(bx + gx, by + gy)}, err) ||
            !pick_points(shell, Tool::Line, {P(ax - gx, ay - gy), P(bx - gx, by - gy)}, err))
            return error(err).dump();
        const double cx = (ax + bx) * 0.5, cy = (ay + by) * 0.5;
        shell.canvas().hatch_at_world(
            gcm::to_lattice(shell.canvas().volume(), cx, cy), "ANSI36",
            std::max(t * 2.0, 0.2), 45.0);
    }

    shell.canvas().group_object(first_window_shape,"window");
    shell.canvas().modify_shapes([&](auto& shapes) {
        for(std::size_t i=first_window_shape;i<shapes.size();++i)
            shapes[i].attrs.user["opening_host"]=snapped?"wall":"none";
    });
    const int jacks = int(a.num("jacks", default_jacks(w)));
    if (a.str("framing", "wood") != "none" && jacks > 0) {
        draw_jamb_framing(shell, ax, ay, -ux, -uy, nx, ny, t, jacks, err);
        draw_jamb_framing(shell, bx, by, ux, uy, nx, ny, t, jacks, err);
        out["framing"] = std::to_string(jacks) + " jack(s) + king each side" +
                         (jacks > 1 ? " (doubled)" : "");
    }
    out["drawn"] = "window";
    out["width"] = display(shell, w);
    const auto cleanup=snapped ? shell.canvas().merge_overlaps(.002,false,true,
        Canvas::OpeningCut{gcm::to_lattice(shell.canvas().volume(),ax,ay),
            gcm::to_lattice(shell.canvas().volume(),bx,by),host_thickness,host_group}) : Canvas::CleanupReport{};
    out["wall_faces_cut"]=cleanup.cut;out["snapped_to_wall"]=snapped;
    out["note"] = cleanup.cut>0?"Wall faces cut at the opening":"No wall faces intersected this opening";
    return out.dump();
}

std::string run_merge_overlaps(Shell& shell, const Args& a, Json& out) {
    const std::string what = a.str("what", "all");
    double tol = 0.002;
    if (const auto given = gunit::parse_length(a.str("tolerance", "0"),
                                               shell.canvas().display_unit());
        given && *given > 0.0)
        tol = *given;
    const bool dedupe = what == "all" || what == "duplicates";
    const bool openings = what == "all" || what == "openings";
    const auto rep = shell.canvas().merge_overlaps(tol, dedupe, openings);
    out["merged_duplicates"] = rep.merged;
    out["wall_faces_cut_at_openings"] = rep.cut;
    out["jamb_caps_added"] = rep.caps;
    out["removed"] = rep.removed;
    if (rep.merged + rep.cut + rep.caps + rep.removed == 0)
        out["note"] = "nothing overlapped; the drawing was already clean";
    return out.dump();
}

std::string run_label_room(Shell& shell, const Args& a, Json& out) {
    double x, y;
    if (!parse_point(shell, point_text(a, "at"), x, y))
        return error("at reads as \"x,y\"").dump();
    std::string name = a.str("name");
    if (name.empty()) return error("the room needs a name").dump();
    for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
    const std::string area = a.str("area");
    if (!area.empty()) name += "  " + area;
    double h = 0.2;
    if (const auto given =
            gunit::parse_length(a.str("height", "0"), shell.canvas().display_unit());
        given && *given > 0.0)
        h = *given;
    LayerGuard layer(shell, "A-ANNO-DIMS");
    shell.canvas().add_text(gcm::to_lattice(shell.canvas().volume(), x, y),
                            name, h, 0.0, 1, 2);
    out["labelled"] = name;
    return out.dump();
}

std::string command_word(const gai::ToolCall& call) {
    const Args a = read_args(call);
    std::string cmd = a.str("command");
    const auto sp = cmd.find(' ');
    if (sp != std::string::npos) cmd = cmd.substr(0, sp);
    for (char& c : cmd) c = char(std::toupper(static_cast<unsigned char>(c)));
    return cmd;
}

bool destructive_command(const std::string& cmd) {
    static const char* kGate[] = {"ERASE", "DELETEBODY", "NEW", "OPEN", "SAVE", "SAVEAS",
                                  "IMPORT", "EXPORT", "PLOT", "CLOSETAB", "PURGE", "OVERKILL"};
    for (const char* g : kGate)
        if (cmd == g) return true;
    return false;
}

std::string resolve_host(const std::string& host) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return {};
    char buf[INET6_ADDRSTRLEN] = {0};
    std::string out;
    for (addrinfo* a = res; a; a = a->ai_next) {
        if (a->ai_family == AF_INET) {
            auto* sa = reinterpret_cast<sockaddr_in*>(a->ai_addr);
            if (::inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) { out = buf; break; }
        }
        if (a->ai_family == AF_INET6 && out.empty()) {
            auto* sa = reinterpret_cast<sockaddr_in6*>(a->ai_addr);
            if (::inet_ntop(AF_INET6, &sa->sin6_addr, buf, sizeof(buf))) out = buf;
        }
    }
    ::freeaddrinfo(res);
    return out;
}

bool https_get_once(const std::string& host, const std::string& path,
                    std::string& body, int& status, std::string& location,
                    std::string& error) {
    body.clear(); location.clear(); status = 0;
    if (!gio::tls_available()) { error = "this build has no TLS"; return false; }
    const std::string ip = resolve_host(host);
    if (ip.empty()) { error = "could not resolve " + host; return false; }
    gio::TlsConfig cfg;
    cfg.server_name = host;
    gio::TlsTransport t(cfg);
    t.set_timeout(15.0);
    gio::Status s;
    auto conn = t.connect(gio::Endpoint::tls(ip, 443), gio::Mode::ReadWrite, s);
    if (!conn) { error = s.message.empty() ? "TLS connect failed" : s.message; return false; }
    const std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "User-Agent: GLCAD/1.0\r\n"
        "Accept: text/html, text/plain, application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n\r\n";
    conn->output()->write_all(reinterpret_cast<const gcm::u8*>(req.data()), req.size());
    conn->output()->flush();
    std::string raw;
    gcm::u8 buf[8192];
    while (raw.size() < (2u << 20)) {
        const std::size_t n = conn->input()->read(buf, sizeof(buf));
        if (n == 0) break;
        raw.append(reinterpret_cast<const char*>(buf), n);
    }
    conn->close();
    const std::size_t hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string::npos) { error = "no HTTP response"; return false; }
    const std::string head = raw.substr(0, hdr_end);
    status = std::atoi(head.c_str() + head.find(' '));
    std::string lower = head;
    for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    const auto header = [&](const char* name) -> std::string {
        const std::size_t at = lower.find("\r\n" + std::string(name) + ":");
        if (at == std::string::npos) return {};
        std::size_t from = head.find(':', at + 2) + 1;
        std::size_t to = head.find("\r\n", from);
        std::string v = head.substr(from, to - from);
        while (!v.empty() && v.front() == ' ') v.erase(0, 1);
        return v;
    };
    location = header("location");
    body = raw.substr(hdr_end + 4);
    if (header("transfer-encoding").find("chunked") != std::string::npos) {
        std::string flat;
        std::size_t at = 0;
        while (at < body.size()) {
            const std::size_t eol = body.find("\r\n", at);
            if (eol == std::string::npos) break;
            const long len = std::strtol(body.c_str() + at, nullptr, 16);
            if (len <= 0) break;
            flat.append(body, eol + 2, std::size_t(len));
            at = eol + 2 + std::size_t(len) + 2;
        }
        body.swap(flat);
    }
    return true;
}

bool https_get(std::string host, std::string path, std::string& body,
               std::string& error) {
    for (int hop = 0; hop < 4; ++hop) {
        int status = 0;
        std::string loc;
        if (!https_get_once(host, path, body, status, loc, error)) return false;
        if (status >= 300 && status < 400 && !loc.empty()) {
            if (loc.rfind("https://", 0) == 0) {
                const std::string rest = loc.substr(8);
                const std::size_t slash = rest.find('/');
                host = rest.substr(0, slash);
                path = slash == std::string::npos ? "/" : rest.substr(slash);
                continue;
            }
            if (loc[0] == '/') { path = loc; continue; }
            error = "redirected off https";
            return false;
        }
        if (status != 200) { error = "HTTP " + std::to_string(status); return false; }
        return true;
    }
    error = "too many redirects";
    return false;
}

std::string strip_html(const std::string& html) {
    std::string out;
    out.reserve(html.size() / 2);
    std::size_t i = 0;
    const auto skip_block = [&](const char* open, const char* close) {
        if (html.compare(i, std::strlen(open), open) != 0) return false;
        const std::size_t end = html.find(close, i);
        i = end == std::string::npos ? html.size() : end + std::strlen(close);
        return true;
    };
    while (i < html.size()) {
        if (html[i] == '<') {
            if (skip_block("<script", "</script>")) continue;
            if (skip_block("<style", "</style>")) continue;
            const std::size_t end = html.find('>', i);
            if (html.compare(i, 4, "</p>") == 0 || html.compare(i, 4, "<br>") == 0 ||
                html.compare(i, 5, "</h1>") == 0 || html.compare(i, 5, "</h2>") == 0 ||
                html.compare(i, 5, "</h3>") == 0 || html.compare(i, 6, "</div>") == 0 ||
                html.compare(i, 5, "</li>") == 0 || html.compare(i, 5, "</tr>") == 0)
                out.push_back('\n');
            i = end == std::string::npos ? html.size() : end + 1;
            continue;
        }
        if (html[i] == '&') {
            static const std::pair<const char*, char> kEnt[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
                {"&quot;", '"'}, {"&#x27;", '\''}, {"&#39;", '\''}, {"&nbsp;", ' '}};
            bool hit = false;
            for (const auto& [e, ch] : kEnt)
                if (html.compare(i, std::strlen(e), e) == 0) {
                    out.push_back(ch);
                    i += std::strlen(e);
                    hit = true;
                    break;
                }
            if (hit) continue;
        }
        out.push_back(html[i++]);
    }
    std::string tidy;
    tidy.reserve(out.size());
    int nl = 0, sp = 0;
    for (char c : out) {
        if (c == '\n') { ++nl; sp = 0; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { ++sp; continue; }
        if (nl) tidy.push_back('\n');
        else if (sp && !tidy.empty()) tidy.push_back(' ');
        nl = sp = 0;
        tidy.push_back(c);
    }
    return tidy;
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back(char(c));
        else if (c == ' ')
            out.push_back('+');
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string url_decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out.push_back(char(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string run_web_search(const std::string& query) {
    std::string body, err;
    if (!https_get("html.duckduckgo.com", "/html/?q=" + url_encode(query), body, err))
        return "{\"error\":\"search failed: " + err + "\"}";
    Json results = Json::array();
    std::size_t at = 0;
    while (results.size() < 6) {
        const std::size_t a = body.find("result__a", at);
        if (a == std::string::npos) break;
        const std::size_t href = body.find("href=\"", a);
        const std::size_t href_end = body.find('"', href + 6);
        const std::size_t text_end = body.find("</a>", href_end);
        const std::size_t open_end = body.find('>', href_end);
        if (href == std::string::npos || text_end == std::string::npos) break;
        std::string url = body.substr(href + 6, href_end - href - 6);
        if (const std::size_t u = url.find("uddg="); u != std::string::npos) {
            url = url_decode(url.substr(u + 5, url.find('&', u) - u - 5));
        }
        const std::string title = strip_html(body.substr(open_end + 1, text_end - open_end - 1));
        std::string snippet;
        if (const std::size_t sn = body.find("result__snippet", text_end); sn != std::string::npos) {
            const std::size_t so = body.find('>', sn);
            const std::size_t se = body.find("</a>", so);
            if (se != std::string::npos)
                snippet = strip_html(body.substr(so + 1, se - so - 1));
        }
        Json row = Json::object();
        row["title"] = title;
        row["url"] = url;
        row["snippet"] = snippet;
        results.push_back(std::move(row));
        at = text_end;
    }
    Json out = Json::object();
    out["results"] = std::move(results);
    if (out["results"].empty()) out["note"] = "no results parsed";
    return out.dump();
}

std::string run_save_standard(Shell& shell, const std::string& title,
                              const std::string& text, const std::string& source);

std::string run_fetch_url(Shell& shell, const std::string& url,
                          const std::string& save_title) {
    if (url.rfind("https://", 0) != 0)
        return "{\"error\":\"only https:// addresses are fetched\"}";
    const std::string rest = url.substr(8);
    const std::size_t slash = rest.find('/');
    const std::string host = rest.substr(0, slash);
    const std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
    std::string body, err;
    if (!https_get(host, path, body, err))
        return "{\"error\":\"fetch failed: " + err + "\"}";
    std::string text = strip_html(body);
    Json out = Json::object();
    out["url"] = url;
    if (!save_title.empty()) {
        const std::string sv = run_save_standard(shell, save_title, text, url);
        if (const auto v = gai::parse_json(sv); v && v->is_object()) {
            if (v->contains("saved")) out["saved_to_library"] = (*v)["saved"];
            if (v->contains("indexed")) out["indexed"] = (*v)["indexed"];
        }
    }
    constexpr std::size_t kCap = 8000;
    if (text.size() > kCap) {
        text.resize(kCap);
        out["truncated"] = true;
    }
    out["text"] = std::move(text);
    return out.dump();
}

grag::Library& library() {
    static std::mutex mu;
    static std::map<std::string, std::unique_ptr<grag::Library>> libs;
    const std::string dir =
        (std::filesystem::path(gfile::app_config_path()).parent_path() / "library")
            .string();
    std::lock_guard<std::mutex> lock(mu);
    auto& slot = libs[dir];
    if (!slot)
        slot = std::make_unique<grag::Library>(dir,
#ifdef GLCAD_WITH_PGLITE
                                               grag::Backend::Postgres
#else
                                               grag::Backend::Sidecar
#endif
        );
    return *slot;
}

std::string plan_rules_path() {
    return (std::filesystem::path(library().dir()) / "plan-rules.txt").string();
}

std::string pick_embedding_model(Shell& shell) {
    const auto client = shell.ai_client();
    if (!client) return {};
    for (const auto& sv : shell.app_config().ai_servers)
        if (sv.url == client->url() && !sv.default_embed.empty())
            return sv.default_embed;
    if (const auto m = client->models(); m.ok())
        for (const auto& mi : m->data) {
            if (mi.embeds()) return mi.id;
            std::string low = mi.id;
            for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (low.find("embed") != std::string::npos) return mi.id;
        }
    return {};
}

grag::Embedder embedder_for(const gai::Client& c, const std::string& model) {
    return [c, model](const std::vector<std::string>& texts) {
        gai::EmbeddingRequest req;
        req.model = model;
        req.input = texts;
        std::vector<std::vector<float>> out;
        if (const auto r = c.embed(req); r.ok() && r->data.size() == texts.size())
            for (const auto& row : r->data)
                out.emplace_back(row.embedding.begin(), row.embedding.end());
        return out;
    };
}

std::string run_save_standard(Shell& shell, const std::string& title,
                              const std::string& text, const std::string& source) {
    const std::string path = library().save(title, text, source);
    if (path.empty())
        return "{\"error\":\"a title and the text are both required, and the "
               "library must be writable\"}";
    bool indexed = false;
    const std::string model = pick_embedding_model(shell);
    if (!model.empty())
        if (const auto client = shell.ai_client())
            indexed = library().ensure_indexed(model, embedder_for(*client, model));
    Json out = Json::object();
    out["saved"] = path;
    out["indexed"] = indexed;
    if (!indexed)
        out["note"] = "kept as text; no embedding model on the server, so "
                      "searches will use term matching";
    return out.dump();
}

std::string run_search_standards(Shell& shell, const std::string& query, int k) {
    bool embedded = false;
    const auto hits = library_lookup(shell, query, std::clamp(k, 1, 10), &embedded);
    if (hits.empty() && library().empty())
        return "{\"error\":\"the standards library is empty -- put .txt or .md "
               "files in " + library().dir() +
               ", or find the project's codes online and save_standard them\"}";
    Json out = Json::object();
    out["method"] = embedded ? "embeddings" : "terms";
    Json rows = Json::array();
    for (const auto& h : hits) {
        Json row = Json::object();
        row["file"] = h.file;
        row["text"] = h.text;
        rows.push_back(std::move(row));
    }
    out["passages"] = std::move(rows);
    if (out["passages"].empty()) out["note"] = "nothing in the library matched";
    return out.dump();
}

std::string run_save_plan_rules(const Args& a) {
    gplan::Rules r = gplan::load_rules(plan_rules_path());
    if (const std::string s = a.str("source"); !s.empty()) r.source = s;
    if (const double v = a.num("entrance_min_mm", 0); v > 0)
        r.entrance_min_m = v / 1000.0;
    if (const double v = a.num("egress_door_min_mm", 0); v > 0)
        r.egress_door_min_m = v / 1000.0;
    if (!gplan::save_rules(plan_rules_path(), r))
        return "{\"error\":\"could not write the rules overlay\"}";
    Json out = Json::object();
    out["saved"] = plan_rules_path();
    out["source"] = r.source;
    return out.dump();
}

std::string run_calc(Shell& shell, const Args& a, Json& out) {
    std::string err;
    const auto q = ggeo::plan::calc(a.str("expr"), err);
    if (!q) return error(err).dump();
    if (q->dim == 0) {
        out["value"] = q->v;
    } else if (q->dim == 1) {
        out["display_units"] = display(shell, q->v);
        out["metres"] = q->v;
        out["feet"] = q->v / 0.3048;
    } else if (q->dim == 2) {
        out["m2"] = q->v;
        out["sqft"] = q->v / 0.09290304;
    } else {
        out["value_si"] = q->v;
        out["length_power"] = q->dim;
    }
    return out.dump();
}

std::string run_plan_footprint(Shell& shell, const Args& a, Json& out) {
    std::string err;
    ggeo::plan::FootprintSpec spec;
    const auto area = ggeo::plan::calc(a.str("area"), err);
    if (!area) return error("area: " + err).dump();
    if (area->dim != 2)
        return error("give the area WITH its unit, e.g. \"2800 sqft\" or \"260 m2\"").dump();
    spec.area = area->v;
    spec.style = a.str("style", "rectangle");
    spec.aspect = a.num("aspect", 1.4);
    const auto unit = shell.canvas().display_unit();
    const auto len = [&](const char* key, double fb) {
        const std::string s = a.str(key);
        if (s.empty()) return fb;
        return gunit::parse_length(s, unit).value_or(fb);
    };
    spec.grid = len("grid", 0.1524);
    spec.lot_w = len("lot_width", 0.0);
    spec.lot_d = len("lot_depth", 0.0);
    spec.front_setback = len("setback_front", 0.0);
    spec.side_setback = len("setback_side", 0.0);
    spec.rear_setback = len("setback_rear", 0.0);
    const auto fp = ggeo::plan::footprint(spec, err);
    if (!fp) return error(err).dump();
    Json pts = Json::array(), walls = Json::array();
    for (const auto& p : fp->pts) pts.push_back(fmt_point(shell, p.first, p.second));
    for (std::size_t i = 0; i < fp->pts.size(); ++i) {
        const auto& p = fp->pts[i];
        const auto& q = fp->pts[(i + 1) % fp->pts.size()];
        Json seg = Json::object();
        seg["from"] = fmt_point(shell, p.first, p.second);
        seg["to"] = fmt_point(shell, q.first, q.second);
        walls.push_back(std::move(seg));
    }
    out["points"] = std::move(pts);
    out["walls"] = std::move(walls);
    out["width"] = display(shell, fp->width);
    out["depth"] = display(shell, fp->depth);
    out["area_m2"] = fp->area;
    out["area_sqft"] = fp->area / 0.09290304;
    out["off_target_percent"] = (fp->area - spec.area) / spec.area * 100.0;
    if (!fp->note.empty()) out["note"] = fp->note;
    out["draw"] = "one draw_wall per entry of `walls`, coordinates exactly as given";
    return out.dump();
}


struct WorldSeg { double ax, ay, bx, by; };

std::vector<WorldSeg> world_segments(Shell& shell) {
    const auto& canvas = shell.canvas();
    std::vector<WorldSeg> out;
    for (const auto& s : canvas.boundary_segments()) {
        WorldSeg w{};
        gcm::to_world(canvas.volume(), s.a, w.ax, w.ay);
        gcm::to_world(canvas.volume(), s.b, w.bx, w.by);
        if (std::hypot(w.bx - w.ax, w.by - w.ay) > 1e-9) out.push_back(w);
    }
    return out;
}

double ray_hit(const std::vector<WorldSeg>& segs, double x, double y,
               double dx, double dy) {
    double best = 1e300;
    for (const auto& s : segs) {
        const double ex = s.bx - s.ax, ey = s.by - s.ay;
        const double den = dx * ey - dy * ex;
        if (std::fabs(den) < 1e-15) continue;
        const double t = ((s.ax - x) * ey - (s.ay - y) * ex) / den;
        const double u = ((s.ax - x) * dy - (s.ay - y) * dx) / den;
        if (t > 1e-9 && u >= -1e-9 && u <= 1.0 + 1e-9) best = std::min(best, t);
    }
    return best;
}

double loop_area(const gcm::WorkingVolume& v, const gdoc::Loop& loop) {
    double sum = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        double ax, ay, bx, by;
        gcm::to_world(v, loop[i], ax, ay);
        gcm::to_world(v, loop[(i + 1) % loop.size()], bx, by);
        sum += ax * by - bx * ay;
    }
    return std::fabs(sum) * 0.5;
}

double loop_perimeter(const gcm::WorkingVolume& v, const gdoc::Loop& loop) {
    double sum = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        double ax, ay, bx, by;
        gcm::to_world(v, loop[i], ax, ay);
        gcm::to_world(v, loop[(i + 1) % loop.size()], bx, by);
        sum += std::hypot(bx - ax, by - ay);
    }
    return sum;
}

std::string run_measure_span(Shell& shell, const Args& a, Json& out) {
    double x, y;
    if (!parse_point(shell, point_text(a, "at"), x, y))
        return error("at reads as \"x,y\" in display units").dump();
    const auto segs = world_segments(shell);
    if (segs.empty()) return error("the drawing has no geometry to measure against").dump();

    const std::string given = a.str("direction");
    const double kPi = 3.14159265358979323846;
    double best = 1e300, best_deg = 0, best_a = 0, best_b = 0;
    const auto probe = [&](double deg) {
        const double r = deg * kPi / 180.0;
        const double dx = std::cos(r), dy = std::sin(r);
        const double f = ray_hit(segs, x, y, dx, dy);
        const double b = ray_hit(segs, x, y, -dx, -dy);
        if (f > 1e299 || b > 1e299) return;
        if (f + b < best) { best = f + b; best_deg = deg; best_a = f; best_b = b; }
    };
    if (!given.empty()) probe(std::atof(given.c_str()));
    else for (int deg = 0; deg < 180; ++deg) probe(double(deg));

    if (best > 1e299)
        return error(given.empty()
            ? "no closed span through that point -- it is outside the geometry, "
              "or the walls do not enclose it"
            : "nothing on both sides along that direction").dump();
    out["clear_width"] = display(shell, best);
    out["clear_width_text"] = fmt_length(shell, best);
    out["clear_width_mm"] = best * 1000.0;
    out["clear_width_in"] = best / 0.0254;
    out["direction_deg"] = best_deg;
    const double kx = std::cos(best_deg * kPi / 180.0), ky = std::sin(best_deg * kPi / 180.0);
    out["side_a"] = fmt_point(shell, x + kx * best_a, y + ky * best_a);
    out["side_b"] = fmt_point(shell, x - kx * best_b, y - ky * best_b);
    out["measured"] = given.empty() ? "narrowest span through the point"
                                    : "along the given direction";
    return out.dump();
}

std::string run_measure_room(Shell& shell, const Args& a, Json& out) {
    double x, y;
    if (!parse_point(shell, point_text(a, "at"), x, y))
        return error("at reads as \"x,y\" in display units").dump();
    const auto& canvas = shell.canvas();
    const auto loops = gdoc::trace_region(canvas.boundary_segments(),
                                          gcm::to_lattice(canvas.volume(), x, y));
    if (!loops.ok() || loops.loops.empty()) {
        Json e = error("no closed room around that point: " +
                       loops.message(canvas.volume()));
        double gx, gy;
        gcm::to_world(canvas.volume(), loops.gap_at, gx, gy);
        e["gap_at"] = fmt_point(shell, gx, gy);
        e["next"] = "the walls do not close around that point -- an opening, a "
                    "hidden layer, or geometry drawn short. measure_span at the "
                    "same point still gives the clear width across it.";
        return e.dump();
    }

    double net = loop_area(canvas.volume(), loops.loops.front());
    for (std::size_t i = 1; i < loops.loops.size(); ++i)
        net -= loop_area(canvas.volume(), loops.loops[i]);
    net = std::max(net, 0.0);
    out.update(area_json(shell, net));
    const double perim = loop_perimeter(canvas.volume(), loops.loops.front());
    out["perimeter"] = display(shell, perim);
    out["perimeter_text"] = fmt_length(shell, perim);
    out["islands"] = static_cast<gcm::i64>(loops.loops.size() - 1);

    double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
    for (const auto& p : loops.loops.front()) {
        double px, py;
        gcm::to_world(canvas.volume(), p, px, py);
        lo_x = std::min(lo_x, px); hi_x = std::max(hi_x, px);
        lo_y = std::min(lo_y, py); hi_y = std::max(hi_y, py);
    }
    out["bounds_low"] = fmt_point(shell, lo_x, lo_y);
    out["bounds_high"] = fmt_point(shell, hi_x, hi_y);
    out["size_text"] = fmt_length(shell, hi_x - lo_x) + " x " + fmt_length(shell, hi_y - lo_y);
    out["corners"] = static_cast<gcm::i64>(loops.loops.front().size());
    Json span = Json::object();
    Args at_centre;
    at_centre.v["at"] = fmt_point(shell, (lo_x + hi_x) * 0.5, (lo_y + hi_y) * 0.5);
    at_centre.ok = true;
    const auto probed = gai::parse_json(run_measure_span(shell, at_centre, span));
    if (probed && probed->is_object() && probed->contains("clear_width_text"))
        out["narrowest_at_centre"] = (*probed)["clear_width_text"];
    return out.dump();
}

std::string run_list_text(Shell& shell, const Args& a, Json& out) {
    const auto& canvas = shell.canvas();
    std::string needle = a.str("contains");
    for (char& c : needle) c = char(std::tolower(static_cast<unsigned char>(c)));
    const std::string layer = a.str("layer");
    const int limit = std::max(1, int(a.num("limit", 200)));
    Json arr = Json::array();
    const auto flatten = [](const std::string& raw) {
        const auto parsed = gtext::parse_mtext(raw);
        std::string out;
        int paragraph = 0;
        for (const auto& run : parsed.runs) {
            if (run.paragraph != paragraph) { out += " / "; paragraph = run.paragraph; }
            out += run.text;
            if (run.stack != gtext::Stack::None) out += "/" + run.denominator;
        }
        return out.empty() ? raw : out;
    };
    const auto keep = [&](const std::string& raw, const std::string& lay,
                          double px, double py, const char* from) {
        if (int(arr.size()) >= limit) return;
        if (!layer.empty() && lay != layer) return;
        const std::string text = flatten(raw);
        if (!needle.empty()) {
            std::string hay = text;
            for (char& c : hay) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (hay.find(needle) == std::string::npos) return;
        }
        Json o = Json::object();
        o["text"] = text;
        o["at"] = fmt_point(shell, px, py);
        if (!lay.empty()) o["layer"] = lay;
        o["from"] = from;
        arr.push_back(std::move(o));
    };
    for (std::size_t i=0;i<canvas.texts().size();++i) {
        const auto& t=canvas.texts()[i];
        if (t.text.empty()) continue;
        double px, py;
        gcm::to_world(canvas.volume(), t.pos, px, py);
        const auto previous=arr.size();
        keep(t.text,t.layer,px,py,"note");
        if(arr.size()>previous) {
            auto& item=arr.back();item["text_id"]=i;item["height"]=fmt_length(shell,t.height);
            for(const auto* key:{"FONT","COLOR","BACKGROUND","HIGHLIGHT","BOLD","ITALIC"})item["style"][key]=canvas.text_property(t,key);
        }
    }
    for (const auto& r : canvas.blocks().references()) {
        double px, py;
        gcm::to_world(canvas.volume(), r.insertion, px, py);
        for (const auto& v : r.attributes)
            if (!v.value.empty()) keep(v.tag + ": " + v.value, r.layer, px, py, "block attribute");
        for (const auto& n : r.notes)
            if (!n.text.empty()) keep(n.text, n.layer, px, py, "block text");
    }
    out["text"] = std::move(arr);
    out["notes_total"] = static_cast<gcm::i64>(canvas.texts().size());
    return out.dump();
}

std::string run_list_blocks(Shell& shell, const Args& a, Json& out) {
    const auto& canvas = shell.canvas();
    const std::string want = a.str("name");
    const std::string layer = a.str("layer");
    const int limit = std::max(1, int(a.num("limit", 100)));
    std::map<std::string, int> tally;
    Json arr = Json::array();
    for (const auto& r : canvas.blocks().references()) {
        if (!want.empty() && r.name != want) continue;
        if (!layer.empty() && r.layer != layer) continue;
        ++tally[r.name];
        if (int(arr.size()) >= limit) continue;
        double px, py;
        gcm::to_world(canvas.volume(), r.insertion, px, py);
        Json o = Json::object();
        o["name"] = r.name;
        o["at"] = fmt_point(shell, px, py);
        o["layer"] = r.layer.empty() ? "0" : r.layer;
        o["rotation_deg"] = r.rotation * 180.0 / 3.14159265358979323846;
        o["scale"] = std::to_string(r.scale_x) + "," + std::to_string(r.scale_y);
        if (!r.attributes.empty()) {
            Json at = Json::object();
            for (const auto& v : r.attributes) at[v.tag] = v.value;
            o["attributes"] = std::move(at);
        }
        arr.push_back(std::move(o));
    }
    Json counts = Json::object();
    for (const auto& [name, n] : tally) counts[name] = n;
    out["counts_by_name"] = std::move(counts);
    out["insertions"] = std::move(arr);
    out["total"] = static_cast<gcm::i64>(canvas.blocks().references().size());
    return out.dump();
}

std::string run_count_objects(Shell& shell, Json& out) {
    const auto& canvas = shell.canvas();
    std::map<std::string, std::map<std::string, int>> by_layer;
    for (const auto& sh : canvas.shapes()) {
        const char* k = sh.kind == gsnap::ShapeKind::Segment ? "lines"
                      : sh.kind == gsnap::ShapeKind::Circle ? "circles"
                      : sh.kind == gsnap::ShapeKind::Arc ? "arcs" : "other";
        ++by_layer[sh.layer.empty() ? "0" : sh.layer][k];
    }
    for (const auto& r : canvas.blocks().references())
        ++by_layer[r.layer.empty() ? "0" : r.layer]["block insertions"];
    Json layers = Json::object();
    for (const auto& [layer, kinds] : by_layer) {
        Json k = Json::object();
        int total = 0;
        for (const auto& [kind, n] : kinds) { k[kind] = n; total += n; }
        k["total"] = total;
        layers[layer] = std::move(k);
    }
    out["by_layer"] = std::move(layers);
    out["notes"] = static_cast<gcm::i64>(canvas.texts().size());
    out["dimensions"] = static_cast<gcm::i64>(canvas.dimensions().size());
    out["hatches"] = static_cast<gcm::i64>(canvas.hatches().size());
    return out.dump();
}

std::string run_drawing_units(Shell& shell, Json& out) {
    const auto& canvas = shell.canvas();
    out["display_unit"] = gunit::name(canvas.display_unit());
    out["display_unit_suffix"] = gunit::suffix(canvas.display_unit());
    double lo_x, lo_y, hi_x, hi_y;
    if (drawing_extents(shell, lo_x, lo_y, hi_x, hi_y)) {
        Json each = Json::object();
        for (const auto u : {gunit::Length::Millimetre, gunit::Length::Centimetre,
                             gunit::Length::Metre, gunit::Length::Inch,
                             gunit::Length::Foot}) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%.4g x %.4g", gunit::from_metres(hi_x - lo_x, u),
                          gunit::from_metres(hi_y - lo_y, u));
            each[gunit::suffix(u)] = buf;
        }
        out["extents_in_each_unit"] = std::move(each);
        out["extents_now"] = fmt_length(shell, hi_x - lo_x) + " x " +
                             fmt_length(shell, hi_y - lo_y);
    }
    std::vector<double> radii;
    for (const auto& sh : canvas.shapes())
        if (sh.kind == gsnap::ShapeKind::Arc)
            radii.push_back(double(sh.radius) * canvas.volume().inv_scale());
    std::sort(radii.begin(), radii.end());
    if (!radii.empty()) {
        Json sample = Json::array();
        const std::size_t step = std::max<std::size_t>(1, radii.size() / 12);
        for (std::size_t i = 0; i < radii.size(); i += step)
            sample.push_back(fmt_length(shell, radii[i]));
        out["arc_radii_sample"] = std::move(sample);
        out["arc_radius_median"] = fmt_length(shell, radii[radii.size() / 2]);
        out["arc_count"] = static_cast<gcm::i64>(radii.size());
        out["reading"] = "arcs clustered near a door leaf (750-950 mm / 30-36 in) "
                         "mean the scale is right; a cluster ten or twelve times "
                         "that means the drawing is being read in the wrong unit";
    }
    out["fix"] = "run_command \"DWGUNITS ft\" (or in, mm, cm, m) rescales the "
                 "imported model when the units are wrong; the unit chip alone "
                 "only changes how numbers are displayed";
    return out.dump();
}

std::string run_measure_path(Shell& shell, const Args& a, Json& out) {
    const auto it = a.v.find("points");
    if (it == a.v.end() || !it->is_array() || it->size() < 2)
        return error("points is an array of at least two \"x,y\" strings").dump();
    double total = 0.0, px = 0, py = 0;
    Json legs = Json::array();
    for (std::size_t i = 0; i < it->size(); ++i) {
        const auto& e = (*it)[i];
        std::string text;
        if (e.is_string()) text = e.get<std::string>();
        else if (e.is_array() && e.size() == 2 && e[0].is_number() && e[1].is_number()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.10g,%.10g", e[0].get<double>(), e[1].get<double>());
            text = buf;
        }
        double x, y;
        if (!parse_point(shell, text, x, y))
            return error("point " + std::to_string(i + 1) + " does not read as \"x,y\"").dump();
        if (i) {
            const double leg = std::hypot(x - px, y - py);
            total += leg;
            legs.push_back(fmt_length(shell, leg));
        }
        px = x; py = y;
    }
    out["total"] = display(shell, total);
    out["total_text"] = fmt_length(shell, total);
    out["total_ft"] = total / 0.3048;
    out["total_m"] = total;
    out["legs"] = std::move(legs);
    return out.dump();
}

}

std::vector<LibraryHit> library_lookup(Shell& shell, const std::string& query,
                                       int k, bool* used_embeddings) {
    const std::string model = pick_embedding_model(shell);
    grag::Embedder emb;
    if (!model.empty())
        if (const auto client = shell.ai_client())
            emb = embedder_for(*client, model);
    const auto hits = library().lookup(query, k, emb ? model : std::string{}, emb,
                                       used_embeddings);
    std::vector<LibraryHit> out;
    out.reserve(hits.size());
    for (const auto& h : hits) out.push_back({h.file, h.text});
    return out;
}


gai::Json drawing_context(const Shell& shell) {
    const auto discipline=shell.drawing_discipline();
    const char* name=discipline==DrawingDiscipline::Architectural ? "architectural" :
                     discipline==DrawingDiscipline::Mechanical ? "mechanical" : "general";
    std::string guidance="The drawing discipline is "+std::string(name)+
        ". Discipline describes the document's purpose independently of its 2D or 3D view. "
        "Use set_drawing_discipline when the user establishes or changes that purpose. "
        "After a change, follow the returned discipline and guidance instead of the initial purpose-specific conventions. ";
    if(discipline==DrawingDiscipline::Architectural)
        guidance+="For walls, doors and windows, use draw_wall, draw_door and draw_window. "
            "A wall is a joined, closed 2D region with thickness and a hatchable core, not a "
            "collection of independent lines or overlapping capped rectangles. Supply connected "
            "wall centerlines; the wall tool joins their boundaries. Door and window tools cut "
            "host wall openings automatically; use snap_to_wall when aligning a placement. "
            "Use area_background for closed rooms, including rooms bounded by door thresholds, "
            "and plan_check to locate and fix gaps and crossings. Keep floor plans in 2D unless "
            "the user requests 3D construction. Generic line and curve tools remain appropriate "
            "for details, grids, annotations and non-wall geometry.";
    else if(discipline==DrawingDiscipline::Mechanical)
        guidance+="Use precise lines, arcs, circles and closed profiles for parts, with dimensions "
            "and constraints as needed. A closed or narrow profile is not automatically a building "
            "wall. Use area_background or hatching for closed sections and explicit solid operations "
            "when the user asks for 3D. Architectural objects are available when explicitly requested.";
    else
        guidance+="Use general drafting primitives without assuming a building or mechanical part. "
            "Establish the purpose from the user's request before choosing architectural conventions. "
            "When an object is intended to be a wall, door or window, use its semantic drawing tool.";
    return {{"drawing_discipline",name},{"drafting_guidance",guidance}};
}

std::vector<gai::ToolDefinition> registry() {
    using gai::ToolDefinition;
    std::vector<ToolDefinition> t;

    t.push_back(ToolDefinition::make("document_summary",
        "What is in the drawing: units, extents, object counts per kind and layer, "
        "the current selection size, mode, drawing discipline and its drafting guidance.",
        obj_schema({})));
    t.push_back(ToolDefinition::make("list_objects",
        "List geometry primitives with ids and exact coordinates. Wall boundary lines are "
        "parts of a joined wall region, not separate walls. object_type and compound_id "
        "identify semantic type and shared boundary grouping. "
        "Filter by kind (line|circle|arc), "
        "layer, and/or a rectangular region of the drawing (region_low/region_high "
        "corners as \"x,y\"); `limit` caps the answer (default 50).",
        obj_schema({{"kind", "line, circle or arc"},
                    {"layer", "only this layer"},
                    {"region_low", "lower-left corner of the area to read"},
                    {"region_high", "upper-right corner of the area to read"},
                    {"cell", "a read_grid cell like B3 (with the same cols/rows)"},
                    {"cols", "grid columns when using cell (default 8)"},
                    {"rows", "grid rows when using cell (default 6)"},
                    {"limit", "maximum objects to return"}})));
    t.push_back(ToolDefinition::make("read_grid",
        "Survey the drawing as a labelled grid: the extents divide into cells "
        "(columns A,B,C... left to right; rows 1,2,3... bottom to top) and each "
        "cell reports what it holds. Orient here, then list_objects with `cell` "
        "to read one cell in detail. Default 8 columns x 6 rows.",
        obj_schema({{"cols", "columns (default 8)"}, {"rows", "rows (default 6)"}})));
    t.push_back(ToolDefinition::make("object_info",
        "Everything about one object, by the id list_objects gave.",
        obj_schema({{"id", "the object id"}}, {"id"})));
    t.push_back(ToolDefinition::make("measure",
        "The exact distance between two points, in the display units.",
        obj_schema({{"from", kPoint}, {"to", kPoint}}, {"from", "to"})));
    t.push_back(ToolDefinition::make("measure_span",
        "CLEAR WIDTH at a point: the shortest wall-to-wall span through it, "
        "swept over every direction, so a corridor reads across and not along. "
        "This is the call for door clear width, corridor width, aisle width "
        "and any code minimum written as a clear dimension. Give `direction` "
        "in degrees to force one axis. Answers in the display unit and also in "
        "mm and inches, because code minimums are written in both.",
        obj_schema({{"at", "a point INSIDE the opening or corridor, \"x,y\""},
                    {"direction", "optional, degrees; omit to find the narrowest"}},
                   {"at"})));
    t.push_back(ToolDefinition::make("measure_room",
        "The ROOM around a point: net floor area (islands subtracted), "
        "perimeter, bounding size, and the narrowest span across it. Use for "
        "occupant load, area checks and turning-radius questions. Fails "
        "loudly when the walls do not close around the point, which is itself "
        "the finding.",
        obj_schema({{"at", "a point inside the room, \"x,y\""}}, {"at"})));
    t.push_back(ToolDefinition::make("measure_path",
        "The length of a ROUTE through a list of points -- travel distance to "
        "an exit, a run of pipe, a path length. Pass `points` as a JSON array "
        "of \"x,y\" strings.",
        [] {
            Json s = Json::object();
            s["type"] = "object";
            s["properties"]["points"]["type"] = "array";
            s["properties"]["points"]["items"]["type"] = "string";
            s["properties"]["points"]["description"] = "the route, in order";
            s["required"] = Json::array({"points"});
            return s;
        }()));
    t.push_back(ToolDefinition::make("list_text",
        "Every WORD on the drawing: notes, keynotes, room names, schedules, "
        "title-block fields and block attribute tags, each with its position "
        "and layer. Filter with `contains` (case-insensitive) or `layer`. "
        "Read this before saying a specification is not on the drawing -- "
        "equipment schedules and door specs are text, not geometry.",
        obj_schema({{"contains", "only text containing this"},
                    {"layer", "only this layer"},
                    {"limit", "maximum entries (default 200)"}})));
    t.push_back(ToolDefinition::make("list_symbols",
        "List the built-in 2D symbol catalog and counts of top-level symbols/blocks and semantic door/window/frame/hinge groups in the active drawing. Legends and xrefs are excluded.", obj_schema({})));
    t.push_back(ToolDefinition::make("draw_symbol",
        "Place a movable 2D symbol block from list_symbols. Uses the current layer; size defaults to 500mm. Rotate or move it like any block.",
        obj_schema({{"symbol","north, section, elevation, datum, direction, or both_directions"},
                    {"at",kPoint},{"size","symbol size with optional unit suffix (default 500mm)"},
                    {"rotation","rotation in degrees (default 0)"}}, {"symbol","at"})));
    t.push_back(ToolDefinition::make("draw_arrow",
        "Place a standalone movable arrow from tail to tip. It is a direction symbol block, counted in the symbol legend.",
        obj_schema({{"from",kPoint},{"to",kPoint},{"direction","forward (default) or both"}}, {"from","to"})));
    t.push_back(ToolDefinition::make("symbol_legend",
        "Generate a movable symbol legend with samples, names and counts of top-level blocks and semantic door/window/frame/hinge groups currently used. Updates existing legends in place, preserving their transforms. Existing legends update automatically with symbol edits in the same undo transaction; this tool can also recompute them. Excludes legends and xrefs. Requires at for initial placement.",
        obj_schema({{"at",kPoint},{"scale","initial legend scale (default 1); existing placement and scale are retained"}})));
    t.push_back(ToolDefinition::make("list_blocks",
        "The block INSERTIONS: how many of each symbol, where, on which "
        "layer, with their attribute tags. Counting the doors on a layer, the "
        "fixtures in a restroom or the exits marked on a plan is this call.",
        obj_schema({{"name", "only this block"},
                    {"layer", "only this layer"},
                    {"limit", "maximum insertions listed (default 100)"}})));
    t.push_back(ToolDefinition::make("count_objects",
        "The tally: how many lines, arcs, circles and insertions sit on each "
        "layer, plus notes, dimensions and hatches. Orient here before "
        "reading a drawing somebody else drew.",
        obj_schema({})));
    t.push_back(ToolDefinition::make("drawing_units",
        "Is this drawing being read at the right SCALE? Reports the display "
        "unit, the extents written out in every candidate unit, and the arc "
        "radii -- a floor plan's door swings are its own ruler. Call this "
        "FIRST when measurements from an imported file look implausible or "
        "inconsistent, before reporting that the drawing cannot be measured.",
        obj_schema({})));
    t.push_back(ToolDefinition::make("selection_info",
        "What the user currently has selected.", obj_schema({})));
    t.push_back(ToolDefinition::make("help",
        "How a command works: its description and the arguments it prompts for. "
        "Use before run_command when unsure of syntax.",
        obj_schema({{"command", "the command name, e.g. OFFSET"}}, {"command"})));

    t.push_back(ToolDefinition::make("set_drawing_discipline",
        "Set the document purpose independently of 2D/3D mode. Architectural plans use semantic "
        "walls, doors and windows; mechanical drawings use part profiles. General makes no assumption. "
        "The choice is saved in the document and shared with the UI, assistant and MCP.",
        [] {
            auto schema=obj_schema({{"discipline","architectural, mechanical or general"}},{"discipline"});
            schema["properties"]["discipline"]["enum"]={"architectural","mechanical","general"};
            return schema;
        }()));

    t.push_back(ToolDefinition::make("draw_line",
        "Draw a line for mechanical part edges or general drafting details. In architecture, "
        "use this for grids, annotations and details, never to outline an intended wall: use "
        "draw_wall even for a 2D floor plan.",
        obj_schema({{"from", kPoint}, {"to", kPoint}}, {"from", "to"})));
    t.push_back(ToolDefinition::make("draw_polyline",
        "Draw connected line segments through the given points, as JSON array "
        "of \"x,y\" strings in `points`. Use for mechanical profiles and general details. "
        "For an architectural wall use draw_wall, not a polyline wall outline.",
        [] {
            Json s = Json::object();
            s["type"] = "object";
            s["properties"]["points"]["type"] = "array";
            s["properties"]["points"]["items"]["type"] = "string";
            s["properties"]["points"]["description"] = "the points, in order";
            s["required"] = Json::array({"points"});
            return s;
        }()));
    t.push_back(ToolDefinition::make("draw_rect",
        "Draw a rectangle for mechanical profiles or general drafting. For an architectural "
        "wall use draw_wall, not an overlapping capped rectangle; 2D mode does not change this rule.",
        obj_schema({{"corner_a", kPoint}, {"corner_b", kPoint}}, {"corner_a", "corner_b"})));
    t.push_back(ToolDefinition::make("draw_circle",
        "Draw a circle from its centre and radius (radius in display units, "
        "or with a unit suffix).",
        obj_schema({{"centre", kPoint}, {"radius", "the radius"}}, {"centre", "radius"})));
    t.push_back(ToolDefinition::make("area_background",
        "Fill or recolor a closed room at a seed point, bridging door thresholds. Available to the UI and MCP. Colors use #RRGGBB; gap defaults to 10mm.",
        obj_schema({{"at",kPoint},{"color","#RRGGBB"},{"gap","maximum boundary gap, e.g. 10mm"},
                    {"opacity","0 to 100 percent (default 35)"}},{"at","color"})));
    t.push_back(ToolDefinition::make("detect_walls",
        "Recognize closed A-WALL outlines and narrow untyped wall outlines as hatchable wall regions.",obj_schema({})));
    t.push_back(ToolDefinition::make("draw_wall",
        "Draw a semantic 2D wall region from a centerline, with thickness and a hatched core "
        "on A-WALL. Connected wall runs join into closed regions without internal end caps. "
        "Each connected core keeps one automatic hatch and retains its existing hatch style. "
        "Types: exterior (165mm default) or interior (115mm); specify thickness to match "
        "the intended construction. Door and window tools cut openings automatically. "
        "Use this tool for architectural walls instead of independent lines. It does not create 3D solids.",
        obj_schema({{"from", kPoint}, {"to", kPoint},
                    {"type", "exterior (default) or interior"},
                    {"thickness", "finished thickness, overriding the type"}},
                   {"from", "to"})));
    t.push_back(ToolDefinition::make("edit_doors",
        "Edit every selected swing door atomically. Select door objects first. Set flow arrows, "
        "reverse swing side, or swap hinge/latch while preserving width and walls. "
        "Hosted reversals require matching jamb faces; obstructed or unsupported edits reject the entire batch. "
        "One undo restores all edited doors.",
        obj_schema({{"flow", "forward (toward swing), reverse (away), both or off; omitted leaves arrows unchanged"},
                    {"swing", "reverse to flip swing side; omitted leaves it unchanged"},
                    {"hinge", "reverse to swap hinge/latch; omitted leaves it unchanged"}})));
    t.push_back(ToolDefinition::make("draw_door",
        "Place a DOOR in a wall opening, on layer A-DOOR. hinge and latch "
        "are the two sides of the opening ON the wall centerline. kind "
        "swing (default) draws the leaf and its quarter-circle arc; kind "
        "rollup draws a GARAGE/overhead door -- panel in the opening and "
        "tracks running back, NO swing arc, use it for every garage door. "
        "Jamb framing (jacks + king, doubled for wide openings per IRC "
        "R602.7) is drawn automatically. Nearby wall hosts are detected by default "
        "and cut through their measured thickness; the swing hinge is placed on "
        "the swing-side wall face. snap_to_wall=true requires a host, while false "
        "keeps a standalone symbol without cutting walls. Wall-obstructed hosted "
        "swings are rejected before drawing. The requested width is preserved. "
        "The response reports how many host faces were actually cut.",
        obj_schema({{"hinge", kPoint}, {"latch", kPoint},
                    {"swing", "left (default) or right"},
                    {"kind", "swing (default) or rollup (garage/overhead)"},
                    {"snap_to_wall", "omitted detects a nearby wall automatically; true requires a wall host; false preserves standalone coordinates and does not cut walls"},
                    {"wall_thickness", "standalone frame thickness (default 165mm); hosted openings use the measured wall thickness"},
                    {"jacks", "jack studs per side (default: 1, 2 when >= 6ft)"},
                    {"component", "door (default), frame or hinge"},
                    {"framing", "wood (default) or none"}},
                   {"hinge", "latch"})));
    t.push_back(ToolDefinition::make("draw_window",
        "Place a WINDOW in a wall: frame across the wall, two glass lines, "
        "and an ANSI36 glass hatch (never a wall's ANSI31), on layer "
        "A-GLAZ, with jamb framing (jacks + king, doubled when wide, IRC "
        "R602.7) on A-WALL. from and to are the jambs ON the wall "
        "centerline. Nearby wall hosts are detected and cut through their measured thickness. "
        "snap_to_wall=true requires a host; false places a standalone symbol without cutting walls.",
        obj_schema({{"from", kPoint}, {"to", kPoint},
                    {"snap_to_wall", "omitted detects a nearby wall automatically; true requires a wall host; false preserves standalone coordinates and does not cut walls"},
                    {"wall_thickness", "standalone frame thickness (default 165mm); hosted openings use the measured wall thickness"},
                    {"jacks", "jack studs per side (default: 1, 2 when >= 6ft)"},
                    {"framing", "wood (default) or none"}},
                   {"from", "to"})));
    t.push_back(ToolDefinition::make("merge_overlaps",
        "AUTO CLEANUP: merge duplicated/overlapping collinear lines "
        "(double-drawn walls become one) and cut A-WALL faces across every "
        "door and window opening, capping the jambs -- so no wall line "
        "blocks an entrance or a window. Run it once after placing the "
        "doors and windows, and any time geometry was drawn twice.",
        obj_schema({{"what", "all (default), duplicates, or openings"},
                    {"tolerance", "merge tolerance (default 2mm)"}}, {})));
    t.push_back(ToolDefinition::make("label_room",
        "Label a room at its centre with its NAME (and area if given), "
        "centred, plan-scale text on A-ANNO-DIMS. Every room on a plan "
        "carries its name.",
        obj_schema({{"at", "the room's centre as \"x,y\""},
                    {"name", "e.g. LIVING, BEDROOM 2, KITCHEN"},
                    {"area", "optional, e.g. \"18.2 m2\" or \"196 sqft\""},
                    {"height", "text height (optional)"}},
                   {"at", "name"})));

    t.push_back(ToolDefinition::make("add_dimension",
        "Place a persistent aligned dimension measuring between two points, "
        "with arrows and text.",
        obj_schema({{"from", kPoint}, {"to", kPoint}}, {"from", "to"})));
    t.push_back(ToolDefinition::make("add_text",
        "Place a note at a point.",
        obj_schema({{"at", kPoint}, {"text", "the note"},
                    {"height", "text height in display units (optional)"}},
                   {"at", "text"})));
    {
        auto schema=obj_schema({{"property","FONT, HEIGHT, COLOR, BACKGROUND, HIGHLIGHT, BOLD or ITALIC"},
            {"value","Installed font name or Default; height in display units; #RRGGBB color or NONE/BYLAYER; ON/OFF for bold and italic"}}, {"property","value"});
        schema["properties"]["text_ids"]=Json{{"type","array"},{"description","Optional text indices from list_text; otherwise formats the current text selection"},{"items",Json{{"type","integer"},{"minimum",0}}}};
        t.push_back(ToolDefinition::make("set_text_style",
            "Format editable notes through the same controls as the text style bar. Sets one appearance property on all selected notes, or specified text_ids. Font changes render the actual installed face. Background colors the whole note; highlight colors its text runs. Each call is undoable.",schema));
    }
    t.push_back(ToolDefinition::make("list_hatches",
        "List editable hatches and area backgrounds with hatch_ids, pattern, scale, angle, color and selection. Also lists registered hatch patterns for architecture materials and mechanical sections.",obj_schema({})));
    {
        auto schema=obj_schema({{"property","PATTERN, COLOR, SCALE or ANGLE"},
            {"value","Registered pattern name; BYLAYER, ACI index 1-255 or #RRGGBB; positive pattern scale; angle in degrees"}}, {"property","value"});
        schema["properties"]["hatch_ids"]=Json{{"type","array"},{"description","Optional hatch indices from list_hatches; otherwise edits current hatch selection"},{"items",Json{{"type","integer"},{"minimum",0}}}};
        t.push_back(ToolDefinition::make("set_hatch_style",
            "Change hatch appearance through the same controls as the Hatch style bar. Updates the existing fill without changing its boundary or architectural/mechanical object type. Works with selected hatches or explicit hatch_ids from list_hatches. Each call is undoable and saved/exported.",schema));
    }
    t.push_back(ToolDefinition::make("plan_check",
        "Run the PLAN CHECK: rule-based review of the drawing the way a "
        "plan reviewer reads it -- structure on A-WALL with hatched cores, "
        "doors with swings and an entrance-width leaf, egress widths (IRC "
        "R311.2), glazing, room labels, dimensions. Returns the findings. "
        "ALWAYS run this after drawing a plan, and fix every error before "
        "declaring the plan done.",
        obj_schema({})));
    t.push_back(ToolDefinition::make("save_plan_rules",
        "Write the jurisdiction's numeric thresholds into the PLAN CHECK "
        "rules overlay, so the checker enforces the local code and cites "
        "it. Use after fetching a jurisdiction's code: extract the numbers "
        "and pass them in millimetres with the code section as source.",
        obj_schema({{"source", "citation, e.g. \"Fresno CA - CRC R311.2\""},
                    {"entrance_min_mm", "minimum entrance door leaf, mm"},
                    {"egress_door_min_mm", "minimum egress door leaf, mm"}},
                   {"source"})));
    t.push_back(ToolDefinition::make("erase_selected",
        "Erase everything currently selected -- shapes, labels, insertions, "
        "and the dimensions and hatches attached to them. ORDER MATTERS: "
        "select and erase BEFORE drawing anything new; a selection made "
        "before drawing is refused. The user is asked before it runs. "
        "There is no DELETE or ESC command; this is the eraser.",
        obj_schema({})));
    t.push_back(ToolDefinition::make("select",
        "Change the selection: `what` is all|none|kind:<line|circle|arc>|layer:<name>.",
        obj_schema({{"what", "all, none, kind:line, layer:Walls, ..."}}, {"what"})));
    t.push_back(ToolDefinition::make("move_selected",
        "Move the selected objects by a displacement.",
        obj_schema({{"dx", "x displacement in display units"},
                    {"dy", "y displacement in display units"}}, {"dx", "dy"})));
    t.push_back(ToolDefinition::make("view_screenshot",
        "Take a screenshot of what the user currently sees -- canvas, panels "
        "and all -- and receive it as an image to look at. Needs a vision "
        "model; use for questions about appearance and layout.",
        obj_schema({})));
    t.push_back(ToolDefinition::make("web_search",
        "Search the web. Returns titles, links and snippets; follow up with "
        "fetch_url to read a result in full.",
        obj_schema({{"query", "what to search for"}}, {"query"})));
    t.push_back(ToolDefinition::make("fetch_url",
        "Fetch a web page (https) and return its readable text, tags "
        "stripped, truncated to ~8000 characters. When the page holds a "
        "standard, code section or reference worth keeping, ALWAYS pass "
        "save_title: the full text then lands in the local standards "
        "library, indexed, in this same call.",
        obj_schema({{"url", "the page address, https://..."},
                    {"save_title", "archive the page into the standards "
                                   "library under this title (do this for "
                                   "codes and standards)"}},
                   {"url"})));
    t.push_back(ToolDefinition::make("calc",
        "Deterministic arithmetic with units. Use for EVERY product, area, "
        "unit conversion or derived coordinate instead of computing in your "
        "head: e.g. {\"expr\":\"sqrt(2800 ft2 / 1.4)\"} or \"40 ft * 70 ft\". "
        "Supports + - * / sqrt(), lengths (mm cm m in ft yd) and areas "
        "(m2 sqft ft2 acre ha). Lengths come back in the drawing's display "
        "units, ready to use in points.",
        obj_schema({{"expr", "the expression"}}, {"expr"})));
    t.push_back(ToolDefinition::make("plan_footprint",
        "Plan a building footprint for a target floor area: a closed, "
        "grid-snapped polygon (rectangle, L, T or U) with the wall list "
        "ready to draw, placed on the lot when its size is given. ALWAYS "
        "start an envelope here instead of working out dimensions yourself, "
        "then call draw_wall once per returned wall, coordinates as given.",
        obj_schema({{"area", "target area with unit, e.g. \"2800 sqft\""},
                    {"style", "rectangle | l | t | u (default rectangle)"},
                    {"aspect", "width/depth hint (default 1.4)"},
                    {"lot_width", "lot width, with unit (optional)"},
                    {"lot_depth", "lot depth, with unit (optional)"},
                    {"setback_front", "front setback (optional)"},
                    {"setback_side", "side setback (optional)"},
                    {"setback_rear", "rear setback (optional)"},
                    {"grid", "snap increment (default 6in)"}},
                   {"area"})));

    t.push_back(ToolDefinition::make("search_standards",
        "Search the LOCAL standards library: building codes, architecture "
        "standards and office norms saved for this user. Check here FIRST "
        "for any code or standards question. Empty or missing? Ask the user "
        "where the project is located, find the jurisdiction's codes with "
        "web_search/fetch_url, and keep them with save_standard.",
        obj_schema({{"query", "what to look up"},
                    {"k", "passages to return (default 4)"}}, {"query"})));
    t.push_back(ToolDefinition::make("save_standard",
        "Save a standard, code excerpt or reference into the local library "
        "for every future question. Use after finding relevant standards "
        "online for the project's location.",
        obj_schema({{"title", "what this is, e.g. \"IBC 2021 stair requirements\""},
                    {"text", "the content to keep"},
                    {"source", "where it came from (a URL, optional)"}},
                   {"title", "text"})));

    t.push_back(ToolDefinition::make("run_command",
        "Run ANY GLCAD command exactly as a user would type it -- the whole "
        "product is here: LAYER, OFFSET, FILLET, GROUPSEL, EXTRUDE, PLOTSCALE, "
        "ZOOMEXTENTS, UNDO... Ask `help` for syntax first when unsure. "
        "Destructive commands pause for the user's confirmation.",
        obj_schema({{"command", "the full command line, e.g. \"PLOTSCALE 1:50\""}},
                   {"command"})));
    return t;
}

bool needs_confirmation(const gai::ToolCall& call) {
    if (call.name() == "run_command") return destructive_command(command_word(call));
    return call.name() == "erase_selected";
}

bool pure(const gai::ToolCall& call) {
    const std::string n = call.name();
    return n == "web_search" || n == "fetch_url" || n == "search_standards" ||
           n == "save_standard" || n == "save_plan_rules" || n == "calc" ||
           n == "plan_footprint";
}

std::string describe(const gai::ToolCall& call) {
    if (call.name() == "run_command") {
        const Args a = read_args(call);
        return "> " + a.str("command");
    }
    std::string args = call.arguments();
    if (args.size() > 120) args = args.substr(0, 117) + "...";
    return "> " + call.name() + " " + args;
}

namespace {

std::string execute_call(Shell& shell, const gai::ToolCall& call) {
    const std::string name = call.name();
    const Args a = read_args(call);
    if (!a.ok) return error("arguments did not parse as JSON").dump();

    Json out = Json::object();
    const auto done = [&](Json v) { return v.dump(); };

    if (name == "web_search") return run_web_search(a.str("query"));
    if (name == "fetch_url")
        return run_fetch_url(shell, a.str("url"), a.str("save_title"));
    if (name == "search_standards")
        return run_search_standards(shell, a.str("query"),
                                    std::atoi(a.str("k", "4").c_str()));
    if (name == "save_standard")
        return run_save_standard(shell, a.str("title"), a.str("text"), a.str("source"));
    if (name == "save_plan_rules") return run_save_plan_rules(a);
    if (name == "calc") return run_calc(shell, a, out);
    if (name == "plan_footprint") return run_plan_footprint(shell, a, out);

    if(name=="list_hatches") {
        const auto& canvas=shell.canvas();out["hatches"]=Json::array();out["patterns"]=Json::array();
        for(const auto& pattern:shell.canvas().patterns().all())out["patterns"].push_back(Json{{"name",pattern.name},{"description",pattern.description}});
        for(std::size_t i=0;i<canvas.hatches().size();++i) {
            const auto& h=canvas.hatches()[i];const auto& color=h.attrs.color;
            std::string text="BYLAYER";
            if(color.kind==gdoc::Color::Kind::Index)text=std::to_string(color.index);
            else if(color.kind==gdoc::Color::Kind::True){char hex[8];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",color.r,color.g,color.b);text=hex;}
            out["hatches"].push_back(Json{{"hatch_id",i},{"pattern",h.style.name},{"scale",h.style.scale},{"angle",h.style.angle},
                {"color",text},{"layer",h.layer},{"area_background",h.area_background},{"orphaned",h.orphaned},
                {"selected",std::find(canvas.selected_hatches().begin(),canvas.selected_hatches().end(),i)!=canvas.selected_hatches().end()}});
        }
        return done(out);
    }
    if(name=="set_hatch_style") {
        if(shell.read_only())return done(error("The active document is read-only"));
        auto& canvas=shell.canvas();auto selected=canvas.selected_hatches();
        if(a.v.contains("hatch_ids")) {
            if(!a.v["hatch_ids"].is_array())return done(error("hatch_ids must be an array of indices from list_hatches"));
            selected.clear();
            for(const auto& id:a.v["hatch_ids"]) {
                if(!id.is_number_integer() || id.get<gcm::i64>()<0 || id.get<gcm::u64>()>=canvas.hatches().size())return done(error("Unknown hatch index"));
                const auto index=id.get<std::size_t>();if(std::find(selected.begin(),selected.end(),index)==selected.end())selected.push_back(index);
            }
        }
        if(selected.empty())return done(error("Click inside a hatch or provide hatch_ids from list_hatches"));
        if(!canvas.set_hatch_property(selected,a.str("property"),a.str("value")))return done(error("Invalid hatch property or value; use registered patterns, positive scale, finite angle, or BYLAYER/ACI/#RRGGBB color"));
        canvas.set_selected_hatches(selected);out["updated"]=selected.size();out["property"]=a.str("property");return done(out);
    }
    if(name=="set_text_style") {
        if(shell.read_only())return done(error("The active document is read-only"));
        auto& canvas=shell.canvas();
        auto selected=canvas.selected_texts();
        if(a.v.contains("text_ids")) {
            if(!a.v["text_ids"].is_array())return done(error("text_ids must be an array of text indices"));
            selected.clear();
            for(const auto& id:a.v["text_ids"]) {
                if(!id.is_number_integer() || id.get<gcm::i64>()<0 || id.get<gcm::u64>()>=canvas.texts().size())return done(error("Unknown text index"));
                selected.push_back(id.get<std::size_t>());
            }
        }
        if(selected.empty())return done(error("Select text or provide text_ids from list_text"));
        std::string property=a.str("property");for(auto& c:property)c=char(std::toupper(static_cast<unsigned char>(c)));
        if(property!="FONT" && property!="HEIGHT" && property!="COLOR" && property!="BACKGROUND" && property!="HIGHLIGHT" && property!="BOLD" && property!="ITALIC")return done(error("Unsupported text style property"));
        canvas.set_selected_texts(selected);
        if(!shell.set_selection_property(property,a.str("value")))return done(error("Invalid text style value or unavailable font"));
        out["updated"]=selected.size();out["property"]=property;return done(out);
    }
    if (name == "set_drawing_discipline") {
        if(shell.read_only()) return done(error("The active document is read-only"));
        const auto value=a.str("discipline");
        if(value!="architectural" && value!="mechanical" && value!="general")
            return done(error("discipline must be architectural, mechanical or general"));
        shell.set_drawing_discipline(value=="architectural" ? DrawingDiscipline::Architectural :
                                     value=="mechanical" ? DrawingDiscipline::Mechanical : DrawingDiscipline::General);
        return done(drawing_context(shell));
    }

    if (name == "document_summary") {
        out=drawing_context(shell);
        const auto& canvas = shell.canvas();
        out["units"] = gunit::name(canvas.display_unit());
        Json counts = Json::object();
        double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
        for (const auto& sh : canvas.shapes()) {
            const char* k = sh.kind == gsnap::ShapeKind::Segment ? "lines"
                          : sh.kind == gsnap::ShapeKind::Circle ? "circles"
                          : sh.kind == gsnap::ShapeKind::Arc ? "arcs" : "other";
            counts[k] = (counts.contains(k) ? counts[k].get<gcm::i64>() : 0) + 1;
            double x, y;
            gcm::to_world(canvas.volume(), sh.a, x, y);
            lo_x = std::min(lo_x, x); hi_x = std::max(hi_x, x);
            lo_y = std::min(lo_y, y); hi_y = std::max(hi_y, y);
        }
        counts["notes"] = static_cast<gcm::i64>(canvas.texts().size());
        counts["dimensions"] = static_cast<gcm::i64>(canvas.dimensions().size());
        counts["block_insertions"] = static_cast<gcm::i64>(canvas.blocks().references().size());
        counts["solid_bodies"] = static_cast<gcm::i64>(shell.modeller().bodies().size());
        out["counts"] = std::move(counts);
        if (lo_x < hi_x) {
            out["extents_low"] = fmt_point(shell, lo_x, lo_y);
            out["extents_high"] = fmt_point(shell, hi_x, hi_y);
            out["extents_size"] = fmt_length(shell, hi_x - lo_x) + " x " +
                                  fmt_length(shell, hi_y - lo_y);
        }
        Json layers = Json::array();
        for (const auto& l : shell.layers().all()) layers.push_back(l.name);
        out["layers"] = std::move(layers);
        out["selected"] = static_cast<gcm::i64>(shell.canvas().selection().size());
        out["mode"] = shell.mode() == Mode::Sketch ? "sketch"
                    : shell.mode() == Mode::Solid ? "solid" : "drawing";
        return done(out);
    }
    if (name == "list_objects") {
        const auto& canvas = shell.canvas();
        const std::string kind = a.str("kind");
        const std::string layer = a.str("layer");
        const int limit = int(a.num("limit", 50));
        bool have_region = false;
        double rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
        if (!a.str("cell").empty()) {
            const int cols = std::clamp(int(a.num("cols", 8)), 1, 26);
            const int rows = std::clamp(int(a.num("rows", 6)), 1, 50);
            if (!cell_box(shell, a.str("cell"), cols, rows, rx0, ry0, rx1, ry1))
                return done(error("cell names read like B3, within the grid"));
            have_region = true;
        } else if (!a.str("region_low").empty() || !a.str("region_high").empty()) {
            if (!parse_point(shell, point_text(a, "region_low"), rx0, ry0) ||
                !parse_point(shell, point_text(a, "region_high"), rx1, ry1))
                return done(error("region corners read as \"x,y\""));
            if (rx1 < rx0) std::swap(rx0, rx1);
            if (ry1 < ry0) std::swap(ry0, ry1);
            have_region = true;
        }
        Json arr = Json::array();
        for (std::size_t i = 0; i < canvas.shapes().size() &&
                                int(arr.size()) < std::max(1, limit); ++i) {
            const auto& sh = canvas.shapes()[i];
            if (!layer.empty() && sh.layer != layer) continue;
            if (have_region) {
                double ax, ay, bx, by;
                gcm::to_world(canvas.volume(), sh.a, ax, ay);
                gcm::to_world(canvas.volume(), sh.b, bx, by);
                const double r = sh.is_curve() ? double(sh.radius) * canvas.volume().inv_scale() : 0.0;
                const double lo_x = std::min(ax, sh.is_curve() ? ax : bx) - r;
                const double hi_x = std::max(ax, sh.is_curve() ? ax : bx) + r;
                const double lo_y = std::min(ay, sh.is_curve() ? ay : by) - r;
                const double hi_y = std::max(ay, sh.is_curve() ? ay : by) + r;
                if (hi_x < rx0 || lo_x > rx1 || hi_y < ry0 || lo_y > ry1) continue;
            }
            Json o = shape_json(shell, i);
            if (!kind.empty() && o["kind"] != kind) continue;
            arr.push_back(std::move(o));
        }
        out["objects"] = std::move(arr);
        out["total"] = static_cast<gcm::i64>(canvas.shapes().size());
        return done(out);
    }
    if (name == "read_grid") {
        const int cols = std::clamp(int(a.num("cols", 8)), 1, 26);
        const int rows = std::clamp(int(a.num("rows", 6)), 1, 50);
        double lo_x, lo_y, hi_x, hi_y;
        if (!drawing_extents(shell, lo_x, lo_y, hi_x, hi_y))
            return done(error("the drawing is empty"));
        const double cw = (hi_x - lo_x) / cols, ch = (hi_y - lo_y) / rows;
        out["cols"] = cols;
        out["rows"] = rows;
        out["cell_width"] = display(shell, cw);
        out["cell_height"] = display(shell, ch);
        out["cell_width_text"] = fmt_length(shell, cw);
        out["cell_height_text"] = fmt_length(shell, ch);
        out["extents_low"] = fmt_point(shell, lo_x, lo_y);
        out["extents_high"] = fmt_point(shell, hi_x, hi_y);
        out["units"] = gunit::name(shell.canvas().display_unit());
        std::vector<int> tally(std::size_t(cols) * std::size_t(rows), 0);
        const auto vote = [&](double x, double y) {
            int c = int((x - lo_x) / cw), r = int((y - lo_y) / ch);
            c = std::clamp(c, 0, cols - 1);
            r = std::clamp(r, 0, rows - 1);
            ++tally[std::size_t(r) * std::size_t(cols) + std::size_t(c)];
        };
        for (const auto& sh : shell.canvas().shapes()) {
            double ax, ay, bx, by;
            gcm::to_world(shell.canvas().volume(), sh.a, ax, ay);
            gcm::to_world(shell.canvas().volume(), sh.b, bx, by);
            vote(ax, ay);
            if (!sh.is_curve()) vote(bx, by);
        }
        Json cells = Json::object();
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const int n = tally[std::size_t(r) * std::size_t(cols) + std::size_t(c)];
                if (!n) continue;
                const std::string cell_name = std::string(1, char('A' + c)) + std::to_string(r + 1);
                cells[cell_name] = n;
            }
        out["cells"] = std::move(cells);
        return done(out);
    }
    if (name == "object_info") {
        const auto id = std::size_t(a.num("id", -1));
        if (id >= shell.canvas().shapes().size()) return done(error("no object with that id"));
        return done(shape_json(shell, id));
    }
    if (name == "measure") {
        double ax, ay, bx, by;
        if (!parse_point(shell, point_text(a, "from"), ax, ay) ||
            !parse_point(shell, point_text(a, "to"), bx, by))
            return done(error("points read as \"x,y\" in display units"));
        const double d = std::hypot(bx - ax, by - ay);
        out["distance"] = display(shell, d);
        out["distance_text"] = fmt_length(shell, d);
        out["distance_mm"] = d * 1000.0;
        out["distance_in"] = d / 0.0254;
        out["dx"] = display(shell, bx - ax);
        out["dy"] = display(shell, by - ay);
        out["angle_deg"] = std::atan2(by - ay, bx - ax) * 180.0 / 3.14159265358979323846;
        return done(out);
    }
    if (name == "measure_span") return run_measure_span(shell, a, out);
    if (name == "measure_room") return run_measure_room(shell, a, out);
    if (name == "measure_path") return run_measure_path(shell, a, out);
    if (name == "list_text") return run_list_text(shell, a, out);
    if (name == "list_blocks") return run_list_blocks(shell, a, out);
    if (name == "count_objects") return run_count_objects(shell, out);
    if (name == "drawing_units") return run_drawing_units(shell, out);
    if (name == "selection_info") {
        Json arr = Json::array();
        for (std::size_t i : shell.canvas().selection())
            if (i < shell.canvas().shapes().size()) arr.push_back(shape_json(shell, i));
        out["selected"] = std::move(arr);
        out["references"] = static_cast<gcm::i64>(shell.canvas().selected_references().size());
        return done(out);
    }
    if (name == "help") {
        std::string cmd = a.str("command");
        for (char& c : cmd) c = char(std::toupper(static_cast<unsigned char>(c)));
        const gcmd::Command* c = shell.commands().by_name(cmd);
        if (!c) c = shell.commands().find(cmd);
        if (!c) return done(error("no command named " + cmd));
        out["name"] = c->name;
        out["description"] = c->description;
        Json prompts = Json::array();
        for (const auto& pr : c->prompts) prompts.push_back(pr.text);
        out["prompts"] = std::move(prompts);
        return done(out);
    }

    if(name=="list_symbols") {
        out["catalog"]=Json::array();out["used"]=Json::array();
        for(const auto& item:symbol_catalog()) out["catalog"].push_back({{"id",item.id},{"name",item.label}});
        for(const auto& item:used_symbols(shell.canvas())) out["used"].push_back({{"block",item.block},{"name",item.label},{"count",item.count}});
        return done(out);
    }
    if(name=="draw_symbol" || name=="draw_arrow" || name=="symbol_legend") {
        if(shell.read_only())return done(error("The active document is read-only"));
        if(shell.mode()!=Mode::Sketch)return done(error("Open a 2D sketch first"));
        auto& canvas=shell.canvas();double x=0,y=0;
        const auto point=[&](const char* key,double& px,double& py) {
            return parse_point(shell,point_text(a,key),px,py) && std::isfinite(px) && std::isfinite(py) && std::abs(px)<=1e7 && std::abs(py)<=1e7;
        };
        bool ok=false;
        if(name=="draw_arrow") {
            double tx,ty;
            if(!point("from",x,y) || !point("to",tx,ty))return done(error("Arrow endpoints must be finite drawing coordinates"));
            const auto direction=a.str("direction","forward");
            if(direction!="forward" && direction!="both")return done(error("direction must be forward or both"));
            const auto length=std::hypot(tx-x,ty-y);
            ok=canvas.place_symbol(direction=="both"?"both_directions":"direction",gcm::to_lattice(canvas.volume(),x,y),length,std::atan2(ty-y,tx-x));
        } else if(name=="draw_symbol") {
            const auto size=gunit::parse_length(a.str("size","500mm"),canvas.display_unit());
            const auto angle=a.num("rotation",a.v.contains("rotation")?NAN:0);
            if(!point("at",x,y) || !size || !std::isfinite(angle))return done(error("Supply finite at, size and rotation values"));
            ok=canvas.place_symbol(a.str("symbol"),gcm::to_lattice(canvas.volume(),x,y),*size,angle*3.14159265358979323846/180);
        } else {
            const auto& refs=canvas.blocks().references();
            const bool exists=std::any_of(refs.begin(),refs.end(),[&](const auto& ref){return is_symbol_legend(canvas.blocks(),ref.name);});
            if((a.v.contains("at") || !exists) && !point("at",x,y))return done(error("Supply at for the initial legend placement"));
            const auto scale=a.num("scale",a.v.contains("scale")?NAN:1);
            ok=canvas.update_symbol_legend(gcm::to_lattice(canvas.volume(),x,y),scale);
            out["updated"]=exists;out["types"]=used_symbols(canvas).size();
        }
        if(!ok)return done(error(canvas.message()));
        shell.ai_note_drawing();out["message"]=canvas.message();return done(out);
    }

    std::string err;
    if (name == "draw_line" || name == "draw_polyline" || name == "draw_rect" ||
        name == "draw_circle" || name == "add_dimension" || name == "add_text")
        shell.ai_note_drawing();
    if (name == "draw_line") {
        if (!pick_points(shell, Tool::Line, {a.str("from"), a.str("to")}, err))
            return done(error(err));
        out["drawn"] = "line";
        return done(out);
    }
    if (name == "draw_polyline") {
        std::vector<std::string> pts;
        if (const auto it = a.v.find("points"); it != a.v.end() && it->is_array())
            for (const auto& p : *it)
                if (p.is_string()) pts.push_back(p.get<std::string>());
        if (pts.size() < 2) return done(error("points wants at least two \"x,y\" strings"));
        if (!pick_points(shell, Tool::Line, pts, err)) return done(error(err));
        out["drawn"] = "polyline";
        out["segments"] = static_cast<gcm::i64>(pts.size() - 1);
        return done(out);
    }
    if (name == "draw_rect") {
        if (!pick_points(shell, Tool::Rect, {a.str("corner_a"), a.str("corner_b")}, err))
            return done(error(err));
        out["drawn"] = "rectangle";
        return done(out);
    }
    if (name == "draw_circle") {
        double cx, cy;
        if (!parse_point(shell, point_text(a, "centre"), cx, cy))
            return done(error("centre reads as \"x,y\""));
        const auto r = gunit::parse_length(a.str("radius"), shell.canvas().display_unit());
        if (!r || *r <= 0.0) return done(error("radius must be a positive length"));
        if (!pick_points(shell, Tool::Circle,
                         {a.str("centre"), fmt_point(shell, cx + *r, cy)}, err))
            return done(error(err));
        out["drawn"] = "circle";
        return done(out);
    }
    if(name=="detect_walls") {
        shell.canvas().assign_keys();
        out["recognized"]=shell.canvas().detect_wall_regions(); return done(out);
    }
    if(name=="area_background") {
        double x,y; const auto color=a.str("color"); unsigned rgb=0;
        if(!parse_point(shell,point_text(a,"at"),x,y) || color.size()!=7 || color[0]!='#')
            return done(error("Use at: x,y and color: #RRGGBB"));
        auto parsed=std::from_chars(color.data()+1,color.data()+7,rgb,16);
        auto gap=gunit::parse_length(a.str("gap","10mm"),shell.canvas().display_unit());
        const double opacity=a.num("opacity",35);
        if(parsed.ec!=std::errc{} || parsed.ptr!=color.data()+7 || !gap || !std::isfinite(opacity) || opacity<0 || opacity>100)
            return done(error("Invalid color, gap or opacity"));
        if(!shell.canvas().background_at_world(gcm::to_lattice(shell.canvas().volume(),x,y),
            gdoc::Color::rgb(gcm::u8(rgb>>16),gcm::u8(rgb>>8),gcm::u8(rgb)),*gap,int(opacity)))
            return done(error(shell.canvas().message()));
        out["filled"]=true;return done(out);
    }
    if (name == "draw_wall") { shell.ai_note_drawing(); return run_draw_wall(shell, a, out, err); }
    if (name == "edit_doors") {
        if (shell.read_only()) return error("The active document is read-only").dump();
        if (shell.mode() != Mode::Sketch) return error("Open a 2D sketch first").dump();
        const auto flow = a.str("flow"), swing = a.str("swing"), hinge = a.str("hinge");
        if ((!swing.empty() && swing != "reverse") || (!hinge.empty() && hinge != "reverse"))
            return error("swing and hinge accept reverse or may be omitted").dump();
        if (!shell.canvas().edit_selected_doors(flow, swing == "reverse", hinge == "reverse"))
            return error(shell.canvas().message()).dump();
        shell.ai_note_drawing();
        out["edited_doors"] = shell.canvas().selected_door_count();
        out["message"] = shell.canvas().message();
        return out.dump();
    }
    if (name == "draw_door") { shell.ai_note_drawing(); return run_draw_door(shell, a, out, err); }
    if (name == "draw_window") { shell.ai_note_drawing(); return run_draw_window(shell, a, out, err); }
    if (name == "merge_overlaps") return run_merge_overlaps(shell, a, out);
    if (name == "label_room") { shell.ai_note_drawing(); return run_label_room(shell, a, out); }
    if (name == "add_dimension") {
        double ax, ay, bx, by;
        if (!parse_point(shell, point_text(a, "from"), ax, ay) ||
            !parse_point(shell, point_text(a, "to"), bx, by))
            return done(error("points read as \"x,y\""));
        const double mx = (ax + bx) * 0.5, my = (ay + by) * 0.5;
        const double len = std::hypot(bx - ax, by - ay);
        if (len < 1e-12) return done(error("the points coincide"));
        const double nx = -(by - ay) / len, ny = (bx - ax) / len;
        const double off = len / 20.0;
        if (!pick_points(shell, Tool::DimAligned,
                         {a.str("from"), a.str("to"),
                          fmt_point(shell, mx + nx * off, my + ny * off)}, err))
            return done(error(err));
        out["dimensioned"] = display(shell, len);
        return done(out);
    }
    if (name == "add_text") {
        double x, y;
        if (!parse_point(shell, point_text(a, "at"), x, y)) return done(error("at reads as \"x,y\""));
        const auto h = gunit::parse_length(a.str("height", "0"), shell.canvas().display_unit());
        const double height = (h && *h > 0.0) ? *h : 0.005;
        shell.canvas().add_text(gcm::to_lattice(shell.canvas().volume(), x, y),
                                a.str("text"), height, 0.0);
        out["placed"] = "text";
        return done(out);
    }
    if (name == "plan_check") {
        Json rows = Json::array();
        for (const auto& f : shell.plan_check()) {
            Json row = Json::object();
            row["level"] = f.level == Shell::PlanFinding::Level::Error ? "error"
                         : f.level == Shell::PlanFinding::Level::Warning ? "warning"
                                                                         : "note";
            row["rule"] = f.rule;
            row["finding"] = f.text;
            row["shape_keys"] = f.shape_keys;
            if(f.located) {
                double x,y;gcm::to_world(shell.canvas().volume(),f.location,x,y);
                row["location_m"]={{"x",x},{"y",y}};
            }
            rows.push_back(std::move(row));
        }
        out["findings"] = std::move(rows);
        return done(out);
    }
    if (name == "erase_selected") {
        if (shell.ai_selection_stale())
            return done(error(
                "the drawing changed since that selection was made -- erasing "
                "now would take the OLD geometry out from under the NEW. "
                "Erase FIRST, then draw; select again right before erasing"));
        shell.canvas().erase_selected();
        shell.ai_note_erase();
        out["result"] = shell.canvas().message();
        return done(out);
    }
    if (name == "select") {
        shell.ai_note_selection();
        const std::string what = a.str("what");
        auto& canvas = shell.canvas();
        if (what == "all") canvas.select_all();
        else if (what == "none") canvas.clear_selection();
        else if (what.rfind("kind:", 0) == 0) {
            Canvas::Filter f;
            f.by_kind = true;
            const std::string k = what.substr(5);
            f.kind = k == "circle" ? gsnap::ShapeKind::Circle
                   : k == "arc" ? gsnap::ShapeKind::Arc : gsnap::ShapeKind::Segment;
            canvas.select_filtered(f, false);
        } else if (what.rfind("layer:", 0) == 0) {
            Canvas::Filter f;
            f.by_layer = true;
            f.layer = what.substr(6);
            canvas.select_filtered(f, false);
        } else return done(error("what is all|none|kind:<k>|layer:<name>"));
        out["selected"] = static_cast<gcm::i64>(canvas.selection().size());
        return done(out);
    }
    if (name == "move_selected") {
        if (shell.canvas().selection().empty() &&
            shell.canvas().selected_references().empty())
            return done(error("nothing is selected; call select first"));
        const std::string d = a.str("dx") + "," + a.str("dy");
        if (!pick_points(shell, Tool::Move, {"0,0", d}, err)) return done(error(err));
        out["moved"] = static_cast<gcm::i64>(shell.canvas().selection().size());
        return done(out);
    }
    if (name == "view_screenshot") {
        auto png = snapshot_png(shell, int(shell.viewport_width()), int(shell.viewport_height()));
        if (png.empty()) return done(error("could not render the view"));
        out["bytes"] = static_cast<gcm::i64>(png.size());
        out["note"] = "the screenshot follows this message as an image";
        shell.ai_stash_screenshot(std::move(png));
        return done(out);
    }
    if (name == "run_command") {
        const std::string cmd = a.str("command");
        if (cmd.empty()) return done(error("command is empty"));
        const std::size_t log_before = shell.log().size();
        shell.run_command(cmd);
        std::string said;
        for (std::size_t i = log_before; i < shell.log().size(); ++i)
            said += (said.empty() ? "" : "\n") + shell.log()[i];
        out["ran"] = cmd;
        out["log"] = said;
        return done(out);
    }
    return done(error("unknown tool " + name));
}

}

std::string activity_entry(const gai::ToolCall& call, Origin origin, const std::string& result) {
    if(origin==Origin::Silent)return {};
    const char* source=origin==Origin::Assistant ? "Assistant" : origin==Origin::MCP ? "MCP" :
                       origin==Origin::Library ? "Library" : "Tool";
    const auto compact=[](std::string text,std::size_t limit) {
        for(auto& c:text)if(c=='\n' || c=='\r' || c=='\t')c=' ';
        if(text.size()>limit)text=text.substr(0,limit)+"...";
        return text;
    };
    std::vector<std::string> secrets;
    const auto redact=[&](auto&& self,Json& value)->void {
        if(value.is_object())for(auto& [key,item]:value.items()) {
            auto lower=key;
            std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(lower.find("password")!=std::string::npos || lower.find("secret")!=std::string::npos ||
               lower.find("token")!=std::string::npos || lower.find("api_key")!=std::string::npos ||
               lower.find("apikey")!=std::string::npos || lower=="authorization") {
                if(item.is_string() && !item.get<std::string>().empty())secrets.push_back(item.get<std::string>());
                item="[redacted]";
            } else self(self,item);
        } else if(value.is_array())for(auto& item:value)self(self,item);
    };
    auto arguments=gai::parse_json(call.arguments());
    if(arguments)redact(redact,*arguments);
    const auto prefix="["+std::string(source)+"] "+compact(call.name(),80);
    if(result.empty())return prefix+" "+compact(arguments ? arguments->dump() : "[invalid JSON arguments]",384);
    auto outcome=gai::parse_json(result);
    bool failed=!outcome || (outcome->is_object() &&
        (outcome->contains("error") || (outcome->contains("isError") && (*outcome)["isError"]==true)));
    std::string summary=failed ? "error" : "ok";
    if(outcome && outcome->is_object() && outcome->contains("error")) {
        auto error=(*outcome)["error"];
        redact(redact,error);
        summary="error: "+(error.is_string() ? error.get<std::string>() : error.dump());
    }
    for(const auto& secret:secrets) {
        std::size_t at=0;
        while((at=summary.find(secret,at))!=std::string::npos) {
            summary.replace(at,secret.size(),"[redacted]");at+=10;
        }
    }
    return prefix+": "+compact(summary,220);
}

std::string execute(Shell& shell, const gai::ToolCall& call, Origin origin) {
    if(origin!=Origin::Silent)shell.log_line(activity_entry(call,origin));
    std::string raw;
    try {raw=execute_call(shell,call);}
    catch(const std::exception& e) {raw=error(e.what()).dump();}
    if(!pure(call))shell.refresh_symbol_legends();
    if(origin!=Origin::Silent)shell.log_line(activity_entry(call,origin,raw));
    const auto v = gai::parse_json(raw);
    if (!v || !v->is_object()) return raw;
    Json out = *v;
    const auto unit = shell.canvas().display_unit();
    out["units"] = gunit::name(unit);
    out["unit_suffix"] = gunit::suffix(unit);
    out["units_note"] = std::string("every length, coordinate, radius and "
        "distance in this result is a number of ") + gunit::name(unit) +
        " (" + gunit::suffix(unit) + "); areas are square " + gunit::name(unit) +
        " unless the field names another unit. The *_text fields are the same "
        "values already written with their marks.";
    return out.dump();
}

}
