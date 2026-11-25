#include "vga.h"
#include "shell.h"

void kernel_main(void) {
    vga_set_color(15, 1);
    vga_clear();
    vga_puts(" RioOS x86_64 ");
    vga_set_color(7, 0);
    vga_puts("\nBooted in 64-bit long mode.\n\n");
    shell_run();

    for (;;) __asm__ volatile ("cli; hlt");
}
