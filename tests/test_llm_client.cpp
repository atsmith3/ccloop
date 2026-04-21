#include "harness.h"
#include "../src/llm_client.h"
#include "../src/context.h"
#include "../src/json.h"

TEST(llm_request_build_basic) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::string request = LlmClient::build_request_json(ctx, cfg);
    JsonValue parsed = parse_json(request);

    CHECK(parsed.get("model"));
    CHECK(parsed.get("messages"));
    CHECK(parsed.get("max_tokens"));
    CHECK(parsed.get("temperature"));
}

TEST(llm_request_no_tools_array) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::string request = LlmClient::build_request_json(ctx, cfg);
    CHECK(request.find("\"tools\"") == std::string::npos);
}

TEST(llm_parse_response_text_only) {
    std::string response = R"({
        "choices": [{
            "message": {
                "content": "Hello, this is a response"
            },
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 10,
            "completion_tokens": 15,
            "total_tokens": 25
        }
    })";

    LlmResponse llm_resp = LlmClient::parse_response_json(response);
    CHECK_EQ(llm_resp.content, std::string("Hello, this is a response"));
    CHECK_EQ(llm_resp.usage.prompt_tokens, size_t(10));
    CHECK_EQ(llm_resp.usage.completion_tokens, size_t(15));
    CHECK_EQ(llm_resp.usage.total_tokens, size_t(25));
}

TEST(llm_parse_response_usage) {
    std::string response = R"({
        "choices": [{
            "message": {"content": "test"},
            "finish_reason": "stop"
        }],
        "usage": {
            "prompt_tokens": 100,
            "completion_tokens": 50,
            "total_tokens": 150
        }
    })";

    LlmResponse llm_resp = LlmClient::parse_response_json(response);
    CHECK_EQ(llm_resp.usage.prompt_tokens, size_t(100));
    CHECK_EQ(llm_resp.usage.completion_tokens, size_t(50));
    CHECK_EQ(llm_resp.usage.total_tokens, size_t(150));
}

TEST(llm_parse_malformed_response) {
    std::string response = R"({
        "choices": [{
            "message": {"content": "fallback"}
        }]
    })";

    LlmResponse llm_resp = LlmClient::parse_response_json(response);
    CHECK_EQ(llm_resp.content, std::string("fallback"));
    CHECK_EQ(llm_resp.usage.total_tokens, size_t(0));
}

TEST(llm_parse_malformed_json_sets_is_error) {
    LlmResponse r = LlmClient::parse_response_json("not valid json {{{{");
    CHECK(r.is_error);
    CHECK(!r.content.empty());
}

TEST(llm_parse_response_is_error_false_for_success) {
    std::string response = R"({
        "choices": [{"message": {"content": "ok"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}
    })";
    LlmResponse r = LlmClient::parse_response_json(response);
    CHECK(!r.is_error);
}

TEST(llm_build_request_json_escapes_model_name) {
    Config cfg = Config::defaults();
    cfg.model = "my-\"model\"";
    ContextManager ctx(8000);
    ctx.push_user("test");

    std::string request = LlmClient::build_request_json(ctx, cfg);
    CHECK(request.find("my-\\\"model\\\"") != std::string::npos);
    CHECK(request.find("\"my-\"model\"\"") == std::string::npos);
}

TEST(llm_retryable_error_429) {
    CHECK(LlmClient::is_retryable_status(429));
}

TEST(llm_retryable_error_503) {
    CHECK(LlmClient::is_retryable_status(503));
}

TEST(llm_non_retryable_error_400) {
    CHECK(!LlmClient::is_retryable_status(400));
}

TEST(llm_non_retryable_error_401) {
    CHECK(!LlmClient::is_retryable_status(401));
}

TEST(llm_parse_xml_single_tool_call) {
    std::string body = R"({
        "choices":[{"finish_reason":"stop","message":{
            "content":"<read_file>\n<path>/tmp/x.txt</path>\n</read_file>",
            "role":"assistant"
        }}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = LlmClient::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK(r.tool_calls[0].args.count("path") > 0);
    CHECK_EQ(r.tool_calls[0].args.at("path").as_string(), std::string("/tmp/x.txt"));
    CHECK(!r.tool_calls[0].id.empty());
}

TEST(llm_parse_xml_multiple_tool_calls) {
    std::string body = R"({
        "choices":[{"finish_reason":"stop","message":{
            "content":"<read_file>\n<path>/tmp/a.txt</path>\n</read_file>\n<list_dir>\n<path>/tmp</path>\n</list_dir>",
            "role":"assistant"
        }}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = LlmClient::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(2));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK_EQ(r.tool_calls[1].name, std::string("list_dir"));
    CHECK(r.tool_calls[0].id != r.tool_calls[1].id);
}

TEST(llm_parse_xml_no_tool_calls) {
    std::string body = R"({
        "choices":[{"finish_reason":"stop","message":{
            "content":"just plain text, no tool calls",
            "role":"assistant"
        }}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = LlmClient::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(0));
    CHECK_EQ(r.content, std::string("just plain text, no tool calls"));
}

TEST(llm_parse_xml_multiple_params) {
    std::string body = R"({
        "choices":[{"finish_reason":"stop","message":{
            "content":"<search_files>\n<path>/src</path>\n<pattern>main</pattern>\n<file_glob>*.cpp</file_glob>\n</search_files>",
            "role":"assistant"
        }}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = LlmClient::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].name, std::string("search_files"));
    CHECK_EQ(r.tool_calls[0].args.at("path").as_string(), std::string("/src"));
    CHECK_EQ(r.tool_calls[0].args.at("pattern").as_string(), std::string("main"));
    CHECK_EQ(r.tool_calls[0].args.at("file_glob").as_string(), std::string("*.cpp"));
}
