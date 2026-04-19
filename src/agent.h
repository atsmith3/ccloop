#pragma once

#include <atomic>
#include "config.h"
#include "context.h"
#include "tools.h"
#include "llm_client.h"
#include "ui.h"

extern std::atomic<bool> should_exit;

class Agent {
public:
    Agent(Config config, Ui& ui);
    void run();

private:
    Config         config_;
    AgentMode      mode_ = AgentMode::Explore;
    ContextManager context_;
    LlmClient      llm_;
    ToolRegistry   registry_;
    Ui&            ui_;

    void loop();
    void handle_tool_calls(const std::vector<ToolCall>& calls);
    bool handle_slash_command(std::string_view input);
    void transition_to(AgentMode next);
    void rebuild_registry();
    std::string system_prompt() const;
};
