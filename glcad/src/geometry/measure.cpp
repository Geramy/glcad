#include "geometry/measure.hpp"

#include <algorithm>
#include <cmath>

#include "geometry/boolean.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/triangulate.hpp"

namespace gtopo {

namespace {

struct P3 { f64 x, y, z; };

P3 world(const Volume& v, gcm::Lattice3 p) {
    f64 x, y, z;
    gcm::to_world(v, p, x, y, z);
    return {x, y, z};
}

f64 norm3(f64 x, f64 y, f64 z) { return std::sqrt(x * x + y * y + z * z); }

void eigenvalues_sym3(const f64 m[9], f64 out[3]) {
    f64 a[3][3] = {{m[0], m[1], m[2]}, {m[3], m[4], m[5]}, {m[6], m[7], m[8]}};

    for (int sweep = 0; sweep < 32; ++sweep) {
        f64 off = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j) off += a[i][j] * a[i][j];
        if (off < 1e-24) break;

        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::fabs(a[p][q]) < 1e-30) continue;
                const f64 theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const f64 t = (theta >= 0.0 ? 1.0 : -1.0) /
                              (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const f64 c = 1.0 / std::sqrt(t * t + 1.0);
                const f64 s = t * c;

                for (int k = 0; k < 3; ++k) {
                    const f64 akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const f64 apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
            }
    }

    out[0] = a[0][0];
    out[1] = a[1][1];
    out[2] = a[2][2];
    std::sort(out, out + 3);
}

}

MassProperties mass_properties(const Body& b, const Volume& v, f64 density) {
    MassProperties out;
    out.density = density;

    if (b.face_count() == 0) {
        out.reason = "an empty body has no mass";
        return out;
    }
    const Topology t = b.analyse();
    if (!t.closed || !t.manifold) {
        out.reason = "mass properties need a closed, manifold solid";
        return out;
    }

    const TriMesh mesh = triangulate(b, v);
    if (mesh.indices.size() < 3) {
        out.reason = "the solid could not be triangulated";
        return out;
    }

    f64 volume = 0.0;
    f64 cx = 0.0, cy = 0.0, cz = 0.0;
    f64 area = 0.0;
    f64 ixx = 0.0, iyy = 0.0, izz = 0.0, ixy = 0.0, ixz = 0.0, iyz = 0.0;

    bool first = true;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const P3 a = world(v, b.vertex_point[mesh.indices[i]]);
        const P3 c = world(v, b.vertex_point[mesh.indices[i + 1]]);
        const P3 d = world(v, b.vertex_point[mesh.indices[i + 2]]);

        for (const P3& p : {a, c, d}) {
            const f64 xyz[3] = {p.x, p.y, p.z};
            for (int k = 0; k < 3; ++k) {
                if (first) {
                    out.lo[k] = out.hi[k] = xyz[k];
                } else {
                    out.lo[k] = std::min(out.lo[k], xyz[k]);
                    out.hi[k] = std::max(out.hi[k], xyz[k]);
                }
            }
            first = false;
        }

        const f64 det = a.x * (c.y * d.z - c.z * d.y) -
                        a.y * (c.x * d.z - c.z * d.x) +
                        a.z * (c.x * d.y - c.y * d.x);
        const f64 vol6 = det;
        volume += vol6;

        cx += vol6 * (a.x + c.x + d.x);
        cy += vol6 * (a.y + c.y + d.y);
        cz += vol6 * (a.z + c.z + d.z);

        const f64 ux = c.x - a.x, uy = c.y - a.y, uz = c.z - a.z;
        const f64 wx = d.x - a.x, wy = d.y - a.y, wz = d.z - a.z;
        area += 0.5 * norm3(uy * wz - uz * wy, uz * wx - ux * wz,
                            ux * wy - uy * wx);

        auto mom = [&](f64 p, f64 q, f64 r) {
            return p * p + q * q + r * r + p * q + q * r + r * p;
        };
        auto prod = [&](f64 p1, f64 q1, f64 r1, f64 p2, f64 q2, f64 r2) {
            return 2.0 * (p1 * p2 + q1 * q2 + r1 * r2) + p1 * q2 + q1 * p2 +
                   q1 * r2 + r1 * q2 + r1 * p2 + p1 * r2;
        };
        ixx += vol6 * mom(a.x, c.x, d.x);
        iyy += vol6 * mom(a.y, c.y, d.y);
        izz += vol6 * mom(a.z, c.z, d.z);
        ixy += vol6 * prod(a.x, c.x, d.x, a.y, c.y, d.y);
        ixz += vol6 * prod(a.x, c.x, d.x, a.z, c.z, d.z);
        iyz += vol6 * prod(a.y, c.y, d.y, a.z, c.z, d.z);
    }

    out.volume = volume / 6.0;
    if (out.volume <= 0.0) {
        out.reason = "the solid has no positive volume: is it inside out?";
        return out;
    }

    out.area = area;
    out.mass = out.volume * density;

    const f64 k = 1.0 / (24.0 * out.volume);
    out.centroid[0] = cx * k;
    out.centroid[1] = cy * k;
    out.centroid[2] = cz * k;

    ixx /= 60.0;
    iyy /= 60.0;
    izz /= 60.0;
    ixy /= 120.0;
    ixz /= 120.0;
    iyz /= 120.0;

    const f64 m = out.mass;
    const f64 gx = out.centroid[0], gy = out.centroid[1], gz = out.centroid[2];
    const f64 dxx = density * ixx - m * gx * gx;
    const f64 dyy = density * iyy - m * gy * gy;
    const f64 dzz = density * izz - m * gz * gz;
    const f64 dxy = density * ixy - m * gx * gy;
    const f64 dxz = density * ixz - m * gx * gz;
    const f64 dyz = density * iyz - m * gy * gz;

    out.inertia[0] = dyy + dzz;
    out.inertia[4] = dxx + dzz;
    out.inertia[8] = dxx + dyy;
    out.inertia[1] = out.inertia[3] = -dxy;
    out.inertia[2] = out.inertia[6] = -dxz;
    out.inertia[5] = out.inertia[7] = -dyz;

    eigenvalues_sym3(out.inertia, out.principal);
    for (int i = 0; i < 3; ++i)
        out.radius_of_gyration[i] =
            m > 0.0 ? std::sqrt(std::max(0.0, out.principal[i] / m)) : 0.0;

    out.ok = true;
    return out;
}

SectionResult section(const Body& b, const Plane& p, const Volume& v) {
    SectionResult out;
    if (!p.valid()) {
        out.reason = "the cutting plane is degenerate";
        return out;
    }
    if (b.face_count() == 0) {
        out.reason = "nothing to cut";
        return out;
    }

    struct Seg { P3 a, b; };
    std::vector<Seg> segs;

    for (FaceId f = 0; f < b.face_count(); ++f) {
        std::vector<P3> crossings;
        for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li) {
            const u32 begin = b.loop_begin(li), end = b.loop_end(li);
            const u32 n = end - begin;
            for (u32 i = 0; i < n; ++i) {
                const P3 a = world(v, b.vertex_point[b.he_vertex[begin + i]]);
                const P3 c =
                    world(v, b.vertex_point[b.he_vertex[begin + (i + 1) % n]]);
                const f64 da = p.distance(a.x, a.y, a.z);
                const f64 dc = p.distance(c.x, c.y, c.z);

                // Avoid double-counting a vertex exactly on the plane.
                if (std::fabs(da) < 1e-12) {
                    crossings.push_back(a);
                    continue;
                }
                if ((da > 0.0) == (dc > 0.0)) continue;
                const f64 t = da / (da - dc);
                crossings.push_back({a.x + (c.x - a.x) * t, a.y + (c.y - a.y) * t,
                                     a.z + (c.z - a.z) * t});
            }
        }
        if (crossings.size() >= 2) segs.push_back({crossings.front(), crossings.back()});
    }

    for (const Seg& s : segs) {
        out.segments.insert(out.segments.end(),
                            {s.a.x, s.a.y, s.a.z, s.b.x, s.b.y, s.b.z});
    }

    if (segs.empty()) {
        out.ok = true;
        out.reason = "the plane misses the solid";
        return out;
    }

    std::vector<bool> used(segs.size(), false);
    auto close_enough = [](const P3& a, const P3& b) {
        return norm3(a.x - b.x, a.y - b.y, a.z - b.z) < 1e-9;
    };

    for (std::size_t start = 0; start < segs.size(); ++start) {
        if (used[start]) continue;
        used[start] = true;

        std::vector<P3> loop{segs[start].a, segs[start].b};
        bool grew = true;
        while (grew) {
            grew = false;
            for (std::size_t i = 0; i < segs.size(); ++i) {
                if (used[i]) continue;
                if (close_enough(loop.back(), segs[i].a)) {
                    loop.push_back(segs[i].b);
                } else if (close_enough(loop.back(), segs[i].b)) {
                    loop.push_back(segs[i].a);
                } else {
                    continue;
                }
                used[i] = true;
                grew = true;
                if (close_enough(loop.front(), loop.back())) break;
            }
        }

        if (loop.size() < 3) continue;

        SectionLoop sl;
        for (const P3& q : loop) sl.points.insert(sl.points.end(), {q.x, q.y, q.z});

        f64 ax = 0.0, ay = 0.0, az = 0.0;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const P3& q = loop[i];
            const P3& r = loop[(i + 1) % loop.size()];
            ax += q.y * r.z - q.z * r.y;
            ay += q.z * r.x - q.x * r.z;
            az += q.x * r.y - q.y * r.x;
        }
        sl.area = std::fabs(0.5 * (ax * p.nx + ay * p.ny + az * p.nz));
        out.cut_area += sl.area;
        out.loops.push_back(std::move(sl));
    }

    if (!out.loops.empty()) {
        std::size_t biggest = 0;
        for (std::size_t i = 1; i < out.loops.size(); ++i)
            if (out.loops[i].area > out.loops[biggest].area) biggest = i;
        for (std::size_t i = 0; i < out.loops.size(); ++i)
            out.loops[i].outer = i == biggest;
        out.cut_area = out.loops[biggest].area;
        for (std::size_t i = 0; i < out.loops.size(); ++i)
            if (i != biggest) out.cut_area -= out.loops[i].area;
    }

    out.ok = true;
    return out;
}

Measurement measure_edge_length(const Body& b, HalfedgeId e, const Volume& v) {
    Measurement m;
    m.units = "m";
    if (e >= b.halfedge_count()) {
        m.reason = "no such edge";
        return m;
    }
    const P3 a = world(v, b.vertex_point[b.he_vertex[e]]);
    const P3 c = world(v, b.vertex_point[b.he_vertex[b.he_next[e]]]);
    m.value = norm3(c.x - a.x, c.y - a.y, c.z - a.z);
    m.ok = true;
    return m;
}

Measurement measure_face_area(const Body& b, FaceId f, const Volume& v) {
    Measurement m;
    m.units = "m^2";
    if (f >= b.face_count()) {
        m.reason = "no such face";
        return m;
    }
    const Plane p = face_plane(b, f, v);
    if (!p.valid()) {
        m.reason = "that face has no usable plane";
        return m;
    }

    f64 total = 0.0;
    bool outer = true;
    for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li) {
        f64 ax = 0.0, ay = 0.0, az = 0.0;
        const u32 begin = b.loop_begin(li), end = b.loop_end(li);
        const u32 n = end - begin;
        for (u32 i = 0; i < n; ++i) {
            const P3 q = world(v, b.vertex_point[b.he_vertex[begin + i]]);
            const P3 r =
                world(v, b.vertex_point[b.he_vertex[begin + (i + 1) % n]]);
            ax += q.y * r.z - q.z * r.y;
            ay += q.z * r.x - q.x * r.z;
            az += q.x * r.y - q.y * r.x;
        }
        const f64 a = std::fabs(0.5 * (ax * p.nx + ay * p.ny + az * p.nz));
        total += outer ? a : -a;
        outer = false;
    }

    m.value = std::max(0.0, total);
    m.ok = true;
    return m;
}

Measurement measure_face_perimeter(const Body& b, FaceId f, const Volume& v) {
    Measurement m;
    m.units = "m";
    if (f >= b.face_count()) {
        m.reason = "no such face";
        return m;
    }
    for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li) {
        const u32 begin = b.loop_begin(li), end = b.loop_end(li);
        const u32 n = end - begin;
        for (u32 i = 0; i < n; ++i) {
            const P3 q = world(v, b.vertex_point[b.he_vertex[begin + i]]);
            const P3 r =
                world(v, b.vertex_point[b.he_vertex[begin + (i + 1) % n]]);
            m.value += norm3(r.x - q.x, r.y - q.y, r.z - q.z);
        }
    }
    m.ok = true;
    return m;
}

Measurement measure_angle_between_faces(const Body& b, FaceId a, FaceId c,
                                        const Volume& v) {
    Measurement m;
    m.units = "rad";
    const Plane pa = face_plane(b, a, v), pc = face_plane(b, c, v);
    if (!pa.valid() || !pc.valid()) {
        m.reason = "one of those faces has no usable plane";
        return m;
    }
    const f64 d = std::clamp(-(pa.nx * pc.nx + pa.ny * pc.ny + pa.nz * pc.nz),
                             -1.0, 1.0);
    m.value = std::acos(d);
    m.ok = true;
    return m;
}

Measurement measure_distance(const Body& b, VertexId a, VertexId c,
                             const Volume& v) {
    Measurement m;
    m.units = "m";
    if (a >= b.vertex_count() || c >= b.vertex_count()) {
        m.reason = "no such vertex";
        return m;
    }
    const P3 p = world(v, b.vertex_point[a]);
    const P3 q = world(v, b.vertex_point[c]);
    m.value = norm3(q.x - p.x, q.y - p.y, q.z - p.z);
    m.ok = true;
    return m;
}

namespace {

// Distance to the triangle, not its plane.
f64 point_triangle_distance(const P3& p, const P3& a, const P3& b, const P3& c) {
    const f64 abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const f64 acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    const f64 apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;

    const f64 d1 = abx * apx + aby * apy + abz * apz;
    const f64 d2 = acx * apx + acy * apy + acz * apz;
    if (d1 <= 0.0 && d2 <= 0.0) return norm3(apx, apy, apz);

    const f64 bpx = p.x - b.x, bpy = p.y - b.y, bpz = p.z - b.z;
    const f64 d3 = abx * bpx + aby * bpy + abz * bpz;
    const f64 d4 = acx * bpx + acy * bpy + acz * bpz;
    if (d3 >= 0.0 && d4 <= d3) return norm3(bpx, bpy, bpz);

    const f64 vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const f64 t = d1 / (d1 - d3);
        return norm3(apx - abx * t, apy - aby * t, apz - abz * t);
    }

    const f64 cpx = p.x - c.x, cpy = p.y - c.y, cpz = p.z - c.z;
    const f64 d5 = abx * cpx + aby * cpy + abz * cpz;
    const f64 d6 = acx * cpx + acy * cpy + acz * cpz;
    if (d6 >= 0.0 && d5 <= d6) return norm3(cpx, cpy, cpz);

    const f64 vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const f64 t = d2 / (d2 - d6);
        return norm3(apx - acx * t, apy - acy * t, apz - acz * t);
    }

    const f64 va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        const f64 t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return norm3(p.x - (b.x + (c.x - b.x) * t), p.y - (b.y + (c.y - b.y) * t),
                     p.z - (b.z + (c.z - b.z) * t));
    }

    const f64 denom = 1.0 / (va + vb + vc);
    const f64 u = vb * denom, w = vc * denom;
    return norm3(apx - (abx * u + acx * w), apy - (aby * u + acy * w),
                 apz - (abz * u + acz * w));
}

f64 closest_vertices_to_faces(const Body& from, const Body& to, const Volume& v) {
    const TriMesh mesh = triangulate(to, v);
    f64 best = 1e300;
    for (u32 i = 0; i < from.vertex_count(); ++i) {
        const P3 p = world(v, from.vertex_point[i]);
        for (std::size_t k = 0; k + 2 < mesh.indices.size(); k += 3)
            best = std::min(best,
                            point_triangle_distance(
                                p, world(v, to.vertex_point[mesh.indices[k]]),
                                world(v, to.vertex_point[mesh.indices[k + 1]]),
                                world(v, to.vertex_point[mesh.indices[k + 2]])));
    }
    return best;
}

}

Measurement measure_clearance(const Body& a, const Body& b, const Volume& v) {
    Measurement m;
    m.units = "m";
    if (a.vertex_count() == 0 || b.vertex_count() == 0) {
        m.reason = "one of the bodies is empty";
        return m;
    }

    // Not a proven minimum: two edges can pass closer than any vertex.
    const f64 best = std::min(closest_vertices_to_faces(a, b, v),
                              closest_vertices_to_faces(b, a, v));

    m.value = best;
    m.ok = true;
    m.reason = "closest approach between vertices and faces";
    return m;
}

}
