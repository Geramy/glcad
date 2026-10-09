#include "ui/mcp.hpp"
#include "ui/ai_tools.hpp"
#include "ui/shell.hpp"
#include "ui/snapshot.hpp"
#include "ai/encode.hpp"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <set>
namespace gcapp::mcp {
namespace {
using gai::Json;
constexpr const char *modern = "2026-07-28";
const Json versions = {modern, "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
Json failure(const Json &id, int code, const std::string &message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}
Json text_result(const Json &value) {
    return {{"content", Json::array({{{"type", "text"}, {"text", value.dump()}}})},
            {"structuredContent", value},
            {"isError", value.contains("error")}};
}
Json schema() { return {{"type", "object"}, {"properties", Json::object()}}; }
struct ToolActivity {
    Shell& shell;
    gai::ToolCall call;
    Json outcome={{"error","tool call did not complete"}};
    ToolActivity(Shell& s,const std::string& name,const Json& args):shell(s) {
        call.function.name=name;call.function.arguments=args.dump();
        shell.log_line(ai_tools::activity_entry(call,ai_tools::Origin::MCP));
    }
    ~ToolActivity() {
        shell.log_line(ai_tools::activity_entry(call,ai_tools::Origin::MCP,outcome.dump()));
    }
};

std::vector<gai::ToolDefinition> catalog() {
    auto tools = ai_tools::registry();
    const std::set<std::string> exclude = {"web_search", "fetch_url", "search_standards",
                                           "save_standard", "save_plan_rules"};
    std::erase_if(tools, [&](const auto &t) { return exclude.count(t.function.name); });
    for (auto &t : tools)
        if (t.function.name == "run_command")
            t.function.description =
                "Run a CAD command in the server document. Use help for syntax. Use document_open, "
                "document_save and document_export for files.";
    for (const auto &name : {"document_open", "document_save", "document_export"}) {
        auto s = schema();
        s["properties"]["path"] = {{"type", "string"}, {"description", "Absolute local file path"}};
        s["required"] = {"path"};
        s["properties"]["overwrite"] = {
            {"type", "boolean"},
            {"description", "Explicit permission to overwrite an existing output file"}};
        s["properties"]["discard_changes"] = {
            {"type", "boolean"},
            {"description", "Explicit permission to replace an unsaved server document"}};
        tools.push_back(gai::ToolDefinition::make(name,
                                                  std::string(name) == "document_open"
                                                      ? "Open a file in the active CAD document."
                                                      : "Write the open document to a file.",
                                                  s));
    }
    tools.push_back(gai::ToolDefinition::make(
        "document_info",
        "Get the explicit document handle used by this server and its edit status.", schema()));
    for (auto &t : tools)
        if (t.function.name != "document_info")
            t.function.parameters["properties"]["document_id"] = {
                {"type", "string"},
                {"description",
                 "Document handle from document_info (required by the 2026 protocol)"},
                {"minLength", 1}};
    std::sort(tools.begin(), tools.end(),
              [](const auto &a, const auto &b) { return a.function.name < b.function.name; });
    return tools;
}
bool valid(const Json &value, const Json &s) {
    const auto type = s.value("type", "");
    if (type == "object" && !value.is_object())
        return false;
    if (type == "string" && !value.is_string())
        return false;
    if (type == "boolean" && !value.is_boolean())
        return false;
    if (type == "number" && !value.is_number())
        return false;
    if (type == "integer" && !value.is_number_integer())
        return false;
    if (type == "array" && !value.is_array())
        return false;
    if (s.contains("enum") &&
        std::find(s["enum"].begin(), s["enum"].end(), value) == s["enum"].end())
        return false;
    if (value.is_object()) {
        if (s.contains("required"))
            for (const auto &key : s["required"])
                if (!value.contains(key.get<std::string>()))
                    return false;
        if (s.contains("properties"))
            for (const auto &[key, field] : s["properties"].items())
                if (value.contains(key) && !valid(value[key], field))
                    return false;
    }
    if (value.is_array() && s.contains("items"))
        for (const auto &item : value)
            if (!valid(item, s["items"]))
                return false;
    return true;
}
}
std::optional<Json> Server::handle(const Json &request) {
    const std::string document_id = live_ ? shell_.document_identity() : "main";
    Json id = request.is_object() ? request.value("id", Json(nullptr)) : Json(nullptr);
    if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0" ||
        !request.contains("method") || !request["method"].is_string())
        return failure(id, -32600, "Invalid JSON-RPC request");
    const std::string method = request["method"];
    if (!request.contains("id"))
        return std::nullopt;
    if (!id.is_string() && !id.is_number_integer())
        return failure(id, -32600, "Request id must be a string or integer");
    const Json params = request.value("params", Json::object());
    if (!params.is_object())
        return failure(id, -32602, "Parameters must be an object");
    try {
        bool current = false;
        if (params.contains("_meta")) {
            const auto &meta = params["_meta"];
            if (!meta.is_object())
                return failure(id, -32602, "Invalid request metadata");
            if (meta.contains("io.modelcontextprotocol/protocolVersion")) {
                const auto version = meta["io.modelcontextprotocol/protocolVersion"];
                if (version != modern) {
                    auto error = failure(id, -32022, "Unsupported protocol version");
                    error["error"]["data"] = {{"supported", versions}, {"requested", version}};
                    return error;
                }
                if (!meta.contains("io.modelcontextprotocol/clientCapabilities") ||
                    !meta["io.modelcontextprotocol/clientCapabilities"].is_object())
                    return failure(id, -32602, "Client capabilities are required");
                current = true;
            }
        }
        Json result;
        if (method == "initialize") {
            if (current)
                return failure(id, -32601, "The 2026 protocol uses server/discover");
            if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string())
                return failure(id, -32602, "protocolVersion is required");
            auto version = params["protocolVersion"].get<std::string>();
            if (version == modern ||
                std::find(versions.begin(), versions.end(), version) == versions.end())
                version = "2025-11-25";
            initialized_ = true;
            result = {{"protocolVersion", version},
                      {"capabilities", {{"tools", Json::object()}}},
                      {"serverInfo", {{"name", "glcad"}, {"version", "0.2.0"}}},
                      {"instructions",
                       "Work in the open document. Use document_open/save/export for "
                       "files; changes are undoable through run_command UNDO. Lengths use the "
                       "document's displayed units. " +
                       ai_tools::drawing_context(shell_)["drafting_guidance"].get<std::string>()}};
        } else {
            if (!current && !initialized_)
                return failure(
                    id, -32600,
                    "Initialize before using the legacy protocol, or supply 2026 request metadata");
            if (method == "server/discover") {
                result = {{"supportedVersions", versions},
                          {"capabilities", {{"tools", Json::object()}}},
                          {"_meta",
                           {{"io.modelcontextprotocol/serverInfo",
                             {{"name", "glcad"}, {"version", "0.2.0"}}}}},
                          {"instructions",
                           "Call document_info for the document handle, then supply document_id on "
                           "every document tool call. The HTTP server edits the active document "
                           "in the running application. Read document_info or document_summary for current "
                           "drawing discipline and drafting guidance. " +
                           ai_tools::drawing_context(shell_)["drafting_guidance"].get<std::string>()}};
            } else if (method == "ping")
                result = Json::object();
            else if (method == "tools/list") {
                if (params.contains("cursor") && !params["cursor"].get<std::string>().empty())
                    return failure(id, -32602, "Invalid cursor");
                result["tools"] = Json::array();
                for (const auto &tool : catalog()) {
                    auto schema = tool.function.parameters;
                    if ((current || live_) && tool.function.name != "document_info") {
                        if (!schema.contains("required"))
                            schema["required"] = Json::array();
                        schema["required"].push_back("document_id");
                    }
                    result["tools"].push_back({{"name", tool.function.name},
                                               {"description", tool.function.description},
                                               {"inputSchema", schema}});
                }
            } else if (method == "tools/call") {
                if (!params.contains("name") || !params["name"].is_string())
                    return failure(id, -32602, "Tool name is required");
                const auto name = params["name"].get<std::string>();
                const auto args = params.value("arguments", Json::object());
                ToolActivity activity(shell_,name,args);
                const auto reject=[&](const char* message) {
                    activity.outcome={{"error",message}};
                    return failure(id,-32602,message);
                };
                const auto tools = catalog();
                const auto found = std::find_if(tools.begin(), tools.end(), [&](const auto &t) {
                    return t.function.name == name;
                });
                if (found == tools.end())
                    return reject("Unknown tool");
                if (!valid(args, found->function.parameters))
                    return reject("Arguments do not match the tool schema");
                if ((current || live_ || args.contains("document_id")) && name != "document_info" &&
                    args.value("document_id", "") != document_id)
                    return reject("Supply document_id from document_info");
                Json value;
                if (live_ && shell_.read_only() && name != "document_info" &&
                    name != "document_summary" && name != "view_screenshot")
                    value = {{"error", "The active document is read-only"}};
                else if (name == "document_info") {
                    value = ai_tools::drawing_context(shell_);
                    value["document_id"] = document_id;
                    value["dirty"] = shell_.document_dirty();
                    value["units"] = gunit::name(shell_.canvas().display_unit());
                }
                else if (name == "document_open" || name == "document_save" ||
                         name == "document_export") {
                    const std::filesystem::path path = args.at("path").get<std::string>();
                    if (!path.is_absolute())
                        value = {{"error", "Use an absolute file path"}};
                    else if (name == "document_open" && shell_.document_dirty() &&
                             !args.value("discard_changes", false))
                        value = {{"error",
                                  "Save the current document or explicitly set discard_changes"}};
                    else if (name != "document_open" && std::filesystem::exists(path) &&
                             !args.value("overwrite", false))
                        value = {
                            {"error", "Output exists; explicitly set overwrite to replace it"}};
                    else {
                        const bool ok = name == "document_open" ? shell_.open_file(path.string())
                                        : name == "document_save"
                                            ? shell_.save_file(path.string())
                                            : shell_.export_file(path.string());
                        value = ok ? Json{{"path", path.string()}, {"document_id", document_id}}
                                   : Json{{"error", shell_.log().empty() ? "File operation failed"
                                                                         : shell_.log().back()}};
                    }
                } else if (name == "view_screenshot") {
                    std::string error;
                    shell_.load_font(&error);
                    shell_.build_frame();
                    auto png = snapshot_png(shell_, int(shell_.viewport_width()),
                                            int(shell_.viewport_height()));
                    if (png.empty())
                        value = {{"error", "Could not render screenshot"}};
                    else
                        result = {{"content", Json::array({{{"type", "image"},
                                                            {"mimeType", "image/png"},
                                                            {"data", gai::base64(png)}}})},
                                  {"isError", false}};
                } else {
                    if (name == "run_command") {
                        std::istringstream words(args.at("command").get<std::string>());
                        std::string word;
                        words >> word;
                        const auto *command = shell_.commands().find(word);
                        const std::string resolved = command ? command->name : word;
                        const std::set<std::string> file_commands = {
                            "OPEN",    "SAVE",    "SAVEAS", "QSAVE",    "EXPORT",
                            "IMPORT",  "NEW",     "CLOSE",  "CLOSETAB", "NEWTAB",
                            "NEXTTAB", "PREVTAB", "SCRIPT", "RECOVER"};
                        if (file_commands.count(resolved)) {
                            result = text_result(
                                Json{{"error", "Use the document tools for file operations; this "
                                               "server owns one document"}});
                            activity.outcome=result["structuredContent"];
                            if (current)
                                result["resultType"] = "complete";
                            return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
                        }
                    }
                    gai::ToolCall call;
                    call.function.name = name;
                    call.function.arguments = args.dump();
                    const auto output = ai_tools::execute(shell_, call, ai_tools::Origin::Silent);
                    value = Json::parse(output);
                    if (name == "run_command")
                        value["recent_command_history"] = shell_.log();
                    shell_.sync_document();
                }
                if (result.is_null())
                    result = text_result(value);
                activity.outcome=value.is_null() ? result : value;
            } else
                return failure(id, -32601, "Method not found");
        }
        if (current)
            result["resultType"] = "complete";
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    } catch (const std::exception &e) {
        return failure(id, -32602, e.what());
    }
}
}
