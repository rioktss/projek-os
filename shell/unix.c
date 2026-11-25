#include "unix.h"
#include "vga.h"
#include "keyboard.h"
#include "lib.h"
#include "env.h"
#include "ramfs.h"
#include "time.h"
#include <stdint.h>

#define OS_NAME "RioOS"
#define OS_VERSION "2.1.0"
#define MACHINE "x86_64"
#define HOST_FALLBACK "rioos"

static void out(const char *s) { vga_puts(s); }
static void outln(const char *s) { vga_puts(s); vga_putc('\n'); }

static void out_uint(unsigned value) {
    char buf[16];
    uint_to_str(value, buf, sizeof(buf));
    vga_puts(buf);
}

static void out_int(int value) {
    char buf[16];
    int_to_str(value, buf, sizeof(buf));
    vga_puts(buf);
}

static void out_2(int value) {
    if (value < 10) vga_putc('0');
    out_uint((unsigned)value);
}

static void out_padded_uint(unsigned value, unsigned width) {
    char buf[16];
    uint_to_str(value, buf, sizeof(buf));
    unsigned len = (unsigned)str_len(buf);
    while (len < width) {
        vga_putc(' ');
        ++len;
    }
    vga_puts(buf);
}

static void out_hex(unsigned value, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[16];
    int n = 0;
    if (value == 0) {
        vga_putc('0');
        return;
    }
    while (value) {
        buf[n++] = digits[value & 0xFU];
        value >>= 4;
    }
    while (n) vga_putc(buf[--n]);
}

static void out_hex_padded(unsigned value, int width, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[16];
    int n = 0;
    do {
        buf[n++] = digits[value & 0xFU];
        value >>= 4;
    } while (value);
    while (n < width) buf[n++] = '0';
    while (n) vga_putc(buf[--n]);
}

static void cmd_error(const char *cmd, const char *message) {
    vga_puts(cmd);
    vga_puts(": ");
    vga_puts(message);
    vga_putc('\n');
}

static void fs_error(const char *cmd, int error) {
    cmd_error(cmd, ramfs_strerror(error));
}

static void usage_error(const char *cmd, const char *usage) {
    vga_puts("Usage: ");
    vga_puts(cmd);
    if (usage && *usage) {
        vga_putc(' ');
        vga_puts(usage);
    }
    vga_putc('\n');
}

static int get_file(const char *cmd, const char *path,
                    const char **data, unsigned *size) {
    int index;
    int rc = ramfs_lookup(path, &index);
    if (rc != RAMFS_OK) {
        fs_error(cmd, rc);
        return -1;
    }
    if (!ramfs_is_file(index)) {
        cmd_error(cmd, "Is a directory");
        return -1;
    }
    *data = ramfs_data(index);
    *size = ramfs_size(index);
    return 0;
}

static int collect_children(int dir, int *out_indices, int max) {
    int count = 0;
    int child = ramfs_first_child(dir);
    while (child >= 0) {
        if (count < max) out_indices[count++] = child;
        child = ramfs_next_child(dir, child);
    }
    return count;
}

static void sort_node_indices(int *indices, int count) {
    for (int i = 1; i < count; ++i) {
        int key = indices[i];
        int j = i - 1;
        while (j >= 0 && str_cmp(ramfs_name(indices[j]), ramfs_name(key)) > 0) {
            indices[j + 1] = indices[j];
            --j;
        }
        indices[j + 1] = key;
    }
}

static void build_child_path(const char *base, const char *name,
                             char *out_buf, size_t size) {
    str_copy(out_buf, base, size);
    size_t len = str_len(out_buf);
    if (len == 0 || out_buf[len - 1] != '/') {
        str_cat(out_buf, "/", size);
    }
    str_cat(out_buf, name, size);
}

static int line_contains(const char *line, unsigned len,
                         const char *needle, unsigned needle_len, int ignore_case) {
    if (needle_len == 0) return 1;
    if (needle_len > len) return 0;
    for (unsigned i = 0; i + needle_len <= len; ++i) {
        unsigned j = 0;
        while (j < needle_len) {
            char a = line[i + j];
            char b = needle[j];
            if (ignore_case) {
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            }
            if (a != b) break;
            ++j;
        }
        if (j == needle_len) return 1;
    }
    return 0;
}

static int parse_line_int(const char *p, unsigned len) {
    unsigned i = 0;
    while (i < len && (p[i] == ' ' || p[i] == '\t')) ++i;
    int negative = 0;
    if (i < len && (p[i] == '+' || p[i] == '-')) {
        negative = (p[i] == '-');
        ++i;
    }
    long long value = 0;
    int any = 0;
    while (i < len && p[i] >= '0' && p[i] <= '9') {
        any = 1;
        value = value * 10 + (p[i] - '0');
        if (value > 2147483647LL) value = 2147483647LL;
        ++i;
    }
    if (!any) return 0;
    return negative ? (int)-value : (int)value;
}

void unix_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[RAMFS_PATH_MAX];
    int rc = ramfs_getcwd(buf, sizeof(buf));
    if (rc != RAMFS_OK) {
        fs_error("pwd", rc);
        return;
    }
    outln(buf);
}

void unix_cd(int argc, char **argv) {
    if (argc > 2) {
        usage_error("cd", "[PATH]");
        return;
    }
    const char *target;
    char buffer[RAMFS_PATH_MAX];
    int print_after = 0;

    if (argc == 1) {
        target = env_get("HOME");
        if (!target) target = "/";
    } else if (str_cmp(argv[1], "-") == 0) {
        target = env_get("OLDPWD");
        if (!target) {
            cmd_error("cd", "OLDPWD not set");
            return;
        }
        print_after = 1;
    } else if (str_cmp(argv[1], "~") == 0) {
        target = env_get("HOME");
        if (!target) target = "/";
    } else if (str_starts_with(argv[1], "~/")) {
        const char *home = env_get("HOME");
        if (!home) home = "/";
        if (str_len(home) + str_len(argv[1]) + 1 > sizeof(buffer)) {
            cmd_error("cd", "path too long");
            return;
        }
        str_copy(buffer, home, sizeof(buffer));
        str_cat(buffer, argv[1] + 1, sizeof(buffer));
        target = buffer;
    } else {
        target = argv[1];
    }

    char old_dir[RAMFS_PATH_MAX];
    if (ramfs_getcwd(old_dir, sizeof(old_dir)) != RAMFS_OK) old_dir[0] = 0;

    int rc = ramfs_chdir(target);
    if (rc != RAMFS_OK) {
        fs_error("cd", rc);
        return;
    }

    char new_dir[RAMFS_PATH_MAX];
    if (ramfs_getcwd(new_dir, sizeof(new_dir)) == RAMFS_OK) {
        env_set("OLDPWD", old_dir);
        env_set("PWD", new_dir);
        if (print_after) outln(new_dir);
    }
}

static void ls_print_entry(int index, const char *display, int long_format) {
    if (long_format) {
        if (ramfs_is_dir(index)) out("drwxr-xr-x ");
        else out("-rw-r--r-- ");
        out_padded_uint(ramfs_size(index), 8);
        vga_putc(' ');
    }
    if (ramfs_is_dir(index)) vga_set_color(11, 0);
    out(display);
    vga_set_color(7, 0);
    if (long_format) vga_putc('\n');
}

void unix_ls(int argc, char **argv) {
    int long_format = 0;
    int show_all = 0;
    const char *path = ".";
    int path_set = 0;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'l') long_format = 1;
                else if (*p == 'a') show_all = 1;
                else {
                    cmd_error("ls", "invalid option");
                    return;
                }
            }
        } else if (!path_set) {
            path = arg;
            path_set = 1;
        } else {
            usage_error("ls", "[-l] [-a] [PATH]");
            return;
        }
    }

    int index;
    int rc = ramfs_lookup(path, &index);
    if (rc != RAMFS_OK) {
        fs_error("ls", rc);
        return;
    }

    if (ramfs_is_file(index)) {
        ls_print_entry(index, path, long_format);
        if (!long_format) vga_putc('\n');
        return;
    }

    int entries[RAMFS_MAX_NODES];
    int count = collect_children(index, entries, RAMFS_MAX_NODES);
    sort_node_indices(entries, count);

    int printed = 0;
    if (show_all) {
        if (long_format) {
            out("drwxr-xr-x        0 .\n");
            out("drwxr-xr-x        0 ..\n");
        } else {
            out(".  ..");
            printed = 2;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (long_format) {
            ls_print_entry(entries[i], ramfs_name(entries[i]), 1);
        } else {
            if (printed) out("  ");
            if (ramfs_is_dir(entries[i])) vga_set_color(11, 0);
            out(ramfs_name(entries[i]));
            vga_set_color(7, 0);
            ++printed;
        }
    }
    if (!long_format) vga_putc('\n');
}

void unix_mkdir(int argc, char **argv) {
    int parents = 0;
    int first = 1;
    if (argc > 1 && str_cmp(argv[1], "-p") == 0) {
        parents = 1;
        first = 2;
    }
    if (first >= argc) {
        usage_error("mkdir", "[-p] PATH...");
        return;
    }
    for (int i = first; i < argc; ++i) {
        int rc = ramfs_mkdir(argv[i], parents);
        if (rc != RAMFS_OK) fs_error("mkdir", rc);
    }
}

void unix_rmdir(int argc, char **argv) {
    if (argc < 2) {
        usage_error("rmdir", "PATH...");
        return;
    }
    for (int i = 1; i < argc; ++i) {
        int rc = ramfs_rmdir(argv[i]);
        if (rc != RAMFS_OK) fs_error("rmdir", rc);
    }
}

void unix_touch(int argc, char **argv) {
    if (argc < 2) {
        usage_error("touch", "PATH...");
        return;
    }
    for (int i = 1; i < argc; ++i) {
        int rc = ramfs_touch(argv[i]);
        if (rc != RAMFS_OK) fs_error("touch", rc);
    }
}

void unix_cat(int argc, char **argv) {
    int number = 0;
    int first = 1;
    if (argc > 1 && str_cmp(argv[1], "-n") == 0) {
        number = 1;
        first = 2;
    }
    if (first >= argc) {
        usage_error("cat", "[-n] FILE...");
        return;
    }
    unsigned line_number = 0;
    int at_line_start = 1;
    for (int i = first; i < argc; ++i) {
        const char *data;
        unsigned size;
        if (get_file("cat", argv[i], &data, &size) != 0) continue;
        for (unsigned j = 0; j < size; ++j) {
            if (number && at_line_start) {
                ++line_number;
                out_padded_uint(line_number, 6);
                vga_putc('\t');
                at_line_start = 0;
            }
            char c = data[j];
            vga_putc(c);
            if (c == '\n') at_line_start = 1;
        }
    }
}

void unix_rm(int argc, char **argv) {
    int recursive = 0;
    int force = 0;
    int first = 1;
    for (; first < argc; ++first) {
        const char *arg = argv[first];
        if (arg[0] != '-' || !arg[1]) break;
        for (const char *p = arg + 1; *p; ++p) {
            if (*p == 'r' || *p == 'R') recursive = 1;
            else if (*p == 'f') force = 1;
            else {
                cmd_error("rm", "invalid option");
                return;
            }
        }
    }
    if (first >= argc) {
        usage_error("rm", "[-r] [-f] PATH...");
        return;
    }
    for (int i = first; i < argc; ++i) {
        int rc = ramfs_unlink(argv[i], recursive);
        if (rc == RAMFS_ENOENT && force) continue;
        if (rc != RAMFS_OK) fs_error("rm", rc);
    }
}

void unix_cp(int argc, char **argv) {
    int recursive = 0;
    int first = 1;
    for (; first < argc; ++first) {
        const char *arg = argv[first];
        if (arg[0] != '-' || !arg[1]) break;
        for (const char *p = arg + 1; *p; ++p) {
            if (*p == 'r' || *p == 'R') recursive = 1;
            else if (*p == 'f') continue;
            else {
                cmd_error("cp", "invalid option");
                return;
            }
        }
    }
    if (argc - first != 2) {
        usage_error("cp", "[-r] SRC DST");
        return;
    }
    int rc = ramfs_copy(argv[first], argv[first + 1], recursive);
    if (rc != RAMFS_OK) fs_error("cp", rc);
}

void unix_mv(int argc, char **argv) {
    int first = 1;
    for (; first < argc; ++first) {
        const char *arg = argv[first];
        if (arg[0] != '-' || !arg[1]) break;
        for (const char *p = arg + 1; *p; ++p) {
            if (*p == 'f') continue;
            else {
                cmd_error("mv", "invalid option");
                return;
            }
        }
    }
    if (argc - first != 2) {
        usage_error("mv", "SRC DST");
        return;
    }
    int rc = ramfs_move(argv[first], argv[first + 1]);
    if (rc != RAMFS_OK) fs_error("mv", rc);
}

static int parse_count_option(int argc, char **argv, unsigned *count, int *first) {
    *count = 10;
    *first = 1;
    if (argc > 1 && str_cmp(argv[1], "-n") == 0) {
        if (argc < 3) return -1;
        if (str_to_uint(argv[2], count) != 0) return -1;
        *first = 3;
        return 0;
    }
    if (argc > 1 && argv[1][0] == '-' && argv[1][1]) {
        const char *number = argv[1] + 1;
        if (*number == 'n') ++number;
        if (!*number || str_to_uint(number, count) != 0) return -1;
        *first = 2;
    }
    return 0;
}

void unix_head(int argc, char **argv) {
    unsigned count;
    int first;
    if (parse_count_option(argc, argv, &count, &first) != 0) {
        cmd_error("head", "invalid number");
        return;
    }
    if (first >= argc) {
        usage_error("head", "[-n N] FILE...");
        return;
    }
    for (int i = first; i < argc; ++i) {
        const char *data;
        unsigned size;
        if (get_file("head", argv[i], &data, &size) != 0) continue;
        if (argc - first > 1) {
            if (i > first) vga_putc('\n');
            out("==> ");
            out(argv[i]);
            out(" <==\n");
        }
        unsigned line = 0;
        unsigned pos = 0;
        while (pos < size && line < count) {
            char c = data[pos++];
            vga_putc(c);
            if (c == '\n') ++line;
        }
        if (pos > 0 && data[pos - 1] != '\n') vga_putc('\n');
    }
}

static unsigned tail_start(const char *data, unsigned size, unsigned lines) {
    if (lines == 0) return size;
    unsigned count = 0;
    unsigned i = size;
    if (i > 0 && data[i - 1] == '\n') --i;
    while (i > 0) {
        --i;
        if (data[i] == '\n') {
            ++count;
            if (count == lines) return i + 1;
        }
    }
    return 0;
}

void unix_tail(int argc, char **argv) {
    unsigned count;
    int first;
    if (parse_count_option(argc, argv, &count, &first) != 0) {
        cmd_error("tail", "invalid number");
        return;
    }
    if (first >= argc) {
        usage_error("tail", "[-n N] FILE...");
        return;
    }
    for (int i = first; i < argc; ++i) {
        const char *data;
        unsigned size;
        if (get_file("tail", argv[i], &data, &size) != 0) continue;
        if (argc - first > 1) {
            if (i > first) vga_putc('\n');
            out("==> ");
            out(argv[i]);
            out(" <==\n");
        }
        unsigned start = tail_start(data, size, count);
        for (unsigned j = start; j < size; ++j) vga_putc(data[j]);
        if (size > 0 && data[size - 1] != '\n') vga_putc('\n');
    }
}

static void count_text(const char *data, unsigned size,
                       unsigned *lines, unsigned *words, unsigned *bytes) {
    unsigned line_count = 0;
    unsigned word_count = 0;
    int in_word = 0;
    for (unsigned i = 0; i < size; ++i) {
        char c = data[i];
        if (c == '\n') ++line_count;
        if (str_is_space(c)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            ++word_count;
        }
    }
    *lines = line_count;
    *words = word_count;
    *bytes = size;
}

static void wc_print(unsigned lines, unsigned words, unsigned bytes,
                     int show_lines, int show_words, int show_bytes,
                     const char *name) {
    if (show_lines) out_padded_uint(lines, 7);
    if (show_words) out_padded_uint(words, 7);
    if (show_bytes) out_padded_uint(bytes, 7);
    if (name) {
        vga_putc(' ');
        out(name);
    }
    vga_putc('\n');
}

void unix_wc(int argc, char **argv) {
    int show_lines = 0;
    int show_words = 0;
    int show_bytes = 0;
    int first = 1;
    for (; first < argc; ++first) {
        const char *arg = argv[first];
        if (arg[0] != '-' || !arg[1]) break;
        for (const char *p = arg + 1; *p; ++p) {
            if (*p == 'l') show_lines = 1;
            else if (*p == 'w') show_words = 1;
            else if (*p == 'c') show_bytes = 1;
            else {
                cmd_error("wc", "invalid option");
                return;
            }
        }
    }
    if (!show_lines && !show_words && !show_bytes) {
        show_lines = show_words = show_bytes = 1;
    }
    if (first >= argc) {
        usage_error("wc", "[-l] [-w] [-c] FILE...");
        return;
    }
    unsigned total_lines = 0;
    unsigned total_words = 0;
    unsigned total_bytes = 0;
    int files = 0;
    for (int i = first; i < argc; ++i) {
        const char *data;
        unsigned size;
        if (get_file("wc", argv[i], &data, &size) != 0) continue;
        unsigned lines;
        unsigned words;
        unsigned bytes;
        count_text(data, size, &lines, &words, &bytes);
        wc_print(lines, words, bytes, show_lines, show_words, show_bytes, argv[i]);
        total_lines += lines;
        total_words += words;
        total_bytes += bytes;
        ++files;
    }
    if (files > 1) {
        wc_print(total_lines, total_words, total_bytes,
                 show_lines, show_words, show_bytes, "total");
    }
}

void unix_grep(int argc, char **argv) {
    int ignore_case = 0;
    int invert = 0;
    int show_number = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        for (const char *p = argv[i] + 1; *p; ++p) {
            if (*p == 'i') ignore_case = 1;
            else if (*p == 'v') invert = 1;
            else if (*p == 'n') show_number = 1;
            else {
                cmd_error("grep", "invalid option");
                return;
            }
        }
        ++i;
    }
    if (argc - i < 2) {
        usage_error("grep", "[-i] [-v] [-n] PATTERN FILE...");
        return;
    }
    const char *pattern = argv[i++];
    unsigned pattern_len = (unsigned)str_len(pattern);
    int multiple = (argc - i) > 1;

    for (; i < argc; ++i) {
        const char *data;
        unsigned size;
        if (get_file("grep", argv[i], &data, &size) != 0) continue;
        unsigned pos = 0;
        unsigned line_number = 0;
        while (pos < size) {
            unsigned start = pos;
            while (pos < size && data[pos] != '\n') ++pos;
            unsigned len = pos - start;
            if (pos < size) ++pos;
            ++line_number;
            int match = line_contains(data + start, len, pattern, pattern_len, ignore_case);
            if (invert) match = !match;
            if (!match) continue;
            if (multiple) {
                out(argv[i]);
                vga_putc(':');
            }
            if (show_number) {
                out_uint(line_number);
                vga_putc(':');
            }
            for (unsigned j = 0; j < len; ++j) vga_putc(data[start + j]);
            vga_putc('\n');
        }
    }
}

typedef struct {
    const char *ptr;
    unsigned len;
} line_ref_t;

static line_ref_t sort_lines[512];

static int line_compare(const line_ref_t *a, const line_ref_t *b, int numeric) {
    if (numeric) {
        int av = parse_line_int(a->ptr, a->len);
        int bv = parse_line_int(b->ptr, b->len);
        if (av < bv) return -1;
        if (av > bv) return 1;
    }
    unsigned n = a->len < b->len ? a->len : b->len;
    int cmp = mem_cmp(a->ptr, b->ptr, n);
    if (cmp != 0) return cmp;
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
    return 0;
}

void unix_sort(int argc, char **argv) {
    int reverse = 0;
    int numeric = 0;
    int first = 1;
    for (; first < argc; ++first) {
        const char *arg = argv[first];
        if (arg[0] != '-' || !arg[1]) break;
        for (const char *p = arg + 1; *p; ++p) {
            if (*p == 'r') reverse = 1;
            else if (*p == 'n') numeric = 1;
            else {
                cmd_error("sort", "invalid option");
                return;
            }
        }
    }
    if (argc - first != 1) {
        usage_error("sort", "[-r] [-n] FILE");
        return;
    }
    const char *data;
    unsigned size;
    if (get_file("sort", argv[first], &data, &size) != 0) return;

    unsigned count = 0;
    unsigned pos = 0;
    while (pos < size) {
        unsigned start = pos;
        while (pos < size && data[pos] != '\n') ++pos;
        if (count >= 512) {
            cmd_error("sort", "too many lines");
            return;
        }
        sort_lines[count].ptr = data + start;
        sort_lines[count].len = pos - start;
        ++count;
        if (pos < size) ++pos;
    }

    for (unsigned i = 1; i < count; ++i) {
        line_ref_t key = sort_lines[i];
        int j = (int)i - 1;
        while (j >= 0) {
            int cmp = line_compare(&sort_lines[j], &key, numeric);
            if (reverse ? (cmp < 0) : (cmp > 0)) {
                sort_lines[j + 1] = sort_lines[j];
                --j;
            } else {
                break;
            }
        }
        sort_lines[j + 1] = key;
    }

    for (unsigned i = 0; i < count; ++i) {
        for (unsigned j = 0; j < sort_lines[i].len; ++j) {
            vga_putc(sort_lines[i].ptr[j]);
        }
        vga_putc('\n');
    }
}

void unix_rev(int argc, char **argv) {
    if (argc != 2) {
        usage_error("rev", "FILE");
        return;
    }
    const char *data;
    unsigned size;
    if (get_file("rev", argv[1], &data, &size) != 0) return;
    unsigned pos = 0;
    while (pos < size) {
        unsigned start = pos;
        while (pos < size && data[pos] != '\n') ++pos;
        for (unsigned i = pos; i > start; --i) vga_putc(data[i - 1]);
        vga_putc('\n');
        if (pos < size) ++pos;
    }
}

static void find_walk(int index, const char *display, const char *pattern, int depth) {
    if (depth > RAMFS_MAX_DEPTH) return;
    if (!pattern || str_glob(pattern, ramfs_name(index))) outln(display);
    if (!ramfs_is_dir(index)) return;
    int entries[RAMFS_MAX_NODES];
    int count = collect_children(index, entries, RAMFS_MAX_NODES);
    sort_node_indices(entries, count);
    for (int i = 0; i < count; ++i) {
        char child[RAMFS_PATH_MAX];
        build_child_path(display, ramfs_name(entries[i]), child, sizeof(child));
        find_walk(entries[i], child, pattern, depth + 1);
    }
}

void unix_find(int argc, char **argv) {
    const char *path = 0;
    const char *pattern = 0;
    int i = 1;
    if (i < argc && str_cmp(argv[i], "-name") != 0) path = argv[i++];
    if (i < argc) {
        if (str_cmp(argv[i], "-name") != 0 || i + 1 >= argc) {
            usage_error("find", "[PATH] [-name PATTERN]");
            return;
        }
        pattern = argv[i + 1];
        i += 2;
    }
    if (i != argc) {
        usage_error("find", "[PATH] [-name PATTERN]");
        return;
    }
    if (!path) path = ".";
    int index;
    int rc = ramfs_lookup(path, &index);
    if (rc != RAMFS_OK) {
        fs_error("find", rc);
        return;
    }
    find_walk(index, path, pattern, 0);
}

void unix_stat(int argc, char **argv) {
    if (argc != 2) {
        usage_error("stat", "FILE");
        return;
    }
    int index;
    int rc = ramfs_lookup(argv[1], &index);
    if (rc != RAMFS_OK) {
        fs_error("stat", rc);
        return;
    }
    int is_dir = ramfs_is_dir(index);
    out("  File: ");
    outln(argv[1]);
    out("  Size: ");
    out_uint(ramfs_size(index));
    out("          Type: ");
    outln(is_dir ? "directory" : "regular file");
    out("  Inode: ");
    out_uint((unsigned)index);
    out("        Links: ");
    out_uint(is_dir ? (unsigned)(2 + ramfs_child_count(index)) : 1U);
    vga_putc('\n');
    out("Access: (");
    out(is_dir ? "0755/drwxr-xr-x" : "0644/-rw-r--r--");
    out(")  Uid: (1000/rio)  Gid: (1000/rio)\n");
}

static unsigned du_walk(int index, const char *display, int summary, int depth) {
    if (depth > RAMFS_MAX_DEPTH) return 0;
    if (ramfs_is_file(index)) return ramfs_size(index);
    unsigned total = 0;
    int entries[RAMFS_MAX_NODES];
    int count = collect_children(index, entries, RAMFS_MAX_NODES);
    sort_node_indices(entries, count);
    for (int i = 0; i < count; ++i) {
        char child[RAMFS_PATH_MAX];
        build_child_path(display, ramfs_name(entries[i]), child, sizeof(child));
        total += du_walk(entries[i], child, summary, depth + 1);
    }
    if (!summary) {
        out_uint(total);
        vga_putc('\t');
        outln(display);
    }
    return total;
}

void unix_du(int argc, char **argv) {
    int summary = 0;
    int first = 1;
    if (argc > 1 && str_cmp(argv[1], "-s") == 0) {
        summary = 1;
        first = 2;
    }
    if (argc - first > 1) {
        usage_error("du", "[-s] [PATH]");
        return;
    }
    const char *path = (first < argc) ? argv[first] : ".";
    int index;
    int rc = ramfs_lookup(path, &index);
    if (rc != RAMFS_OK) {
        fs_error("du", rc);
        return;
    }
    if (ramfs_is_file(index)) {
        out_uint(ramfs_size(index));
        vga_putc('\t');
        outln(path);
        return;
    }
    unsigned total = du_walk(index, path, summary, 0);
    if (summary) {
        out_uint(total);
        vga_putc('\t');
        outln(path);
    }
}

void unix_basename(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        usage_error("basename", "PATH [SUFFIX]");
        return;
    }
    char buf[RAMFS_PATH_MAX];
    str_copy(buf, argv[1], sizeof(buf));
    size_t len = str_len(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = 0;
    if (len == 0) {
        outln("/");
        return;
    }
    const char *slash = str_rchr(buf, '/');
    const char *base = slash ? slash + 1 : buf;
    char result[RAMFS_PATH_MAX];
    if (!*base) {
        outln("/");
        return;
    }
    str_copy(result, base, sizeof(result));
    if (argc == 3) {
        size_t rlen = str_len(result);
        size_t slen = str_len(argv[2]);
        if (slen > 0 && slen <= rlen &&
            str_cmp(result + rlen - slen, argv[2]) == 0) {
            result[rlen - slen] = 0;
        }
    }
    outln(result);
}

void unix_dirname(int argc, char **argv) {
    if (argc != 2) {
        usage_error("dirname", "PATH");
        return;
    }
    char buf[RAMFS_PATH_MAX];
    str_copy(buf, argv[1], sizeof(buf));
    size_t len = str_len(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = 0;
    char *slash = str_rchr(buf, '/');
    if (!slash) {
        outln(".");
        return;
    }
    if (slash == buf) {
        outln("/");
        return;
    }
    *slash = 0;
    outln(buf);
}

void unix_hexdump(int argc, char **argv) {
    if (argc != 2) {
        usage_error("hexdump", "FILE");
        return;
    }
    const char *data;
    unsigned size;
    if (get_file("hexdump", argv[1], &data, &size) != 0) return;
    for (unsigned offset = 0; offset < size; offset += 16) {
        unsigned chunk = size - offset;
        if (chunk > 16) chunk = 16;
        out_hex_padded(offset, 8, 0);
        out("  ");
        for (unsigned i = 0; i < 16; ++i) {
            if (i < chunk) {
                out_hex_padded((unsigned char)data[offset + i], 2, 0);
                vga_putc(' ');
            } else {
                out("   ");
            }
            if (i == 7) vga_putc(' ');
        }
        out(" |");
        for (unsigned i = 0; i < chunk; ++i) {
            unsigned char c = (unsigned char)data[offset + i];
            vga_putc((c >= 32 && c < 127) ? (char)c : '.');
        }
        out("|\n");
    }
}

static void tree_walk(int index, const char *prefix, int depth,
                      unsigned *dir_count, unsigned *file_count) {
    int entries[RAMFS_MAX_NODES];
    int count = collect_children(index, entries, RAMFS_MAX_NODES);
    sort_node_indices(entries, count);
    for (int i = 0; i < count; ++i) {
        int last = (i == count - 1);
        out(prefix);
        out(last ? "`-- " : "|-- ");
        if (ramfs_is_dir(entries[i])) vga_set_color(11, 0);
        outln(ramfs_name(entries[i]));
        vga_set_color(7, 0);
        if (ramfs_is_dir(entries[i])) {
            ++*dir_count;
            if (depth < 12) {
                char next[128];
                str_copy(next, prefix, sizeof(next));
                str_cat(next, last ? "    " : "|   ", sizeof(next));
                tree_walk(entries[i], next, depth + 1, dir_count, file_count);
            }
        } else {
            ++*file_count;
        }
    }
}

void unix_tree(int argc, char **argv) {
    if (argc > 2) {
        usage_error("tree", "[PATH]");
        return;
    }
    const char *path = (argc == 2) ? argv[1] : ".";
    int index;
    int rc = ramfs_lookup(path, &index);
    if (rc != RAMFS_OK) {
        fs_error("tree", rc);
        return;
    }
    outln(path);
    unsigned dir_count = 0;
    unsigned file_count = 0;
    if (ramfs_is_file(index)) {
        ++file_count;
    } else {
        tree_walk(index, "", 0, &dir_count, &file_count);
    }
    vga_putc('\n');
    out_uint(dir_count);
    out(dir_count == 1 ? " directory, " : " directories, ");
    out_uint(file_count);
    out(file_count == 1 ? " file\n" : " files\n");
}

void unix_more(int argc, char **argv) {
    if (argc != 2) {
        usage_error("more", "FILE");
        return;
    }
    const char *data;
    unsigned size;
    if (get_file("more", argv[1], &data, &size) != 0) return;
    unsigned pos = 0;
    unsigned lines = 0;
    while (pos < size) {
        unsigned start = pos;
        while (pos < size && data[pos] != '\n') ++pos;
        for (unsigned i = start; i < pos; ++i) vga_putc(data[i]);
        if (pos < size) ++pos;
        vga_putc('\n');
        ++lines;
        if (lines % 22 == 0 && pos < size) {
            out("--More--");
            char key = keyboard_getchar();
            for (int i = 0; i < 8; ++i) vga_putc('\b');
            if (key == 'q' || key == 'Q') return;
        }
    }
}

static void date_format(const rtc_time_t *t, const char *format) {
    for (const char *p = format; *p; ++p) {
        if (*p != '%') {
            vga_putc(*p);
            continue;
        }
        ++p;
        if (!*p) {
            vga_putc('%');
            break;
        }
        switch (*p) {
            case 'Y': out_padded_uint((unsigned)t->year, 4); break;
            case 'y': out_2(t->year % 100); break;
            case 'm': out_2(t->month); break;
            case 'd': out_2(t->day); break;
            case 'H': out_2(t->hour); break;
            case 'M': out_2(t->minute); break;
            case 'S': out_2(t->second); break;
            case 'A': out(time_day_name(t->weekday)); break;
            case 'a': {
                const char *name = time_day_name(t->weekday);
                for (int i = 0; i < 3 && name[i]; ++i) vga_putc(name[i]);
                break;
            }
            case 'B': out(time_month_name(t->month)); break;
            case 'b': {
                const char *name = time_month_name(t->month);
                for (int i = 0; i < 3 && name[i]; ++i) vga_putc(name[i]);
                break;
            }
            case 'F':
                out_padded_uint((unsigned)t->year, 4);
                vga_putc('-');
                out_2(t->month);
                vga_putc('-');
                out_2(t->day);
                break;
            case 'T':
                out_2(t->hour);
                vga_putc(':');
                out_2(t->minute);
                vga_putc(':');
                out_2(t->second);
                break;
            case 's': {
                long long epoch = time_epoch();
                if (epoch < 0) out_uint(0);
                else out_uint((unsigned)epoch);
                break;
            }
            case 'n': vga_putc('\n'); break;
            case 't': vga_putc('\t'); break;
            case '%': vga_putc('%'); break;
            default:
                vga_putc('%');
                vga_putc(*p);
                break;
        }
    }
}

void unix_date(int argc, char **argv) {
    rtc_time_t now;
    if (time_read_rtc(&now) != 0) {
        cmd_error("date", "cannot read the real-time clock");
        return;
    }
    if (argc == 1) {
        date_format(&now, "%Y-%m-%d %H:%M:%S");
        vga_putc('\n');
        return;
    }
    if (argc == 2 && argv[1][0] == '+') {
        date_format(&now, argv[1] + 1);
        vga_putc('\n');
        return;
    }
    usage_error("date", "[+FORMAT]");
}

void unix_uname(int argc, char **argv) {
    int show_s = 0;
    int show_n = 0;
    int show_r = 0;
    int show_m = 0;
    if (argc == 1) show_s = 1;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (arg[0] != '-' || !arg[1]) {
            usage_error("uname", "[-a] [-s] [-n] [-r] [-m]");
            return;
        }
        for (const char *p = arg + 1; *p; ++p) {
            if (*p == 'a') show_s = show_n = show_r = show_m = 1;
            else if (*p == 's') show_s = 1;
            else if (*p == 'n') show_n = 1;
            else if (*p == 'r') show_r = 1;
            else if (*p == 'm') show_m = 1;
            else {
                cmd_error("uname", "invalid option");
                return;
            }
        }
    }
    if (!show_s && !show_n && !show_r && !show_m) show_s = 1;
    int printed = 0;
    if (show_s) {
        out(OS_NAME);
        printed = 1;
    }
    if (show_n) {
        if (printed) vga_putc(' ');
        const char *host = env_get("HOSTNAME");
        out(host ? host : HOST_FALLBACK);
        printed = 1;
    }
    if (show_r) {
        if (printed) vga_putc(' ');
        out(OS_VERSION);
        printed = 1;
    }
    if (show_m) {
        if (printed) vga_putc(' ');
        out(MACHINE);
    }
    vga_putc('\n');
}

void unix_free(int argc, char **argv) {
    (void)argc;
    (void)argv;
    unsigned used_bytes = 0;
    ramfs_usage(0, &used_bytes);
    unsigned used_kb = (used_bytes + 1023U) / 1024U;
    unsigned total_kb = 128U * 1024U;
    unsigned free_kb = total_kb > used_kb ? total_kb - used_kb : 0;
    out("              total        used        free\n");
    out("Mem: ");
    out_padded_uint(total_kb, 12);
    out_padded_uint(used_kb, 12);
    out_padded_uint(free_kb, 12);
    vga_putc('\n');
    out("Swap:");
    out_padded_uint(0, 12);
    out_padded_uint(0, 12);
    out_padded_uint(0, 12);
    vga_putc('\n');
}

void unix_df(int argc, char **argv) {
    (void)argc;
    (void)argv;
    unsigned used_bytes = 0;
    ramfs_usage(0, &used_bytes);
    unsigned total_kb = RAMFS_DATA_SIZE / 1024U;
    unsigned used_kb = (used_bytes + 1023U) / 1024U;
    unsigned available = total_kb > used_kb ? total_kb - used_kb : 0;
    unsigned percent = total_kb ? (used_kb * 100U) / total_kb : 0;
    out("Filesystem     1K-blocks  Used Available Use% Mounted on\n");
    out("ramfs");
    out_padded_uint(total_kb, 15);
    out_padded_uint(used_kb, 6);
    out_padded_uint(available, 10);
    out_padded_uint(percent, 5);
    out("% /\n");
}

void unix_uptime(int argc, char **argv) {
    (void)argc;
    (void)argv;
    rtc_time_t now;
    if (time_read_rtc(&now) == 0) {
        out_2(now.hour);
        vga_putc(':');
        out_2(now.minute);
        vga_putc(':');
        out_2(now.second);
    } else {
        out("--:--:--");
    }
    unsigned long long uptime = time_uptime_seconds();
    unsigned long long days = uptime / 86400ULL;
    uptime %= 86400ULL;
    unsigned hours = (unsigned)(uptime / 3600ULL);
    uptime %= 3600ULL;
    unsigned minutes = (unsigned)(uptime / 60ULL);
    unsigned seconds = (unsigned)(uptime % 60ULL);
    out(" up ");
    out_uint((unsigned)days);
    out(" days, ");
    out_2((int)hours);
    vga_putc(':');
    out_2((int)minutes);
    vga_putc(':');
    out_2((int)seconds);
    out(",  1 user,  load average: 0.00, 0.00, 0.00\n");
}

void unix_hostname(int argc, char **argv) {
    if (argc > 2) {
        usage_error("hostname", "[NAME]");
        return;
    }
    if (argc == 2) {
        size_t len = str_len(argv[1]);
        if (len == 0 || len >= RAMFS_NAME_MAX) {
            cmd_error("hostname", "invalid hostname");
            return;
        }
        char buffer[RAMFS_NAME_MAX + 1];
        str_copy(buffer, argv[1], sizeof(buffer));
        str_cat(buffer, "\n", sizeof(buffer));
        int rc = ramfs_write("/etc/hostname", buffer, (unsigned)str_len(buffer));
        if (rc != RAMFS_OK) {
            fs_error("hostname", rc);
            return;
        }
        env_set("HOSTNAME", argv[1]);
        return;
    }
    int index;
    int rc = ramfs_lookup("/etc/hostname", &index);
    if (rc == RAMFS_OK && ramfs_is_file(index)) {
        const char *data = ramfs_data(index);
        unsigned size = ramfs_size(index);
        while (size > 0 && (data[size - 1] == '\n' || data[size - 1] == '\r')) --size;
        for (unsigned i = 0; i < size; ++i) vga_putc(data[i]);
        vga_putc('\n');
        return;
    }
    const char *host = env_get("HOSTNAME");
    outln(host ? host : HOST_FALLBACK);
}

void unix_whoami(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *user = env_get("USER");
    outln(user ? user : "rio");
}

void unix_id(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *user = env_get("USER");
    if (!user) user = "rio";
    out("uid=1000(");
    out(user);
    out(") gid=1000(");
    out(user);
    out(") groups=1000(");
    out(user);
    out(")\n");
}

void unix_tty(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("/dev/tty0");
}

void unix_seq(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        usage_error("seq", "[FIRST [STEP]] LAST");
        return;
    }
    int values[3];
    int count = argc - 1;
    for (int i = 0; i < count; ++i) {
        if (str_to_int(argv[i + 1], &values[i]) != 0) {
            cmd_error("seq", "invalid number");
            return;
        }
    }
    long long first = 1;
    long long step = 1;
    long long last;
    if (count == 1) {
        last = values[0];
    } else if (count == 2) {
        first = values[0];
        last = values[1];
    } else {
        first = values[0];
        step = values[1];
        last = values[2];
    }
    if (step == 0) {
        cmd_error("seq", "step cannot be zero");
        return;
    }
    unsigned printed = 0;
    if (step > 0) {
        for (long long v = first; v <= last; v += step) {
            out_int((int)v);
            vga_putc('\n');
            if (++printed >= 100000U) break;
        }
    } else {
        for (long long v = first; v >= last; v += step) {
            out_int((int)v);
            vga_putc('\n');
            if (++printed >= 100000U) break;
        }
    }
}

void unix_sleep(int argc, char **argv) {
    if (argc != 2) {
        usage_error("sleep", "SECONDS");
        return;
    }
    unsigned seconds;
    if (str_to_uint(argv[1], &seconds) != 0) {
        cmd_error("sleep", "invalid number");
        return;
    }
    if (seconds > 86400U) {
        cmd_error("sleep", "maximum is 86400 seconds");
        return;
    }
    time_sleep_seconds(seconds);
}

void unix_yes(int argc, char **argv) {
    char text[128];
    text[0] = 0;
    if (argc == 1) {
        str_copy(text, "y", sizeof(text));
    } else {
        for (int i = 1; i < argc; ++i) {
            if (i > 1) str_cat(text, " ", sizeof(text));
            str_cat(text, argv[i], sizeof(text));
        }
    }
    keyboard_flush();
    while (!keyboard_pending()) {
        outln(text);
    }
    keyboard_flush();
}

void unix_true(int argc, char **argv) {
    (void)argc;
    (void)argv;
}

void unix_false(int argc, char **argv) {
    (void)argc;
    (void)argv;
}

void unix_printf(int argc, char **argv) {
    if (argc < 2) {
        usage_error("printf", "FORMAT [ARG...]");
        return;
    }
    const char *format = argv[1];
    int arg = 2;
    for (const char *p = format; *p; ++p) {
        if (*p == '\\') {
            ++p;
            if (!*p) {
                vga_putc('\\');
                break;
            }
            switch (*p) {
                case 'n': vga_putc('\n'); break;
                case 't': vga_putc('\t'); break;
                case 'r': vga_putc('\r'); break;
                case 'b': vga_putc('\b'); break;
                case '\\': vga_putc('\\'); break;
                default:
                    vga_putc('\\');
                    vga_putc(*p);
                    break;
            }
        } else if (*p == '%') {
            ++p;
            if (!*p) {
                vga_putc('%');
                break;
            }
            switch (*p) {
                case 's':
                    if (arg < argc) out(argv[arg]);
                    ++arg;
                    break;
                case 'd':
                case 'i': {
                    int value = 0;
                    if (arg < argc) str_to_int(argv[arg], &value);
                    ++arg;
                    out_int(value);
                    break;
                }
                case 'u': {
                    unsigned value = 0;
                    if (arg < argc) str_to_uint(argv[arg], &value);
                    ++arg;
                    out_uint(value);
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned value = 0;
                    if (arg < argc) str_to_uint(argv[arg], &value);
                    ++arg;
                    out_hex(value, *p == 'X');
                    break;
                }
                case 'c':
                    if (arg < argc && argv[arg][0]) vga_putc(argv[arg][0]);
                    ++arg;
                    break;
                case '%':
                    vga_putc('%');
                    break;
                default:
                    vga_putc('%');
                    vga_putc(*p);
                    break;
            }
        } else {
            vga_putc(*p);
        }
    }
}

void unix_expr(int argc, char **argv) {
    if (argc != 4) {
        usage_error("expr", "A OP B");
        return;
    }
    int a;
    int b;
    if (str_to_int(argv[1], &a) != 0 || str_to_int(argv[3], &b) != 0) {
        cmd_error("expr", "non-integer argument");
        return;
    }
    const char *op = argv[2];
    int result;
    if (str_cmp(op, "+") == 0) {
        result = a + b;
    } else if (str_cmp(op, "-") == 0) {
        result = a - b;
    } else if (str_cmp(op, "*") == 0) {
        result = a * b;
    } else if (str_cmp(op, "/") == 0) {
        if (b == 0) {
            cmd_error("expr", "division by zero");
            return;
        }
        result = a / b;
    } else if (str_cmp(op, "%") == 0) {
        if (b == 0) {
            cmd_error("expr", "division by zero");
            return;
        }
        result = a % b;
    } else {
        cmd_error("expr", "unknown operator");
        return;
    }
    out_int(result);
    vga_putc('\n');
}

void unix_factor(int argc, char **argv) {
    if (argc != 2) {
        usage_error("factor", "NUMBER");
        return;
    }
    unsigned value;
    if (str_to_uint(argv[1], &value) != 0) {
        cmd_error("factor", "invalid number");
        return;
    }
    if (value == 0) {
        cmd_error("factor", "zero is not supported");
        return;
    }
    out_uint(value);
    vga_putc(':');
    if (value == 1) {
        vga_putc('\n');
        return;
    }
    unsigned remaining = value;
    while ((remaining & 1U) == 0) {
        out(" 2");
        remaining >>= 1;
    }
    for (unsigned divisor = 3; divisor <= remaining / divisor; divisor += 2) {
        while (remaining % divisor == 0) {
            vga_putc(' ');
            out_uint(divisor);
            remaining /= divisor;
        }
    }
    if (remaining > 1) {
        vga_putc(' ');
        out_uint(remaining);
    }
    vga_putc('\n');
}

void unix_cal(int argc, char **argv) {
    rtc_time_t now;
    if (time_read_rtc(&now) != 0) {
        cmd_error("cal", "cannot read the real-time clock");
        return;
    }
    int month = now.month;
    int year = now.year;
    if (argc == 2) {
        if (str_to_int(argv[1], &month) != 0) {
            cmd_error("cal", "invalid month");
            return;
        }
    } else if (argc == 3) {
        if (str_to_int(argv[1], &month) != 0 || str_to_int(argv[2], &year) != 0) {
            cmd_error("cal", "invalid month or year");
            return;
        }
    } else if (argc > 3) {
        usage_error("cal", "[MONTH] [YEAR]");
        return;
    }
    if (month < 1 || month > 12 || year < 1 || year > 9999) {
        cmd_error("cal", "month or year out of range");
        return;
    }
    int days = time_days_in_month(month, year);
    char header[32];
    str_copy(header, time_month_name(month), sizeof(header));
    str_cat(header, " ", sizeof(header));
    char year_buf[8];
    int_to_str(year, year_buf, sizeof(year_buf));
    str_cat(header, year_buf, sizeof(header));
    unsigned header_len = (unsigned)str_len(header);
    unsigned padding = header_len < 20 ? (20 - header_len) / 2 : 0;
    for (unsigned i = 0; i < padding; ++i) vga_putc(' ');
    outln(header);
    outln("Su Mo Tu We Th Fr Sa");
    unsigned weekday = (unsigned)time_weekday(year, month, 1);
    for (unsigned i = 0; i < weekday * 3; ++i) vga_putc(' ');
    for (int day = 1; day <= days; ++day) {
        out_2(day);
        vga_putc(' ');
        ++weekday;
        if (weekday == 7) {
            vga_putc('\n');
            weekday = 0;
        }
    }
    if (weekday != 0) vga_putc('\n');
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                       (offset & 0xFCU);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static const char *pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass storage controller";
        case 0x02: return "Network controller";
        case 0x03: return "Display controller";
        case 0x04: return "Multimedia controller";
        case 0x05: return "Memory controller";
        case 0x06: return "Bridge";
        case 0x07: return "Communication controller";
        case 0x08: return "System peripheral";
        case 0x09: return "Input device controller";
        case 0x0A: return "Docking station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial bus controller";
        case 0x0D: return "Wireless controller";
        case 0x0E: return "Intelligent controller";
        case 0x0F: return "Satellite controller";
        case 0x10: return "Encryption controller";
        case 0x11: return "Signal processing controller";
        case 0x12: return "Processing accelerator";
        case 0x13: return "Non-essential instrumentation";
        default: return "Unknown";
    }
}

void unix_lspci(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int found = 0;
    for (unsigned bus = 0; bus < 256; ++bus) {
        for (unsigned slot = 0; slot < 32; ++slot) {
            uint32_t id = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0);
            if ((id & 0xFFFFU) == 0xFFFFU) continue;
            uint32_t header = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
            int functions = (header & 0x00800000U) ? 8 : 1;
            for (int function = 0; function < functions; ++function) {
                uint32_t device_id = (function == 0) ? id :
                    pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0);
                if ((device_id & 0xFFFFU) == 0xFFFFU) continue;
                uint32_t class_register = pci_read32((uint8_t)bus, (uint8_t)slot,
                                                     (uint8_t)function, 0x08);
                uint8_t class_code = (uint8_t)((class_register >> 24) & 0xFFU);
                out_hex_padded(bus, 2, 0);
                vga_putc(':');
                out_hex_padded(slot, 2, 0);
                vga_putc('.');
                out_uint((unsigned)function);
                out(" Class ");
                out_hex_padded(class_code, 2, 0);
                out(": ");
                out_hex_padded(device_id & 0xFFFFU, 4, 0);
                vga_putc(':');
                out_hex_padded(device_id >> 16, 4, 0);
                out(" (");
                out(pci_class_name(class_code));
                out(")\n");
                ++found;
            }
        }
    }
    if (!found) outln("lspci: no PCI devices found");
}

void unix_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    out("  PID TTY          TIME CMD\n");
    out("    1 tty0     00:00:00 rioinit\n");
    out("    2 tty0     00:00:00 riosh\n");
}

void unix_mount(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("/dev/ram0 on / type ramfs (rw,size=32k)");
}

void unix_env(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int slots = env_slot_count();
    for (int i = 0; i < slots; ++i) {
        const char *name = env_name_at(i);
        if (!name) continue;
        out(name);
        vga_putc('=');
        outln(env_value_at(i));
    }
}

void unix_printenv(int argc, char **argv) {
    if (argc == 1) {
        int slots = env_slot_count();
        for (int i = 0; i < slots; ++i) {
            const char *name = env_name_at(i);
            if (!name) continue;
            out(name);
            vga_putc('=');
            outln(env_value_at(i));
        }
        return;
    }
    for (int i = 1; i < argc; ++i) {
        const char *value = env_get(argv[i]);
        if (value) outln(value);
    }
}

void unix_export(int argc, char **argv) {
    if (argc == 1) {
        int slots = env_slot_count();
        for (int i = 0; i < slots; ++i) {
            const char *name = env_name_at(i);
            if (!name) continue;
            out(name);
            vga_putc('=');
            outln(env_value_at(i));
        }
        return;
    }
    for (int i = 1; i < argc; ++i) {
        char buffer[160];
        str_copy(buffer, argv[i], sizeof(buffer));
        char *equals = str_chr(buffer, '=');
        if (equals) {
            *equals = 0;
            if (!*buffer || env_set(buffer, equals + 1) != 0) {
                cmd_error("export", "cannot set variable");
            }
        } else {
            if (!env_get(buffer)) env_set(buffer, "");
        }
    }
}

void unix_unset(int argc, char **argv) {
    if (argc < 2) {
        usage_error("unset", "NAME...");
        return;
    }
    for (int i = 1; i < argc; ++i) {
        env_unset(argv[i]);
    }
}

void unix_echo(int argc, char **argv) {
    int newline = 1;
    int escapes = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        int valid = 1;
        for (const char *p = argv[i] + 1; *p; ++p) {
            if (*p == 'n') newline = 0;
            else if (*p == 'e') escapes = 1;
            else {
                valid = 0;
                break;
            }
        }
        if (!valid) break;
        ++i;
    }
    int start = i;
    for (; i < argc; ++i) {
        if (i > start) vga_putc(' ');
        const char *s = argv[i];
        while (*s) {
            if (escapes && *s == '\\' && s[1]) {
                ++s;
                switch (*s) {
                    case 'n': vga_putc('\n'); break;
                    case 't': vga_putc('\t'); break;
                    case 'r': vga_putc('\r'); break;
                    case 'b': vga_putc('\b'); break;
                    case '\\': vga_putc('\\'); break;
                    default:
                        vga_putc('\\');
                        vga_putc(*s);
                        break;
                }
                ++s;
            } else {
                vga_putc(*s++);
            }
        }
    }
    if (newline) vga_putc('\n');
}
