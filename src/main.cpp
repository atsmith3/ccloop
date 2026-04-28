#include <csignal>
#include <atomic>
#include <iostream>
#include "config.h"
#include "agent.h"
#include "ui.h"

static void sigint_handler(int) {
    should_interrupt = true;
}

static void sigterm_handler(int) {
    should_exit = true;
}

static void print_help() {
    std::cout << "ccl — C++ Coding Loop\n\n"
              << "Usage: ccl [OPTIONS]\n\n"
              << "Options:\n"
              << "  --config <path>          Config file path\n"
              << "  --model <name>           LLM model (overrides config)\n"
              << "  --endpoint <url>         API endpoint (overrides config)\n"
              << "  --mode|-m plan|act       Start in specified mode\n"
              << "  --prompt|-p <text>       Run one turn non-interactively then exit\n"
              << "  --yolo|-y                Auto-approve all tool calls\n"
              << "  --debug                  Log raw LLM responses to stderr\n"
              << "  --help, -h               Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;
    std::string cli_model;
    std::string cli_endpoint;
    std::string cli_prompt;
    bool cli_debug = false;
    bool cli_yolo  = false;
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
        } else if (arg == "--debug") {
            cli_debug = true;
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
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
    if (cli_yolo) {
        loaded_config.permissions.auto_approve_read   = true;
        loaded_config.permissions.auto_approve_write  = true;
        loaded_config.permissions.auto_approve_delete = true;
        loaded_config.permissions.auto_approve_shell  = true;
    }
    // Register signal handlers: SIGINT cancels the current task (interrupt-to-prompt),
    // SIGTERM exits cleanly. SA_INTERRUPT ensures SIGINT breaks blocking syscalls.
    struct sigaction sa_int{};
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_INTERRUPT;  // do NOT restart interrupted syscalls
    sigaction(SIGINT, &sa_int, nullptr);

    struct sigaction sa_term{};
    sa_term.sa_handler = sigterm_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa_term, nullptr);

    // Run agent
    Ui ui;
    Agent agent(loaded_config, ui, initial_mode);
    return agent.run(cli_prompt);
}
