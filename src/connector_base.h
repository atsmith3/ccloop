#pragma once

#include <string>
#include <curl/curl.h>
#include "connector.h"
#include "config.h"
#include "json.h"

class ConnectorBase : public Connector {
public:
    static bool is_retryable_status(int http_status);

protected:
    struct HttpResult { int status; std::string body; };

    Config cfg_;
    CURL*  curl_ = nullptr;

    explicit ConnectorBase(const Config& cfg);
    ~ConnectorBase();

    // POST body to url using given headers slist (caller owns/frees the slist).
    // Retry logic is built in. Auth set either via headers or via curl handle options.
    HttpResult http_post(const std::string& url,
                         const std::string& body,
                         struct curl_slist* headers);

    // Shared helpers for subclass request builders and response parsers
    static std::string build_tool_params_json(const std::vector<ToolParam>& params);
    static ToolArgs     json_obj_to_args(const JsonObject& obj);
    static LlmResponse  make_http_error(const HttpResult& result);
    static std::string  make_fallback_call_id(int index) { return "call_" + std::to_string(index); }

private:
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};
