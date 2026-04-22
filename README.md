# ccl — C++ Coding Loop

A minimal, self-contained agentic coding CLI. One binary. One config file. No surprises.

## Features

- **Agentic loop:** Agent reads code, formulates plans, executes with approval
- **Two modes:** Plan (explores codebase, builds plans), Act (executes changes)
- **Tool-use:** Read files, search code, write files atomically, run shell commands
- **Streaming:** Real-time LLM token output via SSE
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
endpoint = "http://localhost:4000/v1"
api_key = "sk-..."
model = "qwen3-235b"
token_limit = 8000
```

Override with environment variables:
```bash
export CCL_API_KEY="sk-..."
export CCL_ENDPOINT="http://localhost:4000/v1"
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
- `/help` — show help
- `/quit` — exit

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
- JSON parser (parse, serialize, roundtrip)
- TOML parser + env var override logic
- Context manager (message history, compaction)
- Local tools (file I/O, atomicity, diffs)
- LLM client (request building, response parsing)

See `TEST_PLAN.md` for details.

## Project Structure

```
ccl/
├── CMakeLists.txt
├── Dockerfile                   -- standardized dev environment
├── config/
│   └── ccl.toml.example        -- config template
├── src/
│   ├── main.cpp                -- CLI parsing, signal handling
│   ├── types.h                 -- shared types
│   ├── json.h/cpp              -- JSON parser (recursive descent)
│   ├── config.h/cpp            -- TOML parser + config loading
│   ├── context.h/cpp           -- message history + compaction
│   ├── tools.h/cpp             -- tool registry + implementations
│   ├── llm_client.h/cpp        -- HTTP client + streaming
│   ├── agent.h/cpp             -- core agent loop
│   └── ui.h/cpp                -- terminal UI
└── tests/
    ├── harness.h               -- custom test harness
    ├── main.cpp
    ├── test_json.cpp
    ├── test_config.cpp
    ├── test_context.cpp
    ├── test_tools.cpp
    └── test_llm_client.cpp
```

## Design Principles

1. **Minimal dependencies:** Only libcurl at runtime. Build uses system cmake/compiler.
2. **Supply-chain safety:** All parsing (JSON, TOML) written from scratch. Custom test harness — no Boost, Google Test, or Catch2.
3. **Explicit layers:** Clear separation: UI → Agent Loop → LLM Client + Tool Registry → Utilities (JSON, TOML, Context).
4. **Terminal-friendly:** Pure text I/O. No fancy UI framework. Natural scrolling terminal. ASCII only.
5. **Testability:** All core components unit-tested. Agent and UI are integration layers tested manually.

## Documentation

- `PLAN.md` — architecture, design decisions, development roadmap
- `TEST_PLAN.md` — unit testing strategy, test harness design, test cases
- Dockerfile comments — build environment rationale

## License

MIT (TBD)
