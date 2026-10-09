#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "geometry/body.hpp"
#include "math/config.hpp"

namespace gxt {

using gcm::f64;
using gcm::u32;

struct ReadResult {
    std::vector<gtopo::Body> bodies;
    bool        ok{false};
    std::string error;

    std::string modeller;
    std::string schema;
    std::size_t nodes{0};
    std::size_t unknown_nodes{0};
    std::vector<std::string> skipped;
};

ReadResult read(std::string_view text, const gcm::WorkingVolume& v);
ReadResult read_file(const std::string& path, const gcm::WorkingVolume& v);

std::string dump(std::string_view text, std::string* error = nullptr);

struct WriteOptions {
    std::string application{"GLCAD"};
    bool write_exact_surfaces{true};
};

std::string write(const std::vector<gtopo::Body>& bodies, const gcm::WorkingVolume& v,
                  const WriteOptions& o = {});

}
