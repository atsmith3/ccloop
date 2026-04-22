#include "config.h"
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// Utility: trim leading/trailing whitespace
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Utility: expand ~ to $HOME
static std::string expand_home(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

// Utility: parse a quoted string value
static std::string parse_string_value(const std::string& value) {
    std::string trimmed = trim(value);
    if (trimmed.size() >= 2 && trimmed[0] == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

// Parse TOML file into Config
static void parse_toml(const std::string& path, Config& cfg) {
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
        if (line.empty()) continue;

        // Handle sections
        if (line[0] == '[' && line.back() == ']') {
            current_section = trim(line.substr(1, line.size() - 2));
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
            std::transform(val_lower.begin(), val_lower.end(),
                           val_lower.begin(), [](unsigned char c) { return std::tolower(c); });
            bool bool_val = (val_lower == "true");
            if      (key == "read")   cfg.permissions.auto_approve_read   = bool_val;
            else if (key == "write")  cfg.permissions.auto_approve_write  = bool_val;
            else if (key == "delete") cfg.permissions.auto_approve_delete = bool_val;
            else if (key == "shell")  cfg.permissions.auto_approve_shell  = bool_val;
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
        } else if (key == "connector") {
            std::string v = parse_string_value(value);
            if      (v == "openai-json") cfg.connector_type = ConnectorType::OpenAiJson;
            else if (v == "bedrock")     cfg.connector_type = ConnectorType::Bedrock;
            else                         cfg.connector_type = ConnectorType::QwenXml;
        } else if (key == "aws_region") {
            cfg.aws_region = parse_string_value(value);
        } else if (key == "aws_access_key") {
            cfg.aws_access_key = parse_string_value(value);
        } else if (key == "aws_secret_key") {
            cfg.aws_secret_key = parse_string_value(value);
        }
    }
}

Config Config::defaults() {
    return Config();
}

Config Config::load(const std::string& explicit_path) {
    Config cfg = defaults();

    // Determine which config file to load
    std::string config_path;

    if (!explicit_path.empty()) {
        config_path = explicit_path;
    } else {
        const char* env_config = std::getenv("CCL_CONFIG");
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
        } catch (const std::exception& e) {
            // Fail silently, use defaults
        }
    }

    return cfg;
}

void Config::apply_env_overrides(Config& cfg) {
    const char* api_key = std::getenv("CCL_API_KEY");
    if (api_key) cfg.api_key = api_key;

    const char* endpoint = std::getenv("CCL_ENDPOINT");
    if (endpoint) cfg.endpoint = endpoint;

    const char* timeout = std::getenv("CCL_TIMEOUT");
    if (timeout) {
        try {
            cfg.timeout_sec = std::stoi(timeout);
        } catch (...) {
            // Ignore invalid timeout
        }
    }

    const char* aws_region = std::getenv("AWS_REGION");
    if (aws_region) cfg.aws_region = aws_region;

    const char* aws_access_key = std::getenv("AWS_ACCESS_KEY_ID");
    if (aws_access_key) cfg.aws_access_key = aws_access_key;

    const char* aws_secret_key = std::getenv("AWS_SECRET_ACCESS_KEY");
    if (aws_secret_key) cfg.aws_secret_key = aws_secret_key;
}
