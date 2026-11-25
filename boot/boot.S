/* 32-bit entry stub: BIOS boot sector jumps here at physical 0x10000. */
.section .text.boot, "ax"
.code32
.global _start
.type _start, @function
.extern kernel_main
.extern __bss_start
.extern __bss_end

_start:
    cli
    cld

    /* Zero .bss before using page tables or the kernel stack. */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb

    movl $stack_top, %esp

    /* PML4[0] -> PDPT; PDPT[0] -> PD. */
    movl $pdpt_table, %eax
    orl $0x03, %eax
    movl %eax, pml4_table
    movl $0, pml4_table + 4

    movl $pd_table, %eax
    orl $0x03, %eax
    movl %eax, pdpt_table
    movl $0, pdpt_table + 4

    /* Identity-map the first 1 GiB using 512 x 2 MiB pages.
       The first 2 MiB uses PCD/PWT so legacy VGA text memory is uncached. */
    movl $pd_table, %edi
    movl $0x0000009B, %eax
    movl $512, %ecx
1:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x00200000, %eax
    andl $0xFFFFFFE7, %eax
    addl $8, %edi
    loop 1b

    /* Enable PAE. */
    movl %cr4, %eax
    orl $0x00000020, %eax
    movl %eax, %cr4

    /* Point CR3 at our PML4. */
    movl $pml4_table, %eax
    movl %eax, %cr3

    /* Enable long mode in EFER. */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x00000100, %eax
    wrmsr

    /* Enable paging (protected mode is already active). */
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    lgdt gdt64_descriptor
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    xorq %rbp, %rbp
    movq $stack_top, %rsp
    andq $-16, %rsp
    call kernel_main

2:
    cli
    hlt
    jmp 2b

.align 8
gdt64_start:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF /* 64-bit code */
    .quad 0x00CF92000000FFFF /* data */
gdt64_end:
gdt64_descriptor:
    .word gdt64_end - gdt64_start - 1
    .long gdt64_start

.section .bss, "aw", @nobits
.align 4096
pml4_table:
    .skip 4096
.align 4096
pdpt_table:
    .skip 4096
.align 4096
pd_table:
    .skip 4096
.align 16
stack_bottom:
    .skip 16384
stack_top:
