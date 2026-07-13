# ccl — C++ Coding Loop

A minimal, self-contained agentic coding CLI. One binary. One config file. No surprises.

## Features

- **Agentic loop:** Agent reads and edits code and runs commands, with approval
- **Minimal fixed toolset:** `read_file`, `write_file`, `edit_file`, and `run_shell` — the terminal covers listing, searching, metadata, and everything else
- **MCP tools:** Connect to any MCP server over stdio, HTTP, or legacy SSE — tools appear alongside the built-in tools
- **Multi-connector:** OpenAI-compatible JSON and AWS Bedrock — selected via one config line
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

## Install

After building, install to a prefix:

```bash
# Per-user (no sudo required)
cmake --install build --prefix ~/.local

# System-wide
sudo cmake --install build --prefix /usr/local
```

The binary lands at `<prefix>/bin/ccl`. `DESTDIR` staging for `.deb`/`.rpm` packaging is supported:

```bash
DESTDIR=/tmp/staging cmake --install build --prefix /usr
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
model       = "gpt-4o"
token_limit = 8000

connector = "openai-json"   # openai-json | bedrock
```

Any OpenAI-compatible endpoint (local models, Qwen, etc.) works with `connector = "openai-json"`.

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
      "transport": "stdio",
      "command": "npx -y @modelcontextprotocol/server-filesystem /home/user/docs",
      "writeTools": ["write_file", "create_directory"]
    },
    "my-http-server": {
      "transport": "http",
      "url": "http://localhost:3001/mcp",
      "apiKey": "optional-token"
    },
    "legacy-sse-server": {
      "transport": "sse",
      "url": "http://localhost:3002/sse"
    }
  }
}
```

| Field | Description |
|-------|-------------|
| `transport` | `"http"` (default), `"stdio"`, or `"sse"` |
| `url` | HTTP/SSE endpoint — required for `http` and `sse` |
| `command` | Shell command to launch server — required for `stdio` |
| `apiKey` | Optional Bearer token (`http` and `sse` transports) |
| `writeTools` | Tool names requiring `write` approval (all others default to `read`) |

MCP tools are discovered at startup via `tools/list` and registered alongside built-in tools. They show as `(mcp)` in the tool call display.

ccl supports all three MCP transports: **streamable-http** (spec 2024-11-05+, default), **stdio** (subprocess with line-delimited JSON-RPC — standard for Claude Desktop plugins and `npx`-based servers), and **legacy-sse** (pre-2024-11-05 GET /sse + POST).

Override with environment variables:
```bash
export CCL_API_KEY="sk-..."
export CCL_ENDPOINT="http://localhost:4000/v1"
export CCL_MCP_CONFIG="~/.config/ccl/mcp.json"
ccl
```

## Usage

```bash
ccl                              # start an interactive session
ccl --model qwen3-235b           # override model
ccl --endpoint http://localhost:4000/v1
ccl --config /path/to/config.toml
ccl --debug                      # log raw LLM responses to stderr
ccl --version                    # print version and build info
ccl --help                       # show usage
```

### Non-interactive Mode

Use `-p` to supply a prompt on the command line. The agent runs the task and exits — no stdin required.

```bash
# Run and exit
ccl -p "add input validation to the login endpoint"

# Skip prompts for all tool calls
ccl -p "add input validation to the login endpoint" -y
```

**Flags:**

| Flag | Description |
|------|-------------|
| `-p`/`--prompt <text>` | Run non-interactively until the task is done, then exit. |
| `-y`/`--yolo` | Auto-approve all tool calls — no approval prompts. |
| `-s`/`--silent` | Suppress all output except the final completion. |
| `-v`/`--version` | Print version and build info, then exit. |
| `-h`/`--help` | Show usage, then exit. |

### Interactive Session Example

```
tokens: 0/8000
> refactor the auth module to use JWT tokens

[call] read_file path=src/auth.cpp (local)
[result] OK - 1842 bytes

[call] edit_file path=src/auth.cpp (local)
Approve edit_file? [y/n]: y
[result] OK - Edited: src/auth.cpp (+12 lines)

[completed] Refactoring complete — auth now issues and validates JWTs.

tokens: 1203/8000
>
```

When the agent needs input, it simply replies with a question and no tool call; that ends the turn so you can answer on the next line.

**Slash commands:**
- `/clear` — reset context (keeps system prompt)
- `/compact` — summarize and compact the context window
- `/export <file>` — save the session to a file
- `/import <file>` — restore a session from a file
- `/context` — show current context usage (tokens used / max)
- `/mcp list|reload` — show MCP server status or reconnect
- `/edit` — open `$EDITOR` to compose a multi-line prompt
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
│     single-mode loop  ·  context compaction          │
│     tool approval     ·  /clear /compact /help        │
└────────────┬──────────────────────────┬──────────────┘
             │                          │
┌────────────▼────────────┐  ┌──────────▼─────────────┐
│   Connector (factory)   │  │      ToolRegistry       │
├────────────┬────────────┤  │  local tools            │
│ OpenAI-JSON│ Bedrock    │  │  + MCP tools (dynamic) │
│            │ SigV4      │  └──────────┬──────────────┘
└────────────┴────────────┘             │
                            ┌──────────▼──────────────┐
                            │      McpClient(s)        │
                            │  JSON-RPC over           │
                            │  http / stdio / sse      │
                            └──────────────────────────┘

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
- Connector request building + response parsing (OpenAI-JSON, Bedrock)
- MCP client: JSON-RPC building, response parsing, STDIO subprocess I/O, legacy-SSE event dispatch

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
│   ├── main.cpp                 -- CLI parsing, config loading, startup
│   ├── signals.h/cpp            -- SIGINT handling (graceful interrupt)
│   ├── types.h                  -- shared types (ToolDef, LlmResponse, etc.)
│   ├── json.h/cpp               -- JSON parser (recursive descent)
│   ├── config.h/cpp             -- TOML parser + config loading
│   ├── context.h/cpp            -- message history + token compaction
│   ├── tools.h/cpp              -- tool implementations
│   ├── tool_registry.h/cpp      -- tool registration + lookup
│   ├── mcp_client.h/cpp         -- MCP client (JSON-RPC over http/stdio/sse)
│   ├── connector.h/cpp          -- Connector interface + factory
│   ├── connector_base.h/cpp     -- shared HTTP/retry layer
│   ├── connector_openai.h/cpp   -- OpenAI-compatible endpoint + JSON tool calling
│   ├── connector_bedrock.h/cpp  -- AWS Bedrock Converse API + SigV4
│   ├── agent.h/cpp              -- core agent loop (single mode)
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
3. **Explicit layers:** `UI → Agent → Connector (OpenAI-JSON/Bedrock) + ToolRegistry → Utilities (json, config, context)`
4. **Terminal-friendly:** Pure text I/O. No fancy UI framework. Natural scrolling terminal. ASCII only.
5. **Testability:** All core components unit-tested. Static helper methods on connectors make request/response logic testable without network access.

## Documentation

- `ETHOS.md` — project values and philosophy

## License

[MIT](LICENSE.md)
