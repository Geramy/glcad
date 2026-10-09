#include "ai/discovery.hpp"

#include <cstring>

#include "ai/types.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace gai {

namespace {

std::string resolve_url(const std::string& source_ip, const std::string& beacon_url) {
    std::string port;
    const auto scheme = beacon_url.find("://");
    const std::string rest = scheme == std::string::npos ? beacon_url : beacon_url.substr(scheme + 3);
    const auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
        for (std::size_t i = colon + 1; i < rest.size() && rest[i] >= '0' && rest[i] <= '9'; ++i)
            port.push_back(rest[i]);
    }
    return port.empty() ? "http://" + source_ip : "http://" + source_ip + ":" + port;
}

}

bool Discovery::start() {
    if (running_) return true;
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    const int s = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (s < 0) return false;
    // Shared bind: a Lemonade server on this machine already owns the port.
    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#if defined(SO_REUSEPORT)
    ::setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kPort);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#if defined(_WIN32)
        ::closesocket(s);
#else
        ::close(s);
#endif
        return false;
    }
    // The short receive timeout is the shutdown mechanism.
#if defined(_WIN32)
    DWORD tv = 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{1, 0};
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    socket_ = s;
    running_ = true;
    thread_ = std::thread([this] { pump(); });
    return true;
}

void Discovery::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (socket_ >= 0) {
#if defined(_WIN32)
        ::closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = -1;
    }
}

void Discovery::pump() {
    char buf[2048];
    while (running_) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        const auto n = ::recvfrom(socket_, buf, sizeof(buf) - 1, 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n <= 0) continue;
        Beacon b;
        if (!b.parse(std::string_view(buf, static_cast<std::size_t>(n)))) continue;
        if (b.service != "lemonade") continue;
        char ip[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        Discovered d;
        d.hostname = b.hostname.empty() ? "Unknown" : b.hostname;
        d.address = ip;
        d.url = resolve_url(d.address, b.url);
        d.last_seen = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        bool found = false;
        for (auto& e : servers_)
            if (e.hostname == d.hostname && e.url == d.url) {
                e.last_seen = d.last_seen;
                found = true;
                break;
            }
        if (!found) servers_.push_back(std::move(d));
    }
}

std::vector<Discovered> Discovery::snapshot() const {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Discovered> out;
    for (const auto& e : servers_)
        if (now - e.last_seen < std::chrono::seconds(15)) out.push_back(e);
    return out;
}

}
