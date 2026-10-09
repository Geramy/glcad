#pragma once

#include <memory>
#include <string>

#include "engine/transport.hpp"

namespace gio {

// No-op except on Windows, where sockets need WSAStartup first.
bool socket_startup();
void socket_shutdown();

class TcpTransport final : public Transport {
public:
    TcpTransport() = default;
    ~TcpTransport() override;

    const char* name() const noexcept override { return "tcp"; }
    bool supports(Family f) const noexcept override {
        return f == Family::IPv4 || f == Family::IPv6;
    }

    std::unique_ptr<Connection> connect(const Endpoint& e, Mode m,
                                        Status& s) override;
    bool listen(const Endpoint& e, Status& s) override;
    std::unique_ptr<Connection> accept(Status& s) override;

    // Seconds; zero blocks.
    void set_timeout(double seconds) noexcept { timeout_ = seconds; }
    double timeout() const noexcept { return timeout_; }

    bool listening() const noexcept { return listen_fd_ >= 0; }
    Endpoint bound() const noexcept { return bound_; }
    void close_listener();

private:
    int      listen_fd_{-1};
    Endpoint bound_{};
    double   timeout_{5.0};
};

bool register_tcp_transport();

bool ipv6_available();

}
