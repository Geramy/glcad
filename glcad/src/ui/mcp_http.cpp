#include "ui/mcp.hpp"
#include "ui/shell.hpp"
#include "engine/tcp.hpp"
#include "engine/session.hpp"
#include "ai/encode.hpp"
#include <atomic>
#include <charconv>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gcapp::mcp {
namespace {
using Json = gai::Json;
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
bool nonblocking(Socket s) {
    u_long yes = 1;
    return ioctlsocket(s, FIONBIO, &yes) == 0;
}
bool again() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
using Socket = int;
constexpr Socket invalid = -1;
void close_socket(Socket s) { close(s); }
bool nonblocking(Socket s) { return fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK) == 0; }
bool again() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
#endif
constexpr std::size_t body_limit = 4 * 1024 * 1024, header_limit = 16384;
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t");
    if (a == s.npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}
Json error(int code, const std::string &message, Json id = nullptr) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}
std::string http(int status, const Json &value = nullptr) {
    const char *reason = status == 200   ? "OK"
                         : status == 202 ? "Accepted"
                         : status == 401 ? "Unauthorized"
                         : status == 403 ? "Forbidden"
                         : status == 404 ? "Not Found"
                         : status == 405 ? "Method Not Allowed"
                         : status == 413 ? "Content Too Large"
                         : status == 503 ? "Service Unavailable"
                                         : "Bad Request";
    const auto body = value.is_null() ? std::string{} : value.dump();
    return "HTTP/1.1 " + std::to_string(status) + " " + reason +
           "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\nCache-Control: no-store\r\n" +
           (status == 405 ? "Allow: POST\r\n" : "") + "\r\n" + body;
}
bool authorized(const std::string &value, const std::string &token) {
    const auto want = "Bearer " + token;
    if (value.size() != want.size())
        return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < want.size(); ++i)
        diff |= unsigned(value[i] ^ want[i]);
    return diff == 0;
}
std::string name_header(const std::string &name) {
    bool encode = name.starts_with("=?base64?") && name.ends_with("?=");
    for (unsigned char c : name)
        if (c < 32 || c > 126)
            encode = true;
    if (!name.empty() && (name.front() == ' ' || name.back() == ' '))
        encode = true;
    return encode
               ? "=?base64?" +
                     gai::base64(reinterpret_cast<const gcm::u8 *>(name.data()), name.size()) + "?="
               : name;
}
}
struct HttpServer::Impl {
    struct Job {
        Json request;
        std::mutex mutex;
        std::string reply;
        bool cancelled{false};
    };
    struct Client {
        Socket socket{invalid};
        std::string input, output;
        std::size_t sent{0}, body_at{0}, length{0};
        std::map<std::string, std::string> headers;
        std::shared_ptr<Job> job;
        Clock::time_point deadline = Clock::now() + std::chrono::seconds(10);
    };
    Socket listener{invalid};
    unsigned short port{0};
    std::string token;
    std::atomic<bool> stopping{false}, pending{false};
    std::thread worker;
    std::mutex queue_mutex;
    std::deque<std::shared_ptr<Job>> jobs;
    void parse(Client &c) {
        if (!c.body_at) {
            const auto end = c.input.find("\r\n\r\n");
            if (end == std::string::npos) {
                if (c.input.size() > header_limit)
                    c.output = http(413);
                return;
            }
            if (end > header_limit) {
                c.output = http(413);
                return;
            }
            const auto line = c.input.find("\r\n");
            std::istringstream first(c.input.substr(0, line));
            std::string method, path, version, extra;
            if (!(first >> method >> path >> version) || first >> extra || version != "HTTP/1.1") {
                c.output = http(400);
                return;
            }
            std::size_t pos = line + 2;
            while (pos < end) {
                const auto next = c.input.find("\r\n", pos), colon = c.input.find(':', pos);
                if (colon == std::string::npos || colon >= next) {
                    c.output = http(400);
                    return;
                }
                const auto name = lower(c.input.substr(pos, colon - pos)),
                           value = trim(c.input.substr(colon + 1, next - colon - 1));
                if (name.empty() ||
                    name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != name.npos ||
                    !c.headers.emplace(name, value).second) {
                    c.output = http(400);
                    return;
                }
                for (unsigned char ch : value)
                    if ((ch < 32 && ch != '\t') || ch == 127) {
                        c.output = http(400);
                        return;
                    }
                pos = next + 2;
            }
            const auto authority = "127.0.0.1:" + std::to_string(port),
                       local = "localhost:" + std::to_string(port);
            if (c.headers["host"] != authority && c.headers["host"] != local) {
                c.output = http(403);
                return;
            }
            if (c.headers.count("origin") && c.headers["origin"] != "http://" + authority &&
                c.headers["origin"] != "http://" + local) {
                c.output = http(403);
                return;
            }
            if (!token.empty() && !authorized(c.headers["authorization"], token)) {
                c.output = http(401);
                return;
            }
            if (path != "/mcp") {
                c.output = http(404);
                return;
            }
            if (method != "POST") {
                c.output = http(405);
                return;
            }
            if (lower(c.headers["content-type"]).substr(0, 16) != "application/json" ||
                c.headers.count("transfer-encoding")) {
                c.output = http(400);
                return;
            }
            if (c.headers["accept"].find("application/json") == std::string::npos ||
                c.headers["accept"].find("text/event-stream") == std::string::npos) {
                c.output = http(400);
                return;
            }
            const auto &length = c.headers["content-length"];
            auto parsed = std::from_chars(length.data(), length.data() + length.size(), c.length);
            if (length.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != length.data() + length.size()) {
                c.output = http(400);
                return;
            }
            if (c.length > body_limit) {
                c.output = http(413);
                return;
            }
            c.body_at = end + 4;
        }
        if (c.input.size() < c.body_at + c.length)
            return;
        auto request = Json::parse(c.input.substr(c.body_at, c.length), nullptr, false);
        if (request.is_discarded()) {
            c.output = http(400, error(-32700, "Invalid JSON"));
            return;
        }
        if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0" ||
            !request.contains("method") || !request["method"].is_string()) {
            c.output = http(400, error(-32600, "Invalid request"));
            return;
        }
        const auto params = request.value("params", Json::object());
        if (!params.is_object()) {
            c.output = http(400, error(-32602, "Invalid parameters"));
            return;
        }
        const auto protocol = c.headers["mcp-protocol-version"];
        const auto meta = params.value("_meta", Json::object());
        if (!meta.is_object()) {
            c.output = http(400, error(-32602, "Invalid metadata"));
            return;
        }
        if (protocol == "2026-07-28" || meta.contains("io.modelcontextprotocol/protocolVersion")) {
            if (protocol != "2026-07-28" ||
                meta.value("io.modelcontextprotocol/protocolVersion", Json()) != protocol ||
                c.headers["mcp-method"] != request["method"].get<std::string>() ||
                (request["method"] == "tools/call" &&
                 (!params.contains("name") || !params["name"].is_string() ||
                  c.headers["mcp-name"] != name_header(params["name"].get<std::string>())))) {
                c.output = http(400, error(-32020, "MCP request headers do not match the message",
                                           request.value("id", Json())));
                return;
            }
        } else if (!protocol.empty() && protocol != "2025-11-25" && protocol != "2025-06-18" &&
                   protocol != "2025-03-26") {
            auto e = error(-32022, "Unsupported protocol version", request.value("id", Json()));
            e["error"]["data"] = {
                {"supported", {"2026-07-28", "2025-11-25", "2025-06-18", "2025-03-26"}},
                {"requested", protocol}};
            c.output = http(400, e);
            return;
        }
        c.job = std::make_shared<Job>();
        c.job->request = std::move(request);
        {
            std::lock_guard lock(queue_mutex);
            if (jobs.size() >= 32) {
                c.job.reset();
                c.output = http(503);
                return;
            }
            jobs.push_back(c.job);
            pending = true;
        }
        c.deadline = Clock::now() + std::chrono::minutes(5);
        c.input.clear();
    }
    void run() {
        std::vector<Client> clients;
        while (!stopping) {
            fd_set reads, writes;
            FD_ZERO(&reads);
            FD_ZERO(&writes);
            FD_SET(listener, &reads);
            Socket high = listener;
            for (auto &c : clients) {
                if (c.job && c.output.empty()) {
                    std::lock_guard lock(c.job->mutex);
                    c.output.swap(c.job->reply);
                }
                if (!c.output.empty())
                    FD_SET(c.socket, &writes);
                else
                    FD_SET(c.socket, &reads);
                high = std::max(high, c.socket);
            }
            timeval timeout{0, 100000};
            if (select(int(high) + 1, &reads, &writes, nullptr, &timeout) < 0)
                continue;
            if (FD_ISSET(listener, &reads)) {
                Socket fd = accept(listener, nullptr, nullptr);
                if (fd != invalid) {
                    bool usable = clients.size() < 8;
#ifndef _WIN32
                    usable = usable && fd < FD_SETSIZE;
#endif
                    if (!usable || !nonblocking(fd))
                        close_socket(fd);
                    else {
#ifdef SO_NOSIGPIPE
                        int yes = 1;
                        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
                        clients.push_back(Client{});
                        clients.back().socket = fd;
                    }
                }
            }
            for (auto it = clients.begin(); it != clients.end();) {
                auto &c = *it;
                bool finished = Clock::now() > c.deadline;
                if (!finished && FD_ISSET(c.socket, &reads) && c.output.empty()) {
                    char bytes[8192];
                    const auto n = recv(c.socket, bytes, sizeof(bytes), 0);
                    if (n == 0 || (n < 0 && !again()))
                        finished = true;
                    else if (n > 0) {
                        if (c.job)
                            finished = true;
                        else {
                            c.input.append(bytes, std::size_t(n));
                            try {
                                parse(c);
                            } catch (...) {
                                c.output = http(400, error(-32600, "Invalid request"));
                            }
                        }
                    }
                }
                if (!finished && !c.output.empty() && FD_ISSET(c.socket, &writes)) {
#ifdef MSG_NOSIGNAL
                    constexpr int flags = MSG_NOSIGNAL;
#else
                    constexpr int flags = 0;
#endif
                    const auto n =
                        send(c.socket, c.output.data() + c.sent,
                             int(std::min<std::size_t>(c.output.size() - c.sent, 65536)), flags);
                    if (n > 0) {
                        c.sent += std::size_t(n);
                        finished = c.sent == c.output.size();
                    } else if (n < 0 && !again())
                        finished = true;
                }
                if (finished) {
                    if (c.job) {
                        std::lock_guard lock(c.job->mutex);
                        c.job->cancelled = true;
                    }
                    close_socket(c.socket);
                    it = clients.erase(it);
                } else
                    ++it;
            }
        }
        for (auto &c : clients)
            close_socket(c.socket);
    }
};
HttpServer::HttpServer() : impl_(std::make_unique<Impl>()) {}
HttpServer::~HttpServer() { stop(); }
bool HttpServer::start(unsigned short port, const std::string &token, std::string &error_text) {
    stop();
    if (!gio::socket_startup()) {
        error_text = "Could not initialize MCP networking";
        return false;
    }
    Socket fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == invalid) {
        error_text = "Could not create MCP listener";
        return false;
    }
#ifndef _WIN32
    if (fd >= FD_SETSIZE) {
        close_socket(fd);
        error_text = "Too many open sockets";
        return false;
    }
#endif
    int exclusive = 1;
#ifdef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
               sizeof(exclusive));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &exclusive, sizeof(exclusive));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(fd, 8) != 0 || !nonblocking(fd)) {
        close_socket(fd);
        error_text =
            "Could not listen on localhost port " + std::to_string(port) + "; choose another port";
        return false;
    }
#ifdef _WIN32
    int size = sizeof(address);
#else
    socklen_t size = sizeof(address);
#endif
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
        close_socket(fd);
        error_text = "Could not read MCP port";
        return false;
    }
    impl_->listener = fd;
    impl_->port = ntohs(address.sin_port);
    impl_->token = token;
    impl_->stopping = false;
    try {
        impl_->worker = std::thread([state = impl_.get()] { state->run(); });
    } catch (...) {
        close_socket(fd);
        impl_->listener = invalid;
        impl_->port = 0;
        error_text = "Could not start MCP worker";
        return false;
    }
    return true;
}
void HttpServer::stop() {
    impl_->stopping = true;
    if (impl_->worker.joinable())
        impl_->worker.join();
    if (impl_->listener != invalid)
        close_socket(impl_->listener);
    impl_->listener = invalid;
    impl_->port = 0;
    std::lock_guard lock(impl_->queue_mutex);
    impl_->jobs.clear();
    impl_->pending = false;
}
bool HttpServer::pending() const noexcept { return impl_->pending.load(); }
unsigned short HttpServer::port() const noexcept { return impl_->port; }
bool HttpServer::process(Shell &shell) {
    std::shared_ptr<Impl::Job> job;
    {
        std::lock_guard lock(impl_->queue_mutex);
        if (impl_->jobs.empty())
            return false;
        job = impl_->jobs.front();
        impl_->jobs.pop_front();
        impl_->pending = !impl_->jobs.empty();
    }
    {
        std::lock_guard lock(job->mutex);
        if (job->cancelled)
            return false;
    }
    // No Shell reference crosses the worker boundary.
    Server server(shell, true);
    auto response = server.handle(job->request);
    int status = 200;
    if (response && response->contains("error")) {
        const int code = (*response)["error"].value("code", 0);
        if (code == -32601)
            status = 404;
        else if (code == -32020 || code == -32022 || code == -32600 || code == -32602)
            status = 400;
    }
    auto result = response ? http(status, *response) : http(202);
    {
        std::lock_guard lock(job->mutex);
        job->reply = std::move(result);
    }
    return true;
}
std::string new_token() {
    const char *hex = "0123456789abcdef";
    std::string result;
    for (int i = 0; i < 2; ++i)
        for (auto byte : gio::make_nonce()) {
            result += hex[byte >> 4];
            result += hex[byte & 15];
        }
    return result;
}
}

namespace gcapp {
bool Shell::set_mcp_enabled(bool enabled, int port) {
    if (port < 0)
        port = app_config_.mcp_port;
    if (port < 0 || port > 65535) {
        mcp_error_ = "MCP port must be 1–65535";
        log_.push_back(mcp_error_);
        return false;
    }
    if (mcp_server_)
        mcp_server_->stop();
    app_config_.mcp_enabled = false;
    app_config_.mcp_port = port;
    mcp_error_.clear();
    if (enabled) {
        try {
            if (app_config_.mcp_token.empty())
                app_config_.mcp_token = mcp::new_token();
            if (!mcp_server_)
                mcp_server_ = std::make_unique<mcp::HttpServer>();
            if (!mcp_server_->start(static_cast<unsigned short>(port),
                                    app_config_.mcp_require_token ? app_config_.mcp_token
                                                                  : std::string{},
                                    mcp_error_)) {
                log_.push_back(mcp_error_);
                save_app_config();
                return false;
            }
        } catch (const std::exception &) {
            mcp_error_ = "Could not start MCP server";
            log_.push_back(mcp_error_);
            save_app_config();
            return false;
        }
        app_config_.mcp_enabled = true;
        app_config_.mcp_port = mcp_server_->port();
    }
    mcp_port_input_ = std::to_string(app_config_.mcp_port);
    save_app_config();
    log_.push_back(enabled ? "MCP server listening at " + mcp_url() : "MCP server stopped");
    return true;
}
std::string Shell::mcp_url() const {
    return "http://127.0.0.1:" +
           std::to_string(mcp_running() ? mcp_server_->port() : app_config_.mcp_port) + "/mcp";
}
void Shell::process_mcp_requests() {
    if (mcp_processing_ || !mcp_server_ || !mcp_server_->pending())
        return;
    mcp_processing_ = true;
    struct Reset {
        bool &value;
        ~Reset() { value = false; }
    } reset{mcp_processing_};
    if (mcp_server_->process(*this))
        mesh_dirty_ = true;
}
void Shell::draw_settings_mcp(gui::Stack &stack) {
    const auto label = [&](const std::string &text) {
        gui::label(ui_ctx_, stack.next(float(px(30))), text);
    };
    label("Let connected clients work on the open CAD document.");
    bool enabled = mcp_running();
    if (gui::toggle(ui_ctx_, stack.next(float(px(38))), "Enable MCP server", enabled))
        set_mcp_enabled(enabled);
    bool require_token = app_config_.mcp_require_token;
    if (gui::toggle(ui_ctx_, stack.next(float(px(38))), "Require access token", require_token)) {
        app_config_.mcp_require_token = require_token;
        set_mcp_enabled(mcp_running());
    }
    label(mcp_running() ? "Running on this device" : "Stopped");
    if (mcp_port_input_.empty())
        mcp_port_input_ = std::to_string(app_config_.mcp_port);
    label("Local port");
    gui::text_field(ui_ctx_, stack.next(float(px(36))), "MCP port", mcp_port_input_);
    if (gui::button(ui_ctx_, stack.next(float(px(36))), "Apply port")) {
        int port = 0;
        auto parsed = std::from_chars(mcp_port_input_.data(),
                                      mcp_port_input_.data() + mcp_port_input_.size(), port);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != mcp_port_input_.data() + mcp_port_input_.size() || port < 1 ||
            port > 65535)
            log_.push_back("MCP port must be 1–65535");
        else
            set_mcp_enabled(mcp_running(), port);
    }
    label(mcp_url());
    if (gui::button(ui_ctx_, stack.next(float(px(36))), "Copy MCP URL") && clipboard_set_hook)
        clipboard_set_hook(mcp_url());
    if (mcp_running()) {
        if (app_config_.mcp_require_token &&
            gui::button(ui_ctx_, stack.next(float(px(36))), "Copy access token") &&
            clipboard_set_hook)
            clipboard_set_hook(app_config_.mcp_token);
        if (gui::button(ui_ctx_, stack.next(float(px(36))), "Copy client configuration") &&
            clipboard_set_hook) {
            gai::Json config = {
                {"mcpServers",
                 {{"glcad",
                   {{"url", mcp_url()},
                    {"headers", {{"Authorization", "Bearer " + app_config_.mcp_token}}}}}}}};
            if (!app_config_.mcp_require_token)
                config["mcpServers"]["glcad"].erase("headers");
            clipboard_set_hook(config.dump(2));
        }
        if (gui::button(ui_ctx_, stack.next(float(px(36))), "Replace access token")) {
            try {
                app_config_.mcp_token = mcp::new_token();
                set_mcp_enabled(true);
            } catch (...) {
                log_.push_back("Could not replace MCP access token");
            }
        }
    }
    label(app_config_.mcp_require_token ? "Connect using Streamable HTTP and the copied token."
                                        : "Connect using Streamable HTTP; no token is required.");
    label("Turning this off disconnects clients. No restart is needed.");
}
}
