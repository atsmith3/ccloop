#include "llm_client.h"
#include "context.h"
#include "json.h"
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>

// ============================================================================
// CURL write callback
// ============================================================================

size_t LlmClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    std::string* response = static_cast<std::string*>(userdata);
    response->append(ptr, realsize);
    return realsize;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

LlmClient::LlmClient(const Config& config)
    : config_(config), curl_(curl_easy_init()) {}

LlmClient::~LlmClient() {
    if (curl_) curl_easy_cleanup(curl_);
}

// ============================================================================
// Request building
// ============================================================================

std::string LlmClient::build_request_json(const ContextManager& ctx,
                                          const std::vector<ToolDef>& tools,
                                          const Config& cfg,
                                          bool stream) {
    std::ostringstream ss;
    ss << "{"
       << "\"model\":" << "\"" << cfg.model << "\""
       << ",\"messages\":" << ctx.to_json()
       << ",\"max_tokens\":" << cfg.max_tokens
       << ",\"temperature\":" << cfg.temperature
       << ",\"stream\":" << (stream ? "true" : "false");

    // Tools array
    if (!tools.empty()) {
        ss << ",\"tools\":[";
        for (size_t i = 0; i < tools.size(); ++i) {
            if (i > 0) ss << ",";
            const auto& tool = tools[i];

            ss << "{"
               << "\"type\":\"function\","
               << "\"function\":{"
               << "\"name\":\"" << tool.name << "\""
               << ",\"description\":\"" << tool.description << "\""
               << ",\"parameters\":{"
               << "\"type\":\"object\","
               << "\"properties\":{";

            for (size_t j = 0; j < tool.params.size(); ++j) {
                if (j > 0) ss << ",";
                const auto& param = tool.params[j];
                ss << "\"" << param.name << "\":"
                   << "{\"type\":\"" << param.type << "\""
                   << ",\"description\":\"" << param.description << "\""
                   << "}";
            }

            ss << "},"
               << "\"required\":[";
            bool first = true;
            for (const auto& param : tool.params) {
                if (param.required) {
                    if (!first) ss << ",";
                    ss << "\"" << param.name << "\"";
                    first = false;
                }
            }
            ss << "]"
               << "}"
               << "}"
               << "}";
        }
        ss << "]";
    }

    ss << "}";
    return ss.str();
}

// ============================================================================
// Response parsing
// ============================================================================

LlmResponse LlmClient::parse_response_json(const std::string& body) {
    LlmResponse response;

    try {
        JsonValue root = parse_json(body);

        // Extract content
        auto choices_opt = root.get("choices");
        if (choices_opt.has_value() && choices_opt->is_array()) {
            const auto& arr = choices_opt->as_array();
            if (!arr.empty()) {
                auto choice0 = arr[0];
                auto msg_opt = choice0->get("message");
                if (msg_opt.has_value()) {
                    auto content_opt = msg_opt->get("content");
                    if (content_opt.has_value() && content_opt->is_string()) {
                        response.content = content_opt->as_string();
                    }

                    // Extract tool calls
                    auto tool_calls_arr_opt = msg_opt->get("tool_calls");
                    if (tool_calls_arr_opt.has_value() && tool_calls_arr_opt->is_array()) {
                        const auto& tool_arr = tool_calls_arr_opt->as_array();
                        for (const auto& tc_ptr : tool_arr) {
                            auto id_opt = tc_ptr->get("id");
                            auto function_opt = tc_ptr->get("function");
                            if (id_opt.has_value() && function_opt.has_value()) {
                                auto name_opt = function_opt->get("name");
                                auto args_opt = function_opt->get("arguments");
                                if (name_opt.has_value() && args_opt.has_value()) {
                                    ToolCall call;
                                    call.id = id_opt->as_string();
                                    call.name = name_opt->as_string();

                                    // Parse arguments JSON string
                                    try {
                                        if (args_opt->is_string()) {
                                            JsonValue args_val = parse_json(args_opt->as_string());
                                            if (args_val.is_object()) {
                                                for (const auto& [key, val] : args_val.as_object()) {
                                                    call.args[key] = *val;
                                                }
                                            }
                                        }
                                    } catch (...) {
                                        // Ignore parse errors
                                    }

                                    response.tool_calls.push_back(call);
                                }
                            }
                        }
                    }
                }

                // Extract finish_reason
                auto finish_reason_opt = choice0->get("finish_reason");
                if (finish_reason_opt.has_value() && finish_reason_opt->is_string()) {
                    response.finish_reason = finish_reason_opt->as_string();
                }
            }
        }

        // Extract usage
        auto usage_opt = root.get("usage");
        if (usage_opt.has_value()) {
            auto prompt_tokens_opt = usage_opt->get("prompt_tokens");
            if (prompt_tokens_opt.has_value()) {
                response.usage.prompt_tokens = static_cast<size_t>(prompt_tokens_opt->as_number());
            }
            auto completion_tokens_opt = usage_opt->get("completion_tokens");
            if (completion_tokens_opt.has_value()) {
                response.usage.completion_tokens = static_cast<size_t>(completion_tokens_opt->as_number());
            }
            auto total_tokens_opt = usage_opt->get("total_tokens");
            if (total_tokens_opt.has_value()) {
                response.usage.total_tokens = static_cast<size_t>(total_tokens_opt->as_number());
            }
        }
    } catch (...) {
        // On parse error, return empty response
    }

    return response;
}

// ============================================================================
// Retry logic
// ============================================================================

bool LlmClient::is_retryable_status(int http_status) {
    return http_status == 429 || (http_status >= 500 && http_status < 600);
}

LlmClient::HttpResult LlmClient::send_with_retry(const std::string& request_body) {
    if (!curl_) return HttpResult{500, "CURL not initialized"};

    std::string response_body;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string auth_header = "Authorization: Bearer " + config_.api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl_, CURLOPT_URL, (config_.endpoint + "/chat/completions").c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, static_cast<void*>(&response_body));
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, static_cast<long>(config_.timeout_sec));

    int retry_count = 0;
    int sleep_ms[] = {1000, 2000, 4000};

    while (retry_count <= config_.max_retries) {
        response_body.clear();

        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            return HttpResult{500, std::string(curl_easy_strerror(res))};
        }

        long http_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

        if (!is_retryable_status(http_code)) {
            curl_slist_free_all(headers);
            return HttpResult{static_cast<int>(http_code), response_body};
        }

        if (retry_count < config_.max_retries) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(sleep_ms[retry_count]));
        }
        ++retry_count;
    }

    curl_slist_free_all(headers);
    return HttpResult{500, "Max retries exceeded"};
}

// ============================================================================
// Main API
// ============================================================================

LlmResponse LlmClient::complete(const ContextManager& context,
                                const std::vector<ToolDef>& tools) {
    std::string request_body = build_request_json(context, tools, config_, false);
    HttpResult http_result = send_with_retry(request_body);

    if (http_result.status != 200) {
        LlmResponse response;
        response.content = "[ERROR] HTTP " + std::to_string(http_result.status);
        return response;
    }

    return parse_response_json(http_result.body);
}
