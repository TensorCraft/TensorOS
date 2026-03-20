#pragma once

#include "runtime_syscall.h"

#include <stdint.h>

#define RUNTIME_SERVICE_SESSION_CAPACITY 8u

struct runtime_service_session_info {
  uintptr_t object;
  uint32_t owner_pid;
  uint32_t provider_pid;
  uint32_t service_slot;
};

struct runtime_service_session_entry {
  uintptr_t object;
  uint32_t owner_pid;
  uint32_t provider_pid;
  uint32_t service_slot;
  uint8_t active;
};

struct runtime_service_session_table {
  struct runtime_service_session_entry entries[RUNTIME_SERVICE_SESSION_CAPACITY];
};

void runtime_service_session_table_init(struct runtime_service_session_table *table);
int32_t runtime_service_session_open(struct runtime_service_session_table *table, uint32_t owner_pid,
                                     uint32_t provider_pid, uint32_t service_slot,
                                     uintptr_t *object);
int32_t runtime_service_session_close(struct runtime_service_session_table *table, uintptr_t object);
int32_t runtime_service_session_info(const struct runtime_service_session_table *table,
                                     uintptr_t object,
                                     struct runtime_service_session_info *info);
uint32_t runtime_service_session_invalidate_provider(struct runtime_service_session_table *table,
                                                     uint32_t provider_pid);
