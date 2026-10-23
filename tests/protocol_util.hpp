#pragma once

#include "uartcom/protocol.hpp"

#include <cstdint>
#include <string>
#include <string_view>

inline uartcom::Protocol make_protocol() {
    return uartcom::Protocol({"NUCLEO-F446RE", "STM32F446RE", "USART2"});
}

inline void arm_nominal_link(uartcom::Protocol& protocol) {
    uartcom::BoardInfo board;
    board.source = "hse_bypass";
    board.sysclk_hz = 180000000;
    board.cpp_standard = 202302;
    board.constructors_ran = 1;
    board.pll_enabled = 1;
    board.data_ok = 1;
    uartcom::LinkStatus link;
    link.kernel_clock_hz = 45000000;
    link.baud_rate = 115200;
    link.actual_baud = 115089;
    link.brr = 391;
    link.error_ppm = -959;
    protocol.set_board(board);
    protocol.set_link(link);
}

inline void feed(uartcom::Protocol& protocol, std::string_view bytes) {
    for (const auto byte : bytes) {
        protocol.ingest(static_cast<std::uint8_t>(byte));
    }
}

inline void feed_bytes(uartcom::Protocol& protocol, const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        protocol.ingest(data[i]);
    }
}

inline std::string collect(uartcom::Protocol& protocol) {
    std::string out;
    std::uint8_t buffer[64];
    std::size_t count = 0;
    while ((count = protocol.read_tx(buffer)) > 0) {
        out.append(reinterpret_cast<const char*>(buffer), count);
    }
    return out;
}
