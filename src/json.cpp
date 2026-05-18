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

#include "json.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

// ============================================================================
// JsonValue methods
// ============================================================================

bool JsonValue::is_null() const {
  return std::holds_alternative<std::monostate>(data);
}

bool JsonValue::is_bool() const { return std::holds_alternative<bool>(data); }

bool JsonValue::is_number() const {
  return std::holds_alternative<double>(data);
}

bool JsonValue::is_string() const {
  return std::holds_alternative<std::string>(data);
}

bool JsonValue::is_array() const {
  return std::holds_alternative<JsonArray>(data);
}

bool JsonValue::is_object() const {
  return std::holds_alternative<JsonObject>(data);
}

bool JsonValue::as_bool() const {
  if (!is_bool())
    throw std::runtime_error("JsonValue is not a boolean");
  return std::get<bool>(data);
}

double JsonValue::as_number() const {
  if (!is_number())
    throw std::runtime_error("JsonValue is not a number");
  return std::get<double>(data);
}

const std::string &JsonValue::as_string() const {
  if (!is_string())
    throw std::runtime_error("JsonValue is not a string");
  return std::get<std::string>(data);
}

const JsonArray &JsonValue::as_array() const {
  if (!is_array())
    throw std::runtime_error("JsonValue is not an array");
  return std::get<JsonArray>(data);
}

const JsonObject &JsonValue::as_object() const {
  if (!is_object())
    throw std::runtime_error("JsonValue is not an object");
  return std::get<JsonObject>(data);
}

std::optional<JsonValue> JsonValue::get(const std::string &key) const {
  if (!is_object())
    return std::nullopt;
  const auto &obj = std::get<JsonObject>(data);
  auto it = obj.find(key);
  if (it == obj.end())
    return std::nullopt;
  return *it->second;
}

std::optional<JsonValue> JsonValue::get(size_t index) const {
  if (!is_array())
    return std::nullopt;
  const auto &arr = std::get<JsonArray>(data);
  if (index >= arr.size())
    return std::nullopt;
  return *arr[index];
}

std::optional<JsonValue> JsonValue::operator[](const std::string &key) const {
  return get(key);
}

std::optional<JsonValue> JsonValue::operator[](size_t index) const {
  return get(index);
}

// ============================================================================
// Parser
// ============================================================================

struct Parser {
  std::string_view input;
  size_t pos = 0;

  void skip_whitespace() {
    while (pos < input.size() && std::isspace(input[pos])) {
      ++pos;
    }
  }

  char current() const {
    if (pos >= input.size())
      return '\0';
    return input[pos];
  }

  char peek(size_t offset = 1) const {
    size_t p = pos + offset;
    if (p >= input.size())
      return '\0';
    return input[p];
  }

  void advance() { ++pos; }

  void expect(char c) {
    if (current() != c) {
      std::ostringstream msg;
      msg << "Expected '" << c << "' but got '" << current() << "' at position "
          << pos;
      throw std::runtime_error(msg.str());
    }
    advance();
  }

  void error(const std::string &msg) {
    std::ostringstream oss;
    oss << msg << " at position " << pos;
    throw std::runtime_error(oss.str());
  }

  // Parse a JSON value
  JsonValue parse_value() {
    skip_whitespace();
    char c = current();

    if (c == 'n')
      return parse_null();
    if (c == 't' || c == 'f')
      return parse_bool();
    if (c == '"')
      return parse_string();
    if (c == '[')
      return parse_array();
    if (c == '{')
      return parse_object();
    if (c == '-' || std::isdigit(c))
      return parse_number();

    error(std::string("Unexpected character: ") + c);
    return JsonValue(); // unreachable
  }

  JsonValue parse_null() {
    if (input.substr(pos, 4) != "null") {
      error("Invalid null literal");
    }
    pos += 4;
    JsonValue v;
    v.data = std::monostate();
    return v;
  }

  JsonValue parse_bool() {
    JsonValue v;
    if (input.substr(pos, 4) == "true") {
      v.data = true;
      pos += 4;
    } else if (input.substr(pos, 5) == "false") {
      v.data = false;
      pos += 5;
    } else {
      error("Invalid boolean literal");
    }
    return v;
  }

  JsonValue parse_number() {
    size_t start = pos;

    // Optional minus
    if (current() == '-')
      advance();

    // At least one digit
    if (!std::isdigit(current())) {
      error("Invalid number: expected digit");
    }

    // Digits before decimal
    while (std::isdigit(current()))
      advance();

    // Optional decimal part
    if (current() == '.') {
      advance();
      if (!std::isdigit(current())) {
        error("Invalid number: expected digit after decimal point");
      }
      while (std::isdigit(current()))
        advance();
    }

    // Optional exponent
    if (current() == 'e' || current() == 'E') {
      advance();
      if (current() == '+' || current() == '-')
        advance();
      if (!std::isdigit(current())) {
        error("Invalid number: expected digit in exponent");
      }
      while (std::isdigit(current()))
        advance();
    }

    std::string num_str(input.substr(start, pos - start));
    double val = std::stod(num_str);

    JsonValue v;
    v.data = val;
    return v;
  }

  JsonValue parse_string() {
    expect('"');

    std::string result;
    while (current() != '"') {
      if (current() == '\0') {
        error("Unterminated string");
      }

      if (current() == '\\') {
        advance();
        char c = current();
        switch (c) {
        case '"':
          result += '"';
          break;
        case '\\':
          result += '\\';
          break;
        case '/':
          result += '/';
          break;
        case 'b':
          result += '\b';
          break;
        case 'f':
          result += '\f';
          break;
        case 'n':
          result += '\n';
          break;
        case 'r':
          result += '\r';
          break;
        case 't':
          result += '\t';
          break;
        case 'u': {
          // Unicode escape sequence \uXXXX
          advance();
          if (pos + 4 > input.size()) {
            error("Invalid unicode escape: incomplete sequence");
          }
          std::string hex(input.substr(pos, 4));
          try {
            int codepoint = std::stoi(hex, nullptr, 16);
            // For simplicity, only support BMP (U+0000 to U+FFFF)
            // In a real implementation, handle surrogates and UTF-8 encoding
            if (codepoint < 0x80) {
              result += static_cast<char>(codepoint);
            } else if (codepoint < 0x800) {
              result += static_cast<char>(0xC0 | (codepoint >> 6));
              result += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
              result += static_cast<char>(0xE0 | (codepoint >> 12));
              result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
              result += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            pos += 3; // We'll advance one more at the end of the loop
          } catch (...) {
            error("Invalid unicode escape sequence");
          }
          break;
        }
        default:
          error(std::string("Invalid escape character: ") + c);
        }
        advance();
      } else {
        result += current();
        advance();
      }
    }

    expect('"');

    JsonValue v;
    v.data = result;
    return v;
  }

  JsonValue parse_array() {
    expect('[');
    skip_whitespace();

    JsonArray arr;

    if (current() == ']') {
      advance();
      JsonValue v;
      v.data.emplace<JsonArray>(arr);
      return v;
    }

    while (true) {
      auto elem = std::make_shared<JsonValue>(parse_value());
      arr.push_back(elem);

      skip_whitespace();
      if (current() == ']') {
        advance();
        break;
      }

      if (current() != ',') {
        error("Expected ',' or ']' in array");
      }
      advance();
      skip_whitespace();
    }

    JsonValue v;
    v.data.emplace<JsonArray>(arr);
    return v;
  }

  JsonValue parse_object() {
    expect('{');
    skip_whitespace();

    JsonObject obj;

    if (current() == '}') {
      advance();
      JsonValue v;
      v.data.emplace<JsonObject>(obj);
      return v;
    }

    while (true) {
      skip_whitespace();

      // Parse key
      if (current() != '"') {
        error("Expected string key in object");
      }
      JsonValue key_val = parse_string();
      std::string key = key_val.as_string();

      skip_whitespace();
      expect(':');
      skip_whitespace();

      // Parse value
      auto val = std::make_shared<JsonValue>(parse_value());
      obj.insert({key, val});

      skip_whitespace();
      if (current() == '}') {
        advance();
        break;
      }

      if (current() != ',') {
        error("Expected ',' or '}' in object");
      }
      advance();
      skip_whitespace();
    }

    JsonValue v;
    v.data.emplace<JsonObject>(obj);
    return v;
  }
};

// ============================================================================
// Public interface
// ============================================================================

JsonValue parse_json(std::string_view input) {
  Parser parser{input};
  JsonValue result = parser.parse_value();
  parser.skip_whitespace();
  if (parser.pos < input.size()) {
    parser.error("Unexpected characters after JSON value");
  }
  return result;
}

std::string to_json(const JsonValue &val) {
  if (val.is_null()) {
    return "null";
  } else if (val.is_bool()) {
    return val.as_bool() ? "true" : "false";
  } else if (val.is_number()) {
    double num = val.as_number();
    // Check for special values
    if (std::isnan(num) || std::isinf(num)) {
      return "null"; // JSON doesn't support NaN/Inf
    }
    // Format as integer if it's a whole number
    if (num == std::floor(num)) {
      return std::to_string(static_cast<long long>(num));
    }
    std::ostringstream oss;
    oss.precision(17);
    oss << num;
    return oss.str();
  } else if (val.is_string()) {
    return "\"" + escape_json(val.as_string()) + "\"";
  } else if (val.is_array()) {
    std::string result = "[";
    const auto &arr = val.as_array();
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        result += ",";
      result += to_json(*arr[i]);
    }
    result += "]";
    return result;
  } else if (val.is_object()) {
    std::string result = "{";
    const auto &obj = val.as_object();
    bool first = true;
    for (const auto &[key, value] : obj) {
      if (!first)
        result += ",";
      first = false;
      result += "\"" + escape_json(key) + "\":" + to_json(*value);
    }
    result += "}";
    return result;
  }
  return "null";
}

std::string escape_json(const std::string &s) {
  std::string result;
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (c < 32) {
        char buf[7];
        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<int>(c));
        result += buf;
      } else {
        result += c;
      }
    }
  }
  return result;
}
