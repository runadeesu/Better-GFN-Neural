#pragma once
// Minimal self-contained unit test framework (no external dependencies).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace bgntest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

inline int& failureCount() {
    static int n = 0;
    return n;
}

inline void fail(const char* file, int line, const std::string& msg) {
    std::printf("    FAILED %s:%d: %s\n", file, line, msg.c_str());
    ++failureCount();
}

inline int runAll(const char* filter = nullptr) {
    int failedTests = 0, run = 0;
    for (auto& t : registry()) {
        if (filter && std::string(t.name).find(filter) == std::string::npos) continue;
        int before = failureCount();
        std::printf("[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (const std::exception& e) {
            fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
        } catch (...) {
            fail(__FILE__, __LINE__, "unknown exception");
        }
        ++run;
        bool ok = failureCount() == before;
        if (!ok) ++failedTests;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d tests, %d failed\n", run, failedTests);
    return failedTests == 0 ? 0 : 1;
}

} // namespace bgntest

#define BGN_CAT2(a, b) a##b
#define BGN_CAT(a, b) BGN_CAT2(a, b)
#define TEST_CASE(name)                                                                          \
    static void BGN_CAT(test_fn_, __LINE__)();                                                   \
    static ::bgntest::Registrar BGN_CAT(test_reg_, __LINE__)(name, &BGN_CAT(test_fn_, __LINE__)); \
    static void BGN_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) ::bgntest::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        auto _va = (a);                                                                             \
        auto _vb = (b);                                                                             \
        if (!(_va == _vb)) ::bgntest::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")");         \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                          \
    do {                                                                                                               \
        double _va = double(a), _vb = double(b);                                                                       \
        if (!(std::fabs(_va - _vb) <= (eps)))                                                                          \
            ::bgntest::fail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ") " + std::to_string(_va) + " vs " + std::to_string(_vb)); \
    } while (0)
