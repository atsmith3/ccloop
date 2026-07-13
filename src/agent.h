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
#include "connector.h"
#include "context.h"
#include "signals.h"
#include "tools.h"
#include "ui.h"
#include <functional>
#include <unordered_set>
#include <vector>

using SlashFn = std::function<void(const std::string &arg)>;

struct SlashCommand {
  std::string name;
  std::string description; // shown verbatim in /help output
  SlashFn fn;
};

class Agent {
public:
  Agent(Config config, Ui &ui);
  Agent(Config config, Ui &ui, std::unique_ptr<Connector> connector);
  int run(const std::string &initial_prompt = "");

private:
  Config config_;
  ContextManager context_;
  std::unique_ptr<Connector> connector_;
  ToolRegistry registry_;
  Ui &ui_;
  std::string pending_execution_;
  bool non_interactive_ = false;
  int exit_code_ = 0;
  bool task_done_called_ = false;
  std::unordered_set<size_t> seen_calls_;
  std::vector<SlashCommand> slash_commands_;
  std::vector<McpServerStatus> mcp_status_;
  SessionStats stats_;

  void loop();
  void compact_with_summary();
  double compute_cost(const LlmResponse::Usage &usage) const;
  void handle_tool_calls(const std::vector<ToolCall> &calls);
  bool requires_approval(const ToolDef &def) const;
  bool save_context(const std::string &path) const;
  bool restore_context(const std::string &path);
  bool handle_slash_command(std::string_view input);
  void reset_context();
  void rebuild_registry();
  void build_slash_commands();
  void cmd_mcp_list();
  void cmd_mcp_reload();
  std::string system_prompt() const;

  friend struct AgentTests;
};
