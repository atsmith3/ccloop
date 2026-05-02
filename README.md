# ccl — C++ Coding Loop

A minimal, self-contained agentic coding CLI. One binary. One config file. No surprises.

## Features

- **Agentic loop:** Agent reads code, formulates plans, executes with approval
- **Two modes:** Plan (explores codebase, builds plans), Act (executes changes)
- **Tool-use:** Read files, search code, write files atomically, run shell commands
- **MCP tools:** Connect to any MCP server over SSE/HTTP — tools appear alongside built-in tools
- **Multi-connector:** OpenAI JSON and AWS Bedrock — selected via one config line
- **Zero external dependencies:** Only libcurl. No npm, pip, Boost, or test frameworks
- **Supply-chain safe:** Custom minimal TOML/JSON parsers, custom test harness, all from stdlib

## Quick Start

### With Docker (Recommended)

**Build the image:**
```bash
docker build -t ccloop:latest .
```

**Enter the build environment (interactive shell):**
```bash
docker run --rm -it -u $(id -u):$(id -g) -v $PWD:/workspace:z ccloop:latest /bin/bash
```

Then inside the container:
```bash
cmake -B build -G Ninja && cmake --build build
make -C build test                                # run all tests
./build/ccl                                       # run the CLI
valgrind --leak-check=full ./build/ccl_test      # check for memory leaks
```

**Or run build/test commands directly (non-interactive):**
```bash
# Build and run all tests
docker run --rm -u $(id -u):$(id -g) -v $PWD:/workspace:z ccloop:latest bash -c "cd /workspace && cmake -B build -G Ninja && cmake --build build && make -C build test"

# Or with Make instead of Ninja
docker run --rm -u $(id -u):$(id -g) -v $PWD:/workspace:z ccloop:latest bash -c "cd /workspace && cmake -B build && make -C build && make -C build test"

# Or with ctest
docker run --rm -u $(id -u):$(id -g) -v $PWD:/workspace:z ccloop:latest bash -c "cd /workspace && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure"
```

The `:z` flag on the volume mount is for SELinux compatibility (Fedora/RHEL). Files will have matching ownership with your host user.

**Docker image includes:**
- GCC 15 and Clang 21 (full C++23 support)
- CMake, Ninja, libcurl, gdb, valgrind

### Native Build (Ubuntu 24.04+, Debian 13+, Fedora 41+)

**Install dependencies:**
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake libcurl4-openssl-dev

# Fedora
sudo dnf install gcc g++ cmake libcurl-devel
```

**Build:**
```bash
cmake -B build && cmake --build build
./build/ccl_test   # run tests
./build/ccl        # run the CLI
```

## Configure

Copy the config template:
```bash
cp config/ccl.toml.example ./ccl.toml
```

Edit `ccl.toml`:
```toml
endpoint    = "http://localhost:4000/v1"
api_key     = "sk-..."
model       = "qwen3-235b"
token_limit = 8000

connector = "openai-qwen"   # openai-qwen | openai-json | bedrock
```

**AWS Bedrock:**
```toml
connector      = "bedrock"
model          = "anthropic.claude-3-5-sonnet-20241022-v2:0"
aws_region     = "us-east-1"
aws_access_key = "AKIA..."
aws_secret_key = "..."
```

**MCP Tools:**

Add MCP servers by pointing `ccl.toml` at a JSON config file:
```toml
mcp_config = "~/.config/ccl/mcp.json"
```

Create `mcp.json` (see `config/mcp.json.example`):
```json
{
  "mcpServers": {
    "filesystem": {
      "url": "http://localhost:3001/mcp",
      "writeTools": ["write_file", "create_directory"]
    },
    "github": {
      "url": "http://localhost:3002/mcp",
      "apiKey": "ghp_..."
    }
  }
}
```

- `url` — MCP server's streamable HTTP endpoint (MCP spec 2024-11-05+)
- `apiKey` — optional Bearer token
- `writeTools` — tool names that require `write` approval (all others default to `read`)

MCP tools are discovered at startup via `tools/list` and registered alongside built-in tools. They show as `(mcp)` in the tool call display.

**Server requirements:** ccl uses the [Streamable HTTP transport](https://spec.modelcontextprotocol.io/specification/2024-11-05/basic/transports/#streamable-http) only. The older SSE transport is not supported. For the Python MCP SDK (`mcp >= 1.0`):
```python
mcp.run(transport="streamable-http", host="127.0.0.1", port=8300)
# url in mcp.json: "http://127.0.0.1:8300/mcp"
```

Override with environment variables:
```bash
export CCL_API_KEY="sk-..."
export CCL_ENDPOINT="http://localhost:4000/v1"
export CCL_MCP_CONFIG="~/.config/ccl/mcp.json"
ccl
```

## Usage

```bash
ccl                              # start in Plan mode (default)
ccl --mode act                   # start in Act mode
ccl -m act                       # short form of --mode
ccl --model qwen3-235b           # override model
ccl --endpoint http://localhost:4000/v1
ccl --config /path/to/config.toml
ccl --debug                      # log raw LLM responses to stderr
```

### Non-interactive / Subagent Mode

Use `-p` to supply a prompt on the command line. The agent runs a full plan→act cycle and exits — no stdin required.

```bash
# Plan, execute, exit (default plan mode → auto-transitions to act)
ccl -p "add input validation to the login endpoint"

# Skip prompts for all tool calls
ccl -p "add input validation to the login endpoint" -y

# Start directly in act mode (no planning phase)
ccl -p "add input validation to the login endpoint" -m act -y
```

**Flags:**

| Flag | Description |
|------|-------------|
| `-p`/`--prompt <text>` | Run one turn non-interactively then exit. In plan mode (default), auto-transitions to act and exits after act completes. |
| `-y`/`--yolo` | Auto-approve all tool calls — no approval prompts. |
| `-m`/`--mode plan\|act` | Start in the specified mode (`-m` is a short alias for `--mode`). |

### Interactive Session Example

```
[ccl] Mode: plan | tokens: 0/8000
> refactor the auth module to use JWT tokens

[agent] Let me explore the codebase first.

[call] read_file path=src/auth.cpp (local)
[result] OK - 1842 bytes

[agent] Here's my plan:
  ## Plan: auth-jwt-refactor
  1. [ ] Replace session token storage with JWT signing — src/auth.cpp
  2. [ ] Add token expiration validation — src/auth.cpp
  3. [ ] Update tests — tests/test_auth.cpp

tokens: 645/8000

> /mode act
[ccl] Mode: act

[call] write_file path=src/auth.cpp (local)
Approve write_file? [y/n]: y
[result] OK - wrote 2048 bytes

[agent] Refactoring complete!

tokens: 1203/8000
>
```

**Slash commands:**
- `/mode plan|act` — switch modes
- `/clear` — reset context (keeps system prompt)
- `/help` — show help
- `/quit` — exit

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                     main.cpp                         │
│          CLI args · Config loading · Signals         │
└───────────────────────────┬──────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────┐
│                      Agent                           │
│     plan ↔ act loop  ·  context compaction           │
│     tool approval    ·  /mode /clear /help           │
└────────────┬──────────────────────────┬──────────────┘
             │                          │
┌────────────▼────────────┐  ┌──────────▼─────────────┐
│        LlmClient        │  │      ToolRegistry       │
│   (connector facade)    │  │  local tools            │
└────────────┬────────────┘  │  + MCP tools (dynamic) │
             │               └──────────┬──────────────┘
┌────────────▼────────────┐             │
│   Connector (factory)   │  ┌──────────▼──────────────┐
├────────────┬────────────┤  │      McpClient(s)       │
│ OpenAI     │ Bedrock    │  │  JSON-RPC · SSE/HTTP    │
│ JSON tc    │ SigV4      │  └─────────────────────────┘
└────────────┴────────────┘

─────────────────────────────────────────────────────
 json · config · context · types · ui
─────────────────────────────────────────────────────
```

The LLM connector is selected at startup from `ccl.toml`. MCP servers are loaded from the
optional `mcp_config` JSON file — one `McpClient` per server, tools registered at startup.

## Testing

Unit tests are built into the project — no external test framework dependency.

**Quick test (Docker):**
```bash
docker run --rm -u $(id -u):$(id -g) -v $PWD:/workspace:z ccloop:latest bash -c "cd /workspace && cmake -B build -G Ninja && cmake --build build && make -C build test"
```

**Run tests directly (after building):**
```bash
cmake --build build
./build/ccl_test              # run the test binary directly
```

**Or with CTest:**
```bash
make -C build test            # Make
cmake --build build --target test  # Ninja
ctest --test-dir build --output-on-failure
```

**Memory leak testing:**
```bash
docker run --rm -u $(id -u):$(id -g) -v $PWD:/workspace:z ccloop:latest bash -c "cd /workspace && cmake -B build -G Ninja && cmake --build build && valgrind --leak-check=full ./build/ccl_test"
```

**Test coverage:**
- JSON parser (parse, serialize, roundtrip, edge cases)
- TOML parser + env var override logic
- Context manager (message history, token estimation, compaction)
- Local tools (file I/O, atomicity, diffs, shell execution)
- Connector request building + response parsing (Qwen, OpenAI, Bedrock)
- MCP client JSON-RPC building and SSE/HTTP response parsing

## Project Structure

```
ccl/
├── CMakeLists.txt
├── Dockerfile                    -- standardized dev environment
├── ETHOS.md                      -- project values
├── config/
│   ├── ccl.toml.example         -- config template
│   └── mcp.json.example         -- MCP servers config template
├── src/
│   ├── main.cpp                 -- CLI parsing, signal handling
│   ├── types.h                  -- shared types (ToolDef, LlmResponse, etc.)
│   ├── json.h/cpp               -- JSON parser (recursive descent)
│   ├── config.h/cpp             -- TOML parser + config loading
│   ├── context.h/cpp            -- message history + token compaction
│   ├── tools.h/cpp              -- tool registry + all tool implementations
│   ├── mcp_client.h/cpp         -- MCP client (JSON-RPC over SSE/HTTP)
│   ├── connector.h/cpp          -- Connector interface + factory
│   ├── connector_base.h/cpp     -- shared HTTP/retry layer
│   ├── connector_openai.h/cpp   -- OpenAI endpoint + JSON tool calling
│   ├── connector_bedrock.h/cpp  -- AWS Bedrock Converse API + SigV4
│   ├── llm_client.h/cpp         -- thin facade over Connector
│   ├── agent.h/cpp              -- core agent loop (plan/act modes)
│   └── ui.h/cpp                 -- terminal UI
└── tests/
    ├── harness.h                -- custom test harness
    ├── main.cpp
    ├── test_json.cpp
    ├── test_config.cpp
    ├── test_context.cpp
    ├── test_tools.cpp
    ├── test_llm_client.cpp
    └── test_mcp_client.cpp
```

## Design Principles

1. **Minimal dependencies:** Only libcurl at runtime. Build uses system cmake/compiler.
2. **Supply-chain safety:** All parsing (JSON, TOML) written from scratch. Custom test harness — no Boost, Google Test, or Catch2.
3. **Explicit layers:** `UI → Agent → LlmClient → Connector (Qwen/OpenAI/Bedrock) + ToolRegistry → Utilities (json, config, context)`
4. **Terminal-friendly:** Pure text I/O. No fancy UI framework. Natural scrolling terminal. ASCII only.
5. **Testability:** All core components unit-tested. Static helper methods on connectors make request/response logic testable without network access.

## Documentation

- `ETHOS.md` — project values and philosophy

## License

[MIT](LICENSE.md)
