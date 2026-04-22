#pragma once

#include "connector_base.h"
#include "context.h"

class QwenConnector : public ConnectorBase {
public:
    explicit QwenConnector(const Config& cfg);

    LlmResponse complete(const ContextManager& ctx,
                         const std::vector<ToolDef>& tools) override;

    // Static helpers exposed for unit testing (no CURL needed)
    static std::string  build_request_json(const ContextManager& ctx, const Config& cfg);
    static LlmResponse  parse_response_json(const std::string& body);
    static std::vector<ToolCall> parse_xml_tool_calls(const std::string& content);
};
