#pragma once

#include <atomic>
#include "config.h"
#include "context.h"
#include "tools.h"
#include "llm_client.h"
#include "ui.h"

extern std::atomic<bool> should_exit;
extern std::atomic<bool> should_interrupt;

class Agent {
public:
    Agent(Config config, Ui& ui, AgentMode initial_mode = AgentMode::Plan);
    void run(const std::string& initial_prompt = "");

private:
    Config         config_;
    AgentMode      mode_ = AgentMode::Plan;
    ContextManager context_;
    LlmClient      llm_;
    ToolRegistry   registry_;
    Ui&            ui_;
    std::string    pending_execution_;
    bool           non_interactive_ = false;

    void loop();
    void handle_tool_calls(const std::vector<ToolCall>& calls);
    bool requires_approval(const ToolDef& def) const;
    bool handle_slash_command(std::string_view input);
    void transition_to(AgentMode next);
    void rebuild_registry();
    std::string system_prompt() const;
};
