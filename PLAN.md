# ccl — C++ Coding Loop

```
ccl                    -- runs with ./ccl.toml in cwd or ~/.config/ccl/config.toml
ccl --mode plan        -- start in plan mode
ccl --model qwen3-235b -- override model
ccl --endpoint http://localhost:4000/v1
ccl --config /path/to/config.toml
```

A minimal, self-contained agentic coding loop.
One binary. One config file. No surprises.

## Install
```
  cmake -B build && cmake --build build
  cp build/ccl ~/.local/bin/
```

## Configure
```
  # Project-local config
  cp ccl.toml.example ./ccl.toml

  # User-global config
  mkdir -p ~/.config/ccl
  cp ccl.toml.example ~/.config/ccl/config.toml

  # Environment variables override file config
  export CCL_API_KEY="sk-..."
  export CCL_CONFIG="./ccl.toml"
```

## Use
```
  ccl
```



## Project Structure

```
ccl/
├── CMakeLists.txt
├── config/
│   └── ccl.toml.example     -- default config template
└── src/
    ├── main.cpp             -- CLI parsing, signal handling, startup
    ├── types.h              -- shared types, zero dependencies
    ├── json.h/cpp           -- zero-dep JSON parser (recursive descent) + serializer
    ├── config.h/cpp         -- custom minimal TOML parser + env var fallback
    ├── context.h/cpp        -- message history, sliding window compaction, tokens
    ├── tools.h/cpp          -- tool registry, local tool implementations
    ├── llm_client.h/cpp     -- libcurl HTTP client, blocking + streaming, retry logic
    ├── agent.h/cpp          -- core agent loop, mode transitions, slash command parsing
    └── ui.h/cpp             -- terminal UI (stdin/stdout, simple text-based)
```

**Dependencies:** Only libcurl (system package). Zero external build dependencies.

## Layer Diagram

```
+-----------------------------------+
|  Ui (Terminal/Stdin/Stdout)      |
|  read/write text, approval       |
+---------------+-------------------+
                |
+---------------v-------------------+
|        Agent Loop                 |
| mode | orchestration | approval   |
+------+----------+------------------+
       |          |
+------v---+  +---v------------------+
| LlmClient |  |  ToolRegistry        |
| HTTP/retry|  | local tools|MCP stub |
| blocking+ |  |(write gated Plan)    |
| streaming |  |                      |
+------+---+  +---+------------------+
       |          |
+------v---+  +---v------+
| Context  |  |  Tools   |
| Manager  |  |atomic fs |
|(tokens)  |  |shell     |
+----------+  +----------+
       |
+------v-----------+
| JSON + TOML      |
| parser/ser       |
+------------------+
```

## types.h

```
// agent mode
enum class AgentMode {
    Explore,  // Q&A, no agenda
    Plan,     // read-only tools, builds plan
    Act,      // all tools, executes plan
};

enum class Approval {
    Accept,
    Reject,
    Edit,
};

// tool parameter definition
struct ToolParam {
    std::string name;
    std::string type;         // "string" | "integer" | "boolean"
    std::string description;
    bool        required;
};

// tool definition -- sent to LLM
struct ToolDef {
    std::string            name;
    std::string            description;
    std::vector<ToolParam> params;
};

// parsed tool call from LLM response
// Tools deserialize typed values as needed (int, bool, etc.) from JsonValue
using ToolArgs = std::unordered_map<std::string, JsonValue>;

struct ToolCall {
    std::string id;
    std::string name;
    ToolArgs    args;
};

// tool execution result
struct ToolResult {
    bool        success;
    std::string content;
    std::string error;

    static ToolResult ok(std::string content);
    static ToolResult fail(std::string error);
    std::string to_context_string() const;
};

// tool call record stored in context
struct ToolCallRecord {
    std::string id;
    std::string name;
    std::string arguments_json;   // raw, preserved exactly
};

// single context message
struct Message {
    enum class Role { System, User, Assistant, Tool };

    Role                     role;
    std::string              content;
    std::vector<ToolCallRecord> tool_calls;  // assistant only
    std::string              tool_call_id;   // tool only
    size_t                   estimated_tokens = 0;
};

// LLM response
struct LlmResponse {
    std::string           content;
    std::vector<ToolCall> tool_calls;
    std::string           finish_reason;

    struct Usage {
        size_t prompt_tokens     = 0;
        size_t completion_tokens = 0;
        size_t total_tokens      = 0;
    } usage;

    // streaming accumulation -- internal use
    struct PartialToolCall {
        std::string id;
        std::string name;
        std::string accumulated_args;
    };
    std::vector<PartialToolCall> partial_tool_calls;
};
```

### json.h/cpp

```
// type aliases
using JsonNull   = std::monostate;
using JsonBool   = bool;
using JsonNumber = double;
using JsonString = std::string;
// JsonArray and JsonObject defined after JsonValue (recursive)

// value type -- holds any JSON value
struct JsonValue {
    std::variant<JsonNull, JsonBool, JsonNumber, JsonString,
                 std::vector<JsonValue>, std::map<std::string, JsonValue>> data;

    // type checks
    bool is_null()   const;
    bool is_bool()   const;
    bool is_number() const;
    bool is_string() const;
    bool is_array()  const;
    bool is_object() const;

    // accessors -- throw on wrong type
    bool               as_bool()   const;
    double             as_number() const;
    const std::string& as_string() const;
    const JsonArray&   as_array()  const;
    const JsonObject&  as_object() const;

    // safe path access -- returns nullopt if missing
    std::optional<JsonValue> get(const std::string& key) const;
    std::optional<JsonValue> get(size_t index)           const;

    // chained access
    std::optional<JsonValue> operator[](const std::string& key) const;
    std::optional<JsonValue> operator[](size_t index)           const;
};

// public interface
JsonValue   parse_json(std::string_view input);
std::string to_json(const JsonValue& val);
std::string escape_json(const std::string& s);
```

### Config

```
struct McpServerConfig {
    std::string name;
    std::string command;         // stdio: "npx @mcp/server-fs /home"
    std::string url;             // http: "http://localhost:3000"
    std::unordered_set<std::string> write_tools;  // gated in Plan mode
};

struct Config {
    // LLM
    std::string endpoint    = "http://localhost:4000/v1";
    std::string api_key     = "";
    std::string model       = "qwen3-235b";
    int         timeout_sec = 30;
    int         max_retries = 3;  // for 429/5xx responses
    size_t      max_tokens  = 4096;
    float       temperature = 0.7f;
    bool        streaming   = true;  // use SSE streaming if supported

    // context
    size_t token_limit      = 8000;

    // tools
    std::vector<McpServerConfig> mcp_servers;  // empty until Phase 2

    // Config loading:
    // Search order: CLI --config > $CCL_CONFIG env > ./ccl.toml > ~/.config/ccl/config.toml > defaults
    // API key: file value overridden by $CCL_API_KEY or $OPENAI_API_KEY env vars
    static Config load(const std::string& path);
    static Config defaults();
};
```

### TOML Format

Parsed by custom minimal TOML parser in config.cpp (~200 lines). Only flat key=value and [sections] with key=value are supported.

```
endpoint    = "http://localhost:4000/v1"
api_key     = ""
model       = "qwen3-235b"
timeout_sec = 30
token_limit = 8000
streaming   = true

# Phase 2 -- MCP servers (section support required)
# [mcp_servers]
# filesystem = "npx @modelcontextprotocol/server-filesystem /home"
```

**Environment variable override:** `CCL_API_KEY`, `CCL_CONFIG`, `CCL_ENDPOINT` (each overrides file config)

### Context

```
class ContextManager {
public:
    explicit ContextManager(size_t token_limit);

    // push helpers
    void push_system   (std::string content);
    void push_user     (std::string content);
    void push_assistant(std::string content,
                        std::vector<ToolCallRecord> tool_calls = {});
    void push_tool_result(std::string call_id, const ToolResult& result);

    // sync exact token count from API response
    void sync_token_count(const LlmResponse::Usage& usage);

    // compaction
    bool needs_compaction() const;
    void compact();              // sliding window -- Phase 1
    void compact_summarized();  // local summary   -- Phase 2

    // serialization
    std::string to_json() const;

    // introspection
    size_t total_tokens()  const;
    size_t message_count() const;

private:
    std::vector<Message> messages_;
    size_t               total_tokens_  = 0;
    size_t               token_limit_;

    size_t estimate_tokens(const std::string& text) const;
    size_t index_of_first_non_system()               const;
    size_t find_safe_drop_end(size_t start)          const;
};
```

Invariant enforced here: tool_call groups (assistant +
tool results) are always dropped or kept together. Never split.

#### Token tracking strategy:

```
API response  => sync_token_count()   (exact, from usage field)
push results  => estimate_tokens()    (heuristic delta only)
next response => sync_token_count()   (exact again)
```

Token estimation: `text.size() / 4` (standard English/code approximation, corrected on next round-trip).

### Tools

```
// executor function type
using ToolFn = std::function<ToolResult(const ToolArgs&)>;

// source for TUI display
enum class ToolSource { Local, Mcp };

struct Tool {
    ToolDef     def;
    ToolFn      fn;
    ToolSource  source      = ToolSource::Local;
    std::string mcp_server  = "";   // set if source == Mcp
};

class ToolRegistry {
public:
    void register_tool(Tool tool);

    // returns defs for all registered tools -- sent to LLM
    std::vector<ToolDef> definitions() const;

    // lookup by name
    std::optional<const Tool*> find(const std::string& name) const;

private:
    std::vector<Tool>                          tools_;
    std::unordered_map<std::string, size_t>    index_;
};

// factory -- mode controls which tools are registered
ToolRegistry make_registry(AgentMode mode, const Config& config);
```

#### Phase 1 Local Tools

```
// read-only -- always registered in all modes
ToolResult tool_read_file   (const ToolArgs& args);
ToolResult tool_list_dir    (const ToolArgs& args);
ToolResult tool_search_files(const ToolArgs& args);  // ripgrep-style pattern matching
ToolResult tool_file_info   (const ToolArgs& args);  // size, mtime, exists, perms

// write -- Act mode only, require approval
ToolResult tool_write_file  (const ToolArgs& args);  // atomic: write to .tmp, rename()
ToolResult tool_create_dir  (const ToolArgs& args);
ToolResult tool_delete_file (const ToolArgs& args);  // extra approval gate

// shell -- Act mode only, sandboxed with timeout
ToolResult tool_run_shell   (const ToolArgs& args);  // timeout, cwd, timeout_sec (int)
```

**write_file implementation detail:**
- Reads existing file (if present) into memory
- Generates line-diff (simple Myers algorithm or subprocess `diff -u`)
- Writes new content to `{path}.ccl.tmp` (atomic)
- On success: renames to `{path}`, includes diff in ToolResult::content
- On failure: deletes .tmp file, returns error
- Diff displayed to stdout when tool_result is shown

#### Phase 2 MCP Stub

```
class McpClient {
public:
    explicit McpClient(const McpServerConfig& cfg);
    ~McpClient();  // SIGTERM child on stdio transport

    std::vector<ToolDef> list_tools();
    ToolResult           call_tool(const std::string& name,
                                   const ToolArgs& args);
private:
    // stdio: FILE* pipes + pid_t
    // http:  CURL* handle + base url
};

void register_mcp_server(ToolRegistry&      registry,
                         McpClient&         client,
                         const std::string& server_name);
```

### LLM Client

```
class LlmClient {
public:
    explicit LlmClient(const Config& config);
    ~LlmClient();

    // blocking with retry -- Phase 1
    // Exponential backoff (1s, 2s, 4s) for 429/5xx, max config.max_retries attempts
    LlmResponse complete(
        const ContextManager&       context,
        const std::vector<ToolDef>& tools);

    // streaming with retry -- Phase 2
    LlmResponse complete_streaming(
        const ContextManager&       context,
        const std::vector<ToolDef>& tools,
        std::function<void(std::string_view chunk)> on_chunk);

private:
    Config config_;
    CURL*  curl_;

    std::string build_request(
        const ContextManager&       context,
        const std::vector<ToolDef>& tools,
        bool stream) const;

    LlmResponse     parse_response        (const std::string& body)  const;
    std::vector<ToolCall> extract_tool_calls(const JsonValue& root)   const;
    LlmResponse::Usage    extract_usage     (const JsonValue& root)   const;

    // retry logic
    struct HttpResult { int status; std::string body; };
    HttpResult send_request_with_retry(const std::string& request_body, bool stream);
    bool is_retryable_error(int http_status) const;  // 429, 5xx

    // streaming internals
    static size_t write_callback  (char*, size_t, size_t, void*);
    static size_t stream_callback (char*, size_t, size_t, void*);

    void accumulate_streaming_tool_calls(
        LlmResponse& response, const JsonValue& delta) const;
    void finalize_tool_calls(LlmResponse& response) const;
};
```

#### HTTP Contract

```
POST {endpoint}/chat/completions
Authorization: Bearer {api_key}
Content-Type:  application/json

{
  "model":    "...",
  "messages": [...],
  "tools":    [...],
  "stream":   false,
  "max_tokens": 4096
}
```

### Agent:

```
class Agent {
public:
    Agent(Config config, Ui& ui);

    void run();   // blocks until user quits

private:
    Config          config_;
    AgentMode       mode_    = AgentMode::Explore;
    ContextManager  context_;
    LlmClient       llm_;
    ToolRegistry    registry_;
    Ui&             ui_;       // terminal UI (stdin/stdout)

    // main loop
    void loop();

    // response handling
    void handle_response(const LlmResponse& response);
    void handle_tool_calls(const std::vector<ToolCall>& calls);
    void handle_text_response(const std::string& content);

    // slash command parsing (user input begins with '/')
    // /mode explore, /mode plan, /mode act
    // /help, /quit, /clear
    bool handle_slash_command(std::string_view input);

    // mode transitions
    void transition_to(AgentMode next);
    void rebuild_registry();          // swap tool set on mode change

    // system prompts per mode
    std::string system_prompt() const;
};
```


### The work flow loop

```
startup
  → push system prompt
  → wait for user input

each iteration:
  1. compact context if needed
  2. complete(context, registry.definitions())
  3. sync_token_count(response.usage)
  4. push assistant message
  5. if tool_calls:
       for each call:
         show in TUI
         request approval
         execute if approved
         push tool result
       goto 2  (keep going until text response)
  6. if text:
       show in TUI
       wait for user input
       push user message
       goto 1
```

### Modes & System Prompts:

```
Explore: open Q&A, read tools available,
         no agenda, answer questions about codebase

Plan:    read-only tools, think step by step,
         produce numbered plan, do not write files,
         stop and present plan for approval

Act:     execute the approved plan step by step,
         use write tools, stop if unexpected,
         report what was done when complete
```

### Ui (Terminal Interface)

Simple text-based terminal interface. Single-threaded, synchronous I/O.

```
class Ui {
public:
    explicit Ui();

    // Display methods (write to stdout with formatting)
    void show_message    (std::string_view role, std::string_view content);
    void show_tool_call  (const ToolCall& call, ToolSource source);
    void show_tool_result(const ToolCall& call, const ToolResult& result);
    void show_mode       (AgentMode mode);
    void update_tokens   (size_t used, size_t limit);
    void append_chunk    (std::string_view chunk);  // for streaming, append then flush

    // Blocking interactive methods (read from stdin)
    Approval      request_approval(const ToolCall& call);
    ToolArgs      edit_args       (const ToolCall& call);  // not used initially
    std::string   wait_for_input  ();

private:
    // Helper methods for formatting
    std::string format_tool_call(const ToolCall& call, ToolSource source) const;
    std::string format_tool_result(const ToolResult& result) const;
};
```

**Output format (to stdout):**
```
[ccl] Mode: explore | tokens: 1234/8000
> user prompt here

agent: Agent's text response...

[call] tool_name arg1=val1 arg2=val2
[result] ✓ success message

Approve tool_name? [y/n]: 
```

### Main.cpp

```
int main(int argc, char* argv[]) {
    // CLI parsing (manual loop, no dependencies)
    Config config = Config::defaults();
    std::string config_path;
    AgentMode initial_mode = AgentMode::Explore;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            config.model = argv[++i];
        } else if (arg == "--endpoint" && i + 1 < argc) {
            config.endpoint = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string mode = argv[++i];
            if (mode == "plan") initial_mode = AgentMode::Plan;
            else if (mode == "act") initial_mode = AgentMode::Act;
        } else if (arg == "--no-stream") {
            config.streaming = false;
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        }
    }

    // load config (environment overrides file, file overrides defaults)
    auto loaded_config = Config::load(config_path);
    config.merge(loaded_config);

    // Signal handling -- graceful shutdown on SIGINT/SIGTERM
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // run agent
    Ui ui;
    Agent agent(config, ui);
    agent.run();

    return 0;
}
```

**Signal handler:** Sets a flag that Agent checks in its main loop to gracefully exit.

### CMake Build System:

```
cmake_minimum_required(VERSION 3.20)
project(ccl)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libcurl (system package required)
find_package(CURL REQUIRED)

# Main binary
add_executable(ccl
    src/main.cpp
    src/json.cpp
    src/config.cpp
    src/context.cpp
    src/tools.cpp
    src/llm_client.cpp
    src/agent.cpp
    src/ui.cpp
)

target_link_libraries(ccl PRIVATE
    CURL::libcurl
)

# Compiler flags
target_compile_options(ccl PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion
)

# Optional: static binary
# set_target_properties(ccl PROPERTIES LINK_SEARCH_START_STATIC ON)
```

### Development Plan

**Phase 1 -- Core agent (one week)**

- CMakeLists.txt        C++23, libcurl, minimal dependencies
- types.h               complete type definitions + ToolArgs = map<string, JsonValue>
- json.cpp              recursive descent parser + serializer
- config.cpp            custom minimal TOML parser, env var fallback, config search
- context.cpp           message management, sliding window, token estimation (text.size/4)
- tools.cpp             read_file, list_dir, search_files, file_info (read-only tools)
- llm_client.cpp        libcurl blocking complete(), request building, response parsing, retry logic
- ui.cpp                terminal output, prompt input, approval dialogs (simple text-based)
- agent.cpp             core loop, slash command handling, mode transitions, tool dispatch
- main.cpp              CLI parsing, signal handling, config loading, startup

**Phase 2 -- Write tools & streaming (one week)**

- tools.cpp             write_file (atomic .tmp -> rename), create_dir, delete_file, run_shell (timeout)
- llm_client.cpp        complete_streaming() with SSE parsing, on_chunk callback
- ui.cpp                streaming chunk display, improved output formatting
- agent.cpp             tool approval workflow, error handling

**Phase 3 -- Polish & optimization**

- context.cpp           compact_summarized() using LLM (context compression for long conversations)
- tools.cpp             edge cases, error messages, diff generation for write_file
- agent.cpp             better system prompts, mode-specific behavior

**Phase 2+ -- MCP (after core is stable)**

- mcp_client.cpp        stdio transport (fork/exec + pipe), http transport
- tools.cpp             register_mcp_server from McpClient
- config.cpp            [mcp_servers] section support

---

## Implementation Notes

### Error Handling Strategy

- **At layer boundaries** (file I/O, HTTP, JSON parse): Return `std::expected<T, std::string>` (C++23) or `std::optional<T>` with error reporting to UI
- **Tool results:** Always succeed with `ToolResult::ok()` or `ToolResult::fail()` — never throw
- **Exceptions:** Only for unrecoverable invariant violations (e.g., memory exhaustion). All normal error paths are recoverable.

### JSON Value Representation

Due to recursive nature (Array contains JsonValue, Object maps to JsonValue), use:
- **Recommended approach:** Store `std::vector<std::shared_ptr<JsonValue>>` for arrays and `std::map<std::string, std::shared_ptr<JsonValue>>` for objects
- This keeps the variant simpler and is exception-safe with shared_ptr lifetime management

### Token Estimation

- Use `text.size() / 4` as baseline estimate (standard English/code approximation)
- Cache estimated tokens on each Message (store estimated_tokens field)
- The sync_token_count() from API response corrects drift on next round-trip
- Don't re-estimate if message already estimated

### Config Merge Strategy

When loading with environment variables:
1. Start with Config::defaults()
2. Load TOML file (if found via search order) -- overwrite defaults
3. Check env vars (CCL_API_KEY, CCL_ENDPOINT, CCL_TIMEOUT) -- overwrite file values

Config search order:
1. `--config <path>` CLI flag
2. `$CCL_CONFIG` environment variable
3. `./ccl.toml` (project-local)
4. `~/.config/ccl/config.toml` (user-global)
5. Built-in defaults (no file required)

### Tool Approval Gates

- **Always gated (all modes):** run_shell (safety), delete_file (destructive)
- **Gated in Plan mode only:** write_file, create_dir (side effects)
- **Never gated:** read_file, list_dir, search_files, file_info (safe reads)

### Workflow Example: Explore -> Plan -> Act

```
Start: Mode = Explore
User: "refactor the auth module"
  => Agent: "I'll read the current impl"
  => read_file /src/auth.cpp OK (no gate, reads always allowed)
  => "/mode plan"  (slash command)
  => Mode = Plan, rebuild_registry (write tools disabled)
  => Agent: "Here's my plan:\n  1. Restructure...\n  2. Refactor..."
  => "/mode act"
  => Mode = Act, rebuild_registry (write tools enabled)
  => Agent: "Executing step 1: write_file"
  => write_file call -> approval dialog -> [y]es -> execute -> show diff
  => "Step 1 complete. Moving to step 2..."
```

### Atomic File Writes

To prevent partial/corrupt writes on crash or interrupt:

1. Open target file for read (if exists), read into buffer
2. Generate unified diff of old -> new (line-by-line, simple format or `diff -u` subprocess)
3. Write new content to `{path}.ccl.tmp` (atomic on most filesystems)
4. If write succeeds: `rename("{path}.ccl.tmp", "{path}")` (atomic POSIX operation)
5. If write fails: `unlink("{path}.ccl.tmp")` (cleanup)
6. Return diff string in ToolResult::content to be shown in approval prompt

Diff format (simple unified diff, ASCII only):
```
--- /src/auth.cpp (old)
+++ /src/auth.cpp (new)
@@ -10,3 +10,4 @@
 void auth_init() {
-  static_token = "abc123";
+  jwt_secret = "xyz789";
+  jwt_exp = 3600;
 }
```

When tool_result is shown, if diff is present, display it to the user (helps with approval decision). Use standard unified diff format (no special characters).

### Signal Handling

Main.cpp registers SIGINT/SIGTERM handlers that:
1. Set a volatile atomic flag `should_exit`
2. Agent main loop checks flag and exits gracefully
3. Normal cleanup (context saved, sockets closed) happens as part of normal scope exit

### Terminal UX & Output Format

All output is plain text to stdout/stderr. Terminal scrolls naturally (no fancy rendering).

**Example session (ASCII-only):**

```
$ ccl --mode explore

[ccl] Mode: explore | tokens: 0/8000
> read the auth module and explain its structure

[agent] I'll read and analyze the auth module for you.

[call] read_file path=/src/auth.cpp (local)

[result] OK - 312 bytes

[agent] The auth module has three main components:
  1. User authentication...
  2. Token generation...
  3. Session management...

tokens: 342/8000

> /mode plan
[ccl] Mode: plan

> refactor to use JWT tokens

[agent] Here's my refactoring plan:
  1. Replace session tokens with JWT
  2. Update validation logic
  3. Add token expiration

tokens: 645/8000

> /mode act
[ccl] Mode: act

[agent] Starting refactoring...

[call] write_file path=/src/auth.cpp (local)
  arg: content={...1024 bytes...}

Approve write_file? [y/n]: y
[result] OK - wrote 1024 bytes

[agent] Refactoring complete!

tokens: 1203/8000

> 
```

**Output conventions (ASCII only):**

- `[ccl]` — mode indicator + token count (printed once per agent turn)
- `[agent]` — agent text response (prefixed line by line)
- `[call]` — tool call details (name + args)
- `[result]` — tool result (OK/ERROR + summary)
- `Approve <tool>? [y/n]: ` — approval prompt, reads single char
- `tokens: used/limit` — printed after tool execution and agent responses
- Blank lines separate major sections for readability
- All special markers use brackets: [OK], [ERROR], [call], [result], [agent], [ccl]

### Streaming Implementation

LLM streaming uses SSE (Server-Sent Events) format:
```
data: {"delta":{"content":"Hello"}}
data: {"delta":{"content":" world"}}
data: [DONE]
```

Implementation:
1. CURL callback accumulates HTTP body line-by-line
2. On "data: " prefix, parse JSON delta
3. Pass content delta to `ui.append_chunk()` (prints chunk to stdout, flushes)
4. Accumulate partial tool calls across multiple deltas
5. On [DONE], finalize tool calls and return complete LlmResponse


---

## Verification Checklist

- [ ] Build: `cmake -B build && cmake --build build` → single `ccl` binary, no warnings, ~10s build time
- [ ] JSON parser: valid/invalid JSON, unicode, escapes, nested arrays/objects, round-trip
- [ ] TOML parser: flat keys, values, [section] support, env var override precedence
- [ ] Config: defaults work with no file, env vars override file values, search path priority correct
- [ ] CLI parsing: --model, --endpoint, --mode, --config all work, unknown flags ignored or error
- [ ] LLM integration: request sent with correct format, response parsed, tokens tracked
- [ ] Tool execution: read/write/shell tools work, approval gates enforced per mode
- [ ] Mode transitions: `/mode plan` disables write tools, `/mode act` enables them
- [ ] Atomic writes: .tmp file cleaned up on failure, atomicity preserved on success, diff generated
- [ ] Streaming: SSE chunks accumulated and printed in real-time (with flushing)
- [ ] Approval flow: tool call displayed, prompt shown, user input processed correctly
- [ ] Signal handling: Ctrl+C cleanly exits, no partial state left behind
- [ ] Token tracking: estimates reasonable, synced on each response, compaction triggers at limit
- [ ] Terminal I/O: formatting readable, no control characters, line breaks proper
