#pragma once

// In-memory USART. TXE/TC and RXNE/ORE follow the STM32F4 rules closely
// enough for the driver tests: a DR write clears TXE, complete_shift() models
// the byte moving into the shift register, and a second byte that arrives
// while RXNE is set raises ORE and is dropped.

#include "uartcom/registers.hpp"

#include <cstdint>
#include <vector>

class FakeUsart {
   public:
    FakeUsart() : sr_(uartcom::reg::kSrTxe | uartcom::reg::kSrTc) {}

    [[nodiscard]] std::uint32_t read_sr() noexcept { return sr_; }
    [[nodiscard]] std::uint32_t read_dr() noexcept {
        const auto value = dr_;
        sr_ &= ~(uartcom::reg::kSrRxne | uartcom::reg::kSrOre | uartcom::reg::kSrPe | uartcom::reg::kSrFe |
                 uartcom::reg::kSrNf | uartcom::reg::kSrIdle);
        return value;
    }
    void write_dr(std::uint32_t value) {
        tx_log_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        sr_ &= ~(uartcom::reg::kSrTxe | uartcom::reg::kSrTc);
    }

    [[nodiscard]] std::uint32_t read_brr() const noexcept { return brr_; }
    void write_brr(std::uint32_t value) noexcept { brr_ = value; }
    [[nodiscard]] std::uint32_t read_cr1() const noexcept { return cr1_; }
    void write_cr1(std::uint32_t value) noexcept { cr1_ = value; }
    [[nodiscard]] std::uint32_t read_cr2() const noexcept { return cr2_; }
    void write_cr2(std::uint32_t value) noexcept { cr2_ = value; }
    [[nodiscard]] std::uint32_t read_cr3() const noexcept { return cr3_; }
    void write_cr3(std::uint32_t value) noexcept { cr3_ = value; }

    void lock() noexcept { ++lock_depth_; }
    void unlock() noexcept {
        if (lock_depth_ == 0) {
            lock_underflow_ = true;
            return;
        }
        --lock_depth_;
    }

    void complete_shift() noexcept { sr_ |= uartcom::reg::kSrTxe | uartcom::reg::kSrTc; }

    void inject_rx(std::uint8_t byte, std::uint32_t extra_status = 0) noexcept {
        if ((sr_ & uartcom::reg::kSrRxne) != 0) {
            sr_ |= uartcom::reg::kSrOre;
            return;
        }
        dr_ = byte;
        sr_ |= uartcom::reg::kSrRxne | extra_status;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& transmitted() const noexcept { return tx_log_; }
    [[nodiscard]] bool lock_fault() const noexcept { return lock_underflow_ || lock_depth_ != 0; }
    [[nodiscard]] int lock_depth() const noexcept { return lock_depth_; }

   private:
    std::uint32_t sr_ = 0;
    std::uint32_t dr_ = 0;
    std::uint32_t brr_ = 0;
    std::uint32_t cr1_ = 0;
    std::uint32_t cr2_ = 0;
    std::uint32_t cr3_ = 0;
    int lock_depth_ = 0;
    bool lock_underflow_ = false;
    std::vector<std::uint8_t> tx_log_;
};

template <typename Uart>
void drain_transmitter(Uart& uart, FakeUsart& hw) {
    for (int guard = 0; guard < 10000 && !uart.tx_idle(); ++guard) {
        hw.complete_shift();
        uart.service();
    }
}
