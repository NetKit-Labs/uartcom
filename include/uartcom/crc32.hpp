#pragma once

// CRC-32/ISO-HDLC (the same polynomial as zlib.crc32): reflected poly
// 0xEDB88320, init 0xFFFFFFFF, xor-out 0xFFFFFFFF.

#include "uartcom/meta.hpp"

#include <cstdint>

namespace uartcom {

[[nodiscard]] constexpr std::uint32_t crc32(Span<const std::uint8_t> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

inline constexpr std::uint8_t kCrc32Check[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

static_assert(crc32({}) == 0u);
static_assert(crc32(Span<const std::uint8_t>{kCrc32Check, 9}) == 0xCBF43926u);

}  // namespace uartcom
