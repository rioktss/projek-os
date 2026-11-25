OUTPUT_FORMAT(elf32-i386)
ENTRY(boot_start)
SECTIONS
{
    . = 0x7C00;
    .boot : { KEEP(*(.boot)) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
