#include "test.hpp"

#include <cmath>

#include "geometry/d2.hpp"
#include "geometry/d3.hpp"

using namespace ggeo;
using namespace ggeo::d3;
using gcm::DF64;
using gcm::F64;

TEST(d3_cross_and_dot) {
    const auto x = Vec<F64>::unit_x();
    const auto y = Vec<F64>::unit_y();
    const auto z = cross(x, y);
    CHECK(std::fabs(z.z.to_f64() - 1.0) < 1e-12);
    CHECK(std::fabs(dot(x, y).to_f64()) < 1e-12);
}

TEST(d3_triangle_area_and_normal) {
    Triangle<F64> t{Point<F64>::from(0, 0, 0), Point<F64>::from(4, 0, 0),
                    Point<F64>::from(0, 3, 0)};
    CHECK(std::fabs(t.area().to_f64() - 6.0) < 1e-12);
    CHECK(std::fabs(t.normal().z.to_f64() - 1.0) < 1e-12);
}

TEST(d3_plane_signed_distance) {
    const auto p = Plane<F64>::xy();
    CHECK(std::fabs(p.signed_distance(Point<F64>::from(0, 0, 5)).to_f64() - 5.0) < 1e-12);
    CHECK(p.signed_distance(Point<F64>::from(0, 0, -2)).to_f64() < 0.0);
}

TEST(d3_aabb_centre_and_size) {
    auto b = AABB<F64>::empty();
    b.expand(Point<F64>::from(-1, -2, -3));
    b.expand(Point<F64>::from(3, 4, 5));
    CHECK(std::fabs(b.centre().x.to_f64() - 1.0) < 1e-12);
    CHECK(std::fabs(b.size().z.to_f64() - 8.0) < 1e-12);
    CHECK(b.contains(Point<F64>::from(0, 0, 0)));
}

TEST(d3_transform_translation_and_compose) {
    const auto t = Transform<F64>::translation(Vec<F64>::from(1, 2, 3));
    const auto s = Transform<F64>::scaling(F64::from(2.0));
    const auto p = Point<F64>::from(1, 1, 1);

    const auto ts = t.compose(s).apply(p);
    CHECK(std::fabs(ts.x.to_f64() - 3.0) < 1e-12);
    CHECK(std::fabs(ts.z.to_f64() - 5.0) < 1e-12);
}

TEST(d3_frame_bridges_2d_into_3d) {
    auto f = Frame<F64>::world_xy();
    f.origin = Point<F64>::from(10, 20, 30);

    const auto w = f.to_world(F64::from(3.0), F64::from(4.0));
    CHECK(std::fabs(w.x.to_f64() - 13.0) < 1e-12);
    CHECK(std::fabs(w.y.to_f64() - 24.0) < 1e-12);
    CHECK(std::fabs(w.z.to_f64() - 30.0) < 1e-12);

    F64 px, py;
    f.to_plane(w, px, py);
    CHECK(std::fabs(px.to_f64() - 3.0) < 1e-12);
    CHECK(std::fabs(py.to_f64() - 4.0) < 1e-12);
}

TEST(d3_generic_over_scalar) {
    auto vol = []<Scalar S>() {
        const auto a = Vec<S>::from(1, 0, 0);
        const auto b = Vec<S>::from(0, 2, 0);
        const auto c = Vec<S>::from(0, 0, 3);
        return dot(cross(a, b), c).to_f64();
    };
    CHECK(std::fabs(vol.operator()<F64>() - 6.0) < 1e-12);
    CHECK(std::fabs(vol.operator()<DF64>() - 6.0) < 1e-10);
}

TEST(d3_2d_and_3d_types_do_not_mix) {
    static_assert(!std::is_same_v<ggeo::d3::Point<F64>, ggeo::d2::Point<F64>>);
    static_assert(sizeof(ggeo::d3::Point<F64>) > sizeof(ggeo::d2::Point<F64>));
}
