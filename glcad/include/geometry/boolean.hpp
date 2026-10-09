#pragma once

#include "math/bigint.hpp"
#include "geometry/body.hpp"

namespace gtopo {

enum class BoolOp : gcm::u8 { Union, Subtract, Intersect };

enum class BoolError : gcm::u8 {
    None,
    NonPlanarFace,
    OpenBody,
    EmptyResult,
    Degenerate,
};

struct BoolResult {
    Body      body;
    BoolError error{BoolError::None};
    bool      ok() const noexcept { return error == BoolError::None; }
    const char* message() const noexcept;
};

// Both operands must be closed, manifold, planar solids.
BoolResult boolean_op(const Body& a, const Body& b, BoolOp op,
                      const gcm::WorkingVolume& vol);

inline BoolResult unite(const Body& a, const Body& b, const gcm::WorkingVolume& v) {
    return boolean_op(a, b, BoolOp::Union, v);
}
inline BoolResult subtract(const Body& a, const Body& b, const gcm::WorkingVolume& v) {
    return boolean_op(a, b, BoolOp::Subtract, v);
}
inline BoolResult intersect(const Body& a, const Body& b, const gcm::WorkingVolume& v) {
    return boolean_op(a, b, BoolOp::Intersect, v);
}

bool all_faces_planar(const Body& b);

Body merge_coplanar_faces(const Body& in, const gcm::WorkingVolume& vol,
                         std::vector<FaceId>* face_remap = nullptr);

struct CurvedBoolSettings {
    f64  chord_tolerance{1e-4};

    bool refine_seams{true};

    bool preserve_surfaces{true};
};

struct CurvedBoolResult {
    Body        body;
    BoolError   error{BoolError::None};
    std::size_t faceted{0};
    std::size_t preserved{0};
    std::size_t refined{0};
    f64         chord_tolerance{0.0};

    bool ok() const noexcept { return error == BoolError::None; }
    const char* message() const noexcept;
};

CurvedBoolResult boolean_curved(const Body& a, const Body& b, BoolOp op,
                                const gcm::WorkingVolume& vol,
                                const CurvedBoolSettings& s = {});

bool has_curved_faces(const Body& b) noexcept;

// Signed volume x 6, exact.
gcm::SInt<256> volume_6x(const Body& b);
double volume(const Body& b, const gcm::WorkingVolume& vol);

}
