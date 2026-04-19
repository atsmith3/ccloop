#include "tools.h"
#include <fstream>
#include <filesystem>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

// ============================================================================
// ToolResult implementation
// ============================================================================

ToolResult ToolResult::ok(std::string content) {
    return ToolResult{true, std::move(content), ""};
}

ToolResult ToolResult::fail(std::string error) {
    return ToolResult{false, "", std::move(error)};
}

std::string ToolResult::to_context_string() const {
    if (success) {
        return content;
    } else {
        return "[ERROR] " + error;
    }
}

// ============================================================================
// ToolRegistry implementation
// ============================================================================

void ToolRegistry::register_tool(Tool tool) {
    index_[tool.def.name] = tools_.size();
    tools_.push_back(std::move(tool));
}

std::vector<ToolDef> ToolRegistry::definitions() const {
    std::vector<ToolDef> defs;
    for (const auto& tool : tools_) {
        defs.push_back(tool.def);
    }
    return defs;
}

std::optional<const Tool*> ToolRegistry::find(const std::string& name) const {
    auto it = index_.find(name);
    if (it == index_.end()) return std::nullopt;
    return &tools_[it->second];
}

// ============================================================================
// Tool implementations
// ============================================================================

ToolResult tool_read_file(const ToolArgs& args) {
    auto path_val = args.find("path");
    if (path_val == args.end()) {
        return ToolResult::fail("Missing 'path' argument");
    }

    if (!path_val->second.is_string()) {
        return ToolResult::fail("'path' must be a string");
    }

    std::string path = path_val->second.as_string();

    std::ifstream file(path);
    if (!file) {
        return ToolResult::fail("File not found: " + path);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ToolResult::ok(ss.str());
}

ToolResult tool_list_dir(const ToolArgs& args) {
    auto path_val = args.find("path");
    if (path_val == args.end()) {
        return ToolResult::fail("Missing 'path' argument");
    }

    if (!path_val->second.is_string()) {
        return ToolResult::fail("'path' must be a string");
    }

    std::string path = path_val->second.as_string();

    try {
        if (!fs::exists(path)) {
            return ToolResult::fail("Directory not found: " + path);
        }

        std::ostringstream ss;
        bool first = true;
        for (const auto& entry : fs::directory_iterator(path)) {
            if (!first) ss << "\n";
            ss << entry.path().filename().string();
            first = false;
        }

        return ToolResult::ok(ss.str());
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_search_files(const ToolArgs& args) {
    auto path_val = args.find("path");
    auto pattern_val = args.find("pattern");

    if (path_val == args.end() || pattern_val == args.end()) {
        return ToolResult::fail("Missing 'path' or 'pattern' argument");
    }

    if (!path_val->second.is_string() || !pattern_val->second.is_string()) {
        return ToolResult::fail("'path' and 'pattern' must be strings");
    }

    std::string path = path_val->second.as_string();
    std::string pattern = pattern_val->second.as_string();

    try {
        if (!fs::exists(path)) {
            return ToolResult::fail("Path not found: " + path);
        }

        std::regex re(pattern);
        std::ostringstream ss;
        bool found_any = false;

        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (!fs::is_regular_file(entry)) continue;

            std::ifstream file(entry.path());
            if (!file) continue;

            std::string line;
            int line_no = 0;
            while (std::getline(file, line)) {
                ++line_no;
                if (std::regex_search(line, re)) {
                    if (found_any) ss << "\n";
                    ss << entry.path().string() << ":" << line_no << ": " << line;
                    found_any = true;
                }
            }
        }

        return ToolResult::ok(ss.str());
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_file_info(const ToolArgs& args) {
    auto path_val = args.find("path");
    if (path_val == args.end()) {
        return ToolResult::fail("Missing 'path' argument");
    }

    if (!path_val->second.is_string()) {
        return ToolResult::fail("'path' must be a string");
    }

    std::string path = path_val->second.as_string();

    try {
        if (!fs::exists(path)) {
            return ToolResult::ok("exists: false");
        }

        std::ostringstream ss;
        ss << "exists: true\n";
        ss << "is_file: " << (fs::is_regular_file(path) ? "true" : "false") << "\n";
        ss << "is_dir: " << (fs::is_directory(path) ? "true" : "false") << "\n";

        if (fs::is_regular_file(path)) {
            ss << "size: " << fs::file_size(path) << " bytes\n";
        }

        auto mtime = fs::last_write_time(path);
        auto time_t_val = std::chrono::duration_cast<std::chrono::seconds>(
            mtime.time_since_epoch()).count();
        ss << "modified: " << std::ctime(reinterpret_cast<time_t*>(&time_t_val));

        return ToolResult::ok(ss.str());
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

// ============================================================================
// Registry factory
// ============================================================================

ToolRegistry make_registry(AgentMode mode, const Config& cfg) {
    ToolRegistry registry;

    // Read-only tools: always registered
    {
        Tool tool;
        tool.def.name = "read_file";
        tool.def.description = "Read the contents of a file";
        tool.def.params.push_back({"path", "string", "Path to file", true});
        tool.fn = tool_read_file;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    {
        Tool tool;
        tool.def.name = "list_dir";
        tool.def.description = "List files and directories in a directory";
        tool.def.params.push_back({"path", "string", "Path to directory", true});
        tool.fn = tool_list_dir;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    {
        Tool tool;
        tool.def.name = "search_files";
        tool.def.description = "Search files for a regex pattern";
        tool.def.params.push_back({"path", "string", "Root path to search", true});
        tool.def.params.push_back({"pattern", "string", "Regex pattern to match", true});
        tool.fn = tool_search_files;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    {
        Tool tool;
        tool.def.name = "file_info";
        tool.def.description = "Get metadata about a file or directory";
        tool.def.params.push_back({"path", "string", "Path to file or directory", true});
        tool.fn = tool_file_info;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    // Write tools stubbed (Milestone 3)
    // In Act mode, these would be registered
    // For now, they're not registered

    return registry;
}
