#include "exchange/mesh_export.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string_view>
#include <tuple>

#include "exchange/dwf.hpp"
#include "geometry/triangulate.hpp"

namespace gmesh {

namespace {

void put32(std::vector<u8>& out, u32 v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}

void put_float(std::vector<u8>& out, f64 v) {
    put32(out, std::bit_cast<u32>(static_cast<float>(v)));
}

u32 get32(const std::vector<u8>& in, std::size_t at) {
    return u32(in[at]) | (u32(in[at + 1]) << 8) | (u32(in[at + 2]) << 16) | (u32(in[at + 3]) << 24);
}

f64 get_float(const std::vector<u8>& in, std::size_t at) {
    return static_cast<f64>(std::bit_cast<float>(get32(in, at)));
}

f64 signed_volume(const Object& o) {
    f64 total = 0;
    for (std::size_t t = 0; t + 2 < o.triangles.size(); t += 3) {
        const f64* a = &o.vertices[3 * o.triangles[t]];
        const f64* b = &o.vertices[3 * o.triangles[t + 1]];
        const f64* c = &o.vertices[3 * o.triangles[t + 2]];
        total += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) +
                 a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
    return total / 6.0;
}

void facet_normal(const Object& o, std::size_t t, f64 n[3]) {
    const f64* a = &o.vertices[3 * o.triangles[t]];
    const f64* b = &o.vertices[3 * o.triangles[t + 1]];
    const f64* c = &o.vertices[3 * o.triangles[t + 2]];
    const f64 u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const f64 w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    n[0] = u[1] * w[2] - u[2] * w[1];
    n[1] = u[2] * w[0] - u[0] * w[2];
    n[2] = u[0] * w[1] - u[1] * w[0];
    const f64 l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 0) { n[0] /= l; n[1] /= l; n[2] /= l; }
    else n[0] = n[1] = n[2] = 0;
}

void append(std::string& s, f64 v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.10g", v == 0 ? 0.0 : v);
    s += buf;
}

std::vector<u8> bytes_of(std::string_view s) { return {s.begin(), s.end()}; }

bool attribute(std::string_view tag, std::string_view key, std::string_view& value) {
    std::size_t at = 0;
    while ((at = tag.find(key, at)) != std::string_view::npos) {
        const bool start = at == 0 || tag[at - 1] == ' ' || tag[at - 1] == '\t' ||
                           tag[at - 1] == '\n' || tag[at - 1] == '\r';
        std::size_t p = at + key.size();
        while (p < tag.size() && (tag[p] == ' ' || tag[p] == '\t')) ++p;
        if (start && p < tag.size() && tag[p] == '=') {
            ++p;
            while (p < tag.size() && (tag[p] == ' ' || tag[p] == '\t')) ++p;
            if (p >= tag.size() || (tag[p] != '"' && tag[p] != '\'')) return false;
            const char q = tag[p];
            const std::size_t end = tag.find(q, p + 1);
            if (end == std::string_view::npos) return false;
            value = tag.substr(p + 1, end - p - 1);
            return true;
        }
        at += key.size();
    }
    return false;
}

bool number(std::string_view s, f64& v) {
    const std::string text(s);
    char* end = nullptr;
    v = std::strtod(text.c_str(), &end);
    return end && end != text.c_str() && *end == 0 && std::isfinite(v);
}

}

f64 metres_per(Unit u) noexcept {
    switch (u) {
        case Unit::Micrometre: return 1e-6;
        case Unit::Millimetre: return 1e-3;
        case Unit::Centimetre: return 1e-2;
        case Unit::Metre:      return 1.0;
        case Unit::Inch:       return 0.0254;
        case Unit::Foot:       return 0.3048;
    }
    return 1e-3;
}

const char* unit_name_3mf(Unit u) noexcept {
    switch (u) {
        case Unit::Micrometre: return "micron";
        case Unit::Millimetre: return "millimeter";
        case Unit::Centimetre: return "centimeter";
        case Unit::Metre:      return "meter";
        case Unit::Inch:       return "inch";
        case Unit::Foot:       return "foot";
    }
    return "millimeter";
}

bool unit_from_3mf(std::string_view name, Unit& u) noexcept {
    for (Unit c : {Unit::Micrometre, Unit::Millimetre, Unit::Centimetre, Unit::Metre, Unit::Inch, Unit::Foot})
        if (name == unit_name_3mf(c)) { u = c; return true; }
    return false;
}

std::vector<Object> mesh(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& v,
                         const Options& o) {
    const f64 scale = 1.0 / metres_per(o.unit);
    std::vector<Object> objects;
    for (std::size_t bi = 0; bi < bodies.size(); ++bi) {
        const gtopo::Body& b = bodies[bi];
        const gtopo::TriMesh tri = gtopo::triangulate(b, v);
        Object obj;
        obj.name = bodies.size() == 1 ? o.name : o.name + " " + std::to_string(bi + 1);
        std::map<std::tuple<gcm::i64, gcm::i64, gcm::i64>, u32> index;
        std::vector<u32> remap(b.vertex_count(), gtopo::kInvalid);
        const auto vertex = [&](u32 bv) {
            if (remap[bv] != gtopo::kInvalid) return remap[bv];
            const gcm::Lattice3& p = b.vertex_point[bv];
            const auto [it, added] = index.try_emplace({p.x, p.y, p.z}, u32(obj.vertex_count()));
            if (added) {
                f64 x, y, z;
                gcm::to_world(v, p, x, y, z);
                obj.vertices.insert(obj.vertices.end(), {x * scale, y * scale, z * scale});
            }
            return remap[bv] = it->second;
        };
        for (std::size_t t = 0; t + 2 < tri.indices.size(); t += 3) {
            const u32 a = vertex(tri.indices[t]), c1 = vertex(tri.indices[t + 1]), c2 = vertex(tri.indices[t + 2]);
            if (a == c1 || c1 == c2 || a == c2) continue;
            obj.triangles.insert(obj.triangles.end(), {a, c1, c2});
        }
        if (obj.triangles.empty()) continue;
        if (signed_volume(obj) < 0)
            for (std::size_t t = 0; t + 2 < obj.triangles.size(); t += 3)
                std::swap(obj.triangles[t + 1], obj.triangles[t + 2]);
        objects.push_back(std::move(obj));
    }
    return objects;
}

Result write_stl(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& v,
                 const Options& o) {
    Result r;
    if (bodies.empty()) { r.error = "no solid to export"; return r; }
    const std::vector<Object> objects = mesh(bodies, v, o);
    for (const Object& obj : objects) r.triangles += obj.triangle_count();
    if (r.triangles == 0) { r.error = "the solids produced no triangles"; return r; }
    if (r.triangles > 0xFFFFFFFFull) { r.error = "too many triangles for STL"; return r; }

    std::string header = o.name + " STL, units: " + unit_name_3mf(o.unit);
    if (header.rfind("solid", 0) == 0) header.insert(0, "GLCAD ");
    header.resize(80, ' ');
    r.bytes.assign(header.begin(), header.end());
    put32(r.bytes, static_cast<u32>(r.triangles));
    r.bytes.reserve(84 + 50 * r.triangles);
    for (const Object& obj : objects)
        for (std::size_t t = 0; t + 2 < obj.triangles.size(); t += 3) {
            f64 n[3];
            facet_normal(obj, t, n);
            for (f64 c : n) put_float(r.bytes, c);
            for (int k = 0; k < 3; ++k)
                for (int axis = 0; axis < 3; ++axis)
                    put_float(r.bytes, obj.vertices[3 * obj.triangles[t + k] + axis]);
            r.bytes.push_back(0);
            r.bytes.push_back(0);
        }
    r.objects = objects.size();
    r.ok = true;
    return r;
}

Result write_3mf(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& v,
                 const Options& o) {
    Result r;
    if (bodies.empty()) { r.error = "no solid to export"; return r; }
    const std::vector<Object> objects = mesh(bodies, v, o);
    if (objects.empty()) { r.error = "the solids produced no triangles"; return r; }

    std::string model;
    model += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    model += "<model unit=\"";
    model += unit_name_3mf(o.unit);
    model += "\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
    model += " <metadata name=\"Application\">GLCAD</metadata>\n <resources>\n";
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const Object& obj = objects[i];
        std::string name;
        for (char c : obj.name) {
            if (c == '&') name += "&amp;";
            else if (c == '<') name += "&lt;";
            else if (c == '>') name += "&gt;";
            else if (c == '"') name += "&quot;";
            else name += c;
        }
        model += "  <object id=\"" + std::to_string(i + 1) + "\" type=\"model\" name=\"" + name +
                 "\">\n   <mesh>\n    <vertices>\n";
        for (std::size_t k = 0; k < obj.vertex_count(); ++k) {
            model += "     <vertex x=\"";
            append(model, obj.vertices[3 * k]);
            model += "\" y=\"";
            append(model, obj.vertices[3 * k + 1]);
            model += "\" z=\"";
            append(model, obj.vertices[3 * k + 2]);
            model += "\"/>\n";
        }
        model += "    </vertices>\n    <triangles>\n";
        for (std::size_t t = 0; t + 2 < obj.triangles.size(); t += 3)
            model += "     <triangle v1=\"" + std::to_string(obj.triangles[t]) + "\" v2=\"" +
                     std::to_string(obj.triangles[t + 1]) + "\" v3=\"" +
                     std::to_string(obj.triangles[t + 2]) + "\"/>\n";
        model += "    </triangles>\n   </mesh>\n  </object>\n";
        r.triangles += obj.triangle_count();
    }
    model += " </resources>\n <build>\n";
    for (std::size_t i = 0; i < objects.size(); ++i)
        model += "  <item objectid=\"" + std::to_string(i + 1) + "\"/>\n";
    model += " </build>\n</model>\n";

    const std::string types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>"
        "</Types>\n";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>"
        "</Relationships>\n";
    r.bytes = gplot::zip({{"[Content_Types].xml", bytes_of(types)},
                          {"_rels/.rels", bytes_of(rels)},
                          {"3D/3dmodel.model", bytes_of(model)}});
    r.objects = objects.size();
    r.ok = true;
    return r;
}

ReadResult read_stl(const std::vector<u8>& bytes) {
    ReadResult r;
    if (bytes.size() >= 84 && 84 + 50ull * get32(bytes, 80) == bytes.size()) {
        Object obj;
        std::map<std::tuple<f64, f64, f64>, u32> index;
        const std::size_t count = get32(bytes, 80);
        for (std::size_t t = 0; t < count; ++t) {
            const std::size_t at = 84 + 50 * t + 12;
            for (int k = 0; k < 3; ++k) {
                const f64 x = get_float(bytes, at + 12 * k), y = get_float(bytes, at + 12 * k + 4),
                          z = get_float(bytes, at + 12 * k + 8);
                const auto [it, added] = index.try_emplace({x, y, z}, u32(obj.vertex_count()));
                if (added) obj.vertices.insert(obj.vertices.end(), {x, y, z});
                obj.triangles.push_back(it->second);
            }
        }
        r.objects.push_back(std::move(obj));
        r.ok = true;
        return r;
    }
    r.error = "not a binary STL file";
    return r;
}

ReadResult read_3mf(const std::vector<u8>& bytes) {
    ReadResult r;
    std::vector<u8> raw;
    if (!gplot::unzip_entry(bytes, "3D/3dmodel.model", raw)) {
        r.error = "no 3D/3dmodel.model part";
        return r;
    }
    const std::string_view xml(reinterpret_cast<const char*>(raw.data()), raw.size());
    Object* current = nullptr;
    std::size_t at = 0;
    while ((at = xml.find('<', at)) != std::string_view::npos) {
        const std::size_t end = xml.find('>', at);
        if (end == std::string_view::npos) break;
        const std::string_view tag = xml.substr(at + 1, end - at - 1);
        at = end + 1;
        std::string_view value;
        if (tag.rfind("model", 0) == 0 && (tag.size() == 5 || tag[5] == ' ' || tag[5] == '\n')) {
            if (attribute(tag, "unit", value) && !unit_from_3mf(value, r.unit)) {
                r.error = "unknown unit " + std::string(value);
                return r;
            }
        } else if (tag.rfind("object", 0) == 0 && tag.size() > 6 && tag[6] == ' ') {
            r.objects.emplace_back();
            current = &r.objects.back();
            if (attribute(tag, "name", value)) current->name = std::string(value);
        } else if (current && tag.rfind("vertex ", 0) == 0) {
            f64 c[3];
            const char* keys[3] = {"x", "y", "z"};
            for (int k = 0; k < 3; ++k)
                if (!attribute(tag, keys[k], value) || !number(value, c[k])) {
                    r.error = "bad vertex";
                    return r;
                }
            current->vertices.insert(current->vertices.end(), {c[0], c[1], c[2]});
        } else if (current && tag.rfind("triangle ", 0) == 0) {
            const char* keys[3] = {"v1", "v2", "v3"};
            for (const char* key : keys) {
                f64 index = -1;
                if (!attribute(tag, key, value) || !number(value, index) || index < 0 ||
                    index >= f64(current->vertex_count())) {
                    r.error = "bad triangle";
                    return r;
                }
                current->triangles.push_back(static_cast<u32>(index));
            }
        }
    }
    if (r.objects.empty()) { r.error = "no objects"; return r; }
    r.ok = true;
    return r;
}

}
