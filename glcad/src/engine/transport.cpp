#include "engine/transport.hpp"

#include <map>

namespace gio {

const char* family_name(Family f) noexcept {
    switch (f) {
        case Family::Memory: return "memory";
        case Family::File:   return "file";
        case Family::IPv4:   return "ipv4";
        case Family::IPv6:   return "ipv6";
        case Family::Tls:    return "tls";
        case Family::Unix:   return "unix";
    }
    return "?";
}

Endpoint Endpoint::file(std::string path) {
    return {Family::File, std::move(path), 0};
}
Endpoint Endpoint::ipv4(std::string address, u16 port) {
    return {Family::IPv4, std::move(address), port};
}
Endpoint Endpoint::ipv6(std::string address, u16 port) {
    if (address.size() >= 2 && address.front() == '[' && address.back() == ']')
        address = address.substr(1, address.size() - 2);
    return {Family::IPv6, std::move(address), port};
}
Endpoint Endpoint::tls(std::string host, u16 port) {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    return {Family::Tls, std::move(host), port};
}
Endpoint Endpoint::memory(std::string name) {
    return {Family::Memory, std::move(name), 0};
}

bool Endpoint::is_wildcard() const noexcept {
    if (family == Family::IPv4) return host == "*" || host == "0.0.0.0";
    if (family == Family::IPv6) return host == "*" || host == "::";
    return false;
}

bool Endpoint::valid() const noexcept {
    if (host.empty()) return false;
    if (is_network()) return port != 0;
    return true;
}

std::string Endpoint::to_string() const {
    std::string s = family_name(family);
    s += "://";
    if (family == Family::IPv6 && host.find(':') != std::string::npos) {
        s += '[';
        s += host;
        s += ']';
    } else {
        s += host;
    }
    if (port) {
        s += ':';
        s += std::to_string(port);
    }
    return s;
}

bool Endpoint::parse(std::string_view url, Endpoint& out) {
    const auto sep = url.find("://");
    if (sep == std::string_view::npos) return false;

    const std::string_view scheme = url.substr(0, sep);
    std::string_view rest = url.substr(sep + 3);

    if (scheme == "memory") out.family = Family::Memory;
    else if (scheme == "file") out.family = Family::File;
    else if (scheme == "ipv4" || scheme == "tcp") out.family = Family::IPv4;
    else if (scheme == "ipv6") out.family = Family::IPv6;
    else if (scheme == "tls" || scheme == "tcps") out.family = Family::Tls;
    else if (scheme == "unix") out.family = Family::Unix;
    else return false;

    out.port = 0;
    // A path may contain colons; only network families have a port.
    if ((out.family == Family::IPv6 || out.family == Family::Tls) &&
        !rest.empty() && rest.front() == '[') {
        const auto close = rest.find(']');
        if (close == std::string_view::npos) return false;
        out.host = std::string(rest.substr(1, close - 1));
        rest = rest.substr(close + 1);
        if (rest.empty() || rest.front() != ':') return false;
        const std::string p(rest.substr(1));
        if (p.empty()) return false;
        for (char c : p)
            if (c < '0' || c > '9') return false;
        const long v = std::stol(p);
        if (v <= 0 || v > 65535) return false;
        out.port = static_cast<u16>(v);
        return !out.host.empty();
    }

    if (out.is_network()) {
        const auto colon = rest.rfind(':');
        if (colon == std::string_view::npos) return false;
        out.host = std::string(rest.substr(0, colon));
        if (out.family == Family::IPv6 &&
            out.host.find(':') != std::string::npos)
            return false;
        const std::string p(rest.substr(colon + 1));
        if (p.empty()) return false;
        for (char c : p)
            if (c < '0' || c > '9') return false;
        const long v = std::stol(p);
        if (v <= 0 || v > 65535) return false;
        out.port = static_cast<u16>(v);
    } else {
        out.host = std::string(rest);
    }
    return !out.host.empty();
}

namespace {

class FileConnection final : public Connection {
public:
    FileConnection(Endpoint e, Mode m) : peer_(std::move(e)), mode_(m) {}
    ~FileConnection() override { close(); }

    Status open() {
        if (mode_ == Mode::Read) {
            reader_ = std::make_unique<FileReader>();
            return reader_->open(peer_.host);
        }
        if (mode_ == Mode::Write) {
            writer_ = std::make_unique<FileWriter>();
            return writer_->open(peer_.host);
        }
        return Status::fail("a file connection is read or write, not both");
    }

    Reader* input() override { return reader_.get(); }
    Writer* output() override { return writer_.get(); }
    bool    connected() const override { return reader_ || writer_; }
    Endpoint peer() const override { return peer_; }

    Status close() override {
        Status s = Status::good();
        if (writer_) {
            s = writer_->close();
            writer_.reset();
        }
        reader_.reset();
        return s;
    }

private:
    Endpoint peer_;
    Mode     mode_;
    std::unique_ptr<FileReader> reader_;
    std::unique_ptr<FileWriter> writer_;
};

class MemoryConnection final : public Connection {
public:
    MemoryConnection(Endpoint e, Mode m) : peer_(std::move(e)), mode_(m) {}
    ~MemoryConnection() override { close(); }

    Status open() {
        auto& buf = MemoryTransport::buffer(peer_.host);
        if (mode_ == Mode::Read || mode_ == Mode::ReadWrite)
            reader_ = std::make_unique<MemoryReader>(buf);
        if (mode_ == Mode::Write || mode_ == Mode::ReadWrite)
            writer_ = std::make_unique<MemoryWriter>();
        return Status::good();
    }

    Reader* input() override { return reader_.get(); }
    Writer* output() override { return writer_.get(); }
    bool    connected() const override { return reader_ || writer_; }
    Endpoint peer() const override { return peer_; }

    Status close() override {
        if (writer_) MemoryTransport::buffer(peer_.host) = writer_->bytes();
        writer_.reset();
        reader_.reset();
        return Status::good();
    }

private:
    Endpoint peer_;
    Mode     mode_;
    std::unique_ptr<MemoryReader> reader_;
    std::unique_ptr<MemoryWriter> writer_;
};

std::map<std::string, std::vector<u8>>& memory_store() {
    static std::map<std::string, std::vector<u8>> m;
    return m;
}

}

std::unique_ptr<Connection> FileTransport::connect(const Endpoint& e, Mode m,
                                                   Status& s) {
    if (e.family != Family::File) {
        s = Status::fail("the file transport does not serve " +
                         std::string(family_name(e.family)));
        return nullptr;
    }
    auto c = std::make_unique<FileConnection>(e, m);
    s = c->open();
    return s.ok ? std::unique_ptr<Connection>(std::move(c)) : nullptr;
}

std::vector<u8>& MemoryTransport::buffer(const std::string& name) {
    return memory_store()[name];
}
void MemoryTransport::reset() { memory_store().clear(); }

std::unique_ptr<Connection> MemoryTransport::connect(const Endpoint& e, Mode m,
                                                     Status& s) {
    if (e.family != Family::Memory) {
        s = Status::fail("the memory transport does not serve " +
                         std::string(family_name(e.family)));
        return nullptr;
    }
    auto c = std::make_unique<MemoryConnection>(e, m);
    s = c->open();
    return s.ok ? std::unique_ptr<Connection>(std::move(c)) : nullptr;
}

TransportRegistry::TransportRegistry() {
    transports_.push_back(std::make_unique<FileTransport>());
    transports_.push_back(std::make_unique<MemoryTransport>());
}

TransportRegistry& TransportRegistry::instance() {
    static TransportRegistry r;
    return r;
}

void TransportRegistry::add(std::unique_ptr<Transport> t) {
    if (t) transports_.push_back(std::move(t));
}

Transport* TransportRegistry::find(Family f) const {
    // Last registered wins, so a host application can override a built-in.
    for (auto it = transports_.rbegin(); it != transports_.rend(); ++it)
        if ((*it)->supports(f)) return it->get();
    return nullptr;
}

std::vector<std::string> TransportRegistry::registered() const {
    std::vector<std::string> names;
    for (const auto& t : transports_) names.emplace_back(t->name());
    return names;
}

std::unique_ptr<Connection> TransportRegistry::open(const Endpoint& e, Mode m,
                                                    Status& s) {
    if (!e.valid()) {
        s = Status::fail("incomplete endpoint " + e.to_string());
        return nullptr;
    }
    Transport* t = find(e.family);
    if (!t) {
        s = Status::fail("no transport is registered for " +
                         std::string(family_name(e.family)));
        return nullptr;
    }
    return t->connect(e, m, s);
}

std::unique_ptr<Connection> TransportRegistry::open(std::string_view url, Mode m,
                                                    Status& s) {
    Endpoint e;
    if (!Endpoint::parse(url, e)) {
        s = Status::fail("cannot parse endpoint '" + std::string(url) + "'");
        return nullptr;
    }
    return open(e, m, s);
}

}
