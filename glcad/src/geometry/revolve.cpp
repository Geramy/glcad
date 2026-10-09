#include "geometry/revolve.hpp"

#include <algorithm>
#include <cmath>

namespace gtopo {

using gcm::F64;
using gcm::Lattice3;
using P2 = ggeo::d2::Point<F64>;
using V2 = ggeo::d2::Vec<F64>;
using P3 = ggeo::d3::Point<F64>;
using V3 = ggeo::d3::Vec<F64>;

constexpr f64 kTwoPi = 6.28318530717958647692;

namespace {

struct Local { f64 along; f64 radius; };

Local to_local(f64 px, f64 py, f64 ax, f64 ay, f64 ux, f64 uy) {
    const f64 dx = px - ax, dy = py - ay;
    const f64 along = dx * ux + dy * uy;
    const f64 radius = dx * (-uy) + dy * ux;
    return {along, radius};
}

}

bool axis_crosses_profile(const Profile& p, const gcm::WorkingVolume& vol,
                          P2 axis_point, V2 axis_dir) {
    const f64 ax = axis_point.x.to_f64(), ay = axis_point.y.to_f64();
    f64 ux = axis_dir.x.to_f64(), uy = axis_dir.y.to_f64();
    const f64 len = std::hypot(ux, uy);
    if (len < 1e-12) return true;
    ux /= len; uy /= len;

    // Few-lattice-step tolerance: on-axis points round off the axis.
    const f64 eps = 4.0 / vol.scale();
    bool pos = false, neg = false;
    for (const auto& q : p.outer) {
        f64 x, y;
        gcm::to_world(vol, q, x, y);
        const f64 r = to_local(x, y, ax, ay, ux, uy).radius;
        if (r > eps) pos = true;
        if (r < -eps) neg = true;
    }
    return pos && neg;
}

Body revolve(const Profile& p_in, const gcm::WorkingVolume& vol,
             const ggeo::d3::Frame<F64>& frame, P2 axis_point, V2 axis_dir,
             f64 angle, int segments) {
    Builder b;

    Profile p = p_in;
    p.normalize();
    if (!p.valid() || segments < 3) return b.build();
    if (axis_crosses_profile(p, vol, axis_point, axis_dir)) return b.build();

    const f64 ax = axis_point.x.to_f64(), ay = axis_point.y.to_f64();
    f64 ux = axis_dir.x.to_f64(), uy = axis_dir.y.to_f64();
    const f64 len = std::hypot(ux, uy);
    if (len < 1e-12) return b.build();
    ux /= len; uy /= len;

    const bool full = std::fabs(std::fabs(angle) - kTwoPi) < 1e-9;
    const int rings = full ? segments : segments + 1;

    const V3 axis3 = ggeo::d3::normalized(frame.u * F64::from(ux) +
                                          frame.v * F64::from(uy));
    const V3 perp3 = ggeo::d3::normalized(frame.u * F64::from(-uy) +
                                          frame.v * F64::from(ux));
    const V3 out3  = ggeo::d3::cross(axis3, perp3);

    std::vector<const Loop2*> loops{&p.outer};
    for (const auto& h : p.holes) loops.push_back(&h);

    std::vector<std::vector<std::vector<VertexId>>> grid(loops.size());

    for (std::size_t li = 0; li < loops.size(); ++li) {
        const Loop2& L = *loops[li];
        grid[li].resize(rings);

        for (int r = 0; r < rings; ++r) {
            const f64 t = angle * (static_cast<f64>(r) / static_cast<f64>(segments));
            const f64 ct = std::cos(t), stt = std::sin(t);
            grid[li][r].reserve(L.size());

            for (const auto& q : L) {
                f64 qx, qy;
                gcm::to_world(vol, q, qx, qy);
                const Local lo = to_local(qx, qy, ax, ay, ux, uy);

                // Origin + along*axis + radius*(cos t * perp + sin t * out)
                const P3 world =
                    frame.to_world(F64::from(ax), F64::from(ay)) +
                    axis3 * F64::from(lo.along) +
                    perp3 * F64::from(lo.radius * ct) +
                    out3  * F64::from(lo.radius * stt);

                grid[li][r].push_back(b.add_vertex(gcm::to_lattice(
                    vol, world.x.to_f64(), world.y.to_f64(), world.z.to_f64())));
            }
        }
    }

    for (std::size_t li = 0; li < loops.size(); ++li) {
        const std::size_t n = grid[li][0].size();
        const int steps = full ? segments : segments;
        for (int r = 0; r < steps; ++r) {
            const int r1 = full ? (r + 1) % segments : r + 1;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                b.begin_face();
                b.add_loop({grid[li][r][i], grid[li][r][j],
                            grid[li][r1][j], grid[li][r1][i]});
                b.end_face();
            }
        }
    }

    if (!full) {
        b.begin_face();
        {
            std::vector<VertexId> o = grid[0][0];
            std::reverse(o.begin(), o.end());
            b.add_loop(o);
            for (std::size_t li = 1; li < loops.size(); ++li) {
                std::vector<VertexId> h = grid[li][0];
                std::reverse(h.begin(), h.end());
                b.add_loop(h);
            }
        }
        b.end_face();

        b.begin_face();
        b.add_loop(grid[0][rings - 1]);
        for (std::size_t li = 1; li < loops.size(); ++li)
            b.add_loop(grid[li][rings - 1]);
        b.end_face();
    }

    return b.build();
}

Body revolve_y(const Profile& p, const gcm::WorkingVolume& vol, f64 angle,
               int segments) {
    return revolve(p, vol, ggeo::d3::Frame<F64>::world_xy(),
                   P2::from(0.0, 0.0), V2::from(0.0, 1.0), angle, segments);
}

Body sweep(const Profile& p_in, const gcm::WorkingVolume& vol,
           const std::vector<P3>& path, bool cap_ends) {
    Builder b;

    Profile p = p_in;
    p.normalize();
    if (!p.valid() || path.size() < 2) return b.build();

    std::vector<const Loop2*> loops{&p.outer};
    for (const auto& h : p.holes) loops.push_back(&h);

    V3 tangent = ggeo::d3::normalized(path[1] - path[0]);
    V3 up = std::fabs(tangent.z.to_f64()) > 0.9 ? V3::unit_x() : V3::unit_z();
    V3 side = ggeo::d3::normalized(ggeo::d3::cross(up, tangent));
    V3 vert = ggeo::d3::cross(tangent, side);

    std::vector<std::vector<std::vector<VertexId>>> grid(loops.size());
    for (auto& g : grid) g.resize(path.size());

    for (std::size_t k = 0; k < path.size(); ++k) {
        if (k > 0) {
            const V3 next = (k + 1 < path.size())
                ? ggeo::d3::normalized(path[k + 1] - path[k])
                : tangent;

            const V3 axis = ggeo::d3::cross(tangent, next);
            const f64 sin_a = ggeo::d3::length(axis).to_f64();
            const f64 cos_a = ggeo::d3::dot(tangent, next).to_f64();
            if (sin_a > 1e-12) {
                const V3 n = ggeo::d3::normalized(axis);
                auto rotate = [&](V3 v) {
                    const V3 c = ggeo::d3::cross(n, v);
                    const f64 d = ggeo::d3::dot(n, v).to_f64();
                    return v * F64::from(cos_a) + c * F64::from(sin_a) +
                           n * F64::from(d * (1.0 - cos_a));
                };
                side = ggeo::d3::normalized(rotate(side));
                vert = ggeo::d3::normalized(rotate(vert));
            }
            tangent = next;
        }

        for (std::size_t li = 0; li < loops.size(); ++li) {
            const Loop2& L = *loops[li];
            grid[li][k].reserve(L.size());
            for (const auto& q : L) {
                f64 qx, qy;
                gcm::to_world(vol, q, qx, qy);
                const P3 world = path[k] + side * F64::from(qx) + vert * F64::from(qy);
                grid[li][k].push_back(b.add_vertex(gcm::to_lattice(
                    vol, world.x.to_f64(), world.y.to_f64(), world.z.to_f64())));
            }
        }
    }

    for (std::size_t li = 0; li < loops.size(); ++li) {
        const std::size_t n = grid[li][0].size();
        for (std::size_t k = 0; k + 1 < path.size(); ++k)
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                b.begin_face();
                b.add_loop({grid[li][k][i], grid[li][k][j],
                            grid[li][k + 1][j], grid[li][k + 1][i]});
                b.end_face();
            }
    }

    if (cap_ends) {
        b.begin_face();
        {
            std::vector<VertexId> o = grid[0][0];
            std::reverse(o.begin(), o.end());
            b.add_loop(o);
            for (std::size_t li = 1; li < loops.size(); ++li) {
                std::vector<VertexId> h = grid[li][0];
                std::reverse(h.begin(), h.end());
                b.add_loop(h);
            }
        }
        b.end_face();

        b.begin_face();
        b.add_loop(grid[0].back());
        for (std::size_t li = 1; li < loops.size(); ++li) b.add_loop(grid[li].back());
        b.end_face();
    }

    return b.build();
}

}
