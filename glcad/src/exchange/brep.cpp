#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "exchange/step.hpp"
#include "geometry/solid_edit.hpp"

namespace gstep {
namespace {

constexpr f64 kEps = 1e-12;

std::string number(f64 v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12g", v);
    std::string s = buf;
    if (s.find('.') == std::string::npos && s.find('E') == std::string::npos &&
        s.find('e') == std::string::npos)
        s += ".";
    return s;
}

struct V3 { f64 x{0}, y{0}, z{0}; };

V3 sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
f64 dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 norm(const V3& a) {
    const f64 n = std::sqrt(dot(a, a));
    return n > kEps ? V3{a.x / n, a.y / n, a.z / n} : V3{0, 0, 1};
}

struct Emitter {
    std::vector<std::string> lines;
    gcm::u64                 next{1};

    gcm::u64 emit(const std::string& body) {
        const gcm::u64 id = next++;
        lines.push_back("#" + std::to_string(id) + " = " + body + ";");
        return id;
    }
    std::string ref(gcm::u64 id) const { return "#" + std::to_string(id); }
};

std::string list_of(const std::vector<gcm::u64>& ids) {
    std::string s = "(";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) s += ",";
        s += "#" + std::to_string(ids[i]);
    }
    return s + ")";
}

}

std::string write(const gtopo::Body& b, const gcm::WorkingVolume& v,
                  const WriteOptions& o) {
    Emitter e;
    const f64 to_file = 1.0 / metres_per(o.unit);

    auto world = [&](const gcm::Lattice3& p) {
        f64 x, y, z;
        gcm::to_world(v, p, x, y, z);
        return V3{x * to_file, y * to_file, z * to_file};
    };

    auto point = [&](const V3& p) {
        return e.emit("CARTESIAN_POINT('',(" + number(p.x) + "," + number(p.y) +
                      "," + number(p.z) + "))");
    };
    auto direction = [&](const V3& d) {
        const V3 n = norm(d);
        return e.emit("DIRECTION('',(" + number(n.x) + "," + number(n.y) + "," +
                      number(n.z) + "))");
    };
    auto placement = [&](const V3& origin, const V3& axis, const V3& ref_dir) {
        const gcm::u64 p = point(origin);
        const gcm::u64 a = direction(axis);
        const gcm::u64 r = direction(ref_dir);
        return e.emit("AXIS2_PLACEMENT_3D('()'," + e.ref(p) + "," + e.ref(a) +
                      "," + e.ref(r) + ")");
    };

    std::vector<gcm::u64> vertex_id(b.vertex_count(), 0);
    for (gcm::u32 i = 0; i < b.vertex_count(); ++i) {
        const gcm::u64 p = point(world(b.vertex_point[i]));
        vertex_id[i] = e.emit("VERTEX_POINT('()'," + e.ref(p) + ")");
    }

    std::map<std::pair<gcm::u32, gcm::u32>, gcm::u64> edge_id;
    auto key_of = [](gcm::u32 a, gcm::u32 c) {
        return a < c ? std::make_pair(a, c) : std::make_pair(c, a);
    };

    struct CircleEdge { V3 centre, axis; f64 radius; };
    std::map<std::pair<gcm::u32,gcm::u32>,CircleEdge> circles;
    if (o.exact_surfaces) for (gcm::u32 f=0;f<b.face_count();++f) {
        const auto* sf=b.surface_of(f);
        if (!sf || sf->kind!=gtopo::SurfaceKind::Cylinder) continue;
        const V3 axis=norm({sf->ax,sf->ay,sf->az});
        const V3 centre{sf->ox*to_file,sf->oy*to_file,sf->oz*to_file};
        const f64 radius=sf->radius*to_file, tolerance=v.inv_scale()*to_file*8;
        for (gcm::u32 l=b.face_loop_begin(f);l<b.face_loop_end(f);++l)
            for (gcm::u32 h=b.loop_begin(l);h<b.loop_end(l);++h) {
                const auto key=key_of(b.he_vertex[h],b.he_vertex[b.he_next[h]]);
                const auto a=world(b.vertex_point[key.first]), c=world(b.vertex_point[key.second]);
                const auto da=sub(a,centre), dc=sub(c,centre);
                const f64 za=dot(da,axis), zc=dot(dc,axis);
                if (std::fabs(za-zc)>tolerance) continue;
                const V3 origin{centre.x+axis.x*za,centre.y+axis.y*za,centre.z+axis.z*za};
                const auto ra=sub(a,origin), rc=sub(c,origin);
                if (std::fabs(std::sqrt(dot(ra,ra))-radius)>tolerance ||
                    std::fabs(std::sqrt(dot(rc,rc))-radius)>tolerance) continue;
                // EDGE_CURVE runs from the lower vertex id to the higher; orient the arc that way.
                const f64 sign=dot(axis,cross(ra,rc))<0 ? -1 : 1;
                circles[key]={origin,{axis.x*sign,axis.y*sign,axis.z*sign},radius};
            }
    }

    auto edge_between = [&](gcm::u32 a, gcm::u32 c) {
        const auto key = key_of(a, c);
        const auto it = edge_id.find(key);
        if (it != edge_id.end()) return it->second;

        const V3 pa = world(b.vertex_point[key.first]);
        const V3 pb = world(b.vertex_point[key.second]);
        gcm::u64 geometry;
        if (const auto arc=circles.find(key);arc!=circles.end()) {
            const auto& circle=arc->second;
            const auto frame=placement(circle.centre,circle.axis,norm(sub(pa,circle.centre)));
            geometry=e.emit("CIRCLE('',"+e.ref(frame)+","+number(circle.radius)+")");
        } else {
            const gcm::u64 origin = point(pa);
            const gcm::u64 dir = direction(sub(pb, pa));
            const gcm::u64 vec = e.emit("VECTOR('()'," + e.ref(dir) + ",1.)");
            geometry = e.emit("LINE('()'," + e.ref(origin) + "," + e.ref(vec) + ")");
        }
        const gcm::u64 curve =
            e.emit("EDGE_CURVE('()'," + e.ref(vertex_id[key.first]) + "," +
                   e.ref(vertex_id[key.second]) + "," + e.ref(geometry) + ",.T.)");
        edge_id[key] = curve;
        return curve;
    };

    std::vector<gcm::u64> faces;
    for (gcm::u32 f = 0; f < b.face_count(); ++f) {
        const gtopo::Plane pl = gtopo::face_plane(b, f, v);
        if (!pl.valid()) continue;

        gcm::u64 surface = 0;
        const gtopo::Surface* sf = o.exact_surfaces ? b.surface_of(f) : nullptr;
        if (sf && sf->kind == gtopo::SurfaceKind::Cylinder) {
            const V3 axis = norm({sf->ax, sf->ay, sf->az});
            V3 seed{1, 0, 0};
            if (std::fabs(dot(axis, seed)) > 0.9) seed = {0, 1, 0};
            const V3 ref_dir = norm(cross(seed, axis));
            const gcm::u64 pl3 =
                placement({sf->ox * to_file, sf->oy * to_file, sf->oz * to_file},
                          axis, ref_dir);
            surface = e.emit("CYLINDRICAL_SURFACE('()'," + e.ref(pl3) + "," +
                             number(sf->radius * to_file) + ")");
        } else if (sf && sf->kind == gtopo::SurfaceKind::Sphere) {
            const gcm::u64 pl3 =
                placement({sf->ox * to_file, sf->oy * to_file, sf->oz * to_file},
                          {0, 0, 1}, {1, 0, 0});
            surface = e.emit("SPHERICAL_SURFACE('()'," + e.ref(pl3) + "," +
                             number(sf->radius * to_file) + ")");
        } else if (sf && sf->kind == gtopo::SurfaceKind::Cone) {
            const V3 axis = norm({sf->ax, sf->ay, sf->az});
            V3 seed{1, 0, 0};
            if (std::fabs(dot(axis, seed)) > 0.9) seed = {0, 1, 0};
            const V3 ref_dir = norm(cross(seed, axis));
            const gcm::u64 pl3 =
                placement({sf->ox * to_file, sf->oy * to_file, sf->oz * to_file},
                          axis, ref_dir);
            surface = e.emit("CONICAL_SURFACE('()'," + e.ref(pl3) + ",0.," +
                             number(sf->half_angle) + ")");
        } else if (sf && sf->kind == gtopo::SurfaceKind::Torus) {
            const V3 axis = norm({sf->ax, sf->ay, sf->az});
            V3 seed{1, 0, 0};
            if (std::fabs(dot(axis, seed)) > 0.9) seed = {0, 1, 0};
            const V3 ref_dir = norm(cross(seed, axis));
            const gcm::u64 pl3 =
                placement({sf->ox * to_file, sf->oy * to_file, sf->oz * to_file},
                          axis, ref_dir);
            surface = e.emit("TOROIDAL_SURFACE('()'," + e.ref(pl3) + "," +
                             number(sf->radius * to_file) + "," +
                             number(sf->minor_radius * to_file) + ")");
        }
        if (surface == 0) {
            const V3 n{pl.nx, pl.ny, pl.nz};
            V3 seed{1, 0, 0};
            if (std::fabs(dot(n, seed)) > 0.9) seed = {0, 1, 0};
            const V3 ref_dir = norm(cross(seed, n));
            const V3 origin{n.x * pl.d * to_file, n.y * pl.d * to_file,
                            n.z * pl.d * to_file};
            const gcm::u64 pl3 = placement(origin, n, ref_dir);
            surface = e.emit("PLANE('()'," + e.ref(pl3) + ")");
        }

        std::vector<gcm::u64> bounds;
        for (gcm::u32 loop = b.face_loop_begin(f); loop < b.face_loop_end(f);
             ++loop) {
            const gcm::u32 lb = b.loop_begin(loop), le = b.loop_end(loop);
            if (le - lb < 3) continue;

            std::vector<gcm::u64> oriented;
            for (gcm::u32 h = lb; h < le; ++h) {
                const gcm::u32 nx = (h + 1 < le) ? h + 1 : lb;
                const gcm::u32 a = b.he_vertex[h], c = b.he_vertex[nx];
                if (a == c) continue;
                const gcm::u64 curve = edge_between(a, c);
                const bool forward = a < c;
                oriented.push_back(
                    e.emit(std::string("ORIENTED_EDGE('()',*,*,") + e.ref(curve) +
                           "," + (forward ? ".T." : ".F.") + ")"));
            }
            if (oriented.size() < 3) continue;

            const gcm::u64 edge_loop =
                e.emit("EDGE_LOOP('()'," + list_of(oriented) + ")");
            // Loop 0 is the outer boundary; the rest are holes.
            const bool outer = loop == b.face_loop_begin(f);
            bounds.push_back(e.emit(
                std::string(outer ? "FACE_OUTER_BOUND" : "FACE_BOUND") +
                "('()'," + e.ref(edge_loop) + ",.T.)"));
        }
        if (bounds.empty()) continue;

        faces.push_back(e.emit("ADVANCED_FACE('()'," + list_of(bounds) + "," +
                               e.ref(surface) + ",.T.)"));
    }

    const gcm::u64 shell = e.emit("CLOSED_SHELL('()'," + list_of(faces) + ")");
    const gcm::u64 solid =
        e.emit("MANIFOLD_SOLID_BREP(" + encode_string(o.product_name) + "," +
               e.ref(shell) + ")");

    const char* si_prefix =
        o.unit == LengthUnit::Millimetre ? ".MILLI." :
        o.unit == LengthUnit::Centimetre ? ".CENTI." : "$";
    const bool si = o.unit == LengthUnit::Metre ||
                    o.unit == LengthUnit::Millimetre ||
                    o.unit == LengthUnit::Centimetre;

    gcm::u64 length_unit = 0;
    if (si) {
        length_unit = e.emit(
            std::string("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(") +
            (o.unit == LengthUnit::Metre ? "$" : si_prefix) + ",.METRE.) )");
    } else {
        const gcm::u64 metre =
            e.emit("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.) )");
        const gcm::u64 measure =
            e.emit("LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(" +
                   number(metres_per(o.unit)) + ")," + e.ref(metre) + ")");
        const gcm::u64 dimensions = e.emit("DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.)");
        length_unit = e.emit(
            "( CONVERSION_BASED_UNIT(" + encode_string(length_unit_name(o.unit)) +
            "," + e.ref(measure) + ") LENGTH_UNIT() NAMED_UNIT(" +
            e.ref(dimensions) + ") )");
    }

    const gcm::u64 angle =
        e.emit("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )");
    const gcm::u64 solid_angle =
        e.emit("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )");
    const gcm::u64 tolerance =
        e.emit("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(" +
               number(1.0 / v.scale() * to_file) + ")," + e.ref(length_unit) +
               ",'closure','tolerance')");
    const gcm::u64 context = e.emit(
        "( GEOMETRIC_REPRESENTATION_CONTEXT(3) "
        "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((" + e.ref(tolerance) + ")) "
        "GLOBAL_UNIT_ASSIGNED_CONTEXT((" + e.ref(length_unit) + "," +
        e.ref(angle) + "," + e.ref(solid_angle) + ")) "
        "REPRESENTATION_CONTEXT('','3D') )");

    const gcm::u64 rep =
        e.emit("ADVANCED_BREP_SHAPE_REPRESENTATION(" +
               encode_string(o.product_name) + ",(" + e.ref(solid) + ")," +
               e.ref(context) + ")");
    // AP214 readers find transferable roots through the product definition.
    const auto application=e.emit("APPLICATION_CONTEXT('automotive design')");
    e.emit("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,"+
           e.ref(application)+")");
    const auto product_context=e.emit("PRODUCT_CONTEXT('',"+e.ref(application)+",'mechanical')");
    const auto product=e.emit("PRODUCT("+encode_string(o.product_name)+","+
        encode_string(o.product_name)+",'',("+e.ref(product_context)+"))");
    const auto formation=e.emit("PRODUCT_DEFINITION_FORMATION('','',"+e.ref(product)+")");
    const auto definition_context=e.emit("PRODUCT_DEFINITION_CONTEXT('part definition',"+
        e.ref(application)+",'design')");
    const auto definition=e.emit("PRODUCT_DEFINITION('design','',"+e.ref(formation)+","+
        e.ref(definition_context)+")");
    const auto product_shape=e.emit("PRODUCT_DEFINITION_SHAPE('','',"+e.ref(definition)+")");
    e.emit("SHAPE_DEFINITION_REPRESENTATION("+e.ref(product_shape)+","+e.ref(rep)+")");

    std::ostringstream out;
    out << "ISO-10303-21;\n";
    out << "HEADER;\n";
    out << "FILE_DESCRIPTION(('" << "GLCAD advanced B-rep" << "'),'2;1');\n";
    out << "FILE_NAME(" << encode_string(o.product_name) << ",'"
        << o.timestamp << "',(" << encode_string(o.author) << "),("
        << encode_string(o.organisation) << "),"
        << encode_string("GLCAD") << "," << encode_string("GLCAD") << ",'');\n";
    out << "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 }'));\n";
    out << "ENDSEC;\n";
    out << "DATA;\n";
    for (const std::string& line : e.lines) out << line << "\n";
    out << "ENDSEC;\n";
    out << "END-ISO-10303-21;\n";
    return out.str();
}

namespace {

bool as_id(std::string_view s, gcm::u64& out) {
    if (s.empty() || s[0] != '#') return false;
    out = 0;
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return i > 1;
        out = out * 10 + static_cast<gcm::u64>(s[i] - '0');
    }
    return s.size() > 1;
}

std::vector<gcm::u64> as_id_list(std::string_view s) {
    std::vector<gcm::u64> out;
    std::string_view v = s;
    if (v.size() >= 2 && v.front() == '(' && v.back() == ')')
        v = v.substr(1, v.size() - 2);
    for (const std::string& piece : split_arguments(v)) {
        gcm::u64 id = 0;
        if (as_id(piece, id)) out.push_back(id);
    }
    return out;
}

bool as_real(std::string_view s, f64& out) {
    if (s.empty()) return false;
    try {
        out = std::stod(std::string(s));
        return true;
    } catch (...) {
        return false;
    }
}

}

ReadResult read(std::string_view text, const gcm::WorkingVolume& v) {
    ReadResult out;
    const ParseResult p = parse(text);
    if (!p.ok) {
        out.error = p.error.empty() ? "the file could not be parsed" : p.error;
        return out;
    }
    out.schema = p.schema;
    out.product_name = p.name;

    // Inch files carry a metre base plus a CONVERSION_BASED_UNIT; the conversion wins.
    bool found_si = false;
    LengthUnit si_unit = LengthUnit::Metre;
    bool found_conversion = false;
    LengthUnit conversion_unit = LengthUnit::Metre;

    for (const Instance& i : p.instances) {
        std::string all = i.type;
        for (const std::string& a : i.args) all += " " + a;
        if (all.find("LENGTH_UNIT") == std::string::npos) continue;

        if (all.find("CONVERSION_BASED_UNIT") != std::string::npos) {
            if (all.find("inch") != std::string::npos) {
                conversion_unit = LengthUnit::Inch;
                found_conversion = true;
            } else if (all.find("foot") != std::string::npos) {
                conversion_unit = LengthUnit::Foot;
                found_conversion = true;
            } else if (all.find("millimetre") != std::string::npos) {
                conversion_unit = LengthUnit::Millimetre;
                found_conversion = true;
            }
            continue;
        }
        if (all.find(".METRE.") == std::string::npos) continue;
        if (all.find(".MILLI.") != std::string::npos) si_unit = LengthUnit::Millimetre;
        else if (all.find(".CENTI.") != std::string::npos) si_unit = LengthUnit::Centimetre;
        else si_unit = LengthUnit::Metre;
        found_si = true;
    }

    if (found_conversion) {
        out.unit = conversion_unit;
        out.unit_stated = true;
    } else if (found_si) {
        out.unit = si_unit;
        out.unit_stated = true;
    }

    const f64 to_metres = metres_per(out.unit);

    std::unordered_map<gcm::u64, V3> points;
    for (const Instance& i : p.instances) {
        if (i.type != "CARTESIAN_POINT" || i.args.size() < 2) continue;
        const auto coords = split_arguments(
            i.args[1].size() >= 2 ? i.args[1].substr(1, i.args[1].size() - 2)
                                  : std::string());
        V3 q;
        if (coords.size() >= 1) as_real(coords[0], q.x);
        if (coords.size() >= 2) as_real(coords[1], q.y);
        if (coords.size() >= 3) as_real(coords[2], q.z);
        points[i.id] = {q.x * to_metres, q.y * to_metres, q.z * to_metres};
    }

    std::unordered_map<gcm::u64, gcm::u64> vertex_point;
    for (const Instance& i : p.instances) {
        if (i.type != "VERTEX_POINT" || i.args.size() < 2) continue;
        gcm::u64 id = 0;
        if (as_id(i.args[1], id)) vertex_point[i.id] = id;
    }

    // EDGE_CURVE(name, start, end, geometry, same_sense)
    struct Edge {
        gcm::u64 a{0}, b{0};
        gcm::u64 curve{0};
        bool     same_sense{true};
    };
    std::unordered_map<gcm::u64, Edge> edges;
    for (const Instance& i : p.instances) {
        if (i.type != "EDGE_CURVE" || i.args.size() < 3) continue;
        Edge e;
        if (!as_id(i.args[1], e.a) || !as_id(i.args[2], e.b)) continue;
        if (i.args.size() > 3) as_id(i.args[3], e.curve);
        if (i.args.size() > 4) e.same_sense = i.args[4].find('T') != std::string::npos;
        edges[i.id] = e;
    }

    struct Oriented { gcm::u64 edge{0}; bool forward{true}; };
    std::unordered_map<gcm::u64, Oriented> oriented;
    for (const Instance& i : p.instances) {
        if (i.type != "ORIENTED_EDGE" || i.args.size() < 5) continue;
        Oriented o;
        if (!as_id(i.args[3], o.edge)) continue;
        o.forward = i.args[4].find('T') != std::string::npos;
        oriented[i.id] = o;
    }

    std::unordered_map<gcm::u64, std::vector<gcm::u64>> loops;
    for (const Instance& i : p.instances) {
        if (i.type != "EDGE_LOOP" || i.args.size() < 2) continue;
        loops[i.id] = as_id_list(i.args[1]);
    }

    struct Bound { gcm::u64 loop{0}; bool outer{true}; bool forward{true}; };
    std::unordered_map<gcm::u64, Bound> bounds;
    for (const Instance& i : p.instances) {
        const bool outer = i.type == "FACE_OUTER_BOUND";
        if (!outer && i.type != "FACE_BOUND") continue;
        if (i.args.size() < 2) continue;
        Bound bd;
        bd.outer = outer;
        if (i.args.size()>2) bd.forward=i.args[2].find('T')!=std::string::npos;
        if (as_id(i.args[1], bd.loop)) bounds[i.id] = bd;
    }

    struct Cylinder { V3 origin, axis; f64 radius{0}; };
    // A circle's parameter starts at the ref direction.
    std::unordered_map<gcm::u64, gcm::u64> placement_point, placement_axis, placement_ref;
    for (const Instance& i : p.instances) {
        if (i.type != "AXIS2_PLACEMENT_3D" || i.args.size() < 3) continue;
        gcm::u64 pt = 0, ax = 0, rf = 0;
        if (as_id(i.args[1], pt)) placement_point[i.id] = pt;
        if (as_id(i.args[2], ax)) placement_axis[i.id] = ax;
        if (i.args.size() > 3 && as_id(i.args[3], rf)) placement_ref[i.id] = rf;
    }
    std::unordered_map<gcm::u64, V3> directions;
    for (const Instance& i : p.instances) {
        if (i.type != "DIRECTION" || i.args.size() < 2) continue;
        const auto c = split_arguments(
            i.args[1].size() >= 2 ? i.args[1].substr(1, i.args[1].size() - 2)
                                  : std::string());
        V3 d;
        if (c.size() >= 1) as_real(c[0], d.x);
        if (c.size() >= 2) as_real(c[1], d.y);
        if (c.size() >= 3) as_real(c[2], d.z);
        directions[i.id] = d;
    }
    struct Cone { V3 origin, axis; f64 radius{0}, half_angle{0}; };
    struct Torus { V3 origin, axis; f64 major{0}, minor{0}; };
    std::unordered_map<gcm::u64, Cone>  cones;
    std::unordered_map<gcm::u64, Torus> tori;
    std::unordered_map<gcm::u64, Cylinder> cylinders;
    for (const Instance& i : p.instances) {
        if (i.type != "CYLINDRICAL_SURFACE" || i.args.size() < 3) continue;
        gcm::u64 place = 0;
        if (!as_id(i.args[1], place)) continue;
        Cylinder c;
        if (!as_real(i.args[2], c.radius)) continue;
        c.radius *= to_metres;
        if (const auto pt = placement_point.find(place); pt != placement_point.end())
            if (const auto q = points.find(pt->second); q != points.end())
                c.origin = q->second;
        if (const auto ax = placement_axis.find(place); ax != placement_axis.end())
            if (const auto d = directions.find(ax->second); d != directions.end())
                c.axis = d->second;
        cylinders[i.id] = c;
    }
    for (const Instance& i : p.instances) {
        if (i.type != "CONICAL_SURFACE" || i.args.size() < 4) continue;
        gcm::u64 place = 0;
        if (!as_id(i.args[1], place)) continue;
        Cone c;
        if (!as_real(i.args[2], c.radius) || !as_real(i.args[3], c.half_angle)) continue;
        c.radius *= to_metres;
        if (const auto pt = placement_point.find(place); pt != placement_point.end())
            if (const auto q = points.find(pt->second); q != points.end()) c.origin = q->second;
        if (const auto ax = placement_axis.find(place); ax != placement_axis.end())
            if (const auto d = directions.find(ax->second); d != directions.end()) c.axis = d->second;
        cones[i.id] = c;
    }
    for (const Instance& i : p.instances) {
        if (i.type != "TOROIDAL_SURFACE" || i.args.size() < 4) continue;
        gcm::u64 place = 0;
        if (!as_id(i.args[1], place)) continue;
        Torus t;
        if (!as_real(i.args[2], t.major) || !as_real(i.args[3], t.minor)) continue;
        t.major *= to_metres;
        t.minor *= to_metres;
        if (const auto pt = placement_point.find(place); pt != placement_point.end())
            if (const auto q = points.find(pt->second); q != points.end()) t.origin = q->second;
        if (const auto ax = placement_axis.find(place); ax != placement_axis.end())
            if (const auto d = directions.find(ax->second); d != directions.end()) t.axis = d->second;
        tori[i.id] = t;
    }

    struct Circle { V3 centre, axis{0, 0, 1}, ref{1, 0, 0}; f64 radius{0}; bool ok{false}; };
    std::unordered_map<gcm::u64, Circle> circles;
    for (const Instance& i : p.instances) {
        if (i.type != "CIRCLE" || i.args.size() < 3) continue;
        gcm::u64 place = 0;
        if (!as_id(i.args[1], place)) continue;
        Circle c;
        if (!as_real(i.args[2], c.radius) || c.radius <= 0.0) continue;
        c.radius *= to_metres;
        if (const auto pt = placement_point.find(place); pt != placement_point.end())
            if (const auto q = points.find(pt->second); q != points.end()) c.centre = q->second;
        if (const auto ax = placement_axis.find(place); ax != placement_axis.end())
            if (const auto d = directions.find(ax->second); d != directions.end()) c.axis = d->second;
        if (const auto rf = placement_ref.find(place); rf != placement_ref.end())
            if (const auto d = directions.find(rf->second); d != directions.end()) c.ref = d->second;
        c.ok = true;
        circles[i.id] = c;
    }

    // EDGE_CURVE may reference a SURFACE_CURVE wrapper rather than the CIRCLE.
    std::unordered_map<gcm::u64,gcm::u64> curve_geometry;
    for (const Instance& i:p.instances) {
        if ((i.type!="SURFACE_CURVE" && i.type!="SEAM_CURVE") || i.args.size()<2) continue;
        gcm::u64 geometry=0;
        if (as_id(i.args[1],geometry)) curve_geometry[i.id]=geometry;
    }
    for (auto& [id,edge]:edges) {
        std::unordered_set<gcm::u64> visited;
        while (curve_geometry.contains(edge.curve) && visited.insert(edge.curve).second)
            edge.curve=curve_geometry.at(edge.curve);
    }

    gcm::u64 next_synthetic = 1;
    for (const auto& [id, q] : points) next_synthetic = std::max(next_synthetic, id);
    for (const auto& [id, q] : vertex_point) next_synthetic = std::max(next_synthetic, id);
    next_synthetic += 1000;

    std::size_t arcs_walked = 0;
    std::unordered_map<gcm::u64,std::vector<gcm::u64>> arc_vertices;

    // Excludes both ends.
    const auto arc_points = [&](gcm::u64 edge_id, const Edge& e, bool forward,
                                std::vector<gcm::u64>& out_ids) {
        if (const auto found=arc_vertices.find(edge_id);found!=arc_vertices.end()) {
            if (forward) out_ids.insert(out_ids.end(),found->second.begin(),found->second.end());
            else out_ids.insert(out_ids.end(),found->second.rbegin(),found->second.rend());
            return;
        }
        const auto ci = circles.find(e.curve);
        if (ci == circles.end() || !ci->second.ok) return;
        const Circle& c = ci->second;

        const auto world_of = [&](gcm::u64 vid, V3& q) {
            const auto vp = vertex_point.find(vid);
            if (vp == vertex_point.end()) return false;
            const auto pt = points.find(vp->second);
            if (pt == points.end()) return false;
            q = pt->second;
            return true;
        };
        V3 pa, pb;
        if (!world_of(e.a, pa)) return;
        if (!world_of(e.b, pb)) return;

        const auto norm = [](V3 d) {
            const f64 l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (l < 1e-15) return V3{1, 0, 0};
            return V3{d.x / l, d.y / l, d.z / l};
        };
        const V3 n = norm(c.axis);
        V3 x = norm(c.ref);
        // A file's ref need not be exactly perpendicular.
        const f64 dot = x.x * n.x + x.y * n.y + x.z * n.z;
        x = norm(V3{x.x - n.x * dot, x.y - n.y * dot, x.z - n.z * dot});
        const V3 y{n.y * x.z - n.z * x.y, n.z * x.x - n.x * x.z, n.x * x.y - n.y * x.x};

        const auto angle_of = [&](const V3& q) {
            const V3 d{q.x - c.centre.x, q.y - c.centre.y, q.z - c.centre.z};
            return std::atan2(d.x * y.x + d.y * y.y + d.z * y.z,
                              d.x * x.x + d.y * x.y + d.z * x.z);
        };
        constexpr f64 kTwoPi = 6.28318530717958647692;
        const f64 a0 = angle_of(pa);
        f64 a1 = angle_of(pb);

        const bool increasing = e.same_sense;
        if (increasing) { while (a1 <= a0 + 1e-9) a1 += kTwoPi; }
        else            { while (a1 >= a0 - 1e-9) a1 -= kTwoPi; }
        f64 sweep = a1 - a0;
        // Coincident ends are a full circle.
        if (std::fabs(sweep) < 1e-9) sweep = increasing ? kTwoPi : -kTwoPi;

        const f64 tol = 1e-4;
        const f64 max_step = c.radius > tol
                                 ? 2.0 * std::acos(std::max(0.0, 1.0 - tol / c.radius))
                                 : kTwoPi;
        int steps = int(std::ceil(std::fabs(sweep) / std::max(max_step, 1e-3)));
        steps = std::clamp(steps, 2, 4096);

        auto& samples=arc_vertices[edge_id];
        for (int k = 1; k < steps; ++k) {
            const f64 t = a0 + sweep * (f64(k) / f64(steps));
            const V3 q{c.centre.x + c.radius * (std::cos(t) * x.x + std::sin(t) * y.x),
                       c.centre.y + c.radius * (std::cos(t) * x.y + std::sin(t) * y.y),
                       c.centre.z + c.radius * (std::cos(t) * x.z + std::sin(t) * y.z)};
            const gcm::u64 pid = ++next_synthetic;
            const gcm::u64 vid = ++next_synthetic;
            points[pid] = q;
            vertex_point[vid] = pid;
            samples.push_back(vid);
        }
        if (forward) out_ids.insert(out_ids.end(),samples.begin(),samples.end());
        else out_ids.insert(out_ids.end(),samples.rbegin(),samples.rend());
        ++arcs_walked;
    };

    struct Face {
        std::vector<std::pair<std::vector<gcm::u64>, bool>> loops;
        gcm::u64 surface{0};
    };
    std::unordered_map<gcm::u64, Face> faces;

    for (const Instance& i : p.instances) {
        if (i.type != "ADVANCED_FACE" && i.type != "FACE_SURFACE") continue;
        if (i.args.size() < 3) continue;

        Face face;
        as_id(i.args[2], face.surface);
        for (gcm::u64 bid : as_id_list(i.args[1])) {
            const auto bd = bounds.find(bid);
            if (bd == bounds.end()) continue;
            const auto lp = loops.find(bd->second.loop);
            if (lp == loops.end()) continue;

            std::vector<gcm::u64> ring;
            for (gcm::u64 oid : lp->second) {
                const auto o = oriented.find(oid);
                if (o == oriented.end()) continue;
                const auto ed = edges.find(o->second.edge);
                if (ed == edges.end()) continue;
                ring.push_back(o->second.forward ? ed->second.a : ed->second.b);
                arc_points(o->second.edge, ed->second, o->second.forward, ring);
            }
            if (!bd->second.forward) std::reverse(ring.begin(),ring.end());
            if (ring.size() >= 3) face.loops.push_back({ring, bd->second.outer});
        }
        if (!face.loops.empty()) faces[i.id] = std::move(face);
    }

    std::vector<std::vector<gcm::u64>> shells;
    for (const Instance& i : p.instances) {
        if (i.type != "CLOSED_SHELL" && i.type != "OPEN_SHELL") continue;
        if (i.args.size() < 2) continue;
        shells.push_back(as_id_list(i.args[1]));
    }
    if (shells.empty() && !faces.empty()) {
        std::vector<gcm::u64> all;
        for (const auto& [id, f] : faces) all.push_back(id);
        std::sort(all.begin(), all.end());
        shells.push_back(std::move(all));
        out.skipped.push_back("the file has faces but no shell; they were "
                              "gathered into one body");
    }

    for (const std::vector<gcm::u64>& shell : shells) {
        gtopo::Builder builder;
        std::unordered_map<gcm::u64, gtopo::VertexId> made;
        std::vector<std::pair<gcm::u32, gcm::u64>> face_surfaces;

        auto vertex_for = [&](gcm::u64 vid) -> gtopo::VertexId {
            const auto it = made.find(vid);
            if (it != made.end()) return it->second;
            V3 q;
            if (const auto vp = vertex_point.find(vid); vp != vertex_point.end())
                if (const auto pt = points.find(vp->second); pt != points.end())
                    q = pt->second;
            const gtopo::VertexId id =
                builder.add_vertex(gcm::to_lattice(v, q.x, q.y, q.z));
            made[vid] = id;
            return id;
        };

        gcm::u32 face_index = 0;
        for (gcm::u64 fid : shell) {
            const auto it = faces.find(fid);
            if (it == faces.end()) continue;

            std::vector<const std::pair<std::vector<gcm::u64>, bool>*> ordered;
            for (const auto& l : it->second.loops) if (l.second) ordered.push_back(&l);
            for (const auto& l : it->second.loops) if (!l.second) ordered.push_back(&l);
            if (ordered.empty()) continue;

            // Keep cylindrical strips as quads; one nonplanar polygon flattens the side.
            if (ordered.size()==1 && cylinders.contains(it->second.surface)) {
                const auto& cylinder=cylinders.at(it->second.surface);
                const auto axis=norm(cylinder.axis);
                const auto& ring=ordered[0]->first;
                std::vector<V3> positions;
                std::vector<f64> heights;
                f64 lo=1e300,hi=-1e300;
                for (const auto id:ring) {
                    const auto vertex=vertex_point.find(id);
                    if (vertex==vertex_point.end() || !points.contains(vertex->second)) {
                        out.error="a cylindrical face references a missing vertex point";
                        out.bodies.clear();
                        return out;
                    }
                    const auto p=points.at(vertex->second);
                    positions.push_back(p);
                    const auto height=dot(sub(p,cylinder.origin),axis);
                    heights.push_back(height); lo=std::min(lo,height); hi=std::max(hi,height);
                }
                const f64 tolerance=std::max(v.inv_scale()*8,1e-9);
                std::vector<std::size_t> low,high;
                for (std::size_t k=0;k<ring.size();++k) {
                    if (std::fabs(heights[k]-lo)<tolerance) low.push_back(k);
                    else if (std::fabs(heights[k]-hi)<tolerance) high.push_back(k);
                }
                bool strip=hi-lo>tolerance && low.size()>=2 && low.size()==high.size() &&
                           low.size()+high.size()==ring.size();
                std::map<std::size_t,std::size_t> matching;
                std::map<std::size_t,gcm::u64> used;
                if (strip) for (const auto a:low) {
                    f64 best=1e300; std::size_t choice=0;
                    for (const auto c:high) {
                        const V3 delta=sub(positions[c],positions[a]);
                        const f64 along=dot(delta,axis);
                        const V3 radial{delta.x-axis.x*along,delta.y-axis.y*along,delta.z-axis.z*along};
                        const f64 distance=std::sqrt(dot(radial,radial));
                        if (distance<best) { best=distance; choice=c; }
                    }
                    // A full cylindrical face traverses its seam twice.
                    if (best>tolerance || (used.contains(choice) && used.at(choice)!=ring[a])) { strip=false; break; }
                    matching[a]=choice; used[choice]=ring[a];
                }
                if (strip) {
                    for (const auto a:low) {
                        const auto c=(a+1)%ring.size();
                        if (!matching.contains(c)) continue;
                        builder.begin_face();
                        builder.add_loop({vertex_for(ring[a]),vertex_for(ring[c]),
                            vertex_for(ring[matching[c]]),vertex_for(ring[matching[a]])});
                        builder.end_face();
                        face_surfaces.push_back({face_index++,it->second.surface});
                    }
                    continue;
                }
            }

            builder.begin_face();
            for (const auto* l : ordered) {
                std::vector<gtopo::VertexId> ring;
                for (gcm::u64 vid : l->first) ring.push_back(vertex_for(vid));
                builder.add_loop(ring);
            }
            builder.end_face();
            face_surfaces.push_back({face_index++, it->second.surface});
        }

        gtopo::Body body = builder.build();
        if (body.face_count() == 0) continue;

        bool any_curved = false;
        std::vector<gtopo::Surface> table(body.face_count());
        for (const auto& [index, sid] : face_surfaces) {
            if (index >= table.size()) continue;
            if (const auto c = cylinders.find(sid); c != cylinders.end()) {
                table[index] = gtopo::Surface::cylinder(
                    c->second.origin.x, c->second.origin.y, c->second.origin.z,
                    c->second.axis.x, c->second.axis.y, c->second.axis.z,
                    c->second.radius);
                any_curved = true;
            } else if (const auto k = cones.find(sid); k != cones.end()) {
                // Apex: at the placement when radius is zero, else back by radius/tan(angle).
                V3 apex = k->second.origin;
                const f64 ta = std::tan(k->second.half_angle);
                if (k->second.radius > 1e-12 && ta > 1e-12) {
                    const f64 back = k->second.radius / ta;
                    apex = {apex.x - k->second.axis.x * back,
                            apex.y - k->second.axis.y * back,
                            apex.z - k->second.axis.z * back};
                }
                table[index] = gtopo::Surface::cone(
                    apex.x, apex.y, apex.z,
                    k->second.axis.x, k->second.axis.y, k->second.axis.z,
                    k->second.half_angle);
                any_curved = true;
            } else if (const auto t = tori.find(sid); t != tori.end()) {
                table[index] = gtopo::Surface::torus(
                    t->second.origin.x, t->second.origin.y, t->second.origin.z,
                    t->second.axis.x, t->second.axis.y, t->second.axis.z,
                    t->second.major, t->second.minor);
                any_curved = true;
            }
        }
        if (any_curved) body.face_surface = std::move(table);

        out.bodies.push_back(std::move(body));
    }

    if (arcs_walked)
        out.skipped.push_back(std::to_string(arcs_walked) +
                              " curved edge(s) followed round rather than cut across");

    static const char* kKnown[] = {
        "CARTESIAN_POINT", "DIRECTION", "VECTOR", "LINE", "VERTEX_POINT",
        "EDGE_CURVE", "ORIENTED_EDGE", "EDGE_LOOP", "FACE_OUTER_BOUND",
        "FACE_BOUND", "ADVANCED_FACE", "FACE_SURFACE", "CLOSED_SHELL",
        "OPEN_SHELL", "MANIFOLD_SOLID_BREP", "PLANE", "CYLINDRICAL_SURFACE",
        "SPHERICAL_SURFACE", "CONICAL_SURFACE", "TOROIDAL_SURFACE",
        "AXIS2_PLACEMENT_3D", "AXIS2_PLACEMENT_2D", "CIRCLE",
        "SURFACE_CURVE", "SEAM_CURVE", "PCURVE", "DEFINITIONAL_REPRESENTATION",
        "ADVANCED_BREP_SHAPE_REPRESENTATION", "UNCERTAINTY_MEASURE_WITH_UNIT",
        "LENGTH_MEASURE_WITH_UNIT", "PRODUCT", "PRODUCT_DEFINITION",
        "DIMENSIONAL_EXPONENTS", "APPLICATION_CONTEXT", "APPLICATION_PROTOCOL_DEFINITION", "PRODUCT_CONTEXT",
        "PRODUCT_DEFINITION_FORMATION", "PRODUCT_DEFINITION_CONTEXT",
        "PRODUCT_DEFINITION_SHAPE", "SHAPE_DEFINITION_REPRESENTATION",
    };
    for (const Instance& i : p.instances) {
        bool known = i.type.empty();
        for (const char* k : kKnown) if (i.type == k) known = true;
        if (!known) ++out.unknown_entities;
    }

    out.ok = true;
    if (out.bodies.empty())
        out.error = "the file contained no solid this reader could build";
    return out;
}

}
