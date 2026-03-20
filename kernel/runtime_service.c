#include "runtime_service.h"

static void runtime_service_session_clear(struct runtime_service_session_entry *entry) {
  if (entry == 0) {
    return;
  }

  entry->object = 0u;
  entry->owner_pid = 0u;
  entry->provider_pid = 0u;
  entry->service_slot = 0u;
  entry->active = 0u;
}

void runtime_service_session_table_init(struct runtime_service_session_table *table) {
  if (table == 0) {
    return;
  }

  for (uint32_t index = 0u; index < RUNTIME_SERVICE_SESSION_CAPACITY; ++index) {
    runtime_service_session_clear(&table->entries[index]);
  }
}

int32_t runtime_service_session_open(struct runtime_service_session_table *table, uint32_t owner_pid,
                                     uint32_t provider_pid, uint32_t service_slot,
                                     uintptr_t *object) {
  if ((table == 0) || (object == 0) || (owner_pid == 0u) || (provider_pid == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (uint32_t index = 0u; index < RUNTIME_SERVICE_SESSION_CAPACITY; ++index) {
    if (table->entries[index].active != 0u) {
      continue;
    }

    table->entries[index].object = (uintptr_t)(index + 1u);
    table->entries[index].owner_pid = owner_pid;
    table->entries[index].provider_pid = provider_pid;
    table->entries[index].service_slot = service_slot;
    table->entries[index].active = 1u;
    *object = table->entries[index].object;
    return RUNTIME_SYSCALL_STATUS_OK;
  }

  return RUNTIME_SYSCALL_STATUS_ENOSPC;
}

int32_t runtime_service_session_close(struct runtime_service_session_table *table, uintptr_t object) {
  if ((table == 0) || (object == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (uint32_t index = 0u; index < RUNTIME_SERVICE_SESSION_CAPACITY; ++index) {
    if ((table->entries[index].active != 0u) && (table->entries[index].object == object)) {
      runtime_service_session_clear(&table->entries[index]);
      return RUNTIME_SYSCALL_STATUS_OK;
    }
  }

  return RUNTIME_SYSCALL_STATUS_EBADF;
}

int32_t runtime_service_session_info(const struct runtime_service_session_table *table,
                                     uintptr_t object,
                                     struct runtime_service_session_info *info) {
  if ((table == 0) || (info == 0) || (object == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (uint32_t index = 0u; index < RUNTIME_SERVICE_SESSION_CAPACITY; ++index) {
    if ((table->entries[index].active != 0u) && (table->entries[index].object == object)) {
      info->object = table->entries[index].object;
      info->owner_pid = table->entries[index].owner_pid;
      info->provider_pid = table->entries[index].provider_pid;
      info->service_slot = table->entries[index].service_slot;
      return RUNTIME_SYSCALL_STATUS_OK;
    }
  }

  return RUNTIME_SYSCALL_STATUS_EBADF;
}

uint32_t runtime_service_session_invalidate_provider(struct runtime_service_session_table *table,
                                                     uint32_t provider_pid) {
  uint32_t closed = 0u;

  if ((table == 0) || (provider_pid == 0u)) {
    return 0u;
  }

  for (uint32_t index = 0u; index < RUNTIME_SERVICE_SESSION_CAPACITY; ++index) {
    if ((table->entries[index].active == 0u) || (table->entries[index].provider_pid != provider_pid)) {
      continue;
    }

    runtime_service_session_clear(&table->entries[index]);
    ++closed;
  }

  return closed;
}
