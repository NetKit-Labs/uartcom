/* STM32F446RE: 512 KiB flash at 0x08000000, 128 KiB SRAM at 0x20000000. */
ENTRY(Reset_Handler)

_Min_Stack_Size = 0x2000;
_estack = ORIGIN(RAM) + LENGTH(RAM);

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM (rw)    : ORIGIN = 0x20000000, LENGTH = 128K
}

PHDRS
{
  flash PT_LOAD FLAGS(5); /* read + execute */
  ram   PT_LOAD FLAGS(6); /* read + write */
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector*))
  } > FLASH : flash

  .text :
  {
    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } > FLASH : flash

  .init_array :
  {
    . = ALIGN(4);
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __init_array_end = .;
  } > FLASH : flash

  .fini_array :
  {
    . = ALIGN(4);
    __fini_array_start = .;
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    __fini_array_end = .;
  } > FLASH : flash

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(8);
    _sdata = .;
    *(.data*)
    . = ALIGN(8);
    _edata = .;
  } > RAM AT > FLASH : ram

  .bss (NOLOAD) :
  {
    . = ALIGN(8);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
  } > RAM : ram

  ._user_stack (NOLOAD) :
  {
    . = ALIGN(8);
    . = . + _Min_Stack_Size;
  } > RAM

  . = ALIGN(8);
  _end = .;
  PROVIDE(end = .);

  ASSERT(_ebss + _Min_Stack_Size <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflow: bss collided with the stack reserve")

  /DISCARD/ :
  {
    *(.ARM.exidx*)
    *(.ARM.extab*)
  }
}
