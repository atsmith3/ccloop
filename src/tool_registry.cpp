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

#include "mcp_client.h"
#include "tools.h"
#include <iostream>

// ============================================================================
// Registry factory
// ============================================================================

static Tool make_local_tool(std::string name, std::string desc,
                            std::vector<ToolParam> params, Permission perm,
                            ToolFn fn) {
  Tool t;
  t.def.name = std::move(name);
  t.def.description = std::move(desc);
  t.def.params = std::move(params);
  t.def.permission = perm;
  t.fn = std::move(fn);
  t.source = ToolSource::Local;
  return t;
}

ToolRegistry make_registry(const Config &cfg,
                           std::vector<McpServerStatus> *status_out) {
  ToolRegistry registry;

  registry.register_tool(make_local_tool(
      "read_file",
      "Read the contents of a file. Use offset/limit to read a specific line "
      "range.",
      {{"path", "string", "Path to file", true},
       {"offset", "integer", "1-based first line to read (optional)", false},
       {"limit", "integer", "Maximum number of lines to return (optional)",
        false}},
      Permission::Read, tool_read_file));

  registry.register_tool(make_local_tool(
      "write_file", "Write content to a file (atomic via temporary file)",
      {{"path", "string", "Path to file", true},
       {"content", "string", "Content to write", true}},
      Permission::Write, tool_write_file));

  registry.register_tool(make_local_tool(
      "edit_file",
      "Replace an exact string in a file (old_str must appear exactly once)",
      {{"path", "string", "Path to file", true},
       {"old_str", "string", "Exact text to replace", true},
       {"new_str", "string", "Replacement text", true}},
      Permission::Write, tool_edit_file));

  // run_shell is the agent's terminal: listing, searching, metadata, mkdir,
  // rm, builds, and tests all go through it.
  registry.register_tool(make_local_tool(
      "run_shell", "Execute a shell command (requires approval)",
      {{"command", "string", "Shell command to execute", true},
       {"cwd", "string", "Working directory (optional)", false},
       {"timeout_sec", "integer", "Timeout in seconds (optional, default 30)",
        false}},
      Permission::Execute, tool_run_shell));

  // Register MCP tools from configured servers
  for (const auto &server_cfg : cfg.mcp_servers) {
    auto client = std::make_shared<McpClient>(server_cfg, cfg);
    if (!client->initialize()) {
      std::cerr << "[mcp] warning: could not connect to '" << server_cfg.name
                << "'\n";
      if (status_out)
        status_out->push_back({server_cfg.name, server_cfg.transport,
                               server_cfg.url, server_cfg.command, false, 0});
      continue;
    }
    auto tool_defs = client->list_tools();
    if (status_out)
      status_out->push_back({server_cfg.name, server_cfg.transport,
                             server_cfg.url, server_cfg.command, true,
                             (int)tool_defs.size()});
    for (auto def : tool_defs) {
      def.permission = server_cfg.write_tools.count(def.name)
                           ? Permission::Write
                           : Permission::Read;
      Tool tool;
      tool.def = def;
      tool.source = ToolSource::Mcp;
      tool.mcp_server = server_cfg.name;
      tool.fn = [client, name = def.name](const ToolArgs &args) {
        return client->call_tool(name, args);
      };
      registry.register_tool(std::move(tool));
    }
  }

  return registry;
}
