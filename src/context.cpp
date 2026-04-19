#include "context.h"
#include "json.h"
#include <sstream>

ContextManager::ContextManager(size_t token_limit)
    : token_limit_(token_limit) {}

void ContextManager::push_system(std::string content) {
    Message msg;
    msg.role = Message::Role::System;
    msg.content = std::move(content);
    msg.estimated_tokens = estimate_tokens(msg.content);
    messages_.push_back(msg);
    total_tokens_ += msg.estimated_tokens;
}

void ContextManager::replace_system(std::string content) {
    Message msg;
    msg.role = Message::Role::System;
    msg.content = std::move(content);
    msg.estimated_tokens = estimate_tokens(msg.content);

    if (!messages_.empty() && messages_[0].role == Message::Role::System) {
        // Replace existing system message, update token count
        total_tokens_ -= messages_[0].estimated_tokens;
        messages_[0] = msg;
        total_tokens_ += msg.estimated_tokens;
    } else {
        // No existing system message, insert at front
        messages_.insert(messages_.begin(), msg);
        total_tokens_ += msg.estimated_tokens;
    }
}

void ContextManager::push_user(std::string content) {
    Message msg;
    msg.role = Message::Role::User;
    msg.content = std::move(content);
    msg.estimated_tokens = estimate_tokens(msg.content);
    messages_.push_back(msg);
    total_tokens_ += msg.estimated_tokens;
}

void ContextManager::push_assistant(std::string content,
                                    std::vector<ToolCallRecord> tool_calls) {
    Message msg;
    msg.role = Message::Role::Assistant;
    msg.content = std::move(content);
    msg.tool_calls = std::move(tool_calls);
    msg.estimated_tokens = estimate_tokens(msg.content);
    messages_.push_back(msg);
    total_tokens_ += msg.estimated_tokens;
}

void ContextManager::push_tool_result(std::string call_id, const ToolResult& result) {
    Message msg;
    msg.role = Message::Role::Tool;
    msg.tool_call_id = std::move(call_id);
    msg.content = result.to_context_string();
    msg.estimated_tokens = estimate_tokens(msg.content);
    messages_.push_back(msg);
    total_tokens_ += msg.estimated_tokens;
}

void ContextManager::sync_token_count(const LlmResponse::Usage& usage) {
    total_tokens_ = usage.total_tokens;
}

bool ContextManager::needs_compaction() const {
    return total_tokens_ >= token_limit_;
}

size_t ContextManager::estimate_tokens(const std::string& text) const {
    return (text.size() + 3) / 4;  // ceiling division
}

size_t ContextManager::index_of_first_non_system() const {
    for (size_t i = 0; i < messages_.size(); ++i) {
        if (messages_[i].role != Message::Role::System) {
            return i;
        }
    }
    return messages_.size();
}

size_t ContextManager::find_safe_drop_end(size_t start) const {
    // Find a safe boundary starting from index `start`
    // Safe boundary: after a non-tool message that follows a non-assistant message
    // Never leave an assistant message without its tool results

    if (start >= messages_.size()) return messages_.size();

    for (size_t i = start; i < messages_.size(); ++i) {
        // Skip if this is an assistant message (need to keep with subsequent tools)
        if (messages_[i].role == Message::Role::Assistant) {
            // Skip to after all tool results following this assistant
            ++i;
            while (i < messages_.size() && messages_[i].role == Message::Role::Tool) {
                ++i;
            }
            // Now i points to first non-tool after assistant; this is safe to drop before
            return i;
        }
        // If it's User, Tool, or System, safe to drop after
        if (messages_[i].role != Message::Role::Assistant) {
            return i + 1;
        }
    }
    return messages_.size();
}

void ContextManager::compact() {
    if (!needs_compaction()) return;

    size_t first_non_system = index_of_first_non_system();
    if (first_non_system >= messages_.size()) {
        // Only system messages, can't compact
        return;
    }

    // Find safe drop boundary and drop
    size_t drop_end = find_safe_drop_end(first_non_system);
    if (drop_end > first_non_system) {
        // Calculate tokens being removed
        size_t tokens_removed = 0;
        for (size_t i = first_non_system; i < drop_end; ++i) {
            tokens_removed += messages_[i].estimated_tokens;
        }

        // Remove messages
        messages_.erase(messages_.begin() + first_non_system,
                        messages_.begin() + drop_end);
        total_tokens_ -= tokens_removed;
    }
}

std::string ContextManager::to_json() const {
    std::ostringstream ss;
    ss << "[";

    for (size_t i = 0; i < messages_.size(); ++i) {
        if (i > 0) ss << ",";

        const Message& msg = messages_[i];
        ss << "{\"role\":";

        // Role
        switch (msg.role) {
            case Message::Role::System:
                ss << "\"system\"";
                break;
            case Message::Role::User:
                ss << "\"user\"";
                break;
            case Message::Role::Assistant:
                ss << "\"assistant\"";
                break;
            case Message::Role::Tool:
                ss << "\"tool\"";
                break;
        }
        ss << ",\"content\":\"" << escape_json(msg.content) << "\"";

        // Tool call ID (for tool messages)
        if (msg.role == Message::Role::Tool && !msg.tool_call_id.empty()) {
            ss << ",\"tool_call_id\":\"" << escape_json(msg.tool_call_id) << "\"";
        }

        // Tool calls (for assistant messages)
        if (msg.role == Message::Role::Assistant && !msg.tool_calls.empty()) {
            ss << ",\"tool_calls\":[";
            for (size_t j = 0; j < msg.tool_calls.size(); ++j) {
                if (j > 0) ss << ",";
                const auto& tc = msg.tool_calls[j];
                ss << "{\"id\":\"" << escape_json(tc.id) << "\""
                   << ",\"type\":\"function\",\"function\":{"
                   << "\"name\":\"" << escape_json(tc.name) << "\""
                   << ",\"arguments\":" << tc.arguments_json
                   << "}}";
            }
            ss << "]";
        }

        ss << "}";
    }

    ss << "]";
    return ss.str();
}

size_t ContextManager::total_tokens() const {
    return total_tokens_;
}

size_t ContextManager::message_count() const {
    return messages_.size();
}
