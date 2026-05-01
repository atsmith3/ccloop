#include "tools.h"
#include "mcp_client.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <limits>
#include <fnmatch.h>
#include <regex>
#include <set>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <chrono>

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
// Argument helpers
// ============================================================================

// Expand a leading ~ to $HOME (std::filesystem does not do this automatically).
static std::string expand_tilde(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

// Extract a required string argument; sets err and returns nullopt on failure.
static std::optional<std::string> arg_str(const ToolArgs& args, const std::string& key, std::string& err) {
    auto it = args.find(key);
    if (it == args.end()) { err = "Missing '" + key + "' argument"; return {}; }
    if (!it->second.is_string()) { err = "'" + key + "' must be a string"; return {}; }
    return it->second.as_string();
}

// Extract a required path argument, expanding ~ automatically.
static std::optional<std::string> arg_path(const ToolArgs& args, const std::string& key, std::string& err) {
    auto p = arg_str(args, key, err);
    if (!p) return {};
    return expand_tilde(*p);
}

// Extract an optional integer argument from string or number type.
static std::optional<int> arg_int(const ToolArgs& args, const std::string& key) {
    auto it = args.find(key);
    if (it == args.end()) return std::nullopt;
    if (it->second.is_number()) return static_cast<int>(it->second.as_number());
    if (it->second.is_string()) {
        try { return std::stoi(it->second.as_string()); } catch (...) {}
    }
    return std::nullopt;
}

// ============================================================================
// Tool implementations
// ============================================================================

ToolResult tool_read_file(const ToolArgs& args) {
    std::string err;
    auto path = arg_path(args, "path", err);
    if (!path) return ToolResult::fail(err);

    std::ifstream file(*path);
    if (!file) return ToolResult::fail("File not found: " + *path);

    auto offset_opt = arg_int(args, "offset");
    auto limit_opt  = arg_int(args, "limit");

    if (!offset_opt && !limit_opt) {
        std::ostringstream ss;
        ss << file.rdbuf();
        return ToolResult::ok(ss.str());
    }

    int start_line = offset_opt.value_or(1);
    int max_lines  = limit_opt.value_or(std::numeric_limits<int>::max());
    if (start_line < 1) start_line = 1;

    std::ostringstream ss;
    std::string line;
    int line_no = 0, lines_written = 0;
    while (lines_written < max_lines && std::getline(file, line)) {
        ++line_no;
        if (line_no < start_line) continue;
        ss << line << "\n";
        ++lines_written;
    }
    return ToolResult::ok(ss.str());
}

ToolResult tool_list_dir(const ToolArgs& args) {
    std::string err;
    auto path = arg_path(args, "path", err);
    if (!path) return ToolResult::fail(err);

    try {
        if (!fs::exists(*path)) return ToolResult::fail("Directory not found: " + *path);

        std::ostringstream ss;
        bool first = true;
        for (const auto& entry : fs::directory_iterator(*path)) {
            if (!first) ss << "\n";
            ss << entry.path().filename().string();
            first = false;
        }
        if (first) return ToolResult::ok("(directory is empty)");
        return ToolResult::ok(ss.str());
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_search_files(const ToolArgs& args) {
    std::string err;
    auto path = arg_path(args, "path", err); if (!path) return ToolResult::fail(err);

    // Optional content-search pattern
    std::string pattern_str;
    auto pat_val = args.find("pattern");
    if (pat_val != args.end() && pat_val->second.is_string()) {
        pattern_str = pat_val->second.as_string();
    }

    // Optional glob filter (e.g., "*.cpp", "*.h")
    std::string file_glob;
    auto glob_val = args.find("file_glob");
    if (glob_val != args.end() && glob_val->second.is_string()) {
        file_glob = glob_val->second.as_string();
    }

    try {
        if (!fs::exists(*path)) return ToolResult::fail("Path not found: " + *path);

        // Compile regex once before iterating files
        std::regex re;
        if (!pattern_str.empty()) {
            try {
                re = std::regex(pattern_str);
            } catch (const std::regex_error& e) {
                return ToolResult::fail(std::string("invalid pattern: ") + e.what());
            }
        }

        std::ostringstream ss;
        bool found_any = false;

        // Common non-project directories to skip (in addition to hidden dirs)
        static const std::set<std::string> skip_dirs = {
            "venv", "env", ".venv", "node_modules", "__pycache__",
            "dist", "build", ".tox", ".eggs", "site-packages"
        };

        // Use explicit iterator so we can skip hidden directories (.git, etc.)
        bool truncated = false;
        for (auto it = fs::recursive_directory_iterator(*path);
             it != fs::recursive_directory_iterator(); ++it) {
            // Skip hidden directories and common non-project dirs
            if (it->is_directory()) {
                const std::string& dname = it->path().filename().string();
                if (!dname.empty() && (dname[0] == '.' || skip_dirs.count(dname))) {
                    it.disable_recursion_pending();
                    continue;
                }
            }
            if (!it->is_regular_file()) continue;

            // Apply optional glob filter
            if (!file_glob.empty()) {
                std::string fname = it->path().filename().string();
                if (fnmatch(file_glob.c_str(), fname.c_str(), 0) != 0) continue;
            }

            if (pattern_str.empty()) {
                // No pattern: list file paths only
                if (found_any) ss << "\n";
                ss << it->path().string();
                found_any = true;
            } else {
                // Pattern present: search file contents
                std::ifstream file(it->path());
                if (!file) continue;

                std::string line;
                int line_no = 0;
                while (std::getline(file, line)) {
                    ++line_no;
                    if (line.size() > 2048) continue;  // prevent catastrophic backtracking
                    if (std::regex_search(line, re)) {
                        if (found_any) ss << "\n";
                        ss << it->path().string() << ":" << line_no << ": " << line;
                        found_any = true;
                    }
                }
            }

            // Cap output at 8000 bytes to keep context manageable
            if (ss.tellp() > 8000) {
                truncated = true;
                break;
            }
        }

        if (truncated) {
            ss << "\n[... results truncated — use file_glob or a narrower path to refine]";
        }

        return ToolResult::ok(ss.str());
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_file_info(const ToolArgs& args) {
    std::string err;
    auto path = arg_path(args, "path", err);
    if (!path) return ToolResult::fail(err);

    try {
        if (!fs::exists(*path)) return ToolResult::ok("exists: false");

        std::ostringstream ss;
        ss << "exists: true\n";
        ss << "is_file: " << (fs::is_regular_file(*path) ? "true" : "false") << "\n";
        ss << "is_dir: "  << (fs::is_directory(*path)    ? "true" : "false") << "\n";

        if (fs::is_regular_file(*path)) {
            ss << "size: " << fs::file_size(*path) << " bytes\n";
        }

        auto mtime = fs::last_write_time(*path);
        // fix #2: use static_cast instead of reinterpret_cast (UB)
        time_t t = static_cast<time_t>(
            std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count());
        ss << "modified: " << std::ctime(&t);

        return ToolResult::ok(ss.str());
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

// ============================================================================
// Write tool implementations
// ============================================================================

// Helper: write content atomically via a .ccl.tmp file; returns error string on failure.
static std::string atomic_write(const std::string& path, const std::string& content) {
    std::string tmp_path = path + ".ccl." + std::to_string(getpid()) + ".tmp";
    {
        std::ofstream tmp_file(tmp_path);
        if (!tmp_file) return "Failed to create temporary file: " + tmp_path;
        tmp_file << content;
        if (!tmp_file.good()) return "Failed to write to temporary file: " + tmp_path;
    }
    try {
        fs::rename(tmp_path, path);
    } catch (const std::exception& e) {
        try { fs::remove(tmp_path); } catch (...) {}
        return "Failed to rename temporary file: " + std::string(e.what());
    }
    return "";
}

// Helper: Generate a unified diff (simple line-based)
static std::string generate_diff(const std::string& old_content, const std::string& new_content, const std::string& path) {
    auto split_lines = [](const std::string& s) {
        std::vector<std::string> lines;
        std::istringstream iss(s);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
        return lines;
    };

    std::vector<std::string> old_lines = split_lines(old_content);
    std::vector<std::string> new_lines = split_lines(new_content);

    if (old_lines.size() > 500 || new_lines.size() > 500) {
        return "content changed: " + std::to_string(old_content.size()) + " bytes -> " +
               std::to_string(new_content.size()) + " bytes";
    }

    std::ostringstream diff;
    diff << "--- " << path << " (old)\n";
    diff << "+++ " << path << " (new)\n";
    diff << "@@ -1," << old_lines.size() << " +1," << new_lines.size() << " @@\n";

    size_t old_idx = 0, new_idx = 0;
    while (old_idx < old_lines.size() || new_idx < new_lines.size()) {
        if (old_idx < old_lines.size() && new_idx < new_lines.size() &&
            old_lines[old_idx] == new_lines[new_idx]) {
            diff << " " << old_lines[old_idx] << "\n";
            ++old_idx; ++new_idx;
        } else if (old_idx < old_lines.size() &&
                   (new_idx >= new_lines.size() || old_lines[old_idx] != new_lines[new_idx])) {
            diff << "-" << old_lines[old_idx++] << "\n";
        } else {
            diff << "+" << new_lines[new_idx++] << "\n";
        }
    }
    return diff.str();
}

ToolResult tool_write_file(const ToolArgs& args) {
    std::string err;
    auto path        = arg_path(args, "path",    err); if (!path)        return ToolResult::fail(err);
    auto new_content = arg_str (args, "content", err); if (!new_content) return ToolResult::fail(err);

    try {
        std::string old_content;
        if (fs::exists(*path)) {
            std::ifstream file(*path);
            if (file) { std::ostringstream ss; ss << file.rdbuf(); old_content = ss.str(); }
        }

        if (old_content == *new_content)
            return ToolResult::ok("(no changes) " + *path + " already has identical content.");

        std::string diff = generate_diff(old_content, *new_content, *path);
        std::string write_err = atomic_write(*path, *new_content);
        if (!write_err.empty()) return ToolResult::fail(write_err);
        return ToolResult::ok("Written: " + *path + "\n" + diff);
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_edit_file(const ToolArgs& args) {
    std::string err;
    auto path    = arg_path(args, "path",    err); if (!path)    return ToolResult::fail(err);
    auto old_str = arg_str (args, "old_str", err); if (!old_str) return ToolResult::fail(err);
    auto new_str = arg_str (args, "new_str", err); if (!new_str) return ToolResult::fail(err);

    try {
        if (!fs::exists(*path)) return ToolResult::fail("File not found: " + *path);

        std::ifstream in(*path);
        if (!in) return ToolResult::fail("Cannot open file: " + *path);
        std::ostringstream ss; ss << in.rdbuf();
        std::string content = ss.str();

        size_t pos = content.find(*old_str);
        if (pos == std::string::npos) return ToolResult::fail("old_str not found in file");
        if (content.find(*old_str, pos + 1) != std::string::npos)
            return ToolResult::fail("old_str is ambiguous (appears more than once)");

        std::string new_content = content.substr(0, pos) + *new_str + content.substr(pos + old_str->size());
        std::string diff = generate_diff(content, new_content, *path);
        std::string write_err = atomic_write(*path, new_content);
        if (!write_err.empty()) return ToolResult::fail(write_err);
        return ToolResult::ok("Edited: " + *path + "\n" + diff);
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_create_dir(const ToolArgs& args) {
    std::string err;
    auto path = arg_path(args, "path", err);
    if (!path) return ToolResult::fail(err);

    try {
        if (fs::exists(*path)) {
            if (fs::is_directory(*path)) return ToolResult::ok("already exists: " + *path);
            return ToolResult::fail("path exists but is not a directory: " + *path);
        }
        fs::create_directories(*path);
        return ToolResult::ok("created: " + *path);
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_delete_file(const ToolArgs& args) {
    std::string err;
    auto path = arg_path(args, "path", err);
    if (!path) return ToolResult::fail(err);

    try {
        if (!fs::exists(*path)) return ToolResult::fail("not found: " + *path);
        if (!fs::is_regular_file(*path)) return ToolResult::fail("not a regular file: " + *path);
        fs::remove(*path);
        return ToolResult::ok("deleted: " + *path);
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_delete_dir(const ToolArgs& args) {
    std::string err;
    auto path = arg_path(args, "path", err);
    if (!path) return ToolResult::fail(err);

    bool recursive = false;
    auto rec_it = args.find("recursive");
    if (rec_it != args.end()) {
        if (rec_it->second.is_bool()) recursive = rec_it->second.as_bool();
        else if (rec_it->second.is_string()) recursive = (rec_it->second.as_string() == "true");
    }

    try {
        if (!fs::exists(*path)) return ToolResult::fail("not found: " + *path);
        if (!fs::is_directory(*path)) return ToolResult::fail("not a directory: " + *path);
        if (!recursive && !fs::is_empty(*path))
            return ToolResult::fail("directory is not empty — set recursive=true to delete recursively");
        uintmax_t removed = fs::remove_all(*path);
        return ToolResult::ok("deleted: " + *path + " (" + std::to_string(removed) + " entries removed)");
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }
}

ToolResult tool_find_symbol(const ToolArgs& args) {
    std::string err;
    auto symbol_opt = arg_str(args, "symbol", err);
    if (!symbol_opt) return ToolResult::fail(err);
    const std::string& symbol = *symbol_opt;

    // Map language name to glob(s)
    std::string file_glob = "*";
    auto lang_it = args.find("language");
    if (lang_it != args.end() && lang_it->second.is_string()) {
        std::string lang = lang_it->second.as_string();
        if (lang == "cpp" || lang == "c++")          file_glob = "*.cpp;*.h;*.cc;*.cxx";
        else if (lang == "c")                        file_glob = "*.c;*.h";
        else if (lang == "python" || lang == "py")   file_glob = "*.py";
        else if (lang == "rust")                     file_glob = "*.rs";
        else if (lang == "go")                       file_glob = "*.go";
        else if (lang == "javascript" || lang == "js") file_glob = "*.js;*.mjs;*.cjs";
        else if (lang == "typescript" || lang == "ts") file_glob = "*.ts;*.tsx";
    }

    std::string search_path = ".";
    auto path_it = args.find("path");
    if (path_it != args.end() && path_it->second.is_string())
        search_path = expand_tilde(path_it->second.as_string());

    std::regex re;
    try {
        re = std::regex("\\b" + symbol + "\\b");
    } catch (const std::regex_error& e) {
        return ToolResult::fail("invalid symbol: " + std::string(e.what()));
    }

    std::vector<std::string> globs;
    {
        std::istringstream iss(file_glob);
        std::string g;
        while (std::getline(iss, g, ';'))
            if (!g.empty()) globs.push_back(g);
    }

    static const std::set<std::string> skip_dirs = {
        "venv", "env", ".venv", "node_modules", "__pycache__",
        "dist", "build", ".tox", ".eggs", "site-packages"
    };

    std::ostringstream ss;
    bool found_any = false, truncated = false;
    try {
        for (auto it = fs::recursive_directory_iterator(search_path);
             it != fs::recursive_directory_iterator(); ++it) {
            if (it->is_directory()) {
                const std::string& dname = it->path().filename().string();
                if (!dname.empty() && (dname[0] == '.' || skip_dirs.count(dname))) {
                    it.disable_recursion_pending();
                    continue;
                }
            }
            if (!it->is_regular_file()) continue;
            std::string fname = it->path().filename().string();
            bool matches = false;
            for (const auto& g : globs) {
                if (fnmatch(g.c_str(), fname.c_str(), 0) == 0) { matches = true; break; }
            }
            if (!matches) continue;

            std::ifstream file(it->path());
            if (!file) continue;
            std::string line;
            int line_no = 0;
            while (std::getline(file, line)) {
                ++line_no;
                if (line.size() > 2048) continue;
                if (std::regex_search(line, re)) {
                    if (found_any) ss << "\n";
                    ss << it->path().string() << ":" << line_no << ": " << line;
                    found_any = true;
                }
            }
            if (ss.tellp() > 8000) { truncated = true; break; }
        }
    } catch (const std::exception& e) {
        return ToolResult::fail(std::string(e.what()));
    }

    if (!found_any) return ToolResult::ok("(no matches for '" + symbol + "')");
    if (truncated) ss << "\n[... truncated — narrow with path= or language=]";
    return ToolResult::ok(ss.str());
}

ToolResult tool_run_shell(const ToolArgs& args) {
    std::string err;
    auto command_opt = arg_str(args, "command", err);
    if (!command_opt) return ToolResult::fail(err);
    std::string command = *command_opt;

    // Get optional cwd
    std::string cwd;
    auto cwd_val = args.find("cwd");
    if (cwd_val != args.end() && cwd_val->second.is_string()) {
        cwd = cwd_val->second.as_string();
    }

    // Get optional timeout (default 30 seconds)
    int timeout_sec = 30;
    auto timeout_opt = arg_int(args, "timeout_sec");
    if (timeout_opt) timeout_sec = *timeout_opt;

    // Create pipe for stdout/stderr
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return ToolResult::fail("Failed to create pipe");
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return ToolResult::fail("Failed to fork");
    }

    if (pid == 0) {
        // Child process: put in its own process group so kill(-pgid) kills
        // the shell and all grandchildren (e.g. sleep spawned by sh -c)
        setpgid(0, 0);

        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (!cwd.empty()) {
            if (chdir(cwd.c_str()) != 0) {
                _exit(126);  // cwd failed
            }
        }

        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);  // exec failed
    }

    // Parent process
    close(pipefd[1]);

    // Read output with timeout
    std::ostringstream output;
    std::vector<char> buf(4096);
    auto start_time = std::chrono::steady_clock::now();
    int status = 0;
    bool timed_out = false;
    bool exited = false;

    while (!exited) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_sec) {
            timed_out = true;
            kill(-pid, SIGKILL);  // kill entire process group (shell + grandchildren)
            waitpid(pid, &status, 0);
            // Drain remaining buffered output before closing the pipe
            {
                ssize_t bytes;
                while ((bytes = read(pipefd[0], buf.data(), buf.size())) > 0) {
                    output.write(buf.data(), bytes);
                }
            }
            break;
        }

        // Non-blocking check for child exit
        int wait_status = 0;
        pid_t wait_result = waitpid(pid, &wait_status, WNOHANG);
        if (wait_result == pid) {
            status = wait_status;
            exited = true;
        }

        // Try to read data (with timeout)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pipefd[0], &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms

        int select_result = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &tv);
        if (select_result > 0 && FD_ISSET(pipefd[0], &readfds)) {
            ssize_t bytes = read(pipefd[0], buf.data(), buf.size());
            if (bytes > 0) {
                output.write(buf.data(), bytes);
            } else if (bytes == 0) {
                // EOF: wait for process to complete and get exit status
                waitpid(pid, &status, 0);
                exited = true;
            }
        }
    }

    close(pipefd[0]);

    if (timed_out) {
        std::string captured = output.str();
        std::string msg = "timeout after " + std::to_string(timeout_sec) + " seconds";
        if (!captured.empty()) {
            msg += "\n[output before timeout]\n" + captured;
        }
        return ToolResult::fail(msg);
    }

    if (!exited) {
        return ToolResult::fail("command did not complete");
    }

    int exit_code = -1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }

    if (exit_code != 0) {
        return ToolResult::fail("exit " + std::to_string(exit_code) + ": " + output.str());
    }

    return ToolResult::ok(output.str());
}

// ============================================================================
// spawn_agent tool
// ============================================================================

static std::string shell_quote(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else           result += c;
    }
    return result + "'";
}

ToolResult tool_spawn_agent(const ToolArgs& args, const std::string& config_path) {
    auto prompt_it = args.find("prompt");
    if (prompt_it == args.end() || !prompt_it->second.is_string()) {
        return ToolResult::fail("spawn_agent: 'prompt' parameter is required");
    }
    std::string prompt = prompt_it->second.as_string();

    std::string mode = "act";
    auto mode_it = args.find("mode");
    if (mode_it != args.end() && mode_it->second.is_string()) {
        std::string m = mode_it->second.as_string();
        if (m == "plan" || m == "act") mode = m;
    }

    // Resolve own binary path via /proc/self/exe
    char self_path[4096];
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (len < 0) return ToolResult::fail("spawn_agent: cannot determine binary path");
    self_path[len] = '\0';

    std::string cmd = std::string(self_path) + " -p " + shell_quote(prompt)
                    + " -m " + mode + " -y";
    if (!config_path.empty()) {
        cmd += " --config " + shell_quote(config_path);
    }

    ToolArgs shell_args;
    { JsonValue v; v.data = std::string(cmd); shell_args["command"] = v; }

    auto cwd_it = args.find("working_dir");
    if (cwd_it != args.end() && cwd_it->second.is_string()) {
        shell_args["cwd"] = cwd_it->second;
    }

    auto timeout_it = args.find("timeout_sec");
    if (timeout_it != args.end()) {
        shell_args["timeout_sec"] = timeout_it->second;
    } else {
        JsonValue v; v.data = static_cast<double>(600);
        shell_args["timeout_sec"] = v;
    }

    return tool_run_shell(shell_args);
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
        tool.def.description = "Read the contents of a file. Use offset/limit to read a specific line range.";
        tool.def.params.push_back({"path",   "string",  "Path to file",                                    true});
        tool.def.params.push_back({"offset", "integer", "1-based first line to read (optional)",           false});
        tool.def.params.push_back({"limit",  "integer", "Maximum number of lines to return (optional)",    false});
        tool.def.permission = "read";
        tool.fn = tool_read_file;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    {
        Tool tool;
        tool.def.name = "list_dir";
        tool.def.description = "List files and directories in a directory";
        tool.def.params.push_back({"path", "string", "Path to directory", true});
        tool.def.permission = "read";
        tool.fn = tool_list_dir;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    {
        Tool tool;
        tool.def.name = "search_files";
        tool.def.description = "List files or search file contents by regex (skips hidden dirs like .git). Omit 'pattern' to list matching files only.";
        tool.def.params.push_back({"path", "string", "Root path to search", true});
        tool.def.params.push_back({"pattern", "string", "Regex to search file contents (optional — omit to list files)", false});
        tool.def.params.push_back({"file_glob", "string", "Optional glob filter, e.g. '*.py'", false});
        tool.def.permission = "read";
        tool.fn = tool_search_files;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    {
        Tool tool;
        tool.def.name = "file_info";
        tool.def.description = "Get metadata about a file or directory";
        tool.def.params.push_back({"path", "string", "Path to file or directory", true});
        tool.def.permission = "read";
        tool.fn = tool_file_info;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    {
        Tool tool;
        tool.def.name = "find_symbol";
        tool.def.description =
            "Search for a symbol (function, class, variable) by name across source files. "
            "Returns file:line matches with word-boundary matching. Use language= to restrict "
            "to a specific file type (cpp, python, rust, go, javascript, typescript).";
        tool.def.params.push_back({"symbol",   "string", "Symbol name to search for",                          true});
        tool.def.params.push_back({"language", "string", "Language filter: cpp, python, rust, go, js, ts",     false});
        tool.def.params.push_back({"path",     "string", "Root path to search (default: current directory)",   false});
        tool.def.permission = "read";
        tool.fn = tool_find_symbol;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    // present_plan: Plan mode only
    if (mode == AgentMode::Plan) {
        Tool tool;
        tool.def.name = "present_plan";
        tool.def.description =
            "Present the completed plan to the user for approval. "
            "The user will accept (proceed to execution), request refinements, "
            "or reject the plan. Call this when the plan is fully formed.";
        tool.def.params.push_back({"plan", "string",
            "The complete numbered plan text to present to the user", true});
        tool.def.permission = "read";
        tool.fn = [](const ToolArgs&) { return ToolResult::ok(""); };
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    // print: both modes — explicit output to user / parent agent
    {
        Tool tool;
        tool.def.name = "print";
        tool.def.description =
            "Print a message to the user or parent agent. Use sparingly — only for "
            "important findings, key decisions, or final answers. Do not use for "
            "routine step announcements.";
        tool.def.params.push_back({"message", "string", "The message to display", true});
        tool.def.permission = "read";
        tool.fn = [](const ToolArgs&) { return ToolResult::ok(""); };
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    // Write tools (Act mode only)
    if (mode == AgentMode::Act) {
        {
            Tool tool;
            tool.def.name = "write_file";
            tool.def.description = "Write content to a file (atomic via temporary file)";
            tool.def.params.push_back({"path", "string", "Path to file", true});
            tool.def.params.push_back({"content", "string", "Content to write", true});
            tool.def.permission = "write";
            tool.fn = tool_write_file;
            tool.source = ToolSource::Local;
            registry.register_tool(std::move(tool));
        }

        {
            Tool tool;
            tool.def.name = "edit_file";
            tool.def.description = "Replace an exact string in a file (old_str must appear exactly once)";
            tool.def.params.push_back({"path",    "string", "Path to file",                    true});
            tool.def.params.push_back({"old_str", "string", "Exact text to replace",           true});
            tool.def.params.push_back({"new_str", "string", "Replacement text",                true});
            tool.def.permission = "write";
            tool.fn = tool_edit_file;
            tool.source = ToolSource::Local;
            registry.register_tool(std::move(tool));
        }

        {
            Tool tool;
            tool.def.name = "create_dir";
            tool.def.description = "Create a directory (creates parent directories as needed)";
            tool.def.params.push_back({"path", "string", "Path to directory", true});
            tool.def.permission = "write";
            tool.fn = tool_create_dir;
            tool.source = ToolSource::Local;
            registry.register_tool(std::move(tool));
        }

        {
            Tool tool;
            tool.def.name = "delete_file";
            tool.def.description = "Delete a file (requires approval)";
            tool.def.params.push_back({"path", "string", "Path to file", true});
            tool.def.permission = "delete";
            tool.fn = tool_delete_file;
            tool.source = ToolSource::Local;
            registry.register_tool(std::move(tool));
        }

        {
            Tool tool;
            tool.def.name = "delete_dir";
            tool.def.description = "Delete a directory. Set recursive=true to delete non-empty directories.";
            tool.def.params.push_back({"path",      "string",  "Path to directory",                              true});
            tool.def.params.push_back({"recursive", "boolean", "Delete non-empty directories recursively",       false});
            tool.def.permission = "delete";
            tool.fn = tool_delete_dir;
            tool.source = ToolSource::Local;
            registry.register_tool(std::move(tool));
        }
    }

    // run_shell is available in all modes (read-only exploration, build checks, etc.)
    {
        Tool tool;
        tool.def.name = "run_shell";
        tool.def.description = "Execute a shell command (requires approval)";
        tool.def.params.push_back({"command", "string", "Shell command to execute", true});
        tool.def.params.push_back({"cwd", "string", "Working directory (optional)", false});
        tool.def.params.push_back({"timeout_sec", "integer", "Timeout in seconds (optional, default 30)", false});
        tool.def.permission = "shell";
        tool.fn = tool_run_shell;
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    // spawn_agent: invoke a fresh ccl instance as a focused sub-agent
    {
        Tool tool;
        tool.def.name = "spawn_agent";
        tool.def.description =
            "Spawn a sub-agent of ccl to handle a focused task. The sub-agent runs with full "
            "tool access and returns all its output. Use for bounded research, exploration, or "
            "investigation tasks that benefit from isolation.";
        tool.def.params.push_back({"prompt",      "string",  "Task description for the sub-agent",                  true});
        tool.def.params.push_back({"mode",        "string",  "Agent mode: 'act' (default) or 'plan'",              false});
        tool.def.params.push_back({"working_dir", "string",  "Working directory for the sub-agent (optional)",     false});
        tool.def.params.push_back({"timeout_sec", "integer", "Timeout in seconds (optional, default 600)",         false});
        tool.def.permission = "shell";
        tool.fn = [config_path = cfg.config_path](const ToolArgs& args) {
            return tool_spawn_agent(args, config_path);
        };
        tool.source = ToolSource::Local;
        registry.register_tool(std::move(tool));
    }

    // Register MCP tools from configured servers
    for (const auto& server_cfg : cfg.mcp_servers) {
        auto client = std::make_shared<McpClient>(server_cfg, cfg);
        if (!client->initialize()) {
            std::cerr << "[mcp] warning: could not connect to '" << server_cfg.name << "'\n";
            continue;
        }
        for (auto def : client->list_tools()) {
            def.permission = server_cfg.write_tools.count(def.name) ? "write" : "read";
            Tool tool;
            tool.def       = def;
            tool.source    = ToolSource::Mcp;
            tool.mcp_server = server_cfg.name;
            tool.fn = [client, name = def.name](const ToolArgs& args) {
                return client->call_tool(name, args);
            };
            registry.register_tool(std::move(tool));
        }
    }

    return registry;
}
