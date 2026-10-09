#include "test.hpp"

#include <cmath>
#include <limits>

#include "render/viewcube.hpp"

using namespace gview;

static bool near(f64 a, f64 b, f64 tol = 1e-9) { return std::fabs(a - b) < tol; }
constexpr f64 kPi = 3.14159265358979323846;

TEST(camera_basis_is_orthonormal) {
    Camera c;
    for (auto v : {StandardView::Front, StandardView::Top, StandardView::Iso,
                   StandardView::IsoNW, StandardView::Left}) {
        c.look(v);
        const Vec3 f = c.forward(), r = c.right(), u = c.up();

        CHECK(near(ggeo::d3::length(f).to_f64(), 1.0, 1e-9));
        CHECK(near(ggeo::d3::length(r).to_f64(), 1.0, 1e-9));
        CHECK(near(ggeo::d3::length(u).to_f64(), 1.0, 1e-9));

        CHECK_MSG(near(ggeo::d3::dot(f, r).to_f64(), 0.0, 1e-9),
                  "forward.right = %.3e in %s", ggeo::d3::dot(f, r).to_f64(),
                  view_name(v));
        CHECK(near(ggeo::d3::dot(f, u).to_f64(), 0.0, 1e-9));
        CHECK(near(ggeo::d3::dot(r, u).to_f64(), 0.0, 1e-9));
    }
}

TEST(camera_standard_views_point_the_right_way) {
    Camera c;
    c.look(StandardView::Top);
    CHECK_MSG(c.forward().z.to_f64() < -0.999, "Top must look down");

    c.look(StandardView::Bottom);
    CHECK(c.forward().z.to_f64() > 0.999);

    c.look(StandardView::Front);
    CHECK_MSG(c.forward().y.to_f64() > 0.999, "Front must look along +Y");

    c.look(StandardView::Back);
    CHECK(c.forward().y.to_f64() < -0.999);

    c.look(StandardView::Right);
    CHECK(c.forward().x.to_f64() < -0.999);

    c.look(StandardView::Left);
    CHECK(c.forward().x.to_f64() > 0.999);
}

TEST(camera_axis_alignment_detects_2d_ready_views) {
    Camera c;
    for (auto v : {StandardView::Top, StandardView::Front, StandardView::Right,
                   StandardView::Left, StandardView::Back, StandardView::Bottom})
        { c.look(v); CHECK_MSG(c.is_axis_aligned(1e-6), "%s not axis-aligned",
                               view_name(v)); }

    c.look(StandardView::Iso);
    CHECK(!c.is_axis_aligned(1e-6));
}

TEST(camera_elevation_is_clamped_at_the_poles) {
    Camera c;
    c.set_orbit(0.0, 10.0);
    CHECK(c.elevation() < kPi * 0.5);
    CHECK(near(ggeo::d3::length(c.up()).to_f64(), 1.0, 1e-9));

    c.set_orbit(0.0, -10.0);
    CHECK(c.elevation() > -kPi * 0.5);
    CHECK(near(ggeo::d3::length(c.up()).to_f64(), 1.0, 1e-9));
}

TEST(camera_orbit_preserves_target_and_distance) {
    Camera c;
    c.set_target(Point3::from(3, 4, 5));
    c.set_distance(25.0);

    c.orbit(0.7, 0.3);
    CHECK(near(c.distance(), 25.0));
    CHECK(near(c.target().x.to_f64(), 3.0));
    CHECK(near(c.target().z.to_f64(), 5.0));

    const Point3 e = c.eye();
    const f64 d = std::sqrt(std::pow(e.x.to_f64() - 3, 2) +
                            std::pow(e.y.to_f64() - 4, 2) +
                            std::pow(e.z.to_f64() - 5, 2));
    CHECK(near(d, 25.0, 1e-9));
}

TEST(camera_dolly_scales_distance) {
    Camera c;
    c.set_distance(10.0);
    c.dolly(2.0);
    CHECK(near(c.distance(), 20.0));
    c.dolly(0.25);
    CHECK(near(c.distance(), 5.0));
    for (int i = 0; i < 200; ++i) c.dolly(0.5);
    CHECK(c.distance() > 0.0);
}

TEST(camera_pan_moves_the_target_in_the_view_plane) {
    Camera c;
    c.look(StandardView::Top);
    c.set_viewport(1000, 800);
    c.set_distance(10.0);
    const Point3 before = c.target();

    c.pan(100, 0);
    CHECK_MSG(!near(c.target().x.to_f64(), before.x.to_f64(), 1e-9) ||
              !near(c.target().y.to_f64(), before.y.to_f64(), 1e-9),
              "pan did not move the target");
    CHECK(near(c.target().z.to_f64(), before.z.to_f64(), 1e-9));
}

TEST(camera_zoom_to_frames_a_box) {
    Camera c;
    c.set_viewport(1000, 1000);
    auto box = ggeo::d3::AABB<gcm::F64>::empty();
    box.expand(Point3::from(-5, -5, -5));
    box.expand(Point3::from(5, 5, 5));

    c.zoom_to(box);
    CHECK(near(c.target().x.to_f64(), 0.0, 1e-9));
    CHECK(near(c.target().z.to_f64(), 0.0, 1e-9));
    CHECK_MSG(c.distance() > 5.0, "distance %.3f too close to frame the box",
              c.distance());

    for (f64 x : {-5.0, 5.0}) for (f64 y : {-5.0, 5.0}) for (f64 z : {-5.0, 5.0}) {
        f64 px, py;
        CHECK(c.project(Point3::from(x, y, z), px, py));
        CHECK_MSG(px >= -1.0 && px <= 1001.0 && py >= -1.0 && py <= 1001.0,
                  "corner projected to (%.1f, %.1f)", px, py);
    }
}

TEST(camera_project_and_unproject_round_trip) {
    Camera c;
    c.set_viewport(1200, 800);
    c.look(StandardView::Iso);
    c.set_distance(30.0);

    for (auto proj : {Projection::Perspective, Projection::Orthographic}) {
        c.set_projection(proj);
        for (f64 px : {100.0, 600.0, 1100.0})
            for (f64 py : {100.0, 400.0, 700.0}) {
                const auto ray = c.ray_through(px, py);
                const Point3 hit = ray.at(gcm::F64::from(30.0));
                f64 bx, by;
                CHECK(c.project(hit, bx, by));
                CHECK_MSG(near(bx, px, 1e-6) && near(by, py, 1e-6),
                          "(%.1f,%.1f) -> (%.4f,%.4f)", px, py, bx, by);
            }
    }
}

TEST(camera_orthographic_rays_are_parallel) {
    Camera c;
    c.set_viewport(1000, 800);
    c.set_projection(Projection::Orthographic);
    c.look(StandardView::Iso);

    const auto a = c.ray_through(100, 100);
    const auto b = c.ray_through(900, 700);
    const f64 d = ggeo::d3::dot(a.direction, b.direction).to_f64();
    CHECK_MSG(near(d, 1.0, 1e-9), "ortho ray directions differ (dot %.9f)", d);
}

TEST(camera_perspective_rays_diverge) {
    Camera c;
    c.set_viewport(1000, 800);
    c.set_projection(Projection::Perspective);
    const auto a = c.ray_through(100, 100);
    const auto b = c.ray_through(900, 700);
    CHECK(ggeo::d3::dot(a.direction, b.direction).to_f64() < 0.999);
}

TEST(camera_projection_toggle_preserves_apparent_size) {
    Camera c;
    c.set_viewport(1000, 1000);
    c.look(StandardView::Front);
    c.set_distance(20.0);

    f64 px1, py1, px2, py2;
    const Point3 p = Point3::from(1, 0, 0);
    CHECK(c.project(p, px1, py1));
    c.toggle_projection();
    CHECK(c.project(p, px2, py2));

    CHECK_MSG(std::fabs(px1 - px2) < 2.0,
              "toggling projection moved the point %.2f px", std::fabs(px1 - px2));
}

TEST(viewcube_has_26_pickable_parts) {
    const auto& p = ViewCube::parts();
    CHECK_MSG(p.size() == 26, "expected 26 parts, got %zu", p.size());

    std::size_t faces = 0, edges = 0, corners = 0;
    for (const auto& q : p) {
        if (q.region == CubeRegion::Face) ++faces;
        if (q.region == CubeRegion::Edge) ++edges;
        if (q.region == CubeRegion::Corner) ++corners;
    }
    CHECK(faces == 6);
    CHECK(edges == 12);
    CHECK(corners == 8);
}

TEST(viewcube_sits_in_the_top_right) {
    const auto l = CubeLayout::top_right(1440, 900);
    CHECK(l.x + l.size <= 1440);
    CHECK(l.y >= 0);
    CHECK(l.x > 1440 * 0.7);
    CHECK(l.contains(l.x + 5, l.y + 5));
    CHECK(!l.contains(10, 10));
}

TEST(viewcube_back_faces_are_not_pickable) {
    Camera c;
    c.look(StandardView::Top);
    for (const auto& p : ViewCube::parts()) {
        if (p.region != CubeRegion::Face) continue;
        if (p.label == "TOP")    CHECK(ViewCube::facing_camera(c, p));
        if (p.label == "BOTTOM") CHECK(!ViewCube::facing_camera(c, p));
    }
}

TEST(viewcube_clicking_a_face_snaps_the_camera) {
    Camera c;
    c.set_viewport(1440, 900);
    c.look(StandardView::Iso);
    const auto layout = CubeLayout::top_right(1440, 900);

    for (const auto& p : ViewCube::parts()) {
        if (p.label != "TOP") continue;
        f64 px, py;
        CHECK(ViewCube::part_screen_position(c, layout, p, px, py));

        const CubePart hit = ViewCube::pick(c, layout, px, py);
        CHECK_MSG(hit.valid(), "picking the TOP face centre found nothing");

        ViewCube::apply(c, hit);
        CHECK_MSG(c.forward().z.to_f64() < -0.99,
                  "after clicking TOP the camera looks (%.3f,%.3f,%.3f)",
                  c.forward().x.to_f64(), c.forward().y.to_f64(),
                  c.forward().z.to_f64());
    }
}

TEST(viewcube_face_orientations_match_standard_views) {
    Camera cube; cube.set_viewport(1000, 1000);
    Camera std_cam; std_cam.set_viewport(1000, 1000);

    struct { const char* label; StandardView view; } pairs[] = {
        {"TOP", StandardView::Top},     {"BOTTOM", StandardView::Bottom},
        {"FRONT", StandardView::Front}, {"BACK", StandardView::Back},
        {"RIGHT", StandardView::Right}, {"LEFT", StandardView::Left},
    };

    for (const auto& pr : pairs) {
        for (const auto& p : ViewCube::parts()) {
            if (p.label != pr.label) continue;
            ViewCube::apply(cube, p);
            std_cam.look(pr.view);
            const f64 d = ggeo::d3::dot(cube.forward(), std_cam.forward()).to_f64();
            CHECK_MSG(near(d, 1.0, 1e-6),
                      "cube face %s does not match view %s (dot %.6f)",
                      pr.label, view_name(pr.view), d);
        }
    }
}

TEST(viewcube_corner_gives_an_isometric_view) {
    Camera c;
    c.set_viewport(1000, 1000);
    for (const auto& p : ViewCube::parts()) {
        if (p.region != CubeRegion::Corner) continue;
        ViewCube::apply(c, p);
        const Vec3 f = c.forward();
        CHECK(std::fabs(f.x.to_f64()) > 0.3);
        CHECK(std::fabs(f.y.to_f64()) > 0.3);
        CHECK(std::fabs(f.z.to_f64()) > 0.3);
        CHECK(!c.is_axis_aligned(1e-6));
    }
}

TEST(viewcube_pick_misses_outside_the_widget) {
    Camera c;
    c.set_viewport(1440, 900);
    const auto layout = CubeLayout::top_right(1440, 900);
    CHECK(!ViewCube::pick(c, layout, 100, 500).valid());
    CHECK(!ViewCube::pick(c, layout, 720, 450).valid());
}

TEST(viewcube_reports_the_nearest_standard_view) {
    Camera c;
    c.look(StandardView::Top);
    CHECK(c.nearest_standard() == StandardView::Top);

    c.look(StandardView::Front);
    CHECK(c.nearest_standard() == StandardView::Front);

    c.orbit(0.02, 0.02);
    CHECK(c.nearest_standard() == StandardView::Front);
}

TEST(viewcube_top_label_follows_the_orientation) {
    Camera c;
    c.look(StandardView::Front);
    CHECK_MSG(ViewCube::top_label(c) == "TOP",
              "top label was \"%s\"", ViewCube::top_label(c).c_str());
}

TEST(camera_plan_pose_is_top_down_orthographic) {
    Camera c;
    c.set_viewport(800, 600);
    c.look(StandardView::Iso);
    CHECK(!c.is_plan());
    c.look_plan();
    CHECK(c.is_plan());
    CHECK(c.projection() == Projection::Orthographic);

    const Vec3 r = c.right(), u = c.up();
    CHECK_MSG(std::fabs(r.x.to_f64() - 1.0) < 1e-6 && std::fabs(r.y.to_f64()) < 1e-6,
              "plan right = (%.4f, %.4f, %.4f)", r.x.to_f64(), r.y.to_f64(), r.z.to_f64());
    CHECK_MSG(std::fabs(u.y.to_f64() - 1.0) < 1e-6 && std::fabs(u.x.to_f64()) < 1e-6,
              "plan up = (%.4f, %.4f, %.4f)", u.x.to_f64(), u.y.to_f64(), u.z.to_f64());
}

TEST(camera_plan_projection_matches_the_2d_mapping) {
    Camera c;
    c.set_viewport(800, 600);
    c.look_plan();
    c.set_target(Point3::from(3.0, 2.0, 0.0));

    const f64 scale = 600.0 / (2.0 * c.ortho_half_height());
    for (auto [wx, wy] : {std::pair{3.0, 2.0}, std::pair{5.0, 2.0},
                          std::pair{3.0, 4.0}, std::pair{-1.0, -7.0}}) {
        f64 px, py;
        CHECK(c.project(Point3::from(wx, wy, 0.0), px, py));
        const f64 ex = (wx - 3.0) * scale + 400.0;
        const f64 ey = 300.0 - (wy - 2.0) * scale;
        CHECK_MSG(std::fabs(px - ex) < 1e-6 && std::fabs(py - ey) < 1e-6,
                  "(%g,%g) projected to (%.3f,%.3f), expected (%.3f,%.3f)",
                  wx, wy, px, py, ex, ey);
    }
}

TEST(camera_plane_hit_inverts_project) {
    Camera c;
    c.set_viewport(800, 600);
    for (StandardView v : {StandardView::Top, StandardView::Iso, StandardView::IsoNW}) {
        c.look(v);
        for (auto [wx, wy] : {std::pair{0.0, 0.0}, std::pair{4.0, -3.0}, std::pair{-2.5, 6.0}}) {
            f64 px, py, hx, hy;
            CHECK(c.project(Point3::from(wx, wy, 0.0), px, py));
            CHECK(c.plane_hit(px, py, 0.0, hx, hy));
            CHECK_MSG(std::fabs(hx - wx) < 1e-6 && std::fabs(hy - wy) < 1e-6,
                      "%s: (%g,%g) -> pixel -> (%.4f,%.4f)", view_name(v), wx, wy, hx, hy);
        }
    }
}

TEST(camera_plane_hit_fails_when_looking_at_the_horizon) {
    Camera c;
    c.set_viewport(800, 600);
    c.look(StandardView::Front);
    c.set_target(Point3::from(0.0, 0.0, 1.0));
    f64 hx, hy;
    CHECK(!c.plane_hit(400.0, 10.0, 0.0, hx, hy));
}

TEST(camera_zoom_at_keeps_the_point_under_the_cursor) {
    Camera c;
    c.set_viewport(800, 600);
    for (StandardView v : {StandardView::Top, StandardView::Iso,StandardView::Right,StandardView::Front}) {
        c.look(v);
        c.set_target(Point3::from(0.0, 0.0, 1.0));
        const f64 px = 650.0, py = 120.0;
        const auto on=c.target();const auto n=c.forward();Point3 before,after;
        CHECK(c.plane_hit(px, py, on,n,before));
        c.zoom_at(px, py, 0.5);
        c.zoom_at(px, py, 1.3);
        CHECK(c.plane_hit(px, py, on,n,after));
        CHECK(std::fabs(after.x.to_f64()-before.x.to_f64())<1e-6);
        CHECK(std::fabs(after.y.to_f64()-before.y.to_f64())<1e-6);
        CHECK(std::fabs(after.z.to_f64()-before.z.to_f64())<1e-6);
    }
}

TEST(camera_a_plan_view_zoomed_in_close_still_shows_and_picks_the_plane) {
    Camera c;
    c.set_viewport(800, 600);
    c.look_plan();
    for (int i = 0; i < 40; ++i) c.zoom_at(400, 300, 0.7);
    CHECK(c.distance() < 0.01);
    const Mat4 vp = c.view_projection();
    const f64 z = vp.m[2] * 0.0 + vp.m[6] * 0.0 + vp.m[10] * 0.0 + vp.m[14];
    const f64 w = vp.m[3] * 0.0 + vp.m[7] * 0.0 + vp.m[11] * 0.0 + vp.m[15];
    const f64 depth = z / w;
    CHECK_MSG(depth >= 0.0 && depth <= 1.0, "the plane's depth is %.4f: clipped", depth);
    f64 hx, hy;
    CHECK(c.plane_hit(400.0, 300.0, 0.0, hx, hy));
    c.set_target(Point3::from(0.0, 0.0, 1.0));
    c.set_distance(0.5);
    CHECK(c.plane_hit(400.0, 300.0, 0.0, hx, hy));
}

TEST(camera_side_view_zoom_near_horizon_does_not_throw_work_away) {
    for (double elevation:{0.0,1e-10,-1e-10,1e-6,-1e-6}) {
        Camera c;c.set_viewport(1200,800);c.set_orbit(0,elevation);
        c.set_target(Point3::from(0,0,1));c.set_distance(6);
        const auto initial=c.target();
        for(int i=0;i<100;++i) {c.zoom_at(750,400.001,.9);c.zoom_at(750,400.001,1/.9);}
        CHECK(std::fabs(c.distance()-6)<1e-9);
        CHECK(std::fabs(c.target().x.to_f64()-initial.x.to_f64())<1e-8);
        CHECK(std::fabs(c.target().y.to_f64()-initial.y.to_f64())<1e-8);
        CHECK(std::fabs(c.target().z.to_f64()-initial.z.to_f64())<1e-8);
        c.zoom_at(750,400,std::numeric_limits<double>::quiet_NaN());
        c.zoom_at(750,400,std::numeric_limits<double>::infinity());
        CHECK(std::isfinite(c.distance()));CHECK(std::fabs(c.distance()-6)<1e-9);
    }
}

TEST(camera_selected_depth_stays_under_the_pointer_during_close_navigation) {
    for(auto projection:{Projection::Orthographic,Projection::Perspective}) {
        Camera c;c.set_viewport(1000,800);c.look(StandardView::Iso);
        c.set_projection(projection);c.set_target(Point3::from(0,0,0));c.set_distance(4);
        const auto picked=c.eye()+c.forward()*gcm::F64::from(.8)+c.right()*gcm::F64::from(.05);
        double x,y;CHECK(c.project(picked,x,y));
        for(int i=0;i<15;++i) {
            c.zoom_at(x,y,.95,picked);
            double a,b;CHECK(c.project(picked,a,b));CHECK(near(a,x,1e-6));CHECK(near(b,y,1e-6));
        }
        c.pan(25,-15,picked);
        double a,b;CHECK(c.project(picked,a,b));CHECK(near(a,x+25,1e-6));CHECK(near(b,y-15,1e-6));
        c.orbit_about(picked,.13,-.07);
        double ox,oy;CHECK(c.project(picked,ox,oy));CHECK(near(ox,a,1e-6));CHECK(near(oy,b,1e-6));
    }
}

TEST(camera_fit_region_contains_all_corners_in_both_projections) {
    for(auto projection:{Projection::Perspective,Projection::Orthographic})
    for(auto view:{StandardView::Top,StandardView::Iso,StandardView::Front})
    for(auto size:{std::pair{240.,5.},std::pair{5.,240.},std::pair{40.,40.}}) {
        Camera camera;camera.set_viewport(1600,1000);camera.look(view);camera.set_projection(projection);
        auto bounds=ggeo::d3::AABB<gcm::F64>::empty();
        bounds.expand(Point3::from(12,23,-7));bounds.expand(Point3::from(12+size.first,23+size.second,17));
        camera.zoom_to_region(bounds,80,300,1050,620);
        for(int i=0;i<8;++i) {
            double x,y;
            CHECK(camera.project(Point3::from((i&1)?bounds.hi.x.to_f64():bounds.lo.x.to_f64(),
                (i&2)?bounds.hi.y.to_f64():bounds.lo.y.to_f64(),(i&4)?17:-7),x,y));
            CHECK_MSG(x>=80 && x<=1130 && y>=300 && y<=920,"fitted corner %.2f %.2f",x,y);
        }
    }
}

TEST(camera_orthographic_region_fit_uses_projected_size_of_deep_solids) {
    Camera camera;camera.set_viewport(1200,800);camera.look(StandardView::Top);
    camera.set_projection(Projection::Orthographic);
    auto bounds=ggeo::d3::AABB<gcm::F64>::empty();
    bounds.expand(Point3::from(0,0,0));bounds.expand(Point3::from(1,1,100));
    camera.zoom_to_region(bounds,100,200,700,500);
    double left,top,right,bottom;
    CHECK(camera.project(Point3::from(0,0,50),left,top));
    CHECK(camera.project(Point3::from(1,1,50),right,bottom));
    CHECK(std::fabs(right-left)>400);
    CHECK(std::fabs(top-bottom)>400);
    for(int i=0;i<8;++i) {
        double x,y;
        CHECK(camera.project(Point3::from((i&1)?1:0,(i&2)?1:0,(i&4)?100:0),x,y));
        CHECK(x>=100 && x<=800 && y>=200 && y<=700);
    }
}
