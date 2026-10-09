#include "test.hpp"

#include <cmath>

#include "geometry/body.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/extrude.hpp"
#include "geometry/sketch.hpp"

using namespace gtopo;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

Loop2 rect(const gcm::WorkingVolume& v, double x0, double y0, double x1, double y1) {
    return {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
            gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
}

Body box(const gcm::WorkingVolume& v, double x0, double y0, double x1, double y1,
         double h, double z0 = 0.0) {
    Profile p;
    p.outer = rect(v, x0, y0, x1, y1);
    p.normalize();
    auto frame = ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z0);
    return extrude(p, v, frame, h);
}

FaceId face_facing(const Body& b, const gcm::WorkingVolume& v, f64 nx, f64 ny,
                   f64 nz) {
    FaceId best = kInvalid;
    f64 best_dot = -2.0;
    for (FaceId f = 0; f < b.face_count(); ++f) {
        const Plane p = face_plane(b, f, v);
        if (!p.valid()) continue;
        const f64 d = p.nx * nx + p.ny * ny + p.nz * nz;
        if (d > best_dot) { best_dot = d; best = f; }
    }
    return best;
}

gsnap::Shape seg(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = gcm::to_lattice(v, x0, y0);
    s.b = gcm::to_lattice(v, x1, y1);
    return s;
}

gsnap::Shape circ(const gcm::WorkingVolume& v, f64 cx, f64 cy, f64 r) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Circle;
    s.a = gcm::to_lattice(v, cx, cy);
    s.radius = static_cast<gcm::i64>(std::llround(r * v.scale()));
    return s;
}

}

TEST(sketch_plane_round_trips_a_point) {
    const SketchPlane p =
        SketchPlane::from_normal(1.0, 2.0, 3.0, 0.3, -0.5, 0.8);
    for (f64 x : {-4.0, 0.0, 7.5})
        for (f64 y : {-2.0, 0.0, 3.25}) {
            f64 wx, wy, wz, bx, by;
            p.to_world(x, y, wx, wy, wz);
            p.to_plane(wx, wy, wz, bx, by);
            CHECK_MSG(std::fabs(bx - x) < 1e-9 && std::fabs(by - y) < 1e-9,
                      "(%g,%g) came back as (%g,%g)", x, y, bx, by);
            CHECK(std::fabs(p.distance(wx, wy, wz)) < 1e-9);
        }
}

TEST(sketch_axes_are_orthonormal_and_right_handed) {
    for (auto n : {std::array<f64, 3>{0, 0, 1}, {1, 0, 0}, {0, 1, 0},
                   {0.5, -0.5, 0.7071}, {-1, 0, 0}, {0.1, 0.1, 0.99}}) {
        f64 u[3], v[3];
        stable_axes(n[0], n[1], n[2], u, v);
        auto d = [](const f64 a[3], const f64 b[3]) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        f64 nn[3] = {n[0], n[1], n[2]};
        const f64 len = std::sqrt(d(nn, nn));
        for (int i = 0; i < 3; ++i) nn[i] /= len;

        CHECK(std::fabs(d(u, u) - 1.0) < 1e-9);
        CHECK(std::fabs(d(v, v) - 1.0) < 1e-9);
        CHECK_MSG(std::fabs(d(u, v)) < 1e-9, "the in-plane axes are not square");
        CHECK_MSG(std::fabs(d(u, nn)) < 1e-9, "u is not in the plane");
        CHECK(std::fabs(d(v, nn)) < 1e-9);
        const f64 cx = u[1] * v[2] - u[2] * v[1];
        const f64 cy = u[2] * v[0] - u[0] * v[2];
        const f64 cz = u[0] * v[1] - u[1] * v[0];
        CHECK_MSG(std::fabs(cx - nn[0]) < 1e-9 && std::fabs(cy - nn[1]) < 1e-9 &&
                      std::fabs(cz - nn[2]) < 1e-9,
                  "the sketch frame is left-handed");
    }
}

TEST(sketch_axes_depend_only_on_the_normal) {
    f64 u1[3], v1[3], u2[3], v2[3];
    stable_axes(0.3, -0.5, 0.8, u1, v1);
    stable_axes(3.0, -5.0, 8.0, u2, v2);
    for (int i = 0; i < 3; ++i) {
        CHECK_MSG(std::fabs(u1[i] - u2[i]) < 1e-9,
                  "the same plane gave different axes");
        CHECK(std::fabs(v1[i] - v2[i]) < 1e-9);
    }
}

TEST(sketch_on_a_face_lands_on_that_face) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    CHECK(top != kInvalid);

    const auto s = sketch_on_face(b, top, v);
    CHECK_MSG(s.has_value(), "sketching on a flat top face was refused");
    const Plane fp = face_plane(b, top, v);
    CHECK(std::fabs(s->plane.nx * fp.nx + s->plane.ny * fp.ny +
                    s->plane.nz * fp.nz - 1.0) < 1e-9);
    CHECK_MSG(std::fabs(s->plane.oz - 2.0) < 1e-6,
              "the sketch origin sits at z=%g, not on the top face",
              s->plane.oz);
}

TEST(sketch_on_a_face_brings_its_boundary_in_as_reference) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    const auto s = sketch_on_face(b, top, v);
    CHECK(s.has_value());
    CHECK_MSG(s->reference.size() == 4, "%zu reference edges for a rectangle",
              s->reference.size());
    CHECK_MSG(s->geometry.empty(),
              "the reference geometry was put in the sketch itself");

    f64 lo_x = 1e30, hi_x = -1e30, lo_y = 1e30, hi_y = -1e30;
    for (const gsnap::Shape& e : s->reference) {
        f64 ax, ay, bx, by;
        gcm::to_world(v, e.a, ax, ay);
        gcm::to_world(v, e.b, bx, by);
        lo_x = std::min({lo_x, ax, bx}); hi_x = std::max({hi_x, ax, bx});
        lo_y = std::min({lo_y, ay, by}); hi_y = std::max({hi_y, ay, by});
    }
    const f64 w = hi_x - lo_x, h = hi_y - lo_y;
    CHECK_MSG((std::fabs(w - 4.0) < 1e-3 && std::fabs(h - 3.0) < 1e-3) ||
                  (std::fabs(w - 3.0) < 1e-3 && std::fabs(h - 4.0) < 1e-3),
              "the projected face is %g x %g", w, h);
}

TEST(sketch_on_a_curved_face_is_refused) {
    const auto v = vol();
    Body b = box(v, 0, 0, 4, 3, 2);
    b.face_surface.assign(b.face_count(), Surface{});
    const FaceId top = face_facing(b, v, 0, 0, 1);
    b.face_surface[top].kind = SurfaceKind::Cylinder;
    CHECK(!sketch_on_face(b, top, v).has_value());
    CHECK(!sketch_on_face(b, 999, v).has_value());
}

TEST(sketch_on_a_datum_plane_has_no_link) {
    const Sketch s = sketch_on_plane(SketchPlane::world_xy(), "Front");
    CHECK(!s.is_linked());
    CHECK(s.name == "Front");
    CHECK(s.reference.empty());
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    CHECK(check(s, b, v) == SketchState::Ok);
    CHECK(!reattach(const_cast<Sketch&>(s), b, v));
}

TEST(sketch_says_so_when_its_face_is_gone) {
    const auto v = vol();
    const Body big = box(v, 0, 0, 4, 3, 2);
    const FaceId top = face_facing(big, v, 0, 0, 1);
    auto s = sketch_on_face(big, top, v);
    CHECK(s.has_value());
    CHECK(check(*s, big, v) == SketchState::Ok);

    Body small;
    CHECK_MSG(check(*s, small, v) == SketchState::FaceGone, "%s",
              sketch_state_name(check(*s, small, v)));
    CHECK(!reattach(*s, small, v));
}

TEST(sketch_says_so_when_its_face_moved) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    auto s = sketch_on_face(b, top, v);
    CHECK(s.has_value());

    const Body taller = box(v, 0, 0, 4, 3, 5);
    const SketchState st = check(*s, taller, v);
    CHECK_MSG(st == SketchState::FaceMoved || st == SketchState::FaceGone,
              "a moved face reported '%s'", sketch_state_name(st));
}

TEST(sketch_says_so_when_its_face_became_curved) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    auto s = sketch_on_face(b, top, v);
    CHECK(s.has_value());

    Body curved = b;
    curved.face_surface.assign(curved.face_count(), Surface{});
    curved.face_surface[top].kind = SurfaceKind::Sphere;
    CHECK(check(*s, curved, v) == SketchState::FaceNotPlanar);
}

TEST(reattaching_keeps_the_geometry_where_it_was_drawn_relative_to_the_face) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId top = face_facing(b, v, 0, 0, 1);
    auto s = sketch_on_face(b, top, v);
    CHECK(s.has_value());
    s->geometry.push_back(circ(v, 0.5, 0.5, 0.4));

    const auto before = lift(*s, v);
    CHECK(!before.empty());
    const f64 z_before = before[0][0][2];
    CHECK(std::fabs(z_before - 2.0) < 1e-6);

    const Body taller = box(v, 0, 0, 4, 3, 5);
    const FaceId new_top = face_facing(taller, v, 0, 0, 1);
    s->face = new_top;
    CHECK(reattach(*s, taller, v, 2));
    CHECK(s->body_revision == 2);
    CHECK(check(*s, taller, v) == SketchState::Ok);

    const auto after = lift(*s, v);
    CHECK(!after.empty());
    CHECK_MSG(std::fabs(after[0][0][2] - 5.0) < 1e-6,
              "the sketch stayed at z=%g after the face moved to z=5",
              after[0][0][2]);
    CHECK(std::fabs(s->geometry[0].a.x - gcm::to_lattice(v, 0.5, 0.5).x) < 4);
}

TEST(lift_puts_every_point_exactly_on_the_sketch_plane) {
    const auto v = vol();
    Sketch s = sketch_on_plane(
        SketchPlane::from_normal(1.0, 2.0, 3.0, 0.3, -0.5, 0.8));
    s.geometry.push_back(seg(v, -1, -1, 2, 3));
    s.geometry.push_back(circ(v, 0, 0, 1.5));

    for (const auto& poly : lift(s, v))
        for (const auto& p : poly)
            CHECK_MSG(std::fabs(s.plane.distance(p[0], p[1], p[2])) < 1e-6,
                      "a lifted point is %.6g off the plane",
                      s.plane.distance(p[0], p[1], p[2]));
}

TEST(lift_tessellates_a_circle_to_the_chord_tolerance) {
    const auto v = vol();
    Sketch s = sketch_on_plane(SketchPlane::world_xy());
    s.geometry.push_back(circ(v, 0, 0, 1.0));

    const auto fine = lift(s, v, 1e-5);
    const auto coarse = lift(s, v, 1e-2);
    CHECK(!fine.empty() && !coarse.empty());
    CHECK_MSG(fine[0].size() > coarse[0].size(),
              "a tighter tolerance gave %zu points and a looser one %zu",
              fine[0].size(), coarse[0].size());

    for (auto& [pts, tol] : std::vector<std::pair<const std::vector<std::array<f64, 3>>*, f64>>{
             {&fine[0], 1e-5}, {&coarse[0], 1e-2}}) {
        for (std::size_t i = 0; i + 1 < pts->size(); ++i) {
            const auto& a = (*pts)[i];
            const auto& b = (*pts)[i + 1];
            const f64 mx = (a[0] + b[0]) * 0.5, my = (a[1] + b[1]) * 0.5;
            const f64 sag = 1.0 - std::hypot(mx, my);
            CHECK_MSG(sag <= tol * 1.5 + 1e-9, "chord sag %.3g exceeds %.3g",
                      sag, tol);
        }
    }
}

TEST(lift_of_an_empty_sketch_is_empty) {
    const auto v = vol();
    const Sketch s = sketch_on_plane(SketchPlane::world_xy());
    CHECK(lift(s, v).empty());
}

TEST(sketch_becomes_solver_entities_and_comes_back) {
    const auto v = vol();
    Sketch s = sketch_on_plane(SketchPlane::world_xy());
    s.geometry.push_back(seg(v, 0, 0, 3, 0.02));
    s.geometry.push_back(circ(v, 1, 1, 0.5));

    SketchSystem sys = to_system(s, v);
    CHECK_MSG(sys.mapping.size() == 2, "%zu shapes mapped", sys.mapping.size());
    CHECK(sys.system.entity_count() >= 4);

    sys.system.set_point(sys.mapping[0].b, 3.0, 0.0);
    write_back(s, sys, v);
    f64 bx, by;
    gcm::to_world(v, s.geometry[0].b, bx, by);
    CHECK(std::fabs(bx - 3.0) < 1e-6 && std::fabs(by) < 1e-6);
}

TEST(dof_badge_of_an_empty_sketch_says_empty) {
    gsolve::System s;
    const BadgeState b = badge_of(s);
    CHECK(b.badge == DofBadge::Empty);
    CHECK(!b.text.empty());
}

TEST(dof_badge_counts_what_is_still_required) {
    gsolve::System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(3, 0);
    s.add_line(a, b);

    const BadgeState st = badge_of(s);
    CHECK(st.badge == DofBadge::UnderConstrained);
    CHECK_MSG(st.dof.remaining == 4, "%d degrees of freedom for a free line",
              st.dof.remaining);
    CHECK_MSG(st.text.find("4") != std::string::npos, "the badge says '%s'",
              st.text.c_str());
}

TEST(dof_badge_reads_fully_constrained_when_it_is) {
    gsolve::System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(3, 0);
    const u32 line = s.add_line(a, b);
    s.fix(a);
    s.add({gsolve::Kind::Horizontal, {line}});
    s.add({gsolve::Kind::Distance, {a, b}, 3.0});

    const BadgeState st = badge_of(s);
    CHECK_MSG(st.badge == DofBadge::FullyConstrained, "%s: %s",
              badge_name(st.badge), st.text.c_str());
    CHECK(st.dof.remaining == 0);
}

TEST(dof_badge_reports_over_constrained_before_under) {
    gsolve::System s;
    const u32 a = s.add_point(0, 0);
    const u32 b = s.add_point(3, 0);
    const u32 line = s.add_line(a, b);
    s.add({gsolve::Kind::Horizontal, {line}});
    s.add({gsolve::Kind::Horizontal, {line}});

    const BadgeState st = badge_of(s);
    if (st.dof.redundant > 0) {
        CHECK_MSG(st.badge == DofBadge::OverConstrained, "%s",
                  badge_name(st.badge));
        CHECK_MSG(!st.conflicting.empty(),
                  "over-constrained without saying which constraints");
    }
}

TEST(badge_names_exist_for_every_state) {
    for (DofBadge b : {DofBadge::Empty, DofBadge::UnderConstrained,
                       DofBadge::FullyConstrained, DofBadge::OverConstrained})
        CHECK(badge_name(b)[0] != '\0');
    for (SketchState s : {SketchState::Ok, SketchState::FaceGone,
                          SketchState::FaceNotPlanar, SketchState::FaceMoved})
        CHECK(sketch_state_name(s)[0] != '\0');
}

TEST(auto_constrain_tidies_a_roughly_drawn_rectangle) {
    const auto v = vol();
    Sketch s = sketch_on_plane(SketchPlane::world_xy());
    s.geometry.push_back(seg(v, 0.0,    0.0,   4.0,   0.011));
    s.geometry.push_back(seg(v, 4.0,    0.011, 4.013, 3.0));
    s.geometry.push_back(seg(v, 4.013,  3.0,   0.009, 2.994));
    s.geometry.push_back(seg(v, 0.009,  2.994, 0.0,   0.0));

    const BadgeState before = badge_of(to_system(s, v).system);
    const BadgeState after = auto_constrain(s, v);
    CHECK_MSG(after.dof.constraints > 0, "nothing was inferred");
    CHECK_MSG(after.dof.remaining < before.dof.remaining,
              "auto-constrain left %d degrees of freedom, the same as before",
              after.dof.remaining);

    f64 ax, ay, bx, by;
    gcm::to_world(v, s.geometry[0].a, ax, ay);
    gcm::to_world(v, s.geometry[0].b, bx, by);
    CHECK_MSG(std::fabs(by - ay) < 1e-3,
              "the bottom edge still rises %.4f over its length", by - ay);
}

TEST(auto_constrain_leaves_a_deliberate_angle_alone) {
    const auto v = vol();
    auto drawn_angle = [&](f64 dx, f64 tol_deg) {
        Sketch s = sketch_on_plane(SketchPlane::world_xy());
        s.geometry.push_back(seg(v, 0, 0, 4, 0));
        s.geometry.push_back(seg(v, 0, 0, dx, 4.0));

        gsolve::InferenceSettings in;
        in.angle_tolerance_deg = tol_deg;
        auto_constrain(s, v, in);

        f64 ax, ay, bx, by;
        gcm::to_world(v, s.geometry[1].a, ax, ay);
        gcm::to_world(v, s.geometry[1].b, bx, by);
        return std::atan2(by - ay, bx - ax) * 180.0 / 3.14159265358979;
    };

    const f64 deliberate = drawn_angle(4.0 * std::tan(3.0 * 3.14159265358979 / 180.0),
                                       2.0);
    CHECK_MSG(std::fabs(deliberate - 90.0) > 0.5,
              "a 3 degree lean was snapped to %.3f degrees", deliberate);

    const f64 wobble = drawn_angle(4.0 * std::tan(1.0 * 3.14159265358979 / 180.0),
                                   2.0);
    CHECK_MSG(std::fabs(wobble - 90.0) < 1e-3,
              "a 1 degree wobble was left at %.3f degrees", wobble);
}

TEST(auto_constrain_of_an_empty_sketch_is_harmless) {
    const auto v = vol();
    Sketch s = sketch_on_plane(SketchPlane::world_xy());
    const BadgeState b = auto_constrain(s, v);
    CHECK(b.badge == DofBadge::Empty);
    CHECK(s.geometry.empty());
}

TEST(sketch_face_origin_does_not_move_when_an_edge_is_subdivided) {
    const auto v=vol();
    for (bool subdivided:{false,true}) {
        Builder builder;
        std::vector<VertexId> loop;
        loop.push_back(builder.add_vertex(gcm::to_lattice(v,0,0,1)));
        if (subdivided) loop.push_back(builder.add_vertex(gcm::to_lattice(v,1,0,1)));
        loop.push_back(builder.add_vertex(gcm::to_lattice(v,4,0,1)));
        loop.push_back(builder.add_vertex(gcm::to_lattice(v,4,4,1)));
        loop.push_back(builder.add_vertex(gcm::to_lattice(v,0,4,1)));
        builder.begin_face(); builder.add_loop(loop); builder.end_face();
        const auto plane=plane_of_face(builder.build(),0,v);
        CHECK(std::fabs(plane.ox-2)<1e-6);
        CHECK(std::fabs(plane.oy-2)<1e-6);
        CHECK(std::fabs(plane.oz-1)<1e-6);
        CHECK(std::fabs(plane.nz-1)<1e-6);
    }
}
