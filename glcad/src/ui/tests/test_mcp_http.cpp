#include "test.hpp"
#include "ui/shell.hpp"
#include "engine/tcp.hpp"
#include <chrono>
#include <future>
#include <thread>
#include <filesystem>
using namespace gcapp;
namespace {
using Json = gai::Json;
struct Reply {
    int status{};
    Json body;
};
Reply request(Shell &shell, Json body, std::string token = {}, std::string extra = {},
              std::string method = "POST", std::string wire_body = {}) {
    const auto url = shell.mcp_url();
    const auto start = url.find(':', 7);
    const int port = std::stoi(url.substr(start + 1));
    auto future = std::async(std::launch::async, [=] {
        gio::TcpTransport tcp;
        tcp.set_timeout(3);
        gio::Status status;
        auto connection = tcp.connect(gio::Endpoint::ipv4("127.0.0.1", gcm::u16(port)),
                                      gio::Mode::ReadWrite, status);
        if (!connection)
            return Reply{};
        const auto payload = wire_body.empty() ? body.dump() : wire_body;
        std::string text = method + " /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                           "\r\nContent-Type: application/json\r\nAccept: application/json, "
                           "text/event-stream\r\nContent-Length: " +
                           std::to_string(payload.size()) + "\r\n";
        if (!token.empty())
            text += "Authorization: Bearer " + token + "\r\n";
        text += extra + "\r\n" + payload;
        connection->output()->write_all(reinterpret_cast<const gcm::u8 *>(text.data()),
                                        text.size());
        std::vector<gcm::u8> bytes;
        connection->input()->read_all(bytes);
        const std::string response(bytes.begin(), bytes.end());
        if (response.size() < 12)
            return Reply{};
        const auto split = response.find("\r\n\r\n");
        Reply reply;
        reply.status = std::stoi(response.substr(9, 3));
        if (split != response.npos && !response.substr(split + 4).empty())
            reply.body = Json::parse(response.substr(split + 4));
        return reply;
    });
    while (future.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready)
        shell.process_mcp_requests();
    return future.get();
}
Json rpc(const char *method, Json params = Json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
}
Shell setup() {
    Shell shell;
    shell.set_dpr(1);
    shell.set_viewport(1200, 800);
    return shell;
}
}
TEST(mcp_http_setting_controls_live_document_and_optional_token) {
    auto shell = setup();
    CHECK(!shell.mcp_running());
    CHECK(shell.set_mcp_enabled(true, 0));
    CHECK(shell.mcp_running());
    const auto token = shell.app_config().mcp_token;
    CHECK(token.size() == 64);
    CHECK(request(shell, rpc("ping")).status == 401);
    CHECK(request(shell, rpc("ping"), "wrong").status == 401);
    CHECK(request(shell, rpc("ping"), token, "Origin: https://untrusted.example\r\n").status ==
          403);
    CHECK(request(shell, rpc("ping"), token, "", "GET").status == 405);
    CHECK(request(shell, rpc("ping"), token, "", "POST", "{bad").status == 400);
    auto init = request(shell, rpc("initialize", {{"protocolVersion", "2025-11-25"}}), token);
    CHECK(init.status == 200);
    CHECK(request(shell, {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, token)
              .status == 202);
    auto info = request(shell, rpc("tools/call", {{"name", "document_info"}}), token);
    CHECK(info.status == 200);
    CHECK(info.body["result"]["structuredContent"]["document_id"] == shell.document_identity());
    const auto id = shell.document_identity();
    auto drawn =
        request(shell,
                rpc("tools/call",
                    {{"name", "draw_line"},
                     {"arguments", {{"document_id", id}, {"from", "0m,0m"}, {"to", "1m,1m"}}}}),
                token);
    CHECK(!drawn.body["result"]["isError"].get<bool>());
    CHECK(shell.canvas().shapes().size() == 1);
    auto undo =
        request(shell,
                rpc("tools/call", {{"name", "run_command"},
                                   {"arguments", {{"document_id", id}, {"command", "UNDO"}}}}),
                token);
    CHECK(undo.status == 200);
    CHECK(shell.canvas().shapes().empty());
    shell.new_tab();
    auto stale =
        request(shell,
                rpc("tools/call",
                    {{"name", "draw_line"},
                     {"arguments", {{"document_id", id}, {"from", "0m,0m"}, {"to", "1m,1m"}}}}),
                token);
    CHECK(stale.body.contains("error"));
    CHECK(shell.canvas().shapes().empty());
    shell.app_config().mcp_require_token = false;
    CHECK(shell.set_mcp_enabled(true));
    CHECK(request(shell, rpc("ping")).status == 200);
    CHECK(request(shell, rpc("ping"), {}, "Origin: https://untrusted.example\r\n").status == 403);
    shell.build_frame();
    CHECK(!shell.wants_frame());
    CHECK(shell.set_mcp_enabled(false));
    CHECK(!shell.mcp_running());
    CHECK(shell.set_mcp_enabled(true));
    CHECK(request(shell, rpc("ping")).status == 200);
    const auto config = std::filesystem::temp_directory_path() / "glcad-mcp-settings-test.bin";
    CHECK(gfile::save_app_config(config.string(), shell.app_config()));
    gfile::AppConfig restored;
    CHECK(gfile::load_app_config(config.string(), restored));
    CHECK(restored.mcp_enabled);
    CHECK(!restored.mcp_require_token);
    CHECK(restored.mcp_port == shell.app_config().mcp_port);
    CHECK(restored.mcp_token == token);
    std::filesystem::remove(config);
    shell.set_mcp_enabled(false);
}
TEST(mcp_http_modern_headers_and_settings_ui) {
    auto shell = setup();
    shell.app_config().mcp_require_token = false;
    CHECK(shell.set_mcp_enabled(true, 0));
    Json meta = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                 {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
    auto call = rpc("server/discover", {{"_meta", meta}});
    CHECK(request(shell, call).status == 400);
    const auto headers = "MCP-Protocol-Version: 2026-07-28\r\nMcp-Method: server/discover\r\n";
    auto good = request(shell, call, {}, headers);
    CHECK(good.status == 200);
    CHECK(good.body["result"]["resultType"] == "complete");
    CHECK(request(shell, call, {}, "MCP-Protocol-Version: 2026-07-28\r\nMcp-Method: ping\r\n")
              .status == 400);
    shell.command("OPTIONS");
    shell.build_frame();
    gui::Rect tab{};
    for (const auto &node : shell.accessible_ui())
        if (node.label == "MCP")
            tab = node.bounds;
    CHECK(tab.w > 0);
    const double x = tab.x + tab.w / 2, y = tab.y + tab.h / 2;
    shell.pointer_move(x, y);
    shell.build_frame();
    shell.pointer_down(x, y);
    shell.build_frame();
    shell.pointer_up(x, y);
    shell.build_frame();
    bool enable = false, auth = false;
    for (const auto &node : shell.accessible_ui()) {
        if (node.label == "Enable MCP server")
            enable = true;
        if (node.label == "Require access token")
            auth = true;
    }
    CHECK(enable);
    CHECK(auth);
    gui::Rect auth_button{};
    for (const auto &node : shell.accessible_ui())
        if (node.label == "Require access token")
            auth_button = node.bounds;
    const double ax = auth_button.x + auth_button.w / 2, ay = auth_button.y + auth_button.h / 2;
    shell.pointer_move(ax, ay);
    shell.build_frame();
    shell.pointer_down(ax, ay);
    shell.build_frame();
    shell.pointer_up(ax, ay);
    shell.build_frame();
    CHECK(shell.app_config().mcp_require_token);
    CHECK(request(shell, rpc("ping")).status == 401);
    shell.pointer_down(ax, ay);
    shell.build_frame();
    shell.pointer_up(ax, ay);
    shell.build_frame();
    CHECK(!shell.app_config().mcp_require_token);
    CHECK(request(shell, rpc("ping")).status == 200);
    shell.set_mcp_enabled(false);
}
