#pragma once

// Volatile register access for one STM32F4 USART. lock() masks IRQs with
// PRIMASK so the main thread and the USART IRQ cannot interleave a
// read-modify-write of CR1 or a ring push/pop.

#include "uartcom/registers.hpp"

#include <cstdint>

namespace board {

class MmioUsart {
   public:
    explicit MmioUsart(std::uint32_t base) noexcept
        : block_(reinterpret_cast<uartcom::reg::Block*>(base)), saved_primask_(0), lock_depth_(0) {}

    [[nodiscard]] std::uint32_t read_sr() noexcept { return block_->sr; }
    [[nodiscard]] std::uint32_t read_dr() noexcept { return block_->dr; }
    void write_dr(std::uint32_t value) noexcept { block_->dr = value; }

    [[nodiscard]] std::uint32_t read_brr() noexcept { return block_->brr; }
    void write_brr(std::uint32_t value) noexcept { block_->brr = value; }

    [[nodiscard]] std::uint32_t read_cr1() noexcept { return block_->cr1; }
    void write_cr1(std::uint32_t value) noexcept { block_->cr1 = value; }

    [[nodiscard]] std::uint32_t read_cr2() noexcept { return block_->cr2; }
    void write_cr2(std::uint32_t value) noexcept { block_->cr2 = value; }

    [[nodiscard]] std::uint32_t read_cr3() noexcept { return block_->cr3; }
    void write_cr3(std::uint32_t value) noexcept { block_->cr3 = value; }

    void lock() noexcept {
        const std::uint32_t primask = read_primask();
        asm volatile("cpsid i" ::: "memory");
        if (lock_depth_ == 0) {
            saved_primask_ = primask;
        }
        ++lock_depth_;
    }

    void unlock() noexcept {
        if (lock_depth_ == 0) {
            return;
        }
        --lock_depth_;
        if (lock_depth_ == 0 && saved_primask_ == 0) {
            asm volatile("cpsie i" ::: "memory");
        }
    }

   private:
    [[nodiscard]] static std::uint32_t read_primask() noexcept {
        std::uint32_t value = 0;
        asm volatile("mrs %0, primask" : "=r"(value)::"memory");
        return value;
    }

    uartcom::reg::Block* block_;
    std::uint32_t saved_primask_;
    std::uint32_t lock_depth_;
};

}  // namespace board
