#include "connector_qwen.h"
#include "json.h"
#include <sstream>

QwenConnector::QwenConnector(const Config& cfg)
    : ConnectorBase(cfg) {}

std::string QwenConnector::build_request_json(const ContextManager& ctx,
                                              const Config& cfg) {
    std::ostringstream ss;
    ss << "{"
       << "\"model\":" << "\"" << escape_json(cfg.model) << "\""
       << ",\"messages\":" << ctx.to_json()
       << ",\"max_tokens\":" << cfg.max_tokens
       << ",\"temperature\":" << cfg.temperature
       << "}";
    return ss.str();
}

LlmResponse QwenConnector::parse_response_json(const std::string& body) {
    LlmResponse response;

    try {
        JsonValue root = parse_json(body);

        auto choices_opt = root.get("choices");
        if (choices_opt.has_value() && choices_opt->is_array()) {
            const auto& arr = choices_opt->as_array();
            if (!arr.empty()) {
                auto msg_opt = arr[0]->get("message");
                if (msg_opt.has_value()) {
                    auto content_opt = msg_opt->get("content");
                    if (content_opt.has_value() && content_opt->is_string()) {
                        response.content = content_opt->as_string();
                        response.tool_calls = parse_xml_tool_calls(response.content);
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

std::vector<ToolCall> QwenConnector::parse_xml_tool_calls(const std::string& content) {
    std::vector<ToolCall> result;
    int call_index = 0;
    size_t pos = 0;

    while (pos < content.size()) {
        size_t tag_open = content.find('<', pos);
        if (tag_open == std::string::npos) break;

        if (tag_open + 1 < content.size() && content[tag_open + 1] == '/') {
            pos = tag_open + 1;
            continue;
        }

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

        std::string close_tag = "</" + tag_name + ">";
        size_t block_start = tag_name_end + 1;
        size_t close_pos = content.find(close_tag, block_start);
        if (close_pos == std::string::npos) { pos = tag_open + 1; continue; }

        std::string block = content.substr(block_start, close_pos - block_start);

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

LlmResponse QwenConnector::complete(const ContextManager& ctx,
                                    const std::vector<ToolDef>& /*tools*/) {
    std::string url = cfg_.endpoint + "/chat/completions";
    std::string body = build_request_json(ctx, cfg_);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + cfg_.api_key;
    headers = curl_slist_append(headers, auth.c_str());

    HttpResult result = http_post(url, body, headers);
    curl_slist_free_all(headers);

    if (result.status != 200) {
        return make_http_error(result);
    }

    return parse_response_json(result.body);
}
