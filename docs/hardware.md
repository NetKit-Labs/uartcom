# Hardware

The firmware is linked for the **NUCLEO-F446RE**, MCU **STM32F446RET6**.

| Resource | Value |
| --- | --- |
| Flash | 512 KiB at `0x08000000` |
| SRAM | 128 KiB at `0x20000000`, initial MSP `0x20020000` |
| DBGMCU_IDCODE | `0x10006421` (DEV_ID `0x421`) |
| USART2 | `0x40004400`, APB1, IRQ 38 |
| TX / RX | PA2 / PA3, AF7, pull-up |
| LD2 | PA5, push-pull |
| HSE | ST-Link 8 MHz MCO into OSC_IN (HSE bypass) |

No extra USB-UART cable is required. On a stock Nucleo-64, SB13 and SB14 are closed, which connects USART2 to the ST-Link virtual COM port (UM1724). `make identify` reads the id code and the flash size through OpenOCD.

Flashing this image replaces the previous firmware on the board.

## Clock

Target tree, from RM0390:

| Clock | Frequency |
| --- | --- |
| SYSCLK | 180 MHz |
| HCLK / AHB | 180 MHz |
| PCLK1 / APB1 | 45 MHz (USART2 kernel clock; not the doubled timer clock) |
| PCLK2 / APB2 | 90 MHz |

Bring-up in `firmware/src/clock.cpp`, from `SystemInit`, before static constructors:

1. Enable the FPU (`CPACR`).
2. Enable `PWR`, set voltage scale 1, and wait for `VOSRDY`.
3. Program flash for 5 wait states plus prefetch and the instruction and data caches.
4. Start HSE in bypass mode. If it does not lock, try HSE crystal mode, then HSI.
5. Program the PLL. From an 8 MHz HSE: `M=8`, `N=360`, `P=/2`, `Q=7`, which is 180 MHz. From HSI at 16 MHz: `M=8`, `N=180`, `P=/2`.
6. Enable over-drive (`ODEN` then `ODSWEN`). 180 MHz is not legal without it.
7. Set APB1 prescaler `/4` and APB2 `/2`, then switch `SW` to the PLL and wait for `SWS`.

If the PLL or over-drive does not lock, the tree falls back to HSI at 16 MHz, the PLL is turned off, and flash latency goes back to 0. `INFO` reports `pll=0`, `src=hsi`, `sysclk=16000000`, `pclk=16000000`. The baud generator uses that PCLK, so 115200 is still programmed correctly. The Python suite treats the 16 MHz path as a failure, because this board should take HSE bypass.

## Pins and the LED

`prepare_usart2()` enables GPIOA and USART2, pulses `USART2RST` (APB1RSTR bit 17), then sets PA2 and PA3 to AF7, very high speed, pull-up. `init()` of the driver runs after that reset pulse. The NVIC pending bit for IRQ 38 is cleared and the line is enabled only after init returns.

LD2 (PA5) turns on after the USART is up, toggles once per command response, and blinks quickly from the fault handlers. Fault handlers do not execute `BKPT`, which would fault again when no debugger is attached.

## Image

The vector table has 113 entries (16 system vectors plus IRQ 0 through 96). USART2 is index 54. Reset copies `.data`, zeroes `.bss`, calls `SystemInit`, walks `.init_array`, and calls `firmware_main`. The entry point is not `main`, because `-Wpedantic` rejects taking the address of `::main`.

`g_data_anchor` is `0x446` in `.data`. `INFO data=1` means the copy landed. A static `CtorProbe` writes `0xC0A15`; `INFO ctors=1` means `.init_array` ran.

The linker script uses two program headers: flash is read and execute, RAM is read and write. The 8 KiB stack reservation is a link-time budget only. The CPU boots with MSP at the top of SRAM. The image is about 15 KiB of flash and 10 KiB of RAM.

OpenOCD uses `interface/stlink.cfg` plus `firmware/openocd/nucleo_f446re.cfg`. The adapter stays at 1800 kHz. `make flash` programs, verifies, and resets.
