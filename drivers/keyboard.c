#include "keyboard.h"
#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static const char normal_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0
};

static const char shifted_map[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0
};

static int is_letter(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

int keyboard_pending(void) {
    return (inb(0x64) & 1) != 0;
}

void keyboard_flush(void) {
    while (inb(0x64) & 1) (void)inb(0x60);
}

char keyboard_getchar(void) {
    static int shift_down = 0;
    static int caps_lock = 0;

    for (;;) {
        if (!(inb(0x64) & 1)) continue;
        uint8_t scan = inb(0x60);

        if (scan == 0x2A || scan == 0x36) {
            shift_down = 1;
            continue;
        }
        if (scan == 0xAA || scan == 0xB6) {
            shift_down = 0;
            continue;
        }
        if (scan & 0x80) continue; /* Ignore other key-release codes. */
        if (scan == 0x3A) {
            caps_lock = !caps_lock;
            continue;
        }
        if (scan >= 128) continue;

        char ch = normal_map[scan];
        if (!ch) continue;

        if (is_letter(ch)) {
            if (shift_down ^ caps_lock) ch = (char)(ch - 'a' + 'A');
        } else if (shift_down) {
            ch = shifted_map[scan];
            if (!ch) continue;
        }
        return ch;
    }
}
