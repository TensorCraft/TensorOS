#include "runtime_display.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_display_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct runtime_display_service service = {0};
  uint32_t response = 0u;

  runtime_display_service_init(&service, 80u, 25u,
                               RUNTIME_DISPLAY_FLAG_TEXT_GRID |
                                   RUNTIME_DISPLAY_FLAG_KERNEL_LOCAL);

  expect(runtime_display_service_request(&service, RUNTIME_DISPLAY_REQUEST_INFO, &response) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "display info request succeeds");
  expect(runtime_display_unpack_width(response) == 80u, "display info preserves width");
  expect(runtime_display_unpack_height(response) == 25u, "display info preserves height");

  expect(runtime_display_service_request(&service, RUNTIME_DISPLAY_REQUEST_FLAGS, &response) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "display flags request succeeds");
  expect(response == (RUNTIME_DISPLAY_FLAG_TEXT_GRID | RUNTIME_DISPLAY_FLAG_KERNEL_LOCAL),
         "display flags preserve capability bits");

  expect(runtime_display_service_request(&service, RUNTIME_DISPLAY_REQUEST_PRESENT_COUNT,
                                         &response) == RUNTIME_SYSCALL_STATUS_OK,
         "present count request succeeds before presents");
  expect(response == 0u, "present count starts at zero");

  runtime_display_service_mark_present(&service);
  runtime_display_service_mark_present(&service);
  expect(runtime_display_service_request(&service, RUNTIME_DISPLAY_REQUEST_PRESENT_COUNT,
                                         &response) == RUNTIME_SYSCALL_STATUS_OK,
         "present count request succeeds after presents");
  expect(response == 2u, "present count tracks present markers");

  service.present_count = UINT32_MAX;
  runtime_display_service_mark_present(&service);
  expect(runtime_display_service_request(&service, RUNTIME_DISPLAY_REQUEST_PRESENT_COUNT,
                                         &response) == RUNTIME_SYSCALL_STATUS_OK,
         "present count remains queryable at saturation");
  expect(response == UINT32_MAX, "present count saturates instead of wrapping");

  expect(runtime_display_service_request(&service, 99u, &response) ==
             RUNTIME_SYSCALL_STATUS_ENOSYS,
         "unknown display request returns enosys");
  expect(runtime_display_service_request(0, RUNTIME_DISPLAY_REQUEST_INFO, &response) ==
             RUNTIME_SYSCALL_STATUS_EINVAL,
         "null display service returns einval");
  expect(runtime_display_service_request(&service, RUNTIME_DISPLAY_REQUEST_INFO, 0) ==
             RUNTIME_SYSCALL_STATUS_EINVAL,
         "null display response returns einval");

  puts("runtime display host checks passed");
  return 0;
}
