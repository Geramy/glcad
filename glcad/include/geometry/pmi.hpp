#pragma once

#include <string>
#include <vector>

#include "geometry/solid_edit.hpp"
#include "geometry/surface.hpp"

namespace gtopo {

using gcm::u64;
using gcm::u8;

enum class PmiTarget : u8 { Face, Edge, Vertex };

struct PmiRef {
    PmiTarget target{PmiTarget::Face};
    u32       index{kInvalid};

    u64 fingerprint{0};

    bool operator==(const PmiRef& o) const noexcept {
        return target == o.target && index == o.index &&
               fingerprint == o.fingerprint;
    }
};

u64    fingerprint_of(const Body& b, PmiTarget t, u32 index, const Volume& v);
PmiRef make_ref(const Body& b, PmiTarget t, u32 index, const Volume& v);

u32 resolve(const PmiRef& r, const Body& b, const Volume& v);

enum class PmiKind : u8 {
    Distance,
    Diameter,
    Radius,
    Angle,
    Thickness,
};
const char* pmi_kind_name(PmiKind k) noexcept;

struct Tolerance {
    f64 plus{0.0}, minus{0.0};
    bool none() const noexcept { return plus == 0.0 && minus == 0.0; }
    f64  upper(f64 nominal) const noexcept { return nominal + plus; }
    f64  lower(f64 nominal) const noexcept { return nominal - minus; }
    bool contains(f64 nominal, f64 actual) const noexcept {
        return actual >= lower(nominal) - 1e-12 && actual <= upper(nominal) + 1e-12;
    }
};

struct PmiDimension {
    std::string label;
    PmiKind     kind{PmiKind::Distance};
    PmiRef      a, b;             // b unused for Diameter and Radius
    f64         nominal{0.0};
    Tolerance   tolerance;

    bool driving{false};

    bool moves_b{true};
};

struct PmiMeasurement {
    bool        ok{false};
    f64         value{0.0};
    bool        in_tolerance{true};
    std::string reason;
};

PmiMeasurement measure(const Body& b, const PmiDimension& d, const Volume& v);

struct DriveResult {
    Body        body;
    bool        ok{false};
    std::string reason;
    f64         before{0.0}, after{0.0};

    PmiDimension updated;
    f64 volume_before{0.0}, volume_after{0.0};
};

DriveResult drive(const Body& b, const PmiDimension& d, f64 value,
                  const Volume& v);

struct PmiConflict {
    u32         a{kInvalid}, b{kInvalid};
    std::string reason;
};

class PmiSet {
public:
    u32 add(PmiDimension d);
    bool remove(u32 i);
    PmiDimension*       at(u32 i);
    const PmiDimension* at(u32 i) const;
    std::size_t size() const noexcept { return dims_.size(); }
    const std::vector<PmiDimension>& all() const noexcept { return dims_; }

    std::vector<PmiConflict> conflicts(const Body& b, const Volume& v) const;

    struct Report {
        std::vector<PmiMeasurement> measurements;
        std::size_t out_of_tolerance{0};
        std::size_t broken{0};
        bool passes() const noexcept {
            return out_of_tolerance == 0 && broken == 0;
        }
    };
    Report inspect(const Body& b, const Volume& v) const;

    std::size_t reattach(const Body& b, const Volume& v);

private:
    std::vector<PmiDimension> dims_;
};

}
