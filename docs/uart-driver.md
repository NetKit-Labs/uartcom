# UART driver

The driver programs the STM32F4 USART by writing `SR`, `DR`, `BRR`, `CR1`, `CR2`, and `CR3`. It does not call the STM32 HAL, and it does not wrap a HAL handle. The register map is the one in RM0390. `uartcom::reg::Block` is a local layout with `static_assert`s on every offset through `GTPR` at `0x18`.

USART2 lives at `0x40004400`. The driver never sees that address. `board::MmioUsart` holds the pointer and does the volatile accesses. Host tests pass `FakeUsart`, which implements the same methods and models `TXE`, `TC`, `RXNE`, and overrun. A C++20 `UsartHw` concept checks the method set when the compiler is new enough.

## Programming order

`Driver::init` checks the frame and the baud rate before it writes a register. An illegal combination (7 data bits with no parity, 9 data bits with parity, a zero clock, a divisor outside the BRR range) returns false and leaves the block alone.

When the request is legal, init holds the IRQ lock and writes:

1. `CR1 = 0`, which clears `UE` and every interrupt enable.
2. `CR2` stop bits (`STOP` at bits 13:12). One stop bit is `0`. Two stop bits are `0x2000`.
3. `CR3 = EIE` when the receiver interrupt is enabled, otherwise `0`. `EIE` delivers framing, noise, and overrun through the same IRQ as `RXNE`.
4. `BRR`.
5. A dummy `DR` read if `RXNE`, `ORE`, or a line error is already set, so the first `RXNEIE` does not fire on a stale flag.
6. `CR1` with `UE` last, plus `TE`, `RE`, `RXNEIE`, `OVER8`, and the word-length and parity bits.

`TXEIE` is not set in init. `write()` arms it when the TX ring becomes non-empty, and `service()` clears it after the ring drains. Clearing `TXEIE` is followed by one more pop, so a byte queued in the window between the empty check and the clear is not stuck until the next call.

## Frame bits

`M` is the USART word-length bit. Parity consumes one of those bits, so the mapping is:

| Request | `M` | `PCE` | `PS` | `CR1` with UE, TE, RE, RXNEIE |
| --- | --- | --- | --- | --- |
| 8N1 | 0 | 0 | 0 | `0x202C` |
| 8E1 | 1 | 1 | 0 | `0x342C` |
| 8O1 | 1 | 1 | 1 | `0x362C` |
| 7E1 | 0 | 1 | 0 | `0x242C` |
| 9N1 | 1 | 0 | 0 | `0x302C` |

7N and 9-plus-parity are rejected. The byte API keeps bits 7:0 of `DR`. Nine-bit mode is programmed, and the ninth bit is discarded on read and written as zero.

Polling mode (RX interrupt off) is `CR1 = 0x200C` and `CR3 = 0`. Oversampling by 8 sets `OVER8` as well, so the 8N1 IRQ image becomes `0xA02C`.

## Baud

Integer half-up rounding, no floating point. `round_div(n, d)` is `(n + d/2) / d`. On C++23 that helper is `if consteval` so a constant argument is folded at compile time.

Oversampling by 16 (the firmware default):

```text
DIV = round(fck / baud)
BRR = DIV
actual = fck / DIV
```

Oversampling by 8 uses the same `DIV`, then splits it the way RM0390 requires. Bit 3 of `BRR` stays clear. The fraction is three bits:

```text
DIV        = round(fck / baud)
mantissa   = DIV[14:3]
fraction   = DIV[2:0]
BRR        = (mantissa << 4) | fraction
actual     = fck / DIV
```

The error in parts per million uses truncating-toward-zero division, which is the C++ rule for signed integers. The Python tests use the same division, not floor division.

Worked example, USART2 at 45 MHz, 115200 baud, oversampling by 16:

```text
45_000_000 / 115_200 = 390.625
round half-up        = 391
BRR                  = 391
actual               = 45_000_000 / 391 = 115089
ppm                  = (45_000_000 - 115200*391) * 1_000_000 / (115200*391)
                     = -959
```

The same divisor in oversampling-by-8 form is mantissa 48 and fraction 7, so `BRR = 775`.

`BAUD` on the wire prints `pclk`, the requested baud, the actual baud, `brr`, and `ppm`.

## IRQ and the rings

The rings are power-of-two byte FIFOs. Head and tail are free-running 32-bit indexes. Usable capacity is the full depth (there is no wasted slot). The firmware uses 1024 bytes in each direction. The regression test uses depth 8 on purpose, so a 100-byte transfer wraps many times.

The rings are not lock-free. `write()`, `read()`, and `service()` all run with the IRQ lock held. On the MCU the lock is `PRIMASK`, with a nesting depth so a locked section inside another locked section does not re-enable IRQs early. The host fake counts lock depth and fails the test if it is left non-zero.

`service()` drains RX first, then TX. RX records parity, framing, noise, and overrun, and counts bytes that did not fit in the software ring. `status()` copies those counters. `clear_counters()` zeroes them. `RESET` in the protocol sets a flag; the firmware applies it before it sends `OK`, so the reply itself is not counted in the cleared snapshot.

`tx_idle()` is true when the software queue is empty and `TXEIE` is clear. It does not wait for `TC`. The last stop bit may still be leaving the pin. At 115200 that is short compared with the host's next command.

The firmware waits with `WFE`, and `USART2_IRQHandler` ends with `SEV`. `WFI` is not used: a USART event that arrives before `WFI` would be lost, and the core would sleep until some other interrupt. USART2 is IRQ 38. The NVIC line is enabled only after `init()` returns.

## What is not in the driver

GPIO clocks, the USART2 enable and reset pulse, the alternate function, and the NVIC live in `firmware/src/board.cpp`. The PLL lives in `firmware/src/clock.cpp`. The command parser lives in `include/uartcom/protocol.hpp` and never touches a register.

The firmware image is linked `-nostdlib`. `startup.cpp` provides `memset`, `memcpy`, `memmove`, and `strlen`. GCC's `-ftree-loop-distribute-patterns` rewrites those loops into calls to the functions themselves, which recurses until the stack is gone. That file is compiled with `no-tree-loop-distribute-patterns`, and the fills go through a `volatile` pointer so the rewrite cannot come back. `libgcc` is linked after the objects so `__aeabi_uldivmod` (used by the baud math) is pulled in.
