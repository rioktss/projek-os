#pragma once

#define RAMFS_MAX_NODES 96
#define RAMFS_NAME_MAX 32
#define RAMFS_DATA_SIZE 32768
#define RAMFS_PATH_MAX 256
#define RAMFS_MAX_DEPTH 32

enum {
    RAMFS_OK = 0,
    RAMFS_ENOENT = -1,
    RAMFS_EEXIST = -2,
    RAMFS_ENOTDIR = -3,
    RAMFS_EISDIR = -4,
    RAMFS_ENOTEMPTY = -5,
    RAMFS_ENOSPC = -6,
    RAMFS_EINVAL = -7,
    RAMFS_EBUSY = -8
};

void ramfs_init(void);
int ramfs_chdir(const char *path);
int ramfs_getcwd(char *buf, unsigned size);
int ramfs_lookup(const char *path, int *index);
int ramfs_mkdir(const char *path, int parents);
int ramfs_touch(const char *path);
int ramfs_rmdir(const char *path);
int ramfs_unlink(const char *path, int recursive);
int ramfs_read(const char *path, char *buf, unsigned max, unsigned *out_len);
int ramfs_write(const char *path, const char *data, unsigned len);
int ramfs_copy(const char *src, const char *dst, int recursive);
int ramfs_move(const char *src, const char *dst);
int ramfs_is_dir(int index);
int ramfs_is_file(int index);
unsigned ramfs_size(int index);
const char *ramfs_name(int index);
const char *ramfs_data(int index);
int ramfs_parent(int index);
int ramfs_first_child(int dir);
int ramfs_next_child(int dir, int previous);
int ramfs_child_count(int dir);
int ramfs_usage(unsigned *nodes_used, unsigned *bytes_used);
int ramfs_root(void);
const char *ramfs_strerror(int error);
