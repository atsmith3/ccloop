#include "ui.h"
#include "agent.h"
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
    std::cout << "[call] " << call.name;

    size_t arg_count = 0;
    for (const auto& [key, val] : call.args) {
        if (arg_count >= 3) { std::cout << " ..."; break; }
        std::string val_str = val.is_string() ? val.as_string() : to_json(val);
        std::cout << " " << key << "=" << (key == "command" ? val_str : truncate(val_str, 40));
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

void Ui::show_usage(const LlmResponse::Usage& usage, size_t ctx_used, size_t ctx_limit) {
    std::cout << "[tokens] in: " << usage.prompt_tokens
              << " | out: " << usage.completion_tokens;
    if (usage.cache_read_tokens > 0)
        std::cout << " | cache_rd: " << usage.cache_read_tokens;
    if (usage.cache_write_tokens > 0)
        std::cout << " | cache_wr: " << usage.cache_write_tokens;
    std::cout << " | ctx: " << ctx_used << "/" << ctx_limit << "\n";
    std::cout.flush();
}

void Ui::show_error(std::string_view msg) {
    std::cerr << "[error] " << msg << "\n";
    std::cerr.flush();
}

Approval Ui::request_approval(const ToolCall& call) {
    std::cout << "Approve " << call.name;
    size_t arg_count = 0;
    for (const auto& [key, val] : call.args) {
        if (arg_count++ >= 3) { std::cout << " ..."; break; }
        std::string val_str = val.is_string() ? val.as_string() : to_json(val);
        std::cout << " " << key << "=" << (key == "command" ? val_str : truncate(val_str, 60));
    }
    std::cout << "? [y/n]: ";
    std::cout.flush();

    while (true) {
        if (should_interrupt.load()) {
            std::cout << "\n";
            return Approval::Reject;
        }
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cin.clear();
            std::cout << "\n";
            return Approval::Reject;  // EOF or EINTR
        }
        // Trim \r (Windows-style line endings)
        while (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) {
            if (should_interrupt.load()) return Approval::Reject;
            std::cout << "[y/n]: ";
            std::cout.flush();
            continue;
        }
        if (line[0] == 'y' || line[0] == 'Y') return Approval::Accept;
        if (line[0] == 'n' || line[0] == 'N') return Approval::Reject;

        std::cout << "[y/n]: ";
        std::cout.flush();
    }
}

std::string Ui::wait_for_input() {
    std::cout << "> ";
    std::cout.flush();

    std::string line;
    if (!std::getline(std::cin, line)) {
        std::cin.clear();  // reset failbit set by EINTR or EOF
        return "";
    }

    // Trim trailing whitespace
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.pop_back();
    }

    return line;
}
