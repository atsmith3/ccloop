#include "agent.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <set>

static std::string build_tools_prompt(const std::vector<ToolDef>& tools) {
    std::ostringstream ss;
    ss << "## Tools\n\n"
       << "To use a tool, output ONLY the XML block — no surrounding text. "
       << "One tool per response. Wait for the result before continuing.\n";

    for (const auto& tool : tools) {
        ss << "\n<" << tool.name << ">\n";
        for (const auto& p : tool.params) {
            ss << "<" << p.name << ">"
               << p.description
               << " (" << p.type << ", " << (p.required ? "required" : "optional") << ")"
               << "</" << p.name << ">\n";
        }
        ss << "</" << tool.name << ">\n"
           << tool.description << "\n";
    }
    return ss.str();
}

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
    std::string tools_section = "\n\n" + build_tools_prompt(registry_.definitions());

    switch (mode_) {
        case AgentMode::Plan:
            return
                "You are an expert software architect helping a developer understand and plan "
                "changes to their codebase.\n\n"
                "Before answering any question, always use tools to gather information:\n"
                "- Use list_dir to explore directory structure\n"
                "- Use read_file to read relevant source files\n"
                "- Use search_files to find patterns, definitions, or usages across the codebase\n"
                "- Use run_shell to run build checks, tests, or other read-only commands\n\n"
                "Never guess at file contents or project structure — always read first. "
                "If you are unsure which files are relevant, keep exploring until you are confident.\n\n"
                "When producing an implementation plan:\n"
                "1. Explore the codebase thoroughly to understand existing patterns and conventions\n"
                "2. Identify every file that will need to change\n"
                "3. Write a clear numbered checklist using this exact format:\n\n"
                "   ## Plan: <short-descriptive-slug>\n"
                "   1. [ ] [action] — [file or target]\n"
                "   2. [ ] [action] — [file or target]\n"
                "   ...\n\n"
                "   The slug must be kebab-case, 2-4 words (e.g. add-lcs-algorithm). "
                "It will become the plan filename.\n"
                "4. Call out risks, dependencies, or prerequisites\n\n"
                "Keep plans grounded in what you actually read — do not invent structure or APIs "
                "that you have not confirmed exist. Do not write or modify any files."
                + tools_section;
        case AgentMode::Act:
            return
                "You are an expert software engineer executing tasks precisely and methodically.\n\n"
                "## Phase 1 — Identify or create the plan\n\n"
                "If a numbered plan exists in the conversation context, use it.\n"
                "If no plan exists, write one now (task steps only — do not include saving the plan):\n\n"
                "  ## Plan: <short-descriptive-slug>\n"
                "  1. [ ] task step one\n"
                "  2. [ ] task step two\n"
                "  ...\n\n"
                "## Phase 2 — Save the plan (setup, NOT a task step)\n\n"
                "Silently persist the plan before executing:\n"
                "  create_dir ~/.ccl/plans\n"
                "  write_file ~/.ccl/plans/<slug>.md  (task steps only, no meta-steps)\n"
                "Announce once: \"Plan saved to ~/.ccl/plans/<slug>.md — beginning execution.\"\n\n"
                "## Phase 3 — Execute every task step without pausing\n\n"
                "For each numbered task step:\n"
                "- Announce: \"## Step N: [description]\"\n"
                "- Read any file before modifying it\n"
                "- Apply changes with write_file or edit_file\n"
                "- Read the file back to verify\n"
                "- Confirm: \"\\u2713 Step N complete\"\n"
                "Do NOT pause between steps to ask permission. Complete all steps in one pass.\n\n"
                "## Phase 4 — Completion report (always, without being asked)\n\n"
                "  ## Completed\n"
                "  **Task**: [one-line summary]\n"
                "  **Plan**: ~/.ccl/plans/<slug>.md\n"
                "  **Created**: [new files, or \"none\"]\n"
                "  **Modified**: [changed files, or \"none\"]\n"
                "  **Steps**: N of N completed\n\n"
                "Match existing code style. Stay within plan scope. "
                "Explain errors before asking for help. Ask before destructive actions."
                + tools_section;
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

            LlmResponse response = llm_.complete(context_, registry_.definitions());

            if (response.is_error) {
                // Error response: show to user but don't add to context or sync tokens
                ui_.show_error("[ERROR] " + response.content);
                ui_.update_tokens(context_.total_tokens(), config_.token_limit);
                break;  // Exit inner loop, wait for next user input
            }

            context_.sync_token_count(response.usage);

            // Drop tool calls for unrecognized tool names (e.g. bare <path> tags)
            {
                auto& tc = response.tool_calls;
                tc.erase(std::remove_if(tc.begin(), tc.end(),
                    [&](const ToolCall& c) { return !registry_.find(c.name).has_value(); }),
                    tc.end());
            }

            if (!response.tool_calls.empty()) {
                // Assistant called tools — push message and execute them
                context_.push_assistant(response.content);
                handle_tool_calls(response.tool_calls);
                continue;
            } else if (!response.content.empty()) {
                // Normal text response — push and show
                context_.push_assistant(response.content);
                ui_.show_message("agent", response.content);
            } else {
                // Empty content, no tool calls — don't pollute context
                ui_.show_error("[warning] empty response from model");
            }
            ui_.update_tokens(context_.total_tokens(), config_.token_limit);
            break;  // Exit inner loop, get next user input
        }
    }
}

bool Agent::requires_approval(const std::string& tool_name) const {
    static const std::set<std::string> read_tools  = {"read_file", "list_dir", "search_files", "file_info"};
    static const std::set<std::string> write_tools = {"write_file", "edit_file", "create_dir"};

    if (read_tools.count(tool_name))  return !config_.permissions.auto_approve_read;
    if (write_tools.count(tool_name)) return !config_.permissions.auto_approve_write;
    if (tool_name == "delete_file")   return !config_.permissions.auto_approve_delete;
    if (tool_name == "run_shell")     return !config_.permissions.auto_approve_shell;
    return true;  // unknown tool: gate by default
}

void Agent::handle_tool_calls(const std::vector<ToolCall>& calls) {
    std::string combined;

    for (const auto& call : calls) {
        ui_.show_tool_call(call, ToolSource::Local);

        ToolResult result = [&]() -> ToolResult {
            // Check approval gate
            if (requires_approval(call.name)) {
                Approval approval = ui_.request_approval(call);
                if (approval != Approval::Accept) {
                    return ToolResult::fail("rejected by user");
                }
            }

            // Find and execute tool
            auto tool_opt = registry_.find(call.name);
            if (!tool_opt.has_value()) {
                return ToolResult::fail("Tool not found: " + call.name);
            }
            return tool_opt.value()->fn(call.args);
        }();

        ui_.show_tool_result(call, result);

        // Accumulate results into a single message
        if (!combined.empty()) combined += "\n\n";
        combined += "[Tool: " + call.name + "]\n" + result.to_context_string();
    }

    if (!combined.empty()) {
        context_.push_user(combined);
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
        pending_execution_ =
            "A plan was just produced above. "
            "Save it to ~/.ccl/plans/<slug>.md as setup (not a task step), "
            "then immediately execute every task step in the plan without pausing. "
            "Announce each step, confirm when done, "
            "and output a completion report when all task steps are finished.";
    }
}

void Agent::rebuild_registry() {
    registry_ = make_registry(mode_, config_);
}
