#include "runtime_resource.h"

#include <stddef.h>

static int runtime_resource_segment_ok(const char *segment, size_t length) {
  if ((segment == 0) || (length == 0u)) {
    return 0;
  }

  if ((length == 1u) && (segment[0] == '.')) {
    return 0;
  }

  if ((length == 2u) && (segment[0] == '.') && (segment[1] == '.')) {
    return 0;
  }

  for (size_t index = 0u; index < length; ++index) {
    if (segment[index] == '/') {
      return 0;
    }
  }

  return 1;
}

int runtime_resource_validate_relative_path(const char *resource_path) {
  const char *cursor = resource_path;

  if ((resource_path == 0) || (resource_path[0] == '\0') || (resource_path[0] == '/')) {
    return 0;
  }

  while (*cursor != '\0') {
    const char *segment = cursor;
    size_t length = 0u;

    while ((cursor[length] != '\0') && (cursor[length] != '/')) {
      ++length;
    }

    if (!runtime_resource_segment_ok(segment, length)) {
      return 0;
    }

    cursor += length;
    if (*cursor == '/') {
      ++cursor;
      if (*cursor == '\0') {
        return 0;
      }
    }
  }

  return 1;
}

int32_t runtime_resource_build_path(const struct runtime_loader_record *records, uint32_t count,
                                    const struct runtime_resource_locator *locator, char *buffer,
                                    size_t capacity) {
  const struct runtime_loader_record *record;
  const char *prefix = RUNTIME_RESOURCE_PATH_PREFIX;
  const char *resources = RUNTIME_RESOURCE_PATH_RESOURCES;
  size_t index = 0u;

  if ((records == 0) || (locator == 0) || (buffer == 0) || (capacity == 0u) ||
      (locator->app_key == 0) || (locator->resource_path == 0)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (!runtime_resource_validate_relative_path(locator->resource_path)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  record = runtime_loader_find(records, count, locator->app_key);
  if (record == 0) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  while (*prefix != '\0') {
    if ((index + 1u) >= capacity) {
      return RUNTIME_SYSCALL_STATUS_ENOSPC;
    }
    buffer[index++] = *prefix++;
  }

  for (const char *cursor = record->app_key; *cursor != '\0'; ++cursor) {
    if ((index + 1u) >= capacity) {
      return RUNTIME_SYSCALL_STATUS_ENOSPC;
    }
    buffer[index++] = *cursor;
  }

  while (*resources != '\0') {
    if ((index + 1u) >= capacity) {
      return RUNTIME_SYSCALL_STATUS_ENOSPC;
    }
    buffer[index++] = *resources++;
  }

  for (const char *cursor = locator->resource_path; *cursor != '\0'; ++cursor) {
    if ((index + 1u) >= capacity) {
      return RUNTIME_SYSCALL_STATUS_ENOSPC;
    }
    buffer[index++] = *cursor;
  }

  buffer[index] = '\0';
  return RUNTIME_SYSCALL_STATUS_OK;
}
