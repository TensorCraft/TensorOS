#include "runtime_fs.h"
#include "runtime_fs_image.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ORACLE_HANDLE_CAPACITY 16u
#define ORACLE_DIR_ENTRY_CAPACITY 16u
#define ORACLE_ROUNDS 128u

struct runtime_backend {
  uint32_t current_pid;
  struct runtime_fs fs;
  struct runtime_syscall_table table;
};

struct host_open_handle {
  int used;
  int fd;
  DIR *dir;
  int is_dir;
  char path[PATH_MAX];
};

struct host_backend {
  char root[PATH_MAX];
  struct host_open_handle handles[ORACLE_HANDLE_CAPACITY];
};

struct oracle_dir_list {
  uint32_t count;
  struct runtime_dir_entry entries[ORACLE_DIR_ENTRY_CAPACITY];
};

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_fs_oracle_host_test failed: %s\n", message);
    exit(1);
  }
}

static int string_has_prefix(const char *value, const char *prefix) {
  uint32_t index = 0u;

  while (prefix[index] != '\0') {
    if (value[index] != prefix[index]) {
      return 0;
    }
    ++index;
  }

  return RUNTIME_SYSCALL_STATUS_OK;
}

static void copy_string(char *dest, uint32_t capacity, const char *src) {
  uint32_t index = 0u;

  while ((src[index] != '\0') && (index + 1u < capacity)) {
    dest[index] = src[index];
    ++index;
  }

  dest[index] = '\0';
}

static void join_host_path(char *buffer, uint32_t capacity, const char *root, const char *path) {
  uint32_t index = 0u;
  uint32_t cursor = 0u;

  while ((root[index] != '\0') && (cursor + 1u < capacity)) {
    buffer[cursor++] = root[index++];
  }

  index = 0u;
  while ((path[index] != '\0') && (cursor + 1u < capacity)) {
    buffer[cursor++] = path[index++];
  }

  buffer[cursor] = '\0';
}

static uint32_t host_process_current_pid(void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return backend->current_pid;
}

static int32_t runtime_backend_open(const char *path, uint32_t flags, uintptr_t *file_object,
                                    void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_open(&backend->fs, backend->current_pid, path, flags, file_object);
}

static int32_t runtime_backend_read(uintptr_t file_object, void *buffer, uint32_t size,
                                    void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_read(&backend->fs, file_object, buffer, size);
}

static int32_t runtime_backend_write(uintptr_t file_object, const void *buffer, uint32_t size,
                                     void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_write(&backend->fs, file_object, buffer, size);
}

static int32_t runtime_backend_close(uintptr_t file_object, void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_close(&backend->fs, file_object);
}

static int32_t runtime_backend_stat(uintptr_t file_object, struct runtime_file_stat *stat,
                                    void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_stat(&backend->fs, file_object, stat);
}

static int32_t runtime_backend_readdir(uintptr_t file_object, struct runtime_dir_entry *entry,
                                       void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_readdir(&backend->fs, file_object, entry);
}

static int32_t runtime_backend_seek(uintptr_t file_object, int32_t offset, uint32_t whence,
                                    void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_seek(&backend->fs, file_object, offset, whence);
}

static int32_t runtime_backend_remove(const char *path, void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_remove(&backend->fs, backend->current_pid, path);
}

static int32_t runtime_backend_rename(const char *old_path, const char *new_path, void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_rename(&backend->fs, backend->current_pid, old_path, new_path);
}

static int32_t runtime_backend_mkdir(const char *path, uint32_t mode, void *context) {
  struct runtime_backend *backend = (struct runtime_backend *)context;

  return runtime_fs_mkdir(&backend->fs, backend->current_pid, path, mode);
}

static int32_t runtime_dispatch(struct runtime_backend *backend, uint32_t number,
                                struct runtime_syscall_args *args,
                                const struct runtime_syscall_ops *ops) {
  return runtime_syscall_dispatch(number, args, ops, &backend->table, backend);
}

static int path_is_root(const char *path) {
  return (path[0] == '/') && (path[1] == '\0');
}

static int resolve_host_parent(const char *path, char *parent, uint32_t parent_capacity,
                               char *leaf, uint32_t leaf_capacity) {
  const char *slash = strrchr(path, '/');
  uint32_t parent_length;
  uint32_t leaf_length;

  if ((path == NULL) || (path[0] != '/') || path_is_root(path) || (slash == NULL) ||
      (slash[1] == '\0')) {
    return 0;
  }

  parent_length = (uint32_t)(slash - path);
  if (parent_length == 0u) {
    if (parent_capacity < 2u) {
      return 0;
    }
    parent[0] = '/';
    parent[1] = '\0';
  } else {
    if (parent_length + 1u >= parent_capacity) {
      return 0;
    }
    memcpy(parent, path, parent_length);
    parent[parent_length] = '\0';
  }

  leaf_length = (uint32_t)strlen(slash + 1);
  if (leaf_length + 1u >= leaf_capacity) {
    return 0;
  }
  memcpy(leaf, slash + 1, leaf_length + 1u);
  return 1;
}

static int32_t host_status_from_errno(int err) {
  switch (err) {
    case ENOENT:
      return RUNTIME_SYSCALL_STATUS_ENOENT;
    case EEXIST:
      return RUNTIME_SYSCALL_STATUS_EEXIST;
    case ENOTDIR:
      return RUNTIME_SYSCALL_STATUS_ENOTDIR;
    case EISDIR:
      return RUNTIME_SYSCALL_STATUS_EISDIR;
    case ENOTEMPTY:
      return RUNTIME_SYSCALL_STATUS_ENOTEMPTY;
    case ENOSPC:
      return RUNTIME_SYSCALL_STATUS_ENOSPC;
    case EBUSY:
      return RUNTIME_SYSCALL_STATUS_EBUSY;
    case EINVAL:
      return RUNTIME_SYSCALL_STATUS_EINVAL;
    default:
      return RUNTIME_SYSCALL_STATUS_EBADF;
  }
}

static void host_backend_init(struct host_backend *backend, const char *root) {
  uint32_t index;

  copy_string(backend->root, (uint32_t)sizeof(backend->root), root);
  for (index = 0u; index < ORACLE_HANDLE_CAPACITY; ++index) {
    backend->handles[index].used = 0;
    backend->handles[index].fd = -1;
    backend->handles[index].dir = NULL;
    backend->handles[index].is_dir = 0;
    backend->handles[index].path[0] = '\0';
  }
}

static int remove_tree_recursive(const char *path) {
  DIR *dir = opendir(path);
  struct dirent *entry;

  if (dir == NULL) {
    return 0;
  }

  while ((entry = readdir(dir)) != NULL) {
    char child[PATH_MAX];
    struct stat st;

    if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
      continue;
    }

    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    if (lstat(child, &st) != 0) {
      closedir(dir);
      return 0;
    }
    if (S_ISDIR(st.st_mode)) {
      if (!remove_tree_recursive(child) || (rmdir(child) != 0)) {
        closedir(dir);
        return 0;
      }
    } else if (unlink(child) != 0) {
      closedir(dir);
      return 0;
    }
  }

  closedir(dir);
  return 1;
}

static void host_backend_reset(struct host_backend *backend) {
  uint32_t index;

  expect(remove_tree_recursive(backend->root) != 0, "remove temp tree contents");
  for (index = 0u; index < ORACLE_HANDLE_CAPACITY; ++index) {
    backend->handles[index].used = 0;
    backend->handles[index].fd = -1;
    backend->handles[index].dir = NULL;
    backend->handles[index].is_dir = 0;
    backend->handles[index].path[0] = '\0';
  }
}

static void runtime_backend_checkpoint_roundtrip(struct runtime_backend *backend, uint8_t *image_buffer) {
  struct runtime_fs_image_layout layout = {0};
  uint32_t image_bytes = 0u;

  expect(runtime_fs_image_export(&backend->fs, image_buffer, RUNTIME_FS_IMAGE_MAX_BYTES,
                                 &image_bytes, &layout) == RUNTIME_SYSCALL_STATUS_OK,
         "oracle export image");
  expect(runtime_fs_image_validate(image_buffer, image_bytes, &layout) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "oracle validate image");
  expect(runtime_fs_image_import(&backend->fs, image_buffer, image_bytes, &layout) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "oracle import image");
  expect(runtime_fs_validate(&backend->fs) == RUNTIME_SYSCALL_STATUS_OK,
         "oracle fs validates after image roundtrip");
}

static int32_t host_mkdir(struct host_backend *backend, const char *path, uint32_t mode) {
  char full_path[PATH_MAX];
  char parent_path[PATH_MAX];
  char leaf[64];
  char full_parent[PATH_MAX];
  struct stat st;

  (void)mode;
  if ((path == NULL) || (path[0] != '/') || path_is_root(path)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  join_host_path(full_path, (uint32_t)sizeof(full_path), backend->root, path);
  if (lstat(full_path, &st) == 0) {
    return RUNTIME_SYSCALL_STATUS_EEXIST;
  }

  if (!resolve_host_parent(path, parent_path, (uint32_t)sizeof(parent_path), leaf,
                           (uint32_t)sizeof(leaf))) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  join_host_path(full_parent, (uint32_t)sizeof(full_parent), backend->root, parent_path);
  if (lstat(full_parent, &st) != 0) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }
  if (!S_ISDIR(st.st_mode)) {
    return RUNTIME_SYSCALL_STATUS_ENOTDIR;
  }

  if (mkdir(full_path, 0755) != 0) {
    return host_status_from_errno(errno);
  }

  return RUNTIME_SYSCALL_STATUS_OK;
}

static int host_handle_alloc(struct host_backend *backend) {
  uint32_t index;

  for (index = 0u; index < ORACLE_HANDLE_CAPACITY; ++index) {
    if (!backend->handles[index].used) {
      backend->handles[index].used = 1;
      backend->handles[index].fd = -1;
      backend->handles[index].dir = NULL;
      backend->handles[index].is_dir = 0;
      backend->handles[index].path[0] = '\0';
      return (int)(index + 1u);
    }
  }

  return 0;
}

static struct host_open_handle *host_handle_lookup(struct host_backend *backend, uintptr_t handle_id) {
  if ((handle_id == 0u) || (handle_id > ORACLE_HANDLE_CAPACITY)) {
    return NULL;
  }

  if (!backend->handles[handle_id - 1u].used) {
    return NULL;
  }

  return &backend->handles[handle_id - 1u];
}

static int host_path_is_open(const struct host_backend *backend, const char *path) {
  uint32_t index;

  for (index = 0u; index < ORACLE_HANDLE_CAPACITY; ++index) {
    if (backend->handles[index].used && (strcmp(backend->handles[index].path, path) == 0)) {
      return 1;
    }
  }

  return 0;
}

static int32_t host_open(struct host_backend *backend, const char *path, uint32_t flags,
                         uintptr_t *file_object) {
  char full_path[PATH_MAX];
  char parent_path[PATH_MAX];
  char leaf[64];
  char full_parent[PATH_MAX];
  struct stat st;
  int handle_id;

  if ((path == NULL) || (file_object == NULL) || (path[0] != '/')) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  join_host_path(full_path, (uint32_t)sizeof(full_path), backend->root, path);
  if (lstat(full_path, &st) != 0) {
    if ((flags & RUNTIME_FILE_OPEN_CREATE) == 0u) {
      return RUNTIME_SYSCALL_STATUS_ENOENT;
    }

    if (!resolve_host_parent(path, parent_path, (uint32_t)sizeof(parent_path), leaf,
                             (uint32_t)sizeof(leaf))) {
      return RUNTIME_SYSCALL_STATUS_EINVAL;
    }
    join_host_path(full_parent, (uint32_t)sizeof(full_parent), backend->root, parent_path);
    if (lstat(full_parent, &st) != 0) {
      return RUNTIME_SYSCALL_STATUS_ENOENT;
    }
    if (!S_ISDIR(st.st_mode)) {
      return RUNTIME_SYSCALL_STATUS_ENOTDIR;
    }

    {
      int fd = open(full_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
      if (fd < 0) {
        return host_status_from_errno(errno);
      }
      close(fd);
    }

    if (lstat(full_path, &st) != 0) {
      return host_status_from_errno(errno);
    }
  }

  handle_id = host_handle_alloc(backend);
  if (handle_id == 0) {
    return RUNTIME_SYSCALL_STATUS_ENOSPC;
  }

  copy_string(backend->handles[handle_id - 1u].path,
              (uint32_t)sizeof(backend->handles[handle_id - 1u].path), path);

  if (S_ISDIR(st.st_mode)) {
    if ((flags & RUNTIME_FILE_OPEN_WRITE) != 0u) {
      backend->handles[handle_id - 1u].used = 0;
      return RUNTIME_SYSCALL_STATUS_EISDIR;
    }
    backend->handles[handle_id - 1u].dir = opendir(full_path);
    if (backend->handles[handle_id - 1u].dir == NULL) {
      backend->handles[handle_id - 1u].used = 0;
      return host_status_from_errno(errno);
    }
    backend->handles[handle_id - 1u].is_dir = 1;
  } else if (S_ISREG(st.st_mode)) {
    int open_flags = ((flags & RUNTIME_FILE_OPEN_WRITE) != 0u) ? O_RDWR : O_RDONLY;
    if ((flags & RUNTIME_FILE_OPEN_TRUNCATE) != 0u) {
      open_flags |= O_TRUNC;
    }

    backend->handles[handle_id - 1u].fd = open(full_path, open_flags);
    if (backend->handles[handle_id - 1u].fd < 0) {
      backend->handles[handle_id - 1u].used = 0;
      return host_status_from_errno(errno);
    }
  } else {
    backend->handles[handle_id - 1u].used = 0;
    return RUNTIME_SYSCALL_STATUS_ENOTSUP;
  }

  *file_object = (uintptr_t)handle_id;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_close(struct host_backend *backend, uintptr_t file_object) {
  struct host_open_handle *handle = host_handle_lookup(backend, file_object);

  if (handle == NULL) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  if (handle->is_dir) {
    closedir(handle->dir);
  } else {
    close(handle->fd);
  }

  handle->used = 0;
  handle->fd = -1;
  handle->dir = NULL;
  handle->is_dir = 0;
  handle->path[0] = '\0';
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_seek(struct host_backend *backend, uintptr_t file_object, int32_t offset,
                         uint32_t whence) {
  struct host_open_handle *handle = host_handle_lookup(backend, file_object);
  int seek_whence;
  off_t result;

  if (handle == NULL) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }
  if (handle->is_dir) {
    return RUNTIME_SYSCALL_STATUS_EISDIR;
  }

  switch (whence) {
    case RUNTIME_FILE_SEEK_SET:
      seek_whence = SEEK_SET;
      break;
    case RUNTIME_FILE_SEEK_CUR:
      seek_whence = SEEK_CUR;
      break;
    case RUNTIME_FILE_SEEK_END:
      seek_whence = SEEK_END;
      break;
    default:
      return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  result = lseek(handle->fd, (off_t)offset, seek_whence);
  if (result < 0) {
    return host_status_from_errno(errno);
  }
  if (result > RUNTIME_FS_FILE_CAPACITY) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  return (int32_t)result;
}

static int32_t host_read(struct host_backend *backend, uintptr_t file_object, void *buffer,
                         uint32_t size) {
  struct host_open_handle *handle = host_handle_lookup(backend, file_object);
  ssize_t count;

  if ((handle == NULL) || (buffer == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }
  if (handle->is_dir) {
    return RUNTIME_SYSCALL_STATUS_EISDIR;
  }

  count = read(handle->fd, buffer, size);
  if (count < 0) {
    return host_status_from_errno(errno);
  }

  return (int32_t)count;
}

static int32_t host_write(struct host_backend *backend, uintptr_t file_object, const void *buffer,
                          uint32_t size) {
  struct host_open_handle *handle = host_handle_lookup(backend, file_object);
  ssize_t count;
  off_t current_offset;

  if ((handle == NULL) || (buffer == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }
  if (handle->is_dir) {
    return RUNTIME_SYSCALL_STATUS_EISDIR;
  }

  current_offset = lseek(handle->fd, 0, SEEK_CUR);
  if ((current_offset < 0) || ((uint32_t)current_offset + size > RUNTIME_FS_FILE_CAPACITY)) {
    return RUNTIME_SYSCALL_STATUS_ENOSPC;
  }

  count = write(handle->fd, buffer, size);
  if (count < 0) {
    return host_status_from_errno(errno);
  }

  return (int32_t)count;
}

static int32_t host_stat(struct host_backend *backend, uintptr_t file_object,
                         struct runtime_file_stat *stat_out) {
  struct host_open_handle *handle = host_handle_lookup(backend, file_object);
  struct stat st;
  char full_path[PATH_MAX];

  if ((handle == NULL) || (stat_out == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  if (handle->is_dir) {
    join_host_path(full_path, (uint32_t)sizeof(full_path), backend->root, handle->path);
    if (stat(full_path, &st) != 0) {
      return host_status_from_errno(errno);
    }
  } else if (fstat(handle->fd, &st) != 0) {
    return host_status_from_errno(errno);
  }

  stat_out->type = handle->is_dir ? RUNTIME_FILE_TYPE_DIRECTORY : RUNTIME_FILE_TYPE_REGULAR;
  stat_out->mode = (uint32_t)(st.st_mode & 0777u);
  stat_out->size_bytes = handle->is_dir ? 0u : (uint32_t)st.st_size;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_readdir(struct host_backend *backend, uintptr_t file_object,
                            struct runtime_dir_entry *entry_out) {
  struct host_open_handle *handle = host_handle_lookup(backend, file_object);
  struct dirent *entry;

  (void)backend;
  if ((handle == NULL) || (entry_out == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }
  if (!handle->is_dir) {
    return RUNTIME_SYSCALL_STATUS_ENOTDIR;
  }

  for (;;) {
    entry = readdir(handle->dir);
    if (entry == NULL) {
      return 0;
    }
    if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
      break;
    }
  }

  entry_out->type = entry->d_type == DT_DIR ? RUNTIME_FILE_TYPE_DIRECTORY : RUNTIME_FILE_TYPE_REGULAR;
  copy_string(entry_out->name, (uint32_t)sizeof(entry_out->name), entry->d_name);
  return 1;
}

static void host_update_renamed_paths(struct host_backend *backend, const char *old_path,
                                      const char *new_path) {
  uint32_t index;
  uint32_t old_length = (uint32_t)strlen(old_path);

  for (index = 0u; index < ORACLE_HANDLE_CAPACITY; ++index) {
    struct host_open_handle *handle = &backend->handles[index];
    if (!handle->used) {
      continue;
    }

    if (strcmp(handle->path, old_path) == 0) {
      copy_string(handle->path, (uint32_t)sizeof(handle->path), new_path);
      continue;
    }

    if (string_has_prefix(handle->path, old_path) && (handle->path[old_length] == '/')) {
      char updated[PATH_MAX];
      snprintf(updated, sizeof(updated), "%s%s", new_path, handle->path + old_length);
      copy_string(handle->path, (uint32_t)sizeof(handle->path), updated);
    }
  }
}

static int path_is_descendant(const char *path, const char *ancestor) {
  uint32_t ancestor_length = (uint32_t)strlen(ancestor);

  return string_has_prefix(path, ancestor) && (path[ancestor_length] == '/');
}

static int32_t host_remove(struct host_backend *backend, const char *path) {
  char full_path[PATH_MAX];
  struct stat st;

  if ((path == NULL) || (path[0] != '/')) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }
  if (path_is_root(path)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }
  if (host_path_is_open(backend, path)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  join_host_path(full_path, (uint32_t)sizeof(full_path), backend->root, path);
  if (lstat(full_path, &st) != 0) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  if (S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(full_path);
    struct dirent *entry;
    if (dir == NULL) {
      return host_status_from_errno(errno);
    }
    while ((entry = readdir(dir)) != NULL) {
      if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
        closedir(dir);
        return RUNTIME_SYSCALL_STATUS_ENOTEMPTY;
      }
    }
    closedir(dir);
    if (rmdir(full_path) != 0) {
      return host_status_from_errno(errno);
    }
  } else if (unlink(full_path) != 0) {
    return host_status_from_errno(errno);
  }

  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_rename(struct host_backend *backend, const char *old_path, const char *new_path) {
  char old_full[PATH_MAX];
  char new_full[PATH_MAX];
  char parent_path[PATH_MAX];
  char leaf[64];
  char full_parent[PATH_MAX];
  struct stat st;

  if ((old_path == NULL) || (new_path == NULL) || (old_path[0] != '/') || (new_path[0] != '/')) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }
  if (strcmp(old_path, new_path) == 0) {
    return RUNTIME_SYSCALL_STATUS_OK;
  }
  if (path_is_root(old_path) || path_is_root(new_path)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  join_host_path(old_full, (uint32_t)sizeof(old_full), backend->root, old_path);
  join_host_path(new_full, (uint32_t)sizeof(new_full), backend->root, new_path);

  if (lstat(old_full, &st) != 0) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }
  if (lstat(new_full, &st) == 0) {
    return RUNTIME_SYSCALL_STATUS_EEXIST;
  }
  if (!resolve_host_parent(new_path, parent_path, (uint32_t)sizeof(parent_path), leaf,
                           (uint32_t)sizeof(leaf))) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }
  join_host_path(full_parent, (uint32_t)sizeof(full_parent), backend->root, parent_path);
  if (lstat(full_parent, &st) != 0) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }
  if (!S_ISDIR(st.st_mode)) {
    return RUNTIME_SYSCALL_STATUS_ENOTDIR;
  }
  if (path_is_descendant(new_path, old_path)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (rename(old_full, new_full) != 0) {
    return host_status_from_errno(errno);
  }

  host_update_renamed_paths(backend, old_path, new_path);
  return RUNTIME_SYSCALL_STATUS_OK;
}

static void sort_dir_list(struct oracle_dir_list *list) {
  uint32_t outer;
  uint32_t inner;

  for (outer = 0u; outer < list->count; ++outer) {
    for (inner = outer + 1u; inner < list->count; ++inner) {
      if (strcmp(list->entries[outer].name, list->entries[inner].name) > 0) {
        struct runtime_dir_entry temp = list->entries[outer];
        list->entries[outer] = list->entries[inner];
        list->entries[inner] = temp;
      }
    }
  }
}

static int dir_list_has_entry(const struct oracle_dir_list *list, const char *name, uint32_t type) {
  uint32_t index;

  for (index = 0u; index < list->count; ++index) {
    if ((strcmp(list->entries[index].name, name) == 0) && (list->entries[index].type == type)) {
      return 1;
    }
  }

  return 0;
}

static void collect_runtime_dir(struct runtime_backend *backend, const struct runtime_syscall_ops *ops,
                                const char *path, struct oracle_dir_list *list) {
  struct runtime_syscall_args args = {0};
  int32_t result;
  runtime_handle_t handle;

  list->count = 0u;
  args.arg0 = (uintptr_t)path;
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  result = runtime_dispatch(backend, RUNTIME_SYSCALL_FILE_OPEN, &args, ops);
  expect(result > 0, "open runtime dir for collection");
  handle = (runtime_handle_t)result;

  for (;;) {
    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)&list->entries[list->count];
    result = runtime_dispatch(backend, RUNTIME_SYSCALL_FILE_READDIR, &args, ops);
    if (result == 0) {
      break;
    }
    expect(result == 1, "runtime readdir collection succeeds");
    ++list->count;
    expect(list->count < ORACLE_DIR_ENTRY_CAPACITY, "runtime dir capacity is sufficient");
  }

  args.arg0 = (uintptr_t)handle;
  expect(runtime_dispatch(backend, RUNTIME_SYSCALL_HANDLE_CLOSE, &args, ops) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "close runtime dir after collection");
  sort_dir_list(list);
}

static void collect_host_dir(struct host_backend *backend, const char *path, struct oracle_dir_list *list) {
  uintptr_t handle = 0u;
  int32_t result;

  list->count = 0u;
  result = host_open(backend, path, RUNTIME_FILE_OPEN_READ, &handle);
  expect(result == RUNTIME_SYSCALL_STATUS_OK, "open host dir for collection");

  for (;;) {
    result = host_readdir(backend, handle, &list->entries[list->count]);
    if (result == 0) {
      break;
    }
    expect(result == 1, "host readdir collection succeeds");
    ++list->count;
    expect(list->count < ORACLE_DIR_ENTRY_CAPACITY, "host dir capacity is sufficient");
  }

  expect(host_close(backend, handle) == RUNTIME_SYSCALL_STATUS_OK,
         "close host dir after collection");
  sort_dir_list(list);
}

static void fill_binary_payload(uint8_t *buffer, uint32_t size, uint32_t round, uint32_t seed) {
  uint32_t index;
  uint32_t value = 0x9E3779B9u ^ round ^ (seed * 0x45D9F3Bu);

  for (index = 0u; index < size; ++index) {
    value = (value * 1664525u) + 1013904223u;
    buffer[index] = (uint8_t)(value >> 24);
  }
}

int main(void) {
  struct runtime_backend runtime_backend = {.current_pid = 7u};
  struct host_backend host_backend;
  struct runtime_syscall_args args = {0};
  struct runtime_file_stat runtime_stat = {0};
  struct runtime_file_stat host_file_stat = {0};
  struct oracle_dir_list runtime_list;
  struct oracle_dir_list host_list;
  uint8_t image_buffer[RUNTIME_FS_IMAGE_MAX_BYTES];
  uint8_t payload_a[48];
  uint8_t payload_b[24];
  uint8_t payload_c[5];
  uint8_t read_runtime[64];
  uint8_t read_host[64];
  char temp_root_template[] = "/tmp/tensoros-fs-oracle-XXXXXX";
  const struct runtime_syscall_file_ops file_ops = {
      .open = runtime_backend_open,
      .read = runtime_backend_read,
      .write = runtime_backend_write,
      .close = runtime_backend_close,
      .stat = runtime_backend_stat,
      .readdir = runtime_backend_readdir,
      .seek = runtime_backend_seek,
      .remove = runtime_backend_remove,
      .rename = runtime_backend_rename,
  };
  const struct runtime_syscall_ops ops = {
      .process_current_pid = host_process_current_pid,
      .fs_mkdir = runtime_backend_mkdir,
      .file_ops = &file_ops,
  };
  char *temp_root = mkdtemp(temp_root_template);
  uint32_t round;

  expect(temp_root != NULL, "create oracle temp root");
  host_backend_init(&host_backend, temp_root);

  for (round = 0u; round < ORACLE_ROUNDS; ++round) {
    runtime_handle_t handle;
    int32_t runtime_status;
    int32_t host_status;
    uintptr_t host_handle;

    runtime_fs_init(&runtime_backend.fs);
    runtime_syscall_table_init(&runtime_backend.table);
    host_backend_reset(&host_backend);
    expect(runtime_fs_validate(&runtime_backend.fs) == RUNTIME_SYSCALL_STATUS_OK,
           "runtime fs validates after round reset");

    fill_binary_payload(payload_a, sizeof(payload_a), round, 1u);
    fill_binary_payload(payload_b, sizeof(payload_b), round, 2u);
    fill_binary_payload(payload_c, sizeof(payload_c), round, 3u);

    runtime_status = runtime_backend_mkdir("/stress", 0755u, &runtime_backend);
    host_status = host_mkdir(&host_backend, "/stress", 0755u);
    expect(runtime_status == host_status, "mkdir /stress status matches");

    runtime_status = runtime_backend_mkdir("/stress/logs", 0755u, &runtime_backend);
    host_status = host_mkdir(&host_backend, "/stress/logs", 0755u);
    expect(runtime_status == host_status, "mkdir /stress/logs status matches");
    expect(runtime_fs_validate(&runtime_backend.fs) == RUNTIME_SYSCALL_STATUS_OK,
           "runtime fs validates after mkdir bootstrap");

    args.arg0 = (uintptr_t)"/stress/a.bin";
    args.arg1 = RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_READ;
    runtime_status = runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_OPEN, &args, &ops);
    host_status = host_open(&host_backend, "/stress/a.bin",
                            RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_READ,
                            &host_handle);
    expect((runtime_status > 0) && (host_status == RUNTIME_SYSCALL_STATUS_OK),
           "open create a.bin on both backends");
    handle = (runtime_handle_t)runtime_status;

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)payload_a;
    args.arg2 = sizeof(payload_a);
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_WRITE, &args, &ops) ==
               (int32_t)sizeof(payload_a),
           "runtime write payload_a");
    expect(host_write(&host_backend, host_handle, payload_a, sizeof(payload_a)) ==
               (int32_t)sizeof(payload_a),
           "host write payload_a");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)-12;
    args.arg2 = RUNTIME_FILE_SEEK_END;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_SEEK, &args, &ops) ==
               (int32_t)(sizeof(payload_a) - 12u),
           "runtime seek near end");
    expect(host_seek(&host_backend, host_handle, -12, RUNTIME_FILE_SEEK_END) ==
               (int32_t)(sizeof(payload_a) - 12u),
           "host seek near end");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)payload_b;
    args.arg2 = sizeof(payload_b);
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_WRITE, &args, &ops) ==
               (int32_t)sizeof(payload_b),
           "runtime patch write");
    expect(host_write(&host_backend, host_handle, payload_b, sizeof(payload_b)) ==
               (int32_t)sizeof(payload_b),
           "host patch write");
    expect(runtime_fs_validate(&runtime_backend.fs) == RUNTIME_SYSCALL_STATUS_OK,
           "runtime fs validates after patch write");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = 0u;
    args.arg2 = RUNTIME_FILE_SEEK_SET;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_SEEK, &args, &ops) == 0,
           "runtime rewind file");
    expect(host_seek(&host_backend, host_handle, 0, RUNTIME_FILE_SEEK_SET) == 0,
           "host rewind file");

    memset(read_runtime, 0, sizeof(read_runtime));
    memset(read_host, 0, sizeof(read_host));
    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)read_runtime;
    args.arg2 = sizeof(read_runtime);
    runtime_status = runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_READ, &args, &ops);
    host_status = host_read(&host_backend, host_handle, read_host, sizeof(read_host));
    expect(runtime_status == host_status, "read sizes match");
    expect(memcmp(read_runtime, read_host, (uint32_t)runtime_status) == 0, "read payloads match");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)&runtime_stat;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_STAT, &args, &ops) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "runtime stat open file");
    expect(host_stat(&host_backend, host_handle, &host_file_stat) == RUNTIME_SYSCALL_STATUS_OK,
           "host stat open file");
    expect((runtime_stat.type == host_file_stat.type) &&
               (runtime_stat.size_bytes == host_file_stat.size_bytes),
           "stat matches after patch write");

    collect_runtime_dir(&runtime_backend, &ops, "/stress", &runtime_list);
    collect_host_dir(&host_backend, "/stress", &host_list);
    expect(runtime_list.count == host_list.count, "dir child count matches");
    expect(runtime_list.count == 2u, "stress dir contains file and logs directory");
    expect(dir_list_has_entry(&runtime_list, "a.bin", RUNTIME_FILE_TYPE_REGULAR) != 0,
           "runtime stress dir includes a.bin");
    expect(dir_list_has_entry(&runtime_list, "logs", RUNTIME_FILE_TYPE_DIRECTORY) != 0,
           "runtime stress dir includes logs");
    expect(dir_list_has_entry(&host_list, "a.bin", RUNTIME_FILE_TYPE_REGULAR) != 0,
           "host stress dir includes a.bin");
    expect(dir_list_has_entry(&host_list, "logs", RUNTIME_FILE_TYPE_DIRECTORY) != 0,
           "host stress dir includes logs");

    runtime_status = runtime_backend_remove("/stress/a.bin", &runtime_backend);
    host_status = host_remove(&host_backend, "/stress/a.bin");
    expect(runtime_status == host_status, "remove busy file status matches");

    runtime_status = runtime_backend_rename("/stress/a.bin", "/stress/logs/b.bin", &runtime_backend);
    host_status = host_rename(&host_backend, "/stress/a.bin", "/stress/logs/b.bin");
    expect(runtime_status == host_status, "rename a.bin to b.bin matches");
    expect(runtime_fs_validate(&runtime_backend.fs) == RUNTIME_SYSCALL_STATUS_OK,
           "runtime fs validates after rename");

    args.arg0 = (uintptr_t)handle;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "runtime close renamed file");
    expect(host_close(&host_backend, host_handle) == RUNTIME_SYSCALL_STATUS_OK,
           "host close renamed file");
    runtime_backend_checkpoint_roundtrip(&runtime_backend, image_buffer);

    runtime_status = runtime_backend_remove("/stress/logs", &runtime_backend);
    host_status = host_remove(&host_backend, "/stress/logs");
    expect(runtime_status == host_status, "remove non-empty dir status matches");

    args.arg0 = (uintptr_t)"/stress/logs/b.bin";
    args.arg1 = RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_TRUNCATE;
    runtime_status = runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_OPEN, &args, &ops);
    host_status = host_open(&host_backend, "/stress/logs/b.bin",
                            RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE |
                                RUNTIME_FILE_OPEN_TRUNCATE,
                            &host_handle);
    expect((runtime_status > 0) && (host_status == RUNTIME_SYSCALL_STATUS_OK),
           "reopen truncated b.bin");
    handle = (runtime_handle_t)runtime_status;

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)&runtime_stat;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_STAT, &args, &ops) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "runtime stat truncated file");
    expect(host_stat(&host_backend, host_handle, &host_file_stat) == RUNTIME_SYSCALL_STATUS_OK,
           "host stat truncated file");
    expect((runtime_stat.size_bytes == 0u) && (host_file_stat.size_bytes == 0u),
           "truncate resets size on both backends");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = 12u;
    args.arg2 = RUNTIME_FILE_SEEK_SET;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_SEEK, &args, &ops) == 12,
           "runtime seek beyond eof after truncate");
    expect(host_seek(&host_backend, host_handle, 12, RUNTIME_FILE_SEEK_SET) == 12,
           "host seek beyond eof after truncate");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)payload_c;
    args.arg2 = sizeof(payload_c);
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_WRITE, &args, &ops) ==
               (int32_t)sizeof(payload_c),
           "runtime sparse tail write");
    expect(host_write(&host_backend, host_handle, payload_c, sizeof(payload_c)) ==
               (int32_t)sizeof(payload_c),
           "host sparse tail write");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = 0u;
    args.arg2 = RUNTIME_FILE_SEEK_SET;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_SEEK, &args, &ops) == 0,
           "runtime rewind sparse truncated file");
    expect(host_seek(&host_backend, host_handle, 0, RUNTIME_FILE_SEEK_SET) == 0,
           "host rewind sparse truncated file");

    memset(read_runtime, 0xFF, sizeof(read_runtime));
    memset(read_host, 0xFF, sizeof(read_host));
    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)read_runtime;
    args.arg2 = 17u;
    runtime_status = runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_FILE_READ, &args, &ops);
    host_status = host_read(&host_backend, host_handle, read_host, 17u);
    expect(runtime_status == 17, "runtime sparse read size");
    expect(host_status == 17, "host sparse read size");
    expect(memcmp(read_runtime, read_host, 17u) == 0, "sparse binary payload matches host oracle");
    expect((read_runtime[0] == 0u) && (read_runtime[1] == 0u) && (read_runtime[2] == 0u) &&
               (read_runtime[3] == 0u) && (read_runtime[4] == 0u) && (read_runtime[5] == 0u) &&
               (read_runtime[6] == 0u) && (read_runtime[7] == 0u) && (read_runtime[8] == 0u) &&
               (read_runtime[9] == 0u) && (read_runtime[10] == 0u) && (read_runtime[11] == 0u),
           "sparse prefix remains zero-filled after truncate");
    expect(memcmp(&read_runtime[12], payload_c, sizeof(payload_c)) == 0,
           "sparse tail bytes preserved after truncate");
    expect(runtime_fs_validate(&runtime_backend.fs) == RUNTIME_SYSCALL_STATUS_OK,
           "runtime fs validates after sparse truncate write");

    args.arg0 = (uintptr_t)handle;
    expect(runtime_dispatch(&runtime_backend, RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "runtime close truncated file");
    expect(host_close(&host_backend, host_handle) == RUNTIME_SYSCALL_STATUS_OK,
           "host close truncated file");
    runtime_backend_checkpoint_roundtrip(&runtime_backend, image_buffer);

    runtime_status = runtime_backend_remove("/stress/logs/b.bin", &runtime_backend);
    host_status = host_remove(&host_backend, "/stress/logs/b.bin");
    expect(runtime_status == host_status, "remove b.bin matches");

    runtime_status = runtime_backend_remove("/stress/logs", &runtime_backend);
    host_status = host_remove(&host_backend, "/stress/logs");
    expect(runtime_status == host_status, "remove logs dir matches");

    runtime_status = runtime_backend_open("/stress", RUNTIME_FILE_OPEN_READ, &args.arg0, &runtime_backend);
    host_status = host_open(&host_backend, "/stress", RUNTIME_FILE_OPEN_READ, &host_handle);
    expect(runtime_status == RUNTIME_SYSCALL_STATUS_OK, "runtime reopen stress dir before final remove");
    expect(host_status == RUNTIME_SYSCALL_STATUS_OK, "host reopen stress dir before final remove");
    expect(runtime_backend_remove("/stress", &runtime_backend) == RUNTIME_SYSCALL_STATUS_EBUSY,
           "runtime cannot remove open stress dir");
    expect(host_remove(&host_backend, "/stress") == RUNTIME_SYSCALL_STATUS_EBUSY,
           "host cannot remove open stress dir");
    expect(runtime_backend_close(args.arg0, &runtime_backend) == RUNTIME_SYSCALL_STATUS_OK,
           "runtime close stress dir");
    expect(host_close(&host_backend, host_handle) == RUNTIME_SYSCALL_STATUS_OK,
           "host close stress dir");

    runtime_status = runtime_backend_remove("/stress", &runtime_backend);
    host_status = host_remove(&host_backend, "/stress");
    expect(runtime_status == host_status, "remove stress dir matches");
    expect(runtime_fs_validate(&runtime_backend.fs) == RUNTIME_SYSCALL_STATUS_OK,
           "runtime fs validates after round cleanup");
    runtime_backend_checkpoint_roundtrip(&runtime_backend, image_buffer);

    runtime_status = runtime_backend_remove("/stress/missing.bin", &runtime_backend);
    host_status = host_remove(&host_backend, "/stress/missing.bin");
    expect(runtime_status == host_status, "remove missing path matches");
  }

  expect(remove_tree_recursive(host_backend.root) != 0, "cleanup oracle temp tree");
  expect(rmdir(host_backend.root) == 0, "remove oracle temp root");
  return 0;
}
