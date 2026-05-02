#pragma once

#include <memory>
#include <vector>
#include "types.h"

// LLM connector type — lives here so new connectors don't require editing types.h
enum class ConnectorType {
    OpenAiJson, // OpenAI completions + JSON tools array in request
    Bedrock,    // AWS Bedrock Converse API + SigV4
};

class ContextManager;
struct Config;

class Connector {
public:
    virtual ~Connector() = default;
    virtual LlmResponse complete(const ContextManager& ctx,
                                 const std::vector<ToolDef>& tools) = 0;
};

std::unique_ptr<Connector> make_connector(const Config& cfg);
