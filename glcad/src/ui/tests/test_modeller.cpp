#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "ui/modeller.hpp"
#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/solid_edit.hpp"
#include "ui/solids_file.hpp"

using namespace gcapp;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

std::vector<gdoc::Segment2> rect_sketch(const gcm::WorkingVolume& v,
                                        double x0 = 0, double y0 = 0,
                                        double x1 = 8, double y1 = 5) {
    const auto L = [&](double x, double y) { return gcm::to_lattice(v, x, y); };
    return {{L(x0, y0), L(x1, y0)}, {L(x1, y0), L(x1, y1)},
            {L(x1, y1), L(x0, y1)}, {L(x0, y1), L(x0, y0)}};
}

gview::Camera plan_camera() {
    gview::Camera c;
    c.set_viewport(800, 600);
    c.look_plan();
    c.set_target(gview::Point3::from(4.0, 2.5, 0.0));
    c.set_distance(20.0);
    return c;
}

gview::Camera iso_camera() {
    gview::Camera c;
    c.set_viewport(800, 600);
    c.look(gview::StandardView::Iso);
    c.set_target(gview::Point3::from(4.0, 2.5, 0.0));
    c.set_distance(30.0);
    return c;
}

void pixel(const gview::Camera& c, double x, double y, double z, double& px, double& py) {
    CHECK(c.project(gview::Point3::from(x, y, z), px, py));
}

void pull(Modeller& m, const gview::Camera& c, const std::vector<gdoc::Segment2>& sk,
          double h) {
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(m.press(c, px, py, sk));
    CHECK(m.stage() == Modeller::Stage::Placed);
    CHECK(m.press(c, px, py, sk));
    CHECK(m.stage() == Modeller::Stage::Dragging);
    double tx, ty;
    pixel(c, 4.0, 2.5, h, tx, ty);
    m.drag(c, tx, ty);
    CHECK(m.release());
}

}

TEST(modeller_does_nothing_until_armed) {
    Modeller m(vol());
    const auto sk = rect_sketch(vol());
    const auto c = plan_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(!m.hovered().valid);
    CHECK(!m.press(c, px, py, sk));
    CHECK(m.bodies().empty());
}

TEST(modeller_hovering_a_closed_region_lights_it_up) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    const auto c = plan_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    const auto& t = m.hovered();
    CHECK(t.valid);
    CHECK(!t.on_body);
    CHECK_MSG(t.outline.size() == 1 && t.outline[0].size() == 4,
              "outline has %zu loops", t.outline.size());
    CHECK(std::fabs(t.normal.z.to_f64() - 1.0) < 1e-9);
    CHECK(std::fabs(t.centroid.x.to_f64() - 4.0) < 1e-6);
    CHECK(std::fabs(t.centroid.y.to_f64() - 2.5) < 1e-6);
}

TEST(modeller_hovering_outside_every_region_lights_nothing) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    const auto c = plan_camera();
    double px, py;
    pixel(c, 20.0, 20.0, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(!m.hovered().valid);
}

TEST(modeller_an_open_sketch_is_not_a_region) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    auto sk = rect_sketch(vol());
    sk.pop_back();
    const auto c = plan_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(!m.hovered().valid);
}

TEST(modeller_dragging_the_arrow_extrudes_the_region) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    pull(m, iso_camera(), sk, 3.0);

    CHECK_MSG(m.bodies().size() == 1, "%zu bodies", m.bodies().size());
    if (m.bodies().empty()) return;
    const auto bb = m.bodies()[0].bounds(vol());
    CHECK_MSG(std::fabs(bb.lo.z.to_f64()) < 1e-6 && std::fabs(bb.hi.z.to_f64() - 3.0) < 1e-6,
              "z from %.4f to %.4f", bb.lo.z.to_f64(), bb.hi.z.to_f64());
    CHECK(std::fabs(bb.hi.x.to_f64() - 8.0) < 1e-6);
    CHECK(std::fabs(bb.hi.y.to_f64() - 5.0) < 1e-6);
    CHECK_MSG(std::fabs(gtopo::volume(m.bodies()[0], vol()) - 120.0) < 1e-3,
              "volume %.6f", gtopo::volume(m.bodies()[0], vol()));
    CHECK(m.stage() == Modeller::Stage::PickTarget);
}

TEST(modeller_a_typed_distance_is_exact) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    const auto c = plan_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(m.press(c, px, py, sk));
    for (char ch : std::string("2.5")) m.type(ch);
    CHECK(m.typed() == "2.5");
    CHECK(m.commit_typed());
    CHECK(m.bodies().size() == 1);
    if (m.bodies().empty()) return;
    const auto bb = m.bodies()[0].bounds(vol());
    CHECK_MSG(std::fabs(bb.hi.z.to_f64() - 2.5) < 1e-6, "height %.6f", bb.hi.z.to_f64());
}

TEST(modeller_a_zero_drag_makes_nothing) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    const auto c = plan_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(m.press(c, px, py, sk));
    CHECK(m.press(c, px, py, sk));
    m.drag(c, px, py);
    CHECK(!m.release());
    CHECK(m.bodies().empty());
}

TEST(modeller_points_at_a_face_of_a_solid) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    pull(m, iso_camera(), sk, 3.0);
    CHECK(m.bodies().size() == 1);

    for (const gview::Camera& c : {plan_camera(), iso_camera()}) {
        double px, py;
        pixel(c, 4.0, 2.5, 3.0, px, py);
        m.hover(c, px, py, {});
        const auto& t = m.hovered();
        CHECK_MSG(t.valid && t.on_body, "no face under the top's centre");
        if (!t.valid) continue;
        CHECK_MSG(std::fabs(t.normal.z.to_f64() - 1.0) < 1e-9,
                  "top face normal z = %.4f", t.normal.z.to_f64());
        CHECK(std::fabs(t.centroid.z.to_f64() - 3.0) < 1e-6);
    }
}

TEST(modeller_pulling_a_face_grows_the_solid) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    pull(m, iso_camera(), sk, 3.0);

    const auto c = iso_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 3.0, px, py);
    m.hover(c, px, py, {});
    CHECK(m.press(c, px, py, {}));
    CHECK(m.press(c, px, py, {}));
    double tx, ty;
    pixel(c, 4.0, 2.5, 5.0, tx, ty);
    m.drag(c, tx, ty);
    CHECK(m.release());

    CHECK_MSG(m.bodies().size() == 1, "%zu bodies after pulling a face", m.bodies().size());
    if (m.bodies().empty()) return;
    const auto bb = m.bodies()[0].bounds(vol());
    CHECK_MSG(std::fabs(bb.hi.z.to_f64() - 5.0) < 1e-6, "top at %.4f", bb.hi.z.to_f64());
    CHECK_MSG(std::fabs(gtopo::volume(m.bodies()[0], vol()) - 200.0) < 1e-3,
              "volume %.6f", gtopo::volume(m.bodies()[0], vol()));
}

TEST(modeller_a_region_pulled_downward_makes_a_solid_below_the_plane) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    const auto c = plan_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(m.press(c, px, py, sk));
    for (char ch : std::string("-3")) m.type(ch);
    CHECK(m.commit_typed());
    CHECK(m.bodies().size() == 1);
    if (m.bodies().empty()) return;
    const auto bb = m.bodies()[0].bounds(vol());
    CHECK_MSG(std::fabs(bb.lo.z.to_f64() + 3.0) < 1e-6 && std::fabs(bb.hi.z.to_f64()) < 1e-6,
              "z from %.4f to %.4f", bb.lo.z.to_f64(), bb.hi.z.to_f64());
    CHECK_MSG(std::fabs(gtopo::volume(m.bodies()[0], vol()) - 120.0) < 1e-4,
              "volume %.6f", gtopo::volume(m.bodies()[0], vol()));
}

TEST(modeller_pushing_a_region_on_a_face_cuts_into_the_solid) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    {
        const auto sk = rect_sketch(vol());
        const auto c = plan_camera();
        double px, py;
        pixel(c, 4.0, 2.5, 0.0, px, py);
        m.hover(c, px, py, sk);
        CHECK(m.press(c, px, py, sk));
        for (char ch : std::string("-3")) m.type(ch);
        CHECK(m.commit_typed());
    }
    CHECK(m.bodies().size() == 1);

    const auto square = rect_sketch(vol(), 1, 1, 3, 3);
    const auto c = plan_camera();
    double px, py;
    pixel(c, 2.0, 2.0, 0.0, px, py);
    m.hover(c, px, py, square);
    const auto& t = m.hovered();
    CHECK_MSG(t.valid && t.on_body, "the square is not seen as lying on the solid");
    if (!t.valid) return;
    CHECK_MSG(t.outline.size() == 1 && t.outline[0].size() == 4,
              "the target is the face, not the square drawn on it");
    CHECK_MSG(std::fabs(t.normal.z.to_f64() - 1.0) < 1e-9, "normal z = %.4f", t.normal.z.to_f64());

    CHECK(m.press(c, px, py, square));
    const auto serial=m.edit_serial();
    for (char ch : std::string("-1")) m.type(ch);
    m.mesh();
    const auto preview=m.gpu_preview();
    CHECK(preview.operation==gbool::Operation::Subtract);
    CHECK(!preview.body.empty() && !preview.tool.empty());
    CHECK(m.stage()==Modeller::Stage::Placed);
    CHECK(m.edit_serial()==serial);
    CHECK(std::fabs(gtopo::volume(m.bodies()[0],vol())-120)<1e-4);
    CHECK(m.commit_typed());
    CHECK(m.bodies().size() == 1);
    if (m.bodies().empty()) return;
    CHECK_MSG(std::fabs(gtopo::volume(m.bodies()[0], vol()) - 116.0) < 1e-4,
              "volume %.6f after a 4-unit cut", gtopo::volume(m.bodies()[0], vol()));

    double fx, fy;
    pixel(c, 2.0, 2.0, -1.0, fx, fy);
    m.hover(c, fx, fy, {});
    CHECK_MSG(m.hovered().valid && m.hovered().on_body, "no floor face under the pocket");
    CHECK_MSG(std::fabs(m.hovered().centroid.z.to_f64() + 1.0) < 1e-6,
              "floor at z = %.4f", m.hovered().centroid.z.to_f64());
    CHECK(m.press(c, fx, fy, {}));
    for (char ch : std::string("1")) m.type(ch);
    CHECK(m.commit_typed());
    CHECK_MSG(m.bodies().size() == 1, "%zu bodies after filling the pocket", m.bodies().size());
    if (m.bodies().empty()) return;
    CHECK_MSG(std::fabs(gtopo::volume(m.bodies()[0], vol()) - 120.0) < 1e-4,
              "volume %.6f after filling the pocket", gtopo::volume(m.bodies()[0], vol()));
}

TEST(modeller_undo_restores_the_solids) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    pull(m, iso_camera(), rect_sketch(vol()), 3.0);
    CHECK(m.bodies().size() == 1);
    CHECK(m.undo());
    CHECK(m.bodies().empty());
    CHECK(m.redo());
    CHECK(m.bodies().size() == 1);
    CHECK(!m.undo() || m.bodies().empty());
}

TEST(modeller_feature_value_transactions_keep_geometry_and_recipe_together) {
    Modeller m(vol()); m.arm(Modeller::Op::Extrude);
    pull(m, iso_camera(), rect_sketch(vol()), 3.0);
    const auto before = encode_solids(m.bodies());
    const auto recipe = encode_feature_history(m.histories());
    CHECK(m.set_feature_value(0, 0, 5));
    const auto after = encode_solids(m.bodies());
    const auto edited = encode_feature_history(m.histories());
    CHECK(after != before); CHECK(edited != recipe);
    CHECK(m.undo());
    CHECK(encode_solids(m.bodies()) == before);
    CHECK(encode_feature_history(m.histories()) == recipe);
    CHECK(m.redo());
    CHECK(encode_solids(m.bodies()) == after);
    CHECK(encode_feature_history(m.histories()) == edited);
    const auto serial = m.edit_serial();
    for (const double invalid : {0.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!m.set_feature_value(0, 0, invalid));
        CHECK(m.edit_serial() == serial);
        CHECK(encode_solids(m.bodies()) == after);
        CHECK(encode_feature_history(m.histories()) == edited);
    }
    CHECK(m.undo());
    CHECK(encode_solids(m.bodies()) == before);
    CHECK(encode_feature_history(m.histories()) == recipe);
}

TEST(modeller_suppression_undo_restores_the_feature_flag_and_solid) {
    Modeller m(vol()); m.arm(Modeller::Op::Extrude);
    pull(m, iso_camera(), rect_sketch(vol()), 3.0);
    const auto before = encode_solids(m.bodies());
    const auto recipe = encode_feature_history(m.histories());
    std::string why;
    CHECK(m.suppress(0, 0, true, why));
    CHECK(m.bodies()[0].face_count() == 0);
    CHECK(m.history(0)[0].suppressed);
    CHECK(m.undo());
    CHECK(encode_solids(m.bodies()) == before);
    CHECK(encode_feature_history(m.histories()) == recipe);
    CHECK(!m.history(0)[0].suppressed);
    CHECK(m.redo());
    CHECK(m.history(0)[0].suppressed);
    CHECK(m.suppress(0, 0, false, why));
    CHECK(encode_solids(m.bodies()) == before);
    CHECK(m.undo()); CHECK(m.history(0)[0].suppressed);
    CHECK(m.bodies()[0].face_count() == 0);
}

TEST(modeller_opened_driving_sketch_refuses_rebuild_and_preserves_the_part) {
    Modeller m(vol()); m.arm(Modeller::Op::Extrude);
    auto sketch=rect_sketch(vol());
    pull(m, iso_camera(), sketch, 3.0);
    const auto before=encode_solids(m.bodies());
    const auto recipe=encode_feature_history(m.histories());
    auto sources=m.sketches();
    CHECK(sources.size()==1);
    if (sources.empty()) return;
    sketch.pop_back(); sources[0].segments=&sketch; ++sources[0].version;
    m.set_sketches(sources);
    const auto serial=m.edit_serial();
    std::string why; CHECK(!m.rebuild(0,why));
    CHECK(why.find("sketch region")!=std::string::npos);
    CHECK(m.edit_serial()==serial);
    CHECK(encode_solids(m.bodies())==before);
    CHECK(encode_feature_history(m.histories())==recipe);
}

TEST(modeller_reorder_is_atomic_and_cannot_turn_a_cut_into_a_base_solid) {
    Modeller m(vol()); m.arm(Modeller::Op::Extrude);
    pull(m, iso_camera(), rect_sketch(vol()), 3.0);
    auto features = m.history(0);
    for (const double x : {1.0, 4.0}) {
        auto pocket = features[0];
        pocket.label = x == 1 ? "Cut left" : "Cut right";
        pocket.on_body = true; pocket.value = -0.5; pocket.has_seed = false;
        pocket.frame = ggeo::d3::Frame<gcm::F64>::world_xy();
        pocket.frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, features[0].value);
        pocket.profile.outer.clear(); pocket.profile.holes.clear();
        for (const auto& p : std::vector<std::array<double,2>>{{x,1},{x+1,1},{x+1,2},{x,2}})
            pocket.profile.outer.push_back(gcm::to_lattice(vol(), p[0], p[1]));
        features.push_back(pocket);
    }
    m.set_history({features});
    std::string why; CHECK_MSG(m.rebuild(0, why), "%s", why.c_str());
    const auto before = encode_solids(m.bodies());
    const auto recipe = encode_feature_history(m.histories());
    CHECK_MSG(m.reorder(0, 1, 2, why), "%s", why.c_str());
    const auto reordered = encode_feature_history(m.histories());
    CHECK(reordered != recipe);
    CHECK_MSG(std::fabs(gtopo::volume(m.bodies()[0], vol()) - 119) < 1e-4,
              "volume %.12g", gtopo::volume(m.bodies()[0], vol()));
    CHECK(m.undo()); CHECK(encode_solids(m.bodies()) == before);
    CHECK(encode_feature_history(m.histories()) == recipe);
    CHECK(m.redo()); CHECK(encode_feature_history(m.histories()) == reordered);
    const auto solid = encode_solids(m.bodies());
    const auto serial = m.edit_serial();
    CHECK(!m.reorder(0, 0, 1, why));
    CHECK(!m.suppress(0, 0, true, why));
    CHECK(m.edit_serial() == serial);
    CHECK(encode_solids(m.bodies()) == solid);
    CHECK(encode_feature_history(m.histories()) == reordered);
    CHECK(!m.history(0)[0].suppressed);
}

TEST(modeller_cancel_drops_the_target_and_keeps_the_tool) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    const auto c = plan_camera();
    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    CHECK(m.press(c, px, py, sk));
    CHECK(m.stage() == Modeller::Stage::Placed);
    m.cancel();
    CHECK(m.stage() == Modeller::Stage::PickTarget);
    CHECK(m.op() == Modeller::Op::Extrude);
    m.cancel();
    CHECK(m.op() == Modeller::Op::None);
}

TEST(modeller_overlay_draws_the_arrow_when_placed) {
    Modeller m(vol());
    m.arm(Modeller::Op::Extrude);
    const auto sk = rect_sketch(vol());
    const auto c = iso_camera();
    const float col[4] = {1, 1, 1, 1};
    std::vector<LineVertex> lines;

    m.overlay(c, col, col, lines);
    CHECK(lines.empty());

    double px, py;
    pixel(c, 4.0, 2.5, 0.0, px, py);
    m.hover(c, px, py, sk);
    m.overlay(c, col, col, lines);
    CHECK_MSG(lines.size() == 8, "a hovered rectangle drew %zu vertices", lines.size());

    lines.clear();
    CHECK(m.press(c, px, py, sk));
    m.overlay(c, col, col, lines);
    CHECK_MSG(lines.size() == 8, "a placed target drew %zu vertices", lines.size());

    double lx, ly;
    std::string label;
    CHECK(m.gizmo_label(c, lx, ly, label));
    CHECK(label == "0.000");
}

namespace {
double preview_volume(const std::vector<MeshVertex>& vertices) {
    double volume = 0;
    for (std::size_t i=0; i+2<vertices.size(); i+=3) {
        const auto *a=vertices[i].pos, *b=vertices[i+1].pos, *c=vertices[i+2].pos;
        volume += double(a[0])*(double(b[1])*c[2]-double(b[2])*c[1]) +
                  double(a[1])*(double(b[2])*c[0]-double(b[0])*c[2]) +
                  double(a[2])*(double(b[0])*c[1]-double(b[1])*c[0]);
    }
    return volume/6;
}
}

TEST(modeller_gpu_prism_matches_cpu_commit_for_concave_holed_and_negative_pulls) {
    for (bool hole : {false,true}) for (double distance : {-2.0,2.0}) {
        Modeller m(vol()); m.arm(Modeller::Op::Extrude);
        std::vector<gdoc::Segment2> sketch;
        if (hole) {
            sketch=rect_sketch(vol(),0,0,8,5);
            const auto inner=rect_sketch(vol(),3,1,5,4);
            sketch.insert(sketch.end(),inner.begin(),inner.end());
        } else {
            const double points[][2]={{0,0},{8,0},{8,2},{3,2},{3,5},{0,5}};
            for (int i=0;i<6;++i) sketch.push_back({gcm::to_lattice(vol(),points[i][0],points[i][1]),
                                                   gcm::to_lattice(vol(),points[(i+1)%6][0],points[(i+1)%6][1])});
        }
        const auto camera=iso_camera(); double x,y;
        pixel(camera,1,1,0,x,y); m.hover(camera,x,y,sketch); CHECK(m.press(camera,x,y,sketch));
        CHECK(m.target().valid);
        if (!m.target().valid) continue;
        if (hole) CHECK(m.target().profile.holes.size()==1);
        const auto center=m.target().centroid;
        const auto normal=m.target().normal;
        CHECK(camera.project(center,x,y)); CHECK(m.press(camera,x,y,sketch));
        CHECK(m.stage()==Modeller::Stage::Dragging);
        CHECK(camera.project(center+normal*gcm::F64::from(distance),x,y)); m.drag(camera,x,y);
        m.mesh(); const auto gpu=m.gpu_preview();
        CHECK(m.bodies().empty()); CHECK(gpu.body.empty()); CHECK(!gpu.tool.empty());
        CHECK(gpu.operation==gbool::Operation::Union);
        const double expected=(hole ? 34.0 : 25.0)*std::fabs(m.distance());
        CHECK_MSG(std::fabs(preview_volume(gpu.tool)-expected)<1e-4,
                  "preview volume %g, expected %g",preview_volume(gpu.tool),expected);
        for (std::size_t i=0;i+2<gpu.tool.size();i+=3) {
            const auto *a=gpu.tool[i].pos,*b=gpu.tool[i+1].pos,*c=gpu.tool[i+2].pos,*n=gpu.tool[i].normal;
            const double dot=((b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]))*n[0]+
                             ((b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]))*n[1]+
                             ((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))*n[2];
            CHECK(dot>0);
        }
        CHECK(m.release()); CHECK(m.bodies().size()==1);
        CHECK(m.gpu_preview().tool.empty());
        CHECK(!m.preview().active); CHECK(m.preview_range().second==0);
        if (!m.bodies().empty()) CHECK(std::fabs(gtopo::volume(m.bodies()[0],vol())-expected)<1e-4);
    }
}

TEST(modeller_gpu_combine_previews_do_not_mutate_cpu_solids) {
    for (auto operation : {Modeller::Op::Union,Modeller::Op::Subtract,Modeller::Op::Intersect}) {
        Modeller m(vol());
        for (double x : {0.0,2.0}) {
            gtopo::Profile profile;
            for (const auto& q : std::vector<std::array<double,2>>{{x,0},{x+4,0},{x+4,4},{x,4}})
                profile.outer.push_back(gcm::to_lattice(vol(),q[0],q[1]));
            m.add_body(gtopo::extrude(profile,vol(),ggeo::d3::Frame<gcm::F64>::world_xy(),2),"box");
        }
        m.mesh(); m.arm(operation); const auto camera=plan_camera(); double x,y;
        pixel(camera,1,2,2,x,y); m.hover(camera,x,y,{}); CHECK(m.press(camera,x,y,{}));
        CHECK(m.stage()==Modeller::Stage::PickSecond);
        pixel(camera,5,2,2,x,y); m.hover(camera,x,y,{});
        const auto gpu=m.gpu_preview(); CHECK(!gpu.body.empty()); CHECK(!gpu.tool.empty());
        CHECK(gpu.target_body_count>0); CHECK(gpu.tool_body_count>0);
        const auto expected_op=operation==Modeller::Op::Union ? gbool::Operation::Union :
                               operation==Modeller::Op::Subtract ? gbool::Operation::Subtract : gbool::Operation::Intersect;
        CHECK(gpu.operation==expected_op); CHECK(m.bodies().size()==2);
        for (const auto& body:m.bodies()) CHECK(std::fabs(gtopo::volume(body,vol())-32)<1e-4);
        CHECK(m.press(camera,x,y,{})); CHECK(m.bodies().size()==1); CHECK(m.gpu_preview().tool.empty());
        const double expected=operation==Modeller::Op::Union ? 48:16;
        if (!m.bodies().empty()) CHECK(std::fabs(gtopo::volume(m.bodies()[0],vol())-expected)<1e-4);
        CHECK(m.undo()); CHECK(m.bodies().size()==2);
    }
}

TEST(modeller_gpu_cancel_discards_only_the_preview) {
    Modeller m(vol()); m.arm(Modeller::Op::Extrude);
    const auto camera=iso_camera(); const auto sketch=rect_sketch(vol());
    double x,y; pixel(camera,4,2.5,0,x,y);
    m.hover(camera,x,y,sketch); CHECK(m.press(camera,x,y,sketch));
    CHECK(m.press(camera,x,y,sketch));
    pixel(camera,4,2.5,2,x,y); m.drag(camera,x,y); m.mesh();
    CHECK(m.preview().active); CHECK(!m.gpu_preview().tool.empty());
    m.cancel();
    CHECK(!m.preview().active); CHECK(m.preview_range().second==0);
    CHECK(m.gpu_preview().tool.empty()); CHECK(m.bodies().empty()); CHECK(m.mesh().empty());
}

TEST(modeller_failed_join_does_not_add_a_body_or_commit_history) {
    const auto v=vol();
    Modeller m(v);
    gtopo::Builder builder;
    const auto a=builder.add_vertex(gcm::to_lattice(v,0,0,0));
    const auto b=builder.add_vertex(gcm::to_lattice(v,8,0,0));
    const auto c=builder.add_vertex(gcm::to_lattice(v,8,5,0));
    const auto d=builder.add_vertex(gcm::to_lattice(v,0,5,0));
    builder.begin_face(); builder.add_loop({a,b,c,d}); builder.end_face();
    m.set_bodies({builder.build()});
    auto cam=plan_camera(); double px,py; pixel(cam,4,2.5,0,px,py);
    Modeller::Target target;
    CHECK(m.target_at(cam,px,py,target));
    m.place(Modeller::Op::Extrude,target);
    m.set_typed("1");
    const auto serial=m.edit_serial();
    const auto history=m.history(0).size();
    CHECK(!m.commit_typed());
    CHECK(m.bodies().size()==1); CHECK(m.bodies()[0].face_count()==1);
    CHECK(m.edit_serial()==serial); CHECK(m.history(0).size()==history);
    CHECK(!m.take_committed()); CHECK(m.typed()=="1"); CHECK(!m.value_error().empty());
}

TEST(modeller_face_sketch_roundoff_does_not_leave_a_lid_over_corner_cut) {
    const auto v=vol();
    gtopo::Profile base;base.outer={gcm::to_lattice(v,-1,-1),gcm::to_lattice(v,1,-1),gcm::to_lattice(v,1,1),gcm::to_lattice(v,-1,1)};
    Modeller m(v);m.add_body(gtopo::extrude_z(base,v,2),"Box");
    const double top=m.bodies()[0].bounds(v).hi.z.to_f64();
    auto plane=gtopo::SketchPlane::world_xy();plane.oz=top;plane.vz=1e-6;
    const auto sketch=rect_sketch(v,0.9,-1.1,1.1,-0.9);
    m.set_sketches({{plane,&sketch,1,true}});
    auto camera=plan_camera();camera.set_target(gview::Point3::from(0,0,top));
    double x,y;pixel(camera,0.98,-0.98,top,x,y);
    Modeller::Target target;CHECK(m.target_at(camera,x,y,target,false));
    CHECK(target.on_body);
    CHECK(std::fabs(target.frame.origin.z.to_f64()-top)<1e-12);
    for(const char* depth:{"-854.28mm","-2242.2304mm"}) {
        auto edit=m;edit.place(Modeller::Op::Extrude,target);edit.set_typed(depth);
        CHECK_MSG(edit.commit_typed(),"%s: %s",depth,edit.message().c_str());
        CHECK(edit.bodies().size()==1);
        CHECK(edit.bodies()[0].analyse().closed && edit.bodies()[0].analyse().manifold);
    }
}

TEST(modeller_body_descriptions_and_mesh_follow_edits_undo_and_display_units) {
    const auto v=vol();
    gtopo::Profile p;
    p.outer={gcm::to_lattice(v,0,0),gcm::to_lattice(v,1,0),
             gcm::to_lattice(v,1,1),gcm::to_lattice(v,0,1)};
    Modeller m(v);
    m.add_body(gtopo::extrude_z(p,v,1),"Block");
    const auto check_volume=[&](double expected) {
        for(int i=0;i<2;++i) {
            double actual=0;
            CHECK(std::sscanf(m.describe(0).c_str(),"volume %lf",&actual)==1);
            CHECK(std::abs(actual-expected)<1e-6*std::max(1.0,expected));
        }
    };
    const auto check_top=[&](float expected) {
        float top=-1;
        for(const auto& vertex:m.mesh())top=std::max(top,vertex.pos[2]);
        CHECK(std::abs(top-expected)<1e-6f);
    };
    check_volume(1);check_top(1);
    CHECK(m.replace_body(0,gtopo::extrude_z(p,v,2)));
    check_volume(2);check_top(2);
    m.set_display_unit(gunit::Length::Millimetre);
    check_volume(2e9);CHECK(m.describe(0).find("mm")!=std::string::npos);
    CHECK(m.undo());check_volume(1e9);check_top(1);
    CHECK(m.redo());check_volume(2e9);check_top(2);
    m.set_bodies({gtopo::extrude_z(p,v,3)});
    check_volume(3e9);check_top(3);
    m.clear();CHECK(m.describe(0)=="no solid");CHECK(m.mesh().empty());
}

TEST(modeller_sketch_consumption_refreshes_after_geometry_and_history_changes) {
    const auto v = vol();
    Modeller m(v);
    auto sketch = rect_sketch(v);
    m.arm(Modeller::Op::Extrude);
    pull(m, iso_camera(), sketch, 3);
    m.set_sketches({{gtopo::SketchPlane::world_xy(), &sketch, 1}});
    CHECK(m.sketch_consumed(0));
    CHECK(m.sketch_consumed(0));
    const auto added = rect_sketch(v, 12, 0, 16, 5);
    sketch.insert(sketch.end(), added.begin(), added.end());
    m.set_sketches({{gtopo::SketchPlane::world_xy(), &sketch, 2}});
    CHECK(!m.sketch_consumed(0));
    sketch.resize(4);
    m.set_sketches({{gtopo::SketchPlane::world_xy(), &sketch, 3}});
    CHECK(m.sketch_consumed(0));
    CHECK(m.undo());
    CHECK(!m.sketch_consumed(0));
    CHECK(m.redo());
    CHECK(m.sketch_consumed(0));
    const auto history = m.histories();
    m.set_history({}); CHECK(!m.sketch_consumed(0));
    m.set_history(history); CHECK(m.sketch_consumed(0));
    CHECK(!m.sketch_consumed(-1));
    CHECK(!m.sketch_consumed(5));
}

TEST(modeller_multi_edge_toggle_rounds_all_as_one_undo_step) {
    Modeller m(vol());
    for(double x:{0.0,12.0}) {
        gtopo::Profile profile;
        for(auto [u,v]:{std::pair{x,0.0},std::pair{x+8,0.0},std::pair{x+8,5.0},std::pair{x,5.0}})
            profile.outer.push_back(gcm::to_lattice(vol(),u,v));
        m.add_body(gtopo::extrude_z(profile,vol(),3),"Block");
    }
    auto camera=plan_camera();camera.set_target(gview::Point3::from(10,2.5,0));camera.set_distance(40);
    const auto toggle=[&](double x) {
        double px,py;pixel(camera,x,0,3,px,py);
        CHECK(m.toggle_edge_at(camera,px,py));
    };
    toggle(4);toggle(16);
    CHECK(m.selected_edges().size()==2);
    CHECK(m.edge_highlight().size()==2);
    toggle(4);CHECK(m.selected_edges().size()==1);
    toggle(4);CHECK(m.selected_edges().size()==2);
    const auto serial=m.edit_serial();
    CHECK(m.apply_edge(Modeller::Op::FilletEdge,.25));
    CHECK(m.edit_serial()==serial+1);
    for(const auto& body:m.bodies()) CHECK(gtopo::volume(body,vol())<120);
    CHECK(m.selected_edges().size()==2);
    const auto first_volume=gtopo::volume(m.bodies()[0],vol());
    CHECK(!m.apply_edge(Modeller::Op::FilletEdge,100));
    CHECK(std::fabs(gtopo::volume(m.bodies()[0],vol())-first_volume)<1e-8);
    CHECK(m.selected_edges().size()==2);
    CHECK(m.apply_edge(Modeller::Op::FilletEdge,.5));
    CHECK(m.op()==Modeller::Op::FilletEdge);
    CHECK(gtopo::volume(m.bodies()[0],vol())<first_volume);
    CHECK(m.undo());
    CHECK(m.selected_edges().empty());
    for(const auto& body:m.bodies()) CHECK(std::fabs(gtopo::volume(body,vol())-120)<1e-4);
}

TEST(modeller_bulk_erase_validates_before_mutation_and_restores_metadata) {
    Modeller m(vol());
    gtopo::Profile profile;
    profile.outer={gcm::to_lattice(vol(),0,0),gcm::to_lattice(vol(),4,0),
                   gcm::to_lattice(vol(),4,4),gcm::to_lattice(vol(),0,4)};
    for(int i=0;i<3;++i) m.add_body(gtopo::extrude_z(profile,vol(),i+1),"Block");
    const auto metadata=m.metadata();
    const auto serial=m.edit_serial();
    CHECK(!m.erase_many({0,3}));
    CHECK(m.edit_serial()==serial);CHECK(m.bodies().size()==3);
    CHECK(m.erase_many({2,0,2}));
    CHECK(m.edit_serial()==serial+1);CHECK(m.bodies().size()==1);
    CHECK(m.metadata().body_keys[0]==metadata.body_keys[1]);
    CHECK(m.undo());CHECK(m.bodies().size()==3);
    CHECK(m.metadata().body_keys==metadata.body_keys);
}

namespace {
void preview_test_box(Modeller& modeller,double x,double height=3) {
    gtopo::Profile profile;
    for(auto [u,v]:{std::pair{x,0.0},std::pair{x+8,0.0},std::pair{x+8,5.0},std::pair{x,5.0}})
        profile.outer.push_back(gcm::to_lattice(vol(),u,v));
    modeller.add_body(gtopo::extrude_z(profile,vol(),height),"Block");
}
void preview_test_edge(Modeller& modeller,const gview::Camera& camera,double x,double y,double z) {
    double px,py;pixel(camera,x,y,z,px,py);CHECK(modeller.toggle_edge_at(camera,px,py));
}
std::vector<double> displayed_volumes(Modeller& modeller) {
    const auto& mesh=modeller.mesh();std::vector<double> volumes(modeller.bodies().size());
    for(const auto& range:modeller.mesh_ranges()) if(range.body<volumes.size())
        volumes[range.body]=preview_volume(std::vector<MeshVertex>(mesh.begin()+range.first,mesh.begin()+range.first+range.count));
    return volumes;
}
std::vector<std::array<float,3>> displayed_positions(Modeller& modeller) {
    std::vector<std::array<float,3>> result;
    for(const auto& vertex:modeller.mesh()) result.push_back({vertex.pos[0],vertex.pos[1],vertex.pos[2]});
    return result;
}
}

TEST(modeller_multi_edge_preview_across_bodies_matches_commit_and_one_step_undo) {
    for(const auto operation:{Modeller::Op::FilletEdge,Modeller::Op::ChamferEdge}) {
        Modeller modeller(vol());
        for(double x:{0.0,12.0,24.0}) preview_test_box(modeller,x);
        Modeller::Material red;red.name="Preview red";red.r=.8f;red.g=.1f;red.b=.2f;
        modeller.add_material(red);CHECK(modeller.set_material(0,red.name));
        auto camera=plan_camera();camera.set_target(gview::Point3::from(16,2.5,0));camera.set_distance(60);
        preview_test_edge(modeller,camera,4,0,3);preview_test_edge(modeller,camera,16,0,3);
        modeller.set_edge_operation(operation);
        const auto serial=modeller.edit_serial();
        const auto keys=modeller.metadata().body_keys;
        const auto first_history=modeller.history(0).size(),second_history=modeller.history(1).size();
        const auto original=displayed_positions(modeller);
        modeller.set_typed(".25");
        CHECK(modeller.value_preview_valid());CHECK(modeller.value_error().empty());
        const auto volumes=displayed_volumes(modeller);
        CHECK(volumes[0]<120);CHECK(volumes[1]<120);CHECK(std::fabs(volumes[2]-120)<1e-4);
        CHECK(modeller.edit_serial()==serial);CHECK(modeller.metadata().body_keys==keys);
        CHECK(modeller.history(0).size()==first_history);CHECK(modeller.history(1).size()==second_history);
        for(const auto& body:modeller.bodies()) CHECK(std::fabs(gtopo::volume(body,vol())-120)<1e-4);
        for(const auto& range:modeller.mesh_ranges()) {
            CHECK(range.key==keys[range.body]);
            if(range.body==0) CHECK(modeller.mesh()[range.first].color[0]==red.r);
        }
        const auto preview=displayed_positions(modeller);
        CHECK(preview!=original);CHECK(modeller.commit_typed());
        CHECK(modeller.edit_serial()==serial+1);CHECK(displayed_positions(modeller)==preview);
        const double applied=gtopo::volume(modeller.bodies()[0],vol());
        modeller.set_typed(".5");CHECK(modeller.value_preview_valid());
        CHECK(displayed_volumes(modeller)[0]<applied);
        CHECK(std::fabs(gtopo::volume(modeller.bodies()[0],vol())-applied)<1e-8);
        modeller.set_typed("100");CHECK(!modeller.value_preview_valid());CHECK(!modeller.value_error().empty());
        CHECK(displayed_positions(modeller)==preview);
        CHECK(modeller.selected_edges().size()==2);
        modeller.set_typed(".5");CHECK(modeller.value_preview_valid());
        const auto replacement=displayed_positions(modeller);
        CHECK(modeller.commit_typed());CHECK(displayed_positions(modeller)==replacement);
        CHECK(modeller.undo());CHECK(displayed_positions(modeller)==original);
        CHECK(modeller.metadata().body_keys==keys);
        CHECK(modeller.redo());CHECK(displayed_positions(modeller)==replacement);
    }
}

TEST(modeller_multi_edge_preview_on_one_body_cancels_without_geometry_or_history_changes) {
    for(const auto operation:{Modeller::Op::FilletEdge,Modeller::Op::ChamferEdge}) {
        Modeller modeller(vol());preview_test_box(modeller,0);
        const auto camera=plan_camera();
        preview_test_edge(modeller,camera,4,0,3);preview_test_edge(modeller,camera,4,5,3);
        modeller.set_edge_operation(operation);
        const auto serial=modeller.edit_serial();
        const auto original=displayed_positions(modeller);
        std::vector<float> original_wires;modeller.wireframe(original_wires);
        modeller.set_typed(".25");CHECK(modeller.value_preview_valid());
        CHECK(displayed_volumes(modeller)[0]<120);CHECK(modeller.mesh_ranges().size()==1);
        std::vector<float> preview_wires;modeller.wireframe(preview_wires);CHECK(preview_wires!=original_wires);
        modeller.set_typed("");CHECK(!modeller.value_preview_valid());CHECK(displayed_positions(modeller)==original);
        modeller.set_typed(".25");CHECK(modeller.value_preview_valid());
        modeller.cancel();CHECK(!modeller.value_preview_valid());CHECK(modeller.edit_serial()==serial);
        CHECK(displayed_positions(modeller)==original);
        modeller.wireframe(preview_wires);CHECK(preview_wires==original_wires);
        CHECK(modeller.undo());CHECK(modeller.bodies().empty());
    }
}

TEST(modeller_multi_edge_preview_discards_every_draft_if_one_edge_fails) {
    for(const auto operation:{Modeller::Op::FilletEdge,Modeller::Op::ChamferEdge}) {
        Modeller modeller(vol());preview_test_box(modeller,0);preview_test_box(modeller,12,.25);
        auto camera=plan_camera();camera.set_target(gview::Point3::from(10,2.5,0));camera.set_distance(40);
        preview_test_edge(modeller,camera,4,0,3);preview_test_edge(modeller,camera,16,0,.25);
        modeller.set_edge_operation(operation);
        const auto serial=modeller.edit_serial();const auto original=displayed_positions(modeller);
        modeller.set_typed(".05");CHECK(modeller.value_preview_valid());
        modeller.set_typed("1");CHECK(!modeller.value_preview_valid());CHECK(!modeller.value_error().empty());
        CHECK(displayed_positions(modeller)==original);CHECK(modeller.edit_serial()==serial);
        CHECK(modeller.selected_edges().size()==2);
        CHECK(!modeller.commit_typed());CHECK(displayed_positions(modeller)==original);
        CHECK(modeller.edit_serial()==serial);
    }
}


TEST(modeller_context_edge_selection_preserves_multiple_edges_and_live_operation) {
    Modeller modeller(vol());preview_test_box(modeller,0);preview_test_box(modeller,12);
    auto camera=plan_camera();camera.set_target(gview::Point3::from(10,2.5,0));camera.set_distance(40);
    preview_test_edge(modeller,camera,4,0,3);preview_test_edge(modeller,camera,16,0,3);
    modeller.set_edge_operation(Modeller::Op::ChamferEdge);modeller.set_typed(".25");
    CHECK(modeller.value_preview_valid());const auto preview=displayed_positions(modeller);
    const auto serial=modeller.edit_serial();const auto active=modeller.edge_body_index();
    const auto selection=modeller.selected_edges();
    const auto unchanged=[&] {
        CHECK(modeller.selected_edges().size()==selection.size());CHECK(modeller.edge_body_index()==active);
        CHECK(modeller.op()==Modeller::Op::ChamferEdge);CHECK(modeller.stage()==Modeller::Stage::TypeValue);
        CHECK(modeller.typed()==".25");CHECK(modeller.value_preview_valid());
        CHECK(modeller.edit_serial()==serial);CHECK(displayed_positions(modeller)==preview);
        for(std::size_t i=0;i<selection.size() && i<modeller.selected_edges().size();++i) {
            CHECK(modeller.selected_edges()[i].body==selection[i].body);
            CHECK(modeller.selected_edges()[i].halfedge==selection[i].halfedge);
        }
    };
    double px,py;pixel(camera,4,0,3,px,py);bool preserved=false;
    CHECK(modeller.select_context_edge_at(camera,px,py,preserved));CHECK(preserved);unchanged();
    preserved=true;CHECK(!modeller.select_context_edge_at(camera,-1000,-1000,preserved));CHECK(!preserved);unchanged();
    pixel(camera,4,5,3,px,py);CHECK(modeller.select_context_edge_at(camera,px,py,preserved));CHECK(!preserved);
    CHECK(modeller.selected_edges().size()==1);CHECK(modeller.edge_body_index()==0);CHECK(modeller.edge_kept());
    CHECK(modeller.typed().empty());CHECK(!modeller.value_preview_valid());CHECK(modeller.edit_serial()==serial);
}

TEST(modeller_context_edge_selection_preserves_equivalent_circular_ring) {
    const auto volume=vol();gtopo::Profile profile;constexpr int sides=32;
    constexpr double tau=6.2831853071795864769;
    for(int i=0;i<sides;++i)profile.outer.push_back(gcm::to_lattice(volume,2*std::cos(tau*i/sides),2*std::sin(tau*i/sides)));
    auto body=gtopo::extrude_z(profile,volume,3);
    body.face_surface.assign(body.face_count(),gtopo::Surface{});
    for(gcm::u32 face=0;face<body.face_count();++face) {
        const auto plane=gtopo::face_plane(body,face,volume);
        if(plane.valid() && std::abs(plane.nz)<1e-6)body.face_surface[face]=gtopo::Surface::cylinder(0,0,0,0,0,1,2);
    }
    Modeller modeller(volume);modeller.add_body(body,"Cylinder");
    auto camera=plan_camera();camera.set_target(gview::Point3::from(0,0,0));
    const auto at=[&](int i,double& px,double& py) {
        const double a=tau*i/sides,b=tau*(i+1)/sides;
        pixel(camera,std::cos(a)+std::cos(b),std::sin(a)+std::sin(b),3,px,py);
    };
    double px,py;at(0,px,py);CHECK(modeller.toggle_edge_at(camera,px,py));
    CHECK(modeller.selected_edges().size()==1);if(modeller.selected_edges().empty())return;
    const auto selected=modeller.selected_edges().front();
    CHECK(gtopo::circular_edge_ring(body,selected.halfedge,volume).size()>1);
    at(8,px,py);bool preserved=false;CHECK(modeller.select_context_edge_at(camera,px,py,preserved));CHECK(preserved);
    CHECK(modeller.selected_edges().size()==1);CHECK(modeller.selected_edges().front().halfedge==selected.halfedge);
    CHECK(modeller.edge_kept());
}
