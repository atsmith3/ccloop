#include "agent.h"
#include <iostream>
#include <atomic>

static bool is_act_terminal(const std::string& content) {
    return content.find("## Completed") != std::string::npos
        || content.find("## Roadblock") != std::string::npos;
}


Agent::Agent(Config config, Ui& ui, AgentMode initial_mode)
    : config_(config), mode_(initial_mode),
      context_(config.token_limit, config.compaction_keep_recent), llm_(config),
      registry_(make_registry(initial_mode, config)), ui_(ui) {}

int Agent::run(const std::string& initial_prompt) {
    context_.push_system(system_prompt());
    ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
    ui_.update_tokens(context_.total_tokens(), config_.token_limit);
    if (!initial_prompt.empty()) {
        pending_execution_ = initial_prompt;
        non_interactive_ = true;
    }
    loop();
    return exit_code_;
}

std::string Agent::system_prompt() const {
    switch (mode_) {
        case AgentMode::Plan:
            return
                "You are an expert software architect helping a developer understand and plan "
                "changes to their codebase.\n\n"
                "## Keep this context lean — delegate exploration to sub-agents\n\n"
                "Avoid reading files or running shell commands directly in this agent except for "
                "quick single-item lookups. For anything broader, delegate to spawn_agent: the "
                "sub-agent runs in isolation and returns a compact summary you use here. Raw file "
                "contents should not accumulate in this context.\n\n"
                "Use spawn_agent for:\n"
                "- Reading more than one file, or any file over ~100 lines\n"
                "- Searching the codebase for patterns or usages\n"
                "- Running build/test commands and summarising the output\n"
                "- Any research question that might touch multiple files\n\n"
                "Examples:\n"
                "  spawn_agent(prompt='Read src/agent.cpp and explain how the agent loop works')\n"
                "  spawn_agent(prompt='Find all callers of compact() and report when it fires')\n"
                "  spawn_agent(prompt='List src/ and summarize what each module does')\n\n"
                "Important: do not call spawn_agent recursively. If you were invoked with a "
                "specific focused task, complete it directly using tools — do not spawn further agents.\n\n"
                "Never guess at file contents or project structure — always explore first. "
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
                "that you have not confirmed exist. Do not write or modify any files.";
        case AgentMode::Act:
            return
                "You are an expert software engineer executing tasks precisely and methodically.\n\n"
                "## Phase 1 — Identify or create the plan\n\n"
                "If a numbered plan exists in the conversation context, use it.\n"
                "If no plan exists, write one now:\n\n"
                "  ## Plan: <short-descriptive-slug>\n"
                "  1. [ ] task step one\n"
                "  2. [ ] task step two\n"
                "  ...\n\n"
                "## Phase 2 — Execute every task step without pausing\n\n"
                "For each numbered task step:\n"
                "- Announce: \"## Step N: [description]\"\n"
                "- Read any file before modifying it\n"
                "- Apply changes with write_file or edit_file\n"
                "- Read the file back to verify\n"
                "- Confirm: \"\\u2713 Step N complete\"\n"
                "Do NOT pause between steps to ask permission. Complete all steps in one pass.\n\n"
                "## Phase 3 — Completion report (always, without being asked)\n\n"
                "  ## Completed\n"
                "  **Task**: [one-line summary]\n"
                "  **Created**: [new files, or \"none\"]\n"
                "  **Modified**: [changed files, or \"none\"]\n"
                "  **Steps**: N of N completed\n\n"
                "## Keep this context lean — delegate heavy steps to sub-agents\n\n"
                "For any plan step that involves reading files, making edits, and verifying — "
                "spawn a sub-agent with the complete task description:\n\n"
                "  spawn_agent(prompt='In src/foo.cpp edit function bar to do X. "
                "Read the file first, apply the change with edit_file, verify by reading it back.')\n"
                "  spawn_agent(prompt='Run cmake --build build && ./build/ccl_test. Report pass/fail.')\n\n"
                "The sub-agent returns a summary of what it did. Record that, mark the step done, "
                "and move on. Do not re-read files the sub-agent already processed.\n\n"
                "Only work directly (read_file, edit_file, write_file) for genuinely trivial "
                "changes: a single targeted edit of < 5 lines where you already know exactly "
                "what to change.\n\n"
                "Important: do not call spawn_agent recursively. Complete the specific task you "
                "were given without spawning further agents.\n\n"
                "Match existing code style. Stay within plan scope.\n\n"
                "## Roadblock (use only when you cannot proceed without user input)\n\n"
                "  ## Roadblock: <one-line description of what is blocking you>\n\n"
                "Output this marker to pause execution and ask the user for guidance. "
                "Do not output it for recoverable errors — attempt fixes first.";
    }
    return "";
}

void Agent::compact_with_summary() {
    ui_.show_message("system", "Compacting context with summarization...");

    std::string conversation = context_.extract_conversation_for_summary();
    if (conversation.empty()) {
        context_.compact();
        return;
    }

    ContextManager summary_ctx(config_.token_limit * 4);
    summary_ctx.push_system(
        "You are a helpful assistant. Summarize the following conversation concisely but "
        "completely. Preserve: key decisions, files created or modified, code changes made, "
        "plan steps completed and any remaining steps, errors encountered and how they were "
        "resolved. Output only the summary with no preamble.");
    summary_ctx.push_user("Summarize this conversation:\n\n" + conversation);

    LlmResponse resp = llm_.complete(summary_ctx, {});
    if (resp.is_error || resp.content.empty()) {
        ui_.show_error("[compact] Summarization failed — falling back to rolling window");
        context_.compact();
    } else {
        context_.compact_to_summary(resp.content);
    }
    ui_.update_tokens(context_.total_tokens(), config_.token_limit);
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
            if (should_interrupt.load()) {
                should_interrupt = false;
                std::cout << "\n[Interrupted] Type /quit to exit.\n> ";
                std::cout.flush();
            }
            continue;
        }

        context_.push_user(input);

        // Main interaction loop: keep calling LLM until we get text response
        int act_steps = 0;
        constexpr int kActStepLimit = 25;
        bool turn_error = false;
        while (!should_exit.load() && !should_interrupt.load()) {
            if (context_.needs_compaction()) {
                compact_with_summary();
            }

            LlmResponse response = llm_.complete(context_, registry_.definitions());

            if (response.is_error) {
                // Suppress curl-abort errors that result from Ctrl+C interruption
                if (!should_interrupt.load()) {
                    ui_.show_error("[ERROR] " + response.content);
                    ui_.update_tokens(context_.total_tokens(), config_.token_limit);
                    turn_error = true;
                }
                break;  // Exit inner loop, wait for next user input
            }

            context_.sync_token_count(response.usage);
            ui_.show_usage(response.usage, context_.total_tokens(), config_.token_limit);

            if (!response.tool_calls.empty()) {
                // Assistant called tools — push message and execute them
                context_.push_assistant(response.content);
                handle_tool_calls(response.tool_calls);
                continue;
            } else if (!response.content.empty()) {
                // Normal text response — push and show
                context_.push_assistant(response.content);
                ui_.show_message("agent", response.content);
                // Act mode: auto-continue until the model signals completion or a roadblock
                if (mode_ == AgentMode::Act && !is_act_terminal(response.content)
                        && !should_interrupt.load()) {
                    if (++act_steps >= kActStepLimit) {
                        ui_.show_error("[warning] Act step limit reached — stopping");
                        break;
                    }
                    context_.push_user("Continue.");
                    ui_.update_tokens(context_.total_tokens(), config_.token_limit);
                    continue;
                }
            } else {
                // Empty content, no tool calls — don't pollute context
                ui_.show_error("[warning] empty response from model");
            }
            break;  // Exit inner loop, get next user input
        }

        // Handle Ctrl+C: reset flag and return to prompt
        if (should_interrupt.load()) {
            should_interrupt = false;
            std::cout << "\n[Interrupted] Task cancelled. Type /quit to exit.\n";
            std::cout.flush();
            continue;
        }

        if (non_interactive_) {
            if (turn_error) {
                exit_code_ = 1;
                return;
            }
            if (mode_ == AgentMode::Plan) {
                // Plan turn complete — auto-transition to act and execute
                transition_to(AgentMode::Act);
                continue;  // pending_execution_ is now set by transition_to()
            }
            return;  // Act turn complete — exit
        }
    }
}

bool Agent::requires_approval(const ToolDef& def) const {
    if (def.permission == "read")   return !config_.permissions.auto_approve_read;
    if (def.permission == "write")  return !config_.permissions.auto_approve_write;
    if (def.permission == "delete") return !config_.permissions.auto_approve_delete;
    if (def.permission == "shell")  return !config_.permissions.auto_approve_shell;
    return true;  // unknown permission: gate by default
}

void Agent::handle_tool_calls(const std::vector<ToolCall>& calls) {
    std::string combined;

    for (const auto& call : calls) {
        auto tool_opt = registry_.find(call.name);
        ToolSource src = (tool_opt.has_value()) ? tool_opt.value()->source : ToolSource::Local;
        ui_.show_tool_call(call, src);

        ToolResult result = [&]() -> ToolResult {
            if (!tool_opt.has_value()) {
                return ToolResult::fail("Tool not found: " + call.name);
            }

            if (requires_approval(tool_opt.value()->def)) {
                if (non_interactive_) {
                    return ToolResult::fail(
                        "tool '" + call.name + "' requires approval but running in "
                        "non-interactive mode — use -y to auto-approve");
                }
                Approval approval = ui_.request_approval(call);
                if (approval != Approval::Accept) {
                    return ToolResult::fail("rejected by user");
                }
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

    if (command == "compact") {
        compact_with_summary();
        return true;
    }

    if (command == "help") {
        std::cout << "Slash commands:\n"
                  << "  /mode plan|act - switch modes\n"
                  << "  /compact - summarize and compact the context window\n"
                  << "  /quit - exit\n"
                  << "  /clear - clear context\n"
                  << "  /help - show this help\n";
        return true;
    }

    if (command == "clear") {
        ContextManager new_ctx(config_.token_limit, config_.compaction_keep_recent);
        new_ctx.push_system(system_prompt());
        context_ = std::move(new_ctx);
        ui_.update_tokens(context_.total_tokens(), config_.token_limit);
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
            "Immediately execute every task step in the plan without pausing. "
            "Announce each step, confirm when done, "
            "and output a completion report when all task steps are finished.";
    }
}

void Agent::rebuild_registry() {
    registry_ = make_registry(mode_, config_);
}
