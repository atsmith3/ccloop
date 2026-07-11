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

struct Tool {
  ToolDef def;
  ToolFn fn;
  ToolSource source = ToolSource::Local;
  std::string mcp_server = "";
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

// Factory — builds the fixed minimal toolset (read_file, write_file, edit_file,
// run_shell) plus any MCP tools from configured servers.
ToolRegistry make_registry(const Config &cfg,
                           std::vector<McpServerStatus> *status_out = nullptr);

ToolResult tool_read_file(const ToolArgs &args);
ToolResult tool_write_file(const ToolArgs &args);
ToolResult tool_edit_file(const ToolArgs &args);
ToolResult tool_run_shell(const ToolArgs &args);
