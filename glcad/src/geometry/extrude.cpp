#include "geometry/extrude.hpp"

#include <algorithm>

namespace gtopo {

using gcm::F64;
using gcm::Lattice3;

Body extrude(const Profile& p_in, const gcm::WorkingVolume& vol,
             const ggeo::d3::Frame<F64>& frame, f64 height) {
    Profile p = p_in;
    p.normalize();

    Builder b;
    if (!p.valid()) return b.build();

    std::vector<const Loop2*> loops;
    loops.push_back(&p.outer);
    for (const auto& h : p.holes) loops.push_back(&h);

    std::vector<std::vector<VertexId>> bot(loops.size()), top(loops.size());

    for (std::size_t li = 0; li < loops.size(); ++li) {
        const Loop2& L = *loops[li];
        bot[li].reserve(L.size());
        top[li].reserve(L.size());
        for (const auto& q : L) {
            f64 px, py;
            gcm::to_world(vol, q, px, py);

            const auto base = frame.to_world(F64::from(px), F64::from(py));
            const auto up   = frame.n;

            const auto b3 = base;
            const auto t3 = base + up * F64::from(height);

            bot[li].push_back(b.add_vertex(
                gcm::to_lattice(vol, b3.x.to_f64(), b3.y.to_f64(), b3.z.to_f64())));
            top[li].push_back(b.add_vertex(
                gcm::to_lattice(vol, t3.x.to_f64(), t3.y.to_f64(), t3.z.to_f64())));
        }
    }

    b.begin_face();
    {
        std::vector<VertexId> o = bot[0];
        std::reverse(o.begin(), o.end());
        b.add_loop(o);
        for (std::size_t li = 1; li < loops.size(); ++li) {
            std::vector<VertexId> h = bot[li];
            std::reverse(h.begin(), h.end());
            b.add_loop(h);
        }
    }
    b.end_face();

    b.begin_face();
    b.add_loop(top[0]);
    for (std::size_t li = 1; li < loops.size(); ++li) b.add_loop(top[li]);
    b.end_face();

    for (std::size_t li = 0; li < loops.size(); ++li) {
        const std::size_t n = bot[li].size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            b.begin_face();
            b.add_loop({bot[li][i], bot[li][j], top[li][j], top[li][i]});
            b.end_face();
        }
    }

    return b.build();
}

Body extrude_z(const Profile& p, const gcm::WorkingVolume& vol, f64 height) {
    return extrude(p, vol, ggeo::d3::Frame<F64>::world_xy(), height);
}

}
