#include "ai/client.hpp"

#include <cstdlib>

#include <httplib.h>

namespace gai {

namespace {

// httplib's read timeout is per socket read: an idle timeout, not a cap.
constexpr const char* kLocalPrefix  = "/api/v1";
constexpr const char* kHostedPrefix = "/v1";
const char* default_prefix(bool tls) { return tls ? kHostedPrefix : kLocalPrefix; }

Error transport_error(const httplib::Result& r) {
    Error e;
    e.status = 0;
    e.message = "could not reach the server (" + httplib::to_string(r.error()) + ")";
    return e;
}

Error http_error(const httplib::Response& res) {
    Error e;
    e.status = res.status;
    std::string body = res.body.substr(0, 200);
    for (char& c : body)
        if (static_cast<unsigned char>(c) < 0x20 && c != ' ') c = ' ';
    e.message = "HTTP " + std::to_string(res.status) + (body.empty() ? "" : ": " + body);
    return e;
}

template <class T>
Result<T> parse_body(const std::string& body) {
    Result<T> out;
    T value{};
    if (!value.parse(body)) {
        out.error = {0, "the server's answer did not parse"};
        return out;
    }
    out.value = std::move(value);
    return out;
}

}

static httplib::Headers auth_headers(const std::string& key) {
    httplib::Headers h;
    if (!key.empty()) h.emplace("Authorization", "Bearer " + key);
    return h;
}

bool Client::tls_available() noexcept {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    return true;
#else
    return false;
#endif
}

std::optional<Client> Client::from_url(const std::string& url) {
    std::string s = url;
    bool tls = false;
    if (s.rfind("http://", 0) == 0) s = s.substr(7);
    else if (s.rfind("https://", 0) == 0) { s = s.substr(8); tls = true; }

    std::string path;
    if (const auto slash = s.find('/'); slash != std::string::npos) {
        path = s.substr(slash);
        s = s.substr(0, slash);
    }
    if (s.empty()) return std::nullopt;

    int port = -1;
    if (const auto colon = s.rfind(':'); colon != std::string::npos) {
        const std::string p = s.substr(colon + 1);
        char* end = nullptr;
        const long n = std::strtol(p.c_str(), &end, 10);
        if (!end || *end != '\0' || n <= 0 || n > 65535) return std::nullopt;
        port = static_cast<int>(n);
        s = s.substr(0, colon);
    }
    if (s.empty()) return std::nullopt;
    if (port < 0) port = tls ? 443 : 8000;

    while (!path.empty() && path.back() == '/') path.pop_back();

    Client c(std::move(s), port);
    c.tls_ = tls;
    c.prefix_ = path.empty() ? default_prefix(tls) : path;
    return c;
}

std::string Client::url() const {
    std::string u = (tls_ ? "https://" : "http://") + host_;
    if (!tls_ || port_ != 443) u += ":" + std::to_string(port_);
    if (prefix_ != default_prefix(tls_)) u += prefix_;
    return u;
}

std::string Client::endpoint() const {
    return (tls_ ? "https://" : "http://") + host_ + ":" + std::to_string(port_);
}

std::optional<Error> Client::tls_error() const {
    if (tls_ && !tls_available())
        return Error{0, "this build cannot open https connections (no OpenSSL); "
                        "use a http:// address, or rebuild with OpenSSL"};
    return std::nullopt;
}

Result<Health> Client::health() const {
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(5);
    auto r = c.Get(prefix_ + "/health", auth_headers(api_key_));
    if (r && r->status == 404 && prefix_ != "/")
        r = c.Get("/health", auth_headers(api_key_));
    if (!r) return {std::nullopt, transport_error(r)};
    if (r->status != 200) return {std::nullopt, http_error(*r)};
    return parse_body<Health>(r->body);
}

Result<ModelList> Client::models() const {
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(10);
    const auto r = c.Get(prefix_ + "/models", auth_headers(api_key_));
    if (!r) return {std::nullopt, transport_error(r)};
    if (r->status != 200) return {std::nullopt, http_error(*r)};
    return parse_body<ModelList>(r->body);
}

Result<ChatResponse> Client::chat(const ChatRequest& req) const {
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(read_timeout_);
    const auto r = c.Post(prefix_ + "/chat/completions",
                          auth_headers(api_key_), req.to_json(), "application/json");
    if (!r) return {std::nullopt, transport_error(r)};
    if (r->status != 200) return {std::nullopt, http_error(*r)};
    return parse_body<ChatResponse>(r->body);
}

Result<Client::StreamResult> Client::chat_stream(
    ChatRequest req,
    const std::function<void(const std::string&, bool)>& on_delta,
    const std::atomic<bool>* abort) const {
    req.stream = true;
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(read_timeout_);

    StreamResult out_r;
    ToolCallAssembler tools;
    std::string carry;
    const auto feed_line = [&](std::string_view line) {
        if (line.rfind("data:", 0) != 0) return;
        std::string_view payload = line.substr(5);
        while (!payload.empty() && payload.front() == ' ') payload.remove_prefix(1);
        if (payload == "[DONE]") return;
        ChatDelta d;
        if (!d.parse(payload)) return;
        for (const auto& ch : d.choices) {
            if (ch.delta.content.has() && !ch.delta.content.get().empty()) {
                out_r.text += ch.delta.content.get();
                if (on_delta) on_delta(ch.delta.content.get(), false);
            }
            if (ch.delta.reasoning_content.has() &&
                !ch.delta.reasoning_content.get().empty()) {
                out_r.reasoning += ch.delta.reasoning_content.get();
                if (on_delta) on_delta(ch.delta.reasoning_content.get(), true);
            }
            if (ch.delta.reasoning.has() &&
                !ch.delta.reasoning.get().empty()) {
                out_r.reasoning += ch.delta.reasoning.get();
                if (on_delta) on_delta(ch.delta.reasoning.get(), true);
            }
            tools.feed(ch.delta);
            if (ch.finish_reason.has()) out_r.finish_reason = ch.finish_reason.get();
        }
    };

    httplib::Request hr;
    hr.method = "POST";
    hr.path = prefix_ + "/chat/completions";
    hr.set_header("Content-Type", "application/json");
    if (!api_key_.empty()) hr.set_header("Authorization", "Bearer " + api_key_);
    hr.body = req.to_json();
    hr.content_receiver = [&](const char* data, std::size_t len, std::uint64_t, std::uint64_t) {
        if (abort && abort->load()) return false;
        carry.append(data, len);
        std::size_t start = 0;
        for (std::size_t i = 0; i < carry.size(); ++i)
            if (carry[i] == '\n') {
                std::string_view line(carry.data() + start, i - start);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                feed_line(line);
                start = i + 1;
            }
        carry.erase(0, start);
        return true;
    };
    httplib::Response res;
    httplib::Error err = httplib::Error::Success;
    if (!c.send(hr, res, err)) {
        Error e;
        e.message = "could not reach the server (" + httplib::to_string(err) + ")";
        return {std::nullopt, e};
    }
    if (res.status != 200) return {std::nullopt, http_error(res)};
    if (!carry.empty()) feed_line(carry);
    out_r.tool_calls = tools.take();
    return {std::move(out_r), {}};
}

Result<EmbeddingResponse> Client::embed(const EmbeddingRequest& req) const {
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(read_timeout_);
    const auto r = c.Post(prefix_ + "/embeddings",
                          auth_headers(api_key_), req.to_json(),
                          "application/json");
    if (!r) return {std::nullopt, transport_error(r)};
    if (r->status != 200) return {std::nullopt, http_error(*r)};
    return parse_body<EmbeddingResponse>(r->body);
}

Result<std::vector<u8>> Client::speech(const SpeechRequest& req) const {
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(read_timeout_);
    const auto r = c.Post(prefix_ + "/audio/speech",
                          auth_headers(api_key_), req.to_json(), "application/json");
    if (!r) return {std::nullopt, transport_error(r)};
    if (r->status != 200) return {std::nullopt, http_error(*r)};
    std::vector<u8> bytes(r->body.begin(), r->body.end());
    return {std::move(bytes), {}};
}

Result<std::size_t> Client::speech_stream(
    SpeechRequest req, const std::function<void(const u8*, std::size_t)>& on_audio) const {
    req.stream = true;
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(read_timeout_);
    std::size_t total = 0;
    httplib::Request hr;
    hr.method = "POST";
    hr.path = prefix_ + "/audio/speech";
    hr.set_header("Content-Type", "application/json");
    if (!api_key_.empty()) hr.set_header("Authorization", "Bearer " + api_key_);
    hr.body = req.to_json();
    hr.content_receiver = [&](const char* data, std::size_t len, std::uint64_t, std::uint64_t) {
        total += len;
        if (on_audio) on_audio(reinterpret_cast<const u8*>(data), len);
        return true;
    };
    httplib::Response res;
    httplib::Error err = httplib::Error::Success;
    if (!c.send(hr, res, err)) {
        Error e;
        e.message = "could not reach the server (" + httplib::to_string(err) + ")";
        return {std::nullopt, e};
    }
    if (res.status != 200) return {std::nullopt, http_error(res)};
    return {total, {}};
}

Result<Transcription> Client::transcribe(const std::string& model,
                                         const std::vector<u8>& audio,
                                         const std::string& filename,
                                         const std::string& mime,
                                         const std::string& language) const {
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(read_timeout_);
    httplib::MultipartFormDataItems items = {
        {"model", model, "", ""},
        {"response_format", "json", "", ""},
        {"file", std::string(audio.begin(), audio.end()), filename, mime},
    };
    if (!language.empty()) items.push_back({"language", language, "", ""});
    const auto r = c.Post(prefix_ + "/audio/transcriptions", auth_headers(api_key_), items);
    if (!r) return {std::nullopt, transport_error(r)};
    if (r->status != 200) return {std::nullopt, http_error(*r)};
    return parse_body<Transcription>(r->body);
}

Result<ImageResponse> Client::image(const ImageRequest& req) const {
    if (const auto e = tls_error()) return {std::nullopt, *e};
    httplib::Client c(endpoint());
    c.set_connection_timeout(connect_timeout_);
    c.set_read_timeout(std::max(read_timeout_, 300));   // diffusion is slow
    const auto r = c.Post(prefix_ + "/images/generations",
                          auth_headers(api_key_), req.to_json(), "application/json");
    if (!r) return {std::nullopt, transport_error(r)};
    if (r->status != 200) return {std::nullopt, http_error(*r)};
    return parse_body<ImageResponse>(r->body);
}

}
