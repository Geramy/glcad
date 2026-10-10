#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gai::detail {

struct HttpReply {
    bool        reached{false};
    int         status{0};
    std::string body;
    std::string error;
};

using Receiver = std::function<bool(const char*, std::size_t)>;

HttpReply apple_request(const std::string& url, const std::string& method,
                        const std::vector<std::pair<std::string, std::string>>& headers,
                        const std::string& body, int connect_timeout_s, int idle_timeout_s,
                        const Receiver* receiver);

}
