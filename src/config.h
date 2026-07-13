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

#include "connector.h"
#include "types.h"
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

struct Permissions {
  bool auto_approve_read = true;  // read_file (and read-only MCP tools)
  bool auto_approve_write = true; // write_file, edit_file (and write MCP tools)
  bool auto_approve_execute = false; // run_shell (the terminal)
};

enum class McpTransportType { Http, Stdio, LegacySse };

struct McpServerConfig {
  std::string name;
  McpTransportType transport = McpTransportType::Http; // transport type
  std::string url;     // HTTP/SSE endpoint (Http, LegacySse)
  std::string command; // shell command to launch server (Stdio)
  std::string api_key; // optional Bearer auth (Http transports)
  std::unordered_set<std::string>
      write_tools; // tools that require write approval
};

// One pricing tier for a model. Anthropic-style models bill at different rates
// depending on the request's input-token count, so a model may have several
// tiers matched by [context_min, context_max). All rates are USD per token.
struct PricingTier {
  std::string model;
  size_t context_min = 0;
  size_t context_max = std::numeric_limits<size_t>::max();
  double input_cost_per_token = 0.0;
  double cache_read_input_token_cost = 0.0;
  double cache_creation_input_token_cost = 0.0;
  double output_cost_per_token = 0.0;
};

struct Config {
  // LLM settings
  std::string endpoint = "http://localhost:4000/v1";
  std::string api_key = "";
  std::string model = "qwen3-235b";
  int timeout_sec = 30;
  int max_retries = 3;
  size_t max_tokens = 4096;
  float temperature = 0.7f;
  bool debug = false;
  bool silent = false;

  // Context settings
  size_t token_limit = 8000;
  size_t compaction_keep_recent = 8;

  // Tool approval permissions
  Permissions permissions;

  // Connector type
  ConnectorType connector_type = ConnectorType::OpenAiJson;

  // AWS Bedrock settings (only needed when connector_type == Bedrock)
  std::string aws_region = "us-east-1";
  std::string aws_access_key = "";
  std::string aws_secret_key = "";

  // Path of the loaded config file (absolute, set at load time)
  std::string config_path = "";

  // Working directory at startup, injected into system prompt
  std::string working_dir = "";

  // Preferred editor for /edit command (empty = use $VISUAL/$EDITOR/nano)
  std::string editor = "";

  // MCP config file path (JSON, optional)
  std::string mcp_config = "";

  // MCP servers (populated from mcp_config JSON file)
  std::vector<McpServerConfig> mcp_servers;

  // Per-model pricing tiers (populated from [[pricing]] blocks). Empty = $0.
  std::vector<PricingTier> pricing;

  static Config defaults();
  static Config load(const std::string &explicit_path = "");
  static void apply_env_overrides(Config &cfg);
  void reload_mcp_servers();
};
