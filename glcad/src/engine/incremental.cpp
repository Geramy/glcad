#include <algorithm>
#include <cstring>

#include "engine/document_file.hpp"
#include "engine/state_archive.hpp"

namespace gfile {

namespace {

bool same_shape(const gsnap::Shape& a, const gsnap::Shape& b) {
    return a.kind == b.kind && a.a.x == b.a.x && a.a.y == b.a.y &&
           a.b.x == b.b.x && a.b.y == b.b.y && a.radius == b.radius &&
           a.start_angle == b.start_angle && a.end_angle == b.end_angle &&
           a.key == b.key && a.poly == b.poly && a.poly_index == b.poly_index &&
           a.region == b.region && a.region_loop == b.region_loop && a.region_reversed == b.region_reversed &&
           a.layer == b.layer && a.attrs.color == b.attrs.color &&
           a.attrs.linetype == b.attrs.linetype && a.attrs.lineweight == b.attrs.lineweight &&
           a.attrs.linetype_scale == b.attrs.linetype_scale && a.attrs.transparency == b.attrs.transparency &&
           a.attrs.visible == b.attrs.visible && a.attrs.user == b.attrs.user &&
           a.attrs.foreign_format == b.attrs.foreign_format && a.attrs.foreign_blob == b.attrs.foreign_blob;
}

bool same_entry(const Contents::SpaceGeometry& a, std::size_t ai,
                const Contents::SpaceGeometry& b, std::size_t bi) {
    if (!same_shape(a.shapes[ai], b.shapes[bi])) return false;
    const std::string& la = ai < a.layers.size() ? a.layers[ai] : std::string();
    const std::string& lb = bi < b.layers.size() ? b.layers[bi] : std::string();
    return la == lb;
}

bool same_layer(const gdoc::Layer& a, const gdoc::Layer& b) {
    return a.name == b.name && a.on == b.on && a.frozen == b.frozen &&
           a.locked == b.locked && a.plottable == b.plottable &&
           a.color.kind == b.color.kind && a.color.index == b.color.index &&
           a.color.r == b.color.r && a.color.g == b.color.g &&
           a.color.b == b.color.b && a.linetype == b.linetype &&
           a.lineweight == b.lineweight && a.transparency == b.transparency &&
           a.plot_style == b.plot_style && a.vp_frozen == b.vp_frozen;
}

struct Sink {
    std::vector<u8> bytes;

    void u32v(u32 v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void u64v(u64 v) {
        for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void i64v(i64 v) { u64v(static_cast<u64>(v)); }
    void f64v(f64 v) {
        u64 bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u64v(bits);
    }
    void str(const std::string& s) {
        u32v(static_cast<u32>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
};

struct Source {
    const u8* p{nullptr};
    std::size_t left{0};
    bool ok{true};

    u32 u32v() {
        if (left < 4) { ok = false; return 0; }
        u32 v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<u32>(p[i]) << (8 * i);
        p += 4;
        left -= 4;
        return v;
    }
    u64 u64v() {
        if (left < 8) { ok = false; return 0; }
        u64 v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<u64>(p[i]) << (8 * i);
        p += 8;
        left -= 8;
        return v;
    }
    i64 i64v() { return static_cast<i64>(u64v()); }
    f64 f64v() {
        const u64 bits = u64v();
        f64 v = 0.0;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    std::string str() {
        const u32 n = u32v();
        if (!ok || n > left) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        left -= n;
        return s;
    }
};

}

namespace {
Contents metadata_of(const Contents& c) {
    Contents m;
    m.volume = c.volume; m.units = c.units; m.precision = c.precision; m.created_by = c.created_by;
    m.layers = c.layers; m.linetypes = c.linetypes; m.multilines = c.multilines; m.blocks = c.blocks;
    m.spaces = c.spaces; m.current_space = c.current_space; m.entities = c.entities; m.foreign = c.foreign;
    return m;
}
std::vector<u8> document_bytes(const Contents& c) {
    gio::MemoryWriter out;
    write_document(out, c);
    return out.bytes();
}
}

Delta diff(const Contents& before, const Contents& after) {
    Delta d;
    auto old_metadata = metadata_of(before), new_metadata = metadata_of(after);
    if (document_bytes(old_metadata) != document_bytes(new_metadata)) d.metadata = std::move(new_metadata);
    std::vector<u32> old_order, new_order;
    for (const auto& g : before.geometry) old_order.push_back(g.space);
    for (const auto& g : after.geometry) new_order.push_back(g.space);
    if (old_order != new_order) d.geometry_order = std::move(new_order);

    static const Contents::SpaceGeometry kEmpty;

    for (const auto& a : after.geometry) {
        const Contents::SpaceGeometry* b = &kEmpty;
        for (const auto& x : before.geometry)
            if (x.space == a.space) b = &x;

        const std::size_t bn = b->shapes.size(), an = a.shapes.size();
        std::size_t head = 0;
        while (head < bn && head < an && same_entry(*b, head, a, head)) ++head;

        std::size_t tail = 0;
        while (tail < bn - head && tail < an - head &&
               same_entry(*b, bn - 1 - tail, a, an - 1 - tail))
            ++tail;

        SpaceDelta sd;
        sd.space = a.space;
        sd.at = static_cast<u32>(head);
        sd.removed = static_cast<u32>(bn - head - tail);
        for (std::size_t i = head; i < an - tail; ++i) {
            sd.inserted.push_back(a.shapes[i]);
            sd.layers.push_back(i < a.layers.size() ? a.layers[i] : "0");
        }
        if (!sd.empty()) d.spaces.push_back(std::move(sd));
    }

    for (const auto& l : after.layers.all()) {
        const gdoc::Layer* old = before.layers.find(l.name);
        if (!old || !same_layer(*old, l)) d.changed_layers.push_back(l);
    }
    for (const auto& l : before.layers.all())
        if (!after.layers.find(l.name)) d.removed_layers.push_back(l.name);

    return d;
}

void apply(Contents& c, const Delta& d) {
    if (d.metadata) {
        auto geometry = std::move(c.geometry);
        c = *d.metadata;
        c.geometry = std::move(geometry);
    }
    for (const auto& sp : d.spaces) {
        Contents::SpaceGeometry* target = nullptr;
        for (auto& x : c.geometry)
            if (x.space == sp.space) target = &x;
        if (!target) {
            c.geometry.push_back({sp.space, {}, {}});
            target = &c.geometry.back();
        }
        target->layers.resize(target->shapes.size(), "0");

        if (sp.at > target->shapes.size()) continue;
        const std::size_t end =
            std::min<std::size_t>(sp.at + sp.removed, target->shapes.size());

        target->shapes.erase(target->shapes.begin() + sp.at,
                             target->shapes.begin() + static_cast<long>(end));
        target->layers.erase(target->layers.begin() + sp.at,
                             target->layers.begin() + static_cast<long>(end));
        target->shapes.insert(target->shapes.begin() + sp.at,
                              sp.inserted.begin(), sp.inserted.end());
        target->layers.insert(target->layers.begin() + sp.at, sp.layers.begin(),
                              sp.layers.end());
    }
    for (const auto& l : d.changed_layers) c.layers.add(l);
    for (const auto& name : d.removed_layers) c.layers.remove(name);
    if (d.geometry_order) {
        std::vector<Contents::SpaceGeometry> geometry;
        for (const auto space : *d.geometry_order) {
            const auto it = std::find_if(c.geometry.begin(), c.geometry.end(),
                                        [&](const auto& g) { return g.space == space; });
            if (it != c.geometry.end()) geometry.push_back(std::move(*it));
            else geometry.push_back({space, {}, {}});
        }
        c.geometry = std::move(geometry);
    }
}

std::vector<u8> encode_delta(const Delta& d) {
    Sink header; header.u32v(2); header.u32v(d.sequence);
    state::Writer out;
    Contents changes;
    std::vector<u32> headers;
    for (const auto& sp : d.spaces) {
        headers.insert(headers.end(), {sp.space, sp.at, sp.removed});
        changes.geometry.push_back({static_cast<u32>(changes.geometry.size()), sp.inserted, sp.layers});
    }
    std::vector<std::string> layer_names;
    for (const auto& layer : d.changed_layers) { changes.layers.add(layer); layer_names.push_back(layer.name); }
    const auto meta = d.metadata ? document_bytes(*d.metadata) : std::vector<u8>{};
    out(headers, encode(changes), layer_names, d.removed_layers, meta,
        d.geometry_order.has_value(), d.geometry_order.value_or(std::vector<u32>{}));
    header.bytes.insert(header.bytes.end(), out.bytes.begin(), out.bytes.end());
    return header.bytes;
}

Error decode_delta(const u8* data, std::size_t n, Delta& d) {
    Source src{data, n, true};
    const u32 version = src.u32v();
    if (src.ok && version == 2) {
        try {
            Delta result; result.sequence = src.u32v();
            if (!src.ok) return Error::fail("truncated delta");
            const std::vector<u8> payload(src.p, src.p + src.left);
            state::Reader in{payload};
            std::vector<u32> headers, order;
            std::vector<u8> changes_bytes, meta;
            std::vector<std::string> layer_names;
            bool has_order;
            in(headers, changes_bytes, layer_names, result.removed_layers, meta, has_order, order);
            in.finish();
            Contents changes;
            auto error = decode(changes_bytes, changes);
            if (!error.ok) return error;
            if (headers.size() % 3 || changes.geometry.size() != headers.size() / 3)
                return Error::fail("invalid delta splice records");
            for (std::size_t i = 0; i < changes.geometry.size(); ++i) {
                auto& g = changes.geometry[i];
                result.spaces.push_back({headers[i * 3], headers[i * 3 + 1], headers[i * 3 + 2],
                                         std::move(g.shapes), std::move(g.layers)});
            }
            for (const auto& name : layer_names) {
                const auto* l = changes.layers.find(name);
                if (!l) return Error::fail("missing delta layer");
                result.changed_layers.push_back(*l);
            }
            if (!meta.empty()) {
                gio::MemoryReader stream(meta);
                Contents c; error = read_document(stream, c);
                if (!error.ok) return error;
                result.metadata = std::move(c);
            }
            if (has_order) result.geometry_order = std::move(order);
            d = std::move(result);
            return Error::good();
        } catch (const std::exception& e) { return Error::fail(e.what()); }
    }
    if (!src.ok || version != 1) return Error::fail("unknown delta version");
    d = Delta{};
    d.sequence = src.u32v();

    const u32 spaces = src.u32v();
    if (!src.ok || spaces > 1u << 20) return Error::fail("implausible delta");
    for (u32 i = 0; i < spaces && src.ok; ++i) {
        SpaceDelta sp;
        sp.space = src.u32v();
        sp.at = src.u32v();
        sp.removed = src.u32v();
        const u32 shapes = src.u32v();
        if (!src.ok || shapes > 1u << 26) return Error::fail("implausible delta");
        for (u32 k = 0; k < shapes && src.ok; ++k) {
            gsnap::Shape sh;
            sh.kind = static_cast<gsnap::ShapeKind>(src.u32v());
            sh.a.x = src.i64v();
            sh.a.y = src.i64v();
            sh.b.x = src.i64v();
            sh.b.y = src.i64v();
            sh.radius = src.i64v();
            sh.start_angle = src.f64v();
            sh.end_angle = src.f64v();
            sp.inserted.push_back(sh);
        }
        const u32 layers = src.u32v();
        for (u32 k = 0; k < layers && src.ok; ++k) sp.layers.push_back(src.str());
        sp.layers.resize(sp.inserted.size(), "0");
        d.spaces.push_back(std::move(sp));
    }

    const u32 changed = src.u32v();
    if (!src.ok || changed > 1u << 20) return Error::fail("implausible delta");
    for (u32 i = 0; i < changed && src.ok; ++i) {
        gdoc::Layer l;
        l.name = src.str();
        l.color.kind = static_cast<gdoc::Color::Kind>(src.u32v());
        l.color.index = static_cast<gcm::i32>(src.u32v());
        l.lineweight = static_cast<gdoc::Lineweight>(static_cast<gcm::i32>(src.u32v()));
        l.transparency = static_cast<gcm::i32>(src.u32v());
        l.linetype = src.str();
        l.plot_style = src.str();
        const u32 flags = src.u32v();
        l.on = (flags & 1u) != 0;
        l.frozen = (flags & 2u) != 0;
        l.locked = (flags & 4u) != 0;
        l.plottable = (flags & 8u) != 0;
        d.changed_layers.push_back(std::move(l));
    }

    const u32 removed = src.u32v();
    if (!src.ok || removed > 1u << 20) return Error::fail("implausible delta");
    for (u32 i = 0; i < removed && src.ok; ++i) d.removed_layers.push_back(src.str());

    if (!src.ok) return Error::fail("the delta ended early");
    return Error::good();
}

Error append_delta(const std::string& path, const Delta& d, u64* new_size) {
    if (d.empty()) {
        if (new_size) *new_size = 0;
        return Error::good();
    }

    gio::FileReader probe;
    std::vector<u8> existing;
    if (probe.open(path).ok) {
        probe.read_all(existing);
        probe.close();
    }
    if (existing.empty()) return Error::fail("there is no document to append to");

    gio::MemoryWriter w;
    if (!gio::write_frame(w, gio::FrameKind::Delta, encode_delta(d)))
        return Error::fail("could not encode the delta");

    existing.insert(existing.end(), w.bytes().begin(), w.bytes().end());

    gio::FileWriter out;
    const gio::Status s = out.open(path);
    if (!s.ok) return Error::fail(s.message);
    out.write_all(existing);
    const gio::Status closed = out.close();
    if (!closed.ok) return Error::fail(closed.message);

    if (new_size) *new_size = existing.size();
    return Error::good();
}

u32 TransactionLog::record(std::string_view command, std::string_view detail,
                           i64 timestamp) {
    Transaction t;
    t.sequence = next_++;
    t.command = std::string(command);
    t.detail = std::string(detail);
    t.timestamp = timestamp;
    entries_.push_back(std::move(t));
    return entries_.back().sequence;
}

u32 TransactionLog::record(const Delta& d, std::string_view command,
                           i64 timestamp) {
    const u32 seq = record(command, "", timestamp);
    entries_.back().payload = encode_delta(d);
    return seq;
}

Error TransactionLog::write(gio::Writer& w) const {
    Sink s;
    s.u32v(1);
    s.u32v(static_cast<u32>(entries_.size()));
    for (const Transaction& t : entries_) {
        s.u32v(t.sequence);
        s.str(t.command);
        s.str(t.detail);
        s.i64v(t.timestamp);
        s.u32v(static_cast<u32>(t.payload.size()));
        s.bytes.insert(s.bytes.end(), t.payload.begin(), t.payload.end());
    }
    if (!gio::write_frame(w, gio::FrameKind::Transaction, s.bytes))
        return Error::fail("could not write the transaction log");
    return Error::good();
}

Error TransactionLog::read(gio::Reader& r) {
    std::vector<u8> payload;
    for (;;) {
        const auto res = gio::read_frame(r, payload);
        if (res.end_of_stream) break;
        if (!res.ok) return Error::fail(res.error);
        if (res.header.kind != gio::FrameKind::Transaction) continue;

        Source src{payload.data(), payload.size(), true};
        if (src.u32v() != 1) return Error::fail("unknown log version");
        const u32 count = src.u32v();
        if (!src.ok || count > 1u << 24) return Error::fail("implausible log");

        for (u32 i = 0; i < count && src.ok; ++i) {
            Transaction t;
            t.sequence = src.u32v();
            t.command = src.str();
            t.detail = src.str();
            t.timestamp = src.i64v();
            const u32 n = src.u32v();
            if (!src.ok || n > src.left) return Error::fail("the log ended early");
            t.payload.assign(src.p, src.p + n);
            src.p += n;
            src.left -= n;
            next_ = std::max(next_, t.sequence + 1);
            entries_.push_back(std::move(t));
        }
        if (!src.ok) return Error::fail("the log ended early");
    }
    return Error::good();
}

Error TransactionLog::append_to(const std::string& path) const {
    gio::FileReader probe;
    std::vector<u8> existing;
    if (probe.open(path).ok) {
        probe.read_all(existing);
        probe.close();
    }

    gio::MemoryWriter w;
    const Error e = write(w);
    if (!e.ok) return e;
    existing.insert(existing.end(), w.bytes().begin(), w.bytes().end());

    gio::FileWriter out;
    const gio::Status s = out.open(path);
    if (!s.ok) return Error::fail(s.message);
    out.write_all(existing);
    const gio::Status closed = out.close();
    return closed.ok ? Error::good() : Error::fail(closed.message);
}

std::size_t TransactionLog::replay(Contents& c, u32 through) const {
    std::size_t applied = 0;
    for (const Transaction& t : entries_) {
        if (t.sequence > through) break;
        if (t.payload.empty()) continue;
        Delta d;
        if (!decode_delta(t.payload.data(), t.payload.size(), d).ok) continue;
        apply(c, d);
        ++applied;
    }
    return applied;
}

bool TessellationCache::valid_for(std::size_t index, u64 hash) const {
    return index < entries.size() && entries[index].source_hash == hash;
}

void TessellationCache::invalidate(u32 space, u32 shape) {
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const Entry& e) {
                                     return e.space == space && e.shape == shape;
                                 }),
                  entries.end());
}

std::vector<u8> encode_cache(const TessellationCache& c) {
    Sink s;
    s.u32v(1);
    s.u32v(static_cast<u32>(c.entries.size()));
    for (const auto& e : c.entries) {
        s.u32v(e.space);
        s.u32v(e.shape);
        s.u64v(e.source_hash);
        s.u32v(static_cast<u32>(e.points.size()));
        for (f64 v : e.points) s.f64v(v);
    }
    return s.bytes;
}

Error decode_cache(const u8* data, std::size_t n, TessellationCache& c) {
    Source src{data, n, true};
    if (src.u32v() != 1) return Error::fail("unknown cache version");
    const u32 count = src.u32v();
    if (!src.ok || count > 1u << 24) return Error::fail("implausible cache");

    for (u32 i = 0; i < count && src.ok; ++i) {
        TessellationCache::Entry e;
        e.space = src.u32v();
        e.shape = src.u32v();
        e.source_hash = src.u64v();
        const u32 pts = src.u32v();
        if (!src.ok || pts > (1u << 28)) return Error::fail("implausible cache");
        e.points.reserve(pts);
        for (u32 k = 0; k < pts && src.ok; ++k) e.points.push_back(src.f64v());
        c.entries.push_back(std::move(e));
    }
    return src.ok ? Error::good() : Error::fail("the cache ended early");
}

gdoc::BlockTable template_block_definitions(const gdoc::BlockTable& blocks) {
    const auto project_block = [&](std::string_view name) {
        const auto* def = blocks.find(name);
        return (def && def->is_xref) || blocks.xref(name) != nullptr;
    };
    gdoc::BlockTable standards;
    for (auto def : blocks.all()) {
        if (project_block(def.name)) continue;
        def.nested.erase(std::remove_if(def.nested.begin(), def.nested.end(), project_block), def.nested.end());
        std::erase_if(def.nested_refs,[&](const auto& ref){return project_block(ref.name);});
        standards.define(std::move(def));
    }
    std::vector<gdoc::DynamicBlock> dynamics;
    for (const auto& d : blocks.dynamics())
        if (standards.find(d.name)) dynamics.push_back(d);
    standards.load_extensions(std::move(dynamics), {});
    return standards;
}

static Contents template_content(const Contents& c) {
    Contents t = c;
    const auto sheet = [&](gdoc::SpaceId id) {
        return std::any_of(t.spaces.begin(), t.spaces.end(), [&](const auto& s) {
            return s.id == id && s.kind == gdoc::SpaceKind::Sheet;
        });
    };
    t.geometry.erase(std::remove_if(t.geometry.begin(), t.geometry.end(),
        [&](const auto& g) { return !sheet(g.space); }), t.geometry.end());
    t.entities.erase(std::remove_if(t.entities.begin(), t.entities.end(),
        [&](const auto& e) { return !sheet(e.space); }), t.entities.end());
    t.blocks = template_block_definitions(c.blocks);
    t.foreign.clear();
    return t;
}

Error save_template(const std::string& path, const Contents& c) {
    return save_file(path, template_content(c));
}

Error load_template(const std::string& path, Contents& c) {
    const Error e = load_file(path, c);
    if (!e.ok) return e;
    c = template_content(c);
    return Error::good();
}

Error save_package(const std::string& path, const Package& p) {
    gio::FileWriter out;
    const gio::Status s = out.open(path);
    if (!s.ok) return Error::fail(s.message);

    const Error e = write_document(out, p.document);
    if (!e.ok) return e;

    Sink m;
    m.u32v(1);
    m.str(p.note);
    for (const auto* list : {&p.fonts, &p.xrefs, &p.images}) {
        m.u32v(static_cast<u32>(list->size()));
        for (const auto& n : *list) m.str(n);
    }
    if (!gio::write_frame(out, gio::FrameKind::Manifest, m.bytes))
        return Error::fail("could not write the package manifest");

    const gio::Status closed = out.close();
    return closed.ok ? Error::good() : Error::fail(closed.message);
}

Error load_package(const std::string& path, Package& p) {
    gio::FileReader in;
    const gio::Status s = in.open(path);
    if (!s.ok) return Error::fail(s.message);

    std::vector<u8> all;
    in.read_all(all);
    in.close();

    gio::MemoryReader doc(all);
    const Error e = read_document(doc, p.document);
    if (!e.ok) return e;

    for (const auto& f : p.document.foreign) {
        if (f.kind != static_cast<u16>(gio::FrameKind::Manifest)) continue;
        Source src{f.payload.data(), f.payload.size(), true};
        if (src.u32v() != 1) continue;
        p.note = src.str();
        for (auto* list : {&p.fonts, &p.xrefs, &p.images}) {
            const u32 n = src.u32v();
            if (!src.ok || n > 1u << 20) break;
            for (u32 i = 0; i < n && src.ok; ++i) list->push_back(src.str());
        }
    }
    return Error::good();
}

}
