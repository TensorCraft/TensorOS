#include "runtime_catalog.h"

#include <stddef.h>

static int runtime_catalog_string_equals(const char *left, const char *right) {
  uint32_t index = 0u;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  while ((left[index] != '\0') && (right[index] != '\0')) {
    if (left[index] != right[index]) {
      return 0;
    }
    ++index;
  }

  return (left[index] == '\0') && (right[index] == '\0');
}

const struct runtime_process_manifest *runtime_catalog_find(
    const struct runtime_process_manifest *manifests, uint32_t count, const char *task_key) {
  if ((manifests == NULL) || (task_key == NULL)) {
    return NULL;
  }

  for (uint32_t index = 0u; index < count; ++index) {
    if (runtime_catalog_string_equals(manifests[index].task_key, task_key)) {
      return &manifests[index];
    }
  }

  return NULL;
}

uint32_t runtime_catalog_snapshot(const struct runtime_process_manifest *manifests, uint32_t count,
                                  struct runtime_process_catalog_entry *buffer,
                                  uint32_t capacity) {
  if (manifests == NULL) {
    return 0u;
  }

  for (uint32_t index = 0u; index < count; ++index) {
    if ((buffer != NULL) && (index < capacity)) {
      buffer[index].task_key = manifests[index].task_key;
      buffer[index].process_name = manifests[index].process_name;
      buffer[index].default_role = manifests[index].default_role;
    }
  }

  return count;
}
