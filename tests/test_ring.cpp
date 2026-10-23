#include "test_harness.hpp"

#include "uartcom/ring_buffer.hpp"

namespace {

UARTCOM_TEST(ring_push_pop_and_full) {
    uartcom::RingBuffer<8> ring;
    CHECK(ring.empty());
    for (std::uint8_t i = 0; i < 8; ++i) {
        CHECK(ring.push(i));
    }
    CHECK(!ring.push(0xFF));
    CHECK_EQ(ring.size(), 8u);
    for (std::uint8_t i = 0; i < 8; ++i) {
        std::uint8_t value = 0;
        CHECK(ring.pop(value));
        CHECK_EQ(value, i);
    }
    CHECK(ring.empty());
    std::uint8_t discarded = 0xAA;
    CHECK(!ring.pop(discarded));
    CHECK_EQ(discarded, 0xAAu);
}

UARTCOM_TEST(ring_wraps_past_capacity) {
    uartcom::RingBuffer<8> ring;
    for (std::uint8_t i = 0; i < 8; ++i) {
        CHECK(ring.push(i));
    }
    for (std::uint8_t i = 0; i < 5; ++i) {
        std::uint8_t value = 0;
        CHECK(ring.pop(value));
        CHECK_EQ(value, i);
    }
    for (std::uint8_t i = 0; i < 5; ++i) {
        CHECK(ring.push(static_cast<std::uint8_t>(100 + i)));
    }
    const std::uint8_t expect[] = {5, 6, 7, 100, 101, 102, 103, 104};
    for (const auto wanted : expect) {
        std::uint8_t value = 0;
        CHECK(ring.pop(value));
        CHECK_EQ(value, wanted);
    }
    CHECK(ring.empty());
}

UARTCOM_TEST(ring_clear_drops_queued_bytes) {
    uartcom::RingBuffer<4> ring;
    CHECK(ring.push(1));
    CHECK(ring.push(2));
    ring.clear();
    CHECK(ring.empty());
    CHECK(ring.push(9));
    std::uint8_t value = 0;
    CHECK(ring.pop(value));
    CHECK_EQ(value, 9u);
}

}  // namespace
