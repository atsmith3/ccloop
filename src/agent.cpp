// Copyright 2026 Andrew Smith
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "agent.h"
#include "json.h"
#include <atomic>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

Agent::Agent(Config config, Ui &ui, AgentMode initial_mode)
    : config_(std::move(config)), mode_(initial_mode),
      context_(config_.token_limit, config_.compaction_keep_recent),
      connector_(make_connector(config_)), ui_(ui) {
  rebuild_registry();
  build_slash_commands();
}

Agent::Agent(Config config, Ui &ui, std::unique_ptr<Connector> connector,
             AgentMode initial_mode)
    : config_(std::move(config)), mode_(initial_mode),
      context_(config_.token_limit, config_.compaction_keep_recent),
      connector_(std::move(connector)), ui_(ui) {
  rebuild_registry();
  build_slash_commands();
}

int Agent::run(const std::string &initial_prompt) {
  context_.push_system(system_prompt());
  ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
  ui_.update_tokens(context_.total_tokens(), config_.token_limit);
  if (!initial_prompt.empty()) {
    pending_execution_ = initial_prompt;
    non_interactive_ = true;
    rebuild_registry();
  }
  loop();
  return exit_code_;
}

void Agent::reset_context() {
  ContextManager fresh(config_.token_limit, config_.compaction_keep_recent);
  fresh.push_system(system_prompt());
  context_ = std::move(fresh);
}

std::string Agent::system_prompt() const {
  switch (mode_) {
  case AgentMode::Plan:
    return "You are an expert software architect helping a developer "
           "understand and plan "
           "changes to their codebase.\n\n"
           "## Keep this context lean — delegate exploration to sub-agents\n\n"
           "Never read files or run shell commands directly in this context. "
           "Every exploration "
           "task — even reading a single short file — must go through "
           "spawn_agent. Raw file "
           "contents must never accumulate here; only compact summaries "
           "returned by sub-agents.\n\n"
           "Delegate everything:\n"
           "- Reading any file -> spawn_agent\n"
           "- Searching for patterns or usages -> spawn_agent\n"
           "- Running build/test commands -> spawn_agent\n"
           "- Any question that requires looking at the codebase -> "
           "spawn_agent\n\n"
           "Examples:\n"
           "  spawn_agent(prompt='Read src/agent.cpp and explain how the agent "
           "loop works')\n"
           "  spawn_agent(prompt='Find all callers of compact() and report "
           "when it fires')\n"
           "  spawn_agent(prompt='List src/ and summarize what each module "
           "does')\n\n"
           "When research tasks are independent, issue all spawn_agent calls "
           "in a single response\n"
           "so they run in parallel:\n\n"
           "  spawn_agent(prompt='Read src/agent.cpp and explain the agent "
           "loop')\n"
           "  spawn_agent(prompt='Read src/tools.cpp and list every tool and "
           "its permission level')\n"
           "  spawn_agent(prompt='Run cd build_local && make -j 2>&1 | tail "
           "-10 and report errors')\n\n"
           "Delegation rule — keep partitioning until each agent holds an "
           "atomic task:\n"
           "Spawn sub-agents for each sub-question in your task. If a "
           "sub-agent's task still\n"
           "covers multiple files or operations, it should spawn further "
           "sub-agents — one per\n"
           "file or operation. Continue until each agent's task is truly "
           "atomic (one file, one\n"
           "search, one command). An agent with an atomic task works directly "
           "with tools.\n\n"
           "Never guess at file contents or project structure — always explore "
           "first. "
           "If you are unsure which files are relevant, keep exploring until "
           "you are confident.\n\n"
           "When producing an implementation plan:\n"
           "1. Explore the codebase thoroughly to understand existing patterns "
           "and conventions\n"
           "2. Identify every file that will need to change\n"
           "3. Call present_plan with a clear numbered checklist as the 'plan' "
           "argument:\n\n"
           "   present_plan(plan=\"## Plan: <slug>\\n1. [ ] action — file\\n2. "
           "[ ] action — file\\n...\")\n\n"
           "   The slug must be kebab-case, 2-4 words (e.g. "
           "add-lcs-algorithm).\n"
           "   The user will accept, refine, or reject. On refinement, revise "
           "and call present_plan again.\n"
           "4. Call out risks, dependencies, or prerequisites in the plan "
           "text\n\n"
           "For simple questions that do not require a plan, answer directly "
           "in text — no tool call needed.\n\n"
           "Use ask_user to clarify requirements before planning. Ask ONE "
           "focused question per call — "
           "never bundle multiple questions into a single call. When you need "
           "several answers, ask them "
           "sequentially: wait for the response, then ask the next "
           "question.\n\n"
           "Use print(message=...) to surface key findings mid-exploration. "
           "Keep output minimal.\n\n"
           "Keep plans grounded in what you actually read — do not invent "
           "structure or APIs "
           "that you have not confirmed exist. Do not write or modify any "
           "files.\n\n"
           "## Style\n\n"
           "No emojis. Use only ASCII characters in all output.\n"
           "Document all code using Doxygen style (/** ... */ comments with "
           "@brief, @param, @return, etc.).";
  case AgentMode::Act:
    return "You are an expert software engineer executing tasks precisely and "
           "methodically.\n\n"
           "## Phase 1 — Identify or create the plan\n\n"
           "If a numbered plan exists in the conversation context, use it.\n"
           "If no plan exists, write one now:\n\n"
           "  ## Plan: <short-descriptive-slug>\n"
           "  1. [ ] task step one\n"
           "  2. [ ] task step two\n"
           "  ...\n\n"
           "## Phase 2 — Execute every task step without pausing\n\n"
           "Work through all steps continuously. A text-only response (no tool "
           "calls) signals\n"
           "that ALL work is complete — never emit one between steps.\n\n"
           "Rules:\n"
           "- While steps remain incomplete, always include a tool call — do "
           "not emit a\n"
           "  standalone acknowledgment between steps\n"
           "- Once all steps are verified done (files written, no pending "
           "changes), respond\n"
           "  with a text summary only — no trailing tool call\n"
           "- Read the file immediately before every edit_file call — old_str "
           "must come from\n"
           "  the most recent read, not from memory\n"
           "- Make one edit at a time; re-read before any subsequent edit to "
           "the same file\n"
           "- When one step finishes, proceed immediately to the next with a "
           "tool call — do\n"
           "  not emit a step-complete acknowledgment as a standalone message\n"
           "If edit_file returns \"old_str not found\", re-read the file, "
           "locate the correct\n"
           "text, and retry. Never abandon a step due to this error.\n\n"
           "After each step finishes — whether done directly or via sub-agent "
           "— call\n"
           "complete_step(step=N) before proceeding. Never emit text between "
           "steps.\n\n"
           "## Phase 3 — Signal completion\n\n"
           "When all steps are done, respond with a text summary of what was "
           "accomplished.\n\n"
           "For conversational questions that don't require plan execution, "
           "answer in text.\n\n"
           "Use print(message=...) to surface important status or findings "
           "mid-execution.\n\n"
           "## Keep this context lean — delegate every step to sub-agents\n\n"
           "Spawn a worker for EVERY plan step — no exceptions. Your only "
           "direct tool calls\n"
           "are spawn_agent, complete_step, ask_user, and print. Never call "
           "read_file,\n"
           "edit_file, write_file, or run_shell directly in this context.\n\n"
           "  spawn_agent(prompt='Step 2: in src/foo.cpp change function bar "
           "to do X. "
           "Read the file first, apply the change, verify by reading it back. "
           "If the step touches more than one file, spawn a sub-agent per "
           "file.')\n"
           "  spawn_agent(prompt='Run cd build_local && make -j 2>&1 | tail "
           "-20. Report pass/fail and any errors.')\n\n"
           "When multiple steps are independent (different files, no ordering "
           "dependency),\n"
           "call spawn_agent for each in a single response — they run in "
           "parallel:\n\n"
           "  spawn_agent(prompt='Step 3: edit src/foo.cpp — change function X "
           "to do Y. Read first, edit, verify.')\n"
           "  spawn_agent(prompt='Step 4: edit src/bar.cpp — add function Z. "
           "Read first, write, verify.')\n\n"
           "The worker returns a summary. Call complete_step(step=N), then "
           "immediately\n"
           "continue to the next step with another spawn_agent or "
           "complete_step. Never\n"
           "re-read files the worker already processed.\n\n"
           "Delegation rule — keep partitioning until each agent holds an "
           "atomic task:\n"
           "A worker whose step covers multiple files should spawn one "
           "sub-agent per file.\n"
           "Each sub-agent should further partition if needed. Continue until "
           "each agent's\n"
           "task is truly atomic (one file read, one edit, one command). An "
           "agent with an\n"
           "atomic task works directly with tools — no further spawning.\n\n"
           "Match existing code style. Stay within plan scope.\n\n"
           "## Roadblock (use only when you cannot proceed without user "
           "input)\n\n"
           "  ## Roadblock: <one-line description of what is blocking you>\n\n"
           "Output this marker to pause execution and ask the user for "
           "guidance. "
           "Do not output it for recoverable errors — attempt fixes first.\n\n"
           "When using ask_user mid-execution, ask ONE focused question per "
           "call. "
           "Never bundle multiple questions into a single call.\n\n"
           "## Style\n\n"
           "No emojis. Use only ASCII characters in all output.\n"
           "Document all code using Doxygen style (/** ... */ comments with "
           "@brief, @param, @return, etc.).";
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

  // 4× the main limit so the summarization call itself is never truncated —
  // it receives the full conversation text plus the system prompt.
  ContextManager summary_ctx(config_.token_limit * 4);
  summary_ctx.push_system(
      "You are a helpful assistant. Summarize the following conversation "
      "concisely but "
      "completely. Preserve: key decisions, files created or modified, code "
      "changes made, "
      "plan steps completed and any remaining steps, errors encountered and "
      "how they were "
      "resolved. Output only the summary with no preamble.");
  summary_ctx.push_user("Summarize this conversation:\n\n" + conversation);

  LlmResponse resp = connector_->complete(summary_ctx, {});
  if (resp.is_error || resp.content.empty()) {
    ui_.show_error(
        "[compact] Summarization failed — falling back to rolling window");
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
      is_synthetic = true; // avoids slash-command dispatch on injected prompts
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

      LlmResponse response =
          connector_->complete(context_, registry_.definitions());

      if (response.is_error) {
        // Suppress curl-abort errors that result from Ctrl+C interruption
        if (!should_interrupt.load()) {
          ui_.show_error("[ERROR] " + response.content);
          ui_.update_tokens(context_.total_tokens(), config_.token_limit);
          turn_error = true;
        }
        break; // Exit inner loop, wait for next user input
      }

      context_.sync_token_count(response.usage);
      ui_.show_usage(response.usage, context_.total_tokens(),
                     config_.token_limit);

      if (!response.tool_calls.empty()) {
        // Assistant called tools — push message and execute them
        context_.push_assistant(response.content);
        handle_tool_calls(response.tool_calls);
        if (task_done_called_ || plan_accepted_ || plan_rejected_)
          break;
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
      break; // Exit inner loop, get next user input
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
      mode_ = AgentMode::Act;
      rebuild_registry();
      reset_context();
      ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
      pending_execution_ =
          "Execute the following plan step by step without pausing.\n\n" +
          plan_text;
      continue;
    }

    if (plan_rejected_) {
      plan_rejected_ = false;
      reset_context();
      ui_.update_tokens(context_.total_tokens(), config_.token_limit);
      std::cout << "[plan rejected] Context cleared. Ready for new task.\n";
      std::cout.flush();
      continue;
    }

    if (task_done_called_) {
      task_done_called_ = false;
      if (non_interactive_)
        return;
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

bool Agent::requires_approval(const ToolDef &def) const {
  switch (def.permission) {
  case Permission::Read:
    return !config_.permissions.auto_approve_read;
  case Permission::Write:
    return !config_.permissions.auto_approve_write;
  case Permission::Delete:
    return !config_.permissions.auto_approve_delete;
  case Permission::Shell:
    return !config_.permissions.auto_approve_shell;
  }
  return true; // unknown permission: gate by default
}

void Agent::handle_tool_calls(const std::vector<ToolCall> &calls) {
  seen_calls_.clear();
  std::string combined;
  std::vector<ToolCall> normal_calls;

  // Pre-pass: intercept special agent-level tools
  for (const auto &call : calls) {
    auto tool_opt = registry_.find(call.name);
    if (tool_opt.has_value() && tool_opt.value()->agent_native) {
      // Agent-native tools run sequentially in the pre-pass (they may block on
      // user I/O)
      ui_.show_tool_call(call, tool_opt.value()->source);
      ToolResult r = tool_opt.value()->fn(call.args);
      ui_.show_tool_result(call, r);
      if (!combined.empty())
        combined += "\n\n";
      combined += "[Tool: " + call.name + "]\n" + r.to_context_string();
    } else {
      normal_calls.push_back(call);
    }
  }

  if (!normal_calls.empty()) {
    struct PendingCall {
      const ToolCall &call;
      const Tool *tool;
      bool approved = false;
      std::string reject_reason;
    };

    // Phase 1: Show all tool calls and collect approvals (sequential — stdin)
    std::vector<PendingCall> pending;
    pending.reserve(normal_calls.size());
    for (const auto &call : normal_calls) {
      auto tool_opt = registry_.find(call.name);
      ToolSource src =
          tool_opt.has_value() ? tool_opt.value()->source : ToolSource::Local;
      ui_.show_tool_call(call, src);

      PendingCall pc{call, tool_opt.has_value() ? tool_opt.value() : nullptr,
                     false, ""};

      if (!tool_opt.has_value()) {
        pc.reject_reason = "Tool not found: " + call.name;
      } else if (requires_approval(tool_opt.value()->def)) {
        if (non_interactive_) {
          pc.reject_reason = "tool '" + call.name +
                             "' requires approval but running in "
                             "non-interactive mode — use -y to auto-approve";
        } else {
          Approval a = ui_.request_approval(call);
          if (a == Approval::Accept)
            pc.approved = true;
          else
            pc.reject_reason = "rejected by user";
        }
      } else {
        pc.approved = true;
      }

      if (pc.approved) {
        // Key: name + args serialized as a sorted-key JSON object.
        // Using to_json() avoids delimiter collisions that a hand-rolled
        // "k=v;" format would have if names or values contain those chars.
        JsonValue args_obj;
        JsonObject obj;
        for (const auto &[k, v] : call.args)
          obj[k] = std::make_shared<JsonValue>(v);
        args_obj.data = std::move(obj);
        std::string key = call.name + ":" + to_json(args_obj);
        if (!seen_calls_.insert(std::hash<std::string>{}(key)).second) {
          pc.approved = false;
          pc.reject_reason =
              "(skipped) Identical call already executed this turn.";
        }
      }

      pending.push_back(std::move(pc));
    }

    // Phase 2: Dispatch all approved tools in parallel
    std::vector<std::future<ToolResult>> futures;
    futures.reserve(pending.size());
    for (const auto &pc : pending) {
      if (!pc.approved) {
        std::promise<ToolResult> p;
        p.set_value(ToolResult::fail(pc.reject_reason));
        futures.push_back(p.get_future());
      } else {
        auto fn = pc.tool->fn;
        auto args = pc.call.args;
        futures.push_back(std::async(
            std::launch::async, [fn = std::move(fn), args = std::move(args)]() {
              return fn(args);
            }));
      }
    }

    // Phase 3: Collect results in arrival order, show, and combine
    for (size_t i = 0; i < pending.size(); ++i) {
      ToolResult result;
      try {
        result = futures[i].get();
      } catch (const std::exception &e) {
        result = ToolResult::fail(std::string("Tool threw: ") + e.what());
      } catch (...) {
        result = ToolResult::fail("Tool threw an unknown exception");
      }
      ui_.show_tool_result(pending[i].call, result);
      if (!combined.empty())
        combined += "\n\n";
      combined +=
          "[Tool: " + pending[i].call.name + "]\n" + result.to_context_string();
    }
  }

  if (!combined.empty()) {
    context_.push_user(combined);
  }
}

ToolResult Agent::handle_present_plan(const ToolArgs &args) {
  auto it = args.find("plan");
  if (it == args.end() || !it->second.is_string())
    return ToolResult::fail("present_plan: 'plan' argument required");
  std::string plan_text = it->second.as_string();

  ui_.show_plan(plan_text);

  bool auto_accept = config_.permissions.auto_approve_shell || non_interactive_;
  if (auto_accept) {
    plan_accepted_ = true;
    plan_accepted_text_ = plan_text;
    return ToolResult::ok("Plan auto-accepted.");
  }

  std::string refinement;
  PlanApproval decision = ui_.request_plan_approval(refinement);

  switch (decision) {
  case PlanApproval::Accept:
    plan_accepted_ = true;
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

ToolResult Agent::handle_print(const ToolArgs &args) {
  auto it = args.find("message");
  if (it == args.end() || !it->second.is_string())
    return ToolResult::fail("print: 'message' argument required");
  ui_.print_output(it->second.as_string());
  return ToolResult::ok("Printed.");
}

ToolResult Agent::handle_ask_user(const ToolArgs &args) {
  auto q_it = args.find("question");
  if (q_it == args.end() || !q_it->second.is_string())
    return ToolResult::fail("ask_user: 'question' argument required");
  const std::string &question = q_it->second.as_string();

  std::vector<std::string> options;
  auto o_it = args.find("options");
  if (o_it != args.end() && o_it->second.is_string()) {
    std::istringstream ss(o_it->second.as_string());
    std::string token;
    while (std::getline(ss, token, ';')) {
      token = trim(token);
      if (!token.empty())
        options.push_back(token);
    }
  }

  std::string response = ui_.ask_user(question, options);
  return ToolResult::ok("User response: " + response);
}

bool Agent::save_context(const std::string &path) const {
  std::ofstream f(path);
  if (!f)
    return false;

  f << "{\n"
    << "  \"version\": 1,\n"
    << "  \"mode\": \"" << (mode_ == AgentMode::Plan ? "plan" : "act")
    << "\",\n"
    << "  \"total_tokens\": " << context_.total_tokens() << ",\n"
    << "  \"messages\": [\n";

  const auto &msgs = context_.messages();
  for (size_t i = 0; i < msgs.size(); ++i) {
    f << "    {\"role\": \"" << role_to_str(msgs[i].role)
      << "\", \"content\": \"" << escape_json(msgs[i].content)
      << "\", \"estimated_tokens\": " << msgs[i].estimated_tokens << "}";
    if (i + 1 < msgs.size())
      f << ",";
    f << "\n";
  }
  f << "  ]\n}\n";
  return f.good();
}

bool Agent::restore_context(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    return false;
  std::ostringstream ss;
  ss << f.rdbuf();

  JsonValue root;
  try {
    root = parse_json(ss.str());
  } catch (...) {
    return false;
  }

  auto ver = root.get("version");
  if (!ver || !ver->is_number() || static_cast<int>(ver->as_number()) != 1)
    return false;

  auto mode_v = root.get("mode");
  if (!mode_v || !mode_v->is_string())
    return false;
  AgentMode new_mode =
      (mode_v->as_string() == "act") ? AgentMode::Act : AgentMode::Plan;

  auto total_v = root.get("total_tokens");
  size_t total_tokens = (total_v && total_v->is_number())
                            ? static_cast<size_t>(total_v->as_number())
                            : 0;

  auto msgs_v = root.get("messages");
  if (!msgs_v || !msgs_v->is_array())
    return false;

  std::vector<Message> msgs;
  for (const auto &m : msgs_v->as_array()) {
    if (!m)
      continue;
    auto role_v = m->get("role");
    auto content_v = m->get("content");
    auto tokens_v = m->get("estimated_tokens");
    if (!role_v || !content_v)
      continue;

    Message::Role role = str_to_role(role_v->as_string());

    size_t est = (tokens_v && tokens_v->is_number())
                     ? static_cast<size_t>(tokens_v->as_number())
                     : 0;
    msgs.push_back({role, content_v->as_string(), est});
  }

  context_ = ContextManager::from_messages(std::move(msgs), total_tokens,
                                           config_.token_limit,
                                           config_.compaction_keep_recent);
  mode_ = new_mode;
  rebuild_registry();
  return true;
}

void Agent::build_slash_commands() {
  slash_commands_.clear();

  slash_commands_.push_back(
      {"mode", "plan|act — switch modes", [this](const std::string &arg) {
         if (arg == "plan")
           transition_to(AgentMode::Plan);
         else if (arg == "act")
           transition_to(AgentMode::Act);
       }});

  slash_commands_.push_back(
      {"compact", "summarize and compact the context window",
       [this](const std::string &) { compact_with_summary(); }});

  slash_commands_.push_back(
      {"clear", "clear context and start fresh", [this](const std::string &) {
         reset_context();
         ui_.update_tokens(context_.total_tokens(), config_.token_limit);
       }});

  slash_commands_.push_back(
      {"context", "save <file> | restore <file>",
       [this](const std::string &arg) {
         size_t sp = arg.find(' ');
         std::string sub = (sp == std::string::npos) ? arg : arg.substr(0, sp);
         std::string file = (sp == std::string::npos) ? "" : arg.substr(sp + 1);
         file = trim(file);

         if (sub == "save" && !file.empty()) {
           if (save_context(file))
             std::cout << "[context saved to " << file << "]\n";
           else
             std::cout << "[error] could not write " << file << "\n";
           std::cout.flush();
         } else if (sub == "restore" && !file.empty()) {
           if (restore_context(file)) {
             ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
             std::cout << "[context restored from " << file << "]\n";
             std::cout.flush();
           } else {
             std::cout << "[error] could not restore from " << file << "\n";
             std::cout.flush();
           }
         } else {
           std::cout
               << "Usage: /context save <file> | /context restore <file>\n";
           std::cout.flush();
         }
       }});

  slash_commands_.push_back(
      {"edit", "open $EDITOR to compose a multi-line prompt",
       [this](const std::string &) {
         std::string text = ui_.open_editor(config_.editor);
         if (text.empty()) {
           std::cout << "[edit cancelled]\n";
           std::cout.flush();
         } else {
           pending_execution_ = std::move(text);
         }
       }});

  slash_commands_.push_back(
      {"mcp", "list | reload — show MCP server status or reconnect all",
       [this](const std::string &arg) {
         if (arg == "list")
           cmd_mcp_list();
         else if (arg == "reload")
           cmd_mcp_reload();
         else {
           std::cout << "Usage: /mcp list | /mcp reload\n";
           std::cout.flush();
         }
       }});

  slash_commands_.push_back(
      {"quit", "exit", [](const std::string &) { should_exit = true; }});

  slash_commands_.push_back(
      {"help", "show this help", [this](const std::string &) {
         std::cout << "Slash commands:\n";
         for (const auto &sc : slash_commands_)
           std::cout << "  /" << sc.name << " — " << sc.description << "\n";
         std::cout.flush();
       }});
}

bool Agent::handle_slash_command(std::string_view input) {
  if (input.empty() || input[0] != '/')
    return false;

  std::string cmd(input);
  size_t space_pos = cmd.find(' ');
  std::string command = (space_pos == std::string::npos)
                            ? cmd.substr(1)
                            : cmd.substr(1, space_pos - 1);
  std::string arg =
      (space_pos == std::string::npos) ? "" : cmd.substr(space_pos + 1);
  arg = trim(arg);

  for (const auto &sc : slash_commands_) {
    if (sc.name == command) {
      sc.fn(arg);
      return true;
    }
  }
  return false;
}

void Agent::transition_to(AgentMode next) {
  if (next == mode_)
    return;
  mode_ = next;
  rebuild_registry();
  context_.replace_system(system_prompt());
  ui_.show_mode(mode_, context_.total_tokens(), config_.token_limit);
}

void Agent::rebuild_registry() {
  AgentHandlers handlers = {
      {"present_plan",
       [this](const ToolArgs &a) { return handle_present_plan(a); }},
      {"print", [this](const ToolArgs &a) { return handle_print(a); }},
      {"ask_user", [this](const ToolArgs &a) { return handle_ask_user(a); }},
  };
  mcp_status_.clear();
  registry_ =
      make_registry(mode_, config_, non_interactive_, handlers, &mcp_status_);
}

void Agent::cmd_mcp_list() {
  if (config_.mcp_config.empty()) {
    std::cout << "No MCP config file configured.\n";
  } else {
    std::cout << "MCP config: " << config_.mcp_config << "\n";
  }
  if (mcp_status_.empty()) {
    std::cout << "  (no MCP servers configured)\n";
  } else {
    for (const auto &s : mcp_status_) {
      const char *tr = (s.transport == McpTransportType::Stdio)       ? "stdio"
                       : (s.transport == McpTransportType::LegacySse) ? "sse"
                                                                      : "http";
      std::string endpoint = s.url.empty() ? s.command : s.url;
      std::string status =
          s.connected ? "connected (" + std::to_string(s.tool_count) + " tools)"
                      : "failed";
      std::cout << "  " << s.name << " [" << tr << "] " << endpoint
                << " \xe2\x80\x94 " << status << "\n";
    }
  }
  std::cout.flush();
}

void Agent::cmd_mcp_reload() {
  std::cout << "Reloading MCP connections...\n";
  std::cout.flush();
  config_.reload_mcp_servers();
  rebuild_registry();
  context_.replace_system(system_prompt());
  cmd_mcp_list();
}
