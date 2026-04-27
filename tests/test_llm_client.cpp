#include "harness.h"
#include "../src/connector_openai.h"
#include "../src/connector_bedrock.h"
#include "../src/connector_base.h"
#include "../src/context.h"
#include "../src/json.h"

// ============================================================================
// OpenAiConnector — request building
// ============================================================================

TEST(llm_request_build_basic) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::string request = OpenAiConnector::build_request_json(ctx, cfg, {});
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

    std::string request = OpenAiConnector::build_request_json(ctx, cfg, {});
    CHECK(request.find("\"tools\"") == std::string::npos);
}

TEST(llm_build_request_json_escapes_model_name) {
    Config cfg = Config::defaults();
    cfg.model = "my-\"model\"";
    ContextManager ctx(8000);
    ctx.push_user("test");

    std::string request = OpenAiConnector::build_request_json(ctx, cfg, {});
    CHECK(request.find("my-\\\"model\\\"") != std::string::npos);
    CHECK(request.find("\"my-\"model\"\"") == std::string::npos);
}

TEST(openai_request_has_tools_array) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::vector<ToolDef> tools = {{
        "read_file", "Read a file",
        {{"path", "string", "File path", true}}, "read"
    }};

    std::string request = OpenAiConnector::build_request_json(ctx, cfg, tools);
    CHECK(request.find("\"tools\"") != std::string::npos);
    CHECK(request.find("\"function\"") != std::string::npos);
    CHECK(request.find("\"read_file\"") != std::string::npos);
}

TEST(openai_request_no_tools_when_empty) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::string request = OpenAiConnector::build_request_json(ctx, cfg, {});
    CHECK(request.find("\"tools\"") == std::string::npos);
}

TEST(openai_parse_tool_calls) {
    std::string body = R"({
        "choices": [{
            "message": {
                "content": null,
                "tool_calls": [{
                    "id": "call_abc123",
                    "type": "function",
                    "function": {
                        "name": "read_file",
                        "arguments": "{\"path\":\"/tmp/test.txt\"}"
                    }
                }]
            },
            "finish_reason": "tool_calls"
        }],
        "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}
    })";

    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK_EQ(r.tool_calls[0].id, std::string("call_abc123"));
    CHECK(r.tool_calls[0].args.count("path") > 0);
    CHECK_EQ(r.tool_calls[0].args.at("path").as_string(), std::string("/tmp/test.txt"));
}

TEST(openai_parse_text_only_response) {
    std::string body = R"({
        "choices": [{
            "message": {"content": "Hello world"},
            "finish_reason": "stop"
        }],
        "usage": {"prompt_tokens": 5, "completion_tokens": 3, "total_tokens": 8}
    })";

    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK_EQ(r.content, std::string("Hello world"));
    CHECK_EQ(r.tool_calls.size(), size_t(0));
    CHECK(!r.is_error);
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

    LlmResponse llm_resp = OpenAiConnector::parse_response_json(response);
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

    LlmResponse llm_resp = OpenAiConnector::parse_response_json(response);
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

    LlmResponse llm_resp = OpenAiConnector::parse_response_json(response);
    CHECK_EQ(llm_resp.content, std::string("fallback"));
    CHECK_EQ(llm_resp.usage.total_tokens, size_t(0));
}

TEST(llm_parse_malformed_json_sets_is_error) {
    LlmResponse r = OpenAiConnector::parse_response_json("not valid json {{{{");
    CHECK(r.is_error);
    CHECK(!r.content.empty());
}

TEST(llm_parse_response_is_error_false_for_success) {
    std::string response = R"({
        "choices": [{"message": {"content": "ok"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(response);
    CHECK(!r.is_error);
}

TEST(llm_retryable_error_429) {
    CHECK(ConnectorBase::is_retryable_status(429));
}

TEST(llm_retryable_error_503) {
    CHECK(ConnectorBase::is_retryable_status(503));
}

TEST(llm_non_retryable_error_400) {
    CHECK(!ConnectorBase::is_retryable_status(400));
}

TEST(llm_non_retryable_error_401) {
    CHECK(!ConnectorBase::is_retryable_status(401));
}

TEST(openai_parse_malformed_json_sets_is_error) {
    LlmResponse r = OpenAiConnector::parse_response_json("not json");
    CHECK(r.is_error);
}

// ============================================================================
// BedrockConnector tests
// ============================================================================

TEST(bedrock_request_has_system_field) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_system("You are a helpful assistant.");
    ctx.push_user("hello");

    std::string request = BedrockConnector::build_request_json(ctx, cfg, {});
    CHECK(request.find("\"system\"") != std::string::npos);
    CHECK(request.find("You are a helpful assistant.") != std::string::npos);
    CHECK(request.find("\"messages\"") != std::string::npos);
}

TEST(bedrock_request_has_tool_config) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::vector<ToolDef> tools = {{
        "read_file", "Read a file",
        {{"path", "string", "File path", true}}, "read"
    }};

    std::string request = BedrockConnector::build_request_json(ctx, cfg, tools);
    CHECK(request.find("\"toolConfig\"") != std::string::npos);
    CHECK(request.find("\"toolSpec\"") != std::string::npos);
    CHECK(request.find("\"read_file\"") != std::string::npos);
}

TEST(bedrock_parse_text_response) {
    std::string body = R"({
        "output": {
            "message": {
                "role": "assistant",
                "content": [{"text": "Hello from Bedrock"}]
            }
        },
        "usage": {"inputTokens": 10, "outputTokens": 5, "totalTokens": 15},
        "stopReason": "end_turn"
    })";

    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK_EQ(r.content, std::string("Hello from Bedrock"));
    CHECK_EQ(r.tool_calls.size(), size_t(0));
    CHECK_EQ(r.usage.prompt_tokens, size_t(10));
    CHECK_EQ(r.usage.completion_tokens, size_t(5));
    CHECK(!r.is_error);
}

TEST(bedrock_parse_tool_use) {
    std::string body = R"({
        "output": {
            "message": {
                "role": "assistant",
                "content": [{
                    "toolUse": {
                        "toolUseId": "bedrock_tool_001",
                        "name": "read_file",
                        "input": {"path": "/tmp/test.txt"}
                    }
                }]
            }
        },
        "usage": {"inputTokens": 20, "outputTokens": 10, "totalTokens": 30},
        "stopReason": "tool_use"
    })";

    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK_EQ(r.tool_calls[0].id, std::string("bedrock_tool_001"));
    CHECK(r.tool_calls[0].args.count("path") > 0);
    CHECK_EQ(r.tool_calls[0].args.at("path").as_string(), std::string("/tmp/test.txt"));
}

TEST(bedrock_parse_malformed_json_sets_is_error) {
    LlmResponse r = BedrockConnector::parse_response_json("not json");
    CHECK(r.is_error);
}

// ============================================================================
// OpenAiConnector — request building edge cases
// ============================================================================

TEST(openai_request_optional_params_not_in_required) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("go");

    std::vector<ToolDef> tools = {{
        "write_file", "Write a file",
        {
            {"path",    "string", "File path",    true},
            {"content", "string", "File content", false}  // optional
        }, "write"
    }};

    std::string request = OpenAiConnector::build_request_json(ctx, cfg, tools);
    JsonValue parsed = parse_json(request);

    auto tools_arr = parsed.get("tools");
    CHECK(tools_arr.has_value());
    auto fn = tools_arr->as_array()[0]->get("function");
    CHECK(fn.has_value());
    auto params = fn->get("parameters");
    CHECK(params.has_value());

    auto req = params->get("required");
    CHECK(req.has_value());
    CHECK_EQ(req->as_array().size(), size_t(1));
    CHECK_EQ(req->as_array()[0]->as_string(), std::string("path"));
}

TEST(openai_request_tool_no_params) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("go");

    std::vector<ToolDef> tools = {{"list_files", "List all files", {}, ""}};
    std::string request = OpenAiConnector::build_request_json(ctx, cfg, tools);
    JsonValue parsed = parse_json(request);

    auto fn = parsed.get("tools")->as_array()[0]->get("function");
    auto params = fn->get("parameters");
    CHECK(params.has_value());
    auto props = params->get("properties");
    CHECK(props.has_value());
    CHECK(props->is_object());
    CHECK(props->as_object().empty());
    CHECK(!params->get("required").has_value());
}

TEST(openai_request_escapes_tool_description) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("go");

    std::vector<ToolDef> tools = {{
        "tool", "Say \"hello\" and \\goodbye\\",
        {{"arg", "string", "Arg with \"quotes\"", true}}, ""
    }};

    std::string request = OpenAiConnector::build_request_json(ctx, cfg, tools);
    JsonValue parsed = parse_json(request);
    auto fn = parsed.get("tools")->as_array()[0]->get("function");
    CHECK_EQ(fn->get("description")->as_string(),
             std::string("Say \"hello\" and \\goodbye\\"));
}

// ============================================================================
// OpenAiConnector — response parsing edge cases
// ============================================================================

TEST(openai_parse_empty_tool_calls_array) {
    std::string body = R"({
        "choices":[{"message":{"content":"done","tool_calls":[]},"finish_reason":"stop"}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK_EQ(r.tool_calls.size(), size_t(0));
    CHECK_EQ(r.content, std::string("done"));
}

TEST(openai_parse_null_content_no_error) {
    std::string body = R"({
        "choices":[{"message":{"content":null},"finish_reason":"stop"}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK(r.content.empty());
    CHECK_EQ(r.tool_calls.size(), size_t(0));
}

TEST(openai_parse_content_and_tool_calls_together) {
    std::string body = R"({
        "choices":[{"message":{
            "content":"I'll read that file.",
            "tool_calls":[{
                "id":"call_1","type":"function",
                "function":{"name":"read_file","arguments":"{\"path\":\"/tmp/x\"}"}
            }]
        },"finish_reason":"tool_calls"}],
        "usage":{"prompt_tokens":5,"completion_tokens":3,"total_tokens":8}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK_EQ(r.content, std::string("I'll read that file."));
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
}

TEST(openai_parse_multiple_tool_calls) {
    std::string body = R"({
        "choices":[{"message":{
            "content":null,
            "tool_calls":[
                {"id":"c1","type":"function","function":{"name":"read_file","arguments":"{\"path\":\"/a\"}"}},
                {"id":"c2","type":"function","function":{"name":"list_dir","arguments":"{\"path\":\"/b\"}"}}
            ]
        },"finish_reason":"tool_calls"}],
        "usage":{"prompt_tokens":5,"completion_tokens":5,"total_tokens":10}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(2));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK_EQ(r.tool_calls[0].id,   std::string("c1"));
    CHECK_EQ(r.tool_calls[1].name, std::string("list_dir"));
    CHECK_EQ(r.tool_calls[1].id,   std::string("c2"));
}

TEST(openai_parse_malformed_arguments_gives_empty_args) {
    std::string body = R"({
        "choices":[{"message":{
            "content":null,
            "tool_calls":[{"id":"c1","type":"function","function":{"name":"read_file","arguments":"not {json"}}]
        },"finish_reason":"tool_calls"}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK(r.tool_calls[0].args.empty());
}

TEST(openai_parse_empty_arguments_object) {
    std::string body = R"({
        "choices":[{"message":{
            "content":null,
            "tool_calls":[{"id":"c1","type":"function","function":{"name":"list_dir","arguments":"{}"}}]
        },"finish_reason":"tool_calls"}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK(r.tool_calls[0].args.empty());
}

TEST(openai_parse_missing_tool_id_uses_fallback) {
    std::string body = R"({
        "choices":[{"message":{
            "content":null,
            "tool_calls":[{"type":"function","function":{"name":"read_file","arguments":"{\"path\":\"/x\"}"}}]
        },"finish_reason":"tool_calls"}],
        "usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}
    })";
    LlmResponse r = OpenAiConnector::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].id, std::string("tc_0"));
}

// ============================================================================
// BedrockConnector — request building edge cases
// ============================================================================

TEST(bedrock_request_no_system_when_no_system_message) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_user("hello");

    std::string request = BedrockConnector::build_request_json(ctx, cfg, {});
    JsonValue parsed = parse_json(request);
    CHECK(!parsed.get("system").has_value());
    CHECK(parsed.get("messages").has_value());
}

TEST(bedrock_request_excludes_system_from_messages_array) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_system("You are helpful.");
    ctx.push_user("hello");

    std::string request = BedrockConnector::build_request_json(ctx, cfg, {});
    JsonValue parsed = parse_json(request);

    CHECK(parsed.get("system").has_value());

    auto msgs = parsed.get("messages");
    CHECK(msgs.has_value());
    CHECK_EQ(msgs->as_array().size(), size_t(1));
    CHECK_EQ(msgs->as_array()[0]->get("role")->as_string(), std::string("user"));
}

TEST(bedrock_request_assistant_messages_included) {
    Config cfg = Config::defaults();
    ContextManager ctx(8000);
    ctx.push_system("Be helpful.");
    ctx.push_user("question");
    ctx.push_assistant("answer");
    ctx.push_user("follow-up");

    std::string request = BedrockConnector::build_request_json(ctx, cfg, {});
    JsonValue parsed = parse_json(request);

    auto msgs = parsed.get("messages");
    CHECK(msgs.has_value());
    CHECK_EQ(msgs->as_array().size(), size_t(3));
    CHECK_EQ(msgs->as_array()[0]->get("role")->as_string(), std::string("user"));
    CHECK_EQ(msgs->as_array()[1]->get("role")->as_string(), std::string("assistant"));
    CHECK_EQ(msgs->as_array()[2]->get("role")->as_string(), std::string("user"));
}

// ============================================================================
// BedrockConnector — response parsing edge cases
// ============================================================================

TEST(bedrock_parse_multiple_text_blocks) {
    std::string body = R"({
        "output": {"message": {"content": [
            {"text": "First part."},
            {"text": "Second part."}
        ]}},
        "usage": {"inputTokens": 5, "outputTokens": 3, "totalTokens": 8}
    })";
    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK_EQ(r.content, std::string("First part.\nSecond part."));
}

TEST(bedrock_parse_mixed_text_and_tool_use) {
    std::string body = R"({
        "output": {"message": {"content": [
            {"text": "I will read the file."},
            {"toolUse": {"toolUseId": "tu_1", "name": "read_file", "input": {"path": "/tmp/x"}}}
        ]}},
        "usage": {"inputTokens": 10, "outputTokens": 5, "totalTokens": 15},
        "stopReason": "tool_use"
    })";
    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK_EQ(r.content, std::string("I will read the file."));
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
}

TEST(bedrock_parse_multiple_tool_use_blocks) {
    std::string body = R"({
        "output": {"message": {"content": [
            {"toolUse": {"toolUseId": "tu_1", "name": "read_file",  "input": {"path": "/a"}}},
            {"toolUse": {"toolUseId": "tu_2", "name": "list_dir",   "input": {"path": "/b"}}}
        ]}},
        "usage": {"inputTokens": 10, "outputTokens": 8, "totalTokens": 18}
    })";
    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(2));
    CHECK_EQ(r.tool_calls[0].name, std::string("read_file"));
    CHECK_EQ(r.tool_calls[1].name, std::string("list_dir"));
}

TEST(bedrock_parse_empty_content_array) {
    std::string body = R"({
        "output": {"message": {"content": []}},
        "usage": {"inputTokens": 1, "outputTokens": 0, "totalTokens": 1}
    })";
    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK(r.content.empty());
    CHECK_EQ(r.tool_calls.size(), size_t(0));
}

TEST(bedrock_parse_missing_tool_use_id_uses_fallback) {
    std::string body = R"({
        "output": {"message": {"content": [
            {"toolUse": {"name": "read_file", "input": {"path": "/x"}}}
        ]}},
        "usage": {"inputTokens": 5, "outputTokens": 3, "totalTokens": 8}
    })";
    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK_EQ(r.tool_calls.size(), size_t(1));
    CHECK_EQ(r.tool_calls[0].id, std::string("bedrock_0"));
}

TEST(bedrock_parse_usage_total_tokens_computed) {
    std::string body = R"({
        "output": {"message": {"content": [{"text": "hi"}]}},
        "usage": {"inputTokens": 10, "outputTokens": 5}
    })";
    LlmResponse r = BedrockConnector::parse_response_json(body);
    CHECK(!r.is_error);
    CHECK_EQ(r.usage.prompt_tokens,     size_t(10));
    CHECK_EQ(r.usage.completion_tokens, size_t(5));
    CHECK_EQ(r.usage.total_tokens,      size_t(15));
}

// ============================================================================
// ConnectorBase — is_retryable_status completeness
// ============================================================================

TEST(retryable_500) { CHECK(ConnectorBase::is_retryable_status(500)); }
TEST(retryable_502) { CHECK(ConnectorBase::is_retryable_status(502)); }
TEST(retryable_504) { CHECK(ConnectorBase::is_retryable_status(504)); }
TEST(non_retryable_200) { CHECK(!ConnectorBase::is_retryable_status(200)); }
TEST(non_retryable_404) { CHECK(!ConnectorBase::is_retryable_status(404)); }
TEST(non_retryable_403) { CHECK(!ConnectorBase::is_retryable_status(403)); }
