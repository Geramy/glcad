#include "geometry/dimension.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace gdim {
std::pair<Point,Point> Dimension::radial_points() const {
    const double angle=std::atan2(double(p3.y)-centre.y,double(p3.x)-centre.x);
    const double dx=double(radius)*std::cos(angle),dy=double(radius)*std::sin(angle);
    const Point chord{i64(std::llround(double(centre.x)+dx)),i64(std::llround(double(centre.y)+dy))};
    const Point other=kind==Kind::Diameter
        ?Point{i64(std::llround(double(centre.x)-dx)),i64(std::llround(double(centre.y)-dy))}:centre;
    return {chord,other};
}

bool Dimension::anchor_point(const gsnap::Shape& shape, Anchor anchor, Point& out) {
    using K=gsnap::ShapeKind;
    if(anchor==Anchor::Edge) {
        if(shape.kind!=K::Segment)return false;
        const double dx=double(shape.b.x)-shape.a.x,dy=double(shape.b.y)-shape.a.y;
        const double length2=dx*dx+dy*dy;
        if(length2<=0)return false;
        const double t=std::clamp(((double(out.x)-shape.a.x)*dx+(double(out.y)-shape.a.y)*dy)/length2,0.0,1.0);
        out={i64(std::llround(shape.a.x+t*dx)),i64(std::llround(shape.a.y+t*dy))};
        return true;
    }
    if(anchor==Anchor::Centre) {
        if(!shape.is_curve() && !shape.is_elliptical())return false;
        out=shape.a;return true;
    }
    if(anchor!=Anchor::Start && anchor!=Anchor::End)return false;
    const bool start=anchor==Anchor::Start;
    if(shape.kind==K::Arc || (shape.is_elliptical() && shape.ellipse_sweep()<6.283185307179586-1e-10)) {
        const double angle=start?shape.start_angle:shape.end_angle;
        double ux=shape.radius,uy=0,vx=0,vy=shape.radius;
        if(shape.is_elliptical()) {
            ux=double(shape.b.x)-shape.a.x;uy=double(shape.b.y)-shape.a.y;
            const double length=std::hypot(ux,uy);if(length<=0)return false;
            vx=-uy*shape.radius/length;vy=ux*shape.radius/length;
        }
        out={i64(std::llround(shape.a.x+ux*std::cos(angle)+vx*std::sin(angle))),
             i64(std::llround(shape.a.y+uy*std::cos(angle)+vy*std::sin(angle)))};
        return true;
    }
    if(shape.kind==K::Segment || shape.kind==K::Point || (shape.kind==K::Ray && start)) {
        out=start?shape.a:shape.b;return true;
    }
    return false;
}

std::vector<Dimension::LeaderPath> Dimension::paths() const {
    return leader_paths.empty()?std::vector<LeaderPath>{{{p1,p2,p3},true}}:leader_paths;
}
std::vector<Point> Dimension::leader_grips() const {
    std::vector<Point> points;
    const auto add=[&](Point point) {
        if(std::none_of(points.begin(),points.end(),[&](auto p){return p.x==point.x&&p.y==point.y;}))points.push_back(point);
    };
    for(const auto& path:paths())for(auto point:path.vertices)add(point);
    add(p3);return points;
}
void Dimension::move_leader_point(Point from,Point to) {
    const auto move=[&](Point& point){if(point.x==from.x&&point.y==from.y)point=to;};
    move(p1);move(p2);move(p3);
    for(auto& path:leader_paths)for(auto& point:path.vertices)move(point);
    o1={};o2={};
}
bool Dimension::move_leader_grip(std::size_t index,Point point) {
    const auto grips=leader_grips();if(index>=grips.size())return false;
    move_leader_point(grips[index],point);return true;
}

constexpr f64 kPi = 3.14159265358979323846;

const char* kind_name(Kind k) noexcept {
    switch (k) {
        case Kind::Linear:     return "Linear";
        case Kind::Horizontal: return "Horizontal";
        case Kind::Vertical:   return "Vertical";
        case Kind::Aligned:    return "Aligned";
        case Kind::Angular:    return "Angular";
        case Kind::Radial:     return "Radial";
        case Kind::Diameter:   return "Diameter";
        case Kind::Ordinate:   return "Ordinate";
        case Kind::ArcLength:  return "ArcLength";
        case Kind::Leader: return "Leader";
    }
    return "";
}

StyleTable::StyleTable() { styles_.push_back(Style{}); }

const Style* StyleTable::find(std::string_view n) const {
    for (const auto& s : styles_)
        if (s.name == n) return &s;
    return nullptr;
}
Style& StyleTable::add(Style s) {
    for (auto& e : styles_)
        if (e.name == s.name) { e = std::move(s); return e; }
    styles_.push_back(std::move(s));
    return styles_.back();
}
void StyleTable::set_current(std::string_view n) {
    if (find(n)) current_ = std::string(n);
}

namespace {
struct W { f64 x, y; };
W world(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    return {x, y};
}
Point lat(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }

gsnap::Shape seg(const Volume& v, W a, W b) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = lat(v, a.x, a.y);
    s.b = lat(v, b.x, b.y);
    return s;
}
}

namespace {

// Use the offset outside the origin box, not from the midpoint (baseline chains).
bool linear_is_horizontal(f64 ax, f64 ay, f64 bx, f64 by, f64 px, f64 py) noexcept {
    const f64 x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax;
    const f64 y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;

    const f64 out_x = px < x0 ? x0 - px : (px > x1 ? px - x1 : 0.0);
    const f64 out_y = py < y0 ? y0 - py : (py > y1 ? py - y1 : 0.0);

    if (out_x == 0.0 && out_y == 0.0) return (x1 - x0) >= (y1 - y0);
    return out_y >= out_x;
}

}

bool linear_is_horizontal(const Dimension& d, const Volume& v) {
    const W a=world(v,d.p1),b=world(v,d.p2),p=world(v,d.p3);
    return linear_is_horizontal(a.x,a.y,b.x,b.y,p.x,p.y);
}

f64 measure(const Dimension& d, const Volume& v) {
    const W a = world(v, d.p1), b = world(v, d.p2);
    switch (d.kind) {
        case Kind::Horizontal: return std::fabs(b.x - a.x);
        case Kind::Vertical:   return std::fabs(b.y - a.y);
        case Kind::Aligned:    return std::hypot(b.x - a.x, b.y - a.y);

        case Kind::Linear: {
            const W p = world(v, d.p3);
            const f64 dx = std::fabs(b.x - a.x), dy = std::fabs(b.y - a.y);
            return linear_is_horizontal(a.x, a.y, b.x, b.y, p.x, p.y) ? dx : dy;
        }

        case Kind::Radial:   return f64(d.radius) * v.inv_scale();
        case Kind::Diameter: return 2.0 * f64(d.radius) * v.inv_scale();

        case Kind::Angular: {
            const W c = world(v, d.centre);
            const f64 a1 = std::atan2(a.y - c.y, a.x - c.x);
            const f64 a2 = std::atan2(b.y - c.y, b.x - c.x);
            f64 sweep = a2 - a1;
            while (sweep < 0.0) sweep += 2.0 * kPi;
            while (sweep > 2.0 * kPi) sweep -= 2.0 * kPi;
            return sweep;                    // radians
        }

        case Kind::ArcLength: {
            const W c = world(v, d.centre);
            const f64 a1 = std::atan2(a.y - c.y, a.x - c.x);
            const f64 a2 = std::atan2(b.y - c.y, b.x - c.x);
            f64 sweep = a2 - a1;
            while (sweep < 0.0) sweep += 2.0 * kPi;
            return sweep * f64(d.radius) * v.inv_scale();
        }

        case Kind::Ordinate: {
            const W p = world(v, d.p3);
            return std::fabs(b.y-a.y)>=std::fabs(b.x-a.x) ? a.x-p.x : a.y-p.y;
        }
    }
    return 0.0;
}

std::string format(const Dimension& d, const Style& s, const Volume& v) {
    if(d.kind==Kind::Leader) return d.text_override;
    if (d.has_override()) return d.text_override;

    const f64 raw = measure(d, v);

    if (d.kind == Kind::Angular) {
        gunit::AngleFormat af;
        af.unit = gunit::Angle::Degree;
        af.precision = s.dimdec;
        return s.prefix + gunit::format_angle(raw, af) + s.suffix;
    }

    gunit::Format f;
    f.unit = s.unit;
    f.style = s.format;
    f.precision = s.dimdec;
    f.show_unit = false;
    f.suppress_trailing_zeros = s.dimzin_trailing;

    std::string body = gunit::format_length(raw * s.dimlfac, f);

    std::string prefix = s.prefix;
    if (d.kind == Kind::Diameter && prefix.empty()) prefix = "\xe2\x8c\x80";  // ⌀
    if (d.kind == Kind::Radial && prefix.empty()) prefix = "R";

    std::string out = prefix + body + s.suffix;

    if (s.dimtol && (s.dimtp != 0.0 || s.dimtm != 0.0)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), " +%.*f/-%.*f", s.dimdec, s.dimtp,
                      s.dimdec, s.dimtm);
        out += buf;
    }
    return out;
}

namespace {

void push_arrow(std::vector<gsnap::Shape>& out, const Volume& v, W tip, W dir,
                f64 size, ArrowType type) {
    const f64 len = std::hypot(dir.x, dir.y);
    if (len < 1e-12 || type == ArrowType::None) return;
    const W u{dir.x / len, dir.y / len};
    const W n{-u.y, u.x};

    switch (type) {
        case ArrowType::Tick: {
            const f64 h = size * 0.5;
            out.push_back(seg(v, {tip.x - (u.x + n.x) * h, tip.y - (u.y + n.y) * h},
                                 {tip.x + (u.x + n.x) * h, tip.y + (u.y + n.y) * h}));
            break;
        }
        case ArrowType::Dot: {
            gsnap::Shape c;
            c.kind = gsnap::ShapeKind::Circle;
            c.a = lat(v, tip.x, tip.y);
            c.radius = i64(std::llround(size * 0.25 * v.scale()));
            out.push_back(c);
            break;
        }
        default: {
            const f64 w = size * 0.28;
            const W back{tip.x + u.x * size, tip.y + u.y * size};
            const W l{back.x + n.x * w, back.y + n.y * w};
            const W r{back.x - n.x * w, back.y - n.y * w};
            out.push_back(seg(v, tip, l));
            out.push_back(seg(v, tip, r));
            if (type == ArrowType::ClosedFilled) {
                out.push_back(seg(v, l, r));
                for (int i = 1; i <= 3; ++i) {
                    const f64 t = f64(i) / 4.0;
                    out.push_back(seg(v, tip, {l.x + (r.x - l.x) * t, l.y + (r.y - l.y) * t}));
                }
            }
            break;
        }
    }
}

}

Geometry build(const Dimension& d, const Style& s, const Volume& v) {
    Geometry g;
    g.text = format(d, s, v);
    g.text_height = s.scaled(s.dimtxt) * 0.001;   // style sizes are millimetres

    const f64 exe = s.scaled(s.dimexe) * 0.001;
    const f64 exo = s.scaled(s.dimexo) * 0.001;
    const f64 asz = s.scaled(s.dimasz) * 0.001;
    const f64 gap = s.scaled(s.dimgap) * 0.001;

    const W a = world(v, d.p1), b = world(v, d.p2), p = world(v, d.p3);

    switch (d.kind) {
        case Kind::Linear:
        case Kind::Horizontal:
        case Kind::Vertical:
        case Kind::Aligned: {
            W u{}, n{};
            if (d.kind == Kind::Aligned) {
                const f64 dx = b.x - a.x, dy = b.y - a.y;
                const f64 len = std::hypot(dx, dy);
                if (len < 1e-12) { g.reason = "coincident origins"; return g; }
                u = {dx / len, dy / len};
                n = {-u.y, u.x};
            } else {
                const bool horiz =
                    d.kind == Kind::Horizontal ||
                    (d.kind == Kind::Linear &&
                     linear_is_horizontal(a.x, a.y, b.x, b.y, p.x, p.y));
                u = horiz ? W{1, 0} : W{0, 1};
                n = horiz ? W{0, 1} : W{1, 0};
            }

            const f64 off = (p.x - a.x) * n.x + (p.y - a.y) * n.y;
            const W da{a.x + n.x * off, a.y + n.y * off};
            const W db{b.x + n.x * off, b.y + n.y * off};

            const W shift{double(d.extension_offset.x) * v.inv_scale(),
                          double(d.extension_offset.y) * v.inv_scale()};
            const W ea{a.x + shift.x, a.y + shift.y};
            const W eb{b.x + shift.x, b.y + shift.y};
            const W unshifted = world(v, {d.p3.x - d.extension_offset.x,
                                          d.p3.y - d.extension_offset.y});
            const f64 sign = (unshifted.x - a.x) * n.x +
                             (unshifted.y - a.y) * n.y >= 0.0 ? 1.0 : -1.0;
            if (!s.dimse1)
                g.lines.push_back(seg(v,
                    {ea.x + n.x * exo * sign, ea.y + n.y * exo * sign},
                    {da.x + n.x * exe * sign, da.y + n.y * exe * sign}));
            if (!s.dimse2)
                g.lines.push_back(seg(v,
                    {eb.x + n.x * exo * sign, eb.y + n.y * exo * sign},
                    {db.x + n.x * exe * sign, db.y + n.y * exe * sign}));

            g.lines.push_back(seg(v, da, db));

            const f64 dlen = std::hypot(db.x - da.x, db.y - da.y);
            if (dlen > 1e-12) {
                const W dir{(db.x - da.x) / dlen, (db.y - da.y) / dlen};
                if (!s.dimsd1) push_arrow(g.arrows, v, da, dir, asz, s.arrow);
                if (!s.dimsd2) push_arrow(g.arrows, v, db, {-dir.x, -dir.y}, asz,
                                          s.arrow);
            }

            const W mid{(da.x + db.x) * 0.5, (da.y + db.y) * 0.5};
            const f64 lift = s.text_pos == TextPos::Above ? gap + g.text_height * 0.5
                           : s.text_pos == TextPos::Below ? -(gap + g.text_height * 0.5)
                                                          : 0.0;
            g.text_at = lat(v, mid.x + n.x * lift * sign, mid.y + n.y * lift * sign);
            g.text_angle = (s.dimtih || d.kind != Kind::Aligned)
                               ? 0.0
                               : std::atan2(u.y, u.x);
            g.ok = true;
            break;
        }

        case Kind::Radial:
        case Kind::Diameter: {
            const W c = world(v, d.centre);
            const f64 r = f64(d.radius) * v.inv_scale();
            const f64 ang = std::atan2(p.y - c.y, p.x - c.x);
            const W on{c.x + std::cos(ang) * r, c.y + std::sin(ang) * r};

            if (d.kind == Kind::Diameter) {
                const W opp{c.x - std::cos(ang) * r, c.y - std::sin(ang) * r};
                g.lines.push_back(seg(v, opp, p));
                push_arrow(g.arrows, v, opp, {std::cos(ang), std::sin(ang)}, asz,
                           s.arrow);
            } else {
                g.lines.push_back(seg(v, c, p));
            }
            push_arrow(g.arrows, v, on, {-std::cos(ang), -std::sin(ang)}, asz,
                       s.arrow);

            g.text_at = lat(v, p.x, p.y + gap);
            g.ok = true;
            break;
        }

        case Kind::Angular: {
            const W c = world(v, d.centre);
            const f64 a1 = std::atan2(a.y - c.y, a.x - c.x);
            const f64 a2 = std::atan2(b.y - c.y, b.x - c.x);
            const f64 r = std::hypot(p.x - c.x, p.y - c.y);

            g.lines.push_back(seg(v, c, a));
            g.lines.push_back(seg(v, c, b));

            gsnap::Shape arc;
            arc.kind = gsnap::ShapeKind::Arc;
            arc.a = lat(v, c.x, c.y);
            arc.radius = i64(std::llround(r * v.scale()));
            arc.start_angle = a1;
            arc.end_angle = a2;
            g.lines.push_back(arc);

            const f64 amid = a1 + (a2 - a1) * 0.5;
            g.text_at = lat(v, c.x + std::cos(amid) * (r + gap),
                            c.y + std::sin(amid) * (r + gap));
            g.ok = true;
            break;
        }

        case Kind::ArcLength: {
            const W c = world(v, d.centre);
            const f64 r = f64(d.radius) * v.inv_scale();
            gsnap::Shape arc;
            arc.kind = gsnap::ShapeKind::Arc;
            arc.a = lat(v, c.x, c.y);
            arc.radius = i64(std::llround((r + exo) * v.scale()));
            arc.start_angle = std::atan2(a.y - c.y, a.x - c.x);
            arc.end_angle = std::atan2(b.y - c.y, b.x - c.x);
            g.lines.push_back(arc);
            g.text_at = d.p3;
            g.ok = true;
            break;
        }

        case Kind::Leader: {
            if(d.leader_text_height>0)g.text_height=d.leader_text_height;
            g.text_angle=d.leader_text_angle;
            for(const auto& path:d.paths()) {
                for(std::size_t i=1;i<path.vertices.size();++i) {
                    double ax,ay,bx,by;gcm::to_world(v,path.vertices[i-1],ax,ay);gcm::to_world(v,path.vertices[i],bx,by);
                    if(ax==bx&&ay==by)continue;
                    g.lines.push_back(seg(v,{ax,ay},{bx,by}));
                }
                if(path.arrow && path.vertices.size()>1) {
                    double ax,ay,bx,by;gcm::to_world(v,path.vertices.front(),ax,ay);
                    auto next=std::find_if(path.vertices.begin()+1,path.vertices.end(),[&](auto point){return point.x!=path.vertices.front().x||point.y!=path.vertices.front().y;});
                    if(next!=path.vertices.end()) {
                        gcm::to_world(v,*next,bx,by);const double length=std::hypot(bx-ax,by-ay);
                        push_arrow(g.arrows,v,{ax,ay},{(bx-ax)/length,(by-ay)/length},d.leader_arrow_size>0?d.leader_arrow_size:asz,s.arrow);
                    }
                }
            }
            if(g.lines.empty()){g.reason="leader needs distinct vertices";break;}
            const double tx=std::cos(g.text_angle),ty=std::sin(g.text_angle);
            const bool right=(p.x-a.x)*tx+(p.y-a.y)*ty>=0;
            const double offset=right?gap:-gap;
            g.text_at=d.leader_text_absolute?d.p3:lat(v,p.x+offset*tx,p.y+offset*ty);
            g.halign=d.leader_text_halign>=0?d.leader_text_halign:(right?0:2);
            g.valign=d.leader_text_valign>=0?d.leader_text_valign:0;g.ok=true;
            break;
        }
        case Kind::Ordinate: {
            g.lines.push_back(seg(v, a, b));
            g.text_at = lat(v, b.x, b.y + gap);
            g.ok = true;
            break;
        }
    }
    return g;
}

}
