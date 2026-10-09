#include "test.hpp"

#include <cmath>

#include "exchange/views.hpp"

using namespace gdraft;

static gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

static gtopo::Body box(const gcm::WorkingVolume& v, f64 sx, f64 sy, f64 sz) {
    gtopo::Builder b;
    const f64 hx = sx * 0.5, hy = sy * 0.5, hz = sz * 0.5;
    auto add = [&](f64 x, f64 y, f64 z) {
        return b.add_vertex(gcm::to_lattice(v, x, y, z));
    };
    const auto v000 = add(-hx, -hy, -hz);
    const auto v100 = add( hx, -hy, -hz);
    const auto v110 = add( hx,  hy, -hz);
    const auto v010 = add(-hx,  hy, -hz);
    const auto v001 = add(-hx, -hy,  hz);
    const auto v101 = add( hx, -hy,  hz);
    const auto v111 = add( hx,  hy,  hz);
    const auto v011 = add(-hx,  hy,  hz);

    auto face = [&](std::vector<gtopo::VertexId> loop) {
        b.begin_face();
        b.add_loop(loop);
        b.end_face();
    };
    face({v000, v010, v110, v100});   // bottom, -Z
    face({v001, v101, v111, v011});   // top, +Z
    face({v000, v100, v101, v001});   // front, -Y
    face({v110, v010, v011, v111});   // back, +Y
    face({v100, v110, v111, v101});   // right, +X
    face({v000, v001, v011, v010});   // left, -X
    return b.build();
}

static SourceModel model(const gcm::WorkingVolume& v, f64 sx, f64 sy, f64 sz) {
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = box(v, sx, sy, sz);
    return m;
}

TEST(draft_front_view_shows_width_and_height_not_depth) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);   // wide 3, deep 7, tall 5
    const ViewContent c = project_body(m.body, v, StandardView::Front, 1.0);
    CHECK_MSG(std::fabs(c.width - 3.0) < 1e-3, "front is %.4f wide", c.width);
    CHECK_MSG(std::fabs(c.height - 5.0) < 1e-3, "front is %.4f tall", c.height);
}

TEST(draft_top_view_shows_width_and_depth) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    const ViewContent c = project_body(m.body, v, StandardView::Top, 1.0);
    CHECK(std::fabs(c.width - 3.0) < 1e-3);
    CHECK_MSG(std::fabs(c.height - 7.0) < 1e-3, "top is %.4f deep", c.height);
}

TEST(draft_right_view_shows_depth_and_height) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    const ViewContent c = project_body(m.body, v, StandardView::Right, 1.0);
    CHECK(std::fabs(c.width - 7.0) < 1e-3);
    CHECK(std::fabs(c.height - 5.0) < 1e-3);
}

TEST(draft_the_six_orthographic_views_agree_on_up) {
    for (StandardView d : {StandardView::Front, StandardView::Back,
                           StandardView::Left, StandardView::Right}) {
        f64 r[3], u[3], t[3];
        view_axes(d, r, u, t);
        CHECK_MSG(std::fabs(u[2] - 1.0) < 1e-12, "%s does not have Z up",
                  gview::view_name(d));
    }
}

TEST(draft_view_axes_are_orthonormal_and_right_handed) {
    for (StandardView d : {StandardView::Front, StandardView::Back,
                           StandardView::Left, StandardView::Right,
                           StandardView::Top, StandardView::Bottom,
                           StandardView::Iso, StandardView::IsoNE,
                           StandardView::IsoNW, StandardView::IsoSE,
                           StandardView::IsoSW}) {
        f64 r[3], u[3], t[3];
        view_axes(d, r, u, t);
        auto dot = [](const f64 a[3], const f64 b[3]) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        CHECK_MSG(std::fabs(dot(r, r) - 1.0) < 1e-9, "%s right is not unit",
                  gview::view_name(d));
        CHECK(std::fabs(dot(u, u) - 1.0) < 1e-9);
        CHECK(std::fabs(dot(t, t) - 1.0) < 1e-9);
        CHECK_MSG(std::fabs(dot(r, u)) < 1e-9, "%s axes are not square",
                  gview::view_name(d));
        CHECK(std::fabs(dot(r, t)) < 1e-9);
        CHECK(std::fabs(dot(u, t)) < 1e-9);
        // right x up == toward: a left-handed frame mirrors the part.
        const f64 cx = r[1] * u[2] - r[2] * u[1];
        const f64 cy = r[2] * u[0] - r[0] * u[2];
        const f64 cz = r[0] * u[1] - r[1] * u[0];
        CHECK_MSG(std::fabs(cx - t[0]) < 1e-9 && std::fabs(cy - t[1]) < 1e-9 &&
                      std::fabs(cz - t[2]) < 1e-9,
                  "%s is left-handed -- the part comes out mirrored",
                  gview::view_name(d));
    }
}

TEST(draft_isometric_foreshortens_all_three_axes_equally) {
    const auto v = vol();
    const auto m = model(v, 4.0, 4.0, 4.0);
    const ViewContent c = project_body(m.body, v, StandardView::Iso, 1.0);
    // A cube of side s spans s*sqrt(2) across the flats and 2*s*sqrt(2/3) tall.
    CHECK_MSG(std::fabs(c.width - 4.0 * std::sqrt(2.0)) < 1e-3,
              "iso box is %.4f wide", c.width);
    CHECK_MSG(std::fabs(c.height - 2.0 * 4.0 * std::sqrt(2.0 / 3.0)) < 1e-3,
              "iso box is %.4f tall", c.height);
    CHECK(std::fabs(c.height / c.width - 2.0 / std::sqrt(3.0)) < 1e-6);

    f64 r[3], u[3], t[3];
    view_axes(StandardView::Iso, r, u, t);
    const f64 lx = std::hypot(r[0], u[0]);
    const f64 ly = std::hypot(r[1], u[1]);
    const f64 lz = std::hypot(r[2], u[2]);
    CHECK_MSG(std::fabs(lx - ly) < 1e-9 && std::fabs(ly - lz) < 1e-9,
              "axes measure %.6f %.6f %.6f -- not isometric", lx, ly, lz);
    // And that common length is the isometric ratio, sqrt(2/3).
    CHECK(std::fabs(lx - std::sqrt(2.0 / 3.0)) < 1e-9);
}

TEST(draft_scale_divides_the_paper_size) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    const ViewContent one = project_body(m.body, v, StandardView::Front, 1.0);
    const ViewContent fifty = project_body(m.body, v, StandardView::Front, 50.0);
    CHECK_MSG(std::fabs(fifty.width - one.width / 50.0) < 1e-9,
              "3 m at 1:50 measured %.6f m", fifty.width);
    CHECK(std::fabs(fifty.height - one.height / 50.0) < 1e-9);
}

TEST(draft_content_is_centred_on_the_view_origin) {
    const auto v = vol();
    SourceModel m = model(v, 3.0, 7.0, 5.0);
    for (auto& p : m.body.vertex_point) p.x += 1000000000;

    const ViewContent c = project_body(m.body, v, StandardView::Front, 1.0);
    f64 lo = 1e30, hi = -1e30;
    for (const Edge2& e : c.visible) {
        lo = std::min({lo, e.x0, e.x1});
        hi = std::max({hi, e.x0, e.x1});
    }
    CHECK_MSG(std::fabs(lo + hi) < 1e-6, "the view is off centre by %.6f",
              lo + hi);
}

TEST(draft_hidden_edges_are_separated_from_visible_ones) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    const ViewContent c = project_body(m.body, v, StandardView::Front, 1.0, true);

    CHECK_MSG(!c.visible.empty(), "nothing was visible");
    CHECK_MSG(!c.hidden.empty(), "a solid box showed no hidden detail");
    CHECK_MSG(c.visible.size() + c.hidden.size() + c.degenerate == 12,
              "a box has 12 edges, got %zu + %zu + %zu", c.visible.size(),
              c.hidden.size(), c.degenerate);
    CHECK_MSG(c.visible.size() == 4, "%zu visible edges", c.visible.size());
    CHECK_MSG(c.hidden.size() == 4, "%zu hidden edges", c.hidden.size());
    CHECK_MSG(c.degenerate == 4, "%zu degenerate edges", c.degenerate);
}

TEST(draft_hidden_lines_can_be_switched_off) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    const ViewContent off = project_body(m.body, v, StandardView::Front, 1.0, false);
    CHECK(off.hidden.empty());
    CHECK(!off.visible.empty());
    const ViewContent on = project_body(m.body, v, StandardView::Front, 1.0, true);
    CHECK(std::fabs(off.width - on.width) < 1e-9);
    CHECK(std::fabs(off.height - on.height) < 1e-9);
}

TEST(draft_the_outline_of_a_box_is_its_silhouette) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    const ViewContent c = project_body(m.body, v, StandardView::Front, 1.0);
    CHECK_MSG(c.silhouette.size() == 4, "%zu silhouette edges",
              c.silhouette.size());
    for (const Edge2& e : c.silhouette) {
        const f64 hx = 1.5, hy = 2.5;
        const bool on_edge =
            (std::fabs(std::fabs(e.x0) - hx) < 1e-3 && std::fabs(std::fabs(e.x1) - hx) < 1e-3) ||
            (std::fabs(std::fabs(e.y0) - hy) < 1e-3 && std::fabs(std::fabs(e.y1) - hy) < 1e-3);
        CHECK_MSG(on_edge, "a silhouette edge is not on the outline");
    }
}

TEST(draft_each_edge_is_drawn_once) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    const ViewContent c = project_body(m.body, v, StandardView::Front, 1.0);
    CHECK(c.visible.size() + c.hidden.size() + c.degenerate == 12);

    for (std::size_t i = 0; i < c.visible.size(); ++i)
        for (std::size_t j = i + 1; j < c.visible.size(); ++j) {
            const Edge2& a = c.visible[i];
            const Edge2& b = c.visible[j];
            const bool same =
                (std::fabs(a.x0 - b.x0) < 1e-9 && std::fabs(a.y0 - b.y0) < 1e-9 &&
                 std::fabs(a.x1 - b.x1) < 1e-9 && std::fabs(a.y1 - b.y1) < 1e-9) ||
                (std::fabs(a.x0 - b.x1) < 1e-9 && std::fabs(a.y0 - b.y1) < 1e-9 &&
                 std::fabs(a.x1 - b.x0) < 1e-9 && std::fabs(a.y1 - b.y0) < 1e-9);
            CHECK_MSG(!same, "an edge was drawn twice");
        }

    const ViewContent iso = project_body(m.body, v, StandardView::Iso, 1.0);
    CHECK(iso.degenerate == 0);
    CHECK(iso.visible.size() + iso.hidden.size() == 12);
}

TEST(draft_third_angle_puts_the_top_view_above) {
    f64 r, u;
    projection_offset(StandardView::Front, StandardView::Top,
                      ProjectionAngle::Third, r, u);
    CHECK_MSG(u > 0.0, "third-angle top view went below the front");
    CHECK(std::fabs(r) < 1e-12);

    projection_offset(StandardView::Front, StandardView::Right,
                      ProjectionAngle::Third, r, u);
    CHECK_MSG(r > 0.0, "third-angle right view went to the left");
    CHECK(std::fabs(u) < 1e-12);
}

TEST(draft_first_angle_is_the_mirror_of_third) {
    for (StandardView d : {StandardView::Top, StandardView::Bottom,
                           StandardView::Left, StandardView::Right,
                           StandardView::Back}) {
        f64 r1, u1, r3, u3;
        projection_offset(StandardView::Front, d, ProjectionAngle::First, r1, u1);
        projection_offset(StandardView::Front, d, ProjectionAngle::Third, r3, u3);
        CHECK_MSG(std::fabs(r1 + r3) < 1e-12 && std::fabs(u1 + u3) < 1e-12,
                  "%s: first (%g,%g) is not the mirror of third (%g,%g)",
                  gview::view_name(d), r1, u1, r3, u3);
    }
}

TEST(draft_first_angle_puts_the_top_view_below) {
    f64 r, u;
    projection_offset(StandardView::Front, StandardView::Top,
                      ProjectionAngle::First, r, u);
    CHECK_MSG(u < 0.0, "first-angle top view went above the front");
    projection_offset(StandardView::Front, StandardView::Right,
                      ProjectionAngle::First, r, u);
    CHECK_MSG(r < 0.0, "first-angle right view went to the right");
}

TEST(draft_alignment_follows_the_direction) {
    CHECK(alignment_for(StandardView::Front, StandardView::Top) ==
          Alignment::Vertical);
    CHECK(alignment_for(StandardView::Front, StandardView::Bottom) ==
          Alignment::Vertical);
    CHECK(alignment_for(StandardView::Front, StandardView::Right) ==
          Alignment::Horizontal);
    CHECK(alignment_for(StandardView::Front, StandardView::Left) ==
          Alignment::Horizontal);
    CHECK(alignment_for(StandardView::Front, StandardView::Iso) ==
          Alignment::None);
}

TEST(draft_layouts_are_two_three_and_four_views) {
    CHECK(layout_views(Layout::OneView).size() == 1);
    CHECK(layout_views(Layout::TwoView).size() == 2);
    CHECK(layout_views(Layout::ThreeView).size() == 3);
    CHECK(layout_views(Layout::FourView).size() == 4);
    CHECK(layout_views(Layout::FourView)[0] == StandardView::Front);
    CHECK(layout_views(Layout::FourView)[3] == StandardView::Iso);
}

TEST(draft_a_three_view_layout_is_arranged_by_the_convention) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);

    ViewSet third(ProjectionAngle::Third);
    const auto ids = third.add_layout(m, Layout::ThreeView, 0.5, 0.5, 100.0);
    CHECK(ids.size() == 3);
    const DrawingView* front = third.view(ids[0]);
    const DrawingView* top = third.view(ids[1]);
    const DrawingView* right = third.view(ids[2]);
    CHECK(front && top && right);

    CHECK_MSG(top->y > front->y, "third-angle top view is not above the front");
    CHECK_MSG(std::fabs(top->x - front->x) < 1e-12,
              "the top view is not on the front view's vertical axis");
    CHECK_MSG(right->x > front->x, "third-angle right view is not to the right");
    CHECK_MSG(std::fabs(right->y - front->y) < 1e-12,
              "the right view is not on the front view's horizontal axis");
}

TEST(draft_first_angle_lays_the_same_layout_out_the_other_way) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet first(ProjectionAngle::First);
    const auto ids = first.add_layout(m, Layout::ThreeView, 0.5, 0.5, 100.0);
    CHECK(first.view(ids[1])->y < first.view(ids[0])->y);
    CHECK(first.view(ids[2])->x < first.view(ids[0])->x);
}

TEST(draft_changing_the_convention_relays_the_sheet) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s(ProjectionAngle::Third);
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.5, 0.5, 100.0);
    CHECK(s.view(ids[1])->y > s.view(ids[0])->y);

    s.set_angle(ProjectionAngle::First);
    CHECK(s.angle() == ProjectionAngle::First);
    CHECK_MSG(s.view(ids[1])->y < s.view(ids[0])->y,
              "the views did not follow the symbol");
    CHECK(s.view(ids[2])->x < s.view(ids[0])->x);
}

TEST(draft_views_do_not_overlap_however_big_the_part_is) {
    const auto v = vol();
    for (f64 size : {0.5, 5.0, 50.0, 400.0}) {
        const auto m = model(v, size, size, size);
        ViewSet s(ProjectionAngle::Third);
        const auto ids = s.add_layout(m, Layout::ThreeView, 0.0, 0.0, 100.0,
                                      0.020);
        const DrawingView* f = s.view(ids[0]);
        const DrawingView* t = s.view(ids[1]);
        const f64 clear = (t->y - f->y) -
                          (f->content.height + t->content.height) * 0.5;
        CHECK_MSG(clear > 0.019, "a %.1f m part left %.4f m between views",
                  size, clear);
    }
}

TEST(draft_a_four_view_layout_puts_the_pictorial_in_the_free_corner) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s(ProjectionAngle::Third);
    const auto ids = s.add_layout(m, Layout::FourView, 0.5, 0.5, 100.0);
    CHECK(ids.size() == 4);
    const DrawingView* iso = s.view(ids[3]);
    CHECK(iso->kind == ViewKind::Isometric);
    CHECK(iso->alignment == Alignment::None);
    CHECK_MSG(iso->x > s.view(ids[0])->x && iso->y > s.view(ids[0])->y,
              "the pictorial is not in the free corner");
}

TEST(draft_projected_views_inherit_the_parents_scale) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    const auto ids = s.add_layout(m, Layout::FourView, 0.5, 0.5, 25.0);
    for (u32 i : ids) {
        CHECK_MSG(std::fabs(s.view(i)->scale - 25.0) < 1e-12,
                  "%s plots at 1:%g", s.view(i)->label.c_str(),
                  s.view(i)->scale);
    }
}

TEST(draft_a_view_knows_when_its_model_has_moved_on) {
    const auto v = vol();
    SourceModel m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.5, 0.5, 100.0);
    CHECK(s.stale(m).empty());

    m.body = box(v, 6.0, 7.0, 5.0);
    m.touch();
    CHECK_MSG(s.stale(m).size() == ids.size(),
              "%zu of %zu views noticed", s.stale(m).size(), ids.size());
    for (u32 i : ids) CHECK(s.is_stale(i, m));
}

TEST(draft_regenerating_makes_the_views_match_the_model_again) {
    const auto v = vol();
    SourceModel m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.5, 0.5, 1.0);
    CHECK(std::fabs(s.view(ids[0])->content.width - 3.0) < 1e-3);

    m.body = box(v, 6.0, 7.0, 5.0);
    m.touch();
    CHECK_MSG(s.regenerate(m) == ids.size(), "not every view regenerated");
    CHECK(s.stale(m).empty());
    CHECK_MSG(std::fabs(s.view(ids[0])->content.width - 6.0) < 1e-3,
              "the front view is still %.4f wide",
              s.view(ids[0])->content.width);
    CHECK(std::fabs(s.view(ids[1])->content.width - 6.0) < 1e-3);
    CHECK(std::fabs(s.view(ids[2])->content.width - 7.0) < 1e-3);
}

TEST(draft_regenerating_moves_the_views_apart_again) {
    const auto v = vol();
    SourceModel m = model(v, 1.0, 1.0, 1.0);
    ViewSet s;
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.0, 0.0, 1.0, 0.020);

    m.body = box(v, 10.0, 10.0, 10.0);
    m.touch();
    s.regenerate(m);

    const DrawingView* f = s.view(ids[0]);
    const DrawingView* t = s.view(ids[1]);
    const f64 clear = (t->y - f->y) -
                      (f->content.height + t->content.height) * 0.5;
    CHECK_MSG(clear > 0.019, "the grown views overlap by %.4f m", -clear);
}

TEST(draft_views_of_another_model_are_left_alone) {
    const auto v = vol();
    SourceModel a = model(v, 3.0, 7.0, 5.0);
    SourceModel b = model(v, 2.0, 2.0, 2.0);
    b.id = 2;

    ViewSet s;
    const u32 ia = s.add_base(a, StandardView::Front, 0.1, 0.1, 1.0);
    const u32 ib = s.add_base(b, StandardView::Front, 0.5, 0.1, 1.0);

    a.touch();
    const auto st = s.stale(a);
    CHECK_MSG(st.size() == 1 && st[0] == ia,
              "editing one model marked another model's views stale");
    CHECK(!s.is_stale(ib, a));
    CHECK(s.regenerate(a) == 1);
    CHECK(std::fabs(s.view(ib)->content.width - 2.0) < 1e-3);
}

TEST(draft_a_projected_view_slides_only_along_its_axis) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.5, 0.5, 100.0);

    const f64 top_x = s.view(ids[1])->x;
    CHECK(s.move(ids[1], top_x + 0.3, s.view(ids[1])->y + 0.1));
    CHECK_MSG(std::fabs(s.view(ids[1])->x - top_x) < 1e-12,
              "the top view left its vertical axis");
    CHECK(s.view(ids[1])->y > 0.5);

    const f64 right_y = s.view(ids[2])->y;
    s.move(ids[2], s.view(ids[2])->x + 0.2, right_y + 0.4);
    CHECK_MSG(std::fabs(s.view(ids[2])->y - right_y) < 1e-12,
              "the right view left its horizontal axis");
}

TEST(draft_moving_the_base_view_takes_its_children_with_it) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.5, 0.5, 100.0);
    const f64 dx = s.view(ids[1])->x - s.view(ids[0])->x;
    const f64 dy = s.view(ids[1])->y - s.view(ids[0])->y;

    CHECK(s.move(ids[0], 0.9, 0.2));
    CHECK(std::fabs(s.view(ids[0])->x - 0.9) < 1e-12);
    CHECK_MSG(std::fabs((s.view(ids[1])->x - s.view(ids[0])->x) - dx) < 1e-12,
              "moving the front view scattered the set");
    CHECK(std::fabs((s.view(ids[1])->y - s.view(ids[0])->y) - dy) < 1e-12);
}

TEST(draft_alignment_can_be_broken_deliberately) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.5, 0.5, 100.0);

    CHECK(s.unalign(ids[1]));
    CHECK(!s.unalign(ids[1]));
    CHECK(!s.unalign(999));
    CHECK(s.move(ids[1], 0.9, 0.9));
    CHECK(std::fabs(s.view(ids[1])->x - 0.9) < 1e-12);

    SourceModel m2 = m;
    m2.touch();
    s.regenerate(m2);
    CHECK_MSG(std::fabs(s.view(ids[1])->x - 0.9) < 1e-12,
              "regeneration snapped a deliberately freed view back");
}

TEST(draft_moving_a_view_that_does_not_exist_is_refused) {
    ViewSet s;
    CHECK(!s.move(0, 1.0, 1.0));
    CHECK(s.view(0) == nullptr);
    CHECK(s.size() == 0);
}

TEST(draft_projecting_from_a_view_that_does_not_exist_is_refused) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    CHECK(s.project(m, 42, StandardView::Top) == kNoView);
    CHECK(s.size() == 0);
}

TEST(draft_names_exist_for_every_kind_angle_and_layout) {
    for (ViewKind k : {ViewKind::Base, ViewKind::Projected, ViewKind::Isometric,
                       ViewKind::Section, ViewKind::Detail})
        CHECK(view_kind_name(k)[0] != '\0');
    for (Layout l : {Layout::OneView, Layout::TwoView, Layout::ThreeView,
                     Layout::FourView})
        CHECK(layout_name(l)[0] != '\0');
    CHECK(projection_angle_name(ProjectionAngle::First)[0] != '\0');
    CHECK(projection_angle_name(ProjectionAngle::Third)[0] != '\0');
}

TEST(draft_a_view_set_becomes_sheet_geometry_in_paper_coordinates) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    gcm::WorkingVolume paper;
    paper.extent = 4.0;
    paper.resolution_bits = 32;

    ViewSet s;
    const auto ids = s.add_layout(m, Layout::ThreeView, 0.5, 0.4, 100.0);

    std::vector<gsnap::Shape> shapes;
    std::vector<std::string> layers;
    to_sheet_shapes(s, paper, shapes, layers);

    CHECK_MSG(!shapes.empty(), "nothing reached the sheet");
    CHECK_MSG(shapes.size() == layers.size(),
              "%zu shapes but %zu layers -- one of them is unlabelled",
              shapes.size(), layers.size());

    for (const gsnap::Shape& sh : shapes) {
        f64 x, y;
        gcm::to_world(paper, sh.a, x, y);
        CHECK_MSG(x > 0.0 && x < 2.0 && y > 0.0 && y < 2.0,
                  "a line landed at %.3f,%.3f -- off the sheet", x, y);
    }

    const DrawingView* front = s.view(ids[0]);
    bool near_front = false;
    for (const gsnap::Shape& sh : shapes) {
        f64 x, y;
        gcm::to_world(paper, sh.a, x, y);
        if (std::fabs(x - front->x) < 0.05 && std::fabs(y - front->y) < 0.05)
            near_front = true;
    }
    CHECK(near_front);
}

TEST(draft_each_class_of_line_gets_its_own_layer) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    gcm::WorkingVolume paper;
    paper.extent = 4.0;
    paper.resolution_bits = 32;

    ViewSet s;
    s.add_base(m, StandardView::Front, 0.5, 0.4, 100.0);

    std::vector<gsnap::Shape> shapes;
    std::vector<std::string> layers;
    SheetLayers names;
    to_sheet_shapes(s, paper, shapes, layers, names);

    std::size_t vis = 0, hid = 0, sil = 0;
    for (const std::string& l : layers) {
        if (l == names.visible) ++vis;
        else if (l == names.hidden) ++hid;
        else if (l == names.silhouette) ++sil;
        else CHECK_MSG(false, "unexpected layer '%s'", l.c_str());
    }
    CHECK_MSG(sil == 4, "%zu outline lines", sil);
    CHECK_MSG(hid == 4, "%zu hidden lines", hid);
    CHECK_MSG(vis == 0, "%zu lines drawn twice", vis);
    CHECK(shapes.size() == 8);
}

TEST(draft_hidden_lines_switched_off_do_not_reach_the_sheet) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    gcm::WorkingVolume paper;
    paper.extent = 4.0;
    paper.resolution_bits = 32;

    ViewSet s;
    const u32 i = s.add_base(m, StandardView::Front, 0.5, 0.4, 100.0);
    s.view(i)->show_hidden = false;
    s.regenerate(m);

    std::vector<gsnap::Shape> shapes;
    std::vector<std::string> layers;
    to_sheet_shapes(s, paper, shapes, layers);
    for (const std::string& l : layers)
        CHECK_MSG(l != SheetLayers{}.hidden, "a hidden line was plotted anyway");
    CHECK(shapes.size() == 4);
}

TEST(draft_the_sheet_follows_the_model_through_a_regenerate) {
    const auto v = vol();
    SourceModel m = model(v, 3.0, 7.0, 5.0);
    gcm::WorkingVolume paper;
    paper.extent = 4.0;
    paper.resolution_bits = 32;

    ViewSet s;
    s.add_base(m, StandardView::Front, 0.5, 0.4, 10.0);

    auto width_on_paper = [&] {
        std::vector<gsnap::Shape> shapes;
        std::vector<std::string> layers;
        to_sheet_shapes(s, paper, shapes, layers);
        f64 lo = 1e30, hi = -1e30;
        for (const gsnap::Shape& sh : shapes) {
            f64 x, y;
            gcm::to_world(paper, sh.a, x, y);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
            gcm::to_world(paper, sh.b, x, y);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        return hi - lo;
    };

    // 3 m at 1:10 is 300 mm of paper.
    CHECK_MSG(std::fabs(width_on_paper() - 0.300) < 1e-3,
              "plotted %.4f m", width_on_paper());

    m.body = box(v, 6.0, 7.0, 5.0);
    m.touch();
    s.regenerate(m);
    CHECK_MSG(std::fabs(width_on_paper() - 0.600) < 1e-3,
              "after the edit the sheet still shows %.4f m", width_on_paper());
}

static gtopo::Body tube(const gcm::WorkingVolume& v, f64 outer, f64 inner,
                        f64 depth) {
    gtopo::Builder b;
    const f64 ho = outer * 0.5, hi = inner * 0.5, hd = depth * 0.5;
    std::vector<gtopo::VertexId> front_o, back_o, front_i, back_i;
    const f64 xs[4] = {-ho, ho, ho, -ho};
    const f64 zs[4] = {-ho, -ho, ho, ho};
    const f64 xi[4] = {-hi, hi, hi, -hi};
    const f64 zi[4] = {-hi, -hi, hi, hi};
    for (int i = 0; i < 4; ++i) {
        front_o.push_back(b.add_vertex(gcm::to_lattice(v, xs[i], -hd, zs[i])));
        back_o.push_back(b.add_vertex(gcm::to_lattice(v, xs[i], hd, zs[i])));
        front_i.push_back(b.add_vertex(gcm::to_lattice(v, xi[i], -hd, zi[i])));
        back_i.push_back(b.add_vertex(gcm::to_lattice(v, xi[i], hd, zi[i])));
    }
    b.begin_face();
    b.add_loop({front_o[0], front_o[1], front_o[2], front_o[3]});
    b.add_loop({front_i[3], front_i[2], front_i[1], front_i[0]});
    b.end_face();
    b.begin_face();
    b.add_loop({back_o[3], back_o[2], back_o[1], back_o[0]});
    b.add_loop({back_i[0], back_i[1], back_i[2], back_i[3]});
    b.end_face();
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        b.begin_face();
        b.add_loop({front_o[i], front_o[j], back_o[j], back_o[i]});
        b.end_face();
        b.begin_face();
        b.add_loop({front_i[j], front_i[i], back_i[i], back_i[j]});
        b.end_face();
    }
    return b.build();
}

TEST(draft_a_section_removes_what_is_in_front_of_the_plane) {
    const auto v = vol();
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = tube(v, 6.0, 2.0, 4.0);

    ViewSet s(ProjectionAngle::Third);
    const u32 front = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    SectionLine line;
    line.x0 = -4.0; line.y0 = 0.0;
    line.x1 =  4.0; line.y1 = 0.0;
    line.look_x = 0.0; line.look_y = -1.0;   // looking down the page
    line.label = "A";

    const u32 sec = s.add_section(m, front, line);
    CHECK_MSG(sec != kNoView, "the section was refused");
    const DrawingView* sv = s.view(sec);
    CHECK(sv->kind == ViewKind::Section);
    CHECK_MSG(sv->label == "SECTION A-A", "labelled '%s'", sv->label.c_str());
    CHECK(!sv->content.visible.empty());
    CHECK(s.section_line(sec) != nullptr);
    CHECK(s.section_line(sec)->label == "A");
}

TEST(draft_a_section_hatches_the_material_the_plane_passed_through) {
    const auto v = vol();
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = tube(v, 6.0, 2.0, 4.0);

    ViewSet s;
    const u32 front = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    SectionLine line;
    line.x0 = -4.0; line.y0 = 0.0;
    line.x1 =  4.0; line.y1 = 0.0;
    line.look_x = 0.0; line.look_y = -1.0;
    const u32 sec = s.add_section(m, front, line);

    const std::vector<CutFace>& cut = s.cut_faces(sec);
    CHECK_MSG(!cut.empty(), "the cut produced no material to hatch");
    CHECK_MSG(cut.size() == 2, "%zu cut regions", cut.size());
    for (const CutFace& f : cut)
        CHECK_MSG(f.loop.size() >= 3, "a cut region has only %zu points",
                  f.loop.size());
}

TEST(draft_a_cut_that_misses_the_solid_hatches_nothing) {
    const auto v = vol();
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = box(v, 3.0, 7.0, 5.0);

    ViewSet s;
    const u32 front = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    SectionLine line;
    line.x0 = -10.0; line.y0 = 40.0;
    line.x1 =  10.0; line.y1 = 40.0;
    line.look_x = 0.0; line.look_y = -1.0;
    const u32 sec = s.add_section(m, front, line);
    CHECK(sec != kNoView);
    CHECK_MSG(s.cut_faces(sec).empty(),
              "a cut that missed the part still hatched something");
}

TEST(draft_a_section_is_placed_by_the_projection_convention) {
    const auto v = vol();
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = box(v, 4.0, 4.0, 4.0);

    SectionLine line;
    line.x0 = -3.0; line.y0 = 0.0;
    line.x1 =  3.0; line.y1 = 0.0;
    line.look_x = 0.0; line.look_y = -1.0;

    ViewSet third(ProjectionAngle::Third);
    const u32 fa = third.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    const u32 sa = third.add_section(m, fa, line);
    CHECK_MSG(third.view(sa)->y > third.view(fa)->y,
              "third-angle section went the wrong way");

    ViewSet first(ProjectionAngle::First);
    const u32 fb = first.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    const u32 sb = first.add_section(m, fb, line);
    CHECK_MSG(first.view(sb)->y < first.view(fb)->y,
              "first-angle section went the wrong way");
}

TEST(draft_a_section_is_aligned_to_its_cut_line) {
    const auto v = vol();
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = box(v, 4.0, 4.0, 4.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    SectionLine horizontal;
    horizontal.x0 = -3.0; horizontal.x1 = 3.0;
    horizontal.look_x = 0.0; horizontal.look_y = -1.0;
    CHECK(s.view(s.add_section(m, f, horizontal))->alignment ==
          Alignment::Vertical);

    SectionLine vertical;
    vertical.x0 = 0.0; vertical.y0 = -3.0;
    vertical.x1 = 0.0; vertical.y1 =  3.0;
    vertical.look_x = 1.0; vertical.look_y = 0.0;
    CHECK(s.view(s.add_section(m, f, vertical))->alignment ==
          Alignment::Horizontal);
}

TEST(draft_a_section_with_no_look_direction_is_refused) {
    const auto v = vol();
    const auto m = model(v, 3.0, 7.0, 5.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    SectionLine bad;
    bad.look_x = 0.0;
    bad.look_y = 0.0;
    CHECK(s.add_section(m, f, bad) == kNoView);
    CHECK(s.add_section(m, 99, bad) == kNoView);
    CHECK(s.size() == 1);
}

TEST(draft_a_section_is_not_quietly_reprojected_into_an_ordinary_view) {
    const auto v = vol();
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = tube(v, 6.0, 2.0, 4.0);

    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    SectionLine line;
    line.x0 = -4.0; line.x1 = 4.0;
    line.look_x = 0.0; line.look_y = -1.0;
    const u32 sec = s.add_section(m, f, line);
    const std::size_t before = s.view(sec)->content.visible.size();

    m.touch();
    s.regenerate(m);
    CHECK(s.stale(m).empty());
    CHECK_MSG(s.view(sec)->content.visible.size() == before,
              "the section turned back into a plain view on regenerate");
    CHECK(s.view(sec)->kind == ViewKind::Section);
}

TEST(draft_a_detail_shows_only_what_is_inside_its_circle) {
    const auto v = vol();
    const auto m = model(v, 10.0, 10.0, 10.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    DetailCircle c;
    c.x = 5.0;      // the top-right corner of the front view
    c.y = 5.0;
    c.radius = 2.0;
    c.label = "A";

    const u32 d = s.add_detail(m, f, c, 0.25, 0.02);   // four times bigger
    CHECK_MSG(d != kNoView, "the detail was refused");
    const DrawingView* dv = s.view(d);
    CHECK(dv->kind == ViewKind::Detail);
    CHECK(dv->label == "DETAIL A");
    CHECK(dv->clipped);

    CHECK_MSG(dv->content.visible.size() < s.view(f)->content.visible.size(),
              "the detail showed as much as the whole view");
    CHECK_MSG(!dv->content.visible.empty(), "the detail showed nothing at all");

    for (const Edge2& e : dv->content.visible) {
        CHECK_MSG(std::hypot(e.x0, e.y0) <= dv->clip_radius + 1e-6,
                  "a line starts %.6f from the centre of a %.6f circle",
                  std::hypot(e.x0, e.y0), dv->clip_radius);
        CHECK(std::hypot(e.x1, e.y1) <= dv->clip_radius + 1e-6);
    }
}

TEST(draft_a_detail_keeps_its_own_scale) {
    const auto v = vol();
    const auto m = model(v, 10.0, 10.0, 10.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    DetailCircle c;
    c.x = 5.0; c.y = 5.0; c.radius = 2.0;
    const u32 d = s.add_detail(m, f, c, 0.25);
    CHECK(!s.view(d)->scale_from_parent);
    CHECK(std::fabs(s.view(d)->scale - 0.25) < 1e-12);
    CHECK(std::fabs(s.view(f)->scale - 1.0) < 1e-12);

    CHECK_MSG(std::fabs(s.view(d)->content.width - 2.0 * 2.0 * 4.0) < 1e-6,
              "the detail is %.4f across", s.view(d)->content.width);
    CHECK(s.detail_circle(d) != nullptr);
    CHECK(std::fabs(s.detail_circle(d)->radius - 2.0) < 1e-12);
}

TEST(draft_a_detail_survives_a_regenerate_still_clipped) {
    const auto v = vol();
    SourceModel m = model(v, 10.0, 10.0, 10.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    DetailCircle c;
    c.x = 5.0; c.y = 5.0; c.radius = 2.0;
    const u32 d = s.add_detail(m, f, c, 0.25);

    m.touch();
    s.regenerate(m);
    CHECK(s.view(d)->clipped);
    for (const Edge2& e : s.view(d)->content.visible)
        CHECK_MSG(std::hypot(e.x0, e.y0) <= s.view(d)->clip_radius + 1e-6,
                  "a regenerated detail leaked outside its circle");
}

TEST(draft_a_detail_with_no_radius_or_no_scale_is_refused) {
    const auto v = vol();
    const auto m = model(v, 10.0, 10.0, 10.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    DetailCircle zero;
    zero.radius = 0.0;
    CHECK(s.add_detail(m, f, zero, 0.25) == kNoView);

    DetailCircle ok;
    ok.radius = 1.0;
    CHECK(s.add_detail(m, f, ok, 0.0) == kNoView);
    CHECK(s.add_detail(m, f, ok, -1.0) == kNoView);
    CHECK(s.add_detail(m, 99, ok, 0.25) == kNoView);
    CHECK(s.size() == 1);
}

TEST(draft_a_detail_that_misses_the_part_shows_nothing_rather_than_everything) {
    const auto v = vol();
    const auto m = model(v, 4.0, 4.0, 4.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    DetailCircle c;
    c.x = 50.0; c.y = 50.0; c.radius = 1.0;
    const u32 d = s.add_detail(m, f, c, 0.5);
    CHECK(d != kNoView);
    CHECK_MSG(s.view(d)->content.visible.empty(),
              "a detail off the part showed %zu lines",
              s.view(d)->content.visible.size());
}

TEST(draft_a_section_reaches_the_sheet_with_its_cut_and_its_mark) {
    const auto v = vol();
    SourceModel m;
    m.id = 1;
    m.volume = v;
    m.body = tube(v, 6.0, 2.0, 4.0);
    gcm::WorkingVolume paper;
    paper.extent = 4.0;
    paper.resolution_bits = 32;

    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 100.0);
    SectionLine line;
    line.x0 = -0.04; line.x1 = 0.04;
    line.look_x = 0.0; line.look_y = -1.0;
    line.label = "A";
    const u32 sec = s.add_section(m, f, line);
    CHECK(sec != kNoView);

    std::vector<gsnap::Shape> shapes;
    std::vector<std::string> layers;
    SheetLayers names;
    to_sheet_shapes(s, paper, shapes, layers, names);

    std::size_t cut = 0, marks = 0;
    for (const std::string& l : layers) {
        if (l == names.cut) ++cut;
        else if (l == names.annotation) ++marks;
    }
    CHECK_MSG(cut > 0, "the cut material never reached the sheet");
    CHECK_MSG(marks == 1,
              "%zu cut lines on the parent -- a section nobody can locate",
              marks);
}

TEST(draft_a_detail_circle_is_drawn_on_its_parent) {
    const auto v = vol();
    const auto m = model(v, 10.0, 10.0, 10.0);
    gcm::WorkingVolume paper;
    paper.extent = 4.0;
    paper.resolution_bits = 32;

    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.4, 0.4, 100.0);
    DetailCircle c;
    c.x = 0.05; c.y = 0.05; c.radius = 0.02;
    const u32 d = s.add_detail(m, f, c, 25.0);
    CHECK(d != kNoView);

    std::vector<gsnap::Shape> shapes;
    std::vector<std::string> layers;
    SheetLayers names;
    to_sheet_shapes(s, paper, shapes, layers, names);

    bool circle_on_parent = false;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (layers[i] != names.annotation) continue;
        if (shapes[i].kind != gsnap::ShapeKind::Circle) continue;
        f64 x, y;
        gcm::to_world(paper, shapes[i].a, x, y);
        if (std::fabs(x - (s.view(f)->x + c.x)) < 1e-4 &&
            std::fabs(y - (s.view(f)->y + c.y)) < 1e-4)
            circle_on_parent = true;
    }
    CHECK_MSG(circle_on_parent, "the detail circle is not on the parent view");
}

TEST(draft_a_break_shortens_the_view_by_what_it_removed) {
    const auto v = vol();
    const auto m = model(v, 40.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    const f64 before = s.view(f)->content.width;
    CHECK(std::fabs(before - 40.0) < 1e-3);

    ViewBreak b;
    b.from = -10.0;
    b.to = 10.0;
    b.horizontal = true;
    b.gap = 1.0;
    CHECK_MSG(s.add_break(f, b), "the break was refused");

    const f64 after = s.view(f)->content.width;
    // 20 removed, 1 of gap left: 40 - 19 = 21.
    CHECK_MSG(std::fabs(after - 21.0) < 1e-3, "the view is %.4f wide", after);
    CHECK(std::fabs(s.view(f)->removed_length() - 20.0) < 1e-9);
}

TEST(draft_a_break_wider_than_what_it_removes_is_refused) {
    const auto v = vol();
    const auto m = model(v, 40.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewBreak b;
    b.from = 0.0;
    b.to = 1.0;
    b.gap = 2.0;
    CHECK(!s.add_break(f, b));

    ViewBreak zero;
    zero.from = 5.0;
    zero.to = 5.0;
    CHECK(!s.add_break(f, zero));
    CHECK(!s.add_break(99, b));
    CHECK(s.view(f)->breaks.empty());
}

TEST(draft_a_break_cuts_the_lines_that_cross_it) {
    const auto v = vol();
    const auto m = model(v, 40.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewBreak b;
    b.from = -10.0;
    b.to = 10.0;
    b.gap = 1.0;
    s.add_break(f, b);

    for (const Edge2& e : s.view(f)->content.visible) {
        const bool inside = std::fabs(e.x0) < 0.49 && std::fabs(e.x1) < 0.49;
        CHECK_MSG(!inside, "an edge survived inside the break gap");
    }
    CHECK_MSG(!s.view(f)->content.visible.empty(),
              "the break removed the whole view");
}

TEST(draft_a_vertical_break_shortens_the_height_not_the_width) {
    const auto v = vol();
    const auto m = model(v, 2.0, 2.0, 40.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    const f64 w_before = s.view(f)->content.width;

    ViewBreak b;
    b.from = -10.0;
    b.to = 10.0;
    b.horizontal = false;
    b.gap = 1.0;
    CHECK(s.add_break(f, b));

    CHECK_MSG(std::fabs(s.view(f)->content.height - 21.0) < 1e-3,
              "the column is %.4f tall", s.view(f)->content.height);
    CHECK_MSG(std::fabs(s.view(f)->content.width - w_before) < 1e-9,
              "a vertical break changed the width");
}

TEST(draft_the_break_symbol_is_two_lines_on_the_gap_edges) {
    const auto v = vol();
    const auto m = model(v, 40.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewBreak b;
    b.from = -10.0;
    b.to = 10.0;
    b.gap = 1.0;
    s.add_break(f, b);

    for (BreakStyle style : {BreakStyle::Straight, BreakStyle::Zigzag,
                             BreakStyle::Jagged}) {
        ViewBreak styled = b;
        styled.style = style;
        const auto sym = break_symbol(styled, *s.view(f));
        CHECK_MSG(sym.size() == 2, "%s gave %zu lines", break_style_name(style),
                  sym.size());
        for (const auto& line : sym) {
            CHECK(line.size() >= 2);
            f64 mean = 0.0;
            for (const auto& p : line) mean += p.first;
            mean /= static_cast<f64>(line.size());
            CHECK_MSG(std::fabs(std::fabs(mean - (-10.0)) - 0.5) < 0.2,
                      "%s line centred at %.4f", break_style_name(style), mean);
        }
        CHECK(sym[0][0].first < sym[1][0].first);
    }
    CHECK(break_symbol(ViewBreak{}, *s.view(f)).empty());
}

TEST(draft_the_jagged_symbol_is_deterministic) {
    const auto v = vol();
    const auto m = model(v, 40.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewBreak b;
    b.from = -5.0;
    b.to = 5.0;
    b.gap = 1.0;
    b.style = BreakStyle::Jagged;

    const auto a = break_symbol(b, *s.view(f));
    const auto c = break_symbol(b, *s.view(f));
    CHECK(a.size() == c.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].size() == c[i].size());
        for (std::size_t k = 0; k < a[i].size(); ++k) {
            CHECK(a[i][k].first == c[i][k].first);
            CHECK(a[i][k].second == c[i][k].second);
        }
    }
}

TEST(draft_a_break_survives_a_regenerate) {
    const auto v = vol();
    SourceModel m = model(v, 40.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewBreak b;
    b.from = -10.0;
    b.to = 10.0;
    b.gap = 1.0;
    s.add_break(f, b);
    const f64 broken = s.view(f)->content.width;

    m.touch();
    s.regenerate(m);
    CHECK_MSG(s.view(f)->breaks.size() == 1, "the break was lost");
    CHECK_MSG(std::fabs(s.view(f)->content.width - broken) < 1e-6,
              "after regenerating the view is %.4f, not %.4f",
              s.view(f)->content.width, broken);

    CHECK(s.clear_breaks(f));
    CHECK(!s.clear_breaks(f));
    s.regenerate(m);
    CHECK_MSG(std::fabs(s.view(f)->content.width - 40.0) < 1e-3,
              "clearing the break did not restore the length");
}

TEST(draft_a_crop_hides_without_moving_anything) {
    const auto v = vol();
    const auto m = model(v, 20.0, 2.0, 10.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewCrop c;
    c.enabled = true;
    c.lo_x = -3.0; c.hi_x = 3.0;
    c.lo_y = -6.0; c.hi_y = 6.0;
    CHECK(s.set_crop(f, c));

    CHECK_MSG(std::fabs(s.view(f)->content.width - 6.0) < 1e-9,
              "the crop reported a width of %.4f", s.view(f)->content.width);
    for (const Edge2& e : s.view(f)->content.visible) {
        CHECK_MSG(e.x0 >= c.lo_x - 1e-6 && e.x0 <= c.hi_x + 1e-6,
                  "an edge starts at x=%.4f, outside the crop", e.x0);
        CHECK(e.y0 >= c.lo_y - 1e-6 && e.y0 <= c.hi_y + 1e-6);
        CHECK(e.x1 >= c.lo_x - 1e-6 && e.x1 <= c.hi_x + 1e-6);
    }
    CHECK_MSG(!s.view(f)->content.visible.empty(),
              "the crop removed everything");
    CHECK_MSG(s.view(f)->removed_length() == 0.0,
              "a crop claimed to have removed length");
}

TEST(draft_a_crop_trims_an_edge_rather_than_dropping_it) {
    const auto v = vol();
    const auto m = model(v, 20.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);
    const std::size_t before = s.view(f)->content.visible.size();

    ViewCrop c;
    c.enabled = true;
    c.lo_x = -2.0; c.hi_x = 2.0;
    c.lo_y = -5.0; c.hi_y = 5.0;
    s.set_crop(f, c);

    CHECK_MSG(!s.view(f)->content.visible.empty(), "everything was dropped");
    CHECK(s.view(f)->content.visible.size() <= before);
    bool trimmed = false;
    for (const Edge2& e : s.view(f)->content.visible)
        if (std::fabs(std::fabs(e.x0) - 2.0) < 1e-6 ||
            std::fabs(std::fabs(e.x1) - 2.0) < 1e-6)
            trimmed = true;
    CHECK_MSG(trimmed, "no edge was cut at the crop boundary");
}

TEST(draft_an_invalid_crop_is_refused) {
    const auto v = vol();
    const auto m = model(v, 20.0, 2.0, 2.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewCrop backwards;
    backwards.enabled = true;
    backwards.lo_x = 5.0; backwards.hi_x = -5.0;
    backwards.lo_y = -1.0; backwards.hi_y = 1.0;
    CHECK(!s.set_crop(f, backwards));
    CHECK(!s.set_crop(99, backwards));

    ViewCrop off;
    CHECK(s.set_crop(f, off));
    CHECK(!s.view(f)->crop.enabled);
}

TEST(draft_a_crop_survives_a_regenerate) {
    const auto v = vol();
    SourceModel m = model(v, 20.0, 2.0, 10.0);
    ViewSet s;
    const u32 f = s.add_base(m, StandardView::Front, 0.5, 0.5, 1.0);

    ViewCrop c;
    c.enabled = true;
    c.lo_x = -3.0; c.hi_x = 3.0;
    c.lo_y = -3.0; c.hi_y = 3.0;
    s.set_crop(f, c);

    m.touch();
    s.regenerate(m);
    CHECK(s.view(f)->crop.enabled);
    CHECK_MSG(std::fabs(s.view(f)->content.width - 6.0) < 1e-9,
              "the crop was lost on regenerate");
}
