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

#include "agent.h"
#include "config.h"
#include "ui.h"
#include "version.h"
#include <atomic>
#include <csignal>
#include <iostream>

static void sigint_handler(int) { should_interrupt = true; }

static void sigterm_handler(int) { should_exit = true; }

static void print_version() {
  std::cout << "ccl " << CCL_VERSION << "\n"
            << "Build:  " << CCL_BUILD_TYPE << "\n"
            << "Flags:  " << CCL_CXX_FLAGS << "\n";
}

static void print_help() {
  std::cout
      << "ccl — C++ Coding Loop\n\n"
      << "Usage: ccl [OPTIONS]\n\n"
      << "Options:\n"
      << "  --config <path>          Config file path\n"
      << "  --model <name>           LLM model (overrides config)\n"
      << "  --endpoint <url>         API endpoint (overrides config)\n"
      << "  --mode|-m plan|act       Start in specified mode\n"
      << "  --prompt|-p <text>       Run one turn non-interactively then exit\n"
      << "  --yolo|-y                Auto-approve all tool calls\n"
      << "  --silent|-s              Suppress all output except print and "
         "completion\n"
      << "  --debug                  Log raw LLM responses to stderr\n"
      << "  --version, -v            Print version and build info\n"
      << "  --help, -h               Show this help\n";
}

int main(int argc, char *argv[]) {
  std::string config_path;
  std::string cli_model;
  std::string cli_endpoint;
  std::string cli_prompt;
  bool cli_debug = false;
  bool cli_yolo = false;
  bool cli_silent = false;
  AgentMode initial_mode = AgentMode::Plan;

  // Parse command-line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_path = argv[++i];
    } else if (arg == "--model" && i + 1 < argc) {
      cli_model = argv[++i];
    } else if (arg == "--endpoint" && i + 1 < argc) {
      cli_endpoint = argv[++i];
    } else if ((arg == "--mode" || arg == "-m") && i + 1 < argc) {
      std::string mode_str = argv[++i];
      if (mode_str == "act") {
        initial_mode = AgentMode::Act;
      }
      // Plan is already the default
    } else if ((arg == "--prompt" || arg == "-p") && i + 1 < argc) {
      cli_prompt = argv[++i];
    } else if (arg == "--yolo" || arg == "-y") {
      cli_yolo = true;
    } else if (arg == "--silent" || arg == "-s") {
      cli_silent = true;
    } else if (arg == "--debug") {
      cli_debug = true;
    } else if (arg == "--version" || arg == "-v") {
      print_version();
      return 0;
    } else if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    } else {
      std::cerr << "warning: unknown option '" << arg << "' (ignored)\n";
    }
  }

  // Load config file (if exists)
  auto loaded_config = Config::load(config_path);
  Config::apply_env_overrides(loaded_config);

  // CLI arguments take highest priority for model and endpoint
  if (!cli_model.empty()) {
    loaded_config.model = cli_model;
  }
  if (!cli_endpoint.empty()) {
    loaded_config.endpoint = cli_endpoint;
  }
  if (cli_debug) {
    loaded_config.debug = true;
  }
  if (cli_silent) {
    loaded_config.silent = true;
  }
  if (cli_yolo) {
    loaded_config.permissions.auto_approve_read = true;
    loaded_config.permissions.auto_approve_write = true;
    loaded_config.permissions.auto_approve_delete = true;
    loaded_config.permissions.auto_approve_shell = true;
  }
  // Register signal handlers: SIGINT cancels the current task
  // (interrupt-to-prompt), SIGTERM exits cleanly. SA_INTERRUPT ensures SIGINT
  // breaks blocking syscalls.
  struct sigaction sa_int{};
  sa_int.sa_handler = sigint_handler;
  sigemptyset(&sa_int.sa_mask);
  sa_int.sa_flags = SA_INTERRUPT; // do NOT restart interrupted syscalls
  sigaction(SIGINT, &sa_int, nullptr);

  struct sigaction sa_term{};
  sa_term.sa_handler = sigterm_handler;
  sigemptyset(&sa_term.sa_mask);
  sa_term.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &sa_term, nullptr);
  signal(SIGPIPE, SIG_IGN); // writes to closed pipes return EPIPE instead of
                            // killing process

  if (loaded_config.api_key.empty()) {
    std::cerr << "[warning] No API key set — requests will likely fail (set "
                 "CCL_API_KEY or api_key in config)\n";
  }

  // Run agent
  Ui ui(loaded_config.silent);
  Agent agent(loaded_config, ui, initial_mode);
  return agent.run(cli_prompt);
}
