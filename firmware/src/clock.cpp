#include "clock.hpp"

// STM32F446 clock bring-up from RM0390.
//
// Target: SYSCLK 180 MHz from the PLL, APB1 (USART2 kernel clock) 45 MHz,
// APB2 90 MHz. 180 MHz needs voltage scale 1 and over-drive. The Nucleo
// normally feeds an 8 MHz MCO from the ST-Link into OSC_IN, which is HSE
// bypass mode. If that clock is absent the same PLL ratio is taken from HSI.
// If the PLL or over-drive does not lock, SYSCLK stays on HSI at 16 MHz so a
// host can still talk to the board.

namespace {

volatile std::uint32_t* const kRcc = reinterpret_cast<volatile std::uint32_t*>(0x40023800u);
volatile std::uint32_t* const kFlashAcr = reinterpret_cast<volatile std::uint32_t*>(0x40023C00u);
volatile std::uint32_t* const kPwr = reinterpret_cast<volatile std::uint32_t*>(0x40007000u);
volatile std::uint32_t* const kCpacr = reinterpret_cast<volatile std::uint32_t*>(0xE000ED88u);

constexpr std::uint32_t kCrHsiRdy = 1u << 1;
constexpr std::uint32_t kCrHseOn = 1u << 16;
constexpr std::uint32_t kCrHseRdy = 1u << 17;
constexpr std::uint32_t kCrHseByp = 1u << 18;
constexpr std::uint32_t kCrPllOn = 1u << 24;
constexpr std::uint32_t kCrPllRdy = 1u << 25;

constexpr std::uint32_t kCfgrSwPll = 2u;
constexpr std::uint32_t kCfgrSwsMask = 3u << 2;
constexpr std::uint32_t kCfgrSwsPll = 2u << 2;
constexpr std::uint32_t kCfgrPpre1Div4 = 5u << 10;
constexpr std::uint32_t kCfgrPpre2Div2 = 4u << 13;

constexpr std::uint32_t kApb1EnrPwr = 1u << 28;
constexpr unsigned kRccApb1Enr = 0x40u / 4u;

constexpr std::uint32_t kPwrVosMask = 3u << 14;
constexpr std::uint32_t kPwrVosScale1 = 3u << 14;
constexpr std::uint32_t kPwrOden = 1u << 16;
constexpr std::uint32_t kPwrOdswen = 1u << 17;
constexpr std::uint32_t kPwrVosRdy = 1u << 14;
constexpr std::uint32_t kPwrOdRdy = 1u << 16;
constexpr std::uint32_t kPwrOdswRdy = 1u << 17;

constexpr std::uint32_t kFlashLatency5 = 5u;
constexpr std::uint32_t kFlashPrft = 1u << 8;
constexpr std::uint32_t kFlashIcen = 1u << 9;
constexpr std::uint32_t kFlashDcen = 1u << 10;
constexpr std::uint32_t kFlashCaches = kFlashPrft | kFlashIcen | kFlashDcen;

constexpr std::uint32_t kSpins = 2000000u;

bool wait_set(volatile std::uint32_t& reg, std::uint32_t mask) noexcept {
    for (std::uint32_t spins = kSpins; spins > 0; --spins) {
        if ((reg & mask) != 0) {
            return true;
        }
    }
    return false;
}

bool wait_clear(volatile std::uint32_t& reg, std::uint32_t mask) noexcept {
    for (std::uint32_t spins = kSpins; spins > 0; --spins) {
        if ((reg & mask) == 0) {
            return true;
        }
    }
    return false;
}

void pll_off() noexcept {
    kRcc[0] &= ~kCrPllOn;
    (void)wait_clear(kRcc[0], kCrPllRdy);
}

void hse_off() noexcept {
    kRcc[0] &= ~kCrHseOn;
    (void)wait_clear(kRcc[0], kCrHseRdy);
}

bool start_hse(bool bypass) noexcept {
    hse_off();
    if (bypass) {
        kRcc[0] |= kCrHseByp;
    } else {
        kRcc[0] &= ~kCrHseByp;
    }
    kRcc[0] |= kCrHseOn;
    return wait_set(kRcc[0], kCrHseRdy);
}

std::uint32_t pllcfgr(bool hse, std::uint32_t n) noexcept {
    // PLLM = 8, PLLP = /2 (bits 17:16 = 0), PLLQ = 7, PLLN = n, source in bit 22.
    // HSE 8 MHz / 8 * 360 / 2 = 180 MHz. HSI 16 MHz / 8 * 180 / 2 = 180 MHz.
    return (7u << 24) | (hse ? (1u << 22) : 0u) | (n << 6) | 8u;
}

void remember_hsi() noexcept {
    board::g_clocks.sysclk_hz = 16000000u;
    board::g_clocks.hclk_hz = 16000000u;
    board::g_clocks.pclk1_hz = 16000000u;
    board::g_clocks.pclk2_hz = 16000000u;
    board::g_clocks.source = board::ClockSource::Hsi;
    board::g_clocks.pll_enabled = 0;
    kRcc[2] = 0;
    (void)wait_clear(kRcc[2], kCfgrSwsMask);
    pll_off();
    *kFlashAcr = (*kFlashAcr & ~0xFu) | kFlashCaches;
}

}  // namespace

board::ClockTree board::g_clocks;

const char* board::clock_source_name(ClockSource source) noexcept {
    switch (source) {
        case ClockSource::HseBypass:
            return "hse_bypass";
        case ClockSource::HseCrystal:
            return "hse_crystal";
        case ClockSource::Hsi:
            return "hsi";
    }
    return "hsi";
}

extern "C" void SystemInit() {
    *kCpacr |= 0xFu << 20;
    asm volatile("dsb" ::: "memory");
    asm volatile("isb" ::: "memory");

    (void)wait_set(kRcc[0], kCrHsiRdy);

    kRcc[kRccApb1Enr] |= kApb1EnrPwr;
    (void)kRcc[kRccApb1Enr];

    kPwr[0] = (kPwr[0] & ~kPwrVosMask) | kPwrVosScale1;
    (void)wait_set(kPwr[1], kPwrVosRdy);

    *kFlashAcr = (*kFlashAcr & ~0xFu) | kFlashLatency5 | kFlashCaches;

    board::ClockSource source = board::ClockSource::Hsi;
    bool pll_from_hse = false;
    if (start_hse(true)) {
        source = board::ClockSource::HseBypass;
        pll_from_hse = true;
    } else if (start_hse(false)) {
        source = board::ClockSource::HseCrystal;
        pll_from_hse = true;
    }

    pll_off();
    kRcc[1] = pllcfgr(pll_from_hse, pll_from_hse ? 360u : 180u);
    kRcc[0] |= kCrPllOn;
    if (!wait_set(kRcc[0], kCrPllRdy)) {
        remember_hsi();
        return;
    }

    kPwr[0] |= kPwrOden;
    if (!wait_set(kPwr[1], kPwrOdRdy)) {
        remember_hsi();
        return;
    }
    kPwr[0] |= kPwrOdswen;
    if (!wait_set(kPwr[1], kPwrOdswRdy)) {
        remember_hsi();
        return;
    }

    kRcc[2] = kCfgrPpre1Div4 | kCfgrPpre2Div2 | kCfgrSwPll;
    if (!wait_set(kRcc[2], kCfgrSwsPll) || (kRcc[2] & kCfgrSwsMask) != kCfgrSwsPll) {
        remember_hsi();
        return;
    }

    board::g_clocks.sysclk_hz = 180000000u;
    board::g_clocks.hclk_hz = 180000000u;
    board::g_clocks.pclk1_hz = 45000000u;
    board::g_clocks.pclk2_hz = 90000000u;
    board::g_clocks.source = source;
    board::g_clocks.pll_enabled = 1u;
}
