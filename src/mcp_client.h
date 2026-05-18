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
#include "json.h"
#include "types.h"
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Abstract transport — concrete implementations live in mcp_client.cpp
class McpTransport {
public:
  virtual ~McpTransport() = default;
  virtual std::optional<JsonValue> send_rpc(int id, const std::string &method,
                                            const JsonValue &params) = 0;
  virtual void send_notification(const std::string &method) = 0;
};

class McpClient {
public:
  McpClient(const McpServerConfig &server, const Config &cfg);
  ~McpClient() = default;

  // MCP lifecycle — call initialize() before list_tools() or call_tool()
  bool initialize();
  std::vector<ToolDef> list_tools();
  ToolResult call_tool(const std::string &name, const ToolArgs &args);

  // Testable static helpers (no network required)
  static std::string build_rpc(int id, const std::string &method,
                               const JsonValue &params);
  static std::string build_notification(const std::string &method);
  static JsonValue parse_sse_or_json(const std::string &body,
                                     const std::string &content_type);
  static std::vector<ToolDef> parse_tools_list(const JsonValue &result);
  static ToolResult parse_call_result(const JsonValue &result);

private:
  std::optional<JsonValue> send_rpc(const std::string &method,
                                    const JsonValue &params = JsonValue{});
  void send_notification(const std::string &method);

  McpServerConfig server_;
  Config cfg_;
  std::unique_ptr<McpTransport> transport_;
  int next_id_ = 1;
  std::mutex mutex_;
};
