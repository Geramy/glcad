#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "math/config.hpp"

namespace gcmd {

using gcm::f64;
using gcm::i64;

enum class VarType : gcm::u8 { Int, Real, Text };

struct SysVar {
    std::string name;
    VarType     type{VarType::Int};
    i64         ival{0};
    f64         rval{0.0};
    std::string sval;
    std::string description;
    bool        read_only{false};
};

class SysVars {
public:
    void add_standard();

    SysVar*       find(std::string_view name);
    const SysVar* find(std::string_view name) const;

    i64         get_int(std::string_view name, i64 fallback = 0) const;
    f64         get_real(std::string_view name, f64 fallback = 0.0) const;
    std::string get_text(std::string_view name, std::string_view fallback = "") const;

    bool set_value(std::string_view name, std::string_view value, std::string* error = nullptr);

    bool set_int(std::string_view name, i64 v);
    bool set_real(std::string_view name, f64 v);
    bool set_text(std::string_view name, std::string_view v);

    bool osnap_enabled(i64 bit) const;
    void set_osnap(i64 bit, bool on);

    std::size_t size() const noexcept { return vars_.size(); }
    const std::vector<SysVar>& all() const noexcept { return vars_; }

private:
    std::vector<SysVar> vars_;
    std::unordered_map<std::string, std::size_t> index_;
    void put(SysVar v);
};

enum OsnapBit : gcm::i64 {
    Osnap_Endpoint            = 1,
    Osnap_Midpoint            = 2,
    Osnap_Center              = 4,
    Osnap_Node                = 8,
    Osnap_Quadrant            = 16,
    Osnap_Intersection        = 32,
    Osnap_Insertion           = 64,
    Osnap_Perpendicular       = 128,
    Osnap_Tangent             = 256,
    Osnap_Nearest             = 512,
    Osnap_Off                 = 1024,
    Osnap_ApparentIntersection= 2048,
    Osnap_Extension           = 4096,
    Osnap_Parallel            = 8192,
};

}
