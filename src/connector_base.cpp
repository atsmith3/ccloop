#include "connector_base.h"
#include <chrono>
#include <thread>
#include <iostream>

ConnectorBase::ConnectorBase(const Config& cfg)
    : cfg_(cfg), curl_(curl_easy_init()) {}

ConnectorBase::~ConnectorBase() {
    if (curl_) curl_easy_cleanup(curl_);
}

bool ConnectorBase::is_retryable_status(int http_status) {
    return http_status == 429 || (http_status >= 500 && http_status < 600);
}

size_t ConnectorBase::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, realsize);
    return realsize;
}

ConnectorBase::HttpResult ConnectorBase::http_post(
    const std::string& url,
    const std::string& body,
    struct curl_slist* headers)
{
    if (!curl_) return {500, "CURL not initialized"};

    std::string response_body;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, static_cast<void*>(&response_body));
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, static_cast<long>(cfg_.timeout_sec));

    int sleep_ms[] = {1000, 2000, 4000};
    int retry_count = 0;

    while (retry_count <= cfg_.max_retries) {
        response_body.clear();
        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            return {500, std::string(curl_easy_strerror(res))};
        }

        long http_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

        if (cfg_.debug) {
            std::cerr << "[debug] HTTP " << http_code << " response:\n"
                      << response_body << "\n";
        }

        if (!is_retryable_status(static_cast<int>(http_code))) {
            return {static_cast<int>(http_code), response_body};
        }

        if (retry_count < cfg_.max_retries) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(sleep_ms[retry_count]));
        }
        ++retry_count;
    }

    return {500, "Max retries exceeded"};
}
