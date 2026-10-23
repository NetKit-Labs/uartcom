#include "test_harness.hpp"

#include "fake_usart.hpp"
#include "protocol_util.hpp"

#include "uartcom/driver.hpp"

#include <string>
#include <vector>

namespace {

using SmallUart = uartcom::Driver<FakeUsart, 8, 8>;

uartcom::Config cfg_8n1() {
    uartcom::Config config;
    config.clock_hz = 45000000;
    config.baud_rate = 115200;
    return config;
}

std::uint32_t cr1_of(uartcom::DataBits bits, uartcom::Parity parity, bool over8, bool rx_irq) {
    const auto frame = uartcom::encode_frame(bits, parity);
    std::uint32_t cr1 = uartcom::reg::kCr1Ue | uartcom::reg::kCr1Te | uartcom::reg::kCr1Re | frame.cr1;
    if (rx_irq) {
        cr1 |= uartcom::reg::kCr1Rxneie;
    }
    if (over8) {
        cr1 |= uartcom::reg::kCr1Over8;
    }
    return cr1;
}

UARTCOM_TEST(regression_frozen_register_images) {
    struct Case {
        const char* name;
        uartcom::DataBits bits;
        uartcom::Parity parity;
        uartcom::StopBits stop;
        uartcom::Oversampling over;
        bool rx_irq;
        std::uint32_t cr1;
        std::uint32_t cr2;
        std::uint32_t cr3;
        std::uint32_t brr;
    };
    const Case cases[] = {
        {"8N1", uartcom::DataBits::Eight, uartcom::Parity::None, uartcom::StopBits::One, uartcom::Oversampling::By16,
         true, 0x202C, 0x0000, 0x0001, 391},
        {"8E1", uartcom::DataBits::Eight, uartcom::Parity::Even, uartcom::StopBits::One, uartcom::Oversampling::By16,
         true, 0x342C, 0x0000, 0x0001, 391},
        {"8O1", uartcom::DataBits::Eight, uartcom::Parity::Odd, uartcom::StopBits::One, uartcom::Oversampling::By16,
         true, 0x362C, 0x0000, 0x0001, 391},
        {"7E1", uartcom::DataBits::Seven, uartcom::Parity::Even, uartcom::StopBits::One, uartcom::Oversampling::By16,
         true, 0x242C, 0x0000, 0x0001, 391},
        {"9N1", uartcom::DataBits::Nine, uartcom::Parity::None, uartcom::StopBits::One, uartcom::Oversampling::By16,
         true, 0x302C, 0x0000, 0x0001, 391},
        {"8N2", uartcom::DataBits::Eight, uartcom::Parity::None, uartcom::StopBits::Two, uartcom::Oversampling::By16,
         true, 0x202C, 0x2000, 0x0001, 391},
        {"8N1-over8", uartcom::DataBits::Eight, uartcom::Parity::None, uartcom::StopBits::One, uartcom::Oversampling::By8,
         true, 0xA02C, 0x0000, 0x0001, 775},
        {"8N1-polled", uartcom::DataBits::Eight, uartcom::Parity::None, uartcom::StopBits::One,
         uartcom::Oversampling::By16, false, 0x200C, 0x0000, 0x0000, 391},
    };

    for (const auto& item : cases) {
        FakeUsart hw;
        SmallUart uart{hw};
        auto config = cfg_8n1();
        config.data_bits = item.bits;
        config.parity = item.parity;
        config.stop_bits = item.stop;
        config.oversampling = item.over;
        config.rx_interrupt = item.rx_irq;
        CHECK(uart.init(config));
        CHECK_EQ(hw.read_cr1(), item.cr1);
        CHECK_EQ(hw.read_cr2(), item.cr2);
        CHECK_EQ(hw.read_cr3(), item.cr3);
        CHECK_EQ(hw.read_brr(), item.brr);
        CHECK_EQ(hw.read_cr1(), cr1_of(item.bits, item.parity, item.over == uartcom::Oversampling::By8, item.rx_irq));
        CHECK(!hw.lock_fault());
    }
}

UARTCOM_TEST(regression_reinit_is_stable_and_nine_bit_parity_is_rejected) {
    FakeUsart hw;
    SmallUart uart{hw};
    CHECK(uart.init(cfg_8n1()));
    CHECK(uart.init(cfg_8n1()));
    CHECK_EQ(hw.read_cr1(), 0x202Cu);
    CHECK_EQ(hw.read_brr(), 391u);
    auto bad = cfg_8n1();
    bad.data_bits = uartcom::DataBits::Nine;
    bad.parity = uartcom::Parity::Even;
    CHECK(!uart.init(bad));
    CHECK_EQ(hw.read_cr1(), 0x202Cu);
}

std::vector<std::uint8_t> loopback(SmallUart& uart, FakeUsart& hw, const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> output;
    std::size_t sent = 0;
    for (int guard = 0; guard < 100000 && output.size() < input.size(); ++guard) {
        if (sent < input.size()) {
            sent += uart.write(uartcom::Span<const std::uint8_t>{input.data() + sent, input.size() - sent});
        }
        const auto before = hw.transmitted().size();
        hw.complete_shift();
        uart.service();
        if (hw.transmitted().size() > before) {
            hw.inject_rx(hw.transmitted().back());
            uart.service();
        }
        std::uint8_t buffer[8];
        const auto count = uart.read(buffer);
        output.insert(output.end(), buffer, buffer + count);
    }
    hw.complete_shift();
    uart.service();
    return output;
}

UARTCOM_TEST(regression_loopback_crosses_the_ring_boundary) {
    FakeUsart hw;
    SmallUart uart{hw};
    CHECK(uart.init(cfg_8n1()));
    std::vector<std::uint8_t> payload(100);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>(i * 3 + 1);
    }
    const auto got = loopback(uart, hw, payload);
    CHECK(got == payload);
    CHECK_EQ(uart.status().rx_bytes, 100u);
    CHECK_EQ(uart.status().tx_bytes, 100u);
    CHECK_EQ(uart.status().software_overruns, 0u);
    CHECK_EQ(uart.status().overrun_errors, 0u);
    CHECK(uart.tx_idle());
    CHECK(!hw.lock_fault());
}

UARTCOM_TEST(regression_protocol_transcript) {
    auto protocol = make_protocol();
    arm_nominal_link(protocol);
    feed(protocol, "PING\nECHO abc\nHEX 00ff\nCRC32 313233343536373839\nNOPE\n");
    CHECK_EQ(collect(protocol), std::string("PONG\nECHO abc\nHEX 00FF\nCRC32 CBF43926\nERR unknown\n"));

    int queued = 0;
    for (int i = 0; i < 300; ++i) {
        const auto before = protocol.queued();
        feed(protocol, "PING\n");
        if (protocol.queued() > before) {
            ++queued;
        } else {
            break;
        }
    }
    CHECK(queued > 50);
    CHECK(queued < 300);
    std::string burst = collect(protocol);
    const std::string pong = "PONG\n";
    CHECK_EQ(burst.size() % pong.size(), 0u);
    for (std::size_t i = 0; i < burst.size(); i += pong.size()) {
        CHECK(burst.compare(i, pong.size(), pong) == 0);
    }

    for (int i = 0; i < 100; ++i) {
        feed(protocol, "PING\n");
    }
    burst = collect(protocol);
    CHECK(burst.size() > pong.size() * 50);
    CHECK_EQ(burst.substr(0, pong.size()), pong);
    CHECK_EQ(burst.substr(burst.size() - pong.size()), pong);
}

}  // namespace
