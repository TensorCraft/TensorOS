#include "runtime_fs_image.h"

#include <stddef.h>

#define RUNTIME_FS_IMAGE_HASH_EMPTY 0x811C9DC5u

volatile uint32_t g_runtime_fs_image_validate_diag_code;
volatile uint32_t g_runtime_fs_image_validate_diag_arg0;
volatile uint32_t g_runtime_fs_image_validate_diag_arg1;

struct runtime_fs_image_header {
  uint32_t magic;
  uint32_t version;
  uint32_t header_bytes;
  uint32_t record_count;
  uint32_t record_bytes;
  uint32_t tree_leaf_count;
  uint32_t tree_hash_count;
  uint32_t data_bytes;
  uint32_t root_hash;
  uint32_t image_bytes;
};

struct runtime_fs_image_record {
  uint32_t node_index;
  uint32_t type;
  uint32_t parent_index;
  uint32_t owner_pid;
  uint32_t mode;
  uint32_t size_bytes;
  uint32_t data_offset;
  uint32_t data_size;
  uint32_t data_hash;
  uint32_t object_hash;
  char name[RUNTIME_FS_NAME_CAPACITY];
};

static uint32_t runtime_fs_image_load_u32(const uint8_t *cursor) {
  return ((uint32_t)cursor[0]) | ((uint32_t)cursor[1] << 8u) | ((uint32_t)cursor[2] << 16u) |
         ((uint32_t)cursor[3] << 24u);
}

static void runtime_fs_image_store_u32(uint8_t *cursor, uint32_t value) {
  cursor[0] = (uint8_t)(value & 0xFFu);
  cursor[1] = (uint8_t)((value >> 8u) & 0xFFu);
  cursor[2] = (uint8_t)((value >> 16u) & 0xFFu);
  cursor[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void runtime_fs_image_copy_name(char *dest, const char *src) {
  uint32_t index = 0u;

  for (uint32_t clear = 0u; clear < RUNTIME_FS_NAME_CAPACITY; ++clear) {
    dest[clear] = '\0';
  }

  while ((src[index] != '\0') && (index + 1u < RUNTIME_FS_NAME_CAPACITY)) {
    dest[index] = src[index];
    ++index;
  }

  dest[index] = '\0';
}

static int runtime_fs_image_name_equals_bounded(const char *left, uint32_t left_capacity,
                                                const char *right) {
  uint32_t index = 0u;

  while ((index < left_capacity) && (left[index] != '\0') && (right[index] != '\0')) {
    if (left[index] != right[index]) {
      return 0;
    }
    ++index;
  }

  if (right[index] != '\0') {
    return 0;
  }

  return (index < left_capacity) && (left[index] == '\0');
}

static int32_t runtime_fs_image_validate_fail(uint32_t code, uint32_t arg0, uint32_t arg1,
                                              int32_t status) {
  g_runtime_fs_image_validate_diag_code = code;
  g_runtime_fs_image_validate_diag_arg0 = arg0;
  g_runtime_fs_image_validate_diag_arg1 = arg1;
  return status;
}

static uint32_t runtime_fs_image_hash_mix(uint32_t hash, uint8_t value) {
  return (hash ^ value) * 16777619u;
}

static uint32_t runtime_fs_image_hash_bytes(const void *data, uint32_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t hash = RUNTIME_FS_IMAGE_HASH_EMPTY;

  for (uint32_t index = 0u; index < size; ++index) {
    hash = runtime_fs_image_hash_mix(hash, bytes[index]);
  }

  return hash;
}

static uint32_t runtime_fs_image_hash_u32(uint32_t hash, uint32_t value) {
  hash = runtime_fs_image_hash_mix(hash, (uint8_t)(value & 0xFFu));
  hash = runtime_fs_image_hash_mix(hash, (uint8_t)((value >> 8u) & 0xFFu));
  hash = runtime_fs_image_hash_mix(hash, (uint8_t)((value >> 16u) & 0xFFu));
  hash = runtime_fs_image_hash_mix(hash, (uint8_t)((value >> 24u) & 0xFFu));
  return hash;
}

static uint32_t runtime_fs_image_object_hash(const struct runtime_fs_image_record *record) {
  uint32_t hash = RUNTIME_FS_IMAGE_HASH_EMPTY;

  hash = runtime_fs_image_hash_u32(hash, record->node_index);
  hash = runtime_fs_image_hash_u32(hash, record->type);
  hash = runtime_fs_image_hash_u32(hash, record->parent_index);
  hash = runtime_fs_image_hash_u32(hash, record->owner_pid);
  hash = runtime_fs_image_hash_u32(hash, record->mode);
  hash = runtime_fs_image_hash_u32(hash, record->size_bytes);
  hash = runtime_fs_image_hash_u32(hash, record->data_offset);
  hash = runtime_fs_image_hash_u32(hash, record->data_size);
  hash = runtime_fs_image_hash_u32(hash, record->data_hash);
  for (uint32_t index = 0u; index < RUNTIME_FS_NAME_CAPACITY; ++index) {
    hash = runtime_fs_image_hash_mix(hash, (uint8_t)record->name[index]);
  }

  return hash;
}

static uint32_t runtime_fs_image_tree_hash(uint32_t left, uint32_t right) {
  uint32_t hash = RUNTIME_FS_IMAGE_HASH_EMPTY;

  hash = runtime_fs_image_hash_u32(hash, left);
  hash = runtime_fs_image_hash_u32(hash, right);
  return hash;
}

static uint32_t runtime_fs_image_next_pow2(uint32_t value) {
  uint32_t result = 1u;

  while (result < value) {
    result <<= 1u;
  }

  return result;
}

static int runtime_fs_image_has_open_files(const struct runtime_fs *fs) {
  for (uint32_t index = 0u; index < RUNTIME_FS_MAX_OPEN_FILES; ++index) {
    if (fs->open_files[index].used != 0u) {
      return 1;
    }
  }

  return 0;
}

static uint32_t runtime_fs_image_record_count(const struct runtime_fs *fs) {
  uint32_t count = 0u;

  for (uint32_t index = 0u; index < RUNTIME_FS_MAX_NODES; ++index) {
    if (fs->nodes[index].used != 0u) {
      ++count;
    }
  }

  return count;
}

static uint32_t runtime_fs_image_data_bytes(const struct runtime_fs *fs) {
  uint32_t total = 0u;

  for (uint32_t index = 0u; index < RUNTIME_FS_MAX_NODES; ++index) {
    if ((fs->nodes[index].used != 0u) && (fs->nodes[index].type == RUNTIME_FILE_TYPE_REGULAR)) {
      total += fs->nodes[index].size_bytes;
    }
  }

  return total;
}

static int runtime_fs_image_parse_header(const uint8_t *buffer, uint32_t size,
                                         struct runtime_fs_image_header *header) {
  if ((buffer == NULL) || (header == NULL) || (size < RUNTIME_FS_IMAGE_HEADER_BYTES)) {
    return 0;
  }

  header->magic = runtime_fs_image_load_u32(&buffer[0]);
  header->version = runtime_fs_image_load_u32(&buffer[4]);
  header->header_bytes = runtime_fs_image_load_u32(&buffer[8]);
  header->record_count = runtime_fs_image_load_u32(&buffer[12]);
  header->record_bytes = runtime_fs_image_load_u32(&buffer[16]);
  header->tree_leaf_count = runtime_fs_image_load_u32(&buffer[20]);
  header->tree_hash_count = runtime_fs_image_load_u32(&buffer[24]);
  header->data_bytes = runtime_fs_image_load_u32(&buffer[28]);
  header->root_hash = runtime_fs_image_load_u32(&buffer[32]);
  header->image_bytes = runtime_fs_image_load_u32(&buffer[36]);

  if ((header->magic != RUNTIME_FS_IMAGE_MAGIC) ||
      (header->version != RUNTIME_FS_IMAGE_VERSION) ||
      (header->header_bytes != RUNTIME_FS_IMAGE_HEADER_BYTES) ||
      (header->record_bytes != RUNTIME_FS_IMAGE_RECORD_BYTES) || (header->record_count == 0u) ||
      (header->record_count > RUNTIME_FS_MAX_NODES) ||
      (header->tree_leaf_count == 0u) ||
      (header->tree_leaf_count > RUNTIME_FS_IMAGE_MAX_TREE_LEAVES) ||
      (header->tree_hash_count != ((header->tree_leaf_count * 2u) - 1u)) ||
      (header->tree_leaf_count < header->record_count) ||
      (runtime_fs_image_next_pow2(header->tree_leaf_count) != header->tree_leaf_count)) {
    return 0;
  }

  if (header->image_bytes !=
      (header->header_bytes + (header->record_count * header->record_bytes) +
       (header->tree_hash_count * 4u) + header->data_bytes)) {
    return 0;
  }

  return header->image_bytes == size;
}

static void runtime_fs_image_read_record(const uint8_t *buffer, uint32_t offset,
                                         struct runtime_fs_image_record *record) {
  record->node_index = runtime_fs_image_load_u32(&buffer[offset + 0u]);
  record->type = runtime_fs_image_load_u32(&buffer[offset + 4u]);
  record->parent_index = runtime_fs_image_load_u32(&buffer[offset + 8u]);
  record->owner_pid = runtime_fs_image_load_u32(&buffer[offset + 12u]);
  record->mode = runtime_fs_image_load_u32(&buffer[offset + 16u]);
  record->size_bytes = runtime_fs_image_load_u32(&buffer[offset + 20u]);
  record->data_offset = runtime_fs_image_load_u32(&buffer[offset + 24u]);
  record->data_size = runtime_fs_image_load_u32(&buffer[offset + 28u]);
  record->data_hash = runtime_fs_image_load_u32(&buffer[offset + 32u]);
  record->object_hash = runtime_fs_image_load_u32(&buffer[offset + 36u]);

  for (uint32_t index = 0u; index < RUNTIME_FS_NAME_CAPACITY; ++index) {
    record->name[index] = (char)buffer[offset + 40u + index];
  }
}

static void runtime_fs_image_write_record(uint8_t *buffer, uint32_t offset,
                                          const struct runtime_fs_image_record *record) {
  runtime_fs_image_store_u32(&buffer[offset + 0u], record->node_index);
  runtime_fs_image_store_u32(&buffer[offset + 4u], record->type);
  runtime_fs_image_store_u32(&buffer[offset + 8u], record->parent_index);
  runtime_fs_image_store_u32(&buffer[offset + 12u], record->owner_pid);
  runtime_fs_image_store_u32(&buffer[offset + 16u], record->mode);
  runtime_fs_image_store_u32(&buffer[offset + 20u], record->size_bytes);
  runtime_fs_image_store_u32(&buffer[offset + 24u], record->data_offset);
  runtime_fs_image_store_u32(&buffer[offset + 28u], record->data_size);
  runtime_fs_image_store_u32(&buffer[offset + 32u], record->data_hash);
  runtime_fs_image_store_u32(&buffer[offset + 36u], record->object_hash);

  for (uint32_t index = 0u; index < RUNTIME_FS_NAME_CAPACITY; ++index) {
    buffer[offset + 40u + index] = (uint8_t)record->name[index];
  }
}

static const struct runtime_fs_image_record *runtime_fs_image_find_record(
    const struct runtime_fs_image_record *records, uint32_t count, uint32_t node_index) {
  for (uint32_t index = 0u; index < count; ++index) {
    if (records[index].node_index == node_index) {
      return &records[index];
    }
  }

  return NULL;
}

uint32_t runtime_fs_image_required_bytes(const struct runtime_fs *fs) {
  uint32_t record_count;
  uint32_t tree_leaf_count;

  if ((fs == NULL) || (runtime_fs_validate(fs) != RUNTIME_SYSCALL_STATUS_OK)) {
    return 0u;
  }

  record_count = runtime_fs_image_record_count(fs);
  if (record_count == 0u) {
    return 0u;
  }

  tree_leaf_count = runtime_fs_image_next_pow2(record_count);
  return RUNTIME_FS_IMAGE_HEADER_BYTES + (record_count * RUNTIME_FS_IMAGE_RECORD_BYTES) +
         (((tree_leaf_count * 2u) - 1u) * 4u) + runtime_fs_image_data_bytes(fs);
}

int32_t runtime_fs_image_export(const struct runtime_fs *fs, void *buffer, uint32_t capacity,
                                uint32_t *bytes_written, struct runtime_fs_image_layout *layout) {
  uint8_t *bytes = (uint8_t *)buffer;
  uint32_t record_count;
  uint32_t tree_leaf_count;
  uint32_t tree_hash_count;
  uint32_t data_bytes;
  uint32_t image_bytes;
  uint32_t tree_offset;
  uint32_t data_offset;
  uint32_t record_offset;
  uint32_t payload_cursor = 0u;
  uint32_t record_cursor = 0u;
  uint32_t object_hashes[RUNTIME_FS_MAX_NODES];
  uint32_t tree_hashes[RUNTIME_FS_IMAGE_MAX_TREE_HASHES];

  if ((fs == NULL) || (buffer == NULL) || (bytes_written == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }
  if (runtime_fs_validate(fs) != RUNTIME_SYSCALL_STATUS_OK) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }
  if (runtime_fs_image_has_open_files(fs)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  record_count = runtime_fs_image_record_count(fs);
  if (record_count == 0u) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  tree_leaf_count = runtime_fs_image_next_pow2(record_count);
  tree_hash_count = (tree_leaf_count * 2u) - 1u;
  data_bytes = runtime_fs_image_data_bytes(fs);
  image_bytes = RUNTIME_FS_IMAGE_HEADER_BYTES + (record_count * RUNTIME_FS_IMAGE_RECORD_BYTES) +
                (tree_hash_count * 4u) + data_bytes;
  if (capacity < image_bytes) {
    return RUNTIME_SYSCALL_STATUS_ENOSPC;
  }

  tree_offset = RUNTIME_FS_IMAGE_HEADER_BYTES + (record_count * RUNTIME_FS_IMAGE_RECORD_BYTES);
  data_offset = tree_offset + (tree_hash_count * 4u);
  record_offset = RUNTIME_FS_IMAGE_HEADER_BYTES;

  for (uint32_t node_index = 0u; node_index < RUNTIME_FS_MAX_NODES; ++node_index) {
    struct runtime_fs_image_record record = {0};
    const struct runtime_fs_node *node = &fs->nodes[node_index];

    if (node->used == 0u) {
      continue;
    }

    record.node_index = node_index;
    record.type = node->type;
    record.parent_index = node->parent_index;
    record.owner_pid = node->owner_pid;
    record.mode = node->mode;
    record.size_bytes = node->size_bytes;
    record.data_offset = payload_cursor;
    record.data_size = (node->type == RUNTIME_FILE_TYPE_REGULAR) ? node->size_bytes : 0u;
    record.data_hash =
        runtime_fs_image_hash_bytes(node->data, record.data_size);
    runtime_fs_image_copy_name(record.name, node->name);
    record.object_hash = runtime_fs_image_object_hash(&record);

    runtime_fs_image_write_record(bytes, record_offset, &record);
    record_offset += RUNTIME_FS_IMAGE_RECORD_BYTES;
    object_hashes[record_cursor++] = record.object_hash;

    for (uint32_t data_index = 0u; data_index < record.data_size; ++data_index) {
      bytes[data_offset + payload_cursor + data_index] = node->data[data_index];
    }
    payload_cursor += record.data_size;
  }

  for (uint32_t index = 0u; index < tree_hash_count; ++index) {
    tree_hashes[index] = RUNTIME_FS_IMAGE_HASH_EMPTY;
  }
  for (uint32_t index = 0u; index < tree_leaf_count; ++index) {
    uint32_t value = (index < record_count) ? object_hashes[index] : RUNTIME_FS_IMAGE_HASH_EMPTY;
    tree_hashes[(tree_hash_count - tree_leaf_count) + index] = value;
  }
  for (uint32_t index = tree_hash_count - tree_leaf_count; index > 0u; --index) {
    uint32_t parent = index - 1u;
    tree_hashes[parent] =
        runtime_fs_image_tree_hash(tree_hashes[(parent * 2u) + 1u], tree_hashes[(parent * 2u) + 2u]);
  }
  for (uint32_t index = 0u; index < tree_hash_count; ++index) {
    runtime_fs_image_store_u32(&bytes[tree_offset + (index * 4u)], tree_hashes[index]);
  }

  runtime_fs_image_store_u32(&bytes[0], RUNTIME_FS_IMAGE_MAGIC);
  runtime_fs_image_store_u32(&bytes[4], RUNTIME_FS_IMAGE_VERSION);
  runtime_fs_image_store_u32(&bytes[8], RUNTIME_FS_IMAGE_HEADER_BYTES);
  runtime_fs_image_store_u32(&bytes[12], record_count);
  runtime_fs_image_store_u32(&bytes[16], RUNTIME_FS_IMAGE_RECORD_BYTES);
  runtime_fs_image_store_u32(&bytes[20], tree_leaf_count);
  runtime_fs_image_store_u32(&bytes[24], tree_hash_count);
  runtime_fs_image_store_u32(&bytes[28], data_bytes);
  runtime_fs_image_store_u32(&bytes[32], tree_hashes[0]);
  runtime_fs_image_store_u32(&bytes[36], image_bytes);

  *bytes_written = image_bytes;
  if (layout != NULL) {
    layout->record_count = record_count;
    layout->tree_leaf_count = tree_leaf_count;
    layout->tree_hash_count = tree_hash_count;
    layout->data_bytes = data_bytes;
    layout->image_bytes = image_bytes;
    layout->root_hash = tree_hashes[0];
  }

  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_fs_image_validate(const void *buffer, uint32_t size,
                                  struct runtime_fs_image_layout *layout) {
  const uint8_t *bytes = (const uint8_t *)buffer;
  struct runtime_fs_image_header header;
  struct runtime_fs_image_record records[RUNTIME_FS_MAX_NODES];
  uint32_t tree_hashes[RUNTIME_FS_IMAGE_MAX_TREE_HASHES];
  uint32_t object_hashes[RUNTIME_FS_MAX_NODES];
  uint32_t present_mask = 0u;
  uint32_t record_offset;
  uint32_t tree_offset;
  uint32_t data_offset;
  uint32_t expected_payload_offset = 0u;

  g_runtime_fs_image_validate_diag_code = 0u;
  g_runtime_fs_image_validate_diag_arg0 = 0u;
  g_runtime_fs_image_validate_diag_arg1 = 0u;

  if ((buffer == NULL) || !runtime_fs_image_parse_header(bytes, size, &header)) {
    return runtime_fs_image_validate_fail(0xC100u, size, 0u, RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  record_offset = header.header_bytes;
  tree_offset = record_offset + (header.record_count * header.record_bytes);
  data_offset = tree_offset + (header.tree_hash_count * 4u);

  for (uint32_t index = 0u; index < header.record_count; ++index) {
    struct runtime_fs_image_record *record = &records[index];
    uint32_t data_hash;

    runtime_fs_image_read_record(bytes, record_offset + (index * header.record_bytes), record);

    if (record->node_index >= RUNTIME_FS_MAX_NODES) {
      return runtime_fs_image_validate_fail(0xC110u, index, record->node_index,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (((present_mask >> record->node_index) & 1u) != 0u) {
      return runtime_fs_image_validate_fail(0xC111u, index, record->node_index,
                                            RUNTIME_SYSCALL_STATUS_EEXIST);
    }
    if ((index == 0u) && (record->node_index != 0u)) {
      return runtime_fs_image_validate_fail(0xC112u, index, record->node_index,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((index > 0u) && (record->node_index <= records[index - 1u].node_index)) {
      return runtime_fs_image_validate_fail(0xC113u, index, record->node_index,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((record->type != RUNTIME_FILE_TYPE_DIRECTORY) && (record->type != RUNTIME_FILE_TYPE_REGULAR)) {
      return runtime_fs_image_validate_fail(0xC114u, index, record->type,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((record->type == RUNTIME_FILE_TYPE_DIRECTORY) && (record->data_size != 0u)) {
      return runtime_fs_image_validate_fail(0xC115u, index, record->data_size,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((record->type == RUNTIME_FILE_TYPE_REGULAR) && (record->data_size != record->size_bytes)) {
      return runtime_fs_image_validate_fail(0xC116u, index,
                                            (record->data_size << 16u) | (record->size_bytes & 0xFFFFu),
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (record->size_bytes > RUNTIME_FS_FILE_CAPACITY) {
      return runtime_fs_image_validate_fail(0xC117u, index, record->size_bytes,
                                            RUNTIME_SYSCALL_STATUS_ENOSPC);
    }
    if (record->data_offset != expected_payload_offset) {
      return runtime_fs_image_validate_fail(0xC118u, index,
                                            (record->data_offset << 16u) |
                                                (expected_payload_offset & 0xFFFFu),
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((record->data_offset + record->data_size) > header.data_bytes) {
      return runtime_fs_image_validate_fail(0xC119u, index, header.data_bytes,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((index == 0u) && (record->parent_index != 0u)) {
      return runtime_fs_image_validate_fail(0xC11Au, index, record->parent_index,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((index == 0u) && (record->type != RUNTIME_FILE_TYPE_DIRECTORY)) {
      return runtime_fs_image_validate_fail(0xC11Eu, index, record->type,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((index == 0u) && ((record->name[0] != '/') || (record->name[1] != '\0'))) {
      return runtime_fs_image_validate_fail(
          0xC11Fu, ((uint32_t)(uint8_t)record->name[0]) | (((uint32_t)(uint8_t)record->name[1]) << 8u),
          ((uint32_t)(uint8_t)record->name[2]) | (((uint32_t)(uint8_t)record->name[3]) << 8u),
          RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((index != 0u) &&
        ((record->name[0] == '\0') ||
         ((record->name[0] == '/') && (record->name[1] == '\0')))) {
      return runtime_fs_image_validate_fail(0xC11Bu, index, (uint32_t)(uint8_t)record->name[0],
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }

    data_hash = runtime_fs_image_hash_bytes(&bytes[data_offset + record->data_offset], record->data_size);
    if (data_hash != record->data_hash) {
      return runtime_fs_image_validate_fail(0xC11Cu, index, data_hash,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (runtime_fs_image_object_hash(record) != record->object_hash) {
      return runtime_fs_image_validate_fail(0xC11Du, index, record->object_hash,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }

    object_hashes[index] = record->object_hash;
    expected_payload_offset += record->data_size;
    present_mask |= (1u << record->node_index);
  }

  if (expected_payload_offset != header.data_bytes) {
    return runtime_fs_image_validate_fail(0xC120u, expected_payload_offset, header.data_bytes,
                                          RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  for (uint32_t index = 0u; index < header.record_count; ++index) {
    const struct runtime_fs_image_record *parent;
    uint32_t depth = 0u;
    uint32_t current = records[index].node_index;

    if ((records[index].node_index != 0u) &&
        (((present_mask >> records[index].parent_index) & 1u) == 0u)) {
      return runtime_fs_image_validate_fail(0xC121u, index, records[index].parent_index,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }

    parent = runtime_fs_image_find_record(records, header.record_count, records[index].parent_index);
    if ((records[index].node_index == 0u) && (parent != &records[0])) {
      return runtime_fs_image_validate_fail(0xC122u, index, records[index].parent_index,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((records[index].node_index != 0u) && ((parent == NULL) || (parent->type != RUNTIME_FILE_TYPE_DIRECTORY))) {
      return runtime_fs_image_validate_fail(0xC123u, index, records[index].parent_index,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }

    while (current != 0u) {
      const struct runtime_fs_image_record *node =
          runtime_fs_image_find_record(records, header.record_count, current);
      if (node == NULL) {
        return runtime_fs_image_validate_fail(0xC124u, index, current,
                                              RUNTIME_SYSCALL_STATUS_EINVAL);
      }
      current = node->parent_index;
      ++depth;
      if (depth > RUNTIME_FS_MAX_NODES) {
        return runtime_fs_image_validate_fail(0xC125u, index, depth,
                                              RUNTIME_SYSCALL_STATUS_EINVAL);
      }
    }
  }

  for (uint32_t index = 0u; index < header.record_count; ++index) {
    for (uint32_t other = index + 1u; other < header.record_count; ++other) {
      if ((records[index].parent_index == records[other].parent_index) &&
          runtime_fs_image_name_equals_bounded(records[index].name, RUNTIME_FS_NAME_CAPACITY,
                                               records[other].name)) {
        return runtime_fs_image_validate_fail(0xC126u, index, other,
                                              RUNTIME_SYSCALL_STATUS_EEXIST);
      }
    }
  }

  for (uint32_t index = 0u; index < header.tree_hash_count; ++index) {
    tree_hashes[index] = runtime_fs_image_load_u32(&bytes[tree_offset + (index * 4u)]);
  }
  for (uint32_t index = 0u; index < header.tree_leaf_count; ++index) {
    uint32_t expected_hash = (index < header.record_count) ? object_hashes[index]
                                                           : RUNTIME_FS_IMAGE_HASH_EMPTY;
    if (tree_hashes[(header.tree_hash_count - header.tree_leaf_count) + index] != expected_hash) {
      return runtime_fs_image_validate_fail(0xC127u, index, expected_hash,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
  }
  for (uint32_t index = header.tree_hash_count - header.tree_leaf_count; index > 0u; --index) {
    uint32_t parent = index - 1u;
    uint32_t expected_hash =
        runtime_fs_image_tree_hash(tree_hashes[(parent * 2u) + 1u], tree_hashes[(parent * 2u) + 2u]);
    if (tree_hashes[parent] != expected_hash) {
      return runtime_fs_image_validate_fail(0xC128u, parent, expected_hash,
                                            RUNTIME_SYSCALL_STATUS_EINVAL);
    }
  }
  if (tree_hashes[0] != header.root_hash) {
    return runtime_fs_image_validate_fail(0xC129u, tree_hashes[0], header.root_hash,
                                          RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  if (layout != NULL) {
    layout->record_count = header.record_count;
    layout->tree_leaf_count = header.tree_leaf_count;
    layout->tree_hash_count = header.tree_hash_count;
    layout->data_bytes = header.data_bytes;
    layout->image_bytes = header.image_bytes;
    layout->root_hash = header.root_hash;
  }

  g_runtime_fs_image_validate_diag_code = 0xC1FFu;

  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_fs_image_import(struct runtime_fs *fs, const void *buffer, uint32_t size,
                                struct runtime_fs_image_layout *layout) {
  const uint8_t *bytes = (const uint8_t *)buffer;
  struct runtime_fs_image_header header;
  uint32_t record_offset;
  uint32_t tree_offset;
  uint32_t data_offset;

  if ((fs == NULL) || (buffer == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }
  if (runtime_fs_image_validate(buffer, size, layout) != RUNTIME_SYSCALL_STATUS_OK) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }
  if (!runtime_fs_image_parse_header(bytes, size, &header)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  runtime_fs_init(fs);
  for (uint32_t index = 0u; index < RUNTIME_FS_MAX_NODES; ++index) {
    fs->nodes[index].used = 0u;
    fs->nodes[index].type = RUNTIME_FILE_TYPE_UNKNOWN;
    fs->nodes[index].parent_index = 0u;
    fs->nodes[index].owner_pid = 0u;
    fs->nodes[index].mode = 0u;
    fs->nodes[index].size_bytes = 0u;
    fs->nodes[index].name[0] = '\0';
    for (uint32_t data_index = 0u; data_index < RUNTIME_FS_FILE_CAPACITY; ++data_index) {
      fs->nodes[index].data[data_index] = 0u;
    }
  }
  for (uint32_t index = 0u; index < RUNTIME_FS_MAX_OPEN_FILES; ++index) {
    fs->open_files[index].used = 0u;
    fs->open_files[index].node_index = 0u;
    fs->open_files[index].owner_pid = 0u;
    fs->open_files[index].flags = 0u;
    fs->open_files[index].offset = 0u;
    fs->open_files[index].dir_cursor = 0u;
  }

  record_offset = header.header_bytes;
  tree_offset = record_offset + (header.record_count * header.record_bytes);
  data_offset = tree_offset + (header.tree_hash_count * 4u);

  for (uint32_t index = 0u; index < header.record_count; ++index) {
    struct runtime_fs_image_record record;
    struct runtime_fs_node *node;

    runtime_fs_image_read_record(bytes, record_offset + (index * header.record_bytes), &record);
    node = &fs->nodes[record.node_index];
    node->used = 1u;
    node->type = record.type;
    node->parent_index = record.parent_index;
    node->owner_pid = record.owner_pid;
    node->mode = record.mode;
    node->size_bytes = record.size_bytes;
    runtime_fs_image_copy_name(node->name, record.name);
    for (uint32_t data_index = 0u; data_index < record.data_size; ++data_index) {
      node->data[data_index] = bytes[data_offset + record.data_offset + data_index];
    }
  }

  return runtime_fs_validate(fs);
}
