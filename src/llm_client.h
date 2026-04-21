#pragma once

#include <string>
#include <vector>
#include <curl/curl.h>
#include "types.h"
#include "config.h"

class ContextManager;

class LlmClient {
public:
    explicit LlmClient(const Config& config);
    ~LlmClient();

    LlmResponse complete(const ContextManager& context,
                         const std::vector<ToolDef>& tools);

    // Static helpers exposed for unit testing (no CURL needed)
    static std::string  build_request_json(const ContextManager& ctx,
                                           const std::vector<ToolDef>& tools,
                                           const Config& cfg);
    static LlmResponse  parse_response_json(const std::string& body);
    static bool         is_retryable_status(int http_status);
    static std::vector<ToolCall> parse_hermes_tool_calls(const std::string& text);

private:
    Config  config_;
    CURL*   curl_ = nullptr;

    struct HttpResult { int status; std::string body; };

    HttpResult send_with_retry(const std::string& request_body);

    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};
