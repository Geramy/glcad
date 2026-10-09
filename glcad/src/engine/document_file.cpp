#include "engine/block_state.hpp"
#include "engine/document_file.hpp"
#include "geometry/region.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "glcad_generated.h"

namespace gfile {

namespace fb = glcad::fb;

namespace {

fb::LatticePoint lat(gcm::Lattice2 p) { return fb::LatticePoint(p.x, p.y); }
// Structs in tables arrive as nullable pointers; nested structs by reference.
gcm::Lattice2    lat(const fb::LatticePoint* p) {
    return p ? gcm::Lattice2{p->x(), p->y()} : gcm::Lattice2{};
}
gcm::Lattice2    lat(const fb::LatticePoint& p) { return {p.x(), p.y()}; }

fb::Color col(const gdoc::Color& c) {
    return fb::Color(static_cast<fb::ColorKind>(c.kind), c.index, c.r, c.g, c.b);
}
gdoc::Color col(const fb::Color* c) {
    if (!c) return gdoc::Color::aci(7);
    gdoc::Color out;
    out.kind  = static_cast<gdoc::Color::Kind>(c->kind());
    out.index = c->index();
    out.r = c->r();
    out.g = c->g();
    out.b = c->b();
    return out;
}

fb::Shape shape(const gsnap::Shape& s) {
    return fb::Shape(static_cast<fb::ShapeKind>(s.kind), lat(s.a), lat(s.b),
                     s.radius, s.start_angle, s.end_angle);
}
gsnap::Shape shape(const fb::Shape* s) {
    gsnap::Shape out;
    if (!s) return out;
    out.kind        = static_cast<gsnap::ShapeKind>(s->kind());
    out.a           = lat(s->a());
    out.b           = lat(s->b());
    out.radius      = s->radius();
    out.start_angle = s->start_angle();
    out.end_angle   = s->end_angle();
    return out;
}

std::string str(const flatbuffers::String* s) { return s ? s->str() : std::string(); }

std::string str_or(const flatbuffers::String* s, const char* fallback) {
    return (s && s->size()) ? s->str() : std::string(fallback);
}

bool valid_note(const gdoc::BlockDef::Note& note) {
    return std::isfinite(note.height) && note.height >= 0 &&
           std::isfinite(note.width) && note.width >= 0 && std::isfinite(note.angle) &&
           note.halign >= 0 && note.halign <= 2 && note.valign >= 0 && note.valign <= 3;
}

flatbuffers::Offset<fb::EntityAttributes> write_attrs(flatbuffers::FlatBufferBuilder& b,
                                                      const gdoc::Attributes& at) {
    // Sorted so the same document always serialises to the same bytes.
    std::vector<std::string> keys, values;
    keys.reserve(at.user.size());
    for (const auto& [k, v] : at.user) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    for (const std::string& k : keys) values.push_back(at.user.at(k));

    const auto lt  = b.CreateString(at.linetype);
    const auto ff  = b.CreateString(at.foreign_format);
    const auto blb = b.CreateVector(at.foreign_blob);
    const auto ukv = keys.empty() ? 0 : b.CreateVectorOfStrings(keys);
    const auto uvv = values.empty() ? 0 : b.CreateVectorOfStrings(values);

    fb::EntityAttributesBuilder ab(b);
    ab.add_color_kind(static_cast<gcm::u8>(at.color.kind));
    ab.add_color_index(at.color.index);
    ab.add_color_r(at.color.r);
    ab.add_color_g(at.color.g);
    ab.add_color_b(at.color.b);
    ab.add_linetype(lt);
    ab.add_lineweight(static_cast<short>(at.lineweight));
    ab.add_linetype_scale(at.linetype_scale);
    ab.add_transparency(at.transparency);
    ab.add_visible(at.visible);
    if (ukv.o) ab.add_user_keys(ukv);
    if (uvv.o) ab.add_user_values(uvv);
    ab.add_foreign_format(ff);
    ab.add_foreign_blob(blb);
    return ab.Finish();
}

void read_attrs(const fb::EntityAttributes* a, gdoc::Attributes& out) {
    if (!a) return;
    out.color.kind  = static_cast<gdoc::Color::Kind>(a->color_kind());
    out.color.index = a->color_index();
    out.color.r = a->color_r();
    out.color.g = a->color_g();
    out.color.b = a->color_b();
    out.linetype       = str_or(a->linetype(), "ByLayer");
    out.lineweight     = static_cast<gdoc::Lineweight>(a->lineweight());
    out.linetype_scale = a->linetype_scale();
    out.transparency   = a->transparency();
    out.visible        = a->visible();
    out.foreign_format = str(a->foreign_format());
    if (const auto* blob = a->foreign_blob())
        out.foreign_blob.assign(blob->begin(), blob->end());
    const auto* ks = a->user_keys();
    const auto* vs = a->user_values();
    if (ks && vs) {
        const auto n = std::min(ks->size(), vs->size());
        for (flatbuffers::uoffset_t i = 0; i < n; ++i)
            out.user[ks->Get(i)->str()] = vs->Get(i)->str();
    }
}

flatbuffers::Offset<fb::SpaceGeometry> write_geometry(flatbuffers::FlatBufferBuilder& b, const Contents::SpaceGeometry& g) {
        std::vector<fb::Shape> shapes;
        shapes.reserve(g.shapes.size());
        for (const auto& s : g.shapes) shapes.push_back(shape(s));

        bool any_override = false;
        for (const auto& s2 : g.shapes)
            if (!s2.attrs.all_by_layer()) { any_override = true; break; }
        std::vector<flatbuffers::Offset<fb::EntityAttributes>> attrs;
        if (any_override) {
            attrs.reserve(g.shapes.size());
            for (const auto& s2 : g.shapes) attrs.push_back(write_attrs(b, s2.attrs));
        }

        std::vector<gcm::u32> polys, orders;
        for (const auto& s2 : g.shapes)
            if (s2.poly != 0) {
                polys.reserve(g.shapes.size());
                orders.reserve(g.shapes.size());
                for (const auto& s3 : g.shapes) {
                    polys.push_back(s3.poly);
                    orders.push_back(s3.poly_index);
                }
                break;
            }

        const auto sv = b.CreateVectorOfStructs(shapes);
        const auto lv = b.CreateVectorOfStrings(g.layers);
        std::vector<gcm::u32> keys;
        for (const auto& s2 : g.shapes)
            if (s2.key != 0) {
                keys.reserve(g.shapes.size());
                for (const auto& s3 : g.shapes) keys.push_back(s3.key);
                break;
            }

        const auto pv = polys.empty() ? 0 : b.CreateVector(polys).o;
        const auto ov = orders.empty() ? 0 : b.CreateVector(orders).o;
        const auto kv = keys.empty() ? 0 : b.CreateVector(keys).o;
        const auto av = attrs.empty() ? 0 : b.CreateVector(attrs).o;
        std::vector<gcm::u8> region_flags;
        std::vector<gcm::u32> region_loops;
        if (std::any_of(g.shapes.begin(), g.shapes.end(), [](const auto& sh) { return sh.region; })) {
            for (const auto& sh : g.shapes) {
                region_flags.push_back(sh.region ? (1 | (sh.region_reversed ? 2 : 0)) : 0);
                region_loops.push_back(sh.region ? sh.region_loop : 0);
            }
        }
        const auto rf = region_flags.empty() ? 0 : b.CreateVector(region_flags).o;
        const auto rl = region_loops.empty() ? 0 : b.CreateVector(region_loops).o;
        fb::SpaceGeometryBuilder gb(b);
        gb.add_space(g.space);
        if (rf) gb.add_region_flags(flatbuffers::Offset<flatbuffers::Vector<gcm::u8>>(rf));
        if (rl) gb.add_region_loops(flatbuffers::Offset<flatbuffers::Vector<gcm::u32>>(rl));
        gb.add_shapes(sv);
        gb.add_layers(lv);
        if (av) gb.add_attrs(flatbuffers::Offset<
            flatbuffers::Vector<flatbuffers::Offset<fb::EntityAttributes>>>(av));
        if (pv) gb.add_polys(flatbuffers::Offset<flatbuffers::Vector<gcm::u32>>(pv));
        if (ov) gb.add_poly_orders(flatbuffers::Offset<flatbuffers::Vector<gcm::u32>>(ov));
        if (kv) gb.add_keys(flatbuffers::Offset<flatbuffers::Vector<gcm::u32>>(kv));
        return gb.Finish();
}

Error read_geometry(const fb::SpaceGeometry* g, Contents::SpaceGeometry& out) {
            out.space = g->space();
            if (const auto* ss = g->shapes())
                for (const auto* s : *ss) out.shapes.push_back(shape(s));
            if (const auto* ls = g->layers())
                for (const auto* l : *ls) out.layers.push_back(l->str());
            out.layers.resize(out.shapes.size(), "0");
            if (const auto* pv2 = g->polys()) {
                const auto* ov2 = g->poly_orders();
                const auto n = std::min<std::size_t>(pv2->size(), out.shapes.size());
                for (std::size_t i = 0; i < n; ++i) {
                    out.shapes[i].poly = pv2->Get(flatbuffers::uoffset_t(i));
                    out.shapes[i].poly_index =
                        ov2 && i < ov2->size() ? ov2->Get(flatbuffers::uoffset_t(i)) : gcm::u32(i);
                }
            }
            if (g->region_flags() || g->region_loops()) {
                const auto* flags = g->region_flags();
                const auto* loops = g->region_loops();
                if (!flags || !loops || flags->size() != out.shapes.size() || loops->size() != out.shapes.size())
                    return Error::fail("invalid region topology vectors");
                for (std::size_t i = 0; i < out.shapes.size(); ++i) {
                    auto& sh = out.shapes[i];
                    const auto flag = flags->Get(i);
                    const auto loop = loops->Get(i);
                    if (flag > 3 || (flag == 2) || (!flag && loop) ||
                        (flag && (!sh.poly || loop >= 4096)))
                        return Error::fail("invalid region boundary membership");
                    sh.region = flag & 1;
                    sh.region_reversed = flag & 2;
                    sh.region_loop = loop;
                }
                std::unordered_map<gcm::u32, bool> kinds;
                for (const auto& sh : out.shapes) if (sh.poly) {
                    const auto [it, inserted] = kinds.emplace(sh.poly, sh.region);
                    if (!inserted && it->second != sh.region)
                        return Error::fail("mixed region and polyline membership");
                }
            }
            if (const auto* kv2 = g->keys()) {
                const auto n = std::min<std::size_t>(kv2->size(), out.shapes.size());
                for (std::size_t i = 0; i < n; ++i)
                    out.shapes[i].key = kv2->Get(flatbuffers::uoffset_t(i));
            }
            if (const auto* av = g->attrs()) {
                const auto n = std::min<std::size_t>(av->size(), out.shapes.size());
                for (std::size_t i = 0; i < n; ++i)
                    read_attrs(av->Get(flatbuffers::uoffset_t(i)), out.shapes[i].attrs);
            }
            return Error::good();
}

}

flatbuffers::Offset<fb::BlockRef> write_block_ref(flatbuffers::FlatBufferBuilder& b,const gdoc::BlockRef& r) {
        std::vector<flatbuffers::Offset<fb::AttributeValue>> vals;
        for (const auto& v : r.attributes) {
            const auto tag = b.CreateString(v.tag);
            const auto val = b.CreateString(v.value);
            fb::AttributeValueBuilder vb(b);
            vb.add_tag(tag);
            vb.add_value(val);
            vals.push_back(vb.Finish());
        }
        std::vector<flatbuffers::Offset<fb::BlockNote>> notes;
        for (const auto& note : r.notes) {
            const auto text = b.CreateString(note.text);
            const auto note_layer = b.CreateString(note.layer);
            const auto style = b.CreateString(note.style);
            const auto attrs = write_attrs(b, note.attrs);
            const auto position = lat(note.pos);
            fb::BlockNoteBuilder nb(b);
            nb.add_position(&position);
            nb.add_text(text);
            nb.add_height(note.height);
            nb.add_angle(note.angle);
            nb.add_layer(note_layer);
            nb.add_halign(note.halign);
            nb.add_valign(note.valign);
            nb.add_width(note.width);
            nb.add_style(style);
            nb.add_attrs(attrs);
            notes.push_back(nb.Finish());
        }
        const auto name  = b.CreateString(r.name);
        const auto layer = b.CreateString(r.layer);
        const auto vv    = b.CreateVector(vals);
        const auto nv    = b.CreateVector(notes);
        const auto appearance=write_attrs(b,r.attrs);
        const fb::LatticePoint ins = lat(r.insertion);

        fb::BlockRefBuilder rb(b);
        rb.add_name(name);
        rb.add_insertion(&ins);
        rb.add_scale_x(r.scale_x);
        rb.add_scale_y(r.scale_y);
        rb.add_rotation(r.rotation);
        rb.add_layer(layer);
        rb.add_attributes(vv);
        rb.add_notes(nv);
        rb.add_attrs(appearance);
    return rb.Finish();
}

gdoc::BlockRef read_block_ref(const fb::BlockRef* r) {
    gdoc::BlockRef out;
    out.name=str(r->name());out.insertion=lat(r->insertion());
    out.scale_x=r->scale_x();out.scale_y=r->scale_y();out.rotation=r->rotation();
    out.layer=str_or(r->layer(),"0");read_attrs(r->attrs(),out.attrs);
    if(const auto* values=r->attributes())for(const auto* value:*values)
        out.attributes.push_back({str(value->tag()),str(value->value())});
    if(const auto* notes=r->notes())for(const auto* note:*notes) {
        gdoc::BlockNote text;
        text.pos = lat(note->position());
        text.text = str(note->text());
        text.height = note->height();
        text.angle = note->angle();
        text.layer = str_or(note->layer(), "0");
        text.halign = note->halign();
        text.valign = note->valign();
        text.width = note->width();
        text.style = str_or(note->style(), "Standard");
        read_attrs(note->attrs(), text.attrs);
        if (!valid_note(text)) continue;
        out.notes.push_back(std::move(text));
    }
    return out;
}

std::vector<u8> encode(const Contents& c) {
    flatbuffers::FlatBufferBuilder b(4096);

    std::vector<flatbuffers::Offset<fb::Layer>> layers;
    for (const auto& l : c.layers.all()) {
        const auto name = b.CreateString(l.name);
        const auto desc = b.CreateString(l.description);
        const auto lt   = b.CreateString(l.linetype);
        const auto ps   = b.CreateString(l.plot_style);
        const auto vpf  = b.CreateVector(l.vp_frozen);
        const fb::Color cc = col(l.color);

        fb::LayerBuilder lb(b);
        lb.add_name(name);
        lb.add_description(desc);
        lb.add_on(l.on);
        lb.add_frozen(l.frozen);
        lb.add_locked(l.locked);
        lb.add_plottable(l.plottable);
        lb.add_color(&cc);
        lb.add_linetype(lt);
        lb.add_lineweight(static_cast<gcm::i32>(l.lineweight));
        lb.add_transparency(l.transparency);
        lb.add_plot_style(ps);
        lb.add_vp_frozen(vpf);
        layers.push_back(lb.Finish());
    }

    std::vector<flatbuffers::Offset<fb::Linetype>> linetypes;
    for (const auto& t : c.linetypes.all()) {
        std::vector<flatbuffers::Offset<fb::Dash>> dashes;
        for (const auto& d : t.pattern) {
            const auto txt = b.CreateString(d.text);
            fb::DashBuilder db(b);
            db.add_length(d.length);
            db.add_text(txt);
            db.add_text_scale(d.text_scale);
            dashes.push_back(db.Finish());
        }
        const auto name = b.CreateString(t.name);
        const auto desc = b.CreateString(t.description);
        const auto pat  = b.CreateVector(dashes);
        fb::LinetypeBuilder lb(b);
        lb.add_name(name);
        lb.add_description(desc);
        lb.add_pattern(pat);
        linetypes.push_back(lb.Finish());
    }

    std::vector<flatbuffers::Offset<fb::MultilineStyle>> mlines;
    for (const auto& m : c.multilines.all()) {
        std::vector<flatbuffers::Offset<fb::MultilineElement>> elems;
        for (const auto& e : m.elements) {
            const auto lt = b.CreateString(e.linetype);
            const fb::Color ec = col(e.color);
            fb::MultilineElementBuilder eb(b);
            eb.add_offset(e.offset);
            eb.add_color(&ec);
            eb.add_linetype(lt);
            eb.add_lineweight(static_cast<gcm::i32>(e.lineweight));
            elems.push_back(eb.Finish());
        }
        const auto name = b.CreateString(m.name);
        const auto desc = b.CreateString(m.description);
        const auto ev   = b.CreateVector(elems);
        const fb::Color fc = col(m.fill);
        fb::MultilineStyleBuilder mb(b);
        mb.add_name(name);
        mb.add_description(desc);
        mb.add_elements(ev);
        mb.add_filled(m.filled);
        mb.add_fill(&fc);
        mb.add_start_cap(static_cast<gcm::u8>(m.start_cap));
        mb.add_end_cap(static_cast<gcm::u8>(m.end_cap));
        mb.add_show_joints(m.show_joints);
        mlines.push_back(mb.Finish());
    }

    std::vector<flatbuffers::Offset<fb::BlockDef>> blocks;
    for (const auto& d : c.blocks.all()) {
        std::vector<fb::Shape> geom;
        geom.reserve(d.geometry.size());
        for (const auto& s : d.geometry) geom.push_back(shape(s));

        std::vector<flatbuffers::Offset<fb::AttributeDef>> attrs;
        for (const auto& a : d.attributes) {
            const auto tag = b.CreateString(a.tag);
            const auto pr  = b.CreateString(a.prompt);
            const auto dv  = b.CreateString(a.default_value);
            const auto st  = b.CreateString(a.style);
            const fb::LatticePoint pos = lat(a.position);
            fb::AttributeDefBuilder ab(b);
            ab.add_tag(tag);
            ab.add_prompt(pr);
            ab.add_default_value(dv);
            ab.add_invisible(a.invisible);
            ab.add_constant(a.constant);
            ab.add_verify(a.verify);
            ab.add_preset(a.preset);
            ab.add_multiline(a.multiline);
            ab.add_lock_position(a.lock_position);
            ab.add_position(&pos);
            ab.add_height(a.height);
            ab.add_rotation(a.rotation);
            ab.add_style(st);
            attrs.push_back(ab.Finish());
        }

        std::vector<flatbuffers::Offset<fb::BlockNote>> notes;
        for (const auto& note : d.notes) {
            const auto text = b.CreateString(note.text);
            const auto layer = b.CreateString(note.layer);
            const auto style = b.CreateString(note.style);
            const auto attrs = write_attrs(b, note.attrs);
            const auto position = lat(note.pos);
            fb::BlockNoteBuilder nb(b);
            nb.add_position(&position);
            nb.add_text(text);
            nb.add_height(note.height);
            nb.add_angle(note.angle);
            nb.add_layer(layer);
            nb.add_halign(note.halign);
            nb.add_valign(note.valign);
            nb.add_width(note.width);
            nb.add_style(style);
            nb.add_attrs(attrs);
            notes.push_back(nb.Finish());
        }
        const auto nv = b.CreateVector(notes);
        state::Writer underlay_state;
        underlay_state(std::uint32_t(2), d.underlays, state::underlay_details(d.underlays));
        const auto uv = b.CreateVector(underlay_state.bytes);
        const auto source = b.CreateVector(d.source_document);
        const auto source_owner = b.CreateString(d.source_owner);
        const auto name   = b.CreateString(d.name);
        const auto desc   = b.CreateString(d.description);
        const auto xpath  = b.CreateString(d.xref_path);
        const auto gv     = b.CreateVectorOfStructs(geom);
        const auto av     = b.CreateVector(attrs);
        const auto nested = b.CreateVectorOfStrings(d.nested);
        std::vector<flatbuffers::Offset<fb::BlockRef>> child_refs;
        for(const auto& ref:d.nested_refs)child_refs.push_back(write_block_ref(b,ref));
        const auto nested_refs=b.CreateVector(child_refs);
        Contents::SpaceGeometry block_geometry;
        block_geometry.shapes = d.geometry;
        for (const auto& sh : d.geometry) block_geometry.layers.push_back(sh.layer);
        const auto state = write_geometry(b, block_geometry);
        const fb::LatticePoint base = lat(d.base);

        fb::BlockDefBuilder bb(b);
        bb.add_name(name);
        bb.add_description(desc);
        bb.add_base(&base);
        bb.add_allow_exploding(d.allow_exploding);
        bb.add_annotative(d.annotative);
        bb.add_is_xref(d.is_xref);
        bb.add_xref_path(xpath);
        bb.add_geometry(gv);
        bb.add_attributes(av);
        bb.add_nested(nested);
        bb.add_nested_refs(nested_refs);
        bb.add_notes(nv);
        bb.add_underlay_state(uv);
        bb.add_source_document(source);
        bb.add_source_owner(source_owner);
        bb.add_geometry_state(state);
        blocks.push_back(bb.Finish());
    }

    std::vector<flatbuffers::Offset<fb::BlockRef>> refs;
    for(const auto& r:c.blocks.references())refs.push_back(write_block_ref(b,r));

    std::vector<flatbuffers::Offset<fb::Space>> spaces;
    for (const auto& s : c.spaces) {
        const std::vector<f64> origin{s.frame.origin.x.to_f64(),
                                      s.frame.origin.y.to_f64(),
                                      s.frame.origin.z.to_f64()};
        const std::vector<f64> ax{s.frame.u.x.to_f64(), s.frame.u.y.to_f64(),
                                  s.frame.u.z.to_f64()};
        const std::vector<f64> ay{s.frame.v.x.to_f64(), s.frame.v.y.to_f64(),
                                  s.frame.v.z.to_f64()};
        const std::vector<f64> az{s.frame.n.x.to_f64(), s.frame.n.y.to_f64(),
                                  s.frame.n.z.to_f64()};

        const auto name = b.CreateString(s.name);
        const auto ov = b.CreateVector(origin);
        const auto xv = b.CreateVector(ax);
        const auto yv = b.CreateVector(ay);
        const auto zv = b.CreateVector(az);

        fb::SpaceBuilder sb(b);
        sb.add_id(s.id);
        sb.add_kind(static_cast<fb::SpaceKind>(s.kind));
        sb.add_name(name);
        sb.add_origin(ov);
        sb.add_x_axis(xv);
        sb.add_y_axis(yv);
        sb.add_z_axis(zv);
        sb.add_paper_width(s.paper_width);
        sb.add_paper_height(s.paper_height);
        spaces.push_back(sb.Finish());
    }

    std::vector<flatbuffers::Offset<fb::SpaceGeometry>> geometry;
    for (const auto& g : c.geometry) {
        geometry.push_back(write_geometry(b, g));
    }

    std::vector<flatbuffers::Offset<fb::Entity>> entities;
    for (const gdoc::Entity& e : c.entities) {
        const auto attrs = write_attrs(b, e.attrs);

        std::vector<gcm::i64> pts;
        pts.reserve(e.payload.points.size() * 3);
        for (const auto& q : e.payload.points) {
            pts.push_back(q.x);
            pts.push_back(q.y);
            pts.push_back(q.z);
        }
        std::vector<flatbuffers::Offset<fb::EntityRef>> refs2;
        for (const gdoc::EntityId& id : e.payload.refs) {
            fb::EntityRefBuilder rb(b);
            rb.add_hi(id.hi);
            rb.add_lo(id.lo);
            refs2.push_back(rb.Finish());
        }

        const auto layer = b.CreateString(e.layer);
        const auto text  = b.CreateString(e.payload.text);
        const auto pv    = b.CreateVector(pts);
        const auto rev   = b.CreateVector(e.payload.reals);
        const auto iv    = b.CreateVector(e.payload.ints);
        const auto rfv   = b.CreateVector(refs2);
        const auto blob  = b.CreateVector(e.payload.blob);

        fb::EntityBuilder eb(b);
        eb.add_id_hi(e.id.hi);
        eb.add_id_lo(e.id.lo);
        eb.add_kind(static_cast<gcm::u16>(e.kind));
        eb.add_space(e.space);
        eb.add_layer(layer);
        eb.add_attrs(attrs);
        eb.add_points(pv);
        eb.add_reals(rev);
        eb.add_ints(iv);
        eb.add_text(text);
        eb.add_refs(rfv);
        eb.add_blob(blob);
        entities.push_back(eb.Finish());
    }

    const auto units   = b.CreateString(c.units);
    const auto by      = b.CreateString(c.created_by);
    const auto appname = b.CreateString("GLCAD");
    const fb::WorkingVolume wv(c.volume.extent, c.volume.resolution_bits);

    fb::SettingsBuilder st(b);
    st.add_volume(&wv);
    st.add_units(units);
    st.add_precision(c.precision);
    st.add_created_by(by);
    st.add_application(appname);
    const auto settings = st.Finish();

    const auto lv = b.CreateVector(layers);
    const auto tv = b.CreateVector(linetypes);
    const auto mv = b.CreateVector(mlines);
    const auto bv = b.CreateVector(blocks);
    const auto rv = b.CreateVector(refs);
    const auto sv = b.CreateVector(spaces);
    const auto gv = b.CreateVector(geometry);
    const auto ev = b.CreateVector(entities);

    state::Writer block_state;
    block_state(std::uint32_t{1}, c.blocks.dynamics(), c.blocks.xrefs());
    const auto block_state_bytes = b.CreateVector(block_state.bytes);
    fb::DocumentBuilder db(b);
    db.add_block_state(block_state_bytes);
    db.add_format_version(kSchemaVersion);
    db.add_settings(settings);
    db.add_layers(lv);
    db.add_linetypes(tv);
    db.add_multilines(mv);
    db.add_blocks(bv);
    db.add_block_refs(rv);
    db.add_spaces(sv);
    db.add_geometry(gv);
    db.add_entities(ev);
    db.add_current_space(c.current_space);
    b.Finish(db.Finish(), fb::DocumentIdentifier());

    return std::vector<u8>(b.GetBufferPointer(), b.GetBufferPointer() + b.GetSize());
}

void from_document(const gdoc::Document& d, Contents& c) {
    c.layers = d.layers();
    c.current_space = d.current_space();

    c.spaces.clear();
    for (gdoc::SpaceKind k : {gdoc::SpaceKind::Model, gdoc::SpaceKind::Sketch,
                              gdoc::SpaceKind::Draft, gdoc::SpaceKind::Sheet})
        for (gdoc::SpaceId id : d.spaces_of(k))
            if (const gdoc::Space* s = d.space(id)) c.spaces.push_back(*s);

    // Sorted by id for deterministic output.
    std::sort(c.spaces.begin(), c.spaces.end(),
              [](const gdoc::Space& a, const gdoc::Space& b) { return a.id < b.id; });

    c.entities.clear();
    for (const gdoc::Space& s : c.spaces)
        for (const gdoc::EntityId& id : d.in_space(s.id))
            if (const gdoc::Entity* e = d.find(id)) c.entities.push_back(*e);

    // Sorted: hash map iteration order is not stable.
    std::sort(c.entities.begin(), c.entities.end(),
              [](const gdoc::Entity& a, const gdoc::Entity& b) {
                  return a.id.hi != b.id.hi ? a.id.hi < b.id.hi
                                            : a.id.lo < b.id.lo;
              });
}

void to_document(const Contents& c, gdoc::Document& d) {
    d.layers() = c.layers;
    for (const gdoc::Space& s : c.spaces)
        if (!d.space(s.id)) d.add_space(s);
    for (const gdoc::Entity& e : c.entities) d.adopt(e);
    d.set_current_space(c.current_space);
}

Error decode(const u8* data, std::size_t n, Contents& c) {
    if (!data || n < 8) return Error::fail("document buffer is too small");

    flatbuffers::Verifier v(data, n);
    if (!fb::VerifyDocumentBuffer(v))
        return Error::fail("document failed verification: the file is corrupt");

    const fb::Document* d = fb::GetDocument(data);
    if (!d) return Error::fail("no document in the buffer");

    if (const auto* s = d->settings()) {
        if (const auto* wv = s->volume()) {
            c.volume.extent          = wv->extent();
            c.volume.resolution_bits = wv->resolution_bits();
        }
        c.units      = str_or(s->units(), "mm");
        c.precision  = s->precision();
        c.created_by = str_or(s->created_by(), "GLCAD");
    }

    c.layers = gdoc::LayerTable{};
    if (const auto* v2 = d->layers()) {
        for (const auto* l : *v2) {
            gdoc::Layer out;
            out.name         = str(l->name());
            out.description  = str(l->description());
            out.on           = l->on();
            out.frozen       = l->frozen();
            out.locked       = l->locked();
            out.plottable    = l->plottable();
            out.color        = col(l->color());
            out.linetype     = str_or(l->linetype(), "Continuous");
            out.lineweight   = static_cast<gdoc::Lineweight>(l->lineweight());
            out.transparency = l->transparency();
            out.plot_style   = str_or(l->plot_style(), "Normal");
            if (const auto* f = l->vp_frozen())
                for (auto id : *f) out.vp_frozen.push_back(id);
            c.layers.add(out);
        }
    }

    c.linetypes = gdoc::LinetypeTable{};
    if (const auto* v2 = d->linetypes()) {
        for (const auto* t : *v2) {
            gdoc::Linetype out;
            out.name        = str(t->name());
            out.description = str(t->description());
            if (const auto* p = t->pattern())
                for (const auto* dash : *p)
                    out.pattern.push_back({dash->length(), str(dash->text()),
                                           dash->text_scale()});
            c.linetypes.add(out);
        }
    }

    c.multilines = gdoc::MultilineTable{};
    if (const auto* v2 = d->multilines()) {
        for (const auto* m : *v2) {
            gdoc::MultilineStyle out;
            out.name        = str(m->name());
            out.description = str(m->description());
            if (const auto* e = m->elements())
                for (const auto* el : *e) {
                    gdoc::MultilineElement me;
                    me.offset     = el->offset();
                    me.color      = col(el->color());
                    me.linetype   = str_or(el->linetype(), "ByLayer");
                    me.lineweight = static_cast<gdoc::Lineweight>(el->lineweight());
                    out.elements.push_back(me);
                }
            out.filled      = m->filled();
            out.fill        = col(m->fill());
            out.start_cap   = static_cast<gdoc::EndCap>(m->start_cap());
            out.end_cap     = static_cast<gdoc::EndCap>(m->end_cap());
            out.show_joints = m->show_joints();
            c.multilines.add(out);
        }
    }

    c.blocks = gdoc::BlockTable{};
    if (const auto* v2 = d->blocks()) {
        for (const auto* bd : *v2) {
            gdoc::BlockDef out;
            out.name            = str(bd->name());
            out.description     = str(bd->description());
            out.base            = lat(bd->base());
            out.allow_exploding = bd->allow_exploding();
            out.annotative      = bd->annotative();
            out.is_xref         = bd->is_xref();
            out.xref_path       = str(bd->xref_path());
            out.source_owner    = str(bd->source_owner());
            if (const auto* source=bd->source_document())
                out.source_document.assign(source->begin(),source->end());
            if (const auto* bytes=bd->underlay_state()) {
                try {
                    const std::vector<u8> data(bytes->begin(),bytes->end());
                    state::Reader reader{data};std::uint32_t version;reader(version);
                    if(version!=1&&version!=2)return Error::fail("unknown block underlay state version");
                    reader(out.underlays);
                    if(version>=2) {
                        std::vector<state::UnderlayDetails> details;reader(details);
                        if(!state::apply_underlay_details(out.underlays,details))return Error::fail("invalid block underlay extension");
                    }
                    reader.finish();
                } catch(const std::exception& error) {return Error::fail(error.what());}
            }
            if (const auto* g = bd->geometry())
                for (const auto* s : *g) out.geometry.push_back(shape(s));
            if (const auto* state = bd->geometry_state()) {
                Contents::SpaceGeometry geometry;
                const auto error = read_geometry(state, geometry);
                if (!error.ok) return error;
                for (std::size_t i = 0; i < geometry.shapes.size(); ++i)
                    geometry.shapes[i].layer = geometry.layers[i];
                out.geometry = std::move(geometry.shapes);
            }
            if (const auto* notes = bd->notes())
                for (const auto* note : *notes) {
                    gdoc::BlockDef::Note text;
                    text.pos = lat(note->position());
                    text.text = str(note->text());
                    text.height = note->height();
                    text.angle = note->angle();
                    text.layer = str_or(note->layer(), "0");
                    text.halign = note->halign();
                    text.valign = note->valign();
                    text.width = note->width();
                    text.style = str_or(note->style(), "Standard");
                    read_attrs(note->attrs(), text.attrs);
                    if (!valid_note(text)) return Error::fail("block " + out.name + ": invalid text dimensions or alignment");
                    out.notes.push_back(std::move(text));
                }
            if (const auto* a = bd->attributes())
                for (const auto* ad : *a) {
                    gdoc::AttributeDef at;
                    at.tag           = str(ad->tag());
                    at.prompt        = str(ad->prompt());
                    at.default_value = str(ad->default_value());
                    at.invisible     = ad->invisible();
                    at.constant      = ad->constant();
                    at.verify        = ad->verify();
                    at.preset        = ad->preset();
                    at.multiline     = ad->multiline();
                    at.lock_position = ad->lock_position();
                    at.position      = lat(ad->position());
                    at.height        = ad->height();
                    at.rotation      = ad->rotation();
                    at.style         = str_or(ad->style(), "Standard");
                    out.attributes.push_back(at);
                }
            if (const auto* nn = bd->nested())
                for (const auto* s : *nn) out.nested.push_back(s->str());
            if(const auto* refs=bd->nested_refs())for(const auto* ref:*refs)out.nested_refs.push_back(read_block_ref(ref));
            c.blocks.define(out);
        }
    }
    if(const auto* refs=d->block_refs())for(const auto* ref:*refs)c.blocks.insert(read_block_ref(ref));

    c.spaces.clear();
    if (const auto* v2 = d->spaces()) {
        for (const auto* s : *v2) {
            gdoc::Space out;
            out.id   = s->id();
            out.kind = static_cast<gdoc::SpaceKind>(s->kind());
            out.name = str(s->name());

            auto vec3 = [](const flatbuffers::Vector<f64>* v3, f64 dx, f64 dy,
                           f64 dz, f64& x, f64& y, f64& z) {
                if (v3 && v3->size() == 3) {
                    x = v3->Get(0);
                    y = v3->Get(1);
                    z = v3->Get(2);
                } else {
                    x = dx; y = dy; z = dz;
                }
            };
            f64 x, y, z;
            using S = gcm::F64;
            vec3(s->origin(), 0, 0, 0, x, y, z);
            out.frame.origin = {S::from(x), S::from(y), S::from(z)};
            vec3(s->x_axis(), 1, 0, 0, x, y, z);
            out.frame.u = {S::from(x), S::from(y), S::from(z)};
            vec3(s->y_axis(), 0, 1, 0, x, y, z);
            out.frame.v = {S::from(x), S::from(y), S::from(z)};
            vec3(s->z_axis(), 0, 0, 1, x, y, z);
            out.frame.n = {S::from(x), S::from(y), S::from(z)};

            out.paper_width  = s->paper_width();
            out.paper_height = s->paper_height();
            c.spaces.push_back(out);
        }
    }

    c.geometry.clear();
    if (const auto* v2 = d->geometry()) {
        for (const auto* g : *v2) {
            Contents::SpaceGeometry out;
            const auto error = read_geometry(g, out);
            if (!error.ok) return error;
            c.geometry.push_back(std::move(out));
        }
    }

    c.entities.clear();
    c.current_space = d->current_space();
    if (const auto* v2 = d->entities()) {
        for (const auto* e : *v2) {
            gdoc::Entity out;
            out.id.hi = e->id_hi();
            out.id.lo = e->id_lo();
            out.kind  = static_cast<gdoc::Kind>(e->kind());
            out.space = e->space();
            out.layer = str_or(e->layer(), "0");

            read_attrs(e->attrs(), out.attrs);

            if (const auto* pts = e->points()) {
                for (flatbuffers::uoffset_t i = 0; i + 2 < pts->size(); i += 3)
                    out.payload.points.push_back(
                        gcm::Lattice3{pts->Get(i), pts->Get(i + 1),
                                      pts->Get(i + 2)});
            }
            if (const auto* rs = e->reals())
                out.payload.reals.assign(rs->begin(), rs->end());
            if (const auto* is = e->ints())
                out.payload.ints.assign(is->begin(), is->end());
            out.payload.text = str(e->text());
            if (const auto* blob = e->blob())
                out.payload.blob.assign(blob->begin(), blob->end());
            if (const auto* rf = e->refs())
                for (const auto* r : *rf)
                    out.payload.refs.push_back(gdoc::EntityId{r->hi(), r->lo()});

            c.entities.push_back(std::move(out));
        }
    }

    if (const auto* extension = d->block_state()) {
        try {
            const std::vector<u8> bytes(extension->begin(), extension->end());
            state::Reader in{bytes};
            std::uint32_t version; std::vector<gdoc::DynamicBlock> dynamics;
            std::vector<gdoc::Xref> xrefs;
            in(version);
            if (version != 1) return Error::fail("unknown block state version");
            in(dynamics, xrefs); in.finish();
            c.blocks.load_extensions(std::move(dynamics), std::move(xrefs));
        } catch (const std::exception& e) { return Error::fail(e.what()); }
    }
    return Error::good();
}

Error write_document(gio::Writer& w, const Contents& c) {
    const auto valid = validate_geometry(c);
    if (!valid.ok) return valid;
    const std::vector<u8> body = encode(c);
    if (!gio::write_frame(w, gio::FrameKind::Document, body))
        return Error::fail("failed to write the document frame");

    for (const auto& f : c.foreign)
        if (!gio::write_frame(w, static_cast<gio::FrameKind>(f.kind), f.payload))
            return Error::fail("failed to write a preserved frame");

    if (!w.flush()) return Error::fail("failed to flush the document");
    return Error::good();
}

Error read_document(gio::Reader& r, Contents& c, ReadReport* report) {
    ReadReport rep;
    bool found_document = false;
    std::vector<u8> payload;

    for (;;) {
        const auto res = gio::read_frame(r, payload);
        if (res.end_of_stream) break;
        if (!res.ok) return Error::fail(res.error);
        ++rep.frames_read;

        if (res.header.kind == gio::FrameKind::Document) {
            if (found_document) continue;    // the first one is the document
            const Error e = decode(payload.data(), payload.size(), c);
            if (!e.ok) return e;
            found_document = true;

            if (payload.size() >= 8) {
                const fb::Document* d = fb::GetDocument(payload.data());
                rep.format_version = d->format_version();
                rep.future_version = rep.format_version > kSchemaVersion;
            }
            continue;
        }

        if (res.header.kind == gio::FrameKind::Delta) {
            if (!found_document) return Error::fail("a delta before the document");
            Delta d;
            const Error e = decode_delta(payload.data(), payload.size(), d);
            if (!e.ok) return e;
            apply(c, d);
            ++rep.deltas_applied;
            continue;
        }

        ++rep.frames_skipped;
        c.foreign.push_back({static_cast<u16>(res.header.kind), payload});
    }

    if (report) *report = rep;
    if (!found_document) return Error::fail("no document frame in the stream");
    return validate_geometry(c);
}

Error validate_regions(const Contents& c) {
    for (const auto& g : c.geometry) {
        const auto error = gregion::validate_members(g.shapes);
        if (!error.empty()) return Error::fail(error);
    }
    for (const auto& block : c.blocks.all()) {
        const auto error = gregion::validate_members(block.geometry);
        if (!error.empty()) return Error::fail("block " + block.name + ": " + error);
    }
    return Error::good();
}

Error validate_geometry(const Contents& c) {
    const auto regions=validate_regions(c);if(!regions.ok)return regions;
    for (const auto& block : c.blocks.all()) {
        for (const auto& note : block.notes)
            if (!valid_note(note))
                return Error::fail("block " + block.name + ": invalid text dimensions or alignment");
        for (const auto& ref : block.nested_refs)
            for (const auto& note : ref.notes)
                if (!valid_note(note))
                    return Error::fail("block " + block.name + ": invalid attribute text dimensions or alignment");
    }
    for (const auto& ref : c.blocks.references())
        for (const auto& note : ref.notes)
            if (!valid_note(note))
                return Error::fail("block " + ref.name + ": invalid attribute text dimensions or alignment");
    for(const auto& entity:c.entities) {
        if(entity.kind!=gdoc::Kind::Leader || entity.payload.ints.size()<=5)continue;
        const auto& ints=entity.payload.ints;const auto& points=entity.payload.points;
        const auto& reals=entity.payload.reals;
        if(!reals.empty()&&(reals.size()!=3||!std::isfinite(reals[0])||!std::isfinite(reals[1])||!std::isfinite(reals[2])||reals[0]<0||reals[2]<0))return Error::fail("invalid leader text or arrow dimensions");
        if(ints.size()<8 || ints[5]!=0x4c445201u || (ints[6]&~63u)!=0 || (ints[6]>>3)>4 || ints[7]>256 || ints.size()!=8+2*ints[7] || points.size()<5 || points.size()>4101)
            return Error::fail("invalid leader path descriptor");
        std::size_t count=5;
        for(std::size_t i=0;i<ints[7];++i) {
            if(ints[8+2*i]<2||ints[8+2*i]>4096||ints[9+2*i]>1)return Error::fail("invalid leader path size or arrow flag");
            count+=ints[8+2*i];
        }
        if(count!=points.size())return Error::fail("leader path vertices do not match the descriptor");
    }
    return Error::good();
}

Error save(std::string_view url, const Contents& c) {
    const auto valid=validate_geometry(c);if(!valid.ok)return valid;
    gio::Status s;
    auto conn = gio::TransportRegistry::instance().open(url, gio::Mode::Write, s);
    if (!conn) return Error::fail(s.message);
    if (!conn->output()) return Error::fail("that endpoint cannot be written to");

    const Error e = write_document(*conn->output(), c);
    const gio::Status closed = conn->close();
    if (!e.ok) return e;
    if (!closed.ok) return Error::fail(closed.message);
    return Error::good();
}

Error load(std::string_view url, Contents& c, ReadReport* report) {
    gio::Status s;
    auto conn = gio::TransportRegistry::instance().open(url, gio::Mode::Read, s);
    if (!conn) return Error::fail(s.message);
    if (!conn->input()) return Error::fail("that endpoint cannot be read from");

    const Error e = read_document(*conn->input(), c, report);
    conn->close();
    return e;
}

Error save_file(const std::string& path, const Contents& c) {
    return save("file://" + path, c);
}
Error load_file(const std::string& path, Contents& c, ReadReport* report) {
    return load("file://" + path, c, report);
}

Error DocumentFile::create(const std::string& path) {
    path_ = path;
    file_version_ = kSchemaVersion;
    report_ = ReadReport{};
    return Error::good();
}

Error DocumentFile::open(const std::string& path) {
    Contents probe;
    ReadReport rep;
    const Error e = load_file(path, probe, &rep);
    if (!e.ok) return e;
    path_ = path;
    report_ = rep;
    file_version_ = rep.format_version;
    return Error::good();
}

void DocumentFile::close() {
    path_.clear();
    file_version_ = 0;
    report_ = ReadReport{};
}

Error DocumentFile::write(const Contents& c) {
    if (path_.empty()) return Error::fail("no document is open");
    const Error e = save_file(path_, c);
    if (e.ok) file_version_ = kSchemaVersion;
    return e;
}

Error DocumentFile::read(Contents& c) {
    if (path_.empty()) return Error::fail("no document is open");
    const Error e = load_file(path_, c, &report_);
    if (e.ok) file_version_ = report_.format_version;
    return e;
}

}
