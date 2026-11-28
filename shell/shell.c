#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "lib.h"
#include "env.h"
#include "ramfs.h"
#include "time.h"
#include "unix.h"
#include <stdint.h>

#define INPUT_SIZE 128
#define HISTORY_SIZE 16
#define ARG_MAX 32
#define LINE_MAX 512
#define ALIAS_MAX 16
#define ALIAS_NAME_MAX 24
#define ALIAS_VALUE_MAX 96

typedef struct {
    const char *name;
    const char *usage;
    const char *summary;
    void (*run)(int argc, char **argv);
} command_t;

static void cmd_about(int argc, char **argv);
static void cmd_alias(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_color(int argc, char **argv);
static void cmd_cpuinfo(int argc, char **argv);
static void cmd_exit(int argc, char **argv);
static void cmd_halt(int argc, char **argv);
static void cmd_help(int argc, char **argv);
static void cmd_history(int argc, char **argv);
static void cmd_man(int argc, char **argv);
static void cmd_mem(int argc, char **argv);
static void cmd_neofetch(int argc, char **argv);
static void cmd_poweroff(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_type(int argc, char **argv);
static void cmd_unalias(int argc, char **argv);
static void cmd_version(int argc, char **argv);
static void cmd_which(int argc, char **argv);

static const command_t command_table[] = {
    {"about",    "about",                          "About RioOS",                              cmd_about},
    {"alias",    "alias [NAME[=VALUE]...]",        "Create or list shell aliases",             cmd_alias},
    {"basename", "basename PATH [SUFFIX]",         "Strip directory and suffix from a path",   unix_basename},
    {"cal",      "cal [MONTH] [YEAR]",             "Display a calendar",                       unix_cal},
    {"cat",      "cat [-n] FILE...",               "Concatenate files to the console",         unix_cat},
    {"cd",       "cd [PATH]",                      "Change the working directory",             unix_cd},
    {"clear",    "clear",                          "Clear the screen",                         cmd_clear},
    {"cls",      "cls",                            "Alias for clear",                          cmd_clear},
    {"color",    "color FG [BG]",                  "Change terminal colors",                   cmd_color},
    {"cp",       "cp [-r] SRC DST",                "Copy files or directories",                unix_cp},
    {"cpuinfo",  "cpuinfo",                        "Show CPU information using CPUID",         cmd_cpuinfo},
    {"date",     "date [+FORMAT]",                 "Show or format the RTC date and time",     unix_date},
    {"df",       "df",                             "Show ramfs disk usage",                    unix_df},
    {"dirname",  "dirname PATH",                   "Strip the last path component",            unix_dirname},
    {"du",       "du [-s] [PATH]",                 "Estimate file space usage",                unix_du},
    {"echo",     "echo [-n] [-e] [TEXT...]",       "Print arguments",                          unix_echo},
    {"env",      "env",                            "List environment variables",               unix_env},
    {"exit",     "exit",                           "Exit the shell and halt the CPU",          cmd_exit},
    {"expr",     "expr A OP B",                    "Evaluate an integer expression",           unix_expr},
    {"factor",   "factor NUMBER",                  "Print prime factors",                      unix_factor},
    {"false",    "false",                          "Return an unsuccessful status",            unix_false},
    {"find",     "find [PATH] [-name PATTERN]",    "Search for files recursively",             unix_find},
    {"free",     "free",                           "Show memory usage",                        unix_free},
    {"grep",     "grep [-i] [-v] [-n] PAT FILE...","Search text inside files",                 unix_grep},
    {"gpuclr",   "gpuclr [RRGGBB]",                "Clear the virtio-gpu framebuffer",         unix_gpuclr},
    {"gpudraw",  "gpudraw X Y W H RRGGBB",         "Draw a rectangle on the virtio-gpu",       unix_gpudraw},
    {"gpuinfo",  "gpuinfo",                        "Show virtio-gpu device information",       unix_gpuinfo},
    {"gputest",  "gputest",                        "Render a test pattern on the virtio-gpu",  unix_gputest},
    {"halt",     "halt",                           "Halt the virtual CPU",                     cmd_halt},
    {"head",     "head [-n N] FILE...",            "Show the first lines of files",            unix_head},
    {"help",     "help [COMMAND]",                 "Show available commands",                  cmd_help},
    {"hexdump",  "hexdump FILE",                   "Dump a file in hexadecimal",               unix_hexdump},
    {"history",  "history [-c]",                   "Show or clear command history",            cmd_history},
    {"hostname", "hostname [NAME]",                "Show or set the hostname",                 unix_hostname},
    {"id",       "id",                             "Show user and group identity",             unix_id},
    {"ls",       "ls [-l] [-a] [PATH]",            "List directory contents",                  unix_ls},
    {"lspci",    "lspci",                          "List PCI devices",                         unix_lspci},
    {"man",      "man COMMAND",                    "Show command usage details",               cmd_man},
    {"mem",      "mem",                            "Show memory and ramfs information",        cmd_mem},
    {"mkdir",    "mkdir [-p] PATH...",             "Create directories",                       unix_mkdir},
    {"more",     "more FILE",                      "Page through a file",                      unix_more},
    {"mount",    "mount",                          "Show mounted filesystems",                 unix_mount},
    {"mv",       "mv SRC DST",                     "Move or rename files",                     unix_mv},
    {"neofetch", "neofetch",                       "Show a compact system summary",            cmd_neofetch},
    {"poweroff", "poweroff",                       "Shut down the virtual machine",            cmd_poweroff},
    {"printenv", "printenv [NAME...]",             "Print environment variables",              unix_printenv},
    {"printf",   "printf FORMAT [ARG...]",         "Format and print data",                    unix_printf},
    {"ps",       "ps",                             "Show running tasks",                       unix_ps},
    {"pwd",      "pwd",                            "Print the working directory",              unix_pwd},
    {"reboot",   "reboot",                         "Restart the virtual machine",              cmd_reboot},
    {"rev",      "rev FILE",                       "Reverse each line of a file",              unix_rev},
    {"rm",       "rm [-r] [-f] PATH...",           "Remove files or directories",              unix_rm},
    {"rmdir",    "rmdir PATH...",                  "Remove empty directories",                 unix_rmdir},
    {"seq",      "seq [FIRST [STEP]] LAST",        "Print a sequence of numbers",              unix_seq},
    {"sleep",    "sleep SECONDS",                  "Delay for a number of seconds",            unix_sleep},
    {"sort",     "sort [-r] [-n] FILE",            "Sort lines of a file",                     unix_sort},
    {"stat",     "stat FILE",                      "Show file status",                         unix_stat},
    {"tail",     "tail [-n N] FILE...",            "Show the last lines of files",             unix_tail},
    {"touch",    "touch PATH...",                  "Create empty files",                       unix_touch},
    {"tree",     "tree [PATH]",                    "Display a directory tree",                 unix_tree},
    {"true",     "true",                           "Return a successful status",               unix_true},
    {"tty",      "tty",                            "Print the terminal name",                  unix_tty},
    {"type",     "type NAME...",                   "Describe a command",                       cmd_type},
    {"unalias",  "unalias NAME... | -a",           "Remove shell aliases",                     cmd_unalias},
    {"uname",    "uname [-a] [-s] [-n] [-r] [-m]","Show system information",                  unix_uname},
    {"unset",    "unset NAME...",                  "Remove environment variables",             unix_unset},
    {"uptime",   "uptime",                         "Show how long the system has been up",     unix_uptime},
    {"version",  "version",                        "Show the RioOS version",                   cmd_version},
    {"wc",       "wc [-l] [-w] [-c] FILE...",      "Count lines, words, and bytes",            unix_wc},
    {"which",    "which NAME...",                  "Locate a command",                         cmd_which},
    {"whoami",   "whoami",                         "Print the current user name",              unix_whoami},
    {"yes",      "yes [TEXT...]",                  "Print text until a key is pressed",        unix_yes}
};

#define COMMAND_COUNT ((int)(sizeof(command_table) / sizeof(command_table[0])))

static char history[HISTORY_SIZE][INPUT_SIZE];
static unsigned history_count;
static unsigned history_next;

typedef struct {
    char name[ALIAS_NAME_MAX];
    char value[ALIAS_VALUE_MAX];
    int used;
} alias_entry_t;

static alias_entry_t aliases[ALIAS_MAX];

static void out(const char *s) { vga_puts(s); }
static void outln(const char *s) { vga_puts(s); vga_putc('\n'); }

static void out_uint(unsigned value) {
    char buf[16];
    uint_to_str(value, buf, sizeof(buf));
    vga_puts(buf);
}

static void out_left(const char *s, unsigned width) {
    unsigned len = (unsigned)str_len(s);
    vga_puts(s);
    while (len < width) {
        vga_putc(' ');
        ++len;
    }
}

static void command_error(const char *cmd, const char *message) {
    vga_puts(cmd);
    vga_puts(": ");
    vga_puts(message);
    vga_putc('\n');
}

static const command_t *find_command(const char *name) {
    for (int i = 0; i < COMMAND_COUNT; ++i) {
        if (str_cmp(name, command_table[i].name) == 0) return &command_table[i];
    }
    return 0;
}

static int valid_alias_name(const char *name) {
    if (!name || !*name) return 0;
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) return 0;
    for (const char *p = name + 1; *p; ++p) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) return 0;
    }
    return 1;
}

static const char *alias_get(const char *name) {
    for (int i = 0; i < ALIAS_MAX; ++i) {
        if (aliases[i].used && str_cmp(aliases[i].name, name) == 0) {
            return aliases[i].value;
        }
    }
    return 0;
}

static int alias_set(const char *name, const char *value) {
    if (!valid_alias_name(name) || str_len(value) >= ALIAS_VALUE_MAX) return -1;
    int free_slot = -1;
    for (int i = 0; i < ALIAS_MAX; ++i) {
        if (aliases[i].used && str_cmp(aliases[i].name, name) == 0) {
            str_copy(aliases[i].value, value, ALIAS_VALUE_MAX);
            return 0;
        }
        if (!aliases[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return -1;
    aliases[free_slot].used = 1;
    str_copy(aliases[free_slot].name, name, ALIAS_NAME_MAX);
    str_copy(aliases[free_slot].value, value, ALIAS_VALUE_MAX);
    return 0;
}

static int alias_remove(const char *name) {
    for (int i = 0; i < ALIAS_MAX; ++i) {
        if (aliases[i].used && str_cmp(aliases[i].name, name) == 0) {
            aliases[i].used = 0;
            aliases[i].name[0] = 0;
            aliases[i].value[0] = 0;
            return 0;
        }
    }
    return -1;
}

static void alias_clear(void) {
    mem_set(aliases, 0, sizeof(aliases));
}

static void alias_init(void) {
    alias_clear();
    alias_set("ll", "ls -l");
}

static void history_add(const char *line) {
    if (!line[0]) return;
    if (history_count &&
        str_cmp(history[(history_next + HISTORY_SIZE - 1) % HISTORY_SIZE], line) == 0) {
        return;
    }
    str_copy(history[history_next], line, INPUT_SIZE);
    history_next = (history_next + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) ++history_count;
}

static void cmd_help(int argc, char **argv) {
    if (argc > 1) {
        const command_t *cmd = find_command(argv[1]);
        if (!cmd) {
            command_error("help", "no such command");
            return;
        }
        out("Usage: ");
        outln(cmd->usage);
        outln(cmd->summary);
        return;
    }
    out("RioOS shell commands (");
    out_uint((unsigned)COMMAND_COUNT);
    outln("):");
    for (int i = 0; i < COMMAND_COUNT; ++i) {
        out("  ");
        out_left(command_table[i].name, 12);
        outln(command_table[i].summary);
    }
    outln("");
    outln("Use 'man COMMAND' for usage details.");
}

static void cmd_man(int argc, char **argv) {
    if (argc != 2) {
        outln("Usage: man COMMAND");
        return;
    }
    const command_t *cmd = find_command(argv[1]);
    if (!cmd) {
        command_error("man", "no such command");
        return;
    }
    outln("NAME");
    out("    ");
    out(cmd->name);
    out(" - ");
    outln(cmd->summary);
    outln("SYNOPSIS");
    out("    ");
    outln(cmd->usage);
    outln("DESCRIPTION");
    out("    ");
    out(cmd->summary);
    outln("");
}

static void cmd_which(int argc, char **argv) {
    if (argc < 2) {
        outln("Usage: which NAME...");
        return;
    }
    for (int i = 1; i < argc; ++i) {
        if (find_command(argv[i])) {
            out("/bin/");
            outln(argv[i]);
        } else {
            out("which: no ");
            out(argv[i]);
            outln(" in (/bin:/usr/bin)");
        }
    }
}

static void cmd_type(int argc, char **argv) {
    if (argc < 2) {
        outln("Usage: type NAME...");
        return;
    }
    for (int i = 1; i < argc; ++i) {
        const char *value = alias_get(argv[i]);
        if (value) {
            out(argv[i]);
            out(" is aliased to '");
            out(value);
            outln("'");
        } else if (find_command(argv[i])) {
            out(argv[i]);
            outln(" is a shell builtin");
        } else {
            out("type: ");
            out(argv[i]);
            outln(": not found");
        }
    }
}

static void cmd_alias(int argc, char **argv) {
    if (argc == 1) {
        for (int i = 0; i < ALIAS_MAX; ++i) {
            if (!aliases[i].used) continue;
            out("alias ");
            out(aliases[i].name);
            out("='");
            out(aliases[i].value);
            outln("'");
        }
        return;
    }
    for (int i = 1; i < argc; ++i) {
        char buffer[ALIAS_NAME_MAX + ALIAS_VALUE_MAX + 2];
        str_copy(buffer, argv[i], sizeof(buffer));
        char *equals = str_chr(buffer, '=');
        if (equals) {
            *equals = 0;
            if (alias_set(buffer, equals + 1) != 0) {
                command_error("alias", "invalid alias name or too many aliases");
            }
        } else {
            const char *value = alias_get(buffer);
            if (value) {
                out("alias ");
                out(buffer);
                out("='");
                out(value);
                outln("'");
            } else {
                out("alias: ");
                out(buffer);
                outln(": not found");
            }
        }
    }
}

static void cmd_unalias(int argc, char **argv) {
    if (argc < 2) {
        outln("Usage: unalias NAME... | -a");
        return;
    }
    if (argc == 2 && str_cmp(argv[1], "-a") == 0) {
        alias_clear();
        return;
    }
    for (int i = 1; i < argc; ++i) {
        if (alias_remove(argv[i]) != 0) {
            out("unalias: ");
            out(argv[i]);
            outln(": not found");
        }
    }
}

static void cmd_history(int argc, char **argv) {
    if (argc == 2 && str_cmp(argv[1], "-c") == 0) {
        history_count = 0;
        history_next = 0;
        return;
    }
    if (argc > 1) {
        outln("Usage: history [-c]");
        return;
    }
    if (!history_count) {
        outln("History is empty.");
        return;
    }
    unsigned first = (history_next + HISTORY_SIZE - history_count) % HISTORY_SIZE;
    for (unsigned i = 0; i < history_count; ++i) {
        out("  ");
        out_uint(i + 1);
        out("  ");
        outln(history[(first + i) % HISTORY_SIZE]);
    }
}

static void cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    vga_clear();
}

static void cmd_exit(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("logout");
    for (;;) __asm__ volatile ("cli; hlt");
}

static void cmd_about(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("RioOS is a small x86_64 bare-metal operating system.");
    outln("Powered by RioKernel 2.1.0, it boots directly from a BIOS disk");
    outln("image and runs without Linux. riosh provides 60+ Unix-like");
    outln("commands backed by an in-memory filesystem (ramfs).");
}

static void cmd_version(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("RioOS 2.1.0 (x86_64)");
}

static void cmd_mem(int argc, char **argv) {
    (void)argc;
    (void)argv;
    unsigned nodes = 0;
    unsigned bytes = 0;
    ramfs_usage(&nodes, &bytes);
    outln("Guest RAM configured in run.sh: 128 MiB");
    outln("Kernel identity map: first 1 GiB (virtual mapping)");
    out("ramfs: ");
    out_uint(nodes);
    out(" nodes, ");
    out_uint(bytes);
    outln(" bytes used");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf));
}

static void cmd_cpuinfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    uint32_t a, b, c, d;
    char vendor[13];
    cpuid(0, 0, &a, &b, &c, &d);
    *(uint32_t *)&vendor[0] = b;
    *(uint32_t *)&vendor[4] = d;
    *(uint32_t *)&vendor[8] = c;
    vendor[12] = 0;

    out("CPU vendor : ");
    outln(vendor);
    cpuid(1, 0, &a, &b, &c, &d);
    out("Family/model: ");
    out_uint((a >> 8) & 0x0F);
    vga_putc('/');
    out_uint((a >> 4) & 0x0F);
    vga_putc('\n');

    cpuid(0x80000000U, 0, &a, &b, &c, &d);
    if (a >= 0x80000004U) {
        char brand[49];
        uint32_t *words = (uint32_t *)brand;
        for (uint32_t leaf = 0; leaf < 3; ++leaf) {
            cpuid(0x80000002U + leaf, 0,
                  &words[leaf * 4], &words[leaf * 4 + 1],
                  &words[leaf * 4 + 2], &words[leaf * 4 + 3]);
        }
        brand[48] = 0;
        out("Model      : ");
        outln(brand);
    }
}

static int color_from_name(const char *name) {
    static const char *names[16] = {
        "black", "blue", "green", "cyan", "red", "magenta", "brown", "gray",
        "darkgray", "lightblue", "lightgreen", "lightcyan", "lightred", "pink",
        "yellow", "white"
    };
    for (int i = 0; i < 16; ++i) {
        if (str_cmp(name, names[i]) == 0) return i;
    }
    return -1;
}

static void cmd_color(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        outln("Usage: color FG [BG]");
        outln("Colors: black blue green cyan red magenta brown gray darkgray");
        outln("        lightblue lightgreen lightcyan lightred pink yellow white");
        return;
    }
    char fg_name[24];
    char bg_name[24];
    str_copy(fg_name, argv[1], sizeof(fg_name));
    str_lower(fg_name);
    if (argc == 3) {
        str_copy(bg_name, argv[2], sizeof(bg_name));
        str_lower(bg_name);
    } else {
        str_copy(bg_name, "black", sizeof(bg_name));
    }
    int fg = color_from_name(fg_name);
    int bg = color_from_name(bg_name);
    if (fg < 0 || bg < 0 || fg == bg) {
        outln("color: invalid color name or identical foreground/background");
        return;
    }
    vga_set_color((uint8_t)fg, (uint8_t)bg);
    outln("Color updated.");
}

static void cmd_neofetch(int argc, char **argv) {
    (void)argc;
    (void)argv;
    vga_set_color(11, 0);
    out("     ____           ");
    vga_set_color(7, 0);
    outln("OS       RioOS 2.1.0");
    vga_set_color(11, 0);
    out("    |  _ \\          ");
    vga_set_color(7, 0);
    outln("Kernel   RioKernel x86_64");
    vga_set_color(11, 0);
    out("    | |_) |         ");
    vga_set_color(7, 0);
    outln("Shell    riosh");
    vga_set_color(11, 0);
    out("    |  _ <          ");
    vga_set_color(7, 0);
    outln("RAM      128 MiB (QEMU)");
    vga_set_color(11, 0);
    out("    |_| \\_\\         ");
    vga_set_color(7, 0);
    outln("FS       ramfs (32 KiB)");
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("Restarting RioOS...");
    for (unsigned i = 0; i < 100000; ++i) {
        if (!(inb(0x64) & 0x02)) break;
    }
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("cli; hlt");
}

static void cmd_poweroff(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("Powering off...");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    for (;;) __asm__ volatile ("cli; hlt");
}

static void cmd_halt(int argc, char **argv) {
    (void)argc;
    (void)argv;
    outln("CPU halted. Reset the VM to continue.");
    for (;;) __asm__ volatile ("cli; hlt");
}

static int alias_expand_once(const char *line, char *out_buf, size_t size) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') ++p;
    const char *name_start = p;
    while (*p && *p != ' ' && *p != '\t') ++p;
    size_t name_len = (size_t)(p - name_start);
    if (!name_len || name_len >= ALIAS_NAME_MAX) return 0;
    char name[ALIAS_NAME_MAX];
    mem_copy(name, name_start, name_len);
    name[name_len] = 0;
    const char *value = alias_get(name);
    if (!value) return 0;
    size_t value_len = str_len(value);
    size_t rest_len = str_len(p);
    if (value_len + rest_len + 1 > size) return 0;
    mem_copy(out_buf, value, value_len);
    mem_copy(out_buf + value_len, p, rest_len + 1);
    return 1;
}

static void shell_execute(const char *line) {
    char stage[LINE_MAX];
    str_copy(stage, line, sizeof(stage));

    for (int depth = 0; depth < 4; ++depth) {
        char next[LINE_MAX];
        if (!alias_expand_once(stage, next, sizeof(next))) break;
        str_copy(stage, next, sizeof(stage));
    }

    char expanded[LINE_MAX];
    env_expand(stage, expanded, sizeof(expanded));

    char *argv[ARG_MAX];
    int argc = tokenize(expanded, argv, ARG_MAX);
    if (argc < 0) {
        outln("shell: too many arguments");
        return;
    }
    if (argc == 0) return;

    const command_t *cmd = find_command(argv[0]);
    if (!cmd) {
        out(argv[0]);
        outln(": command not found");
        outln("Type 'help' for a list of commands.");
        return;
    }
    cmd->run(argc, argv);
}

static void print_prompt(void) {
    char cwd[RAMFS_PATH_MAX];
    cwd[0] = 0;
    if (ramfs_getcwd(cwd, sizeof(cwd)) != RAMFS_OK) str_copy(cwd, "/", sizeof(cwd));
    const char *user = env_get("USER");
    const char *host = env_get("HOSTNAME");
    const char *home = env_get("HOME");
    if (!user) user = "rio";
    if (!host) host = "rioos";
    vga_set_color(10, 0);
    out(user);
    vga_putc('@');
    out(host);
    vga_set_color(7, 0);
    vga_putc(':');
    if (home && str_cmp(cwd, home) == 0) out("~");
    else out(cwd);
    out("$ ");
}

void shell_run(void) {
    ramfs_init();
    env_init();
    time_init();
    alias_init();

    char input[INPUT_SIZE];
    vga_set_color(10, 0);
    outln("RioOS v2.1.0 | x86_64 | QEMU");
    vga_set_color(7, 0);
    outln("Type 'help' to list commands.");
    vga_putc('\n');

    for (;;) {
        print_prompt();
        unsigned length = 0;
        input[0] = 0;
        for (;;) {
            char ch = keyboard_getchar();
            if (ch == '\n') {
                vga_putc('\n');
                input[length] = 0;
                break;
            }
            if (ch == '\b') {
                if (length) {
                    --length;
                    input[length] = 0;
                    vga_putc('\b');
                }
                continue;
            }
            if (ch >= 32 && ch < 127 && length + 1 < INPUT_SIZE) {
                input[length++] = ch;
                input[length] = 0;
                vga_putc(ch);
            }
        }
        history_add(input);
        shell_execute(input);
    }
}
