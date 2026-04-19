#include <csignal>
#include <atomic>
#include <iostream>
#include "config.h"
#include "agent.h"
#include "ui.h"

std::atomic<bool> should_exit{false};

static void signal_handler(int) {
    should_exit = true;
}

static void print_help() {
    std::cout << "ccl — C++ Coding Loop\n\n"
              << "Usage: ccl [OPTIONS]\n\n"
              << "Options:\n"
              << "  --config <path>      Config file path\n"
              << "  --model <name>       LLM model (overrides config)\n"
              << "  --endpoint <url>     API endpoint (overrides config)\n"
              << "  --mode explore|plan|act  Start in specified mode\n"
              << "  --no-stream          Disable streaming\n"
              << "  --help, -h           Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string config_path;
    std::string cli_model;
    std::string cli_endpoint;
    bool cli_no_stream = false;
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
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "act") {
                initial_mode = AgentMode::Act;
            }
            // Plan is already the default
        } else if (arg == "--no-stream") {
            cli_no_stream = true;
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
    if (cli_no_stream) {
        loaded_config.streaming = false;
    }

    // Register signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Run agent
    Ui ui;
    Agent agent(loaded_config, ui, initial_mode);
    agent.run();

    return 0;
}
