#include "vga.h"
#include "shell.h"
#include "virtio_gpu.h"
#include "lib.h"

void kernel_main(void) {
    vga_set_color(15, 1);
    vga_clear();
    vga_puts(" RioOS x86_64 ");
    vga_set_color(7, 0);
    vga_puts("\nBooted in 64-bit long mode.\n");

    if (gpu_init() == 0) {
        gpu_info_t info;
        gpu_get_info(&info);
        char width[16];
        char height[16];
        uint_to_str(info.width, width, sizeof(width));
        uint_to_str(info.height, height, sizeof(height));
        vga_puts("virtio-gpu: scanout ready (");
        vga_puts(width);
        vga_putc('x');
        vga_puts(height);
        vga_puts(")\n");
    } else {
        vga_puts("virtio-gpu: device not found\n");
    }
    vga_putc('\n');

    shell_run();

    for (;;) __asm__ volatile ("cli; hlt");
}
