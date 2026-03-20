#include "runtime_fs.h"
#include "runtime_fs_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRESS_ROUNDS 256u

struct host_context {
  uint32_t current_pid;
  struct runtime_fs fs;
};

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_fs_stress_host_test failed: %s\n", message);
    exit(1);
  }
}

static void expect_fs_valid(const struct runtime_fs *fs, const char *message) {
  expect(runtime_fs_validate(fs) == RUNTIME_SYSCALL_STATUS_OK, message);
}

static uint32_t host_process_current_pid(void *context) {
  return ((struct host_context *)context)->current_pid;
}

static int32_t host_fs_open(const char *path, uint32_t flags, uintptr_t *file_object,
                            void *context) {
  struct host_context *ctx = (struct host_context *)context;

  return runtime_fs_open(&ctx->fs, ctx->current_pid, path, flags, file_object);
}

static int32_t host_fs_read(uintptr_t file_object, void *buffer, uint32_t size, void *context) {
  return runtime_fs_read(&((struct host_context *)context)->fs, file_object, buffer, size);
}

static int32_t host_fs_write(uintptr_t file_object, const void *buffer, uint32_t size,
                             void *context) {
  return runtime_fs_write(&((struct host_context *)context)->fs, file_object, buffer, size);
}

static int32_t host_fs_close(uintptr_t file_object, void *context) {
  return runtime_fs_close(&((struct host_context *)context)->fs, file_object);
}

static int32_t host_fs_stat(uintptr_t file_object, struct runtime_file_stat *stat, void *context) {
  return runtime_fs_stat(&((struct host_context *)context)->fs, file_object, stat);
}

static int32_t host_fs_readdir(uintptr_t file_object, struct runtime_dir_entry *entry,
                               void *context) {
  return runtime_fs_readdir(&((struct host_context *)context)->fs, file_object, entry);
}

static int32_t host_fs_seek(uintptr_t file_object, int32_t offset, uint32_t whence, void *context) {
  return runtime_fs_seek(&((struct host_context *)context)->fs, file_object, offset, whence);
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

static void fill_payload(uint8_t *buffer, uint32_t size, uint32_t round, uint32_t seed) {
  uint32_t index;
  uint32_t value = 0x7F4A7C15u ^ round ^ (seed * 0x9E3779B9u);

  for (index = 0u; index < size; ++index) {
    value = value * 1103515245u + 12345u;
    buffer[index] = (uint8_t)(value >> 24);
  }
}

static uint32_t collect_dir_count(struct runtime_syscall_args *args, const struct runtime_syscall_ops *ops,
                                  struct runtime_syscall_table *table, struct host_context *ctx,
                                  const char *path, const char *needle, uint32_t expected_type) {
  runtime_handle_t dir_handle;
  struct runtime_dir_entry entry = {0};
  int32_t result;
  uint32_t found = 0u;

  args->arg0 = (uintptr_t)path;
  args->arg1 = RUNTIME_FILE_OPEN_READ;
  result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, args, ops, table, ctx);
  expect(result > 0, "open directory during stress");
  dir_handle = (runtime_handle_t)result;

  for (;;) {
    args->arg0 = (uintptr_t)dir_handle;
    args->arg1 = (uintptr_t)&entry;
    result = dispatch(RUNTIME_SYSCALL_FILE_READDIR, args, ops, table, ctx);
    if (result == 0) {
      break;
    }
    expect(result == 1, "readdir during stress returns entry");
    if ((strcmp(entry.name, needle) == 0) && (entry.type == expected_type)) {
      ++found;
    }
  }

  args->arg0 = (uintptr_t)dir_handle;
  expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, args, ops, table, ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "close stress directory handle");
  return found;
}

static void checkpoint_roundtrip(struct host_context *ctx, uint8_t *image_buffer) {
  struct runtime_fs_image_layout layout = {0};
  uint32_t image_bytes = 0u;

  expect(runtime_fs_image_export(&ctx->fs, image_buffer, RUNTIME_FS_IMAGE_MAX_BYTES, &image_bytes,
                                 &layout) == RUNTIME_SYSCALL_STATUS_OK,
         "stress export image");
  expect(runtime_fs_image_validate(image_buffer, image_bytes, &layout) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "stress validate image");
  expect(runtime_fs_image_import(&ctx->fs, image_buffer, image_bytes, &layout) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "stress import image");
  expect_fs_valid(&ctx->fs, "fs validates after image roundtrip");
}

int main(void) {
  struct host_context ctx = {.current_pid = 23u};
  struct runtime_syscall_table table;
  struct runtime_syscall_args args = {0};
  struct runtime_file_stat stat = {0};
  uint8_t image_buffer[RUNTIME_FS_IMAGE_MAX_BYTES];
  uint8_t payload[40];
  uint8_t tail[8];
  uint8_t expected[72];
  uint8_t read_buffer[72];
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
  uint32_t round;

  runtime_fs_init(&ctx.fs);
  runtime_syscall_table_init(&table);
  expect_fs_valid(&ctx.fs, "fresh fs validates");

  args.arg0 = (uintptr_t)"/stress";
  args.arg1 = 0755u;
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "mkdir /stress");
  args.arg0 = (uintptr_t)"/stress/archive";
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "mkdir /stress/archive");
  args.arg0 = (uintptr_t)"/stress/tmp";
  expect(dispatch(RUNTIME_SYSCALL_MKDIR, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
         "mkdir /stress/tmp");
  expect_fs_valid(&ctx.fs, "post-bootstrap fs validates");

  for (round = 1u; round <= STRESS_ROUNDS; ++round) {
    runtime_handle_t handle;
    const char *archive_path = (round & 1u) == 0u ? "/stress/archive/slot0.bin"
                                                   : "/stress/archive/slot1.bin";
    uint32_t sparse_offset = 48u + (round & 0xFu);
    uint32_t expected_size = sparse_offset + (uint32_t)sizeof(tail);
    int32_t result;

    fill_payload(payload, (uint32_t)sizeof(payload), round, 1u);
    fill_payload(tail, (uint32_t)sizeof(tail), round, 2u);
    memset(expected, 0, sizeof(expected));
    memcpy(expected, payload, sizeof(payload));
    memcpy(&expected[sparse_offset], tail, sizeof(tail));

    args.arg0 = (uintptr_t)archive_path;
    (void)dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx);

    args.arg0 = (uintptr_t)"/stress/current.bin";
    args.arg1 = RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE |
                RUNTIME_FILE_OPEN_TRUNCATE;
    result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
    expect(result > 0, "open current.bin in stress");
    handle = (runtime_handle_t)result;

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)payload;
    args.arg2 = sizeof(payload);
    expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) == (int32_t)sizeof(payload),
           "write payload in stress");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = sparse_offset;
    args.arg2 = RUNTIME_FILE_SEEK_SET;
    expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == (int32_t)sparse_offset,
           "seek sparse offset in stress");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)tail;
    args.arg2 = sizeof(tail);
    expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) == (int32_t)sizeof(tail),
           "write sparse tail in stress");
    expect_fs_valid(&ctx.fs, "fs validates after sparse write");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = 0u;
    args.arg2 = RUNTIME_FILE_SEEK_SET;
    expect(dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &ctx) == 0,
           "rewind current.bin in stress");

    memset(read_buffer, 0xFF, sizeof(read_buffer));
    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)read_buffer;
    args.arg2 = expected_size;
    expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == (int32_t)expected_size,
           "read expected size in stress");
    expect(memcmp(read_buffer, expected, expected_size) == 0, "stress contents match expected");

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)&stat;
    expect(dispatch(RUNTIME_SYSCALL_FILE_STAT, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
           "stat current.bin in stress");
    expect(stat.size_bytes == expected_size, "stress size matches");

    args.arg0 = (uintptr_t)"/stress/current.bin";
    args.arg1 = (uintptr_t)archive_path;
    expect(dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
           "rename current.bin into archive slot");
    expect_fs_valid(&ctx.fs, "fs validates after archive rename");

    args.arg0 = (uintptr_t)handle;
    expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
           "close archived handle");

    args.arg0 = (uintptr_t)archive_path;
    args.arg1 = RUNTIME_FILE_OPEN_READ;
    result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
    expect(result > 0, "reopen archive file in stress");
    handle = (runtime_handle_t)result;

    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)read_buffer;
    args.arg2 = expected_size;
    expect(dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &ctx) == (int32_t)expected_size,
           "read archive file in stress");
    expect(memcmp(read_buffer, expected, expected_size) == 0, "archive contents match");

    args.arg0 = (uintptr_t)handle;
    expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
           "close archive file in stress");
    checkpoint_roundtrip(&ctx, image_buffer);

    args.arg0 = (uintptr_t)"/stress/tmp/find.bin";
    args.arg1 = RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_TRUNCATE;
    result = dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &ctx);
    expect(result > 0, "open tmp find file in stress");
    handle = (runtime_handle_t)result;
    args.arg0 = (uintptr_t)handle;
    args.arg1 = (uintptr_t)tail;
    args.arg2 = 3u;
    expect(dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &ctx) == 3,
           "write tmp find file");
    args.arg0 = (uintptr_t)handle;
    expect(dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
           "close tmp find file");

    expect(collect_dir_count(&args, &ops, &table, &ctx, "/stress", "archive",
                             RUNTIME_FILE_TYPE_DIRECTORY) == 1u,
           "find archive dir in stress root");
    expect(collect_dir_count(&args, &ops, &table, &ctx, "/stress", "tmp",
                             RUNTIME_FILE_TYPE_DIRECTORY) == 1u,
           "find tmp dir in stress root");
    expect(collect_dir_count(&args, &ops, &table, &ctx, "/stress/archive",
                             (round & 1u) == 0u ? "slot0.bin" : "slot1.bin",
                             RUNTIME_FILE_TYPE_REGULAR) == 1u,
           "find archive slot file");
    expect(collect_dir_count(&args, &ops, &table, &ctx, "/stress/tmp", "find.bin",
                             RUNTIME_FILE_TYPE_REGULAR) == 1u,
           "find tmp file");

    args.arg0 = (uintptr_t)"/stress/tmp/find.bin";
    expect(dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &ctx) == RUNTIME_SYSCALL_STATUS_OK,
           "remove tmp find file");
    expect_fs_valid(&ctx.fs, "fs validates after temp cleanup");
    checkpoint_roundtrip(&ctx, image_buffer);
  }

  expect_fs_valid(&ctx.fs, "final fs validates");
  return 0;
}
