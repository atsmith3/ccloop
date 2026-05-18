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

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase> &test_list() {
  static std::vector<TestCase> v;
  return v;
}

inline int run_tests() {
  int pass = 0, fail = 0;
  for (auto &t : test_list()) {
    try {
      t.fn();
      std::cout << "[pass] " << t.name << "\n";
      ++pass;
    } catch (const std::exception &e) {
      std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
      ++fail;
    } catch (...) {
      std::cout << "[FAIL] " << t.name << ": unknown exception\n";
      ++fail;
    }
  }
  std::cout << "\n"
            << pass + fail << " tests: " << pass << " passed, " << fail
            << " failed\n";
  return fail > 0 ? 1 : 0;
}

struct TestRegistrar {
  TestRegistrar(const char *name, std::function<void()> fn) {
    test_list().push_back({name, fn});
  }
};

// Register a test function
#define TEST(name)                                                             \
  static void _test_fn_##name();                                               \
  static TestRegistrar _reg_##name(#name, _test_fn_##name);                    \
  static void _test_fn_##name()

// Assertion macros -- throw on failure (caught by run_tests)
#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::ostringstream _s;                                                   \
      _s << "CHECK(" #expr ") at " __FILE__ ":" << __LINE__;                   \
      throw std::runtime_error(_s.str());                                      \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    auto _a = (a);                                                             \
    auto _b = (b);                                                             \
    using _CT = std::common_type_t<decltype(_a), decltype(_b)>;                \
    if (static_cast<_CT>(_a) != static_cast<_CT>(_b)) {                        \
      std::ostringstream _s;                                                   \
      _s << "CHECK_EQ(" #a ", " #b ") at " __FILE__ ":" << __LINE__;           \
      throw std::runtime_error(_s.str());                                      \
    }                                                                          \
  } while (0)

#define CHECK_THROWS(expr)                                                     \
  do {                                                                         \
    bool _threw = false;                                                       \
    try {                                                                      \
      (expr);                                                                  \
    } catch (...) {                                                            \
      _threw = true;                                                           \
    }                                                                          \
    if (!_threw) {                                                             \
      std::ostringstream _s;                                                   \
      _s << "CHECK_THROWS(" #expr ") did not throw at " __FILE__ ":"           \
         << __LINE__;                                                          \
      throw std::runtime_error(_s.str());                                      \
    }                                                                          \
  } while (0)
