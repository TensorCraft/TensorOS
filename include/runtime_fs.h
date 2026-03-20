#pragma once

#include "runtime_syscall.h"

#include <stdint.h>

#define RUNTIME_FS_MAX_NODES 16u
#define RUNTIME_FS_MAX_OPEN_FILES 8u
#define RUNTIME_FS_NAME_CAPACITY 16u
#define RUNTIME_FS_FILE_CAPACITY 256u

struct runtime_fs_node {
  uint32_t used;
  uint32_t type;
  uint32_t parent_index;
  uint32_t owner_pid;
  uint32_t mode;
  uint32_t size_bytes;
  char name[RUNTIME_FS_NAME_CAPACITY];
  uint8_t data[RUNTIME_FS_FILE_CAPACITY];
};

struct runtime_fs_open_file {
  uint32_t used;
  uint32_t node_index;
  uint32_t owner_pid;
  uint32_t flags;
  uint32_t offset;
  uint32_t dir_cursor;
};

struct runtime_fs {
  struct runtime_fs_node nodes[RUNTIME_FS_MAX_NODES];
  struct runtime_fs_open_file open_files[RUNTIME_FS_MAX_OPEN_FILES];
};

extern volatile uint32_t g_runtime_fs_validate_diag_code;
extern volatile uint32_t g_runtime_fs_validate_diag_arg0;
extern volatile uint32_t g_runtime_fs_validate_diag_arg1;
extern volatile uint32_t g_runtime_fs_mkdir_diag_code;
extern volatile uint32_t g_runtime_fs_mkdir_diag_arg0;
extern volatile uint32_t g_runtime_fs_mkdir_diag_arg1;

void runtime_fs_init(struct runtime_fs *fs);
int32_t runtime_fs_mkdir(struct runtime_fs *fs, uint32_t current_pid, const char *path, uint32_t mode);
int32_t runtime_fs_open(struct runtime_fs *fs, uint32_t current_pid, const char *path, uint32_t flags,
                        uintptr_t *file_object);
int32_t runtime_fs_read(struct runtime_fs *fs, uintptr_t file_object, void *buffer, uint32_t size);
int32_t runtime_fs_write(struct runtime_fs *fs, uintptr_t file_object, const void *buffer,
                         uint32_t size);
int32_t runtime_fs_close(struct runtime_fs *fs, uintptr_t file_object);
int32_t runtime_fs_stat(struct runtime_fs *fs, uintptr_t file_object, struct runtime_file_stat *stat);
int32_t runtime_fs_readdir(struct runtime_fs *fs, uintptr_t file_object, struct runtime_dir_entry *entry);
int32_t runtime_fs_seek(struct runtime_fs *fs, uintptr_t file_object, int32_t offset, uint32_t whence);
int32_t runtime_fs_remove(struct runtime_fs *fs, uint32_t current_pid, const char *path);
int32_t runtime_fs_rename(struct runtime_fs *fs, uint32_t current_pid, const char *old_path, const char *new_path);
int32_t runtime_fs_validate(const struct runtime_fs *fs);
