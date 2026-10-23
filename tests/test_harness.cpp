#include "test_harness.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace uartcom::test {
namespace {

struct Item {
    const char* name;
    void (*fn)();
};

std::vector<Item>& items() {
    static std::vector<Item> all;
    return all;
}

}  // namespace

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

void Registry::add(const char* name, void (*fn)()) { items().push_back(Item{name, fn}); }

int Registry::run() {
    int failed = 0;
    const auto& all = items();
    for (const auto& test : all) {
        try {
            test.fn();
            std::printf("ok   %s\n", test.name);
        } catch (const Failure& failure) {
            std::printf("FAIL %s\n     %s\n", test.name, failure.what());
            ++failed;
        } catch (const std::exception& failure) {
            std::printf("FAIL %s\n     exception: %s\n", test.name, failure.what());
            ++failed;
        }
    }
    std::printf("%zu tests, %d failed, __cplusplus=%ld\n", all.size(), failed, __cplusplus);
    return failed;
}

}  // namespace uartcom::test

int main() {
#ifndef UARTCOM_CXX_STANDARD
#error "UARTCOM_CXX_STANDARD must be set by the Makefile"
#endif
    static_assert(__cplusplus >= UARTCOM_CXX_STANDARD, "compiler did not honor the selected -std");
    return uartcom::test::Registry::instance().run() == 0 ? 0 : 1;
}
