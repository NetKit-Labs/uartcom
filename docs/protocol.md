# Command protocol

USART2 runs at 115200 8N1. The parser is `uartcom::Protocol`. It is host-tested with a fake USART, and the same object runs on the board.

Lines end with `\n`. `\r` is ignored. Empty lines are ignored. The Python driver strips the newline before returning a reply. The C++ tests compare the reply including `\n`.

A line longer than the parser buffer answers `ERR line`. A non-ASCII byte in a line answers `ERR bad_char`. An unknown command answers `ERR unknown`. Extra arguments on a command that takes none answer `ERR args`.

## Banner

After reset, before it reads a command:

```text
UARTCOM STM32F446RE USART2 ready
```

## Commands

| Command | Reply |
| --- | --- |
| `PING` | `PONG` |
| `ECHO` | `ECHO` |
| `ECHO hello` | `ECHO hello` |
| `ECHO  hello` | `ECHO  hello` |
| `HEX` | `HEX` |
| `HEX 00ff10` | `HEX 00FF10` |
| `HEX 0` or `HEX 0g` | `ERR bad_hex` |
| `CRC32` | `CRC32 00000000` |
| `CRC32 313233343536373839` | `CRC32 CBF43926` |
| `CRC32 00FF10` | `CRC32 71D23404` |
| `STAT` | `STAT rx= tx= pe= fe= ne= ore= swore= rxq= txq=` |
| `BAUD` | `BAUD pclk= baud= actual= brr= ppm=` |
| `RESET` | `OK` |
| `HELP` | `HELP PING INFO ECHO HEX CRC32 STAT BAUD RESET` |

`ECHO` keeps the remainder of the line, including a leading space. `ECHO` with nothing after the word replies `ECHO`.

`HEX` reads an even number of hex digits and prints them in uppercase. An empty payload is legal. An odd length or a non-hex digit is `ERR bad_hex`.

`CRC32` is the ISO-HDLC / zlib polynomial (`0xEDB88320`, reflected, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`). The argument is hex, same rules as `HEX`. The three vectors above are the empty buffer, the ASCII string `123456789`, and the bytes `00 FF 10`.

`STAT` counts are since the last `RESET` (or since boot). `rx` and `tx` are bytes through the driver. `pe`, `fe`, `ne`, and `ore` are USART line errors. `swore` is bytes dropped because the software RX ring was full. `rxq` and `txq` are the current depths of those rings. `RESET` clears the counters and then replies `OK`, so that `OK` is not part of the cleared snapshot.

`INFO` is one line of `key=value` fields:

```text
INFO board=NUCLEO-F446RE mcu=STM32F446RE usart=USART2 src=hse_bypass pll=1 sysclk=180000000 pclk=45000000 baud=115200 actual=115089 brr=391 ppm=-959 cpp=202302 ctors=1 data=1
```

`src` is `hse_bypass`, `hse_crystal`, or `hsi`. On this Nucleo the expected path is `hse_bypass` with `pll=1`, `sysclk=180000000`, and `pclk=45000000`. `cpp` is the firmware `__cplusplus`. `ctors=1` means a static constructor ran. `data=1` means the `.data` copy put `0x446` into RAM. The Python suite requires the 180 MHz path and compares `cpp` with `build/firmware_cxx.txt`.

## Binary frame

A frame may start only at column 0 of a line (the parser is idle, not mid-command).

```text
A5 5A | uint16le length | payload | uint32le crc32
```

`length` is the payload size, 0 through 256. The CRC is the same ISO-HDLC CRC over the payload only. A good frame is echoed with a freshly computed CRC. A bad CRC answers `ERR crc` and the parser returns to line mode. `length` above 256 answers `ERR length` and the parser discards the rest of the payload and the CRC. A lone `A5` that is not followed by `5A` answers `ERR sync`.

The Python helper `Port.transact_frame` writes a frame and reads one back. The first two bytes `ER` mean the device answered with an ASCII error line instead of a frame.
