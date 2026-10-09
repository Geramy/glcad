#include "plancheck/plancheck.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace gplan {

std::vector<Finding> door_swing_collisions(const Plan& plan) {
    std::vector<Finding> findings;
    if(!plan.shapes || !(plan.metres_per_unit>0))return findings;
    const auto& shapes=*plan.shapes;
    constexpr double tau=6.2831853071795864769;
    const auto positive=[&](double angle){angle=std::fmod(angle,tau);return angle<0?angle+tau:angle;};
    const double unit=plan.metres_per_unit,tolerance=.001;
    for(const auto& door:shapes) {
        if(door.kind!=gsnap::ShapeKind::Arc || (door.layer!="A-DOOR" && !door.attrs.user.count("door")))continue;
        const double radius=double(door.radius)*unit,sweep=positive(door.end_angle-door.start_angle);
        if(radius<=tolerance || sweep<=0 || sweep>3.141593)continue;
        const double angular=tolerance/radius;
        const double sx=std::cos(door.start_angle),sy=std::sin(door.start_angle);
        const double ex=std::cos(door.end_angle),ey=std::sin(door.end_angle);
        for(const auto& wall:shapes) {
            if(wall.kind!=gsnap::ShapeKind::Segment || wall.layer!="A-WALL")continue;
            const double ax=(double(wall.a.x)-door.a.x)*unit,ay=(double(wall.a.y)-door.a.y)*unit;
            const double dx=(double(wall.b.x)-wall.a.x)*unit,dy=(double(wall.b.y)-wall.a.y)*unit;
            const double length2=dx*dx+dy*dy;
            if(length2<1e-18)continue;
            // Splitting at the circle and both rays makes membership constant per interval.
            std::vector<double> cuts{0,1};
            const double b=2*(ax*dx+ay*dy),c=ax*ax+ay*ay-radius*radius;
            const double disc=b*b-4*length2*c;
            if(disc>=0) {
                const double root=std::sqrt(disc);
                for(double t:{(-b-root)/(2*length2),(-b+root)/(2*length2)})if(t>0&&t<1)cuts.push_back(t);
            }
            for(auto ray:{std::pair{sx,sy},std::pair{ex,ey}}) {
                const double det=ray.first*dy-ray.second*dx;
                if(std::abs(det)>1e-15) {
                    const double t=-(ray.first*ay-ray.second*ax)/det;
                    if(t>0&&t<1)cuts.push_back(t);
                }
            }
            std::sort(cuts.begin(),cuts.end());
            bool blocked=false;double at=0;
            for(std::size_t i=1;i<cuts.size();++i) {
                at=(cuts[i-1]+cuts[i])*.5;
                const double x=ax+at*dx,y=ay+at*dy,r=std::hypot(x,y);
                const double angle=positive(std::atan2(y,x)-door.start_angle);
                if(r>tolerance && r<radius-tolerance && angle>angular && angle<sweep-angular) {blocked=true;break;}
            }
            if(!blocked)continue;
            findings.push_back({Finding::Level::Warning,"doors.swing_collision",
                "Door swing intersects a wall: move the opening or reverse the swing",true,
                {gcm::i64(std::llround(wall.a.x+at*(double(wall.b.x)-wall.a.x))),
                 gcm::i64(std::llround(wall.a.y+at*(double(wall.b.y)-wall.a.y)))},{door.key,wall.key}});
            break;
        }
        if(findings.size()>=200)break;
    }
    return findings;
}

Rules load_rules(const std::string& path) {
    Rules r;
    std::ifstream f(path);
    if (!f) return r;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string key;
        in >> key;
        if (key == "source") {
            std::string rest;
            std::getline(in, rest);
            while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
            if (!rest.empty()) r.source = rest;
            continue;
        }
        double v = 0.0;
        if (!(in >> v)) continue;
        // Millimetres on the wire.
        if (key == "entrance_min_mm") r.entrance_min_m = v / 1000.0;
        else if (key == "egress_door_min_mm") r.egress_door_min_m = v / 1000.0;
        else if (key == "layer0_slack") r.layer0_slack = int(v);
        else if (key == "jacks_double_over_mm") r.jacks_double_over_m = v / 1000.0;
    }
    return r;
}

bool save_rules(const std::string& path, const Rules& r) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# GLCAD plan-check rules. Millimetres. '#' comments.\n"
      << "source " << r.source << "\n"
      << "entrance_min_mm " << int(std::lround(r.entrance_min_m * 1000.0)) << "\n"
      << "egress_door_min_mm " << int(std::lround(r.egress_door_min_m * 1000.0)) << "\n"
      << "layer0_slack " << r.layer0_slack << "\n"
      << "jacks_double_over_mm " << int(std::lround(r.jacks_double_over_m * 1000.0)) << "\n";
    return bool(f);
}

std::vector<Finding> check(const Plan& plan, const Rules& rules) {
    std::vector<Finding> out;
    using L = Finding::Level;
    const auto add = [&](L lv, const char* rule, std::string text) {
        out.push_back({lv, rule, std::move(text)});
    };
    static const std::vector<gsnap::Shape> none;
    const auto& shapes = plan.shapes ? *plan.shapes : none;

    std::size_t wall_shapes = 0, glaz_shapes = 0, layer0 = 0, door_arcs = 0;
    double widest_door = 0.0, narrowest_door = 1e300;
    for (const auto& sh : shapes) {
        if (sh.layer == "A-WALL") ++wall_shapes;
        else if (sh.layer == "A-GLAZ") ++glaz_shapes;
        else if (sh.layer.empty() || sh.layer == "0") ++layer0;
        if (sh.layer == "A-DOOR" && sh.kind == gsnap::ShapeKind::Arc) {
            ++door_arcs;
            const double w = double(sh.radius) * plan.metres_per_unit;
            widest_door = std::max(widest_door, w);
            narrowest_door = std::min(narrowest_door, w);
        }
    }
    const bool has_plan = wall_shapes > 0 || shapes.size() > 8;
    if (plan.architectural && !has_plan) {
        add(L::Note, "plan.empty", "nothing to check yet -- draw walls first");
        return out;
    }

    if (plan.architectural && wall_shapes == 0)
        add(L::Error, "walls.layer",
            "no walls on A-WALL -- structural walls belong on their layer, "
            "drawn as double lines with a hatched core (draw_wall)");
    if (plan.architectural && wall_shapes > 0 && plan.hatches == 0)
        add(L::Warning, "walls.hatch",
            "wall cores are not hatched -- a wall without poche reads as "
            "two stray lines");
    if (plan.architectural && layer0 > std::size_t(std::max(0, rules.layer0_slack)))
        add(L::Warning, "layers.discipline",
            std::to_string(layer0) + " objects on layer 0 -- structure, "
            "doors, glazing and notes each belong on their layer");

    const long double scale=plan.metres_per_unit;
    const auto project=[](gcm::Lattice2 p,const gsnap::Shape& s) {
        long double dx=(long double)s.b.x-s.a.x,dy=(long double)s.b.y-s.a.y;
        long double ll=dx*dx+dy*dy;
        long double t=ll?std::clamp((((long double)p.x-s.a.x)*dx+((long double)p.y-s.a.y)*dy)/ll,0.L,1.L):0;
        return std::hypot((long double)p.x-s.a.x-t*dx,(long double)p.y-s.a.y-t*dy);
    };
    for(std::size_t i=0;i<shapes.size() && out.size()<200;++i) {
        const auto& a=shapes[i];
        if(a.layer!="A-WALL" || a.kind!=gsnap::ShapeKind::Segment) continue;
        for(auto p:{a.a,a.b}) {
            bool joined=false; long double nearest=1e100L; gcm::u32 other=0;
            for(std::size_t j=0;j<shapes.size();++j) {
                const auto& b=shapes[j];
                if(i==j || b.layer!="A-WALL" || b.kind!=gsnap::ShapeKind::Segment) continue;
                const auto d=project(p,b);
                if(d<=2) joined=true;
                if(d<nearest) {nearest=d;other=b.key;}
            }
            if(!joined && nearest*scale<=.025L && nearest*scale>.00001L)
                out.push_back({L::Warning,"walls.join_gap","Wall is not joined: close this gap",true,p,{a.key,other}});
        }
        for(std::size_t j=i+1;j<shapes.size() && out.size()<200;++j) {
            const auto& b=shapes[j];
            if(b.layer!="A-WALL" || b.kind!=gsnap::ShapeKind::Segment) continue;
            const long double dx=(long double)a.b.x-a.a.x,dy=(long double)a.b.y-a.a.y;
            const long double ex=(long double)b.b.x-b.a.x,ey=(long double)b.b.y-b.a.y;
            const long double qx=(long double)b.a.x-a.a.x,qy=(long double)b.a.y-a.a.y;
            const auto det=dx*ey-dy*ex;
            if(det==0) continue;
            const auto t=(qx*ey-qy*ex)/det,u=(qx*dy-qy*dx)/det;
            if(t>1e-8L && t<1-1e-8L && u>1e-8L && u<1-1e-8L)
                out.push_back({L::Warning,"walls.crossing","Wall faces overlap: join the wall boundaries",true,
                    {gcm::i64(std::llround(a.a.x+t*dx)),gcm::i64(std::llround(a.a.y+t*dy))},{a.key,b.key}});
        }
    }

    const auto collisions=door_swing_collisions(plan);
    out.insert(out.end(),collisions.begin(),collisions.end());

    if (!plan.architectural) {
        if (plan.orphan_hatches)
            add(L::Note, "hatch.orphans", std::to_string(plan.orphan_hatches) + " hatch(es) lost their boundary -- redraw it or erase them");
        if (out.empty()) add(L::Note, "drawing.clean", "No issues found by the available geometry checks");
        return out;
    }

    const auto mm = [](double m) { return std::to_string(int(std::lround(m * 1000.0))); };
    if (door_arcs == 0)
        add(L::Error, "doors.none",
            "no door swings on A-DOOR -- every plan needs its doors, and "
            "every dwelling needs an ENTRANCE door");
    else {
        if (widest_door < rules.entrance_min_m)
            add(L::Warning, "doors.entrance",
                "no entrance-width door: the main entry takes a " +
                mm(rules.entrance_min_m) + "mm+ leaf (" + rules.source +
                "); the widest here is " + mm(widest_door) + "mm");
        if (narrowest_door < rules.egress_door_min_m)
            add(L::Warning, "doors.egress",
                "a door is narrower than the egress minimum of " +
                mm(rules.egress_door_min_m) + "mm (" + rules.source + "): " +
                mm(narrowest_door) + "mm leaf");
    }

    if (glaz_shapes == 0)
        add(L::Warning, "glazing.none",
            "no windows on A-GLAZ -- habitable rooms need light and "
            "sleeping rooms need egress openings (IRC R310)");

    // IRC R602.7: 2 short edges per stud; jack+king pack = 4, doubled = 6.
    {
        struct Opening { double ex, ey, fx, fy, w; };
        std::vector<Opening> ops;
        const double mpu = plan.metres_per_unit;
        const auto near_axis = [&](const Opening& o, double x, double y) {
            return std::hypot(x - o.ex, y - o.ey) < 0.3 &&
                   std::hypot(x - o.fx, y - o.fy) < o.w + 0.6;
        };
        for (const auto& sh : shapes) {
            if (sh.layer == "A-DOOR" && sh.kind == gsnap::ShapeKind::Arc) {
                const double cx = double(sh.a.x) * mpu, cy = double(sh.a.y) * mpu;
                const double r = double(sh.radius) * mpu;
                ops.push_back({cx, cy,
                               cx + std::cos(sh.start_angle) * r,
                               cy + std::sin(sh.start_angle) * r, r});
            } else if (sh.layer == "A-GLAZ" &&
                       sh.kind == gsnap::ShapeKind::Segment) {
                const double ax = double(sh.a.x) * mpu, ay = double(sh.a.y) * mpu;
                const double bx = double(sh.b.x) * mpu, by = double(sh.b.y) * mpu;
                const double w = std::hypot(bx - ax, by - ay);
                if (w < 0.35) continue;
                bool dup = false;
                for (const auto& o : ops)
                    if (near_axis(o, ax, ay) || near_axis(o, bx, by)) dup = true;
                if (!dup) ops.push_back({ax, ay, bx, by, w});
            }
        }
        std::size_t blocked = 0;
        for (const auto& o : ops) {
            const double w = std::hypot(o.fx - o.ex, o.fy - o.ey);
            if (w < 1e-9) continue;
            const double ux = (o.fx - o.ex) / w, uy = (o.fy - o.ey) / w;
            const double midx = (o.ex + o.fx) * 0.5, midy = (o.ey + o.fy) * 0.5;
            for (const auto& sh : shapes) {
                if (sh.layer != "A-WALL" || sh.kind != gsnap::ShapeKind::Segment)
                    continue;
                const double ax = double(sh.a.x) * mpu, ay = double(sh.a.y) * mpu;
                const double bx = double(sh.b.x) * mpu, by = double(sh.b.y) * mpu;
                const double sl = std::hypot(bx - ax, by - ay);
                if (sl < 0.12) continue;   // stud edges and caps are not walls
                if (std::fabs(((bx - ax) * uy - (by - ay) * ux) / sl) > 0.05)
                    continue;
                if (std::fabs((midx - ax) * uy - (midy - ay) * ux) > 0.25)
                    continue;
                const double p0 = (ax - o.ex) * ux + (ay - o.ey) * uy;
                const double p1 = (bx - o.ex) * ux + (by - o.ey) * uy;
                if (std::min(p0, p1) < w * 0.35 && std::max(p0, p1) > w * 0.65) {
                    ++blocked;
                    break;
                }
            }
        }
        if (blocked)
            add(L::Error, "openings.blocked",
                std::to_string(blocked) + " opening(s) have wall lines "
                "running through them -- run merge_overlaps to cut the "
                "faces across every door and window");

        std::size_t unframed = 0, single_jacked = 0;
        for (const auto& o : ops) {
            int at_hinge = 0, at_latch = 0;
            for (const auto& sh : shapes) {
                if (sh.layer != "A-WALL" || sh.kind != gsnap::ShapeKind::Segment)
                    continue;
                const double ax = double(sh.a.x) * mpu, ay = double(sh.a.y) * mpu;
                const double bx = double(sh.b.x) * mpu, by = double(sh.b.y) * mpu;
                if (std::hypot(bx - ax, by - ay) > 0.08) continue;   // not a stud edge
                const double mx = (ax + bx) * 0.5, my = (ay + by) * 0.5;
                if (std::hypot(mx - o.ex, my - o.ey) < 0.35) ++at_hinge;
                if (std::hypot(mx - o.fx, my - o.fy) < 0.35) ++at_latch;
            }
            if (std::min(at_hinge, at_latch) < 4) ++unframed;
            else if (o.w >= rules.jacks_double_over_m &&
                     std::min(at_hinge, at_latch) < 6)
                ++single_jacked;
        }
        if (unframed)
            add(L::Warning, "openings.framing",
                std::to_string(unframed) + " opening(s) show no jamb "
                "framing -- headers land on jack studs inside kings, and "
                "the plan shows the pack (IRC R602.7; draw_door/draw_window "
                "frame it)");
        if (single_jacked)
            add(L::Warning, "openings.jacks",
                std::to_string(single_jacked) + " opening(s) at or over " +
                mm(rules.jacks_double_over_m) + "mm show a single jack -- "
                "that width wants them DOUBLED (IRC Table R602.7(1); " +
                rules.source + ")");
    }

    if (plan.labels == 0)
        add(L::Warning, "rooms.labels",
            "no room labels -- every room carries its name (label_room)");
    if (plan.dimensions == 0)
        add(L::Error, "dims.none",
            "no dimensions -- exterior walls and openings are dimensioned "
            "on any submittable plan");

    if (plan.orphan_hatches)
        add(L::Note, "hatch.orphans",
            std::to_string(plan.orphan_hatches) +
            " hatch(es) lost their boundary -- redraw it or erase them");

    if (out.empty())
        add(L::Note, "plan.clean", "no findings -- the checks pass");
    return out;
}

}
