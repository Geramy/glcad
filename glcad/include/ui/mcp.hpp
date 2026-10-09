#pragma once
#include "ai/model.hpp"
#include <optional>
#include <memory>
#include <string>
namespace gcapp {
class Shell;
namespace mcp {
class Server {
  public:
    explicit Server(Shell &shell, bool http = false)
        : shell_(shell), initialized_(http), live_(http) {}
    std::optional<gai::Json> handle(const gai::Json &request);

  private:
    Shell &shell_;
    bool initialized_{false};
    bool live_{false};
};
class HttpServer {
  public:
    HttpServer();
    ~HttpServer();
    bool start(unsigned short port, const std::string &token, std::string &error);
    void stop();
    bool pending() const noexcept;
    bool process(Shell &shell);
    unsigned short port() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string new_token();
}
}
