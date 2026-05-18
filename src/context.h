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
#include <vector>

class ContextManager {
public:
  explicit ContextManager(size_t token_limit, size_t keep_recent = 8);
  static ContextManager from_messages(std::vector<Message> msgs,
                                      size_t total_tokens, size_t token_limit,
                                      size_t keep_recent);

  void push_system(std::string content);
  void replace_system(
      std::string content); // Update system message, keep conversation
  void push_user(std::string content);
  void push_assistant(std::string content);

  void sync_token_count(const LlmResponse::Usage &usage);

  bool needs_compaction() const;
  void compact();
  std::string extract_conversation_for_summary() const;
  void compact_to_summary(const std::string &summary);

  std::string to_json() const;
  const std::vector<Message> &messages() const;

  size_t total_tokens() const;
  size_t message_count() const;

private:
  std::vector<Message> messages_;
  size_t total_tokens_ = 0;
  size_t token_limit_;
  size_t keep_recent_ = 8;

  size_t estimate_tokens(const std::string &text) const;
  size_t index_of_first_non_system() const;
  size_t find_safe_drop_end(size_t start) const;
};
