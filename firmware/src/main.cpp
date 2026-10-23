#include "board.hpp"
#include "clock.hpp"
#include "mmio_usart.hpp"

#include "uartcom/config.hpp"
#include "uartcom/driver.hpp"
#include "uartcom/protocol.hpp"
#include "uartcom/registers.hpp"

#include <cstddef>
#include <cstdint>

// Lives in .data. INFO reports data=1 only when the startup copy ran.
std::uint32_t g_data_anchor = 0x446u;

namespace {

struct CtorProbe {
    std::uint32_t value;
    CtorProbe() : value(0xC0A15u) {}
};

CtorProbe g_probe;
board::MmioUsart g_mmio{uartcom::reg::kUsart2Base};
uartcom::Driver<board::MmioUsart, 1024, 1024> g_uart{g_mmio};

void wait_activity() noexcept { asm volatile("wfe" ::: "memory"); }

void send_all(uartcom::Span<const std::uint8_t> data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        offset += g_uart.write(data.subspan(offset));
        if (!g_uart.tx_idle()) {
            wait_activity();
        }
    }
    while (!g_uart.tx_idle()) {
        wait_activity();
    }
}

uartcom::BoardInfo board_info() {
    uartcom::BoardInfo info;
    info.source = board::clock_source_name(board::g_clocks.source);
    info.sysclk_hz = board::g_clocks.sysclk_hz;
    info.cpp_standard = static_cast<std::uint32_t>(__cplusplus);
    info.constructors_ran = g_probe.value == 0xC0A15u ? 1u : 0u;
    info.pll_enabled = board::g_clocks.pll_enabled;
    info.data_ok = g_data_anchor == 0x446u ? 1u : 0u;
    return info;
}

}  // namespace

extern "C" void USART2_IRQHandler() {
    g_uart.service();
    asm volatile("sev" ::: "memory");
}

extern "C" void fault_blink();

extern "C" int firmware_main() {
    board::prepare_usart2();
    board::init_led();

    uartcom::Config config;
    config.clock_hz = board::g_clocks.pclk1_hz;
    config.baud_rate = 115200;
    config.data_bits = uartcom::DataBits::Eight;
    config.parity = uartcom::Parity::None;
    config.stop_bits = uartcom::StopBits::One;
    config.oversampling = uartcom::Oversampling::By16;
    config.transmitter = true;
    config.receiver = true;
    config.rx_interrupt = true;
    config.tx_interrupt = true;

    if (!g_uart.init(config)) {
        fault_blink();
    }
    board::enable_usart2_irq();
    board::led(true);

    uartcom::Protocol protocol({"NUCLEO-F446RE", "STM32F446RE", "USART2"});
    constexpr char kBanner[] = "UARTCOM STM32F446RE USART2 ready\n";
    send_all(uartcom::Span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(kBanner), sizeof(kBanner) - 1});

    while (true) {
        std::uint8_t incoming[64];
        const std::size_t got = g_uart.read(incoming);
        if (got == 0) {
            wait_activity();
            continue;
        }
        for (std::size_t i = 0; i < got; ++i) {
            auto link = g_uart.status();
            protocol.set_board(board_info());
            protocol.set_link(link);
            protocol.ingest(incoming[i]);
            if (protocol.take_clear_request()) {
                g_uart.clear_counters();
            }
            bool answered = false;
            std::uint8_t outgoing[128];
            std::size_t produced = 0;
            while ((produced = protocol.read_tx(outgoing)) > 0) {
                answered = true;
                send_all(uartcom::Span<const std::uint8_t>{outgoing, produced});
            }
            if (answered) {
                board::led_toggle();
            }
        }
    }
}
