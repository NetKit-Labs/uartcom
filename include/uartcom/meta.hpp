#pragma once

// Small language helpers shared by the driver and the firmware.
// C++23 facilities are used when this translation unit is built as C++23,
// with a C++20 / C++17 fallback so an older Arm compiler can still build.

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#if __cplusplus >= 202002L
#include <span>
#endif

namespace uartcom {

template <typename Enum>
[[nodiscard]] constexpr std::underlying_type_t<Enum> to_underlying(Enum value) noexcept {
#if defined(__cpp_lib_to_underlying) && __cpp_lib_to_underlying >= 202102L
    return std::to_underlying(value);
#else
    return static_cast<std::underlying_type_t<Enum>>(value);
#endif
}

// Round-half-up division for non-negative integers. `if consteval` keeps a
// zero denominator out of constant evaluation; the same check covers runtime.
[[nodiscard]] constexpr std::uint64_t round_div(std::uint64_t numerator, std::uint64_t denominator) noexcept {
#if __cplusplus >= 202302L
    if consteval {
        if (denominator == 0) {
            return 0;
        }
    }
#endif
    if (denominator == 0) {
        return 0;
    }
    return (numerator + denominator / 2) / denominator;
}

#if __cplusplus >= 202002L
template <typename T>
using Span = std::span<T>;
#else
template <typename T>
class Span {
   public:
    constexpr Span() noexcept : data_(nullptr), size_(0) {}
    constexpr Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}
    template <std::size_t N>
    constexpr Span(T (&array)[N]) noexcept : data_(array), size_(N) {}

    [[nodiscard]] constexpr T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr T& operator[](std::size_t index) const noexcept { return data_[index]; }
    [[nodiscard]] constexpr T* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr T* end() const noexcept { return data_ + size_; }
    [[nodiscard]] constexpr Span subspan(std::size_t offset) const noexcept {
        return Span{data_ + offset, size_ - offset};
    }

   private:
    T* data_;
    std::size_t size_;
};
#endif

}  // namespace uartcom
