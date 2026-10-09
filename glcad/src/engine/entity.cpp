#include "engine/entity.hpp"

#include <algorithm>

namespace gdoc {

const char* kind_name(Kind k) noexcept {
    switch (k) {
        case Kind::Line: return "Line";
        case Kind::Polyline: return "Polyline";
        case Kind::Arc: return "Arc";
        case Kind::Circle: return "Circle";
        case Kind::Ellipse: return "Ellipse";
        case Kind::Spline: return "Spline";
        case Kind::Point: return "Point";
        case Kind::Ray: return "Ray";
        case Kind::Xline: return "Xline";
        case Kind::Hatch: return "Hatch";
        case Kind::Text: return "Text";
        case Kind::MText: return "MText";
        case Kind::Dimension: return "Dimension";
        case Kind::Leader: return "Leader";
        case Kind::Table: return "Table";
        case Kind::RevisionCloud: return "RevisionCloud";
        case Kind::Body: return "Body";
        case Kind::Surface: return "Surface";
        case Kind::Mesh: return "Mesh";
        case Kind::Sketch: return "Sketch";
        case Kind::BlockDefinition: return "BlockDefinition";
        case Kind::BlockInstance: return "BlockInstance";
        case Kind::Xref: return "Xref";
        case Kind::Group: return "Group";
        case Kind::View: return "View";
        case Kind::Viewport: return "Viewport";
        case Kind::Sheet: return "Sheet";
        case Kind::Constraint: return "Constraint";
        case Kind::Parameter: return "Parameter";
        case Kind::Settings: return "Settings";
        default: return "Unknown";
    }
}

bool is_2d_geometry(Kind k) noexcept {
    switch (k) {
        case Kind::Line: case Kind::Polyline: case Kind::Arc: case Kind::Circle:
        case Kind::Ellipse: case Kind::Spline: case Kind::Point: case Kind::Ray:
        case Kind::Xline: case Kind::Hatch:
            return true;
        default: return false;
    }
}

bool is_3d_geometry(Kind k) noexcept {
    return k == Kind::Body || k == Kind::Surface || k == Kind::Mesh;
}

bool is_annotation(Kind k) noexcept {
    switch (k) {
        case Kind::Text: case Kind::MText: case Kind::Dimension:
        case Kind::Leader: case Kind::Table: case Kind::RevisionCloud:
            return true;
        default: return false;
    }
}

const char* space_kind_name(SpaceKind k) noexcept {
    switch (k) {
        case SpaceKind::Model:  return "Model";
        case SpaceKind::Sketch: return "Sketch";
        case SpaceKind::Draft:  return "Draft";
        case SpaceKind::Sheet:  return "Sheet";
    }
    return "";
}

Document::Document() {
    Space m;
    m.kind = SpaceKind::Model;
    m.name = "Model";
    model_ = add_space(m);
    current_space_ = model_;
}

SpaceId Document::add_space(Space s) {
    if (s.id != kNoSpace) {
        if (Space* existing = space(s.id)) { *existing = s; return existing->id; }
        if (s.id >= next_space_) next_space_ = s.id + 1;
        spaces_.push_back(std::move(s));
        return spaces_.back().id;
    }
    s.id = next_space_++;
    spaces_.push_back(std::move(s));
    return spaces_.back().id;
}

Space* Document::space(SpaceId id) {
    for (auto& s : spaces_)
        if (s.id == id) return &s;
    return nullptr;
}
const Space* Document::space(SpaceId id) const {
    for (const auto& s : spaces_)
        if (s.id == id) return &s;
    return nullptr;
}

std::vector<SpaceId> Document::spaces_of(SpaceKind k) const {
    std::vector<SpaceId> out;
    for (const auto& s : spaces_)
        if (s.kind == k) out.push_back(s.id);
    return out;
}

void Document::set_current_space(SpaceId s) noexcept {
    if (space(s)) current_space_ = s;
}

void Document::begin(std::string_view label) {
    if (open_) commit();
    pending_ = Transaction{std::string(label), {}};
    open_ = true;
}

void Document::record(Change c) {
    if (!open_) {
        begin("Edit");
        pending_.changes.push_back(std::move(c));
        commit();
        return;
    }
    pending_.changes.push_back(std::move(c));
}

bool Document::commit() {
    if (!open_) return false;
    open_ = false;
    if (pending_.empty()) return false;

    undo_.push_back(pending_);
    redo_.clear();
    for (auto& l : listeners_) l(pending_);
    pending_ = Transaction{};
    return true;
}

void Document::abort() {
    if (!open_) return;
    // Roll back in reverse so dependent changes unwind correctly.
    for (auto it = pending_.changes.rbegin(); it != pending_.changes.rend(); ++it)
        apply(*it, /*forward=*/false);
    pending_ = Transaction{};
    open_ = false;
}

void Document::apply(const Change& c, bool forward) {
    switch (c.op) {
        case ChangeOp::Create:
            if (forward) entities_[c.id] = c.after;
            else entities_.erase(c.id);
            break;
        case ChangeOp::Delete:
            if (forward) entities_.erase(c.id);
            else entities_[c.id] = c.before;
            break;
        case ChangeOp::Modify:
            entities_[c.id] = forward ? c.after : c.before;
            break;
    }
}

bool Document::undo() {
    if (open_) abort();
    if (undo_.empty()) return false;

    Transaction t = undo_.back();
    undo_.pop_back();
    last_applied_space_ = t.space;
    for (auto it = t.changes.rbegin(); it != t.changes.rend(); ++it)
        apply(*it, /*forward=*/false);
    redo_.push_back(t);
    for (auto& l : listeners_) l(t);
    return true;
}

bool Document::redo() {
    if (open_) abort();
    if (redo_.empty()) return false;

    Transaction t = redo_.back();
    redo_.pop_back();
    last_applied_space_ = t.space;
    for (const auto& c : t.changes) apply(c, /*forward=*/true);
    undo_.push_back(t);
    for (auto& l : listeners_) l(t);
    return true;
}

std::string Document::undo_label() const {
    return undo_.empty() ? std::string{} : undo_.back().label;
}
std::string Document::redo_label() const {
    return redo_.empty() ? std::string{} : redo_.back().label;
}

EntityId Document::create(Entity e) {
    // Sequential low word, fixed high word: not unique across merged documents.
    e.id = EntityId{0x474C4341'44000001ull, next_id_++};
    if (e.space == kNoSpace) e.space = current_space_;
    if (e.layer.empty()) e.layer = layers_.current();

    Change c;
    c.op = ChangeOp::Create;
    c.id = e.id;
    c.after = e;

    entities_[e.id] = std::move(e);
    const EntityId id = c.id;
    record(std::move(c));
    return id;
}

bool Document::adopt(const Entity& e) {
    if (!e.id.valid()) return false;
    Entity copy = e;
    const bool existed = entities_.count(copy.id) != 0;
    Change c;
    c.op = existed ? ChangeOp::Modify : ChangeOp::Create;
    c.id = copy.id;
    if (existed) c.before = entities_[copy.id];
    c.after = copy;
    entities_[copy.id] = std::move(copy);
    record(c);
    return true;
}

bool Document::erase(EntityId id) {
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;

    Change c;
    c.op = ChangeOp::Delete;
    c.id = id;
    c.before = it->second;

    entities_.erase(it);
    record(std::move(c));
    return true;
}

Entity* Document::find(EntityId id) {
    const auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}
const Entity* Document::find(EntityId id) const {
    const auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

bool Document::modify(EntityId id, const std::function<void(Entity&)>& fn) {
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;

    Change c;
    c.op = ChangeOp::Modify;
    c.id = id;
    c.before = it->second;

    fn(it->second);
    c.after = it->second;
    record(std::move(c));
    return true;
}

std::vector<EntityId> Document::in_space(SpaceId s) const {
    std::vector<EntityId> out;
    for (const auto& [id, e] : entities_)
        if (e.space == s) out.push_back(id);
    return out;
}

std::vector<EntityId> Document::on_layer(std::string_view layer) const {
    std::vector<EntityId> out;
    for (const auto& [id, e] : entities_)
        if (e.layer == layer) out.push_back(id);
    return out;
}

std::vector<EntityId> Document::of_kind(Kind k) const {
    std::vector<EntityId> out;
    for (const auto& [id, e] : entities_)
        if (e.kind == k) out.push_back(id);
    return out;
}

std::vector<EntityId> Document::dependents_of(EntityId id) const {
    std::vector<EntityId> out;
    for (const auto& [eid, e] : entities_)
        for (const auto& r : e.payload.refs)
            if (r == id) { out.push_back(eid); break; }
    return out;
}

bool Document::is_visible(EntityId id) const {
    const Entity* e = find(id);
    if (!e || !e->attrs.visible) return false;
    const Layer* l = layers_.find(e->layer);
    return l ? l->visible() : true;
}

bool Document::is_editable(EntityId id) const {
    const Entity* e = find(id);
    if (!e) return false;
    const Layer* l = layers_.find(e->layer);
    return l ? l->editable() : true;
}

}
