#include "ui/doc_entities.hpp"

#include <cstdlib>
#include "engine/document_file.hpp"

namespace gcapp {

namespace {

using gcm::u32;
using gcm::u64;

gdoc::EntityId id_for(gdoc::Kind k, std::size_t index) {
    return {static_cast<u64>(k), static_cast<u64>(index) + 1};
}

gcm::Lattice3 lift(const gcm::Lattice2& p) { return {p.x, p.y, 0}; }
gcm::Lattice2 flatten(const gcm::Lattice3& p) { return {p.x, p.y}; }

const std::string* user_value(const gdoc::Attributes& a, const char* key) {
    const auto it = a.user.find(key);
    return it == a.user.end() ? nullptr : &it->second;
}

}

void append_entities(const std::vector<gdim::Dimension>& dims,
                     const std::vector<gdoc::AssociativeHatch>& hatches,
                     const std::vector<ggeo::NurbsCurve2<Scalar>>& splines,
                     const std::vector<Canvas::StoredConstraint>& constraints,
                     std::vector<gdoc::Entity>& out) {
    for (std::size_t i = 0; i < dims.size(); ++i) {
        const gdim::Dimension& d = dims[i];
        gdoc::Entity e;
        e.kind = d.kind==gdim::Kind::Leader?gdoc::Kind::Leader:gdoc::Kind::Dimension;
        e.id = id_for(e.kind, i);
        e.layer = d.layer.empty() ? "0" : d.layer;
        e.payload.points = {lift(d.p1), lift(d.p2), lift(d.p3), lift(d.centre),
                            // Lattice count, not a length: a double would round it above 2^53.
                            gcm::Lattice3{d.radius, 0, 0}};
        e.payload.ints = {static_cast<u32>(d.kind),
                          d.o1.shape, static_cast<u32>(d.o1.at),
                          d.o2.shape, static_cast<u32>(d.o2.at)};
        if(d.kind==gdim::Kind::Leader && (!d.leader_paths.empty()||d.leader_text_absolute||d.leader_text_angle||d.leader_text_height||d.leader_arrow_size||d.leader_text_halign>=0||d.leader_text_valign>=0)) {
            e.payload.reals={d.leader_text_height,d.leader_text_angle,d.leader_arrow_size};
            e.payload.ints.insert(e.payload.ints.end(),{0x4c445201u,u32(d.leader_text_absolute)|(u32(d.leader_text_halign+1)<<1)|(u32(d.leader_text_valign+1)<<3),u32(d.leader_paths.size())});
            for(const auto& path:d.leader_paths) {
                e.payload.ints.push_back(u32(path.vertices.size()));e.payload.ints.push_back(u32(path.arrow));
                for(auto point:path.vertices)e.payload.points.push_back(lift(point));
            }
        }
        if(d.kind!=gdim::Kind::Leader && (d.extension_offset.x || d.extension_offset.y)) {
            e.payload.ints.push_back(0x45585401u);
            e.payload.points.push_back(lift(d.extension_offset));
        }
        e.payload.text = d.style;
        if (d.has_override()) e.attrs.user["override"] = d.text_override;
        out.push_back(std::move(e));
    }

    for (std::size_t i = 0; i < hatches.size(); ++i) {
        const gdoc::AssociativeHatch& h = hatches[i];
        gdoc::Entity e;
        e.id = id_for(gdoc::Kind::Hatch, i);
        e.kind = gdoc::Kind::Hatch;
        e.attrs = h.attrs;
        e.layer = h.layer.empty() ? "0" : h.layer;
        e.payload.points = {lift(h.seed), lift(h.style.origin)};
        e.payload.reals = {h.style.scale, h.style.angle};
        e.payload.ints = h.boundary_shapes;
        if (!h.boundary_geometry.empty()) {
            gfile::Delta boundary;
            gfile::SpaceDelta geometry; geometry.inserted=h.boundary_geometry;
            for (const auto& shape:geometry.inserted) geometry.layers.push_back(shape.layer);
            boundary.spaces.push_back(std::move(geometry));
            e.payload.blob=gfile::encode_delta(boundary);
            e.attrs.user["boundary_geometry"]="1";
        }
        e.payload.text = h.style.name;
        if(h.area_background) {
            e.attrs.user["area_background"]="1";
            e.payload.reals.push_back(h.gap_tolerance);
            e.payload.ints=h.source_keys;
        }
        e.attrs.user["seed"] = h.seed_valid ? "1" : "0";
        e.attrs.user["assoc"] = h.style.associative ? "1" : "0";
        e.attrs.user["islands"] = std::to_string(static_cast<int>(h.style.islands));
        out.push_back(std::move(e));
    }

    for (std::size_t i = 0; i < constraints.size(); ++i) {
        const Canvas::StoredConstraint& c = constraints[i];
        gdoc::Entity e;
        e.id = id_for(gdoc::Kind::Constraint, i);
        e.kind = gdoc::Kind::Constraint;
        e.payload.ints = {static_cast<u32>(c.kind), c.a, c.b,
                          static_cast<u32>(c.driving ? 1 : 0)};
        e.payload.reals = {c.value};
        out.push_back(std::move(e));
    }

    for (std::size_t i = 0; i < splines.size(); ++i) {
        const auto& s = splines[i];
        gdoc::Entity e;
        e.id = id_for(gdoc::Kind::Spline, i);
        e.kind = gdoc::Kind::Spline;
        e.payload.ints = {static_cast<u32>(s.degree),
                          static_cast<u32>(s.control.size()),
                          static_cast<u32>(s.knots.knots.size())};
        e.payload.reals.reserve(s.control.size() * 3 + s.knots.knots.size());
        for (const auto& cp : s.control) {
            e.payload.reals.push_back(cp.p.x.to_f64());
            e.payload.reals.push_back(cp.p.y.to_f64());
            e.payload.reals.push_back(cp.w.to_f64());
        }
        for (const double k : s.knots.knots) e.payload.reals.push_back(k);
        out.push_back(std::move(e));
    }
}

void read_entities(const std::vector<gdoc::Entity>& in,
                   std::vector<gdim::Dimension>& dims,
                   std::vector<gdoc::AssociativeHatch>& hatches,
                   std::vector<ggeo::NurbsCurve2<Scalar>>& splines,
                   std::vector<Canvas::StoredConstraint>& constraints) {
    using S = Scalar;
    for (const gdoc::Entity& e : in) {
        switch (e.kind) {
            case gdoc::Kind::Leader:
            case gdoc::Kind::Dimension: {
                if (e.payload.points.size() < 5) break;
                gdim::Dimension d;
                d.layer = e.layer;
                d.p1 = flatten(e.payload.points[0]);
                d.p2 = flatten(e.payload.points[1]);
                d.p3 = flatten(e.payload.points[2]);
                d.centre = flatten(e.payload.points[3]);
                d.radius = e.payload.points[4].x;
                if (!e.payload.ints.empty())
                    d.kind = static_cast<gdim::Kind>(e.payload.ints[0]);
                if (e.payload.ints.size() >= 5) {
                    d.o1.shape = e.payload.ints[1];
                    d.o1.at    = static_cast<gdim::Dimension::Anchor>(e.payload.ints[2]);
                    d.o2.shape = e.payload.ints[3];
                    d.o2.at    = static_cast<gdim::Dimension::Anchor>(e.payload.ints[4]);
                }
                if(d.kind!=gdim::Kind::Leader && e.payload.ints.size()>5 &&
                   e.payload.ints[5]==0x45585401u && e.payload.points.size()>5)
                    d.extension_offset=flatten(e.payload.points[5]);
                if (!e.payload.text.empty()) d.style = e.payload.text;
                if(d.kind==gdim::Kind::Leader && e.payload.ints.size()>5) {
                    if(e.payload.ints.size()<8 || e.payload.ints[5]!=0x4c445201u || (e.payload.ints[6]&~63u)!=0 || (e.payload.ints[6]>>3)>4 || e.payload.ints[7]>256 || e.payload.ints.size()!=8+2*e.payload.ints[7])break;
                    d.leader_text_absolute=(e.payload.ints[6]&1)!=0;
                    d.leader_text_halign=int((e.payload.ints[6]>>1)&3)-1;d.leader_text_valign=int(e.payload.ints[6]>>3)-1;
                    std::size_t at=5;bool valid=true;
                    if(e.payload.reals.size()==3) {
                        d.leader_text_height=e.payload.reals[0];d.leader_text_angle=e.payload.reals[1];d.leader_arrow_size=e.payload.reals[2];
                    }
                    for(std::size_t path=0;path<e.payload.ints[7];++path) {
                        const auto count=e.payload.ints[8+2*path],arrow=e.payload.ints[9+2*path];
                        if(count<2||count>4096||arrow>1||count>e.payload.points.size()-at||at+count>4101){valid=false;break;}
                        gdim::Dimension::LeaderPath run;run.arrow=arrow!=0;
                        for(u32 i=0;i<count;++i)run.vertices.push_back(flatten(e.payload.points[at++]));
                        d.leader_paths.push_back(std::move(run));
                    }
                    if(!valid||at!=e.payload.points.size())break;
                }
                if (const std::string* o = user_value(e.attrs, "override"))
                    d.text_override = *o;
                dims.push_back(std::move(d));
                break;
            }
            case gdoc::Kind::Constraint: {
                if (e.payload.ints.size() < 3) break;
                Canvas::StoredConstraint c;
                c.kind = static_cast<gsolve::Kind>(e.payload.ints[0]);
                c.a = e.payload.ints[1];
                c.b = e.payload.ints[2];
                c.driving = e.payload.ints.size() < 4 || e.payload.ints[3] != 0;
                if (!e.payload.reals.empty()) c.value = e.payload.reals[0];
                constraints.push_back(c);
                break;
            }
            case gdoc::Kind::Hatch: {
                if (e.payload.points.empty()) break;
                gdoc::AssociativeHatch h;
                h.layer = e.layer;
                h.attrs = e.attrs;
                if(const auto* background=user_value(e.attrs,"area_background"); background && *background=="1") {
                    h.area_background=true;h.source_keys=e.payload.ints;
                    if(e.payload.reals.size()>2)h.gap_tolerance=e.payload.reals[2];
                }
                h.seed = flatten(e.payload.points[0]);
                if (e.payload.points.size() > 1)
                    h.style.origin = flatten(e.payload.points[1]);
                if (e.payload.reals.size() > 1) {
                    h.style.scale = e.payload.reals[0];
                    h.style.angle = e.payload.reals[1];
                }
                h.boundary_shapes = e.payload.ints;
                if (const auto* version=user_value(e.attrs,"boundary_geometry"); version && *version=="1") {
                    gfile::Delta boundary;
                    if (gfile::decode_delta(e.payload.blob.data(),e.payload.blob.size(),boundary).ok &&
                        boundary.spaces.size()==1)
                        h.boundary_geometry=std::move(boundary.spaces[0].inserted);
                }
                if (!e.payload.text.empty()) h.style.name = e.payload.text;
                if (const std::string* v = user_value(e.attrs, "seed"))
                    h.seed_valid = (*v == "1");
                if (const std::string* v = user_value(e.attrs, "assoc"))
                    h.style.associative = (*v == "1");
                if (const std::string* v = user_value(e.attrs, "islands"))
                    h.style.islands =
                        static_cast<gdoc::HatchStyle::Islands>(std::atoi(v->c_str()));
                h.valid = false;
                hatches.push_back(std::move(h));
                break;
            }
            case gdoc::Kind::Spline: {
                if (e.payload.ints.size() < 3) break;
                const std::size_t nc = e.payload.ints[1];
                const std::size_t nk = e.payload.ints[2];
                if (e.payload.reals.size() < nc * 3 + nk) break;
                ggeo::NurbsCurve2<S> c;
                c.degree = static_cast<int>(e.payload.ints[0]);
                c.control.reserve(nc);
                for (std::size_t i = 0; i < nc; ++i) {
                    ggeo::ControlPoint2<S> cp;
                    cp.p.x = S::from(e.payload.reals[i * 3 + 0]);
                    cp.p.y = S::from(e.payload.reals[i * 3 + 1]);
                    cp.w = S::from(e.payload.reals[i * 3 + 2]);
                    c.control.push_back(cp);
                }
                c.knots.knots.assign(e.payload.reals.begin() + long(nc * 3),
                                     e.payload.reals.begin() + long(nc * 3 + nk));
                splines.push_back(std::move(c));
                break;
            }
            default: break;
        }
    }
}

void append_text_entities(const std::vector<Canvas::TextItem>& texts,
                          gdoc::SpaceId space, std::vector<gdoc::Entity>& out) {
    for (const auto& t : texts) {
        gdoc::Entity e;
        e.id = {static_cast<gcm::u64>(gdoc::Kind::MText), out.size() + 1};
        e.kind = gdoc::Kind::MText;
        e.layer = t.layer.empty() ? "0" : t.layer;
        e.space = space;
        e.payload.points = {lift(t.pos)};
        e.payload.reals = {t.height, t.angle, t.width};
        e.payload.ints = {static_cast<u32>(t.halign), static_cast<u32>(t.valign)};
        e.payload.text = t.text;
        e.attrs=t.attrs; e.attrs.user.erase("style"); e.attrs.user.erase("field");
        e.attrs.user["style"] = t.style;
        if (!t.field.empty()) e.attrs.user["field"] = t.field;
        out.push_back(std::move(e));
    }
}

void read_text_entities(const std::vector<gdoc::Entity>& in, gdoc::SpaceId space,
                        std::vector<Canvas::TextItem>& texts) {
    for (const auto& e : in) {
        if (e.kind != gdoc::Kind::MText || e.space != space ||
            e.payload.points.empty() || e.payload.reals.size() < 3 ||
            e.payload.ints.size() < 2) continue;
        Canvas::TextItem t;
        t.layer = e.layer;
        t.attrs=e.attrs;
        t.pos = flatten(e.payload.points[0]);
        t.height = e.payload.reals[0];
        t.angle = e.payload.reals[1];
        t.width = e.payload.reals[2];
        t.halign = static_cast<int>(e.payload.ints[0]);
        t.valign = static_cast<int>(e.payload.ints[1]);
        t.text = e.payload.text;
        t.selection_id = e.id.lo;
        if (const auto* style = user_value(e.attrs, "style")) t.style = *style;
        if (const auto* field = user_value(e.attrs, "field")) t.field = *field;
        texts.push_back(std::move(t));
    }
}

}
