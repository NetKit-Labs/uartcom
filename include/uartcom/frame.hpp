#pragma once

// Map a data-bit / parity choice onto USART_CR1 M, PCE, and PS.
//
// The hardware word length includes the parity bit when PCE = 1:
//   8N1 -> M=0 PCE=0
//   9N1 -> M=1 PCE=0
//   7E1 / 7O1 -> M=0 PCE=1
//   8E1 / 8O1 -> M=1 PCE=1
// Nine data bits plus parity does not fit in this USART, and 7 data bits
// with no parity is not a hardware word length.

#include "uartcom/config.hpp"
#include "uartcom/meta.hpp"
#include "uartcom/registers.hpp"

#include <cstdint>

namespace uartcom {

struct FrameBits {
    std::uint32_t cr1 = 0;
    bool valid = false;
};

[[nodiscard]] constexpr FrameBits encode_frame(DataBits data_bits, Parity parity) noexcept {
    const bool parity_on = parity != Parity::None;
    if (data_bits == DataBits::Nine && parity_on) {
        return {};
    }
    if (data_bits == DataBits::Seven && !parity_on) {
        return {};
    }
    if (data_bits != DataBits::Seven && data_bits != DataBits::Eight && data_bits != DataBits::Nine) {
        return {};
    }

    FrameBits bits;
    bits.valid = true;
    if (data_bits == DataBits::Nine || (data_bits == DataBits::Eight && parity_on)) {
        bits.cr1 |= reg::kCr1M;
    }
    if (parity_on) {
        bits.cr1 |= reg::kCr1Pce;
        if (parity == Parity::Odd) {
            bits.cr1 |= reg::kCr1Ps;
        }
    }
    return bits;
}

[[nodiscard]] constexpr std::uint32_t encode_stop_bits(StopBits stop_bits) noexcept {
    return static_cast<std::uint32_t>(to_underlying(stop_bits)) << reg::kCr2StopShift;
}

static_assert(encode_frame(DataBits::Eight, Parity::None).valid);
static_assert(encode_frame(DataBits::Eight, Parity::None).cr1 == 0);
static_assert((encode_frame(DataBits::Eight, Parity::Even).cr1 & reg::kCr1M) != 0);
static_assert((encode_frame(DataBits::Eight, Parity::Even).cr1 & reg::kCr1Pce) != 0);
static_assert((encode_frame(DataBits::Eight, Parity::Odd).cr1 & reg::kCr1Ps) != 0);
static_assert(!encode_frame(DataBits::Seven, Parity::None).valid);
static_assert(!encode_frame(DataBits::Nine, Parity::Even).valid);
static_assert(encode_stop_bits(StopBits::Two) == (2u << 12));

}  // namespace uartcom
