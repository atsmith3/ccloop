#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include "json.h"

// Agent mode
enum class AgentMode {
    Plan,
    Act,
};

enum class Approval {
    Accept,
    Reject,
    Edit,
};

// Tool parameter definition
struct ToolParam {
    std::string name;
    std::string type;         // "string" | "integer" | "boolean"
    std::string description;
    bool        required;
};

// Tool definition -- sent to LLM
struct ToolDef {
    std::string            name;
    std::string            description;
    std::vector<ToolParam> params;
};

// Parsed tool call from LLM response
// Tools deserialize typed values as needed (int, bool, etc.) from JsonValue
using ToolArgs = std::unordered_map<std::string, JsonValue>;

struct ToolCall {
    std::string id;
    std::string name;
    ToolArgs    args;
};

// Tool execution result
struct ToolResult {
    bool        success;
    std::string content;
    std::string error;

    static ToolResult ok(std::string content);
    static ToolResult fail(std::string error);
    std::string to_context_string() const;
};

// Tool call record stored in context
struct ToolCallRecord {
    std::string id;
    std::string name;
    std::string arguments_json;   // raw, preserved exactly
};

// Single context message
struct Message {
    enum class Role { System, User, Assistant, Tool };

    Role                        role;
    std::string                 content;
    std::vector<ToolCallRecord> tool_calls;        // assistant only
    std::string                 tool_call_id;      // tool only
    size_t                      estimated_tokens = 0;
};

// LLM response
struct LlmResponse {
    std::string           content;
    std::vector<ToolCall> tool_calls;
    std::string           finish_reason;

    struct Usage {
        size_t prompt_tokens     = 0;
        size_t completion_tokens = 0;
        size_t total_tokens      = 0;
    } usage;

    // Streaming accumulation -- internal use
    struct PartialToolCall {
        std::string id;
        std::string name;
        std::string accumulated_args;
    };
    std::vector<PartialToolCall> partial_tool_calls;
};

// Tool source for UI display
enum class ToolSource { Local, Mcp };
