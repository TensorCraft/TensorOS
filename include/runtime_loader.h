#pragma once

#include <stdint.h>

struct runtime_loader_record {
  const char *app_key;
  const char *display_name;
  const char *version;
  const char *task_key;
  uint32_t default_role;
};

struct runtime_loader_snapshot_entry {
  const char *app_key;
  const char *display_name;
  const char *version;
  const char *task_key;
  uint32_t default_role;
};

const struct runtime_loader_record *runtime_loader_find(
    const struct runtime_loader_record *records, uint32_t count, const char *app_key);
uint32_t runtime_loader_snapshot(const struct runtime_loader_record *records, uint32_t count,
                                 struct runtime_loader_snapshot_entry *buffer, uint32_t capacity);
int runtime_loader_validate_catalog(const struct runtime_loader_record *records, uint32_t count);
