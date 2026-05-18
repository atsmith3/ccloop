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

#include "../src/json.h"
#include "harness.h"
#include <cmath>
#include <limits>

// ============================================================================
// Parsing tests
// ============================================================================

TEST(json_parse_null) {
  JsonValue v = parse_json("null");
  CHECK(v.is_null());
}

TEST(json_parse_bool_true) {
  JsonValue v = parse_json("true");
  CHECK(v.is_bool());
  CHECK_EQ(v.as_bool(), true);
}

TEST(json_parse_bool_false) {
  JsonValue v = parse_json("false");
  CHECK(v.is_bool());
  CHECK_EQ(v.as_bool(), false);
}

TEST(json_parse_integer) {
  JsonValue v = parse_json("42");
  CHECK(v.is_number());
  CHECK_EQ(v.as_number(), 42.0);
}

TEST(json_parse_float) {
  JsonValue v = parse_json("3.14");
  CHECK(v.is_number());
  CHECK_EQ(v.as_number(), 3.14);
}

TEST(json_parse_negative) {
  JsonValue v = parse_json("-17");
  CHECK(v.is_number());
  CHECK_EQ(v.as_number(), -17.0);
}

TEST(json_parse_string) {
  JsonValue v = parse_json("\"hello\"");
  CHECK(v.is_string());
  CHECK_EQ(v.as_string(), "hello");
}

TEST(json_parse_string_with_escapes) {
  JsonValue v = parse_json("\"hello\\nworld\\t!\"");
  CHECK(v.is_string());
  CHECK_EQ(v.as_string(), "hello\nworld\t!");
}

TEST(json_parse_string_with_unicode_escape) {
  JsonValue v = parse_json("\"hello\\u0020world\"");
  CHECK(v.is_string());
  CHECK_EQ(v.as_string(), "hello world");
}

TEST(json_parse_empty_array) {
  JsonValue v = parse_json("[]");
  CHECK(v.is_array());
  CHECK_EQ(v.as_array().size(), 0);
}

TEST(json_parse_nested_array) {
  JsonValue v = parse_json("[1, [2, 3], 4]");
  CHECK(v.is_array());
  const auto &arr = v.as_array();
  CHECK_EQ(arr.size(), 3);
  CHECK(arr[0]->is_number());
  CHECK_EQ(arr[0]->as_number(), 1.0);
  CHECK(arr[1]->is_array());
  const auto &inner = arr[1]->as_array();
  CHECK_EQ(inner.size(), 2);
  CHECK_EQ(inner[0]->as_number(), 2.0);
}

TEST(json_parse_empty_object) {
  JsonValue v = parse_json("{}");
  CHECK(v.is_object());
  CHECK_EQ(v.as_object().size(), 0);
}

TEST(json_parse_nested_object) {
  JsonValue v = parse_json("{\"a\": 1, \"b\": {\"c\": 2}}");
  CHECK(v.is_object());
  const auto &obj = v.as_object();
  CHECK_EQ(obj.size(), 2);
  CHECK(obj.at("a")->is_number());
  CHECK_EQ(obj.at("a")->as_number(), 1.0);
  CHECK(obj.at("b")->is_object());
  const auto &inner = obj.at("b")->as_object();
  CHECK_EQ(inner.at("c")->as_number(), 2.0);
}

// ============================================================================
// Roundtrip tests
// ============================================================================

TEST(json_roundtrip_simple_object) {
  std::string s = R"({"k":"v","n":42})";
  JsonValue v = parse_json(s);
  std::string serialized = to_json(v);
  // Re-parse to verify both are equivalent
  JsonValue v2 = parse_json(serialized);
  CHECK(v2.is_object());
  const auto &obj = v2.as_object();
  CHECK_EQ(obj.at("k")->as_string(), "v");
  CHECK_EQ(obj.at("n")->as_number(), 42.0);
}

TEST(json_roundtrip_nested) {
  std::string s = R"([1, {"a": true, "b": null}, "test"])";
  JsonValue v = parse_json(s);
  std::string serialized = to_json(v);
  JsonValue v2 = parse_json(serialized);
  CHECK(v2.is_array());
  const auto &arr = v2.as_array();
  CHECK_EQ(arr.size(), 3);
  CHECK_EQ(arr[0]->as_number(), 1.0);
  CHECK(arr[1]->is_object());
  const auto &obj = arr[1]->as_object();
  CHECK_EQ(obj.at("a")->as_bool(), true);
  CHECK(obj.at("b")->is_null());
  CHECK_EQ(arr[2]->as_string(), "test");
}

// ============================================================================
// Access tests
// ============================================================================

TEST(json_access_get_key_present) {
  JsonValue v = parse_json(R"({"name": "Alice", "age": 30})");
  auto name = v.get("name");
  CHECK(name.has_value());
  CHECK_EQ(name->as_string(), "Alice");
}

TEST(json_access_get_key_missing) {
  JsonValue v = parse_json(R"({"name": "Alice"})");
  auto missing = v.get("age");
  CHECK(!missing.has_value());
}

TEST(json_access_index_present) {
  JsonValue v = parse_json("[10, 20, 30]");
  auto elem = v.get(1);
  CHECK(elem.has_value());
  CHECK_EQ(elem->as_number(), 20.0);
}

TEST(json_access_index_out_of_bounds) {
  JsonValue v = parse_json("[10, 20, 30]");
  auto elem = v.get(10);
  CHECK(!elem.has_value());
}

// ============================================================================
// Type checking and error tests
// ============================================================================

TEST(json_wrong_type_throws) {
  JsonValue v = parse_json("42");
  CHECK_THROWS(v.as_string());
}

TEST(json_invalid_input_throws) { CHECK_THROWS(parse_json("{bad")); }

// ============================================================================
// Escape tests
// ============================================================================

TEST(json_escape_special_chars) {
  std::string escaped = escape_json("hello\nworld\t\"quoted\"\\backslash");
  std::string expected = "hello\\nworld\\t\\\"quoted\\\"\\\\backslash";
  CHECK_EQ(escaped, expected);
}

// ============================================================================
// Number edge cases
// ============================================================================

TEST(json_parse_scientific_notation) {
  JsonValue v = parse_json("1.5e10");
  CHECK(v.is_number());
  CHECK(std::abs(v.as_number() - 1.5e10) < 1e5);
}

TEST(json_parse_negative_exponent) {
  JsonValue v = parse_json("1.5e-3");
  CHECK(v.is_number());
  CHECK(std::abs(v.as_number() - 1.5e-3) < 1e-10);
}

TEST(json_to_json_nan_is_null) {
  JsonValue v;
  v.data = std::numeric_limits<double>::quiet_NaN();
  CHECK_EQ(to_json(v), std::string("null"));
}

TEST(json_to_json_infinity_is_null) {
  JsonValue v;
  v.data = std::numeric_limits<double>::infinity();
  CHECK_EQ(to_json(v), std::string("null"));
}

// ============================================================================
// Escape / input edge cases
// ============================================================================

TEST(json_escape_empty_string) { CHECK_EQ(escape_json(""), std::string("")); }

TEST(json_escape_control_characters) {
  std::string escaped = escape_json("\x01\x1f");
  CHECK_EQ(escaped, std::string("\\u0001\\u001f"));
}

TEST(json_parse_whitespace_around_value) {
  JsonValue v = parse_json("  42  ");
  CHECK(v.is_number());
  CHECK_EQ(v.as_number(), 42.0);
}

TEST(json_parse_empty_input_throws) { CHECK_THROWS(parse_json("")); }

TEST(json_parse_unicode_escape_ascii) {
  // A is 'A', B is 'B'
  JsonValue v = parse_json("\"\\u0041\\u0042\"");
  CHECK(v.is_string());
  CHECK_EQ(v.as_string(), std::string("AB"));
}

TEST(json_escape_all_control_chars) {
  // All bytes 0x00–0x1f must be escaped — either \uXXXX or a named escape (\n,
  // \t, etc.)
  for (int c = 0x00; c <= 0x1f; ++c) {
    std::string input(1, static_cast<char>(c));
    std::string escaped = escape_json(input);
    // Raw control character must not appear in the escaped output
    CHECK(escaped.find(static_cast<char>(c)) == std::string::npos);
    // All escape sequences start with a backslash
    CHECK(!escaped.empty() && escaped[0] == '\\');
  }
}

TEST(json_serialize_float_roundtrip) {
  double original = 1.0 / 3.0;
  JsonValue v;
  v.data.emplace<double>(original);
  std::string serialized = to_json(v);
  JsonValue parsed = parse_json(serialized);
  CHECK(parsed.is_number());
  double diff = parsed.as_number() - original;
  if (diff < 0)
    diff = -diff;
  CHECK(diff < 1e-14);
}

// ============================================================================
// operator[] overloads
// ============================================================================

TEST(json_operator_brackets_string_key) {
  JsonValue v = parse_json(R"({"name": "Alice", "age": 30})");
  auto name = v["name"];
  CHECK(name.has_value());
  CHECK_EQ(name->as_string(), std::string("Alice"));
  auto missing = v["missing"];
  CHECK(!missing.has_value());
}

TEST(json_operator_brackets_index) {
  JsonValue v = parse_json("[10, 20, 30]");
  auto elem = v[size_t(1)];
  CHECK(elem.has_value());
  CHECK_EQ(elem->as_number(), 20.0);
  auto oob = v[size_t(99)];
  CHECK(!oob.has_value());
}

// ============================================================================
// Additional escape sequences
// ============================================================================

TEST(json_parse_escape_backspace_formfeed_carriage) {
  JsonValue v = parse_json("\"a\\bb\\fc\\rd\"");
  CHECK(v.is_string());
  std::string s = v.as_string();
  CHECK_EQ(s, std::string("a\bb\fc\rd"));
}
