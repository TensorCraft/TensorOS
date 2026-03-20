#include "runtime_manage.h"

#include <stdint.h>

static int ascii_is_space(char ch) {
  return (ch == ' ') || (ch == '\t') || (ch == '\n') || (ch == '\r');
}

static const char *skip_spaces(const char *cursor) {
  while ((*cursor != '\0') && ascii_is_space(*cursor)) {
    ++cursor;
  }

  return cursor;
}

static const char *next_token(const char *cursor, const char **start, uint32_t *length) {
  const char *token_start = skip_spaces(cursor);
  uint32_t token_length = 0u;

  while ((token_start[token_length] != '\0') && !ascii_is_space(token_start[token_length])) {
    ++token_length;
  }

  *start = token_start;
  *length = token_length;
  return token_start + token_length;
}

static int token_equals(const char *token, uint32_t length, const char *word) {
  uint32_t index = 0u;

  while ((index < length) && (word[index] != '\0')) {
    if (token[index] != word[index]) {
      return 0;
    }
    ++index;
  }

  return (index == length) && (word[index] == '\0');
}

static int token_is_empty(const char *token, uint32_t length) {
  (void)token;
  return length == 0u;
}

static int parse_u32_token(const char *token, uint32_t length, uint32_t *value) {
  uint32_t base = 10u;
  uint32_t result = 0u;
  uint32_t index = 0u;

  if (length == 0u) {
    return 0;
  }

  if ((length > 2u) && (token[0] == '0') && ((token[1] == 'x') || (token[1] == 'X'))) {
    base = 16u;
    index = 2u;
  }

  if (index == length) {
    return 0;
  }

  for (; index < length; ++index) {
    uint32_t digit;
    char ch = token[index];

    if ((ch >= '0') && (ch <= '9')) {
      digit = (uint32_t)(ch - '0');
    } else if ((base == 16u) && (ch >= 'a') && (ch <= 'f')) {
      digit = 10u + (uint32_t)(ch - 'a');
    } else if ((base == 16u) && (ch >= 'A') && (ch <= 'F')) {
      digit = 10u + (uint32_t)(ch - 'A');
    } else {
      return 0;
    }

    if (digit >= base) {
      return 0;
    }

    result = (result * base) + digit;
  }

  *value = result;
  return 1;
}

static int parse_i32_token(const char *token, uint32_t length, int32_t *value) {
  uint32_t unsigned_value = 0u;

  if ((length > 1u) && (token[0] == '-')) {
    if (!parse_u32_token(token + 1, length - 1u, &unsigned_value) ||
        (unsigned_value > 2147483648u)) {
      return 0;
    }

    *value = -(int32_t)unsigned_value;
    return 1;
  }

  if (!parse_u32_token(token, length, &unsigned_value) || (unsigned_value > 2147483647u)) {
    return 0;
  }

  *value = (int32_t)unsigned_value;
  return 1;
}

static void runtime_manage_log_help(const struct runtime_manage_ops *ops, void *context) {
  if (ops->log == 0) {
    return;
  }

  ops->log("MGMT", "commands:", context);
  ops->log("MGMT", "help", context);
  ops->log("MGMT", "runtime", context);
  ops->log("MGMT", "path", context);
  ops->log("MGMT", "which <name>", context);
  ops->log("MGMT", "ps", context);
  ops->log("MGMT", "inspect <pid>", context);
  ops->log("MGMT", "kmem", context);
  ops->log("MGMT", "mailbox status", context);
  ops->log("MGMT", "mailbox send <value>", context);
  ops->log("MGMT", "mailbox recv", context);
  ops->log("MGMT", "event signal", context);
  ops->log("MGMT", "wake <pid>", context);
  ops->log("MGMT", "kill <pid> [exit_code]", context);
  ops->log("MGMT", "spawn list", context);
  ops->log("MGMT", "spawn <role> <task>", context);
  ops->log("MGMT", "demo status", context);
  ops->log("MGMT", "demo auto on", context);
  ops->log("MGMT", "demo auto off", context);
  ops->log("MGMT", "demo profile smoke", context);
  ops->log("MGMT", "demo profile off", context);
}

static const char *runtime_manage_demo_profile_name(uint32_t profile) {
  switch (profile) {
    case RUNTIME_MANAGE_DEMO_PROFILE_SMOKE:
      return "profile_smoke";
    case RUNTIME_MANAGE_DEMO_PROFILE_OFF:
      return "profile_off";
    default:
      return "profile_unknown";
  }
}

static const char *process_state_name(uint32_t state) {
  switch (state) {
    case PROCESS_STATE_READY:
      return "ready";
    case PROCESS_STATE_RUNNING:
      return "running";
    case PROCESS_STATE_BLOCKED:
      return "blocked";
    case PROCESS_STATE_ZOMBIE:
      return "zombie";
    default:
      return "unused";
  }
}

static const char *process_wait_reason_name(uint32_t wait_reason) {
  switch (wait_reason) {
    case PROCESS_WAIT_SLEEP_TICKS:
      return "sleep_ticks";
    case PROCESS_WAIT_MANUAL:
      return "manual";
    case PROCESS_WAIT_CHILD:
      return "child";
    case PROCESS_WAIT_EVENT_CHANNEL:
      return "event_channel";
    default:
      return "none";
  }
}

static uint32_t count_role_in_snapshot(const struct process_info *snapshot, uint32_t count,
                                       uint32_t role) {
  uint32_t matches = 0u;

  for (uint32_t index = 0u; index < count; ++index) {
    if (snapshot[index].role == role) {
      ++matches;
    }
  }

  return matches;
}

static void runtime_manage_log_runtime(const struct runtime_manage_ops *ops, void *context) {
  if ((ops->log == 0) || (ops->scheduler_mode_name == 0) || (ops->runtime_mode_name == 0) ||
      (ops->preempt_status_name == 0)) {
    return;
  }

  ops->log("MGMT", "runtime", context);
  ops->log("MGMT", ops->scheduler_mode_name(context), context);
  ops->log("MGMT", ops->runtime_mode_name(context), context);
  ops->log("MGMT", ops->preempt_status_name(context), context);
}

static void runtime_manage_log_snapshot(const struct runtime_manage_ops *ops, void *context) {
  struct process_info snapshot[8];
  uint32_t count;

  if ((ops->snapshot == 0) || (ops->log == 0) || (ops->log_u32 == 0)) {
    return;
  }

  count = ops->snapshot(snapshot, 8u, context);
  ops->log_u32("PROC", "count=", count, context);
  ops->log_u32("PROC", "foreground_count=",
               count_role_in_snapshot(snapshot, count, RUNTIME_PROCESS_ROLE_FOREGROUND_APP),
               context);

  for (uint32_t index = 0u; (index < count) && (index < 8u); ++index) {
    ops->log_u32("PROC", "pid=", snapshot[index].pid, context);
    ops->log("PROC", runtime_process_role_name(snapshot[index].role), context);
    ops->log("PROC", process_state_name(snapshot[index].state), context);
    if (snapshot[index].wait_reason != PROCESS_WAIT_NONE) {
      ops->log("PROC", process_wait_reason_name(snapshot[index].wait_reason), context);
      ops->log_u32("PROC", "wait_channel=", snapshot[index].wait_channel, context);
    }
    if (snapshot[index].wake_tick != 0u) {
      ops->log_u32("PROC", "wake_tick=", (uint32_t)snapshot[index].wake_tick, context);
    }
  }
}

static int runtime_manage_log_process_detail(const struct runtime_manage_ops *ops, uint32_t target_pid,
                                             void *context) {
  struct process_info snapshot[8];
  uint32_t count;

  if ((ops->snapshot == 0) || (ops->log == 0) || (ops->log_u32 == 0)) {
    return 0;
  }

  count = ops->snapshot(snapshot, 8u, context);
  for (uint32_t index = 0u; (index < count) && (index < 8u); ++index) {
    if (snapshot[index].pid != target_pid) {
      continue;
    }

    ops->log("PROC", "inspect", context);
    ops->log_u32("PROC", "pid=", snapshot[index].pid, context);
    ops->log_u32("PROC", "parent_pid=", snapshot[index].parent_pid, context);
    if (snapshot[index].name != 0) {
      ops->log("PROC", snapshot[index].name, context);
    }
    ops->log("PROC", runtime_process_role_name(snapshot[index].role), context);
    ops->log("PROC", process_state_name(snapshot[index].state), context);
    ops->log_u32("PROC", "switch_count=", snapshot[index].switch_count, context);
    ops->log_u32("PROC", "yield_count=", snapshot[index].yield_count, context);
    ops->log_u32("PROC", "exit_code=", (uint32_t)snapshot[index].exit_code, context);
    if (snapshot[index].wait_reason != PROCESS_WAIT_NONE) {
      ops->log("PROC", process_wait_reason_name(snapshot[index].wait_reason), context);
      ops->log_u32("PROC", "wait_channel=", snapshot[index].wait_channel, context);
    }
    if (snapshot[index].wake_tick != 0u) {
      ops->log_u32("PROC", "wake_tick=", (uint32_t)snapshot[index].wake_tick, context);
    }
    return 1;
  }

  return 0;
}

static void runtime_manage_log_kmem(const struct runtime_manage_ops *ops, void *context) {
  struct kmem_stats stats;

  if ((ops->log_u32 == 0) || (ops->kmem_stats == 0)) {
    return;
  }

  ops->kmem_stats(&stats, context);
  ops->log_u32("MGMT", "kmem_arena_capacity_bytes=", stats.arena_capacity_bytes, context);
  ops->log_u32("MGMT", "kmem_free_bytes=", stats.free_bytes, context);
  ops->log_u32("MGMT", "kmem_largest_free_block=", stats.largest_free_block, context);
  ops->log_u32("MGMT", "kmem_bytes_in_use=", stats.bytes_in_use, context);
  ops->log_u32("MGMT", "kmem_peak_bytes_in_use=", stats.peak_bytes_in_use, context);
  ops->log_u32("MGMT", "kmem_allocation_count=", stats.allocation_count, context);
  ops->log_u32("MGMT", "kmem_free_count=", stats.free_count, context);
  ops->log_u32("MGMT", "kmem_allocation_fail_count=", stats.allocation_fail_count, context);
  ops->log_u32("MGMT", "kmem_live_allocations=", stats.live_allocations, context);
  ops->log_u32("MGMT", "kmem_peak_live_allocations=", stats.peak_live_allocations, context);
  ops->log_u32("MGMT", "kmem_block_count=", stats.block_count, context);
  ops->log_u32("MGMT", "kmem_free_block_count=", stats.free_block_count, context);
  ops->log_u32("MGMT", "kmem_used_block_count=", stats.used_block_count, context);
}

static void runtime_manage_log_mailbox_status(const struct runtime_manage_ops *ops, void *context) {
  if ((ops->log_u32 == 0) || (ops->mailbox_has_message == 0) ||
      (ops->mailbox_waiting_senders == 0) || (ops->mailbox_waiting_receivers == 0)) {
    return;
  }

  ops->log_u32("MGMT", "mailbox_has_message=", (uint32_t)ops->mailbox_has_message(context),
               context);
  ops->log_u32("MGMT", "mailbox_waiting_senders=", ops->mailbox_waiting_senders(context),
               context);
  ops->log_u32("MGMT", "mailbox_waiting_receivers=", ops->mailbox_waiting_receivers(context),
               context);
}

static void runtime_manage_log_demo_status(const struct runtime_manage_ops *ops, void *context) {
  if ((ops->log == 0) || (ops->demo_profile == 0)) {
    return;
  }

  ops->log("MGMT", "demo", context);
  ops->log("MGMT", runtime_manage_demo_profile_name(ops->demo_profile(context)), context);
  ops->log("MGMT",
           ops->demo_profile(context) == RUNTIME_MANAGE_DEMO_PROFILE_OFF ? "auto_off" : "auto_on",
           context);

  if ((ops->log_u32 != 0) && (ops->demo_completed_steps != 0) && (ops->demo_total_steps != 0)) {
    ops->log_u32("MGMT", "demo_completed_steps=", ops->demo_completed_steps(context), context);
    ops->log_u32("MGMT", "demo_total_steps=", ops->demo_total_steps(context), context);
  }
}

static void runtime_manage_log_spawnables(const struct runtime_manage_ops *ops, void *context) {
  struct runtime_manage_spawnable_info snapshot[RUNTIME_MANAGE_SPAWNABLE_SNAPSHOT_CAPACITY];
  uint32_t count;

  if ((ops->spawnable_snapshot == 0) || (ops->log == 0) || (ops->log_u32 == 0)) {
    return;
  }

  count = ops->spawnable_snapshot(snapshot, RUNTIME_MANAGE_SPAWNABLE_SNAPSHOT_CAPACITY, context);
  ops->log("MGMT", "spawnables", context);
  ops->log_u32("MGMT", "spawnable_count=", count, context);

  for (uint32_t index = 0u; (index < count) && (index < RUNTIME_MANAGE_SPAWNABLE_SNAPSHOT_CAPACITY);
       ++index) {
    if (snapshot[index].task_name != 0) {
      ops->log("MGMT", snapshot[index].task_name, context);
    }
    if (snapshot[index].process_name != 0) {
      ops->log("MGMT", snapshot[index].process_name, context);
    }
    ops->log("MGMT", runtime_process_role_name(snapshot[index].default_role), context);
  }
}

static int runtime_manage_parse_role(const char *token, uint32_t length, uint32_t *role) {
  if (token_equals(token, length, "system")) {
    *role = RUNTIME_PROCESS_ROLE_SYSTEM;
    return 1;
  }
  if (token_equals(token, length, "foreground") || token_equals(token, length, "foreground_app")) {
    *role = RUNTIME_PROCESS_ROLE_FOREGROUND_APP;
    return 1;
  }
  if (token_equals(token, length, "background") || token_equals(token, length, "background_app")) {
    *role = RUNTIME_PROCESS_ROLE_BACKGROUND_APP;
    return 1;
  }
  if (token_equals(token, length, "live") || token_equals(token, length, "live_activity")) {
    *role = RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY;
    return 1;
  }

  return 0;
}

static void runtime_manage_log_shell_path(const struct runtime_manage_ops *ops, void *context) {
  if ((ops->log == 0) || (ops->shell_path == 0) || (ops->shell_path(context) == 0)) {
    return;
  }

  ops->log("MGMT", ops->shell_path(context), context);
}

enum runtime_manage_status runtime_manage_execute(const char *command,
                                                  const struct runtime_manage_ops *ops,
                                                  void *context) {
  const char *token = 0;
  uint32_t length = 0u;
  const char *cursor;

  if ((command == 0) || (ops == 0)) {
    return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
  }

  cursor = next_token(command, &token, &length);
  if (token_is_empty(token, length)) {
    return RUNTIME_MANAGE_STATUS_EMPTY;
  }

  if (token_equals(token, length, "runtime")) {
    runtime_manage_log_runtime(ops, context);
    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "help")) {
    runtime_manage_log_help(ops, context);
    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "path")) {
    if (ops->shell_path == 0) {
      return RUNTIME_MANAGE_STATUS_UNAVAILABLE;
    }
    runtime_manage_log_shell_path(ops, context);
    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "which")) {
    const char *command_token = 0;
    uint32_t command_length = 0u;
    char command_name[24];
    char resolved_path[RUNTIME_MANAGE_PATH_CAPACITY];

    cursor = next_token(cursor, &command_token, &command_length);
    if (token_is_empty(command_token, command_length) || (command_length >= sizeof(command_name)) ||
        (ops->resolve_command_path == 0) || (ops->log == 0)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    for (uint32_t index = 0u; index < command_length; ++index) {
      command_name[index] = command_token[index];
    }
    command_name[command_length] = '\0';

    if (!ops->resolve_command_path(command_name, resolved_path, sizeof(resolved_path), context)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    ops->log("MGMT", resolved_path, context);
    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "ps")) {
    runtime_manage_log_snapshot(ops, context);
    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "inspect")) {
    const char *pid_token = 0;
    uint32_t pid_length = 0u;
    uint32_t pid = 0u;

    cursor = next_token(cursor, &pid_token, &pid_length);
    if (!parse_u32_token(pid_token, pid_length, &pid)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    return runtime_manage_log_process_detail(ops, pid, context) ? RUNTIME_MANAGE_STATUS_OK
                                                                : RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
  }

  if (token_equals(token, length, "kmem")) {
    runtime_manage_log_kmem(ops, context);
    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "wake")) {
    const char *pid_token = 0;
    uint32_t pid_length = 0u;
    uint32_t pid = 0u;

    cursor = next_token(cursor, &pid_token, &pid_length);
    if (!parse_u32_token(pid_token, pid_length, &pid) || (ops->process_wake == 0) ||
        (ops->process_wake(pid, context) == 0)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "kill")) {
    const char *pid_token = 0;
    const char *exit_token = 0;
    uint32_t pid_length = 0u;
    uint32_t exit_length = 0u;
    uint32_t pid = 0u;
    int32_t exit_code = -1;

    cursor = next_token(cursor, &pid_token, &pid_length);
    if (!parse_u32_token(pid_token, pid_length, &pid) || (ops->process_kill == 0)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    cursor = next_token(cursor, &exit_token, &exit_length);
    if (!token_is_empty(exit_token, exit_length) &&
        !parse_i32_token(exit_token, exit_length, &exit_code)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    if (ops->process_kill(pid, exit_code, context) == 0) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "spawn")) {
    const char *role_token = 0;
    const char *task_token = 0;
    uint32_t role_length = 0u;
    uint32_t task_length = 0u;
    uint32_t role = RUNTIME_PROCESS_ROLE_NONE;
    char task_name[24];

    cursor = next_token(cursor, &role_token, &role_length);
    if (token_equals(role_token, role_length, "list")) {
      if (ops->spawnable_snapshot == 0) {
        return RUNTIME_MANAGE_STATUS_UNAVAILABLE;
      }
      runtime_manage_log_spawnables(ops, context);
      return RUNTIME_MANAGE_STATUS_OK;
    }
    cursor = next_token(cursor, &task_token, &task_length);
    if (!runtime_manage_parse_role(role_token, role_length, &role) || token_is_empty(task_token, task_length) ||
        (task_length >= sizeof(task_name)) || (ops->spawn_named_process == 0)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    for (uint32_t index = 0u; index < task_length; ++index) {
      task_name[index] = task_token[index];
    }
    task_name[task_length] = '\0';

    if (ops->spawn_named_process(task_name, role, context) == 0u) {
      return RUNTIME_MANAGE_STATUS_UNAVAILABLE;
    }

    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "event")) {
    const char *verb = 0;
    uint32_t verb_length = 0u;

    cursor = next_token(cursor, &verb, &verb_length);
    if (!token_equals(verb, verb_length, "signal") || (ops->event_signal == 0)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    (void)ops->event_signal(context);
    return RUNTIME_MANAGE_STATUS_OK;
  }

  if (token_equals(token, length, "demo")) {
    const char *verb = 0;
    uint32_t verb_length = 0u;

    cursor = next_token(cursor, &verb, &verb_length);
    if (token_equals(verb, verb_length, "status")) {
      runtime_manage_log_demo_status(ops, context);
      return RUNTIME_MANAGE_STATUS_OK;
    }

    if (token_equals(verb, verb_length, "auto")) {
      const char *mode = 0;
      uint32_t mode_length = 0u;

      cursor = next_token(cursor, &mode, &mode_length);
      if ((ops->demo_set_profile == 0) || token_is_empty(mode, mode_length)) {
        return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
      }

      if (token_equals(mode, mode_length, "on")) {
        if (ops->demo_set_profile(RUNTIME_MANAGE_DEMO_PROFILE_SMOKE, context) == 0) {
          return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
        }
        return RUNTIME_MANAGE_STATUS_OK;
      }

      if (token_equals(mode, mode_length, "off")) {
        if (ops->demo_set_profile(RUNTIME_MANAGE_DEMO_PROFILE_OFF, context) == 0) {
          return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
        }
        return RUNTIME_MANAGE_STATUS_OK;
      }
    }

    if (token_equals(verb, verb_length, "profile")) {
      const char *profile = 0;
      uint32_t profile_length = 0u;

      cursor = next_token(cursor, &profile, &profile_length);
      if ((ops->demo_set_profile == 0) || token_is_empty(profile, profile_length)) {
        return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
      }

      if (token_equals(profile, profile_length, "smoke")) {
        if (ops->demo_set_profile(RUNTIME_MANAGE_DEMO_PROFILE_SMOKE, context) == 0) {
          return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
        }
        return RUNTIME_MANAGE_STATUS_OK;
      }

      if (token_equals(profile, profile_length, "off")) {
        if (ops->demo_set_profile(RUNTIME_MANAGE_DEMO_PROFILE_OFF, context) == 0) {
          return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
        }
        return RUNTIME_MANAGE_STATUS_OK;
      }
    }

    return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
  }

  if (token_equals(token, length, "mailbox")) {
    const char *verb = 0;
    uint32_t verb_length = 0u;

    cursor = next_token(cursor, &verb, &verb_length);
    if (token_is_empty(verb, verb_length)) {
      return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    }

    if (token_equals(verb, verb_length, "send")) {
      const char *value_token = 0;
      uint32_t value_length = 0u;
      uint32_t message = 0u;

      cursor = next_token(cursor, &value_token, &value_length);
      if (!parse_u32_token(value_token, value_length, &message)) {
        return RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
      }

      if ((ops->mailbox_send == 0) || (ops->mailbox_send(message, context) == 0)) {
        return RUNTIME_MANAGE_STATUS_UNAVAILABLE;
      }

      return RUNTIME_MANAGE_STATUS_OK;
    }

    if (token_equals(verb, verb_length, "status")) {
      runtime_manage_log_mailbox_status(ops, context);
      return RUNTIME_MANAGE_STATUS_OK;
    }

    if (token_equals(verb, verb_length, "recv")) {
      uint32_t message = 0u;

      if ((ops->mailbox_receive == 0) || (ops->mailbox_receive(&message, context) == 0)) {
        return RUNTIME_MANAGE_STATUS_UNAVAILABLE;
      }

      return RUNTIME_MANAGE_STATUS_OK;
    }

    return RUNTIME_MANAGE_STATUS_UNKNOWN_COMMAND;
  }

  return RUNTIME_MANAGE_STATUS_UNKNOWN_COMMAND;
}

const char *runtime_manage_status_name(enum runtime_manage_status status) {
  switch (status) {
    case RUNTIME_MANAGE_STATUS_OK:
      return "ok";
    case RUNTIME_MANAGE_STATUS_EMPTY:
      return "empty";
    case RUNTIME_MANAGE_STATUS_UNKNOWN_COMMAND:
      return "unknown_command";
    case RUNTIME_MANAGE_STATUS_BAD_ARGUMENT:
      return "bad_argument";
    case RUNTIME_MANAGE_STATUS_UNAVAILABLE:
      return "unavailable";
    default:
      return "unknown_status";
  }
}

void runtime_manage_line_init(struct runtime_manage_line_state *state) {
  if (state == 0) {
    return;
  }

  state->length = 0u;
  state->buffer[0] = '\0';
}

int runtime_manage_line_push_char(struct runtime_manage_line_state *state, char ch,
                                  const struct runtime_manage_ops *ops, void *context,
                                  enum runtime_manage_status *status) {
  if ((state == 0) || (status == 0)) {
    return 0;
  }

  *status = RUNTIME_MANAGE_STATUS_EMPTY;

  if ((ch == '\b') || (ch == 0x7Fu)) {
    if (state->length != 0u) {
      --state->length;
      state->buffer[state->length] = '\0';
    }
    return 0;
  }

  if ((ch == '\r') || (ch == '\n')) {
    if (state->length == 0u) {
      *status = RUNTIME_MANAGE_STATUS_EMPTY;
      return 1;
    }

    state->buffer[state->length] = '\0';
    *status = runtime_manage_execute(state->buffer, ops, context);
    runtime_manage_line_init(state);
    return 1;
  }

  if ((ch < ' ') || (ch > '~')) {
    return 0;
  }

  if (state->length >= (RUNTIME_MANAGE_LINE_CAPACITY - 1u)) {
    runtime_manage_line_init(state);
    *status = RUNTIME_MANAGE_STATUS_BAD_ARGUMENT;
    return 1;
  }

  state->buffer[state->length++] = ch;
  state->buffer[state->length] = '\0';
  return 0;
}
