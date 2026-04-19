#include "agent.h"
#include <iostream>
#include <sstream>
#include <atomic>
#include <set>

Agent::Agent(Config config, Ui& ui)
    : config_(config), context_(config.token_limit), llm_(config),
      registry_(make_registry(mode_, config)), ui_(ui) {}

void Agent::run() {
    context_.push_system(system_prompt());
    ui_.show_mode(mode_);
    ui_.update_tokens(context_.total_tokens(), config_.token_limit);
    loop();
}

std::string Agent::system_prompt() const {
    switch (mode_) {
        case AgentMode::Explore:
            return "You are an expert coding assistant. You can read and analyze code, answer "
                   "questions, and explore codebases. You have read-only file access tools. "
                   "Answer questions thoroughly and help developers understand their code.";
        case AgentMode::Plan:
            return "You are a software architect. Use read-only tools to understand the codebase, "
                   "then produce a numbered implementation plan. Do not write files or make changes. "
                   "Focus on understanding the existing code structure and proposing clear, "
                   "actionable steps.";
        case AgentMode::Act:
            return "You are an expert software engineer. Execute the plan step by step using "
                   "available tools. Report progress and any issues. Be careful and precise "
                   "with your operations.";
    }
    return "";
}

void Agent::loop() {
    while (!should_exit.load()) {
        std::string input = ui_.wait_for_input();

        if (handle_slash_command(input)) {
            continue;
        }

        if (input.empty()) {
            continue;
        }

        context_.push_user(input);

        // Main interaction loop: keep calling LLM until we get text response
        while (!should_exit.load()) {
            if (context_.needs_compaction()) {
                context_.compact();
            }

            LlmResponse response;
            if (config_.streaming) {
                // Use streaming
                response = llm_.complete_streaming(context_, registry_.definitions(),
                    [&](std::string_view chunk) { ui_.append_chunk(chunk); });
                // Add newline after streamed content
                if (!response.content.empty()) {
                    std::cout << "\n\n";
                }
            } else {
                // Use blocking call
                response = llm_.complete(context_, registry_.definitions());
            }

            context_.sync_token_count(response.usage);

            // Convert tool calls to records for storage
            std::vector<ToolCallRecord> tool_records;
            for (const auto& call : response.tool_calls) {
                ToolCallRecord rec;
                rec.id = call.id;
                rec.name = call.name;
                // Serialize args back to JSON string
                std::ostringstream args_ss;
                args_ss << "{";
                bool first = true;
                for (const auto& [key, val] : call.args) {
                    if (!first) args_ss << ",";
                    args_ss << escape_json(key) << ":";
                    args_ss << to_json(val);
                    first = false;
                }
                args_ss << "}";
                rec.arguments_json = args_ss.str();
                tool_records.push_back(rec);
            }

            context_.push_assistant(response.content, tool_records);

            // If there are tool calls, execute them
            if (!response.tool_calls.empty()) {
                handle_tool_calls(response.tool_calls);
                // Loop back to call LLM again
                continue;
            }

            // No tool calls and we have text response
            if (!config_.streaming) {
                // For non-streaming, show the message (streaming already printed it)
                ui_.show_message("agent", response.content);
            }
            ui_.update_tokens(context_.total_tokens(), config_.token_limit);
            break;  // Exit inner loop, get next user input
        }
    }
}

bool Agent::requires_approval(const std::string& tool_name) const {
    static const std::set<std::string> always_gated{"run_shell", "delete_file"};
    return always_gated.count(tool_name) > 0;
}

void Agent::handle_tool_calls(const std::vector<ToolCall>& calls) {
    for (const auto& call : calls) {
        ui_.show_tool_call(call, ToolSource::Local);

        // Check approval gate
        if (requires_approval(call.name)) {
            Approval approval = ui_.request_approval(call);
            if (approval != Approval::Accept) {
                ToolResult rejected = ToolResult::fail("rejected by user");
                ui_.show_tool_result(call, rejected);
                context_.push_tool_result(call.id, rejected);
                continue;
            }
        }

        // Find and execute tool
        auto tool_opt = registry_.find(call.name);
        if (!tool_opt.has_value()) {
            ToolResult result = ToolResult::fail("Tool not found: " + call.name);
            ui_.show_tool_result(call, result);
            context_.push_tool_result(call.id, result);
            continue;
        }

        const Tool* tool = tool_opt.value();

        // Execute tool
        ToolResult result = tool->fn(call.args);
        ui_.show_tool_result(call, result);
        context_.push_tool_result(call.id, result);
    }
}

bool Agent::handle_slash_command(std::string_view input) {
    if (input.empty() || input[0] != '/') {
        return false;
    }

    std::string cmd(input);
    size_t space_pos = cmd.find(' ');
    std::string command = (space_pos == std::string::npos)
        ? cmd.substr(1)
        : cmd.substr(1, space_pos - 1);
    std::string arg = (space_pos == std::string::npos)
        ? ""
        : cmd.substr(space_pos + 1);

    // Trim arg
    while (!arg.empty() && arg[0] == ' ') arg = arg.substr(1);
    while (!arg.empty() && arg.back() == ' ') arg.pop_back();

    if (command == "mode") {
        if (arg == "explore") {
            transition_to(AgentMode::Explore);
        } else if (arg == "plan") {
            transition_to(AgentMode::Plan);
        } else if (arg == "act") {
            transition_to(AgentMode::Act);
        }
        return true;
    }

    if (command == "quit") {
        should_exit = true;
        return true;
    }

    if (command == "help") {
        std::cout << "Slash commands:\n"
                  << "  /mode explore|plan|act - switch modes\n"
                  << "  /quit - exit\n"
                  << "  /clear - clear context\n"
                  << "  /help - show this help\n";
        return true;
    }

    if (command == "clear") {
        // Clear all but system message
        while (context_.message_count() > 1) {
            // Rebuild context with just system
            // For simplicity, recreate
            ContextManager new_ctx(config_.token_limit);
            new_ctx.push_system(system_prompt());
            context_ = new_ctx;
            break;
        }
        return true;
    }

    return false;
}

void Agent::transition_to(AgentMode next) {
    if (next == mode_) return;

    mode_ = next;
    rebuild_registry();
    ui_.show_mode(mode_);

    // Update system message
    ContextManager new_ctx(config_.token_limit);
    new_ctx.push_system(system_prompt());
    // TODO: preserve other messages if needed
    context_ = new_ctx;
}

void Agent::rebuild_registry() {
    registry_ = make_registry(mode_, config_);
}
