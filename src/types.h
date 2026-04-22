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

// LLM connector type
enum class ConnectorType {
    QwenXml,    // OpenAI completions + XML tool calling in system prompt
    OpenAiJson, // OpenAI completions + JSON tools array in request
    Bedrock,    // AWS Bedrock Converse API + SigV4
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
    std::string            permission;  // "read" | "write" | "delete" | "shell"
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

// Single context message
struct Message {
    enum class Role { System, User, Assistant };

    Role        role;
    std::string content;
    size_t      estimated_tokens = 0;
};

// LLM response
struct LlmResponse {
    bool                  is_error = false;   // set when transport/HTTP error occurred
    std::string           content;
    std::vector<ToolCall> tool_calls;
    struct Usage {
        size_t prompt_tokens     = 0;
        size_t completion_tokens = 0;
        size_t total_tokens      = 0;
    } usage;

};

// Tool source for UI display
enum class ToolSource { Local, Mcp };
