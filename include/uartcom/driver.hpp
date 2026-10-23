#pragma once

// Interrupt-driven USART driver.
//
// The hardware is a template argument so host tests can supply a fake register
// block. On the board, MmioUsart turns these calls into volatile loads and
// stores. Nothing in this file calls the STM32 HAL.
//
// Call init() only after the RCC has enabled the USART clock. Enable the NVIC
// line after init() returns. The IRQ handler should call service().

#include "uartcom/baud.hpp"
#include "uartcom/config.hpp"
#include "uartcom/frame.hpp"
#include "uartcom/meta.hpp"
#include "uartcom/registers.hpp"
#include "uartcom/ring_buffer.hpp"
#include "uartcom/status.hpp"

#include <cstddef>
#include <cstdint>
#if __cplusplus >= 202002L
#include <concepts>
#endif

namespace uartcom {

#if __cplusplus >= 202002L
template <typename Hw>
concept UsartHw = requires(Hw& hw, std::uint32_t value) {
    { hw.read_sr() } -> std::convertible_to<std::uint32_t>;
    { hw.read_dr() } -> std::convertible_to<std::uint32_t>;
    hw.write_dr(value);
    { hw.read_brr() } -> std::convertible_to<std::uint32_t>;
    hw.write_brr(value);
    { hw.read_cr1() } -> std::convertible_to<std::uint32_t>;
    hw.write_cr1(value);
    { hw.read_cr2() } -> std::convertible_to<std::uint32_t>;
    hw.write_cr2(value);
    { hw.read_cr3() } -> std::convertible_to<std::uint32_t>;
    hw.write_cr3(value);
    hw.lock();
    hw.unlock();
};
#endif

template <typename Hw>
class IrqLock {
   public:
    explicit IrqLock(Hw& hw) noexcept : hw_(hw) { hw_.lock(); }
    ~IrqLock() { hw_.unlock(); }
    IrqLock(const IrqLock&) = delete;
    IrqLock& operator=(const IrqLock&) = delete;

   private:
    Hw& hw_;
};

#if __cplusplus >= 202002L
template <UsartHw Hw, std::size_t kRxDepth, std::size_t kTxDepth>
#else
template <typename Hw, std::size_t kRxDepth, std::size_t kTxDepth>
#endif
class Driver {
    static_assert(kRxDepth >= 2 && (kRxDepth & (kRxDepth - 1)) == 0, "RX depth must be a power of two");
    static_assert(kTxDepth >= 2 && (kTxDepth & (kTxDepth - 1)) == 0, "TX depth must be a power of two");

   public:
    explicit Driver(Hw& hw) noexcept : hw_(hw) {}

    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    // Program BRR, CR2, CR3, then CR1. UE is set in the final CR1 write.
    // Returns false, and does not touch registers, when the frame or baud is illegal.
    [[nodiscard]] bool init(const Config& config) {
        const FrameBits frame = encode_frame(config.data_bits, config.parity);
        if (!frame.valid) {
            return false;
        }
        const BaudResult baud = compute_baud(config.clock_hz, config.baud_rate, config.oversampling);
        if (!baud.ok()) {
            return false;
        }

        IrqLock lock{hw_};
        started_ = false;
        config_ = config;
        baud_ = baud.program;
        counters_ = {};
        rx_.clear();
        tx_.clear();

        hw_.write_cr1(0);
        hw_.write_cr2(encode_stop_bits(config.stop_bits));
        const bool want_rx_irq = config.receiver && config.rx_interrupt;
        hw_.write_cr3(want_rx_irq ? reg::kCr3Eie : 0);
        hw_.write_brr(baud.program.brr);

        // A stale RXNE/ORE would fire as soon as RXNEIE is set.
        const std::uint32_t pending = hw_.read_sr();
        if ((pending & (reg::kSrRxne | reg::kSrOre | reg::kSrLineErrors)) != 0) {
            (void)hw_.read_dr();
        }

        std::uint32_t cr1 = reg::kCr1Ue | frame.cr1;
        if (config.transmitter) {
            cr1 |= reg::kCr1Te;
        }
        if (config.receiver) {
            cr1 |= reg::kCr1Re;
        }
        if (want_rx_irq) {
            cr1 |= reg::kCr1Rxneie;
        }
        if (config.oversampling == Oversampling::By8) {
            cr1 |= reg::kCr1Over8;
        }
        hw_.write_cr1(cr1);
        started_ = true;
        return true;
    }

    void disable() {
        IrqLock lock{hw_};
        hw_.write_cr1(0);
        hw_.write_cr3(0);
        started_ = false;
    }

    // Queue bytes and arm TXEIE when interrupt transmit is enabled.
    // Returns how many bytes fit in the TX ring.
    [[nodiscard]] std::size_t write(Span<const std::uint8_t> data) {
        IrqLock lock{hw_};
        if (!started_ || !config_.transmitter) {
            return 0;
        }
        std::size_t accepted = 0;
        for (const auto byte : data) {
            if (!tx_.push(byte)) {
                break;
            }
            ++accepted;
        }
        if (accepted > 0 && config_.tx_interrupt) {
            hw_.write_cr1(hw_.read_cr1() | reg::kCr1Txeie);
        }
        return accepted;
    }

    [[nodiscard]] std::size_t read(Span<std::uint8_t> data) {
        IrqLock lock{hw_};
        std::size_t count = 0;
        while (count < data.size()) {
            std::uint8_t byte = 0;
            if (!rx_.pop(byte)) {
                break;
            }
            data[count++] = byte;
        }
        return count;
    }

    // Move one TX byte and/or one RX byte. Call this from the USART IRQ,
    // or from a polled main loop when interrupts are left disabled.
    void service() {
        if (!started_) {
            return;
        }
        IrqLock lock{hw_};
        receive();
        transmit();
    }

    [[nodiscard]] bool tx_idle() {
        IrqLock lock{hw_};
        if (!started_) {
            return false;
        }
        const bool armed = (hw_.read_cr1() & reg::kCr1Txeie) != 0;
        return tx_.empty() && !armed;
    }

    [[nodiscard]] std::uint32_t tx_pending() {
        IrqLock lock{hw_};
        return tx_.size();
    }

    [[nodiscard]] std::uint32_t rx_available() {
        IrqLock lock{hw_};
        return rx_.size();
    }

    [[nodiscard]] LinkStatus status() {
        IrqLock lock{hw_};
        LinkStatus link;
        link.rx_bytes = counters_.rx_bytes;
        link.tx_bytes = counters_.tx_bytes;
        link.parity_errors = counters_.parity_errors;
        link.framing_errors = counters_.framing_errors;
        link.noise_errors = counters_.noise_errors;
        link.overrun_errors = counters_.overrun_errors;
        link.software_overruns = counters_.software_overruns;
        link.kernel_clock_hz = config_.clock_hz;
        link.baud_rate = config_.baud_rate;
        link.actual_baud = baud_.actual_baud;
        link.brr = baud_.brr;
        link.error_ppm = baud_.error_ppm;
        link.rx_queued = rx_.size();
        link.tx_queued = tx_.size();
        return link;
    }

    void clear_counters() {
        IrqLock lock{hw_};
        counters_ = {};
    }

   private:
    struct Counters {
        std::uint32_t rx_bytes = 0;
        std::uint32_t tx_bytes = 0;
        std::uint32_t parity_errors = 0;
        std::uint32_t framing_errors = 0;
        std::uint32_t noise_errors = 0;
        std::uint32_t overrun_errors = 0;
        std::uint32_t software_overruns = 0;
    };

    // SR was already sampled. Reading DR clears RXNE, ORE, FE, NF, and PE.
    void receive() {
        const std::uint32_t cr1 = hw_.read_cr1();
        if ((cr1 & reg::kCr1Re) == 0) {
            return;
        }
        const std::uint32_t sr = hw_.read_sr();
        const bool rxne = (sr & reg::kSrRxne) != 0;
        const bool overrun = (sr & reg::kSrOre) != 0;
        const bool line_error = (sr & reg::kSrLineErrors) != 0;
        if (!rxne && !overrun && !line_error) {
            return;
        }
        const std::uint32_t data = hw_.read_dr();
        if ((sr & reg::kSrPe) != 0) {
            ++counters_.parity_errors;
        }
        if ((sr & reg::kSrFe) != 0) {
            ++counters_.framing_errors;
        }
        if ((sr & reg::kSrNf) != 0) {
            ++counters_.noise_errors;
        }
        if (overrun) {
            ++counters_.overrun_errors;
        }
        if (!rxne) {
            return;
        }
        const auto byte = static_cast<std::uint8_t>(data & 0xFFu);
        if (!rx_.push(byte)) {
            ++counters_.software_overruns;
        } else {
            ++counters_.rx_bytes;
        }
    }

    // TXE means the data register can take another byte. TXEIE is cleared when
    // the software queue is empty so the level-sensitive IRQ does not spin.
    // The second pop covers a byte that arrived after TXEIE was cleared.
    void transmit() {
        if ((hw_.read_sr() & reg::kSrTxe) == 0) {
            return;
        }
        std::uint8_t byte = 0;
        if (tx_.pop(byte)) {
            hw_.write_dr(byte);
            ++counters_.tx_bytes;
            return;
        }
        const std::uint32_t cr1 = hw_.read_cr1();
        if ((cr1 & reg::kCr1Txeie) == 0) {
            return;
        }
        hw_.write_cr1(cr1 & ~reg::kCr1Txeie);
        if (tx_.pop(byte)) {
            hw_.write_dr(byte);
            ++counters_.tx_bytes;
            hw_.write_cr1(cr1 | reg::kCr1Txeie);
        }
    }

    Hw& hw_;
    Config config_{};
    BaudProgram baud_{};
    Counters counters_{};
    RingBuffer<kRxDepth> rx_{};
    RingBuffer<kTxDepth> tx_{};
    bool started_ = false;
};

}  // namespace uartcom
