#include "test_harness.hpp"

#include "protocol_util.hpp"

#include "uartcom/crc32.hpp"

namespace {

const char* kInfo = "INFO board=NUCLEO-F446RE mcu=STM32F446RE usart=USART2 src=hse_bypass pll=1 "
                     "sysclk=180000000 pclk=45000000 baud=115200 actual=115089 brr=391 ppm=-959 "
                     "cpp=202302 ctors=1 data=1\n";

UARTCOM_TEST(protocol_ping_echo_help_and_args) {
    auto protocol = make_protocol();
    feed(protocol, "PING\n");
    CHECK_EQ(collect(protocol), std::string("PONG\n"));
    feed(protocol, "PING extra\n");
    CHECK_EQ(collect(protocol), std::string("ERR args\n"));
    feed(protocol, "ECHO\n");
    CHECK_EQ(collect(protocol), std::string("ECHO\n"));
    feed(protocol, "ECHO  hello\n");
    CHECK_EQ(collect(protocol), std::string("ECHO  hello\n"));
    feed(protocol, "\r\n");
    CHECK_EQ(collect(protocol), std::string(""));
    feed(protocol, "HELP\n");
    CHECK_EQ(collect(protocol), std::string("HELP PING INFO ECHO HEX CRC32 STAT BAUD RESET\n"));
    feed(protocol, "NOPE\n");
    CHECK_EQ(collect(protocol), std::string("ERR unknown\n"));
}

UARTCOM_TEST(protocol_info_stat_baud_and_reset) {
    auto protocol = make_protocol();
    arm_nominal_link(protocol);
    feed(protocol, "INFO\n");
    CHECK_EQ(collect(protocol), std::string(kInfo));
    feed(protocol, "BAUD\n");
    CHECK_EQ(collect(protocol), std::string("BAUD pclk=45000000 baud=115200 actual=115089 brr=391 ppm=-959\n"));
    uartcom::LinkStatus link;
    link.rx_bytes = 4;
    link.tx_bytes = 9;
    link.parity_errors = 1;
    link.framing_errors = 2;
    link.noise_errors = 3;
    link.overrun_errors = 4;
    link.software_overruns = 5;
    link.rx_queued = 6;
    link.tx_queued = 7;
    protocol.set_link(link);
    feed(protocol, "STAT\n");
    CHECK_EQ(collect(protocol), std::string("STAT rx=4 tx=9 pe=1 fe=2 ne=3 ore=4 swore=5 rxq=6 txq=7\n"));
    feed(protocol, "RESET\n");
    CHECK(protocol.take_clear_request());
    CHECK(!protocol.take_clear_request());
    CHECK_EQ(collect(protocol), std::string("OK\n"));
}

UARTCOM_TEST(protocol_hex_and_crc32) {
    auto protocol = make_protocol();
    feed(protocol, "HEX\n");
    CHECK_EQ(collect(protocol), std::string("HEX\n"));
    feed(protocol, "HEX 00ff0a\n");
    CHECK_EQ(collect(protocol), std::string("HEX 00FF0A\n"));
    feed(protocol, "HEX 0\n");
    CHECK_EQ(collect(protocol), std::string("ERR bad_hex\n"));
    feed(protocol, "HEX zz\n");
    CHECK_EQ(collect(protocol), std::string("ERR bad_hex\n"));
    feed(protocol, "CRC32\n");
    CHECK_EQ(collect(protocol), std::string("CRC32 00000000\n"));
    feed(protocol, "CRC32 313233343536373839\n");
    CHECK_EQ(collect(protocol), std::string("CRC32 CBF43926\n"));
    const std::uint8_t sample[] = {0x00, 0xFF, 0x10};
    CHECK_EQ(uartcom::crc32(sample), 0x71D23404u);
}

UARTCOM_TEST(protocol_binary_frame_and_resync) {
    auto protocol = make_protocol();
    const std::uint8_t payload[] = {0x00, 0xFF, 0x10};
    const auto crc = uartcom::crc32(payload);
    std::uint8_t frame[2 + 2 + 3 + 4] = {0xA5, 0x5A, 0x03, 0x00, 0x00, 0xFF, 0x10};
    frame[7] = static_cast<std::uint8_t>(crc & 0xFF);
    frame[8] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    frame[9] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
    frame[10] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
    feed_bytes(protocol, frame, sizeof(frame));
    const auto echoed = collect(protocol);
    CHECK_EQ(echoed.size(), sizeof(frame));
    CHECK(echoed == std::string(reinterpret_cast<const char*>(frame), sizeof(frame)));

    frame[10] ^= 0xFF;
    feed_bytes(protocol, frame, sizeof(frame));
    CHECK_EQ(collect(protocol), std::string("ERR crc\n"));
    feed(protocol, "PING\n");
    CHECK_EQ(collect(protocol), std::string("PONG\n"));
}

UARTCOM_TEST(protocol_rejects_bad_sync_length_and_long_lines) {
    auto protocol = make_protocol();
    const std::uint8_t sync[] = {0xA5, 0x00};
    feed_bytes(protocol, sync, sizeof(sync));
    CHECK_EQ(collect(protocol), std::string("ERR sync\n"));

    std::string huge(600, 'A');
    huge.push_back('\n');
    feed(protocol, huge);
    CHECK_EQ(collect(protocol), std::string("ERR line\n"));

    const std::uint8_t magic[] = {0xA5, 0x5A, 0x01, 0x01};
    feed_bytes(protocol, magic, sizeof(magic));
    CHECK_EQ(collect(protocol), std::string("ERR length\n"));
    std::uint8_t junk[261];
    for (auto& byte : junk) {
        byte = 0x5A;
    }
    feed_bytes(protocol, junk, sizeof(junk));
    CHECK_EQ(collect(protocol), std::string(""));
    feed(protocol, "PING\n");
    CHECK_EQ(collect(protocol), std::string("PONG\n"));
}

}  // namespace
