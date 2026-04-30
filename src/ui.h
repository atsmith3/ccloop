#pragma once

#include <string>
#include <string_view>
#include "types.h"

class Ui {
public:
    explicit Ui();

    void show_message    (std::string_view role, std::string_view content);
    void show_tool_call  (const ToolCall& call, ToolSource source);
    void show_tool_result(const ToolCall& call, const ToolResult& result);
    void show_mode       (AgentMode mode, size_t tokens_used, size_t token_limit);
    void update_tokens   (size_t used, size_t limit);
    void show_usage      (const LlmResponse::Usage& usage, size_t ctx_used, size_t ctx_limit);
    void show_error      (std::string_view msg);
    void print_output    (std::string_view message);

    Approval     request_approval    (const ToolCall& call);
    PlanApproval request_plan_approval(std::string& refinement_out);
    void         show_plan            (const std::string& plan);
    void         show_completion      (const std::string& summary);
    std::string  wait_for_input();
};
