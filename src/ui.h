#pragma once

#include <string>
#include <string_view>
#include "types.h"

class Ui {
public:
    Ui() = default;

    void show_message    (std::string_view role, std::string_view content);
    void show_tool_call  (const ToolCall& call, ToolSource source);
    void show_tool_result(const ToolCall& call, const ToolResult& result);
    void show_mode       (AgentMode mode);
    void update_tokens   (size_t used, size_t limit);
    void show_error      (std::string_view msg);
    void append_chunk    (std::string_view chunk);  // for streaming output

    Approval    request_approval(const ToolCall& call);
    std::string wait_for_input();
};
