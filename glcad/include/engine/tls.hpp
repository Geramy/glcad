#pragma once

#include <memory>
#include <string>

#include "engine/transport.hpp"

namespace gio {

bool tls_available() noexcept;
const char* tls_backend() noexcept;

struct TlsConfig {
    std::string certificate_file;
    std::string key_file;

    std::string ca_file;
    std::string ca_directory;

    std::string server_name;

    bool verify_peer{true};
    bool require_client_certificate{false};

    bool insecure_skip_verify{false};

    int minimum_version_major{1};
    int minimum_version_minor{2};
};

class TlsTransport final : public Transport {
public:
    explicit TlsTransport(TlsConfig config = {});
    ~TlsTransport() override;

    const char* name() const noexcept override { return "tls"; }
    bool supports(Family f) const noexcept override { return f == Family::Tls; }

    std::unique_ptr<Connection> connect(const Endpoint& e, Mode m,
                                        Status& s) override;
    bool listen(const Endpoint& e, Status& s) override;
    std::unique_ptr<Connection> accept(Status& s) override;

    void   set_timeout(double seconds) noexcept;
    double timeout() const noexcept;
    bool     listening() const noexcept;
    Endpoint bound() const noexcept;
    void     close_listener();

    TlsConfig&       config() noexcept { return config_; }
    const TlsConfig& config() const noexcept { return config_; }

    const std::string& last_tls_error() const noexcept { return tls_error_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TlsConfig             config_;
    std::string           tls_error_;
};

bool register_tls_transport(TlsConfig config = {});

}
