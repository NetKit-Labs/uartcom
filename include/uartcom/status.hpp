#pragma once

#include <cstdint>

namespace uartcom {

// Snapshot of the byte queues and the programmed baud generator.
// sysclk, reset source, and the C++ standard are board facts and live in
// Protocol::BoardInfo rather than here.
struct LinkStatus {
    std::uint32_t rx_bytes = 0;
    std::uint32_t tx_bytes = 0;
    std::uint32_t parity_errors = 0;
    std::uint32_t framing_errors = 0;
    std::uint32_t noise_errors = 0;
    std::uint32_t overrun_errors = 0;
    std::uint32_t software_overruns = 0;
    std::uint32_t kernel_clock_hz = 0;
    std::uint32_t baud_rate = 0;
    std::uint32_t actual_baud = 0;
    std::uint32_t brr = 0;
    std::int32_t error_ppm = 0;
    std::uint32_t rx_queued = 0;
    std::uint32_t tx_queued = 0;
};

}  // namespace uartcom
