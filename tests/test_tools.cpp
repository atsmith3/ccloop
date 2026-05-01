#include "harness.h"
#include "../src/tools.h"
#include <chrono>
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
    // 4 read-only tools + find_symbol + present_plan + print + run_shell + spawn_agent
    CHECK_EQ(defs.size(), size_t(9));
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
    CHECK(result.content == "Written: " + file_path + " (2 lines)");
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

TEST(tool_run_shell_timeout_sec_override) {
    ToolArgs args;
    args["command"] = JsonValue(); args["command"].data.emplace<std::string>("sleep 100");
    args["timeout_sec"] = JsonValue(); args["timeout_sec"].data.emplace<std::string>("1");

    auto t0 = std::chrono::steady_clock::now();
    ToolResult result = tool_run_shell(args);
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - t0).count();

    CHECK(!result.success);
    CHECK(result.error.find("timeout after 1") != std::string::npos);
    CHECK(elapsed < 15);  // must not wait 30s
}

TEST(tool_run_shell_timeout_captures_output) {
    ToolArgs args;
    args["command"] = JsonValue();
    args["command"].data.emplace<std::string>("echo started && sleep 100");
    args["timeout_sec"] = JsonValue(); args["timeout_sec"].data.emplace<std::string>("1");

    ToolResult result = tool_run_shell(args);
    CHECK(!result.success);
    CHECK(result.error.find("started") != std::string::npos);
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
    // Should have 13 tools: 4 read-only + find_symbol + print + run_shell + spawn_agent + 5 write (write_file, edit_file, create_dir, delete_file, delete_dir)
    CHECK_EQ(defs.size(), size_t(13));

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
    CHECK(result.content == "Edited: " + file_path + " (+0 lines)");
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

// ============================================================================
// Additional edge case tests
// ============================================================================

TEST(tool_search_files_invalid_regex) {
    TmpDir tmp;
    ToolArgs args;
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"].data.emplace<std::string>("[unclosed");

    ToolResult result = tool_search_files(args);
    CHECK(!result.success);
    CHECK(result.error.find("invalid pattern") != std::string::npos);
}

TEST(tool_search_files_output_truncated) {
    TmpDir tmp;
    std::string filepath = tmp.path + "/bigfile.txt";
    {
        std::ofstream f(filepath);
        // 100 lines × ~90 chars each ≈ 9000 bytes — exceeds the 8000-byte cap
        for (int i = 0; i < 100; ++i) {
            f << "FINDME " << std::string(80, 'x') << " line" << i << "\n";
        }
    }

    ToolArgs args;
    args["path"].data.emplace<std::string>(tmp.path);
    args["pattern"].data.emplace<std::string>("FINDME");

    ToolResult result = tool_search_files(args);
    CHECK(result.success);
    CHECK(result.content.find("truncated") != std::string::npos);
}

TEST(tool_edit_file_multiline_old_str) {
    TmpDir tmp;
    std::string path = tmp.path + "/multi.txt";
    {
        std::ofstream f(path);
        f << "line1\nline2\nline3\n";
    }

    ToolArgs args;
    args["path"].data.emplace<std::string>(path);
    args["old_str"].data.emplace<std::string>("line1\nline2");
    args["new_str"].data.emplace<std::string>("REPLACED");

    ToolResult result = tool_edit_file(args);
    CHECK(result.success);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    CHECK(content.find("REPLACED") != std::string::npos);
    CHECK(content.find("line1") == std::string::npos);
    CHECK(content.find("line3") != std::string::npos);
}

TEST(tool_edit_file_empty_old_str_ambiguous) {
    TmpDir tmp;
    std::string path = tmp.path + "/file.txt";
    {
        std::ofstream f(path);
        f << "hello world\n";
    }

    ToolArgs args;
    args["path"].data.emplace<std::string>(path);
    args["old_str"].data.emplace<std::string>("");
    args["new_str"].data.emplace<std::string>("X");

    ToolResult result = tool_edit_file(args);
    CHECK(!result.success);
    CHECK(result.error.find("ambiguous") != std::string::npos);
}

TEST(tool_edit_file_new_str_equals_old_str) {
    TmpDir tmp;
    std::string path = tmp.path + "/file.txt";
    {
        std::ofstream f(path);
        f << "unchanged content\n";
    }

    ToolArgs args;
    args["path"].data.emplace<std::string>(path);
    args["old_str"].data.emplace<std::string>("unchanged content");
    args["new_str"].data.emplace<std::string>("unchanged content");

    ToolResult result = tool_edit_file(args);
    CHECK(result.success);

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    CHECK(content.find("unchanged content") != std::string::npos);
}

TEST(tool_create_dir_file_exists_error) {
    TmpDir tmp;
    std::string path = tmp.path + "/notadir";
    {
        std::ofstream f(path);
        f << "I am a file\n";
    }

    ToolArgs args;
    args["path"].data.emplace<std::string>(path);
    ToolResult result = tool_create_dir(args);
    CHECK(!result.success);
    CHECK(result.error.find("not a directory") != std::string::npos);
}

TEST(tool_run_shell_inherits_env) {
    setenv("CCL_TEST_SENTINEL", "hello_sentinel_456", 1);

    ToolArgs args;
    args["command"].data.emplace<std::string>("echo $CCL_TEST_SENTINEL");
    ToolResult result = tool_run_shell(args);

    unsetenv("CCL_TEST_SENTINEL");

    CHECK(result.success);
    CHECK(result.content.find("hello_sentinel_456") != std::string::npos);
}

// ============================================================================
// read_file offset/limit tests
// ============================================================================

static std::string make_5line_file(const std::string& dir) {
    std::string path = dir + "/lines.txt";
    std::ofstream f(path);
    f << "line1\nline2\nline3\nline4\nline5\n";
    return path;
}

TEST(tool_read_file_offset_reads_from_line) {
    TmpDir tmp;
    std::string path = make_5line_file(tmp.path);

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(path);
    args["offset"] = JsonValue(); args["offset"].data.emplace<double>(3);

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK(result.content.find("line1") == std::string::npos);
    CHECK(result.content.find("line3") != std::string::npos);
    CHECK(result.content.find("line5") != std::string::npos);
}

TEST(tool_read_file_limit_reads_n_lines) {
    TmpDir tmp;
    std::string path = make_5line_file(tmp.path);

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(path);
    args["limit"] = JsonValue(); args["limit"].data.emplace<double>(2);

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK(result.content.find("line1") != std::string::npos);
    CHECK(result.content.find("line2") != std::string::npos);
    CHECK(result.content.find("line3") == std::string::npos);
}

TEST(tool_read_file_offset_and_limit) {
    TmpDir tmp;
    std::string path = make_5line_file(tmp.path);

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(path);
    args["offset"] = JsonValue(); args["offset"].data.emplace<double>(2);
    args["limit"] = JsonValue(); args["limit"].data.emplace<double>(2);

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK(result.content.find("line1") == std::string::npos);
    CHECK(result.content.find("line2") != std::string::npos);
    CHECK(result.content.find("line3") != std::string::npos);
    CHECK(result.content.find("line4") == std::string::npos);
}

TEST(tool_read_file_offset_beyond_eof_returns_empty) {
    TmpDir tmp;
    std::string path = tmp.path + "/short.txt";
    { std::ofstream f(path); f << "a\nb\nc\n"; }

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(path);
    args["offset"] = JsonValue(); args["offset"].data.emplace<double>(10);

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK(result.content.empty());
}

TEST(tool_read_file_limit_larger_than_file_returns_all) {
    TmpDir tmp;
    std::string path = tmp.path + "/short.txt";
    { std::ofstream f(path); f << "a\nb\nc\n"; }

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(path);
    args["limit"] = JsonValue(); args["limit"].data.emplace<double>(100);

    ToolResult result = tool_read_file(args);
    CHECK(result.success);
    CHECK(result.content.find("a") != std::string::npos);
    CHECK(result.content.find("c") != std::string::npos);
}

TEST(tool_read_file_offset_1_same_as_no_offset) {
    TmpDir tmp;
    std::string path = make_5line_file(tmp.path);

    ToolArgs no_offset_args;
    no_offset_args["path"] = JsonValue();
    no_offset_args["path"].data.emplace<std::string>(path);
    ToolResult no_offset = tool_read_file(no_offset_args);

    ToolArgs offset1_args;
    offset1_args["path"] = JsonValue(); offset1_args["path"].data.emplace<std::string>(path);
    offset1_args["offset"] = JsonValue(); offset1_args["offset"].data.emplace<double>(1);
    ToolResult with_offset = tool_read_file(offset1_args);

    CHECK(no_offset.success);
    CHECK(with_offset.success);
    CHECK_EQ(no_offset.content, with_offset.content);
}

// ============================================================================
// delete_dir tests
// ============================================================================

TEST(tool_delete_dir_empty_dir) {
    TmpDir tmp;
    std::string dir = tmp.path + "/toremove";
    fs::create_directories(dir);

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(dir);

    ToolResult result = tool_delete_dir(args);
    CHECK(result.success);
    CHECK(!fs::exists(dir));
}

TEST(tool_delete_dir_nonempty_requires_recursive) {
    TmpDir tmp;
    std::string dir = tmp.path + "/toremove";
    fs::create_directories(dir);
    { std::ofstream f(dir + "/file.txt"); f << "content"; }

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(dir);

    ToolResult result = tool_delete_dir(args);
    CHECK(!result.success);
    CHECK(result.error.find("not empty") != std::string::npos);
    CHECK(fs::exists(dir));
}

TEST(tool_delete_dir_recursive_deletes_all_contents) {
    TmpDir tmp;
    std::string dir = tmp.path + "/toremove";
    fs::create_directories(dir + "/sub");
    { std::ofstream f(dir + "/file.txt"); f << "data"; }
    { std::ofstream f(dir + "/sub/nested.txt"); f << "data"; }

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(dir);
    args["recursive"] = JsonValue(); args["recursive"].data.emplace<bool>(true);

    ToolResult result = tool_delete_dir(args);
    CHECK(result.success);
    CHECK(!fs::exists(dir));
}

TEST(tool_delete_dir_missing_path_fails) {
    ToolArgs args;
    args["path"] = JsonValue();
    args["path"].data.emplace<std::string>("/nonexistent/path/xyz");

    ToolResult result = tool_delete_dir(args);
    CHECK(!result.success);
}

TEST(tool_delete_dir_on_file_fails) {
    TmpDir tmp;
    std::string file = tmp.path + "/afile.txt";
    { std::ofstream f(file); f << "hello"; }

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(file);

    ToolResult result = tool_delete_dir(args);
    CHECK(!result.success);
    CHECK(result.error.find("not a directory") != std::string::npos);
}

TEST(tool_delete_dir_explicit_recursive_false_nonempty_fails) {
    TmpDir tmp;
    std::string dir = tmp.path + "/toremove";
    fs::create_directories(dir);
    { std::ofstream f(dir + "/file.txt"); f << "content"; }

    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(dir);
    args["recursive"] = JsonValue(); args["recursive"].data.emplace<bool>(false);

    ToolResult result = tool_delete_dir(args);
    CHECK(!result.success);
}

// ============================================================================
// find_symbol tests
// ============================================================================

TEST(tool_find_symbol_basic_match) {
    TmpDir tmp;
    std::string file = tmp.path + "/code.cpp";
    { std::ofstream f(file); f << "void myFunction() {}\n"; }

    ToolArgs args;
    args["symbol"] = JsonValue(); args["symbol"].data.emplace<std::string>("myFunction");
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_find_symbol(args);
    CHECK(result.success);
    CHECK(result.content.find("myFunction") != std::string::npos);
    CHECK(result.content.find("code.cpp") != std::string::npos);
}

TEST(tool_find_symbol_no_match_returns_message) {
    TmpDir tmp;
    std::string file = tmp.path + "/code.cpp";
    { std::ofstream f(file); f << "void otherFunc() {}\n"; }

    ToolArgs args;
    args["symbol"] = JsonValue(); args["symbol"].data.emplace<std::string>("nonexistent_xyz");
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_find_symbol(args);
    CHECK(result.success);
    CHECK(result.content.find("no matches") != std::string::npos);
}

TEST(tool_find_symbol_word_boundary_no_partial) {
    TmpDir tmp;
    std::string file = tmp.path + "/code.cpp";
    { std::ofstream f(file); f << "void myFunctionHelper() {}\n"; }

    ToolArgs args;
    args["symbol"] = JsonValue(); args["symbol"].data.emplace<std::string>("myFunction");
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_find_symbol(args);
    CHECK(result.success);
    CHECK(result.content.find("no matches") != std::string::npos);
}

TEST(tool_find_symbol_language_filter_matches_cpp) {
    TmpDir tmp;
    std::string cpp_file = tmp.path + "/code.cpp";
    std::string py_file  = tmp.path + "/code.py";
    { std::ofstream f(cpp_file); f << "void myFunc() {}\n"; }
    { std::ofstream f(py_file);  f << "def myFunc(): pass\n"; }

    ToolArgs args;
    args["symbol"]   = JsonValue(); args["symbol"].data.emplace<std::string>("myFunc");
    args["language"] = JsonValue(); args["language"].data.emplace<std::string>("cpp");
    args["path"]     = JsonValue(); args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_find_symbol(args);
    CHECK(result.success);
    CHECK(result.content.find("code.cpp") != std::string::npos);
    CHECK(result.content.find("code.py") == std::string::npos);
}

TEST(tool_find_symbol_language_filter_excludes_cpp) {
    TmpDir tmp;
    std::string cpp_file = tmp.path + "/code.cpp";
    std::string py_file  = tmp.path + "/code.py";
    { std::ofstream f(cpp_file); f << "void myFunc() {}\n"; }
    { std::ofstream f(py_file);  f << "def myFunc(): pass\n"; }

    ToolArgs args;
    args["symbol"]   = JsonValue(); args["symbol"].data.emplace<std::string>("myFunc");
    args["language"] = JsonValue(); args["language"].data.emplace<std::string>("python");
    args["path"]     = JsonValue(); args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_find_symbol(args);
    CHECK(result.success);
    CHECK(result.content.find("code.py") != std::string::npos);
    CHECK(result.content.find("code.cpp") == std::string::npos);
}

TEST(tool_find_symbol_multiple_files) {
    TmpDir tmp;
    std::string file_a = tmp.path + "/a.cpp";
    std::string file_b = tmp.path + "/b.cpp";
    { std::ofstream f(file_a); f << "void myFunc() {}\n"; }
    { std::ofstream f(file_b); f << "void myFunc() {}\n"; }

    ToolArgs args;
    args["symbol"] = JsonValue(); args["symbol"].data.emplace<std::string>("myFunc");
    args["path"]   = JsonValue(); args["path"].data.emplace<std::string>(tmp.path);

    ToolResult result = tool_find_symbol(args);
    CHECK(result.success);
    CHECK(result.content.find("a.cpp") != std::string::npos);
    CHECK(result.content.find("b.cpp") != std::string::npos);
}

TEST(tool_find_symbol_missing_symbol_arg_fails) {
    ToolArgs args;
    args["path"] = JsonValue(); args["path"].data.emplace<std::string>(".");

    ToolResult result = tool_find_symbol(args);
    CHECK(!result.success);
    CHECK(result.error.find("symbol") != std::string::npos);
}

TEST(tool_find_symbol_path_restricts_search) {
    TmpDir tmp;
    std::string dir_a = tmp.path + "/a";
    std::string dir_b = tmp.path + "/b";
    fs::create_directories(dir_a);
    fs::create_directories(dir_b);
    { std::ofstream f(dir_a + "/code.cpp"); f << "void myFunc() {}\n"; }
    { std::ofstream f(dir_b + "/code.cpp"); f << "void myFunc() {}\n"; }

    ToolArgs args;
    args["symbol"] = JsonValue(); args["symbol"].data.emplace<std::string>("myFunc");
    args["path"]   = JsonValue(); args["path"].data.emplace<std::string>(dir_a);

    ToolResult result = tool_find_symbol(args);
    CHECK(result.success);
    CHECK(result.content.find(dir_a) != std::string::npos);
    CHECK(result.content.find(dir_b) == std::string::npos);
}
