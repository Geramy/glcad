#include "test.hpp"

#include <cmath>

#include "geometry/d2.hpp"

using namespace ggeo;
using namespace ggeo::d2;
using gcm::DF64;
using gcm::F64;

TEST(d2_point_and_vector_are_distinct_types) {
    static_assert(!std::is_same_v<Point<F64>, Vec<F64>>);

    const Point<F64> a = Point<F64>::from(1.0, 2.0);
    const Point<F64> b = Point<F64>::from(4.0, 6.0);
    const Vec<F64>   d = b - a;
    CHECK(std::fabs(d.x.to_f64() - 3.0) < 1e-12);
    CHECK(std::fabs(d.y.to_f64() - 4.0) < 1e-12);
    CHECK(std::fabs(length(d).to_f64() - 5.0) < 1e-12);

    const Point<F64> c = a + d;
    CHECK(c == b);
}

TEST(d2_is_generic_over_the_scalar) {
    auto area2 = []<Scalar S>() {
        const auto a = Point<S>::from(0.0, 0.0);
        const auto b = Point<S>::from(4.0, 0.0);
        const auto c = Point<S>::from(0.0, 3.0);
        return cross(b - a, c - a).to_f64() * 0.5;
    };
    CHECK(std::fabs(area2.operator()<F64>() - 6.0) < 1e-12);
    CHECK(std::fabs(area2.operator()<DF64>() - 6.0) < 1e-10);
    CHECK(std::fabs(area2.operator()<gcm::F32>() - 6.0) < 1e-4);
}

TEST(d2_segment_basics) {
    Segment<F64> s{Point<F64>::from(0.0, 0.0), Point<F64>::from(10.0, 0.0)};
    CHECK(std::fabs(s.length().to_f64() - 10.0) < 1e-12);
    CHECK(std::fabs(s.midpoint().x.to_f64() - 5.0) < 1e-12);
    CHECK(std::fabs(s.at(F64::from(0.25)).x.to_f64() - 2.5) < 1e-12);
}

TEST(d2_aabb_expand_and_query) {
    auto box = AABB<F64>::empty();
    CHECK(!box.valid());
    box.expand(Point<F64>::from(1.0, 2.0));
    box.expand(Point<F64>::from(-3.0, 7.0));
    CHECK(box.valid());
    CHECK(std::fabs(box.lo.x.to_f64() + 3.0) < 1e-12);
    CHECK(std::fabs(box.hi.y.to_f64() - 7.0) < 1e-12);
    CHECK(box.contains(Point<F64>::from(0.0, 3.0)));
    CHECK(!box.contains(Point<F64>::from(5.0, 3.0)));

    auto other = AABB<F64>::empty();
    other.expand(Point<F64>::from(0.0, 0.0));
    other.expand(Point<F64>::from(2.0, 2.0));
    CHECK(box.intersects(other));
}

TEST(d2_transform_compose_and_apply) {
    const auto t = Transform<F64>::translation(Vec<F64>::from(10.0, 5.0));
    const auto r = Transform<F64>::rotation(3.14159265358979 / 2.0);
    const auto p = Point<F64>::from(1.0, 0.0);

    const auto rotated = r.apply(p);
    CHECK(std::fabs(rotated.x.to_f64()) < 1e-9);
    CHECK(std::fabs(rotated.y.to_f64() - 1.0) < 1e-9);

    const auto both = t.compose(r).apply(p);
    CHECK(std::fabs(both.x.to_f64() - 10.0) < 1e-9);
    CHECK(std::fabs(both.y.to_f64() - 6.0) < 1e-9);

    CHECK(std::fabs(Transform<F64>::identity().determinant().to_f64() - 1.0) < 1e-12);
}

TEST(d2_lattice_roundtrip) {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    const auto p = Point<F64>::from(123.456, -78.9);
    const auto l = to_lattice(v, p);
    const auto q = to_point<F64>(v, l);
    CHECK(std::fabs(q.x.to_f64() - 123.456) < v.half_cell() * 2);
    CHECK(std::fabs(q.y.to_f64() + 78.9) < v.half_cell() * 2);
}

TEST(d2_perp_is_orthogonal) {
    const auto v = Vec<F64>::from(3.0, 4.0);
    CHECK(std::fabs(dot(v, perp(v)).to_f64()) < 1e-12);
    CHECK(std::fabs(length(normalized(v)).to_f64() - 1.0) < 1e-12);
}
