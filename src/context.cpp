#include "context.h"
#include "json.h"
#include <sstream>

ContextManager::ContextManager(size_t token_limit, size_t keep_recent)
    : token_limit_(token_limit), keep_recent_(keep_recent) {}

ContextManager ContextManager::from_messages(std::vector<Message> msgs,
                                             size_t total_tokens,
                                             size_t token_limit,
                                             size_t keep_recent) {
    ContextManager ctx(token_limit, keep_recent);
    ctx.messages_     = std::move(msgs);
    ctx.total_tokens_ = total_tokens;
    return ctx;
}

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

void ContextManager::push_assistant(std::string content) {
    Message msg;
    msg.role = Message::Role::Assistant;
    msg.content = std::move(content);
    msg.estimated_tokens = estimate_tokens(msg.content);
    messages_.push_back(msg);
    total_tokens_ += msg.estimated_tokens;
}


void ContextManager::sync_token_count(const LlmResponse::Usage& usage) {
    if (usage.total_tokens > 0) {
        total_tokens_ = usage.total_tokens;
    }
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
    if (start >= messages_.size()) return messages_.size();

    for (size_t i = start; i < messages_.size(); ++i) {
        // Any non-assistant message is a safe drop boundary
        if (messages_[i].role != Message::Role::Assistant) {
            return i + 1;
        }
    }
    return messages_.size();
}

void ContextManager::compact() {
    size_t first_non_system = index_of_first_non_system();
    if (first_non_system >= messages_.size()) return;

    // Drop oldest message groups until below the token limit (or no progress possible)
    while (needs_compaction()) {
        size_t drop_end = find_safe_drop_end(first_non_system);
        if (drop_end <= first_non_system) break;

        size_t before = messages_.size();
        size_t tokens_removed = 0;
        for (size_t i = first_non_system; i < drop_end; ++i) {
            tokens_removed += messages_[i].estimated_tokens;
        }
        messages_.erase(messages_.begin() + first_non_system,
                        messages_.begin() + drop_end);
        total_tokens_ -= tokens_removed;
        if (messages_.size() == before) break;  // safety: no progress made
    }
}

std::string ContextManager::extract_conversation_for_summary() const {
    std::ostringstream ss;
    for (const auto& msg : messages_) {
        if (msg.role == Message::Role::System) continue;
        ss << (msg.role == Message::Role::User ? "User" : "Assistant")
           << ": " << msg.content << "\n\n";
    }
    return ss.str();
}

void ContextManager::compact_to_summary(const std::string& summary) {
    size_t first_ns = index_of_first_non_system();
    size_t keep_from = messages_.size() > first_ns + keep_recent_
                     ? messages_.size() - keep_recent_ : first_ns;

    Message sum_msg;
    sum_msg.role = Message::Role::User;
    sum_msg.content = "[Context Summary — earlier conversation condensed]\n" + summary;
    sum_msg.estimated_tokens = estimate_tokens(sum_msg.content);

    std::vector<Message> kept;
    for (size_t i = 0; i < first_ns; ++i)               kept.push_back(messages_[i]);
    kept.push_back(sum_msg);
    for (size_t i = keep_from; i < messages_.size(); ++i) kept.push_back(messages_[i]);

    messages_ = std::move(kept);
    total_tokens_ = 0;
    for (const auto& m : messages_) total_tokens_ += m.estimated_tokens;
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
        }
        ss << ",\"content\":\"" << escape_json(msg.content) << "\"";

        ss << "}";
    }

    ss << "]";
    return ss.str();
}

const std::vector<Message>& ContextManager::messages() const {
    return messages_;
}

size_t ContextManager::total_tokens() const {
    return total_tokens_;
}

size_t ContextManager::message_count() const {
    return messages_.size();
}
