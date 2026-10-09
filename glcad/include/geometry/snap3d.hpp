#pragma once

#include <optional>
#include <vector>

#include "geometry/snap.hpp"
#include "geometry/body.hpp"

namespace gsnap {

using Point3 = gcm::Lattice3;

struct Result3 {
    bool   valid{false};
    Point3 point{};
    Kind   kind{None};
    gtopo::FaceId   face{gtopo::kInvalid};
    gtopo::VertexId vertex{gtopo::kInvalid};
    gtopo::HalfedgeId edge{gtopo::kInvalid};
    double distance{0.0};

    const char* name() const noexcept { return kind_name(kind); }
};

struct Ray3 {
    double ox{0.0}, oy{0.0}, oz{0.0};
    double dx{0.0}, dy{0.0}, dz{-1.0};
};

struct Settings3 {
    u32    running{Endpoint | Midpoint | Center | Nearest};
    bool   enabled{true};
    u32    forced{None};
    double aperture_world{0.05};

    bool wants(Kind k) const noexcept {
        if (forced != None) return (forced & k) != 0;
        return enabled && (running & k) != 0;
    }
};

class Engine3 {
public:
    Settings3 settings;

    void clear() { bodies_.clear(); }
    u32  add(const gtopo::Body* b);      // borrowed, not owned
    std::size_t size() const noexcept { return bodies_.size(); }

    Result3 snap(const gcm::WorkingVolume& v, const Ray3& ray) const;

    Result3 snap_point(const gcm::WorkingVolume& v, double wx, double wy,
                       double wz) const;

    struct Axis {
        bool   valid{false};
        double ox{0.0}, oy{0.0}, oz{0.0};
        double dx{0.0}, dy{0.0}, dz{0.0};
    };
    Axis face_normal_axis(const gcm::WorkingVolume& v, u32 body,
                          gtopo::FaceId f) const;
    Axis edge_axis(const gcm::WorkingVolume& v, u32 body,
                   gtopo::HalfedgeId he) const;

    static bool lock_to_axis(const Axis& a, double wx, double wy, double wz,
                             double& ox, double& oy, double& oz);

private:
    std::vector<const gtopo::Body*> bodies_;
};

enum class GlyphShape : u8 {
    Square,
    Triangle,
    Circle,
    CircleDot,
    Diamond,
    Cross,
    DoubleCross,
    Perpendicular,
    TangentCircle,
    Hourglass,
    Parallel,
    Ellipse,
    Node,
    Dashes,
};

GlyphShape glyph_for(Kind k) noexcept;
const char* glyph_name(GlyphShape g) noexcept;

std::vector<double> glyph_outline(GlyphShape g);

}
