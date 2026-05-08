#include "mcp_client.h"
#include "signals.h"
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <curl/curl.h>
#include <curl/multi.h>

// ============================================================================
// McpClient static protocol helpers (transport-agnostic, all tests use these)
// ============================================================================

// Build a JSON-RPC 2.0 request body. Omits "params" when params.is_null().
std::string McpClient::build_rpc(int id, const std::string& method, const JsonValue& params) {
    std::ostringstream ss;
    ss << "{\"jsonrpc\":\"2.0\",\"id\":" << id
       << ",\"method\":\"" << escape_json(method) << "\"";
    if (!params.is_null()) {
        ss << ",\"params\":" << to_json(params);
    }
    ss << "}";
    return ss.str();
}

// Build a JSON-RPC 2.0 notification (no id, no response expected).
std::string McpClient::build_notification(const std::string& method) {
    return "{\"jsonrpc\":\"2.0\",\"method\":\"" + escape_json(method) + "\"}";
}

// Parse a response body as JSON or SSE (text/event-stream).
// For SSE, returns the first valid data event. Returns null JsonValue on failure.
JsonValue McpClient::parse_sse_or_json(const std::string& body, const std::string& content_type) {
    if (content_type.find("text/event-stream") != std::string::npos) {
        std::istringstream stream(body);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() >= 6 && line.substr(0, 6) == "data: ") {
                std::string data = line.substr(6);
                if (!data.empty() && data != "[DONE]") {
                    try { return parse_json(data); } catch (...) {}
                }
            }
        }
        return JsonValue{};
    }
    try { return parse_json(body); } catch (...) { return JsonValue{}; }
}

std::vector<ToolDef> McpClient::parse_tools_list(const JsonValue& result) {
    auto tools_v = result.get("tools");
    if (!tools_v || !tools_v->is_array()) return {};

    std::vector<ToolDef> defs;
    for (const auto& tool_ptr : tools_v->as_array()) {
        if (!tool_ptr) continue;

        auto name_v = tool_ptr->get("name");
        if (!name_v || !name_v->is_string()) continue;

        ToolDef def;
        def.name = name_v->as_string();

        auto desc_v = tool_ptr->get("description");
        if (desc_v && desc_v->is_string()) def.description = desc_v->as_string();

        auto schema_v = tool_ptr->get("inputSchema");
        if (schema_v) {
            std::unordered_set<std::string> required_set;
            auto req_v = schema_v->get("required");
            if (req_v && req_v->is_array()) {
                for (const auto& r : req_v->as_array()) {
                    if (r && r->is_string()) required_set.insert(r->as_string());
                }
            }

            auto props_v = schema_v->get("properties");
            if (props_v && props_v->is_object()) {
                for (const auto& [pname, pval] : props_v->as_object()) {
                    if (!pval) continue;
                    ToolParam param;
                    param.name     = pname;
                    param.required = required_set.count(pname) > 0;

                    auto ptype = pval->get("type");
                    param.type = (ptype && ptype->is_string()) ? ptype->as_string() : "string";

                    auto pdesc = pval->get("description");
                    param.description = (pdesc && pdesc->is_string()) ? pdesc->as_string() : "";

                    def.params.push_back(std::move(param));
                }
            }
        }

        defs.push_back(std::move(def));
    }
    return defs;
}

ToolResult McpClient::parse_call_result(const JsonValue& result) {
    auto is_error_v = result.get("isError");
    bool is_error = (is_error_v && is_error_v->is_bool() && is_error_v->as_bool());

    std::string output;
    auto content_v = result.get("content");
    if (content_v && content_v->is_array()) {
        for (const auto& item_ptr : content_v->as_array()) {
            if (!item_ptr) continue;
            auto text_v = item_ptr->get("text");
            if (text_v && text_v->is_string()) output += text_v->as_string();
        }
    }

    return is_error ? ToolResult::fail(output) : ToolResult::ok(output);
}

// ============================================================================
// HttpTransport — MCP Streamable HTTP (spec 2024-11-05+)
// ============================================================================

class HttpTransport : public McpTransport {
public:
    HttpTransport(const McpServerConfig& server, const Config& cfg)
        : server_(server), cfg_(cfg), curl_(curl_easy_init()) {}

    ~HttpTransport() {
        if (curl_) curl_easy_cleanup(curl_);
    }

    std::optional<JsonValue> send_rpc(int id, const std::string& method,
                                      const JsonValue& params) override {
        if (!curl_) return std::nullopt;
        curl_easy_reset(curl_);

        std::string body = McpClient::build_rpc(id, method, params);
        std::string response_body;
        struct curl_slist* headers = build_headers();
        std::string captured_session_id;

        curl_easy_setopt(curl_, CURLOPT_URL,            server_.url.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,      body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,   static_cast<long>(body.size()));
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,      headers);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,   write_callback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA,       static_cast<void*>(&response_body));
        curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION,  header_callback);
        curl_easy_setopt(curl_, CURLOPT_HEADERDATA,      static_cast<void*>(&captured_session_id));
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT,         static_cast<long>(cfg_.timeout_sec));
        curl_easy_setopt(curl_, CURLOPT_NOPROGRESS,      0L);
        curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, interrupt_cb);

        CURLcode res = curl_easy_perform(curl_);
        curl_slist_free_all(headers);
        if (!captured_session_id.empty()) session_id_ = captured_session_id;

        if (res != CURLE_OK) {
            if (cfg_.debug) {
                std::cerr << "[mcp:" << server_.name << "] curl error: "
                          << curl_easy_strerror(res) << "\n";
            }
            return std::nullopt;
        }

        long http_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code < 200 || http_code >= 300) {
            if (cfg_.debug) {
                std::cerr << "[mcp:" << server_.name << "] HTTP " << http_code
                          << ": " << response_body.substr(0, 200) << "\n";
            }
            return std::nullopt;
        }

        char* ct_ptr = nullptr;
        curl_easy_getinfo(curl_, CURLINFO_CONTENT_TYPE, &ct_ptr);
        std::string content_type = ct_ptr ? ct_ptr : "application/json";

        if (cfg_.debug) {
            std::cerr << "[mcp:" << server_.name << "] response: " << response_body << "\n";
        }

        JsonValue resp = McpClient::parse_sse_or_json(response_body, content_type);

        auto err_v = resp.get("error");
        if (err_v.has_value()) {
            if (cfg_.debug) {
                auto msg = err_v->get("message");
                std::cerr << "[mcp:" << server_.name << "] rpc error: "
                          << (msg && msg->is_string() ? msg->as_string() : "unknown") << "\n";
            }
            return std::nullopt;
        }

        auto result = resp.get("result");
        if (!result.has_value()) return std::nullopt;
        return *result;
    }

    void send_notification(const std::string& method) override {
        if (!curl_) return;
        curl_easy_reset(curl_);

        std::string body = McpClient::build_notification(method);
        std::string response_body;
        struct curl_slist* headers = build_headers();

        curl_easy_setopt(curl_, CURLOPT_URL,           server_.url.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,     body.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body.size()));
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER,     headers);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION,  write_callback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA,      static_cast<void*>(&response_body));
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT,        static_cast<long>(cfg_.timeout_sec));

        curl_easy_perform(curl_);  // fire-and-forget
        curl_slist_free_all(headers);
    }

private:
    static int interrupt_cb(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        return should_interrupt.load() ? 1 : 0;
    }

    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        size_t realsize = size * nmemb;
        static_cast<std::string*>(userdata)->append(ptr, realsize);
        return realsize;
    }

    static size_t header_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        size_t len = size * nmemb;
        std::string line(ptr, len);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const std::string prefix = "mcp-session-id:";
        std::string lower = line;
        for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (lower.size() >= prefix.size() && lower.substr(0, prefix.size()) == prefix) {
            std::string val = line.substr(prefix.size());
            size_t start = val.find_first_not_of(" \t");
            if (start != std::string::npos) val = val.substr(start);
            *static_cast<std::string*>(userdata) = val;
        }
        return len;
    }

    curl_slist* build_headers() const {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json, text/event-stream");
        if (!session_id_.empty()) {
            std::string sid = "Mcp-Session-Id: " + session_id_;
            headers = curl_slist_append(headers, sid.c_str());
        }
        if (!server_.api_key.empty()) {
            std::string auth = "Authorization: Bearer " + server_.api_key;
            headers = curl_slist_append(headers, auth.c_str());
        }
        return headers;
    }

    McpServerConfig server_;
    Config          cfg_;
    CURL*           curl_       = nullptr;
    std::string     session_id_;
};

// ============================================================================
// StdioTransport — MCP over subprocess stdin/stdout (line-delimited JSON-RPC)
// ============================================================================

class StdioTransport : public McpTransport {
public:
    StdioTransport(const McpServerConfig& server, const Config& cfg)
        : server_(server), cfg_(cfg) {
        int pipefd_in[2], pipefd_out[2];
        if (pipe(pipefd_in) != 0) return;
        if (pipe(pipefd_out) != 0) {
            close(pipefd_in[0]); close(pipefd_in[1]);
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd_in[0]);  close(pipefd_in[1]);
            close(pipefd_out[0]); close(pipefd_out[1]);
            return;
        }

        if (pid == 0) {
            // Child: connect pipes to stdin/stdout, exec the MCP server
            dup2(pipefd_in[0],  STDIN_FILENO);
            dup2(pipefd_out[1], STDOUT_FILENO);
            close(pipefd_in[0]);  close(pipefd_in[1]);
            close(pipefd_out[0]); close(pipefd_out[1]);
            setpgid(0, 0);  // own process group so destructor can kill the whole subtree
            execl("/bin/sh", "sh", "-c", server.command.c_str(), nullptr);
            _exit(127);
        }

        // Parent: keep write-end of in-pipe, read-end of out-pipe
        close(pipefd_in[0]);
        close(pipefd_out[1]);
        child_pid_ = pid;
        stdin_fd_  = pipefd_in[1];
        stdout_fd_ = pipefd_out[0];
    }

    ~StdioTransport() {
        if (stdin_fd_  != -1) { close(stdin_fd_);  stdin_fd_  = -1; }
        if (stdout_fd_ != -1) { close(stdout_fd_); stdout_fd_ = -1; }
        if (child_pid_ != -1) {
            int status;
            if (waitpid(child_pid_, &status, WNOHANG) == 0) {
                kill(-child_pid_, SIGTERM);  // kill entire process group
                struct timeval tv { 0, 500000 };
                select(0, nullptr, nullptr, nullptr, &tv);
                if (waitpid(child_pid_, &status, WNOHANG) == 0) {
                    kill(-child_pid_, SIGKILL);
                    waitpid(child_pid_, &status, 0);
                }
            }
            child_pid_ = -1;
        }
    }

    std::optional<JsonValue> send_rpc(int id, const std::string& method,
                                      const JsonValue& params) override {
        if (child_pid_ == -1 || stdin_fd_ == -1) return std::nullopt;

        std::string body = McpClient::build_rpc(id, method, params) + "\n";
        ssize_t total = 0;
        while (total < (ssize_t)body.size()) {
            ssize_t n = write(stdin_fd_, body.data() + total, body.size() - total);
            if (n <= 0) return std::nullopt;
            total += n;
        }

        // Read response lines until we find the one with matching id
        while (true) {
            std::string line = read_line();
            if (line.empty()) return std::nullopt;  // EOF or timeout

            JsonValue resp;
            try { resp = parse_json(line); } catch (...) { continue; }

            auto id_v = resp.get("id");
            if (!id_v || !id_v->is_number()) continue;  // notification or missing id
            if ((int)id_v->as_number() != id) continue;  // different request's response

            if (cfg_.debug) {
                std::cerr << "[mcp:" << server_.name << "] stdio response: " << line << "\n";
            }

            auto err_v = resp.get("error");
            if (err_v.has_value()) {
                if (cfg_.debug) {
                    auto msg_v = err_v->get("message");
                    std::cerr << "[mcp:" << server_.name << "] rpc error: "
                              << (msg_v && msg_v->is_string() ? msg_v->as_string() : "unknown") << "\n";
                }
                return std::nullopt;
            }

            auto result = resp.get("result");
            if (!result.has_value()) return std::nullopt;
            return *result;
        }
    }

    void send_notification(const std::string& method) override {
        if (stdin_fd_ == -1) return;
        std::string body = McpClient::build_notification(method) + "\n";
        ssize_t total = 0;
        while (total < (ssize_t)body.size()) {
            ssize_t n = write(stdin_fd_, body.data() + total, body.size() - total);
            if (n <= 0) return;
            total += n;
        }
    }

private:
    // Returns next complete line from stdout (strips \n, \r\n). Empty on timeout or EOF.
    std::string read_line() {
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::seconds(cfg_.timeout_sec);
        while (true) {
            auto pos = read_buf_.find('\n');
            if (pos != std::string::npos) {
                std::string line = read_buf_.substr(0, pos);
                read_buf_.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return line;
            }

            if (should_interrupt.load()) return "";

            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return "";

            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(stdout_fd_, &rfds);
            struct timeval tv;
            tv.tv_sec  = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            if (select(stdout_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return "";

            char buf[4096];
            ssize_t n = read(stdout_fd_, buf, sizeof(buf));
            if (n <= 0) return "";
            read_buf_.append(buf, (size_t)n);
        }
    }

    McpServerConfig server_;
    Config          cfg_;
    pid_t           child_pid_ = -1;
    int             stdin_fd_  = -1;
    int             stdout_fd_ = -1;
    std::string     read_buf_;
};

// ============================================================================
// LegacySseTransport — MCP legacy SSE (pre-2024-11-05 spec)
//
// The client maintains a persistent GET /sse stream (read by a background
// thread). Requests are POSTed to a URL announced in the first SSE event.
// Responses arrive as "message" events on the SSE stream; they are routed to
// waiting send_rpc callers by JSON-RPC id via a shared pending_ map.
// ============================================================================

class LegacySseTransport : public McpTransport {
public:
    LegacySseTransport(const McpServerConfig& server, const Config& cfg)
        : server_(server), cfg_(cfg),
          sse_curl_(curl_easy_init()), post_curl_(curl_easy_init()) {
        // Self-pipe for cancelling the reader thread's select() from the destructor
        if (pipe(cancel_pipe_) != 0) cancel_pipe_[0] = cancel_pipe_[1] = -1;

        if (!sse_curl_) return;

        // Set up the persistent GET /sse handle
        if (!server_.api_key.empty()) {
            std::string auth = "Authorization: Bearer " + server_.api_key;
            sse_headers_ = curl_slist_append(sse_headers_, auth.c_str());
            curl_easy_setopt(sse_curl_, CURLOPT_HTTPHEADER, sse_headers_);
        }
        curl_easy_setopt(sse_curl_, CURLOPT_URL,           server_.url.c_str());
        curl_easy_setopt(sse_curl_, CURLOPT_WRITEFUNCTION, sse_write_callback);
        curl_easy_setopt(sse_curl_, CURLOPT_WRITEDATA,     static_cast<void*>(this));
        curl_easy_setopt(sse_curl_, CURLOPT_TIMEOUT,       0L);  // no hard timeout

        reader_ = std::thread(&LegacySseTransport::reader_thread_main, this);
    }

    ~LegacySseTransport() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            done_ = true;
        }
        cv_.notify_all();
        // Wake the reader thread's select() so it can exit promptly
        if (cancel_pipe_[1] != -1) {
            char b = 1;
            ssize_t wr = write(cancel_pipe_[1], &b, 1); (void)wr;
        }
        if (reader_.joinable()) reader_.join();

        if (sse_curl_)   curl_easy_cleanup(sse_curl_);
        if (post_curl_)  curl_easy_cleanup(post_curl_);
        if (sse_headers_) curl_slist_free_all(sse_headers_);
        if (cancel_pipe_[0] != -1) close(cancel_pipe_[0]);
        if (cancel_pipe_[1] != -1) close(cancel_pipe_[1]);
    }

    std::optional<JsonValue> send_rpc(int id, const std::string& method,
                                      const JsonValue& params) override {
        // Wait until the reader thread has received the endpoint URL
        {
            std::unique_lock<std::mutex> lk(mu_);
            if (!cv_.wait_for(lk, std::chrono::seconds(cfg_.timeout_sec),
                              [this] { return !post_url_.empty() || done_; })) {
                return std::nullopt;  // timeout
            }
            if (done_ && post_url_.empty()) return std::nullopt;
        }

        // POST the request
        if (!do_post(McpClient::build_rpc(id, method, params))) return std::nullopt;

        // Wait for the matching response from the reader thread
        std::unique_lock<std::mutex> lk(mu_);
        if (!cv_.wait_for(lk, std::chrono::seconds(cfg_.timeout_sec),
                          [this, id] { return pending_.count(id) || done_; })) {
            return std::nullopt;
        }
        auto it = pending_.find(id);
        if (it == pending_.end()) return std::nullopt;
        std::optional<JsonValue> result = std::move(it->second);
        pending_.erase(it);
        return result;
    }

    void send_notification(const std::string& method) override {
        std::string url;
        { std::lock_guard<std::mutex> lk(mu_); url = post_url_; }
        if (!url.empty()) do_post_to(McpClient::build_notification(method), url);
    }

private:
    // Called by curl_multi_perform in the reader thread when SSE bytes arrive
    static size_t sse_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* self = static_cast<LegacySseTransport*>(userdata);
        if (self->done_.load()) return 0;  // returning 0 aborts the transfer
        self->sse_buf_.append(ptr, size * nmemb);
        self->process_sse_buffer();
        return size * nmemb;
    }

    static size_t post_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // Parse complete SSE events from sse_buf_ line by line
    void process_sse_buffer() {
        size_t pos = 0;
        while (pos < sse_buf_.size()) {
            auto nl = sse_buf_.find('\n', pos);
            if (nl == std::string::npos) break;

            std::string line = sse_buf_.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pos = nl + 1;

            if (line.empty()) {
                dispatch_event();
                cur_type_.clear();
                cur_data_.clear();
            } else if (line.size() > 7 && line.substr(0, 7) == "event: ") {
                cur_type_ = line.substr(7);
            } else if (line.size() > 6 && line.substr(0, 6) == "data: ") {
                if (!cur_data_.empty()) cur_data_ += "\n";
                cur_data_ += line.substr(6);
            }
            // Ignore id:, retry:, and comment lines (:)
        }
        sse_buf_.erase(0, pos);
    }

    void dispatch_event() {
        if (cur_type_ == "endpoint") {
            std::string url = cur_data_;
            // Resolve relative path against server URL base (scheme://host:port)
            if (!url.empty() && url[0] == '/') {
                auto p = server_.url.find('/', server_.url.find("://") + 3);
                std::string base = (p != std::string::npos) ? server_.url.substr(0, p) : server_.url;
                url = base + url;
            }
            std::lock_guard<std::mutex> lk(mu_);
            post_url_ = url;
            cv_.notify_all();
        } else if (cur_type_ == "message" && !cur_data_.empty()) {
            try {
                JsonValue resp = parse_json(cur_data_);
                auto id_v = resp.get("id");
                if (id_v && id_v->is_number()) {
                    int id = (int)id_v->as_number();
                    auto err_v = resp.get("error");
                    auto res_v = resp.get("result");
                    std::lock_guard<std::mutex> lk(mu_);
                    // Store nullopt for errors, the result value for success
                    if (err_v.has_value()) {
                        if (cfg_.debug) {
                            auto msg = err_v->get("message");
                            std::cerr << "[mcp:" << server_.name << "] sse rpc error: "
                                      << (msg && msg->is_string() ? msg->as_string() : "unknown") << "\n";
                        }
                        pending_[id] = std::nullopt;
                    } else if (res_v.has_value()) {
                        pending_[id] = *res_v;
                    }
                    cv_.notify_all();
                }
            } catch (...) {}
        }
    }

    bool do_post(const std::string& body) {
        std::string url;
        { std::lock_guard<std::mutex> lk(mu_); url = post_url_; }
        return do_post_to(body, url);
    }

    bool do_post_to(const std::string& body, const std::string& url) {
        if (!post_curl_ || url.empty()) return false;
        curl_easy_reset(post_curl_);

        std::string response_body;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        if (!server_.api_key.empty()) {
            std::string auth = "Authorization: Bearer " + server_.api_key;
            headers = curl_slist_append(headers, auth.c_str());
        }

        curl_easy_setopt(post_curl_, CURLOPT_URL,           url.c_str());
        curl_easy_setopt(post_curl_, CURLOPT_POSTFIELDS,     body.c_str());
        curl_easy_setopt(post_curl_, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body.size()));
        curl_easy_setopt(post_curl_, CURLOPT_HTTPHEADER,     headers);
        curl_easy_setopt(post_curl_, CURLOPT_WRITEFUNCTION,  post_write_callback);
        curl_easy_setopt(post_curl_, CURLOPT_WRITEDATA,      static_cast<void*>(&response_body));
        curl_easy_setopt(post_curl_, CURLOPT_TIMEOUT,        static_cast<long>(cfg_.timeout_sec));

        CURLcode res = curl_easy_perform(post_curl_);
        curl_slist_free_all(headers);
        return res == CURLE_OK;
    }

    // Background thread: drives the SSE GET stream via curl_multi.
    // Uses a self-pipe (cancel_pipe_) to wake select() when the destructor wants to stop.
    void reader_thread_main() {
        CURLM* multi = curl_multi_init();
        curl_multi_add_handle(multi, sse_curl_);

        while (!done_.load()) {
            int still_running = 0;
            curl_multi_perform(multi, &still_running);
            if (!still_running) break;

            fd_set rfds, wfds, efds;
            FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
            int maxfd = -1;
            curl_multi_fdset(multi, &rfds, &wfds, &efds, &maxfd);

            if (cancel_pipe_[0] != -1) {
                FD_SET(cancel_pipe_[0], &rfds);
                if (cancel_pipe_[0] > maxfd) maxfd = cancel_pipe_[0];
            }

            struct timeval tv = {1, 0};
            if (maxfd == -1) {
                // curl has no fds yet (connection in progress); sleep briefly
                select(0, nullptr, nullptr, nullptr, &tv);
                continue;
            }

            select(maxfd + 1, &rfds, &wfds, &efds, &tv);

            if (cancel_pipe_[0] != -1 && FD_ISSET(cancel_pipe_[0], &rfds)) break;
        }

        curl_multi_remove_handle(multi, sse_curl_);
        curl_multi_cleanup(multi);

        std::lock_guard<std::mutex> lk(mu_);
        done_ = true;
        cv_.notify_all();
    }

    McpServerConfig  server_;
    Config           cfg_;
    CURL*            sse_curl_    = nullptr;
    CURL*            post_curl_   = nullptr;
    curl_slist*      sse_headers_ = nullptr;
    int              cancel_pipe_[2] = {-1, -1};

    // SSE parsing state (accessed only by reader thread)
    std::string      sse_buf_;
    std::string      cur_type_;
    std::string      cur_data_;

    // Shared between reader thread and send_rpc callers
    std::mutex       mu_;
    std::condition_variable cv_;
    std::string      post_url_;
    std::map<int, std::optional<JsonValue>> pending_;  // nullopt = rpc error
    std::atomic<bool> done_{false};

    std::thread      reader_;
};

// ============================================================================
// McpClient — thin coordinator: selects transport, delegates all I/O
// ============================================================================

McpClient::McpClient(const McpServerConfig& server, const Config& cfg)
    : server_(server), cfg_(cfg) {
    switch (server.transport) {
        case McpTransportType::Stdio:
            transport_ = std::make_unique<StdioTransport>(server, cfg);
            break;
        case McpTransportType::LegacySse:
            transport_ = std::make_unique<LegacySseTransport>(server, cfg);
            break;
        default:
            transport_ = std::make_unique<HttpTransport>(server, cfg);
            break;
    }
}

std::optional<JsonValue> McpClient::send_rpc(const std::string& method, const JsonValue& params) {
    if (!transport_) return std::nullopt;
    return transport_->send_rpc(next_id_++, method, params);
}

void McpClient::send_notification(const std::string& method) {
    if (transport_) transport_->send_notification(method);
}

bool McpClient::initialize() {
    auto params = parse_json(R"({
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "ccl", "version": "1.0"}
    })");

    auto result = send_rpc("initialize", params);
    if (!result) return false;

    send_notification("notifications/initialized");
    return true;
}

std::vector<ToolDef> McpClient::list_tools() {
    auto result = send_rpc("tools/list");
    if (!result) return {};
    return parse_tools_list(*result);
}

ToolResult McpClient::call_tool(const std::string& name, const ToolArgs& args) {
    std::ostringstream args_json;
    args_json << "{";
    bool first = true;
    for (const auto& [k, v] : args) {
        if (!first) args_json << ",";
        args_json << "\"" << escape_json(k) << "\":" << to_json(v);
        first = false;
    }
    args_json << "}";

    std::string params_str = "{\"name\":\"" + escape_json(name)
                           + "\",\"arguments\":" + args_json.str() + "}";
    JsonValue params = parse_json(params_str);

    auto result = send_rpc("tools/call", params);
    if (!result) {
        return ToolResult::fail("MCP call failed: no response from '" + server_.name + "'");
    }
    return parse_call_result(*result);
}
