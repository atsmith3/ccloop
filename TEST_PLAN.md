# ccl — Unit Testing Plan

## Context

Every ccl component has unit tests with zero external test framework dependencies (no Boost.Test, no Google Test, no Catch2). This preserves the supply-chain safety and minimal-dependency philosophy of the project. The solution is a custom minimal test harness (~80 lines, single header, zero dependencies) that lives in-repo.

---

## Test Harness Design

A single file `tests/harness.h`. No macros for auto-registration beyond a simple static initializer trick.

```cpp
// tests/harness.h — ~80 lines, pure C++23 stdlib, no dependencies

#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <stdexcept>

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& test_list() {
    static std::vector<TestCase> v;
    return v;
}

inline int run_tests() {
    int pass = 0, fail = 0;
    for (auto& t : test_list()) {
        try {
            t.fn();
            std::cout << "[pass] " << t.name << "\n";
            ++pass;
        } catch (std::exception& e) {
            std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
            ++fail;
        }
    }
    std::cout << "\n" << pass + fail << " tests: " << pass << " passed, "
              << fail << " failed\n";
    return fail > 0 ? 1 : 0;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        test_list().push_back({name, fn});
    }
};

// Register a test function
#define TEST(name) \
    static void _test_fn_##name(); \
    static TestRegistrar _reg_##name(#name, _test_fn_##name); \
    static void _test_fn_##name()

// Assertion macros -- throw on failure (caught by run_tests)
#define CHECK(expr) \
    do { if (!(expr)) { \
        std::ostringstream _s; \
        _s << "CHECK(" #expr ") at " __FILE__ ":" << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define CHECK_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (_a != _b) { \
        std::ostringstream _s; \
        _s << "CHECK_EQ(" #a ", " #b ") at " __FILE__ ":" << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define CHECK_THROWS(expr) \
    do { bool _threw = false; \
        try { (expr); } catch (...) { _threw = true; } \
        if (!_threw) { \
            std::ostringstream _s; \
            _s << "CHECK_THROWS(" #expr ") did not throw at " __FILE__ ":" << __LINE__; \
            throw std::runtime_error(_s.str()); \
        } \
    } while(0)
```

**tests/main.cpp:**
```cpp
#include "harness.h"
int main() { return run_tests(); }
```

Usage in test files:
```cpp
#include "harness.h"
#include "../src/json.h"

TEST(json_parse_null)       { CHECK(parse_json("null").is_null()); }
TEST(json_parse_bool_true)  { CHECK(parse_json("true").as_bool() == true); }
TEST(json_roundtrip_object) {
    std::string s = R"({"k":"v","n":42})";
    CHECK_EQ(to_json(parse_json(s)), s);
}
TEST(json_invalid_throws)   { CHECK_THROWS(parse_json("{bad")); }
```

---

## File Structure Changes

```
ccl/
├── CMakeLists.txt
├── src/                      -- (unchanged)
└── tests/
    ├── harness.h             -- test harness (~80 lines, zero deps)
    ├── main.cpp              -- int main() { return run_tests(); }
    ├── test_json.cpp         -- JSON parser + serializer tests
    ├── test_config.cpp       -- TOML parser + env var override tests
    ├── test_context.cpp      -- ContextManager tests
    ├── test_tools.cpp        -- local tool implementation tests (uses /tmp)
    └── test_llm_client.cpp   -- request builder + response parser tests (no HTTP)
```

Note: `agent.cpp` and `ui.cpp` are not directly unit-tested — they are integration layers. Their behavior is covered by running `ccl` manually. If integration tests are added later, a mock Ui implementation can be passed to Agent.

---

## CMake Changes

Add to the bottom of `CMakeLists.txt`:

```cmake
enable_testing()

add_executable(ccl_test
    tests/main.cpp
    tests/test_json.cpp
    tests/test_config.cpp
    tests/test_context.cpp
    tests/test_tools.cpp
    tests/test_llm_client.cpp
    src/json.cpp
    src/config.cpp
    src/context.cpp
    src/tools.cpp
    src/llm_client.cpp
    # agent.cpp and ui.cpp excluded -- integration layer, not unit-tested
)

target_include_directories(ccl_test PRIVATE src tests)
target_link_libraries(ccl_test PRIVATE CURL::libcurl)
target_compile_options(ccl_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

add_test(NAME ccl_tests COMMAND ccl_test)
```

Run all tests: `cmake --build build && ctest --test-dir build --output-on-failure`
Or directly: `./build/ccl_test`

---

## Test Coverage by Component

### test_json.cpp
Tests for `parse_json()`, `to_json()`, `escape_json()`, and `JsonValue` accessors.

```
json_parse_null
json_parse_bool_true / false
json_parse_integer / float / negative
json_parse_string / string_with_escapes / string_with_unicode_escape
json_parse_empty_array / nested_array
json_parse_empty_object / nested_object
json_roundtrip_simple_object
json_roundtrip_nested
json_access_get_key_present / missing
json_access_index_present / out_of_bounds
json_wrong_type_throws        -- as_bool() on number throws
json_invalid_input_throws     -- malformed JSON throws
json_escape_special_chars     -- escape_json() handles \n \t \" \\
```

### test_config.cpp
Tests for `Config::load()`, `Config::defaults()`, and env var override logic.

```
config_defaults_all_fields_present
config_load_minimal_toml          -- only endpoint set, rest default
config_load_all_fields            -- all keys present, correct types
config_load_missing_file_uses_defaults
config_string_value_parsed
config_int_value_parsed
config_bool_value_parsed
config_float_value_parsed
config_env_var_api_key_overrides  -- setenv("CCL_API_KEY", "test"), verify override
config_env_var_endpoint_overrides -- setenv("CCL_ENDPOINT", "http://x"), verify
config_search_project_local       -- ./ccl.toml takes priority over global
```

Note: tests that call `setenv()` must clean up with `unsetenv()` in all paths.

### test_context.cpp
Tests for `ContextManager` push/serialize/compact behavior.

```
context_push_system_serializes
context_push_user_serializes
context_push_assistant_no_tools_serializes
context_push_assistant_with_tool_calls_serializes
context_push_tool_result_serializes
context_to_json_message_order    -- system first, then user/assistant in order
context_token_estimation         -- push text, estimate = len/4 approx
context_sync_token_count         -- overwrites estimate with exact value
context_needs_compaction_false   -- below limit
context_needs_compaction_true    -- at or above limit
context_compact_preserves_system -- system message never dropped
context_compact_drops_oldest_non_system
context_compact_keeps_tool_groups_together  -- never splits assistant+tool result pair
context_message_count
```

### test_tools.cpp
Tests for local tool implementations. Uses a temp directory (mkdtemp or `std::filesystem::temp_directory_path()`) for write tests; cleanup in each test.

```
tool_read_file_existing        -- reads content correctly
tool_read_file_missing         -- returns ToolResult::fail
tool_list_dir_existing         -- returns file names
tool_list_dir_missing          -- returns fail
tool_file_info_existing        -- size, exists=true
tool_file_info_missing         -- exists=false
tool_search_files_finds_match  -- pattern found in files
tool_search_files_no_match     -- returns empty result, not error
tool_write_file_creates_new    -- creates file with content
tool_write_file_overwrites     -- replaces existing file atomically
tool_write_file_tmp_then_rename -- .ccl.tmp is cleaned up, target exists
tool_write_file_generates_diff -- ToolResult::content contains diff on update
tool_write_file_creates_dir    -- creates parent dirs if needed
```

### test_llm_client.cpp
Tests the non-HTTP parts: request JSON building and response JSON parsing. Does not make real HTTP calls.

```
llm_request_build_basic         -- model, messages, tools present in JSON
llm_request_build_with_tools    -- tools array serialized correctly
llm_request_no_stream           -- stream:false when streaming disabled
llm_request_with_stream         -- stream:true when streaming enabled
llm_parse_response_text_only    -- content field, no tool_calls
llm_parse_response_tool_calls   -- tool_calls array parsed correctly
llm_parse_response_usage        -- prompt_tokens, completion_tokens extracted
llm_extract_tool_calls_multi    -- multiple tool calls in one response
llm_parse_malformed_response    -- graceful error on missing fields
llm_retryable_error_429         -- is_retryable_error(429) == true
llm_retryable_error_503         -- is_retryable_error(503) == true
llm_non_retryable_error_400     -- is_retryable_error(400) == false
llm_non_retryable_error_401     -- is_retryable_error(401) == false
```

Note: `build_request()` and `parse_response()` must be testable without a live CURL handle. Make them `static` free functions or extract them as standalone functions that take/return `std::string` so they can be called directly in tests without constructing a full `LlmClient`.

---

## Implementation Notes

### Testability of LlmClient

The current `build_request()` and `parse_response()` are private member functions. To make them testable without a live CURL connection, expose them as free functions or as `static` member functions:

```cpp
// In llm_client.h, expose as static for testability:
static std::string build_request_json(
    const ContextManager& ctx,
    const std::vector<ToolDef>& tools,
    const Config& cfg,
    bool stream);

static LlmResponse parse_response_json(const std::string& body);
static LlmResponse::Usage parse_usage(const JsonValue& root);
static std::vector<ToolCall> parse_tool_calls(const JsonValue& root);
```

### Temp File Cleanup in tool tests

Use RAII guard in test_tools.cpp:
```cpp
struct TmpDir {
    std::string path;
    TmpDir() {
        char tmpl[] = "/tmp/ccl_test_XXXXXX";
        path = mkdtemp(tmpl);
    }
    ~TmpDir() {
        std::filesystem::remove_all(path);  // cleanup
    }
};
```

### Config Env Var Tests

Use `setenv()`/`unsetenv()` (POSIX). Wrap each env var test in a guard:
```cpp
TEST(config_env_api_key) {
    setenv("CCL_API_KEY", "test_key", 1);
    auto c = Config::defaults();
    Config::apply_env_overrides(c);
    CHECK_EQ(c.api_key, std::string("test_key"));
    unsetenv("CCL_API_KEY");
}
```

This requires `apply_env_overrides()` to be a separate testable function rather than inlined in `Config::load()`.

---

## Running Tests

### Build and Run

```bash
cmake -B build && cmake --build build
./build/ccl_test
```

### With CTest

```bash
ctest --test-dir build --output-on-failure
```

### Expected Output

```
[pass] json_parse_null
[pass] json_parse_bool_true
...
[pass] llm_non_retryable_error_401

47 tests: 47 passed, 0 failed
```
