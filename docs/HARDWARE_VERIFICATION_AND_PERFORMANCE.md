# Hardware Verification and Performance Report — UARTCOM

**Target:** ST NUCLEO-F446RE (STM32F446RET6, Cortex-M4F)  
**Evidence review:** 2026-10-25; repository records, not a new test execution  
**Disposition:** Host/firmware verification is documented; physical-board execution is supported by test procedures, but no timestamped on-board result log or raw oscilloscope/logic-analyzer capture was found in the tracked evidence reviewed.

## 1. Objective and configuration

Verify register-level USART2 firmware, including startup/clock configuration, baud calculation, interrupt-driven receive/transmit, circular-buffer behavior, and a host-to-board command protocol.

| Parameter | Configuration / documented expectation |
| --- | --- |
| Board | NUCLEO-F446RE / STM32F446RET6 |
| Core and system clock | Cortex-M4F, intended 180 MHz PLL (16 MHz HSI is a defined fallback) |
| USART | USART2, PA2 TX / PA3 RX, AF7, IRQ 38 |
| Transport | On-board ST-Link virtual COM port, 115200 baud, 8N1 |
| Peripheral clock | APB1 / USART2 = 45 MHz on normal PLL path |
| TX/RX buffers | 1024-byte software rings, interrupt serviced |
| Toolchain and CI | Arm GNU firmware build, C++ host checks with ASan/UBSan |

The driver accesses USART registers through an MMIO adapter; it does not use STM32 HAL or LL. [Hardware definition](hardware.md), [driver design](uart-driver.md), [README](../README.md).

## 2. Requirements and recorded verification evidence

| ID | Requirement / behavior | Existing test or result | Evidence class |
| --- | --- | --- | --- |
| UART-01 | Configure USART2 frame and baud correctly | Host golden register-image / baud tests cover 8N1, parity/stop variants, oversampling and polling | Automated **host** tests |
| UART-02 | Derive accurate 115200 baud at 45 MHz PCLK1 | Calculated and golden-tested: BRR **391**, nominal actual **115089 baud**, **−959 ppm** | Numerical / software, **not measured pin timing** |
| UART-03 | Transmit and receive across queue wrapping | Host regression loops **100 bytes through depth-8 rings**; checks TX IRQ arming/disarming and RX overrun | Automated host regression |
| UART-04 | Preserve protocol integrity | Tests for PING/ECHO/HEX/CRC32, framing errors, bad CRC, outbox pressure and transcript | Automated host regression |
| UART-05 | Boot and communicate on Nucleo | `tools/hwtest.py` resets MCU via OpenOCD, validates boot banner, INFO (including 180 MHz HSE path), command responses, binary frames and counters | Implemented **on-target** acceptance procedure; no raw dated pass artifact in tracked tree |
| UART-06 | Cross-compile firmware | CI firmware job runs `make firmware` with xPack Arm GNU 14.2.1 | CI build gate; **not** board execution |

The source documents describe a firmware image of **about 15 KiB flash and 10 KiB RAM**. These are approximate reported sizes rather than a newly extracted build/map measurement. [Hardware notes](hardware.md).

### Pass criteria and gaps

The hardware suite is coded to fail if the expected boot banner is absent, the normal 180 MHz HSE/PLL path is not reported, or protocol and counter checks disagree. Those are **acceptance criteria**, not evidence that this report ran the suite. There is no retained numeric sustained-throughput/BER run, logic-analyzer baud measurement, worst-case interrupt latency, long-duration soak, or independent timing trace in the reviewed tracked repository.

## 3. Repeatability

Host / firmware verification (also run by CI):

```sh
make test
make firmware
```

On the physically connected NUCLEO-F446RE, with the ST-Link VCP free:

```sh
make identify
make flash
make hwtest
# equivalent combined target:
make check-target
```

The on-target step needs OpenOCD, a connected board and its virtual COM port; hosted GitHub Actions cannot validate physical UART waveforms. See [testing procedure](testing.md), [hardware configuration](hardware.md), [CI workflow](../.github/workflows/ci.yml).

## 4. Assessment and next measured evidence

**Verified in source:** register and baud golden cases, queue wrap/overrun, protocol cases, firmware-build CI coverage. **Not independently substantiated by a retained hardware log:** an on-target pass from a particular firmware SHA. The next strongest evidence would be a dated `make check-target` transcript tied to the ELF SHA, plus a logic-analyzer capture of TX timing, specified stress duration/byte count, error and loss totals, and measured flash/RAM from `arm-none-eabi-size`.

This report does **not** claim zero dropped bytes, proven hardware UART ppm, interrupt WCET, production qualification or a newly executed on-board test.
