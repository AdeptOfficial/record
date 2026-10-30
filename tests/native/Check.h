#pragma once

// Minimal test registry so the host tests need nothing beyond a C++17 compiler.
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Test {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Test>& registry() {
    static std::vector<Test> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline int runAll(const char* filter) {
    int run = 0;
    for (const Test& test : registry()) {
        if (filter != nullptr && std::string(test.name).find(filter) == std::string::npos) continue;
        const int before = failures();
        test.body();
        ++run;
        std::printf("%s %s\n", failures() == before ? "PASS" : "FAIL", test.name);
    }
    std::printf("%d tests, %d failed checks\n", run, failures());
    return failures() == 0 && run > 0 ? 0 : 1;
}

}  // namespace check

#define TEST(name) \
    static void name(); \
    static check::Register name##_registration(#name, name); \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ++check::failures();                                                      \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);    \
        }                                                                             \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                    \
    do {                                                                                         \
        const double check_a = (a), check_b = (b);                                               \
        if (!(std::fabs(check_a - check_b) <= (tol))) {                                          \
            ++check::failures();                                                                 \
            std::printf("  %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, check_a, check_b); \
        }                                                                                        \
    } while (0)
