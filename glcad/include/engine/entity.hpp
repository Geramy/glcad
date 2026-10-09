#pragma once

#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "math/fixed.hpp"
#include "geometry/d3.hpp"
#include "engine/layer.hpp"

namespace gdoc {

using gcm::u64;

struct EntityId {
    u64 hi{0}, lo{0};

    bool valid() const noexcept { return hi != 0 || lo != 0; }
    bool operator==(const EntityId& o) const noexcept { return hi == o.hi && lo == o.lo; }
    bool operator!=(const EntityId& o) const noexcept { return !(*this == o); }
};

struct EntityIdHash {
    std::size_t operator()(const EntityId& e) const noexcept {
        return std::hash<u64>{}(e.hi) * 1000003u ^ std::hash<u64>{}(e.lo);
    }
};

using SpaceId = u32;
inline constexpr SpaceId kNoSpace = 0xFFFFFFFFu;

enum class Kind : gcm::u16 {
    Unknown = 0,
    Line, Polyline, Arc, Circle, Ellipse, Spline, Point, Ray, Xline,
    Hatch, Text, MText, Dimension, Leader, Table, RevisionCloud,
    Body, Surface, Mesh, Sketch,
    BlockDefinition, BlockInstance, Xref, Group,
    View, Viewport, Sheet,
    Constraint, Parameter,
    Settings,
};

const char* kind_name(Kind k) noexcept;
bool is_2d_geometry(Kind k) noexcept;
bool is_3d_geometry(Kind k) noexcept;
bool is_annotation(Kind k) noexcept;

enum class SpaceKind : gcm::u8 { Model, Sketch, Draft, Sheet };

const char* space_kind_name(SpaceKind k) noexcept;

struct Space {
    SpaceId     id{kNoSpace};
    SpaceKind   kind{SpaceKind::Model};
    std::string name;
    ggeo::d3::Frame<gcm::F64> frame{ggeo::d3::Frame<gcm::F64>::world_xy()};

    double paper_width{0.297}, paper_height{0.210};

    bool is_planar() const noexcept {
        return kind == SpaceKind::Sketch || kind == SpaceKind::Draft ||
               kind == SpaceKind::Sheet;
    }
    bool embedded_in_world() const noexcept {
        return kind == SpaceKind::Sketch || kind == SpaceKind::Draft;
    }
};

struct Payload {
    std::vector<gcm::Lattice3> points;
    std::vector<double>        reals;
    std::vector<gcm::u32>      ints;
    std::string                text;
    std::vector<EntityId>      refs;

    std::vector<gcm::u8>       blob;
};

struct Entity {
    EntityId    id{};
    Kind        kind{Kind::Unknown};
    SpaceId     space{kNoSpace};
    std::string layer{"0"};
    Attributes  attrs;
    Payload     payload;

    bool valid() const noexcept { return id.valid() && kind != Kind::Unknown; }
};

enum class ChangeOp : gcm::u8 { Create, Modify, Delete };

struct Change {
    ChangeOp op{ChangeOp::Create};
    EntityId id{};
    Entity   before;
    Entity   after;
};

struct Transaction {
    std::string         label;
    std::vector<Change> changes;
    SpaceId space{kNoSpace};
    bool empty() const noexcept { return changes.empty(); }
};

class Document {
public:
    Document();

    SpaceId      add_space(Space s);
    Space*       space(SpaceId id);
    const Space* space(SpaceId id) const;
    SpaceId      model_space() const noexcept { return model_; }
    std::vector<SpaceId> spaces_of(SpaceKind k) const;
    std::size_t  space_count() const noexcept { return spaces_.size(); }

    SpaceId current_space() const noexcept { return current_space_; }
    void    set_current_space(SpaceId s) noexcept;

    LayerTable&       layers() noexcept { return layers_; }
    const LayerTable& layers() const noexcept { return layers_; }

    EntityId      create(Entity e);

    bool          adopt(const Entity& e);
    bool          erase(EntityId id);
    Entity*       find(EntityId id);
    const Entity* find(EntityId id) const;
    bool          modify(EntityId id, const std::function<void(Entity&)>& fn);

    std::size_t entity_count() const noexcept { return entities_.size(); }
    std::vector<EntityId> in_space(SpaceId s) const;
    std::vector<EntityId> on_layer(std::string_view layer) const;
    std::vector<EntityId> of_kind(Kind k) const;

    std::vector<EntityId> dependents_of(EntityId id) const;

    bool is_visible(EntityId id) const;
    bool is_editable(EntityId id) const;

    void begin(std::string_view label);
    bool commit();
    void abort();
    bool in_transaction() const noexcept { return open_; }
    void set_transaction_space(SpaceId s) noexcept {
        if (open_) pending_.space = s;
    }

    bool undo();
    bool redo();
    SpaceId last_applied_space() const noexcept { return last_applied_space_; }
    void forget_history() noexcept { undo_.clear(); redo_.clear(); }
    bool can_undo() const noexcept { return !undo_.empty(); }
    bool can_redo() const noexcept { return !redo_.empty(); }
    std::size_t undo_depth() const noexcept { return undo_.size(); }
    std::string undo_label() const;
    std::string redo_label() const;

    const std::vector<Transaction>& history() const noexcept { return undo_; }

    using Listener = std::function<void(const Transaction&)>;
    void on_change(Listener l) { listeners_.push_back(std::move(l)); }

private:
    void record(Change c);
    void apply(const Change& c, bool forward);

    std::unordered_map<EntityId, Entity, EntityIdHash> entities_;
    std::vector<Space> spaces_;
    LayerTable         layers_;

    SpaceId model_{0};
    SpaceId current_space_{0};
    SpaceId next_space_{0};
    u64     next_id_{1};

    bool                     open_{false};
    Transaction              pending_;
    std::vector<Transaction> undo_, redo_;
    std::vector<Listener>    listeners_;
    SpaceId                  last_applied_space_{kNoSpace};
};

}
