#include "tools.h"
#include "mcp_client.h"
#include <iostream>

// ============================================================================
// Registry factory
// ============================================================================

static Tool make_local_tool(std::string name, std::string desc,
                             std::vector<ToolParam> params,
                             Permission perm, ToolFn fn) {
    Tool t;
    t.def.name        = std::move(name);
    t.def.description = std::move(desc);
    t.def.params      = std::move(params);
    t.def.permission  = perm;
    t.fn              = std::move(fn);
    t.source          = ToolSource::Local;
    return t;
}

static ToolFn handler_or_noop(const AgentHandlers& handlers, const std::string& name) {
    auto h = handlers.find(name);
    return (h != handlers.end()) ? h->second : [](const ToolArgs&) { return ToolResult::ok(""); };
}

ToolRegistry make_registry(AgentMode mode, const Config& cfg, bool non_interactive, const AgentHandlers& handlers, std::vector<McpServerStatus>* status_out) {
    ToolRegistry registry;

    // Read-only tools: always registered
    registry.register_tool(make_local_tool("read_file",
        "Read the contents of a file. Use offset/limit to read a specific line range.",
        {{"path",   "string",  "Path to file",                                 true},
         {"offset", "integer", "1-based first line to read (optional)",        false},
         {"limit",  "integer", "Maximum number of lines to return (optional)", false}},
        Permission::Read, tool_read_file));

    registry.register_tool(make_local_tool("list_dir",
        "List files and directories in a directory",
        {{"path", "string", "Path to directory", true}},
        Permission::Read, tool_list_dir));

    registry.register_tool(make_local_tool("search_files",
        "List files or search file contents by regex (skips hidden dirs like .git). Omit 'pattern' to list matching files only.",
        {{"path",      "string", "Root path to search",                                              true},
         {"pattern",   "string", "Regex to search file contents (optional — omit to list files)",   false},
         {"file_glob", "string", "Optional glob filter, e.g. '*.py'",                               false}},
        Permission::Read, tool_search_files));

    registry.register_tool(make_local_tool("file_info",
        "Get metadata about a file or directory",
        {{"path", "string", "Path to file or directory", true}},
        Permission::Read, tool_file_info));

    registry.register_tool(make_local_tool("find_symbol",
        "Search for a symbol (function, class, variable) by name across source files. "
        "Returns file:line matches with word-boundary matching. Use language= to restrict "
        "to a specific file type (cpp, python, rust, go, javascript, typescript).",
        {{"symbol",   "string", "Symbol name to search for",                        true},
         {"language", "string", "Language filter: cpp, python, rust, go, js, ts",   false},
         {"path",     "string", "Root path to search (default: current directory)",  false}},
        Permission::Read, tool_find_symbol));

    auto register_native = [&](std::string name, std::string desc,
                                std::vector<ToolParam> params, ToolFn fn) {
        Tool t = make_local_tool(std::move(name), std::move(desc), std::move(params),
                                 Permission::Read, std::move(fn));
        t.agent_native = true;
        registry.register_tool(std::move(t));
    };

    // present_plan: Plan mode only
    if (mode == AgentMode::Plan) {
        register_native("present_plan",
            "Present the completed plan to the user for approval. "
            "The user will accept (proceed to execution), request refinements, "
            "or reject the plan. Call this when the plan is fully formed.",
            {{"plan", "string", "The complete numbered plan text to present to the user", true}},
            handler_or_noop(handlers, "present_plan"));
    }

    // print: both modes — explicit output to user / parent agent
    register_native("print",
        "Print a message to the user or parent agent. Use sparingly — only for "
        "important findings, key decisions, or final answers. Do not use for "
        "routine step announcements.",
        {{"message", "string", "The message to display", true}},
        handler_or_noop(handlers, "print"));

    // ask_user: omitted in non-interactive and yolo (auto_approve_shell) modes
    if (!non_interactive && !cfg.permissions.auto_approve_shell) {
        register_native("ask_user",
            "Ask the user a clarifying question and wait for their response. "
            "Optionally provide semicolon-separated choices (e.g. \"Yes;No;Maybe\"); "
            "a 'Custom response' option is always appended as the last choice.",
            {{"question", "string", "The question to present to the user",                            true},
             {"options",  "string", "Optional semicolon-separated list of choices, e.g. \"Option A;Option B\"", false}},
            handler_or_noop(handlers, "ask_user"));
    }

    // Write tools (Act mode only)
    if (mode == AgentMode::Act) {
        registry.register_tool(make_local_tool("write_file",
            "Write content to a file (atomic via temporary file)",
            {{"path",    "string", "Path to file",       true},
             {"content", "string", "Content to write",   true}},
            Permission::Write, tool_write_file));

        registry.register_tool(make_local_tool("edit_file",
            "Replace an exact string in a file (old_str must appear exactly once)",
            {{"path",    "string", "Path to file",          true},
             {"old_str", "string", "Exact text to replace", true},
             {"new_str", "string", "Replacement text",      true}},
            Permission::Write, tool_edit_file));

        registry.register_tool(make_local_tool("create_dir",
            "Create a directory (creates parent directories as needed)",
            {{"path", "string", "Path to directory", true}},
            Permission::Write, tool_create_dir));

        registry.register_tool(make_local_tool("delete_file",
            "Delete a file (requires approval)",
            {{"path", "string", "Path to file", true}},
            Permission::Delete, tool_delete_file));

        registry.register_tool(make_local_tool("delete_dir",
            "Delete a directory. Set recursive=true to delete non-empty directories.",
            {{"path",      "string",  "Path to directory",                          true},
             {"recursive", "boolean", "Delete non-empty directories recursively",   false}},
            Permission::Delete, tool_delete_dir));

        registry.register_tool(make_local_tool("complete_step",
            "Mark a numbered plan step as done. Call this after finishing each step, "
            "then immediately continue to the next step with a tool call.",
            {{"step", "integer", "Step number to mark complete (1-based)", true}},
            Permission::Read,
            [](const ToolArgs& args) -> ToolResult {
                auto it = args.find("step");
                if (it == args.end() || !it->second.is_number())
                    return ToolResult::fail("complete_step: 'step' argument required");
                int n = (int)it->second.as_number();
                return ToolResult::ok("Step " + std::to_string(n) + " done.");
            }));
    }

    // run_shell is available in all modes (read-only exploration, build checks, etc.)
    registry.register_tool(make_local_tool("run_shell",
        "Execute a shell command (requires approval)",
        {{"command",     "string",  "Shell command to execute",                     true},
         {"cwd",         "string",  "Working directory (optional)",                 false},
         {"timeout_sec", "integer", "Timeout in seconds (optional, default 30)",    false}},
        Permission::Shell, tool_run_shell));

    // spawn_agent: invoke a fresh ccl instance as a focused sub-agent
    registry.register_tool(make_local_tool("spawn_agent",
        "Spawn a sub-agent of ccl to handle a focused task. The sub-agent runs with full "
        "tool access and returns all its output. Use for bounded research, exploration, or "
        "investigation tasks that benefit from isolation.",
        {{"prompt",      "string",  "Task description for the sub-agent",             true},
         {"mode",        "string",  "Agent mode: 'act' (default) or 'plan'",          false},
         {"working_dir", "string",  "Working directory for the sub-agent (optional)", false},
         {"timeout_sec", "integer", "Timeout in seconds (optional, default 600)",     false}},
        Permission::Shell,
        [config_path = cfg.config_path](const ToolArgs& args) {
            return tool_spawn_agent(args, config_path);
        }));

    // Register MCP tools from configured servers
    for (const auto& server_cfg : cfg.mcp_servers) {
        auto client = std::make_shared<McpClient>(server_cfg, cfg);
        if (!client->initialize()) {
            std::cerr << "[mcp] warning: could not connect to '" << server_cfg.name << "'\n";
            if (status_out)
                status_out->push_back({server_cfg.name, server_cfg.transport,
                                       server_cfg.url, server_cfg.command, false, 0});
            continue;
        }
        auto tool_defs = client->list_tools();
        if (status_out)
            status_out->push_back({server_cfg.name, server_cfg.transport,
                                   server_cfg.url, server_cfg.command, true,
                                   (int)tool_defs.size()});
        for (auto def : tool_defs) {
            def.permission = server_cfg.write_tools.count(def.name) ? Permission::Write : Permission::Read;
            Tool tool;
            tool.def        = def;
            tool.source     = ToolSource::Mcp;
            tool.mcp_server = server_cfg.name;
            tool.fn = [client, name = def.name](const ToolArgs& args) {
                return client->call_tool(name, args);
            };
            registry.register_tool(std::move(tool));
        }
    }

    return registry;
}
