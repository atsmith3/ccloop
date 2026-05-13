#include "ui.h"
#include "signals.h"
#include "json.h"
#include <iostream>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

void Ui::show_message(std::string_view role, std::string_view content) {
    if (role == "system") return;
    if (silent_ && role != "completed") return;
    std::cout << "[" << role << "] " << content << "\n\n";
    std::cout.flush();
}

void Ui::print_output(std::string_view message) {
    std::cout << message << "\n";
    std::cout.flush();
}

// Truncate a string to max_len, appending "..." if cut
static std::string truncate(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "...";
}

void Ui::show_tool_call(const ToolCall& call, ToolSource source) {
    if (silent_) return;
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
    if (silent_) return;
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
    if (silent_) return;
    std::string mode_str = (mode == AgentMode::Plan) ? "plan" : "act";
    std::cout << "[ccl] Mode: " << mode_str
              << " | tokens: " << tokens_used << "/" << token_limit << "\n";
    std::cout.flush();
}

void Ui::update_tokens(size_t used, size_t limit) {
    if (silent_) return;
    std::cout << "tokens: " << used << "/" << limit << "\n";
    std::cout.flush();
}

void Ui::show_usage(const LlmResponse::Usage& usage, size_t ctx_used, size_t ctx_limit) {
    if (silent_) return;
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

void Ui::show_plan(const std::string& plan) {
    std::cout << "\n=== PLAN ===\n" << plan << "\n============\n\n";
    std::cout.flush();
}

void Ui::show_completion(const std::string& summary) {
    show_message("completed", summary);
}

PlanApproval Ui::request_plan_approval(std::string& refinement_out) {
    std::cout << "Accept plan? [Y]es / [R]efine / [N]o: ";
    std::cout.flush();

    while (true) {
        if (should_interrupt.load()) {
            std::cout << "\n";
            return PlanApproval::Reject;
        }
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cin.clear();
            std::cout << "\n";
            return PlanApproval::Reject;
        }
        while (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) {
            if (should_interrupt.load()) return PlanApproval::Reject;
            std::cout << "[Y]es / [R]efine / [N]o: ";
            std::cout.flush();
            continue;
        }

        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));
        if (c == 'y' || line == "yes") return PlanApproval::Accept;
        if (c == 'n' || line == "no")  return PlanApproval::Reject;
        if (c == 'r' || line == "refine") {
            std::cout << "Refinement: ";
            std::cout.flush();
            if (!std::getline(std::cin, refinement_out)) {
                std::cin.clear();
                refinement_out.clear();
            }
            while (!refinement_out.empty() && refinement_out.back() == '\r')
                refinement_out.pop_back();
            return PlanApproval::Refine;
        }

        std::cout << "[Y]es / [R]efine / [N]o: ";
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

std::string Ui::ask_user(const std::string& question, const std::vector<std::string>& options) {
    std::cout << "\n[Question] " << question << "\n";

    if (!options.empty()) {
        for (size_t i = 0; i < options.size(); ++i)
            std::cout << "  " << (i + 1) << ". " << options[i] << "\n";
        size_t custom_idx = options.size() + 1;
        std::cout << "  " << custom_idx << ". Custom response\n";

        while (true) {
            std::cout << "Choice [1-" << custom_idx << "]: ";
            std::cout.flush();
            std::string line;
            if (!std::getline(std::cin, line)) { std::cin.clear(); break; }
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
                line.pop_back();
            try {
                size_t idx = std::stoul(line);
                if (idx >= 1 && idx < custom_idx) return options[idx - 1];
                if (idx == custom_idx) break;
            } catch (...) {}
            std::cout << "Enter a number between 1 and " << custom_idx << ".\n";
        }
    }

    std::cout << "> ";
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) { std::cin.clear(); return ""; }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
        line.pop_back();
    return line;
}

std::string Ui::open_editor(const std::string& configured_editor) {
    char tmp_path[] = "/tmp/ccl_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd == -1) return "";
    close(fd);

    const char* editor = configured_editor.empty() ? nullptr : configured_editor.c_str();
    if (!editor || !*editor) editor = std::getenv("VISUAL");
    if (!editor || !*editor) editor = std::getenv("EDITOR");
    if (!editor || !*editor) editor = "nano";

    std::string cmd = std::string(editor) + " " + tmp_path;
    int r = system(cmd.c_str());
    (void)r;

    std::ifstream f(tmp_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    std::remove(tmp_path);

    while (!content.empty() && (content.back() == '\n' || content.back() == '\r' ||
                                 content.back() == ' '  || content.back() == '\t')) {
        content.pop_back();
    }
    return content;
}
