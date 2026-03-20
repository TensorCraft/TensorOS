#include "runtime_manage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct host_log_state {
  char runtime_banner_seen;
  char scheduler_seen;
  char runtime_mode_seen;
  char preempt_seen;
  char help_seen;
  char inspect_seen;
  char spawnables_seen;
  char shell_path_seen;
  char which_ls_seen;
  char inspect_name_seen;
  char inspect_role_seen;
  char inspect_state_seen;
  char inspect_wait_reason_seen;
  char spawnable_task_a_seen;
  char spawnable_task_ev_seen;
  char spawnable_process_taska_seen;
  char spawnable_process_eventwaiter_seen;
  char demo_status_seen;
  char demo_profile_smoke_seen;
  char demo_profile_off_seen;
  char demo_auto_on_seen;
  char demo_auto_off_seen;
  uint32_t process_count_logged;
  uint32_t foreground_count_logged;
  uint32_t pid_logs;
  uint32_t parent_pid_logged;
  uint32_t switch_count_logged;
  uint32_t yield_count_logged;
  uint32_t exit_code_logged;
  uint32_t wait_channel_logged;
  uint32_t wake_tick_logged;
  uint32_t kmem_free_bytes_logged;
  uint32_t kmem_largest_free_block_logged;
  uint32_t kmem_arena_capacity_logged;
  uint32_t kmem_bytes_in_use_logged;
  uint32_t kmem_peak_bytes_in_use_logged;
  uint32_t kmem_allocation_count_logged;
  uint32_t kmem_free_count_logged;
  uint32_t kmem_allocation_fail_count_logged;
  uint32_t kmem_live_allocations_logged;
  uint32_t kmem_peak_live_allocations_logged;
  uint32_t kmem_block_count_logged;
  uint32_t kmem_free_block_count_logged;
  uint32_t kmem_used_block_count_logged;
  uint32_t mailbox_has_message_logged;
  uint32_t mailbox_waiting_senders_logged;
  uint32_t mailbox_waiting_receivers_logged;
  uint32_t mailbox_send_count;
  uint32_t mailbox_recv_count;
  uint32_t last_send_message;
  uint32_t next_receive_message;
  uint32_t event_signal_count;
  uint32_t last_wake_pid;
  uint32_t last_kill_pid;
  int32_t last_kill_exit_code;
  uint32_t spawn_count;
  uint32_t spawnable_count_logged;
  uint32_t last_spawn_role;
  char last_spawn_name[24];
  uint32_t demo_profile;
  uint32_t demo_completed_steps_logged;
  uint32_t demo_total_steps_logged;
};

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_manage_host_test failed: %s\n", message);
    exit(1);
  }
}

static uint32_t host_snapshot(struct process_info *buffer, uint32_t capacity, void *context) {
  (void)context;

  if (capacity < 2u) {
    return 0u;
  }

  buffer[0].pid = 1u;
  buffer[0].role = RUNTIME_PROCESS_ROLE_SYSTEM;
  buffer[0].state = PROCESS_STATE_RUNNING;

  buffer[1].pid = 2u;
  buffer[1].parent_pid = 1u;
  buffer[1].name = "APPMAIN";
  buffer[1].role = RUNTIME_PROCESS_ROLE_FOREGROUND_APP;
  buffer[1].state = PROCESS_STATE_BLOCKED;
  buffer[1].exit_code = -3;
  buffer[1].switch_count = 5u;
  buffer[1].yield_count = 9u;
  buffer[1].wait_reason = PROCESS_WAIT_MANUAL;
  buffer[1].wait_channel = 77u;
  buffer[1].wake_tick = 88u;
  return 2u;
}

static uint32_t host_spawnable_snapshot(struct runtime_manage_spawnable_info *buffer,
                                        uint32_t capacity, void *context) {
  (void)context;

  if (capacity < 2u) {
    return 0u;
  }

  buffer[0].task_name = "a";
  buffer[0].process_name = "TASKA";
  buffer[0].default_role = RUNTIME_PROCESS_ROLE_FOREGROUND_APP;
  buffer[1].task_name = "ev";
  buffer[1].process_name = "EVENTWAITER";
  buffer[1].default_role = RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY;
  return 2u;
}

static int host_mailbox_send(uint32_t message, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  ++state->mailbox_send_count;
  state->last_send_message = message;
  return 1;
}

static int host_mailbox_receive(uint32_t *message, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  ++state->mailbox_recv_count;
  *message = state->next_receive_message;
  return 1;
}

static void host_kmem_stats(struct kmem_stats *stats, void *context) {
  (void)context;

  stats->arena_capacity_bytes = 2048u;
  stats->free_bytes = 1024u;
  stats->largest_free_block = 512u;
  stats->bytes_in_use = 768u;
  stats->peak_bytes_in_use = 1280u;
  stats->allocation_count = 12u;
  stats->free_count = 7u;
  stats->allocation_fail_count = 2u;
  stats->live_allocations = 5u;
  stats->peak_live_allocations = 6u;
  stats->block_count = 8u;
  stats->free_block_count = 3u;
  stats->used_block_count = 5u;
}

static int host_mailbox_has_message(void *context) {
  (void)context;
  return 1;
}

static uint32_t host_mailbox_waiting_senders(void *context) {
  (void)context;
  return 2u;
}

static uint32_t host_mailbox_waiting_receivers(void *context) {
  (void)context;
  return 3u;
}

static uint32_t host_event_signal(void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  ++state->event_signal_count;
  return 4u;
}

static int host_process_wake(uint32_t pid, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  state->last_wake_pid = pid;
  return 1;
}

static int host_process_kill(uint32_t pid, int32_t exit_code, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  state->last_kill_pid = pid;
  state->last_kill_exit_code = exit_code;
  return 1;
}

static uint32_t host_spawn_named_process(const char *task_name, uint32_t role, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;
  size_t index = 0u;

  ++state->spawn_count;
  state->last_spawn_role = role;
  while ((task_name[index] != '\0') && (index + 1u < sizeof(state->last_spawn_name))) {
    state->last_spawn_name[index] = task_name[index];
    ++index;
  }
  state->last_spawn_name[index] = '\0';
  return 99u;
}

static uint32_t host_demo_profile(void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  return state->demo_profile;
}

static int host_demo_set_profile(uint32_t profile, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  state->demo_profile = profile;
  return 1;
}

static uint32_t host_demo_completed_steps(void *context) {
  (void)context;
  return 3u;
}

static uint32_t host_demo_total_steps(void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  return state->demo_profile == RUNTIME_MANAGE_DEMO_PROFILE_SMOKE ? 5u : 0u;
}

static const char *host_scheduler_mode_name(void *context) {
  (void)context;
  return "cooperative";
}

static const char *host_runtime_mode_name(void *context) {
  (void)context;
  return "multiprocess";
}

static const char *host_preempt_status_name(void *context) {
  (void)context;
  return "cooperative_only";
}

static const char *host_shell_path(void *context) {
  (void)context;
  return "/system/bin:/bin";
}

static int host_resolve_command_path(const char *command, char *resolved_path, uint32_t capacity,
                                     void *context) {
  (void)context;
  const char *match = 0;
  uint32_t length = 0u;

  if ((command == 0) || (resolved_path == 0) || (capacity == 0u)) {
    return 0;
  }

  if (strcmp(command, "ls") == 0) {
    match = "/system/bin/ls";
  } else if (strcmp(command, "mkdir") == 0) {
    match = "/system/bin/mkdir";
  } else {
    return 0;
  }

  while ((match[length] != '\0') && ((length + 1u) < capacity)) {
    resolved_path[length] = match[length];
    ++length;
  }

  if (match[length] != '\0') {
    return 0;
  }

  resolved_path[length] = '\0';
  return 1;
}

static void host_log(const char *tag, const char *message, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  (void)tag;

  if (strcmp(message, "runtime") == 0) {
    state->runtime_banner_seen = 1;
  } else if (strcmp(message, "commands:") == 0) {
    state->help_seen = 1;
  } else if (strcmp(message, "inspect") == 0) {
    state->inspect_seen = 1;
  } else if (strcmp(message, "spawnables") == 0) {
    state->spawnables_seen = 1;
  } else if (strcmp(message, "/system/bin:/bin") == 0) {
    state->shell_path_seen = 1;
  } else if (strcmp(message, "/system/bin/ls") == 0) {
    state->which_ls_seen = 1;
  } else if (strcmp(message, "cooperative") == 0) {
    state->scheduler_seen = 1;
  } else if (strcmp(message, "multiprocess") == 0) {
    state->runtime_mode_seen = 1;
  } else if (strcmp(message, "cooperative_only") == 0) {
    state->preempt_seen = 1;
  } else if (strcmp(message, "a") == 0) {
    state->spawnable_task_a_seen = 1;
  } else if (strcmp(message, "ev") == 0) {
    state->spawnable_task_ev_seen = 1;
  } else if (strcmp(message, "TASKA") == 0) {
    state->spawnable_process_taska_seen = 1;
  } else if (strcmp(message, "EVENTWAITER") == 0) {
    state->spawnable_process_eventwaiter_seen = 1;
  } else if (strcmp(message, "APPMAIN") == 0) {
    state->inspect_name_seen = 1;
  } else if (strcmp(message, "foreground_app") == 0) {
    state->inspect_role_seen = 1;
  } else if (strcmp(message, "blocked") == 0) {
    state->inspect_state_seen = 1;
  } else if (strcmp(message, "manual") == 0) {
    state->inspect_wait_reason_seen = 1;
  } else if (strcmp(message, "demo") == 0) {
    state->demo_status_seen = 1;
  } else if (strcmp(message, "profile_smoke") == 0) {
    state->demo_profile_smoke_seen = 1;
  } else if (strcmp(message, "profile_off") == 0) {
    state->demo_profile_off_seen = 1;
  } else if (strcmp(message, "auto_on") == 0) {
    state->demo_auto_on_seen = 1;
  } else if (strcmp(message, "auto_off") == 0) {
    state->demo_auto_off_seen = 1;
  }
}

static void host_log_u32(const char *tag, const char *label, uint32_t value, void *context) {
  struct host_log_state *state = (struct host_log_state *)context;

  (void)tag;
  if (strcmp(label, "count=") == 0) {
    state->process_count_logged = value;
  } else if (strcmp(label, "foreground_count=") == 0) {
    state->foreground_count_logged = value;
  } else if (strcmp(label, "pid=") == 0) {
    ++state->pid_logs;
  } else if (strcmp(label, "parent_pid=") == 0) {
    state->parent_pid_logged = value;
  } else if (strcmp(label, "switch_count=") == 0) {
    state->switch_count_logged = value;
  } else if (strcmp(label, "yield_count=") == 0) {
    state->yield_count_logged = value;
  } else if (strcmp(label, "exit_code=") == 0) {
    state->exit_code_logged = value;
  } else if (strcmp(label, "wait_channel=") == 0) {
    state->wait_channel_logged = value;
  } else if (strcmp(label, "wake_tick=") == 0) {
    state->wake_tick_logged = value;
  } else if (strcmp(label, "kmem_free_bytes=") == 0) {
    state->kmem_free_bytes_logged = value;
  } else if (strcmp(label, "kmem_largest_free_block=") == 0) {
    state->kmem_largest_free_block_logged = value;
  } else if (strcmp(label, "kmem_arena_capacity_bytes=") == 0) {
    state->kmem_arena_capacity_logged = value;
  } else if (strcmp(label, "kmem_bytes_in_use=") == 0) {
    state->kmem_bytes_in_use_logged = value;
  } else if (strcmp(label, "kmem_peak_bytes_in_use=") == 0) {
    state->kmem_peak_bytes_in_use_logged = value;
  } else if (strcmp(label, "kmem_allocation_count=") == 0) {
    state->kmem_allocation_count_logged = value;
  } else if (strcmp(label, "kmem_free_count=") == 0) {
    state->kmem_free_count_logged = value;
  } else if (strcmp(label, "kmem_allocation_fail_count=") == 0) {
    state->kmem_allocation_fail_count_logged = value;
  } else if (strcmp(label, "kmem_live_allocations=") == 0) {
    state->kmem_live_allocations_logged = value;
  } else if (strcmp(label, "kmem_peak_live_allocations=") == 0) {
    state->kmem_peak_live_allocations_logged = value;
  } else if (strcmp(label, "kmem_block_count=") == 0) {
    state->kmem_block_count_logged = value;
  } else if (strcmp(label, "kmem_free_block_count=") == 0) {
    state->kmem_free_block_count_logged = value;
  } else if (strcmp(label, "kmem_used_block_count=") == 0) {
    state->kmem_used_block_count_logged = value;
  } else if (strcmp(label, "mailbox_has_message=") == 0) {
    state->mailbox_has_message_logged = value;
  } else if (strcmp(label, "mailbox_waiting_senders=") == 0) {
    state->mailbox_waiting_senders_logged = value;
  } else if (strcmp(label, "mailbox_waiting_receivers=") == 0) {
    state->mailbox_waiting_receivers_logged = value;
  } else if (strcmp(label, "spawnable_count=") == 0) {
    state->spawnable_count_logged = value;
  } else if (strcmp(label, "demo_completed_steps=") == 0) {
    state->demo_completed_steps_logged = value;
  } else if (strcmp(label, "demo_total_steps=") == 0) {
    state->demo_total_steps_logged = value;
  }
}

int main(void) {
  struct host_log_state state = {0};
  const struct runtime_manage_ops ops = {
      .snapshot = host_snapshot,
      .spawnable_snapshot = host_spawnable_snapshot,
      .kmem_stats = host_kmem_stats,
      .mailbox_has_message = host_mailbox_has_message,
      .mailbox_waiting_senders = host_mailbox_waiting_senders,
      .mailbox_waiting_receivers = host_mailbox_waiting_receivers,
      .mailbox_send = host_mailbox_send,
      .mailbox_receive = host_mailbox_receive,
      .event_signal = host_event_signal,
      .process_wake = host_process_wake,
      .process_kill = host_process_kill,
      .spawn_named_process = host_spawn_named_process,
      .demo_profile = host_demo_profile,
      .demo_set_profile = host_demo_set_profile,
      .demo_completed_steps = host_demo_completed_steps,
      .demo_total_steps = host_demo_total_steps,
      .shell_path = host_shell_path,
      .resolve_command_path = host_resolve_command_path,
      .scheduler_mode_name = host_scheduler_mode_name,
      .runtime_mode_name = host_runtime_mode_name,
      .preempt_status_name = host_preempt_status_name,
      .log = host_log,
      .log_u32 = host_log_u32,
  };

  state.next_receive_message = 0x345u;

  expect(runtime_manage_execute("runtime", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "runtime command succeeds");
  expect(state.runtime_banner_seen != 0, "runtime command logs runtime banner");
  expect(state.scheduler_seen != 0, "runtime command logs scheduler mode");
  expect(state.runtime_mode_seen != 0, "runtime command logs runtime mode");
  expect(state.preempt_seen != 0, "runtime command logs preempt information");

  expect(runtime_manage_execute("help", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "help command succeeds");
  expect(state.help_seen != 0, "help command logs command list banner");

  expect(runtime_manage_execute("path", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "path command succeeds");
  expect(state.shell_path_seen != 0, "path command logs shell path");

  expect(runtime_manage_execute("which ls", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "which command succeeds");
  expect(state.which_ls_seen != 0, "which command logs resolved path");

  expect(runtime_manage_execute("ps", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "ps command succeeds");
  expect(state.process_count_logged == 2u, "ps logs process count");
  expect(state.foreground_count_logged == 1u, "ps logs foreground count");
  expect(state.pid_logs == 2u, "ps logs each pid");
  expect(state.wait_channel_logged == 77u, "ps logs wait channel");
  expect(state.wake_tick_logged == 88u, "ps logs wake tick");

  expect(runtime_manage_execute("inspect 2", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "inspect command succeeds");
  expect(state.inspect_seen != 0, "inspect logs banner");
  expect(state.inspect_name_seen != 0, "inspect logs process name");
  expect(state.inspect_role_seen != 0, "inspect logs process role");
  expect(state.inspect_state_seen != 0, "inspect logs process state");
  expect(state.inspect_wait_reason_seen != 0, "inspect logs wait reason");
  expect(state.parent_pid_logged == 1u, "inspect logs parent pid");
  expect(state.switch_count_logged == 5u, "inspect logs switch count");
  expect(state.yield_count_logged == 9u, "inspect logs yield count");
  expect(state.exit_code_logged == (uint32_t)-3, "inspect logs exit code");

  expect(runtime_manage_execute("kmem", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "kmem command succeeds");
  expect(state.kmem_arena_capacity_logged == 2048u, "kmem logs arena capacity");
  expect(state.kmem_free_bytes_logged == 1024u, "kmem logs free bytes");
  expect(state.kmem_largest_free_block_logged == 512u, "kmem logs largest free block");
  expect(state.kmem_bytes_in_use_logged == 768u, "kmem logs bytes in use");
  expect(state.kmem_peak_bytes_in_use_logged == 1280u, "kmem logs peak bytes in use");
  expect(state.kmem_allocation_count_logged == 12u, "kmem logs allocation count");
  expect(state.kmem_free_count_logged == 7u, "kmem logs free count");
  expect(state.kmem_allocation_fail_count_logged == 2u, "kmem logs allocation failures");
  expect(state.kmem_live_allocations_logged == 5u, "kmem logs live allocations");
  expect(state.kmem_peak_live_allocations_logged == 6u, "kmem logs peak live allocations");
  expect(state.kmem_block_count_logged == 8u, "kmem logs block count");
  expect(state.kmem_free_block_count_logged == 3u, "kmem logs free block count");
  expect(state.kmem_used_block_count_logged == 5u, "kmem logs used block count");

  expect(runtime_manage_execute("mailbox status", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "mailbox status succeeds");
  expect(state.mailbox_has_message_logged == 1u, "mailbox status logs message flag");
  expect(state.mailbox_waiting_senders_logged == 2u, "mailbox status logs waiting senders");
  expect(state.mailbox_waiting_receivers_logged == 3u, "mailbox status logs waiting receivers");

  expect(runtime_manage_execute("mailbox send 0x123", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "mailbox send succeeds");
  expect(state.mailbox_send_count == 1u, "mailbox send callback runs");
  expect(state.last_send_message == 0x123u, "mailbox send parses hex values");

  expect(runtime_manage_execute("mailbox recv", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "mailbox recv succeeds");
  expect(state.mailbox_recv_count == 1u, "mailbox recv callback runs");

  expect(runtime_manage_execute("event signal", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "event signal succeeds");
  expect(state.event_signal_count == 1u, "event signal callback runs");

  expect(runtime_manage_execute("wake 12", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "wake command succeeds");
  expect(state.last_wake_pid == 12u, "wake forwards target pid");

  expect(runtime_manage_execute("kill 9 -7", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "kill command succeeds");
  expect(state.last_kill_pid == 9u, "kill forwards target pid");
  expect(state.last_kill_exit_code == -7, "kill forwards exit code");

  expect(runtime_manage_execute("spawn foreground a", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "spawn command succeeds");
  expect(state.spawn_count == 1u, "spawn callback runs");
  expect(state.last_spawn_role == RUNTIME_PROCESS_ROLE_FOREGROUND_APP,
         "spawn forwards runtime role");
  expect(strcmp(state.last_spawn_name, "a") == 0, "spawn forwards task name");

  expect(runtime_manage_execute("spawn list", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "spawn list succeeds");
  expect(state.spawnables_seen != 0, "spawn list logs banner");
  expect(state.spawnable_count_logged == 2u, "spawn list logs catalog count");
  expect(state.spawnable_task_a_seen != 0, "spawn list logs short task key");
  expect(state.spawnable_task_ev_seen != 0, "spawn list logs second task key");
  expect(state.spawnable_process_taska_seen != 0, "spawn list logs process label");
  expect(state.spawnable_process_eventwaiter_seen != 0, "spawn list logs second process label");

  state.demo_profile = RUNTIME_MANAGE_DEMO_PROFILE_SMOKE;
  expect(runtime_manage_execute("demo status", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "demo status succeeds");
  expect(state.demo_status_seen != 0, "demo status logs banner");
  expect(state.demo_profile_smoke_seen != 0, "demo status logs profile");
  expect(state.demo_auto_on_seen != 0, "demo status reflects enabled state");
  expect(state.demo_completed_steps_logged == 3u, "demo status logs completed steps");
  expect(state.demo_total_steps_logged == 5u, "demo status logs total steps");

  expect(runtime_manage_execute("demo auto off", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "demo auto off succeeds");
  expect(state.demo_profile == RUNTIME_MANAGE_DEMO_PROFILE_OFF, "demo auto off updates profile");

  expect(runtime_manage_execute("demo auto on", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "demo auto on succeeds");
  expect(state.demo_profile == RUNTIME_MANAGE_DEMO_PROFILE_SMOKE, "demo auto on updates profile");

  expect(runtime_manage_execute("demo profile off", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "demo profile off succeeds");
  expect(state.demo_profile == RUNTIME_MANAGE_DEMO_PROFILE_OFF,
         "demo profile off selects off profile");

  expect(runtime_manage_execute("demo profile smoke", &ops, &state) == RUNTIME_MANAGE_STATUS_OK,
         "demo profile smoke succeeds");
  expect(state.demo_profile == RUNTIME_MANAGE_DEMO_PROFILE_SMOKE,
         "demo profile smoke selects smoke profile");

  expect(runtime_manage_execute("mailbox send nope", &ops, &state) ==
             RUNTIME_MANAGE_STATUS_BAD_ARGUMENT,
         "mailbox send rejects bad numbers");
  expect(runtime_manage_execute("which missing", &ops, &state) ==
             RUNTIME_MANAGE_STATUS_BAD_ARGUMENT,
         "which rejects unresolved command");
  expect(runtime_manage_execute("inspect 999", &ops, &state) == RUNTIME_MANAGE_STATUS_BAD_ARGUMENT,
         "inspect rejects missing pid");
  expect(runtime_manage_execute("spawn nope a", &ops, &state) ==
             RUNTIME_MANAGE_STATUS_BAD_ARGUMENT,
         "spawn rejects bad role");
  expect(runtime_manage_execute("unknown", &ops, &state) ==
             RUNTIME_MANAGE_STATUS_UNKNOWN_COMMAND,
         "unknown command is rejected");

  expect(runtime_manage_status_name(RUNTIME_MANAGE_STATUS_UNAVAILABLE)[0] == 'u',
         "status names are stable");

  {
    struct runtime_manage_line_state line_state;
    enum runtime_manage_status status = RUNTIME_MANAGE_STATUS_OK;

    runtime_manage_line_init(&line_state);
    expect(runtime_manage_line_push_char(&line_state, 'p', &ops, &state, &status) == 0,
           "plain character does not execute command");
    expect(runtime_manage_line_push_char(&line_state, 's', &ops, &state, &status) == 0,
           "line buffer appends characters");
    expect(runtime_manage_line_push_char(&line_state, '\n', &ops, &state, &status) == 1,
           "newline executes buffered command");
    expect(status == RUNTIME_MANAGE_STATUS_OK, "line helper returns command status");

    runtime_manage_line_init(&line_state);
    expect(runtime_manage_line_push_char(&line_state, 'x', &ops, &state, &status) == 0,
           "single char is buffered");
    expect(runtime_manage_line_push_char(&line_state, '\b', &ops, &state, &status) == 0,
           "backspace edits buffered line");
    expect(runtime_manage_line_push_char(&line_state, '\n', &ops, &state, &status) == 1,
           "newline completes empty line");
    expect(status == RUNTIME_MANAGE_STATUS_EMPTY, "empty line reports empty status");

    runtime_manage_line_init(&line_state);
    for (uint32_t i = 0u; i < RUNTIME_MANAGE_LINE_CAPACITY; ++i) {
      if (runtime_manage_line_push_char(&line_state, 'a', &ops, &state, &status) != 0) {
        break;
      }
    }
    expect(status == RUNTIME_MANAGE_STATUS_BAD_ARGUMENT, "overflow reports bad argument");
  }

  puts("runtime management host checks passed");
  return 0;
}
