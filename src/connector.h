#pragma once

#include <memory>
#include <vector>
#include "types.h"

class ContextManager;
struct Config;

class Connector {
public:
    virtual ~Connector() = default;
    virtual LlmResponse complete(const ContextManager& ctx,
                                 const std::vector<ToolDef>& tools) = 0;
};

std::unique_ptr<Connector> make_connector(const Config& cfg);
