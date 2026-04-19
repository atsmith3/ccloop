#include "ui.h"
#include <iostream>

void Ui::show_message(std::string_view role, std::string_view content) {
    std::cout << "[" << role << "] " << content << "\n\n";
    std::cout.flush();
}

void Ui::show_tool_call(const ToolCall& call, ToolSource source) {
    std::cout << "[call] " << call.name;

    // Show first few args (truncate if too long)
    size_t arg_count = 0;
    for (const auto& [key, val] : call.args) {
        if (arg_count >= 3) {
            std::cout << " ...";
            break;
        }
        std::cout << " " << key << "=...";
        ++arg_count;
    }

    std::cout << " (" << (source == ToolSource::Local ? "local" : "mcp") << ")\n";
    std::cout.flush();
}

void Ui::show_tool_result(const ToolCall& call, const ToolResult& result) {
    std::string status = result.success ? "OK" : "ERROR";

    if (result.success) {
        size_t content_len = result.content.length();
        std::cout << "[result] " << status << " - " << content_len << " bytes\n";
    } else {
        std::cout << "[result] " << status << ": " << result.error << "\n";
    }
    std::cout.flush();
}

void Ui::show_mode(AgentMode mode) {
    std::string mode_str;
    switch (mode) {
        case AgentMode::Explore:
            mode_str = "explore";
            break;
        case AgentMode::Plan:
            mode_str = "plan";
            break;
        case AgentMode::Act:
            mode_str = "act";
            break;
    }
    std::cout << "[ccl] Mode: " << mode_str << " | tokens: 0/8000\n";
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
    std::cout << "Approve " << call.name << "? [y/n]: ";
    std::cout.flush();

    std::string line;
    if (std::getline(std::cin, line)) {
        if (!line.empty() && (line[0] == 'y' || line[0] == 'Y')) {
            return Approval::Accept;
        }
    }
    return Approval::Reject;
}

void Ui::append_chunk(std::string_view chunk) {
    std::cout << chunk;
    std::cout.flush();
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
