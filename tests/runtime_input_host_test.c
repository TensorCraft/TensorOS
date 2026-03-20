#include "runtime_input.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_input_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct runtime_input_service service;
  struct runtime_ui_event event = {0};
  uint32_t response = 0u;

  runtime_input_service_init(&service, RUNTIME_INPUT_FLAG_KEYBOARD | RUNTIME_INPUT_FLAG_POINTER |
                                           RUNTIME_INPUT_FLAG_FOCUS);

  expect(runtime_input_service_request(&service, RUNTIME_INPUT_REQUEST_FLAGS, &response) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "input flags request succeeds");
  expect(response == (RUNTIME_INPUT_FLAG_KEYBOARD | RUNTIME_INPUT_FLAG_POINTER |
                      RUNTIME_INPUT_FLAG_FOCUS),
         "input flags preserve configured bits");

  expect(runtime_input_push_key(&service, 10u, 100u, 13u, 1u) == RUNTIME_SYSCALL_STATUS_OK,
         "key event push succeeds");
  expect(runtime_input_push_pointer_move(&service, 10u, 101u, 12u, 34u) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "pointer move push succeeds");
  expect(runtime_input_push_focus(&service, 10u, 102u, 1u) == RUNTIME_SYSCALL_STATUS_OK,
         "focus event push succeeds");

  expect(runtime_input_service_request(&service, RUNTIME_INPUT_REQUEST_DEPTH, &response) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "input depth request succeeds");
  expect(response == 3u, "input depth reflects queued events");

  expect(runtime_input_pop(&service, &event) == RUNTIME_SYSCALL_STATUS_OK,
         "input pop returns first queued event");
  expect(event.type == RUNTIME_UI_EVENT_INPUT_KEY, "first event preserves key type");
  expect(event.arg0 == 13u, "key event preserves keycode");
  expect(event.arg1 == 1u, "key event preserves pressed state");

  expect(runtime_input_pop(&service, &event) == RUNTIME_SYSCALL_STATUS_OK,
         "second input pop succeeds");
  expect(event.type == RUNTIME_UI_EVENT_INPUT_POINTER_MOVE, "pointer move type preserved");
  expect(event.arg0 == 12u, "pointer move preserves x");
  expect(event.arg1 == 34u, "pointer move preserves y");

  expect(runtime_input_pop(&service, &event) == RUNTIME_SYSCALL_STATUS_OK,
         "third input pop succeeds");
  expect(event.type == RUNTIME_UI_EVENT_FOCUS, "focus type preserved");
  expect(event.arg0 == 1u, "focus payload preserved");

  expect(runtime_input_service_request(&service, 99u, &response) == RUNTIME_SYSCALL_STATUS_ENOSYS,
         "unknown input request returns enosys");

  puts("runtime input host checks passed");
  return 0;
}
