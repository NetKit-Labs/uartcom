#pragma once

// Byte FIFO. Capacity is a power of two and is fully usable.
// The UART driver calls this only while the hardware adapter's lock is held,
// so a single producer and a single consumer never run at the same time.

#include <array>
#include <cstddef>
#include <cstdint>

namespace uartcom {

template <std::size_t kCapacity>
class RingBuffer {
    static_assert(kCapacity >= 2, "ring needs at least two slots");
    static_assert((kCapacity & (kCapacity - 1)) == 0, "ring capacity must be a power of two");

   public:
    RingBuffer() = default;

    [[nodiscard]] bool push(std::uint8_t byte) noexcept {
        if (size() == kCapacity) {
            return false;
        }
        data_[head_ & (kCapacity - 1)] = byte;
        ++head_;
        return true;
    }

    [[nodiscard]] bool pop(std::uint8_t& byte) noexcept {
        if (empty()) {
            return false;
        }
        byte = data_[tail_ & (kCapacity - 1)];
        ++tail_;
        return true;
    }

    [[nodiscard]] std::uint32_t size() const noexcept { return head_ - tail_; }
    [[nodiscard]] bool empty() const noexcept { return head_ == tail_; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return kCapacity; }

    void clear() noexcept {
        head_ = 0;
        tail_ = 0;
    }

   private:
    std::array<std::uint8_t, kCapacity> data_{};
    std::uint32_t head_ = 0;
    std::uint32_t tail_ = 0;
};

}  // namespace uartcom
