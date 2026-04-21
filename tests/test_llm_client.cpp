#include "harness.h"
#include "../src/llm_client.h"
#include "../src/context.h"
#include "../src/json.h"

TEST(llm_request_build_basic) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::string request = LlmClient::build_request_json(ctx, {}, cfg);
    JsonValue parsed = parse_json(request);

    CHECK(parsed.get("model"));
    CHECK(parsed.get("messages"));
    CHECK(parsed.get("max_tokens"));
    CHECK(parsed.get("temperature"));
}

TEST(llm_request_build_with_tools) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("read a file");

    ToolDef tool;
    tool.name = "read_file";
    tool.description = "Read file contents";
    tool.params.push_back({"path", "string", "File path", true});

    std::vector<ToolDef> tools = {tool};
    std::string request = LlmClient::build_request_json(ctx, tools, cfg);
    JsonValue parsed = parse_json(request);

    CHECK(parsed.get("tools").has_value());
    auto tools_val = parsed.get("tools");
    CHECK(tools_val->is_array());
    CHECK(tools_val->as_array().size() > 0);
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

TEST(llm_parse_response_tool_calls) {
    std::string response = R"({
        "choices": [{
            "message": {
                "content": "I'll read that file for you",
                "tool_calls": [{
                    "id": "call_123",
                    "function": {
                        "name": "read_file",
                        "arguments": "{\"path\":\"/etc/passwd\"}"
                    }
                }]
            },
            "finish_reason": "tool_calls"
        }],
        "usage": {
            "prompt_tokens": 20,
            "completion_tokens": 25,
            "total_tokens": 45
        }
    })";

    LlmResponse llm_resp = LlmClient::parse_response_json(response);
    CHECK_EQ(llm_resp.tool_calls.size(), size_t(1));
    CHECK_EQ(llm_resp.tool_calls[0].id, std::string("call_123"));
    CHECK_EQ(llm_resp.tool_calls[0].name, std::string("read_file"));
    CHECK(llm_resp.tool_calls[0].args.find("path") != llm_resp.tool_calls[0].args.end());
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

TEST(llm_extract_tool_calls_multi) {
    std::string response = R"({
        "choices": [{
            "message": {
                "content": "Running two operations",
                "tool_calls": [
                    {
                        "id": "call_1",
                        "function": {
                            "name": "read_file",
                            "arguments": "{\"path\":\"/file1\"}"
                        }
                    },
                    {
                        "id": "call_2",
                        "function": {
                            "name": "list_dir",
                            "arguments": "{\"path\":\"/dir\"}"
                        }
                    }
                ]
            },
            "finish_reason": "tool_calls"
        }],
        "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}
    })";

    LlmResponse llm_resp = LlmClient::parse_response_json(response);
    CHECK_EQ(llm_resp.tool_calls.size(), size_t(2));
    CHECK_EQ(llm_resp.tool_calls[0].name, std::string("read_file"));
    CHECK_EQ(llm_resp.tool_calls[1].name, std::string("list_dir"));
}

TEST(llm_parse_malformed_response) {
    std::string response = R"({
        "choices": [{
            "message": {"content": "fallback"}
        }]
    })";

    // Should parse gracefully without crash
    LlmResponse llm_resp = LlmClient::parse_response_json(response);
    CHECK_EQ(llm_resp.content, std::string("fallback"));
    // Missing usage fields default to 0
    CHECK_EQ(llm_resp.usage.total_tokens, size_t(0));
}

TEST(llm_parse_malformed_json_sets_is_error) {
    LlmResponse r = LlmClient::parse_response_json("not valid json {{{{");
    CHECK(r.is_error);
    CHECK(!r.content.empty());  // error message present
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

    std::string request = LlmClient::build_request_json(ctx, {}, cfg);
    // The literal quote chars must be escaped as \"
    CHECK(request.find("my-\\\"model\\\"") != std::string::npos);
    // The raw unescaped form must NOT appear as a bare string boundary
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

TEST(llm_parse_hermes_single_tool_call) {
    std::string body = R"({
        "choices":[{"finish_reason":"stop","message":{
            "content":"","role":"assistant",
            "reasoning_content":"Thinking...\n<tool_call>\n<function=read_file>\n<parameter=path>\n/tmp/x.txt\n</parameter>\n</function>\n</tool_call>"
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

TEST(llm_parse_hermes_multiple_tool_calls) {
    std::string body = R"({
        "choices":[{"finish_reason":"stop","message":{
            "content":"","role":"assistant",
            "reasoning_content":"<tool_call>\n<function=read_file>\n<parameter=path>\n/tmp/a.txt\n</parameter>\n</function>\n</tool_call>\n<tool_call>\n<function=list_dir>\n<parameter=path>\n/tmp\n</parameter>\n</function>\n</tool_call>"
        }}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = LlmClient::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(2));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK_EQ(r.tool_calls[1].name, std::string("list_dir"));
    CHECK(r.tool_calls[0].id != r.tool_calls[1].id);
}

TEST(llm_parse_hermes_no_reasoning_content) {
    std::string body = R"({
        "choices":[{"finish_reason":"stop","message":{
            "content":"just text","role":"assistant"
        }}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = LlmClient::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(0));
    CHECK_EQ(r.content, std::string("just text"));
}

