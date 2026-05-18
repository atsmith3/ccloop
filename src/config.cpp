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

#include "config.h"
#include "json.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

// Utility: trim leading/trailing whitespace
static std::string trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

// Utility: expand ~ to $HOME
static std::string expand_home(const std::string &path) {
  if (path.empty() || path[0] != '~')
    return path;
  const char *home = std::getenv("HOME");
  if (!home)
    return path;
  return std::string(home) + path.substr(1);
}

// Utility: parse a quoted string value
static std::string parse_string_value(const std::string &value) {
  std::string trimmed = trim(value);
  if (trimmed.size() >= 2 && trimmed[0] == '"' && trimmed.back() == '"') {
    return trimmed.substr(1, trimmed.size() - 2);
  }
  return trimmed;
}

// Parse TOML file into Config
static void parse_toml(const std::string &path, Config &cfg) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open config file: " + path);
  }

  std::string line;
  std::string current_section;
  while (std::getline(file, line)) {
    // Strip comments
    size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }

    line = trim(line);
    if (line.empty())
      continue;

    // Handle sections
    if (line[0] == '[' && line.back() == ']') {
      current_section = trim(line.substr(1, line.size() - 2));
      if (current_section != "permissions") {
        std::cerr << "warning: " << path << ": unrecognized section '"
                  << current_section << "' (ignored)\n";
      }
      continue;
    }

    // Parse key = value
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      throw std::runtime_error("Invalid TOML line: " + line);
    }

    std::string key = trim(line.substr(0, eq_pos));
    std::string value = trim(line.substr(eq_pos + 1));

    // Parse [permissions] section
    if (current_section == "permissions") {
      std::string val_lower = value;
      std::transform(val_lower.begin(), val_lower.end(), val_lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      bool bool_val = (val_lower == "true");
      if (key == "read")
        cfg.permissions.auto_approve_read = bool_val;
      else if (key == "write")
        cfg.permissions.auto_approve_write = bool_val;
      else if (key == "delete")
        cfg.permissions.auto_approve_delete = bool_val;
      else if (key == "shell")
        cfg.permissions.auto_approve_shell = bool_val;
      else
        std::cerr << "warning: " << path << ": unrecognized key '" << key
                  << "' (ignored)\n";
      continue;
    }

    // Parse top-level keys
    if (key == "endpoint") {
      cfg.endpoint = parse_string_value(value);
    } else if (key == "api_key") {
      cfg.api_key = parse_string_value(value);
    } else if (key == "model") {
      cfg.model = parse_string_value(value);
    } else if (key == "timeout_sec") {
      cfg.timeout_sec = std::stoi(value);
    } else if (key == "max_retries") {
      cfg.max_retries = std::stoi(value);
    } else if (key == "max_tokens") {
      cfg.max_tokens = std::stoul(value);
    } else if (key == "temperature") {
      cfg.temperature = std::stof(value);
    } else if (key == "token_limit") {
      cfg.token_limit = std::stoul(value);
    } else if (key == "compaction_keep_recent") {
      cfg.compaction_keep_recent = std::stoul(value);
    } else if (key == "connector") {
      std::string v = parse_string_value(value);
      if (v == "bedrock")
        cfg.connector_type = ConnectorType::Bedrock;
      else
        cfg.connector_type = ConnectorType::OpenAiJson;
    } else if (key == "aws_region") {
      cfg.aws_region = parse_string_value(value);
    } else if (key == "aws_access_key") {
      cfg.aws_access_key = parse_string_value(value);
    } else if (key == "aws_secret_key") {
      cfg.aws_secret_key = parse_string_value(value);
    } else if (key == "mcp_config") {
      cfg.mcp_config = parse_string_value(value);
    } else if (key == "editor") {
      cfg.editor = parse_string_value(value);
    } else if (current_section.empty()) {
      std::cerr << "warning: " << path << ": unrecognized key '" << key
                << "' (ignored)\n";
    }
  }
}

// Parse MCP servers from a JSON config file (mcpServers object format)
static void load_mcp_config(const std::string &path, Config &cfg) {
  std::ifstream file(path);
  if (!file)
    return;

  std::ostringstream ss;
  ss << file.rdbuf();

  JsonValue root = parse_json(ss.str());

  auto servers_v = root.get("mcpServers");
  if (!servers_v || !servers_v->is_object())
    return;

  for (const auto &[name, server_val] : servers_v->as_object()) {
    if (!server_val)
      continue;

    McpServerConfig srv;
    srv.name = name;

    // Parse transport (default: http)
    std::string transport_str = "http";
    auto transport_v = server_val->get("transport");
    if (transport_v && transport_v->is_string())
      transport_str = transport_v->as_string();

    if (transport_str == "stdio") {
      srv.transport = McpTransportType::Stdio;
      auto cmd_v = server_val->get("command");
      if (!cmd_v || !cmd_v->is_string()) {
        std::cerr << "[mcp] server '" << name
                  << "': transport=stdio requires 'command' (skipping)\n";
        continue;
      }
      srv.command = cmd_v->as_string();
    } else if (transport_str == "sse") {
      srv.transport = McpTransportType::LegacySse;
      auto url_v = server_val->get("url");
      if (!url_v || !url_v->is_string()) {
        std::cerr << "[mcp] server '" << name
                  << "': transport=sse requires 'url' (skipping)\n";
        continue;
      }
      srv.url = url_v->as_string();
    } else if (transport_str == "http" || transport_str == "streamable-http") {
      srv.transport = McpTransportType::Http;
      auto url_v = server_val->get("url");
      if (!url_v || !url_v->is_string()) {
        std::cerr << "[mcp] server '" << name
                  << "': transport=http requires 'url' (skipping)\n";
        continue;
      }
      srv.url = url_v->as_string();
    } else {
      std::cerr << "[mcp] server '" << name << "': unknown transport '"
                << transport_str << "' (skipping)\n";
      continue;
    }

    auto key_v = server_val->get("apiKey");
    if (key_v && key_v->is_string())
      srv.api_key = key_v->as_string();

    auto wt_v = server_val->get("writeTools");
    if (wt_v && wt_v->is_array()) {
      for (const auto &wt : wt_v->as_array()) {
        if (wt && wt->is_string())
          srv.write_tools.insert(wt->as_string());
      }
    }

    cfg.mcp_servers.push_back(std::move(srv));
  }
}

Config Config::defaults() { return Config(); }

Config Config::load(const std::string &explicit_path) {
  Config cfg = defaults();

  // Determine which config file to load
  std::string config_path;

  if (!explicit_path.empty()) {
    config_path = explicit_path;
  } else {
    const char *env_config = std::getenv("CCL_CONFIG");
    if (env_config) {
      config_path = env_config;
    } else if (fs::exists("./ccl.toml")) {
      config_path = "./ccl.toml";
    } else {
      std::string global_config = expand_home("~/.config/ccl/config.toml");
      if (fs::exists(global_config)) {
        config_path = global_config;
      }
    }
  }

  // Load config file if found
  if (!config_path.empty() && fs::exists(config_path)) {
    try {
      parse_toml(config_path, cfg);
      cfg.config_path = fs::absolute(config_path).string();
    } catch (const std::exception &e) {
      std::cerr << "warning: failed to parse config " << config_path << ": "
                << e.what() << "\n";
    }
  }

  // Load MCP server config from JSON file if specified
  if (!cfg.mcp_config.empty()) {
    std::string mcp_path = expand_home(cfg.mcp_config);
    if (fs::exists(mcp_path)) {
      try {
        load_mcp_config(mcp_path, cfg);
      } catch (const std::exception &e) {
        std::cerr << "warning: failed to load MCP config " << mcp_path << ": "
                  << e.what() << "\n";
      } catch (...) {
        std::cerr << "warning: failed to load MCP config " << mcp_path << "\n";
      }
    }
  }

  return cfg;
}

void Config::reload_mcp_servers() {
  mcp_servers.clear();
  if (mcp_config.empty())
    return;
  std::string path = expand_home(mcp_config);
  if (!fs::exists(path))
    return;
  try {
    load_mcp_config(path, *this);
  } catch (const std::exception &e) {
    std::cerr << "warning: failed to reload MCP config " << path << ": "
              << e.what() << "\n";
  } catch (...) {
    std::cerr << "warning: failed to reload MCP config " << path << "\n";
  }
}

void Config::apply_env_overrides(Config &cfg) {
  const char *api_key = std::getenv("CCL_API_KEY");
  if (api_key)
    cfg.api_key = api_key;

  const char *endpoint = std::getenv("CCL_ENDPOINT");
  if (endpoint)
    cfg.endpoint = endpoint;

  const char *timeout = std::getenv("CCL_TIMEOUT");
  if (timeout) {
    try {
      cfg.timeout_sec = std::stoi(timeout);
    } catch (...) {
      std::cerr << "warning: CCL_TIMEOUT=" << timeout
                << " is not a valid integer; using default\n";
    }
  }

  const char *mcp_config = std::getenv("CCL_MCP_CONFIG");
  if (mcp_config)
    cfg.mcp_config = mcp_config;

  const char *aws_region = std::getenv("AWS_REGION");
  if (aws_region)
    cfg.aws_region = aws_region;

  const char *aws_access_key = std::getenv("AWS_ACCESS_KEY_ID");
  if (aws_access_key)
    cfg.aws_access_key = aws_access_key;

  const char *aws_secret_key = std::getenv("AWS_SECRET_ACCESS_KEY");
  if (aws_secret_key)
    cfg.aws_secret_key = aws_secret_key;
}
