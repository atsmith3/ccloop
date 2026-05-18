// Copyright 2026 Andrew Smith
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "connector_base.h"
#include "signals.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

static int interrupt_progress_cb(void *, curl_off_t, curl_off_t, curl_off_t,
                                 curl_off_t) {
  return should_interrupt.load() ? 1 : 0;
}

ConnectorBase::ConnectorBase(const Config &cfg)
    : cfg_(cfg), curl_(curl_easy_init()) {
  if (!curl_)
    throw std::runtime_error("curl_easy_init() failed");
}

ConnectorBase::~ConnectorBase() {
  if (curl_)
    curl_easy_cleanup(curl_);
}

bool ConnectorBase::is_retryable_status(int http_status) {
  return http_status == 429 || (http_status >= 500 && http_status < 600);
}

size_t ConnectorBase::write_callback(char *ptr, size_t size, size_t nmemb,
                                     void *userdata) {
  size_t realsize = size * nmemb;
  static_cast<std::string *>(userdata)->append(ptr, realsize);
  return realsize;
}

ConnectorBase::HttpResult ConnectorBase::http_post(const std::string &url,
                                                   const std::string &body,
                                                   struct curl_slist *headers) {
  if (!curl_)
    return {500, "CURL not initialized"};

  std::string response_body;
  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA,
                   static_cast<void *>(&response_body));
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, static_cast<long>(cfg_.timeout_sec));
  curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, interrupt_progress_cb);

  // Exponential backoff: 1s, 2s, 4s (capped at 3 doublings)
  auto retry_delay = [](int attempt) {
    return std::chrono::milliseconds(1000 << std::min(attempt, 2));
  };

  int retry_count = 0;

  while (retry_count <= cfg_.max_retries) {
    response_body.clear();
    CURLcode res = curl_easy_perform(curl_);

    if (res != CURLE_OK) {
      // Retry on timeout; all other curl errors are terminal
      if (res != CURLE_OPERATION_TIMEDOUT || retry_count >= cfg_.max_retries ||
          should_interrupt.load()) {
        return {500, std::string(curl_easy_strerror(res))};
      }
      std::this_thread::sleep_for(retry_delay(retry_count));
      ++retry_count;
      continue;
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

    if (retry_count < cfg_.max_retries && !should_interrupt.load()) {
      std::this_thread::sleep_for(retry_delay(retry_count));
    }
    ++retry_count;
  }

  return {500, "Max retries exceeded"};
}

std::string
ConnectorBase::build_tool_params_json(const std::vector<ToolParam> &params) {
  std::ostringstream props;
  std::ostringstream required;
  bool first_prop = true;
  bool first_req = true;

  for (const auto &p : params) {
    if (!first_prop)
      props << ",";
    props << "\"" << escape_json(p.name) << "\":"
          << "{\"type\":\"" << escape_json(p.type) << "\""
          << ",\"description\":\"" << escape_json(p.description) << "\"}";
    first_prop = false;

    if (p.required) {
      if (!first_req)
        required << ",";
      required << "\"" << escape_json(p.name) << "\"";
      first_req = false;
    }
  }

  std::ostringstream ss;
  ss << "{\"type\":\"object\",\"properties\":{" << props.str() << "}";
  if (!first_req) {
    ss << ",\"required\":[" << required.str() << "]";
  }
  ss << "}";
  return ss.str();
}

ToolArgs ConnectorBase::json_obj_to_args(const JsonObject &obj) {
  ToolArgs args;
  for (const auto &[k, v] : obj) {
    args[k] = *v;
  }
  return args;
}

LlmResponse ConnectorBase::make_http_error(const HttpResult &result) {
  LlmResponse response;
  response.is_error = true;
  response.content = "HTTP " + std::to_string(result.status) + ": " +
                     result.body.substr(0, 500);
  return response;
}
