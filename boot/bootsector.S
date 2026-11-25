/*
 * Tiny BIOS boot sector for RioOS.
 * Loads 127 sectors from disk LBA 1 to physical 0x10000, then enters
 * 32-bit protected mode and jumps to the kernel entry point.
 */
.section .boot, "ax"
.code16
.global boot_start
boot_start:
    /* Normalize CS so the absolute labels below use segment 0. */
    ljmp $0x0000, $real_start

real_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    cld
    movb %dl, boot_drive

    /* Force the standard 80x25 VGA text mode. */
    movw $0x0003, %ax
    int $0x10
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp

    /* Read kernel payload from sectors 1..127 to 0x10000. */
    movw $dap, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc disk_error
    /* BIOS calls may clobber data segments; restore our real-mode state. */
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp

    /* Enable A20 through the fast A20 gate. */
    inb $0x92, %al
    orb $0x02, %al
    andb $0xFE, %al
    outb %al, $0x92

    cli
    lgdt gdt_descriptor
    movl %cr0, %eax
    orl $0x00000001, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_mode

.code32
protected_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x90000, %esp
    movl $0x10000, %eax
    jmp *%eax

.code16
disk_error:
    movw $0x0E45, %ax        /* BIOS teletype: 'E' */
    movw $0x0007, %bx
    int $0x10
1:
    cli
    hlt
    jmp 1b

.align 16
dap:
    .byte 0x10, 0x00
    .word 127                /* one BIOS transfer, 127 * 512 bytes */
    .word 0x0000             /* buffer offset */
    .word 0x1000             /* buffer segment: physical 0x10000 */
    .quad 1                  /* starting LBA */

boot_drive:
    .byte 0

.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF /* 32-bit code */
    .quad 0x00CF92000000FFFF /* 32-bit data */
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.org 510
.word 0xAA55
