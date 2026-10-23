#pragma once

// Integer baud-rate generator programming for the STM32F4 USART.
//
// OVER8 = 0:  baud = fck / BRR,           BRR = round(fck / baud)
// OVER8 = 1:  baud = fck / DIV,           DIV = round(fck / baud)
//             BRR mantissa = DIV[14:3],   BRR fraction = DIV[2:0], bit 3 stays 0
//
// Rounding is half-up. The error in ppm uses truncating-toward-zero division,
// which is the C++ rule for integers and the same rule the Python tests use.

#include "uartcom/config.hpp"
#include "uartcom/meta.hpp"

#include <cstdint>

namespace uartcom {

enum class BaudError : std::uint8_t {
    Ok = 0,
    ZeroClock,
    ZeroBaud,
    BaudTooHigh,
    DivisorTooLarge,
};

struct BaudProgram {
    std::uint32_t brr = 0;
    std::uint32_t actual_baud = 0;
    std::int32_t error_ppm = 0;
    std::uint32_t divisor = 0;
};

struct BaudResult {
    BaudProgram program{};
    BaudError error = BaudError::Ok;

    [[nodiscard]] constexpr bool ok() const noexcept { return error == BaudError::Ok; }
};

[[nodiscard]] constexpr BaudResult make_baud_error(BaudError error) noexcept {
    BaudResult result;
    result.error = error;
    return result;
}

[[nodiscard]] constexpr BaudResult compute_baud(std::uint32_t clock_hz, std::uint32_t baud_rate,
                                                Oversampling oversampling) noexcept {
    if (clock_hz == 0) {
        return make_baud_error(BaudError::ZeroClock);
    }
    if (baud_rate == 0) {
        return make_baud_error(BaudError::ZeroBaud);
    }

    const std::uint64_t divisor = round_div(clock_hz, baud_rate);
    const bool over8 = oversampling == Oversampling::By8;
    const std::uint64_t k_min = over8 ? 8ull : 16ull;
    const std::uint64_t k_max = over8 ? ((0xFFFull << 3) | 0x7ull) : 0xFFFFull;
    if (divisor < k_min) {
        return make_baud_error(BaudError::BaudTooHigh);
    }
    if (divisor > k_max) {
        return make_baud_error(BaudError::DivisorTooLarge);
    }

    BaudResult result;
    result.program.divisor = static_cast<std::uint32_t>(divisor);
    if (over8) {
        const auto mantissa = static_cast<std::uint32_t>(divisor >> 3);
        const auto fraction = static_cast<std::uint32_t>(divisor & 0x7ull);
        result.program.brr = (mantissa << 4) | fraction;
    } else {
        result.program.brr = static_cast<std::uint32_t>(divisor);
    }

    result.program.actual_baud = static_cast<std::uint32_t>(static_cast<std::uint64_t>(clock_hz) / divisor);
    const auto numerator = (static_cast<std::int64_t>(clock_hz) -
                            static_cast<std::int64_t>(baud_rate) * static_cast<std::int64_t>(divisor)) *
                           static_cast<std::int64_t>(1000000);
    const auto denominator = static_cast<std::int64_t>(baud_rate) * static_cast<std::int64_t>(divisor);
    result.program.error_ppm = static_cast<std::int32_t>(numerator / denominator);
    result.error = BaudError::Ok;
    return result;
}

static_assert(compute_baud(45000000, 115200, Oversampling::By16).program.brr == 391);
static_assert(compute_baud(45000000, 115200, Oversampling::By16).program.actual_baud == 115089);
static_assert(compute_baud(45000000, 115200, Oversampling::By16).program.error_ppm == -959);
static_assert(compute_baud(16000000, 115200, Oversampling::By16).program.brr == 139);
static_assert(compute_baud(16000000, 115200, Oversampling::By16).program.error_ppm == -799);
static_assert(compute_baud(45000000, 115200, Oversampling::By8).program.brr == 775);
static_assert(compute_baud(16000000, 9600, Oversampling::By16).program.brr == 1667);
static_assert(compute_baud(84000000, 9600, Oversampling::By16).program.error_ppm == 0);
static_assert(compute_baud(0, 115200, Oversampling::By16).error == BaudError::ZeroClock);
static_assert(compute_baud(16000000, 0, Oversampling::By16).error == BaudError::ZeroBaud);
static_assert(compute_baud(16000000, 3000000, Oversampling::By16).error == BaudError::BaudTooHigh);

}  // namespace uartcom
