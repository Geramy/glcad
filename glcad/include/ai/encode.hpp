#pragma once

#include <string>
#include <vector>

#include "math/config.hpp"

namespace gai {

inline std::string base64(const gcm::u8* data, std::size_t n) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        const unsigned b0 = data[i];
        const unsigned b1 = i + 1 < n ? data[i + 1] : 0;
        const unsigned b2 = i + 2 < n ? data[i + 2] : 0;
        out.push_back(k[b0 >> 2]);
        out.push_back(k[((b0 & 3) << 4) | (b1 >> 4)]);
        out.push_back(i + 1 < n ? k[((b1 & 15) << 2) | (b2 >> 6)] : '=');
        out.push_back(i + 2 < n ? k[b2 & 63] : '=');
    }
    return out;
}

inline std::string base64(const std::vector<gcm::u8>& v) {
    return base64(v.data(), v.size());
}

}
