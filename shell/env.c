#include "env.h"
#include "lib.h"

#define ENV_MAX 24
#define ENV_NAME_MAX 24
#define ENV_VALUE_MAX 96

typedef struct {
    char name[ENV_NAME_MAX];
    char value[ENV_VALUE_MAX];
    int used;
} env_entry_t;

static env_entry_t entries[ENV_MAX];

static int valid_name(const char *name) {
    if (!name || !*name) return 0;
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) return 0;
    for (const char *p = name + 1; *p; ++p) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) return 0;
    }
    return 1;
}

static int set_raw(const char *name, const char *value) {
    if (!valid_name(name)) return -1;
    if (str_len(value) + 1 > ENV_VALUE_MAX) return -1;
    int free_slot = -1;
    for (int i = 0; i < ENV_MAX; ++i) {
        if (entries[i].used && str_cmp(entries[i].name, name) == 0) {
            str_copy(entries[i].value, value, ENV_VALUE_MAX);
            return 0;
        }
        if (!entries[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return -1;
    entries[free_slot].used = 1;
    str_copy(entries[free_slot].name, name, ENV_NAME_MAX);
    str_copy(entries[free_slot].value, value, ENV_VALUE_MAX);
    return 0;
}

void env_init(void) {
    mem_set(entries, 0, sizeof(entries));
    set_raw("USER", "rio");
    set_raw("LOGNAME", "rio");
    set_raw("HOME", "/home/rio");
    set_raw("PWD", "/home/rio");
    set_raw("SHELL", "/bin/riosh");
    set_raw("HOSTNAME", "rioos");
    set_raw("TERM", "vga");
    set_raw("PATH", "/bin:/usr/bin");
    set_raw("OS", "RioOS");
    set_raw("VERSION", "2.1.0");
}

const char *env_get(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < ENV_MAX; ++i) {
        if (entries[i].used && str_cmp(entries[i].name, name) == 0) {
            return entries[i].value;
        }
    }
    return 0;
}

int env_set(const char *name, const char *value) {
    if (!name || !value) return -1;
    return set_raw(name, value);
}

int env_unset(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < ENV_MAX; ++i) {
        if (entries[i].used && str_cmp(entries[i].name, name) == 0) {
            entries[i].used = 0;
            entries[i].name[0] = 0;
            entries[i].value[0] = 0;
            return 0;
        }
    }
    return 0;
}

int env_slot_count(void) {
    return ENV_MAX;
}

const char *env_name_at(int index) {
    if (index < 0 || index >= ENV_MAX || !entries[index].used) return 0;
    return entries[index].name;
}

const char *env_value_at(int index) {
    if (index < 0 || index >= ENV_MAX || !entries[index].used) return 0;
    return entries[index].value;
}

void env_expand(const char *src, char *dst, size_t size) {
    if (!dst || !size) return;
    size_t d = 0;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (*src && d + 1 < size) {
        if (*src == '$' && src[1]) {
            const char *p = src + 1;
            char name[ENV_NAME_MAX];
            size_t n = 0;
            int valid = 0;
            if (*p == '{') {
                ++p;
                while (*p && *p != '}' && n + 1 < sizeof(name)) name[n++] = *p++;
                if (*p == '}') {
                    ++p;
                    valid = 1;
                }
            } else {
                while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                       (*p >= '0' && *p <= '9') || *p == '_') {
                    if (n + 1 < sizeof(name)) name[n++] = *p;
                    ++p;
                }
                valid = n > 0;
            }
            if (valid) {
                name[n] = 0;
                const char *value = env_get(name);
                if (value) {
                    while (*value && d + 1 < size) dst[d++] = *value++;
                }
                src = p;
                continue;
            }
        }
        dst[d++] = *src++;
    }
    dst[d] = 0;
}
