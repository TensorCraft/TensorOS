#pragma once

#include "runtime.h"

#include <stdint.h>

#define RUNTIME_MANAGE_LINE_CAPACITY 64u
#define RUNTIME_MANAGE_SPAWNABLE_SNAPSHOT_CAPACITY 16u
#define RUNTIME_MANAGE_PATH_CAPACITY 96u

enum runtime_manage_demo_profile {
  RUNTIME_MANAGE_DEMO_PROFILE_OFF = 0u,
  RUNTIME_MANAGE_DEMO_PROFILE_SMOKE = 1u
};

enum runtime_manage_status {
  RUNTIME_MANAGE_STATUS_OK = 0u,
  RUNTIME_MANAGE_STATUS_EMPTY = 1u,
  RUNTIME_MANAGE_STATUS_UNKNOWN_COMMAND = 2u,
  RUNTIME_MANAGE_STATUS_BAD_ARGUMENT = 3u,
  RUNTIME_MANAGE_STATUS_UNAVAILABLE = 4u
};

struct runtime_manage_spawnable_info {
  const char *task_name;
  const char *process_name;
  uint32_t default_role;
};

struct runtime_manage_ops {
  uint32_t (*snapshot)(struct process_info *buffer, uint32_t capacity, void *context);
  uint32_t (*spawnable_snapshot)(struct runtime_manage_spawnable_info *buffer, uint32_t capacity,
                                 void *context);
  void (*kmem_stats)(struct kmem_stats *stats, void *context);
  int (*mailbox_has_message)(void *context);
  uint32_t (*mailbox_waiting_senders)(void *context);
  uint32_t (*mailbox_waiting_receivers)(void *context);
  int (*mailbox_send)(uint32_t message, void *context);
  int (*mailbox_receive)(uint32_t *message, void *context);
  uint32_t (*event_signal)(void *context);
  int (*process_wake)(uint32_t pid, void *context);
  int (*process_kill)(uint32_t pid, int32_t exit_code, void *context);
  uint32_t (*spawn_named_process)(const char *task_name, uint32_t role, void *context);
  uint32_t (*demo_profile)(void *context);
  int (*demo_set_profile)(uint32_t profile, void *context);
  uint32_t (*demo_completed_steps)(void *context);
  uint32_t (*demo_total_steps)(void *context);
  const char *(*shell_path)(void *context);
  int (*resolve_command_path)(const char *command, char *resolved_path, uint32_t capacity,
                              void *context);
  const char *(*scheduler_mode_name)(void *context);
  const char *(*runtime_mode_name)(void *context);
  const char *(*preempt_status_name)(void *context);
  void (*log)(const char *tag, const char *message, void *context);
  void (*log_u32)(const char *tag, const char *label, uint32_t value, void *context);
};

struct runtime_manage_line_state {
  char buffer[RUNTIME_MANAGE_LINE_CAPACITY];
  uint32_t length;
};

enum runtime_manage_status runtime_manage_execute(const char *command,
                                                  const struct runtime_manage_ops *ops,
                                                  void *context);
const char *runtime_manage_status_name(enum runtime_manage_status status);
void runtime_manage_line_init(struct runtime_manage_line_state *state);
int runtime_manage_line_push_char(struct runtime_manage_line_state *state, char ch,
                                  const struct runtime_manage_ops *ops, void *context,
                                  enum runtime_manage_status *status);
