/*
 * Minimal virtio-gpu 1.0 driver for RioOS.
 *
 * Uses the modern virtio PCI interface (vendor-specific capabilities in PCI
 * config space), split virtqueues and polling only - no interrupts are needed.
 * The first scanout is bound to a 2D resource backed by a physically
 * contiguous framebuffer in the kernel's .bss (identity-mapped RAM).
 */
#include "virtio_gpu.h"
#include "pci.h"
#include "lib.h"

#define VIRTIO_PCI_VENDOR_ID     0x1AF4U
#define VIRTIO_GPU_DEVICE_ID     0x1050U

#define VIRTIO_F_VERSION_1       1U         /* bit 32 = word 1, bit 0 */

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO      0x0100U
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D    0x0101U
#define VIRTIO_GPU_CMD_SET_SCANOUT           0x0103U
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH        0x0104U
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define VIRTIO_GPU_RESP_OK_NODATA            0x1100U
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO      0x1101U

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM     1U
#define VIRTIO_GPU_RESOURCE_ID               1U
#define VIRTIO_GPU_MAX_SCANOUTS              16

#define STATUS_ACKNOWLEDGE   0x01U
#define STATUS_DRIVER        0x02U
#define STATUS_FEATURES_OK   0x08U
#define STATUS_DRIVER_OK     0x04U

/* Common configuration offsets (virtio 1.0, little-endian). */
#define COMMON_DEVICE_FEATURE_SELECT 0x00U
#define COMMON_DEVICE_FEATURE        0x04U
#define COMMON_DRIVER_FEATURE_SELECT 0x08U
#define COMMON_DRIVER_FEATURE        0x0CU
#define COMMON_DEVICE_STATUS         0x14U
#define COMMON_CONFIG_GENERATION     0x15U
#define COMMON_QUEUE_SELECT          0x16U
#define COMMON_QUEUE_SIZE            0x18U
#define COMMON_QUEUE_MSIX_VECTOR     0x1AU
#define COMMON_QUEUE_ENABLE          0x1CU
#define COMMON_QUEUE_NOTIFY_OFF      0x1EU
#define COMMON_QUEUE_DESC            0x20U
#define COMMON_QUEUE_AVAIL           0x28U
#define COMMON_QUEUE_USED            0x30U

#define VQ_DESC_F_NEXT  0x1U
#define VQ_DESC_F_WRITE 0x2U
#define VQ_ENTRIES      64U

#define FB_MAX_WIDTH    1280U
#define FB_MAX_HEIGHT   800U
#define FB_ALIGN        0x200000U /* 2 MiB */
#define FB_POOL_SIZE    (6U * 1024U * 1024U)

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t ring_idx;
} gpu_ctrl_hdr; /* 24 bytes */

typedef struct {
    gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} gpu_create_2d;

typedef struct {
    gpu_ctrl_hdr hdr;
    uint32_t r_x, r_y, r_width, r_height;
    uint32_t scanout_id;
    uint32_t resource_id;
} gpu_set_scanout;

typedef struct {
    gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} gpu_attach_backing;

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} gpu_mem_entry;

typedef struct {
    gpu_ctrl_hdr hdr;
    uint32_t r_x, r_y, r_width, r_height;
    uint32_t resource_id;
    uint32_t flush_flags;
} gpu_resource_flush;

typedef struct {
    uint32_t x, y, width, height;
    uint32_t enabled;
    uint32_t flags;
} gpu_display_one; /* 24 bytes without VIRTIO_GPU_F_EDID */

typedef struct {
    gpu_ctrl_hdr hdr;
    gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} gpu_resp_display_info;

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vq_desc;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    volatile uint16_t ring[VQ_ENTRIES];
} vq_avail;

typedef struct {
    volatile uint16_t flags;
    volatile uint16_t idx;
    struct {
        volatile uint32_t id;
        volatile uint32_t len;
    } ring[VQ_ENTRIES];
    volatile uint16_t avail_event;
} vq_used;

static int gpu_initialized;
static gpu_info_t gpu_info_data;
static uint8_t bus_num, slot_num, func_num;
static uintptr_t common_cfg;
static uintptr_t notify_cfg;
static uint32_t notify_multiplier;
static uint64_t notify_addr;
static uint16_t queue_size;
static uint16_t queue_notify_off;

static vq_desc queue_desc[VQ_ENTRIES] __attribute__((aligned(16)));
static vq_avail queue_avail __attribute__((aligned(16)));
static vq_used queue_used __attribute__((aligned(16)));
static uint16_t used_index;

/* Identity-mapped RAM.  The pointer is aligned at runtime so a 4 MiB
 * framebuffer never crosses a 2 MiB page table entry. */
static uint8_t framebuffer_pool[FB_POOL_SIZE] __attribute__((aligned(FB_ALIGN)));
static uint8_t *framebuffer;
static uint32_t fb_width, fb_height, fb_stride;

static inline void barrier(void) {
    __asm__ volatile ("" ::: "memory");
}

static inline uint8_t mmio_read8(uintptr_t addr) {
    return *(volatile uint8_t *)addr;
}
static inline uint16_t mmio_read16(uintptr_t addr) {
    return *(volatile uint16_t *)addr;
}
static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}
static inline void mmio_write8(uintptr_t addr, uint8_t value) {
    *(volatile uint8_t *)addr = value;
}
static inline void mmio_write16(uintptr_t addr, uint16_t value) {
    *(volatile uint16_t *)addr = value;
}
static inline void mmio_write32(uintptr_t addr, uint32_t value) {
    *(volatile uint32_t *)addr = value;
}

static void mmio_write64(uintptr_t addr, uint64_t value) {
    mmio_write32(addr, (uint32_t)value);
    mmio_write32(addr + 4U, (uint32_t)(value >> 32));
}

static int set_status(uint8_t status) {
    mmio_write8(common_cfg + COMMON_DEVICE_STATUS, status);
    return mmio_read8(common_cfg + COMMON_DEVICE_STATUS) == status ? 0 : -1;
}

/* Submit one control command and wait for its completion.  `extra` (when
 * non-null) is a device-readable descriptor placed between the command and
 * the device-writable response. */
static int submit_command(void *cmd, uint32_t cmd_len,
                          void *extra, uint32_t extra_len,
                          void *resp, uint32_t resp_len) {
    uint32_t n = 0;

    queue_desc[n].addr = (uint64_t)(uintptr_t)cmd;
    queue_desc[n].len = cmd_len;
    queue_desc[n].flags = (extra || resp) ? VQ_DESC_F_NEXT : 0;
    queue_desc[n].next = (uint16_t)(n + 1U);
    ++n;

    if (extra) {
        queue_desc[n].addr = (uint64_t)(uintptr_t)extra;
        queue_desc[n].len = extra_len;
        queue_desc[n].flags = (extra && resp) ? VQ_DESC_F_NEXT : 0;
        queue_desc[n].next = (uint16_t)(n + 1U);
        ++n;
    }

    if (resp) {
        queue_desc[n].addr = (uint64_t)(uintptr_t)resp;
        queue_desc[n].len = resp_len;
        queue_desc[n].flags = VQ_DESC_F_WRITE;
        queue_desc[n].next = 0;
        ++n;
    } else {
        --n;
        queue_desc[n].flags &= (uint16_t)~VQ_DESC_F_NEXT;
        queue_desc[n].next = 0;
    }

    barrier();
    queue_avail.ring[queue_avail.idx & (uint16_t)(queue_size - 1U)] = 0;
    barrier();
    ++queue_avail.idx;
    barrier();
    mmio_write16(notify_addr, 0);

    while (queue_used.idx == used_index) __asm__ volatile ("pause");

    uint32_t id = queue_used.ring[used_index & (uint16_t)(queue_size - 1U)].id;
    ++used_index;
    return id == 0 ? 0 : -1;
}

static int find_device(void) {
    return pci_find_device(VIRTIO_PCI_VENDOR_ID, VIRTIO_GPU_DEVICE_ID,
                           &bus_num, &slot_num, &func_num);
}

static int map_virtio_capabilities(void) {
    uint8_t cap = pci_read8(bus_num, slot_num, func_num, PCI_CFG_CAP_PTR) & 0xFCU;

    /* Capability lists are not required to be sorted by offset. */
    for (int i = 0; i < 32 && cap >= 0x40U; ++i) {
        uint8_t id = pci_read8(bus_num, slot_num, func_num, cap);
        uint8_t next = pci_read8(bus_num, slot_num, func_num, (uint8_t)(cap + 1U));
        uint8_t length = pci_read8(bus_num, slot_num, func_num, (uint8_t)(cap + 2U));
        if (id == PCI_CAP_ID_VENDOR && length >= 12U) {
            uint8_t type = pci_read8(bus_num, slot_num, func_num, (uint8_t)(cap + 3U));
            uint8_t bar_index = pci_read8(bus_num, slot_num, func_num, (uint8_t)(cap + 4U));
            /* struct virtio_pci_cap: cap+5 is `id`, +6..7 padding,
             * offset lives at +8 and length at +12. */
            uint32_t offset = pci_read32_unaligned(bus_num, slot_num, func_num,
                                                   (uint8_t)(cap + 8U));
            uintptr_t base = pci_bar_base(bus_num, slot_num, func_num, bar_index);
            if (base != 0) {
                uintptr_t addr = base + (uintptr_t)offset;
                if (type == VIRTIO_PCI_CAP_COMMON_CFG) {
                    common_cfg = addr;
                } else if (type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                    notify_cfg = addr;
                    if (length >= 20U) {
                        notify_multiplier = pci_read32_unaligned(
                            bus_num, slot_num, func_num, (uint8_t)(cap + 16U));
                    }
                }
            }
        }
        if (next == 0) break;
        cap = next & 0xFCU;
    }
    return (common_cfg && notify_cfg) ? 0 : -1;
}

static void *allocate_framebuffer(uint32_t width, uint32_t height) {
    uintptr_t raw = (uintptr_t)framebuffer_pool;
    uintptr_t aligned = (raw + FB_ALIGN - 1U) & ~(uintptr_t)(FB_ALIGN - 1U);
    uint32_t size = width * height * 4U;
    if (aligned + size > raw + FB_POOL_SIZE) return 0;
    framebuffer = (uint8_t *)aligned;
    fb_width = width;
    fb_height = height;
    fb_stride = width * 4U;
    mem_set(framebuffer, 0, size);
    return framebuffer;
}

int gpu_init(void) {
    if (gpu_initialized) return 0;
    if (find_device() != 0) return -1;

    uint16_t command = pci_read16(bus_num, slot_num, func_num, PCI_CFG_COMMAND);
    pci_write16(bus_num, slot_num, func_num, PCI_CFG_COMMAND, command | 0x0007U);

    if (map_virtio_capabilities() != 0) return -1;

    /* Reset, then acknowledge and declare this driver. */
    mmio_write8(common_cfg + COMMON_DEVICE_STATUS, 0);
    while (mmio_read8(common_cfg + COMMON_DEVICE_STATUS) != 0) __asm__ volatile ("pause");
    if (set_status(STATUS_ACKNOWLEDGE) != 0) return -1;
    if (set_status(STATUS_ACKNOWLEDGE | STATUS_DRIVER) != 0) return -1;

    mmio_write32(common_cfg + COMMON_DEVICE_FEATURE_SELECT, 0);
    gpu_info_data.device_features[0] = mmio_read32(common_cfg + COMMON_DEVICE_FEATURE);
    mmio_write32(common_cfg + COMMON_DEVICE_FEATURE_SELECT, 1);
    gpu_info_data.device_features[1] = mmio_read32(common_cfg + COMMON_DEVICE_FEATURE);

    /* A modern-only device requires VIRTIO_F_VERSION_1.  Other features
     * (VIRGL, EDID, packed rings, event_idx) are deliberately not used. */
    if ((gpu_info_data.device_features[1] & VIRTIO_F_VERSION_1) == 0) return -1;
    mmio_write32(common_cfg + COMMON_DRIVER_FEATURE_SELECT, 1);
    mmio_write32(common_cfg + COMMON_DRIVER_FEATURE, VIRTIO_F_VERSION_1);
    if (set_status(STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK) != 0) return -1;

    /* Control virtqueue (index 0).  One command at a time, so descriptors
     * 0..2 are reused for every submission. */
    mmio_write16(common_cfg + COMMON_QUEUE_SELECT, 0);
    queue_size = mmio_read16(common_cfg + COMMON_QUEUE_SIZE);
    if (queue_size == 0 || queue_size > VQ_ENTRIES ||
        (queue_size & (uint16_t)(queue_size - 1U)) != 0) return -1;

    mmio_write64(common_cfg + COMMON_QUEUE_DESC, (uint64_t)(uintptr_t)queue_desc);
    mmio_write64(common_cfg + COMMON_QUEUE_AVAIL, (uint64_t)(uintptr_t)&queue_avail);
    mmio_write64(common_cfg + COMMON_QUEUE_USED, (uint64_t)(uintptr_t)&queue_used);
    mmio_write16(common_cfg + COMMON_QUEUE_ENABLE, 1);
    queue_notify_off = mmio_read16(common_cfg + COMMON_QUEUE_NOTIFY_OFF);
    notify_addr = (uint64_t)(uintptr_t)notify_cfg +
                  (uint64_t)queue_notify_off * (uint64_t)notify_multiplier;

    mmio_write8(common_cfg + COMMON_DEVICE_STATUS,
                (uint8_t)(STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK |
                          STATUS_DRIVER_OK));

    gpu_ctrl_hdr header;
    gpu_ctrl_hdr response;
    mem_set(&header, 0, sizeof(header));
    header.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    gpu_resp_display_info display;
    mem_set(&display, 0, sizeof(display));
    if (submit_command(&header, sizeof(header), 0, 0, &display, sizeof(display)) != 0) return -1;
    if (display.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return -1;

    uint32_t width = display.pmodes[0].width;
    uint32_t height = display.pmodes[0].height;
    if (width == 0 || height == 0) return -1;
    if (width > FB_MAX_WIDTH) width = FB_MAX_WIDTH;
    if (height > FB_MAX_HEIGHT) height = FB_MAX_HEIGHT;
    if (allocate_framebuffer(width, height) == 0) return -1;

    mem_set(&header, 0, sizeof(header));
    header.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    gpu_create_2d create;
    mem_set(&create, 0, sizeof(create));
    create.hdr.type = header.type;
    create.resource_id = VIRTIO_GPU_RESOURCE_ID;
    create.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    create.width = width;
    create.height = height;
    if (submit_command(&create, sizeof(create), 0, 0, &response, sizeof(response)) != 0) return -1;
    if (response.type != VIRTIO_GPU_RESP_OK_NODATA) return -1;

    /* Attach the guest framebuffer first: QEMU needs the backing store
     * in place before a resource can be bound to a scanout. */
    gpu_attach_backing backing;
    mem_set(&backing, 0, sizeof(backing));
    backing.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    backing.resource_id = VIRTIO_GPU_RESOURCE_ID;
    backing.nr_entries = 1;
    gpu_mem_entry entry;
    entry.addr = (uint64_t)(uintptr_t)framebuffer;
    entry.length = width * height * 4U;
    entry.padding = 0;
    if (submit_command(&backing, sizeof(backing), &entry, sizeof(entry),
                       &response, sizeof(response)) != 0) return -1;
    if (response.type != VIRTIO_GPU_RESP_OK_NODATA) return -1;

    gpu_set_scanout scanout;
    mem_set(&scanout, 0, sizeof(scanout));
    scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout.r_width = width;
    scanout.r_height = height;
    scanout.scanout_id = 0;
    scanout.resource_id = VIRTIO_GPU_RESOURCE_ID;
    if (submit_command(&scanout, sizeof(scanout), 0, 0, &response, sizeof(response)) != 0) return -1;
    if (response.type != VIRTIO_GPU_RESP_OK_NODATA) return -1;

    gpu_info_data.bus = bus_num;
    gpu_info_data.slot = slot_num;
    gpu_info_data.func = func_num;
    gpu_info_data.vendor = VIRTIO_PCI_VENDOR_ID;
    gpu_info_data.device = VIRTIO_GPU_DEVICE_ID;
    gpu_info_data.queue_size = queue_size;
    gpu_info_data.width = width;
    gpu_info_data.height = height;
    gpu_info_data.framebuffer_phys = (uintptr_t)framebuffer;
    gpu_info_data.framebuffer_size = width * height * 4U;
    gpu_initialized = 1;
    return 0;
}

int gpu_ready(void) {
    return gpu_initialized;
}

void gpu_get_info(gpu_info_t *info) {
    if (info) *info = gpu_info_data;
}

uint32_t gpu_color(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void gpu_clear(uint32_t color) {
    if (!gpu_initialized) return;
    uint32_t count = fb_width * fb_height;
    uint32_t *pixel = (uint32_t *)framebuffer;
    for (uint32_t i = 0; i < count; ++i) pixel[i] = color;
}

void gpu_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!gpu_initialized || x >= fb_width || y >= fb_height) return;
    ((uint32_t *)framebuffer)[y * fb_width + x] = color;
}

void gpu_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!gpu_initialized || w == 0 || h == 0) return;
    if (x >= fb_width) return;
    if (y >= fb_height) return;
    if (w > fb_width - x) w = fb_width - x;
    if (h > fb_height - y) h = fb_height - y;
    for (uint32_t row = 0; row < h; ++row) {
        uint32_t *dst = (uint32_t *)(framebuffer + (y + row) * fb_stride) + x;
        for (uint32_t col = 0; col < w; ++col) dst[col] = color;
    }
}

int gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!gpu_initialized) return -1;
    if (x >= fb_width || y >= fb_height || w == 0 || h == 0) return -1;
    if (w > fb_width - x) w = fb_width - x;
    if (h > fb_height - y) h = fb_height - y;

    gpu_resource_flush flush;
    mem_set(&flush, 0, sizeof(flush));
    flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.r_x = x;
    flush.r_y = y;
    flush.r_width = w;
    flush.r_height = h;
    flush.resource_id = VIRTIO_GPU_RESOURCE_ID;

    gpu_ctrl_hdr response;
    if (submit_command(&flush, sizeof(flush), 0, 0, &response, sizeof(response)) != 0) return -1;
    return response.type == VIRTIO_GPU_RESP_OK_NODATA ? 0 : -1;
}

/* Compact 5x7 glyphs for the letters used by the demo banner. */
static const uint8_t glyph_rows[9][7] = {
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, /* I */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}  /* P */
};

static int glyph_index(char c) {
    if (c >= 'A' && c <= 'Z') {
        const char *map = "RIOSTVUGP";
        for (int i = 0; i < 9; ++i) if (map[i] == c) return i;
    }
    return -1;
}

void gpu_text(uint32_t x, uint32_t y, const char *text, uint32_t scale, uint32_t color) {
    if (!gpu_initialized || !text || scale == 0) return;
    uint32_t pen = x;
    for (const char *p = text; *p; ++p) {
        int glyph = glyph_index(*p);
        if (glyph >= 0) {
            for (uint32_t row = 0; row < 7; ++row) {
                uint8_t bits = glyph_rows[glyph][row];
                for (uint32_t col = 0; col < 5; ++col) {
                    if (bits & (uint8_t)(1U << (4U - col))) {
                        gpu_fill_rect(pen + col * scale, y + row * scale,
                                      scale, scale, color);
                    }
                }
            }
        }
        pen += 6U * scale;
    }
}

void gpu_test_pattern(void) {
    if (!gpu_initialized) return;

    gpu_clear(gpu_color(8, 12, 32));

    const uint32_t bars[8] = {
        gpu_color(255, 255, 255), gpu_color(255, 255, 0),
        gpu_color(0, 255, 255),   gpu_color(0, 255, 0),
        gpu_color(255, 0, 255),   gpu_color(255, 0, 0),
        gpu_color(0, 0, 255),     gpu_color(16, 20, 48)
    };
    uint32_t bar_height = fb_height / 8U;
    for (uint32_t i = 0; i < 8; ++i) {
        gpu_fill_rect(0, i * bar_height, fb_width, bar_height, bars[i]);
    }

    uint32_t band_y = bar_height + 40U;
    uint32_t band_h = fb_height / 4U;
    if (band_y + band_h > fb_height) band_h = fb_height - band_y;
    for (uint32_t x = 0; x < fb_width; ++x) {
        uint32_t r = (x * 255U) / (fb_width - 1U);
        uint32_t g = ((fb_width - 1U - x) * 255U) / (fb_width - 1U);
        uint32_t b = (x * 2U) & 0xFFU;
        gpu_fill_rect(x, band_y, 1, band_h, gpu_color((uint8_t)r, (uint8_t)g, (uint8_t)b));
    }

    uint32_t square = 16U;
    for (uint32_t y = 0; y < fb_height; y += square) {
        for (uint32_t x = 0; x < fb_width; x += square) {
            if (((x / square) + (y / square)) & 1U) {
                gpu_fill_rect(x, y, square, square, gpu_color(24, 28, 56));
            }
        }
    }

    gpu_text(80, band_y + band_h + 60U, "RIOOS VIRTIO GPU", 5U,
             gpu_color(255, 255, 255));
    gpu_flush(0, 0, fb_width, fb_height);
}
