#pragma once

#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
#include <functional>
#include <sstream>
#include <stdexcept>

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& test_list() {
    static std::vector<TestCase> v;
    return v;
}

inline int run_tests() {
    int pass = 0, fail = 0;
    for (auto& t : test_list()) {
        try {
            t.fn();
            std::cout << "[pass] " << t.name << "\n";
            ++pass;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
            ++fail;
        } catch (...) {
            std::cout << "[FAIL] " << t.name << ": unknown exception\n";
            ++fail;
        }
    }
    std::cout << "\n" << pass + fail << " tests: " << pass << " passed, "
              << fail << " failed\n";
    return fail > 0 ? 1 : 0;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        test_list().push_back({name, fn});
    }
};

// Register a test function
#define TEST(name) \
    static void _test_fn_##name(); \
    static TestRegistrar _reg_##name(#name, _test_fn_##name); \
    static void _test_fn_##name()

// Assertion macros -- throw on failure (caught by run_tests)
#define CHECK(expr) \
    do { if (!(expr)) { \
        std::ostringstream _s; \
        _s << "CHECK(" #expr ") at " __FILE__ ":" << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define CHECK_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); \
        using _CT = std::common_type_t<decltype(_a), decltype(_b)>; \
        if (static_cast<_CT>(_a) != static_cast<_CT>(_b)) { \
        std::ostringstream _s; \
        _s << "CHECK_EQ(" #a ", " #b ") at " __FILE__ ":" << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define CHECK_THROWS(expr) \
    do { bool _threw = false; \
        try { (expr); } catch (...) { _threw = true; } \
        if (!_threw) { \
            std::ostringstream _s; \
            _s << "CHECK_THROWS(" #expr ") did not throw at " __FILE__ ":" << __LINE__; \
            throw std::runtime_error(_s.str()); \
        } \
    } while(0)
