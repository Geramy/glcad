#include "geometry/triangulate.hpp"

#include <algorithm>
#include <cmath>

#include "math/predicate.hpp"

namespace gtopo {

using gcm::F64;
using gcm::Lattice2;
using gcm::Lattice3;
using gcm::Sign;
using P = gcm::predicate::Host;

namespace {

bool point_in_triangle(Lattice2 p, Lattice2 a, Lattice2 b, Lattice2 c) {
    if (p.x<std::min({a.x,b.x,c.x}) || p.x>std::max({a.x,b.x,c.x}) ||
        p.y<std::min({a.y,b.y,c.y}) || p.y>std::max({a.y,b.y,c.y})) return false;
    const Sign d1 = P::orient2d(a, b, p);
    const Sign d2 = P::orient2d(b, c, p);
    const Sign d3 = P::orient2d(c, a, p);
    const bool neg = d1 == Sign::Negative || d2 == Sign::Negative || d3 == Sign::Negative;
    const bool pos = d1 == Sign::Positive || d2 == Sign::Positive || d3 == Sign::Positive;
    return !(neg && pos);
}

bool crosses(Lattice2 a, Lattice2 b, Lattice2 c, Lattice2 d) {
    auto same = [](Lattice2 u, Lattice2 v) { return u.x == v.x && u.y == v.y; };
    if (same(a, c) || same(a, d) || same(b, c) || same(b, d)) return false;
    return P::segments_properly_intersect(a, b, c, d);
}

void ear_clip(std::vector<u32> poly, const std::vector<Lattice2>& pts,
              std::vector<u32>& out) {
    if (poly.size() < 3) return;
    int cursor=0;

    while (poly.size() > 3) {
        const int n = static_cast<int>(poly.size());
        bool clipped = false;

        for (int visit = 0; visit < n; ++visit) {
            const int i=(cursor+visit)%n;
            const u32 ia = poly[(i + n - 1) % n];
            const u32 ib = poly[i];
            const u32 ic = poly[(i + 1) % n];

            if (P::orient2d(pts[ia], pts[ib], pts[ic]) != Sign::Positive) continue;

            const auto lox = std::min({pts[ia].x, pts[ib].x, pts[ic].x}), hix = std::max({pts[ia].x, pts[ib].x, pts[ic].x});
            const auto loy = std::min({pts[ia].y, pts[ib].y, pts[ic].y}), hiy = std::max({pts[ia].y, pts[ib].y, pts[ic].y});
            bool empty = true;
            for (int j = 0; j < n && empty; ++j) {
                const u32 iv = poly[j];
                const auto& q = pts[iv];
                if (q.x < lox || q.x > hix || q.y < loy || q.y > hiy) continue;
                if (iv == ia || iv == ib || iv == ic) continue;
                if (point_in_triangle(q, pts[ia], pts[ib], pts[ic])) empty = false;
            }
            if (!empty) continue;

            out.push_back(ia);
            out.push_back(ib);
            out.push_back(ic);
            poly.erase(poly.begin() + i);
            cursor=(i+1)%int(poly.size());
            clipped = true;
            break;
        }

        if (!clipped) {
            int drop = 0;
            for (int i = 0; i < n; ++i) {
                const u32 ia = poly[(i + n - 1) % n];
                const u32 ib = poly[i];
                const u32 ic = poly[(i + 1) % n];
                if (P::orient2d(pts[ia], pts[ib], pts[ic]) == Sign::Zero) { drop = i; break; }
            }
            poly.erase(poly.begin() + drop);
            cursor=drop%int(poly.size());
        }
    }
    if (poly.size() == 3) {
        out.push_back(poly[0]);
        out.push_back(poly[1]);
        out.push_back(poly[2]);
    }
}

struct EdgeGrid {
    double x0{0}, y0{0}, cell{1};
    int nx{1}, ny{1};
    std::vector<std::vector<std::pair<Lattice2, Lattice2>>> cells;
    void build(const std::vector<std::pair<Lattice2, Lattice2>>& edges) {
        if (edges.empty()) return;
        double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
        for (const auto& [a, b] : edges)
            for (const auto& p : {a, b}) {
                minx = std::min(minx, double(p.x)); maxx = std::max(maxx, double(p.x));
                miny = std::min(miny, double(p.y)); maxy = std::max(maxy, double(p.y));
            }
        const double side = std::max({maxx - minx, maxy - miny, 1.0});
        const int n = std::clamp(int(std::sqrt(double(edges.size()))), 1, 512);
        cell = side / n;
        x0 = minx; y0 = miny; nx = int((maxx - minx) / cell) + 1; ny = int((maxy - miny) / cell) + 1;
        cells.assign(std::size_t(nx) * std::size_t(ny), {});
        for (const auto& e : edges) visit(e.first, e.second, [&](auto& c) { c.push_back(e); return false; });
    }
    template <class F> bool visit(Lattice2 a, Lattice2 b, F&& f) {
        if (cells.empty()) return false;
        const int i0 = std::clamp(int((std::min(a.x, b.x) - x0) / cell), 0, nx - 1);
        const int i1 = std::clamp(int((std::max(a.x, b.x) - x0) / cell), 0, nx - 1);
        const int j0 = std::clamp(int((std::min(a.y, b.y) - y0) / cell), 0, ny - 1);
        const int j1 = std::clamp(int((std::max(a.y, b.y) - y0) / cell), 0, ny - 1);
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
                if (f(cells[std::size_t(j) * std::size_t(nx) + std::size_t(i)])) return true;
        return false;
    }
};

std::vector<u32> bridge_holes(std::vector<std::vector<u32>> loops,
                              const std::vector<Lattice2>& pts) {
    std::vector<u32> outer = loops[0];
    const auto locally_inside = [&](const std::vector<u32>& loop, std::size_t i, Lattice2 point) {
        const auto a=pts[loop[(i+loop.size()-1)%loop.size()]],b=pts[loop[i]],c=pts[loop[(i+1)%loop.size()]];
        const auto left=P::orient2d(b,point,a),right=P::orient2d(b,c,point);
        if (P::orient2d(a,b,c)==Sign::Positive)
            return left!=Sign::Negative && right!=Sign::Negative;
        return left==Sign::Positive || right==Sign::Positive;
    };

    std::vector<std::pair<Lattice2, Lattice2>> edges;
    for (const auto& loop : loops)
        for (std::size_t k = 0; k < loop.size(); ++k)
            edges.push_back({pts[loop[k]], pts[loop[(k + 1) % loop.size()]]});
    EdgeGrid grid;
    grid.build(edges);
    std::vector<std::pair<Lattice2, Lattice2>> bridges;
    const auto blocked = [&](Lattice2 a, Lattice2 b) {
        for (const auto& [c, d] : bridges)
            if (crosses(a, b, c, d)) return true;
        return grid.visit(a, b, [&](const auto& cell) {
            for (const auto& [c, d] : cell)
                if (crosses(a, b, c, d)) return true;
            return false;
        });
    };

    struct Candidate { double d2; std::size_t oi, hj; };
    std::vector<Candidate> candidates;
    std::vector<std::pair<double, std::size_t>> near;
    for (std::size_t hi = 1; hi < loops.size(); ++hi) {
        const std::vector<u32>& hole = loops[hi];
        if (hole.size() < 3) continue;
        double hx0 = 1e300, hy0 = 1e300, hx1 = -1e300, hy1 = -1e300;
        for (const auto v : hole) {
            hx0 = std::min(hx0, double(pts[v].x)); hx1 = std::max(hx1, double(pts[v].x));
            hy0 = std::min(hy0, double(pts[v].y)); hy1 = std::max(hy1, double(pts[v].y));
        }
        near.clear();
        for (std::size_t oi = 0; oi < outer.size(); ++oi) {
            const double x = double(pts[outer[oi]].x), y = double(pts[outer[oi]].y);
            const double dx = x < hx0 ? hx0 - x : x > hx1 ? x - hx1 : 0.0;
            const double dy = y < hy0 ? hy0 - y : y > hy1 ? y - hy1 : 0.0;
            near.push_back({dx * dx + dy * dy, oi});
        }
        int bo = -1, bh = -1;
        for (const std::size_t limit : {std::size_t(64), outer.size()}) {
            const std::size_t take = std::min(limit, near.size());
            std::partial_sort(near.begin(), near.begin() + std::ptrdiff_t(take), near.end());
            candidates.clear();
            for (std::size_t n = 0; n < take; ++n) {
                const std::size_t oi = near[n].second;
                for (std::size_t hj = 0; hj < hole.size(); ++hj) {
                    const Lattice2 a = pts[outer[oi]], b = pts[hole[hj]];
                    const double dx = double(a.x - b.x), dy = double(a.y - b.y);
                    candidates.push_back({dx * dx + dy * dy, oi, hj});
                }
            }
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& p, const Candidate& q) {
                return p.d2 != q.d2 ? p.d2 < q.d2 : (p.oi != q.oi ? p.oi < q.oi : p.hj < q.hj);
            });
            for (const auto& c : candidates) {
                const Lattice2 a = pts[outer[c.oi]], b = pts[hole[c.hj]];
                if (!locally_inside(outer, c.oi, b) || !locally_inside(hole, c.hj, a)) continue;
                if (blocked(a, b)) continue;
                bo = int(c.oi); bh = int(c.hj);
                break;
            }
            if (bo >= 0 || take == near.size()) break;
        }
        if (bo < 0) continue;

        bridges.push_back({pts[outer[std::size_t(bo)]], pts[hole[std::size_t(bh)]]});
        std::vector<u32> merged;
        merged.reserve(outer.size() + hole.size() + 2);
        for (int i = 0; i <= bo; ++i) merged.push_back(outer[std::size_t(i)]);
        for (std::size_t k = 0; k < hole.size(); ++k)
            merged.push_back(hole[(std::size_t(bh) + k) % hole.size()]);
        merged.push_back(hole[std::size_t(bh)]);
        for (std::size_t i = std::size_t(bo); i < outer.size(); ++i) merged.push_back(outer[i]);
        outer.swap(merged);
    }
    return outer;
}

}

std::vector<u32> triangulate_face(const std::vector<std::vector<u32>>& loops,
                                  const std::vector<Lattice2>& pts) {
    std::vector<u32> out;
    if (loops.empty() || loops[0].size() < 3) return out;
    if (loops.size()==1) {
        const auto& loop=loops[0];
        bool convex=true;
        for(std::size_t i=0;i<loop.size();++i)
            if(P::orient2d(pts[loop[i]],pts[loop[(i+1)%loop.size()]],pts[loop[(i+2)%loop.size()]])!=Sign::Positive) {
                convex=false;break;
            }
        if(convex) {
            out.reserve((loop.size()-2)*3);
            const auto split=[&](auto&& self,std::size_t a,std::size_t b)->void {
                if(b-a<2)return;
                const auto mid=a+(b-a)/2;
                self(self,a,mid);self(self,mid,b);
                out.insert(out.end(),{loop[a],loop[mid],loop[b]});
            };
            split(split,0,loop.size()-1);
            return out;
        }
    }
    ear_clip(loops.size() == 1 ? loops[0] : bridge_holes(loops, pts), pts, out);
    return out;
}

TriMesh triangulate(const Body& b, const gcm::WorkingVolume& vol) {
    TriMesh mesh;

    for (u32 f = 0; f < b.face_count(); ++f) {
        ggeo::d3::Vec<F64> n = ggeo::d3::Vec<F64>::zero();
        {
            const u32 l0 = b.face_loop_begin(f);
            const u32 s = b.loop_begin(l0), e = b.loop_end(l0);
            for (u32 h = s; h < e; ++h) {
                const auto& p = b.vertex_point[b.he_vertex[h]];
                const auto& q = b.vertex_point[b.he_vertex[(h + 1 < e) ? h + 1 : s]];
                n.x = n.x + F64::from(double(p.y - q.y) * double(p.z + q.z));
                n.y = n.y + F64::from(double(p.z - q.z) * double(p.x + q.x));
                n.z = n.z + F64::from(double(p.x - q.x) * double(p.y + q.y));
            }
            const double len = std::sqrt(n.x.to_f64() * n.x.to_f64() +
                                         n.y.to_f64() * n.y.to_f64() +
                                         n.z.to_f64() * n.z.to_f64());
            if (len > 0) n = ggeo::d3::Vec<F64>::from(n.x.to_f64() / len,
                                                      n.y.to_f64() / len,
                                                      n.z.to_f64() / len);
        }

        const double ax = std::fabs(n.x.to_f64());
        const double ay = std::fabs(n.y.to_f64());
        const double az = std::fabs(n.z.to_f64());
        int u_ax, v_ax;
        if (az >= ax && az >= ay)      { u_ax = 0; v_ax = 1; if (n.z.to_f64() < 0) std::swap(u_ax, v_ax); }
        else if (ay >= ax)             { u_ax = 2; v_ax = 0; if (n.y.to_f64() < 0) std::swap(u_ax, v_ax); }
        else                           { u_ax = 1; v_ax = 2; if (n.x.to_f64() < 0) std::swap(u_ax, v_ax); }

        auto axis = [](const Lattice3& p, int a) -> gcm::i64 {
            return a == 0 ? p.x : (a == 1 ? p.y : p.z);
        };

        std::vector<gcm::Lattice2> pts;
        std::vector<u32>           global;
        std::vector<std::vector<u32>> loops;

        for (u32 li = b.face_loop_begin(f); li < b.face_loop_end(f); ++li) {
            std::vector<u32> loop;
            for (u32 h = b.loop_begin(li); h < b.loop_end(li); ++h) {
                const u32 gv = b.he_vertex[h];
                const Lattice3& p = b.vertex_point[gv];
                loop.push_back(static_cast<u32>(pts.size()));
                pts.push_back(Lattice2{axis(p, u_ax), axis(p, v_ax)});
                global.push_back(gv);
            }
            loops.push_back(std::move(loop));
        }

        const std::vector<u32> tri = triangulate_face(loops, pts);
        for (std::size_t i = 0; i + 2 < tri.size(); i += 3) {
            mesh.indices.push_back(global[tri[i]]);
            mesh.indices.push_back(global[tri[i + 1]]);
            mesh.indices.push_back(global[tri[i + 2]]);
            mesh.normals.push_back(n);
            mesh.face_of.push_back(f);
        }
    }
    (void)vol;
    return mesh;
}

}
