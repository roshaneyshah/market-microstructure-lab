#pragma once

// Minimal dependency-free test harness. Tests self-register at static init
// time; `mml_tests [filter]` runs every test whose name contains `filter`.

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mml::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

inline int& check_count() {
    static int n = 0;
    return n;
}

template <class A, class B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    ++check_count();
    if (!(a == b)) {
        std::ostringstream os;
        os << file << ":" << line << ": CHECK_EQ(" << ea << ", " << eb << ") failed";
        if constexpr (requires(std::ostream& o) { o << a << b; }) {
            os << " [" << a << " vs " << b << "]";
        }
        throw Failure{os.str()};
    }
}

inline void check_near(double a, double b, double tol, const char* ea, const char* eb, const char* file, int line) {
    ++check_count();
    if (!(std::fabs(a - b) <= tol)) {
        std::ostringstream os;
        os << file << ":" << line << ": CHECK_NEAR(" << ea << ", " << eb << ") failed [" << a << " vs " << b
           << ", tol " << tol << "]";
        throw Failure{os.str()};
    }
}

}  // namespace mml::test

#define MML_CONCAT_INNER(a, b) a##b
#define MML_CONCAT(a, b) MML_CONCAT_INNER(a, b)

#define TEST(name)                                                                                  \
    static void MML_CONCAT(test_fn_, name)();                                                       \
    static const ::mml::test::Registrar MML_CONCAT(test_reg_, name){#name, &MML_CONCAT(test_fn_, name)}; \
    static void MML_CONCAT(test_fn_, name)()

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        ++::mml::test::check_count();                                                                \
        if (!(cond)) {                                                                               \
            throw ::mml::test::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) +     \
                                       ": CHECK(" #cond ") failed"};                                \
        }                                                                                            \
    } while (0)

#define CHECK_EQ(a, b) ::mml::test::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) ::mml::test::check_near((a), (b), (tol), #a, #b, __FILE__, __LINE__)
