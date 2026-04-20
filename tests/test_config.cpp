#include "harness.h"
#include "../src/config.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// Helper: create temp TOML file
static std::string create_temp_toml(const std::string& content) {
    std::string tmpdir = fs::temp_directory_path().string();
    std::string path = tmpdir + "/test_config_" + std::to_string(std::rand()) + ".toml";
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
    CHECK_EQ(cfg.streaming, false);
    CHECK_EQ(cfg.token_limit, size_t(8000));
}

TEST(config_load_minimal_toml) {
    std::string path = create_temp_toml("endpoint = \"http://test:9000/v1\"\n");
    Config cfg = Config::load(path);
    CHECK_EQ(cfg.endpoint, std::string("http://test:9000/v1"));
    CHECK_EQ(cfg.model, std::string("qwen3-235b"));  // default
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
streaming   = false
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
    CHECK_EQ(cfg.streaming, false);
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

TEST(config_bool_value_parsed) {
    std::string path = create_temp_toml("streaming = false\n");
    Config cfg = Config::load(path);
    CHECK_EQ(cfg.streaming, false);
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
