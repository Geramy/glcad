#pragma once

#include <optional>
#include <string>
#include <vector>

#include "geometry/snap.hpp"
#include "geometry/system.hpp"
#include "geometry/body.hpp"

namespace gtopo {

using gcm::f64;
using gcm::u32;
using gcm::u64;
using gcm::u8;

struct SketchPlane {
    f64 ox{0}, oy{0}, oz{0};
    f64 ux{1}, uy{0}, uz{0};
    f64 vx{0}, vy{1}, vz{0};
    f64 nx{0}, ny{0}, nz{1};

    void to_world(f64 x, f64 y, f64& wx, f64& wy, f64& wz) const noexcept;
    void to_plane(f64 wx, f64 wy, f64 wz, f64& x, f64& y) const noexcept;
    f64  distance(f64 wx, f64 wy, f64 wz) const noexcept;

    static SketchPlane world_xy() noexcept { return {}; }
    static SketchPlane from_normal(f64 px, f64 py, f64 pz,
                                   f64 nx, f64 ny, f64 nz) noexcept;
};

enum class SketchSupport : u8 { Datum, Face };

enum class SketchState : u8 {
    Ok,
    FaceGone,
    FaceNotPlanar,
    FaceMoved,
};
const char* sketch_state_name(SketchState s) noexcept;

struct Sketch {
    std::string   name;
    SketchSupport support{SketchSupport::Datum};
    SketchPlane   plane;

    u32 face{kInvalid};
    u64 body_revision{0};
    SketchPlane original;

    std::vector<gsnap::Shape> geometry;
    std::vector<gsnap::Shape> reference;

    bool is_linked() const noexcept { return support == SketchSupport::Face; }
};

SketchPlane plane_of_face(const Body& b, u32 face, const gcm::WorkingVolume& v);

void stable_axes(f64 nx, f64 ny, f64 nz, f64 u[3], f64 vv[3]) noexcept;

std::optional<Sketch> sketch_on_face(const Body& b, u32 face,
                                     const gcm::WorkingVolume& v,
                                     u64 body_revision = 0);

Sketch sketch_on_plane(const SketchPlane& p, std::string name = "Sketch");

SketchState check(const Sketch& s, const Body& b, const gcm::WorkingVolume& v);

bool reattach(Sketch& s, const Body& b, const gcm::WorkingVolume& v,
              u64 body_revision = 0);

std::vector<std::vector<std::array<f64, 3>>> lift(const Sketch& s,
                                                  const gcm::WorkingVolume& v,
                                                  f64 chord_tolerance = 1e-4);

struct SketchSystem {
    gsolve::System   system;
    struct Mapping {
        u32 shape{0};
        u32 a{gsolve::kInvalid}, b{gsolve::kInvalid};
        u32 entity{gsolve::kInvalid};
    };
    std::vector<Mapping> mapping;
};

SketchSystem to_system(const Sketch& s, const gcm::WorkingVolume& v);

void write_back(Sketch& s, const SketchSystem& sys, const gcm::WorkingVolume& v);

enum class DofBadge : u8 {
    Empty,
    UnderConstrained,
    FullyConstrained,
    OverConstrained,
};

struct BadgeState {
    DofBadge    badge{DofBadge::Empty};
    gsolve::Dof dof{};
    std::string text;
    std::vector<u32> conflicting;
};

BadgeState badge_of(const gsolve::System& s);
const char* badge_name(DofBadge b) noexcept;

BadgeState auto_constrain(Sketch& s, const gcm::WorkingVolume& v,
                          const gsolve::InferenceSettings& in = {});

}
