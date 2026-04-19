#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
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

    LlmResponse complete_streaming(const ContextManager& context,
                                   const std::vector<ToolDef>& tools,
                                   std::function<void(std::string_view chunk)> on_chunk);

    // Static helpers exposed for unit testing (no CURL needed)
    static std::string  build_request_json(const ContextManager& ctx,
                                           const std::vector<ToolDef>& tools,
                                           const Config& cfg,
                                           bool stream);
    static LlmResponse  parse_response_json(const std::string& body);
    static bool         is_retryable_status(int http_status);
    static std::optional<std::string> extract_sse_data(const std::string& line);
    static void accumulate_sse_chunk(LlmResponse& resp,
                                     const std::string& data_json,
                                     const std::function<void(std::string_view)>& on_chunk);
    static void finalize_streaming_tool_calls(LlmResponse& resp);

private:
    Config  config_;
    CURL*   curl_ = nullptr;

    struct HttpResult { int status; std::string body; };
    struct StreamState {
        LlmResponse response;
        std::string line_buf;
        std::function<void(std::string_view)> on_chunk;
    };

    HttpResult send_with_retry(const std::string& request_body);
    HttpResult send_streaming(const std::string& request_body, StreamState& state);

    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t stream_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};
