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

#pragma once

#include "types.h"
#include <string>
#include <string_view>

class Ui {
public:
  explicit Ui(bool silent = false) : silent_(silent) {}

  void show_message(std::string_view role, std::string_view content) const;
  void show_tool_call(const ToolCall &call, ToolSource source) const;
  void show_tool_result(const ToolCall &call, const ToolResult &result) const;
  void update_tokens(size_t used, size_t limit) const;
  void show_stats(const SessionStats &stats, const std::string &model) const;
  void show_context(size_t used, size_t limit, size_t messages) const;
  void show_error(std::string_view msg) const;

  Approval request_approval(const ToolCall &call);
  void show_completion(const std::string &summary) const;
  std::string wait_for_input(size_t ctx_used, size_t ctx_limit);
  std::string open_editor(const std::string &configured_editor = "");

private:
  bool silent_ = false;
};
