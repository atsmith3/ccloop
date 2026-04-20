#include "agent.h"
#include <iostream>
#include <sstream>
#include <atomic>
#include <set>

Agent::Agent(Config config, Ui& ui, AgentMode initial_mode)
    : config_(config), mode_(initial_mode), context_(config.token_limit), llm_(config),
      registry_(make_registry(initial_mode, config)), ui_(ui) {}

void Agent::run() {
    context_.push_system(system_prompt());
    ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
    ui_.update_tokens(context_.total_tokens(), config_.token_limit);
    loop();
}

std::string Agent::system_prompt() const {
    switch (mode_) {
        case AgentMode::Plan:
            return
                "You are an expert software architect helping a developer understand and plan "
                "changes to their codebase.\n\n"
                "Before answering any question, always use tools to gather information:\n"
                "- Use list_dir to explore directory structure\n"
                "- Use read_file to read relevant source files\n"
                "- Use search_files to find patterns, definitions, or usages across the codebase\n\n"
                "Never guess at file contents or project structure — always read first. "
                "If you are unsure which files are relevant, keep exploring until you are confident.\n\n"
                "When producing an implementation plan:\n"
                "1. Explore the codebase thoroughly to understand existing patterns and conventions\n"
                "2. Identify every file that will need to change\n"
                "3. Write a clear, numbered plan with specific file paths and concrete changes\n"
                "4. Call out risks, dependencies, or prerequisites\n\n"
                "Keep plans grounded in what you actually read — do not invent structure or APIs "
                "that you have not confirmed exist. Do not write or modify any files.";
        case AgentMode::Act:
            return
                "You are an expert software engineer executing an implementation plan.\n\n"
                "Work methodically through each step:\n"
                "1. Read the plan from the conversation context\n"
                "2. Before modifying a file, read its current contents first\n"
                "3. Apply changes using write_file\n"
                "4. After writing, read the file back to verify the change was applied correctly\n"
                "5. Report what was completed and what comes next\n\n"
                "When writing code:\n"
                "- Match the style and conventions of the existing codebase\n"
                "- Prefer editing existing files over creating new ones\n"
                "- Do not make changes beyond what the plan requires\n\n"
                "If you encounter an error or unexpected situation, explain what happened and "
                "attempt to resolve it autonomously before asking for help. "
                "Ask before taking any destructive or irreversible action.";
    }
    return "";
}

void Agent::loop() {
    while (!should_exit.load()) {
        std::string input;
        bool is_synthetic = false;

        if (!pending_execution_.empty()) {
            input = std::move(pending_execution_);
            pending_execution_.clear();
            is_synthetic = true;  // fix #11: skip slash command handling for synthetic input
        } else {
            input = ui_.wait_for_input();
        }

        if (!is_synthetic && handle_slash_command(input)) {
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
                if (!response.content.empty()) {
                    std::cout << "\n\n";
                }
            } else {
                // Use blocking call
                response = llm_.complete(context_, registry_.definitions());
            }

            if (response.is_error) {
                // Error response: show to user but don't add to context or sync tokens
                ui_.show_error("[ERROR] " + response.content);
                ui_.update_tokens(context_.total_tokens(), config_.token_limit);
                break;  // Exit inner loop, wait for next user input
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
                    args_ss << "\"" << escape_json(key) << "\":";
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
        if (arg == "plan") {
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
                  << "  /mode plan|act - switch modes\n"
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

    AgentMode prev = mode_;
    mode_ = next;
    rebuild_registry();
    context_.replace_system(system_prompt());
    ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);

    // Auto-execute when switching Plan → Act
    if (prev == AgentMode::Plan && next == AgentMode::Act) {
        pending_execution_ = "Now execute the plan step by step using the available tools.";
    }
}

void Agent::rebuild_registry() {
    registry_ = make_registry(mode_, config_);
}
