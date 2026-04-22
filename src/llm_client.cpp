#include "llm_client.h"

LlmClient::LlmClient(const Config& config)
    : connector_(make_connector(config)) {}

LlmResponse LlmClient::complete(const ContextManager& context,
                                const std::vector<ToolDef>& tools) {
    return connector_->complete(context, tools);
}
