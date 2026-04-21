#include "harness.h"
#include "../src/context.h"
#include "../src/json.h"

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

    const auto& arr = parsed.as_array();
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
