#include "harness.h"
#include "../src/tools.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// RAII helper for temp directory
struct TmpDir {
    std::string path;
    TmpDir() {
        path = fs::temp_directory_path().string() + "/ccl_test_" + std::to_string(std::rand());
        fs::create_directories(path);
    }
    ~TmpDir() {
        if (fs::exists(path)) {
            fs::remove_all(path);
        }
    }
};

TEST(tool_read_file_existing) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "Hello, world!";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK_EQ(result.content, std::string("Hello, world!"));
}

TEST(tool_read_file_missing) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/file.txt");

    ToolResult result = tool_read_file(args);
    CHECK(!result.success);
    CHECK(!result.error.empty());
}

TEST(tool_list_dir_existing) {
    TmpDir tmp;
    {
        std::ofstream f(tmp.path + "/file1.txt");
        f << "test";
    }
    {
        std::ofstream f(tmp.path + "/file2.txt");
        f << "test";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_list_dir(args);
    CHECK(result.success);
    // Should contain file names
    CHECK(result.content.find("file1.txt") != std::string::npos);
    CHECK(result.content.find("file2.txt") != std::string::npos);
}

TEST(tool_list_dir_missing) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/dir");

    ToolResult result = tool_list_dir(args);
    CHECK(!result.success);
}

TEST(tool_file_info_existing) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "test content";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);

    ToolResult result = tool_file_info(args);
    CHECK(result.success);
    CHECK(result.content.find("exists: true") != std::string::npos);
    CHECK(result.content.find("is_file: true") != std::string::npos);
}

TEST(tool_file_info_missing) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/path");

    ToolResult result = tool_file_info(args);
    CHECK(result.success);
    CHECK(result.content.find("exists: false") != std::string::npos);
}

TEST(tool_search_files_finds_match) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "hello world\n";
        f << "goodbye world\n";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"] = JsonValue();
    args["pattern"].data.emplace<std::string>("hello");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK(result.content.find("hello") != std::string::npos);
}

TEST(tool_search_files_no_match) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";
    {
        std::ofstream f(file_path);
        f << "hello world\n";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"] = JsonValue();
    args["pattern"].data.emplace<std::string>("nomatch");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK_EQ(result.content, std::string(""));
}

TEST(tool_registry_find_existing) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Explore, cfg);

    auto tool = registry.find("read_file");
    CHECK(tool.has_value());
    CHECK_EQ(tool.value()->def.name, std::string("read_file"));
}

TEST(tool_registry_find_missing) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Explore, cfg);

    auto tool = registry.find("nonexistent_tool");
    CHECK(!tool.has_value());
}

TEST(tool_registry_definitions_count) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Explore, cfg);

    auto defs = registry.definitions();
    // Should have 4 read-only tools
    CHECK_EQ(defs.size(), size_t(4));
}

TEST(tool_result_ok_fields) {
    ToolResult result = ToolResult::ok("success message");
    CHECK(result.success);
    CHECK_EQ(result.content, std::string("success message"));
    CHECK_EQ(result.error, std::string(""));
}

TEST(tool_result_fail_fields) {
    ToolResult result = ToolResult::fail("error message");
    CHECK(!result.success);
    CHECK_EQ(result.content, std::string(""));
    CHECK_EQ(result.error, std::string("error message"));
}

// ============================================================================
// Write tool tests
// ============================================================================

TEST(tool_write_file_creates_new_file) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/new_file.txt";

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);
    args["content"] = JsonValue();
    args["content"].data.emplace<std::string>("Hello, world!");

    ToolResult result = tool_write_file(args);
    CHECK(result.success);

    // Verify file exists with correct content
    std::ifstream file(file_path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK_EQ(content, std::string("Hello, world!"));

    // Verify no .tmp file remains
    CHECK(!fs::exists(file_path + ".ccl.tmp"));
}

TEST(tool_write_file_overwrites_existing) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/existing.txt";

    // Create initial file
    {
        std::ofstream f(file_path);
        f << "Old content";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);
    args["content"] = JsonValue();
    args["content"].data.emplace<std::string>("New content");

    ToolResult result = tool_write_file(args);
    CHECK(result.success);

    // Verify file has new content
    std::ifstream file(file_path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK_EQ(content, std::string("New content"));
}

TEST(tool_write_file_diff_in_result) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/test.txt";

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);
    args["content"] = JsonValue();
    args["content"].data.emplace<std::string>("line1\nline2");

    ToolResult result = tool_write_file(args);
    CHECK(result.success);
    // Diff should be in content
    CHECK(result.content.find("+++") != std::string::npos || result.content.find("line1") != std::string::npos);
}

TEST(tool_write_file_atomic_no_tmp_remaining) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/atomic.txt";
    std::string tmp_path = file_path + ".ccl.tmp";

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);
    args["content"] = JsonValue();
    args["content"].data.emplace<std::string>("atomic write");

    ToolResult result = tool_write_file(args);
    CHECK(result.success);

    // Verify .tmp file was cleaned up
    CHECK(!fs::exists(tmp_path));
    // Verify actual file exists
    CHECK(fs::exists(file_path));
}

TEST(tool_create_dir_creates_directory) {
    TmpDir tmp;
    std::string dir_path = tmp.path + "/newdir";

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(dir_path);

    ToolResult result = tool_create_dir(args);
    CHECK(result.success);
    CHECK(fs::exists(dir_path));
    CHECK(fs::is_directory(dir_path));
}

TEST(tool_create_dir_nested_path) {
    TmpDir tmp;
    std::string dir_path = tmp.path + "/a/b/c";

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(dir_path);

    ToolResult result = tool_create_dir(args);
    CHECK(result.success);
    CHECK(fs::exists(dir_path));
}

TEST(tool_create_dir_already_exists_ok) {
    TmpDir tmp;
    std::string dir_path = tmp.path + "/existing";
    fs::create_directories(dir_path);

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(dir_path);

    ToolResult result = tool_create_dir(args);
    CHECK(result.success);
}

TEST(tool_delete_file_removes_file) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/to_delete.txt";

    // Create file
    {
        std::ofstream f(file_path);
        f << "delete me";
    }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(file_path);

    ToolResult result = tool_delete_file(args);
    CHECK(result.success);
    CHECK(!fs::exists(file_path));
}

TEST(tool_delete_file_missing_returns_error) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/file.txt");

    ToolResult result = tool_delete_file(args);
    CHECK(!result.success);
    CHECK(!result.error.empty());
}

TEST(tool_run_shell_captures_stdout) {
    ToolArgs args;
    args["command"] = JsonValue();
    args["command"].data.emplace<std::string>("echo hello");

    ToolResult result = tool_run_shell(args);
    CHECK(result.success);
    CHECK(result.content.find("hello") != std::string::npos);
}

TEST(tool_run_shell_nonzero_exit_fails) {
    ToolArgs args;
    args["command"] = JsonValue();
    args["command"].data.emplace<std::string>("exit 1");

    ToolResult result = tool_run_shell(args);
    CHECK(!result.success);
    CHECK(result.error.find("exit 1") != std::string::npos);
}

TEST(tool_run_shell_timeout_kills_process) {
    ToolArgs args;
    args["command"] = JsonValue();
    args["command"].data.emplace<std::string>("sleep 10");
    args["timeout_sec"] = JsonValue();
    args["timeout_sec"].data.emplace<double>(1.0);

    ToolResult result = tool_run_shell(args);
    CHECK(!result.success);
    CHECK(result.error.find("timeout") != std::string::npos);
}

TEST(tool_registry_act_mode_has_write_tools) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Act, cfg);

    auto defs = registry.definitions();
    // Should have 8 tools: 4 read-only + 4 write
    CHECK_EQ(defs.size(), size_t(8));

    // Verify write tools are present
    auto write_file = registry.find("write_file");
    auto delete_file = registry.find("delete_file");
    CHECK(write_file.has_value());
    CHECK(delete_file.has_value());
}
