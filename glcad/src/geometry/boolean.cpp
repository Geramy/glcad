#include "geometry/boolean.hpp"

#include <array>
#include <limits>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "math/predicate.hpp"
#include "math/parallel.hpp"
#include "geometry/triangulate.hpp"

namespace gtopo {

using gcm::Lattice3;
using gcm::Sign;
using P = gcm::predicate::Host;

const char* BoolResult::message() const noexcept {
    switch (error) {
        case BoolError::None:          return "ok";
        case BoolError::NonPlanarFace: return
            "Boolean needs planar faces; curved surfaces require SSI (docs/09 §6.3)";
        case BoolError::OpenBody:      return "Boolean operands must be closed solids";
        case BoolError::EmptyResult:   return "The operation removed all geometry";
        case BoolError::Degenerate:    return "Degenerate input";
    }
    return "";
}

namespace {

struct EdgePlane { Lattice3 a, b, c; };

struct Poly {
    std::vector<Lattice3> v;
    Lattice3 p0, p1, p2;
    bool     from_b{false};
    std::vector<EdgePlane> incoming;

    bool valid() const noexcept { return v.size() >= 3; }
};

struct LatticeAABB {
    gcm::i64 min_x, min_y, min_z;
    gcm::i64 max_x, max_y, max_z;

    static LatticeAABB from_poly(const Poly& p) noexcept {
        LatticeAABB b{p.v[0].x, p.v[0].y, p.v[0].z, p.v[0].x, p.v[0].y, p.v[0].z};
        for (std::size_t i = 1; i < p.v.size(); ++i) {
            b.min_x = std::min(b.min_x, p.v[i].x);
            b.max_x = std::max(b.max_x, p.v[i].x);
            b.min_y = std::min(b.min_y, p.v[i].y);
            b.max_y = std::max(b.max_y, p.v[i].y);
            b.min_z = std::min(b.min_z, p.v[i].z);
            b.max_z = std::max(b.max_z, p.v[i].z);
        }
        return b;
    }

    bool intersects(const LatticeAABB& o) const noexcept {
        return min_x <= o.max_x && max_x >= o.min_x &&
               min_y <= o.max_y && max_y >= o.min_y &&
               min_z <= o.max_z && max_z >= o.min_z;
    }
};

Sign side_of(const Poly& plane, const Lattice3& p) {
    return P::orient3d(plane.p0, plane.p2, plane.p1, p);
}

bool collinear3(const Lattice3& a, const Lattice3& b, const Lattice3& c) {
    const gcm::i64 ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const gcm::i64 wx = c.x - a.x, wy = c.y - a.y, wz = c.z - a.z;

    auto comp = [](gcm::i64 p, gcm::i64 q, gcm::i64 r, gcm::i64 s) {
        return gcm::sub(gcm::mul_i64_i64<128>(p, q), gcm::mul_i64_i64<128>(r, s));
    };
    return comp(uy, wz, uz, wy).is_zero() &&
           comp(uz, wx, ux, wz).is_zero() &&
           comp(ux, wy, uy, wx).is_zero();
}

bool pick_plane(const std::vector<Lattice3>& v, Lattice3& a, Lattice3& b, Lattice3& c) {
    if (v.size() < 3) return false;
    a = v[0];
    std::size_t i = 1;
    for (; i < v.size(); ++i) {
        if (v[i].x != a.x || v[i].y != a.y || v[i].z != a.z) {
            b = v[i];
            break;
        }
    }
    if (i >= v.size()) return false;
    for (std::size_t j = i + 1; j < v.size(); ++j) {
        if (!collinear3(a, b, v[j])) {
            c = v[j];
            return true;
        }
    }
    return false;
}

bool on_plane_exact(const Lattice3& q, const Lattice3& a, const Lattice3& b, const Lattice3& c) {
    const i64 u[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    const i64 w[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
    const gcm::SInt<128> nx = gcm::sub(gcm::mul_i64_i64<128>(u[1], w[2]), gcm::mul_i64_i64<128>(u[2], w[1]));
    const gcm::SInt<128> ny = gcm::sub(gcm::mul_i64_i64<128>(u[2], w[0]), gcm::mul_i64_i64<128>(u[0], w[2]));
    const gcm::SInt<128> nz = gcm::sub(gcm::mul_i64_i64<128>(u[0], w[1]), gcm::mul_i64_i64<128>(u[1], w[0]));
    gcm::SInt<128> d = gcm::mul_i64(nx, q.x - a.x);
    d = gcm::add(d, gcm::mul_i64(ny, q.y - a.y));
    d = gcm::add(d, gcm::mul_i64(nz, q.z - a.z));
    return d.is_zero();
}

bool on_line_exact(const Lattice3& q, const Lattice3& a, const Lattice3& b) {
    const i64 u[3] = {q.x - a.x, q.y - a.y, q.z - a.z};
    const i64 w[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    return gcm::sub(gcm::mul_i64_i64<128>(u[1], w[2]), gcm::mul_i64_i64<128>(u[2], w[1])).is_zero() &&
           gcm::sub(gcm::mul_i64_i64<128>(u[2], w[0]), gcm::mul_i64_i64<128>(u[0], w[2])).is_zero() &&
           gcm::sub(gcm::mul_i64_i64<128>(u[0], w[1]), gcm::mul_i64_i64<128>(u[1], w[0])).is_zero();
}

Lattice3 exact_crossing(const Lattice3& guess, const Lattice3& p0,
                        const Lattice3& p1, const Lattice3& p2,
                        const Lattice3& vi, const Lattice3& vj) {
    for (i64 dz = -1; dz <= 1; ++dz)
        for (i64 dy = -1; dy <= 1; ++dy)
            for (i64 dx = -1; dx <= 1; ++dx) {
                Lattice3 q{guess.x + dx, guess.y + dy, guess.z + dz};
                if (on_plane_exact(q, p0, p1, p2) && on_line_exact(q, vi, vj))
                    return q;
            }
    return guess;
}

// Use original planes, not rounded crossings, so near-parallel cuts don't drift.
using Wide = gcm::SInt<512>;
Wide product(const Wide& a, const Wide& b) {
    Wide result;
    for (int i=0;i<Wide::N;++i) {
        if (b.l[i] == 0) continue;
        const Wide part=gcm::umul_small(a,b.l[i]);
        Wide shifted;
        for (int j=i;j<Wide::N;++j) shifted.l[j]=part.l[j-i];
        result=gcm::add(result,shifted);
    }
    return result;
}
using Wide3 = std::array<Wide,3>;
Wide3 cross_exact(const Wide3& a,const Wide3& b) {
    return {gcm::sub(product(a[1],b[2]),product(a[2],b[1])),
            gcm::sub(product(a[2],b[0]),product(a[0],b[2])),
            gcm::sub(product(a[0],b[1]),product(a[1],b[0]))};
}
Wide dot_exact(const Wide3& a,const Wide3& b) {
    return gcm::add(gcm::add(product(a[0],b[0]),product(a[1],b[1])),product(a[2],b[2]));
}
Wide3 wide_point(const Lattice3& p) { return {Wide::from_i64(p.x),Wide::from_i64(p.y),Wide::from_i64(p.z)}; }
Wide3 plane_normal(const EdgePlane& p) {
    const i64 u[3]={p.b.x-p.a.x,p.b.y-p.a.y,p.b.z-p.a.z};
    const i64 v[3]={p.c.x-p.a.x,p.c.y-p.a.y,p.c.z-p.a.z};
    return {gcm::sub(gcm::mul_i64_i64<512>(u[1],v[2]),gcm::mul_i64_i64<512>(u[2],v[1])),
            gcm::sub(gcm::mul_i64_i64<512>(u[2],v[0]),gcm::mul_i64_i64<512>(u[0],v[2])),
            gcm::sub(gcm::mul_i64_i64<512>(u[0],v[1]),gcm::mul_i64_i64<512>(u[1],v[0]))};
}
i64 rounded_ratio(Wide numerator,Wide denominator) {
    const bool negative=numerator.is_negative()!=denominator.is_negative();
    if (numerator.is_negative()) numerator=gcm::negate(numerator);
    if (denominator.is_negative()) denominator=gcm::negate(denominator);
    const auto approximate=[](const Wide& value) {
        double result=0;
        for (int i=Wide::N-1;i>=0;--i) result=std::ldexp(result,64)+double(value.l[i]);
        return result;
    };
    // Float gives a starting quotient; exact remainders pick the lattice cell.
    i64 q=static_cast<i64>(approximate(numerator)/approximate(denominator));
    Wide remainder=gcm::sub(numerator,gcm::mul_i64(denominator,q));
    while (remainder.is_negative()) { --q; remainder=gcm::add(remainder,denominator); }
    while (gcm::sub(remainder,denominator).sign()!=Sign::Negative) { ++q; remainder=gcm::sub(remainder,denominator); }
    if (gcm::sub(gcm::add(remainder,remainder),denominator).sign()!=Sign::Negative) ++q;
    return negative ? -q : q;
}
bool plane_crossing(const EdgePlane& a,const EdgePlane& b,const EdgePlane& c,Lattice3& out) {
    const Wide3 na=plane_normal(a),nb=plane_normal(b),nc=plane_normal(c);
    const Wide3 bc=cross_exact(nb,nc),ca=cross_exact(nc,na),ab=cross_exact(na,nb);
    const Wide den=dot_exact(na,bc);
    if (den.is_zero()) return false;
    const Wide da=dot_exact(na,wide_point(a.a)),db=dot_exact(nb,wide_point(b.a)),dc=dot_exact(nc,wide_point(c.a));
    i64 result[3];
    for (int k=0;k<3;++k)
        result[k]=rounded_ratio(gcm::add(gcm::add(product(da,bc[k]),product(db,ca[k])),product(dc,ab[k])),den);
    out={result[0],result[1],result[2]};
    return true;
}

void split(const Poly& poly, const Poly& plane, std::vector<Poly>& front,
           std::vector<Poly>& back, std::vector<Poly>& coplanar_front,
           std::vector<Poly>& coplanar_back, const gcm::WorkingVolume& vol) {
    const std::size_t n = poly.v.size();
    std::vector<Sign> s(n);
    int n_front = 0, n_back = 0;

    for (std::size_t i = 0; i < n; ++i) {
        s[i] = side_of(plane, poly.v[i]);
        if (s[i] == Sign::Positive) ++n_front;
        else if (s[i] == Sign::Negative) ++n_back;
    }

    if (n_front == 0 && n_back == 0) {
        auto normal = [&](const Lattice3& a, const Lattice3& b, const Lattice3& c,
                          double& nx, double& ny, double& nz) {
            const double ux = double(b.x - a.x), uy = double(b.y - a.y), uz = double(b.z - a.z);
            const double wx = double(c.x - a.x), wy = double(c.y - a.y), wz = double(c.z - a.z);
            nx = uy * wz - uz * wy;
            ny = uz * wx - ux * wz;
            nz = ux * wy - uy * wx;
        };
        double an, bn, cn, dn2, en, fn;
        normal(poly.p0, poly.p1, poly.p2, an, bn, cn);
        normal(plane.p0, plane.p1, plane.p2, dn2, en, fn);
        if (an * dn2 + bn * en + cn * fn > 0.0) coplanar_front.push_back(poly);
        else coplanar_back.push_back(poly);
        return;
    }
    if (n_back == 0) { front.push_back(poly); return; }
    if (n_front == 0) { back.push_back(poly); return; }

    Poly f = poly, b = poly;
    f.v.clear(); f.v.reserve(n + 2);
    b.v.clear(); b.v.reserve(n + 2);
    f.incoming.clear(); b.incoming.clear();
    const EdgePlane cutter{plane.p0,plane.p1,plane.p2};

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const Lattice3& vi = poly.v[i];
        const Lattice3& vj = poly.v[j];

        const std::size_t previous=(i+n-1)%n;
        if (s[i] != Sign::Negative) {
            f.v.push_back(vi); f.incoming.push_back(s[i]==Sign::Zero && s[previous]==Sign::Negative ? cutter : poly.incoming[i]);
        }
        if (s[i] != Sign::Positive) {
            b.v.push_back(vi); b.incoming.push_back(s[i]==Sign::Zero && s[previous]==Sign::Positive ? cutter : poly.incoming[i]);
        }

        if ((s[i] == Sign::Positive && s[j] == Sign::Negative) ||
            (s[i] == Sign::Negative && s[j] == Sign::Positive)) {
            double ax, ay, az, bx, by, bz, q0x, q0y, q0z, q1x, q1y, q1z, q2x, q2y, q2z;
            gcm::to_world(vol, vi, ax, ay, az);
            gcm::to_world(vol, vj, bx, by, bz);
            gcm::to_world(vol, plane.p0, q0x, q0y, q0z);
            gcm::to_world(vol, plane.p1, q1x, q1y, q1z);
            gcm::to_world(vol, plane.p2, q2x, q2y, q2z);

            const double ux = q1x - q0x, uy = q1y - q0y, uz = q1z - q0z;
            const double wx = q2x - q0x, wy = q2y - q0y, wz = q2z - q0z;
            const double nx = uy * wz - uz * wy;
            const double ny = uz * wx - ux * wz;
            const double nz = ux * wy - uy * wx;

            const double dn = nx * (bx - ax) + ny * (by - ay) + nz * (bz - az);
            if (std::fabs(dn) < 1e-300) continue;
            const double t = (nx * (q0x - ax) + ny * (q0y - ay) + nz * (q0z - az)) / dn;
            const double tc = std::clamp(t, 0.0, 1.0);

            Lattice3 cut = gcm::to_lattice(vol, ax + (bx - ax) * tc,
                                           ay + (by - ay) * tc,
                                           az + (bz - az) * tc);
            if (!plane_crossing({poly.p0,poly.p1,poly.p2},poly.incoming[j],cutter,cut))
                cut = exact_crossing(cut, plane.p0, plane.p1, plane.p2, vi, vj);
            f.v.push_back(cut);
            b.v.push_back(cut);
            f.incoming.push_back(s[i]==Sign::Positive ? poly.incoming[j] : cutter);
            b.incoming.push_back(s[i]==Sign::Negative ? poly.incoming[j] : cutter);
        }
    }

    auto dedup = [](Poly& p) {
        const auto same=[](const Lattice3& a,const Lattice3& b) { return a.x==b.x && a.y==b.y && a.z==b.z; };
        std::size_t count=0;
        for (std::size_t i=0;i<p.v.size();++i) {
            if (count && same(p.v[count-1],p.v[i])) continue;
            p.v[count]=p.v[i]; p.incoming[count]=p.incoming[i]; ++count;
        }
        if (count>1 && same(p.v[0],p.v[count-1])) { p.incoming[0]=p.incoming[count-1]; --count; }
        p.v.resize(count); p.incoming.resize(count);
    };
    dedup(f);
    dedup(b);

    if (f.valid()) front.push_back(std::move(f));
    if (b.valid()) back.push_back(std::move(b));
}

std::vector<Poly> fragment_aabb(const Poly& poly, const std::vector<Poly>& planes,
                                const std::vector<LatticeAABB>& plane_aabbs,
                                const gcm::WorkingVolume& vol, bool convex) {
    Poly seed=poly;
    const auto normal=plane_normal({poly.p0,poly.p1,poly.p2});
    int axis=0;
    while (axis<2 && normal[axis].is_zero()) ++axis;
    for (std::size_t i=0;i<poly.v.size();++i) {
        const auto& a=poly.v[(i+poly.v.size()-1)%poly.v.size()];
        Lattice3 off=a;
        if (axis==0) ++off.x; else if (axis==1) ++off.y; else ++off.z;
        seed.incoming.push_back({a,poly.v[i],off});
    }
    std::vector<Poly> work{std::move(seed)}, next;
    if (plane_aabbs.empty()) return work;
    LatticeAABB tool_bounds=plane_aabbs.front();
    for (const auto& bounds:plane_aabbs) {
        tool_bounds.min_x=std::min(tool_bounds.min_x,bounds.min_x);
        tool_bounds.min_y=std::min(tool_bounds.min_y,bounds.min_y);
        tool_bounds.min_z=std::min(tool_bounds.min_z,bounds.min_z);
        tool_bounds.max_x=std::max(tool_bounds.max_x,bounds.max_x);
        tool_bounds.max_y=std::max(tool_bounds.max_y,bounds.max_y);
        tool_bounds.max_z=std::max(tool_bounds.max_z,bounds.max_z);
    }
    std::vector<Poly> outside;
    const LatticeAABB poly_aabb = LatticeAABB::from_poly(poly);

    for (std::size_t i = 0; i < planes.size(); ++i) {
        if (!poly_aabb.intersects(plane_aabbs[i])) continue;

        const auto& pl = planes[i];
        next.clear();
        for (const auto& p : work) {
            if (!LatticeAABB::from_poly(p).intersects(tool_bounds)) {
                outside.push_back(p);
                continue;
            }
            std::vector<Poly> f, b, cf, cb;
            split(p, pl, f, b, cf, cb, vol);
            for (auto& q : f) (convex ? outside : next).push_back(std::move(q));
            for (auto& q : b) next.push_back(std::move(q));
            for (auto& q : cf) next.push_back(std::move(q));
            for (auto& q : cb) next.push_back(std::move(q));
        }
        work.swap(next);
        if (work.size() > 262144) break;
    }
    work.insert(work.end(),std::make_move_iterator(outside.begin()),std::make_move_iterator(outside.end()));
    return work;
}

struct Tri { double a[3], b[3], c[3]; };

struct RayAABB {
    double min_p[3], max_p[3];

    bool intersects_ray(const double o[3], const double inv_d[3]) const noexcept {
        double tmin = 0.0, tmax = 1e300;
        for (int i = 0; i < 3; ++i) {
            double t1 = (min_p[i] - o[i]) * inv_d[i];
            double t2 = (max_p[i] - o[i]) * inv_d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        return true;
    }
};

struct BVHNode {
    RayAABB box;
    int left = -1;
    int right = -1;
    int tri_start = 0;
    int tri_count = 0;
    bool is_leaf() const noexcept { return left == -1; }
};

struct TriangleBVH {
    std::vector<Tri> tris;
    std::vector<BVHNode> nodes;

    void build(std::vector<Tri> input_tris) {
        tris = std::move(input_tris);
        if (tris.empty()) return;
        nodes.reserve(tris.size() * 2);
        build_recursive(0, tris.size());
    }

    int build_recursive(std::size_t start, std::size_t end) {
        int node_idx = static_cast<int>(nodes.size());
        nodes.push_back({});
        BVHNode node;

        RayAABB box;
        box.min_p[0] = box.min_p[1] = box.min_p[2] = 1e300;
        box.max_p[0] = box.max_p[1] = box.max_p[2] = -1e300;
        for (std::size_t i = start; i < end; ++i) {
            for (int c = 0; c < 3; ++c) {
                box.min_p[c] = std::min({box.min_p[c], tris[i].a[c], tris[i].b[c], tris[i].c[c]});
                box.max_p[c] = std::max({box.max_p[c], tris[i].a[c], tris[i].b[c], tris[i].c[c]});
            }
        }
        node.box = box;

        std::size_t count = end - start;
        if (count <= 4) {
            node.tri_start = static_cast<int>(start);
            node.tri_count = static_cast<int>(count);
            nodes[node_idx] = node;
            return node_idx;
        }

        int axis = 0;
        double dx = box.max_p[0] - box.min_p[0];
        double dy = box.max_p[1] - box.min_p[1];
        double dz = box.max_p[2] - box.min_p[2];
        if (dy > dx && dy > dz) axis = 1;
        else if (dz > dx && dz > dy) axis = 2;

        double mid = 0.5 * (box.min_p[axis] + box.max_p[axis]);
        auto mid_it = std::partition(tris.begin() + start, tris.begin() + end, [axis, mid](const Tri& t) {
            double centroid = (t.a[axis] + t.b[axis] + t.c[axis]) / 3.0;
            return centroid < mid;
        });

        std::size_t mid_idx = mid_it - tris.begin();
        if (mid_idx == start || mid_idx == end) mid_idx = start + count / 2;

        nodes[node_idx] = node;
        int left_child = build_recursive(start, mid_idx);
        int right_child = build_recursive(mid_idx, end);
        nodes[node_idx].left = left_child;
        nodes[node_idx].right = right_child;
        return node_idx;
    }

    int count_ray_hits(const double o[3], const double d[3]) const noexcept {
        if (nodes.empty()) return 0;
        const double inv_d[3] = {1.0 / d[0], 1.0 / d[1], 1.0 / d[2]};
        std::vector<double> hits;
        hits.reserve(8);

        int stack[64];
        int stack_ptr = 0;
        stack[stack_ptr++] = 0;

        while (stack_ptr > 0) {
            int curr = stack[--stack_ptr];
            const auto& node = nodes[curr];
            if (!node.box.intersects_ray(o, inv_d)) continue;

            if (node.is_leaf()) {
                for (int i = 0; i < node.tri_count; ++i) {
                    double tt;
                    if (ray_hits_fast(tris[node.tri_start + i], o, d, tt)) {
                        hits.push_back(tt);
                    }
                }
            } else {
                stack[stack_ptr++] = node.left;
                stack[stack_ptr++] = node.right;
            }
        }
        // A crossing through a shared edge counts once.
        std::sort(hits.begin(),hits.end());
        int crossings=0;
        double previous=0;
        for (double t:hits) {
            const double tolerance=64*std::numeric_limits<double>::epsilon()*std::max(1.0,std::fabs(t));
            if (crossings==0 || t-previous>tolerance) { ++crossings; previous=t; }
        }
        return crossings;
    }

    static bool ray_hits_fast(const Tri& t, const double o[3], const double d[3], double& tt) noexcept {
        const double e1[3] = {t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
        const double e2[3] = {t.c[0] - t.a[0], t.c[1] - t.a[1], t.c[2] - t.a[2]};
        const double h[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2],
                             d[0] * e2[1] - d[1] * e2[0]};
        const double det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
        if (std::fabs(det) < 1e-14) return false;
        const double inv = 1.0 / det;
        const double s[3] = {o[0] - t.a[0], o[1] - t.a[1], o[2] - t.a[2]};
        const double u = inv * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
        if (u < 0.0 || u > 1.0) return false;
        const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2],
                             s[0] * e1[1] - s[1] * e1[0]};
        const double vv = inv * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
        if (vv < 0.0 || u + vv > 1.0) return false;
        tt = inv * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
        return tt > 1e-9;
    }
};

std::vector<Tri> body_triangles(const Body& body, const gcm::WorkingVolume& vol) {
    std::vector<Tri> out;
    const TriMesh m = triangulate(body, vol);
    out.reserve(m.indices.size() / 3);
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        Tri t;
        gcm::to_world(vol, body.vertex_point[m.indices[i]],     t.a[0], t.a[1], t.a[2]);
        gcm::to_world(vol, body.vertex_point[m.indices[i + 1]], t.b[0], t.b[1], t.b[2]);
        gcm::to_world(vol, body.vertex_point[m.indices[i + 2]], t.c[0], t.c[1], t.c[2]);
        out.push_back(t);
    }
    return out;
}

bool point_inside_bvh(const TriangleBVH& bvh, const double p[3]) {
    static const double dirs[3][3] = {
        {0.5773502691896258, 0.5773502691896258, 0.5773502691896258},
        {-0.7071067811865476, 0.4082482904638631, 0.5773502691896258},
        {0.3015113445777636, -0.9045340337332909, 0.3015113445777636},
    };
    int votes = 0;
    for (const auto& d : dirs) {
        int hits = bvh.count_ray_hits(p, d);
        if (hits % 2 == 1) ++votes;
    }
    return votes >= 2;
}

void probe_point(const Poly& p, const gcm::WorkingVolume& vol, double out[3], double nudge) {
    double cx = 0, cy = 0, cz = 0;
    for (const auto& v : p.v) {
        double x, y, z;
        gcm::to_world(vol, v, x, y, z);
        cx += x; cy += y; cz += z;
    }
    const double n = static_cast<double>(p.v.size());
    cx /= n; cy /= n; cz /= n;

    double ax, ay, az, bx, by, bz, ccx, ccy, ccz;
    gcm::to_world(vol, p.p0, ax, ay, az);
    gcm::to_world(vol, p.p1, bx, by, bz);
    gcm::to_world(vol, p.p2, ccx, ccy, ccz);
    const double ux = bx - ax, uy = by - ay, uz = bz - az;
    const double wx = ccx - ax, wy = ccy - ay, wz = ccz - az;
    double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-300) { nx /= len; ny /= len; nz /= len; }

    out[0] = cx - nx * nudge;
    out[1] = cy - ny * nudge;
    out[2] = cz - nz * nudge;
}

std::vector<Poly> body_to_polys(const Body& b, bool from_b, const gcm::WorkingVolume& vol) {
    std::vector<Poly> out;
    TriMesh tm;
    bool have_tm = false;
    for (u32 f = 0; f < b.face_count(); ++f) {
        const bool holed = b.face_loop_end(f) - b.face_loop_begin(f) > 1;
        if (!holed) {
            const u32 li = b.face_loop_begin(f);
            Poly p;
            p.from_b = from_b;
            for (u32 h = b.loop_begin(li); h < b.loop_end(li); ++h)
                p.v.push_back(b.vertex_point[b.he_vertex[h]]);
            if (p.v.size() < 3) continue;
            if (!pick_plane(p.v, p.p0, p.p1, p.p2)) continue;
            out.push_back(std::move(p));
            continue;
        }
        if (!have_tm) { tm = triangulate(b, vol); have_tm = true; }
        for (std::size_t t = 0; t < tm.triangle_count(); ++t) {
            if (tm.face_of[t] != f) continue;
            Poly p;
            p.from_b = from_b;
            p.v = {b.vertex_point[tm.indices[t * 3]],
                   b.vertex_point[tm.indices[t * 3 + 1]],
                   b.vertex_point[tm.indices[t * 3 + 2]]};
            if (!pick_plane(p.v, p.p0, p.p1, p.p2)) continue;
            out.push_back(std::move(p));
        }
    }
    return out;
}

Body polys_to_body(const std::vector<Poly>& polys) {
    Builder bld;
    struct Key {
        gcm::i64 x, y, z;
        bool operator==(const Key& o) const noexcept { return x == o.x && y == o.y && z == o.z; }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            std::uint64_t x = static_cast<std::uint64_t>(k.x);
            std::uint64_t y = static_cast<std::uint64_t>(k.y);
            std::uint64_t z = static_cast<std::uint64_t>(k.z);
            std::uint64_t h = x ^ (y + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2));
            h ^= z + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return static_cast<std::size_t>(h);
        }
    };
    std::unordered_map<Key, VertexId, KeyHash> seen;
    seen.reserve(polys.size() * 4);

    auto vertex_of = [&](const Lattice3& p) -> VertexId {
        Key k{p.x, p.y, p.z};
        auto it = seen.find(k);
        if (it != seen.end()) return it->second;
        const VertexId id = bld.add_vertex(p);
        seen.emplace(k, id);
        return id;
    };

    for (const auto& p : polys) {
        if (p.v.size() < 3) continue;
        std::vector<VertexId> loop;
        loop.reserve(p.v.size());
        for (const auto& q : p.v) loop.push_back(vertex_of(q));
        bld.begin_face();
        bld.add_loop(loop);
        bld.end_face();
    }
    Body body = bld.build();
    if (body.analyse().closed) return body;

    // Split long edges at the opposite face's vertices so halfedges can pair.
    std::vector<VertexId> boundary_vertices;
    for (u32 h = 0; h < body.halfedge_count(); ++h) {
        if (body.he_twin[h] != kInvalid) continue;
        boundary_vertices.push_back(body.he_vertex[h]);
        boundary_vertices.push_back(body.he_vertex[body.he_next[h]]);
    }
    std::sort(boundary_vertices.begin(), boundary_vertices.end());
    boundary_vertices.erase(std::unique(boundary_vertices.begin(), boundary_vertices.end()), boundary_vertices.end());
    const auto stitch = [&](bool allow_rounding) {
        Builder stitched;
        for (const auto& p : body.vertex_point) stitched.add_vertex(p);
        for (u32 f = 0; f < body.face_count(); ++f) {
            const u32 l = body.face_loop_begin(f);
            std::vector<Lattice3> face_points;
            for (u32 h = body.loop_begin(l); h < body.loop_end(l); ++h)
                face_points.push_back(body.vertex_point[body.he_vertex[h]]);
            Lattice3 p0, p1, p2;
            const bool has_plane = pick_plane(face_points, p0, p1, p2);
            std::vector<VertexId> loop;
            for (u32 h = body.loop_begin(l); h < body.loop_end(l); ++h) {
                const VertexId start = body.he_vertex[h], end = body.he_vertex[body.he_next[h]];
                loop.push_back(start);
                if (body.he_twin[h] != kInvalid || !has_plane) continue;
                const auto& a = body.vertex_point[start];
                const auto& b = body.vertex_point[end];
                const i64 av[3] = {a.x, a.y, a.z}, bv[3] = {b.x, b.y, b.z};
                const i64 d[3] = {b.x-a.x, b.y-a.y, b.z-a.z};
                int axis = 0;
                for (int k = 1; k < 3; ++k)
                    if (gcm::abs_to_u64(d[k]) > gcm::abs_to_u64(d[axis])) axis = k;
                if (d[axis] == 0) continue;
                auto tolerance = gcm::SInt<128>::from_i64(allow_rounding ? d[axis] : 0);
                if (tolerance.is_negative()) tolerance = gcm::negate(tolerance);
                tolerance = gcm::add(tolerance, tolerance);
                std::vector<std::pair<i64, VertexId>> between;
                for (VertexId id : boundary_vertices) {
                    const auto& q = body.vertex_point[id];
                    const i64 qv[3] = {q.x, q.y, q.z};
                    if (qv[axis] <= std::min(av[axis],bv[axis]) ||
                        qv[axis] >= std::max(av[axis],bv[axis])) continue;
                    const i64 margin=allow_rounding ? 2 : 0;
                    bool in_bounds=true;
                    for (int k=0;k<3;++k)
                        if (qv[k]<std::min(av[k],bv[k])-margin || qv[k]>std::max(av[k],bv[k])+margin)
                            { in_bounds=false; break; }
                    if (!in_bounds) continue;
                    bool on_edge = true;
                    for (int k = 0; k < 3; ++k) {
                        // Crossing and endpoints rounded independently: two-lattice-unit bound.
                        auto residual = gcm::sub(gcm::mul_i64_i64<128>(qv[k]-av[k],d[axis]),
                                                 gcm::mul_i64_i64<128>(qv[axis]-av[axis],d[k]));
                        if (residual.is_negative()) residual = gcm::negate(residual);
                        if (gcm::sub(residual,tolerance).sign() == Sign::Positive) { on_edge = false; break; }
                    }
                    if (!on_edge) continue;
                    if (!on_plane_exact(q,p0,p1,p2)) {
                        if (!allow_rounding) continue;
                        // Rounded crossings can sit just off an oblique plane; bound it in lattice units.
                        const auto normal=plane_normal({p0,p1,p2});
                        Wide residual=dot_exact(normal,wide_point({q.x-p0.x,q.y-p0.y,q.z-p0.z}));
                        if (residual.is_negative()) residual=gcm::negate(residual);
                        Wide bound;
                        for (auto component:normal) {
                            if (component.is_negative()) component=gcm::negate(component);
                            bound=gcm::add(bound,component);
                        }
                        bound=gcm::add(bound,bound);
                        if (gcm::sub(residual,bound).sign()==Sign::Positive) continue;
                    }
                    between.push_back({qv[axis],id});
                }
                std::sort(between.begin(),between.end());
                if (d[axis] < 0) std::reverse(between.begin(),between.end());
                for (const auto& point : between) loop.push_back(point.second);
            }
            const bool planar=!has_plane || std::all_of(loop.begin(),loop.end(),[&](VertexId id) {
                return on_plane_exact(body.vertex_point[id],p0,p1,p2);
            });
            if (planar) {
                stitched.begin_face(); stitched.add_loop(loop); stitched.end_face();
            } else {
                const double u[3]={double(p1.x-p0.x),double(p1.y-p0.y),double(p1.z-p0.z)};
                const double v[3]={double(p2.x-p0.x),double(p2.y-p0.y),double(p2.z-p0.z)};
                const double n[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
                int axis=0;
                for(int k=1;k<3;++k) if(std::fabs(n[k])>std::fabs(n[axis])) axis=k;
                int x=(axis+1)%3,y=(axis+2)%3;
                if(n[axis]<0) std::swap(x,y);
                std::vector<gcm::Lattice2> points;
                std::vector<u32> indices;
                for(VertexId id:loop) {
                    const auto& p=body.vertex_point[id];const i64 q[3]={p.x,p.y,p.z};
                    indices.push_back(u32(points.size()));points.push_back({q[x],q[y]});
                }
                const auto triangles=triangulate_face({indices},points);
                for(std::size_t i=0;i+2<triangles.size();i+=3) {
                    stitched.begin_face();
                    stitched.add_loop({loop[triangles[i]],loop[triangles[i+1]],loop[triangles[i+2]]});
                    stitched.end_face();
                }
            }
        }
        Body result = stitched.build();
        const Topology topology = result.analyse();

        return topology.closed && topology.manifold ? result : body;
    };
    Body exact = stitch(false);
    if (exact.analyse().closed) return exact;
    return stitch(true);
}

}

bool all_faces_planar(const Body& b) {
    for (u32 f = 0; f < b.face_count(); ++f) {
        for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li) {
            const u32 s = b.loop_begin(li), e = b.loop_end(li);
            if (e - s < 4) continue;
            std::vector<Lattice3> v;
            for (u32 h = s; h < e; ++h) v.push_back(b.vertex_point[b.he_vertex[h]]);
            Lattice3 a, bb, c;
            if (!pick_plane(v, a, bb, c)) continue;
            for (const auto& q : v)
                if (P::orient3d(a, bb, c, q) != Sign::Zero) return false;
        }
    }
    return true;
}

gcm::SInt<256> volume_6x(const Body& b) {
    gcm::SInt<256> acc;
    for (u32 f = 0; f < b.face_count(); ++f) {
        const u32 l0 = b.face_loop_begin(f);
        const u32 s = b.loop_begin(l0), e = b.loop_end(l0);
        if (e - s < 3) continue;
        const Lattice3& a = b.vertex_point[b.he_vertex[s]];
        for (u32 h = s + 1; h + 1 < e; ++h) {
            const Lattice3& p = b.vertex_point[b.he_vertex[h]];
            const Lattice3& q = b.vertex_point[b.he_vertex[h + 1]];

            gcm::SInt<256> t = gcm::mul_i64_i64<256>(p.y, q.z);
            t = gcm::sub(t, gcm::mul_i64_i64<256>(p.z, q.y));
            t = gcm::mul_i64(t, a.x);

            gcm::SInt<256> u = gcm::mul_i64_i64<256>(p.x, q.z);
            u = gcm::sub(u, gcm::mul_i64_i64<256>(p.z, q.x));
            u = gcm::mul_i64(u, a.y);

            gcm::SInt<256> w = gcm::mul_i64_i64<256>(p.x, q.y);
            w = gcm::sub(w, gcm::mul_i64_i64<256>(p.y, q.x));
            w = gcm::mul_i64(w, a.z);

            acc = gcm::add(acc, gcm::sub(gcm::add(t, w), u));
        }
    }
    return acc;
}

double volume(const Body& b, const gcm::WorkingVolume& vol) {
    const TriMesh m = triangulate(b, vol);
    double total = 0.0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        double ax, ay, az, px, py, pz, qx, qy, qz;
        gcm::to_world(vol, b.vertex_point[m.indices[i]],     ax, ay, az);
        gcm::to_world(vol, b.vertex_point[m.indices[i + 1]], px, py, pz);
        gcm::to_world(vol, b.vertex_point[m.indices[i + 2]], qx, qy, qz);
        total += ax * (py * qz - pz * qy) - ay * (px * qz - pz * qx) +
                 az * (px * qy - py * qx);
    }
    return total / 6.0;
}

BoolResult boolean_op(const Body& a, const Body& b, BoolOp op,
                      const gcm::WorkingVolume& vol) {
    BoolResult r;

    if (a.face_count() == 0 || b.face_count() == 0) {
        r.error = BoolError::Degenerate;
        return r;
    }
    if (!a.analyse().closed || !b.analyse().closed) {
        r.error = BoolError::OpenBody;
        return r;
    }
    if (!all_faces_planar(a) || !all_faces_planar(b)) {
        r.error = BoolError::NonPlanarFace;
        return r;
    }

    std::vector<Poly> pa = body_to_polys(a, false, vol);
    std::vector<Poly> pb = body_to_polys(b, true, vol);

    const auto ba = a.bounds(vol), bb = b.bounds(vol);
    if (!ba.intersects(bb)) {
        std::vector<Poly> out;
        switch (op) {
            case BoolOp::Union:
                out = pa;
                out.insert(out.end(), pb.begin(), pb.end());
                break;
            case BoolOp::Subtract:
                out = pa;
                break;
            case BoolOp::Intersect:
                r.error = BoolError::EmptyResult;
                return r;
        }
        r.body = polys_to_body(out);
        return r;
    }

    TriangleBVH bvh_a, bvh_b;
    bvh_a.build(body_triangles(a, vol));
    bvh_b.build(body_triangles(b, vol));

    const double nudge = vol.inv_scale() * 64.0;
    std::vector<Poly> out;

    auto classify = [&](const Poly& frag, const TriangleBVH& other_bvh,
                        bool& back, bool& front) {
        double p_in[3], p_out[3];
        probe_point(frag, vol, p_in, nudge);
        probe_point(frag, vol, p_out, -nudge);
        back = point_inside_bvh(other_bvh, p_in);
        front = point_inside_bvh(other_bvh, p_out);
    };

    auto classify_pool = [&](std::vector<Poly>& frags, const TriangleBVH& other_bvh,
                             auto&& decide) {
        std::vector<std::vector<Poly>> parts(gcm::worker_count());
        gcm::parallel_for(frags.size(), 32, [&](std::size_t lo, std::size_t hi, unsigned t) {
            for (std::size_t i = lo; i < hi; ++i)
                if (decide(frags[i], other_bvh)) parts[t].push_back(std::move(frags[i]));
        });
        for (auto& part : parts)
            for (auto& frag : part) out.push_back(std::move(frag));
    };

    const auto convex=[](const std::vector<Poly>& polys) {
        for (const auto& plane:polys)
            for (const auto& poly:polys)
                for (const auto& point:poly.v)
                    if (side_of(plane,point)==Sign::Positive) return false;
        return true;
    };
    const bool convex_a=convex(pa),convex_b=convex(pb);

    auto parallel_fragment = [&](const std::vector<Poly>& source_polys, const std::vector<Poly>& target_planes, bool target_convex) {
        std::vector<LatticeAABB> target_aabbs(target_planes.size());
        for (std::size_t i = 0; i < target_planes.size(); ++i) {
            target_aabbs[i] = LatticeAABB::from_poly(target_planes[i]);
        }

        std::vector<std::vector<Poly>> thread_frags(gcm::worker_count());
        gcm::parallel_for(source_polys.size(), 16, [&](std::size_t lo, std::size_t hi, unsigned t) {
            for (std::size_t i = lo; i < hi; ++i) {
                auto f = fragment_aabb(source_polys[i], target_planes, target_aabbs, vol, target_convex);
                thread_frags[t].insert(thread_frags[t].end(),
                                       std::make_move_iterator(f.begin()),
                                       std::make_move_iterator(f.end()));
            }
        });

        std::vector<Poly> all_frags;
        std::size_t total = 0;
        for (const auto& tf : thread_frags) total += tf.size();
        all_frags.reserve(total);
        for (auto& tf : thread_frags) {
            all_frags.insert(all_frags.end(), std::make_move_iterator(tf.begin()), std::make_move_iterator(tf.end()));
        }
        return all_frags;
    };

    {
        std::vector<Poly> frags = parallel_fragment(pa, pb, convex_b);
        classify_pool(frags, bvh_b, [&](Poly& frag, const TriangleBVH& other_bvh) {
            if (!frag.valid()) return false;
            bool in_b = false, front_b = false;
            classify(frag, other_bvh, in_b, front_b);
            switch (op) {
                case BoolOp::Union:     return !front_b;
                case BoolOp::Subtract:  return !in_b;
                case BoolOp::Intersect: return in_b;
            }
            return false;
        });
    }

    {
        std::vector<Poly> frags = parallel_fragment(pb, pa, convex_a);
        classify_pool(frags, bvh_a, [&](Poly& frag, const TriangleBVH& other_bvh) {
            if (!frag.valid()) return false;
            bool in_a = false, front_a = false;
            classify(frag, other_bvh, in_a, front_a);

            if (in_a && !front_a) return false;

            bool keep = false;
            switch (op) {
                case BoolOp::Union:     keep = !front_a && !in_a; break;
                case BoolOp::Subtract:  keep =  in_a; break;
                case BoolOp::Intersect: keep =  in_a; break;
            }
            if (!keep) return false;

            if (op == BoolOp::Subtract) {
                std::reverse(frag.v.begin(), frag.v.end());
                std::swap(frag.p1, frag.p2);
            }
            return true;
        });
    }

    if (out.empty()) {
        r.error = BoolError::EmptyResult;
        return r;
    }
    r.body = polys_to_body(out);
    return r;
}

}
