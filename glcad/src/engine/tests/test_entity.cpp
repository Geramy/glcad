#include "test.hpp"

#include <cmath>
#include <cstdlib>

#include "engine/entity.hpp"

using namespace gdoc;

static Entity line(const char* layer = "0") {
    Entity e;
    e.kind = Kind::Line;
    e.layer = layer;
    e.payload.points = {{0, 0, 0}, {1000, 0, 0}};
    return e;
}

TEST(entity_ids_are_stable_and_unique) {
    Document d;
    const EntityId a = d.create(line());
    const EntityId b = d.create(line());
    CHECK(a.valid() && b.valid());
    CHECK(a != b);

    d.erase(b);
    CHECK(d.find(a) != nullptr);
    CHECK(d.find(a)->id == a);
}

TEST(entity_store_holds_2d_and_3d_together) {
    Document d;
    Entity l = line();
    Entity body;
    body.kind = Kind::Body;
    Entity dim;
    dim.kind = Kind::Dimension;

    d.create(l);
    d.create(body);
    d.create(dim);

    CHECK(d.entity_count() == 3);
    CHECK(d.of_kind(Kind::Line).size() == 1);
    CHECK(d.of_kind(Kind::Body).size() == 1);
    CHECK(d.of_kind(Kind::Dimension).size() == 1);

    CHECK(is_2d_geometry(Kind::Line));
    CHECK(is_3d_geometry(Kind::Body));
    CHECK(is_annotation(Kind::Dimension));
}

TEST(entity_layers_apply_to_everything) {
    Document d;
    Layer walls;
    walls.name = "A-WALL";
    d.layers().add(walls);

    Entity body;
    body.kind = Kind::Body;
    body.layer = "A-WALL";
    const EntityId b = d.create(body);

    Entity dim;
    dim.kind = Kind::Dimension;
    dim.layer = "A-WALL";
    d.create(dim);

    CHECK(d.on_layer("A-WALL").size() == 2);

    CHECK(d.is_visible(b));
    d.layers().find("A-WALL")->frozen = true;
    CHECK(!d.is_visible(b));

    d.layers().find("A-WALL")->frozen = false;
    d.layers().find("A-WALL")->locked = true;
    CHECK_MSG(d.is_visible(b), "a locked layer stays visible and snappable");
    CHECK(!d.is_editable(b));
}

TEST(entity_document_starts_with_model_space) {
    Document d;
    CHECK(d.space_count() == 1);
    CHECK(d.space(d.model_space())->kind == SpaceKind::Model);
    CHECK(d.current_space() == d.model_space());
}

TEST(entity_draft_space_is_a_plane_in_the_world) {
    Document d;
    Space draft;
    draft.kind = SpaceKind::Draft;
    draft.name = "Ground floor";
    draft.frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, 3.6);
    const SpaceId id = d.add_space(draft);

    const Space* s = d.space(id);
    CHECK(s->is_planar());
    CHECK_MSG(s->embedded_in_world(),
              "Draft Space must be embedded in the 3D world");
    CHECK(std::abs(s->frame.origin.z.to_f64() - 3.6) < 1e-9);

    Space sheet;
    sheet.kind = SpaceKind::Sheet;
    const SpaceId sid = d.add_space(sheet);
    CHECK(d.space(sid)->is_planar());
    CHECK(!d.space(sid)->embedded_in_world());
}

TEST(entity_stacked_draft_spaces) {
    Document d;
    for (double z : {0.0, 3.6, 7.2}) {
        Space f;
        f.kind = SpaceKind::Draft;
        f.frame.origin = ggeo::d3::Point<gcm::F64>::from(0, 0, z);
        d.add_space(f);
    }
    const auto drafts = d.spaces_of(SpaceKind::Draft);
    CHECK(drafts.size() == 3);
    CHECK(d.spaces_of(SpaceKind::Model).size() == 1);
}

TEST(entity_entities_belong_to_a_space) {
    Document d;
    Space draft;
    draft.kind = SpaceKind::Draft;
    const SpaceId did = d.add_space(draft);

    d.create(line());
    d.set_current_space(did);
    d.create(line());
    d.create(line());

    CHECK(d.in_space(d.model_space()).size() == 1);
    CHECK(d.in_space(did).size() == 2);
}

TEST(entity_undo_is_the_inverse_of_a_transaction) {
    Document d;
    d.begin("Draw wall");
    const EntityId a = d.create(line());
    const EntityId b = d.create(line());
    CHECK(d.commit());

    CHECK(d.entity_count() == 2);
    CHECK(d.can_undo());
    CHECK(d.undo_label() == "Draw wall");

    CHECK(d.undo());
    CHECK_MSG(d.entity_count() == 0, "undo must remove the whole transaction");
    CHECK(d.can_redo());

    CHECK(d.redo());
    CHECK(d.entity_count() == 2);
    CHECK(d.find(a) != nullptr);
    CHECK(d.find(b) != nullptr);
}

TEST(entity_undo_restores_modified_state) {
    Document d;
    const EntityId id = d.create(line());

    d.begin("Move");
    d.modify(id, [](Entity& e) { e.payload.points[1].x = 5000; });
    d.commit();
    CHECK(d.find(id)->payload.points[1].x == 5000);

    d.undo();
    CHECK_MSG(d.find(id)->payload.points[1].x == 1000,
              "undo did not restore the previous geometry");
    d.redo();
    CHECK(d.find(id)->payload.points[1].x == 5000);
}

TEST(entity_undo_restores_deleted_entities) {
    Document d;
    const EntityId id = d.create(line());
    d.begin("Erase");
    d.erase(id);
    d.commit();
    CHECK(d.find(id) == nullptr);

    d.undo();
    CHECK_MSG(d.find(id) != nullptr, "undo must bring a deleted entity back");
    CHECK(d.find(id)->id == id);
}

TEST(entity_abort_rolls_back_without_touching_the_undo_stack) {
    Document d;
    d.create(line());
    const std::size_t depth = d.undo_depth();

    d.begin("Speculative");
    d.create(line());
    d.create(line());
    CHECK(d.entity_count() == 3);

    d.abort();
    CHECK_MSG(d.entity_count() == 1, "abort must roll the changes back");
    CHECK(d.undo_depth() == depth);
}

TEST(entity_a_new_edit_clears_the_redo_branch) {
    Document d;
    d.begin("A"); d.create(line()); d.commit();
    d.undo();
    CHECK(d.can_redo());

    d.begin("B"); d.create(line()); d.commit();
    CHECK_MSG(!d.can_redo(), "a new edit must invalidate the redo branch");
}

TEST(entity_mutations_outside_a_transaction_still_undo) {
    Document d;
    d.create(line());
    CHECK(d.can_undo());
    CHECK(d.undo());
    CHECK(d.entity_count() == 0);
}

TEST(entity_undo_crosses_modes) {
    Document d;
    d.begin("Mixed edit");
    Entity l = line();
    Entity body;
    body.kind = Kind::Body;
    d.create(l);
    d.create(body);
    d.commit();

    CHECK(d.entity_count() == 2);
    d.undo();
    CHECK(d.of_kind(Kind::Line).empty());
    CHECK(d.of_kind(Kind::Body).empty());
}

TEST(entity_change_listeners_fire_on_commit) {
    Document d;
    int fired = 0;
    std::string last;
    d.on_change([&](const Transaction& t) { ++fired; last = t.label; });

    d.begin("Draw");
    d.create(line());
    d.commit();
    CHECK(fired == 1);
    CHECK(last == "Draw");

    d.undo();
    CHECK_MSG(fired == 2, "undo must notify too, or views go stale");
}

TEST(entity_history_is_the_op_log) {
    Document d;
    d.begin("One"); d.create(line()); d.commit();
    d.begin("Two"); d.create(line()); d.create(line()); d.commit();

    const auto& h = d.history();
    CHECK(h.size() == 2);
    CHECK(h[0].label == "One");
    CHECK(h[0].changes.size() == 1);
    CHECK(h[1].changes.size() == 2);
    CHECK(h[1].changes[0].op == ChangeOp::Create);
}

TEST(entity_dependents_are_discoverable) {
    Document d;
    const EntityId geom = d.create(line());

    Entity dim;
    dim.kind = Kind::Dimension;
    dim.payload.refs = {geom};
    const EntityId did = d.create(dim);

    const auto deps = d.dependents_of(geom);
    CHECK(deps.size() == 1);
    CHECK(deps[0] == did);
    CHECK(d.dependents_of(did).empty());
}

TEST(entity_foreign_data_survives_a_round_trip) {
    Document d;
    Entity proxy;
    proxy.kind = Kind::Unknown;
    proxy.attrs.foreign_format = "DWG:AcDbZombieEntity";
    proxy.attrs.foreign_blob = {0xDE, 0xAD, 0xBE, 0xEF};
    const EntityId id = d.create(proxy);

    CHECK(d.find(id)->attrs.has_foreign_data());
    CHECK(d.find(id)->attrs.foreign_blob.size() == 4);
    CHECK(d.find(id)->attrs.foreign_format == "DWG:AcDbZombieEntity");

    d.create(line());
    CHECK(d.find(id)->attrs.foreign_blob[0] == 0xDE);
}

TEST(entity_attributes_default_to_bylayer) {
    Entity e = line();
    CHECK(e.attrs.color.kind == Color::Kind::ByLayer);
    CHECK(e.attrs.linetype == "ByLayer");
    CHECK(e.attrs.lineweight == Lineweight::ByLayer);
    CHECK(e.attrs.transparency == -1);
}
