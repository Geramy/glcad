#include "engine/tcp.hpp"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socket_t = SOCKET;
#  define GIO_INVALID_SOCKET INVALID_SOCKET
#  define GIO_CLOSE closesocket
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using socket_t = int;
#  define GIO_INVALID_SOCKET (-1)
#  define GIO_CLOSE ::close
#endif

namespace gio {

namespace {

std::string last_error() {
#if defined(_WIN32)
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

bool wait_readable(socket_t fd, double seconds) {
    if (seconds <= 0.0) return true;      // blocking: let the call itself wait
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv;
    tv.tv_sec = static_cast<long>(seconds);
    tv.tv_usec = static_cast<int>((seconds - tv.tv_sec) * 1e6);
    return ::select(static_cast<int>(fd) + 1, &set, nullptr, nullptr, &tv) > 0;
}

class SocketReader final : public Reader {
public:
    SocketReader(socket_t fd, double timeout) : fd_(fd), timeout_(timeout) {}

    std::size_t read(u8* dst, std::size_t n) override {
        if (fd_ == GIO_INVALID_SOCKET || n == 0) return 0;
        if (!wait_readable(fd_, timeout_)) return 0;
        const auto got = ::recv(fd_, reinterpret_cast<char*>(dst),
                                static_cast<int>(n), 0);
        if (got <= 0) {
            eof_ = true;
            return 0;
        }
        pos_ += static_cast<u64>(got);
        // Short reads are normal: TCP has no message boundaries.
        return static_cast<std::size_t>(got);
    }

    bool eof() const override { return eof_; }
    u64  position() const override { return pos_; }

private:
    socket_t fd_{GIO_INVALID_SOCKET};
    double   timeout_{0.0};
    u64      pos_{0};
    bool     eof_{false};
};

class SocketWriter final : public Writer {
public:
    explicit SocketWriter(socket_t fd) : fd_(fd) {}

    std::size_t write(const u8* src, std::size_t n) override {
        if (fd_ == GIO_INVALID_SOCKET || n == 0) return 0;
#if defined(MSG_NOSIGNAL)
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const auto put = ::send(fd_, reinterpret_cast<const char*>(src),
                                static_cast<int>(n), flags);
        if (put <= 0) return 0;
        pos_ += static_cast<u64>(put);
        return static_cast<std::size_t>(put);
    }

    u64 position() const override { return pos_; }

private:
    socket_t fd_{GIO_INVALID_SOCKET};
    u64      pos_{0};
};

class TcpConnection final : public Connection {
public:
    TcpConnection(socket_t fd, Endpoint peer, double timeout)
        : fd_(fd), peer_(std::move(peer)), reader_(fd, timeout), writer_(fd) {}
    ~TcpConnection() override { close(); }

    Reader* input() override { return fd_ == GIO_INVALID_SOCKET ? nullptr : &reader_; }
    Writer* output() override { return fd_ == GIO_INVALID_SOCKET ? nullptr : &writer_; }
    bool    connected() const override { return fd_ != GIO_INVALID_SOCKET; }
    Endpoint peer() const override { return peer_; }

    Status close() override {
        if (fd_ == GIO_INVALID_SOCKET) return Status::good();
        // Half-close first so the peer sees end of stream, not a reset.
#if defined(_WIN32)
        ::shutdown(fd_, SD_SEND);
#else
        ::shutdown(fd_, SHUT_WR);
#endif
        GIO_CLOSE(fd_);
        fd_ = GIO_INVALID_SOCKET;
        return Status::good();
    }

private:
    socket_t     fd_;
    Endpoint     peer_;
    SocketReader reader_;
    SocketWriter writer_;
};

bool fill_address(const Endpoint& e, sockaddr_storage& out, socklen_t& len,
                  Status& s) {
    std::memset(&out, 0, sizeof(out));

    if (e.family == Family::IPv6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&out);
        a->sin6_family = AF_INET6;
        a->sin6_port = htons(e.port);
        len = sizeof(sockaddr_in6);
        if (e.is_wildcard()) {
            a->sin6_addr = in6addr_any;
            return true;
        }
        // Strip the zone index ("fe80::1%en0"): inet_pton rejects it.
        std::string host = e.host;
        const auto pct = host.find('%');
        if (pct != std::string::npos) host = host.substr(0, pct);
        if (::inet_pton(AF_INET6, host.c_str(), &a->sin6_addr) != 1) {
            s = Status::fail("'" + e.host + "' is not an IPv6 address");
            return false;
        }
        return true;
    }

    auto* a = reinterpret_cast<sockaddr_in*>(&out);
    a->sin_family = AF_INET;
    a->sin_port = htons(e.port);
    len = sizeof(sockaddr_in);

    if (e.is_wildcard()) {
        a->sin_addr.s_addr = INADDR_ANY;
        return true;
    }
    if (::inet_pton(AF_INET, e.host.c_str(), &a->sin_addr) != 1) {
        // Deliberately no DNS lookup: it would block the frame loop.
        s = Status::fail("'" + e.host + "' is not a dotted IPv4 address");
        return false;
    }
    return true;
}

int family_of(const Endpoint& e) noexcept {
    return e.family == Family::IPv6 ? AF_INET6 : AF_INET;
}

Endpoint peer_of(const sockaddr_storage& from) {
    char text[INET6_ADDRSTRLEN] = {};
    if (from.ss_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(&from);
        ::inet_ntop(AF_INET6, &a->sin6_addr, text, sizeof(text));
        return Endpoint::ipv6(text, ntohs(a->sin6_port));
    }
    const auto* a = reinterpret_cast<const sockaddr_in*>(&from);
    ::inet_ntop(AF_INET, &a->sin_addr, text, sizeof(text));
    return Endpoint::ipv4(text, ntohs(a->sin_port));
}

}

bool socket_startup() {
#if defined(_WIN32)
    static bool started = false;
    if (started) return true;
    WSADATA data;
    started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return started;
#else
    return true;
#endif
}

void socket_shutdown() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

TcpTransport::~TcpTransport() { close_listener(); }

void TcpTransport::close_listener() {
    if (listen_fd_ >= 0) {
        GIO_CLOSE(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
    }
}

std::unique_ptr<Connection> TcpTransport::connect(const Endpoint& e, Mode m,
                                                  Status& s) {
    (void)m;      // a socket is always both directions
    if (!supports(e.family)) {
        s = Status::fail("the tcp transport does not serve " +
                         std::string(family_name(e.family)));
        return nullptr;
    }
    socket_startup();

    sockaddr_storage addr;
    socklen_t addr_len = 0;
    if (!fill_address(e, addr, addr_len, s)) return nullptr;

    const socket_t fd = ::socket(family_of(e), SOCK_STREAM, 0);
    if (fd == GIO_INVALID_SOCKET) {
        s = Status::fail("could not create a socket: " + last_error());
        return nullptr;
    }

    // Nagle off: small framed messages must not wait 40 ms to coalesce.
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one),
                 sizeof(one));

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), addr_len) != 0) {
        s = Status::fail("could not connect to " + e.to_string() + ": " +
                         last_error());
        GIO_CLOSE(fd);
        return nullptr;
    }

    s = Status::good();
    return std::make_unique<TcpConnection>(fd, e, timeout_);
}

bool TcpTransport::listen(const Endpoint& e, Status& s) {
    if (!supports(e.family)) {
        s = Status::fail("the tcp transport does not serve " +
                         std::string(family_name(e.family)));
        return false;
    }
    socket_startup();
    close_listener();

    sockaddr_storage addr;
    socklen_t addr_len = 0;
    if (!fill_address(e, addr, addr_len, s)) return false;

    const socket_t fd = ::socket(family_of(e), SOCK_STREAM, 0);
    if (fd == GIO_INVALID_SOCKET) {
        s = Status::fail("could not create a socket: " + last_error());
        return false;
    }

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&one),
                 sizeof(one));

    if (e.family == Family::IPv6) {
        // Set explicitly: the IPV6_V6ONLY default differs between platforms.
        int on = 1;
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<char*>(&on), sizeof(on));
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), addr_len) != 0) {
        s = Status::fail("could not bind " + e.to_string() + ": " + last_error());
        GIO_CLOSE(fd);
        return false;
    }
    if (::listen(fd, 8) != 0) {
        s = Status::fail("could not listen on " + e.to_string() + ": " +
                         last_error());
        GIO_CLOSE(fd);
        return false;
    }

    listen_fd_ = static_cast<int>(fd);
    bound_ = e;

    if (e.port == 0) {
        sockaddr_storage actual;
        socklen_t len = sizeof(actual);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&actual), &len) == 0) {
            bound_.port = actual.ss_family == AF_INET6
                ? ntohs(reinterpret_cast<sockaddr_in6*>(&actual)->sin6_port)
                : ntohs(reinterpret_cast<sockaddr_in*>(&actual)->sin_port);
        }
    }

    s = Status::good();
    return true;
}

std::unique_ptr<Connection> TcpTransport::accept(Status& s) {
    if (listen_fd_ < 0) {
        s = Status::fail("not listening");
        return nullptr;
    }
    const socket_t lfd = static_cast<socket_t>(listen_fd_);
    if (!wait_readable(lfd, timeout_)) {
        s = Status::fail("no connection arrived within the timeout");
        return nullptr;
    }

    sockaddr_storage from;
    socklen_t len = sizeof(from);
    const socket_t fd = ::accept(lfd, reinterpret_cast<sockaddr*>(&from), &len);
    if (fd == GIO_INVALID_SOCKET) {
        s = Status::fail("accept failed: " + last_error());
        return nullptr;
    }

    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one),
                 sizeof(one));

    const Endpoint peer = peer_of(from);

    s = Status::good();
    return std::make_unique<TcpConnection>(fd, peer, timeout_);
}

bool register_tcp_transport() {
    auto& reg = TransportRegistry::instance();
    if (reg.find(Family::IPv4)) return false;
    reg.add(std::make_unique<TcpTransport>());
    return true;
}

bool ipv6_available() {
    socket_startup();
    const socket_t fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (fd == GIO_INVALID_SOCKET) return false;
    GIO_CLOSE(fd);
    return true;
}

}
