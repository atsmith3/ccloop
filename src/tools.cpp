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

#include "tools.h"
#include "signals.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <signal.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static int count_lines(const std::string &s) {
  if (s.empty())
    return 0;
  int n = (int)std::count(s.begin(), s.end(), '\n');
  if (s.back() != '\n')
    ++n;
  return n;
}

// ============================================================================
// ToolResult implementation
// ============================================================================

ToolResult ToolResult::ok(std::string content) {
  return ToolResult{true, std::move(content), ""};
}

ToolResult ToolResult::fail(std::string error) {
  return ToolResult{false, "", std::move(error)};
}

std::string ToolResult::to_context_string() const {
  if (success) {
    return content;
  } else {
    return "[ERROR] " + error;
  }
}

// ============================================================================
// ToolRegistry implementation
// ============================================================================

void ToolRegistry::register_tool(Tool tool) {
  index_[tool.def.name] = tools_.size();
  tools_.push_back(std::move(tool));
}

std::vector<ToolDef> ToolRegistry::definitions() const {
  std::vector<ToolDef> defs;
  for (const auto &tool : tools_) {
    defs.push_back(tool.def);
  }
  return defs;
}

std::optional<const Tool *> ToolRegistry::find(const std::string &name) const {
  auto it = index_.find(name);
  if (it == index_.end())
    return std::nullopt;
  return &tools_[it->second];
}

// ============================================================================
// Argument helpers
// ============================================================================

// Expand a leading ~ to $HOME (std::filesystem does not do this automatically).
static std::string expand_tilde(const std::string &path) {
  if (path.empty() || path[0] != '~')
    return path;
  const char *home = std::getenv("HOME");
  if (!home)
    return path;
  return std::string(home) + path.substr(1);
}

// Extract a required string argument; sets err and returns nullopt on failure.
static std::optional<std::string>
arg_str(const ToolArgs &args, const std::string &key, std::string &err) {
  auto it = args.find(key);
  if (it == args.end()) {
    err = "Missing '" + key + "' argument";
    return {};
  }
  if (!it->second.is_string()) {
    err = "'" + key + "' must be a string";
    return {};
  }
  return it->second.as_string();
}

// Extract a required path argument, expanding ~ automatically.
static std::optional<std::string>
arg_path(const ToolArgs &args, const std::string &key, std::string &err) {
  auto p = arg_str(args, key, err);
  if (!p)
    return {};
  return expand_tilde(*p);
}

// Extract an optional integer argument from string or number type.
static std::optional<int> arg_int(const ToolArgs &args,
                                  const std::string &key) {
  auto it = args.find(key);
  if (it == args.end())
    return std::nullopt;
  if (it->second.is_number())
    return static_cast<int>(it->second.as_number());
  if (it->second.is_string()) {
    try {
      return std::stoi(it->second.as_string());
    } catch (...) {
    }
  }
  return std::nullopt;
}

// ============================================================================
// Tool implementations
// ============================================================================

ToolResult tool_read_file(const ToolArgs &args) {
  std::string err;
  auto path = arg_path(args, "path", err);
  if (!path)
    return ToolResult::fail(err);

  std::ifstream file(*path);
  if (!file)
    return ToolResult::fail("File not found: " + *path);

  auto offset_opt = arg_int(args, "offset");
  auto limit_opt = arg_int(args, "limit");

  if (!offset_opt && !limit_opt) {
    std::ostringstream ss;
    ss << file.rdbuf();
    return ToolResult::ok(ss.str());
  }

  int start_line = offset_opt.value_or(1);
  int max_lines = limit_opt.value_or(std::numeric_limits<int>::max());
  if (start_line < 1)
    start_line = 1;

  std::ostringstream ss;
  std::string line;
  int line_no = 0, lines_written = 0;
  while (lines_written < max_lines && std::getline(file, line)) {
    ++line_no;
    if (line_no < start_line)
      continue;
    ss << line << "\n";
    ++lines_written;
  }
  if (lines_written == 0)
    return ToolResult::ok("(offset " + std::to_string(start_line) +
                          " is past end of file: " + *path + ")");
  return ToolResult::ok(ss.str());
}

// ============================================================================
// Write tool implementations
// ============================================================================

// Helper: write content atomically via a .ccl.tmp file; returns error string on
// failure.
static std::string atomic_write(const std::string &path,
                                const std::string &content) {
  std::string tmp_path = path + ".ccl." + std::to_string(getpid()) + ".tmp";
  {
    std::ofstream tmp_file(tmp_path);
    if (!tmp_file)
      return "Failed to create temporary file: " + tmp_path;
    tmp_file << content;
    if (!tmp_file.good())
      return "Failed to write to temporary file: " + tmp_path;
  }
  try {
    fs::rename(tmp_path, path);
  } catch (const std::exception &e) {
    try {
      fs::remove(tmp_path);
    } catch (...) {
    }
    return "Failed to rename temporary file: " + std::string(e.what());
  }
  return "";
}

ToolResult tool_write_file(const ToolArgs &args) {
  std::string err;
  auto path = arg_path(args, "path", err);
  if (!path)
    return ToolResult::fail(err);
  auto new_content = arg_str(args, "content", err);
  if (!new_content)
    return ToolResult::fail(err);

  try {
    std::string old_content;
    if (fs::exists(*path)) {
      std::ifstream file(*path);
      if (file) {
        std::ostringstream ss;
        ss << file.rdbuf();
        old_content = ss.str();
      }
    }

    if (old_content == *new_content)
      return ToolResult::ok("(no changes) " + *path +
                            " already has identical content.");

    std::string write_err = atomic_write(*path, *new_content);
    if (!write_err.empty())
      return ToolResult::fail(write_err);
    return ToolResult::ok("Written: " + *path + " (" +
                          std::to_string(count_lines(*new_content)) +
                          " lines)");
  } catch (const std::exception &e) {
    return ToolResult::fail(std::string(e.what()));
  }
}

ToolResult tool_edit_file(const ToolArgs &args) {
  std::string err;
  auto path = arg_path(args, "path", err);
  if (!path)
    return ToolResult::fail(err);
  auto old_str = arg_str(args, "old_str", err);
  if (!old_str)
    return ToolResult::fail(err);
  auto new_str = arg_str(args, "new_str", err);
  if (!new_str)
    return ToolResult::fail(err);

  try {
    if (!fs::exists(*path))
      return ToolResult::fail("File not found: " + *path);

    std::ifstream in(*path);
    if (!in)
      return ToolResult::fail("Cannot open file: " + *path);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    size_t pos = content.find(*old_str);
    if (pos == std::string::npos)
      return ToolResult::fail("old_str not found in file");
    if (content.find(*old_str, pos + 1) != std::string::npos)
      return ToolResult::fail("old_str is ambiguous (appears more than once)");

    std::string new_content = content.substr(0, pos) + *new_str +
                              content.substr(pos + old_str->size());
    std::string write_err = atomic_write(*path, new_content);
    if (!write_err.empty())
      return ToolResult::fail(write_err);
    int delta = count_lines(*new_str) - count_lines(*old_str);
    std::string sign = (delta >= 0) ? "+" : "";
    return ToolResult::ok("Edited: " + *path + " (" + sign +
                          std::to_string(delta) + " lines)");
  } catch (const std::exception &e) {
    return ToolResult::fail(std::string(e.what()));
  }
}

ToolResult tool_run_shell(const ToolArgs &args) {
  std::string err;
  auto command_opt = arg_str(args, "command", err);
  if (!command_opt)
    return ToolResult::fail(err);
  std::string command = *command_opt;

  // Get optional cwd
  std::string cwd;
  auto cwd_val = args.find("cwd");
  if (cwd_val != args.end() && cwd_val->second.is_string()) {
    cwd = cwd_val->second.as_string();
  }

  // Get optional timeout (default 30 seconds)
  int timeout_sec = 30;
  auto timeout_opt = arg_int(args, "timeout_sec");
  if (timeout_opt)
    timeout_sec = *timeout_opt;

  // Create pipe for stdout/stderr
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    return ToolResult::fail("Failed to create pipe");
  }

  pid_t pid = fork();
  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    return ToolResult::fail("Failed to fork");
  }

  if (pid == 0) {
    // Child process: put in its own process group so kill(-pgid) kills
    // the shell and all grandchildren (e.g. sleep spawned by sh -c)
    setpgid(0, 0);

    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    if (!cwd.empty()) {
      if (chdir(cwd.c_str()) != 0) {
        _exit(126); // cwd failed
      }
    }

    execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
    _exit(127); // exec failed
  }

  // Parent process
  close(pipefd[1]);

  // Read output with timeout
  std::ostringstream output;
  std::vector<char> buf(4096);
  auto start_time = std::chrono::steady_clock::now();
  int status = 0;
  bool timed_out = false;
  bool interrupted = false;
  bool exited = false;

  auto drain = [&]() {
    ssize_t bytes;
    while ((bytes = read(pipefd[0], buf.data(), buf.size())) > 0)
      output.write(buf.data(), bytes);
  };

  while (!exited) {
    // Honour Ctrl+C: kill child process group and stop waiting
    if (should_interrupt.load()) {
      interrupted = true;
      kill(-pid, SIGTERM);
      for (int i = 0; i < 5; ++i) {
        struct timeval tv{0, 100000}; // 100 ms
        select(0, nullptr, nullptr, nullptr, &tv);
        if (waitpid(pid, &status, WNOHANG) == pid) {
          exited = true;
          break;
        }
      }
      if (!exited) {
        kill(-pid, SIGKILL);
        waitpid(pid, &status, 0);
      }
      drain();
      break;
    }

    auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >=
        timeout_sec) {
      timed_out = true;
      kill(-pid, SIGKILL); // kill entire process group (shell + grandchildren)
      waitpid(pid, &status, 0);
      drain();
      break;
    }

    // Non-blocking check for child exit
    int wait_status = 0;
    pid_t wait_result = waitpid(pid, &wait_status, WNOHANG);
    if (wait_result == pid) {
      status = wait_status;
      exited = true;
    }

    // Try to read data (with timeout)
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipefd[0], &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms

    int select_result = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &tv);
    if (select_result > 0 && FD_ISSET(pipefd[0], &readfds)) {
      ssize_t bytes = read(pipefd[0], buf.data(), buf.size());
      if (bytes > 0) {
        output.write(buf.data(), bytes);
      } else if (bytes == 0) {
        // EOF: wait for process to complete and get exit status
        waitpid(pid, &status, 0);
        exited = true;
      }
    }
  }

  close(pipefd[0]);

  if (timed_out) {
    std::string captured = output.str();
    std::string msg =
        "timeout after " + std::to_string(timeout_sec) + " seconds";
    if (!captured.empty()) {
      msg += "\n[output before timeout]\n" + captured;
    }
    return ToolResult::fail(msg);
  }

  if (interrupted) {
    return ToolResult::fail("interrupted");
  }

  if (!exited) {
    return ToolResult::fail("command did not complete");
  }

  int exit_code = -1;
  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  }

  if (exit_code != 0) {
    return ToolResult::fail("exit " + std::to_string(exit_code) + ": " +
                            output.str());
  }

  std::string result = output.str();
  return ToolResult::ok(result.empty() ? "exit 0" : result);
}
