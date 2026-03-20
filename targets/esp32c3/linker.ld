OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  drom (r)   : ORIGIN = 0x3C000020, LENGTH = 0x007FFFE0
  irom (rx)  : ORIGIN = 0x42000020, LENGTH = 0x007FFFE0
  iram (rx)  : ORIGIN = 0x40380000, LENGTH = 0x00060000
  dram (rwx) : ORIGIN = 0x3FC80000, LENGTH = 0x00060000
}

SECTIONS
{
  .flash.padding ORIGIN(drom) :
  {
    /*
     * Keep the first DROM PT_LOAD segment MMU-page aligned at 0x3c000000.
     * The ESP-IDF bootloader aborts if the segment start virtual address is
     * not aligned, even though the app descriptor itself still lives at +0x20.
     */
    . = ORIGIN(drom) + 0x20;
  } > drom

  .flash.appdesc :
  {
    KEEP(*(.flash.appdesc))
    KEEP(*(.rodata_desc))
  } > drom

  .exception_vectors_table.text ORIGIN(iram) : ALIGN(256)
  {
    _iram_start = ABSOLUTE(.);
    KEEP(*(.exception_vectors_table.text))
  } > iram

  .exception_vectors.text :
  {
    KEEP(*(.exception_vectors.text))
  } > iram

  .flash.text ORIGIN(irom) :
  {
    *(.init)
    *(.text .text.*)
  } > irom

  .flash.rodata :
  {
    *(.rodata .rodata.*)
  } > drom

  .iram.text :
  {
    _iram_end = ABSOLUTE(.);
  } > iram

  .dram0.dummy (NOLOAD) :
  {
    /*
     * ESP32-C3 IRAM and DRAM are aliases of the same SRAM. Keep this gap so
     * DRAM allocations start after the IRAM image's mirrored range. Removing
     * it can make BSS clearing corrupt mtvec vector slots and trap entry code.
     */
    . = ORIGIN(dram) + (_iram_end - _iram_start);
  } > dram

  .data :
  {
    __data_start = .;
    *(.data .data.*)
    *(.sdata .sdata.*)
    __data_end = .;
  } > dram

  __global_pointer$ = __data_start + 0x800;

  .bss (NOLOAD) :
  {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } > dram

  .task_stacks (NOLOAD) :
  {
    *(.task_stacks .task_stacks.*)
  } > dram

  .stack (NOLOAD) :
  {
    . = ALIGN(16);
    __stack_bottom = .;
    _stack_bottom = .;
    . += 0x4000;
    __stack_top = .;
    _stack_top = .;
  } > dram

  /DISCARD/ :
  {
    *(.comment)
    *(.riscv.attributes)
    *(.eh_frame)
    *(.eh_frame_hdr)
    *(.note.*)
    *(.gnu*)
  }
}
