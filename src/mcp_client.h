#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "config.h"
#include "types.h"
#include "json.h"

// Abstract transport — concrete implementations live in mcp_client.cpp
class McpTransport {
public:
    virtual ~McpTransport() = default;
    virtual std::optional<JsonValue> send_rpc(int id, const std::string& method,
                                              const JsonValue& params) = 0;
    virtual void send_notification(const std::string& method) = 0;
};

class McpClient {
public:
    McpClient(const McpServerConfig& server, const Config& cfg);
    ~McpClient() = default;

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

    McpServerConfig               server_;
    Config                        cfg_;
    std::unique_ptr<McpTransport> transport_;
    int                           next_id_ = 1;
    std::mutex                    mutex_;
};
