#include "ramfs.h"
#include "lib.h"

#define NODE_FREE 0
#define NODE_FILE 1
#define NODE_DIR  2

typedef struct {
    char name[RAMFS_NAME_MAX];
    int parent;
    unsigned offset;
    unsigned size;
    unsigned char type;
} ramfs_node_t;

static ramfs_node_t nodes[RAMFS_MAX_NODES];
static unsigned char pool[RAMFS_DATA_SIZE];
static unsigned pool_used;
static int cwd_index;

static int is_valid_index(int index) {
    return index >= 0 && index < RAMFS_MAX_NODES;
}

int ramfs_root(void) {
    return 0;
}

int ramfs_is_dir(int index) {
    return is_valid_index(index) && nodes[index].type == NODE_DIR;
}

int ramfs_is_file(int index) {
    return is_valid_index(index) && nodes[index].type == NODE_FILE;
}

unsigned ramfs_size(int index) {
    if (!is_valid_index(index)) return 0;
    return nodes[index].size;
}

const char *ramfs_name(int index) {
    if (!is_valid_index(index) || nodes[index].type == NODE_FREE) return "";
    return nodes[index].name;
}

const char *ramfs_data(int index) {
    if (!ramfs_is_file(index)) return 0;
    return (const char *)(pool + nodes[index].offset);
}

int ramfs_parent(int index) {
    if (!is_valid_index(index) || nodes[index].type == NODE_FREE) return -1;
    return nodes[index].parent;
}

static int find_child(int dir, const char *name) {
    if (!ramfs_is_dir(dir)) return -1;
    for (int i = 1; i < RAMFS_MAX_NODES; ++i) {
        if (nodes[i].type != NODE_FREE && nodes[i].parent == dir &&
            str_cmp(nodes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int alloc_node(void) {
    for (int i = 1; i < RAMFS_MAX_NODES; ++i) {
        if (nodes[i].type == NODE_FREE) return i;
    }
    return -1;
}

static int is_ancestor(int ancestor, int node) {
    int current = node;
    while (current >= 0) {
        if (current == ancestor) return 1;
        if (current == 0) break;
        current = nodes[current].parent;
    }
    return 0;
}

static int resolve_path(const char *path, int start, int *out) {
    if (!out) return RAMFS_EINVAL;
    if (!path || !*path) {
        *out = start;
        return RAMFS_OK;
    }
    int current = (path[0] == '/') ? 0 : start;
    const char *p = path;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        if (!ramfs_is_dir(current)) return RAMFS_ENOTDIR;
        char component[RAMFS_NAME_MAX];
        unsigned n = 0;
        while (*p && *p != '/') {
            if (n + 1 >= sizeof(component)) return RAMFS_EINVAL;
            component[n++] = *p++;
        }
        component[n] = 0;
        if (str_cmp(component, ".") == 0) continue;
        if (str_cmp(component, "..") == 0) {
            if (current != 0) current = nodes[current].parent;
            continue;
        }
        int child = find_child(current, component);
        if (child < 0) return RAMFS_ENOENT;
        current = child;
    }
    size_t len = str_len(path);
    if (len > 0 && path[len - 1] == '/' && !ramfs_is_dir(current)) {
        return RAMFS_ENOTDIR;
    }
    *out = current;
    return RAMFS_OK;
}

static int resolve_parent(const char *path, int *parent, char *name) {
    if (!path || !*path) return RAMFS_EINVAL;
    const char *last = str_rchr(path, '/');
    const char *name_start;
    int parent_index;
    if (!last) {
        parent_index = cwd_index;
        name_start = path;
    } else {
        name_start = last + 1;
        if (!*name_start) return RAMFS_EINVAL;
        if (last == path) {
            parent_index = 0;
        } else {
            char prefix[RAMFS_PATH_MAX];
            size_t plen = (size_t)(last - path);
            if (plen + 1 > sizeof(prefix)) return RAMFS_EINVAL;
            mem_copy(prefix, path, plen);
            prefix[plen] = 0;
            int rc = resolve_path(prefix, cwd_index, &parent_index);
            if (rc != RAMFS_OK) return rc;
        }
    }
    size_t nlen = str_len(name_start);
    if (!nlen || nlen >= RAMFS_NAME_MAX) return RAMFS_EINVAL;
    if (str_cmp(name_start, ".") == 0 || str_cmp(name_start, "..") == 0) {
        return RAMFS_EINVAL;
    }
    if (!ramfs_is_dir(parent_index)) return RAMFS_ENOTDIR;
    str_copy(name, name_start, RAMFS_NAME_MAX);
    *parent = parent_index;
    return RAMFS_OK;
}

static int create_node_at(int parent, const char *name, int type) {
    int index = alloc_node();
    if (index < 0) return RAMFS_ENOSPC;
    str_copy(nodes[index].name, name, RAMFS_NAME_MAX);
    nodes[index].parent = parent;
    nodes[index].offset = 0;
    nodes[index].size = 0;
    nodes[index].type = (unsigned char)type;
    return index;
}

static int create_node(const char *path, int type) {
    int parent;
    char name[RAMFS_NAME_MAX];
    int rc = resolve_parent(path, &parent, name);
    if (rc != RAMFS_OK) return rc;
    if (find_child(parent, name) >= 0) return RAMFS_EEXIST;
    int index = create_node_at(parent, name, type);
    return index < 0 ? index : RAMFS_OK;
}

int ramfs_lookup(const char *path, int *index) {
    return resolve_path(path, cwd_index, index);
}

int ramfs_chdir(const char *path) {
    int index;
    int rc = resolve_path(path, cwd_index, &index);
    if (rc != RAMFS_OK) return rc;
    if (!ramfs_is_dir(index)) return RAMFS_ENOTDIR;
    cwd_index = index;
    return RAMFS_OK;
}

int ramfs_getcwd(char *buf, unsigned size) {
    if (!buf || size < 2) return RAMFS_EINVAL;
    int stack[RAMFS_MAX_DEPTH];
    int count = 0;
    int current = cwd_index;
    while (current != 0) {
        if (count >= RAMFS_MAX_DEPTH) return RAMFS_EINVAL;
        stack[count++] = current;
        current = nodes[current].parent;
    }
    size_t pos = 0;
    buf[pos++] = '/';
    for (int i = count - 1; i >= 0; --i) {
        const char *name = nodes[stack[i]].name;
        size_t len = str_len(name);
        if (pos + len + (i > 0 ? 1U : 0U) + 1U > size) return RAMFS_EINVAL;
        mem_copy(buf + pos, name, len);
        pos += len;
        if (i > 0) buf[pos++] = '/';
    }
    buf[pos] = 0;
    return RAMFS_OK;
}

int ramfs_mkdir(const char *path, int parents) {
    if (!path || !*path) return RAMFS_EINVAL;
    if (!parents) {
        int parent;
        char name[RAMFS_NAME_MAX];
        int rc = resolve_parent(path, &parent, name);
        if (rc != RAMFS_OK) return rc;
        if (find_child(parent, name) >= 0) return RAMFS_EEXIST;
        int index = create_node_at(parent, name, NODE_DIR);
        return index < 0 ? index : RAMFS_OK;
    }

    char buf[RAMFS_PATH_MAX];
    if (str_len(path) + 1 > sizeof(buf)) return RAMFS_EINVAL;
    str_copy(buf, path, sizeof(buf));
    char *start = buf;
    if (*start == '/') ++start;
    for (char *p = start; ; ++p) {
        if (*p == '/' || *p == 0) {
            char saved = *p;
            *p = 0;
            if (*start) {
                int index;
                int rc = resolve_path(buf, cwd_index, &index);
                if (rc == RAMFS_ENOENT) {
                    int parent;
                    char name[RAMFS_NAME_MAX];
                    rc = resolve_parent(buf, &parent, name);
                    if (rc != RAMFS_OK) return rc;
                    int created = create_node_at(parent, name, NODE_DIR);
                    if (created < 0) return created;
                } else if (rc != RAMFS_OK) {
                    return rc;
                } else if (!ramfs_is_dir(index)) {
                    return RAMFS_ENOTDIR;
                }
            }
            *p = saved;
            if (!saved) break;
        }
    }
    return RAMFS_OK;
}

int ramfs_touch(const char *path) {
    int index;
    int rc = resolve_path(path, cwd_index, &index);
    if (rc == RAMFS_OK) {
        if (ramfs_is_dir(index)) return RAMFS_OK;
        return RAMFS_OK;
    }
    if (rc != RAMFS_ENOENT) return rc;
    return create_node(path, NODE_FILE);
}

int ramfs_rmdir(const char *path) {
    int index;
    int rc = resolve_path(path, cwd_index, &index);
    if (rc != RAMFS_OK) return rc;
    if (!ramfs_is_dir(index)) return RAMFS_ENOTDIR;
    if (index == 0) return RAMFS_EBUSY;
    if (is_ancestor(index, cwd_index)) return RAMFS_EBUSY;
    if (ramfs_first_child(index) >= 0) return RAMFS_ENOTEMPTY;
    nodes[index].type = NODE_FREE;
    return RAMFS_OK;
}

static int remove_node(int index, int recursive, int depth) {
    if (depth > RAMFS_MAX_DEPTH) return RAMFS_EINVAL;
    if (ramfs_is_dir(index)) {
        int child = ramfs_first_child(index);
        while (child >= 0) {
            int next = ramfs_next_child(index, child);
            int rc = remove_node(child, recursive, depth + 1);
            if (rc != RAMFS_OK) return rc;
            child = next;
        }
    }
    (void)recursive;
    nodes[index].type = NODE_FREE;
    return RAMFS_OK;
}

int ramfs_unlink(const char *path, int recursive) {
    int index;
    int rc = resolve_path(path, cwd_index, &index);
    if (rc != RAMFS_OK) return rc;
    if (index == 0) return RAMFS_EBUSY;
    if (is_ancestor(index, cwd_index)) return RAMFS_EBUSY;
    if (ramfs_is_dir(index) && !recursive) return RAMFS_EISDIR;
    return remove_node(index, recursive, 0);
}

int ramfs_read(const char *path, char *buf, unsigned max, unsigned *out_len) {
    int index;
    int rc = resolve_path(path, cwd_index, &index);
    if (rc != RAMFS_OK) return rc;
    if (!ramfs_is_file(index)) return RAMFS_EISDIR;
    unsigned n = nodes[index].size;
    if (n > max) n = max;
    if (n > 0) {
        if (!buf) return RAMFS_EINVAL;
        mem_copy(buf, pool + nodes[index].offset, n);
    }
    if (out_len) *out_len = n;
    return RAMFS_OK;
}

static int write_file_data(int index, const char *data, unsigned len) {
    if (len > nodes[index].size) {
        if (pool_used + len > RAMFS_DATA_SIZE) return RAMFS_ENOSPC;
        nodes[index].offset = pool_used;
        pool_used += len;
    }
    if (len > 0) {
        if (!data) return RAMFS_EINVAL;
        mem_copy(pool + nodes[index].offset, data, len);
    }
    nodes[index].size = len;
    return RAMFS_OK;
}

int ramfs_write(const char *path, const char *data, unsigned len) {
    int parent;
    char name[RAMFS_NAME_MAX];
    int rc = resolve_parent(path, &parent, name);
    if (rc != RAMFS_OK) return rc;
    int index = find_child(parent, name);
    if (index >= 0) {
        if (ramfs_is_dir(index)) return RAMFS_EISDIR;
        return write_file_data(index, data, len);
    }
    if (pool_used + len > RAMFS_DATA_SIZE) return RAMFS_ENOSPC;
    int created = create_node_at(parent, name, NODE_FILE);
    if (created < 0) return created;
    nodes[created].offset = pool_used;
    return write_file_data(created, data, len);
}

int ramfs_first_child(int dir) {
    if (!ramfs_is_dir(dir)) return -1;
    for (int i = 1; i < RAMFS_MAX_NODES; ++i) {
        if (nodes[i].type != NODE_FREE && nodes[i].parent == dir) return i;
    }
    return -1;
}

int ramfs_next_child(int dir, int previous) {
    if (!ramfs_is_dir(dir)) return -1;
    for (int i = previous + 1; i < RAMFS_MAX_NODES; ++i) {
        if (nodes[i].type != NODE_FREE && nodes[i].parent == dir) return i;
    }
    return -1;
}

int ramfs_child_count(int dir) {
    if (!ramfs_is_dir(dir)) return 0;
    int count = 0;
    for (int i = 1; i < RAMFS_MAX_NODES; ++i) {
        if (nodes[i].type != NODE_FREE && nodes[i].parent == dir) ++count;
    }
    return count;
}

int ramfs_usage(unsigned *nodes_used, unsigned *bytes_used) {
    unsigned used = 0;
    for (int i = 0; i < RAMFS_MAX_NODES; ++i) {
        if (nodes[i].type != NODE_FREE) ++used;
    }
    if (nodes_used) *nodes_used = used;
    if (bytes_used) *bytes_used = pool_used;
    return RAMFS_OK;
}

static int copy_node(int src, int dst_parent, const char *dst_name,
                     int recursive, int depth) {
    if (depth > RAMFS_MAX_DEPTH) return RAMFS_EINVAL;
    if (!ramfs_is_dir(dst_parent)) return RAMFS_ENOTDIR;
    if (ramfs_is_dir(src)) {
        if (!recursive) return RAMFS_EISDIR;
        if (is_ancestor(src, dst_parent)) return RAMFS_EINVAL;
    }
    int existing = find_child(dst_parent, dst_name);
    if (existing == src) return RAMFS_EINVAL;

    if (ramfs_is_file(src)) {
        if (existing >= 0) {
            if (ramfs_is_dir(existing)) return RAMFS_EISDIR;
            return write_file_data(existing, (const char *)(pool + nodes[src].offset), nodes[src].size);
        }
        if (pool_used + nodes[src].size > RAMFS_DATA_SIZE) return RAMFS_ENOSPC;
        int created = create_node_at(dst_parent, dst_name, NODE_FILE);
        if (created < 0) return created;
        nodes[created].offset = pool_used;
        return write_file_data(created, (const char *)(pool + nodes[src].offset), nodes[src].size);
    }

    int dest;
    if (existing >= 0) {
        if (!ramfs_is_dir(existing)) return RAMFS_EEXIST;
        dest = existing;
    } else {
        dest = create_node_at(dst_parent, dst_name, NODE_DIR);
        if (dest < 0) return dest;
    }
    int child = ramfs_first_child(src);
    while (child >= 0) {
        int rc = copy_node(child, dest, nodes[child].name, recursive, depth + 1);
        if (rc != RAMFS_OK) return rc;
        child = ramfs_next_child(src, child);
    }
    return RAMFS_OK;
}

static int build_destination(const char *dst, int src, int *parent, char *name) {
    if (!dst || !*dst) return RAMFS_EINVAL;
    int index;
    int rc = resolve_path(dst, cwd_index, &index);
    if (rc == RAMFS_OK && ramfs_is_dir(index)) {
        char combined[RAMFS_PATH_MAX];
        str_copy(combined, dst, sizeof(combined));
        size_t len = str_len(combined);
        if (len == 0 || combined[len - 1] != '/') {
            str_cat(combined, "/", sizeof(combined));
        }
        str_cat(combined, nodes[src].name, sizeof(combined));
        return resolve_parent(combined, parent, name);
    }
    if (rc == RAMFS_OK || rc == RAMFS_ENOENT) {
        return resolve_parent(dst, parent, name);
    }
    return rc;
}

int ramfs_copy(const char *src_path, const char *dst_path, int recursive) {
    int src;
    int rc = resolve_path(src_path, cwd_index, &src);
    if (rc != RAMFS_OK) return rc;
    if (src == 0) return RAMFS_EINVAL;
    if (ramfs_is_dir(src) && !recursive) return RAMFS_EISDIR;
    int parent;
    char name[RAMFS_NAME_MAX];
    rc = build_destination(dst_path, src, &parent, name);
    if (rc != RAMFS_OK) return rc;
    return copy_node(src, parent, name, recursive, 0);
}

int ramfs_move(const char *src_path, const char *dst_path) {
    int src;
    int rc = resolve_path(src_path, cwd_index, &src);
    if (rc != RAMFS_OK) return rc;
    if (src == 0) return RAMFS_EBUSY;
    if (is_ancestor(src, cwd_index)) return RAMFS_EBUSY;
    int parent;
    char name[RAMFS_NAME_MAX];
    rc = build_destination(dst_path, src, &parent, name);
    if (rc != RAMFS_OK) return rc;
    if (parent == nodes[src].parent && str_cmp(name, nodes[src].name) == 0) {
        return RAMFS_OK;
    }
    if (is_ancestor(src, parent)) return RAMFS_EINVAL;
    rc = copy_node(src, parent, name, 1, 0);
    if (rc != RAMFS_OK) return rc;
    return remove_node(src, 1, 0);
}

const char *ramfs_strerror(int error) {
    switch (error) {
        case RAMFS_OK: return "Success";
        case RAMFS_ENOENT: return "No such file or directory";
        case RAMFS_EEXIST: return "File exists";
        case RAMFS_ENOTDIR: return "Not a directory";
        case RAMFS_EISDIR: return "Is a directory";
        case RAMFS_ENOTEMPTY: return "Directory not empty";
        case RAMFS_ENOSPC: return "No space left on device";
        case RAMFS_EINVAL: return "Invalid argument";
        case RAMFS_EBUSY: return "Device or resource busy";
        default: return "Unknown error";
    }
}

#define WRITE_LITERAL(path, text) ramfs_write((path), (text), sizeof(text) - 1)

void ramfs_init(void) {
    mem_set(nodes, 0, sizeof(nodes));
    mem_set(pool, 0, sizeof(pool));
    pool_used = 0;
    cwd_index = 0;

    nodes[0].type = NODE_DIR;
    nodes[0].parent = -1;
    str_copy(nodes[0].name, "/", RAMFS_NAME_MAX);

    ramfs_mkdir("/home", 0);
    ramfs_mkdir("/home/rio", 0);
    ramfs_mkdir("/etc", 0);
    ramfs_mkdir("/tmp", 0);
    ramfs_mkdir("/var", 0);
    ramfs_mkdir("/var/log", 0);
    ramfs_mkdir("/bin", 0);
    ramfs_mkdir("/usr", 0);
    ramfs_mkdir("/usr/bin", 0);

    WRITE_LITERAL("/etc/hostname", "rioos\n");
    WRITE_LITERAL("/etc/motd",
        "Welcome to RioOS 2.1.0\n"
        "RioKernel x86_64 | ramfs | riosh\n"
        "Type 'help' for commands, 'man CMD' for details.\n");
    WRITE_LITERAL("/etc/os-release",
        "NAME=\"RioOS\"\n"
        "VERSION=\"2.1.0\"\n"
        "ID=rioos\n"
        "PRETTY_NAME=\"RioOS 2.1.0\"\n");
    WRITE_LITERAL("/home/rio/readme.txt",
        "Welcome to RioOS!\n"
        "This is an in-memory filesystem (ramfs).\n"
        "Try: ls, cd /etc, cat motd, grep Rio /etc/os-release,\n"
        "     head -n 2 readme.txt, tree, du -s /.\n");
    WRITE_LITERAL("/home/rio/hello.txt", "Hello from RioOS!\n");
    WRITE_LITERAL("/home/rio/notes.txt",
        "banana 3\n"
        "apple 10\n"
        "cherry 7\n"
        "date 1\n"
        "elderberry 5\n");

    ramfs_chdir("/home/rio");
}
