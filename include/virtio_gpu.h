#pragma once
#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint32_t device_features[2];
    uint16_t queue_size;
    uint32_t width;
    uint32_t height;
    uintptr_t framebuffer_phys;
    uint32_t framebuffer_size;
} gpu_info_t;

/* Initialize the virtio-gpu PCI device and its first 2D scanout. */
int gpu_init(void);
int gpu_ready(void);
void gpu_get_info(gpu_info_t *info);

/* Pixel helpers (B8G8R8A8 little-endian: 0xAARRGGBB). */
uint32_t gpu_color(uint8_t r, uint8_t g, uint8_t b);
void gpu_clear(uint32_t color);
void gpu_set_pixel(uint32_t x, uint32_t y, uint32_t color);
void gpu_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gpu_text(uint32_t x, uint32_t y, const char *text, uint32_t scale, uint32_t color);
int gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Draw a colour-bar / gradient demo onto the scanout. */
void gpu_test_pattern(void);
