#include "lib.h"

void *mem_set(void *dst, int value, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}

void *mem_copy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *mem_move(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int mem_cmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        ++x;
        ++y;
    }
    return 0;
}

size_t str_len(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int str_ncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *b && *a == *b) { ++a; ++b; --n; }
    if (!n) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int str_casecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *str_copy(char *dst, const char *src, size_t size) {
    if (!dst || !size) return dst;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < size) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = 0;
    return dst;
}

char *str_cat(char *dst, const char *src, size_t size) {
    if (!dst || !size) return dst;
    size_t pos = str_len(dst);
    if (pos + 1 >= size || !src) return dst;
    str_copy(dst + pos, src, size - pos);
    return dst;
}

char *str_chr(const char *s, int c) {
    if (!s) return 0;
    while (*s) {
        if (*s == (char)c) return (char *)s;
        ++s;
    }
    return (c == 0) ? (char *)s : 0;
}

char *str_rchr(const char *s, int c) {
    if (!s) return 0;
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        ++s;
    }
    if (c == 0) return (char *)s;
    return (char *)last;
}

char *str_str(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (!*needle) return (char *)haystack;
    size_t nlen = str_len(needle);
    while (*haystack) {
        if (str_ncmp(haystack, needle, nlen) == 0) return (char *)haystack;
        ++haystack;
    }
    return 0;
}

int str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        ++s;
        ++prefix;
    }
    return 1;
}

int str_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return 0;
    size_t slen = str_len(s);
    size_t xlen = str_len(suffix);
    if (xlen > slen) return 0;
    return str_cmp(s + slen - xlen, suffix) == 0;
}

void str_lower(char *s) {
    while (s && *s) {
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
        ++s;
    }
}

void str_upper(char *s) {
    while (s && *s) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
        ++s;
    }
}

int str_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

void str_trim(char *s) {
    if (!s) return;
    char *start = s;
    while (*start && str_is_space(*start)) ++start;
    size_t len = str_len(start);
    if (start != s) mem_move(s, start, len + 1);
    while (len > 0 && str_is_space(s[len - 1])) s[--len] = 0;
}

int str_is_digit(char c) {
    return c >= '0' && c <= '9';
}

int str_to_int(const char *s, int *out) {
    if (!s || !*s) return -1;
    int negative = 0;
    if (*s == '+' || *s == '-') {
        negative = (*s == '-');
        ++s;
    }
    if (!*s) return -1;
    unsigned long long value = 0;
    while (*s) {
        if (!str_is_digit(*s)) return -1;
        value = value * 10ULL + (unsigned)(*s - '0');
        if (value > 2147483648ULL) return -1;
        ++s;
    }
    if (negative) {
        if (value > 2147483648ULL) return -1;
        if (value == 2147483648ULL) *out = (-2147483647 - 1);
        else *out = -(int)value;
    } else {
        if (value > 2147483647ULL) return -1;
        *out = (int)value;
    }
    return 0;
}

int str_to_uint(const char *s, unsigned *out) {
    if (!s || !*s) return -1;
    unsigned long long value = 0;
    while (*s) {
        if (!str_is_digit(*s)) return -1;
        value = value * 10ULL + (unsigned)(*s - '0');
        if (value > 4294967295ULL) return -1;
        ++s;
    }
    *out = (unsigned)value;
    return 0;
}

void int_to_str(int value, char *buf, size_t size) {
    if (!buf || !size) return;
    long long v = value;
    int negative = v < 0;
    unsigned long long u = negative ? (unsigned long long)(-v) : (unsigned long long)v;
    char tmp[24];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + (unsigned)(u % 10ULL));
        u /= 10ULL;
    } while (u);
    size_t pos = 0;
    if (negative && pos + 1 < size) buf[pos++] = '-';
    while (n && pos + 1 < size) buf[pos++] = tmp[--n];
    buf[pos] = 0;
}

void uint_to_str(unsigned value, char *buf, size_t size) {
    if (!buf || !size) return;
    char tmp[16];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value);
    size_t pos = 0;
    while (n && pos + 1 < size) buf[pos++] = tmp[--n];
    buf[pos] = 0;
}

int str_glob(const char *pattern, const char *text) {
    if (!pattern || !text) return 0;
    const char *star = 0;
    const char *match = 0;
    while (*text) {
        if (*pattern == '?' || *pattern == *text) {
            ++pattern;
            ++text;
        } else if (*pattern == '*') {
            star = pattern++;
            match = text;
        } else if (star) {
            pattern = star + 1;
            text = ++match;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') ++pattern;
    return *pattern == 0;
}

int tokenize(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    if (!line || !argv || max_args < 1) return -1;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (argc >= max_args - 1) return -1;
        char *start = p;
        char *out = p;
        int in_token = 0;
        while (*p) {
            if (*p == ' ' || *p == '\t') break;
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                in_token = 1;
                while (*p && *p != quote) *out++ = *p++;
                if (*p == quote) ++p;
                continue;
            }
            *out++ = *p++;
            in_token = 1;
        }
        if (*p) *p++ = 0;
        if (out != start || in_token) {
            *out = 0;
            argv[argc++] = start;
        }
    }
    argv[argc] = 0;
    return argc;
}
