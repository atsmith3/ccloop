#include "connector_bedrock.h"
#include "json.h"
#include <sstream>

BedrockConnector::BedrockConnector(const Config& cfg)
    : ConnectorBase(cfg)
{
    // Configure SigV4 signing on the persistent CURL handle.
    // libcurl handles all signing, date headers, and payload hash transparently.
    if (curl_) {
        std::string sigv4_param = "aws:amz:" + cfg_.aws_region + ":bedrock-runtime";
        curl_easy_setopt(curl_, CURLOPT_AWS_SIGV4, sigv4_param.c_str());
        std::string userpwd = cfg_.aws_access_key + ":" + cfg_.aws_secret_key;
        curl_easy_setopt(curl_, CURLOPT_USERPWD, userpwd.c_str());
    }
}

std::string BedrockConnector::build_request_json(const ContextManager& ctx,
                                                 const Config& cfg,
                                                 const std::vector<ToolDef>& tools) {
    // Parse context messages from JSON (avoids adding a messages() accessor to ContextManager)
    JsonValue msgs_json = parse_json(ctx.to_json());

    std::string system_text;
    std::ostringstream messages_ss;
    bool first_msg = true;

    if (msgs_json.is_array()) {
        for (const auto& msg_ptr : msgs_json.as_array()) {
            auto role_opt    = msg_ptr->get("role");
            auto content_opt = msg_ptr->get("content");
            if (!role_opt.has_value() || !content_opt.has_value()) continue;

            std::string role    = role_opt->as_string();
            std::string content = content_opt->as_string();

            if (role == "system") {
                system_text = content;
                continue;
            }

            if (!first_msg) messages_ss << ",";
            messages_ss << "{\"role\":\"" << escape_json(role) << "\""
                        << ",\"content\":[{\"text\":\"" << escape_json(content) << "\"}]}";
            first_msg = false;
        }
    }

    std::ostringstream ss;
    ss << "{";

    if (!system_text.empty()) {
        ss << "\"system\":[{\"text\":\"" << escape_json(system_text) << "\"}],";
    }

    ss << "\"messages\":[" << messages_ss.str() << "]"
       << ",\"inferenceConfig\":{"
       << "\"maxTokens\":" << cfg.max_tokens
       << ",\"temperature\":" << cfg.temperature
       << "}";

    if (!tools.empty()) {
        ss << ",\"toolConfig\":{\"tools\":[";
        for (size_t i = 0; i < tools.size(); ++i) {
            if (i > 0) ss << ",";
            const auto& t = tools[i];
            ss << "{\"toolSpec\":{"
               << "\"name\":\"" << escape_json(t.name) << "\""
               << ",\"description\":\"" << escape_json(t.description) << "\""
               << ",\"inputSchema\":{\"json\":" << build_tool_params_json(t.params) << "}"
               << "}}";
        }
        ss << "]}";
    }

    ss << "}";
    return ss.str();
}

LlmResponse BedrockConnector::parse_response_json(const std::string& body) {
    LlmResponse response;

    try {
        JsonValue root = parse_json(body);

        // output.message.content is an array of content blocks
        auto output_opt = root.get("output");
        if (output_opt.has_value()) {
            auto msg_opt = output_opt->get("message");
            if (msg_opt.has_value()) {
                auto content_opt = msg_opt->get("content");
                if (content_opt.has_value() && content_opt->is_array()) {
                    int tc_idx = 0;
                    std::ostringstream text_acc;

                    for (const auto& block_ptr : content_opt->as_array()) {
                        // Text block
                        auto text_opt = block_ptr->get("text");
                        if (text_opt.has_value() && text_opt->is_string()) {
                            if (!text_acc.str().empty()) text_acc << "\n";
                            text_acc << text_opt->as_string();
                            continue;
                        }

                        // Tool use block
                        auto tool_use_opt = block_ptr->get("toolUse");
                        if (tool_use_opt.has_value()) {
                            ToolCall call;
                            auto id_opt   = tool_use_opt->get("toolUseId");
                            auto name_opt = tool_use_opt->get("name");
                            auto input_opt = tool_use_opt->get("input");

                            call.id   = id_opt.has_value()   ? id_opt->as_string()
                                                             : "bedrock_" + std::to_string(tc_idx);
                            call.name = name_opt.has_value() ? name_opt->as_string() : "";
                            if (input_opt.has_value() && input_opt->is_object()) {
                                call.args = json_obj_to_args(input_opt->as_object());
                            }
                            response.tool_calls.push_back(std::move(call));
                            ++tc_idx;
                        }
                    }
                    response.content = text_acc.str();
                }
            }
        }

        // usage: inputTokens / outputTokens / totalTokens
        auto usage_opt = root.get("usage");
        if (usage_opt.has_value()) {
            auto it = usage_opt->get("inputTokens");
            if (it.has_value()) response.usage.prompt_tokens = static_cast<size_t>(it->as_number());
            auto ot = usage_opt->get("outputTokens");
            if (ot.has_value()) response.usage.completion_tokens = static_cast<size_t>(ot->as_number());
            auto tt = usage_opt->get("totalTokens");
            if (tt.has_value()) response.usage.total_tokens = static_cast<size_t>(tt->as_number());
            auto cr = usage_opt->get("cacheReadInputTokenCount");
            if (cr.has_value()) response.usage.cache_read_tokens = static_cast<size_t>(cr->as_number());
            auto cw = usage_opt->get("cacheWriteInputTokenCount");
            if (cw.has_value()) response.usage.cache_write_tokens = static_cast<size_t>(cw->as_number());
        }
        if (response.usage.total_tokens == 0) {
            response.usage.total_tokens = response.usage.prompt_tokens
                                        + response.usage.completion_tokens;
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

LlmResponse BedrockConnector::complete(const ContextManager& ctx,
                                       const std::vector<ToolDef>& tools) {
    std::string url = "https://bedrock-runtime." + cfg_.aws_region
                    + ".amazonaws.com/model/" + cfg_.model + "/converse";
    std::string body = build_request_json(ctx, cfg_, tools);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    HttpResult result = http_post(url, body, headers);
    curl_slist_free_all(headers);

    if (result.status != 200) {
        return make_http_error(result);
    }

    return parse_response_json(result.body);
}
