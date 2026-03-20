#include "runtime_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_ui_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct runtime_ui_event_queue queue;
  struct runtime_ui_timer_table timers;
  struct runtime_ui_event event = {0};
  uint32_t due_tick = 0u;

  runtime_ui_event_queue_init(&queue);
  runtime_ui_timer_table_init(&timers);

  expect(runtime_ui_event_pop(&queue, &event) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "empty event queue returns enoent");

  event.type = RUNTIME_UI_EVENT_INPUT_KEY;
  event.target_pid = 7u;
  event.tick = 100u;
  event.arg0 = 42u;
  event.arg1 = 1u;
  event.arg2 = 0u;
  expect(runtime_ui_event_push(&queue, &event) == RUNTIME_SYSCALL_STATUS_OK,
         "event push succeeds");
  expect(runtime_ui_event_peek(&queue, &event) == RUNTIME_SYSCALL_STATUS_OK,
         "event peek succeeds");
  expect(event.target_pid == 7u, "event peek preserves target pid");
  expect(runtime_ui_event_pop(&queue, &event) == RUNTIME_SYSCALL_STATUS_OK,
         "event pop succeeds");
  expect(event.arg0 == 42u, "event pop preserves payload");

  expect(runtime_ui_timer_next_due(&timers, &due_tick) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "next due on empty timer table returns enoent");
  expect(runtime_ui_timer_arm(&timers, 11u, 300u, 1u) == RUNTIME_SYSCALL_STATUS_OK,
         "first timer arm succeeds");
  expect(runtime_ui_timer_arm(&timers, 12u, 200u, 2u) == RUNTIME_SYSCALL_STATUS_OK,
         "second timer arm succeeds");
  expect(runtime_ui_timer_arm(&timers, 11u, 150u, 1u) == RUNTIME_SYSCALL_STATUS_OK,
         "re-arming same timer updates due tick");
  expect(runtime_ui_timer_next_due(&timers, &due_tick) == RUNTIME_SYSCALL_STATUS_OK,
         "next due succeeds");
  expect(due_tick == 150u, "next due returns earliest timer");
  expect(runtime_ui_timer_cancel(&timers, 12u, 2u) == RUNTIME_SYSCALL_STATUS_OK,
         "timer cancel succeeds");
  expect(runtime_ui_timer_cancel(&timers, 12u, 2u) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "timer cancel of missing entry returns enoent");
  expect(runtime_ui_timer_collect_due(&timers, 149u, &queue) == 0u,
         "timer collection waits until due tick");
  expect(runtime_ui_timer_collect_due(&timers, 150u, &queue) == 1u,
         "timer collection emits one due event");
  expect(runtime_ui_event_pop(&queue, &event) == RUNTIME_SYSCALL_STATUS_OK,
         "due timer event becomes available");
  expect(event.type == RUNTIME_UI_EVENT_TIMER, "due timer emits timer event");
  expect(event.target_pid == 11u, "timer event preserves owner pid");
  expect(event.arg0 == 1u, "timer event preserves token");
  expect(event.arg1 == 150u, "timer event preserves due tick");

  puts("runtime ui host checks passed");
  return 0;
}
