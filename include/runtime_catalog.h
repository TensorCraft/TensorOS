#pragma once

#include "runtime.h"

#include <stdint.h>

struct runtime_process_manifest {
  const char *task_key;
  const char *process_name;
  uint32_t default_role;
  task_entry_t entry;
};

struct runtime_process_catalog_entry {
  const char *task_key;
  const char *process_name;
  uint32_t default_role;
};

const struct runtime_process_manifest *runtime_catalog_find(
    const struct runtime_process_manifest *manifests, uint32_t count, const char *task_key);
uint32_t runtime_catalog_snapshot(const struct runtime_process_manifest *manifests, uint32_t count,
                                  struct runtime_process_catalog_entry *buffer,
                                  uint32_t capacity);
