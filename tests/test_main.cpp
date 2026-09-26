#include <chrono>
#include <cstring>
#include <iostream>

#include "test_framework.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : "";
    int passed = 0;
    int failed = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& t : mml::test::registry()) {
        if (std::strstr(t.name.c_str(), filter) == nullptr) {
            continue;
        }
        try {
            t.fn();
            ++passed;
            std::cout << "[ PASS ] " << t.name << "\n";
        } catch (const mml::test::Failure& f) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << "\n         " << f.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[ FAIL ] " << t.name << "\n         unexpected exception: " << e.what() << "\n";
        }
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "\n" << passed << " passed, " << failed << " failed, " << mml::test::check_count()
              << " checks in " << ms << " ms\n";
    return failed == 0 ? 0 : 1;
}
