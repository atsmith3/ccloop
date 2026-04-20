#pragma once

#include <string>
#include <vector>
#include <unordered_set>

struct McpServerConfig {
    std::string name;
    std::string command;
    std::string url;
    std::unordered_set<std::string> write_tools;
};

struct Config {
    // LLM settings
    std::string endpoint    = "http://localhost:4000/v1";
    std::string api_key     = "";
    std::string model       = "qwen3-235b";
    int         timeout_sec = 30;
    int         max_retries = 3;
    size_t      max_tokens  = 4096;
    float       temperature = 0.7f;

    // Context settings
    size_t      token_limit = 8000;

    // MCP servers (Phase 2)
    std::vector<McpServerConfig> mcp_servers;

    static Config defaults();
    static Config load(const std::string& explicit_path = "");
    static void   apply_env_overrides(Config& cfg);
};
