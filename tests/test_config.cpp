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

#include "../src/config.h"
#include "harness.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Helper: create temp TOML file
static std::string create_temp_toml(const std::string &content) {
  std::string tmpdir = fs::temp_directory_path().string();
  std::string path =
      tmpdir + "/test_config_" + std::to_string(std::rand()) + ".toml";
  std::ofstream f(path);
  f << content;
  f.close();
  return path;
}

TEST(config_defaults_all_fields_present) {
  Config cfg = Config::defaults();
  CHECK_EQ(cfg.endpoint, std::string("http://localhost:4000/v1"));
  CHECK_EQ(cfg.model, std::string("qwen3-235b"));
  CHECK_EQ(cfg.timeout_sec, 30);
  CHECK_EQ(cfg.max_retries, 3);
  CHECK_EQ(cfg.max_tokens, size_t(4096));
  CHECK_EQ(cfg.temperature, 0.7f);
  CHECK_EQ(cfg.token_limit, size_t(8000));
}

TEST(config_load_minimal_toml) {
  std::string path = create_temp_toml("endpoint = \"http://test:9000/v1\"\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.endpoint, std::string("http://test:9000/v1"));
  CHECK_EQ(cfg.model, std::string("qwen3-235b")); // default
  fs::remove(path);
}

TEST(config_load_all_fields) {
  std::string content = R"(
endpoint    = "http://custom:5000/v1"
api_key     = "sk-test123"
model       = "gpt-4"
timeout_sec = 60
max_retries = 5
max_tokens  = 8192
temperature = 0.5
token_limit = 16000
)";
  std::string path = create_temp_toml(content);
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.endpoint, std::string("http://custom:5000/v1"));
  CHECK_EQ(cfg.api_key, std::string("sk-test123"));
  CHECK_EQ(cfg.model, std::string("gpt-4"));
  CHECK_EQ(cfg.timeout_sec, 60);
  CHECK_EQ(cfg.max_retries, 5);
  CHECK_EQ(cfg.max_tokens, size_t(8192));
  CHECK_EQ(cfg.temperature, 0.5f);
  CHECK_EQ(cfg.token_limit, size_t(16000));
  fs::remove(path);
}

TEST(config_load_missing_file_uses_defaults) {
  Config cfg = Config::load("/nonexistent/path/to/config.toml");
  Config defaults = Config::defaults();
  CHECK_EQ(cfg.endpoint, defaults.endpoint);
  CHECK_EQ(cfg.model, defaults.model);
}

TEST(config_string_value_parsed) {
  std::string path = create_temp_toml("api_key = \"my-secret-key\"\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.api_key, std::string("my-secret-key"));
  fs::remove(path);
}

TEST(config_int_value_parsed) {
  std::string path = create_temp_toml("timeout_sec = 120\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.timeout_sec, 120);
  fs::remove(path);
}

TEST(config_float_value_parsed) {
  std::string path = create_temp_toml("temperature = 0.3\n");
  Config cfg = Config::load(path);
  CHECK(std::abs(cfg.temperature - 0.3f) < 0.001f);
  fs::remove(path);
}

TEST(config_env_var_api_key_overrides) {
  std::string path = create_temp_toml("api_key = \"file-key\"\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.api_key, std::string("file-key"));

  // Apply env override
  setenv("CCL_API_KEY", "env-key", 1);
  Config::apply_env_overrides(cfg);
  CHECK_EQ(cfg.api_key, std::string("env-key"));
  unsetenv("CCL_API_KEY");

  fs::remove(path);
}

TEST(config_env_var_endpoint_overrides) {
  std::string path = create_temp_toml("endpoint = \"http://file:9000/v1\"\n");
  Config cfg = Config::load(path);

  setenv("CCL_ENDPOINT", "http://env:8000/v1", 1);
  Config::apply_env_overrides(cfg);
  CHECK_EQ(cfg.endpoint, std::string("http://env:8000/v1"));
  unsetenv("CCL_ENDPOINT");

  fs::remove(path);
}

TEST(config_env_var_timeout_overrides) {
  Config cfg = Config::defaults();
  CHECK_EQ(cfg.timeout_sec, 30);

  setenv("CCL_TIMEOUT", "60", 1);
  Config::apply_env_overrides(cfg);
  CHECK_EQ(cfg.timeout_sec, 60);
  unsetenv("CCL_TIMEOUT");
}

TEST(config_permissions_defaults) {
  Config cfg = Config::defaults();
  CHECK(cfg.permissions.auto_approve_read);
  CHECK(cfg.permissions.auto_approve_write);
  CHECK(!cfg.permissions.auto_approve_execute);
}

TEST(config_permissions_from_toml) {
  std::string content = R"(
[permissions]
read    = false
write   = false
execute = true
)";
  std::string path = create_temp_toml(content);
  Config cfg = Config::load(path);
  CHECK(!cfg.permissions.auto_approve_read);
  CHECK(!cfg.permissions.auto_approve_write);
  CHECK(cfg.permissions.auto_approve_execute);
  fs::remove(path);
}

TEST(config_permissions_partial_override) {
  std::string content = R"(
[permissions]
execute = true
)";
  std::string path = create_temp_toml(content);
  Config cfg = Config::load(path);
  CHECK(cfg.permissions.auto_approve_read);    // default unchanged
  CHECK(cfg.permissions.auto_approve_write);   // default unchanged
  CHECK(cfg.permissions.auto_approve_execute); // overridden
  fs::remove(path);
}

// ============================================================================
// Connector type config tests
// ============================================================================

TEST(config_connector_defaults_to_openai_json) {
  Config cfg = Config::defaults();
  CHECK(cfg.connector_type == ConnectorType::OpenAiJson);
}

TEST(config_connector_openai_json_parsed) {
  std::string path = create_temp_toml("connector = \"openai-json\"\n");
  Config cfg = Config::load(path);
  CHECK(cfg.connector_type == ConnectorType::OpenAiJson);
  fs::remove(path);
}

TEST(config_connector_bedrock_parsed) {
  std::string path = create_temp_toml("connector = \"bedrock\"\n");
  Config cfg = Config::load(path);
  CHECK(cfg.connector_type == ConnectorType::Bedrock);
  fs::remove(path);
}

TEST(config_connector_unknown_defaults_to_openai_json) {
  std::string path = create_temp_toml("connector = \"something-unknown\"\n");
  Config cfg = Config::load(path);
  CHECK(cfg.connector_type == ConnectorType::OpenAiJson);
  fs::remove(path);
}

TEST(config_aws_fields_parsed) {
  std::string content = "aws_region = \"eu-west-1\"\n"
                        "aws_access_key = \"AKIATEST\"\n"
                        "aws_secret_key = \"secret123\"\n";
  std::string path = create_temp_toml(content);
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.aws_region, std::string("eu-west-1"));
  CHECK_EQ(cfg.aws_access_key, std::string("AKIATEST"));
  CHECK_EQ(cfg.aws_secret_key, std::string("secret123"));
  fs::remove(path);
}

TEST(config_aws_env_vars_override) {
  Config cfg = Config::defaults();
  setenv("AWS_REGION", "ap-southeast-1", 1);
  setenv("AWS_ACCESS_KEY_ID", "ENVKEY", 1);
  setenv("AWS_SECRET_ACCESS_KEY", "ENVSECRET", 1);
  Config::apply_env_overrides(cfg);
  CHECK_EQ(cfg.aws_region, std::string("ap-southeast-1"));
  CHECK_EQ(cfg.aws_access_key, std::string("ENVKEY"));
  CHECK_EQ(cfg.aws_secret_key, std::string("ENVSECRET"));
  unsetenv("AWS_REGION");
  unsetenv("AWS_ACCESS_KEY_ID");
  unsetenv("AWS_SECRET_ACCESS_KEY");
}

// ============================================================================
// TOML parsing edge cases
// ============================================================================

TEST(config_toml_comments_stripped) {
  std::string path = create_temp_toml(
      "endpoint = \"http://commented:9000/v1\"  # this is a comment\n"
      "model = \"test-model\"  # another comment\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.endpoint, std::string("http://commented:9000/v1"));
  CHECK_EQ(cfg.model, std::string("test-model"));
  fs::remove(path);
}

TEST(config_empty_toml_uses_defaults) {
  std::string path = create_temp_toml("\n\n   \n");
  Config cfg = Config::load(path);
  Config def = Config::defaults();
  CHECK_EQ(cfg.endpoint, def.endpoint);
  CHECK_EQ(cfg.model, def.model);
  CHECK_EQ(cfg.timeout_sec, def.timeout_sec);
  fs::remove(path);
}

TEST(config_unknown_keys_ignored) {
  std::string path = create_temp_toml("model = \"known-model\"\n"
                                      "totally_unknown_setting = \"ignored\"\n"
                                      "another_bogus_key = 42\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.model, std::string("known-model")); // known key parsed
  // No crash from unknown keys
  fs::remove(path);
}

TEST(config_invalid_timeout_env_ignored) {
  Config cfg = Config::defaults();
  int original = cfg.timeout_sec;
  setenv("CCL_TIMEOUT", "notanumber", 1);
  Config::apply_env_overrides(cfg);
  unsetenv("CCL_TIMEOUT");
  CHECK_EQ(cfg.timeout_sec, original); // unchanged
}

TEST(config_ccl_config_env_var) {
  std::string path = create_temp_toml("model = \"via-env-config\"\n");
  setenv("CCL_CONFIG", path.c_str(), 1);
  Config cfg = Config::load(""); // no explicit path — should pick up CCL_CONFIG
  unsetenv("CCL_CONFIG");
  CHECK_EQ(cfg.model, std::string("via-env-config"));
  fs::remove(path);
}

TEST(config_boolean_case_insensitive) {
  std::string content = "[permissions]\n"
                        "read    = False\n"
                        "write   = False\n"
                        "execute = TRUE\n";
  std::string path = create_temp_toml(content);
  Config cfg = Config::load(path);
  CHECK(!cfg.permissions.auto_approve_read);
  CHECK(!cfg.permissions.auto_approve_write);
  CHECK(cfg.permissions.auto_approve_execute);
  fs::remove(path);
}

TEST(config_search_project_local) {
  // Create temp files in temp directory
  std::string tmpdir = fs::temp_directory_path().string();
  std::string global_config = tmpdir + "/global_config.toml";
  std::string local_config = tmpdir + "/local_config.toml";

  // Write distinct values to each
  {
    std::ofstream f(global_config);
    f << "model = \"global-model\"\n";
  }
  {
    std::ofstream f(local_config);
    f << "model = \"local-model\"\n";
  }

  // Load explicit local file
  Config cfg = Config::load(local_config);
  CHECK_EQ(cfg.model, std::string("local-model"));

  fs::remove(global_config);
  fs::remove(local_config);
}

// ============================================================================
// MCP config loading
// ============================================================================

static std::string create_temp_json(const std::string &content) {
  std::string tmpdir = fs::temp_directory_path().string();
  std::string path =
      tmpdir + "/test_mcp_" + std::to_string(std::rand()) + ".json";
  std::ofstream f(path);
  f << content;
  f.close();
  return path;
}

TEST(config_mcp_config_key_parsed_from_toml) {
  std::string json_path = create_temp_json(R"({"mcpServers":{}})");
  std::string toml = "mcp_config = \"" + json_path + "\"\n";
  std::string toml_path = create_temp_toml(toml);
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_config, json_path);
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_single_server_loaded) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "myserver": {"url": "http://localhost:3001"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(1));
  CHECK_EQ(cfg.mcp_servers[0].name, std::string("myserver"));
  CHECK_EQ(cfg.mcp_servers[0].url, std::string("http://localhost:3001"));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_server_api_key_loaded) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "s": {"url": "http://localhost:3001", "apiKey": "tok123"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers[0].api_key, std::string("tok123"));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_server_write_tools_loaded) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "s": {"url": "http://localhost:3001", "writeTools": ["write_file", "delete_file"]}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK(cfg.mcp_servers[0].write_tools.count("write_file") > 0);
  CHECK(cfg.mcp_servers[0].write_tools.count("delete_file") > 0);
  CHECK(cfg.mcp_servers[0].write_tools.count("read_file") == 0);
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_multiple_servers_loaded) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "a": {"url": "http://localhost:3001"},
            "b": {"url": "http://localhost:3002"},
            "c": {"url": "http://localhost:3003"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(3));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_server_without_url_skipped) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "no_url": {"apiKey": "key"},
            "has_url": {"url": "http://localhost:3001"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(1));
  CHECK_EQ(cfg.mcp_servers[0].name, std::string("has_url"));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_nonexistent_file_silently_ignored) {
  std::string toml_path =
      create_temp_toml("mcp_config = \"/nonexistent/mcp.json\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_config,
           std::string("/nonexistent/mcp.json")); // field is set
  CHECK_EQ(cfg.mcp_servers.size(), size_t(0));    // but nothing loaded
  fs::remove(toml_path);
}

TEST(config_mcp_invalid_json_silently_ignored) {
  std::string json_path = create_temp_json("this is not json at all");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(0));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_env_var_sets_mcp_config) {
  Config cfg = Config::defaults();
  CHECK(cfg.mcp_config.empty());
  setenv("CCL_MCP_CONFIG", "/tmp/test_mcp.json", 1);
  Config::apply_env_overrides(cfg);
  CHECK_EQ(cfg.mcp_config, std::string("/tmp/test_mcp.json"));
  unsetenv("CCL_MCP_CONFIG");
}

TEST(config_mcp_http_transport_default) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "s": {"url": "http://localhost:3001"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(1));
  CHECK(cfg.mcp_servers[0].transport == McpTransportType::Http);
  CHECK_EQ(cfg.mcp_servers[0].url, std::string("http://localhost:3001"));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_stdio_transport_parsed) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "fs": {
                "transport": "stdio",
                "command": "npx @modelcontextprotocol/server-filesystem /tmp",
                "writeTools": ["write_file"]
            }
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(1));
  CHECK(cfg.mcp_servers[0].transport == McpTransportType::Stdio);
  CHECK_EQ(cfg.mcp_servers[0].command,
           std::string("npx @modelcontextprotocol/server-filesystem /tmp"));
  CHECK(cfg.mcp_servers[0].url.empty());
  CHECK(cfg.mcp_servers[0].write_tools.count("write_file") > 0);
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_sse_transport_parsed) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "legacy": {"transport": "sse", "url": "http://localhost:9000/sse"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(1));
  CHECK(cfg.mcp_servers[0].transport == McpTransportType::LegacySse);
  CHECK_EQ(cfg.mcp_servers[0].url, std::string("http://localhost:9000/sse"));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_stdio_without_command_skipped) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "bad": {"transport": "stdio"},
            "good": {"url": "http://localhost:3001"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(1));
  CHECK_EQ(cfg.mcp_servers[0].name, std::string("good"));
  fs::remove(toml_path);
  fs::remove(json_path);
}

TEST(config_mcp_unknown_transport_skipped) {
  std::string json_path = create_temp_json(R"({
        "mcpServers": {
            "bad": {"transport": "websocket", "url": "ws://localhost:9000"},
            "good": {"url": "http://localhost:3001"}
        }
    })");
  std::string toml_path =
      create_temp_toml("mcp_config = \"" + json_path + "\"\n");
  Config cfg = Config::load(toml_path);
  CHECK_EQ(cfg.mcp_servers.size(), size_t(1));
  CHECK_EQ(cfg.mcp_servers[0].name, std::string("good"));
  fs::remove(toml_path);
  fs::remove(json_path);
}

// ============================================================================
// compaction_keep_recent tests
// ============================================================================

TEST(config_compaction_keep_recent_default) {
  Config cfg = Config::defaults();
  CHECK_EQ(cfg.compaction_keep_recent, size_t(8));
}

TEST(config_compaction_keep_recent_parsed_from_toml) {
  std::string path = create_temp_toml("compaction_keep_recent = 12\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.compaction_keep_recent, size_t(12));
  fs::remove(path);
}

// ============================================================================
// Path resolution and error handling
// ============================================================================

TEST(config_load_explicit_path_takes_priority) {
  // Write two TOML files with different model names
  std::string explicit_path = create_temp_toml("model = \"explicit-model\"\n");
  std::string other_path = create_temp_toml("model = \"other-model\"\n");
  Config cfg = Config::load(explicit_path);
  CHECK_EQ(cfg.model, std::string("explicit-model"));
  fs::remove(explicit_path);
  fs::remove(other_path);
}

TEST(config_load_silent_fail_bad_toml) {
  // A field that cannot be parsed as integer should not throw; field keeps its
  // default
  std::string path = create_temp_toml("timeout_sec = not_a_number\n");
  Config cfg;
  // Must not throw
  try {
    cfg = Config::load(path);
  } catch (...) {
    fs::remove(path);
    throw;
  }
  // timeout_sec falls back to default (30) because the entire file parse fails
  // silently
  CHECK_EQ(cfg.timeout_sec, 30);
  fs::remove(path);
}

TEST(config_load_ccl_config_env_priority) {
  std::string env_path = create_temp_toml("model = \"env-model\"\n");
  setenv("CCL_CONFIG", env_path.c_str(), 1);
  Config cfg = Config::load("");
  unsetenv("CCL_CONFIG");
  CHECK_EQ(cfg.model, std::string("env-model"));
  fs::remove(env_path);
}

TEST(config_editor_field_parsed) {
  std::string path = create_temp_toml("editor = \"vim\"\n");
  Config cfg = Config::load(path);
  CHECK_EQ(cfg.editor, std::string("vim"));
  fs::remove(path);
}

TEST(config_expand_home_no_crash_when_home_unset) {
  // Save and unset HOME
  const char *saved_home = std::getenv("HOME");
  std::string saved(saved_home ? saved_home : "");
  unsetenv("HOME");

  // Config::load with a non-existent path starting with ~/ must not crash
  Config cfg;
  try {
    cfg = Config::load("~/nonexistent/config.toml");
  } catch (...) {
  }

  if (!saved.empty())
    setenv("HOME", saved.c_str(), 1);
  // Just checking no crash / no segfault — result is defaults
  CHECK_EQ(cfg.timeout_sec, 30);
}
