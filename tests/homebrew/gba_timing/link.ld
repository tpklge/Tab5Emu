ENTRY(_start)
SECTIONS {
  . = 0x08000000;
  .header : { KEEP(*(.header)) }
  .text : { *(.text.start) *(.text*) }
  .rodata : { *(.rodata*) *(.data*) }
  __iwram_load = ALIGN(.,4);
  .iwram 0x03000000 : AT(__iwram_load) {
    __iwram_start = .; *(.iwram*) . = ALIGN(4); __iwram_end = .;
  }
  __ewram_load = __iwram_load + SIZEOF(.iwram);
  .ewram 0x02000000 : AT(__ewram_load) {
    __ewram_start = .; *(.ewram*) . = ALIGN(4); __ewram_end = .;
  }
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) }
}
