#pragma once

#include <optional>
#include <array>
#include <string>
#include <vector>

#include "engine/entity.hpp"
#include "engine/underlay.hpp"
#include "geometry/snap.hpp"
#include "geometry/clip.hpp"

namespace gdoc {
class LayerTable;

using gcm::f64;
using gcm::i64;
using gcm::u8;
using gcm::u32;
using Point2 = gcm::Lattice2;

struct AttributeDef {
    std::string tag;
    std::string prompt;
    std::string default_value;

    bool invisible{false};
    bool constant{false};
    bool verify{false};
    bool preset{false};
    bool multiline{false};
    bool lock_position{false};

    Point2 position{};
    f64    height{0.0025};
    f64    rotation{0.0};
    std::string style{"Standard"};

    bool prompts_user() const noexcept { return !constant && !preset; }
};

struct AttributeValue {
    std::string tag;
    std::string value;
};

struct BlockNote {
    Point2      pos{};
    std::string text;
    f64         height{0.0};
    f64         angle{0.0};
    std::string layer{"0"};
    gcm::i32    halign{0};
    gcm::i32    valign{0};
    f64         width{0.0};
    std::string style{"Standard"};
    Attributes  attrs;
};

struct BlockRef {
    std::string name;
    Point2      insertion{};
    f64         scale_x{1.0}, scale_y{1.0};
    f64         rotation{0.0};
    std::string layer{"0"};

    std::vector<AttributeValue> attributes;
    std::vector<BlockNote> notes;
    gcm::u64 selection_id{0}; // Session identity, retained by undo; never serialized.
    Attributes attrs;

    std::optional<std::string> attribute(std::string_view tag) const;
    void set_attribute(std::string_view tag, std::string_view value);

    bool uniform_scale() const noexcept {
        return std::abs(scale_x - scale_y) < 1e-12;
    }
};

struct BlockDef {
    std::string name;
    std::string description;
    Point2      base{};
    bool        allow_exploding{true};
    bool        annotative{false};
    bool        is_xref{false};
    std::string xref_path;

    std::vector<gsnap::Shape>  geometry;
    using Note = BlockNote;
    std::vector<Note>          notes;
    std::vector<AttributeDef>  attributes;
    std::vector<std::string>   nested;   // legacy insertions at the parent base
    std::vector<BlockRef>      nested_refs; // placements in definition coordinates
    std::vector<Underlay>      underlays;
    std::vector<gcm::u8>       source_document;
    std::string               source_owner;

    bool has_attributes() const noexcept { return !attributes.empty(); }
    const AttributeDef* attribute(std::string_view tag) const;
};

enum class XrefType : gcm::u8 { Attach, Overlay };
enum class XrefState : gcm::u8 { Loaded, Unloaded, NotFound, Unresolved };

struct Xref {
    std::string name;
    std::string path;
    std::string resolved_path;
    XrefType    type{XrefType::Attach};
    XrefState   state{XrefState::Unresolved};

    Point2 insertion{};
    f64    scale{1.0};
    f64    rotation{0.0};

    std::vector<Point2> clip;
    bool   clip_inverted{false};
    bool   clip_enabled{true};

    bool nested{false};
    bool visible() const noexcept { return state == XrefState::Loaded; }
};

enum class ParameterKind : u8 {
    Linear,
    Point,
    Polar,
    XY,
    Rotation,
    Flip,
    Visibility,
    Lookup,
    Alignment,
};

const char* parameter_kind_name(ParameterKind k) noexcept;

enum class ActionKind : u8 {
    Move, Scale, Stretch, Polar, Rotate, Flip, Array, Lookup,
};

const char* action_kind_name(ActionKind k) noexcept;

struct BlockParameter {
    std::string   name;
    ParameterKind kind{ParameterKind::Linear};
    Point2        base{}, end{};

    f64 value{0.0};
    f64 minimum{0.0}, maximum{0.0};   // equal means unconstrained
    f64 increment{0.0};    // 0 means continuous

    std::vector<std::string> choices;
    std::size_t              choice{0};

    bool valid_value(f64 v) const noexcept;
    f64  clamp_value(f64 v) const noexcept;
};

struct BlockAction {
    std::string name;
    ActionKind  kind{ActionKind::Move};
    std::string parameter;

    std::vector<u32> geometry;

    f64 dx{0.0}, dy{0.0};
    f64 scale{1.0};
    Point2 origin{};
    Point2 window_lo{}, window_hi{};
};

struct DynamicBlock {
    std::string                 name;
    std::vector<BlockParameter> parameters;
    std::vector<BlockAction>    actions;

    struct VisibilitySet {
        std::string      name;
        std::vector<u32> geometry;
    };
    std::vector<VisibilitySet> visibility;
    std::size_t                visible_set{0};

    BlockParameter*       parameter(std::string_view name);
    const BlockParameter* parameter(std::string_view name) const;
    bool set_parameter(std::string_view name, f64 value);
    bool set_choice(std::string_view name, std::string_view choice);
    bool set_visibility(std::string_view set_name);
};

struct DynamicResult {
    std::vector<gsnap::Shape> geometry;
    std::size_t               moved{0};
    std::size_t               hidden{0};
    bool                      ok{false};
    const char*               reason{""};
};

DynamicResult evaluate(const BlockDef& def, const DynamicBlock& dyn,
                       const gcm::WorkingVolume& v);

gsnap::Shape place_in_block(const gsnap::Shape& s, const BlockRef& r, Point2 base,
                            const gcm::WorkingVolume& v);

class BlockTable {
public:
    BlockDef&       define(BlockDef d);
    const BlockDef* find(std::string_view name) const;
    std::vector<BlockRef> nested_insertions(const BlockDef& def) const;
    struct NoteInstance {
        BlockDef::Note note;
        // Row-major affine map from definition metres to drawing metres.
        std::array<double, 6> transform{1, 0, 0, 0, 1, 0};
        std::vector<gclip::Boundary> clips;
    };
    std::vector<NoteInstance> note_instances(const BlockRef& ref,
        const gcm::WorkingVolume& volume, int depth = 8,
        const LayerTable* layers = nullptr) const;
    struct UnderlayInstance {
        Underlay underlay;
        std::array<double, 6> transform{1, 0, 0, 0, 1, 0};
        std::vector<gclip::Boundary> clips;
    };
    std::vector<UnderlayInstance> underlay_instances(const BlockRef& ref,
        const gcm::WorkingVolume& volume, int depth = 8,
        const LayerTable* layers = nullptr) const;
    void replace_xref_contents(Xref reference, std::vector<BlockDef> definitions,
        std::vector<Xref> nested, std::vector<DynamicBlock> dynamics);
    bool            remove(std::string_view name);
    bool            rename(std::string_view from, std::string_view to);

    std::size_t size() const noexcept { return defs_.size(); }
    const std::vector<BlockDef>& all() const noexcept { return defs_; }

    DynamicBlock&       make_dynamic(std::string_view name);
    DynamicBlock*       dynamic(std::string_view name);
    const DynamicBlock* dynamic(std::string_view name) const;
    bool                is_dynamic(std::string_view name) const;
    std::size_t         dynamic_count() const noexcept { return dynamics_.size(); }
    const std::vector<DynamicBlock>& dynamics() const noexcept { return dynamics_; }
    gcm::u64 revision() const noexcept { return revision_; }
    void load_extensions(std::vector<DynamicBlock> dynamics, std::vector<Xref> xrefs) {
        ++revision_;
        dynamics_ = std::move(dynamics); xrefs_ = std::move(xrefs);
    }

    gcm::u32 insert(BlockRef r);
    const std::vector<BlockRef>& references() const noexcept { return refs_; }
    BlockRef* reference_mut(gcm::u32 index) {
        ++revision_;
        return index < refs_.size() ? &refs_[index] : nullptr;
    }
    void set_references(std::vector<BlockRef> refs) { ++revision_; refs_ = std::move(refs); }
    std::vector<gcm::u32> references_to(std::string_view name) const;
    BlockRef* reference(gcm::u32 index);
    bool      erase_reference(gcm::u32 index);

    void populate_attributes(BlockRef& r) const;

    std::vector<gsnap::Shape> expand(const BlockRef& r, const gcm::WorkingVolume& v,
                                     int depth = 8, const LayerTable* layers = nullptr) const;

    std::optional<std::vector<gsnap::Shape>> explode(gcm::u32 ref_index,
                                                     const gcm::WorkingVolume& v) const;

    bool would_recurse(std::string_view name,
                       const std::vector<std::string>& nested) const;

    Xref&       attach(Xref x);
    Xref*       xref(std::string_view name);
    const Xref* xref(std::string_view name) const;
    bool        detach(std::string_view name);
    void        reload(std::string_view name, bool found);
    void        unload(std::string_view name);
    std::size_t repair_paths(std::string_view old_prefix,
                             std::string_view new_prefix);
    const std::vector<Xref>& xrefs() const noexcept { return xrefs_; }

private:
    gcm::u64 revision_{0};
    std::vector<DynamicBlock> dynamics_;
    std::vector<BlockDef> defs_;
    std::vector<BlockRef> refs_;
    std::vector<Xref>     xrefs_;
};

}
