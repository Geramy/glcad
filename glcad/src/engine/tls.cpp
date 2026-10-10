#include "engine/tls.hpp"

#include <cstring>
#include <cstdlib>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "engine/tcp.hpp"
#if defined(GLCAD_TLS_NETWORK)
#include "tls_network.hpp"
#endif

#if defined(GLCAD_HAVE_OPENSSL)
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace gio {

#if !defined(GLCAD_HAVE_OPENSSL) && defined(GLCAD_TLS_NETWORK)

bool tls_available() noexcept { return true; }
const char* tls_backend() noexcept { return "Network.framework"; }

struct TlsTransport::Impl {
    double timeout{0.0};
};

TlsTransport::TlsTransport(TlsConfig config)
    : impl_(std::make_unique<Impl>()), config_(std::move(config)) {}
TlsTransport::~TlsTransport() = default;

std::unique_ptr<Connection> TlsTransport::connect(const Endpoint& e, Mode,
                                                  Status& s) {
    tls_error_.clear();
    if (e.family != Family::Tls) {
        s = Status::fail("the tls transport only serves tls:// endpoints");
        return nullptr;
    }
    return detail::network_tls_connect(config_, e, impl_->timeout, s, tls_error_);
}
bool TlsTransport::listen(const Endpoint&, Status& s) {
    s = Status::fail("a TLS server is not available on this platform");
    return false;
}
std::unique_ptr<Connection> TlsTransport::accept(Status& s) {
    s = Status::fail("a TLS server is not available on this platform");
    return nullptr;
}
void   TlsTransport::set_timeout(double seconds) noexcept { impl_->timeout = seconds; }
double TlsTransport::timeout() const noexcept { return impl_->timeout; }
bool   TlsTransport::listening() const noexcept { return false; }
Endpoint TlsTransport::bound() const noexcept { return {}; }
void   TlsTransport::close_listener() {}

bool register_tls_transport(TlsConfig config) {
    auto& reg = TransportRegistry::instance();
    if (reg.find(Family::Tls)) return false;
    reg.add(std::make_unique<TlsTransport>(std::move(config)));
    return true;
}

#elif !defined(GLCAD_HAVE_OPENSSL)

bool tls_available() noexcept { return false; }
const char* tls_backend() noexcept { return "none"; }

struct TlsTransport::Impl {};

TlsTransport::TlsTransport(TlsConfig config) : config_(std::move(config)) {}
TlsTransport::~TlsTransport() = default;

std::unique_ptr<Connection> TlsTransport::connect(const Endpoint&, Mode,
                                                  Status& s) {
    s = Status::fail("this build has no TLS library");
    return nullptr;
}
bool TlsTransport::listen(const Endpoint&, Status& s) {
    s = Status::fail("this build has no TLS library");
    return false;
}
std::unique_ptr<Connection> TlsTransport::accept(Status& s) {
    s = Status::fail("this build has no TLS library");
    return nullptr;
}
void   TlsTransport::set_timeout(double) noexcept {}
double TlsTransport::timeout() const noexcept { return 0.0; }
bool   TlsTransport::listening() const noexcept { return false; }
Endpoint TlsTransport::bound() const noexcept { return {}; }
void   TlsTransport::close_listener() {}

bool register_tls_transport(TlsConfig) { return false; }

#else

bool tls_available() noexcept { return true; }
const char* tls_backend() noexcept { return OPENSSL_VERSION_TEXT; }

namespace {

std::string openssl_error() {
    std::string out;
    unsigned long code = 0;
    while ((code = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(code, buf, sizeof buf);
        if (!out.empty()) out += "; ";
        out += buf;
    }
    return out.empty() ? "no further detail" : out;
}

int socket_of(Connection& c) {
    (void)c;
    return -1;
}

struct StreamBio {
    Reader* in{nullptr};
    Writer* out{nullptr};
};

int bio_write(BIO* b, const char* data, int len) {
    auto* s = static_cast<StreamBio*>(BIO_get_data(b));
    if (!s || !s->out || len < 0) return -1;
    BIO_clear_retry_flags(b);
    const std::size_t n = s->out->write(reinterpret_cast<const u8*>(data),
                                        static_cast<std::size_t>(len));
    if (n == 0 && len > 0) {
        BIO_set_retry_write(b);
        return -1;
    }
    s->out->flush();
    return static_cast<int>(n);
}

int bio_read(BIO* b, char* data, int len) {
    auto* s = static_cast<StreamBio*>(BIO_get_data(b));
    if (!s || !s->in || len < 0) return -1;
    BIO_clear_retry_flags(b);
    const std::size_t n = s->in->read(reinterpret_cast<u8*>(data),
                                      static_cast<std::size_t>(len));
    if (n == 0) {
        // Zero is a clean close, not a retry: retrying a closed socket spins.
        return 0;
    }
    return static_cast<int>(n);
}

long bio_ctrl(BIO*, int cmd, long, void*) {
    return cmd == BIO_CTRL_FLUSH ? 1 : 0;
}

int bio_create(BIO* b) {
    BIO_set_init(b, 1);
    return 1;
}
int bio_destroy(BIO* b) {
    if (!b) return 0;
    delete static_cast<StreamBio*>(BIO_get_data(b));
    BIO_set_data(b, nullptr);
    return 1;
}

BIO_METHOD* stream_bio_method() {
    static BIO_METHOD* method = [] {
        BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK,
                                     "glcad stream");
        BIO_meth_set_write(m, bio_write);
        BIO_meth_set_read(m, bio_read);
        BIO_meth_set_ctrl(m, bio_ctrl);
        BIO_meth_set_create(m, bio_create);
        BIO_meth_set_destroy(m, bio_destroy);
        return m;
    }();
    return method;
}

class TlsConnection final : public Connection {
public:
    TlsConnection(std::unique_ptr<Connection> under, SSL* ssl, Endpoint peer)
        : under_(std::move(under)), ssl_(ssl), peer_(std::move(peer)) {
        reader_.owner = this;
        writer_.owner = this;
    }
    ~TlsConnection() override { close(); }

    Reader*  input() override { return &reader_; }
    Writer*  output() override { return &writer_; }
    bool     connected() const override { return ssl_ != nullptr; }
    Endpoint peer() const override { return peer_; }

    Status close() override {
        if (ssl_) {
            // Clean shutdown, so the peer can tell a close from a truncation.
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (under_) {
            const Status s = under_->close();
            under_.reset();
            return s;
        }
        return Status::good();
    }

    SSL* ssl() const noexcept { return ssl_; }

private:
    struct TlsReader final : Reader {
        TlsConnection* owner{nullptr};
        u64            at{0};
        std::size_t read(u8* dst, std::size_t n) override {
            if (!owner->ssl_ || n == 0) return 0;
            const int got = SSL_read(owner->ssl_, dst, static_cast<int>(n));
            if (got <= 0) return 0;
            at += static_cast<u64>(got);
            return static_cast<std::size_t>(got);
        }
        bool eof() const override { return owner->ssl_ == nullptr; }
        u64  position() const override { return at; }
    };
    struct TlsWriter final : Writer {
        TlsConnection* owner{nullptr};
        u64            at{0};
        std::size_t write(const u8* src, std::size_t n) override {
            if (!owner->ssl_ || n == 0) return 0;
            std::size_t sent = 0;
            while (sent < n) {
                const int put = SSL_write(owner->ssl_, src + sent,
                                          static_cast<int>(n - sent));
                if (put <= 0) break;
                sent += static_cast<std::size_t>(put);
            }
            at += sent;
            return sent;
        }
        bool flush() override {
            return owner->under_ && owner->under_->output()
                       ? owner->under_->output()->flush() : true;
        }
        u64 position() const override { return at; }
    };

    std::unique_ptr<Connection> under_;
    SSL*                        ssl_{nullptr};
    Endpoint                    peer_;
    TlsReader                   reader_;
    TlsWriter                   writer_;
};

}

struct TlsTransport::Impl {
    SSL_CTX*     client{nullptr};
    SSL_CTX*     server{nullptr};
    TcpTransport tcp;

    ~Impl() {
        if (client) SSL_CTX_free(client);
        if (server) SSL_CTX_free(server);
    }
};

TlsTransport::TlsTransport(TlsConfig config)
    : impl_(std::make_unique<Impl>()), config_(std::move(config)) {
    SSL_library_init();
}

TlsTransport::~TlsTransport() = default;

namespace {

bool configure(SSL_CTX* ctx, const TlsConfig& c, bool server,
               std::string& error) {
    const int minimum = (c.minimum_version_major > 1 ||
                         (c.minimum_version_major == 1 && c.minimum_version_minor >= 3))
                            ? TLS1_3_VERSION : TLS1_2_VERSION;
    if (!SSL_CTX_set_min_proto_version(ctx, minimum)) {
        error = "could not set the minimum TLS version: " + openssl_error();
        return false;
    }

    if (!c.certificate_file.empty()) {
        if (SSL_CTX_use_certificate_chain_file(ctx, c.certificate_file.c_str()) != 1) {
            error = "could not load the certificate: " + openssl_error();
            return false;
        }
    }
    if (!c.key_file.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ctx, c.key_file.c_str(),
                                        SSL_FILETYPE_PEM) != 1) {
            error = "could not load the private key: " + openssl_error();
            return false;
        }
        if (SSL_CTX_check_private_key(ctx) != 1) {
            error = "the private key does not match the certificate";
            return false;
        }
    }

    if (!c.ca_file.empty() || !c.ca_directory.empty()) {
        if (SSL_CTX_load_verify_locations(
                ctx, c.ca_file.empty() ? nullptr : c.ca_file.c_str(),
                c.ca_directory.empty() ? nullptr : c.ca_directory.c_str()) != 1) {
            error = "could not load the trust store: " + openssl_error();
            return false;
        }
    } else if (!server) {
#if defined(__APPLE__) && TARGET_OS_OSX
        // A bundled OpenSSL's built-in CA path may be Homebrew's; use macOS's store.
        if (!std::getenv("SSL_CERT_FILE") && !std::getenv("SSL_CERT_DIR")) {
            if (SSL_CTX_load_verify_locations(ctx, "/etc/ssl/cert.pem", nullptr) != 1) {
                error = "could not load the macOS trust store: " + openssl_error();
                return false;
            }
        } else
#endif
        SSL_CTX_set_default_verify_paths(ctx);
    }

    if (server) {
        SSL_CTX_set_verify(ctx,
                           c.require_client_certificate
                               ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
                               : SSL_VERIFY_NONE,
                           nullptr);
    } else {
        const bool verify = c.verify_peer && !c.insecure_skip_verify;
        SSL_CTX_set_verify(ctx, verify ? SSL_VERIFY_PEER : SSL_VERIFY_NONE,
                           nullptr);
    }
    return true;
}

BIO* bio_over(Connection& c) {
    BIO* b = BIO_new(stream_bio_method());
    if (!b) return nullptr;
    auto* s = new StreamBio;
    s->in = c.input();
    s->out = c.output();
    BIO_set_data(b, s);
    return b;
}

}

std::unique_ptr<Connection> TlsTransport::connect(const Endpoint& e, Mode m,
                                                  Status& s) {
    tls_error_.clear();
    if (e.family != Family::Tls) {
        s = Status::fail("the tls transport only serves tls:// endpoints");
        return nullptr;
    }
#if defined(GLCAD_TLS_NETWORK)
    return detail::network_tls_connect(config_, e, impl_->tcp.timeout(), s, tls_error_);
#endif

    if (!impl_->client) {
        impl_->client = SSL_CTX_new(TLS_client_method());
        if (!impl_->client) {
            s = Status::fail("could not create a TLS context: " + openssl_error());
            return nullptr;
        }
        std::string error;
        if (!configure(impl_->client, config_, false, error)) {
            tls_error_ = error;
            s = Status::fail(error);
            return nullptr;
        }
    }

    Endpoint under = e;
    under.family = e.host.find(':') != std::string::npos ? Family::IPv6
                                                         : Family::IPv4;
    auto conn = impl_->tcp.connect(under, m, s);
    if (!conn) return nullptr;

    SSL* ssl = SSL_new(impl_->client);
    if (!ssl) {
        s = Status::fail("could not create a TLS session: " + openssl_error());
        return nullptr;
    }

    const std::string name = config_.server_name.empty() ? e.host
                                                         : config_.server_name;
    SSL_set_tlsext_host_name(ssl, name.c_str());
    if (config_.verify_peer && !config_.insecure_skip_verify) {
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (SSL_set1_host(ssl, name.c_str()) != 1) {
            SSL_free(ssl);
            s = Status::fail("could not set the expected host name");
            return nullptr;
        }
    }

    BIO* bio = bio_over(*conn);
    if (!bio) {
        SSL_free(ssl);
        s = Status::fail("could not wrap the connection");
        return nullptr;
    }
    SSL_set_bio(ssl, bio, bio);
    SSL_set_connect_state(ssl);

    if (SSL_do_handshake(ssl) != 1) {
        tls_error_ = openssl_error();
        const long verify = SSL_get_verify_result(ssl);
        if (verify != X509_V_OK) {
            tls_error_ = std::string("the certificate was rejected: ") +
                         X509_verify_cert_error_string(verify);
        }
        SSL_free(ssl);
        s = Status::fail("the TLS handshake failed: " + tls_error_);
        return nullptr;
    }

    s = Status::good();
    return std::make_unique<TlsConnection>(std::move(conn), ssl, e);
}

bool TlsTransport::listen(const Endpoint& e, Status& s) {
    tls_error_.clear();
    if (e.family != Family::Tls) {
        s = Status::fail("the tls transport only serves tls:// endpoints");
        return false;
    }
    if (config_.certificate_file.empty() || config_.key_file.empty()) {
        s = Status::fail("a TLS server needs a certificate and a private key");
        return false;
    }

    if (!impl_->server) {
        impl_->server = SSL_CTX_new(TLS_server_method());
        if (!impl_->server) {
            s = Status::fail("could not create a TLS context: " + openssl_error());
            return false;
        }
        std::string error;
        if (!configure(impl_->server, config_, true, error)) {
            tls_error_ = error;
            s = Status::fail(error);
            return false;
        }
    }

    Endpoint under = e;
    under.family = e.host.find(':') != std::string::npos ? Family::IPv6
                                                         : Family::IPv4;
    if (!impl_->tcp.listen(under, s)) return false;
    return true;
}

std::unique_ptr<Connection> TlsTransport::accept(Status& s) {
    tls_error_.clear();
    auto conn = impl_->tcp.accept(s);
    if (!conn) return nullptr;

    SSL* ssl = SSL_new(impl_->server);
    if (!ssl) {
        s = Status::fail("could not create a TLS session: " + openssl_error());
        return nullptr;
    }
    BIO* bio = bio_over(*conn);
    if (!bio) {
        SSL_free(ssl);
        s = Status::fail("could not wrap the connection");
        return nullptr;
    }
    SSL_set_bio(ssl, bio, bio);
    SSL_set_accept_state(ssl);

    if (SSL_do_handshake(ssl) != 1) {
        tls_error_ = openssl_error();
        SSL_free(ssl);
        s = Status::fail("the TLS handshake failed: " + tls_error_);
        return nullptr;
    }

    Endpoint peer = conn->peer();
    peer.family = Family::Tls;
    s = Status::good();
    return std::make_unique<TlsConnection>(std::move(conn), ssl, peer);
}

void   TlsTransport::set_timeout(double seconds) noexcept {
    impl_->tcp.set_timeout(seconds);
}
double TlsTransport::timeout() const noexcept { return impl_->tcp.timeout(); }
bool   TlsTransport::listening() const noexcept { return impl_->tcp.listening(); }
Endpoint TlsTransport::bound() const noexcept {
    Endpoint e = impl_->tcp.bound();
    e.family = Family::Tls;
    return e;
}
void TlsTransport::close_listener() { impl_->tcp.close_listener(); }

bool register_tls_transport(TlsConfig config) {
    auto& reg = TransportRegistry::instance();
    if (reg.find(Family::Tls)) return false;
    reg.add(std::make_unique<TlsTransport>(std::move(config)));
    return true;
}

#endif

}
