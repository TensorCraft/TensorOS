#include "runtime_service.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_service_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct runtime_service_session_table table;
  struct runtime_service_session_info info = {0};
  uintptr_t first = 0u;
  uintptr_t second = 0u;

  runtime_service_session_table_init(&table);

  expect(runtime_service_session_open(&table, 7u, 11u, 0u, &first) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "open first service session succeeds");
  expect(runtime_service_session_open(&table, 8u, 11u, 0u, &second) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "open second service session succeeds");
  expect(first != 0u, "first service session object assigned");
  expect(second != 0u, "second service session object assigned");
  expect(first != second, "service session objects stay unique");

  expect(runtime_service_session_info(&table, first, &info) == RUNTIME_SYSCALL_STATUS_OK,
         "service session info succeeds");
  expect(info.owner_pid == 7u, "service session preserves owner pid");
  expect(info.provider_pid == 11u, "service session preserves provider pid");
  expect(info.service_slot == 0u, "service session preserves slot");

  expect(runtime_service_session_invalidate_provider(&table, 11u) == 2u,
         "provider invalidation closes all bound sessions");
  expect(runtime_service_session_info(&table, first, &info) == RUNTIME_SYSCALL_STATUS_EBADF,
         "invalidated session is no longer queryable");
  expect(runtime_service_session_close(&table, second) == RUNTIME_SYSCALL_STATUS_EBADF,
         "invalidated session close reports ebadf");

  for (uint32_t index = 0u; index < RUNTIME_SERVICE_SESSION_CAPACITY; ++index) {
    uintptr_t object = 0u;
    expect(runtime_service_session_open(&table, 10u + index, 20u, 1u, &object) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "fill service session table succeeds");
  }
  expect(runtime_service_session_open(&table, 99u, 20u, 1u, &first) ==
             RUNTIME_SYSCALL_STATUS_ENOSPC,
         "full service session table returns enospc");

  expect(runtime_service_session_close(&table, 1u) == RUNTIME_SYSCALL_STATUS_OK,
         "service session close succeeds");
  expect(runtime_service_session_info(&table, 1u, &info) == RUNTIME_SYSCALL_STATUS_EBADF,
         "closed service session is no longer queryable");

  puts("runtime service host checks passed");
  return 0;
}
