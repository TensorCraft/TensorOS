#pragma once

#include "runtime_fs.h"

#include <stdint.h>

#define RUNTIME_FS_IMAGE_MAGIC 0x31465354u
#define RUNTIME_FS_IMAGE_VERSION 1u
#define RUNTIME_FS_IMAGE_HEADER_WORDS 10u
#define RUNTIME_FS_IMAGE_HEADER_BYTES (RUNTIME_FS_IMAGE_HEADER_WORDS * 4u)
#define RUNTIME_FS_IMAGE_RECORD_WORDS 10u
#define RUNTIME_FS_IMAGE_RECORD_BYTES \
  ((RUNTIME_FS_IMAGE_RECORD_WORDS * 4u) + RUNTIME_FS_NAME_CAPACITY)
#define RUNTIME_FS_IMAGE_MAX_TREE_LEAVES RUNTIME_FS_MAX_NODES
#define RUNTIME_FS_IMAGE_MAX_TREE_HASHES ((RUNTIME_FS_IMAGE_MAX_TREE_LEAVES * 2u) - 1u)
#define RUNTIME_FS_IMAGE_MAX_BYTES                                                \
  (RUNTIME_FS_IMAGE_HEADER_BYTES +                                                \
   (RUNTIME_FS_MAX_NODES * RUNTIME_FS_IMAGE_RECORD_BYTES) +                       \
   (RUNTIME_FS_IMAGE_MAX_TREE_HASHES * 4u) +                                      \
   (RUNTIME_FS_MAX_NODES * RUNTIME_FS_FILE_CAPACITY))

struct runtime_fs_image_layout {
  uint32_t record_count;
  uint32_t tree_leaf_count;
  uint32_t tree_hash_count;
  uint32_t data_bytes;
  uint32_t image_bytes;
  uint32_t root_hash;
};

extern volatile uint32_t g_runtime_fs_image_validate_diag_code;
extern volatile uint32_t g_runtime_fs_image_validate_diag_arg0;
extern volatile uint32_t g_runtime_fs_image_validate_diag_arg1;

uint32_t runtime_fs_image_required_bytes(const struct runtime_fs *fs);
int32_t runtime_fs_image_export(const struct runtime_fs *fs, void *buffer, uint32_t capacity,
                                uint32_t *bytes_written, struct runtime_fs_image_layout *layout);
int32_t runtime_fs_image_validate(const void *buffer, uint32_t size,
                                  struct runtime_fs_image_layout *layout);
int32_t runtime_fs_image_import(struct runtime_fs *fs, const void *buffer, uint32_t size,
                                struct runtime_fs_image_layout *layout);
