#include "runtime_fs.h"

#if defined(__has_include)
#if __has_include("soc.h")
#include "soc.h"
#define RUNTIME_FS_HAS_SOC_DEBUG 1
#endif
#endif

#ifndef RUNTIME_FS_HAS_SOC_DEBUG
#define RUNTIME_FS_HAS_SOC_DEBUG 0
#endif

#include <limits.h>
#include <stddef.h>

volatile uint32_t g_runtime_fs_validate_diag_code;
volatile uint32_t g_runtime_fs_validate_diag_arg0;
volatile uint32_t g_runtime_fs_validate_diag_arg1;
volatile uint32_t g_runtime_fs_mkdir_diag_code;
volatile uint32_t g_runtime_fs_mkdir_diag_arg0;
volatile uint32_t g_runtime_fs_mkdir_diag_arg1;

static inline __attribute__((unused)) void runtime_fs_debug_write(uint32_t reg, uint32_t value) {
#if RUNTIME_FS_HAS_SOC_DEBUG
  reg_write(reg, value);
#else
  (void)reg;
  (void)value;
#endif
}

static int32_t runtime_fs_validate_fail(uint32_t code, uint32_t arg0, uint32_t arg1, int32_t status) {
  g_runtime_fs_validate_diag_code = code;
  g_runtime_fs_validate_diag_arg0 = arg0;
  g_runtime_fs_validate_diag_arg1 = arg1;
#if RUNTIME_FS_HAS_SOC_DEBUG
  runtime_fs_debug_write(RTC_CNTL_STORE1_REG, code);
  runtime_fs_debug_write(RTC_CNTL_STORE2_REG, arg0);
  runtime_fs_debug_write(RTC_CNTL_STORE3_REG, arg1);
#endif
  return status;
}

__attribute__((noinline))
static int32_t runtime_fs_mkdir_fail(uint32_t code, uint32_t arg0, uint32_t arg1, int32_t status) {
  g_runtime_fs_mkdir_diag_code = code;
  g_runtime_fs_mkdir_diag_arg0 = arg0;
  g_runtime_fs_mkdir_diag_arg1 = arg1;
  return status;
}

__attribute__((noinline))
static int32_t runtime_fs_return_ok(void) {
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int name_equals(const char *a, const char *b) {
  uint32_t index = 0u;

  while ((a[index] != '\0') && (b[index] != '\0')) {
    if (a[index] != b[index]) {
      return 0;
    }
    ++index;
  }

  return (a[index] == '\0') && (b[index] == '\0');
}

static void copy_name(char *dest, const char *src) {
  uint32_t index = 0u;

  while ((src[index] != '\0') && (index + 1u < RUNTIME_FS_NAME_CAPACITY)) {
    dest[index] = src[index];
    ++index;
  }

  dest[index] = '\0';
}

static void fs_zero_range(struct runtime_fs_node *node, uint32_t start, uint32_t end) {
  uint32_t index;

  if ((node == NULL) || (start >= end) || (start >= RUNTIME_FS_FILE_CAPACITY)) {
    return;
  }

  if (end > RUNTIME_FS_FILE_CAPACITY) {
    end = RUNTIME_FS_FILE_CAPACITY;
  }

  for (index = start; index < end; ++index) {
    node->data[index] = 0u;
  }
}

static int fs_token_next(const char **cursor, char *token) {
  uint32_t length = 0u;
  const char *scan = *cursor;

  while (*scan == '/') {
    ++scan;
  }

  if (*scan == '\0') {
    *cursor = scan;
    return 0;
  }

  while ((*scan != '\0') && (*scan != '/')) {
    if (length + 1u >= RUNTIME_FS_NAME_CAPACITY) {
      return -1;
    }
    token[length++] = *scan++;
  }

  token[length] = '\0';
  *cursor = scan;
  return 1;
}

static int fs_find_child(const struct runtime_fs *fs, uint32_t parent_index, const char *name) {
  uint32_t index;

  for (index = 0u; index < RUNTIME_FS_MAX_NODES; ++index) {
    if (fs->nodes[index].used && (fs->nodes[index].parent_index == parent_index) &&
        name_equals(fs->nodes[index].name, name)) {
      return (int)index;
    }
  }

  return -1;
}

static int fs_path_is_root(const char *path) {
  return (path != NULL) && (path[0] == '/') && (path[1] == '\0');
}

static int fs_path_is_absolute_non_root(const char *path) {
  return (path != NULL) && (path[0] == '/') && (path[1] != '\0');
}

static int fs_alloc_node(struct runtime_fs *fs) {
  uint32_t index;

  for (index = 1u; index < RUNTIME_FS_MAX_NODES; ++index) {
    if (fs->nodes[index].used == 0u) {
      fs->nodes[index].used = 1u;
      fs->nodes[index].type = RUNTIME_FILE_TYPE_UNKNOWN;
      fs->nodes[index].parent_index = 0u;
      fs->nodes[index].owner_pid = 0u;
      fs->nodes[index].mode = 0u;
      fs->nodes[index].size_bytes = 0u;
      fs->nodes[index].name[0] = '\0';
      fs_zero_range(&fs->nodes[index], 0u, RUNTIME_FS_FILE_CAPACITY);
      return (int)index;
    }
  }

  return -1;
}

static int fs_resolve_path(const struct runtime_fs *fs, const char *path) {
  const char *cursor = path;
  char token[RUNTIME_FS_NAME_CAPACITY];
  int current = 0;
  int step;

  if ((fs == NULL) || (path == NULL) || (path[0] != '/')) {
    return -1;
  }

  if ((path[0] == '/') && (path[1] == '\0')) {
    return 0;
  }

  for (;;) {
    step = fs_token_next(&cursor, token);
    if (step == 0) {
      return current;
    }
    if (step < 0) {
      return -1;
    }

    current = fs_find_child(fs, (uint32_t)current, token);
    if (current < 0) {
      return -1;
    }
  }
}

static int32_t fs_open_slot_alloc(struct runtime_fs *fs, uint32_t node_index, uint32_t flags) {
  uint32_t index;

  for (index = 0u; index < RUNTIME_FS_MAX_OPEN_FILES; ++index) {
    if (fs->open_files[index].used == 0u) {
      fs->open_files[index].used = 1u;
      fs->open_files[index].node_index = node_index;
      fs->open_files[index].owner_pid = 0u;
      fs->open_files[index].flags = flags;
      fs->open_files[index].offset = 0u;
      fs->open_files[index].dir_cursor = 0u;
      return (int32_t)(index + 1u);
    }
  }

  return RUNTIME_SYSCALL_STATUS_ENOSPC;
}

static struct runtime_fs_open_file *fs_open_file_lookup(struct runtime_fs *fs, uintptr_t file_object) {
  if ((fs == NULL) || (file_object == 0u) || (file_object > RUNTIME_FS_MAX_OPEN_FILES)) {
    return NULL;
  }

  if (fs->open_files[file_object - 1u].used == 0u) {
    return NULL;
  }

  return &fs->open_files[file_object - 1u];
}

static void fs_clear_node(struct runtime_fs_node *node) {
  uint32_t index;

  if (node == NULL) {
    return;
  }

  node->used = 0u;
  node->type = RUNTIME_FILE_TYPE_UNKNOWN;
  node->parent_index = 0u;
  node->owner_pid = 0u;
  node->mode = 0u;
  node->size_bytes = 0u;
  node->name[0] = '\0';
  for (index = 0u; index < RUNTIME_FS_FILE_CAPACITY; ++index) {
    node->data[index] = 0u;
  }
}

static int fs_node_has_children(const struct runtime_fs *fs, uint32_t node_index) {
  uint32_t index;

  if (fs == NULL) {
    return 0;
  }

  for (index = 0u; index < RUNTIME_FS_MAX_NODES; ++index) {
    if (fs->nodes[index].used && (fs->nodes[index].parent_index == node_index) && (index != node_index)) {
      return 1;
    }
  }

  return 0;
}

static int fs_node_is_open(const struct runtime_fs *fs, uint32_t node_index) {
  uint32_t index;

  if (fs == NULL) {
    return 0;
  }

  for (index = 0u; index < RUNTIME_FS_MAX_OPEN_FILES; ++index) {
    if (fs->open_files[index].used && (fs->open_files[index].node_index == node_index)) {
      return 1;
    }
  }

  return 0;
}

static int fs_node_owner_allows(const struct runtime_fs_node *node, uint32_t current_pid) {
  if (node == NULL) {
    return 0;
  }

  return (node->owner_pid == 0u) || (current_pid == 0u) || (node->owner_pid == current_pid);
}

static int fs_node_has_open_writer(const struct runtime_fs *fs, uint32_t node_index) {
  uint32_t index;

  if (fs == NULL) {
    return 0;
  }

  for (index = 0u; index < RUNTIME_FS_MAX_OPEN_FILES; ++index) {
    if (fs->open_files[index].used && (fs->open_files[index].node_index == node_index) &&
        ((fs->open_files[index].flags & RUNTIME_FILE_OPEN_WRITE) != 0u)) {
      return 1;
    }
  }

  return 0;
}

static int fs_parent_chain_contains(const struct runtime_fs *fs, uint32_t start_index,
                                    uint32_t candidate_ancestor) {
  uint32_t current = start_index;

  if (fs == NULL) {
    return 0;
  }

  while (current != 0u) {
    if (current == candidate_ancestor) {
      return 1;
    }
    current = fs->nodes[current].parent_index;
  }

  return 0;
}

static int fs_name_is_empty(const char *name) {
  return (name == NULL) || (name[0] == '\0');
}

static int32_t fs_resolve_existing_path(const struct runtime_fs *fs, const char *path, int *node_index) {
  int resolved_index;

  if ((fs == NULL) || (path == NULL) || (node_index == NULL) || (path[0] != '/')) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  resolved_index = fs_resolve_path(fs, path);
  if (resolved_index < 0) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  *node_index = resolved_index;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t fs_resolve_parent_path(const struct runtime_fs *fs, const char *path, char *leaf_name,
                                      int *parent_index) {
  const char *cursor = path;
  char token[RUNTIME_FS_NAME_CAPACITY];
  char next_token[RUNTIME_FS_NAME_CAPACITY];
  int current = 0;
  int step;
  int next_step;

  if ((fs == NULL) || (path == NULL) || (leaf_name == NULL) || (parent_index == NULL) ||
      !fs_path_is_absolute_non_root(path)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  step = fs_token_next(&cursor, token);
  if (step <= 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (;;) {
    const char *peek = cursor;

    next_step = fs_token_next(&peek, next_token);
    if (next_step == 0) {
      copy_name(leaf_name, token);
      *parent_index = current;
      return RUNTIME_SYSCALL_STATUS_OK;
    }
    if (next_step < 0) {
      return RUNTIME_SYSCALL_STATUS_EINVAL;
    }

    current = fs_find_child(fs, (uint32_t)current, token);
    if (current < 0) {
      return RUNTIME_SYSCALL_STATUS_ENOENT;
    }
    if (fs->nodes[current].type != RUNTIME_FILE_TYPE_DIRECTORY) {
      return RUNTIME_SYSCALL_STATUS_ENOTDIR;
    }

    copy_name(token, next_token);
    cursor = peek;
  }
}

void runtime_fs_init(struct runtime_fs *fs) {
  uint8_t *bytes;
  uint32_t index;

  if (fs == NULL) {
    return;
  }

  bytes = (uint8_t *)fs;
  for (index = 0u; index < (uint32_t)sizeof(*fs); ++index) {
    bytes[index] = 0u;
  }

  fs->nodes[0].used = 1u;
  fs->nodes[0].type = RUNTIME_FILE_TYPE_DIRECTORY;
  fs->nodes[0].parent_index = 0u;
  fs->nodes[0].owner_pid = 0u;
  fs->nodes[0].mode = 0755u;
  fs->nodes[0].name[0] = '/';
  fs->nodes[0].name[1] = '\0';
}

int32_t runtime_fs_mkdir(struct runtime_fs *fs, uint32_t current_pid, const char *path, uint32_t mode) {
  char leaf_name[RUNTIME_FS_NAME_CAPACITY];
  int parent_index;
  int node_index;
  g_runtime_fs_mkdir_diag_code = 0u;
  g_runtime_fs_mkdir_diag_arg0 = 0u;
  g_runtime_fs_mkdir_diag_arg1 = 0u;
  g_runtime_fs_mkdir_diag_code = 0xB210u;
  g_runtime_fs_mkdir_diag_arg0 = current_pid;
  g_runtime_fs_mkdir_diag_arg1 = (uint32_t)(uintptr_t)path;

  if ((fs == NULL) || !fs_path_is_absolute_non_root(path)) {
    return runtime_fs_mkdir_fail(0xB200u, (uint32_t)(uintptr_t)fs,
                                 path != NULL ? (((uint32_t)(uint8_t)path[0]) |
                                                     (((uint32_t)(uint8_t)path[1]) << 8))
                                              : 0u,
                                 RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  node_index = fs_resolve_path(fs, path);
  if (node_index >= 0) {
    return runtime_fs_mkdir_fail(0xB201u, (uint32_t)node_index, current_pid,
                                 RUNTIME_SYSCALL_STATUS_EEXIST);
  }
  g_runtime_fs_mkdir_diag_code = 0xB211u;
  g_runtime_fs_mkdir_diag_arg0 = ((uint32_t)(uint8_t)path[1]) |
                                 (((uint32_t)(uint8_t)path[2]) << 8);
  g_runtime_fs_mkdir_diag_arg1 = ((uint32_t)(uint8_t)path[3]) |
                                 (((uint32_t)(uint8_t)path[4]) << 8);

  {
    int32_t status = fs_resolve_parent_path(fs, path, leaf_name, &parent_index);
  if (status != RUNTIME_SYSCALL_STATUS_OK) {
      return runtime_fs_mkdir_fail(0xB202u, (uint32_t)status,
                                   ((uint32_t)(uint8_t)path[1]) |
                                       (((uint32_t)(uint8_t)path[2]) << 8),
                                   status);
    }
  }
  g_runtime_fs_mkdir_diag_code = 0xB212u;
  g_runtime_fs_mkdir_diag_arg0 = (uint32_t)parent_index;
  g_runtime_fs_mkdir_diag_arg1 = ((uint32_t)(uint8_t)leaf_name[0]) |
                                 (((uint32_t)(uint8_t)leaf_name[1]) << 8);

  if (!fs_node_owner_allows(&fs->nodes[parent_index], current_pid)) {
    return runtime_fs_mkdir_fail(0xB203u, (uint32_t)parent_index, fs->nodes[parent_index].owner_pid,
                                 RUNTIME_SYSCALL_STATUS_EACCES);
  }
  g_runtime_fs_mkdir_diag_code = 0xB213u;
  g_runtime_fs_mkdir_diag_arg0 = (uint32_t)parent_index;
  g_runtime_fs_mkdir_diag_arg1 = fs->nodes[parent_index].owner_pid;

  node_index = fs_alloc_node(fs);
  if (node_index < 0) {
    return runtime_fs_mkdir_fail(0xB204u, RUNTIME_FS_MAX_NODES, current_pid,
                                 RUNTIME_SYSCALL_STATUS_ENOSPC);
  }
  g_runtime_fs_mkdir_diag_code = 0xB214u;
  g_runtime_fs_mkdir_diag_arg0 = (uint32_t)node_index;
  g_runtime_fs_mkdir_diag_arg1 = (uint32_t)parent_index;

  fs->nodes[node_index].type = RUNTIME_FILE_TYPE_DIRECTORY;
  fs->nodes[node_index].parent_index = (uint32_t)parent_index;
  fs->nodes[node_index].owner_pid = current_pid;
  fs->nodes[node_index].mode = mode;
  copy_name(fs->nodes[node_index].name, leaf_name);
  g_runtime_fs_mkdir_diag_code = 0xB2FFu;
  g_runtime_fs_mkdir_diag_arg0 = (uint32_t)node_index;
  g_runtime_fs_mkdir_diag_arg1 = (uint32_t)parent_index;
  return runtime_fs_return_ok();
}

int32_t runtime_fs_open(struct runtime_fs *fs, uint32_t current_pid, const char *path, uint32_t flags,
                        uintptr_t *file_object) {
  int node_index;
  char leaf_name[RUNTIME_FS_NAME_CAPACITY];
  int parent_index;

  if ((fs == NULL) || (path == NULL) || (file_object == NULL) || (path[0] != '/')) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  node_index = fs_resolve_path(fs, path);
  if (node_index < 0) {
    if ((flags & RUNTIME_FILE_OPEN_CREATE) == 0u) {
      return RUNTIME_SYSCALL_STATUS_ENOENT;
    }

    {
      int32_t parent_status = fs_resolve_parent_path(fs, path, leaf_name, &parent_index);
      if (parent_status != RUNTIME_SYSCALL_STATUS_OK) {
        return parent_status;
      }

      if (!fs_node_owner_allows(&fs->nodes[parent_index], current_pid)) {
        return RUNTIME_SYSCALL_STATUS_EACCES;
      }
    }

    node_index = fs_alloc_node(fs);
    if (node_index < 0) {
      return RUNTIME_SYSCALL_STATUS_ENOSPC;
    }

    fs->nodes[node_index].type = RUNTIME_FILE_TYPE_REGULAR;
    fs->nodes[node_index].parent_index = (uint32_t)parent_index;
    fs->nodes[node_index].owner_pid = current_pid;
    fs->nodes[node_index].mode = 0644u;
    copy_name(fs->nodes[node_index].name, leaf_name);
  }

  if (fs->nodes[node_index].type == RUNTIME_FILE_TYPE_DIRECTORY) {
    if ((flags & RUNTIME_FILE_OPEN_WRITE) != 0u) {
      return RUNTIME_SYSCALL_STATUS_EISDIR;
    }
  } else if (fs->nodes[node_index].type != RUNTIME_FILE_TYPE_REGULAR) {
    return RUNTIME_SYSCALL_STATUS_ENOTSUP;
  }

  if (((flags & RUNTIME_FILE_OPEN_WRITE) != 0u) && ((fs->nodes[node_index].mode & 0222u) == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }
  if (((flags & RUNTIME_FILE_OPEN_READ) != 0u) && ((fs->nodes[node_index].mode & 0444u) == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  if (((flags & RUNTIME_FILE_OPEN_TRUNCATE) != 0u) &&
      (fs->nodes[node_index].type == RUNTIME_FILE_TYPE_REGULAR)) {
    fs->nodes[node_index].size_bytes = 0u;
    fs_zero_range(&fs->nodes[node_index], 0u, RUNTIME_FS_FILE_CAPACITY);
  }

  if (((flags & RUNTIME_FILE_OPEN_WRITE) != 0u) && fs_node_has_open_writer(fs, (uint32_t)node_index)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  if (((flags & (RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_TRUNCATE)) != 0u) &&
      !fs_node_owner_allows(&fs->nodes[node_index], current_pid)) {
    return RUNTIME_SYSCALL_STATUS_EACCES;
  }

  *file_object = (uintptr_t)fs_open_slot_alloc(fs, (uint32_t)node_index, flags);
  if ((int32_t)*file_object < 0) {
    return (int32_t)*file_object;
  }

  fs->open_files[*file_object - 1u].owner_pid = current_pid;

  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_fs_read(struct runtime_fs *fs, uintptr_t file_object, void *buffer, uint32_t size) {
  struct runtime_fs_open_file *open_file;
  struct runtime_fs_node *node;
  uint32_t remaining;
  uint32_t count;
  uint32_t index;

  if ((fs == NULL) || (buffer == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  open_file = fs_open_file_lookup(fs, file_object);
  if ((open_file == NULL) || ((open_file->flags & RUNTIME_FILE_OPEN_READ) == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  node = &fs->nodes[open_file->node_index];
  if (node->type != RUNTIME_FILE_TYPE_REGULAR) {
    return (node->type == RUNTIME_FILE_TYPE_DIRECTORY) ? RUNTIME_SYSCALL_STATUS_EISDIR
                                                       : RUNTIME_SYSCALL_STATUS_ENOTSUP;
  }

  if (open_file->offset >= node->size_bytes) {
    return 0;
  }

  remaining = node->size_bytes - open_file->offset;
  count = size < remaining ? size : remaining;
  for (index = 0u; index < count; ++index) {
    ((uint8_t *)buffer)[index] = node->data[open_file->offset + index];
  }

  open_file->offset += count;
  return (int32_t)count;
}

int32_t runtime_fs_write(struct runtime_fs *fs, uintptr_t file_object, const void *buffer,
                         uint32_t size) {
  struct runtime_fs_open_file *open_file;
  struct runtime_fs_node *node;
  uint32_t index;
  uint32_t end_offset;

  if ((fs == NULL) || (buffer == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  open_file = fs_open_file_lookup(fs, file_object);
  if ((open_file == NULL) || ((open_file->flags & RUNTIME_FILE_OPEN_WRITE) == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  node = &fs->nodes[open_file->node_index];
  if (node->type != RUNTIME_FILE_TYPE_REGULAR) {
    return (node->type == RUNTIME_FILE_TYPE_DIRECTORY) ? RUNTIME_SYSCALL_STATUS_EISDIR
                                                       : RUNTIME_SYSCALL_STATUS_ENOTSUP;
  }

  end_offset = open_file->offset + size;
  if (end_offset > RUNTIME_FS_FILE_CAPACITY) {
    return RUNTIME_SYSCALL_STATUS_ENOSPC;
  }

  if (open_file->offset > node->size_bytes) {
    fs_zero_range(node, node->size_bytes, open_file->offset);
  }

  for (index = 0u; index < size; ++index) {
    node->data[open_file->offset + index] = ((const uint8_t *)buffer)[index];
  }

  open_file->offset = end_offset;
  if (end_offset > node->size_bytes) {
    node->size_bytes = end_offset;
  }

  return (int32_t)size;
}

int32_t runtime_fs_close(struct runtime_fs *fs, uintptr_t file_object) {
  struct runtime_fs_open_file *open_file;

  if (fs == NULL) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  open_file = fs_open_file_lookup(fs, file_object);
  if (open_file == NULL) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  open_file->used = 0u;
  open_file->node_index = 0u;
  open_file->owner_pid = 0u;
  open_file->flags = 0u;
  open_file->offset = 0u;
  open_file->dir_cursor = 0u;
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_fs_stat(struct runtime_fs *fs, uintptr_t file_object, struct runtime_file_stat *stat) {
  struct runtime_fs_open_file *open_file;
  struct runtime_fs_node *node;

  if ((fs == NULL) || (stat == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  open_file = fs_open_file_lookup(fs, file_object);
  if (open_file == NULL) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  node = &fs->nodes[open_file->node_index];
  stat->type = node->type;
  stat->mode = node->mode;
  stat->size_bytes = node->size_bytes;
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_fs_readdir(struct runtime_fs *fs, uintptr_t file_object, struct runtime_dir_entry *entry) {
  struct runtime_fs_open_file *open_file;
  struct runtime_fs_node *node;
  uint32_t index;

  if ((fs == NULL) || (entry == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  open_file = fs_open_file_lookup(fs, file_object);
  if ((open_file == NULL) || ((open_file->flags & RUNTIME_FILE_OPEN_READ) == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  node = &fs->nodes[open_file->node_index];
  if (node->type != RUNTIME_FILE_TYPE_DIRECTORY) {
    return (node->type == RUNTIME_FILE_TYPE_REGULAR) ? RUNTIME_SYSCALL_STATUS_ENOTDIR
                                                     : RUNTIME_SYSCALL_STATUS_ENOTSUP;
  }

  for (index = open_file->dir_cursor; index < RUNTIME_FS_MAX_NODES; ++index) {
    const struct runtime_fs_node *candidate = &fs->nodes[index];
    if (candidate->used && (candidate->parent_index == open_file->node_index)) {
      entry->type = candidate->type;
      copy_name(entry->name, candidate->name);
      open_file->dir_cursor = index + 1u;
      return 1;
    }
  }

  return 0;
}

int32_t runtime_fs_seek(struct runtime_fs *fs, uintptr_t file_object, int32_t offset, uint32_t whence) {
  struct runtime_fs_open_file *open_file;
  struct runtime_fs_node *node;
  int64_t base = 0;
  int64_t next_offset;

  if (fs == NULL) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  open_file = fs_open_file_lookup(fs, file_object);
  if (open_file == NULL) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  node = &fs->nodes[open_file->node_index];
  if (node->type != RUNTIME_FILE_TYPE_REGULAR) {
    return (node->type == RUNTIME_FILE_TYPE_DIRECTORY) ? RUNTIME_SYSCALL_STATUS_EISDIR
                                                       : RUNTIME_SYSCALL_STATUS_ENOTSUP;
  }

  switch (whence) {
    case RUNTIME_FILE_SEEK_SET:
      base = 0;
      break;
    case RUNTIME_FILE_SEEK_CUR:
      base = (int64_t)open_file->offset;
      break;
    case RUNTIME_FILE_SEEK_END:
      base = (int64_t)node->size_bytes;
      break;
    default:
      return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  next_offset = base + (int64_t)offset;
  if ((next_offset < 0) || (next_offset > (int64_t)RUNTIME_FS_FILE_CAPACITY) ||
      (next_offset > (int64_t)INT32_MAX)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  open_file->offset = (uint32_t)next_offset;
  return (int32_t)open_file->offset;
}

int32_t runtime_fs_remove(struct runtime_fs *fs, uint32_t current_pid, const char *path) {
  int node_index;

  if ((fs == NULL) || (path == NULL) || (path[0] != '/')) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (fs_path_is_root(path)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  {
    int32_t status = fs_resolve_existing_path(fs, path, &node_index);
    if (status != RUNTIME_SYSCALL_STATUS_OK) {
      return status;
    }
  }

  if (!fs_node_owner_allows(&fs->nodes[node_index], current_pid) ||
      !fs_node_owner_allows(&fs->nodes[fs->nodes[node_index].parent_index], current_pid)) {
    return RUNTIME_SYSCALL_STATUS_EACCES;
  }

  if (fs_node_is_open(fs, (uint32_t)node_index)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  if ((fs->nodes[node_index].type == RUNTIME_FILE_TYPE_DIRECTORY) &&
      fs_node_has_children(fs, (uint32_t)node_index)) {
    return RUNTIME_SYSCALL_STATUS_ENOTEMPTY;
  }

  fs_clear_node(&fs->nodes[node_index]);
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_fs_rename(struct runtime_fs *fs, uint32_t current_pid, const char *old_path,
                          const char *new_path) {
  char leaf_name[RUNTIME_FS_NAME_CAPACITY];
  int node_index;
  int parent_index;
  int existing_index;

  if ((fs == NULL) || (old_path == NULL) || (new_path == NULL) || (old_path[0] != '/') ||
      (new_path[0] != '/')) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (name_equals(old_path, new_path)) {
    return RUNTIME_SYSCALL_STATUS_OK;
  }

  if (fs_path_is_root(old_path) || fs_path_is_root(new_path)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  {
    int32_t status = fs_resolve_existing_path(fs, old_path, &node_index);
    if (status != RUNTIME_SYSCALL_STATUS_OK) {
      return status;
    }
  }

  existing_index = fs_resolve_path(fs, new_path);
  if (existing_index >= 0) {
    return RUNTIME_SYSCALL_STATUS_EEXIST;
  }

  {
    int32_t parent_status = fs_resolve_parent_path(fs, new_path, leaf_name, &parent_index);
    if (parent_status != RUNTIME_SYSCALL_STATUS_OK) {
      return parent_status;
    }
  }

  if (!fs_node_owner_allows(&fs->nodes[node_index], current_pid) ||
      !fs_node_owner_allows(&fs->nodes[fs->nodes[node_index].parent_index], current_pid) ||
      !fs_node_owner_allows(&fs->nodes[parent_index], current_pid)) {
    return RUNTIME_SYSCALL_STATUS_EACCES;
  }

  if ((fs->nodes[node_index].type == RUNTIME_FILE_TYPE_DIRECTORY) &&
      fs_parent_chain_contains(fs, (uint32_t)parent_index, (uint32_t)node_index)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  fs->nodes[node_index].parent_index = (uint32_t)parent_index;
  copy_name(fs->nodes[node_index].name, leaf_name);
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_fs_validate(const struct runtime_fs *fs) {
  uint32_t index;
  uint32_t other;

  g_runtime_fs_validate_diag_code = 0u;
  g_runtime_fs_validate_diag_arg0 = 0u;
  g_runtime_fs_validate_diag_arg1 = 0u;
#if RUNTIME_FS_HAS_SOC_DEBUG
  runtime_fs_debug_write(RTC_CNTL_STORE1_REG, 0xD000u);
  runtime_fs_debug_write(RTC_CNTL_STORE2_REG, 0u);
  runtime_fs_debug_write(RTC_CNTL_STORE3_REG, 0u);
#endif

  if (fs == NULL) {
    return runtime_fs_validate_fail(0xD000u, 0u, 0u, RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  if ((fs->nodes[0].used == 0u) || (fs->nodes[0].type != RUNTIME_FILE_TYPE_DIRECTORY) ||
      (fs->nodes[0].parent_index != 0u) || (fs->nodes[0].name[0] != '/') ||
      (fs->nodes[0].name[1] != '\0')) {
    return runtime_fs_validate_fail(0xD001u, fs->nodes[0].used,
                                    (fs->nodes[0].type & 0xFFu) |
                                        ((fs->nodes[0].parent_index & 0xFFu) << 8) |
                                        (((uint32_t)(uint8_t)fs->nodes[0].name[0]) << 16) |
                                        (((uint32_t)(uint8_t)fs->nodes[0].name[1]) << 24),
                                    RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  for (index = 1u; index < RUNTIME_FS_MAX_NODES; ++index) {
    const struct runtime_fs_node *node = &fs->nodes[index];
    uint32_t current;
    uint32_t depth = 0u;

    if (node->used == 0u) {
      continue;
    }

    if ((node->type != RUNTIME_FILE_TYPE_DIRECTORY) && (node->type != RUNTIME_FILE_TYPE_REGULAR)) {
      return runtime_fs_validate_fail(0xD100u | index, node->type, node->used, RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (fs_name_is_empty(node->name)) {
      return runtime_fs_validate_fail(0xD200u | index, index, node->parent_index,
                                      RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (node->parent_index >= RUNTIME_FS_MAX_NODES) {
      return runtime_fs_validate_fail(0xD300u | index, node->parent_index, RUNTIME_FS_MAX_NODES,
                                      RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((fs->nodes[node->parent_index].used == 0u) ||
        (fs->nodes[node->parent_index].type != RUNTIME_FILE_TYPE_DIRECTORY)) {
      return runtime_fs_validate_fail(0xD400u | index, node->parent_index,
                                      (fs->nodes[node->parent_index].used & 0xFFFFu) |
                                          ((fs->nodes[node->parent_index].type & 0xFFFFu) << 16),
                                      RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (node->size_bytes > RUNTIME_FS_FILE_CAPACITY) {
      return runtime_fs_validate_fail(0xD500u | index, node->size_bytes, RUNTIME_FS_FILE_CAPACITY,
                                      RUNTIME_SYSCALL_STATUS_EINVAL);
    }

    current = index;
    while (current != 0u) {
      current = fs->nodes[current].parent_index;
      ++depth;
      if (depth > RUNTIME_FS_MAX_NODES) {
        return runtime_fs_validate_fail(0xD600u | index, current, depth, RUNTIME_SYSCALL_STATUS_EINVAL);
      }
      if (fs->nodes[current].used == 0u) {
        return runtime_fs_validate_fail(0xD700u | index, current, fs->nodes[current].parent_index,
                                        RUNTIME_SYSCALL_STATUS_EINVAL);
      }
    }

    for (other = index + 1u; other < RUNTIME_FS_MAX_NODES; ++other) {
      const struct runtime_fs_node *peer = &fs->nodes[other];
      if (peer->used && (peer->parent_index == node->parent_index) &&
          name_equals(peer->name, node->name)) {
        return runtime_fs_validate_fail(0xD800u | index, other,
                                        ((uint32_t)(uint8_t)node->name[0]) |
                                            (((uint32_t)(uint8_t)node->name[1]) << 8),
                                        RUNTIME_SYSCALL_STATUS_EEXIST);
      }
    }
  }

  for (index = 0u; index < RUNTIME_FS_MAX_OPEN_FILES; ++index) {
    const struct runtime_fs_open_file *open_file = &fs->open_files[index];
    const struct runtime_fs_node *node;

    if (open_file->used == 0u) {
      continue;
    }
    if (open_file->node_index >= RUNTIME_FS_MAX_NODES) {
      return runtime_fs_validate_fail(0xD900u | index, open_file->node_index, RUNTIME_FS_MAX_NODES,
                                      RUNTIME_SYSCALL_STATUS_EBADF);
    }
    node = &fs->nodes[open_file->node_index];
    if (node->used == 0u) {
      return runtime_fs_validate_fail(0xDA00u | index, open_file->node_index, node->used,
                                      RUNTIME_SYSCALL_STATUS_EBADF);
    }
    if ((node->type == RUNTIME_FILE_TYPE_REGULAR) && (open_file->offset > RUNTIME_FS_FILE_CAPACITY)) {
      return runtime_fs_validate_fail(0xDB00u | index, open_file->offset, RUNTIME_FS_FILE_CAPACITY,
                                      RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((node->type == RUNTIME_FILE_TYPE_DIRECTORY) && ((open_file->flags & RUNTIME_FILE_OPEN_WRITE) != 0u)) {
      return runtime_fs_validate_fail(0xDC00u | index, open_file->flags, node->type,
                                      RUNTIME_SYSCALL_STATUS_EISDIR);
    }
  }

  g_runtime_fs_validate_diag_code = 0xDFFFu;
#if RUNTIME_FS_HAS_SOC_DEBUG
  runtime_fs_debug_write(RTC_CNTL_STORE1_REG, 0xDFFFu);
#endif
  return RUNTIME_SYSCALL_STATUS_OK;
}
