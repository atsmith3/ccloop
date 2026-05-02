#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include "types.h"
#include "config.h"

// Forward declaration
class ContextManager;

using ToolFn = std::function<ToolResult(const ToolArgs&)>;

// Handlers for agent-native tools (run sequentially in the agent pre-pass, may block on I/O).
// Pass these into make_registry so new agent-native tools need no changes in agent.cpp.
using AgentHandlers = std::unordered_map<std::string, ToolFn>;

struct Tool {
    ToolDef    def;
    ToolFn     fn;
    ToolSource source       = ToolSource::Local;
    std::string mcp_server  = "";
    bool        agent_native = false;  // true → run in pre-pass (sequential), not thread pool
};

class ToolRegistry {
public:
    void register_tool(Tool tool);
    std::vector<ToolDef>         definitions() const;
    std::optional<const Tool*>   find(const std::string& name) const;

private:
    std::vector<Tool>                       tools_;
    std::unordered_map<std::string, size_t> index_;
};

// Factory — builds registry based on mode.
// Pass handlers for agent-native tools (present_plan, print, ask_user) so their fn fields
// are wired up without hardcoding names in agent.cpp.
ToolRegistry make_registry(AgentMode mode, const Config& cfg,
                            bool non_interactive = false,
                            const AgentHandlers& handlers = {});

// Read-only tools (registered in all modes)
ToolResult tool_read_file    (const ToolArgs& args);
ToolResult tool_list_dir     (const ToolArgs& args);
ToolResult tool_search_files (const ToolArgs& args);
ToolResult tool_file_info    (const ToolArgs& args);
ToolResult tool_find_symbol  (const ToolArgs& args);

// Write tools (Act mode only)
ToolResult tool_write_file   (const ToolArgs& args);
ToolResult tool_edit_file    (const ToolArgs& args);
ToolResult tool_create_dir   (const ToolArgs& args);
ToolResult tool_delete_file  (const ToolArgs& args);
ToolResult tool_delete_dir   (const ToolArgs& args);
ToolResult tool_run_shell    (const ToolArgs& args);
ToolResult tool_spawn_agent  (const ToolArgs& args, const std::string& config_path = "");
