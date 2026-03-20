#include "process_core.h"

#include <stdio.h>
#include <stdlib.h>

#define TEST_CAPACITY 4u

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "process_core_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct process_runtime_state processes[TEST_CAPACITY] = {0};
  struct process_info snapshot[TEST_CAPACITY];

  process_core_reset_slot(&processes[0]);
  process_core_reset_slot(&processes[1]);
  process_core_reset_slot(&processes[2]);
  process_core_reset_slot(&processes[3]);

  expect(process_core_find_free_slot(processes, TEST_CAPACITY) == 0u, "first free slot");

  processes[0].pid = 1u;
  processes[0].role = RUNTIME_PROCESS_ROLE_SYSTEM;
  processes[0].state = PROCESS_STATE_READY;
  processes[0].name = "A";

  processes[1].pid = 2u;
  processes[1].parent_pid = 1u;
  processes[1].role = RUNTIME_PROCESS_ROLE_FOREGROUND_APP;
  processes[1].state = PROCESS_STATE_BLOCKED;
  processes[1].wait_reason = PROCESS_WAIT_EVENT_CHANNEL;
  processes[1].wait_channel = 0x44u;

  processes[2].pid = 3u;
  processes[2].parent_pid = 1u;
  processes[2].role = RUNTIME_PROCESS_ROLE_BACKGROUND_APP;
  processes[2].state = PROCESS_STATE_ZOMBIE;
  processes[2].exit_code = 7;

  processes[3].pid = 4u;
  processes[3].role = RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY;
  processes[3].state = PROCESS_STATE_BLOCKED;
  processes[3].wait_reason = PROCESS_WAIT_SLEEP_TICKS;
  processes[3].wake_tick = 10u;

  expect(process_core_find_index_by_pid(processes, TEST_CAPACITY, 3u) == 2u, "find pid");
  expect(process_core_find_next_runnable(processes, TEST_CAPACITY, 0u) == 0u,
         "round robin wraps to current runnable");
  expect(process_core_count_blocked_sleepers(processes, TEST_CAPACITY) == 1u, "sleepers count");
  expect(process_core_find_waitable_child(processes, TEST_CAPACITY, 1u, 0u) == 2u,
         "find zombie child");
  expect(process_core_has_child(processes, TEST_CAPACITY, 1u, 2u) == 1, "has child");
  expect(process_core_wake_channel(processes, TEST_CAPACITY, 0x44u) == 1u, "wake channel");
  expect(processes[1].state == PROCESS_STATE_READY, "channel wake made ready");
  process_core_block(&processes[1], PROCESS_WAIT_CHILD, 0u, 0u, 3u);
  expect(process_core_wake_waiting_parent(processes, TEST_CAPACITY, 3u) == 1u,
         "wake waiting parent");
  expect(process_core_wake_sleepers(processes, TEST_CAPACITY, 10u) == 1u, "wake sleepers");
  expect(process_core_snapshot(processes, TEST_CAPACITY, snapshot, TEST_CAPACITY) == 4u,
         "snapshot count");
  expect(snapshot[0].pid == 1u, "snapshot order");
  expect(snapshot[1].role == RUNTIME_PROCESS_ROLE_FOREGROUND_APP, "snapshot role");

  puts("process core host checks passed");
  return 0;
}
