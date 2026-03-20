#pragma once

#include "runtime.h"

struct process_runtime_state {
  process_id_t pid;
  process_id_t parent_pid;
  const char *name;
  uint32_t role;
  uint32_t state;
  int32_t exit_code;
  uint32_t switch_count;
  uint32_t yield_count;
  uint64_t wake_tick;
  process_id_t wait_target_pid;
  uint32_t wait_reason;
  uint32_t wait_channel;
};
