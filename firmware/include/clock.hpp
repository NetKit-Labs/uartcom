#pragma once

// Clock tree programmed from RCC / PWR / FLASH registers. No HAL.

#include <cstdint>

namespace board {

enum class ClockSource : std::uint32_t {
    Hsi = 0,
    HseCrystal = 1,
    HseBypass = 2,
};

// Trivial on purpose: SystemInit fills this after .bss is cleared and before
// static constructors run. Do not give it a constructor.
struct ClockTree {
    std::uint32_t sysclk_hz;
    std::uint32_t hclk_hz;
    std::uint32_t pclk1_hz;
    std::uint32_t pclk2_hz;
    ClockSource source;
    std::uint32_t pll_enabled;
};

extern ClockTree g_clocks;

const char* clock_source_name(ClockSource source) noexcept;

}  // namespace board

extern "C" void SystemInit();
