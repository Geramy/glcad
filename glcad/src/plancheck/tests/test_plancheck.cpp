#include "test.hpp"

#include <cstdio>
#include <unistd.h>

#include "plancheck/plancheck.hpp"

using namespace gplan;

namespace {
gsnap::Shape wall_line() {
    gsnap::Shape s;
    s.layer = "A-WALL";
    s.a = {gcm::i64(0), gcm::i64(0)};
    s.b = {gcm::i64(1000), gcm::i64(0)};
    return s;
}
gsnap::Shape door_arc(gcm::i64 radius) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Arc;
    s.layer = "A-DOOR";
    s.radius = radius;
    return s;
}
bool has_rule(const std::vector<Finding>& fs, const char* rule) {
    for (const auto& f : fs) if (f.rule == rule) return true;
    return false;
}
}

TEST(plancheck_reads_an_incomplete_plan) {
    std::vector<gsnap::Shape> shapes(4, wall_line());
    Plan p;
    p.shapes = &shapes;
    p.metres_per_unit = 0.001;
    const auto f = check(p, Rules{});
    CHECK(has_rule(f, "doors.none"));
    CHECK(has_rule(f, "dims.none"));
    CHECK(has_rule(f, "rooms.labels"));
    CHECK(has_rule(f, "walls.hatch"));
    CHECK(has_rule(f, "glazing.none"));
}

TEST(plancheck_measures_doors_against_the_rules) {
    std::vector<gsnap::Shape> shapes(4, wall_line());
    shapes.push_back(door_arc(600));
    Plan p;
    p.shapes = &shapes;
    p.metres_per_unit = 0.001;
    auto f = check(p, Rules{});
    CHECK(has_rule(f, "doors.egress"));
    CHECK(has_rule(f, "doors.entrance"));
    shapes.push_back(door_arc(914));
    f = check(p, Rules{});
    CHECK(!has_rule(f, "doors.entrance"));
    CHECK(has_rule(f, "doors.egress"));
}

TEST(plancheck_rules_overlay_round_trips_and_overrides) {
    const std::string path =
        "/tmp/glcad-plan-rules-" + std::to_string(getpid()) + ".txt";
    Rules r;
    r.entrance_min_m = 1.0;
    r.egress_door_min_m = 0.9;
    r.source = "Testville Municipal Code 4.2";
    CHECK(save_rules(path, r));
    const Rules back = load_rules(path);
    CHECK(back.source == "Testville Municipal Code 4.2");
    CHECK(back.entrance_min_m > 0.999 && back.entrance_min_m < 1.001);
    CHECK(back.egress_door_min_m > 0.899 && back.egress_door_min_m < 0.901);

    std::vector<gsnap::Shape> shapes(4, wall_line());
    shapes.push_back(door_arc(950));
    Plan p;
    p.shapes = &shapes;
    p.metres_per_unit = 0.001;
    CHECK(!has_rule(check(p, Rules{}), "doors.entrance"));
    const auto f = check(p, back);
    CHECK(has_rule(f, "doors.entrance"));
    bool cites = false;
    for (const auto& x : f)
        if (x.rule == "doors.entrance" &&
            x.text.find("Testville") != std::string::npos)
            cites = true;
    CHECK_MSG(cites, "the finding does not cite the overlay's source");
    std::remove(path.c_str());
}

TEST(plancheck_absent_overlay_is_the_baseline) {
    const Rules r = load_rules("/tmp/glcad-no-such-rules-file.txt");
    CHECK(r.entrance_min_m > 0.863 && r.entrance_min_m < 0.865);
    CHECK(r.source.find("IRC") != std::string::npos);
}

TEST(plancheck_wants_opening_framing_shown_and_doubled_when_wide) {
    const auto stud_edges = [](gcm::i64 x, gcm::i64 y, int n) {
        std::vector<gsnap::Shape> out;
        for (int i = 0; i < n; ++i) {
            gsnap::Shape s;
            s.layer = "A-WALL";
            s.a = {x + i * 40, y};
            s.b = {x + i * 40 + 38, y};   // 38mm: a two-by's thickness
            out.push_back(s);
        }
        return out;
    };
    std::vector<gsnap::Shape> shapes(4, wall_line());
    gsnap::Shape arc = door_arc(900);
    arc.a = {0, 0};
    arc.start_angle = 0.0;
    arc.end_angle = 1.5707963;
    shapes.push_back(arc);
    Plan p;
    p.shapes = &shapes;
    p.metres_per_unit = 0.001;
    auto f = check(p, Rules{});
    CHECK(has_rule(f, "openings.framing"));

    for (auto& s : stud_edges(-120, 0, 4)) shapes.push_back(s);
    for (auto& s : stud_edges(910, 0, 4)) shapes.push_back(s);
    f = check(p, Rules{});
    CHECK(!has_rule(f, "openings.framing"));
    CHECK(!has_rule(f, "openings.jacks"));

    gsnap::Shape wide = door_arc(2440);
    wide.a = {10000, 0};
    wide.start_angle = 0.0;
    wide.end_angle = 1.5707963;
    shapes.push_back(wide);
    for (auto& s : stud_edges(9880, 0, 4)) shapes.push_back(s);
    for (auto& s : stud_edges(12450, 0, 4)) shapes.push_back(s);
    f = check(p, Rules{});
    CHECK(has_rule(f, "openings.jacks"));
    for (auto& s : stud_edges(9880, 60, 2)) shapes.push_back(s);
    for (auto& s : stud_edges(12450, 60, 2)) shapes.push_back(s);
    f = check(p, Rules{});
    CHECK(!has_rule(f, "openings.jacks"));
}

TEST(plancheck_flags_wall_lines_through_an_opening) {
    std::vector<gsnap::Shape> shapes(4, wall_line());
    gsnap::Shape arc = door_arc(900);
    arc.a = {2000, 0};
    arc.start_angle = 0.0;
    arc.end_angle = 1.5707963;
    shapes.push_back(arc);
    gsnap::Shape face;
    face.layer = "A-WALL";
    face.a = {0, 80};
    face.b = {5000, 80};
    shapes.push_back(face);
    Plan p;
    p.shapes = &shapes;
    p.metres_per_unit = 0.001;
    auto f = check(p, Rules{});
    CHECK(has_rule(f, "openings.blocked"));
    shapes.back().b = {1990, 80};
    gsnap::Shape rest = face;
    rest.a = {2910, 80};
    shapes.push_back(rest);
    f = check(p, Rules{});
    CHECK(!has_rule(f, "openings.blocked"));
}

TEST(plancheck_locates_door_swing_wall_collisions_without_flagging_jamb_contact) {
    auto door=door_arc(1000);door.key=11;door.start_angle=0;door.end_angle=1.5707963267948966;
    auto wall=wall_line();wall.key=22;wall.a={950,0};wall.b={950,2000};
    std::vector<gsnap::Shape> shapes{door,wall};Plan p;p.shapes=&shapes;p.metres_per_unit=.001;
    auto found=door_swing_collisions(p);CHECK(found.size()==1);
    if(!found.empty()) {
        CHECK(found[0].located);CHECK(found[0].shape_keys==std::vector<gcm::u32>({11,22}));
        CHECK(found[0].location.x==950);CHECK(found[0].location.y>0&&found[0].location.y<313);
    }
    CHECK(has_rule(check(p,Rules{}),"doors.swing_collision"));
    shapes[1].a={1000,0};shapes[1].b={1000,2000};CHECK(door_swing_collisions(p).empty());
    shapes[1].a={0,0};shapes[1].b={0,2000};CHECK(door_swing_collisions(p).empty());
    shapes[1].a={300,300};shapes[1].b={500,300};CHECK(door_swing_collisions(p).size()==1);
    shapes[0].start_angle=-1.5707963267948966;shapes[0].end_angle=0;
    shapes[1].a={950,-2000};shapes[1].b={950,0};CHECK(door_swing_collisions(p).size()==1);
    shapes[1].a={950,10};shapes[1].b={950,2000};CHECK(door_swing_collisions(p).empty());
}
