#include "agent.h"
#include <algorithm>
#include <iostream>
#include <atomic>
#include <future>

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
                "When independent research tasks can run simultaneously, call spawn_agent multiple\n"
                "times in a single response — they execute in parallel and all results return\n"
                "together before the next turn:\n\n"
                "  spawn_agent(prompt='Read src/agent.cpp and explain the agent loop')\n"
                "  spawn_agent(prompt='Read src/tools.cpp and list every tool and its permission level')\n"
                "  spawn_agent(prompt='Run cmake --build build_local 2>&1 | tail -5 and report errors')\n\n"
                "Important: do not call spawn_agent recursively. If you were invoked with a "
                "specific focused task, complete it directly using tools — do not spawn further agents.\n\n"
                "Never guess at file contents or project structure — always explore first. "
                "If you are unsure which files are relevant, keep exploring until you are confident.\n\n"
                "When producing an implementation plan:\n"
                "1. Explore the codebase thoroughly to understand existing patterns and conventions\n"
                "2. Identify every file that will need to change\n"
                "3. Call present_plan with a clear numbered checklist as the 'plan' argument:\n\n"
                "   present_plan(plan=\"## Plan: <slug>\\n1. [ ] action — file\\n2. [ ] action — file\\n...\")\n\n"
                "   The slug must be kebab-case, 2-4 words (e.g. add-lcs-algorithm).\n"
                "   The user will accept, refine, or reject. On refinement, revise and call present_plan again.\n"
                "4. Call out risks, dependencies, or prerequisites in the plan text\n\n"
                "For simple questions that do not require a plan, answer directly in text — no tool call needed.\n\n"
                "Use print(message=...) to surface key findings mid-exploration. Keep output minimal.\n\n"
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
                "Work through all steps continuously. A text-only response (no tool calls) signals\n"
                "that ALL work is complete — never emit one between steps.\n\n"
                "Rules:\n"
                "- While steps remain incomplete, always include a tool call — do not emit a\n"
                "  standalone acknowledgment between steps\n"
                "- Once all steps are verified done (files written, no pending changes), respond\n"
                "  with a text summary only — no trailing tool call\n"
                "- Read the file immediately before every edit_file call — old_str must come from\n"
                "  the most recent read, not from memory\n"
                "- Make one edit at a time; re-read before any subsequent edit to the same file\n"
                "- When one step finishes, proceed immediately to the next with a tool call — do\n"
                "  not emit a step-complete acknowledgment as a standalone message\n"
                "If edit_file returns \"old_str not found\", re-read the file, locate the correct\n"
                "text, and retry. Never abandon a step due to this error.\n\n"
                "## Phase 3 — Signal completion\n\n"
                "When all steps are done, respond with a text summary of what was accomplished.\n\n"
                "For conversational questions that don't require plan execution, answer in text.\n\n"
                "Use print(message=...) to surface important status or findings mid-execution.\n\n"
                "## Keep this context lean — delegate heavy steps to sub-agents\n\n"
                "For any plan step that involves reading files, making edits, and verifying — "
                "spawn a sub-agent with the complete task description:\n\n"
                "  spawn_agent(prompt='In src/foo.cpp edit function bar to do X. "
                "Read the file first, apply the change with edit_file, verify by reading it back.')\n"
                "  spawn_agent(prompt='Run cmake --build build && ./build/ccl_test. Report pass/fail.')\n\n"
                "When multiple plan steps are independent (different files, no ordering dependency),\n"
                "call spawn_agent for each in a single response — they run in parallel:\n\n"
                "  spawn_agent(prompt='Edit src/foo.cpp: change function X to do Y. Read first, edit, verify.')\n"
                "  spawn_agent(prompt='Edit src/bar.cpp: add function Z. Read first, write, verify.')\n"
                "  spawn_agent(prompt='Run cmake --build build_local && ./build_local/ccl_test. Report pass/fail.')\n\n"
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
        seen_calls_.clear();

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
                if (task_done_called_ || plan_accepted_ || plan_rejected_) break;
                if (++act_steps >= kActStepLimit) {
                    ui_.show_error("[warning] Act step limit reached — stopping");
                    break;
                }
                continue;
            } else if (!response.content.empty()) {
                // Text-only response — treat as implicit task_done.
                // Continuation is driven by tool calls only; text = done.
                context_.push_assistant(response.content);
                ui_.show_completion(response.content);
                task_done_called_ = true;
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
            plan_accepted_ = plan_rejected_ = task_done_called_ = false;
            continue;
        }

        if (plan_accepted_) {
            plan_accepted_ = false;
            std::string plan_text = std::move(plan_accepted_text_);
            ContextManager fresh_ctx(config_.token_limit, config_.compaction_keep_recent);
            mode_ = AgentMode::Act;
            rebuild_registry();
            fresh_ctx.push_system(system_prompt());
            context_ = std::move(fresh_ctx);
            ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
            pending_execution_ =
                "Execute the following plan step by step without pausing.\n\n" + plan_text;
            continue;
        }

        if (plan_rejected_) {
            plan_rejected_ = false;
            ContextManager fresh_ctx(config_.token_limit, config_.compaction_keep_recent);
            fresh_ctx.push_system(system_prompt());
            context_ = std::move(fresh_ctx);
            ui_.update_tokens(context_.total_tokens(), config_.token_limit);
            std::cout << "[plan rejected] Context cleared. Ready for new task.\n";
            std::cout.flush();
            continue;
        }

        if (task_done_called_) {
            task_done_called_ = false;
            if (non_interactive_) return;
            continue;
        }

        if (non_interactive_) {
            if (turn_error) {
                exit_code_ = 1;
                return;
            }
            return;
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
    std::vector<ToolCall> normal_calls;

    // Pre-pass: intercept special agent-level tools
    for (const auto& call : calls) {
        if (call.name == "present_plan" || call.name == "print") {
            ui_.show_tool_call(call, ToolSource::Local);
            ToolResult r = (call.name == "present_plan")
                ? handle_present_plan(call.args)
                : handle_print(call.args);
            ui_.show_tool_result(call, r);
            if (!combined.empty()) combined += "\n\n";
            combined += "[Tool: " + call.name + "]\n" + r.to_context_string();
        } else {
            normal_calls.push_back(call);
        }
    }

    if (!normal_calls.empty()) {
        struct PendingCall {
            const ToolCall& call;
            const Tool*     tool;
            bool            approved = false;
            std::string     reject_reason;
        };

        // Phase 1: Show all tool calls and collect approvals (sequential — stdin)
        std::vector<PendingCall> pending;
        pending.reserve(normal_calls.size());
        for (const auto& call : normal_calls) {
            auto tool_opt = registry_.find(call.name);
            ToolSource src = tool_opt.has_value() ? tool_opt.value()->source : ToolSource::Local;
            ui_.show_tool_call(call, src);

            PendingCall pc{call, tool_opt.has_value() ? tool_opt.value() : nullptr, false, ""};

            if (!tool_opt.has_value()) {
                pc.reject_reason = "Tool not found: " + call.name;
            } else if (requires_approval(tool_opt.value()->def)) {
                if (non_interactive_) {
                    pc.reject_reason =
                        "tool '" + call.name + "' requires approval but running in "
                        "non-interactive mode — use -y to auto-approve";
                } else {
                    Approval a = ui_.request_approval(call);
                    if (a == Approval::Accept) pc.approved = true;
                    else pc.reject_reason = "rejected by user";
                }
            } else {
                pc.approved = true;
            }

            if (pc.approved) {
                std::string key = call.name + ":";
                std::vector<std::pair<std::string, std::string>> kv;
                for (const auto& [k, v] : call.args) kv.push_back({k, to_json(v)});
                std::sort(kv.begin(), kv.end());
                for (const auto& [k, v] : kv) key += k + "=" + v + ";";
                if (!seen_calls_.insert(std::hash<std::string>{}(key)).second) {
                    pc.approved = false;
                    pc.reject_reason = "(skipped) Identical call already executed this turn.";
                }
            }

            pending.push_back(std::move(pc));
        }

        // Phase 2: Dispatch all approved tools in parallel
        std::vector<std::future<ToolResult>> futures;
        futures.reserve(pending.size());
        for (const auto& pc : pending) {
            if (!pc.approved) {
                std::promise<ToolResult> p;
                p.set_value(ToolResult::fail(pc.reject_reason));
                futures.push_back(p.get_future());
            } else {
                auto fn   = pc.tool->fn;
                auto args = pc.call.args;
                futures.push_back(std::async(std::launch::async,
                    [fn = std::move(fn), args = std::move(args)]() {
                        return fn(args);
                    }));
            }
        }

        // Phase 3: Collect results in arrival order, show, and combine
        for (size_t i = 0; i < pending.size(); ++i) {
            ToolResult result = futures[i].get();
            ui_.show_tool_result(pending[i].call, result);
            if (!combined.empty()) combined += "\n\n";
            combined += "[Tool: " + pending[i].call.name + "]\n" + result.to_context_string();
        }
    }

    if (!combined.empty()) {
        context_.push_user(combined);
    }
}

ToolResult Agent::handle_present_plan(const ToolArgs& args) {
    auto it = args.find("plan");
    if (it == args.end() || !it->second.is_string())
        return ToolResult::fail("present_plan: 'plan' argument required");
    std::string plan_text = it->second.as_string();

    ui_.show_plan(plan_text);

    bool auto_accept = config_.permissions.auto_approve_shell || non_interactive_;
    if (auto_accept) {
        plan_accepted_      = true;
        plan_accepted_text_ = plan_text;
        return ToolResult::ok("Plan auto-accepted.");
    }

    std::string refinement;
    PlanApproval decision = ui_.request_plan_approval(refinement);

    switch (decision) {
        case PlanApproval::Accept:
            plan_accepted_      = true;
            plan_accepted_text_ = plan_text;
            return ToolResult::ok("Plan accepted. Transitioning to execution.");
        case PlanApproval::Refine:
            return ToolResult::ok("Refinement requested: " + refinement);
        case PlanApproval::Reject:
            plan_rejected_ = true;
            return ToolResult::ok("Plan rejected by user. Returning to planning.");
    }
    return ToolResult::ok("");
}

ToolResult Agent::handle_print(const ToolArgs& args) {
    auto it = args.find("message");
    if (it == args.end() || !it->second.is_string())
        return ToolResult::fail("print: 'message' argument required");
    ui_.print_output(it->second.as_string());
    return ToolResult::ok("Printed.");
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
    mode_ = next;
    rebuild_registry();
    context_.replace_system(system_prompt());
    ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
}

void Agent::rebuild_registry() {
    registry_ = make_registry(mode_, config_);
}
