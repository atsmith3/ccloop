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

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Forward declarations
struct JsonValue;

// Type aliases
using JsonArray = std::vector<std::shared_ptr<JsonValue>>;
using JsonObject = std::map<std::string, std::shared_ptr<JsonValue>>;

// JSON value -- holds any JSON value
struct JsonValue {
  std::variant<std::monostate, bool, double, std::string, JsonArray, JsonObject>
      data;

  // Type checks
  bool is_null() const;
  bool is_bool() const;
  bool is_number() const;
  bool is_string() const;
  bool is_array() const;
  bool is_object() const;

  // Accessors -- throw on wrong type
  bool as_bool() const;
  double as_number() const;
  const std::string &as_string() const;
  const JsonArray &as_array() const;
  const JsonObject &as_object() const;

  // Safe path access -- returns nullopt if missing
  std::optional<JsonValue> get(const std::string &key) const;
  std::optional<JsonValue> get(size_t index) const;

  // Chained access operator
  std::optional<JsonValue> operator[](const std::string &key) const;
  std::optional<JsonValue> operator[](size_t index) const;
};

// Public interface
JsonValue parse_json(std::string_view input);
std::string to_json(const JsonValue &val);
std::string escape_json(const std::string &s);
