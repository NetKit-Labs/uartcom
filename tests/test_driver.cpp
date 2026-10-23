#include "test_harness.hpp"

#include "fake_usart.hpp"

#include "uartcom/driver.hpp"

#include <vector>

namespace {

using Uart = uartcom::Driver<FakeUsart, 8, 8>;

uartcom::Config base_config() {
    uartcom::Config config;
    config.clock_hz = 45000000;
    config.baud_rate = 115200;
    return config;
}

UARTCOM_TEST(driver_init_programs_8n1) {
    FakeUsart hw;
    Uart uart{hw};
    CHECK(uart.init(base_config()));
    CHECK_EQ(hw.read_brr(), 391u);
    CHECK_EQ(hw.read_cr1(), uartcom::reg::kCr1Ue | uartcom::reg::kCr1Te | uartcom::reg::kCr1Re | uartcom::reg::kCr1Rxneie);
    CHECK_EQ(hw.read_cr1(), 0x202Cu);
    CHECK_EQ(hw.read_cr2(), 0u);
    CHECK_EQ(hw.read_cr3(), uartcom::reg::kCr3Eie);
    CHECK(!hw.lock_fault());
    const auto link = uart.status();
    CHECK_EQ(link.actual_baud, 115089u);
    CHECK_EQ(link.error_ppm, -959);
}

UARTCOM_TEST(driver_init_leaves_registers_on_bad_frame) {
    FakeUsart hw;
    hw.write_cr1(0xA5A5A5A5u);
    hw.write_brr(0x1111u);
    Uart uart{hw};
    auto config = base_config();
    config.data_bits = uartcom::DataBits::Seven;
    config.parity = uartcom::Parity::None;
    CHECK(!uart.init(config));
    CHECK_EQ(hw.read_cr1(), 0xA5A5A5A5u);
    CHECK_EQ(hw.read_brr(), 0x1111u);
    CHECK_EQ(uart.write(uartcom::Span<const std::uint8_t>{}), 0u);
}

UARTCOM_TEST(driver_tx_irq_then_idle) {
    FakeUsart hw;
    Uart uart{hw};
    CHECK(uart.init(base_config()));
    const std::uint8_t payload[] = {0x10, 0x20, 0x30};
    CHECK_EQ(uart.write(payload), 3u);
    CHECK((hw.read_cr1() & uartcom::reg::kCr1Txeie) != 0);
    drain_transmitter(uart, hw);
    CHECK(uart.tx_idle());
    CHECK_EQ(hw.transmitted().size(), 3u);
    CHECK_EQ(hw.transmitted()[0], 0x10u);
    CHECK_EQ(hw.transmitted()[2], 0x30u);
    CHECK_EQ(uart.status().tx_bytes, 3u);
    CHECK((hw.read_cr1() & uartcom::reg::kCr1Txeie) == 0);
    CHECK(!hw.lock_fault());
}

UARTCOM_TEST(driver_partial_write_when_tx_ring_is_full) {
    FakeUsart hw;
    Uart uart{hw};
    CHECK(uart.init(base_config()));
    std::uint8_t payload[20] = {};
    CHECK_EQ(uart.write(payload), 8u);
    CHECK_EQ(uart.tx_pending(), 8u);
    CHECK_EQ(uart.write(payload), 0u);
}

UARTCOM_TEST(driver_rx_software_overrun_keeps_oldest) {
    FakeUsart hw;
    Uart uart{hw};
    CHECK(uart.init(base_config()));
    for (std::uint8_t i = 0; i < 8; ++i) {
        hw.inject_rx(i);
        uart.service();
    }
    hw.inject_rx(0xEE);
    uart.service();
    CHECK_EQ(uart.status().software_overruns, 1u);
    CHECK_EQ(uart.status().rx_bytes, 8u);
    std::uint8_t got[8] = {};
    CHECK_EQ(uart.read(got), 8u);
    for (std::uint8_t i = 0; i < 8; ++i) {
        CHECK_EQ(got[i], i);
    }
}

UARTCOM_TEST(driver_hardware_overrun_and_line_errors) {
    FakeUsart hw;
    Uart uart{hw};
    CHECK(uart.init(base_config()));
    hw.inject_rx(0x11);
    hw.inject_rx(0x22);
    uart.service();
    CHECK_EQ(uart.status().overrun_errors, 1u);
    std::uint8_t byte = 0;
    CHECK_EQ(uart.read(uartcom::Span<std::uint8_t>{&byte, 1}), 1u);
    CHECK_EQ(byte, 0x11u);

    hw.inject_rx(0x55, uartcom::reg::kSrFe | uartcom::reg::kSrNf | uartcom::reg::kSrPe);
    uart.service();
    const auto link = uart.status();
    CHECK_EQ(link.framing_errors, 1u);
    CHECK_EQ(link.noise_errors, 1u);
    CHECK_EQ(link.parity_errors, 1u);
    CHECK_EQ(uart.read(uartcom::Span<std::uint8_t>{&byte, 1}), 1u);
    CHECK_EQ(byte, 0x55u);
}

UARTCOM_TEST(driver_polled_transmit_does_not_arm_txeie) {
    FakeUsart hw;
    Uart uart{hw};
    auto config = base_config();
    config.tx_interrupt = false;
    config.rx_interrupt = false;
    CHECK(uart.init(config));
    CHECK_EQ(hw.read_cr3(), 0u);
    const std::uint8_t payload[] = {0xAB};
    CHECK_EQ(uart.write(payload), 1u);
    CHECK((hw.read_cr1() & uartcom::reg::kCr1Txeie) == 0);
    hw.complete_shift();
    uart.service();
    CHECK_EQ(hw.transmitted().size(), 1u);
    CHECK(uart.tx_idle());
}

UARTCOM_TEST(driver_disable_clears_ue) {
    FakeUsart hw;
    Uart uart{hw};
    CHECK(uart.init(base_config()));
    uart.disable();
    CHECK_EQ(hw.read_cr1(), 0u);
    CHECK_EQ(hw.read_cr3(), 0u);
    const std::uint8_t payload[] = {1};
    CHECK_EQ(uart.write(payload), 0u);
}

}  // namespace
