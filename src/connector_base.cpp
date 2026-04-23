#include "connector_base.h"
#include "agent.h"
#include <chrono>
#include <sstream>
#include <thread>
#include <iostream>

static int interrupt_progress_cb(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return should_interrupt.load() ? 1 : 0;
}

ConnectorBase::ConnectorBase(const Config& cfg)
    : cfg_(cfg), curl_(curl_easy_init()) {}

ConnectorBase::~ConnectorBase() {
    if (curl_) curl_easy_cleanup(curl_);
}

bool ConnectorBase::is_retryable_status(int http_status) {
    return http_status == 429 || (http_status >= 500 && http_status < 600);
}

size_t ConnectorBase::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, realsize);
    return realsize;
}

ConnectorBase::HttpResult ConnectorBase::http_post(
    const std::string& url,
    const std::string& body,
    struct curl_slist* headers)
{
    if (!curl_) return {500, "CURL not initialized"};

    std::string response_body;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, static_cast<void*>(&response_body));
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, static_cast<long>(cfg_.timeout_sec));
    curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, interrupt_progress_cb);

    int sleep_ms[] = {1000, 2000, 4000};
    int retry_count = 0;

    while (retry_count <= cfg_.max_retries) {
        response_body.clear();
        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            return {500, std::string(curl_easy_strerror(res))};
        }

        long http_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

        if (cfg_.debug) {
            std::cerr << "[debug] HTTP " << http_code << " response:\n"
                      << response_body << "\n";
        }

        if (!is_retryable_status(static_cast<int>(http_code))) {
            return {static_cast<int>(http_code), response_body};
        }

        if (retry_count < cfg_.max_retries && !should_interrupt.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(sleep_ms[retry_count]));
        }
        ++retry_count;
    }

    return {500, "Max retries exceeded"};
}

std::string ConnectorBase::build_tool_params_json(const std::vector<ToolParam>& params) {
    std::ostringstream props;
    std::ostringstream required;
    bool first_prop = true;
    bool first_req  = true;

    for (const auto& p : params) {
        if (!first_prop) props << ",";
        props << "\"" << escape_json(p.name) << "\":"
              << "{\"type\":\"" << escape_json(p.type) << "\""
              << ",\"description\":\"" << escape_json(p.description) << "\"}";
        first_prop = false;

        if (p.required) {
            if (!first_req) required << ",";
            required << "\"" << escape_json(p.name) << "\"";
            first_req = false;
        }
    }

    std::ostringstream ss;
    ss << "{\"type\":\"object\",\"properties\":{" << props.str() << "}";
    if (!first_req) {
        ss << ",\"required\":[" << required.str() << "]";
    }
    ss << "}";
    return ss.str();
}

ToolArgs ConnectorBase::json_obj_to_args(const JsonObject& obj) {
    ToolArgs args;
    for (const auto& [k, v] : obj) {
        args[k] = *v;
    }
    return args;
}

LlmResponse ConnectorBase::make_http_error(const HttpResult& result) {
    LlmResponse response;
    response.is_error = true;
    response.content  = "HTTP " + std::to_string(result.status)
                      + ": " + result.body.substr(0, 200);
    return response;
}
