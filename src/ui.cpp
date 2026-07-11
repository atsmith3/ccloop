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

#include "ui.h"
#include "json.h"
#include "signals.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unistd.h>

static void trim_trailing(std::string &s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                        s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
}

void Ui::show_message(std::string_view role, std::string_view content) const {
  if (role == "system")
    return;
  if (silent_ && role != "completed")
    return;
  std::cout << "[" << role << "] " << content << "\n\n";
  std::cout.flush();
}

// Truncate a string to max_len, appending "..." if cut
static std::string truncate(const std::string &s, size_t max_len) {
  if (s.size() <= max_len)
    return s;
  return s.substr(0, max_len) + "...";
}

void Ui::show_tool_call(const ToolCall &call, ToolSource source) const {
  if (silent_)
    return;
  std::cout << "[call] " << call.name;

  size_t arg_count = 0;
  for (const auto &[key, val] : call.args) {
    if (arg_count >= 3) {
      std::cout << " ...";
      break;
    }
    std::string val_str = val.is_string() ? val.as_string() : to_json(val);
    std::cout << " " << key << "="
              << (key == "command" ? val_str : truncate(val_str, 40));
    ++arg_count;
  }

  std::cout << " (" << (source == ToolSource::Local ? "local" : "mcp") << ")\n";
  std::cout.flush();
}

void Ui::show_tool_result(const ToolCall & /*call*/,
                          const ToolResult &result) const {
  if (silent_)
    return;
  std::string status = result.success ? "OK" : "ERROR";

  if (result.success) {
    size_t content_len = result.content.length();
    std::cout << "[result] " << status << " - " << content_len << " bytes\n";
  } else {
    std::cout << "[result] " << status << ": " << result.error << "\n";
  }
  std::cout.flush();
}

void Ui::update_tokens(size_t used, size_t limit) const {
  if (silent_)
    return;
  std::cout << "tokens: " << used << "/" << limit << "\n";
  std::cout.flush();
}

void Ui::show_stats(const SessionStats &stats, const std::string &model) const {
  size_t total_messages =
      stats.user_messages + stats.assistant_messages + stats.tool_calls;
  size_t input_total = stats.input_tokens;
  size_t cached = stats.cache_read_tokens;
  size_t uncached = cached > input_total ? 0 : input_total - cached;
  double cached_pct = input_total > 0 ? (100.0 * static_cast<double>(cached) /
                                         static_cast<double>(input_total))
                                      : 0.0;

  std::cout << "Messages\n"
            << " Total: " << total_messages << "\n"
            << " User: " << stats.user_messages << "\n"
            << " Assistant: " << stats.assistant_messages << "\n"
            << " Tools: " << stats.tool_calls << " calls, "
            << stats.tool_results << " results\n\n";

  std::cout << "Tokens\n"
            << " Input: " << input_total << "\n"
            << "   Cached: " << cached << " (" << std::fixed
            << std::setprecision(1) << cached_pct << "%)\n"
            << "   Uncached: " << uncached << "\n"
            << " Output: " << stats.output_tokens << "\n"
            << " Total: " << (input_total + stats.output_tokens) << "\n\n";

  std::cout << "Cost\n"
            << " Total: $" << std::fixed << std::setprecision(6) << stats.cost
            << "\n"
            << "   " << model << ": $" << stats.cost << "\n";
  std::cout.flush();
}

void Ui::show_error(std::string_view msg) const {
  std::cerr << "[error] " << msg << "\n";
  std::cerr.flush();
}

Approval Ui::request_approval(const ToolCall &call) {
  std::cout << "Approve " << call.name;
  size_t arg_count = 0;
  for (const auto &[key, val] : call.args) {
    if (arg_count++ >= 3) {
      std::cout << " ...";
      break;
    }
    std::string val_str = val.is_string() ? val.as_string() : to_json(val);
    std::cout << " " << key << "="
              << (key == "command" ? val_str : truncate(val_str, 60));
  }
  std::cout << "? [y/n]: ";
  std::cout.flush();

  while (true) {
    if (should_interrupt.load()) {
      std::cout << "\n";
      return Approval::Reject;
    }
    std::string line;
    if (!std::getline(std::cin, line)) {
      std::cin.clear();
      std::cout << "\n";
      return Approval::Reject; // EOF or EINTR
    }
    trim_trailing(line);

    if (line.empty()) {
      if (should_interrupt.load())
        return Approval::Reject;
      std::cout << "[y/n]: ";
      std::cout.flush();
      continue;
    }
    if (line[0] == 'y' || line[0] == 'Y')
      return Approval::Accept;
    if (line[0] == 'n' || line[0] == 'N')
      return Approval::Reject;

    std::cout << "[y/n]: ";
    std::cout.flush();
  }
}

void Ui::show_completion(const std::string &summary) const {
  show_message("completed", summary);
}

std::string Ui::wait_for_input(size_t ctx_used, size_t ctx_limit) {
  size_t pct = ctx_limit ? (ctx_used * 100 / ctx_limit) : 0;
  if (pct > 100)
    pct = 100;
  std::cout << "[ " << pct << "% ] > ";
  std::cout.flush();

  std::string line;
  if (!std::getline(std::cin, line)) {
    std::cin.clear(); // reset failbit set by EINTR or EOF
    return "";
  }

  trim_trailing(line);
  return line;
}

std::string Ui::open_editor(const std::string &configured_editor) {
  char tmp_path[] = "/tmp/ccl_XXXXXX";
  int fd = mkstemp(tmp_path);
  if (fd == -1)
    return "";
  if (close(fd) == -1)
    return "";

  const char *editor =
      configured_editor.empty() ? nullptr : configured_editor.c_str();
  if (!editor || !*editor)
    editor = std::getenv("VISUAL");
  if (!editor || !*editor)
    editor = std::getenv("EDITOR");
  if (!editor || !*editor)
    editor = "nano";

  std::string cmd = std::string(editor) + " " + tmp_path;
  if (system(cmd.c_str()) != 0) {
    std::remove(tmp_path);
    return "";
  }

  std::ifstream f(tmp_path);
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  std::remove(tmp_path);

  trim_trailing(content);
  return content;
}
