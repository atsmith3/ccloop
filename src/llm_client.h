#pragma once

#include <memory>
#include <vector>
#include "types.h"
#include "config.h"
#include "connector.h"

class ContextManager;

class LlmClient {
public:
    explicit LlmClient(const Config& config);

    LlmResponse complete(const ContextManager& context,
                         const std::vector<ToolDef>& tools);

private:
    std::unique_ptr<Connector> connector_;
};
