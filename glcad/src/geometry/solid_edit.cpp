#include "geometry/solid_edit.hpp"
#include "face_area.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "geometry/boolean.hpp"

namespace gtopo {

namespace {

struct P3 { f64 x, y, z; };

P3 world(const Volume& v, gcm::Lattice3 p) {
    f64 x, y, z;
    gcm::to_world(v, p, x, y, z);
    return {x, y, z};
}

std::vector<VertexId> outer_loop(const Body& b, FaceId f) {
    std::vector<VertexId> out;
    if (f >= b.face_count()) return out;
    const u32 l = b.face_loop_begin(f);
    if (l >= b.loop_count()) return out;
    for (u32 h = b.loop_begin(l); h < b.loop_end(l); ++h)
        out.push_back(b.he_vertex[h]);
    return out;
}

std::vector<std::vector<VertexId>> face_loops(const Body& b, FaceId f) {
    std::vector<std::vector<VertexId>> out;
    for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li) {
        std::vector<VertexId> loop;
        for (u32 h = b.loop_begin(li); h < b.loop_end(li); ++h)
            loop.push_back(b.he_vertex[h]);
        out.push_back(std::move(loop));
    }
    return out;
}

Body rebuild(const std::vector<gcm::Lattice3>& points,
             const std::vector<std::vector<std::vector<VertexId>>>& faces) {
    std::vector<u32> remap(points.size(), kInvalid);
    Builder bld;
    for (const auto& f : faces)
        for (const auto& loop : f)
            for (VertexId id : loop) {
                if (id >= points.size() || remap[id] != kInvalid) continue;
                remap[id] = bld.add_vertex(points[id]);
            }

    for (const auto& f : faces) {
        bld.begin_face();
        for (const auto& loop : f) {
            std::vector<VertexId> mapped;
            mapped.reserve(loop.size());
            for (VertexId id : loop)
                if (id < remap.size() && remap[id] != kInvalid)
                    mapped.push_back(remap[id]);
            bld.add_loop(mapped);
        }
        bld.end_face();
    }
    return bld.build();
}

f64 norm3(f64 x, f64 y, f64 z) { return std::sqrt(x * x + y * y + z * z); }

}

bool Plane::valid() const noexcept {
    const f64 n = norm3(nx, ny, nz);
    return n > 0.5 && n < 2.0 && std::isfinite(d);
}

Plane face_plane(const Body& b, FaceId f, const Volume& v) {
    Plane p;
    const std::vector<VertexId> loop = outer_loop(b, f);
    if (loop.size() < 3) return Plane{0, 0, 0, 0};

    const auto area = detail::loop_area(b, b.face_loop_begin(f));
    const f64 nx = detail::approximate_area(area[0]);
    const f64 ny = detail::approximate_area(area[1]);
    const f64 nz = detail::approximate_area(area[2]);
    const f64 n = norm3(nx, ny, nz);
    if (n == 0) return Plane{0, 0, 0, 0};
    f64 cx = 0, cy = 0, cz = 0;
    for (const auto id : loop) {
        const P3 point = world(v, b.vertex_point[id]);
        cx += point.x; cy += point.y; cz += point.z;
    }

    p.nx = nx / n;
    p.ny = ny / n;
    p.nz = nz / n;
    const f64 k = static_cast<f64>(loop.size());
    p.d = p.nx * (cx / k) + p.ny * (cy / k) + p.nz * (cz / k);
    return p;
}

f64 face_planarity(const Body& b, FaceId f, const Volume& v) {
    const Plane p = face_plane(b, f, v);
    if (!p.valid()) return 0.0;
    f64 worst = 0.0;
    for (const auto& loop : face_loops(b, f))
        for (VertexId id : loop) {
            const P3 w = world(v, b.vertex_point[id]);
            worst = std::max(worst, std::fabs(p.distance(w.x, w.y, w.z)));
        }
    return worst;
}

std::vector<FaceId> faces_at_vertex(const Body& b, VertexId v) {
    std::vector<FaceId> out;
    for (u32 h = 0; h < b.halfedge_count(); ++h) {
        if (b.he_vertex[h] != v) continue;
        const u32 l = b.he_loop[h];
        if (l >= b.loop_face.size()) continue;
        const FaceId f = b.loop_face[l];
        if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
    }
    return out;
}

bool faces_of_edge(const Body& b, HalfedgeId he, FaceId& a, FaceId& c) {
    if (he >= b.halfedge_count()) return false;
    const u32 twin = b.he_twin[he];
    if (twin == kInvalid || twin >= b.halfedge_count()) return false;
    a = b.loop_face[b.he_loop[he]];
    c = b.loop_face[b.he_loop[twin]];
    return true;
}

bool edge_is_convex(const Body& b, HalfedgeId he, const Volume& v) {
    FaceId fa, fb;
    if (!faces_of_edge(b, he, fa, fb)) return false;
    const Plane pa = face_plane(b, fa, v);
    const Plane pb = face_plane(b, fb, v);
    if (!pa.valid() || !pb.valid()) return false;

    // Material lies left of the halfedge in face A; use this local orientation.
    const P3 a = world(v, b.vertex_point[b.he_vertex[he]]);
    const P3 z = world(v, b.vertex_point[b.he_vertex[b.he_next[he]]]);
    const f64 ex = z.x-a.x, ey = z.y-a.y, ez = z.z-a.z;
    return (pa.ny*ez-pa.nz*ey)*pb.nx +
           (pa.nz*ex-pa.nx*ez)*pb.ny +
           (pa.nx*ey-pa.ny*ex)*pb.nz < 0.0;
}

bool intersect_planes(const Plane& a, const Plane& b, const Plane& c, f64& x,
                      f64& y, f64& z) {
    const f64 m[3][3] = {{a.nx, a.ny, a.nz}, {b.nx, b.ny, b.nz}, {c.nx, c.ny, c.nz}};
    const f64 det =
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
        m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::fabs(det) < 1e-9) return false;

    const f64 r[3] = {a.d, b.d, c.d};
    auto solve = [&](int col) {
        f64 t[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) t[i][j] = (j == col) ? r[i] : m[i][j];
        return t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) -
               t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) +
               t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0]);
    };
    x = solve(0) / det;
    y = solve(1) / det;
    z = solve(2) / det;
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

EditResult retarget_face(const Body& b, FaceId face, const Plane& target,
                         const Volume& v) {
    EditResult r;
    if (face >= b.face_count()) {
        r.reason = "no such face";
        return r;
    }
    if (!target.valid()) {
        r.reason = "the target plane is degenerate";
        return r;
    }
    if (!all_faces_planar(b)) {
        r.reason = "synchronous edits need a planar-faced solid";
        return r;
    }

    r.volume_before = volume(b, v);

    // Planes are taken before anything moves.
    std::vector<Plane> planes(b.face_count());
    for (FaceId f = 0; f < b.face_count(); ++f) planes[f] = face_plane(b, f, v);
    planes[face] = target;

    std::vector<gcm::Lattice3> points = b.vertex_point;

    for (const auto& loop : face_loops(b, face))
        for (VertexId id : loop) {
            const std::vector<FaceId> around = faces_at_vertex(b, id);
            if (around.size() < 3) {
                r.reason = "a vertex on that face is not the corner of three faces";
                return r;
            }

            f64 best = 0.0;
            bool found = false;
            f64 bx = 0, by = 0, bz = 0;
            for (std::size_t i = 0; i < around.size(); ++i) {
                if (around[i] == face) continue;
                for (std::size_t j = i + 1; j < around.size(); ++j) {
                    if (around[j] == face) continue;
                    const Plane& pi = planes[around[i]];
                    const Plane& pj = planes[around[j]];
                    if (!pi.valid() || !pj.valid()) continue;

                    const f64 det =
                        target.nx * (pi.ny * pj.nz - pi.nz * pj.ny) -
                        target.ny * (pi.nx * pj.nz - pi.nz * pj.nx) +
                        target.nz * (pi.nx * pj.ny - pi.ny * pj.nx);
                    if (std::fabs(det) <= best) continue;

                    f64 x, y, z;
                    if (!intersect_planes(target, pi, pj, x, y, z)) continue;
                    best = std::fabs(det);
                    bx = x; by = y; bz = z;
                    found = true;
                }
            }

            if (!found || best < 1e-6) {
                r.reason = "the neighbouring faces do not pin that corner";
                return r;
            }
            points[id] = gcm::to_lattice(v, bx, by, bz);
        }

    std::vector<std::vector<std::vector<VertexId>>> faces;
    for (FaceId f = 0; f < b.face_count(); ++f) faces.push_back(face_loops(b, f));

    r.body = rebuild(points, faces);
    const Topology t = r.body.analyse();
    if (!t.closed || !t.manifold) {
        r.reason = "the edit left the solid open";
        return r;
    }

    r.volume_after = volume(r.body, v);
    if (r.volume_after <= 0.0) {
        r.reason = "the edit turned the solid inside out";
        return r;
    }

    r.ok = true;
    return r;
}

EditResult move_face(const Body& b, FaceId face, Vec3 delta, const Volume& v) {
    EditResult r;
    if (face >= b.face_count()) {
        r.reason = "no such face";
        return r;
    }
    const Plane p = face_plane(b, face, v);
    if (!p.valid()) {
        r.reason = "that face has no usable plane";
        return r;
    }

    const f64 along = p.nx * delta.x.to_f64() + p.ny * delta.y.to_f64() +
                      p.nz * delta.z.to_f64();
    if (std::fabs(along) < 1e-12) {
        r.reason = "that move slides the face within its own plane";
        return r;
    }
    return retarget_face(b, face, p.offset(along), v);
}

EditResult offset_face(const Body& b, FaceId face, f64 distance, const Volume& v) {
    EditResult r;
    if (face >= b.face_count()) {
        r.reason = "no such face";
        return r;
    }
    const Plane p = face_plane(b, face, v);
    if (!p.valid()) {
        r.reason = "that face has no usable plane";
        return r;
    }
    if (std::fabs(distance) < 1e-12) {
        r.reason = "an offset of zero changes nothing";
        return r;
    }
    return retarget_face(b, face, p.offset(distance), v);
}

EditResult draft_face(const Body& b, FaceId face, const Plane& neutral,
                      f64 angle_deg, const Volume& v) {
    EditResult r;
    if (face >= b.face_count()) {
        r.reason = "no such face";
        return r;
    }
    const Plane p = face_plane(b, face, v);
    if (!p.valid() || !neutral.valid()) {
        r.reason = "the face or the neutral plane is degenerate";
        return r;
    }

    const f64 hx = p.ny * neutral.nz - p.nz * neutral.ny;
    const f64 hy = p.nz * neutral.nx - p.nx * neutral.nz;
    const f64 hz = p.nx * neutral.ny - p.ny * neutral.nx;
    const f64 h = norm3(hx, hy, hz);
    if (h < 1e-9) {
        r.reason = "the face is parallel to the neutral plane: no draft line";
        return r;
    }
    const f64 ux = hx / h, uy = hy / h, uz = hz / h;

    const f64 a = angle_deg * 3.14159265358979323846 / 180.0;
    const f64 c = std::cos(a), s = std::sin(a);
    const f64 dot = ux * p.nx + uy * p.ny + uz * p.nz;
    const f64 cx = uy * p.nz - uz * p.ny;
    const f64 cy = uz * p.nx - ux * p.nz;
    const f64 cz = ux * p.ny - uy * p.nx;

    Plane out;
    out.nx = p.nx * c + cx * s + ux * dot * (1.0 - c);
    out.ny = p.ny * c + cy * s + uy * dot * (1.0 - c);
    out.nz = p.nz * c + cz * s + uz * dot * (1.0 - c);
    const f64 n = norm3(out.nx, out.ny, out.nz);
    if (n < 1e-12) {
        r.reason = "the draft produced a degenerate plane";
        return r;
    }
    out.nx /= n;
    out.ny /= n;
    out.nz /= n;

    f64 px, py, pz;
    const Plane cut{ux, uy, uz, 0.0};
    if (!intersect_planes(p, neutral, cut, px, py, pz)) {
        r.reason = "could not locate the draft line";
        return r;
    }
    out.d = out.nx * px + out.ny * py + out.nz * pz;

    return retarget_face(b, face, out, v);
}

namespace {

bool setback_fits(const Body& b, FaceId face, P3 origin, P3 along, P3 inward,
                  double length, double setback, const Volume& vol) {
    const double eps=vol.inv_scale()*4;
    if (!std::isfinite(setback) || setback<=eps || length<=2*eps) return false;
    const auto project=[&](VertexId id) {
        const auto p=world(vol,b.vertex_point[id]);
        const P3 d{p.x-origin.x,p.y-origin.y,p.z-origin.z};
        return std::array<double,2>{d.x*along.x+d.y*along.y+d.z*along.z,
                                    d.x*inward.x+d.y*inward.y+d.z*inward.z};
    };
    for(u32 l=b.face_loop_begin(face);l<b.face_loop_end(face);++l)
        for(u32 h=b.loop_begin(l);h<b.loop_end(l);++h) {
            const auto a=project(b.he_vertex[h]),z=project(b.he_vertex[b.he_next[h]]);
            double lo=0,hi=1;
            const double lower[2]={eps,eps},upper[2]={length-eps,setback+eps};
            bool intersects=true;
            for(int k=0;k<2;++k) {
                const double d=z[k]-a[k];
                if(std::abs(d)<eps) {
                    if(a[k]<lower[k] || a[k]>upper[k]) intersects=false;
                } else {
                    double t0=(lower[k]-a[k])/d,t1=(upper[k]-a[k])/d;
                    if(t0>t1)std::swap(t0,t1);
                    lo=std::max(lo,t0);hi=std::min(hi,t1);
                    if(lo>hi)intersects=false;
                }
            }
            if(intersects)return false;
        }
    return true;
}

}

EditResult chamfer_edge(const Body& b, HalfedgeId edge, f64 setback_a,
                        f64 setback_b, const Volume& v) {
    const auto ring=circular_edge_ring(b,edge,v);
    if(!ring.empty()) {
        const bool cap_first=b.he_loop[edge]==b.he_loop[ring.front()];
        return blend_circular_edge(b,edge,cap_first?setback_a:setback_b,
                                   cap_first?setback_b:setback_a,1,false,v);
    }
    EditResult r;
    FaceId fa, fb;
    if (!faces_of_edge(b, edge, fa, fb)) {
        r.reason = "that halfedge has no twin: it is not an interior edge";
        return r;
    }
    if (!std::isfinite(setback_a) || !std::isfinite(setback_b) || setback_a <= 0.0 || setback_b <= 0.0) {
        r.reason = "a chamfer needs positive setbacks";
        return r;
    }
    if (!all_faces_planar(b)) {
        r.reason = "chamfering needs a planar-faced solid";
        return r;
    }

    const u32 twin = b.he_twin[edge];
    const VertexId v0 = b.he_vertex[edge];
    const VertexId v1 = b.he_vertex[twin];

    const f64 side = edge_is_convex(b, edge, v) ? 1.0 : -1.0;
    const Plane pa = face_plane(b, fa, v);
    const Plane pb = face_plane(b, fb, v);
    if (!pa.valid() || !pb.valid()) {
        r.reason = "one of the edge's faces has no usable plane";
        return r;
    }

    r.volume_before = volume(b, v);

    const P3 w0 = world(v, b.vertex_point[v0]);
    const P3 w1 = world(v, b.vertex_point[v1]);
    f64 ex = w1.x - w0.x, ey = w1.y - w0.y, ez = w1.z - w0.z;
    const f64 elen = norm3(ex, ey, ez);
    if (elen < 1e-12) {
        r.reason = "that edge has zero length";
        return r;
    }
    ex /= elen;
    ey /= elen;
    ez /= elen;

    auto in_face = [&](const Plane& self, const Plane& other, f64& dx, f64& dy,
                       f64& dz) {
        dx = self.ny * ez - self.nz * ey;
        dy = self.nz * ex - self.nx * ez;
        dz = self.nx * ey - self.ny * ex;
        const f64 n = norm3(dx, dy, dz);
        if (n < 1e-12) return false;
        dx /= n; dy /= n; dz /= n;
        if (side * (dx * other.nx + dy * other.ny + dz * other.nz) > 0.0) {
            dx = -dx; dy = -dy; dz = -dz;
        }
        return true;
    };

    f64 ax, ay, az, bx, by, bz;
    if (!in_face(pa, pb, ax, ay, az) || !in_face(pb, pa, bx, by, bz)) {
        r.reason = "could not find the setback directions";
        return r;
    }

    if (!setback_fits(b,fa,w0,{ex,ey,ez},{ax,ay,az},elen,setback_a,v) ||
        !setback_fits(b,fb,w0,{ex,ey,ez},{bx,by,bz},elen,setback_b,v)) {
        r.reason="The round or bevel reaches another boundary";
        return r;
    }

    const P3 a0{w0.x + ax * setback_a, w0.y + ay * setback_a, w0.z + az * setback_a};
    const P3 a1{w1.x + ax * setback_a, w1.y + ay * setback_a, w1.z + az * setback_a};
    const P3 b0{w0.x + bx * setback_b, w0.y + by * setback_b, w0.z + bz * setback_b};
    const P3 b1{w1.x + bx * setback_b, w1.y + by * setback_b, w1.z + bz * setback_b};

    std::vector<gcm::Lattice3> points = b.vertex_point;
    const VertexId ia0 = static_cast<VertexId>(points.size());
    points.push_back(gcm::to_lattice(v, a0.x, a0.y, a0.z));
    const VertexId ia1 = static_cast<VertexId>(points.size());
    points.push_back(gcm::to_lattice(v, a1.x, a1.y, a1.z));
    const VertexId ib0 = static_cast<VertexId>(points.size());
    points.push_back(gcm::to_lattice(v, b0.x, b0.y, b0.z));
    const VertexId ib1 = static_cast<VertexId>(points.size());
    points.push_back(gcm::to_lattice(v, b1.x, b1.y, b1.z));

    std::vector<std::vector<std::vector<VertexId>>> faces;
    for (FaceId f = 0; f < b.face_count(); ++f) {
        std::vector<std::vector<VertexId>> loops = face_loops(b, f);
        for (auto& loop : loops) {
            std::vector<VertexId> out;
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const VertexId id = loop[i];
                if (id != v0 && id != v1) {
                    out.push_back(id);
                    continue;
                }
                const bool zero = id == v0;
                if (f == fa) {
                    out.push_back(zero ? ia0 : ia1);
                } else if (f == fb) {
                    out.push_back(zero ? ib0 : ib1);
                } else {
                    const VertexId nxt = loop[(i + 1) % loop.size()];
                    const bool toward_a = nxt != v0 && nxt != v1 &&
                                          std::find(loop.begin(), loop.end(), nxt) !=
                                              loop.end();
                    (void)toward_a;
                    const VertexId prev = loop[(i + loop.size() - 1) % loop.size()];
                    const std::vector<VertexId> fa_loop = outer_loop(b, fa);
                    const bool from_a =
                        std::find(fa_loop.begin(), fa_loop.end(), prev) != fa_loop.end();
                    if (from_a) {
                        out.push_back(zero ? ia0 : ia1);
                        out.push_back(zero ? ib0 : ib1);
                    } else {
                        out.push_back(zero ? ib0 : ib1);
                        out.push_back(zero ? ia0 : ia1);
                    }
                }
            }
            loop = out;
        }
        faces.push_back(std::move(loops));
    }

    faces.push_back({{ia0, ib0, ib1, ia1}});

    Body built = rebuild(points, faces);
    Topology t = built.analyse();
    if (!t.closed || !t.manifold) {
        faces.back() = {{ia0, ia1, ib1, ib0}};
        built = rebuild(points, faces);
        t = built.analyse();
    }
    if (!t.closed || !t.manifold) {
        r.reason = "the chamfer left the solid open";
        return r;
    }

    r.volume_after = volume(built, v);
    if (r.volume_after <= 0.0 || side * (r.volume_after - r.volume_before) >= 0.0) {
        r.reason = "the chamfer does not fit this edge";
        return r;
    }

    r.body = std::move(built);
    r.ok = true;
    return r;
}

EditResult fillet_edge(const Body& b, HalfedgeId edge, f64 radius, int segments,
                       const Volume& v) {
    if(!circular_edge_ring(b,edge,v).empty()) return blend_circular_edge(b,edge,radius,radius,segments,true,v);
    EditResult r;
    if (!std::isfinite(radius) || radius <= 0.0) {
        r.reason = "a fillet needs a positive radius";
        return r;
    }
    if (segments < 1) segments = 1;

    FaceId fa, fb;
    if (!faces_of_edge(b, edge, fa, fb)) {
        r.reason = "that halfedge has no twin: it is not an interior edge";
        return r;
    }
    if (!all_faces_planar(b)) {
        r.reason = "filleting needs a planar-faced solid";
        return r;
    }

    const u32 twin = b.he_twin[edge];
    const VertexId v0 = b.he_vertex[edge];
    const VertexId v1 = b.he_vertex[twin];

    const f64 side = edge_is_convex(b, edge, v) ? 1.0 : -1.0;
    const Plane pa = face_plane(b, fa, v);
    const Plane pb = face_plane(b, fb, v);
    if (!pa.valid() || !pb.valid()) {
        r.reason = "one of the edge's faces has no usable plane";
        return r;
    }

    r.volume_before = volume(b, v);

    const P3 w0 = world(v, b.vertex_point[v0]);
    const P3 w1 = world(v, b.vertex_point[v1]);
    f64 ex = w1.x - w0.x, ey = w1.y - w0.y, ez = w1.z - w0.z;
    const f64 elen = norm3(ex, ey, ez);
    if (elen < 1e-12) {
        r.reason = "that edge has zero length";
        return r;
    }
    ex /= elen; ey /= elen; ez /= elen;

    auto in_face = [&](const Plane& self, const Plane& other, f64& dx, f64& dy,
                       f64& dz) {
        dx = self.ny * ez - self.nz * ey;
        dy = self.nz * ex - self.nx * ez;
        dz = self.nx * ey - self.ny * ex;
        const f64 n = norm3(dx, dy, dz);
        if (n < 1e-12) return false;
        dx /= n; dy /= n; dz /= n;
        if (side * (dx * other.nx + dy * other.ny + dz * other.nz) > 0.0) {
            dx = -dx; dy = -dy; dz = -dz;
        }
        return true;
    };

    f64 ax, ay, az, bx, by, bz;
    if (!in_face(pa, pb, ax, ay, az) || !in_face(pb, pa, bx, by, bz)) {
        r.reason = "could not find the tangent directions";
        return r;
    }

    const f64 dihedral =
        std::acos(std::clamp(-(pa.nx * pb.nx + pa.ny * pb.ny + pa.nz * pb.nz),
                             -1.0, 1.0));
    const f64 half = dihedral * 0.5;
    if (half < 1e-6 || half > 1.5707963 - 1e-6) {
        r.reason = "the faces are too near parallel to blend";
        return r;
    }
    const f64 setback = radius / std::tan(half);

    if (!setback_fits(b,fa,w0,{ex,ey,ez},{ax,ay,az},elen,setback,v) ||
        !setback_fits(b,fb,w0,{ex,ey,ez},{bx,by,bz},elen,setback,v)) {
        r.reason="The round or bevel reaches another boundary";
        return r;
    }

    const P3 ta0{w0.x + ax * setback, w0.y + ay * setback, w0.z + az * setback};
    const P3 ta1{w1.x + ax * setback, w1.y + ay * setback, w1.z + az * setback};
    const P3 tb0{w0.x + bx * setback, w0.y + by * setback, w0.z + bz * setback};

    const P3 c0{ta0.x - side * pa.nx * radius, ta0.y - side * pa.ny * radius,
                ta0.z - side * pa.nz * radius};

    f64 r0x = ta0.x - c0.x, r0y = ta0.y - c0.y, r0z = ta0.z - c0.z;
    f64 r1x = tb0.x - c0.x, r1y = tb0.y - c0.y, r1z = tb0.z - c0.z;
    const f64 rn0 = norm3(r0x, r0y, r0z), rn1 = norm3(r1x, r1y, r1z);
    if (rn0 < 1e-12 || rn1 < 1e-12) {
        r.reason = "the blend centre coincides with a tangent point";
        return r;
    }
    const f64 sweep = std::acos(std::clamp(
        (r0x * r1x + r0y * r1y + r0z * r1z) / (rn0 * rn1), -1.0, 1.0));

    f64 axx = ex, axy = ey, axz = ez;
    {
        const f64 cx = r0y * r1z - r0z * r1y;
        const f64 cy = r0z * r1x - r0x * r1z;
        const f64 cz = r0x * r1y - r0y * r1x;
        if (cx * axx + cy * axy + cz * axz < 0.0) {
            axx = -axx; axy = -axy; axz = -axz;
        }
    }

    std::vector<gcm::Lattice3> points = b.vertex_point;
    std::vector<VertexId> ring0, ring1;

    const P3 delta{w1.x - w0.x, w1.y - w0.y, w1.z - w0.z};
    for (int k = 0; k <= segments; ++k) {
        const f64 t = sweep * (static_cast<f64>(k) / segments);
        const f64 c = std::cos(t), s = std::sin(t);
        const f64 dot = axx * r0x + axy * r0y + axz * r0z;
        const f64 crx = axy * r0z - axz * r0y;
        const f64 cry = axz * r0x - axx * r0z;
        const f64 crz = axx * r0y - axy * r0x;
        const f64 px = c0.x + r0x * c + crx * s + axx * dot * (1.0 - c);
        const f64 py = c0.y + r0y * c + cry * s + axy * dot * (1.0 - c);
        const f64 pz = c0.z + r0z * c + crz * s + axz * dot * (1.0 - c);

        ring0.push_back(static_cast<VertexId>(points.size()));
        points.push_back(gcm::to_lattice(v, px, py, pz));
        ring1.push_back(static_cast<VertexId>(points.size()));
        points.push_back(gcm::to_lattice(v, px + delta.x, py + delta.y,
                                         pz + delta.z));
    }
    (void)ta1;

    const VertexId ia0 = ring0.front(), ia1 = ring1.front();
    const VertexId ib0 = ring0.back(), ib1 = ring1.back();

    const std::vector<VertexId> fa_loop = outer_loop(b, fa);
    std::vector<std::vector<std::vector<VertexId>>> faces;
    for (FaceId f = 0; f < b.face_count(); ++f) {
        std::vector<std::vector<VertexId>> loops = face_loops(b, f);
        for (auto& loop : loops) {
            std::vector<VertexId> out;
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const VertexId id = loop[i];
                if (id != v0 && id != v1) {
                    out.push_back(id);
                    continue;
                }
                const bool zero = id == v0;
                if (f == fa) {
                    out.push_back(zero ? ia0 : ia1);
                } else if (f == fb) {
                    out.push_back(zero ? ib0 : ib1);
                } else {
                    const VertexId prev = loop[(i + loop.size() - 1) % loop.size()];
                    const bool from_a =
                        std::find(fa_loop.begin(), fa_loop.end(), prev) != fa_loop.end();
                    const std::vector<VertexId>& ring = zero ? ring0 : ring1;
                    if (from_a)
                        for (VertexId id2 : ring) out.push_back(id2);
                    else
                        for (auto it = ring.rbegin(); it != ring.rend(); ++it)
                            out.push_back(*it);
                }
            }
            loop = out;
        }
        faces.push_back(std::move(loops));
    }

    for (int k = 0; k < segments; ++k)
        faces.push_back({{ring0[k], ring0[k + 1], ring1[k + 1], ring1[k]}});

    Body built = rebuild(points, faces);
    Topology t = built.analyse();
    if (!t.closed || !t.manifold) {
        const std::size_t first = faces.size() - static_cast<std::size_t>(segments);
        for (int k = 0; k < segments; ++k)
            faces[first + static_cast<std::size_t>(k)] = {
                {ring0[k], ring1[k], ring1[k + 1], ring0[k + 1]}};
        built = rebuild(points, faces);
        t = built.analyse();
    }
    if (!t.closed || !t.manifold) {
        r.reason = "the fillet left the solid open";
        return r;
    }

    r.volume_after = volume(built, v);
    if (r.volume_after <= 0.0 || side * (r.volume_after - r.volume_before) >= 0.0) {
        r.reason = "the fillet does not fit this edge";
        return r;
    }

    r.body = std::move(built);
    r.ok = true;
    return r;
}

void IdMap::seed(const Body& b) {
    ids_.assign(b.face_count(), 0);
    next_ = 1;
    for (auto& id : ids_) id = next_++;
}

u32 IdMap::face_id(FaceId f) const {
    return f < ids_.size() ? ids_[f] : 0;
}

void IdMap::set_face_id(FaceId f, u32 id) {
    if (f >= ids_.size()) ids_.resize(f + 1, 0);
    ids_[f] = id;
    if (id >= next_) next_ = id + 1;
}

FaceId IdMap::face_of(u32 id) const {
    for (std::size_t i = 0; i < ids_.size(); ++i)
        if (ids_[i] == id) return static_cast<FaceId>(i);
    return kInvalid;
}

u32 IdMap::allocate() { return next_++; }

void IdMap::follow(const Body& before, const Body& after,
                   const std::vector<u32>& survivors) {
    (void)before;
    std::vector<u32> next(after.face_count(), 0);

    for (u32 i = 0; i < after.face_count(); ++i) {
        const u32 from = i < survivors.size() ? survivors[i] : kInvalid;
        next[i] = (from != kInvalid && from < ids_.size()) ? ids_[from] : allocate();
    }
    ids_ = std::move(next);
}


bool offset_surface(const Surface& s, f64 distance, Surface& out) {
    out = s;
    switch (s.kind) {
        case SurfaceKind::Plane:
            out.ox = s.ox + s.ax * distance;
            out.oy = s.oy + s.ay * distance;
            out.oz = s.oz + s.az * distance;
            return true;

        case SurfaceKind::Cylinder:
        case SurfaceKind::Sphere:
            out.radius = s.radius + distance;
            return out.radius > 1e-12;

        case SurfaceKind::Torus:
            out.minor_radius = s.minor_radius + distance;
            return out.minor_radius > 1e-12;

        case SurfaceKind::Cone: {
            // Same half-angle, apex moved along the axis.
            const f64 s_ang = std::sin(s.half_angle);
            if (s_ang < 1e-12) return false;
            const f64 shift = distance / s_ang;
            out.ox = s.ox - s.ax * shift;
            out.oy = s.oy - s.ay * shift;
            out.oz = s.oz - s.az * shift;
            return true;
        }
    }
    return false;
}

EditResult fillet_edge_exact(const Body& b, HalfedgeId edge, f64 radius,
                             int segments, const Volume& v) {
    if(!circular_edge_ring(b,edge,v).empty()) return blend_circular_edge(b,edge,radius,radius,segments,true,v);
    segments = std::max(segments, 1);
    EditResult r = fillet_edge(b, edge, radius, segments, v);
    if (!r.ok) return r;

    FaceId fa, fb;
    if (!faces_of_edge(b, edge, fa, fb)) return r;
    const u32 twin = b.he_twin[edge];
    const P3 w0 = world(v, b.vertex_point[b.he_vertex[edge]]);
    const P3 w1 = world(v, b.vertex_point[b.he_vertex[twin]]);

    f64 ex = w1.x - w0.x, ey = w1.y - w0.y, ez = w1.z - w0.z;
    const f64 elen = norm3(ex, ey, ez);
    if (elen < 1e-12) return r;
    ex /= elen; ey /= elen; ez /= elen;

    const f64 side = edge_is_convex(b, edge, v) ? 1.0 : -1.0;
    const Plane pa = face_plane(b, fa, v);
    const Plane pb = face_plane(b, fb, v);
    if (!pa.valid() || !pb.valid()) return r;

    const f64 dihedral = std::acos(std::clamp(
        -(pa.nx * pb.nx + pa.ny * pb.ny + pa.nz * pb.nz), -1.0, 1.0));
    const f64 half = dihedral * 0.5;
    if (half < 1e-6) return r;
    const f64 setback = radius / std::tan(half);

    auto in_face = [&](const Plane& self, const Plane& other, f64& dx, f64& dy,
                       f64& dz) {
        dx = self.ny * ez - self.nz * ey;
        dy = self.nz * ex - self.nx * ez;
        dz = self.nx * ey - self.ny * ex;
        const f64 n = norm3(dx, dy, dz);
        if (n < 1e-12) return false;
        dx /= n; dy /= n; dz /= n;
        if (side * (dx * other.nx + dy * other.ny + dz * other.nz) > 0.0) {
            dx = -dx; dy = -dy; dz = -dz;
        }
        return true;
    };

    f64 ax, ay, az, bx, by, bz;
    if (!in_face(pa, pb, ax, ay, az) || !in_face(pb, pa, bx, by, bz)) return r;

    const P3 tangent{w0.x + ax * setback, w0.y + ay * setback, w0.z + az * setback};
    const P3 centre{tangent.x - side * pa.nx * radius, tangent.y - side * pa.ny * radius,
                    tangent.z - side * pa.nz * radius};

    const Surface blend =
        Surface::cylinder(centre.x, centre.y, centre.z, ex, ey, ez, radius);

    r.body.face_surface.clear();
    r.body.face_surface.reserve(r.body.face_count());
    const u32 first_strip = r.body.face_count() - static_cast<u32>(segments);
    for (FaceId f = 0; f < r.body.face_count(); ++f) {
        if (f >= first_strip) {
            r.body.face_surface.push_back(blend);
            continue;
        }
        const Plane p = face_plane(r.body, f, v);
        r.body.face_surface.push_back(
            Surface::plane(p.nx * p.d, p.ny * p.d, p.nz * p.d, p.nx, p.ny, p.nz));
    }

    return r;
}

ShellResult shell_body(const Body& b, f64 thickness,
                       const std::vector<FaceId>& open_faces, const Volume& v) {
    ShellResult r;
    if (b.face_count() == 0) {
        r.reason = "nothing to shell";
        return r;
    }
    if (std::fabs(thickness) < 1e-12) {
        r.reason = "a shell needs a non-zero thickness";
        return r;
    }

    std::vector<Surface> outer(b.face_count());
    for (FaceId f = 0; f < b.face_count(); ++f) {
        if (const Surface* s = b.surface_of(f)) {
            outer[f] = *s;
            continue;
        }
        const Plane p = face_plane(b, f, v);
        if (!p.valid()) {
            r.reason = "a face has no usable surface";
            return r;
        }
        outer[f] =
            Surface::plane(p.nx * p.d, p.ny * p.d, p.nz * p.d, p.nx, p.ny, p.nz);
    }

    std::vector<Surface> inner(b.face_count());
    for (FaceId f = 0; f < b.face_count(); ++f) {
        const bool open = std::find(open_faces.begin(), open_faces.end(), f) !=
                          open_faces.end();
        if (open) {
            inner[f] = outer[f];
            ++r.removed_faces;
            continue;
        }
        if (!offset_surface(outer[f], -std::fabs(thickness), inner[f])) {
            r.reason = "the wall is thicker than a feature it would offset";
            return r;
        }
    }

    std::vector<gcm::Lattice3> points = b.vertex_point;
    points.reserve(points.size() * 2);
    std::vector<VertexId> inner_of(b.vertex_count(), kInvalid);

    for (VertexId vid = 0; vid < b.vertex_count(); ++vid) {
        const std::vector<FaceId> around = faces_at_vertex(b, vid);
        if (around.size() < 3) {
            r.reason = "a vertex is not the corner of three faces";
            return r;
        }

        const P3 w = world(v, b.vertex_point[vid]);
        Plane pl[3];
        int found = 0;
        for (FaceId f : around) {
            if (found == 3) break;
            if (inner[f].kind != SurfaceKind::Plane) continue;
            pl[found++] = {inner[f].ax, inner[f].ay, inner[f].az,
                           inner[f].ax * inner[f].ox + inner[f].ay * inner[f].oy +
                               inner[f].az * inner[f].oz};
        }

        f64 x = w.x, y = w.y, z = w.z;
        if (found == 3) {
            if (!intersect_planes(pl[0], pl[1], pl[2], x, y, z)) {
                r.reason = "the offset faces do not pin a corner";
                return r;
            }
        } else {
            for (int it = 0; it < 40; ++it) {
                f64 dx = 0, dy = 0, dz = 0;
                f64 worst = 0.0;
                for (FaceId f : around) {
                    const f64 d = inner[f].implicit(x, y, z);
                    worst = std::max(worst, std::fabs(d));
                    f64 nx, ny, nz;
                    if (!inner[f].normal_at(x, y, z, nx, ny, nz)) continue;
                    dx -= nx * d;
                    dy -= ny * d;
                    dz -= nz * d;
                }
                if (worst < 1e-12) break;
                const f64 k = 1.0 / static_cast<f64>(around.size());
                x += dx * k;
                y += dy * k;
                z += dz * k;
            }
        }

        inner_of[vid] = static_cast<VertexId>(points.size());
        points.push_back(gcm::to_lattice(v, x, y, z));
    }

    std::vector<std::vector<std::vector<VertexId>>> faces;
    for (FaceId f = 0; f < b.face_count(); ++f) {
        const bool open = std::find(open_faces.begin(), open_faces.end(), f) !=
                          open_faces.end();
        if (open) continue;
        faces.push_back(face_loops(b, f));
    }

    for (FaceId f = 0; f < b.face_count(); ++f) {
        const bool open = std::find(open_faces.begin(), open_faces.end(), f) !=
                          open_faces.end();
        if (open) continue;
        std::vector<std::vector<VertexId>> loops = face_loops(b, f);
        for (auto& loop : loops) {
            std::vector<VertexId> flipped;
            for (auto it = loop.rbegin(); it != loop.rend(); ++it)
                flipped.push_back(inner_of[*it]);
            loop = flipped;
        }
        faces.push_back(std::move(loops));
    }

    for (FaceId f : open_faces) {
        if (f >= b.face_count()) continue;
        for (const auto& loop : face_loops(b, f))
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const VertexId a0 = loop[i];
                const VertexId a1 = loop[(i + 1) % loop.size()];
                faces.push_back({{a0, a1, inner_of[a1], inner_of[a0]}});
            }
    }

    r.body = rebuild(points, faces);
    const Topology t = r.body.analyse();
    if (!t.closed || !t.manifold) {
        r.reason = "the shell did not close";
        return r;
    }

    const f64 before = volume(b, v);
    const f64 after = volume(r.body, v);
    if (after <= 0.0 || after >= before) {
        r.body = Body{};
        r.reason = "the wall is thicker than the part it would hollow";
        return r;
    }

    r.thickness = std::fabs(thickness);
    r.ok = true;
    return r;
}

}
