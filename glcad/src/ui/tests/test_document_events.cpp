#include "test.hpp"

#include <cstdlib>

#include "ui/shell.hpp"
#include "ui/doc_entities.hpp"

using namespace gcapp;

namespace {
Shell doc_shell() {
    Shell s;
    s.set_dpr(1.0);
    s.set_viewport(1200, 800);
    return s;
}
void draw_rect(Shell& s, const char* a, const char* b, const char* c, const char* d) {
    s.canvas().set_tool(Tool::Line);
    for (const char* p : {a, b, c, d, a}) s.canvas().type_point(p);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
}

void extrude_at(Shell& s, double u, double v, const char* distance) {
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    double wx, wy, wz, px, py;
    s.sketch_plane().to_world(u, v, wx, wy, wz);
    if (!s.camera().project(gview::Point3::from(wx, wy, wz), px, py)) return;
    s.build_frame();
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (const char* p = distance; *p; ++p) s.key_char(std::string(1, *p));
    s.key_special(0);
    s.build_frame();
}

gdoc::Entity a_line(gdoc::SpaceId space) {
    gdoc::Entity e;
    e.kind = gdoc::Kind::Line;
    e.space = space;
    e.payload.points = {{0, 0, 0}, {1000, 0, 0}};
    return e;
}
}

TEST(document_change_moves_the_shells_revision) {
    Shell s = doc_shell();
    auto& doc = s.document();
    const auto start = s.document_revision();

    doc.begin("draw a line");
    const auto id = doc.create(a_line(doc.model_space()));
    CHECK(id.valid());
    CHECK_MSG(s.document_revision() == start,
              "an OPEN transaction already moved the revision");
    CHECK(doc.commit());
    CHECK_MSG(s.document_revision() != start, "commit did not reach the shell");
    CHECK_MSG(s.last_change_label() == "draw a line",
              "label was \"%s\"", s.last_change_label().c_str());

    const auto mark = s.document_revision();
    CHECK(!s.document_changed_since(mark));
    doc.begin("second");
    doc.create(a_line(doc.model_space()));
    doc.commit();
    CHECK(s.document_changed_since(mark));
}

TEST(document_aborted_change_is_not_announced) {
    Shell s = doc_shell();
    auto& doc = s.document();
    doc.begin("abandoned");
    doc.create(a_line(doc.model_space()));
    const auto before = s.document_revision();
    doc.abort();
    CHECK_MSG(s.document_revision() == before, "an aborted edit was announced");
}

TEST(document_undo_is_itself_a_change_worth_redrawing) {
    Shell s = doc_shell();
    auto& doc = s.document();
    doc.begin("draw");
    doc.create(a_line(doc.model_space()));
    doc.commit();
    const auto after_draw = s.document_revision();
    const auto depth = doc.undo_depth();

    CHECK(doc.can_undo());
    CHECK(doc.undo());
    CHECK_MSG(s.document_revision() != after_draw, "undo did not announce a redraw");
    CHECK_MSG(doc.undo_depth() < depth, "undo recorded itself as a new edit");
}

TEST(document_holds_planes_sketches_and_bodies_in_one_store) {
    Shell s = doc_shell();
    auto& doc = s.document();

    gdoc::Space plane;
    plane.kind = gdoc::SpaceKind::Sketch;
    plane.name = "Front";
    const auto plane_id = doc.add_space(plane);
    CHECK(doc.space(plane_id) != nullptr);
    CHECK(doc.space(plane_id)->is_planar());
    CHECK(doc.space(plane_id)->embedded_in_world());

    doc.begin("sketch and body");
    gdoc::Entity sketch;
    sketch.kind = gdoc::Kind::Sketch;
    sketch.space = plane_id;
    const auto sketch_id = doc.create(sketch);

    gdoc::Entity body;
    body.kind = gdoc::Kind::Body;
    body.space = doc.model_space();
    body.payload.refs = {sketch_id};
    const auto body_id = doc.create(body);
    CHECK(doc.commit());

    const auto dependents = doc.dependents_of(sketch_id);
    CHECK_MSG(dependents.size() == 1, "%zu dependents of the sketch", dependents.size());
    if (!dependents.empty())
        CHECK_MSG(dependents.front() == body_id, "the body is not the sketch's dependent");

    CHECK(doc.find(body_id)->kind == gdoc::Kind::Body);
    CHECK(doc.find(sketch_id)->space == plane_id);
}

namespace {
void extrude_region(Shell& s, double u, double v, const char* distance) {
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    double wx, wy, wz, px, py;
    s.sketch_plane().to_world(u, v, wx, wy, wz);
    if (!s.camera().project(gview::Point3::from(wx, wy, wz), px, py)) return;
    s.build_frame();
    s.pointer_move(px, py);
    s.pointer_down(px, py);
    s.pointer_up(px, py);
    for (const char* c = distance; *c; ++c) s.key_char(std::string(1, *c));
    s.key_special(0);
    s.build_frame();
}
void rect(Shell& s, const char* a, const char* b, const char* c, const char* d) {
    s.canvas().set_tool(Tool::Line);
    for (const char* p : {a, b, c, d, a}) s.canvas().type_point(p);
    s.canvas().cancel();
    s.canvas().set_tool(Tool::Select);
}
}

TEST(scene_carries_one_item_per_body_not_one_lump) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);

    rect(s, "0,0", "2,0", "2,2", "0,2");
    extrude_region(s, 1.0, 1.0, "1");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies after the first extrude",
              s.modeller().bodies().size());
    s.build_frame();

    CHECK_MSG(s.scene().size() >= 1, "%zu draw items for %zu bodies",
              s.scene().size(), s.modeller().bodies().size());
    for (const auto& item : s.scene().items()) {
        CHECK_MSG(item.count > 0, "a draw item covers no vertices");
        CHECK_MSG(!item.bounds.empty, "a draw item has no bounds, so it cannot be culled");
    }

    grender::Bounds far_away;
    far_away.add(500, 500, 500);
    far_away.add(501, 501, 501);
    CHECK_MSG(s.scene().visible_in(far_away).empty(),
              "culling kept a solid that is nowhere near the region");

    grender::Bounds around;
    around.add(-1, -1, -1);
    around.add(3, 3, 3);
    CHECK_MSG(!s.scene().visible_in(around).empty(), "culling dropped the solid it is over");

    const auto id = s.scene().items().front().id;
    const auto before = s.scene().find(id)->bounds;
    CHECK(const_cast<grender::Scene&>(s.scene())
              .set_transform(id, grender::Transform::translation(100, 0, 0)));
    CHECK_MSG(s.scene().find(id)->bounds.lo[0] == before.lo[0],
              "moving an object rewrote its geometry");
}

TEST(an_idle_frame_does_not_serialise_the_document) {
    if (std::getenv("GLCAD_VERIFY_METADATA")) return;
    Shell s = doc_shell();
    for (int i = 0; i < 4; ++i) s.build_frame();
    const auto quiet = s.metadata_encodes();
    for (int i = 0; i < 20; ++i) s.build_frame();
    CHECK_MSG(s.metadata_encodes() == quiet,
              "20 idle frames encoded the document %llu times",
              (unsigned long long)(s.metadata_encodes() - quiet));
}

TEST(an_edit_still_reaches_the_undo_journal_through_the_gate) {
    Shell s = doc_shell();
    for (int i = 0; i < 4; ++i) s.build_frame();
    const auto quiet = s.metadata_encodes();
    const auto depth = s.undo_depth();

    s.command("LTSCALE 2.5");
    s.build_frame();
    CHECK_MSG(s.metadata_encodes() > quiet, "a command did not re-encode");
    CHECK_MSG(s.undo_depth() > depth, "a settings change left no undo step");

    s.command("UNDO");
    s.build_frame();
    CHECK_MSG(s.undo_depth() == depth, "UNDO did not unwind the step");
}

TEST(the_document_mirrors_planes_sketches_and_the_solids_they_drive) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    CHECK_MSG(s.modeller().bodies().size() == 1, "%zu bodies", s.modeller().bodies().size());

    const gdoc::Document& doc = s.document();
    const auto sketches = doc.of_kind(gdoc::Kind::Sketch);
    const auto bodies   = doc.of_kind(gdoc::Kind::Body);
    CHECK_MSG(!sketches.empty(), "no sketch reached the document");
    CHECK_MSG(!bodies.empty(), "the extrude left no body in the document");
    if (sketches.empty() || bodies.empty()) return;

    for (const auto id : sketches) {
        const gdoc::Entity* e = doc.find(id);
        CHECK(e != nullptr);
        if (!e) continue;
        const gdoc::Space* plane = doc.space(e->space);
        CHECK_MSG(plane != nullptr, "a sketch with no plane");
        if (!plane) continue;
        CHECK_MSG(plane->kind == gdoc::SpaceKind::Sketch, "a sketch not on a sketch plane");
        CHECK_MSG(plane->is_planar(), "a sketch plane that is not planar");
    }

    const gdoc::Entity* body = doc.find(bodies.front());
    CHECK(body != nullptr);
    if (!body) return;
    CHECK_MSG(!body->payload.refs.empty(), "the body forgot the sketch it came from");
    if (body->payload.refs.empty()) return;
    const auto driven = doc.dependents_of(body->payload.refs.front());
    bool found = false;
    for (const auto id : driven) found = found || id == bodies.front();
    CHECK_MSG(found, "dependents_of(sketch) does not reach the body");
}

TEST(deleting_a_solid_takes_it_out_of_the_document_too) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    CHECK(s.document().of_kind(gdoc::Kind::Body).size() == 1);

    s.command("ERASE ALL");
    s.build_frame();
    CHECK_MSG(s.document().of_kind(gdoc::Kind::Body).size() ==
                  s.modeller().bodies().size(),
              "document has %zu bodies, the modeller has %zu",
              s.document().of_kind(gdoc::Kind::Body).size(),
              s.modeller().bodies().size());
}

TEST(black_line_work_does_not_disappear_into_a_dark_canvas) {
    Shell s = doc_shell();
    Canvas& c = s.canvas();

    const grender::Color dark{0.086f, 0.094f, 0.106f, 1.0f};
    const grender::Color light{0.97f, 0.97f, 0.97f, 1.0f};
    const grender::Color black{0.0f, 0.0f, 0.0f, 1.0f};
    const grender::Color red{0.8f, 0.1f, 0.1f, 1.0f};

    c.set_ground(dark);
    const auto ink = c.legible(black);
    CHECK_MSG(ink.r > 0.5f && ink.g > 0.5f && ink.b > 0.5f,
              "black on a dark ground stayed at %.2f,%.2f,%.2f", ink.r, ink.g, ink.b);

    c.set_ground(light);
    const auto on_paper = c.legible(black);
    CHECK_MSG(on_paper.r < 0.1f, "black on white was inverted to %.2f", on_paper.r);

    for (const auto ground : {dark, light}) {
        c.set_ground(ground);
        const auto kept = c.legible(red);
        CHECK_MSG(kept.r == red.r && kept.g == red.g && kept.b == red.b,
                  "red was rewritten to %.2f,%.2f,%.2f", kept.r, kept.g, kept.b);
    }

    c.set_ground(dark);
    const auto role = c.legible(grender::d2::kByRole);
    CHECK(role.a == grender::d2::kByRole.a);

    c.set_ground(light);
    const auto white_on_white = c.legible(grender::Color{1.0f, 1.0f, 1.0f, 1.0f});
    CHECK_MSG(white_on_white.r < 0.5f, "white on white stayed at %.2f", white_on_white.r);
}

TEST(hovering_inside_a_closed_shape_offers_the_extrude_arrow) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    s.build_frame();
    s.activate_tool(ToolId::Extrude);
    s.build_frame();

    double wx, wy, wz, px, py;
    s.sketch_plane().to_world(4.0, 2.5, wx, wy, wz);
    CHECK(s.camera().project(gview::Point3::from(wx, wy, wz), px, py));

    s.pointer_move(px, py);
    s.build_frame();
    s.build_frame();
    CHECK_MSG(s.modeller().hovered().valid,
              "nothing under the cursor in the middle of a closed rectangle");
    CHECK_MSG(!s.modeller().hovered().on_body, "hovered a solid, not a region");
}

TEST(the_scene_and_the_document_name_the_same_objects) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    draw_rect(s, "0,0", "8,0", "8,5", "0,5");
    extrude_at(s, 4.0, 2.5, "3");
    s.build_frame();

    const auto bodies = s.document().of_kind(gdoc::Kind::Body);
    CHECK_MSG(bodies.size() == 1, "%zu bodies in the document", bodies.size());
    if (bodies.empty()) return;

    CHECK_MSG(s.scene().size() >= 1, "the scene is empty");
    const grender::DrawItem* item = s.scene().find(bodies.front());
    CHECK_MSG(item != nullptr, "the scene does not know the document's body id");
    if (!item) return;
    CHECK_MSG(item->count > 0, "the body reached the scene with no vertices");
    CHECK_MSG(!item->bounds.empty, "the body reached the scene with no bounds");
}

TEST(the_document_holds_the_drawing_not_just_an_index_of_it) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 1.0, 1.0), "hello", 0.25);
    s.build_frame();

    const gdoc::Document& doc = s.document();
    const auto lines = doc.of_kind(gdoc::Kind::Line);
    CHECK_MSG(lines.size() == 4, "%zu lines reached the document", lines.size());
    if (lines.empty()) return;

    const gdoc::Entity* line = doc.find(lines.front());
    CHECK(line != nullptr);
    if (!line) return;
    const gdoc::Space* plane = doc.space(line->space);
    CHECK_MSG(plane != nullptr, "a line in no space");
    if (!plane) return;
    CHECK(plane->kind == gdoc::SpaceKind::Sketch);

    const auto on_plane = doc.in_space(plane->id);
    CHECK_MSG(on_plane.size() >= 4, "%zu entities on the plane", on_plane.size());
}

TEST(an_erased_line_leaves_the_document_too) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    draw_rect(s, "0,0", "4,0", "4,3", "0,3");
    s.build_frame();
    CHECK(s.document().of_kind(gdoc::Kind::Line).size() == 4);

    s.command("SELECTALL");
    s.command("ERASE");
    s.build_frame();
    CHECK_MSG(s.document().of_kind(gdoc::Kind::Line).size() ==
                  s.canvas().shapes().size(),
              "document has %zu lines, the canvas has %zu",
              s.document().of_kind(gdoc::Kind::Line).size(), s.canvas().shapes().size());
}

TEST(every_drawn_shape_survives_the_round_trip_through_the_document) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    Canvas& c = s.canvas();

    c.set_tool(Tool::Line);
    c.type_point("0,0"); c.type_point("4,0"); c.type_point("4,3"); c.cancel();
    c.set_tool(Tool::Circle);
    c.type_point("8,2"); c.type_point("9,2");
    c.set_tool(Tool::Arc);
    c.type_point("12,0"); c.type_point("13,1"); c.type_point("14,0");
    c.set_tool(Tool::Rect);
    c.type_point("0,6"); c.type_point("5,9");
    c.set_tool(Tool::Select);
    s.build_frame();

    const auto before = s.canvas().shapes();
    CHECK_MSG(before.size() >= 8, "only %zu shapes were drawn", before.size());

    const auto after = s.shapes_from_document(s.active_space_id());
    CHECK_MSG(after.size() == before.size(),
              "%zu shapes went in, %zu came back", before.size(), after.size());
    if (after.size() != before.size()) return;

    for (std::size_t i = 0; i < before.size(); ++i) {
        const auto& x = before[i];
        const auto& y = after[i];
        CHECK_MSG(x.key == y.key, "shape %zu: key %u -> %u", i, x.key, y.key);
        CHECK_MSG(x.kind == y.kind, "shape %zu (key %u): kind %d -> %d",
                  i, x.key, int(x.kind), int(y.kind));
        CHECK_MSG(x.a.x == y.a.x && x.a.y == y.a.y, "shape %zu: point a moved", i);
        CHECK_MSG(x.b.x == y.b.x && x.b.y == y.b.y, "shape %zu: point b moved", i);
        CHECK_MSG(x.radius == y.radius, "shape %zu: radius %lld -> %lld",
                  i, (long long)x.radius, (long long)y.radius);
        CHECK_MSG(x.start_angle == y.start_angle && x.end_angle == y.end_angle,
                  "shape %zu: angles moved", i);
        CHECK_MSG(x.layer == y.layer, "shape %zu: layer \"%s\" -> \"%s\"",
                  i, x.layer.c_str(), y.layer.c_str());
        CHECK_MSG(x.poly == y.poly && x.poly_index == y.poly_index,
                  "shape %zu: polyline membership %u/%u -> %u/%u",
                  i, x.poly, x.poly_index, y.poly, y.poly_index);
        CHECK_MSG(x.region == y.region && x.region_loop == y.region_loop &&
                      x.region_reversed == y.region_reversed,
                  "shape %zu: region data moved", i);
        CHECK_MSG(x.attrs.color == y.attrs.color, "shape %zu: colour moved", i);
        CHECK_MSG(x.attrs.linetype == y.attrs.linetype, "shape %zu: linetype moved", i);
        CHECK_MSG(x.attrs.lineweight == y.attrs.lineweight, "shape %zu: lineweight moved", i);
    }

    bool any_poly = false;
    for (const auto& sh : after) any_poly = any_poly || sh.in_polyline();
    CHECK_MSG(any_poly, "no polyline in the sample: the membership check is vacuous");
}

TEST(notes_survive_the_round_trip_through_the_document) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 1.0, 1.0), "hello", 0.25);
    s.canvas().add_text(gcm::to_lattice(s.canvas().volume(), 2.0, 2.0), "world", 0.5);
    s.build_frame();

    const auto before = s.canvas().texts();
    CHECK_MSG(before.size() == 2, "%zu notes", before.size());
    const auto after = s.notes_from_document(s.active_space_id());
    CHECK_MSG(after.size() == before.size(),
              "%zu notes went in, %zu came back", before.size(), after.size());
    if (after.size() != before.size()) return;
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK_MSG(before[i].text == after[i].text, "note %zu: \"%s\" -> \"%s\"",
                  i, before[i].text.c_str(), after[i].text.c_str());
        CHECK(before[i].pos.x == after[i].pos.x && before[i].pos.y == after[i].pos.y);
        CHECK_MSG(before[i].height == after[i].height, "note %zu: height moved", i);
        CHECK(before[i].angle == after[i].angle);
        CHECK(before[i].width == after[i].width);
    }
}

TEST(the_document_can_put_the_whole_drawing_back) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    Canvas& c = s.canvas();
    c.set_tool(Tool::Line);
    c.type_point("0,0"); c.type_point("4,0"); c.type_point("4,3"); c.type_point("0,0");
    c.cancel();
    c.set_tool(Tool::Circle);
    c.type_point("8,2"); c.type_point("9,2");
    c.set_tool(Tool::Select);
    c.add_text(gcm::to_lattice(c.volume(), 1.0, 1.0), "ROOM 101", 0.25);
    s.build_frame();

    const auto shapes = c.shapes();
    const auto texts  = c.texts();
    CHECK(shapes.size() == 4);
    CHECK(texts.size() == 1);

    s.canvas().load_shapes({});
    s.canvas().load_texts({});
    CHECK(s.canvas().shapes().empty());
    s.apply_document();

    CHECK_MSG(s.canvas().shapes().size() == shapes.size(),
              "%zu shapes came back, expected %zu",
              s.canvas().shapes().size(), shapes.size());
    CHECK_MSG(s.canvas().texts().size() == texts.size(),
              "%zu notes came back, expected %zu",
              s.canvas().texts().size(), texts.size());
    if (s.canvas().shapes().size() != shapes.size()) return;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        CHECK_MSG(shapes[i].key == s.canvas().shapes()[i].key, "shape %zu: key moved", i);
        CHECK_MSG(shapes[i].kind == s.canvas().shapes()[i].kind, "shape %zu: kind moved", i);
        CHECK(shapes[i].a.x == s.canvas().shapes()[i].a.x);
        CHECK(shapes[i].b.y == s.canvas().shapes()[i].b.y);
    }
    CHECK(s.canvas().texts()[0].text == "ROOM 101");

    const auto revision = s.document_revision();
    s.apply_document();
    s.build_frame();
    CHECK_MSG(s.document_revision() == revision,
              "re-applying the document recorded an edit");
}

namespace {
struct DrawnState {
    std::size_t shapes{0}, notes{0};
    std::vector<gcm::u32> keys;
    bool operator==(const DrawnState& o) const {
        return shapes == o.shapes && notes == o.notes && keys == o.keys;
    }
};
DrawnState capture(const Shell& s) {
    DrawnState d;
    d.shapes = s.canvas().shapes().size();
    d.notes = s.canvas().texts().size();
    for (const auto& sh : s.canvas().shapes()) d.keys.push_back(sh.key);
    return d;
}
void draw_three(Shell& s) {
    Canvas& c = s.canvas();
    c.set_display_unit(gunit::Length::Metre);
    c.set_tool(Tool::Line);
    c.type_point("0,0"); c.type_point("4,0"); c.cancel();
    s.build_frame();
    c.set_tool(Tool::Line);
    c.type_point("4,0"); c.type_point("4,3"); c.cancel();
    s.build_frame();
    c.set_tool(Tool::Circle);
    c.type_point("8,2"); c.type_point("9,2");
    c.set_tool(Tool::Select);
    s.build_frame();
}
}

TEST(undo_through_the_document_matches_undo_through_the_journal) {
    Shell journal = doc_shell();
    draw_three(journal);
    const auto full = capture(journal);
    CHECK_MSG(full.shapes == 3, "%zu shapes drawn", full.shapes);
    if (full.shapes != 3) return;

    Shell store = doc_shell();
    store.set_document_undo(true);
    draw_three(store);
    CHECK_MSG(capture(store) == full, "the two shells drew different things");

    for (int step = 0; step < 3; ++step) {
        journal.command("UNDO");
        journal.build_frame();
        store.command("UNDO");
        store.build_frame();
        const auto a = capture(journal), b = capture(store);
        CHECK_MSG(a == b,
                  "after %d undos: journal has %zu shapes, the document has %zu",
                  step + 1, a.shapes, b.shapes);
    }
    CHECK_MSG(capture(store).shapes == 0,
              "%zu shapes survived three undos", capture(store).shapes);

    for (int step = 0; step < 3; ++step) {
        journal.command("REDO");
        journal.build_frame();
        store.command("REDO");
        store.build_frame();
        const auto a = capture(journal), b = capture(store);
        CHECK_MSG(a == b,
                  "after %d redos: journal has %zu shapes, the document has %zu",
                  step + 1, a.shapes, b.shapes);
    }
    CHECK_MSG(capture(store) == full, "redo did not get back to where it started");
}

TEST(document_undo_says_so_when_there_is_nothing_to_undo) {
    Shell s = doc_shell();
    s.set_document_undo(true);
    s.build_frame();
    const auto before = capture(s);
    s.command("UNDO");
    s.build_frame();
    CHECK(capture(s) == before);
    CHECK_MSG(!s.log().empty() && s.log().back() == "nothing to undo",
              "said \"%s\"", s.log().empty() ? "" : s.log().back().c_str());
}

TEST(restoring_the_document_settings_reproduces_them_exactly) {
    Shell s = doc_shell();
    s.canvas().set_display_unit(gunit::Length::Metre);
    Canvas& c = s.canvas();
    c.set_tool(Tool::Line);
    c.type_point("0,0"); c.type_point("4,0"); c.cancel();
    c.set_tool(Tool::Select);
    c.add_text(gcm::to_lattice(c.volume(), 1.0, 1.0), "ROOM", 0.25);
    s.command("LAYER");
    s.build_frame();

    const auto first = s.document_settings_image();
    CHECK_MSG(!first.empty(), "no settings image at all");
    CHECK_MSG(s.restore_document_settings(first), "the image would not restore");
    const auto second = s.document_settings_image();
    CHECK_MSG(first.size() == second.size(),
              "the image changed size on restore: %zu -> %zu",
              first.size(), second.size());
    if (first.size() != second.size()) return;
    std::size_t differing = 0;
    for (std::size_t i = 0; i < first.size(); ++i)
        if (first[i] != second[i]) ++differing;
    CHECK_MSG(differing == 0, "%zu of %zu bytes differ after a restore",
              differing, first.size());
}


TEST(dimension_extension_offset_native_entities_keep_exact_vector_and_anchors) {
    gdim::Dimension dim;dim.kind=gdim::Kind::Aligned;dim.p1={10,20};dim.p2={50,80};dim.p3={100,200};
    dim.o1={7,gdim::Dimension::Anchor::Start};dim.o2={8,gdim::Dimension::Anchor::End};
    dim.extension_offset={(gcm::i64{1}<<55)+123,-987654321};
    std::vector<gdoc::Entity> entities;append_entities({dim},{},{},{},entities);
    std::vector<gdim::Dimension> dims;std::vector<gdoc::AssociativeHatch> hatches;
    std::vector<ggeo::NurbsCurve2<Scalar>> splines;std::vector<Canvas::StoredConstraint> constraints;
    read_entities(entities,dims,hatches,splines,constraints);CHECK(dims.size()==1);
    if(dims.empty())return;
    CHECK(dims[0].extension_offset.x==dim.extension_offset.x);CHECK(dims[0].extension_offset.y==dim.extension_offset.y);
    CHECK(dims[0].p1.x==10);CHECK(dims[0].p2.y==80);CHECK(dims[0].p3.y==200);
    CHECK(dims[0].o1.shape==7);CHECK(dims[0].o2.shape==8);
    entities[0].payload.ints.resize(5);entities[0].payload.points.resize(5);dims.clear();
    read_entities(entities,dims,hatches,splines,constraints);CHECK(dims.size()==1);
    CHECK(dims[0].extension_offset.x==0);CHECK(dims[0].extension_offset.y==0);CHECK(dims[0].o1.shape==7);
}

TEST(dimension_extension_offset_document_mirror_keeps_presentation_and_association) {
    auto shell=doc_shell();auto& canvas=shell.canvas();const auto& volume=canvas.volume();
    gsnap::Shape edge;edge.a=gcm::to_lattice(volume,0,0);edge.b=gcm::to_lattice(volume,4,0);
    canvas.modify_shapes([&](auto& shapes){shapes.push_back(edge);});
    gdim::Dimension dim;dim.kind=gdim::Kind::Horizontal;dim.p1=edge.a;dim.p2=edge.b;
    dim.p3=gcm::to_lattice(volume,2,2);dim.extension_offset={0,gcm::i64(std::llround(.5/volume.inv_scale()))};
    dim.o1={canvas.shapes()[0].key,gdim::Dimension::Anchor::Start};dim.o2={canvas.shapes()[0].key,gdim::Dimension::Anchor::End};
    for(bool shifted:{true,false}) {
        if(!shifted)dim.extension_offset={};
        canvas.load_dimensions({dim});shell.force_document_resync();shell.sync_document();
        canvas.load_dimensions({});shell.apply_document();CHECK(canvas.dimensions().size()==1);
        if(canvas.dimensions().empty())continue;
        const auto& restored=canvas.dimensions()[0];
        CHECK(restored.extension_offset.x==dim.extension_offset.x);CHECK(restored.extension_offset.y==dim.extension_offset.y);
        CHECK(restored.o1.shape==dim.o1.shape);CHECK(restored.o2.shape==dim.o2.shape);
        CHECK(restored.p1.y==dim.p1.y);CHECK(restored.p2.x==dim.p2.x);CHECK(restored.p3.y==dim.p3.y);
    }
}
