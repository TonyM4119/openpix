/* OpenPix firmware for HolidayCoro AlphaPix Evolution (GD32F450, STM32F4-compatible)
 *
 * Flash map (bank 0, 1 MB):
 *   0x08000000 - 0x0800BFFF  sectors 0-2   HolidayCoro bootloader      (never touched)
 *   0x0800C000 - 0x0800FFFF  sector 3      factory device ID + stock settings (never touched)
 *   0x08010000 - 0x0805FFFF  sectors 4-6   application (this firmware), 320 KB
 *   0x08060000 - 0x0807FFFF  sector 7      OpenPix presets / calendar / extras
 *   0x08080000 - 0x080DFFFF  sectors 8-10  update staging area, 384 KB
 *   0x080E0000 - 0x080FFFFF  sector 11     OpenPix settings
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08010000, LENGTH = 320K   /* sectors 4-6; sector 7 holds presets */
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K   /* SRAM0+SRAM1+SRAM2 */
  XRAM  (rw)  : ORIGIN = 0x20030000, LENGTH = 144K   /* additional SRAM (stock uses up to 0x20055BD0) */
  TCM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K    /* CPU-only tightly coupled RAM (stock uses it too) */
}

/* Initial SP in the vector table is kept low (0x20020000) so it passes any
 * "is the app valid" check the bootloader may do. Reset_Handler then moves
 * the stack to the top of RAM. */
_estack_vector = 0x20020000;
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx : { *(.ARM.exidx*) } > FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.ramfunc*)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  .xram (NOLOAD) :
  {
    . = ALIGN(8);
    *(.xram*)
    . = ALIGN(4);
  } > XRAM

  .tcm (NOLOAD) :
  {
    . = ALIGN(8);
    *(.tcm*)
    . = ALIGN(4);
  } > TCM

  /* Keep at least 8 KB for the stack */
  ASSERT(_ebss + 8192 <= _estack, "RAM overflow")

  _image_end = LOADADDR(.data) + SIZEOF(.data);
}
