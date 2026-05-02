#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include "types.h"
#include "connector.h"

struct Permissions {
    bool auto_approve_read   = true;   // read_file, list_dir, search_files, file_info
    bool auto_approve_write  = true;   // write_file, edit_file, create_dir
    bool auto_approve_delete = false;  // delete_file
    bool auto_approve_shell  = false;  // run_shell
};

struct McpServerConfig {
    std::string name;
    std::string url;                                    // SSE/HTTP endpoint
    std::string api_key;                                // optional Bearer auth
    std::unordered_set<std::string> write_tools;        // tools that require write approval
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
    size_t      token_limit            = 8000;
    size_t      compaction_keep_recent = 8;

    // Tool approval permissions
    Permissions permissions;

    // Connector type
    ConnectorType connector_type = ConnectorType::OpenAiJson;

    // AWS Bedrock settings (only needed when connector_type == Bedrock)
    std::string   aws_region     = "us-east-1";
    std::string   aws_access_key = "";
    std::string   aws_secret_key = "";

    // Path of the loaded config file (absolute, set at load time)
    std::string config_path = "";

    // MCP config file path (JSON, optional)
    std::string mcp_config = "";

    // MCP servers (populated from mcp_config JSON file)
    std::vector<McpServerConfig> mcp_servers;

    static Config defaults();
    static Config load(const std::string& explicit_path = "");
    static void   apply_env_overrides(Config& cfg);
};
