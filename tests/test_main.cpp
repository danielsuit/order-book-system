#include "test_framework.h"

std::vector<TestCase>& get_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

int register_test(const std::string& name, std::function<void()> func) {
    get_tests().push_back({name, std::move(func)});
    return 0;
}

int main() {
    int passed = 0, failed = 0;
    for (auto& tc : get_tests()) {
        try {
            tc.func();
            std::cout << "  PASS: " << tc.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "  FAIL: " << tc.name << " — " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}
