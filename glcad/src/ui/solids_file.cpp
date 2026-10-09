#include "ui/solids_file.hpp"

#include <cstring>
#include <sstream>

namespace gcapp {

namespace {

struct Out {
    std::vector<gcm::u8> b;
    template <class T> void put(T v) {
        gcm::u8 tmp[sizeof(T)];
        std::memcpy(tmp, &v, sizeof(T));
        b.insert(b.end(), tmp, tmp + sizeof(T));
    }
};

struct In {
    const std::vector<gcm::u8>& b;
    std::size_t at{0};
    bool ok{true};
    template <class T> T get() {
        T v{};
        if (at + sizeof(T) > b.size()) { ok = false; return v; }
        std::memcpy(&v, b.data() + at, sizeof(T));
        at += sizeof(T);
        return v;
    }
};

constexpr gcm::u32 kMagic = 0x534F4C44;   // "SOLD"
constexpr gcm::u32 kVersion = 1;

}

std::vector<gcm::u8> encode_solids(const std::vector<gtopo::Body>& bodies) {
    Out o;
    o.put(kMagic);
    o.put(kVersion);
    o.put(static_cast<gcm::u32>(bodies.size()));
    for (const auto& b : bodies) {
        o.put(static_cast<gcm::u32>(b.vertex_point.size()));
        for (const auto& p : b.vertex_point) { o.put(p.x); o.put(p.y); o.put(p.z); }
        o.put(b.face_count());
        for (gcm::u32 f = 0; f < b.face_count(); ++f) {
            o.put(b.face_loop_count(f));
            for (gcm::u32 l = b.face_loop_begin(f); l < b.face_loop_end(f); ++l) {
                o.put(b.loop_size(l));
                for (gcm::u32 h = b.loop_begin(l); h < b.loop_end(l); ++h) o.put(b.he_vertex[h]);
            }
        }
        o.put(static_cast<gcm::u32>(b.face_surface.size()));
        for (const auto& s : b.face_surface) {
            o.put(static_cast<gcm::u8>(s.kind));
            for (double v : {s.ox, s.oy, s.oz, s.ax, s.ay, s.az, s.rx, s.ry, s.rz,
                             s.radius, s.minor_radius, s.half_angle})
                o.put(v);
        }
    }
    return o.b;
}

bool decode_solids(const std::vector<gcm::u8>& bytes, std::vector<gtopo::Body>& out) {
    In in{bytes};
    if (in.get<gcm::u32>() != kMagic) return false;
    if (in.get<gcm::u32>() != kVersion) return false;
    const gcm::u32 count = in.get<gcm::u32>();
    if (!in.ok || count > 100000) return false;
    for (gcm::u32 bi = 0; bi < count; ++bi) {
        gtopo::Builder bld;
        const gcm::u32 nv = in.get<gcm::u32>();
        if (!in.ok || nv > 50000000) return false;
        for (gcm::u32 i = 0; i < nv; ++i) {
            gcm::Lattice3 p;
            p.x = in.get<decltype(p.x)>(); p.y = in.get<decltype(p.y)>(); p.z = in.get<decltype(p.z)>();
            bld.add_vertex(p);
        }
        const gcm::u32 nf = in.get<gcm::u32>();
        if (!in.ok) return false;
        for (gcm::u32 f = 0; f < nf; ++f) {
            bld.begin_face();
            const gcm::u32 nl = in.get<gcm::u32>();
            for (gcm::u32 l = 0; l < nl && in.ok; ++l) {
                const gcm::u32 n = in.get<gcm::u32>();
                std::vector<gtopo::VertexId> loop;
                for (gcm::u32 k = 0; k < n && in.ok; ++k) {
                    const gcm::u32 v = in.get<gcm::u32>();
                    if (v >= nv) return false;
                    loop.push_back(v);
                }
                bld.add_loop(loop);
            }
            bld.end_face();
            if (!in.ok) return false;
        }
        gtopo::Body b = bld.build();
        const gcm::u32 ns = in.get<gcm::u32>();
        if (!in.ok) return false;
        for (gcm::u32 i = 0; i < ns; ++i) {
            gtopo::Surface s;
            s.kind = static_cast<gtopo::SurfaceKind>(in.get<gcm::u8>());
            double* fields[] = {&s.ox, &s.oy, &s.oz, &s.ax, &s.ay, &s.az, &s.rx, &s.ry, &s.rz,
                                &s.radius, &s.minor_radius, &s.half_angle};
            for (double* fld : fields) *fld = in.get<double>();
            b.face_surface.push_back(s);
        }
        if (!in.ok) return false;
        if (b.face_surface.size() != b.face_count()) b.face_surface.clear();
        out.push_back(std::move(b));
    }
    return in.ok;
}


std::string encode_feature_history(const std::vector<std::vector<Modeller::Feature>>& hist) {
    std::string h;
    for (std::size_t b = 0; b < hist.size(); ++b)
        for (const auto& f : hist[b]) {
            char rec[512];
            std::snprintf(rec, sizeof(rec), "%zu %d %d %.17g %d %d %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g ",
                          b, f.sketch, static_cast<int>(f.op), f.value, f.regenerable ? 1 : 0, f.on_body ? 1 : 0, f.has_seed ? 1 : 0,
                          f.seed_u, f.seed_v, f.axis_x, f.axis_y, f.axis_dx, f.axis_dy,
                          f.anchor.x.to_f64(), f.anchor.y.to_f64(), f.anchor.z.to_f64(),
                          f.frame.origin.x.to_f64(), f.frame.origin.y.to_f64(), f.frame.origin.z.to_f64(),
                          f.frame.u.x.to_f64(), f.frame.u.y.to_f64(), f.frame.u.z.to_f64(),
                          f.frame.v.x.to_f64(), f.frame.v.y.to_f64(), f.frame.v.z.to_f64(),
                          f.frame.n.x.to_f64(), f.frame.n.y.to_f64(), f.frame.n.z.to_f64());
            h += rec;
            h += std::to_string(f.profile.outer.size());
            for (const auto& q : f.profile.outer) h += " " + std::to_string(q.x) + " " + std::to_string(q.y);
            h += " " + std::to_string(f.profile.holes.size());
            for (const auto& hole : f.profile.holes) {
                h += " " + std::to_string(hole.size());
                for (const auto& q : hole) h += " " + std::to_string(q.x) + " " + std::to_string(q.y);
            }
            h += " |" + f.label + "\n";
        }
    return h;
}

}

namespace gcapp {
std::vector<std::vector<Modeller::Feature>> decode_feature_history(
    const std::vector<gcm::u8>& bytes, std::size_t body_count) {
        std::vector<std::vector<Modeller::Feature>> hist(body_count);
        const std::string t(bytes.begin(), bytes.end());
        std::size_t pos = 0;
        while (pos < t.size()) {
            const std::size_t nl = t.find('\n', pos);
            const std::string line = t.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? t.size() : nl + 1;
            std::size_t body; int sketch;
            std::istringstream in(line);
            if (!(in >> body >> sketch)) continue;
            if (body >= hist.size()) continue;
            Modeller::Feature f;
            f.sketch = sketch;
            const std::size_t bar = line.find('|');
            int op = 0, regen = 0, on_body = 0, has_seed = 0;
            double ax, ay, az, ox, oy, oz, ux, uy, uz, vx, vy, vz, nx, ny, nz;
            if (bar != std::string::npos && (in >> op >> f.value >> regen >> on_body >> has_seed >> f.seed_u >> f.seed_v
                                              >> f.axis_x >> f.axis_y >> f.axis_dx >> f.axis_dy >> ax >> ay >> az
                                              >> ox >> oy >> oz >> ux >> uy >> uz >> vx >> vy >> vz >> nx >> ny >> nz)) {
                f.op = static_cast<Modeller::Op>(op);
                f.regenerable = regen != 0; f.on_body = on_body != 0; f.has_seed = has_seed != 0;
                f.anchor = gview::Point3::from(ax, ay, az);
                f.frame.origin = gview::Point3::from(ox, oy, oz);
                f.frame.u = gview::Vec3::from(ux, uy, uz);
                f.frame.v = gview::Vec3::from(vx, vy, vz);
                f.frame.n = gview::Vec3::from(nx, ny, nz);
                std::size_t n = 0;
                if (in >> n) {
                    for (std::size_t k = 0; k < n; ++k) { long long qx, qy; if (!(in >> qx >> qy)) break; f.profile.outer.push_back({qx, qy}); }
                    std::size_t nh = 0;
                    if (in >> nh)
                        for (std::size_t hh = 0; hh < nh; ++hh) {
                            std::size_t m = 0;
                            if (!(in >> m)) break;
                            gtopo::Loop2 hole;
                            for (std::size_t k = 0; k < m; ++k) { long long qx, qy; if (!(in >> qx >> qy)) break; hole.push_back({qx, qy}); }
                            f.profile.holes.push_back(std::move(hole));
                        }
                }
                f.label = line.substr(bar + 1);
            } else {
                int consumed = 0;
                if (std::sscanf(line.c_str(), "%zu %d %n", &body, &sketch, &consumed) < 2) continue;
                f.label = line.substr(static_cast<std::size_t>(consumed));
            }
            hist[body].push_back(std::move(f));
        }
    return hist;
}
}
