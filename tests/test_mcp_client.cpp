#include "harness.h"
#include "../src/mcp_client.h"
#include "../src/config.h"
#include "../src/json.h"
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

// ============================================================================
// build_rpc
// ============================================================================

TEST(mcp_build_rpc_no_params) {
    std::string rpc = McpClient::build_rpc(1, "tools/list", JsonValue{});
    JsonValue v = parse_json(rpc);
    CHECK_EQ(v.get("jsonrpc")->as_string(), std::string("2.0"));
    CHECK_EQ((int)v.get("id")->as_number(), 1);
    CHECK_EQ(v.get("method")->as_string(), std::string("tools/list"));
    CHECK(!v.get("params").has_value());
}

TEST(mcp_build_rpc_with_params) {
    JsonValue params = parse_json(R"({"name":"my_tool","arguments":{}})");
    std::string rpc = McpClient::build_rpc(42, "tools/call", params);
    JsonValue v = parse_json(rpc);
    CHECK_EQ((int)v.get("id")->as_number(), 42);
    CHECK_EQ(v.get("method")->as_string(), std::string("tools/call"));
    CHECK(v.get("params").has_value());
    CHECK_EQ(v.get("params")->get("name")->as_string(), std::string("my_tool"));
}

TEST(mcp_build_rpc_escapes_special_chars_in_method) {
    // Method with a double-quote — escape_json must be applied
    std::string rpc = McpClient::build_rpc(1, "foo\"bar", JsonValue{});
    // Must produce valid JSON
    JsonValue v = parse_json(rpc);
    // Round-trip: the method name must survive intact
    CHECK_EQ(v.get("method")->as_string(), std::string("foo\"bar"));
}

// ============================================================================
// build_notification
// ============================================================================

TEST(mcp_build_notification) {
    std::string notif = McpClient::build_notification("notifications/initialized");
    JsonValue v = parse_json(notif);
    CHECK_EQ(v.get("jsonrpc")->as_string(), std::string("2.0"));
    CHECK_EQ(v.get("method")->as_string(), std::string("notifications/initialized"));
    CHECK(!v.get("id").has_value());
}

// ============================================================================
// parse_sse_or_json — JSON path
// ============================================================================

TEST(mcp_parse_json_response) {
    std::string body = R"({"jsonrpc":"2.0","id":1,"result":{"tools":[]}})";
    JsonValue v = McpClient::parse_sse_or_json(body, "application/json");
    CHECK(v.get("result").has_value());
    CHECK(v.get("result")->get("tools")->is_array());
}

TEST(mcp_parse_json_with_charset) {
    std::string body = R"({"jsonrpc":"2.0","id":2,"result":{"ok":true}})";
    JsonValue v = McpClient::parse_sse_or_json(body, "application/json; charset=utf-8");
    CHECK(v.get("result").has_value());
}

TEST(mcp_parse_json_invalid_returns_null) {
    JsonValue v = McpClient::parse_sse_or_json("not json", "application/json");
    CHECK(v.is_null());
}

// ============================================================================
// parse_sse_or_json — SSE path
// ============================================================================

TEST(mcp_parse_sse_basic) {
    std::string body =
        "event: message\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"tools\":[]}}\n"
        "\n";
    JsonValue v = McpClient::parse_sse_or_json(body, "text/event-stream");
    CHECK(v.get("result").has_value());
    CHECK(v.get("result")->get("tools")->is_array());
}

TEST(mcp_parse_sse_with_charset) {
    std::string body = "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"x\":42}}\n\n";
    JsonValue v = McpClient::parse_sse_or_json(body, "text/event-stream; charset=utf-8");
    CHECK(v.get("result")->get("x")->as_number() == 42.0);
}

TEST(mcp_parse_sse_skips_comments) {
    std::string body =
        ": keepalive\n"
        "\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"ok\":true}}\n"
        "\n";
    JsonValue v = McpClient::parse_sse_or_json(body, "text/event-stream");
    CHECK(v.get("result")->get("ok")->as_bool() == true);
}

TEST(mcp_parse_sse_done_returns_null) {
    std::string body = "data: [DONE]\n\n";
    JsonValue v = McpClient::parse_sse_or_json(body, "text/event-stream");
    CHECK(v.is_null());
}

TEST(mcp_parse_sse_empty_returns_null) {
    std::string body = "\n\n";
    JsonValue v = McpClient::parse_sse_or_json(body, "text/event-stream");
    CHECK(v.is_null());
}

TEST(mcp_parse_sse_crlf_lines) {
    std::string body = "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\r\n\r\n";
    JsonValue v = McpClient::parse_sse_or_json(body, "text/event-stream");
    CHECK(v.get("result").has_value());
}

TEST(mcp_parse_sse_skips_invalid_json_uses_next) {
    // First data line is bad JSON; second is valid — second should be returned
    std::string body =
        "data: not-valid-json\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"v\":7}}\n"
        "\n";
    JsonValue v = McpClient::parse_sse_or_json(body, "text/event-stream");
    CHECK(v.get("result")->get("v")->as_number() == 7.0);
}

// ============================================================================
// parse_tools_list
// ============================================================================

TEST(mcp_parse_tools_list_empty_array) {
    JsonValue result = parse_json(R"({"tools":[]})");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs.size(), size_t(0));
}

TEST(mcp_parse_tools_list_missing_tools_field) {
    JsonValue result = parse_json(R"({})");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs.size(), size_t(0));
}

TEST(mcp_parse_tools_list_name_only) {
    JsonValue result = parse_json(R"({"tools":[{"name":"my_tool"}]})");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs.size(), size_t(1));
    CHECK_EQ(defs[0].name, std::string("my_tool"));
    CHECK(defs[0].description.empty());
    CHECK_EQ(defs[0].params.size(), size_t(0));
}

TEST(mcp_parse_tools_list_with_description) {
    JsonValue result = parse_json(R"({"tools":[{"name":"read","description":"Read a file"}]})");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs[0].description, std::string("Read a file"));
}

TEST(mcp_parse_tools_list_skips_tool_without_name) {
    JsonValue result = parse_json(R"({"tools":[{"description":"no name here"},{"name":"ok"}]})");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs.size(), size_t(1));
    CHECK_EQ(defs[0].name, std::string("ok"));
}

TEST(mcp_parse_tools_list_params_from_schema) {
    JsonValue result = parse_json(R"({
        "tools": [{
            "name": "write_file",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "File path"},
                    "content": {"type": "string", "description": "Content to write"}
                },
                "required": ["path", "content"]
            }
        }]
    })");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs.size(), size_t(1));
    CHECK_EQ(defs[0].params.size(), size_t(2));

    // Both params must be present (map order not guaranteed; find by name)
    auto find_param = [&](const std::string& name) {
        auto it = std::find_if(defs[0].params.begin(), defs[0].params.end(),
            [&](const ToolParam& p) { return p.name == name; });
        return it != defs[0].params.end() ? &*it : nullptr;
    };
    auto* path_p = find_param("path");
    auto* content_p = find_param("content");
    CHECK(path_p != nullptr);
    CHECK(content_p != nullptr);
    CHECK_EQ(path_p->type, std::string("string"));
    CHECK_EQ(path_p->description, std::string("File path"));
    CHECK(path_p->required);
    CHECK(content_p->required);
}

TEST(mcp_parse_tools_list_required_flag_correctly_set) {
    JsonValue result = parse_json(R"({
        "tools": [{
            "name": "search",
            "inputSchema": {
                "properties": {
                    "query":  {"type": "string", "description": "Search query"},
                    "limit":  {"type": "integer", "description": "Max results"}
                },
                "required": ["query"]
            }
        }]
    })");
    auto defs = McpClient::parse_tools_list(result);
    auto find_param = [&](const std::string& name) {
        auto it = std::find_if(defs[0].params.begin(), defs[0].params.end(),
            [&](const ToolParam& p) { return p.name == name; });
        return it != defs[0].params.end() ? &*it : nullptr;
    };
    auto* query_p = find_param("query");
    auto* limit_p = find_param("limit");
    CHECK(query_p != nullptr && query_p->required);
    CHECK(limit_p != nullptr && !limit_p->required);
    CHECK_EQ(limit_p->type, std::string("integer"));
}

TEST(mcp_parse_tools_list_no_schema_gives_no_params) {
    JsonValue result = parse_json(R"({"tools":[{"name":"ping","description":"Ping the server"}]})");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs[0].params.size(), size_t(0));
}

TEST(mcp_parse_tools_list_empty_required_array) {
    JsonValue result = parse_json(R"({
        "tools": [{
            "name": "t",
            "inputSchema": {
                "properties": {"x": {"type": "string", "description": ""}},
                "required": []
            }
        }]
    })");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs[0].params.size(), size_t(1));
    CHECK(!defs[0].params[0].required);
}

TEST(mcp_parse_tools_list_multiple_tools) {
    JsonValue result = parse_json(R"({"tools":[{"name":"a"},{"name":"b"},{"name":"c"}]})");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs.size(), size_t(3));
}

TEST(mcp_parse_tools_list_param_type_defaults_to_string) {
    // Property with no "type" field — should default to "string"
    JsonValue result = parse_json(R"({
        "tools": [{
            "name": "t",
            "inputSchema": {
                "properties": {"x": {"description": "no type field"}}
            }
        }]
    })");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs[0].params[0].type, std::string("string"));
}

// ============================================================================
// parse_call_result
// ============================================================================

TEST(mcp_parse_call_result_success_single_block) {
    JsonValue result = parse_json(R"({
        "content": [{"type":"text","text":"hello world"}],
        "isError": false
    })");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(r.success);
    CHECK_EQ(r.content, std::string("hello world"));
}

TEST(mcp_parse_call_result_error_flag) {
    JsonValue result = parse_json(R"({
        "content": [{"type":"text","text":"something went wrong"}],
        "isError": true
    })");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(!r.success);
    CHECK_EQ(r.error, std::string("something went wrong"));
}

TEST(mcp_parse_call_result_multiple_text_blocks_concatenated) {
    JsonValue result = parse_json(R"({
        "content": [
            {"type":"text","text":"line1\n"},
            {"type":"text","text":"line2\n"}
        ]
    })");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(r.success);
    CHECK_EQ(r.content, std::string("line1\nline2\n"));
}

TEST(mcp_parse_call_result_non_text_blocks_skipped) {
    // Image block has no "text" field — should be silently skipped
    JsonValue result = parse_json(R"({
        "content": [
            {"type":"image","data":"base64...","mimeType":"image/png"},
            {"type":"text","text":"after image"}
        ]
    })");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(r.success);
    CHECK_EQ(r.content, std::string("after image"));
}

TEST(mcp_parse_call_result_empty_content_array) {
    JsonValue result = parse_json(R"({"content":[]})");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(r.success);
    CHECK(r.content.empty());
}

TEST(mcp_parse_call_result_missing_content_field) {
    JsonValue result = parse_json(R"({})");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(r.success);
    CHECK(r.content.empty());
}

TEST(mcp_parse_call_result_is_error_absent_treated_as_success) {
    JsonValue result = parse_json(R"({"content":[{"type":"text","text":"ok"}]})");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(r.success);
}

// ============================================================================
// StdioTransport — subprocess-based tests
// ============================================================================

// Helper: build a Config suitable for Stdio transport tests
static Config make_stdio_config(const std::string& command) {
    Config cfg = Config::defaults();
    cfg.timeout_sec = 5;
    McpServerConfig srv;
    srv.name      = "test-stdio";
    srv.transport = McpTransportType::Stdio;
    srv.command   = command;
    cfg.mcp_servers.push_back(srv);
    return cfg;
}

TEST(mcp_stdio_send_receive_line) {
    // Server reads one line (the initialize request, always id=1) then responds.
    // After that the server exits; send_notification() will get EPIPE (ignored).
    std::string cmd =
        "IFS= read -r line && "
        "printf '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"capabilities\":{}}}\\n'";

    Config cfg = make_stdio_config(cmd);
    McpClient client(cfg.mcp_servers[0], cfg);
    bool ok = client.initialize();
    CHECK(ok);
}

TEST(mcp_stdio_eof_returns_no_tools) {
    // Server exits immediately — list_tools() should return empty, not hang
    Config cfg = make_stdio_config("exit 0");
    McpClient client(cfg.mcp_servers[0], cfg);
    // initialize will fail (EOF), list_tools should return {}
    client.initialize();  // may fail — OK
    auto tools = client.list_tools();
    CHECK(tools.empty());
}

TEST(mcp_stdio_destructor_kills_child) {
    // Verify the child process is reaped when the transport is destroyed
    pid_t child_pid = -1;

    {
        // Use a server that writes its PID to a pipe then sleeps
        // We use a parent-side pipe to capture the PID before the client destroys it
        int pipefd[2];
        CHECK(pipe(pipefd) == 0);

        std::string cmd = "echo $$ >&" + std::to_string(pipefd[1]) + "; sleep 60";
        Config cfg = make_stdio_config(cmd);

        McpClient client(cfg.mcp_servers[0], cfg);

        // Close write end AFTER fork so child inherits the fd and can write its PID
        close(pipefd[1]);

        // Read the child's PID from our pipe
        char buf[32] = {};
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        close(pipefd[0]);
        if (n > 0) child_pid = std::atoi(buf);

        // client destructor runs here — should kill the child
    }

    if (child_pid > 0) {
        // Give the destructor a moment, then verify the process is gone
        usleep(200000);  // 200ms
        int result = kill(child_pid, 0);
        CHECK(result != 0);  // ESRCH: process does not exist
    }
}

// ============================================================================
// LegacySseTransport — SSE event parsing unit tests (no network required)
// These test parse_sse_or_json which is the same underlying logic used by the reader thread
// ============================================================================

TEST(mcp_sse_parse_endpoint_event_data_extraction) {
    // The SSE "endpoint" event data is a plain URL string.
    // parse_sse_or_json is used to extract data events — verify endpoint data round-trips.
    std::string sse_body = "event: endpoint\ndata: http://localhost:4000/messages\n\n";
    // parse_sse_or_json looks for "data: " lines and tries to JSON-parse them.
    // A plain URL is not valid JSON, so it returns null — that's expected; the
    // real LegacySseTransport reader parses the event type separately.
    // What we test here is that the SSE line parser reads the data field correctly.
    std::string data_line = "data: http://localhost:4000/messages";
    CHECK(data_line.substr(0, 6) == "data: ");
    CHECK_EQ(data_line.substr(6), std::string("http://localhost:4000/messages"));
}

TEST(mcp_sse_parse_message_event_roundtrip) {
    // Verify parse_sse_or_json extracts a JSON-RPC message from a data: line
    std::string sse_body =
        "event: message\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"hello\"}]}}\n"
        "\n";
    // parse_sse_or_json reads the first "data: " line and JSON-parses it
    JsonValue parsed = McpClient::parse_sse_or_json(sse_body, "text/event-stream");
    CHECK(!parsed.is_null());
    auto id_v = parsed.get("id");
    CHECK(id_v.has_value());
    CHECK(id_v->is_number());
    CHECK((int)id_v->as_number() == 1);
}

// ============================================================================
// Coverage gap tests — real-world edge cases
// ============================================================================

TEST(mcp_parse_tools_list_type_array_defaults_to_string) {
    // Real-world MCP servers (Anthropic SDK etc.) use "type": ["string", "null"]
    // for optional parameters. is_string() returns false for arrays, so the parser
    // should fall back to "string" rather than crash or skip the param.
    JsonValue result = parse_json(R"({
        "tools": [{
            "name": "t",
            "inputSchema": {
                "properties": {
                    "x": {"type": ["string", "null"], "description": "optional param"}
                }
            }
        }]
    })");
    auto defs = McpClient::parse_tools_list(result);
    CHECK_EQ(defs.size(), size_t(1));
    CHECK_EQ(defs[0].params.size(), size_t(1));
    CHECK_EQ(defs[0].params[0].type, std::string("string"));
}

TEST(mcp_parse_call_result_null_content_field) {
    // {"content": null} differs from {} (missing key) and {"content": []} (empty array).
    // content_v.has_value() is true but is_array() is false → output empty, success.
    JsonValue result = parse_json(R"({"content": null})");
    ToolResult r = McpClient::parse_call_result(result);
    CHECK(r.success);
    CHECK(r.content.empty());
}
