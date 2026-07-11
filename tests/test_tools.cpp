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

#include "../src/tools.h"
#include "harness.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// RAII helper for temp directory
struct TmpDir {
  std::string path;
  TmpDir() {
    path = fs::temp_directory_path().string() + "/ccl_test_" +
           std::to_string(std::rand());
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

TEST(tool_registry_find_existing) {
  Config cfg = Config::defaults();
  ToolRegistry registry = make_registry(cfg);

  auto tool = registry.find("read_file");
  CHECK(tool.has_value());
  CHECK_EQ(tool.value()->def.name, std::string("read_file"));
}

TEST(tool_registry_find_missing) {
  Config cfg = Config::defaults();
  ToolRegistry registry = make_registry(cfg);

  auto tool = registry.find("nonexistent_tool");
  CHECK(!tool.has_value());
}

TEST(tool_registry_definitions_count) {
  Config cfg = Config::defaults();
  ToolRegistry registry = make_registry(cfg);

  auto defs = registry.definitions();
  // The fixed minimal toolset: read_file, write_file, edit_file, run_shell.
  CHECK_EQ(defs.size(), size_t(4));
}

TEST(tool_registry_has_minimal_toolset) {
  Config cfg = Config::defaults();
  ToolRegistry registry = make_registry(cfg);

  for (const char *name :
       {"read_file", "write_file", "edit_file", "run_shell"}) {
    CHECK(registry.find(name).has_value());
  }
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
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
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
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
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
  args["command"] = JsonValue();
  args["command"].data.emplace<std::string>("sleep 100");
  args["timeout_sec"] = JsonValue();
  args["timeout_sec"].data.emplace<std::string>("1");

  auto t0 = std::chrono::steady_clock::now();
  ToolResult result = tool_run_shell(args);
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::steady_clock::now() - t0)
                     .count();

  CHECK(!result.success);
  CHECK(result.error.find("timeout after 1") != std::string::npos);
  CHECK(elapsed < 15); // must not wait 30s
}

TEST(tool_run_shell_timeout_captures_output) {
  ToolArgs args;
  args["command"] = JsonValue();
  args["command"].data.emplace<std::string>("echo started && sleep 100");
  args["timeout_sec"] = JsonValue();
  args["timeout_sec"].data.emplace<std::string>("1");

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

TEST(tool_registry_has_write_tools) {
  Config cfg = Config::defaults();
  ToolRegistry registry = make_registry(cfg);

  // Write tools are always available now that there is a single mode.
  auto write_file = registry.find("write_file");
  auto edit_file = registry.find("edit_file");
  CHECK(write_file.has_value());
  CHECK(edit_file.has_value());
  CHECK_EQ(write_file.value()->def.permission, Permission::Write);
}

// ============================================================================
// edit_file tool tests
// ============================================================================

TEST(tool_edit_file_replaces_string) {
  TmpDir tmp;
  std::string file_path = tmp.path + "/edit.txt";
  {
    std::ofstream f(file_path);
    f << "hello world\n";
  }

  ToolArgs args;
  args["path"].data.emplace<std::string>(file_path);
  args["old_str"].data.emplace<std::string>("hello");
  args["new_str"].data.emplace<std::string>("goodbye");

  ToolResult result = tool_edit_file(args);
  CHECK(result.success);

  std::ifstream f(file_path);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
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
  {
    std::ofstream f(file_path);
    f << "hello world\n";
  }

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
  {
    std::ofstream f(file_path);
    f << "foo foo\n";
  }

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
  {
    std::ofstream f(file_path);
    f << "line1\nline2\n";
  }

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

// ============================================================================
// Tilde expansion tests
// ============================================================================

TEST(tool_write_file_expands_tilde) {
  const char *home = std::getenv("HOME");
  if (!home)
    return; // skip if HOME not set

  std::string real_path = std::string(home) + "/ccl_tilde_test.txt";
  // Clean up any leftover from a previous run
  fs::remove(real_path);

  ToolArgs args;
  args["path"].data.emplace<std::string>("~/ccl_tilde_test.txt");
  args["content"].data.emplace<std::string>("tilde test");

  ToolResult result = tool_write_file(args);
  CHECK(result.success);
  CHECK(fs::exists(real_path));

  fs::remove(real_path); // clean up
}

TEST(tool_read_file_expands_tilde) {
  const char *home = std::getenv("HOME");
  if (!home)
    return;

  std::string real_path = std::string(home) + "/ccl_tilde_test.txt";
  {
    std::ofstream f(real_path);
    f << "hello tilde";
  }

  ToolArgs args;
  args["path"].data.emplace<std::string>("~/ccl_tilde_test.txt");

  ToolResult result = tool_read_file(args);
  CHECK(result.success);
  CHECK_EQ(result.content, std::string("hello tilde"));

  fs::remove(real_path); // clean up
}

TEST(tool_edit_file_expands_tilde) {
  const char *home = std::getenv("HOME");
  if (!home)
    return;

  std::string real_path = std::string(home) + "/ccl_tilde_edit_test.txt";
  {
    std::ofstream f(real_path);
    f << "original content\n";
  }

  ToolArgs args;
  args["path"].data.emplace<std::string>("~/ccl_tilde_edit_test.txt");
  args["old_str"].data.emplace<std::string>("original");
  args["new_str"].data.emplace<std::string>("replaced");

  ToolResult result = tool_edit_file(args);
  CHECK(result.success);

  std::ifstream f(real_path);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  CHECK(content.find("replaced") != std::string::npos);

  fs::remove(real_path);
}

TEST(tool_run_shell_cwd_sets_working_dir) {
  TmpDir tmp;

  ToolArgs args;
  args["command"].data.emplace<std::string>("pwd");
  args["cwd"].data.emplace<std::string>(tmp.path);

  ToolResult result = tool_run_shell(args);
  CHECK(result.success);
  // Output should contain the temp dir path (pwd resolves symlinks, so check
  // suffix)
  CHECK(result.content.find(tmp.path.substr(tmp.path.rfind('/') + 1)) !=
            std::string::npos ||
        result.content.find(tmp.path) != std::string::npos);
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

static std::string make_5line_file(const std::string &dir) {
  std::string path = dir + "/lines.txt";
  std::ofstream f(path);
  f << "line1\nline2\nline3\nline4\nline5\n";
  return path;
}

TEST(tool_read_file_offset_reads_from_line) {
  TmpDir tmp;
  std::string path = make_5line_file(tmp.path);

  ToolArgs args;
  args["path"] = JsonValue();
  args["path"].data.emplace<std::string>(path);
  args["offset"] = JsonValue();
  args["offset"].data.emplace<double>(3);

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
  args["path"] = JsonValue();
  args["path"].data.emplace<std::string>(path);
  args["limit"] = JsonValue();
  args["limit"].data.emplace<double>(2);

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
  args["path"] = JsonValue();
  args["path"].data.emplace<std::string>(path);
  args["offset"] = JsonValue();
  args["offset"].data.emplace<double>(2);
  args["limit"] = JsonValue();
  args["limit"].data.emplace<double>(2);

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
  {
    std::ofstream f(path);
    f << "a\nb\nc\n";
  }

  ToolArgs args;
  args["path"] = JsonValue();
  args["path"].data.emplace<std::string>(path);
  args["offset"] = JsonValue();
  args["offset"].data.emplace<double>(10);

  ToolResult result = tool_read_file(args);
  CHECK(result.success);
  CHECK(result.content.find("past end of file") != std::string::npos);
}

TEST(tool_read_file_limit_larger_than_file_returns_all) {
  TmpDir tmp;
  std::string path = tmp.path + "/short.txt";
  {
    std::ofstream f(path);
    f << "a\nb\nc\n";
  }

  ToolArgs args;
  args["path"] = JsonValue();
  args["path"].data.emplace<std::string>(path);
  args["limit"] = JsonValue();
  args["limit"].data.emplace<double>(100);

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
  offset1_args["path"] = JsonValue();
  offset1_args["path"].data.emplace<std::string>(path);
  offset1_args["offset"] = JsonValue();
  offset1_args["offset"].data.emplace<double>(1);
  ToolResult with_offset = tool_read_file(offset1_args);

  CHECK(no_offset.success);
  CHECK(with_offset.success);
  CHECK_EQ(no_offset.content, with_offset.content);
}

// ============================================================================
// delete_dir tests
// ============================================================================

// ============================================================================
// find_symbol tests
// ============================================================================

// ============================================================================
// Additional edge case tests
// ============================================================================

TEST(tool_run_shell_nonzero_exit_code_in_error) {
  ToolArgs args;
  args["command"] = JsonValue();
  args["command"].data.emplace<std::string>("bash -c 'exit 42'");

  ToolResult result = tool_run_shell(args);
  CHECK(!result.success);
  CHECK(result.error.find("42") != std::string::npos);
}

TEST(tool_run_shell_bad_cwd_fails) {
  ToolArgs args;
  args["command"] = JsonValue();
  args["command"].data.emplace<std::string>("true");
  args["cwd"] = JsonValue();
  args["cwd"].data.emplace<std::string>("/nonexistent/path/xyz123abc");
  ToolResult r = tool_run_shell(args);
  CHECK(!r.success); // child exits 126 on chdir failure
}
