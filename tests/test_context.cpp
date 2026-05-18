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

#include "../src/context.h"
#include "../src/json.h"
#include "harness.h"

TEST(context_push_system_serializes) {
  ContextManager ctx(8000);
  ctx.push_system("You are helpful");
  CHECK_EQ(ctx.message_count(), size_t(1));
  CHECK(ctx.total_tokens() > 0);
}

TEST(context_push_user_serializes) {
  ContextManager ctx(8000);
  ctx.push_user("Hello, world!");
  CHECK_EQ(ctx.message_count(), size_t(1));
  CHECK(ctx.total_tokens() > 0);
}

TEST(context_push_assistant_serializes) {
  ContextManager ctx(8000);
  ctx.push_assistant("I can help with that.");
  CHECK_EQ(ctx.message_count(), size_t(1));
  CHECK(ctx.total_tokens() > 0);
}

TEST(context_to_json_message_order) {
  ContextManager ctx(8000);
  ctx.push_system("system");
  ctx.push_user("user1");
  ctx.push_assistant("assist1");

  std::string json = ctx.to_json();
  JsonValue parsed = parse_json(json);
  CHECK(parsed.is_array());

  const auto &arr = parsed.as_array();
  CHECK_EQ(arr.size(), size_t(3));

  auto role0 = arr[0]->get("role");
  CHECK(role0.has_value());
  CHECK_EQ(role0->as_string(), std::string("system"));

  auto role1 = arr[1]->get("role");
  CHECK(role1.has_value());
  CHECK_EQ(role1->as_string(), std::string("user"));

  auto role2 = arr[2]->get("role");
  CHECK(role2.has_value());
  CHECK_EQ(role2->as_string(), std::string("assistant"));
}

TEST(context_token_estimation) {
  ContextManager ctx(8000);
  ctx.push_user("hello world");
  size_t tokens = ctx.total_tokens();
  CHECK(tokens > 0);
  CHECK(tokens <= 3);
}

TEST(context_sync_token_count) {
  ContextManager ctx(8000);
  ctx.push_user("some text");
  ctx.total_tokens();

  LlmResponse::Usage usage;
  usage.total_tokens = 123;
  ctx.sync_token_count(usage);

  CHECK_EQ(ctx.total_tokens(), size_t(123));
}

TEST(context_needs_compaction_false) {
  ContextManager ctx(8000);
  ctx.push_user("hello");
  CHECK(ctx.total_tokens() < 8000);
  CHECK(!ctx.needs_compaction());
}

TEST(context_needs_compaction_true) {
  ContextManager ctx(20);
  std::string long_msg(200, 'x');
  ctx.push_user(long_msg);
  CHECK(ctx.needs_compaction());
}

TEST(context_compact_preserves_system) {
  ContextManager ctx(40);
  ctx.push_system("You are helpful assistant");
  ctx.push_user("user message 1 2 3 4 5 6 7 8 9 10 11 12 13");
  ctx.push_user("user message 2 2 3 4 5 6 7 8 9 10 11 12 13");

  ctx.compact();
  std::string json = ctx.to_json();
  JsonValue parsed = parse_json(json);
  auto first_msg = parsed.as_array()[0]->get("role");
  CHECK_EQ(first_msg->as_string(), std::string("system"));
}

TEST(context_compact_drops_oldest_non_system) {
  ContextManager ctx(50);
  ctx.push_system("system");
  std::string long_msg(100, 'x');
  ctx.push_user(long_msg);
  ctx.push_user(long_msg);
  ctx.push_user(long_msg);

  size_t before = ctx.message_count();
  ctx.compact();
  size_t after = ctx.message_count();

  CHECK(after < before);

  std::string json = ctx.to_json();
  JsonValue parsed = parse_json(json);
  auto first_msg = parsed.as_array()[0]->get("role");
  CHECK_EQ(first_msg->as_string(), std::string("system"));
}

TEST(context_message_count) {
  ContextManager ctx(8000);
  CHECK_EQ(ctx.message_count(), size_t(0));
  ctx.push_system("sys");
  CHECK_EQ(ctx.message_count(), size_t(1));
  ctx.push_user("user");
  CHECK_EQ(ctx.message_count(), size_t(2));
}

// ============================================================================
// replace_system tests
// ============================================================================

TEST(context_replace_system_updates_content) {
  ContextManager ctx(8000);
  ctx.push_system("original system");
  ctx.replace_system("new system");

  std::string json = ctx.to_json();
  JsonValue parsed = parse_json(json);
  auto content = parsed.as_array()[0]->get("content");
  CHECK_EQ(content->as_string(), std::string("new system"));
}

TEST(context_replace_system_updates_token_count) {
  ContextManager ctx(8000);
  ctx.push_system("short");
  size_t before = ctx.total_tokens();

  std::string long_prompt(400, 'x');
  ctx.replace_system(long_prompt);

  CHECK(ctx.total_tokens() > before);
}

TEST(context_replace_system_preserves_conversation) {
  ContextManager ctx(8000);
  ctx.push_system("original");
  ctx.push_user("user message");
  ctx.push_assistant("assistant reply");

  ctx.replace_system("updated system");

  CHECK_EQ(ctx.message_count(), size_t(3));

  std::string json = ctx.to_json();
  JsonValue parsed = parse_json(json);
  auto role0 = parsed.as_array()[0]->get("role");
  auto content0 = parsed.as_array()[0]->get("content");
  CHECK_EQ(role0->as_string(), std::string("system"));
  CHECK_EQ(content0->as_string(), std::string("updated system"));
}

TEST(context_replace_system_when_none_exists_inserts_at_front) {
  ContextManager ctx(8000);
  ctx.push_user("user first");
  ctx.replace_system("injected system");

  CHECK_EQ(ctx.message_count(), size_t(2));
  std::string json = ctx.to_json();
  JsonValue parsed = parse_json(json);
  auto role0 = parsed.as_array()[0]->get("role");
  CHECK_EQ(role0->as_string(), std::string("system"));
}

TEST(context_sync_token_count_ignores_zero) {
  ContextManager ctx(8000);
  ctx.push_user("some text");
  size_t before = ctx.total_tokens();
  CHECK(before > 0);

  LlmResponse::Usage usage;
  usage.total_tokens = 0;
  ctx.sync_token_count(usage);

  CHECK_EQ(ctx.total_tokens(), before);
}

TEST(context_compact_leaves_below_limit) {
  ContextManager ctx(50);
  ctx.push_system("system");
  std::string long_msg(120, 'x');
  ctx.push_user(long_msg);
  ctx.push_user(long_msg);
  ctx.push_user(long_msg);

  CHECK(ctx.needs_compaction());
  ctx.compact();

  CHECK(!ctx.needs_compaction());
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(context_push_empty_message_zero_tokens) {
  ContextManager ctx(8000);
  ctx.push_user("");
  CHECK_EQ(ctx.message_count(), size_t(1));
  // estimate_tokens("") = (0+3)/4 = 0 in integer division
  CHECK_EQ(ctx.total_tokens(), size_t(0));
}

TEST(context_to_json_escapes_quotes_in_content) {
  ContextManager ctx(8000);
  ctx.push_user("say \"hello\" and \\world");
  // parse_json will throw if escaping is wrong — harness catches it as FAIL
  JsonValue parsed = parse_json(ctx.to_json());
  auto content = parsed.get(size_t(0))->get("content");
  CHECK(content.has_value());
  CHECK_EQ(content->as_string(), std::string("say \"hello\" and \\world"));
}

TEST(context_to_json_escapes_newlines_in_content) {
  ContextManager ctx(8000);
  ctx.push_assistant("line1\nline2\ttabbed");
  JsonValue parsed = parse_json(ctx.to_json());
  auto content = parsed.get(size_t(0))->get("content");
  CHECK(content.has_value());
  CHECK_EQ(content->as_string(), std::string("line1\nline2\ttabbed"));
}

TEST(context_compact_all_assistant_no_user) {
  // system + 3 assistants with no user messages in between
  // compact() should drop all 3 assistants as one group
  ContextManager ctx(5);
  ctx.push_system("s");     // ~1 token
  std::string msg(20, 'a'); // (20+3)/4 = 5 tokens each
  ctx.push_assistant(msg);  // total = 6 > 5 → needs compact
  ctx.push_assistant(msg);
  ctx.push_assistant(msg);

  CHECK(ctx.needs_compaction());
  ctx.compact();

  // All 3 assistants dropped in one pass — only system remains
  CHECK_EQ(ctx.message_count(), size_t(1));
  CHECK(!ctx.needs_compaction());
}

TEST(context_compact_empty_context_no_crash) {
  ContextManager ctx(8000);
  // compact() on empty context should be a no-op, not crash
  ctx.compact();
  CHECK_EQ(ctx.message_count(), size_t(0));
}

// ============================================================================
// compact_to_summary with configurable keep_recent
// ============================================================================

TEST(context_compact_to_summary_keeps_configured_recent) {
  ContextManager ctx(8000, 3);
  ctx.push_system("system prompt");
  for (int i = 0; i < 10; ++i) {
    ctx.push_user("user message");
    ctx.push_assistant("assistant reply");
  }
  // 1 system + 20 conversation messages = 21 total
  ctx.compact_to_summary("condensed history");
  // expect: 1 system + 1 summary + 3 recent = 5
  CHECK_EQ(ctx.message_count(), size_t(5));
}

TEST(context_compact_to_summary_default_keep_8) {
  ContextManager ctx(8000); // default keep_recent=8
  ctx.push_system("system prompt");
  for (int i = 0; i < 10; ++i) {
    ctx.push_user("user message");
    ctx.push_assistant("assistant reply");
  }
  // 1 system + 20 conversation messages = 21 total
  ctx.compact_to_summary("condensed history");
  // expect: 1 system + 1 summary + 8 recent = 10
  CHECK_EQ(ctx.message_count(), size_t(10));
}

TEST(context_compact_to_summary_keep_1) {
  ContextManager ctx(8000, 1);
  ctx.push_system("system prompt");
  for (int i = 0; i < 5; ++i) {
    ctx.push_user("user message");
    ctx.push_assistant("assistant reply");
  }
  // 1 system + 10 messages = 11 total
  ctx.compact_to_summary("condensed history");
  // expect: 1 system + 1 summary + 1 recent = 3
  CHECK_EQ(ctx.message_count(), size_t(3));
}

TEST(context_compact_to_summary_keep_exceeds_history) {
  ContextManager ctx(8000, 20);
  ctx.push_system("system prompt");
  ctx.push_user("msg1");
  ctx.push_assistant("msg2");
  ctx.push_user("msg3");
  ctx.push_assistant("msg4");
  // 1 system + 4 messages = 5 total; keep_recent=20 > 4
  ctx.compact_to_summary("condensed history");
  // keep_from falls back to first_ns: all 4 non-system messages kept
  // expect: 1 system + 1 summary + 4 = 6
  CHECK_EQ(ctx.message_count(), size_t(6));
}

TEST(context_compact_to_summary_keep_recent_zero) {
  // keep_recent=0: all non-system messages dropped; only system + summary
  // remain.
  ContextManager ctx(8000, 0);
  ctx.push_system("system prompt");
  ctx.push_user("msg1");
  ctx.push_assistant("msg2");
  ctx.compact_to_summary("condensed");
  // expect: 1 system + 1 summary = 2
  CHECK_EQ(ctx.message_count(), size_t(2));
}

TEST(context_compact_to_summary_summary_content_preserved) {
  ContextManager ctx(8000, 2);
  ctx.push_system("system");
  ctx.push_user("hello");
  ctx.push_assistant("world");
  ctx.compact_to_summary("my important summary");

  std::string json = ctx.to_json();
  JsonValue parsed = parse_json(json);
  // Second message (index 1) should be the summary
  auto content = parsed.as_array()[1]->get("content");
  CHECK(content.has_value());
  CHECK(content->as_string().find("my important summary") != std::string::npos);
}

// ============================================================================
// from_messages factory
// ============================================================================

TEST(context_from_messages_roundtrip) {
  std::vector<Message> msgs = {
      {Message::Role::System, "system prompt", 10},
      {Message::Role::User, "hello", 5},
      {Message::Role::Assistant, "world", 5},
  };
  ContextManager ctx = ContextManager::from_messages(msgs, 20, 8000, 8);
  CHECK_EQ(ctx.message_count(), size_t(3));
  CHECK_EQ(ctx.messages()[0].role, Message::Role::System);
  CHECK_EQ(ctx.messages()[1].content, std::string("hello"));
  CHECK_EQ(ctx.messages()[2].role, Message::Role::Assistant);
}

TEST(context_from_messages_preserves_token_count) {
  std::vector<Message> msgs = {
      {Message::Role::User, "x", 999},
  };
  ContextManager ctx = ContextManager::from_messages(msgs, 999, 8000, 8);
  CHECK_EQ(ctx.total_tokens(), size_t(999));
}

// ============================================================================
// extract_conversation_for_summary
// ============================================================================

TEST(context_extract_conversation_format) {
  ContextManager ctx(8000);
  ctx.push_system("system prompt");
  ctx.push_user("user message");
  ctx.push_assistant("assistant reply");

  std::string summary = ctx.extract_conversation_for_summary();
  CHECK(summary.find("User: user message") != std::string::npos);
  CHECK(summary.find("Assistant: assistant reply") != std::string::npos);
  CHECK(summary.find("system prompt") == std::string::npos); // system excluded
}

// ============================================================================
// needs_compaction boundary
// ============================================================================

TEST(context_needs_compaction_at_exact_limit) {
  ContextManager ctx(100);
  // Drive total_tokens to exactly 100 via sync_token_count
  LlmResponse::Usage usage;
  usage.total_tokens = 100;
  ctx.sync_token_count(usage);
  CHECK(ctx.needs_compaction()); // >= limit → true
}

TEST(context_needs_compaction_below_limit) {
  ContextManager ctx(100);
  LlmResponse::Usage usage;
  usage.total_tokens = 99;
  ctx.sync_token_count(usage);
  CHECK(!ctx.needs_compaction());
}

// ============================================================================
// to_json special characters
// ============================================================================

TEST(context_to_json_escapes_special_chars) {
  ContextManager ctx(8000);
  ctx.push_user("say \"hello\"\nand bye");
  std::string json = ctx.to_json();
  // Must be valid JSON — parse_json would throw on invalid
  JsonValue parsed = parse_json(json);
  CHECK(parsed.is_array());
  auto content = parsed.as_array()[0]->get("content");
  CHECK(content.has_value());
  CHECK_EQ(content->as_string(), std::string("say \"hello\"\nand bye"));
}
