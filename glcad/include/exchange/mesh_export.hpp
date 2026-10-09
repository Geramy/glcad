#pragma once

#include <string>
#include <vector>

#include "geometry/body.hpp"

namespace gmesh {

using gcm::f64;
using gcm::u32;
using gcm::u8;

enum class Unit : u8 { Micrometre, Millimetre, Centimetre, Metre, Inch, Foot };

f64         metres_per(Unit u) noexcept;
const char* unit_name_3mf(Unit u) noexcept;
bool        unit_from_3mf(std::string_view name, Unit& u) noexcept;

struct Options {
    Unit        unit{Unit::Millimetre};
    std::string name{"GLCAD"};
};

struct Object {
    std::string      name;
    std::vector<f64> vertices;
    std::vector<u32> triangles;

    std::size_t vertex_count() const noexcept { return vertices.size() / 3; }
    std::size_t triangle_count() const noexcept { return triangles.size() / 3; }
};

struct Result {
    bool            ok{false};
    std::string     error;
    std::vector<u8> bytes;
    std::size_t     objects{0};
    std::size_t     triangles{0};
};

std::vector<Object> mesh(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& v,
                         const Options& o = {});

Result write_stl(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& v,
                 const Options& o = {});
Result write_3mf(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& v,
                 const Options& o = {});

struct ReadResult {
    bool                ok{false};
    std::string         error;
    Unit                unit{Unit::Millimetre};
    std::vector<Object> objects;
};

ReadResult read_stl(const std::vector<u8>& bytes);
ReadResult read_3mf(const std::vector<u8>& bytes);

}
