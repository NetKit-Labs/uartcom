#include "board.hpp"

namespace {

volatile std::uint32_t* const kRcc = reinterpret_cast<volatile std::uint32_t*>(0x40023800u);
volatile std::uint32_t* const kGpioA = reinterpret_cast<volatile std::uint32_t*>(0x40020000u);
volatile std::uint32_t* const kNvicIser = reinterpret_cast<volatile std::uint32_t*>(0xE000E100u);
volatile std::uint32_t* const kNvicIcpr = reinterpret_cast<volatile std::uint32_t*>(0xE000E280u);

constexpr unsigned kRccApb1Rstr = 0x20u / 4u;
constexpr unsigned kRccAhb1Enr = 0x30u / 4u;
constexpr unsigned kRccApb1Enr = 0x40u / 4u;
constexpr std::uint32_t kGpioAEn = 1u << 0;
constexpr std::uint32_t kUsart2En = 1u << 17;
constexpr std::uint32_t kUsart2Irq = 38u;
constexpr std::uint32_t kLedPin = 5u;

void update_field(volatile std::uint32_t& reg, std::uint32_t clear, std::uint32_t set) noexcept {
    reg = (reg & ~clear) | set;
}

}  // namespace

void board::prepare_usart2() {
    kRcc[kRccAhb1Enr] |= kGpioAEn;
    (void)kRcc[kRccAhb1Enr];

    // PA2 TX, PA3 RX, alternate function 7, pull-up so the line idles high.
    constexpr std::uint32_t kPins = (3u << (2 * 2)) | (3u << (3 * 2));
    constexpr std::uint32_t kAf = (2u << (2 * 2)) | (2u << (3 * 2));
    update_field(kGpioA[0], kPins, kAf);
    kGpioA[2] |= (3u << (2 * 2)) | (3u << (3 * 2));
    update_field(kGpioA[3], kPins, (1u << (2 * 2)) | (1u << (3 * 2)));

    constexpr std::uint32_t kAfrMask = (0xFu << (2 * 4)) | (0xFu << (3 * 4));
    constexpr std::uint32_t kAfrAf7 = (7u << (2 * 4)) | (7u << (3 * 4));
    update_field(kGpioA[8], kAfrMask, kAfrAf7);

    kRcc[kRccApb1Enr] |= kUsart2En;
    (void)kRcc[kRccApb1Enr];
    kRcc[kRccApb1Rstr] |= kUsart2En;
    kRcc[kRccApb1Rstr] &= ~kUsart2En;
}

void board::init_led() noexcept {
    kRcc[kRccAhb1Enr] |= kGpioAEn;
    (void)kRcc[kRccAhb1Enr];
    update_field(kGpioA[0], 3u << (kLedPin * 2), 1u << (kLedPin * 2));
    led(false);
}

void board::led(bool on) noexcept {
    // BSRR: low half sets, high half resets.
    kGpioA[6] = on ? (1u << kLedPin) : (1u << (kLedPin + 16));
}

void board::led_toggle() noexcept {
    const bool on = (kGpioA[5] & (1u << kLedPin)) != 0;
    led(!on);
}

void board::enable_usart2_irq() noexcept {
    const std::uint32_t word = kUsart2Irq >> 5;
    const std::uint32_t bit = 1u << (kUsart2Irq & 31u);
    kNvicIcpr[word] = bit;
    kNvicIser[word] = bit;
}
