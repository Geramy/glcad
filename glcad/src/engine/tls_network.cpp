#include "tls_network.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Network/Network.h>
#include <Security/Security.h>
#include <dispatch/dispatch.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace gio::detail {

namespace {

std::string from_cf(CFStringRef s) {
    if (!s) return {};
    const CFIndex len = CFStringGetLength(s);
    const CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(max), '\0');
    if (!CFStringGetCString(s, out.data(), max, kCFStringEncodingUTF8)) return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::string describe(nw_error_t err) {
    if (!err) return "unknown error";
    std::string out;
    if (CFErrorRef cf = nw_error_copy_cf_error(err)) {
        if (CFStringRef d = CFErrorCopyDescription(cf)) {
            out = from_cf(d);
            CFRelease(d);
        }
        CFRelease(cf);
    }
    if (out.empty()) out = "error " + std::to_string(nw_error_get_error_code(err));
    return out;
}

bool is_trust_failure(nw_error_t err) {
    if (!err || nw_error_get_error_domain(err) != nw_error_domain_tls) return false;
    const int code = nw_error_get_error_code(err);
    return code == errSSLBadCert || code == errSSLXCertChainInvalid ||
           code == errSSLUnknownRootCert || code == errSSLNoRootCert ||
           code == errSSLCertExpired || code == errSSLCertNotYetValid ||
           code == errSSLHostNameMismatch || code == errSSLPeerBadCert ||
           code == errSSLPeerCertUnknown || code == errSSLPeerUnknownCA;
}

std::vector<unsigned char> base64(const std::string& in) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> out;
    unsigned value = 0;
    int bits = -8;
    for (const char ch : in) {
        const auto at = chars.find(ch);
        if (at == std::string::npos) continue;
        value = (value << 6) + static_cast<unsigned>(at);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<unsigned char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

CFArrayRef load_anchors(const std::string& path) {
    CFMutableArrayRef anchors = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
    std::ifstream f(path, std::ios::binary);
    if (!f) return anchors;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    const std::string begin = "-----BEGIN CERTIFICATE-----";
    const std::string end = "-----END CERTIFICATE-----";
    std::size_t at = 0;
    while ((at = text.find(begin, at)) != std::string::npos) {
        const std::size_t body = at + begin.size();
        const std::size_t stop = text.find(end, body);
        if (stop == std::string::npos) break;
        const auto der = base64(text.substr(body, stop - body));
        if (CFDataRef data = CFDataCreate(nullptr, der.data(), static_cast<CFIndex>(der.size()))) {
            if (SecCertificateRef cert = SecCertificateCreateWithData(nullptr, data)) {
                CFArrayAppendValue(anchors, cert);
                CFRelease(cert);
            }
            CFRelease(data);
        }
        at = stop + end.size();
    }
    return anchors;
}

template <class Pred>
bool wait_for(std::condition_variable& cv, std::unique_lock<std::mutex>& lk,
              double seconds, Pred pred) {
    if (seconds <= 0.0) {
        cv.wait(lk, pred);
        return true;
    }
    return cv.wait_for(lk, std::chrono::duration<double>(seconds), pred);
}

struct State {
    std::mutex              m;
    std::condition_variable cv;
    std::string             buf;
    bool                    pending{false};
    bool                    eof{false};
    bool                    cancelled{false};
    std::string             error;
};

class NetworkConnection final : public Connection {
public:
    NetworkConnection(nw_connection_t conn, dispatch_queue_t queue,
                      std::shared_ptr<State> st, Endpoint peer, double timeout)
        : conn_(conn), queue_(queue), st_(std::move(st)), peer_(std::move(peer)),
          timeout_(timeout) {
        reader_.owner = this;
        writer_.owner = this;
    }
    ~NetworkConnection() override {
        close();
        if (queue_) dispatch_release(queue_);
    }

    Reader*  input() override { return &reader_; }
    Writer*  output() override { return &writer_; }
    bool     connected() const override { return conn_ != nullptr; }
    Endpoint peer() const override { return peer_; }

    Status close() override {
        if (!conn_) return Status::good();
        nw_connection_t c = conn_;
        conn_ = nullptr;
        nw_connection_send(c, nullptr, NW_CONNECTION_FINAL_MESSAGE_CONTEXT, true,
                           ^(nw_error_t) {});
        nw_connection_cancel(c);
        {
            std::unique_lock lk(st_->m);
            wait_for(st_->cv, lk, 2.0, [&] { return st_->cancelled; });
        }
        nw_release(c);
        return Status::good();
    }

private:
    struct NwReader final : Reader {
        NetworkConnection* owner{nullptr};
        u64                at{0};
        std::size_t read(u8* dst, std::size_t n) override {
            if (!owner->conn_ || n == 0) return 0;
            const auto st = owner->st_;
            std::unique_lock lk(st->m);
            if (st->buf.empty() && !st->eof && !st->pending) {
                st->pending = true;
                nw_connection_receive(
                    owner->conn_, 1, static_cast<uint32_t>(std::min<std::size_t>(n, 1u << 20)),
                    ^(dispatch_data_t content, nw_content_context_t, bool complete,
                      nw_error_t error) {
                        std::lock_guard g(st->m);
                        if (content) {
                            dispatch_data_apply(content, ^bool(dispatch_data_t, size_t,
                                                               const void* p, size_t len) {
                                st->buf.append(static_cast<const char*>(p), len);
                                return true;
                            });
                        }
                        if (error) {
                            st->error = describe(error);
                            st->eof = true;
                        } else if (complete) {
                            st->eof = true;
                        }
                        st->pending = false;
                        st->cv.notify_all();
                    });
            }
            wait_for(st->cv, lk, owner->timeout_, [&] { return !st->buf.empty() || st->eof; });
            if (st->buf.empty()) return 0;
            const std::size_t take = std::min(n, st->buf.size());
            std::memcpy(dst, st->buf.data(), take);
            st->buf.erase(0, take);
            at += take;
            return take;
        }
        bool eof() const override {
            std::lock_guard g(owner->st_->m);
            return owner->st_->eof && owner->st_->buf.empty();
        }
        u64 position() const override { return at; }
    };
    struct NwWriter final : Writer {
        NetworkConnection* owner{nullptr};
        u64                at{0};
        std::size_t write(const u8* src, std::size_t n) override {
            if (!owner->conn_ || n == 0) return 0;
            dispatch_data_t data = dispatch_data_create(src, n, owner->queue_,
                                                        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            auto done = std::make_shared<std::pair<bool, bool>>(false, false);
            const auto st = owner->st_;
            nw_connection_send(owner->conn_, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT,
                               false, ^(nw_error_t error) {
                                   std::lock_guard g(st->m);
                                   done->first = true;
                                   done->second = error == nullptr;
                                   if (error) st->error = describe(error);
                                   st->cv.notify_all();
                               });
            dispatch_release(data);
            std::unique_lock lk(st->m);
            if (!wait_for(st->cv, lk, owner->timeout_, [&] { return done->first; }) ||
                !done->second)
                return 0;
            at += n;
            return n;
        }
        u64 position() const override { return at; }
    };

    nw_connection_t        conn_{nullptr};
    dispatch_queue_t       queue_{nullptr};
    std::shared_ptr<State> st_;
    Endpoint               peer_;
    double                 timeout_{0.0};
    NwReader               reader_;
    NwWriter               writer_;
};

}

std::unique_ptr<Connection> network_tls_connect(const TlsConfig& config,
                                                const Endpoint& e,
                                                double timeout_seconds,
                                                Status& s,
                                                std::string& tls_error) {
    tls_error.clear();
    const std::string port = std::to_string(e.port);
    const std::string name = config.server_name.empty() ? e.host : config.server_name;
    const bool skip = config.insecure_skip_verify || !config.verify_peer;
    const bool pinned = !config.ca_file.empty();
    CFArrayRef anchors = pinned ? load_anchors(config.ca_file) : nullptr;
    const bool tls13 = config.minimum_version_major > 1 ||
                       (config.minimum_version_major == 1 && config.minimum_version_minor >= 3);

    dispatch_queue_t queue = dispatch_queue_create("glcad.tls", DISPATCH_QUEUE_SERIAL);

    nw_parameters_t params = nw_parameters_create_secure_tcp(
        ^(nw_protocol_options_t tls) {
            sec_protocol_options_t so = nw_tls_copy_sec_protocol_options(tls);
            sec_protocol_options_set_tls_server_name(so, name.c_str());
            sec_protocol_options_set_min_tls_protocol_version(
                so, tls13 ? tls_protocol_version_TLSv13 : tls_protocol_version_TLSv12);
            if (skip) {
                sec_protocol_options_set_verify_block(
                    so, ^(sec_protocol_metadata_t, sec_trust_t, sec_protocol_verify_complete_t done) {
                        done(true);
                    },
                    queue);
            } else if (pinned) {
                sec_protocol_options_set_verify_block(
                    so, ^(sec_protocol_metadata_t, sec_trust_t trust, sec_protocol_verify_complete_t done) {
                        SecTrustRef t = sec_trust_copy_ref(trust);
                        bool ok = false;
                        if (t) {
                            SecTrustSetAnchorCertificates(t, anchors);
                            SecTrustSetAnchorCertificatesOnly(t, true);
                            ok = SecTrustEvaluateWithError(t, nullptr);
                            CFRelease(t);
                        }
                        done(ok);
                    },
                    queue);
            }
            sec_release(so);
        },
        NW_PARAMETERS_DEFAULT_CONFIGURATION);

    nw_endpoint_t endpoint = nw_endpoint_create_host(e.host.c_str(), port.c_str());
    nw_connection_t conn = nw_connection_create(endpoint, params);
    nw_release(endpoint);
    nw_release(params);

    auto st = std::make_shared<State>();
    auto ready = std::make_shared<int>(0);
    auto failure = std::make_shared<std::string>();
    auto trust_failed = std::make_shared<bool>(false);
    nw_connection_set_queue(conn, queue);
    nw_connection_set_state_changed_handler(conn, ^(nw_connection_state_t state, nw_error_t error) {
        std::lock_guard g(st->m);
        switch (state) {
            case nw_connection_state_ready:
                if (*ready == 0) *ready = 1;
                break;
            case nw_connection_state_waiting:
            case nw_connection_state_failed:
                if (*ready == 0) {
                    *ready = -1;
                    *failure = describe(error);
                    *trust_failed = is_trust_failure(error);
                } else {
                    st->eof = true;
                    if (error) st->error = describe(error);
                }
                break;
            case nw_connection_state_cancelled:
                st->cancelled = true;
                st->eof = true;
                if (*ready == 0) *ready = -1;
                break;
            default:
                break;
        }
        st->cv.notify_all();
    });
    nw_connection_start(conn);

    bool arrived;
    {
        std::unique_lock lk(st->m);
        arrived = wait_for(st->cv, lk, timeout_seconds, [&] { return *ready != 0; });
    }
    if (!arrived || *ready < 0) {
        std::string why = arrived ? *failure : "no connection within the timeout";
        if (*trust_failed) why = "the certificate was rejected: " + why;
        tls_error = why;
        nw_connection_cancel(conn);
        {
            std::unique_lock lk(st->m);
            wait_for(st->cv, lk, 2.0, [&] { return st->cancelled; });
        }
        nw_release(conn);
        dispatch_release(queue);
        if (anchors) CFRelease(anchors);
        s = Status::fail("the TLS handshake failed: " + why);
        return nullptr;
    }
    if (anchors) CFRelease(anchors);
    s = Status::good();
    return std::make_unique<NetworkConnection>(conn, queue, st, e, timeout_seconds);
}

}
