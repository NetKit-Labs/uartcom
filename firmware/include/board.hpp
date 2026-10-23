#pragma once

#include <cstdint>

namespace board {

// PA2/PA3 AF7, then USART2 clock on and a pulse of USART2RST so the driver
// programs the peripheral from its reset values.
void prepare_usart2();

void init_led() noexcept;
void led(bool on) noexcept;
void led_toggle() noexcept;

// USART2_IRQn = 38 on STM32F446.
void enable_usart2_irq() noexcept;

}  // namespace board
