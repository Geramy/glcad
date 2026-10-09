#pragma once

#include <string>
#include <vector>

#include "exchange/dwg.hpp"
#include "exchange/dxf.hpp"

namespace gdwg {

struct WriteOptions {
    gdxf::Units units{gdxf::Units::Millimetres};
    // R2000 only; per-entity transparency needs a newer version and is rejected.
    std::string version{"AC1015"};
    bool        write_blocks{true};
    bool        quantize_true_color{false};
};

std::vector<u8> write(const gdxf::Document& d, const gcm::WorkingVolume& v,
                      const WriteOptions& o = {}, std::string* error = nullptr);

bool write_file(const std::string& path, const gdxf::Document& d,
                const gcm::WorkingVolume& v, const WriteOptions& o = {},
                std::string* error = nullptr);

}
