#include "runtime_fs.h"
#include "runtime_fs_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_fs_image_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct runtime_fs fs;
  struct runtime_fs restored;
  struct runtime_fs_image_layout layout = {0};
  uintptr_t file_object = 0u;
  uint8_t image[RUNTIME_FS_IMAGE_MAX_BYTES];
  uint8_t read_buffer[20] = {0};
  uint32_t image_bytes = 0u;
  const uint8_t payload[8] = {1u, 2u, 3u, 4u, 0xA5u, 0u, 0x5Au, 0xFFu};

  runtime_fs_init(&fs);
  expect(runtime_fs_mkdir(&fs, 7u, "/persist", 0755u) == RUNTIME_SYSCALL_STATUS_OK,
         "mkdir /persist");
  expect(runtime_fs_mkdir(&fs, 7u, "/persist/cache", 0755u) == RUNTIME_SYSCALL_STATUS_OK,
         "mkdir /persist/cache");
  expect(runtime_fs_open(&fs, 7u, "/persist/cache/blob.bin",
                         RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_READ |
                             RUNTIME_FILE_OPEN_WRITE,
                         &file_object) == RUNTIME_SYSCALL_STATUS_OK,
         "open blob.bin");
  expect(runtime_fs_write(&fs, file_object, payload, (uint32_t)sizeof(payload)) ==
             (int32_t)sizeof(payload),
         "write payload");
  expect(runtime_fs_seek(&fs, file_object, 12, RUNTIME_FILE_SEEK_SET) == 12,
         "seek sparse offset");
  expect(runtime_fs_write(&fs, file_object, "XY", 2u) == 2, "write sparse tail");

  expect(runtime_fs_image_export(&fs, image, (uint32_t)sizeof(image), &image_bytes, &layout) ==
             RUNTIME_SYSCALL_STATUS_EBUSY,
         "export rejects open handles");

  expect(runtime_fs_close(&fs, file_object) == RUNTIME_SYSCALL_STATUS_OK, "close blob.bin");
  expect(runtime_fs_validate(&fs) == RUNTIME_SYSCALL_STATUS_OK, "source fs validates");
  expect(runtime_fs_image_required_bytes(&fs) != 0u, "required image bytes available");
  expect(runtime_fs_image_export(&fs, image, (uint32_t)sizeof(image), &image_bytes, &layout) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "export image succeeds");
  expect(layout.record_count == 4u, "record count includes root and descendants");
  expect(layout.root_hash != 0u, "root hash is populated");
  expect(runtime_fs_image_validate(image, image_bytes, &layout) == RUNTIME_SYSCALL_STATUS_OK,
         "validate exported image");

  expect(runtime_fs_image_import(&restored, image, image_bytes, &layout) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "import exported image");
  expect(runtime_fs_validate(&restored) == RUNTIME_SYSCALL_STATUS_OK, "restored fs validates");

  file_object = 0u;
  expect(runtime_fs_open(&restored, 7u, "/persist/cache/blob.bin", RUNTIME_FILE_OPEN_READ,
                         &file_object) == RUNTIME_SYSCALL_STATUS_OK,
         "open restored blob.bin");
  expect(runtime_fs_read(&restored, file_object, read_buffer, 14u) == 14,
         "read restored sparse file");
  expect(memcmp(read_buffer, payload, sizeof(payload)) == 0, "restored prefix matches payload");
  expect(read_buffer[8] == 0u && read_buffer[9] == 0u && read_buffer[10] == 0u &&
             read_buffer[11] == 0u,
         "restored sparse gap is zero-filled");
  expect(read_buffer[12] == 'X' && read_buffer[13] == 'Y', "restored sparse tail matches");
  expect(runtime_fs_close(&restored, file_object) == RUNTIME_SYSCALL_STATUS_OK,
         "close restored blob.bin");

  image[image_bytes - 1u] ^= 0x5Au;
  expect(runtime_fs_image_validate(image, image_bytes, &layout) == RUNTIME_SYSCALL_STATUS_EINVAL,
         "payload tamper invalidates image");
  image[image_bytes - 1u] ^= 0x5Au;
  image[32] ^= 0x01u;
  expect(runtime_fs_image_validate(image, image_bytes, &layout) == RUNTIME_SYSCALL_STATUS_EINVAL,
         "root hash tamper invalidates image");

  puts("runtime fs image host checks passed");
  return 0;
}
