#pragma once

#include "uartcom/meta.hpp"

#include <exception>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace uartcom::test {

class Failure : public std::exception {
   public:
    explicit Failure(std::string message) : message_(std::move(message)) {}
    [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

   private:
    std::string message_;
};

struct Registry {
    static Registry& instance();
    void add(const char* name, void (*fn)());
    int run();
};

struct AutoReg {
    AutoReg(const char* name, void (*fn)()) { Registry::instance().add(name, fn); }
};

template <typename T>
std::string format_value(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<long long>(to_underlying(value)));
    } else if constexpr (std::is_integral_v<T>) {
        return std::to_string(static_cast<long long>(value));
    } else {
        std::ostringstream out;
        out << value;
        return out.str();
    }
}

template <typename A, typename B>
std::string eq_message(const char* left, const char* right, const A& a, const B& b, const char* file, int line) {
    std::ostringstream out;
    out << file << ":" << line << "  " << left << " == " << right << "  (" << format_value(a) << " vs "
        << format_value(b) << ")";
    return out.str();
}

}  // namespace uartcom::test

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            throw uartcom::test::Failure(std::string(#cond) + " failed at " __FILE__ ":" +               \
                                         std::to_string(__LINE__));                                      \
        }                                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                                   \
    do {                                                                                                 \
        const auto& _uartcom_a = (a);                                                                    \
        const auto& _uartcom_b = (b);                                                                    \
        if (!(_uartcom_a == _uartcom_b)) {                                                               \
            throw uartcom::test::Failure(                                                               \
                uartcom::test::eq_message(#a, #b, _uartcom_a, _uartcom_b, __FILE__, __LINE__));          \
        }                                                                                                \
    } while (0)

#define UARTCOM_TEST(name)                                                                               \
    static void name();                                                                                  \
    static const uartcom::test::AutoReg reg_##name {#name, &name};                                       \
    static void name()
