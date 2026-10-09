#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/stream.hpp"

namespace gio {

enum class Family : u8 {
    Memory,
    File,
    IPv4,
    IPv6,
    Unix,
    Tls,
};

const char* family_name(Family f) noexcept;

struct Endpoint {
    Family      family{Family::Memory};
    std::string host;
    u16         port{0};

    static Endpoint file(std::string path);
    static Endpoint ipv4(std::string address, u16 port);
    // Without brackets: "::1", not "[::1]".
    static Endpoint ipv6(std::string address, u16 port);
    static Endpoint memory(std::string name = "scratch");
    static Endpoint tls(std::string host, u16 port);

    std::string to_string() const;
    static bool parse(std::string_view url, Endpoint& out);
    bool        valid() const noexcept;

    bool is_network() const noexcept {
        return family == Family::IPv4 || family == Family::IPv6 ||
               family == Family::Tls;
    }
    bool is_encrypted() const noexcept { return family == Family::Tls; }
    bool is_wildcard() const noexcept;
};

class Connection {
public:
    virtual ~Connection() = default;

    virtual Reader* input() = 0;      // null when the channel is write-only
    virtual Writer* output() = 0;     // null when it is read-only
    virtual bool     connected() const = 0;
    virtual Status   close() = 0;
    virtual Endpoint peer() const = 0;
};

enum class Mode : u8 { Read, Write, ReadWrite };

class Transport {
public:
    virtual ~Transport() = default;

    virtual const char* name() const noexcept = 0;
    virtual bool        supports(Family f) const noexcept = 0;

    virtual std::unique_ptr<Connection> connect(const Endpoint& e, Mode m,
                                                Status& s) = 0;

    virtual bool listen(const Endpoint&, Status& s) {
        s = Status::fail("this transport cannot listen");
        return false;
    }
    virtual std::unique_ptr<Connection> accept(Status& s) {
        s = Status::fail("this transport cannot accept");
        return nullptr;
    }
};

class TransportRegistry {
public:
    static TransportRegistry& instance();

    void        add(std::unique_ptr<Transport> t);
    Transport*  find(Family f) const;
    std::vector<std::string> registered() const;

    std::unique_ptr<Connection> open(const Endpoint& e, Mode m, Status& s);
    std::unique_ptr<Connection> open(std::string_view url, Mode m, Status& s);

private:
    TransportRegistry();
    std::vector<std::unique_ptr<Transport>> transports_;
};

class FileTransport final : public Transport {
public:
    const char* name() const noexcept override { return "file"; }
    bool supports(Family f) const noexcept override { return f == Family::File; }
    std::unique_ptr<Connection> connect(const Endpoint& e, Mode m,
                                        Status& s) override;
};

class MemoryTransport final : public Transport {
public:
    const char* name() const noexcept override { return "memory"; }
    bool supports(Family f) const noexcept override { return f == Family::Memory; }
    std::unique_ptr<Connection> connect(const Endpoint& e, Mode m,
                                        Status& s) override;

    static std::vector<u8>& buffer(const std::string& name);
    static void             reset();
};

}
