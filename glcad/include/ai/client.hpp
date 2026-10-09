#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ai/types.hpp"

namespace gai {

using gcm::u8;

struct Error {
    int         status{0};
    std::string message;
};

template <class T>
struct Result {
    std::optional<T> value;
    Error            error;

    bool     ok() const noexcept { return value.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    const T& operator*() const noexcept { return *value; }
    const T* operator->() const noexcept { return &*value; }
};

class ToolCallAssembler {
public:
    void feed(const ChatDeltaContent& delta) {
        if (!delta.tool_calls) return;
        for (const auto& d : delta.tool_calls.get()) {
            if (d.index < 0) continue;
            if (static_cast<std::size_t>(d.index) >= calls_.size())
                calls_.resize(static_cast<std::size_t>(d.index) + 1);
            ToolCall& c = calls_[static_cast<std::size_t>(d.index)];
            if (d.id) c.id = d.id.get();
            if (d.function) {
                if (d.function.get().name) c.function.name = d.function.get().name.get();
                if (d.function.get().arguments)
                    c.function.arguments.emplace() =
                        c.arguments() + d.function.get().arguments.get();
            }
        }
    }
    std::vector<ToolCall> take() { return std::move(calls_); }
    bool any() const noexcept { return !calls_.empty(); }

private:
    std::vector<ToolCall> calls_;
};

class Client {
public:
    static std::optional<Client> from_url(const std::string& url);

    Client(std::string host, int port) : host_(std::move(host)), port_(port) {}

    static bool tls_available() noexcept;

    void set_api_key(std::string key) { api_key_ = std::move(key); }
    const std::string& api_key() const noexcept { return api_key_; }

    const std::string& host() const noexcept { return host_; }
    int                port() const noexcept { return port_; }
    bool               secure() const noexcept { return tls_; }
    const std::string& api_prefix() const noexcept { return prefix_; }

    std::string url() const;

    // idle_s resets on every received byte, so a live stream is never cut off.
    void set_timeouts(int connect_s, int idle_s) noexcept {
        connect_timeout_ = connect_s;
        read_timeout_ = idle_s;
    }

    Result<Health>        health() const;
    Result<ModelList>     models() const;
    Result<ChatResponse>  chat(const ChatRequest& req) const;

    struct StreamResult {
        std::string               text;
        std::string               reasoning;
        std::vector<ToolCall>     tool_calls;
        JsonOptional<std::string> finish_reason;
    };
    Result<StreamResult>  chat_stream(
        ChatRequest req,
        const std::function<void(const std::string&, bool reasoning)>& on_delta,
        const std::atomic<bool>* abort = nullptr) const;

    Result<std::vector<u8>> speech(const SpeechRequest& req) const;
    Result<std::size_t>     speech_stream(SpeechRequest req,
                                          const std::function<void(const u8*, std::size_t)>& on_audio) const;

    Result<EmbeddingResponse> embed(const EmbeddingRequest& req) const;

    Result<Transcription> transcribe(const std::string& model,
                                     const std::vector<u8>& audio,
                                     const std::string& filename,
                                     const std::string& mime,
                                     const std::string& language = {}) const;

    Result<ImageResponse> image(const ImageRequest& req) const;

private:
    std::string endpoint() const;
    std::optional<Error> tls_error() const;

    std::string host_;
    int         port_{8000};
    bool        tls_{false};
    std::string prefix_{"/api/v1"};
    std::string api_key_;
    int         connect_timeout_{3};
    int         read_timeout_{120};
};

}
