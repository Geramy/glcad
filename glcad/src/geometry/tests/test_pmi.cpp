#include "test.hpp"

#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/pmi.hpp"

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

}

TEST(pmi_fingerprint_is_the_same_for_the_same_face) {
    const auto v = vol();
    const Body a = box(v, 0, 0, 4, 3, 2);
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId ta = face_facing(a, v, 0, 0, 1);
    const FaceId tb = face_facing(b, v, 0, 0, 1);
    CHECK_MSG(fingerprint_of(a, PmiTarget::Face, ta, v) ==
                  fingerprint_of(b, PmiTarget::Face, tb, v),
              "the same face fingerprinted differently on a rebuild");
    const FaceId bottom = face_facing(a, v, 0, 0, -1);
    CHECK(fingerprint_of(a, PmiTarget::Face, ta, v) !=
          fingerprint_of(a, PmiTarget::Face, bottom, v));
}

TEST(pmi_reference_survives_a_face_renumber) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId top = face_facing(b, v, 0, 0, 1);

    PmiRef r = make_ref(b, PmiTarget::Face, top, v);
    CHECK(resolve(r, b, v) == top);

    r.index = (top + 3) % b.face_count();
    CHECK_MSG(resolve(r, b, v) == top,
              "a renumbered reference did not find its face again");
}

TEST(pmi_reference_reports_a_lost_face_rather_than_guessing) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const PmiRef r = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);

    const Body taller = box(v, 0, 0, 4, 3, 9);
    CHECK_MSG(resolve(r, taller, v) == kInvalid,
              "a dimension found a face that no longer exists at that height");

    const Body empty;
    CHECK(resolve(r, empty, v) == kInvalid);
    CHECK(fingerprint_of(b, PmiTarget::Face, 999, v) == 0);
}

TEST(pmi_reference_refuses_an_ambiguous_match) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiRef r;
    r.target = PmiTarget::Face;
    r.index = 0;
    r.fingerprint = 0;
    CHECK(resolve(r, b, v) == kInvalid);
}

TEST(pmi_edge_fingerprint_is_the_same_from_either_side) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    bool checked = false;
    for (u32 h = 0; h < b.halfedge_count(); ++h) {
        const u32 twin = b.he_twin[h];
        if (twin == kInvalid || twin >= b.halfedge_count()) continue;
        CHECK_MSG(fingerprint_of(b, PmiTarget::Edge, h, v) ==
                      fingerprint_of(b, PmiTarget::Edge, twin, v),
                  "halfedge %u and its twin fingerprinted differently", h);
        checked = true;
    }
    CHECK(checked);
}

TEST(pmi_measures_a_thickness_between_opposed_faces) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);

    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.nominal = 2.0;

    const PmiMeasurement m = measure(b, d, v);
    CHECK_MSG(m.ok, "%s", m.reason.c_str());
    CHECK_MSG(std::fabs(m.value - 2.0) < 1e-6, "measured %.6f", m.value);
    CHECK(m.in_tolerance);
}

TEST(pmi_refuses_to_measure_a_distance_between_faces_that_are_not_parallel) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Distance;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 1, 0, 0), v);
    const PmiMeasurement m = measure(b, d, v);
    CHECK(!m.ok);
    CHECK(!m.reason.empty());
}

TEST(pmi_measures_the_angle_between_the_faces_not_their_normals) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Angle;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 1, 0, 0), v);
    const PmiMeasurement m = measure(b, d, v);
    CHECK_MSG(m.ok, "%s", m.reason.c_str());
    CHECK_MSG(std::fabs(m.value - 90.0) < 1e-6,
              "a square corner measured %.4f degrees", m.value);

    PmiDimension flat = d;
    flat.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    const PmiMeasurement fm = measure(b, flat, v);
    CHECK(fm.ok && std::fabs(fm.value) < 1e-6);
}

TEST(pmi_diameter_needs_a_round_face) {
    const auto v = vol();
    Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Diameter;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 1, 0, 0), v);
    CHECK(!measure(b, d, v).ok);

    b.face_surface.assign(b.face_count(), Surface{});
    const FaceId f = face_facing(b, v, 1, 0, 0);
    b.face_surface[f] = Surface::cylinder(0, 0, 0, 0, 0, 1, 1.5);
    PmiDimension dd;
    dd.kind = PmiKind::Diameter;
    dd.a = make_ref(b, PmiTarget::Face, f, v);
    dd.nominal = 3.0;
    const PmiMeasurement m = measure(b, dd, v);
    CHECK_MSG(m.ok, "%s", m.reason.c_str());
    CHECK_MSG(std::fabs(m.value - 3.0) < 1e-9, "measured %.6f", m.value);

    PmiDimension rr = dd;
    rr.kind = PmiKind::Radius;
    rr.nominal = 1.5;
    CHECK(std::fabs(measure(b, rr, v).value - 1.5) < 1e-9);
}

TEST(pmi_tolerance_bands_are_asymmetric_when_the_drawing_says_so) {
    Tolerance t;
    t.plus = 0.05;
    t.minus = 0.02;
    CHECK(!t.none());
    CHECK(std::fabs(t.upper(40.0) - 40.05) < 1e-12);
    CHECK(std::fabs(t.lower(40.0) - 39.98) < 1e-12);
    CHECK(t.contains(40.0, 40.04));
    CHECK(t.contains(40.0, 39.99));
    CHECK(!t.contains(40.0, 40.06));
    CHECK(!t.contains(40.0, 39.97));
    CHECK(t.contains(40.0, 40.05));
    CHECK(t.contains(40.0, 39.98));

    Tolerance none;
    CHECK(none.none());
}

TEST(pmi_measurement_reports_out_of_tolerance) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.nominal = 2.5;
    d.tolerance = {0.1, 0.1};

    const PmiMeasurement m = measure(b, d, v);
    CHECK(m.ok);
    CHECK_MSG(!m.in_tolerance, "2.0 was accepted against 2.5 +/- 0.1");

    d.tolerance = {0.6, 0.6};
    CHECK(measure(b, d, v).in_tolerance);
}

TEST(pmi_a_reference_dimension_will_not_drive) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.driving = false;

    const DriveResult r = drive(b, d, 5.0, v);
    CHECK(!r.ok);
    CHECK(!r.reason.empty());
    CHECK_MSG(std::fabs(volume(r.body, v) - volume(b, v)) < 1e-9,
              "a reference dimension moved the model anyway");
}

TEST(pmi_driving_a_thickness_moves_the_face_the_right_way) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.nominal = 2.0;
    d.driving = true;
    d.moves_b = true;

    const DriveResult up = drive(b, d, 5.0, v);
    CHECK_MSG(up.ok, "%s", up.reason.c_str());
    CHECK(std::fabs(up.before - 2.0) < 1e-6);
    CHECK_MSG(std::fabs(up.after - 5.0) < 1e-6, "ended at %.6f", up.after);
    CHECK_MSG(up.volume_after > up.volume_before,
              "growing the part shrank its volume: %.4f to %.4f",
              up.volume_before, up.volume_after);

    const DriveResult down = drive(b, d, 1.0, v);
    CHECK_MSG(down.ok, "%s", down.reason.c_str());
    CHECK(std::fabs(down.after - 1.0) < 1e-6);
    CHECK_MSG(down.volume_after < down.volume_before,
              "shrinking the part grew its volume");
}

TEST(pmi_driving_to_the_size_it_already_is_is_a_no_op) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.driving = true;

    const DriveResult r = drive(b, d, 2.0, v);
    CHECK(r.ok);
    CHECK(std::fabs(volume(r.body, v) - volume(b, v)) < 1e-9);
}

TEST(pmi_driving_moves_the_face_the_dimension_nominates) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const Plane bottom_before = face_plane(b, face_facing(b, v, 0, 0, -1), v);

    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.driving = true;
    d.moves_b = true;

    const DriveResult r = drive(b, d, 5.0, v);
    CHECK(r.ok);
    const Plane bottom_after =
        face_plane(r.body, face_facing(r.body, v, 0, 0, -1), v);
    CHECK_MSG(std::fabs(bottom_after.d - bottom_before.d) < 1e-6,
              "the datum face moved from d=%.6f to d=%.6f", bottom_before.d,
              bottom_after.d);
}

TEST(pmi_driving_a_diameter_changes_the_radius) {
    const auto v = vol();
    Body b = box(v, 0, 0, 4, 3, 2);
    b.face_surface.assign(b.face_count(), Surface{});
    const FaceId f = face_facing(b, v, 1, 0, 0);
    b.face_surface[f] = Surface::cylinder(0, 0, 0, 0, 0, 1, 1.5);

    PmiDimension d;
    d.kind = PmiKind::Diameter;
    d.a = make_ref(b, PmiTarget::Face, f, v);
    d.driving = true;

    const DriveResult r = drive(b, d, 5.0, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());
    CHECK(std::fabs(r.body.face_surface[f].radius - 2.5) < 1e-9);

    CHECK(!drive(b, d, 0.0, v).ok);
    CHECK(!drive(b, d, -1.0, v).ok);
}

TEST(pmi_driving_a_broken_reference_is_refused) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.driving = true;

    const Body other = box(v, 0, 0, 9, 9, 9);
    const DriveResult r = drive(other, d, 5.0, v);
    CHECK(!r.ok);
    CHECK(!r.reason.empty());
}

TEST(pmi_set_finds_two_dimensions_driving_the_same_face) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    const FaceId bottom = face_facing(b, v, 0, 0, -1);
    const FaceId top = face_facing(b, v, 0, 0, 1);

    PmiSet set;
    PmiDimension a;
    a.kind = PmiKind::Thickness;
    a.a = make_ref(b, PmiTarget::Face, bottom, v);
    a.b = make_ref(b, PmiTarget::Face, top, v);
    a.driving = true;
    set.add(a);

    PmiDimension c = a;
    c.label = "again";
    set.add(c);

    const auto conflicts = set.conflicts(b, v);
    CHECK_MSG(conflicts.size() == 1, "%zu conflicts found", conflicts.size());
    CHECK(conflicts[0].a == 0 && conflicts[0].b == 1);
    CHECK(!conflicts[0].reason.empty());

    PmiSet mixed;
    mixed.add(a);
    PmiDimension ref = a;
    ref.driving = false;
    mixed.add(ref);
    CHECK_MSG(mixed.conflicts(b, v).empty(),
              "a reference dimension was called a conflict");
}

TEST(pmi_set_inspection_is_the_report_a_drawing_cannot_produce) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiSet set;

    PmiDimension ok;
    ok.kind = PmiKind::Thickness;
    ok.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    ok.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    ok.nominal = 2.0;
    ok.tolerance = {0.1, 0.1};
    set.add(ok);

    PmiDimension bad = ok;
    bad.nominal = 3.0;
    set.add(bad);

    const PmiSet::Report r = set.inspect(b, v);
    CHECK(r.measurements.size() == 2);
    CHECK_MSG(r.out_of_tolerance == 1, "%zu out of tolerance",
              r.out_of_tolerance);
    CHECK(r.broken == 0);
    CHECK(!r.passes());

    set.remove(1);
    CHECK(set.inspect(b, v).passes());
    CHECK(set.size() == 1);
    CHECK(set.at(0) != nullptr);
    CHECK(set.at(9) == nullptr);
    CHECK(!set.remove(9));
}

TEST(pmi_set_inspection_counts_broken_references) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiSet set;
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    set.add(d);

    const Body other = box(v, 0, 0, 4, 3, 7);
    const PmiSet::Report r = set.inspect(other, v);
    CHECK_MSG(r.broken == 1, "%zu broken references", r.broken);
    CHECK(!r.passes());
}

TEST(pmi_set_reattach_repairs_an_index_without_inventing_one) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiSet set;
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    set.add(d);

    set.at(0)->a.index = 99;
    set.at(0)->b.index = 98;
    CHECK_MSG(set.reattach(b, v) == 2, "not every reference was repaired");
    CHECK(set.inspect(b, v).broken == 0);

    PmiSet lost;
    lost.add(d);
    const Body other = box(v, 0, 0, 4, 3, 7);
    CHECK_MSG(lost.reattach(other, v) == 0,
              "a reference was pointed at a plausible-looking face");
}

TEST(pmi_kind_names_exist) {
    for (PmiKind k : {PmiKind::Distance, PmiKind::Diameter, PmiKind::Radius,
                      PmiKind::Angle, PmiKind::Thickness})
        CHECK(pmi_kind_name(k)[0] != '\0');
}

TEST(pmi_driving_hands_back_a_dimension_that_still_finds_its_face) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.nominal = 2.0;
    d.driving = true;

    const DriveResult r = drive(b, d, 5.0, v);
    CHECK_MSG(r.ok, "%s", r.reason.c_str());

    CHECK(!measure(r.body, d, v).ok);
    const PmiMeasurement m = measure(r.body, r.updated, v);
    CHECK_MSG(m.ok, "the returned dimension could not find its own face: %s",
              m.reason.c_str());
    CHECK(std::fabs(m.value - 5.0) < 1e-6);
}

TEST(pmi_a_dimension_with_no_nominal_has_nothing_to_be_out_of) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    const PmiMeasurement m = measure(b, d, v);
    CHECK(m.ok && m.in_tolerance);
    CHECK(std::fabs(m.value - 2.0) < 1e-6);
}

TEST(pmi_an_exact_nominal_is_checked_with_lattice_slack_not_an_epsilon) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Thickness;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, -1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.nominal = 2.0;
    CHECK_MSG(measure(b, d, v).in_tolerance,
              "an exactly-modelled 2 m was reported out of tolerance");

    d.nominal = 2.001;
    CHECK(!measure(b, d, v).in_tolerance);
}

TEST(pmi_angle_dimensions_refuse_to_drive_rather_than_approximate) {
    const auto v = vol();
    const Body b = box(v, 0, 0, 4, 3, 2);
    PmiDimension d;
    d.kind = PmiKind::Angle;
    d.a = make_ref(b, PmiTarget::Face, face_facing(b, v, 0, 0, 1), v);
    d.b = make_ref(b, PmiTarget::Face, face_facing(b, v, 1, 0, 0), v);
    d.driving = true;
    const DriveResult r = drive(b, d, 80.0, v);
    CHECK(!r.ok);
    CHECK(!r.reason.empty());
    CHECK(std::fabs(volume(r.body, v) - volume(b, v)) < 1e-9);
}
