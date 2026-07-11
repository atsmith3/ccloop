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

Agent::Agent(Config config, Ui &ui)
    : config_(std::move(config)),
      context_(config_.token_limit, config_.compaction_keep_recent),
      connector_(make_connector(config_)), ui_(ui) {
  rebuild_registry();
  build_slash_commands();
}

Agent::Agent(Config config, Ui &ui, std::unique_ptr<Connector> connector)
    : config_(std::move(config)),
      context_(config_.token_limit, config_.compaction_keep_recent),
      connector_(std::move(connector)), ui_(ui) {
  rebuild_registry();
  build_slash_commands();
}

int Agent::run(const std::string &initial_prompt) {
  context_.push_system(system_prompt());
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
  return "Working directory: " + config_.working_dir +
         "\n\n"
         "You are ccl, an expert software engineer working directly in the "
         "user's codebase. Work precisely and methodically, using your tools "
         "to inspect and change files.\n\n"
         "## Tools\n\n"
         "- read_file — read a file (use offset/limit for large files)\n"
         "- write_file — create or overwrite a file\n"
         "- edit_file — replace an exact string in a file\n"
         "- run_shell — your terminal. Use it for everything the dedicated "
         "tools do not cover: listing directories (ls), searching (grep/rg), "
         "file metadata (stat), creating directories (mkdir), removing files "
         "(rm), and running builds and tests.\n"
         "When MCP servers are configured, their tools appear alongside "
         "these.\n\n"
         "## Editing files\n\n"
         "- Read a file immediately before every edit_file call — old_str "
         "must come from the most recent read, not from memory.\n"
         "- Make one edit at a time; re-read before any subsequent edit to "
         "the same file.\n"
         "- If edit_file returns \"old_str not found\", re-read the file, "
         "locate the correct text, and retry. If old_str is ambiguous "
         "(appears more than once), include more surrounding context to make "
         "it unique. Never abandon an edit due to these errors.\n\n"
         "## Interaction\n\n"
         "Keep working while there is more to do — every turn that includes a "
         "tool call continues the loop. When the task is complete, or when you "
         "need input from the user, respond with text and no tool call: that "
         "ends the turn and returns control to the user. Put any questions to "
         "the user in that final text; they will answer on the next turn.\n\n"
         "Work within the scope of the request and match the existing code "
         "style.\n\n"
         "## Style\n\n"
         "No emojis. Use only ASCII characters in all output.\n"
         "Document all code using Doxygen style (/** ... */ comments with "
         "@brief, @param, @return, etc.).";
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

    // Main interaction loop: keep calling LLM until we get a text response
    int tool_steps = 0;
    constexpr int kMaxToolSteps = 25;
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
        if (task_done_called_)
          break;
        if (++tool_steps >= kMaxToolSteps) {
          ui_.show_error("[warning] tool step limit reached — stopping");
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
      task_done_called_ = false;
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
  case Permission::Execute:
    return !config_.permissions.auto_approve_execute;
  }
  return true; // unknown permission: gate by default
}

static std::string tool_result_header(const ToolCall &call) {
  auto it = call.args.find("path");
  if (it != call.args.end() && it->second.is_string())
    return "[Tool: " + call.name + " | " + it->second.as_string() + "]";
  return "[Tool: " + call.name + "]";
}

void Agent::handle_tool_calls(const std::vector<ToolCall> &calls) {
  seen_calls_.clear();
  std::string combined;

  if (!calls.empty()) {
    struct PendingCall {
      const ToolCall &call;
      const Tool *tool;
      bool approved = false;
      std::string reject_reason;
    };

    // Phase 1: Show all tool calls and collect approvals (sequential — stdin)
    std::vector<PendingCall> pending;
    pending.reserve(calls.size());
    for (const auto &call : calls) {
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

    // Prune stale reads: if an approved call targets a path already in context,
    // mark the old result [superseded] before appending the fresh one.
    for (const auto &pc : pending) {
      if (!pc.approved)
        continue;
      auto it = pc.call.args.find("path");
      if (it != pc.call.args.end() && it->second.is_string())
        context_.prune_tool_result("[Tool: " + pc.call.name + " | " +
                                   it->second.as_string() + "]");
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
      combined += tool_result_header(pending[i].call) + "\n" +
                  result.to_context_string();
    }
  }

  if (!combined.empty()) {
    context_.push_user(combined);
  }
}

bool Agent::save_context(const std::string &path) const {
  std::ofstream f(path);
  if (!f)
    return false;

  f << "{\n"
    << "  \"version\": 1,\n"
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

  // A "mode" field may exist in contexts saved by older versions; it is
  // ignored now that ccl is single-mode.

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
  rebuild_registry();
  return true;
}

void Agent::build_slash_commands() {
  slash_commands_.clear();

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
             ui_.update_tokens(context_.total_tokens(), config_.token_limit);
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

void Agent::rebuild_registry() {
  mcp_status_.clear();
  registry_ = make_registry(config_, &mcp_status_);
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
