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

#include "config.h"
#include "types.h"
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration
class ContextManager;

using ToolFn = std::function<ToolResult(const ToolArgs &)>;

// Handlers for agent-native tools (run sequentially in the agent pre-pass, may
// block on I/O). Pass these into make_registry so new agent-native tools need
// no changes in agent.cpp.
using AgentHandlers = std::unordered_map<std::string, ToolFn>;

struct Tool {
  ToolDef def;
  ToolFn fn;
  ToolSource source = ToolSource::Local;
  std::string mcp_server = "";
  bool agent_native =
      false; // true → run in pre-pass (sequential), not thread pool
};

struct McpServerStatus {
  std::string name;
  McpTransportType transport;
  std::string url;
  std::string command;
  bool connected = false;
  int tool_count = 0;
};

class ToolRegistry {
public:
  void register_tool(Tool tool);
  std::vector<ToolDef> definitions() const;
  std::optional<const Tool *> find(const std::string &name) const;

private:
  std::vector<Tool> tools_;
  std::unordered_map<std::string, size_t> index_;
};

// Factory — builds registry based on mode.
// Pass handlers for agent-native tools (present_plan, print, ask_user) so their
// fn fields are wired up without hardcoding names in agent.cpp.
ToolRegistry make_registry(AgentMode mode, const Config &cfg,
                           bool non_interactive = false,
                           const AgentHandlers &handlers = {},
                           std::vector<McpServerStatus> *status_out = nullptr);

// Read-only tools (registered in all modes)
ToolResult tool_read_file(const ToolArgs &args);
ToolResult tool_list_dir(const ToolArgs &args);
ToolResult tool_search_files(const ToolArgs &args);
ToolResult tool_file_info(const ToolArgs &args);
ToolResult tool_find_symbol(const ToolArgs &args);

// Write tools (Act mode only)
ToolResult tool_write_file(const ToolArgs &args);
ToolResult tool_edit_file(const ToolArgs &args);
ToolResult tool_create_dir(const ToolArgs &args);
ToolResult tool_delete_file(const ToolArgs &args);
ToolResult tool_delete_dir(const ToolArgs &args);
ToolResult tool_run_shell(const ToolArgs &args);
ToolResult tool_spawn_agent(const ToolArgs &args,
                            const std::string &config_path = "");
