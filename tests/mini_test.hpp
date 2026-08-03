#pragma once
#include <iostream>
#include <vector>
#include <functional>
#include <string>

struct MiniTestCase { std::string name; std::function<void()> fn; };

inline std::vector<MiniTestCase>& miniRegistry() { static std::vector<MiniTestCase> r; return r; }
inline int& miniFailures() { static int f = 0; return f; }

struct MiniRegistrar {
    MiniRegistrar(const std::string& n, std::function<void()> fn) { miniRegistry().push_back({n, fn}); }
};

#define TEST_CASE(name)                                   \
    static void name();                                   \
    static MiniRegistrar mini_reg_##name(#name, name);    \
    static void name()

#define CHECK(cond)                                                              \
    do { if (!(cond)) { ++miniFailures();                                        \
        std::cerr << "  CHECK failed: " #cond " (" << __FILE__ << ":"            \
                  << __LINE__ << ")\n"; } } while (0)

#define CHECK_THROWS(expr)                                                       \
    do { bool threw = false; try { (void)(expr); } catch (...) { threw = true; } \
        if (!threw) { ++miniFailures();                                          \
            std::cerr << "  CHECK_THROWS failed: " #expr " did not throw\n"; }   \
    } while (0)

inline int runAllTests() {
    for (auto& tc : miniRegistry()) {
        int before = miniFailures();
        tc.fn();
        std::cout << (miniFailures() == before ? "[PASS] " : "[FAIL] ") << tc.name << "\n";
    }
    std::cout << (miniFailures() == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return miniFailures() == 0 ? 0 : 1;
}
