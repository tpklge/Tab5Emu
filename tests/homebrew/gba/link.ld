ENTRY(_start)
SECTIONS {
  . = 0x08000000;
  .header : { KEEP(*(.header)) }
  .text : { *(.text.start) *(.text*) }
  .rodata : { *(.rodata*) }
  . = 0x02000000;
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) }
}
