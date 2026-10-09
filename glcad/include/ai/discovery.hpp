#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gai {

struct Discovered {
    std::string hostname;
    std::string url;         // rebuilt: datagram SOURCE address + beacon port
    std::string address;
    std::chrono::steady_clock::time_point last_seen;
};

class Discovery {
public:
    static constexpr int kPort = 13305;

    Discovery() = default;
    ~Discovery() { stop(); }
    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;

    bool start();
    void stop();
    bool running() const noexcept { return running_; }

    std::vector<Discovered> snapshot() const;

private:
    void pump();

    mutable std::mutex      mutex_;
    std::vector<Discovered> servers_;
    std::thread             thread_;
    bool                    running_{false};
    int                     socket_{-1};
};

}
