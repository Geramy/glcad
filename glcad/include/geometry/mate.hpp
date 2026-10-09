#pragma once

#include <string>
#include <vector>

#include "geometry/solid_edit.hpp"

namespace gtopo {

enum class RefKind : gcm::u8 {
    Face,
    Edge,
    Vertex,
    Axis,
};

struct MateRef {
    u32     body{kInvalid};
    RefKind kind{RefKind::Face};
    u32     index{kInvalid};      // face, halfedge or vertex index

    bool valid() const noexcept { return body != kInvalid && index != kInvalid; }
};

enum class MateKind : gcm::u8 {
    Coincident,
    Flush,
    Parallel,
    Perpendicular,
    Distance,
    Angle,
    Concentric,
    Tangent,
    Collinear,
    PointOnFace,
    Fix,
};

const char* mate_kind_name(MateKind k) noexcept;

int mate_dof_removed(MateKind k) noexcept;

struct Mate {
    MateKind    kind{MateKind::Coincident};
    MateRef     a{}, b{};
    f64         value{0.0};        // metres for Distance, radians for Angle
    bool        flip{false};
    bool        suppressed{false};
    std::string name;
};

struct Placement {
    f64 m[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    f64 t[3]{0, 0, 0};

    static Placement identity() noexcept { return {}; }
    static Placement translation(f64 x, f64 y, f64 z) noexcept;
    static Placement rotation(f64 ax, f64 ay, f64 az, f64 radians) noexcept;

    void apply(f64 x, f64 y, f64 z, f64& ox, f64& oy, f64& oz) const noexcept;
    void apply_direction(f64 x, f64 y, f64 z, f64& ox, f64& oy,
                         f64& oz) const noexcept;
    Placement then(const Placement& next) const noexcept;
    bool is_identity(f64 tol = 1e-12) const noexcept;
};

class Assembly;

struct Instance {
    const Body*     body{nullptr};       // borrowed
    const Assembly* sub{nullptr};        // borrowed; set instead of `body`
    Placement       placement{};
    bool            grounded{false};
    std::string     name;

    bool is_subassembly() const noexcept { return sub != nullptr; }
    bool valid() const noexcept { return body != nullptr || sub != nullptr; }
};

enum class PatternKind : gcm::u8 { Linear, Circular, Mirror, Table };

struct Pattern {
    PatternKind kind{PatternKind::Linear};
    u32         seed{kInvalid};

    f64 dx1{0}, dy1{0}, dz1{0};
    int count1{1};
    f64 dx2{0}, dy2{0}, dz2{0};
    int count2{1};

    f64 ox{0}, oy{0}, oz{0};
    f64 ax{0}, ay{0}, az{1};
    f64 total_angle{6.28318530717958647692};
    bool rotate_instances{true};

    Plane mirror_plane{};

    std::vector<Placement> placements;

    std::vector<int> skipped;

    int total() const noexcept;
    bool is_skipped(int index) const noexcept;
};

std::vector<Placement> expand_pattern(const Pattern& p, const Placement& seed);

enum class MateStatus : gcm::u8 { Solved, AlreadySatisfied, DidNotConverge,
                                  OverConstrained, Conflicting, Empty };

struct MateResult {
    MateStatus  status{MateStatus::Empty};
    int         iterations{0};
    f64         residual{0.0};
    std::vector<u32> conflicting;
    const char* message() const noexcept;
    bool ok() const noexcept {
        return status == MateStatus::Solved || status == MateStatus::AlreadySatisfied;
    }
};

struct MateError {
    f64  linear{0.0};      // metres out of position
    f64  angular{0.0};     // radians out of alignment
    bool satisfied{false};
};

class Assembly {
public:
    u32 add(Instance i);
    u32 add_subassembly(const Assembly* sub, Placement at, std::string name = "");
    Instance*       instance(u32 id);
    const Instance* instance(u32 id) const;
    std::size_t     size() const noexcept { return instances_.size(); }
    const std::vector<Instance>& instances() const noexcept { return instances_; }

    u32  add_mate(Mate m);
    bool remove_mate(u32 index);
    std::size_t mate_count() const noexcept { return mates_.size(); }
    const std::vector<Mate>& mates() const noexcept { return mates_; }

    Plane   ref_plane(const MateRef& r, const Volume& v) const;
    bool    ref_axis(const MateRef& r, const Volume& v, f64& ox, f64& oy, f64& oz,
                     f64& dx, f64& dy, f64& dz) const;
    bool    ref_point(const MateRef& r, const Volume& v, f64& x, f64& y,
                      f64& z) const;

    MateError measure(const Mate& m, const Volume& v) const;

    MateResult solve(const Volume& v, int max_iterations = 200,
                     f64 tolerance = 1e-9);

    int degrees_of_freedom() const;

    u32  add_pattern(Pattern p);
    bool remove_pattern(u32 index);
    std::size_t pattern_count() const noexcept { return patterns_.size(); }
    const std::vector<Pattern>& patterns() const noexcept { return patterns_; }

    std::size_t expand_patterns();
    std::size_t patterned_instance_count() const;

    struct FlatPart {
        const Body* body{nullptr};
        Placement   placement{};
        std::string path;
        u32         depth{0};
    };
    std::vector<FlatPart> flatten() const;
    std::size_t part_count() const;
    u32         depth() const;

    struct Interference {
        u32  a{kInvalid}, b{kInvalid};
        bool touching{false};
        f64  volume{0.0};          // cubic metres of overlap
    };
    std::vector<Interference> interference(const Volume& v) const;

private:
    void flatten_into(std::vector<FlatPart>& out, const Placement& parent,
                      const std::string& prefix, u32 depth) const;

    std::vector<Instance> instances_;
    std::vector<Mate>     mates_;
    std::vector<Pattern>  patterns_;
};

namespace d2mate {

struct Edge {
    gcm::Lattice2 a{}, b{};
};

struct EdgeRef {
    u32 part{kInvalid};
    u32 shape{kInvalid};
    bool valid() const noexcept { return part != kInvalid && shape != kInvalid; }
};

struct Mate2 {
    MateKind kind{MateKind::Collinear};
    EdgeRef  a{}, b{};
    f64      value{0.0};
    bool     flip{false};
    bool     suppressed{false};
};

struct Placement2 {
    f64 angle{0.0};           // radians
    f64 tx{0.0}, ty{0.0};

    void apply(f64 x, f64 y, f64& ox, f64& oy) const noexcept;
    void apply_direction(f64 x, f64 y, f64& ox, f64& oy) const noexcept;
    bool is_identity(f64 tol = 1e-12) const noexcept;
};

struct Part {
    std::vector<Edge> shapes;
    Placement2 placement{};
    bool       grounded{false};
    std::string name;
};

class Assembly2 {
public:
    u32 add(Part p);
    Part*       part(u32 id);
    const Part* part(u32 id) const;
    std::size_t size() const noexcept { return parts_.size(); }

    u32 add_mate(Mate2 m);
    std::size_t mate_count() const noexcept { return mates_.size(); }
    const std::vector<Mate2>& mates() const noexcept { return mates_; }

    bool edge_of(const EdgeRef& r, const Volume& v, f64& x0, f64& y0, f64& x1,
                 f64& y1) const;

    MateResult solve(const Volume& v, int max_iterations = 200,
                     f64 tolerance = 1e-9);
    int degrees_of_freedom() const;

private:
    std::vector<Part>  parts_;
    std::vector<Mate2> mates_;
};

}
}
