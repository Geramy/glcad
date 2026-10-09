#pragma once

#include <optional>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "math/config.hpp"

namespace gsolve {

using gcm::f64;
using gcm::i32;
using gcm::u32;
using gcm::u8;

inline constexpr u32 kInvalid = 0xFFFFFFFFu;

enum class EntityKind : u8 { Point, Line, Circle, Arc };

struct Entity {
    EntityKind kind{EntityKind::Point};
    u32        p0{kInvalid}, p1{kInvalid};
    u32        base{kInvalid};
    u32        count{0};
};

enum class Kind : u8 {
    Coincident,      // two points
    Horizontal,      // a line
    Vertical,        // a line
    Parallel,        // two lines
    Perpendicular,   // two lines
    Collinear,       // two lines
    Equal,           // two lines (length) or two circles (radius)
    Distance,        // two points, value
    PointOnLine,     // point, line
    PointOnCircle,   // point, circle
    Tangent,         // line and circle
    Concentric,      // two circles
    Radius,          // circle, value
    Diameter,        // circle, value
    Angle,           // two lines, value in radians
    Symmetric,       // two points about a line
    FixPoint,        // a point, pinned where it is
    HorizontalDist,  // two points, value
    VerticalDist,    // two points, value
};

const char* kind_name(Kind k) noexcept;

struct Constraint {
    Kind             kind{Kind::Coincident};
    std::vector<u32> entities;
    f64              value{0.0};
    std::string      label;
    bool             driving{true};
};

enum class Status : u8 {
    Solved,
    AlreadySatisfied,
    DidNotConverge,
    Overconstrained,
    Empty,
};

struct SolveResult {
    Status      status{Status::Empty};
    int         iterations{0};
    f64         residual{0.0};
    std::vector<u32> conflicting;
    const char* message() const noexcept;
    bool ok() const noexcept {
        return status == Status::Solved || status == Status::AlreadySatisfied;
    }
};

struct Dof {
    i32  parameters{0};
    i32  fixed{0};
    i32  constraints{0};
    i32  redundant{0};
    i32  remaining{0};

    bool fully_constrained() const noexcept { return remaining == 0; }
    bool over_constrained() const noexcept { return remaining < 0 || redundant > 0; }
    bool under_constrained() const noexcept { return remaining > 0; }
    const char* describe() const noexcept;
};

struct InferenceSettings {
    f64 point_tolerance{1e-4};      // metres, for coincidence
    f64 angle_tolerance_deg{2.0};
    f64 length_tolerance{1e-4};

    bool coincident{true};
    bool horizontal{true};
    bool vertical{true};
    bool parallel{true};
    bool perpendicular{true};
    bool equal{true};
    bool concentric{true};
    bool tangent{true};

    std::size_t max_suggestions{64};
};

class System;

std::vector<Constraint> infer_constraints(const System& s,
                                          const InferenceSettings& in = {});

std::size_t auto_constrain(System& s, const InferenceSettings& in = {});

class System {
public:
    u32 add_point(f64 x, f64 y);
    u32 add_line(u32 point_a, u32 point_b);
    u32 add_circle(u32 centre_point, f64 radius);
    u32 add_arc(u32 centre_point, f64 radius, f64 a0, f64 a1);

    void point_at(u32 entity, f64& x, f64& y) const;
    f64  radius_of(u32 entity) const;
    void set_point(u32 entity, f64 x, f64 y);

    std::size_t entity_count() const noexcept { return entities_.size(); }
    const std::vector<Entity>& entities() const noexcept { return entities_; }
    std::size_t parameter_count() const noexcept { return params_.size(); }

    void fix(u32 point_entity, bool fixed = true);
    bool is_fixed(u32 point_entity) const;

    u32  add(Constraint c);
    bool remove(u32 constraint_index);
    std::size_t constraint_count() const noexcept { return constraints_.size(); }
    const std::vector<Constraint>& constraints() const noexcept { return constraints_; }

    SolveResult solve(int max_iterations = 60, f64 tolerance = 1e-10);
    Dof         analyse() const;

    std::vector<f64> residuals() const;
    f64              total_error() const;

    struct Cluster {
        std::vector<u32> constraints;
        std::vector<u32> parameters;
        std::size_t size() const noexcept { return parameters.size(); }
    };
    std::vector<Cluster> decompose() const;

    SolveResult solve_clusters(int max_iterations = 60, f64 tolerance = 1e-10);

    std::vector<u32> touched_parameters(const Constraint& c) const;

    bool constraint_jacobian(const Constraint& c,
                             std::vector<std::vector<std::pair<u32, f64>>>& rows) const;

    bool would_conflict(const Constraint& c) const;

private:
    void residual_of(const Constraint& c, std::vector<f64>& out) const;
    std::vector<f64> all_residuals(const std::vector<f64>& params) const;
    i32 jacobian_rank(f64 eps = 1e-7) const;

    std::vector<f64>        params_;
    std::vector<bool>       fixed_;
    std::vector<Entity>     entities_;
    std::vector<Constraint> constraints_;
};

}
