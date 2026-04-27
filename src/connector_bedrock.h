#pragma once

#include "connector_base.h"
#include "context.h"

class BedrockConnector : public ConnectorBase {
public:
    explicit BedrockConnector(const Config& cfg);

    LlmResponse complete(const ContextManager& ctx,
                         const std::vector<ToolDef>& tools) override;

    // Static helpers exposed for unit testing (no CURL needed)
    static std::string build_request_json(const ContextManager& ctx,
                                          const Config& cfg,
                                          const std::vector<ToolDef>& tools);
    static LlmResponse parse_response_json(const std::string& body);

private:
    std::string userpwd_;  // kept alive for the duration of the curl handle
};
