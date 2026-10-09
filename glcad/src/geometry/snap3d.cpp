#include "geometry/snap3d.hpp"

#include <algorithm>
#include <cmath>

namespace gsnap {

namespace {

struct W3 { double x, y, z; };

W3 world(const gcm::WorkingVolume& v, Point3 p) {
    double x, y, z;
    gcm::to_world(v, p, x, y, z);
    return {x, y, z};
}

double norm3(double x, double y, double z) {
    return std::sqrt(x * x + y * y + z * z);
}

bool face_plane(const gcm::WorkingVolume& v, const gtopo::Body& b,
                gtopo::FaceId f, W3& n, W3& c) {
    if (f >= b.face_count()) return false;
    const gcm::u32 l = b.face_loop_begin(f);
    if (l >= b.loop_count()) return false;

    const gcm::u32 begin = b.loop_begin(l), end = b.loop_end(l);
    const gcm::u32 count = end - begin;
    if (count < 3) return false;

    n = {0, 0, 0};
    c = {0, 0, 0};
    for (gcm::u32 i = 0; i < count; ++i) {
        const W3 a = world(v, b.vertex_point[b.he_vertex[begin + i]]);
        const W3 d = world(v, b.vertex_point[b.he_vertex[begin + (i + 1) % count]]);
        n.x += (a.y - d.y) * (a.z + d.z);
        n.y += (a.z - d.z) * (a.x + d.x);
        n.z += (a.x - d.x) * (a.y + d.y);
        c.x += a.x;
        c.y += a.y;
        c.z += a.z;
    }
    const double len = norm3(n.x, n.y, n.z);
    if (len < 1e-18) return false;
    n = {n.x / len, n.y / len, n.z / len};
    c = {c.x / count, c.y / count, c.z / count};
    return true;
}

}

gcm::u32 Engine3::add(const gtopo::Body* b) {
    if (!b) return gtopo::kInvalid;
    bodies_.push_back(b);
    return static_cast<gcm::u32>(bodies_.size() - 1);
}

Result3 Engine3::snap_point(const gcm::WorkingVolume& v, double wx, double wy,
                            double wz) const {
    Result3 best;
    if (!settings.enabled && settings.forced == None) return best;
    const double aperture = settings.aperture_world;
    double best_score = aperture;

    for (const gtopo::Body* b : bodies_) {
        if (!b) continue;

        if (settings.wants(Endpoint)) {
            for (gtopo::VertexId i = 0; i < b->vertex_count(); ++i) {
                const W3 p = world(v, b->vertex_point[i]);
                const double d = norm3(p.x - wx, p.y - wy, p.z - wz);
                if (d >= best_score) continue;
                best_score = d;
                best = Result3{};
                best.valid = true;
                best.kind = Endpoint;
                best.point = b->vertex_point[i];
                best.vertex = i;
                best.distance = d;
            }
        }

        if (settings.wants(Midpoint)) {
            for (gcm::u32 h = 0; h < b->halfedge_count(); ++h) {
                const gcm::u32 twin = b->he_twin[h];
                if (twin != gtopo::kInvalid && twin < h) continue;
                const W3 a = world(v, b->vertex_point[b->he_vertex[h]]);
                const W3 c = world(v, b->vertex_point[b->he_vertex[b->he_next[h]]]);
                const W3 m{(a.x + c.x) * 0.5, (a.y + c.y) * 0.5, (a.z + c.z) * 0.5};
                const double d = norm3(m.x - wx, m.y - wy, m.z - wz);
                if (d >= best_score) continue;
                if (best.valid && best.kind == Endpoint && d >= best.distance) continue;
                best_score = d;
                best = Result3{};
                best.valid = true;
                best.kind = Midpoint;
                best.point = gcm::to_lattice(v, m.x, m.y, m.z);
                best.edge = h;
                best.distance = d;
            }
        }

        if (settings.wants(Center) || settings.wants(GeometricCenter)) {
            for (gtopo::FaceId f = 0; f < b->face_count(); ++f) {
                W3 n, c;
                if (!face_plane(v, *b, f, n, c)) continue;
                const double d = norm3(c.x - wx, c.y - wy, c.z - wz);
                if (d >= best_score) continue;
                best_score = d;
                best = Result3{};
                best.valid = true;
                best.kind = Center;
                best.point = gcm::to_lattice(v, c.x, c.y, c.z);
                best.face = f;
                best.distance = d;
            }
        }
    }
    return best;
}

Result3 Engine3::snap(const gcm::WorkingVolume& v, const Ray3& ray) const {
    Result3 best;
    const double dl = norm3(ray.dx, ray.dy, ray.dz);
    if (dl < 1e-18) return best;
    const double ux = ray.dx / dl, uy = ray.dy / dl, uz = ray.dz / dl;

    double best_score = settings.aperture_world;

    auto consider = [&](Point3 lp, Kind k, gtopo::FaceId f, gtopo::VertexId vtx,
                        gtopo::HalfedgeId he) {
        if (!settings.wants(k)) return;
        const W3 p = world(v, lp);
        const double tx = p.x - ray.ox, ty = p.y - ray.oy, tz = p.z - ray.oz;
        const double along = tx * ux + ty * uy + tz * uz;
        if (along < 0.0) return;
        const double cx = tx - ux * along, cy = ty - uy * along, cz = tz - uz * along;
        const double d = norm3(cx, cy, cz);
        if (d >= best_score) return;
        best_score = d;
        best = Result3{};
        best.valid = true;
        best.kind = k;
        best.point = lp;
        best.face = f;
        best.vertex = vtx;
        best.edge = he;
        best.distance = d;
    };

    for (const gtopo::Body* b : bodies_) {
        if (!b) continue;
        for (gtopo::VertexId i = 0; i < b->vertex_count(); ++i)
            consider(b->vertex_point[i], Endpoint, gtopo::kInvalid, i,
                     gtopo::kInvalid);

        for (gcm::u32 h = 0; h < b->halfedge_count(); ++h) {
            const gcm::u32 twin = b->he_twin[h];
            if (twin != gtopo::kInvalid && twin < h) continue;
            const W3 a = world(v, b->vertex_point[b->he_vertex[h]]);
            const W3 c = world(v, b->vertex_point[b->he_vertex[b->he_next[h]]]);
            consider(gcm::to_lattice(v, (a.x + c.x) * 0.5, (a.y + c.y) * 0.5,
                                     (a.z + c.z) * 0.5),
                     Midpoint, gtopo::kInvalid, gtopo::kInvalid, h);
        }

        for (gtopo::FaceId f = 0; f < b->face_count(); ++f) {
            W3 n, c;
            if (!face_plane(v, *b, f, n, c)) continue;
            consider(gcm::to_lattice(v, c.x, c.y, c.z), Center, f,
                     gtopo::kInvalid, gtopo::kInvalid);
        }
    }
    return best;
}

Engine3::Axis Engine3::face_normal_axis(const gcm::WorkingVolume& v, gcm::u32 body,
                                        gtopo::FaceId f) const {
    Axis a;
    if (body >= bodies_.size() || !bodies_[body]) return a;
    W3 n, c;
    if (!face_plane(v, *bodies_[body], f, n, c)) return a;
    a.valid = true;
    a.ox = c.x; a.oy = c.y; a.oz = c.z;
    a.dx = n.x; a.dy = n.y; a.dz = n.z;
    return a;
}

Engine3::Axis Engine3::edge_axis(const gcm::WorkingVolume& v, gcm::u32 body,
                                 gtopo::HalfedgeId he) const {
    Axis a;
    if (body >= bodies_.size() || !bodies_[body]) return a;
    const gtopo::Body& b = *bodies_[body];
    if (he >= b.halfedge_count()) return a;

    const W3 p = world(v, b.vertex_point[b.he_vertex[he]]);
    const W3 q = world(v, b.vertex_point[b.he_vertex[b.he_next[he]]]);
    const double len = norm3(q.x - p.x, q.y - p.y, q.z - p.z);
    if (len < 1e-15) return a;

    a.valid = true;
    a.ox = p.x; a.oy = p.y; a.oz = p.z;
    a.dx = (q.x - p.x) / len;
    a.dy = (q.y - p.y) / len;
    a.dz = (q.z - p.z) / len;
    return a;
}

bool Engine3::lock_to_axis(const Axis& a, double wx, double wy, double wz,
                           double& ox, double& oy, double& oz) {
    if (!a.valid) return false;
    const double len = norm3(a.dx, a.dy, a.dz);
    if (len < 1e-15) return false;
    const double ux = a.dx / len, uy = a.dy / len, uz = a.dz / len;
    const double t = (wx - a.ox) * ux + (wy - a.oy) * uy + (wz - a.oz) * uz;
    ox = a.ox + ux * t;
    oy = a.oy + uy * t;
    oz = a.oz + uz * t;
    return true;
}

GlyphShape glyph_for(Kind k) noexcept {
    switch (k) {
        case Endpoint:             return GlyphShape::Square;
        case Midpoint:             return GlyphShape::Triangle;
        case Center:               return GlyphShape::Circle;
        case GeometricCenter:      return GlyphShape::CircleDot;
        case Quadrant:             return GlyphShape::Diamond;
        case Intersection:         return GlyphShape::Cross;
        case ApparentIntersection: return GlyphShape::DoubleCross;
        case Perpendicular:        return GlyphShape::Perpendicular;
        case Tangent:              return GlyphShape::TangentCircle;
        case Nearest:              return GlyphShape::Hourglass;
        case Parallel:             return GlyphShape::Parallel;
        case Insertion:            return GlyphShape::Ellipse;
        case Node:                 return GlyphShape::Node;
        default:                   return GlyphShape::Dashes;
    }
}

const char* glyph_name(GlyphShape g) noexcept {
    switch (g) {
        case GlyphShape::Square:        return "square";
        case GlyphShape::Triangle:      return "triangle";
        case GlyphShape::Circle:        return "circle";
        case GlyphShape::CircleDot:     return "circle-dot";
        case GlyphShape::Diamond:       return "diamond";
        case GlyphShape::Cross:         return "cross";
        case GlyphShape::DoubleCross:   return "double-cross";
        case GlyphShape::Perpendicular: return "perpendicular";
        case GlyphShape::TangentCircle: return "tangent";
        case GlyphShape::Hourglass:     return "hourglass";
        case GlyphShape::Parallel:      return "parallel";
        case GlyphShape::Ellipse:       return "ellipse";
        case GlyphShape::Node:          return "node";
        case GlyphShape::Dashes:        return "dashes";
    }
    return "?";
}

std::vector<double> glyph_outline(GlyphShape g) {
    // Flat x0,y0,x1,y1 pairs on a unit square from -0.5 to 0.5.
    std::vector<double> out;
    auto line = [&](double x0, double y0, double x1, double y1) {
        out.insert(out.end(), {x0, y0, x1, y1});
    };
    auto ring = [&](double r, int steps) {
        double px = r, py = 0.0;
        for (int i = 1; i <= steps; ++i) {
            const double t = 6.28318530717958647692 * i / steps;
            const double nx = r * std::cos(t), ny = r * std::sin(t);
            line(px, py, nx, ny);
            px = nx;
            py = ny;
        }
    };

    switch (g) {
        case GlyphShape::Square:
            line(-0.5, -0.5, 0.5, -0.5);
            line(0.5, -0.5, 0.5, 0.5);
            line(0.5, 0.5, -0.5, 0.5);
            line(-0.5, 0.5, -0.5, -0.5);
            break;
        case GlyphShape::Triangle:
            line(-0.5, -0.4, 0.5, -0.4);
            line(0.5, -0.4, 0.0, 0.5);
            line(0.0, 0.5, -0.5, -0.4);
            break;
        case GlyphShape::Circle:
            ring(0.5, 16);
            break;
        case GlyphShape::CircleDot:
            ring(0.5, 16);
            ring(0.12, 8);
            break;
        case GlyphShape::Diamond:
            line(0.0, -0.5, 0.5, 0.0);
            line(0.5, 0.0, 0.0, 0.5);
            line(0.0, 0.5, -0.5, 0.0);
            line(-0.5, 0.0, 0.0, -0.5);
            break;
        case GlyphShape::Cross:
            line(-0.5, -0.5, 0.5, 0.5);
            line(-0.5, 0.5, 0.5, -0.5);
            break;
        case GlyphShape::DoubleCross:
            line(-0.5, -0.5, 0.5, 0.5);
            line(-0.5, 0.5, 0.5, -0.5);
            line(-0.5, -0.2, 0.2, 0.5);
            line(-0.2, -0.5, 0.5, 0.2);
            break;
        case GlyphShape::Perpendicular:
            line(-0.5, 0.5, -0.5, -0.5);
            line(-0.5, -0.5, 0.5, -0.5);
            line(-0.5, -0.1, -0.1, -0.1);
            line(-0.1, -0.1, -0.1, -0.5);
            break;
        case GlyphShape::TangentCircle:
            ring(0.4, 14);
            line(-0.5, 0.5, 0.5, 0.5);
            break;
        case GlyphShape::Hourglass:
            line(-0.5, 0.5, 0.5, 0.5);
            line(0.5, 0.5, -0.5, -0.5);
            line(-0.5, -0.5, 0.5, -0.5);
            line(0.5, -0.5, -0.5, 0.5);
            break;
        case GlyphShape::Parallel:
            line(-0.5, -0.25, 0.4, -0.05);
            line(-0.4, 0.05, 0.5, 0.25);
            break;
        case GlyphShape::Ellipse:
            for (int i = 0; i < 14; ++i) {
                const double t0 = 6.28318530717958647692 * i / 14;
                const double t1 = 6.28318530717958647692 * (i + 1) / 14;
                line(0.5 * std::cos(t0), 0.32 * std::sin(t0),
                     0.5 * std::cos(t1), 0.32 * std::sin(t1));
            }
            break;
        case GlyphShape::Node:
            line(-0.5, -0.5, 0.5, -0.5);
            line(0.5, -0.5, 0.5, 0.5);
            line(0.5, 0.5, -0.5, 0.5);
            line(-0.5, 0.5, -0.5, -0.5);
            line(-0.3, -0.3, 0.3, 0.3);
            line(-0.3, 0.3, 0.3, -0.3);
            break;
        case GlyphShape::Dashes:
            line(-0.5, 0.0, -0.2, 0.0);
            line(-0.05, 0.0, 0.05, 0.0);
            line(0.2, 0.0, 0.5, 0.0);
            break;
    }
    return out;
}

}
