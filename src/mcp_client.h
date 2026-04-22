#pragma once

#include <optional>
#include <string>
#include <vector>
#include <curl/curl.h>
#include "config.h"
#include "types.h"
#include "json.h"

class McpClient {
public:
    McpClient(const McpServerConfig& server, const Config& cfg);
    ~McpClient();

    // MCP lifecycle — call initialize() before list_tools() or call_tool()
    bool                 initialize();
    std::vector<ToolDef> list_tools();
    ToolResult           call_tool(const std::string& name, const ToolArgs& args);

    // Testable static helpers (no network required)
    static std::string           build_rpc(int id, const std::string& method, const JsonValue& params);
    static std::string           build_notification(const std::string& method);
    static JsonValue             parse_sse_or_json(const std::string& body, const std::string& content_type);
    static std::vector<ToolDef>  parse_tools_list(const JsonValue& result);
    static ToolResult            parse_call_result(const JsonValue& result);

private:
    std::optional<JsonValue> send_rpc(const std::string& method,
                                      const JsonValue& params = JsonValue{});
    void send_notification(const std::string& method);

    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

    McpServerConfig server_;
    Config          cfg_;
    CURL*           curl_ = nullptr;
    int             next_id_ = 1;
};
