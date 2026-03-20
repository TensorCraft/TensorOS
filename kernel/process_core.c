#include "process_core.h"

#include <limits.h>

uint32_t process_core_find_free_slot(const struct process_runtime_state *processes, uint32_t capacity) {
  for (uint32_t index = 0u; index < capacity; ++index) {
    if (processes[index].state == PROCESS_STATE_UNUSED) {
      return index;
    }
  }

  return UINT32_MAX;
}

uint32_t process_core_find_index_by_pid(const struct process_runtime_state *processes, uint32_t capacity,
                                        process_id_t pid) {
  if (pid == 0u) {
    return UINT32_MAX;
  }

  for (uint32_t index = 0u; index < capacity; ++index) {
    if ((processes[index].state != PROCESS_STATE_UNUSED) && (processes[index].pid == pid)) {
      return index;
    }
  }

  return UINT32_MAX;
}

uint32_t process_core_find_next_runnable(const struct process_runtime_state *processes, uint32_t capacity,
                                         uint32_t current_index) {
  uint32_t start_index = (current_index == UINT32_MAX) ? 0u : (current_index + 1u);

  for (uint32_t offset = 0u; offset < capacity; ++offset) {
    uint32_t index = (start_index + offset) % capacity;
    if (processes[index].state == PROCESS_STATE_READY) {
      return index;
    }
  }

  return UINT32_MAX;
}

uint32_t process_core_count_blocked_sleepers(const struct process_runtime_state *processes,
                                             uint32_t capacity) {
  uint32_t count = 0u;

  for (uint32_t index = 0u; index < capacity; ++index) {
    if ((processes[index].state == PROCESS_STATE_BLOCKED) &&
        (processes[index].wait_reason == PROCESS_WAIT_SLEEP_TICKS)) {
      ++count;
    }
  }

  return count;
}

void process_core_reset_slot(struct process_runtime_state *process) {
  process->pid = 0u;
  process->parent_pid = 0u;
  process->name = 0;
  process->role = RUNTIME_PROCESS_ROLE_NONE;
  process->state = PROCESS_STATE_UNUSED;
  process->exit_code = 0;
  process->switch_count = 0u;
  process->yield_count = 0u;
  process->wake_tick = 0u;
  process->wait_target_pid = 0u;
  process->wait_reason = PROCESS_WAIT_NONE;
  process->wait_channel = 0u;
}

void process_core_make_ready(struct process_runtime_state *process) {
  if (process->state == PROCESS_STATE_BLOCKED) {
    process->state = PROCESS_STATE_READY;
    process->wake_tick = 0u;
    process->wait_target_pid = 0u;
    process->wait_reason = PROCESS_WAIT_NONE;
    process->wait_channel = 0u;
  }
}

void process_core_block(struct process_runtime_state *process, uint32_t wait_reason,
                        uint32_t wait_channel, uint64_t wake_tick,
                        process_id_t wait_target_pid) {
  process->state = PROCESS_STATE_BLOCKED;
  process->wait_reason = wait_reason;
  process->wait_channel = wait_channel;
  process->wake_tick = wake_tick;
  process->wait_target_pid = wait_target_pid;
}

static int process_core_is_child_of(const struct process_runtime_state *processes,
                                    process_id_t parent_pid, uint32_t child_index) {
  return (parent_pid != 0u) && (processes[child_index].parent_pid == parent_pid);
}

uint32_t process_core_find_waitable_child(const struct process_runtime_state *processes,
                                          uint32_t capacity, process_id_t parent_pid,
                                          process_id_t target_pid) {
  for (uint32_t index = 0u; index < capacity; ++index) {
    if (processes[index].state == PROCESS_STATE_UNUSED) {
      continue;
    }

    if (!process_core_is_child_of(processes, parent_pid, index)) {
      continue;
    }

    if ((target_pid != 0u) && (processes[index].pid != target_pid)) {
      continue;
    }

    if (processes[index].state == PROCESS_STATE_ZOMBIE) {
      return index;
    }
  }

  return UINT32_MAX;
}

int process_core_has_child(const struct process_runtime_state *processes, uint32_t capacity,
                           process_id_t parent_pid, process_id_t target_pid) {
  for (uint32_t index = 0u; index < capacity; ++index) {
    if (processes[index].state == PROCESS_STATE_UNUSED) {
      continue;
    }

    if (!process_core_is_child_of(processes, parent_pid, index)) {
      continue;
    }

    if ((target_pid == 0u) || (processes[index].pid == target_pid)) {
      return 1;
    }
  }

  return 0;
}

uint32_t process_core_wake_waiting_parent(struct process_runtime_state *processes, uint32_t capacity,
                                          process_id_t child_pid) {
  uint32_t woke = 0u;

  for (uint32_t index = 0u; index < capacity; ++index) {
    if ((processes[index].state != PROCESS_STATE_BLOCKED) ||
        (processes[index].wait_reason != PROCESS_WAIT_CHILD)) {
      continue;
    }

    if ((processes[index].wait_target_pid == 0u) ||
        (processes[index].wait_target_pid == child_pid)) {
      process_core_make_ready(&processes[index]);
      ++woke;
    }
  }

  return woke;
}

uint32_t process_core_wake_sleepers(struct process_runtime_state *processes, uint32_t capacity,
                                    uint64_t tick_count) {
  uint32_t woke = 0u;

  for (uint32_t index = 0u; index < capacity; ++index) {
    if ((processes[index].state == PROCESS_STATE_BLOCKED) &&
        (processes[index].wait_reason == PROCESS_WAIT_SLEEP_TICKS) &&
        (processes[index].wake_tick <= tick_count)) {
      process_core_make_ready(&processes[index]);
      ++woke;
    }
  }

  return woke;
}

uint32_t process_core_wake_channel(struct process_runtime_state *processes, uint32_t capacity,
                                   uint32_t channel) {
  uint32_t woke = 0u;

  for (uint32_t index = 0u; index < capacity; ++index) {
    if ((processes[index].state == PROCESS_STATE_BLOCKED) &&
        (processes[index].wait_reason == PROCESS_WAIT_EVENT_CHANNEL) &&
        (processes[index].wait_channel == channel)) {
      process_core_make_ready(&processes[index]);
      ++woke;
    }
  }

  return woke;
}

uint32_t process_core_wake_first_channel(struct process_runtime_state *processes, uint32_t capacity,
                                         uint32_t channel, uint32_t current_index) {
  if (capacity == 0u) {
    return 0u;
  }

  uint32_t start_index = (current_index == UINT32_MAX) ? 0u : (current_index + 1u);

  for (uint32_t offset = 0u; offset < capacity; ++offset) {
    uint32_t index = (start_index + offset) % capacity;
    if ((processes[index].state == PROCESS_STATE_BLOCKED) &&
        (processes[index].wait_reason == PROCESS_WAIT_EVENT_CHANNEL) &&
        (processes[index].wait_channel == channel)) {
      process_core_make_ready(&processes[index]);
      return 1u;
    }
  }

  return 0u;
}

uint32_t process_core_snapshot(const struct process_runtime_state *processes, uint32_t capacity,
                               struct process_info *buffer, uint32_t buffer_capacity) {
  uint32_t written = 0u;

  for (uint32_t index = 0u; index < capacity; ++index) {
    if (processes[index].state == PROCESS_STATE_UNUSED) {
      continue;
    }

    if ((buffer != 0) && (written < buffer_capacity)) {
      buffer[written].pid = processes[index].pid;
      buffer[written].parent_pid = processes[index].parent_pid;
      buffer[written].name = processes[index].name;
      buffer[written].role = processes[index].role;
      buffer[written].state = processes[index].state;
      buffer[written].exit_code = processes[index].exit_code;
      buffer[written].switch_count = processes[index].switch_count;
      buffer[written].yield_count = processes[index].yield_count;
      buffer[written].wake_tick = processes[index].wake_tick;
      buffer[written].wait_reason = processes[index].wait_reason;
      buffer[written].wait_channel = processes[index].wait_channel;
    }

    ++written;
  }

  return written;
}
