#include "test.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "exchange/xt.hpp"
#include "geometry/boolean.hpp"
#include "geometry/extrude.hpp"
#include "geometry/nurbs.hpp"

using namespace gxt;

namespace {

constexpr f64 kPi = 3.14159265358979323846;

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

gtopo::Body box(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1, f64 h) {
    gtopo::Profile p;
    p.outer = {gcm::to_lattice(v, x0, y0), gcm::to_lattice(v, x1, y0),
               gcm::to_lattice(v, x1, y1), gcm::to_lattice(v, x0, y1)};
    p.normalize();
    return gtopo::extrude(p, v, ggeo::d3::Frame<gcm::F64>::world_xy(), h);
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string nurbs_cylinder_xt() {
    std::string s;
    s += "**ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz**************************\n";
    s += "**PART2;SCH=SCH_1200000_12006;USFLD_SIZE=0;\n";
    s += "**END_OF_HEADER*****************************************************************\n";
    s += "T51 : TRANSMIT FILE created by modeller version 120000017 SCH_1200000_120060\n";
    // BODY fields, in XT reference order.
    s += "12 1 100 0 0 0 0 0 0 1e3 1e-8 0 0 0 1 0 1 1 5 0 0 0 2 17 0\n";
    // REGIONs (node_id attributes body next previous shell type)
    s += "19 2 1 0 1 3 0 4 V\n";
    s += "19 3 2 0 1 0 2 5 S\n";
    // SHELLs (node_id attributes body next face edge vertex region front_face)
    s += "13 4 3 0 1 0 0 0 0 2 6\n";
    s += "13 5 4 0 1 0 6 0 0 3 0\n";
    // FACEs (node_id attributes tolerance next previous loop shell surface sense ...)
    s += "14 6 5 0 ? 7 0 9 5 21 + 0 0 7 0 4\n";     // bottom
    s += "14 7 6 0 ? 8 6 10 5 22 + 0 0 8 6 4\n";    // top
    s += "14 8 7 0 ? 0 7 11 5 23 + 0 0 0 7 4\n";    // side, two loops 11 -> 12
    // LOOPs (node_id attributes fin face next)
    s += "15 9 8 0 13 6 0\n";
    s += "15 10 9 0 14 7 0\n";
    s += "15 11 10 0 15 8 12\n";
    s += "15 12 11 0 16 8 0\n";
    // FINs (attributes loop forward backward vertex other edge curve next_at_vx sense)
    s += "17 13 0 9 13 13 0 15 17 0 0 -\n";
    s += "17 14 0 10 14 14 0 16 18 0 0 +\n";
    s += "17 15 0 11 15 15 0 13 17 0 0 +\n";
    s += "17 16 0 12 16 16 0 14 18 0 0 -\n";
    // EDGEs (node_id attributes tolerance fin previous next curve next_on_curve previous_on_curve owner)
    s += "16 17 12 0 ? 13 0 18 19 0 0 5\n";
    s += "16 18 13 0 ? 14 17 0 20 0 0 5\n";
    // CIRCLEs (node_id attributes owner next previous geometric_owner sense centre normal x_axis radius)
    s += "31 19 14 0 17 0 0 0 + 0 0 0 0 0 1 1 0 0 1\n";
    s += "31 20 15 0 18 0 0 0 + 0 0 1 0 0 1 1 0 0 1\n";
    // PLANEs (... pvec normal x_axis)
    s += "50 21 16 0 6 0 0 0 + 0 0 0 0 0 -1 1 0 0\n";
    s += "50 22 17 0 7 0 0 0 + 0 0 1 0 0 1 1 0 0\n";
    // B_SURFACE (... nurbs data)
    s += "124 23 18 0 8 0 0 0 + 24 0\n";
    // NURBS_SURF fields, in XT reference order.
    s += "126 24 FF2 1 9 2 2 2 5 2 TTF2 4 25 26 27 28 29\n";
    // BSPLINE_VERTICES: 9 u rows of 2 v vertices, homogeneous (x w, y w, z w, w).
    {
        const f64 w = std::sqrt(2.0) / 2.0;
        const f64 pts[9][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}};
        std::string vs = "45 72 25";
        char buf[160];
        for (int i = 0; i < 9; ++i) {
            const f64 wi = (i % 2 == 1) ? w : 1.0;
            for (int j = 0; j < 2; ++j) {
                std::snprintf(buf, sizeof(buf), " %.17g %.17g %.17g %.17g", pts[i][0] * wi, pts[i][1] * wi, j * wi, wi);
                vs += buf;
            }
        }
        s += vs + "\n";
    }
    s += "127 5 26 3 2 2 2 3\n";
    s += "127 2 27 2 2\n";
    s += "128 5 28 0 .25 .5 .75 1\n";
    s += "128 2 29 0 1\n";
    s += "1 0\n";
    // Records join with nothing between them; a trailing number keeps its space.
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' && i > 0 && (std::isdigit(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == '?')) out += ' ';
        out += s[i];
    }
    return out;
}

}

TEST(xt_reads_an_onshape_brick) {
    const gcm::WorkingVolume v = vol();
    const ReadResult r = read_file(GLCAD_XT_FIXTURE, v);
    CHECK_MSG(r.ok, "read failed: %s", r.error.c_str());
    CHECK_MSG(r.schema == "SCH_3201212_32001_13006", "schema %s", r.schema.c_str());
    CHECK_MSG(r.bodies.size() == 1, "%zu bodies", r.bodies.size());
    CHECK_MSG(r.skipped.empty(), "%zu faces skipped: %s", r.skipped.size(),
              r.skipped.empty() ? "" : r.skipped[0].c_str());
    if (r.bodies.empty()) return;
    const gtopo::Body& b = r.bodies[0];
    CHECK_MSG(b.face_count() > 22, "%u faces", b.face_count());
    const gtopo::Topology t = b.analyse();
    CHECK(t.closed);
    CHECK(t.manifold);
    const f64 want = 0.0254 * 0.0254 * 0.0127;
    const f64 got = gtopo::volume(b, v);
    CHECK_MSG(std::fabs(got - want) < want * 1e-4, "volume %g, expected %g", got, want);
    CHECK(b.has_surfaces());
    int cylindrical = 0;
    for (gcm::u32 f = 0; f < b.face_count(); ++f)
        if (b.surface_of(f)->kind == gtopo::SurfaceKind::Cylinder) ++cylindrical;
    CHECK_MSG(cylindrical > 8, "%d cylindrical facets", cylindrical);
}

TEST(xt_dump_names_every_field) {
    std::string err;
    const std::string d = dump(slurp(GLCAD_XT_FIXTURE), &err);
    CHECK_MSG(err.empty(), "%s", err.c_str());
    CHECK(d.find("BODY") != std::string::npos);
    CHECK(d.find("region=") != std::string::npos);
    CHECK(d.find("FIN") != std::string::npos);
    CHECK(d.find("pvec=") != std::string::npos);
}

TEST(xt_box_round_trip) {
    const gcm::WorkingVolume v = vol();
    const gtopo::Body b = box(v, -3, -2, 5, 4, 7);
    const std::string text = write({b}, v);
    CHECK(text.find("SCH_1200000_12006") != std::string::npos);
    CHECK(text.rfind("1 0 \n") == text.size() - 5);
    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "read failed: %s", r.error.c_str());
    CHECK_MSG(r.bodies.size() == 1, "%zu bodies", r.bodies.size());
    CHECK_MSG(r.skipped.empty(), "%zu skipped: %s", r.skipped.size(), r.skipped.empty() ? "" : r.skipped[0].c_str());
    if (r.bodies.empty()) return;
    const gtopo::Body& back = r.bodies[0];
    CHECK_MSG(back.face_count() == b.face_count(), "%u faces, expected %u", back.face_count(), b.face_count());
    CHECK_MSG(back.vertex_count() == b.vertex_count(), "%u vertices", back.vertex_count());
    CHECK_MSG(std::fabs(gtopo::volume(back, v) - gtopo::volume(b, v)) < 1e-6, "volume %g, expected %g",
              gtopo::volume(back, v), gtopo::volume(b, v));
    const gtopo::Topology t = back.analyse();
    CHECK(t.closed);
    CHECK(t.manifold);
}

TEST(xt_reads_a_rational_nurbs_cylinder) {
    const gcm::WorkingVolume v = vol();
    const ReadResult r = read(nurbs_cylinder_xt(), v);
    CHECK_MSG(r.ok, "read failed: %s", r.error.c_str());
    CHECK_MSG(r.bodies.size() == 1, "%zu bodies", r.bodies.size());
    CHECK_MSG(r.skipped.empty(), "%zu skipped: %s", r.skipped.size(), r.skipped.empty() ? "" : r.skipped[0].c_str());
    if (r.bodies.empty()) return;
    const gtopo::Body& b = r.bodies[0];
    const gtopo::Topology t = b.analyse();
    CHECK_MSG(t.closed, "not closed (%u faces)", b.face_count());
    CHECK(t.manifold);
    const f64 want = 24.0 * std::sin(2.0 * kPi / 48.0);
    const f64 got = gtopo::volume(b, v);
    CHECK_MSG(std::fabs(got - want) < 1e-3, "volume %g, expected %g", got, want);
    CHECK(b.has_surfaces());
    int bspline = 0;
    for (gcm::u32 f = 0; f < b.face_count(); ++f)
        if (b.surface_of(f)->kind == gtopo::SurfaceKind::BSpline) ++bspline;
    CHECK_MSG(bspline == 96, "%d B-spline facets", bspline);
}

TEST(xt_writes_and_reads_nurbs_faces) {
    const gcm::WorkingVolume v = vol();
    gtopo::Body b = box(v, 0, 0, 2, 3, 4);
    b.face_surface.clear();
    for (gcm::u32 f = 0; f < b.face_count(); ++f) {
        auto ns = std::make_shared<ggeo::NurbsSurface<gcm::F64>>();
        ns->degree_u = 1; ns->degree_v = 1; ns->count_u = 2; ns->count_v = 2;
        const gcm::u32 l = b.face_loop_begin(f);
        gcm::u32 h = b.loop_begin(l);
        f64 c[4][3];
        for (int k = 0; k < 4; ++k) {
            gcm::to_world(v, b.vertex_point[b.he_vertex[h + k]], c[k][0], c[k][1], c[k][2]);
        }
        const int order[4] = {0, 1, 3, 2};
        for (int k = 0; k < 4; ++k) {
            ggeo::ControlPoint3<gcm::F64> cp;
            cp.p = ggeo::d3::Point<gcm::F64>::from(c[order[k]][0], c[order[k]][1], c[order[k]][2]);
            ns->control.push_back(cp);
        }
        ns->knots_u.knots = {0, 0, 1, 1};
        ns->knots_v.knots = {0, 0, 1, 1};
        CHECK(ns->valid());
        b.face_surface.push_back(gtopo::Surface::bspline(ns));
    }
    const std::string text = write({b}, v);
    CHECK(text.find("\n124 ") != std::string::npos);
    CHECK(text.find("\n126 ") != std::string::npos);
    const ReadResult r = read(text, v);
    CHECK_MSG(r.ok, "read failed: %s", r.error.c_str());
    CHECK_MSG(r.bodies.size() == 1, "%zu bodies", r.bodies.size());
    CHECK_MSG(r.skipped.empty(), "%zu skipped: %s", r.skipped.size(), r.skipped.empty() ? "" : r.skipped[0].c_str());
    if (r.bodies.empty()) return;
    const gtopo::Body& back = r.bodies[0];
    const gtopo::Topology t = back.analyse();
    CHECK(t.closed);
    CHECK(t.manifold);
    CHECK_MSG(std::fabs(gtopo::volume(back, v) - 24.0) < 1e-6, "volume %g", gtopo::volume(back, v));
    CHECK(back.has_surfaces());
    CHECK(back.surface_of(0)->kind == gtopo::SurfaceKind::BSpline);
}

TEST(xt_rejects_junk) {
    const gcm::WorkingVolume v = vol();
    CHECK(!read("hello", v).ok);
    CHECK(!read("**END_OF_HEADER\nT3 abc", v).ok);
}
