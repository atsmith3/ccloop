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

#pragma once

#include "config.h"
#include "connector.h"
#include "json.h"
#include <curl/curl.h>
#include <string>

class ConnectorBase : public Connector {
public:
  static bool is_retryable_status(int http_status);

protected:
  struct HttpResult {
    int status;
    std::string body;
  };

  Config cfg_;
  CURL *curl_ = nullptr;

  explicit ConnectorBase(const Config &cfg);
  ~ConnectorBase();

  // POST body to url using given headers slist (caller owns/frees the slist).
  // Retry logic is built in. Auth set either via headers or via curl handle
  // options.
  HttpResult http_post(const std::string &url, const std::string &body,
                       struct curl_slist *headers);

  // Shared helpers for subclass request builders and response parsers
  static std::string
  build_tool_params_json(const std::vector<ToolParam> &params);
  static ToolArgs json_obj_to_args(const JsonObject &obj);
  static LlmResponse make_http_error(const HttpResult &result);
  static std::string make_fallback_call_id(int index) {
    return "call_" + std::to_string(index);
  }

private:
  static size_t write_callback(char *ptr, size_t size, size_t nmemb,
                               void *userdata);
};
