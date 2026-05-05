#include "connector_openai.h"
#include "json.h"
#include <sstream>

OpenAiConnector::OpenAiConnector(const Config& cfg)
    : ConnectorBase(cfg) {}

std::string OpenAiConnector::build_request_json(const ContextManager& ctx,
                                                const Config& cfg,
                                                const std::vector<ToolDef>& tools) {
    std::ostringstream ss;
    ss << "{"
       << "\"model\":\"" << escape_json(cfg.model) << "\""
       << ",\"messages\":" << ctx.to_json()
       << ",\"max_tokens\":" << cfg.max_tokens
       << ",\"temperature\":" << cfg.temperature;

    if (!tools.empty()) {
        ss << ",\"tools\":[";
        for (size_t i = 0; i < tools.size(); ++i) {
            if (i > 0) ss << ",";
            const auto& t = tools[i];
            ss << "{\"type\":\"function\",\"function\":{"
               << "\"name\":\"" << escape_json(t.name) << "\""
               << ",\"description\":\"" << escape_json(t.description) << "\""
               << ",\"parameters\":" << build_tool_params_json(t.params)
               << "}}";
        }
        ss << "]";
    }

    ss << "}";
    return ss.str();
}

LlmResponse OpenAiConnector::parse_response_json(const std::string& body) {
    LlmResponse response;

    try {
        JsonValue root = parse_json(body);

        auto choices_opt = root.get("choices");
        if (choices_opt.has_value() && choices_opt->is_array()) {
            const auto& arr = choices_opt->as_array();
            if (!arr.empty()) {
                auto msg_opt = arr[0]->get("message");
                if (msg_opt.has_value()) {
                    // Text content (may be null when tool_calls present)
                    auto content_opt = msg_opt->get("content");
                    if (content_opt.has_value() && content_opt->is_string()) {
                        response.content = content_opt->as_string();
                    }

                    // Native JSON tool calls
                    auto tc_opt = msg_opt->get("tool_calls");
                    if (tc_opt.has_value() && tc_opt->is_array()) {
                        int idx = 0;
                        for (const auto& tc_ptr : tc_opt->as_array()) {
                            auto id_opt   = tc_ptr->get("id");
                            auto fn_opt   = tc_ptr->get("function");
                            if (!fn_opt.has_value()) { ++idx; continue; }

                            auto name_opt = fn_opt->get("name");
                            auto args_opt = fn_opt->get("arguments");
                            if (!name_opt.has_value()) { ++idx; continue; }

                            ToolCall call;
                            call.id   = id_opt.has_value() ? id_opt->as_string()
                                                           : make_fallback_call_id(idx);
                            call.name = name_opt->as_string();

                            // arguments is a JSON-encoded string — parse it
                            if (args_opt.has_value() && args_opt->is_string()) {
                                try {
                                    JsonValue args_json = parse_json(args_opt->as_string());
                                    if (args_json.is_object()) {
                                        call.args = json_obj_to_args(args_json.as_object());
                                    }
                                } catch (...) {
                                    // Ignore malformed arguments; tool gets empty args
                                }
                            }

                            response.tool_calls.push_back(std::move(call));
                            ++idx;
                        }
                    }
                }
            }
        }

        auto usage_opt = root.get("usage");
        if (usage_opt.has_value()) {
            auto pt = usage_opt->get("prompt_tokens");
            if (pt.has_value()) response.usage.prompt_tokens = static_cast<size_t>(pt->as_number());
            auto ct = usage_opt->get("completion_tokens");
            if (ct.has_value()) response.usage.completion_tokens = static_cast<size_t>(ct->as_number());
            auto tt = usage_opt->get("total_tokens");
            if (tt.has_value()) response.usage.total_tokens = static_cast<size_t>(tt->as_number());
            auto ptd = usage_opt->get("prompt_tokens_details");
            if (ptd.has_value()) {
                auto cr = ptd->get("cached_tokens");
                if (cr.has_value()) response.usage.cache_read_tokens = static_cast<size_t>(cr->as_number());
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

LlmResponse OpenAiConnector::complete(const ContextManager& ctx,
                                      const std::vector<ToolDef>& tools) {
    std::string url  = cfg_.endpoint + "/chat/completions";
    std::string body = build_request_json(ctx, cfg_, tools);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string key = cfg_.api_key;
    if (key.rfind("Bearer ", 0) == 0) key = key.substr(7);
    std::string auth = "Authorization: Bearer " + key;
    headers = curl_slist_append(headers, auth.c_str());

    HttpResult result = http_post(url, body, headers);
    curl_slist_free_all(headers);

    if (result.status < 200 || result.status >= 300) {
        return make_http_error(result);
    }

    return parse_response_json(result.body);
}
