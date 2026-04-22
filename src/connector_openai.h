#pragma once

#include "connector_base.h"
#include "context.h"

class OpenAiConnector : public ConnectorBase {
public:
    explicit OpenAiConnector(const Config& cfg);

    LlmResponse complete(const ContextManager& ctx,
                         const std::vector<ToolDef>& tools) override;

    // Static helpers exposed for unit testing (no CURL needed)
    static std::string build_request_json(const ContextManager& ctx,
                                          const Config& cfg,
                                          const std::vector<ToolDef>& tools);
    static LlmResponse parse_response_json(const std::string& body);
};
