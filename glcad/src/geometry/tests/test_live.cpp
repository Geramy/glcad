#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "geometry/extrude.hpp"
#include "geometry/live.hpp"

using namespace gtopo;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

Body box(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1, f64 h,
         f64 z0 = 0.0) {
    Profile p;
    p.outer = {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
               gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
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

HalfedgeId edge_between(const Body& b, FaceId a, FaceId c) {
    for (u32 h = 0; h < b.halfedge_count(); ++h) {
        FaceId fa, fb;
        if (!faces_of_edge(b, h, fa, fb)) continue;
        if ((fa == a && fb == c) || (fa == c && fb == a)) return h;
    }
    return kInvalid;
}

std::size_t count_of(const LiveRules& r, RuleKind k) {
    std::size_t n = 0;
    for (const auto& x : r.all())
        if (x.kind == k) ++n;
    return n;
}

}

TEST(live_a_box_has_three_symmetric_pairs) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);

    LiveRules r;
    r.discover(b, v);
    CHECK_MSG(r.size() > 0, "nothing was discovered on a box");
    CHECK_MSG(count_of(r, RuleKind::Symmetric) == 3,
              "%zu symmetric pairs on a box", count_of(r, RuleKind::Symmetric));
    CHECK_MSG(count_of(r, RuleKind::Perpendicular) == 12,
              "%zu perpendicular pairs", count_of(r, RuleKind::Perpendicular));
}

TEST(live_rules_record_the_tolerance_they_were_found_within) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    LiveRules r;
    r.discover(b, v);
    for (const auto& rule : r.all()) {
        CHECK(rule.tolerance >= 0.0);
        CHECK_MSG(rule.tolerance < 0.05, "a rule was found at %.6f",
                  rule.tolerance);
    }
}

TEST(live_symmetric_rules_carry_their_mid_plane) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    LiveRules r;
    r.discover(b, v);

    bool checked = false;
    for (const auto& rule : r.all()) {
        if (rule.kind != RuleKind::Symmetric) continue;
        CHECK_MSG(rule.plane.valid(), "a symmetric rule has no plane");
        checked = true;
    }
    CHECK(checked);
}

TEST(live_curved_rules_come_from_the_exact_surfaces) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult filleted = fillet_edge_exact(b, e, 0.5, 6, v);
    CHECK(filleted.ok);

    LiveRules r;
    r.discover(filleted.body, v);
    CHECK_MSG(count_of(r, RuleKind::EqualRadius) > 0,
              "the blend's strips share a radius and no rule said so");

    const EditResult plain = fillet_edge(b, e, 0.5, 6, v);
    CHECK(plain.ok);
    LiveRules r2;
    r2.discover(plain.body, v);
    CHECK_MSG(count_of(r2, RuleKind::EqualRadius) == 0,
              "a radius rule was invented from facets");
}

TEST(live_each_rule_kind_can_be_switched_off) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);

    LiveRuleSettings s;
    s.symmetric = false;
    LiveRules r;
    r.discover(b, v, s);
    CHECK(count_of(r, RuleKind::Symmetric) == 0);
    CHECK_MSG(count_of(r, RuleKind::Parallel) > 0,
              "switching off symmetry removed parallelism too");
}

TEST(live_rule_count_is_bounded) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    LiveRuleSettings s;
    s.max_rules = 4;
    LiveRules r;
    r.discover(b, v, s);
    CHECK_MSG(r.size() <= 4, "%zu rules past the cap", r.size());
}

TEST(live_a_symmetric_companion_moves_the_opposite_way) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    LiveRules r;
    r.discover(b, v);

    const FaceId east = face_facing(b, v, 1, 0, 0);
    const auto companions = r.companions(east);
    CHECK_MSG(!companions.empty(), "no companions for a box face");

    bool found_symmetric = false;
    for (const auto& c : companions)
        if (c.because == RuleKind::Symmetric) {
            found_symmetric = true;
            CHECK_MSG(c.factor < 0.0, "a symmetric companion moves with factor %.2f",
                      c.factor);
        }
    CHECK(found_symmetric);
}

TEST(live_a_disabled_rule_produces_no_companion) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    LiveRules r;
    r.discover(b, v);
    const FaceId east = face_facing(b, v, 1, 0, 0);
    CHECK(!r.companions(east).empty());

    r.set_enabled(RuleKind::Symmetric, false);
    r.set_enabled(RuleKind::Parallel, false);
    r.set_enabled(RuleKind::Perpendicular, false);
    r.set_enabled(RuleKind::Tangent, false);
    r.set_enabled(RuleKind::Coplanar, false);
    CHECK_MSG(r.companions(east).empty(), "a disabled rule still fired");
}

TEST(live_suspension_is_temporary_not_destructive) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    LiveRules r;
    r.discover(b, v);
    const std::size_t before = r.size();
    const FaceId east = face_facing(b, v, 1, 0, 0);

    r.suspend();
    CHECK(r.suspended());
    CHECK_MSG(r.companions(east).empty(), "a suspended rule still fired");
    CHECK_MSG(r.size() == before, "suspension deleted %zu rules",
              before - r.size());

    r.resume();
    CHECK(!r.suspended());
    CHECK(!r.companions(east).empty());
}

TEST(live_a_face_can_see_which_rules_it_is_ignoring) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    LiveRules r;
    r.discover(b, v);
    const FaceId east = face_facing(b, v, 1, 0, 0);

    const auto involved = r.rules_for(east);
    CHECK(!involved.empty());
    r.set_enabled(involved[0], false);
    CHECK_MSG(r.rules_for(east).size() == involved.size(),
              "disabling a rule hid it from the list");
    CHECK(!r.at(involved[0])->enabled);
}

TEST(live_companions_are_reported_once_each) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 6, 8);
    LiveRules r;
    r.discover(b, v);
    const FaceId east = face_facing(b, v, 1, 0, 0);

    const auto companions = r.companions(east);
    for (std::size_t i = 0; i < companions.size(); ++i)
        for (std::size_t j = i + 1; j < companions.size(); ++j)
            CHECK_MSG(companions[i].face != companions[j].face,
                      "face %u was listed twice", companions[i].face);
}

TEST(live_rule_kinds_are_all_named) {
    for (int i = 0; i <= static_cast<int>(RuleKind::Coaxial); ++i)
        CHECK(std::string(rule_kind_name(static_cast<RuleKind>(i))).size() > 0);
}

TEST(select_face_scope_takes_only_what_was_picked) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    SelectManager sm;
    sm.set_body(&b, &v);

    const SelectionResult r = sm.select(2, SelectScope::Face);
    CHECK(r.ok);
    CHECK(r.faces.size() == 1);
    CHECK(r.faces[0] == 2);
    CHECK(!r.description.empty());
}

TEST(select_body_scope_takes_everything) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    SelectManager sm;
    sm.set_body(&b, &v);
    CHECK(sm.select(0, SelectScope::Body).faces.size() == b.face_count());
}

TEST(select_tangent_scope_takes_a_whole_blend) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult filleted = fillet_edge(b, e, 0.5, 8, v);
    CHECK(filleted.ok);

    SelectManager sm;
    sm.set_body(&filleted.body, &v);
    sm.tangent_tolerance() = 0.4;

    const FaceId strip = filleted.body.face_count() - 4;
    const SelectionResult r = sm.select(strip, SelectScope::Tangent);
    CHECK(r.ok);
    CHECK_MSG(r.faces.size() >= 8, "%zu faces from one blend strip",
              r.faces.size());
}

TEST(select_same_radius_finds_every_hole_of_that_size) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult filleted = fillet_edge_exact(b, e, 0.5, 6, v);
    CHECK(filleted.ok);

    FaceId curved = kInvalid;
    for (FaceId f = 0; f < filleted.body.face_count(); ++f)
        if (filleted.body.face_is_curved(f)) curved = f;
    CHECK(curved != kInvalid);

    SelectManager sm;
    sm.set_body(&filleted.body, &v);
    const SelectionResult r = sm.select(curved, SelectScope::SameRadius);
    CHECK(r.ok);
    CHECK_MSG(r.faces.size() == 6, "%zu faces share that radius", r.faces.size());
}

TEST(select_coplanar_scope) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    SelectManager sm;
    sm.set_body(&b, &v);
    const SelectionResult r = sm.select(0, SelectScope::Coplanar);
    CHECK(r.ok);
    CHECK_MSG(r.faces.size() == 1, "%zu coplanar faces on a box", r.faces.size());
}

TEST(select_feature_scope_falls_back_when_nothing_recognises_it) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    SelectManager sm;
    sm.set_body(&b, &v);

    const SelectionResult r = sm.select(0, SelectScope::Feature);
    CHECK_MSG(r.ok, "the fallback produced nothing");
    CHECK_MSG(r.scope == SelectScope::Tangent, "it fell back to %s",
              select_scope_name(r.scope));
}

TEST(select_feature_scope_uses_recognition_when_it_has_it) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 8, 10, 12);
    const HalfedgeId e = edge_between(b, face_facing(b, v, 0, 0, 1),
                                      face_facing(b, v, 1, 0, 0));
    const EditResult filleted = fillet_edge_exact(b, e, 0.5, 6, v);
    CHECK(filleted.ok);

    const RecognitionResult rec = recognise(filleted.body, v);
    SelectManager sm;
    sm.set_body(&filleted.body, &v);
    sm.set_features(&rec);

    CHECK(!rec.features.empty());
    const FaceId owned = rec.features[0].primary_faces[0];
    const SelectionResult r = sm.select(owned, SelectScope::Feature);
    CHECK(r.ok);
    CHECK_MSG(r.scope == SelectScope::Feature, "it fell back to %s",
              select_scope_name(r.scope));
    CHECK(!r.description.empty());
}

TEST(select_repeated_clicks_widen_the_scope) {
    SelectManager sm;
    CHECK(sm.next_scope(SelectScope::Face) == SelectScope::Feature);
    CHECK(sm.next_scope(SelectScope::Feature) == SelectScope::Tangent);
    CHECK(sm.next_scope(SelectScope::Tangent) == SelectScope::Body);
    CHECK(sm.next_scope(SelectScope::Body) == SelectScope::Face);
}

TEST(select_with_no_body_is_refused) {
    SelectManager sm;
    const SelectionResult r = sm.select(0, SelectScope::Face);
    CHECK(!r.ok);
    CHECK(!r.description.empty());

    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 4, 4);
    sm.set_body(&b, &v);
    CHECK(!sm.select(9999, SelectScope::Face).ok);
}

TEST(select_scopes_are_all_named) {
    for (int i = 0; i <= static_cast<int>(SelectScope::Loop); ++i)
        CHECK(std::string(select_scope_name(static_cast<SelectScope>(i))).size() >
              0);
}
