#include "runtime_fs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct host_context {
  uint32_t current_pid;
  struct runtime_fs fs;
};

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_fs_host_test failed: %s\n", message);
    exit(1);
  }
}

static uint32_t host_process_current_pid(void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return ctx->current_pid;
}

static int32_t host_fs_open(const char *path, uint32_t flags, uintptr_t *file_object,
                            void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_open(&ctx->fs, ctx->current_pid, path, flags, file_object);
}

static int32_t host_fs_read(uintptr_t file_object, void *buffer, uint32_t size, void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_read(&ctx->fs, file_object, buffer, size);
}

static int32_t host_fs_write(uintptr_t file_object, const void *buffer, uint32_t size,
                             void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_write(&ctx->fs, file_object, buffer, size);
}

static int32_t host_fs_close(uintptr_t file_object, void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_close(&ctx->fs, file_object);
}

static int32_t host_fs_stat(uintptr_t file_object, struct runtime_file_stat *stat, void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_stat(&ctx->fs, file_object, stat);
}

static int32_t host_fs_readdir(uintptr_t file_object, struct runtime_dir_entry *entry,
                               void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_readdir(&ctx->fs, file_object, entry);
}

static int32_t host_fs_seek(uintptr_t file_object, int32_t offset, uint32_t whence, void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_seek(&ctx->fs, file_object, offset, whence);
}

static int32_t host_fs_remove(const char *path, void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_remove(&ctx->fs, ctx->current_pid, path);
}

static int32_t host_fs_rename(const char *old_path, const char *new_path, void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_rename(&ctx->fs, ctx->current_pid, old_path, new_path);
}

static int32_t host_fs_mkdir(const char *path, uint32_t mode, void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_mkdir(&ctx->fs, ctx->current_pid, path, mode);
}

static int32_t dispatch(uint32_t number, struct runtime_syscall_args *args,
                        const struct runtime_syscall_ops *ops,
                        struct runtime_syscall_table *table, struct host_context *ctx) {
  return runtime_syscall_dispatch(number, args, ops, table, ctx);
}

static uint32_t collect_dir_names(struct runtime_syscall_args *args, const struct runtime_syscall_ops *ops,
                                  struct runtime_syscall_table *table, struct host_context *ctx,
                                  const char *path, struct runtime_dir_entry *entries,
                                  uint32_t capacity) {
  int32_t result;
  runtime_handle_t dir_handle;
  uint32_t count = 0u;

  args->arg0 = (uintptr_t)path;
  args->arg1 = RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, args, ops, table, ctx);
  expect(result > 0, "open directory for collection");
  dir_handle = (runtime_handle_t)result;

  for (;;) {
    expect(count < capacity, "directory entry collection capacity");
    args->arg0 = (uintptr_t)dir_handle;
    args->arg1 = (uintptr_t)&entries[count];
    result = dispatch(RUNTIME_SYSCALL_FILE_READDIR, args, ops, table, ctx);
    if (result == 0) {
      break;
    }
    expect(result == 1, "directory entry collection succeeds");
    ++count;
  }

  args->arg0 = (uintptr_t)dir_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, args, ops, table, ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close collected directory handle");
  return count;
}

static int dir_entries_have_name(const struct runtime_dir_entry *entries, uint32_t count,
                                 const char *name, uint32_t type) {
  uint32_t index;

  for (index = 0u; index < count; ++index) {
    if ((strcmp(entries[index].name, name) == 0) && (entries[index].type == type)) {
      return 1;
    }
  }

  return 0;
}

int main(void) {
  struct host_context ctx = {.current_pid = 5u};
  struct runtime_syscall_table table;
  struct runtime_syscall_args args = {0};
  uint8_t read_buffer[32] = {0};
  uint8_t sparse_read_buffer[16] = {0};
  struct runtime_dir_entry collected_entries[4] = {0};
  const char write_buffer[5] = {'h', 'e', 'l', 'l', 'o'};
  const uint8_t sparse_prefix[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
  const uint8_t sparse_tail[3] = {0xA5u, 0x00u, 0x5Au};
  const uint8_t truncated_tail[1] = {0x7Eu};
  struct runtime_file_stat stat = {0};
  struct runtime_dir_entry entry = {0};
  const struct runtime_syscall_file_ops file_ops = {
      .open = host_fs_open,
      .read = host_fs_read,
      .write = host_fs_write,
      .close = host_fs_close,
      .stat = host_fs_stat,
      .readdir = host_fs_readdir,
      .seek = host_fs_seek,
      .remove = host_fs_remove,
      .rename = host_fs_rename,
  };
  const struct runtime_syscall_ops ops = {
      .process_current_pid = host_process_current_pid,
      .fs_mkdir = host_fs_mkdir,
      .file_ops = &file_ops,
  };
  int32_t result;
  runtime_handle_t file_handle;
  runtime_handle_t second_file_handle;
  runtime_handle_t dir_handle;

  runtime_fs_init(&ctx.fs);
  runtime_syscall_table_init(&table);

  args.arg0 = (uintptr_t)"/apps";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "create /apps directory");

  args.arg0 = (uintptr_t)"/apps/cache";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "create nested directory");

  args.arg0 = (uintptr_t)"/apps";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EEXIST,
         "duplicate mkdir returns eexist");

  args.arg0 = (uintptr_t)"/missing/child";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "mkdir under missing parent returns enoent");

  args.arg0 = (uintptr_t)"/apps/ui.txt";
  args.arg1 = RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "open create file");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)write_buffer;
  args.arg2 = sizeof(write_buffer);
  expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) == 5,
         "write file contents");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)-2;
  args.arg2 = RUNTIME_FILE_SEEK_END;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 3,
         "seek from end returns new offset");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)"XY";
  args.arg2 = 2u;
  expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) == 2,
         "write after seek updates file");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)&stat;
  expect(dispatch(RUNTIME_SYSCALL_FILE_STAT, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "stat file");
  expect(stat.type == RUNTIME_FILE_TYPE_REGULAR, "regular file stat type");
  expect(stat.size_bytes == 5u, "regular file size preserved");

  args.arg0 = (uintptr_t)"/apps/ui.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EBUSY,
         "cannot remove open file");

  args.arg0 = (uintptr_t)"/apps/ui.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "open second read handle while writer is active");
  second_file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)"/apps/ui.txt";
  args.arg1 = RUNTIME_FILE_OPEN_WRITE;
  expect(dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EBUSY,
         "second writer is rejected while writer is active");

  args.arg0 = (uintptr_t)"/apps/ui.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE;
  expect(dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EBUSY,
         "read write reopen is rejected while writer is active");

  args.arg0 = (uintptr_t)"/apps/ui.txt";
  args.arg1 = (uintptr_t)"/apps/ui-renamed.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "rename open file preserves handle");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = 0u;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 0,
         "seek to start");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)read_buffer;
  args.arg2 = sizeof(read_buffer);
  expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == 5,
         "read renamed open file");
  expect(memcmp(read_buffer, "helXY", 5u) == 0, "renamed open file keeps contents");

  args.arg0 = (uintptr_t)second_file_handle;
  args.arg1 = 0u;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 0,
         "seek second read handle to start");

  memset(read_buffer, 0, sizeof(read_buffer));
  args.arg0 = (uintptr_t)second_file_handle;
  args.arg1 = (uintptr_t)read_buffer;
  args.arg2 = sizeof(read_buffer);
  expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == 5,
         "second read handle can read renamed open file");
  expect(memcmp(read_buffer, "helXY", 5u) == 0, "second read handle sees shared contents");

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close renamed file handle");

  args.arg0 = (uintptr_t)"/apps/ui-renamed.txt";
  args.arg1 = RUNTIME_FILE_OPEN_WRITE;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "writer open succeeds after previous writer closes");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close reopened writer handle");

  args.arg0 = (uintptr_t)second_file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close second read handle");

  args.arg0 = (uintptr_t)"/apps/ui-renamed.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "reopen renamed file");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)-1;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EINVAL,
         "negative seek is rejected");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)512;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EINVAL,
         "seek beyond file capacity is rejected");

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close reopened renamed file");

  args.arg0 = (uintptr_t)"/apps/ui-renamed.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "remove regular file");

  args.arg0 = (uintptr_t)"/apps/ui-renamed.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  expect(dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "removed file cannot reopen");

  args.arg0 = (uintptr_t)"/apps";
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "open directory");
  dir_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)dir_handle;
  args.arg1 = (uintptr_t)&stat;
  expect(dispatch(RUNTIME_SYSCALL_FILE_STAT, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "stat directory");
  expect(stat.type == RUNTIME_FILE_TYPE_DIRECTORY, "directory stat type");

  args.arg0 = (uintptr_t)dir_handle;
  args.arg1 = (uintptr_t)&entry;
  expect(dispatch(RUNTIME_SYSCALL_FILE_READDIR, &args, &ops, &table, &ctx) == 1,
         "readdir returns first child");
  expect(strcmp(entry.name, "cache") == 0, "readdir begins with first child in node order");

  args.arg0 = (uintptr_t)dir_handle;
  args.arg1 = (uintptr_t)read_buffer;
  args.arg2 = sizeof(read_buffer);
  expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EISDIR,
         "reading a directory returns eisdir");

  args.arg0 = (uintptr_t)dir_handle;
  args.arg1 = 0u;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EISDIR,
         "seeking a directory returns eisdir");

  args.arg0 = (uintptr_t)dir_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close directory handle");

  args.arg0 = (uintptr_t)"/apps";
  args.arg1 = RUNTIME_FILE_OPEN_WRITE;
  expect(dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EISDIR,
         "directory write-open returns eisdir");

  args.arg0 = (uintptr_t)"/apps/cache";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "remove empty directory");

  args.arg0 = (uintptr_t)"/apps/data";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "create data directory");

  args.arg0 = (uintptr_t)"/apps/data/nested";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "create nested data directory");

  args.arg0 = (uintptr_t)"/apps/data/blob.txt";
  args.arg1 = RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "create file inside data directory");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)"1234";
  args.arg2 = 4u;
  expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) == 4,
         "write blob file");

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close blob file");

  args.arg0 = (uintptr_t)"/apps/data/nested/leaf.bin";
  args.arg1 = RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "create nested leaf file");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)"XY";
  args.arg2 = 2u;
  expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) == 2,
         "write nested leaf file");

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close nested leaf file");

  expect(collect_dir_names(&args, &ops, &table, &ctx, "/apps/data", collected_entries,
                           (uint32_t)(sizeof(collected_entries) / sizeof(collected_entries[0]))) == 2u,
         "data directory has file and nested directory");
  expect(dir_entries_have_name(collected_entries, 2u, "blob.txt", RUNTIME_FILE_TYPE_REGULAR) != 0,
         "data directory find sees blob file");
  expect(dir_entries_have_name(collected_entries, 2u, "nested", RUNTIME_FILE_TYPE_DIRECTORY) != 0,
         "data directory find sees nested directory");

  args.arg0 = (uintptr_t)"/apps/data";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_ENOTEMPTY,
         "remove non-empty directory returns enotempty");

  args.arg0 = (uintptr_t)"/apps/data";
  args.arg1 = (uintptr_t)"/apps/data/blob.txt/nested";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_ENOTDIR,
         "rename into file parent returns enotdir");

  args.arg0 = (uintptr_t)"/apps/data/blob.txt";
  args.arg1 = (uintptr_t)"/apps/blob.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "rename file across directories");

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  args.arg1 = RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE |
              RUNTIME_FILE_OPEN_TRUNCATE;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "truncate existing file");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)&stat;
  expect(dispatch(RUNTIME_SYSCALL_FILE_STAT, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "stat truncated file");
  expect(stat.size_bytes == 0u, "truncate resets file size");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)read_buffer;
  args.arg2 = sizeof(read_buffer);
  expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == 0,
         "truncated file reads as empty");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)sparse_prefix;
  args.arg2 = sizeof(sparse_prefix);
  expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) ==
             (int32_t)sizeof(sparse_prefix),
         "write sparse prefix");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = 10u;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 10,
         "seek beyond eof for sparse write");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)sparse_tail;
  args.arg2 = sizeof(sparse_tail);
  expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) ==
             (int32_t)sizeof(sparse_tail),
         "write sparse tail");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = 0u;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 0,
         "rewind sparse file");

  memset(sparse_read_buffer, 0, sizeof(sparse_read_buffer));
  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)sparse_read_buffer;
  args.arg2 = 13u;
  expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == 13,
         "read sparse file bytes");
  expect(memcmp(sparse_read_buffer, sparse_prefix, sizeof(sparse_prefix)) == 0,
         "sparse file prefix preserved");
  expect((sparse_read_buffer[6] == 0u) && (sparse_read_buffer[7] == 0u) &&
             (sparse_read_buffer[8] == 0u) && (sparse_read_buffer[9] == 0u),
         "sparse hole is zero-filled");
  expect(memcmp(&sparse_read_buffer[10], sparse_tail, sizeof(sparse_tail)) == 0,
         "sparse tail preserved");

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close sparse file before retruncate");

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_TRUNCATE;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "reopen sparse file with truncate");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = 4u;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 4,
         "seek within truncated file for zero-fill");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)truncated_tail;
  args.arg2 = sizeof(truncated_tail);
  expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) ==
             (int32_t)sizeof(truncated_tail),
         "write truncated tail");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = 0u;
  args.arg2 = RUNTIME_FILE_SEEK_SET;
  expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 0,
         "rewind retruncated file");

  memset(sparse_read_buffer, 0xFF, sizeof(sparse_read_buffer));
  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)sparse_read_buffer;
  args.arg2 = 5u;
  expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == 5,
         "read retruncated file bytes");
  expect((sparse_read_buffer[0] == 0u) && (sparse_read_buffer[1] == 0u) &&
             (sparse_read_buffer[2] == 0u) && (sparse_read_buffer[3] == 0u),
         "truncate clears prior payload bytes");
  expect(sparse_read_buffer[4] == truncated_tail[0], "retruncated tail preserved");

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close truncated file");

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  args.arg1 = (uintptr_t)"/apps/data";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EEXIST,
         "rename over existing target returns eexist");

  args.arg0 = (uintptr_t)"/apps/data";
  args.arg1 = (uintptr_t)"/apps/data/sub";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EINVAL,
         "directory cannot move into descendant");

  ctx.current_pid = 9u;

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "foreign pid can open owned file for read");
  file_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close foreign read handle");

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  args.arg1 = RUNTIME_FILE_OPEN_WRITE;
  expect(dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EACCES,
         "foreign pid cannot open owned file for write");

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EACCES,
         "foreign pid cannot remove owned file");

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  args.arg1 = (uintptr_t)"/apps/alien.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EACCES,
         "foreign pid cannot rename owned file");

  args.arg0 = (uintptr_t)"/apps/data/foreign";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EACCES,
         "foreign pid cannot create child in owned directory");

  ctx.current_pid = 5u;

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "remove blob file after truncate");

  args.arg0 = (uintptr_t)"/apps/data/nested/leaf.bin";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "remove nested leaf file");

  args.arg0 = (uintptr_t)"/apps/data/nested";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "remove nested directory");

  args.arg0 = (uintptr_t)"/apps/data";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "remove emptied directory");

  args.arg0 = (uintptr_t)"/apps/blob.txt";
  args.arg1 = (uintptr_t)"/apps/new/blob.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "rename into missing parent returns enoent");

  args.arg0 = (uintptr_t)"/missing/file.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  expect(dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "missing file open returns enoent");

  args.arg0 = (uintptr_t)"/apps/nope.txt";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "remove missing file returns enoent");

  args.arg0 = (uintptr_t)"/";
  expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EBUSY,
         "remove root returns ebusy");

  args.arg0 = (uintptr_t)"/";
  args.arg1 = (uintptr_t)"/apps/root";
  expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_EBUSY,
         "rename root returns ebusy");

  args.arg0 = (uintptr_t)"/apps";
  args.arg1 = RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
  expect(result > 0, "reopen apps directory");
  dir_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)dir_handle;
  args.arg1 = (uintptr_t)&entry;
  expect(dispatch(RUNTIME_SYSCALL_FILE_READDIR, &args, &ops, &table, &ctx) == 0,
         "apps directory is empty after cleanup");

  args.arg0 = (uintptr_t)dir_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close cleaned directory handle");

  return 0;
}
