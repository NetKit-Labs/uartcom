#pragma once

#include <cstdint>

namespace uartcom {

// Data bits are the payload bits, not including parity. The USART hardware
// folds parity into the word length, and encode_frame() performs that mapping.
enum class DataBits : std::uint8_t { Seven = 7, Eight = 8, Nine = 9 };

enum class Parity : std::uint8_t { None = 0, Even = 1, Odd = 2 };

// Values match USART_CR2 STOP[13:12] (RM0390).
enum class StopBits : std::uint8_t { One = 0, Half = 1, Two = 2, OneAndHalf = 3 };

enum class Oversampling : std::uint8_t { By16 = 0, By8 = 1 };

struct Config {
    std::uint32_t clock_hz = 0;
    std::uint32_t baud_rate = 115200;
    DataBits data_bits = DataBits::Eight;
    Parity parity = Parity::None;
    StopBits stop_bits = StopBits::One;
    Oversampling oversampling = Oversampling::By16;
    bool transmitter = true;
    bool receiver = true;
    bool rx_interrupt = true;
    bool tx_interrupt = true;
};

}  // namespace uartcom
