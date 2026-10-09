#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>

#include "exchange/dwf.hpp"
#include "exchange/mesh_export.hpp"
#include "geometry/extrude.hpp"

namespace {

constexpr double kPi = 3.14159265358979323846;

gcm::WorkingVolume volume() {
    gcm::WorkingVolume v;
    v.extent = 10.0;
    v.resolution_bits = 32;
    return v;
}

gtopo::Body plate(const gcm::WorkingVolume& v) {
    gtopo::Profile p;
    p.outer = {gcm::to_lattice(v, 0, 0), gcm::to_lattice(v, 0.04, 0),
               gcm::to_lattice(v, 0.04, 0.03), gcm::to_lattice(v, 0, 0.03)};
    p.normalize();
    return gtopo::extrude(p, v, ggeo::d3::Frame<gcm::F64>::world_xy(), 0.02);
}

gtopo::Loop2 circle(const gcm::WorkingVolume& v, double r, int n) {
    gtopo::Loop2 loop;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPi * i / n;
        loop.push_back(gcm::to_lattice(v, 0.05 + r * std::cos(a), 0.05 + r * std::sin(a)));
    }
    return loop;
}

constexpr int kSides = 48;
constexpr double kOuter = 0.02, kInner = 0.008, kHeight = 0.01;

gtopo::Body washer(const gcm::WorkingVolume& v) {
    gtopo::Profile p;
    p.outer = circle(v, kOuter, kSides);
    p.holes = {circle(v, kInner, kSides)};
    p.normalize();
    return gtopo::extrude(p, v, ggeo::d3::Frame<gcm::F64>::world_xy(), kHeight);
}

double washer_mm3() {
    const double polygon = 0.5 * kSides * std::sin(2 * kPi / kSides);
    return polygon * (kOuter * kOuter - kInner * kInner) * kHeight * 1e9;
}

double signed_volume(const gmesh::Object& o) {
    double total = 0;
    for (std::size_t t = 0; t + 2 < o.triangles.size(); t += 3) {
        const double* a = &o.vertices[3 * o.triangles[t]];
        const double* b = &o.vertices[3 * o.triangles[t + 1]];
        const double* c = &o.vertices[3 * o.triangles[t + 2]];
        total += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) +
                 a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
    return total / 6.0;
}

bool watertight(const gmesh::Object& o) {
    std::map<std::pair<gcm::u32, gcm::u32>, int> directed;
    for (std::size_t t = 0; t + 2 < o.triangles.size(); t += 3)
        for (int k = 0; k < 3; ++k) {
            const gcm::u32 a = o.triangles[t + k], b = o.triangles[t + (k + 1) % 3];
            if (++directed[{a, b}] > 1) return false;
        }
    for (const auto& [edge, count] : directed)
        if (!directed.contains({edge.second, edge.first})) return false;
    return true;
}

void bounds(const gmesh::Object& o, double lo[3], double hi[3]) {
    for (int k = 0; k < 3; ++k) { lo[k] = 1e300; hi[k] = -1e300; }
    for (std::size_t i = 0; i < o.vertex_count(); ++i)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], o.vertices[3 * i + k]);
            hi[k] = std::max(hi[k], o.vertices[3 * i + k]);
        }
}

}

TEST(mesh_export_stl_box_is_binary_watertight_and_millimetres) {
    const auto v = volume();
    const auto r = gmesh::write_stl({plate(v)}, v);
    CHECK(r.ok);
    CHECK(r.triangles == 12);
    CHECK(r.bytes.size() == 84 + 50 * 12);
    CHECK(std::string(r.bytes.begin(), r.bytes.begin() + 5) != "solid");
    const auto back = gmesh::read_stl(r.bytes);
    CHECK(back.ok);
    CHECK(back.objects.size() == 1);
    const auto& o = back.objects[0];
    CHECK(o.triangle_count() == 12);
    CHECK(o.vertex_count() == 8);
    CHECK(watertight(o));
    CHECK_MSG(std::fabs(signed_volume(o) - 24000.0) < 1e-2, "volume %.9g", signed_volume(o));
    double lo[3], hi[3];
    bounds(o, lo, hi);
    CHECK(std::fabs(hi[0] - lo[0] - 40) < 1e-4);
    CHECK(std::fabs(hi[1] - lo[1] - 30) < 1e-4);
    CHECK(std::fabs(hi[2] - lo[2] - 20) < 1e-4);
}

TEST(mesh_export_stl_normals_point_out) {
    const auto v = volume();
    const auto r = gmesh::write_stl({plate(v)}, v);
    CHECK(r.ok);
    double cx = 20, cy = 15, cz = 10;
    for (std::size_t t = 0; t < r.triangles; ++t) {
        const std::size_t at = 84 + 50 * t;
        float n[3], p[3];
        for (int k = 0; k < 3; ++k) {
            std::memcpy(&n[k], &r.bytes[at + 4 * k], 4);
            std::memcpy(&p[k], &r.bytes[at + 12 + 4 * k], 4);
        }
        const double out = (p[0] - cx) * n[0] + (p[1] - cy) * n[1] + (p[2] - cz) * n[2];
        CHECK(out > 0);
    }
}

TEST(mesh_export_stl_washer_with_hole) {
    const auto v = volume();
    const auto r = gmesh::write_stl({washer(v)}, v);
    CHECK(r.ok);
    CHECK(r.triangles == 4 * kSides * 2);
    const auto back = gmesh::read_stl(r.bytes);
    CHECK(back.ok);
    const auto& o = back.objects[0];
    CHECK(o.vertex_count() == 4 * kSides);
    CHECK(watertight(o));
    CHECK(int(o.vertex_count()) - int(o.triangle_count() * 3 / 2) + int(o.triangle_count()) == 0);
    CHECK_MSG(std::fabs(signed_volume(o) - washer_mm3()) < 1e-3 * washer_mm3(),
              "volume %.9g expected %.9g", signed_volume(o), washer_mm3());
    const double exact = kPi * (kOuter * kOuter - kInner * kInner) * kHeight * 1e9;
    CHECK(std::fabs(signed_volume(o) - exact) < 0.01 * exact);
}

TEST(mesh_export_3mf_package_and_model) {
    const auto v = volume();
    const auto r = gmesh::write_3mf({plate(v), washer(v)}, v);
    CHECK(r.ok);
    CHECK(r.objects == 2);
    const auto names = gplot::zip_names(r.bytes);
    CHECK(names.size() == 3);
    std::vector<gcm::u8> part;
    CHECK(gplot::unzip_entry(r.bytes, "[Content_Types].xml", part));
    CHECK(gplot::unzip_entry(r.bytes, "_rels/.rels", part));
    const std::string rels(part.begin(), part.end());
    CHECK(rels.find("Target=\"/3D/3dmodel.model\"") != std::string::npos);
    const auto back = gmesh::read_3mf(r.bytes);
    CHECK(back.ok);
    CHECK(back.unit == gmesh::Unit::Millimetre);
    CHECK(back.objects.size() == 2);
    CHECK(back.objects[0].name == "GLCAD 1");
    CHECK(back.objects[0].vertex_count() == 8);
    CHECK(back.objects[0].triangle_count() == 12);
    CHECK(back.objects[1].triangle_count() == 4 * kSides * 2);
    for (const auto& o : back.objects) CHECK(watertight(o));
    CHECK(std::fabs(signed_volume(back.objects[0]) - 24000.0) < 1e-2);
    CHECK(std::fabs(signed_volume(back.objects[1]) - washer_mm3()) < 1e-6 * washer_mm3());
}

TEST(mesh_export_3mf_units) {
    const auto v = volume();
    gmesh::Options o;
    o.unit = gmesh::Unit::Inch;
    const auto r = gmesh::write_3mf({plate(v)}, v, o);
    CHECK(r.ok);
    const auto back = gmesh::read_3mf(r.bytes);
    CHECK(back.ok);
    CHECK(back.unit == gmesh::Unit::Inch);
    double lo[3], hi[3];
    bounds(back.objects[0], lo, hi);
    CHECK(std::fabs(hi[0] - lo[0] - 0.04 / 0.0254) < 1e-6);
}

TEST(mesh_export_refuses_without_solids) {
    const auto v = volume();
    const auto stl = gmesh::write_stl({}, v);
    CHECK(!stl.ok);
    CHECK(!stl.error.empty());
    CHECK(stl.bytes.empty());
    const auto three = gmesh::write_3mf({}, v);
    CHECK(!three.ok);
    CHECK(!three.error.empty());
}

TEST(mesh_export_readers_reject_garbage) {
    CHECK(!gmesh::read_stl({1, 2, 3}).ok);
    CHECK(!gmesh::read_3mf({1, 2, 3}).ok);
}
