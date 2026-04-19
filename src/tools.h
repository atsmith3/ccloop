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

struct Tool {
    ToolDef    def;
    ToolFn     fn;
    ToolSource source     = ToolSource::Local;
    std::string mcp_server = "";
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

// Factory — builds registry based on mode
ToolRegistry make_registry(AgentMode mode, const Config& cfg);

// Read-only tools (registered in all modes)
ToolResult tool_read_file   (const ToolArgs& args);
ToolResult tool_list_dir    (const ToolArgs& args);
ToolResult tool_search_files(const ToolArgs& args);
ToolResult tool_file_info   (const ToolArgs& args);

// Write tools (Act mode only)
ToolResult tool_write_file  (const ToolArgs& args);
ToolResult tool_create_dir  (const ToolArgs& args);
ToolResult tool_delete_file (const ToolArgs& args);
ToolResult tool_run_shell   (const ToolArgs& args);
