#pragma once

#include <string>
#include <vector>
#include <unordered_set>

struct Permissions {
    bool auto_approve_read   = true;   // read_file, list_dir, search_files, file_info
    bool auto_approve_write  = true;   // write_file, edit_file, create_dir
    bool auto_approve_delete = false;  // delete_file
    bool auto_approve_shell  = false;  // run_shell
};

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
    float       temperature   = 0.7f;
    bool        debug         = false;

    // Context settings
    size_t      token_limit = 8000;

    // Tool approval permissions
    Permissions permissions;

    // MCP servers (Phase 2)
    std::vector<McpServerConfig> mcp_servers;

    static Config defaults();
    static Config load(const std::string& explicit_path = "");
    static void   apply_env_overrides(Config& cfg);
};
