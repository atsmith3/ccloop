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

TEST(tool_search_files_no_pattern_lists_files) {
    TmpDir tmp;
    { std::ofstream f(tmp.path + "/readme.txt"); f << "hello\n"; }
    { std::ofstream f(tmp.path + "/main.cpp");  f << "hello\n"; }

    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>(tmp.path);
    args["file_glob"] = JsonValue();
    args["file_glob"].data.emplace<std::string>("*.txt");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK(result.content.find("readme.txt") != std::string::npos);
    CHECK(result.content.find("main.cpp") == std::string::npos);
}

TEST(tool_registry_find_existing) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Plan, cfg);

    auto tool = registry.find("read_file");
    CHECK(tool.has_value());
    CHECK_EQ(tool.value()->def.name, std::string("read_file"));
}

TEST(tool_registry_find_missing) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Plan, cfg);

    auto tool = registry.find("nonexistent_tool");
    CHECK(!tool.has_value());
}

TEST(tool_registry_definitions_count) {
    Config cfg = Config::defaults();
    ToolRegistry registry = make_registry(AgentMode::Plan, cfg);

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
    // Should have 9 tools: 4 read-only + 5 write (write_file, edit_file, create_dir, delete_file, run_shell)
    CHECK_EQ(defs.size(), size_t(9));

    // Verify write tools are present
    auto write_file = registry.find("write_file");
    auto edit_file  = registry.find("edit_file");
    auto delete_file = registry.find("delete_file");
    CHECK(write_file.has_value());
    CHECK(edit_file.has_value());
    CHECK(delete_file.has_value());
}

// ============================================================================
// edit_file tool tests
// ============================================================================

TEST(tool_edit_file_replaces_string) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/edit.txt";
    { std::ofstream f(file_path); f << "hello world\n"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>(file_path);
    args["old_str"].data.emplace<std::string>("hello");
    args["new_str"].data.emplace<std::string>("goodbye");

    ToolResult result = tool_edit_file(args);
    CHECK(result.success);

    std::ifstream f(file_path);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(content.find("goodbye") != std::string::npos);
    CHECK(content.find("hello") == std::string::npos);
}

TEST(tool_edit_file_not_found_returns_error) {
    ToolArgs args;
    args["path"].data.emplace<std::string>("/nonexistent/file.txt");
    args["old_str"].data.emplace<std::string>("foo");
    args["new_str"].data.emplace<std::string>("bar");

    ToolResult result = tool_edit_file(args);
    CHECK(!result.success);
}

TEST(tool_edit_file_old_str_missing_returns_error) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/edit.txt";
    { std::ofstream f(file_path); f << "hello world\n"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>(file_path);
    args["old_str"].data.emplace<std::string>("nomatch");
    args["new_str"].data.emplace<std::string>("bar");

    ToolResult result = tool_edit_file(args);
    CHECK(!result.success);
    CHECK(result.error.find("not found") != std::string::npos);
}

TEST(tool_edit_file_ambiguous_returns_error) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/edit.txt";
    { std::ofstream f(file_path); f << "foo foo\n"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>(file_path);
    args["old_str"].data.emplace<std::string>("foo");
    args["new_str"].data.emplace<std::string>("bar");

    ToolResult result = tool_edit_file(args);
    CHECK(!result.success);
    CHECK(result.error.find("ambiguous") != std::string::npos);
}

TEST(tool_edit_file_diff_in_result) {
    TmpDir tmp;
    std::string file_path = tmp.path + "/edit.txt";
    { std::ofstream f(file_path); f << "line1\nline2\n"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>(file_path);
    args["old_str"].data.emplace<std::string>("line2");
    args["new_str"].data.emplace<std::string>("lineX");

    ToolResult result = tool_edit_file(args);
    CHECK(result.success);
    CHECK(result.content.find("lineX") != std::string::npos ||
          result.content.find("+++")   != std::string::npos);
}

// ============================================================================
// search_files file_glob filter tests
// ============================================================================

TEST(tool_search_files_glob_filter_matches) {
    TmpDir tmp;
    { std::ofstream f(tmp.path + "/code.cpp"); f << "hello cpp\n"; }
    { std::ofstream f(tmp.path + "/notes.txt"); f << "hello txt\n"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"].data.emplace<std::string>("hello");
    args["file_glob"].data.emplace<std::string>("*.cpp");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK(result.content.find("code.cpp") != std::string::npos);
    CHECK(result.content.find("notes.txt") == std::string::npos);
}

TEST(tool_search_files_skips_hidden_dirs) {
    TmpDir tmp;
    fs::create_directories(tmp.path + "/.git");
    { std::ofstream f(tmp.path + "/.git/secret"); f << "hello secret\n"; }
    { std::ofstream f(tmp.path + "/visible.txt"); f << "hello visible\n"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"].data.emplace<std::string>("hello");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK(result.content.find("visible.txt") != std::string::npos);
    CHECK(result.content.find(".git") == std::string::npos);
}

// ============================================================================
// Tilde expansion tests
// ============================================================================

TEST(tool_write_file_expands_tilde) {
    const char* home = std::getenv("HOME");
    if (!home) return;  // skip if HOME not set

    std::string real_path = std::string(home) + "/ccl_tilde_test.txt";
    // Clean up any leftover from a previous run
    fs::remove(real_path);

    ToolArgs args;
    args["path"].data.emplace<std::string>("~/ccl_tilde_test.txt");
    args["content"].data.emplace<std::string>("tilde test");

    ToolResult result = tool_write_file(args);
    CHECK(result.success);
    CHECK(fs::exists(real_path));

    fs::remove(real_path);  // clean up
}

TEST(tool_read_file_expands_tilde) {
    const char* home = std::getenv("HOME");
    if (!home) return;

    std::string real_path = std::string(home) + "/ccl_tilde_test.txt";
    { std::ofstream f(real_path); f << "hello tilde"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>("~/ccl_tilde_test.txt");

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK_EQ(result.content, std::string("hello tilde"));

    fs::remove(real_path);  // clean up
}

TEST(tool_list_dir_expands_tilde) {
    const char* home = std::getenv("HOME");
    if (!home) return;

    ToolArgs args;
    args["path"].data.emplace<std::string>("~");

    ToolResult result = tool_list_dir(args);
    CHECK(result.success);
}

TEST(tool_file_info_expands_tilde) {
    const char* home = std::getenv("HOME");
    if (!home) return;

    ToolArgs args;
    args["path"].data.emplace<std::string>("~");

    ToolResult result = tool_file_info(args);
    CHECK(result.success);
    CHECK(result.content.find("exists: true") != std::string::npos);
}

TEST(tool_create_dir_expands_tilde) {
    const char* home = std::getenv("HOME");
    if (!home) return;

    std::string real_path = std::string(home) + "/ccl_tilde_dir_test";
    fs::remove_all(real_path);

    ToolArgs args;
    args["path"].data.emplace<std::string>("~/ccl_tilde_dir_test");

    ToolResult result = tool_create_dir(args);
    CHECK(result.success);
    CHECK(fs::exists(real_path));

    fs::remove_all(real_path);
}

TEST(tool_edit_file_expands_tilde) {
    const char* home = std::getenv("HOME");
    if (!home) return;

    std::string real_path = std::string(home) + "/ccl_tilde_edit_test.txt";
    { std::ofstream f(real_path); f << "original content\n"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>("~/ccl_tilde_edit_test.txt");
    args["old_str"].data.emplace<std::string>("original");
    args["new_str"].data.emplace<std::string>("replaced");

    ToolResult result = tool_edit_file(args);
    CHECK(result.success);

    std::ifstream f(real_path);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(content.find("replaced") != std::string::npos);

    fs::remove(real_path);
}

TEST(tool_delete_file_expands_tilde) {
    const char* home = std::getenv("HOME");
    if (!home) return;

    std::string real_path = std::string(home) + "/ccl_tilde_delete_test.txt";
    { std::ofstream f(real_path); f << "delete me"; }

    ToolArgs args;
    args["path"].data.emplace<std::string>("~/ccl_tilde_delete_test.txt");

    ToolResult result = tool_delete_file(args);
    CHECK(result.success);
    CHECK(!fs::exists(real_path));
}

TEST(tool_run_shell_cwd_sets_working_dir) {
    TmpDir tmp;

    ToolArgs args;
    args["command"].data.emplace<std::string>("pwd");
    args["cwd"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_run_shell(args);
    CHECK(result.success);
    // Output should contain the temp dir path (pwd resolves symlinks, so check suffix)
    CHECK(result.content.find(tmp.path.substr(tmp.path.rfind('/') + 1)) != std::string::npos
          || result.content.find(tmp.path) != std::string::npos);
}

// ============================================================================
// ToolResult::to_context_string tests
// ============================================================================

TEST(tool_result_to_context_string_ok) {
    ToolResult result = ToolResult::ok("file contents here");
    std::string s = result.to_context_string();
    CHECK(s.find("[ERROR]") == std::string::npos);
    CHECK_EQ(s, std::string("file contents here"));
}

TEST(tool_result_to_context_string_fail) {
    ToolResult result = ToolResult::fail("something went wrong");
    std::string s = result.to_context_string();
    CHECK(s.find("[ERROR]") != std::string::npos);
    CHECK(s.find("something went wrong") != std::string::npos);
}
