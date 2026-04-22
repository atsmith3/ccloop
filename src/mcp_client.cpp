#include "mcp_client.h"
#include <iostream>
#include <sstream>
#include <unordered_set>

McpClient::McpClient(const McpServerConfig& server, const Config& cfg)
    : server_(server), cfg_(cfg), curl_(curl_easy_init()) {}

McpClient::~McpClient() {
    if (curl_) curl_easy_cleanup(curl_);
}

size_t McpClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, realsize);
    return realsize;
}

// Build a JSON-RPC 2.0 request body. Omits "params" when params.is_null().
std::string McpClient::build_rpc(int id, const std::string& method, const JsonValue& params) {
    std::ostringstream ss;
    ss << "{\"jsonrpc\":\"2.0\",\"id\":" << id
       << ",\"method\":\"" << escape_json(method) << "\"";
    if (!params.is_null()) {
        ss << ",\"params\":" << to_json(params);
    }
    ss << "}";
    return ss.str();
}

// Build a JSON-RPC 2.0 notification (no id, no response expected).
std::string McpClient::build_notification(const std::string& method) {
    return "{\"jsonrpc\":\"2.0\",\"method\":\"" + escape_json(method) + "\"}";
}

// Parse a response body as JSON or SSE (text/event-stream).
// For SSE, returns the first valid data event. Returns null JsonValue on failure.
JsonValue McpClient::parse_sse_or_json(const std::string& body, const std::string& content_type) {
    if (content_type.find("text/event-stream") != std::string::npos) {
        std::istringstream stream(body);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() >= 6 && line.substr(0, 6) == "data: ") {
                std::string data = line.substr(6);
                if (!data.empty() && data != "[DONE]") {
                    try { return parse_json(data); } catch (...) {}
                }
            }
        }
        return JsonValue{};
    }
    try { return parse_json(body); } catch (...) { return JsonValue{}; }
}

// Parse response headers looking for Mcp-Session-Id. Stores value in *userdata (std::string*).
size_t McpClient::header_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t len = size * nmemb;
    std::string line(ptr, len);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    const std::string prefix = "mcp-session-id:";
    std::string lower = line;
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (lower.size() >= prefix.size() && lower.substr(0, prefix.size()) == prefix) {
        std::string val = line.substr(prefix.size());
        size_t start = val.find_first_not_of(" \t");
        if (start != std::string::npos) val = val.substr(start);
        *static_cast<std::string*>(userdata) = val;
    }
    return len;
}

// POST a JSON-RPC request. Returns the "result" value on success, nullopt on error.
std::optional<JsonValue> McpClient::send_rpc(const std::string& method, const JsonValue& params) {
    if (!curl_) return std::nullopt;
    curl_easy_reset(curl_);

    int id = next_id_++;
    std::string body = build_rpc(id, method, params);
    std::string response_body;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");
    if (!session_id_.empty()) {
        std::string sid = "Mcp-Session-Id: " + session_id_;
        headers = curl_slist_append(headers, sid.c_str());
    }
    if (!server_.api_key.empty()) {
        std::string auth = "Authorization: Bearer " + server_.api_key;
        headers = curl_slist_append(headers, auth.c_str());
    }

    std::string captured_session_id;
    curl_easy_setopt(curl_, CURLOPT_URL,            server_.url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,      body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,   static_cast<long>(body.size()));
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,      headers);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,   write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA,       static_cast<void*>(&response_body));
    curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION,  header_callback);
    curl_easy_setopt(curl_, CURLOPT_HEADERDATA,      static_cast<void*>(&captured_session_id));
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT,         static_cast<long>(cfg_.timeout_sec));

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);
    if (!captured_session_id.empty()) session_id_ = captured_session_id;

    if (res != CURLE_OK) {
        if (cfg_.debug) {
            std::cerr << "[mcp:" << server_.name << "] curl error: "
                      << curl_easy_strerror(res) << "\n";
        }
        return std::nullopt;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        if (cfg_.debug) {
            std::cerr << "[mcp:" << server_.name << "] HTTP " << http_code
                      << ": " << response_body.substr(0, 200) << "\n";
        }
        return std::nullopt;
    }

    char* ct_ptr = nullptr;
    curl_easy_getinfo(curl_, CURLINFO_CONTENT_TYPE, &ct_ptr);
    std::string content_type = ct_ptr ? ct_ptr : "application/json";

    if (cfg_.debug) {
        std::cerr << "[mcp:" << server_.name << "] response: " << response_body << "\n";
    }

    JsonValue resp = parse_sse_or_json(response_body, content_type);

    auto err_v = resp.get("error");
    if (err_v.has_value()) {
        if (cfg_.debug) {
            auto msg = err_v->get("message");
            std::cerr << "[mcp:" << server_.name << "] rpc error: "
                      << (msg && msg->is_string() ? msg->as_string() : "unknown") << "\n";
        }
        return std::nullopt;
    }

    auto result = resp.get("result");
    if (!result.has_value()) return std::nullopt;
    return *result;
}

// POST a notification (no id, no response expected).
void McpClient::send_notification(const std::string& method) {
    if (!curl_) return;
    curl_easy_reset(curl_);

    std::string body = build_notification(method);
    std::string response_body;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!session_id_.empty()) {
        std::string sid = "Mcp-Session-Id: " + session_id_;
        headers = curl_slist_append(headers, sid.c_str());
    }
    if (!server_.api_key.empty()) {
        std::string auth = "Authorization: Bearer " + server_.api_key;
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl_, CURLOPT_URL,           server_.url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body.size()));
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,  write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA,      static_cast<void*>(&response_body));
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT,        static_cast<long>(cfg_.timeout_sec));

    curl_easy_perform(curl_);  // fire-and-forget
    curl_slist_free_all(headers);
}

bool McpClient::initialize() {
    auto params = parse_json(R"({
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "ccl", "version": "1.0"}
    })");

    auto result = send_rpc("initialize", params);
    if (!result) return false;

    send_notification("notifications/initialized");
    return true;
}

std::vector<ToolDef> McpClient::parse_tools_list(const JsonValue& result) {
    auto tools_v = result.get("tools");
    if (!tools_v || !tools_v->is_array()) return {};

    std::vector<ToolDef> defs;
    for (const auto& tool_ptr : tools_v->as_array()) {
        if (!tool_ptr) continue;

        auto name_v = tool_ptr->get("name");
        if (!name_v || !name_v->is_string()) continue;

        ToolDef def;
        def.name = name_v->as_string();

        auto desc_v = tool_ptr->get("description");
        if (desc_v && desc_v->is_string()) def.description = desc_v->as_string();

        // Parse inputSchema for parameter definitions
        auto schema_v = tool_ptr->get("inputSchema");
        if (schema_v) {
            std::unordered_set<std::string> required_set;
            auto req_v = schema_v->get("required");
            if (req_v && req_v->is_array()) {
                for (const auto& r : req_v->as_array()) {
                    if (r && r->is_string()) required_set.insert(r->as_string());
                }
            }

            auto props_v = schema_v->get("properties");
            if (props_v && props_v->is_object()) {
                for (const auto& [pname, pval] : props_v->as_object()) {
                    if (!pval) continue;
                    ToolParam param;
                    param.name     = pname;
                    param.required = required_set.count(pname) > 0;

                    auto ptype = pval->get("type");
                    param.type = (ptype && ptype->is_string()) ? ptype->as_string() : "string";

                    auto pdesc = pval->get("description");
                    param.description = (pdesc && pdesc->is_string()) ? pdesc->as_string() : "";

                    def.params.push_back(std::move(param));
                }
            }
        }

        defs.push_back(std::move(def));
    }
    return defs;
}

ToolResult McpClient::parse_call_result(const JsonValue& result) {
    auto is_error_v = result.get("isError");
    bool is_error = (is_error_v && is_error_v->is_bool() && is_error_v->as_bool());

    std::string output;
    auto content_v = result.get("content");
    if (content_v && content_v->is_array()) {
        for (const auto& item_ptr : content_v->as_array()) {
            if (!item_ptr) continue;
            auto text_v = item_ptr->get("text");
            if (text_v && text_v->is_string()) output += text_v->as_string();
        }
    }

    return is_error ? ToolResult::fail(output) : ToolResult::ok(output);
}

std::vector<ToolDef> McpClient::list_tools() {
    auto result = send_rpc("tools/list");
    if (!result) return {};
    return parse_tools_list(*result);
}

ToolResult McpClient::call_tool(const std::string& name, const ToolArgs& args) {
    // Serialize args map to JSON object string
    std::ostringstream args_json;
    args_json << "{";
    bool first = true;
    for (const auto& [k, v] : args) {
        if (!first) args_json << ",";
        args_json << "\"" << escape_json(k) << "\":" << to_json(v);
        first = false;
    }
    args_json << "}";

    std::string params_str = "{\"name\":\"" + escape_json(name)
                           + "\",\"arguments\":" + args_json.str() + "}";
    JsonValue params = parse_json(params_str);

    auto result = send_rpc("tools/call", params);
    if (!result) {
        return ToolResult::fail("MCP call failed: no response from '" + server_.name + "'");
    }
    return parse_call_result(*result);
}
