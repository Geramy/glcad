#include "engine/state_archive.hpp"
#include "engine/document_file.hpp"
#include "ui/shell.hpp"
#include "ui/solids_file.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

namespace gfile::state {
#define GLCAD_SYNC_FIELDS(Type, ...) template<> struct Fields<Type> { \
    template<class A, class V> static void visit(A& a, V& v) { a(__VA_ARGS__); } };
GLCAD_SYNC_FIELDS(gcm::Lattice2, v.x, v.y)
GLCAD_SYNC_FIELDS(gdoc::Color, v.kind, v.index, v.r, v.g, v.b)
GLCAD_SYNC_FIELDS(gdoc::Attributes, v.color, v.linetype, v.lineweight, v.linetype_scale,
                  v.transparency, v.visible, v.user, v.foreign_format, v.foreign_blob)
GLCAD_SYNC_FIELDS(gdoc::AttributeValue, v.tag, v.value)
GLCAD_SYNC_FIELDS(gdoc::BlockNote, v.pos, v.text, v.height, v.angle, v.layer,
                  v.halign, v.valign, v.width, v.style, v.attrs)
GLCAD_SYNC_FIELDS(gdoc::BlockRef, v.name, v.insertion, v.scale_x, v.scale_y, v.rotation,
                  v.layer, v.attributes, v.notes, v.selection_id, v.attrs)
GLCAD_SYNC_FIELDS(gdim::Dimension::Ref, v.shape, v.at)
GLCAD_SYNC_FIELDS(gdim::Dimension::LeaderPath, v.vertices, v.arrow)
GLCAD_SYNC_FIELDS(gdim::Dimension, v.kind, v.style, v.layer, v.p1, v.p2, v.p3, v.centre,
                  v.radius, v.o1, v.o2, v.orphaned, v.text_override, v.selection_id,
                  v.leader_paths, v.leader_text_absolute, v.leader_text_height,
                  v.leader_text_angle, v.leader_arrow_size, v.leader_text_halign,
                  v.leader_text_valign)
GLCAD_SYNC_FIELDS(gdoc::HatchStyle, v.name, v.scale, v.angle, v.origin, v.associative,
                  v.islands)
GLCAD_SYNC_FIELDS(gdoc::AssociativeHatch, v.style, v.boundary_shapes, v.seed,
                  v.seed_valid, v.orphaned)
#undef GLCAD_SYNC_FIELDS
}

namespace gcapp {

namespace {
constexpr gcm::u64 kSketchTag = 0x5343;   // 'SC'
constexpr gcm::u64 kBodyTag   = 0x4244;   // 'BD'
constexpr gcm::u64 kShapeTag  = 0x5348;   // 'SH'
constexpr gcm::u64 kTextTag   = 0x5458;   // 'TX'
constexpr gcm::u64 kDimTag    = 0x444D;   // 'DM'
constexpr gcm::u64 kPreviewTag = 0x5056;  // 'PV'
constexpr gcm::u64 kInsertTag = 0x494E;   // 'IN'
constexpr gcm::u64 kHatchTag  = 0x4854;   // 'HT'
constexpr gcm::u64 kSplineTag = 0x5350;   // 'SP'
constexpr gcm::u64 kConstraintTag = 0x434E;  // 'CN'
constexpr gcm::u64 kSettingsTag = 0x5354;    // 'ST'
constexpr gcm::u64 kSolidsTag = 0x534C;      // 'SL'

constexpr gdoc::SpaceId kPlaneBase = 0x10000000u;
gdoc::SpaceId plane_space(gcm::u32 sketch_space) noexcept {
    return kPlaneBase + sketch_space;
}

gdoc::EntityId sketch_id(gcm::u32 space) noexcept {
    return gdoc::EntityId{kSketchTag, gcm::u64(space) + 1};
}
gdoc::EntityId body_id(gcm::u32 key) noexcept {
    return gdoc::EntityId{kBodyTag, gcm::u64(key)};
}
gdoc::EntityId shape_id(gcm::u32 key) noexcept {
    return gdoc::EntityId{kShapeTag, gcm::u64(key)};
}
gdoc::EntityId note_id(gcm::u64 selection) noexcept {
    return gdoc::EntityId{kTextTag, selection};
}
gdoc::EntityId dimension_id(gcm::u32 space, std::size_t at) noexcept {
    return gdoc::EntityId{kDimTag, (gcm::u64(space) << 32) | gcm::u64(at)};
}
gdoc::EntityId insert_id(gcm::u64 selection) noexcept {
    return gdoc::EntityId{kInsertTag, selection};
}
gdoc::EntityId hatch_id(gcm::u32 space, std::size_t at) noexcept {
    return gdoc::EntityId{kHatchTag, (gcm::u64(space) << 32) | gcm::u64(at)};
}
gdoc::EntityId spline_id(gcm::u32 space, std::size_t at) noexcept {
    return gdoc::EntityId{kSplineTag, (gcm::u64(space) << 32) | gcm::u64(at)};
}
gdoc::EntityId constraint_id(gcm::u32 space, std::size_t at) noexcept {
    return gdoc::EntityId{kConstraintTag, (gcm::u64(space) << 32) | gcm::u64(at)};
}

template <class T>
std::vector<gcm::u8> to_blob(const T& v) {
    gfile::state::Writer w;
    w(v);
    return std::move(w.bytes);
}

template <class T>
bool from_blob(const std::vector<gcm::u8>& bytes, T& out) {
    try {
        gfile::state::Reader r{bytes};
        r(out);
        r.finish();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<gcm::u8> to_blob(const gdim::Dimension& dimension) {
    gfile::state::Writer writer;writer(dimension);
    if(dimension.extension_offset.x || dimension.extension_offset.y)
        writer(gcm::u32(0x45585401u),dimension.extension_offset);
    return std::move(writer.bytes);
}

bool from_blob(const std::vector<gcm::u8>& bytes,gdim::Dimension& dimension) {
    try {
        gfile::state::Reader reader{bytes};reader(dimension);
        dimension.extension_offset={};
        if(reader.at<bytes.size()) {
            gcm::u32 tag=0;reader(tag);
            if(tag!=0x45585401u)return false;
            reader(dimension.extension_offset);
        }
        reader.finish();return true;
    } catch(const std::exception&) {return false;}
}

std::vector<gcm::u8> to_blob(const gdoc::AssociativeHatch& hatch) {
    gfile::state::Writer writer;
    writer(hatch);
    if (!hatch.boundary_geometry.empty()) {
        gfile::Delta boundary;
        gfile::SpaceDelta geometry; geometry.inserted=hatch.boundary_geometry;
        for (const auto& shape:geometry.inserted) geometry.layers.push_back(shape.layer);
        boundary.spaces.push_back(std::move(geometry));
        writer(gcm::u32(1),gfile::encode_delta(boundary));
    }
    return std::move(writer.bytes);
}

bool from_blob(const std::vector<gcm::u8>& bytes,gdoc::AssociativeHatch& hatch) {
    try {
        gfile::state::Reader reader{bytes}; reader(hatch);
        if (reader.at<bytes.size()) {
            gcm::u32 version=0; std::vector<gcm::u8> geometry;
            reader(version,geometry);
            gfile::Delta boundary;
            if (version!=1 || !gfile::decode_delta(geometry.data(),geometry.size(),boundary).ok ||
                boundary.spaces.size()!=1) return false;
            hatch.boundary_geometry=std::move(boundary.spaces[0].inserted);
        }
        reader.finish();
        return true;
    } catch (const std::exception&) { return false; }
}

constexpr gdoc::SpaceId kSheetBase = 0x20000000u;
gdoc::SpaceId sheet_space(gcm::u32 layout_space) noexcept {
    return kSheetBase + layout_space;
}

bool same(const gdoc::Entity& a, const gdoc::Entity& b) noexcept {
    return a.kind == b.kind && a.space == b.space && a.layer == b.layer &&
           a.attrs.color == b.attrs.color && a.attrs.linetype == b.attrs.linetype &&
           a.attrs.lineweight == b.attrs.lineweight && a.attrs.linetype_scale == b.attrs.linetype_scale &&
           a.attrs.transparency == b.attrs.transparency && a.attrs.visible == b.attrs.visible &&
           a.attrs.user == b.attrs.user && a.attrs.foreign_format == b.attrs.foreign_format &&
           a.attrs.foreign_blob == b.attrs.foreign_blob &&
           a.payload.text == b.payload.text && a.payload.ints == b.payload.ints &&
           a.payload.refs == b.payload.refs && a.payload.reals == b.payload.reals &&
           a.payload.blob == b.payload.blob &&
           a.payload.points.size() == b.payload.points.size() &&
           std::equal(a.payload.points.begin(), a.payload.points.end(),
                      b.payload.points.begin(),
                      [](const gcm::Lattice3& p, const gcm::Lattice3& q) {
                          return p.x == q.x && p.y == q.y && p.z == q.z;
                      });
}

gdoc::Kind kind_of(const gsnap::Shape& s) noexcept {
    switch (s.kind) {
        case gsnap::ShapeKind::Segment: return s.in_polyline() ? gdoc::Kind::Polyline
                                                              : gdoc::Kind::Line;
        case gsnap::ShapeKind::Circle:  return gdoc::Kind::Circle;
        case gsnap::ShapeKind::Arc:     return gdoc::Kind::Arc;
        case gsnap::ShapeKind::Point:   return gdoc::Kind::Point;
        case gsnap::ShapeKind::Block:   return gdoc::Kind::BlockInstance;
        case gsnap::ShapeKind::Ellipse: return gdoc::Kind::Ellipse;
        case gsnap::ShapeKind::Ray:     return gdoc::Kind::Ray;
        case gsnap::ShapeKind::Xline:   return gdoc::Kind::Xline;
    }
    return gdoc::Kind::Line;
}

constexpr gdoc::Kind kDrawnKinds[] = {
    gdoc::Kind::Line, gdoc::Kind::Polyline, gdoc::Kind::Arc, gdoc::Kind::Circle,
    gdoc::Kind::Ellipse, gdoc::Kind::Point, gdoc::Kind::Ray, gdoc::Kind::Xline,
    gdoc::Kind::BlockInstance,
};

// ShapeKind stored as its ordinal: the gdoc::Kind mapping is many-to-one.
enum : std::size_t { kIntKind, kIntPoly, kIntPolyIndex, kIntRegion,
                     kIntRegionLoop, kIntRegionReversed, kIntCount };

void encode_shape(const gsnap::Shape& sh, gdoc::Payload& out) {
    out.points = {{sh.a.x, sh.a.y, 0}, {sh.b.x, sh.b.y, 0}};
    // The lattice stays well inside 2^53, so a double holds it exactly.
    out.reals = {double(sh.radius), sh.start_angle, sh.end_angle};
    out.ints.assign(kIntCount, 0);
    out.ints[kIntKind] = gcm::u32(sh.kind);
    out.ints[kIntPoly] = sh.poly;
    out.ints[kIntPolyIndex] = sh.poly_index;
    out.ints[kIntRegion] = sh.region ? 1u : 0u;
    out.ints[kIntRegionLoop] = sh.region_loop;
    out.ints[kIntRegionReversed] = sh.region_reversed ? 1u : 0u;
}

bool decode_shape(const gdoc::Entity& e, gcm::u32 key, gsnap::Shape& out) {
    if (e.payload.points.size() < 2 || e.payload.reals.size() < 3 ||
        e.payload.ints.size() < kIntCount)
        return false;
    out = gsnap::Shape{};
    out.kind = gsnap::ShapeKind(e.payload.ints[kIntKind]);
    out.layer = e.layer;
    out.attrs = e.attrs;
    out.a = {e.payload.points[0].x, e.payload.points[0].y};
    out.b = {e.payload.points[1].x, e.payload.points[1].y};
    out.radius = gcm::i64(e.payload.reals[0]);
    out.start_angle = e.payload.reals[1];
    out.end_angle = e.payload.reals[2];
    out.key = key;
    out.poly = e.payload.ints[kIntPoly];
    out.poly_index = e.payload.ints[kIntPolyIndex];
    out.region = e.payload.ints[kIntRegion] != 0;
    out.region_loop = e.payload.ints[kIntRegionLoop];
    out.region_reversed = e.payload.ints[kIntRegionReversed] != 0;
    return true;
}

ggeo::d3::Frame<gcm::F64> frame_of(const gtopo::SketchPlane& p) noexcept {
    ggeo::d3::Frame<gcm::F64> f;
    f.origin = ggeo::d3::Point<gcm::F64>::from(p.ox, p.oy, p.oz);
    f.u = ggeo::d3::Vec<gcm::F64>::from(p.ux, p.uy, p.uz);
    f.v = ggeo::d3::Vec<gcm::F64>::from(p.vx, p.vy, p.vz);
    f.n = ggeo::d3::Vec<gcm::F64>::from(p.nx, p.ny, p.nz);
    return f;
}

bool same_frame(const ggeo::d3::Frame<gcm::F64>& a,
                const ggeo::d3::Frame<gcm::F64>& b) noexcept {
    const auto same_point = [](const auto& p, const auto& q) {
        return p.x.to_f64() == q.x.to_f64() && p.y.to_f64() == q.y.to_f64() &&
               p.z.to_f64() == q.z.to_f64();
    };
    return same_point(a.origin, b.origin) && same_point(a.u, b.u) &&
           same_point(a.v, b.v) && same_point(a.n, b.n);
}

gtopo::SketchPlane plane_of(const ggeo::d3::Frame<gcm::F64>& f) noexcept {
    gtopo::SketchPlane p;
    p.ox = f.origin.x.to_f64(); p.oy = f.origin.y.to_f64(); p.oz = f.origin.z.to_f64();
    p.ux = f.u.x.to_f64(); p.uy = f.u.y.to_f64(); p.uz = f.u.z.to_f64();
    p.vx = f.v.x.to_f64(); p.vy = f.v.y.to_f64(); p.vz = f.v.z.to_f64();
    p.nx = f.n.x.to_f64(); p.ny = f.n.y.to_f64(); p.nz = f.n.z.to_f64();
    return p;
}
}

gdoc::EntityId Shell::document_body_id(gcm::u32 key) noexcept { return body_id(key); }
gdoc::EntityId Shell::preview_scene_id(std::size_t body) noexcept {
    return gdoc::EntityId{kPreviewTag, gcm::u64(body) + 1};
}

gcm::u64 Shell::mirror_key() const noexcept {
    gcm::u64 k = canvas_.edit_serial() * 1000003ull;
    k ^= canvas_.texts_serial() + 0x9E3779B97F4A7C15ull + (k << 6) + (k >> 2);
    k ^= modeller_.edit_serial() + 0x9E3779B97F4A7C15ull + (k << 6) + (k >> 2);
    k ^= gcm::u64(sketches_.size()) * 31u + gcm::u64(layouts_.size()) * 131u +
         gcm::u64(space_states_.size()) * 1571u + gcm::u64(active_space_id()) * 7919u;
    k ^= gcm::u64(modeller_.bodies().size()) * 104729u;
    k ^= metadata_gate_ * 2654435761ull;
    return k;
}

void Shell::sync_document(bool commit, gcm::u32 gesture_space) {
    refresh_symbol_legends();
    if (mirrored_ == mirror_key()) {
        if (commit && doc_.in_transaction()) {
            doc_.set_transaction_space(gesture_space ? gesture_space : active_space_id());
            doc_.commit();
        }
        return;
    }
    canvas_.assign_keys();
    if(!canvas_.selection_dragging() && !canvas_.grip_dragging())
        canvas_.detect_wall_regions(drawing_discipline_ != DrawingDiscipline::Mechanical);
    ensure_space_ids();
    canvas_.ensure_selection_ids();
    for (auto& sk : sketches_)
        for (auto& t : sk.texts)
            if (!t.selection_id) t.selection_id = canvas_.mint_selection_id();
    for (auto& l : layouts_) {
        for (auto& t : l.notes)
            if (!t.selection_id) t.selection_id = canvas_.mint_selection_id();
    }
    for (auto& [id, st] : space_states_)
        for (auto& r : st.content.refs)
            if (!r.selection_id) r.selection_id = canvas_.mint_selection_id();

    for (const auto& sk : sketches_) {
        const auto frame = frame_of(sk.plane);
        if (gdoc::Space* existing = doc_.space(plane_space(sk.space_id))) {
            if (existing->name != sk.name || !same_frame(existing->frame, frame)) {
                existing->name = sk.name;
                existing->frame = frame;
            }
            continue;
        }
        gdoc::Space s;
        s.id = plane_space(sk.space_id);
        s.kind = gdoc::SpaceKind::Sketch;
        s.name = sk.name;
        s.frame = frame;
        doc_.add_space(s);
    }

    std::vector<gdoc::Entity> want;
    want.reserve(sketches_.size() + modeller_.bodies().size());

    for (const auto& sk : sketches_) {
        gdoc::Entity e;
        e.id = sketch_id(sk.space_id);
        e.kind = gdoc::Kind::Sketch;
        e.space = plane_space(sk.space_id);
        e.layer = sk.layer;
        e.payload.text = sk.name;
        e.payload.ints = {gcm::u32(sk.on_face ? 1 : 0), gcm::u32(sk.visibility), sk.parent_body_key,
                          gcm::u32(sk.body), sk.face};
        want.push_back(std::move(e));
    }

    const auto& bodies = modeller_.bodies();
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const gcm::u32 key = modeller_.body_name(i);
        if (!key) continue;
        gdoc::Entity e;
        e.id = body_id(key);
        e.kind = gdoc::Kind::Body;
        e.space = doc_.model_space();
        e.layer = modeller_.body_layer(i);
        for (const auto& f : modeller_.history(i)) {
            if (f.sketch < 0 || std::size_t(f.sketch) >= sketches_.size()) continue;
            const auto ref = sketch_id(sketches_[std::size_t(f.sketch)].space_id);
            bool seen = false;
            for (const auto& have : e.payload.refs) seen = seen || have == ref;
            if (!seen) e.payload.refs.push_back(ref);
        }
        want.push_back(std::move(e));
    }

    const auto mirror_space = [&](gdoc::SpaceId space, const std::string& fallback_layer,
                                  const std::vector<gsnap::Shape>* shapes,
                                  const std::vector<Canvas::TextItem>* texts,
                                  const std::vector<gdim::Dimension>* dims,
                                  const std::vector<gdoc::BlockRef>* refs,
                                  const std::vector<gdoc::AssociativeHatch>* hatches,
                                  const std::vector<ggeo::NurbsCurve2<Scalar>>* splines,
                                  const std::vector<Canvas::StoredConstraint>* constraints) {
        if (shapes) for (const auto& sh : *shapes) {
            if (!sh.key) continue;
            gdoc::Entity e;
            e.id = shape_id(sh.key);
            e.kind = kind_of(sh);
            e.space = space;
            e.layer = sh.layer.empty() ? fallback_layer : sh.layer;
            e.attrs = sh.attrs;
            encode_shape(sh, e.payload);
            want.push_back(std::move(e));
        }
        if (texts) for (const auto& t : *texts) {
            if (!t.selection_id) continue;
            gdoc::Entity e;
            e.id = note_id(t.selection_id);
            e.kind = gdoc::Kind::Text;
            e.space = space;
            e.attrs=t.attrs; e.attrs.user.erase("style"); e.attrs.user.erase("field");
            e.layer = t.layer.empty() ? fallback_layer : t.layer;
            e.payload.text = t.text;
            e.payload.points = {{t.pos.x, t.pos.y, 0}};
            e.payload.reals = {t.height, t.angle, t.width};
            e.payload.ints = {static_cast<gcm::u32>(t.halign), static_cast<gcm::u32>(t.valign)};
            if (!t.style.empty()) e.attrs.user["style"] = t.style;
            if (!t.field.empty()) e.attrs.user["field"] = t.field;
            want.push_back(std::move(e));
        }
        if (refs) for (const auto& r : *refs) {
            if (!r.selection_id) continue;
            gdoc::Entity e;
            e.id = insert_id(r.selection_id);
            e.kind = gdoc::Kind::BlockInstance;
            e.space = space;
            e.layer = r.layer.empty() ? fallback_layer : r.layer;
            e.attrs = r.attrs;
            e.payload.text = r.name;
            e.payload.points = {{r.insertion.x, r.insertion.y, 0}};
            e.payload.reals = {r.scale_x, r.scale_y, r.rotation};
            e.payload.blob = to_blob(r);
            want.push_back(std::move(e));
        }
        if (hatches) for (std::size_t i = 0; i < hatches->size(); ++i) {
            const auto& h = (*hatches)[i];
            gdoc::Entity e;
            e.id = hatch_id(space, i);
            e.kind = gdoc::Kind::Hatch;
            e.space = space;
            e.layer = h.layer.empty() ? fallback_layer : h.layer;
            e.attrs=h.attrs;
            e.payload.text = h.style.name;
            e.payload.points = {{h.style.origin.x, h.style.origin.y, 0},
                                {h.seed.x, h.seed.y, 0}};
            e.payload.reals = {h.style.scale, h.style.angle};
            e.payload.ints = {gcm::u32(h.style.associative ? 1 : 0),
                              gcm::u32(h.style.islands),
                              gcm::u32(h.seed_valid ? 1 : 0),
                              gcm::u32(h.orphaned ? 1 : 0)};
            if(h.area_background) {
                e.attrs.user["area_background"]="1";
                e.payload.reals.push_back(h.gap_tolerance);
                e.payload.ints.insert(e.payload.ints.end(),h.source_keys.begin(),h.source_keys.end());
            }
            for (const auto key : h.boundary_shapes) e.payload.refs.push_back(shape_id(key));
            e.payload.blob = to_blob(h);
            want.push_back(std::move(e));
        }
        if (splines) for (std::size_t i = 0; i < splines->size(); ++i) {
            const auto& c = (*splines)[i];
            gdoc::Entity e;
            e.id = spline_id(space, i);
            e.kind = gdoc::Kind::Spline;
            e.space = space;
            e.layer = fallback_layer;
            e.payload.ints = {gcm::u32(c.degree), gcm::u32(c.control.size())};
            for (const auto& cp : c.control) {
                e.payload.reals.push_back(cp.p.x.to_f64());
                e.payload.reals.push_back(cp.p.y.to_f64());
                e.payload.reals.push_back(cp.w.to_f64());
            }
            for (const auto k : c.knots.knots) e.payload.reals.push_back(k);
            want.push_back(std::move(e));
        }
        if (constraints) for (std::size_t i = 0; i < constraints->size(); ++i) {
            const auto& k = (*constraints)[i];
            gdoc::Entity e;
            e.id = constraint_id(space, i);
            e.kind = gdoc::Kind::Constraint;
            e.space = space;
            e.layer = fallback_layer;
            e.payload.ints = {gcm::u32(k.kind), gcm::u32(k.driving ? 1 : 0)};
            e.payload.reals = {k.value};
            if (k.a) e.payload.refs.push_back(shape_id(k.a));
            if (k.b) e.payload.refs.push_back(shape_id(k.b));
            want.push_back(std::move(e));
        }
        if (dims) for (std::size_t i = 0; i < dims->size(); ++i) {
            const auto& d = (*dims)[i];
            gdoc::Entity e;
            e.id = dimension_id(space, i);
            e.kind = gdoc::Kind::Dimension;
            e.space = space;
            e.layer = d.layer.empty() ? fallback_layer : d.layer;
            e.payload.text = d.style;
            e.payload.points = {{d.p1.x, d.p1.y, 0}, {d.p2.x, d.p2.y, 0},
                                {d.centre.x, d.centre.y, 0}};
            for (const auto& ref : {d.o1, d.o2})
                if (ref.bound()) e.payload.refs.push_back(shape_id(ref.shape));
            e.payload.blob = to_blob(d);
            want.push_back(std::move(e));
        }
    };

    const gcm::u32 live_space = active_space_id();
    struct Stored {
        const std::vector<gdim::Dimension>*   dims{nullptr};
        const std::vector<gdoc::BlockRef>*    refs{nullptr};
        const std::vector<gdoc::AssociativeHatch>* hatches{nullptr};
        const std::vector<ggeo::NurbsCurve2<Scalar>>* splines{nullptr};
        const std::vector<Canvas::StoredConstraint>* constraints{nullptr};
    };
    const auto stored_in = [&](gcm::u32 space) {
        Stored out;
        const auto it = space_states_.find(space);
        if (it == space_states_.end()) return out;
        out.dims = &it->second.content.dims;
        out.refs = &it->second.content.refs;
        out.hatches = &it->second.content.hatches;
        out.splines = &it->second.content.splines;
        out.constraints = &it->second.content.constraints;
        return out;
    };

    for (std::size_t i = 0; i < sketches_.size(); ++i) {
        const auto& sk = sketches_[i];
        const bool live = sk.space_id == live_space;
        const Stored st = live ? Stored{} : stored_in(sk.space_id);
        mirror_space(plane_space(sk.space_id), sk.layer,
                     live ? &canvas_.shapes() : &sk.shapes,
                     live ? &canvas_.texts()  : &sk.texts,
                     live ? &canvas_.dimensions() : st.dims,
                     live ? &canvas_.blocks().references() : st.refs,
                     live ? &canvas_.hatches() : st.hatches,
                     live ? &canvas_.splines() : st.splines,
                     live ? &canvas_.constraints() : st.constraints);
    }
    for (const auto& layout : layouts_) {
        const bool live = layout.space_id == live_space;
        if (!doc_.space(sheet_space(layout.space_id))) {
            gdoc::Space sp;
            sp.id = sheet_space(layout.space_id);
            sp.kind = gdoc::SpaceKind::Sheet;
            sp.name = layout.name;
            doc_.add_space(sp);
        } else if (gdoc::Space* sp = doc_.space(sheet_space(layout.space_id))) {
            sp->name = layout.name;
        }
        const Stored st = live ? Stored{} : stored_in(layout.space_id);
        mirror_space(sheet_space(layout.space_id), "0",
                     live ? &canvas_.shapes() : &layout.shapes,
                     live ? &canvas_.texts()  : &layout.notes,
                     live ? &canvas_.dimensions() : st.dims,
                     live ? &canvas_.blocks().references() : st.refs,
                     live ? &canvas_.hatches() : st.hatches,
                     live ? &canvas_.splines() : st.splines,
                     live ? &canvas_.constraints() : st.constraints);
    }

    if (!modeller_.bodies().empty()) {
        gdoc::Entity e;
        e.id = gdoc::EntityId{kSolidsTag, 1};
        e.kind = gdoc::Kind::Mesh;
        e.space = doc_.model_space();
        e.payload.blob = encode_solids(modeller_.bodies());
        const std::string hist = encode_feature_history(modeller_.histories());
        e.payload.text.assign(hist.begin(), hist.end());
        for (std::size_t b = 0; b < modeller_.bodies().size(); ++b) {
            const gcm::u32 key = modeller_.body_name(b);
            if (key) e.payload.refs.push_back(body_id(key));
        }
        want.push_back(std::move(e));
    }

    {
        gdoc::Entity e;
        e.id = gdoc::EntityId{kSettingsTag, 1};
        e.kind = gdoc::Kind::Settings;
        e.space = doc_.model_space();
        e.payload.blob = settings_image();
        want.push_back(std::move(e));
    }

    if (!doc_.in_transaction()) doc_.begin("model");
    std::unordered_set<gdoc::EntityId, gdoc::EntityIdHash> keep;
    keep.reserve(want.size() * 2);
    const bool trace = std::getenv("GLCAD_TRACE_SYNC") != nullptr;
    for (auto& e : want) {
        keep.insert(e.id);
        const gdoc::Entity* have = doc_.find(e.id);
        if (!have) {
            if (trace) std::fprintf(stderr, "sync: NEW %s %llx/%llx\n",
                                    gdoc::kind_name(e.kind),
                                    (unsigned long long)e.id.hi,
                                    (unsigned long long)e.id.lo);
            doc_.adopt(e);
            continue;
        }
        if (same(*have, e)) continue;
        if (trace) std::fprintf(stderr, "sync: CHANGED %s %llx/%llx\n",
                                gdoc::kind_name(e.kind),
                                (unsigned long long)e.id.hi,
                                (unsigned long long)e.id.lo);
        doc_.modify(e.id, [&](gdoc::Entity& t) { t = e; });
    }
    std::vector<gdoc::EntityId> stale;
    for (const auto kind : kDrawnKinds)
        for (const auto id : doc_.of_kind(kind))
            if (!keep.count(id)) stale.push_back(id);
    for (const auto kind : {gdoc::Kind::Sketch, gdoc::Kind::Body, gdoc::Kind::Text,
                            gdoc::Kind::Dimension, gdoc::Kind::Hatch, gdoc::Kind::Spline,
                            gdoc::Kind::Constraint, gdoc::Kind::Settings,
                            gdoc::Kind::Mesh})
        for (const auto id : doc_.of_kind(kind))
            if (!keep.count(id)) stale.push_back(id);
    for (const auto id : stale) {
        if (trace) {
            const gdoc::Entity* e = doc_.find(id);
            std::fprintf(stderr, "sync: GONE %s %llx/%llx\n",
                         e ? gdoc::kind_name(e->kind) : "?",
                         (unsigned long long)id.hi, (unsigned long long)id.lo);
        }
        doc_.erase(id);
    }
    if (commit) {
        doc_.set_transaction_space(gesture_space ? gesture_space : active_space_id());
        doc_.commit();
    }
    mirrored_ = mirror_key();
}

gdoc::SpaceId Shell::document_space_of(gcm::u32 shell_space) const noexcept {
    for (const auto& sk : sketches_)
        if (sk.space_id == shell_space) return plane_space(shell_space);
    for (const auto& l : layouts_)
        if (l.space_id == shell_space) return sheet_space(shell_space);
    return gdoc::kNoSpace;
}

std::vector<gsnap::Shape> Shell::shapes_from_document(gcm::u32 shell_space) const {
    const gdoc::SpaceId space = document_space_of(shell_space);
    if (space == gdoc::kNoSpace) return {};
    std::vector<std::pair<gcm::u32, gsnap::Shape>> keyed;
    for (const auto kind : kDrawnKinds)
        for (const auto id : doc_.of_kind(kind)) {
            const gdoc::Entity* e = doc_.find(id);
            if (!e || e->space != space) continue;
            gsnap::Shape sh;
            if (!decode_shape(*e, gcm::u32(id.lo), sh)) continue;
            keyed.emplace_back(gcm::u32(id.lo), std::move(sh));
        }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<gsnap::Shape> out;
    out.reserve(keyed.size());
    for (auto& [key, sh] : keyed) out.push_back(std::move(sh));
    return out;
}

std::vector<Canvas::TextItem> Shell::notes_from_document(gcm::u32 shell_space) const {
    const gdoc::SpaceId space = document_space_of(shell_space);
    if (space == gdoc::kNoSpace) return {};
    std::vector<std::pair<gcm::u64, Canvas::TextItem>> keyed;
    for (const auto id : doc_.of_kind(gdoc::Kind::Text)) {
        const gdoc::Entity* e = doc_.find(id);
        if (!e || e->space != space || e->payload.points.empty() ||
            e->payload.reals.size() < 3)
            continue;
        Canvas::TextItem t;
        t.pos = {e->payload.points[0].x, e->payload.points[0].y};
        t.text = e->payload.text;
        t.height = e->payload.reals[0];
        t.angle = e->payload.reals[1];
        t.width = e->payload.reals[2];
        if (e->payload.ints.size() >= 2) {
            t.halign = static_cast<int>(e->payload.ints[0]);
            t.valign = static_cast<int>(e->payload.ints[1]);
        }
        if (const auto it = e->attrs.user.find("style"); it != e->attrs.user.end())
            t.style = it->second;
        if (const auto it = e->attrs.user.find("field"); it != e->attrs.user.end())
            t.field = it->second;
        t.selection_id = id.lo;
        t.layer = e->layer;
        t.attrs=e->attrs;
        keyed.emplace_back(id.lo, std::move(t));
    }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Canvas::TextItem> out;
    out.reserve(keyed.size());
    for (auto& [key, t] : keyed) out.push_back(std::move(t));
    return out;
}

std::vector<gdoc::BlockRef> Shell::inserts_from_document(gcm::u32 shell_space) const {
    const gdoc::SpaceId space = document_space_of(shell_space);
    if (space == gdoc::kNoSpace) return {};
    std::vector<std::pair<gcm::u64, gdoc::BlockRef>> keyed;
    for (const auto id : doc_.of_kind(gdoc::Kind::BlockInstance)) {
        const gdoc::Entity* e = doc_.find(id);
        if (!e || e->space != space || e->payload.blob.empty()) continue;
        gdoc::BlockRef r;
        if (!from_blob(e->payload.blob, r)) continue;
        keyed.emplace_back(id.lo, std::move(r));
    }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<gdoc::BlockRef> out;
    out.reserve(keyed.size());
    for (auto& [key, r] : keyed) out.push_back(std::move(r));
    return out;
}

std::vector<Canvas::StoredConstraint> Shell::constraints_from_document(gcm::u32 shell_space) const {
    const gdoc::SpaceId space = document_space_of(shell_space);
    if (space == gdoc::kNoSpace) return {};
    std::vector<std::pair<gcm::u64, Canvas::StoredConstraint>> keyed;
    for (const auto id : doc_.of_kind(gdoc::Kind::Constraint)) {
        const gdoc::Entity* e = doc_.find(id);
        if (!e || e->space != space || e->payload.ints.size() < 2 ||
            e->payload.reals.empty())
            continue;
        Canvas::StoredConstraint k;
        k.kind = gsolve::Kind(e->payload.ints[0]);
        k.driving = e->payload.ints[1] != 0;
        k.value = e->payload.reals[0];
        if (!e->payload.refs.empty()) k.a = gcm::u32(e->payload.refs[0].lo);
        if (e->payload.refs.size() > 1) k.b = gcm::u32(e->payload.refs[1].lo);
        keyed.emplace_back(id.lo, k);
    }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Canvas::StoredConstraint> out;
    out.reserve(keyed.size());
    for (auto& [key, k] : keyed) out.push_back(k);
    return out;
}

std::vector<gdim::Dimension> Shell::dimensions_from_document(gcm::u32 shell_space) const {
    const gdoc::SpaceId space = document_space_of(shell_space);
    if (space == gdoc::kNoSpace) return {};
    std::vector<std::pair<gcm::u64, gdim::Dimension>> keyed;
    for (const auto id : doc_.of_kind(gdoc::Kind::Dimension)) {
        const gdoc::Entity* e = doc_.find(id);
        if (!e || e->space != space || e->payload.blob.empty()) continue;
        gdim::Dimension d;
        if (!from_blob(e->payload.blob, d)) continue;
        keyed.emplace_back(id.lo, std::move(d));
    }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<gdim::Dimension> out;
    out.reserve(keyed.size());
    for (auto& [key, d] : keyed) out.push_back(std::move(d));
    return out;
}

std::vector<gdoc::AssociativeHatch> Shell::hatches_from_document(gcm::u32 shell_space) const {
    const gdoc::SpaceId space = document_space_of(shell_space);
    if (space == gdoc::kNoSpace) return {};
    std::vector<std::pair<gcm::u64, gdoc::AssociativeHatch>> keyed;
    for (const auto id : doc_.of_kind(gdoc::Kind::Hatch)) {
        const gdoc::Entity* e = doc_.find(id);
        if (!e || e->space != space || e->payload.blob.empty()) continue;
        gdoc::AssociativeHatch h;
        if (!from_blob(e->payload.blob, h)) continue;
        h.layer=e->layer;h.attrs=e->attrs;
        if(const auto marker=e->attrs.user.find("area_background");marker!=e->attrs.user.end()&&marker->second=="1") {
            h.area_background=true;
            if(e->payload.reals.size()>2)h.gap_tolerance=e->payload.reals[2];
            if(e->payload.ints.size()>4)h.source_keys.assign(e->payload.ints.begin()+4,e->payload.ints.end());
        }
        keyed.emplace_back(id.lo, std::move(h));
    }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<gdoc::AssociativeHatch> out;
    out.reserve(keyed.size());
    for (auto& [key, h] : keyed) out.push_back(std::move(h));
    return out;
}

std::vector<ggeo::NurbsCurve2<Scalar>> Shell::splines_from_document(gcm::u32 shell_space) const {
    const gdoc::SpaceId space = document_space_of(shell_space);
    if (space == gdoc::kNoSpace) return {};
    std::vector<std::pair<gcm::u64, ggeo::NurbsCurve2<Scalar>>> keyed;
    for (const auto id : doc_.of_kind(gdoc::Kind::Spline)) {
        const gdoc::Entity* e = doc_.find(id);
        if (!e || e->space != space || e->payload.ints.size() < 2) continue;
        const std::size_t n = e->payload.ints[1];
        if (e->payload.reals.size() < n * 3) continue;
        ggeo::NurbsCurve2<Scalar> c;
        c.degree = int(e->payload.ints[0]);
        c.control.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            ggeo::ControlPoint2<Scalar> cp;
            cp.p = ggeo::d2::Point<Scalar>::from(e->payload.reals[i * 3],
                                                 e->payload.reals[i * 3 + 1]);
            cp.w = Scalar::from(e->payload.reals[i * 3 + 2]);
            c.control.push_back(cp);
        }
        c.knots.knots.assign(e->payload.reals.begin() + std::ptrdiff_t(n * 3),
                             e->payload.reals.end());
        keyed.emplace_back(id.lo, std::move(c));
    }
    std::sort(keyed.begin(), keyed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<ggeo::NurbsCurve2<Scalar>> out;
    out.reserve(keyed.size());
    for (auto& [key, c] : keyed) out.push_back(std::move(c));
    return out;
}

void Shell::restore_sketches_from_document() {
    std::vector<std::pair<gcm::u32, const gdoc::Entity*>> list;
    for (const auto id : doc_.of_kind(gdoc::Kind::Sketch)) {
        const gdoc::Entity* e = doc_.find(id);
        if (!e) continue;
        const gcm::u32 space = static_cast<gcm::u32>(e->id.lo) - 1;
        list.emplace_back(space, e);
    }
    std::sort(list.begin(), list.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<SketchDoc> next;
    next.reserve(list.size());
    for (const auto& [space, e] : list) {
        SketchDoc sk;
        for (const auto& old : sketches_)
            if (old.space_id == space) { sk = old; break; }
        sk.space_id = space;
        if (const gdoc::Space* sp = doc_.space(plane_space(space)))
            sk.plane = plane_of(sp->frame);
        sk.name = e->payload.text;
        sk.on_face = !e->payload.ints.empty() && e->payload.ints[0] != 0;
        sk.visibility = e->payload.ints.size()>1 && e->payload.ints[1]<=2
            ? SketchVisibility(e->payload.ints[1]) : SketchVisibility::Automatic;
        sk.parent_body_key = e->payload.ints.size()>2 ? e->payload.ints[2] : 0;
        if (e->payload.ints.size()>4) {
            sk.body=e->payload.ints[3];sk.face=e->payload.ints[4];
        }
        sk.layer = e->layer;
        next.push_back(std::move(sk));
    }
    sketches_ = std::move(next);
    if (active_sketch_ < 0 || std::size_t(active_sketch_) >= sketches_.size())
        active_sketch_ = sketches_.empty() ? 0 : int(sketches_.size()) - 1;
}

void Shell::apply_document(gdoc::SpaceId space) {
    // Solids first: the settings restore validates against the restored bodies.
    if (const gdoc::Entity* e = doc_.find(gdoc::EntityId{kSolidsTag, 1})) {
        std::vector<gtopo::Body> bodies;
        if (decode_solids(e->payload.blob, bodies)) {
            const std::size_t count = bodies.size();
            modeller_.set_bodies(std::move(bodies));
            const std::vector<gcm::u8> hist(e->payload.text.begin(), e->payload.text.end());
            modeller_.set_history(decode_feature_history(hist, count));
        }
    } else if (!modeller_.bodies().empty()) {
        modeller_.set_bodies({});
        modeller_.set_history({});
    }

    restore_sketches_from_document();

    if (const gdoc::Entity* e = doc_.find(gdoc::EntityId{kSettingsTag, 1}))
        if (!e->payload.blob.empty() && !restore_metadata(e->payload.blob))
            log_.push_back("could not restore document settings");

    // Switch to the edit's space before loading geometry.
    if (space != gdoc::kNoSpace && space != active_space_id())
        activate_space(space);

    const gcm::u32 cur_space = active_space_id();
    if (document_space_of(cur_space) == gdoc::kNoSpace) return;

    canvas_.load_shapes(shapes_from_document(cur_space), /*clear_history=*/true);
    canvas_.load_texts(notes_from_document(cur_space));
    canvas_.load_dimensions(dimensions_from_document(cur_space));
    canvas_.load_splines(splines_from_document(cur_space));
    canvas_.load_constraints(constraints_from_document(cur_space));
    canvas_.load_hatches(hatches_from_document(cur_space));
    canvas_.blocks().set_references(inserts_from_document(cur_space));

    canvas_.refresh_hatches();
    canvas_.refresh_dimensions();

    const auto adopt_space = [this](gcm::u32 shell_space) {
        if (document_space_of(shell_space) == gdoc::kNoSpace) return;
        if (auto it = space_states_.find(shell_space); it != space_states_.end()) {
            auto& c = it->second.content;
            c.dims = dimensions_from_document(shell_space);
            c.refs = inserts_from_document(shell_space);
            c.hatches = hatches_from_document(shell_space);
            c.splines = splines_from_document(shell_space);
            c.constraints = constraints_from_document(shell_space);
            c.texts = notes_from_document(shell_space);
        }
    };
    for (std::size_t i = 0; i < sketches_.size(); ++i) {
        sketches_[i].texts = document_space_of(sketches_[i].space_id) != gdoc::kNoSpace
            ? notes_from_document(sketches_[i].space_id) : std::vector<Canvas::TextItem>{};
        adopt_space(sketches_[i].space_id);
    }
    for (std::size_t i = 0; i < layouts_.size(); ++i) {
        layouts_[i].notes = document_space_of(layouts_[i].space_id) != gdoc::kNoSpace
            ? notes_from_document(layouts_[i].space_id) : std::vector<Canvas::TextItem>{};
        adopt_space(layouts_[i].space_id);
    }

    mirrored_ = mirror_key();
    rebuild_sketch_segments();
    mesh_dirty_ = true;
}

bool Shell::undo_through_document(bool redo) {
    // Flush pending edits first, or the first undo steps over them.
    sync_document();
    if (redo ? !doc_.can_redo() : !doc_.can_undo()) return false;

    restoring_history_ = true;
    const bool moved = redo ? doc_.redo() : doc_.undo();
    if (moved) {
        auto& from = redo ? redo_journal_ : undo_journal_;
        auto& to = redo ? undo_journal_ : redo_journal_;
        if (!from.empty()) { to.push_back(from.back()); from.pop_back(); }
        apply_document(doc_.last_applied_space());
    }
    restoring_history_ = false;

    seen_canvas_ = canvas_.edit_serial();
    seen_canvas_marks_ = canvas_.undo_mark_serial();
    seen_solid_ = modeller_.edit_serial();
    seen_metadata_ = metadata_bytes();
    seen_lightweight_metadata_ = metadata_bytes(true);
    seen_block_generation_ = cached_block_generation_;
    return moved;
}

}
