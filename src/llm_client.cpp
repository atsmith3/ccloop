#include "llm_client.h"
#include "context.h"
#include "json.h"
#include <iostream>
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
                                          const Config& cfg) {
    std::ostringstream ss;
    ss << "{"
       << "\"model\":" << "\"" << escape_json(cfg.model) << "\""
       << ",\"messages\":" << ctx.to_json()
       << ",\"max_tokens\":" << cfg.max_tokens
       << ",\"temperature\":" << cfg.temperature
       << ",\"enable_thinking\":" << (cfg.enable_thinking ? "true" : "false")
       << "}";
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
                        response.tool_calls = parse_xml_tool_calls(response.content);
                    }
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
    } catch (const std::exception& e) {
        response.is_error = true;
        response.content  = std::string("Response parse failed: ") + e.what();
        return response;
    } catch (...) {
        response.is_error = true;
        response.content  = "Response parse failed: unknown error";
        return response;
    }

    return response;
}

// ============================================================================
// XML tool call parsing
// ============================================================================

std::vector<ToolCall> LlmClient::parse_xml_tool_calls(const std::string& content) {
    std::vector<ToolCall> result;
    int call_index = 0;
    size_t pos = 0;

    while (pos < content.size()) {
        // Find next opening tag (not a closing tag)
        size_t tag_open = content.find('<', pos);
        if (tag_open == std::string::npos) break;

        // Skip closing tags
        if (tag_open + 1 < content.size() && content[tag_open + 1] == '/') {
            pos = tag_open + 1;
            continue;
        }

        // Extract tag name (up to '>' or space, no '/')
        size_t tag_name_end = tag_open + 1;
        while (tag_name_end < content.size() &&
               content[tag_name_end] != '>' &&
               content[tag_name_end] != ' ' &&
               content[tag_name_end] != '/') {
            ++tag_name_end;
        }
        if (tag_name_end >= content.size() || content[tag_name_end] != '>') {
            pos = tag_open + 1;
            continue;
        }

        std::string tag_name = content.substr(tag_open + 1, tag_name_end - tag_open - 1);
        if (tag_name.empty()) { pos = tag_open + 1; continue; }

        // Find matching closing tag
        std::string close_tag = "</" + tag_name + ">";
        size_t block_start = tag_name_end + 1;
        size_t close_pos = content.find(close_tag, block_start);
        if (close_pos == std::string::npos) { pos = tag_open + 1; continue; }

        std::string block = content.substr(block_start, close_pos - block_start);

        // Extract <key>value</key> parameter pairs from block
        ToolCall call;
        call.id   = "xml_" + std::to_string(call_index++);
        call.name = tag_name;

        size_t p_pos = 0;
        while (p_pos < block.size()) {
            size_t param_open = block.find('<', p_pos);
            if (param_open == std::string::npos) break;
            if (param_open + 1 < block.size() && block[param_open + 1] == '/') {
                p_pos = param_open + 1; continue;
            }

            size_t key_end = param_open + 1;
            while (key_end < block.size() && block[key_end] != '>' && block[key_end] != ' ') ++key_end;
            if (key_end >= block.size() || block[key_end] != '>') { p_pos = param_open + 1; continue; }

            std::string key = block.substr(param_open + 1, key_end - param_open - 1);
            if (key.empty()) { p_pos = param_open + 1; continue; }

            std::string end_tag = "</" + key + ">";
            size_t val_start = key_end + 1;
            size_t val_end   = block.find(end_tag, val_start);
            if (val_end == std::string::npos) { p_pos = param_open + 1; continue; }

            std::string val = block.substr(val_start, val_end - val_start);
            // Trim whitespace
            size_t vs = val.find_first_not_of(" \t\r\n");
            size_t ve = val.find_last_not_of(" \t\r\n");
            val = (vs != std::string::npos) ? val.substr(vs, ve - vs + 1) : "";

            JsonValue jval; jval.data = val;
            call.args[key] = jval;
            p_pos = val_end + end_tag.size();
        }

        result.push_back(call);
        pos = close_pos + close_tag.size();
    }

    return result;
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
            curl_slist_free_all(headers);  // fix #3: free headers on transport error
            return HttpResult{500, std::string(curl_easy_strerror(res))};
        }

        long http_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

        if (!is_retryable_status(static_cast<int>(http_code))) {
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
                                const std::vector<ToolDef>& /*tools*/) {
    std::string request_body = build_request_json(context, config_);
    HttpResult http_result = send_with_retry(request_body);

    if (config_.debug) {
        std::cerr << "[debug] HTTP " << http_result.status << " response:\n"
                  << http_result.body << "\n";
    }

    if (http_result.status != 200) {
        LlmResponse response;
        response.is_error = true;
        response.content = "HTTP " + std::to_string(http_result.status)
                         + ": " + http_result.body.substr(0, 200);
        return response;
    }

    return parse_response_json(http_result.body);
}

