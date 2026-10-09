#include "test.hpp"

#include <algorithm>
#include <cmath>

#include "engine/hatch.hpp"

using namespace gdoc;

static gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

static Point2 pt(const gcm::WorkingVolume& v, f64 x, f64 y) {
    return gcm::to_lattice(v, x, y);
}

static std::vector<Segment2> square(const gcm::WorkingVolume& v, f64 x0, f64 y0,
                                    f64 x1, f64 y1) {
    return {{pt(v, x0, y0), pt(v, x1, y0)},
            {pt(v, x1, y0), pt(v, x1, y1)},
            {pt(v, x1, y1), pt(v, x0, y1)},
            {pt(v, x0, y1), pt(v, x0, y0)}};
}

TEST(hatch_point_in_loop_is_exact) {
    const auto v = vol();
    Loop l = {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)};

    CHECK(point_in_loop(pt(v, 5, 5), l));
    CHECK(!point_in_loop(pt(v, 15, 5), l));
    CHECK(!point_in_loop(pt(v, -1, 5), l));
    CHECK(!point_in_loop(pt(v, 5, 20), l));

    CHECK(point_in_loop(pt(v, 0, 5), l));
    CHECK(point_in_loop(pt(v, 5, 0), l));
}

TEST(hatch_point_in_loop_handles_concave) {
    const auto v = vol();
    Loop l = {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 4),
              pt(v, 4, 4),  pt(v, 4, 10), pt(v, 0, 10)};
    CHECK(point_in_loop(pt(v, 2, 2), l));
    CHECK(point_in_loop(pt(v, 8, 2), l));
    CHECK(point_in_loop(pt(v, 2, 8), l));
    CHECK_MSG(!point_in_loop(pt(v, 8, 8), l), "the notch must read as outside");
}

TEST(hatch_islands_punch_holes) {
    const auto v = vol();
    std::vector<Loop> region = {
        {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)},   // outer
        {pt(v, 4, 4), pt(v, 6, 4),  pt(v, 6, 6),   pt(v, 4, 6)},    // island
    };
    CHECK(point_in_region(pt(v, 1, 1), region));
    CHECK_MSG(!point_in_region(pt(v, 5, 5), region), "island must read as outside");
    CHECK(!point_in_region(pt(v, 20, 20), region));
}

TEST(hatch_loop_orientation_is_exact) {
    const auto v = vol();
    Loop ccw = {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)};
    Loop cw(ccw.rbegin(), ccw.rend());
    CHECK(loop_area_sign(ccw) == 1);
    CHECK(loop_area_sign(cw) == -1);
}

TEST(hatch_extracts_a_closed_loop_from_segments) {
    const auto v = vol();
    const auto r = extract_loops(square(v, 0, 0, 10, 10));
    CHECK_MSG(r.ok(), "%s", r.message(v).c_str());
    CHECK(r.loops.size() == 1);
    CHECK(r.loops[0].size() == 4);
}

TEST(hatch_reports_where_the_boundary_is_open) {
    const auto v = vol();
    auto segs = square(v, 0, 0, 10, 10);
    segs.pop_back();                       // leave one side open

    const auto r = extract_loops(segs);
    CHECK(!r.ok());
    CHECK(r.error == BoundaryError::NotClosed);

    const std::string msg = r.message(v);
    CHECK_MSG(msg.find("gap near") != std::string::npos, "message was: %s", msg.c_str());

    f64 gx, gy;
    gcm::to_world(v, r.gap_at, gx, gy);
    const bool at_corner = (std::fabs(gx) < 1e-3 && std::fabs(gy) < 1e-3) ||
                           (std::fabs(gx) < 1e-3 && std::fabs(gy - 10.0) < 1e-3);
    CHECK_MSG(at_corner, "gap reported at (%.3f, %.3f)", gx, gy);
}

TEST(hatch_rejects_too_few_segments) {
    const auto v = vol();
    const auto r = extract_loops({{pt(v, 0, 0), pt(v, 1, 0)}});
    CHECK(r.error == BoundaryError::TooFewSegments);
}

TEST(hatch_trace_region_from_a_picked_point) {
    const auto v = vol();
    auto segs = square(v, 0, 0, 10, 10);
    const auto inner = square(v, 4, 4, 6, 6);
    segs.insert(segs.end(), inner.begin(), inner.end());

    const auto r = trace_region(segs, pt(v, 1, 1));
    CHECK_MSG(r.ok(), "%s", r.message(v).c_str());
    CHECK_MSG(r.loops.size() == 2, "expected outer + island, got %zu", r.loops.size());

    const auto miss = trace_region(segs, pt(v, 50, 50));
    CHECK(!miss.ok());
    CHECK(miss.error == BoundaryError::NoEnclosingLoop);
}

TEST(hatch_pattern_table_has_the_standard_set) {
    PatternTable t;
    for (const char* n : {"SOLID", "ANSI31", "ANSI32", "ANSI37", "NET",
                          "EARTH", "CONCRETE", "INSUL"})
        CHECK_MSG(t.find(n) != nullptr, "pattern %s missing", n);
    CHECK(t.find("ansi31") != nullptr);
    CHECK(t.find("SOLID")->solid());
    CHECK(!t.find("ANSI31")->solid());
    CHECK(t.find("ANSI37")->lines.size() == 2);
}

TEST(hatch_pat_import) {
    PatternTable t;
    const char* pat =
        "; a comment\n"
        "*MYHATCH, my custom hatch\n"
        "0, 0,0, 0,0.25\n"
        "90, 0,0, 0,0.25\n"
        "*OTHER, another\n"
        "45, 0,0, 0,0.5, 0.25,-0.25\n";
    CHECK(t.load_pat(pat) == 2);
    CHECK(t.find("MYHATCH") != nullptr);
    CHECK(t.find("MYHATCH")->lines.size() == 2);
    CHECK(t.find("OTHER")->lines[0].dashes.size() == 2);
}

TEST(hatch_solid_fill_returns_the_region) {
    const auto v = vol();
    PatternTable pats;
    const std::vector<Loop> loops = {
        {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)}};

    HatchStyle st;
    st.name = "SOLID";
    const Hatch h = generate_hatch(loops, *pats.find("SOLID"), st, v);
    CHECK(h.solid);
    CHECK(h.fill_loops.size() == 1);
    CHECK(h.lines.empty());
}

TEST(hatch_line_pattern_fills_the_area) {
    const auto v = vol();
    PatternTable pats;
    const std::vector<Loop> loops = {
        {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)}};

    HatchStyle st;
    st.name = "ANSI31";
    st.scale = 8.0;
    const Hatch h = generate_hatch(loops, *pats.find("ANSI31"), st, v);

    CHECK(!h.solid);
    CHECK_MSG(!h.lines.empty(), "ANSI31 produced no hatch lines");

    int checked = 0;
    for (const auto& s : h.lines) {
        const Point2 mid{(s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2};
        CHECK(point_in_region(mid, loops));
        if (++checked > 40) break;
    }
}

TEST(hatch_respects_islands) {
    const auto v = vol();
    PatternTable pats;
    const std::vector<Loop> loops = {
        {pt(v, 0, 0), pt(v, 20, 0), pt(v, 20, 20), pt(v, 0, 20)},
        {pt(v, 8, 8), pt(v, 12, 8), pt(v, 12, 12), pt(v, 8, 12)},
    };

    HatchStyle st;
    st.name = "LINE";
    st.scale = 8.0;
    const Hatch h = generate_hatch(loops, *pats.find("LINE"), st, v);
    CHECK(!h.lines.empty());

    for (const auto& s : h.lines) {
        const Point2 mid{(s.a.x + s.b.x) / 2, (s.a.y + s.b.y) / 2};
        CHECK_MSG(point_in_region(mid, loops), "a hatch span crossed the island: (%lld,%lld)-(%lld,%lld)", (long long)s.a.x,(long long)s.a.y,(long long)s.b.x,(long long)s.b.y);
    }
}

TEST(hatch_scale_changes_line_count) {
    const auto v = vol();
    PatternTable pats;
    const std::vector<Loop> loops = {
        {pt(v, 0, 0), pt(v, 10, 0), pt(v, 10, 10), pt(v, 0, 10)}};

    HatchStyle coarse; coarse.scale = 16.0;
    HatchStyle fine;   fine.scale = 8.0;

    const auto a = generate_hatch(loops, *pats.find("LINE"), coarse, v);
    const auto b = generate_hatch(loops, *pats.find("LINE"), fine, v);
    CHECK_MSG(b.lines.size() > a.lines.size(),
              "finer scale gave %zu lines, coarser gave %zu",
              b.lines.size(), a.lines.size());
}

TEST(a_closed_room_traces_even_when_the_drawing_is_full_of_loose_lines) {
    std::vector<gdoc::Segment2> segs;
    const auto line = [&](double x0, double y0, double x1, double y1) {
        gcm::WorkingVolume v; v.extent = 500.0; v.resolution_bits = 32;
        segs.push_back({gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y1)});
    };
    line(0, 0, 4, 0); line(4, 0, 4, 4); line(4, 4, 0, 4); line(0, 4, 0, 0);
    line(10, 10, 12, 11);
    line(-3, 2, -1, 2);
    line(2, 8, 2, 9);
    line(6, 1, 7, 1); line(7, 1, 7, 2);   // an open elbow, dangling both ends

    gcm::WorkingVolume v; v.extent = 500.0; v.resolution_bits = 32;
    const auto r = gdoc::trace_region(segs, gcm::to_lattice(v, 2.0, 2.0));
    CHECK_MSG(r.ok(), "the room did not trace (error %d)", int(r.error));
    CHECK_MSG(!r.loops.empty() && r.loops[0].size() >= 4,
              "the outer loop came back with %zu points",
              r.loops.empty() ? 0 : r.loops[0].size());
}

TEST(a_boundary_that_really_is_open_still_says_so) {
    std::vector<gdoc::Segment2> segs;
    gcm::WorkingVolume v; v.extent = 500.0; v.resolution_bits = 32;
    const auto line = [&](double x0, double y0, double x1, double y1) {
        segs.push_back({gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y1)});
    };
    line(0, 0, 4, 0); line(4, 0, 4, 4); line(4, 4, 0, 4);   // three sides only

    const auto r = gdoc::trace_region(segs, gcm::to_lattice(v, 2.0, 2.0));
    CHECK_MSG(!r.ok(), "an open square traced as if it were closed");
}

TEST(hatch_intersections_preserve_thin_walls_and_narrow_cavities) {
    const auto v=vol();
    const auto rectangle=[&](double x0,double y0,double x1,double y1) {
        return Loop{pt(v,x0,y0),pt(v,x1,y0),pt(v,x1,y1),pt(v,x0,y1)};
    };
    HatchPattern pattern{"horizontal","",{{0,0,0,0,1,{}}}};
    HatchStyle style;style.scale=1;
    const auto thin=rectangle(5.123,0,5.125,10);
    const auto h=generate_hatch({thin},pattern,style,v);
    CHECK(h.lines.size()>=9);
    for(const auto& line:h.lines) {
        CHECK(line.a.x==thin[0].x);CHECK(line.b.x==thin[1].x);
        CHECK(line.a.y==line.b.y);
    }
    const std::vector<Loop> loops{rectangle(0,0,10,10),rectangle(5.123,2,5.125,8)};
    const auto cut=generate_hatch(loops,pattern,style,v);
    std::size_t split=0;
    for(const auto& line:cut.lines) {
        const auto midpoint=Point2{(line.a.x+line.b.x)/2,(line.a.y+line.b.y)/2};
        CHECK(point_in_region(midpoint,loops));
        if(line.a.y>loops[1][0].y && line.a.y<loops[1][2].y) {
            CHECK(line.b.x<=loops[1][0].x || line.a.x>=loops[1][1].x);++split;
        }
    }
    CHECK(split>=8);
}

TEST(hatch_intersections_clip_dashes_without_resetting_phase_at_islands) {
    const auto v=vol();
    const std::vector<Loop> loops{
        {pt(v,-5,-2),pt(v,5,-2),pt(v,5,2),pt(v,-5,2)},
        {pt(v,-.25,-1),pt(v,.25,-1),pt(v,.25,1),pt(v,-.25,1)}};
    HatchPattern pattern{"dash","",{{0,0,0,0,1,{1,-1}}}};
    HatchStyle style;style.scale=1;
    const auto h=generate_hatch(loops,pattern,style,v);
    bool clipped=false;
    for(const auto& line:h.lines) {
        double ax,ay,bx,by;gcm::to_world(v,line.a,ax,ay);gcm::to_world(v,line.b,bx,by);
        const double mid=(ax+bx)*.5;
        double phase=std::fmod(mid,2.);if(phase<0)phase+=2;
        CHECK(phase<1.000001);CHECK(bx-ax<=1.000001);
        if(std::abs(ay)<1e-6 && std::abs(ax-.25)<1e-6)clipped=true;
    }
    CHECK(clipped);
}

TEST(hatch_intersections_handle_concavity_vertices_and_nested_islands) {
    const auto v=vol();
    const std::vector<Loop> loops{
        {pt(v,0,0),pt(v,10,0),pt(v,10,4),pt(v,4,4),pt(v,4,10),pt(v,0,10)},
        {pt(v,1,1),pt(v,3,1),pt(v,3,3),pt(v,1,3)},
        {pt(v,1.5,1.5),pt(v,2.5,1.5),pt(v,2.5,2.5),pt(v,1.5,2.5)}};
    PatternTable patterns;
    for(double angle:{0.,23.,45.,90.,135.}) {
        HatchStyle style;style.scale=4;style.angle=angle;
        const auto h=generate_hatch(loops,*patterns.find("LINE"),style,v);
        CHECK(!h.lines.empty());
        for(const auto& line:h.lines) {
            for(double t:{.1,.5,.9}) {
                double ax,ay,bx,by;gcm::to_world(v,line.a,ax,ay);gcm::to_world(v,line.b,bx,by);
                const auto p=pt(v,ax+(bx-ax)*t,ay+(by-ay)*t);
                CHECK_MSG(point_in_region(p,loops),"angle %.1f span sample outside region (%.9f,%.9f)-(%.9f,%.9f) t %.1f",angle,ax,ay,bx,by,t);
            }
        }
    }
}

TEST(hatch_dense_patterns_report_limits_without_partial_output) {
    const auto v=vol();
    const std::vector<Loop> loops{{pt(v,0,0),pt(v,10,0),pt(v,10,10),pt(v,0,10)}};
    HatchStyle style;
    HatchPattern gap{"gap","",{{0,0,0,0,1,{-1e-8}}}};
    auto h=generate_hatch(loops,gap,style,v);
    CHECK(h.error==nullptr);CHECK(h.lines.empty());
    HatchPattern dense{"dense","",{{0,0,0,0,1,{1e-8,-1e-8}}}};
    h=generate_hatch(loops,dense,style,v);
    CHECK(h.error!=nullptr);CHECK(h.lines.empty());
    HatchPattern lines{"lines","",{{0,0,0,0,1,{}}}};
    style.scale=1e-20;h=generate_hatch(loops,lines,style,v);
    CHECK(h.error!=nullptr);CHECK(h.lines.empty());
    style.scale=0;h=generate_hatch(loops,lines,style,v);
    CHECK(h.error!=nullptr);CHECK(h.lines.empty());
    PatternTable patterns;patterns.add(dense);
    AssociativeHatch attached;attached.style.name="dense";attached.boundary_shapes={0,1,2,3};
    const auto result=update_hatch(attached,square(v,0,0,10,10),patterns,v);
    CHECK(!result.regenerated);CHECK(result.reason && result.reason[0]);CHECK(!attached.valid);
}

TEST(hatch_region_does_not_depend_on_segment_order) {
    const auto v = vol();
    std::vector<Segment2> segs = square(v, 0, 0, 10, 4);
    for (const auto& s : square(v, -5, 0, 0, 4)) segs.push_back(s);
    for (const auto& s : square(v, 10, 0, 15, 4)) segs.push_back(s);
    segs.push_back({pt(v, 0, 1.5), pt(v, 10, 1.5)});
    segs.push_back({pt(v, 0, 2.5), pt(v, 10, 2.5)});
    segs.push_back({pt(v, 0, 0), pt(v, 0, 4)});
    const Point2 seed = pt(v, 5, 2);
    for (int turn = 0; turn < 12; ++turn) {
        std::rotate(segs.begin(), segs.begin() + 1, segs.end());
        if (turn % 3 == 0) std::reverse(segs.begin(), segs.end());
        const auto r = trace_region(segs, seed);
        CHECK_MSG(r.ok(), "turn %d: %s", turn, r.message(v).c_str());
        if (!r.ok()) continue;
        CHECK(r.loops.size() == 1);
        f64 lo = 1e9, hi = -1e9;
        for (const auto& p : r.loops[0]) { f64 x, y; gcm::to_world(v, p, x, y); lo = std::min(lo, y); hi = std::max(hi, y); }
        CHECK_MSG(std::fabs(lo - 1.5) < 1e-6 && std::fabs(hi - 2.5) < 1e-6, "turn %d: band %g..%g", turn, lo, hi);
    }
}

TEST(hatch_region_keeps_islands_whole_and_skips_nested_ones) {
    const auto v = vol();
    std::vector<Segment2> segs = square(v, 0, 0, 20, 20);
    for (const auto& s : square(v, 4, 4, 10, 10)) segs.push_back(s);
    segs.push_back({pt(v, 7, 4), pt(v, 7, 10)});
    for (const auto& s : square(v, 5, 5, 6, 6)) segs.push_back(s);
    const auto r = trace_region(segs, pt(v, 15, 15));
    CHECK_MSG(r.ok(), "%s", r.message(v).c_str());
    CHECK_MSG(r.loops.size() == 2, "%zu loops", r.loops.size());
    if (r.loops.size() == 2) CHECK(r.loops[1].size() >= 4);
    const auto inner = trace_region(segs, pt(v, 8.5, 7));
    CHECK(inner.ok() && inner.loops.size() == 1);
}
