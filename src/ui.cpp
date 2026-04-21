#include "ui.h"
#include "json.h"
#include <iostream>

void Ui::show_message(std::string_view role, std::string_view content) {
    std::cout << "[" << role << "] " << content << "\n\n";
    std::cout.flush();
}

// Truncate a string to max_len, appending "..." if cut
static std::string truncate(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "...";
}

void Ui::show_tool_call(const ToolCall& call, ToolSource source) {
    if (call.name == "run_shell") {
        auto it = call.args.find("command");
        std::string cmd = (it != call.args.end()) ? it->second.as_string() : "";
        std::cout << "[call] run_shell"
                  << " (" << (source == ToolSource::Local ? "local" : "mcp") << ")\n"
                  << "  $ " << cmd << "\n";
        std::cout.flush();
        return;
    }

    std::cout << "[call] " << call.name;

    size_t arg_count = 0;
    for (const auto& [key, val] : call.args) {
        if (arg_count >= 3) { std::cout << " ..."; break; }
        std::string val_str = val.is_string() ? val.as_string() : to_json(val);
        std::cout << " " << key << "=" << truncate(val_str, 40);
        ++arg_count;
    }

    std::cout << " (" << (source == ToolSource::Local ? "local" : "mcp") << ")\n";
    std::cout.flush();
}

void Ui::show_tool_result(const ToolCall& /*call*/, const ToolResult& result) {
    std::string status = result.success ? "OK" : "ERROR";

    if (result.success) {
        size_t content_len = result.content.length();
        std::cout << "[result] " << status << " - " << content_len << " bytes\n";
    } else {
        std::cout << "[result] " << status << ": " << result.error << "\n";
    }
    std::cout.flush();
}

void Ui::show_mode(AgentMode mode, size_t tokens_used, size_t token_limit) {
    std::string mode_str = (mode == AgentMode::Plan) ? "plan" : "act";
    std::cout << "[ccl] Mode: " << mode_str
              << " | tokens: " << tokens_used << "/" << token_limit << "\n";
    std::cout.flush();
}

void Ui::update_tokens(size_t used, size_t limit) {
    std::cout << "tokens: " << used << "/" << limit << "\n";
    std::cout.flush();
}

void Ui::show_error(std::string_view msg) {
    std::cerr << "[error] " << msg << "\n";
    std::cerr.flush();
}

Approval Ui::request_approval(const ToolCall& call) {
    if (call.name == "run_shell") {
        auto it = call.args.find("command");
        std::string cmd = (it != call.args.end()) ? it->second.as_string() : "";
        std::cout << "Approve run_shell?\n"
                  << "  $ " << cmd << "\n"
                  << "[y/n]: ";
        std::cout.flush();
    } else {
        std::cout << "Approve " << call.name;
        size_t arg_count = 0;
        for (const auto& [key, val] : call.args) {
            if (arg_count++ >= 3) { std::cout << " ..."; break; }
            std::string val_str = val.is_string() ? val.as_string() : to_json(val);
            std::cout << " " << key << "=" << truncate(val_str, 60);
        }
        std::cout << "? [y/n]: ";
        std::cout.flush();
    }

    std::string line;
    if (std::getline(std::cin, line)) {
        if (!line.empty() && (line[0] == 'y' || line[0] == 'Y')) {
            return Approval::Accept;
        }
    }
    return Approval::Reject;
}

std::string Ui::wait_for_input() {
    std::cout << "> ";
    std::cout.flush();

    std::string line;
    std::getline(std::cin, line);

    // Trim trailing whitespace
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.pop_back();
    }

    return line;
}
