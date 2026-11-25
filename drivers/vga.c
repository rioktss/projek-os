#include "vga.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static volatile uint16_t *const VGA = (volatile uint16_t *)0xB8000;
static int row = 0;
static int col = 0;
static uint8_t color = 0x07;

static void cursor_update(void) {
    uint16_t position = (uint16_t)(row * VGA_WIDTH + col);
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)0x3D4));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(position & 0xFF)), "Nd"((uint16_t)0x3D5));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D4));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(position >> 8)), "Nd"((uint16_t)0x3D5));
}

static void scroll_if_needed(void) {
    if (row < VGA_HEIGHT) return;

    for (int r = 1; r < VGA_HEIGHT; ++r) {
        for (int c = 0; c < VGA_WIDTH; ++c) {
            VGA[(r - 1) * VGA_WIDTH + c] = VGA[r * VGA_WIDTH + c];
        }
    }
    for (int c = 0; c < VGA_WIDTH; ++c) {
        VGA[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = (uint16_t)((color << 8) | ' ');
    }
    row = VGA_HEIGHT - 1;
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    color = (uint8_t)(((bg & 0x0F) << 4) | (fg & 0x0F));
}

uint8_t vga_get_color(void) {
    return color;
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        VGA[i] = (uint16_t)((color << 8) | ' ');
    }
    row = 0;
    col = 0;
    cursor_update();
}

void vga_putc(char ch) {
    if (ch == '\n') {
        col = 0;
        ++row;
        scroll_if_needed();
        cursor_update();
        return;
    }

    if (ch == '\r') {
        col = 0;
        cursor_update();
        return;
    }

    if (ch == '\b') {
        if (col > 0) {
            --col;
        } else if (row > 0) {
            --row;
            col = VGA_WIDTH - 1;
        }
        VGA[row * VGA_WIDTH + col] = (uint16_t)((color << 8) | ' ');
        cursor_update();
        return;
    }

    if (ch == '\t') {
        int spaces = 4 - (col % 4);
        while (spaces--) vga_putc(' ');
        return;
    }

    VGA[row * VGA_WIDTH + col] = (uint16_t)((color << 8) | (uint8_t)ch);
    ++col;
    if (col >= VGA_WIDTH) {
        col = 0;
        ++row;
        scroll_if_needed();
    }
    cursor_update();
}

void vga_puts(const char *s) {
    if (!s) return;
    while (*s) vga_putc(*s++);
}
