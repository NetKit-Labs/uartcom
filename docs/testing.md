# Testing

Three suites, all required to pass before a change to the driver or the protocol is done.

## Host unit tests

```sh
make test-unit
```

Apple clang (or whatever `CXX` is), C++23 when the compiler accepts it, `-Wall -Wextra -Wpedantic -Werror`, ASan, and UBSan. The binary is `build/test_unit`. A failure throws `uartcom::test::Failure`. Tests register themselves through a static constructor into a Meyers singleton, so the registry is built before `main`.

| File | What it covers |
| --- | --- |
| `tests/test_baud.cpp` | Golden `BRR` and ppm values, including 45 MHz / 115200 → 391 / −959, and the oversampling-by-8 encoding |
| `tests/test_ring.cpp` | Fill, wrap past the power-of-two boundary, and a rejected pop |
| `tests/test_driver.cpp` | Init order, stale `RXNE` drain, TX IRQ arm and clear, overrun, illegal frames |
| `tests/test_protocol.cpp` | Every command, hex and CRC-32 errors, binary frames, `ERR sync` / `ERR length` / `ERR crc` |

`FakeUsart` clears `TXE` when `DR` is written and sets `TXE` and `TC` when the test calls `complete_shift()`. `inject_rx()` sets overrun if `RXNE` is already set. The fake fails the test if a lock is released too often or left held.

## Host regression tests

```sh
make test-regression
```

These freeze behavior that the unit tests only sample:

- The CR1/CR2/CR3/BRR image for 8N1 IRQ, 8E1, 8O1, 7E1, 9N1, two stop bits, oversampling by 8, and polled mode.
- Re-init replaces the previous programming and does not leave `TXEIE` set.
- 100 bytes loop back through rings of depth 8.
- A scripted transcript: `PING`, `ECHO`, `HEX`, `CRC32`, and an unknown command, including the case where the protocol outbox fills and the reply is held until the driver drains it.

`make test` runs both host binaries and `make test-python` (the baud-math cases in `tools/hwtest.py`). They do not open the serial port. GitHub Actions runs that target, plus `make firmware`, on every push and pull request. `make hwtest` is not part of CI because it needs the Nucleo.

## Demo on the board

Plug in the NUCLEO-F446RE through the ST-Link USB connector. Flash, then open the virtual COM port at 115200 8N1. The first line is `UARTCOM STM32F446RE USART2 ready`. Type `PING` and expect `PONG`. `ECHO hello`, `HELP`, `INFO`, and `BAUD` are the rest of a manual check. Commands are uppercase. The README has `screen` and `picocom` examples.

Quit that terminal before the Python suite. The port cannot be shared.

## On-target Python tests

```sh
make flash
make hwtest
```

`tools/hwtest.py` is Python 3, standard library only (`termios`, `select`, `unittest`). It does not need pyserial. It opens the ST-Link VCP at 115200 8N1, resets the MCU through OpenOCD, and expects the banner within four seconds.

`UARTCOM_PORT` overrides discovery. Otherwise the first `/dev/cu.usbmodem*` is used. Close other serial monitors first. OpenOCD has to be able to reset the chip, and the VCP has to be free for the script.

`BaudMathTests` does not need the board. It recomputes the truncating-toward-zero ppm so a Python `//` floor division cannot drift away from the C++ result. `BoardTests` checks the banner, `INFO` (180 MHz, HSE bypass, `cpp` equal to `build/firmware_cxx.txt`), echo, hex, CRC-32, a binary frame and a bad CRC, counter movement across `RESET`, and a burst of commands.

`make check-target` is flash followed by `hwtest`.

## What a failure looks like

- Host sanitizer output is a real bug in the driver or a test that broke the fake's rules.
- `boot banner not seen` means the MCU never reached `firmware_main`, or the VCP was held by another process. A rapidly blinking LD2 means a fault handler is running.
- `INFO` with `pll=0` and `sysclk=16000000` means HSE or over-drive did not lock. The link can still answer. The hardware test fails on purpose so that fallback is visible.
- A binary-frame mismatch that then poisons the next `PING` is a host read that did not put unused bytes back. `Port.read_exact` keeps the tail in an internal buffer for that reason.
