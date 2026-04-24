#pragma once

#include <vector>
#include <string>
#include "types.h"

class ContextManager {
public:
    explicit ContextManager(size_t token_limit);

    void push_system(std::string content);
    void replace_system(std::string content);  // Update system message, keep conversation
    void push_user(std::string content);
    void push_assistant(std::string content);

    void sync_token_count(const LlmResponse::Usage& usage);

    bool needs_compaction() const;
    void compact();
    std::string extract_conversation_for_summary() const;
    void        compact_to_summary(const std::string& summary);

    std::string to_json() const;

    size_t total_tokens()  const;
    size_t message_count() const;

private:
    std::vector<Message> messages_;
    size_t               total_tokens_ = 0;
    size_t               token_limit_;

    size_t estimate_tokens(const std::string& text) const;
    size_t index_of_first_non_system() const;
    size_t find_safe_drop_end(size_t start) const;
};
