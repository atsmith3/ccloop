#pragma once

#include <string>
#include <curl/curl.h>
#include "connector.h"
#include "config.h"

class ConnectorBase : public Connector {
public:
    static bool is_retryable_status(int http_status);

protected:
    struct HttpResult { int status; std::string body; };

    Config cfg_;
    CURL*  curl_ = nullptr;

    explicit ConnectorBase(const Config& cfg);
    ~ConnectorBase();

    // POST body to url using given headers slist (caller owns/frees the slist).
    // Retry logic is built in. Auth set either via headers or via curl handle options.
    HttpResult http_post(const std::string& url,
                         const std::string& body,
                         struct curl_slist* headers);

private:
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};
