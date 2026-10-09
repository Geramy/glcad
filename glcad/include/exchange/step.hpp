#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "geometry/body.hpp"
#include "geometry/surface.hpp"

namespace gstep {

using gcm::f64;
using gcm::u32;
using gcm::u64;
using gcm::u8;

struct Instance {
    u64                      id{0};
    std::string              type;
    std::vector<std::string> args;
};

struct ParseResult {
    std::vector<Instance>               instances;
    std::unordered_map<u64, std::size_t> index;
    std::string                          schema;
    std::string                          name;
    bool                                 ok{false};
    std::string                          error;
    std::size_t                          line{0};

    const Instance* find(u64 id) const;
};

ParseResult parse(std::string_view text);

std::vector<std::string> split_arguments(std::string_view s);

// Apostrophes are doubled; non-ASCII goes through the \X2\ escape.
std::string encode_string(std::string_view s);
std::string decode_string(std::string_view s);

enum class LengthUnit : u8 { Metre, Millimetre, Centimetre, Inch, Foot };
const char* length_unit_name(LengthUnit u) noexcept;
f64         metres_per(LengthUnit u) noexcept;

struct WriteOptions {
    LengthUnit  unit{LengthUnit::Millimetre};
    std::string product_name{"Part"};
    std::string author{"GLCAD"};
    std::string organisation;
    std::string timestamp{"1980-01-01T00:00:00"};

    bool exact_surfaces{true};
};

std::string write(const gtopo::Body& b, const gcm::WorkingVolume& v,
                  const WriteOptions& o = {});

struct ReadResult {
    std::vector<gtopo::Body> bodies;
    LengthUnit               unit{LengthUnit::Millimetre};
    bool                     unit_stated{false};
    std::string              product_name;
    std::string              schema;

    std::vector<std::string> skipped;
    std::size_t              unknown_entities{0};

    bool        ok{false};
    std::string error;
};

ReadResult read(std::string_view text, const gcm::WorkingVolume& v);

}
