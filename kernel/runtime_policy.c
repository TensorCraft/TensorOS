#include "runtime_policy.h"
#include "runtime.h"

int runtime_policy_can_spawn(const struct process_runtime_state *processes, uint32_t capacity,
                             uint32_t role) {
#if CONFIG_RUNTIME_SINGLE_FOREGROUND
  if (role != RUNTIME_PROCESS_ROLE_FOREGROUND_APP) {
    return 1;
  }

  for (uint32_t index = 0u; index < capacity; ++index) {
    if ((processes[index].state == PROCESS_STATE_UNUSED) ||
        (processes[index].state == PROCESS_STATE_ZOMBIE)) {
      continue;
    }

    if (processes[index].role == RUNTIME_PROCESS_ROLE_FOREGROUND_APP) {
      return 0;
    }
  }
#else
  (void)processes;
  (void)capacity;
  (void)role;
#endif

  return 1;
}

uint32_t runtime_policy_count_role(const struct process_runtime_state *processes, uint32_t capacity,
                                   uint32_t role) {
  uint32_t count = 0u;

  for (uint32_t index = 0u; index < capacity; ++index) {
    if ((processes[index].state == PROCESS_STATE_UNUSED) ||
        (processes[index].state == PROCESS_STATE_ZOMBIE)) {
      continue;
    }

    if (processes[index].role == role) {
      ++count;
    }
  }

  return count;
}

const char *scheduler_mode_name(void) {
#if CONFIG_SCHEDULER_COOPERATIVE
  return "cooperative";
#else
  return "preemptive";
#endif
}

const char *runtime_policy_mode_name(void) {
#if CONFIG_RUNTIME_SINGLE_FOREGROUND
  return "single_foreground";
#else
  return "multiprocess";
#endif
}

const char *runtime_process_role_name(uint32_t role) {
  switch (role) {
    case RUNTIME_PROCESS_ROLE_SYSTEM:
      return "system";
    case RUNTIME_PROCESS_ROLE_FOREGROUND_APP:
      return "foreground_app";
    case RUNTIME_PROCESS_ROLE_BACKGROUND_APP:
      return "background_app";
    case RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY:
      return "live_activity";
    default:
      return "none";
  }
}
