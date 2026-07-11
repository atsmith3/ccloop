// Copyright 2026 Andrew Smith
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include "json.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class Approval {
  Accept,
  Reject,
};

// Tool parameter definition
struct ToolParam {
  std::string name;
  std::string type; // "string" | "integer" | "boolean"
  std::string description;
  bool required = false;
};

// Mirrors Unix filesystem permissions: read, write, execute.
enum class Permission { Read, Write, Execute };

// Tool definition -- sent to LLM
struct ToolDef {
  std::string name;
  std::string description;
  std::vector<ToolParam> params;
  Permission permission = Permission::Read;
};

// Parsed tool call from LLM response
// Tools deserialize typed values as needed (int, bool, etc.) from JsonValue
using ToolArgs = std::unordered_map<std::string, JsonValue>;

struct ToolCall {
  std::string id;
  std::string name;
  ToolArgs args;
};

// Tool execution result
struct ToolResult {
  bool success;
  std::string content;
  std::string error;

  static ToolResult ok(std::string content);
  static ToolResult fail(std::string error);
  std::string to_context_string() const;
};

// Single context message
struct Message {
  enum class Role { System, User, Assistant };

  Role role;
  std::string content;
  size_t estimated_tokens = 0;
};

inline std::string_view role_to_str(Message::Role r) {
  switch (r) {
  case Message::Role::System:
    return "system";
  case Message::Role::User:
    return "user";
  case Message::Role::Assistant:
    return "assistant";
  }
  return "user";
}

inline Message::Role str_to_role(const std::string &s) {
  if (s == "system")
    return Message::Role::System;
  if (s == "assistant")
    return Message::Role::Assistant;
  return Message::Role::User;
}

// LLM response
struct LlmResponse {
  bool is_error = false; // set when transport/HTTP error occurred
  std::string content;
  std::vector<ToolCall> tool_calls;
  struct Usage {
    size_t prompt_tokens = 0;
    size_t completion_tokens = 0;
    size_t total_tokens = 0;
    size_t cache_read_tokens = 0;
    size_t cache_write_tokens = 0;
  } usage;
};

// Tool source for UI display
enum class ToolSource { Local, Mcp };

// Cumulative session usage, accumulated across every turn and shown by /stats.
struct SessionStats {
  size_t user_messages = 0;      // real user prompts (not tool-result pushes)
  size_t assistant_messages = 0; // successful model responses
  size_t tool_calls = 0;         // total tool calls dispatched
  size_t tool_results = 0;       // total tool results collected
  size_t input_tokens = 0;       // sum of prompt_tokens
  size_t cache_read_tokens = 0;  // sum of cache_read_tokens
  size_t cache_write_tokens = 0; // sum of cache_write_tokens
  size_t output_tokens = 0;      // sum of completion_tokens
  double cost = 0.0;             // running USD
};
