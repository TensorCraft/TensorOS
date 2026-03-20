#include "runtime_policy.h"

#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_policy_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct process_runtime_state processes[4] = {0};

  processes[0].pid = 1u;
  processes[0].role = RUNTIME_PROCESS_ROLE_SYSTEM;
  processes[0].state = PROCESS_STATE_RUNNING;

  processes[1].pid = 2u;
  processes[1].role = RUNTIME_PROCESS_ROLE_FOREGROUND_APP;
  processes[1].state = PROCESS_STATE_READY;

  processes[2].pid = 3u;
  processes[2].role = RUNTIME_PROCESS_ROLE_BACKGROUND_APP;
  processes[2].state = PROCESS_STATE_BLOCKED;

  expect(runtime_policy_count_role(processes, 4u, RUNTIME_PROCESS_ROLE_FOREGROUND_APP) == 1u,
         "counts active foreground apps");
  expect(runtime_policy_count_role(processes, 4u, RUNTIME_PROCESS_ROLE_BACKGROUND_APP) == 1u,
         "counts background apps");
  expect(runtime_policy_can_spawn(processes, 4u, RUNTIME_PROCESS_ROLE_BACKGROUND_APP) == 1,
         "always allows background app");

#if CONFIG_RUNTIME_SINGLE_FOREGROUND
  expect(runtime_policy_can_spawn(processes, 4u, RUNTIME_PROCESS_ROLE_FOREGROUND_APP) == 0,
         "single foreground rejects second foreground app");
  expect(runtime_policy_mode_name()[0] == 's', "single foreground mode name");
#else
  expect(runtime_policy_can_spawn(processes, 4u, RUNTIME_PROCESS_ROLE_FOREGROUND_APP) == 1,
         "multiprocess allows another foreground app");
  expect(runtime_policy_mode_name()[0] == 'm', "multiprocess mode name");
#endif

#if CONFIG_SCHEDULER_COOPERATIVE
  expect(scheduler_mode_name()[0] == 'c', "scheduler mode name");
#else
  expect(scheduler_mode_name()[0] == 'p', "scheduler mode name");
#endif

  expect(runtime_process_role_name(RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY)[0] == 'l',
         "role names are stable");

  puts("runtime policy host checks passed");
  return 0;
}
