#include "runtime_loader.h"

#include <stddef.h>

static int runtime_loader_string_equals(const char *left, const char *right) {
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

static int runtime_loader_string_nonempty(const char *value) {
  return (value != NULL) && (value[0] != '\0');
}

const struct runtime_loader_record *runtime_loader_find(
    const struct runtime_loader_record *records, uint32_t count, const char *app_key) {
  if ((records == NULL) || (app_key == NULL)) {
    return NULL;
  }

  for (uint32_t index = 0u; index < count; ++index) {
    if (runtime_loader_string_equals(records[index].app_key, app_key)) {
      return &records[index];
    }
  }

  return NULL;
}

uint32_t runtime_loader_snapshot(const struct runtime_loader_record *records, uint32_t count,
                                 struct runtime_loader_snapshot_entry *buffer, uint32_t capacity) {
  if (records == NULL) {
    return 0u;
  }

  for (uint32_t index = 0u; index < count; ++index) {
    if ((buffer != NULL) && (index < capacity)) {
      buffer[index].app_key = records[index].app_key;
      buffer[index].display_name = records[index].display_name;
      buffer[index].version = records[index].version;
      buffer[index].task_key = records[index].task_key;
      buffer[index].default_role = records[index].default_role;
    }
  }

  return count;
}

int runtime_loader_validate_catalog(const struct runtime_loader_record *records, uint32_t count) {
  if (records == NULL) {
    return 0;
  }

  for (uint32_t index = 0u; index < count; ++index) {
    const struct runtime_loader_record *record = &records[index];

    if (!runtime_loader_string_nonempty(record->app_key) ||
        !runtime_loader_string_nonempty(record->display_name) ||
        !runtime_loader_string_nonempty(record->version) ||
        !runtime_loader_string_nonempty(record->task_key)) {
      return 0;
    }

    for (uint32_t other = index + 1u; other < count; ++other) {
      if (runtime_loader_string_equals(record->app_key, records[other].app_key)) {
        return 0;
      }
    }
  }

  return 1;
}
