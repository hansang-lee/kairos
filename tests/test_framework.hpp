#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Minimal test framework — registration, assertions, a runner.
 *
 * Deliberately not GTest. The project's only dependencies are libcurl and
 * nlohmann/json, and pulling in a test framework would mean either a sudo
 * install or a network fetch at configure time. The value here is in the tests,
 * not the harness, and this is enough harness: named cases, assertions that
 * report both values, and a non-zero exit so CI can gate on it.
 */
namespace testing {

struct AssertionFailure: std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct TestCase {
    std::string           suite;
    std::string           name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const std::string& suite, const std::string& name, std::function<void()> fn) {
        registry().push_back({suite, name, std::move(fn)});
    }
};

/** @param filter Substring match on "suite.name"; empty runs everything. */
inline int runAll(const std::string& filter) {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    std::string currentSuite;
    for (const auto& tc : registry()) {
        const std::string full = tc.suite + "." + tc.name;
        if (!filter.empty() && full.find(filter) == std::string::npos) {
            continue;
        }
        if (tc.suite != currentSuite) {
            currentSuite = tc.suite;
            std::cout << "\n[" << currentSuite << "]\n";
        }
        try {
            tc.fn();
            std::cout << "  pass  " << tc.name << "\n";
            ++passed;
        } catch (const AssertionFailure& e) {
            std::cout << "  FAIL  " << tc.name << "\n        " << e.what() << "\n";
            failures.push_back(full + ": " + e.what());
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "  ERROR " << tc.name << "\n        unexpected exception: " << e.what() << "\n";
            failures.push_back(full + ": unexpected exception: " + e.what());
            ++failed;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << " " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";
    for (const auto& f : failures) {
        std::cout << "  " << f << "\n";
    }
    return failed == 0 ? 0 : 1;
}

namespace detail {

template <typename T>
std::string show(const T& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

inline std::string show(const bool& v) {
    return v ? "true" : "false";
}

}  // namespace detail

}  // namespace testing

#define TEST(suite_name, test_name)                                                            \
    static void suite_name##_##test_name();                                                    \
    static ::testing::Registrar registrar_##suite_name##_##test_name(#suite_name, #test_name,  \
                                                                     suite_name##_##test_name); \
    static void suite_name##_##test_name()

#define FAIL_WITH(msg)                                                                         \
    do {                                                                                       \
        std::ostringstream _oss;                                                               \
        _oss << msg << "  (" << __FILE__ << ":" << __LINE__ << ")";                            \
        throw ::testing::AssertionFailure(_oss.str());                                         \
    } while (0)

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) FAIL_WITH("CHECK failed: " #cond);                                        \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                   \
    do {                                                                                       \
        if (!(cond)) FAIL_WITH("CHECK failed: " #cond " — " << msg);                           \
    } while (0)

#define CHECK_EQ(a, b)                                                                         \
    do {                                                                                       \
        const auto _a = (a);                                                                   \
        const auto _b = (b);                                                                   \
        if (!(_a == _b))                                                                       \
            FAIL_WITH("CHECK_EQ failed: " #a " == " #b "\n        got " << ::testing::detail::show(_a) \
                                                        << ", expected " << ::testing::detail::show(_b)); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                  \
    do {                                                                                       \
        const double _a = (a);                                                                 \
        const double _b = (b);                                                                 \
        if (std::fabs(_a - _b) > (tol))                                                        \
            FAIL_WITH("CHECK_NEAR failed: " #a " ~= " #b "\n        got " << _a << ", expected " << _b \
                                                      << " (tolerance " << (tol) << ")");      \
    } while (0)
