# Future work

Nothing in this list is scheduled. The image today is a one-byte interrupt console on USART2 at 115200 8N1. These are the additions worth considering, in the order that would teach the most about this chip.

## DMA

USART2 can request DMA1 channel 4. Stream 5 receives and stream 6 transmits. `DMAR` and `DMAT` in `CR3` enable those requests. An idle-line interrupt on a receive DMA buffer would deliver a chunk instead of one IRQ per byte. The interrupt path should stay, so a test can still run the driver with DMA off.

The measurement that would justify DMA is a hardware sweep: raise the baud until `STAT` shows `ore` climbing, and record that rate for the one-byte driver. `BAUD` is read-only today. A command that reprograms `BRR`, then checks the link at the new rate, is the way to run that sweep without reflashing.

## Finish the frame the registers already describe

`tx_idle()` means the software queue is empty and `TXEIE` is clear. It does not mean the last stop bit has left the pin. Waiting for `TC` would make that true.

`RTSE` and `CTSE` are named in the register map and never set. RTS and CTS would be a small addition on top of the same driver, using the Nucleo morpho pins those signals already have.

Nine-bit mode is programmed, and the byte API keeps only bits 7:0. A 9-bit read and write would make the 9N1 register image match the data path. Host tests can cover that with `FakeUsart` before the board does.

## Protocol and the host tool

Binary frames must start with `A5 5A` at the beginning of a line. DMA completion or an idle-line event could frame a payload without that magic byte.

`tools/hwtest.py` is a test. A small Python client next to it, speaking `PING`, `ECHO`, and the binary frame, would let another program use the board without going through `unittest`.
