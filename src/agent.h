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
    int run(const std::string& initial_prompt = "");

private:
    Config         config_;
    AgentMode      mode_ = AgentMode::Plan;
    ContextManager context_;
    LlmClient      llm_;
    ToolRegistry   registry_;
    Ui&            ui_;
    std::string    pending_execution_;
    bool           non_interactive_          = false;
    int            exit_code_                = 0;
    bool           plan_accepted_            = false;
    std::string    plan_accepted_text_;
    bool           plan_rejected_            = false;
    bool           task_done_called_          = false;

    void loop();
    void compact_with_summary();
    void handle_tool_calls(const std::vector<ToolCall>& calls);
    ToolResult handle_present_plan(const ToolArgs& args);
    ToolResult handle_task_done(const ToolArgs& args);
    ToolResult handle_print(const ToolArgs& args);
    bool requires_approval(const ToolDef& def) const;
    bool handle_slash_command(std::string_view input);
    void transition_to(AgentMode next);
    void rebuild_registry();
    std::string system_prompt() const;
};
