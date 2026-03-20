#include "runtime.h"
#include "runtime_catalog.h"
#include "runtime_fs.h"
#include "runtime_fs_image.h"
#include "runtime_loader.h"
#include "runtime_manage.h"
#include "runtime_service.h"
#include "runtime_display_demo.h"
#include "runtime_syscall.h"
#include "soc.h"

#ifndef TEST_DELAY_GLOBAL_INTERRUPTS
#define TEST_DELAY_GLOBAL_INTERRUPTS 0
#endif

#ifndef TEST_DISABLE_INTERRUPT_PIPELINE
#define TEST_DISABLE_INTERRUPT_PIPELINE 0
#endif

#ifndef TEST_SKIP_SYSTIMER_TICK_INIT
#define TEST_SKIP_SYSTIMER_TICK_INIT 0
#endif

#ifndef CONFIG_RUNTIME_ENABLE_FS_STRESS
#define CONFIG_RUNTIME_ENABLE_FS_STRESS 0
#endif

static inline void interrupts_global_disable(void) {
  __asm__ volatile("csrci mstatus, 8");
}

static inline void interrupts_global_enable(void) {
  __asm__ volatile("csrsi mstatus, 8");
}

static inline uint32_t interrupts_global_state_read(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mstatus" : "=r"(value));
  return value;
}

#define TASK_PROGRESS_INTERVAL 0x1000u
#define CPU_SYSTIMER_INTR_NUM 7u
static uint8_t g_runtime_timer_poll_latched;
static process_id_t g_blocked_pid;
static process_id_t g_event_waiter_pid;
static process_id_t g_mailbox_receiver_pid;
static process_id_t g_service_pid;
static struct kernel_event *g_worker_event;
static struct kernel_mailbox *g_worker_mailbox;
static struct kernel_event *g_service_request_event;
static struct kernel_mailbox *g_service_request_mailbox;
static struct kernel_mailbox *g_service_reply_mailbox;
static struct kernel_semaphore *g_service_reply_semaphore;
static struct kernel_mutex *g_service_state_mutex;
static process_id_t g_fs_stress_pid;
#if CONFIG_RUNTIME_SINGLE_FOREGROUND
static uint8_t g_second_foreground_rejected;
#endif

static void task_a(void);
static void task_b(void);
static void task_c(void);
static void task_d(void);
static void task_e(void);
static void process_event_waiter(void);
static void process_mailbox_receiver(void);
static void process_service_server(void);
static void process_oneshot(void);
static void process_supervisor(void);
static void __attribute__((unused)) process_fs_stress(void);
static uint32_t runtime_fs_stress_init_fs_chunked(struct runtime_fs *fs);
static int32_t runtime_fs_stage1_probe(const struct runtime_fs *fs);
static int runtime_process_snapshot_find(process_id_t pid, struct process_info *info);
static process_id_t runtime_service_provider_pid(void);

static char g_runtime_task_key_a[] = "a";
static char g_runtime_task_key_b[] = "b";
static char g_runtime_task_key_c[] = "c";
static char g_runtime_task_key_d[] = "d";
static char g_runtime_task_key_e[] = "e";
static char g_runtime_task_key_on[] = "on";
static char g_runtime_task_key_ev[] = "ev";
static char g_runtime_task_key_mb[] = "mb";
static char g_runtime_task_key_sv[] = "sv";
static char g_runtime_task_key_su[] = "su";
static char g_runtime_process_name_taska[] = "TASKA";
static char g_runtime_process_name_taskb[] = "TASKB";
static char g_runtime_process_name_taskc[] = "TASKC";
static char g_runtime_process_name_taskd[] = "TASKD";
static char g_runtime_process_name_taske[] = "TASKE";
static char g_runtime_process_name_oneshot[] = "ONESHOT";
static char g_runtime_process_name_eventwaiter[] = "EVENTWAITER";
static char g_runtime_process_name_mailboxrx[] = "MAILBOXRX";
static char g_runtime_process_name_svcserver[] = "SVCSERVER";
static char g_runtime_process_name_supervisor[] = "SUPERVISOR";
static char g_fs_path_stress_root[] = "/stress";
static char g_fs_path_stress_logs[] = "/stress/logs";
static char g_fs_path_stress_a[] = "/stress/a.bin";
static char g_fs_path_stress_missing[] = "/stress/missing.bin";
static char g_fs_path_stress_b[] = "/stress/logs/b.bin";
static char g_fs_path_stress_boot[] = "/stress/boot.txt";
static uint8_t g_fs_payload_boot[] = {'T', 'E', 'N', 'S', 'O', 'R', 'F', 'S'};
static char g_fs_dbg_open_status[] = "open_status=";
static char g_fs_dbg_write_status[] = "write_status=";
static char g_fs_dbg_seek_status[] = "seek_status=";
static char g_fs_dbg_read_status[] = "read_status=";
static char g_fs_dbg_close_status[] = "close_status=";
static char g_fs_dbg_fail_step[] = "fail_step=";
static char g_fs_dbg_fail_status[] = "fail_status=";
static char g_fs_dbg_expect_status[] = "expect_status=";
static char g_fs_dbg_find_count[] = "find_count=";
static char g_fs_dbg_image_code[] = "image_code=";
static char g_fs_dbg_image_arg0[] = "image_arg0=";
static char g_fs_dbg_image_arg1[] = "image_arg1=";

static struct runtime_process_manifest g_runtime_process_catalog[] = {
    {g_runtime_task_key_a, g_runtime_process_name_taska, RUNTIME_PROCESS_ROLE_FOREGROUND_APP, task_a},
    {g_runtime_task_key_b, g_runtime_process_name_taskb, RUNTIME_PROCESS_ROLE_FOREGROUND_APP, task_b},
    {g_runtime_task_key_c, g_runtime_process_name_taskc, RUNTIME_PROCESS_ROLE_FOREGROUND_APP, task_c},
    {g_runtime_task_key_d, g_runtime_process_name_taskd, RUNTIME_PROCESS_ROLE_BACKGROUND_APP, task_d},
    {g_runtime_task_key_e, g_runtime_process_name_taske, RUNTIME_PROCESS_ROLE_FOREGROUND_APP, task_e},
    {g_runtime_task_key_on, g_runtime_process_name_oneshot, RUNTIME_PROCESS_ROLE_SYSTEM, process_oneshot},
    {g_runtime_task_key_ev, g_runtime_process_name_eventwaiter, RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY, process_event_waiter},
    {g_runtime_task_key_mb, g_runtime_process_name_mailboxrx, RUNTIME_PROCESS_ROLE_BACKGROUND_APP, process_mailbox_receiver},
    {g_runtime_task_key_sv, g_runtime_process_name_svcserver, RUNTIME_PROCESS_ROLE_SYSTEM, process_service_server},
    {g_runtime_task_key_su, g_runtime_process_name_supervisor, RUNTIME_PROCESS_ROLE_SYSTEM, process_supervisor},
};

enum runtime_management_demo_profile {
  RUNTIME_MANAGEMENT_DEMO_PROFILE_OFF = 0u,
  RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE = 1u
};

#define RUNTIME_MANAGEMENT_DEMO_MAILBOX_TARGET 3u

struct runtime_management_demo_state {
  struct kernel_mailbox *mailbox;
  struct kernel_event *event;
  uint32_t mailbox_send_count;
  uint32_t mailbox_receive_count;
  uint32_t spawn_count;
  uint8_t demo_profile;
  uint8_t demo_runtime_logged;
  uint8_t demo_child_wait_done;
  uint8_t demo_manual_wake_done;
  uint8_t demo_event_signal_done;
  uint8_t demo_mailbox_auto_count;
};

struct runtime_service_demo_state {
  uint32_t request_count;
  uint32_t response_count;
  uint32_t total_processed;
  uint32_t last_request;
  uint32_t last_response;
  uint32_t next_client_request;
  uint64_t last_client_tick;
};

#ifndef RUNTIME_FS_STRESS_ROUNDS
#define RUNTIME_FS_STRESS_ROUNDS 64u
#endif

#ifndef RUNTIME_FS_STRESS_PROGRESS_INTERVAL
#define RUNTIME_FS_STRESS_PROGRESS_INTERVAL 8u
#endif

#ifndef RUNTIME_FS_STRESS_PROFILE_NAME
#define RUNTIME_FS_STRESS_PROFILE_NAME "stress"
#endif

#define RUNTIME_FS_STRESS_PAYLOAD_A 48u
#define RUNTIME_FS_STRESS_PAYLOAD_B 24u
struct runtime_fs_stress_state {
  struct runtime_fs fs;
  struct runtime_syscall_table table;
  uint8_t image_buffer[RUNTIME_FS_IMAGE_MAX_BYTES];
  uint32_t rounds_completed;
  uint32_t failures;
  int32_t last_status;
  uint32_t last_step;
  uint32_t last_checksum;
  uint32_t last_find_count;
  uint32_t last_image_bytes;
  uint32_t last_image_root_hash;
  uint32_t last_cleanup_count;
  uint8_t complete;
};

static struct runtime_management_demo_state g_runtime_management_state;
static struct runtime_manage_line_state g_runtime_management_line_state;
static struct runtime_service_demo_state g_runtime_service_state;
static struct runtime_fs_stress_state g_runtime_fs_stress_state;
static struct runtime_service_session_table g_runtime_service_sessions;
static uint32_t g_runtime_fs_stage1_diag_code;
static uint32_t g_runtime_fs_stage1_diag_arg0;
static uint32_t g_runtime_fs_stage1_diag_arg1;
static char g_kernel_dbg_tag[] = "KERNEL";
static char g_display_dbg_tag[] = "DISPLAY";
static char g_display_dbg_call_init[] = "call_init";
static char g_kernel_dbg_start[] = "kernel_start_dram";
static char g_kernel_dbg_intr_deferred[] = "interrupts_deferred_dram";
static char __attribute__((unused)) g_stable_dbg_tag[] = "STABLE";
static char __attribute__((unused)) g_stable_dbg_tick[] = "tick=";
static char __attribute__((unused)) g_stable_dbg_switch_count[] = "switch_count=";
#if CONFIG_SCHEDULER_PREEMPTIVE
static char g_preempt_dbg_tag[] = "PREEMPT";
static char g_preempt_dbg_taskd_alive[] = "task_d_alive";
#endif
#if !CONFIG_SCHEDULER_PREEMPTIVE
static char g_fg_dbg_tag[] = "FG";
static char g_fg_dbg_alive[] = "task_a_alive";
static char g_bg_dbg_tag[] = "BG";
static char g_bg_dbg_alive[] = "task_d_alive";
#endif
static char g_fs_dbg_tag[] = "FS";
static char g_fs_dbg_pid[] = "pid=";
static char g_fs_dbg_status[] = "status=";
static char g_fs_dbg_diag_code[] = "diag_code=";
static char g_fs_dbg_diag_arg0[] = "diag_arg0=";
static char g_fs_dbg_diag_arg1[] = "diag_arg1=";

struct runtime_service_manifest {
  const char *service_key;
  const char *service_name;
  uint32_t flags;
};

static char g_runtime_service_key_accum[] = "accum";
static char g_runtime_service_name_svcserver[] = "SVCSERVER";

static struct runtime_service_manifest g_runtime_service_catalog[] = {
    {g_runtime_service_key_accum, g_runtime_service_name_svcserver, RUNTIME_SERVICE_FLAG_REQUEST_REPLY},
};

static char g_runtime_app_key_demo_calc[] = "demo.calc";
static char g_runtime_app_key_demo_inbox[] = "demo.inbox";
static char g_runtime_app_key_demo_live[] = "demo.live";
static char g_runtime_display_name_demo_calc[] = "Demo Calculator";
static char g_runtime_display_name_demo_inbox[] = "Demo Inbox";
static char g_runtime_display_name_demo_live[] = "Demo Live Activity";
static char g_runtime_version_demo[] = "0.1.0";

static struct runtime_loader_record g_runtime_loader_catalog[] = {
    {g_runtime_app_key_demo_calc, g_runtime_display_name_demo_calc, g_runtime_version_demo, g_runtime_task_key_a, RUNTIME_PROCESS_ROLE_FOREGROUND_APP},
    {g_runtime_app_key_demo_inbox, g_runtime_display_name_demo_inbox, g_runtime_version_demo, g_runtime_task_key_mb, RUNTIME_PROCESS_ROLE_BACKGROUND_APP},
    {g_runtime_app_key_demo_live, g_runtime_display_name_demo_live, g_runtime_version_demo, g_runtime_task_key_ev, RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY},
};

static void runtime_management_demo_reset(struct runtime_management_demo_state *state) {
  if (state == 0) {
    return;
  }

  state->demo_runtime_logged = 0u;
  state->demo_child_wait_done = 0u;
  state->demo_manual_wake_done = 0u;
  state->demo_event_signal_done = 0u;
  state->demo_mailbox_auto_count = 0u;
}

static uint32_t runtime_management_snapshot(struct process_info *buffer, uint32_t capacity,
                                            void *context) {
  (void)context;
  return process_snapshot(buffer, capacity);
}

static void runtime_management_kmem_stats(struct kmem_stats *stats, void *context) {
  (void)context;
  kmem_stats_snapshot(stats);
}

static int runtime_management_mailbox_has_message(void *context) {
  const struct runtime_management_demo_state *state =
      (const struct runtime_management_demo_state *)context;

  if ((state == 0) || (state->mailbox == 0)) {
    return 0;
  }

  return kernel_mailbox_has_message(state->mailbox);
}

static uint32_t runtime_management_mailbox_waiting_senders(void *context) {
  const struct runtime_management_demo_state *state =
      (const struct runtime_management_demo_state *)context;

  return ((state == 0) || (state->mailbox == 0)) ? 0u
                                                  : kernel_mailbox_waiting_senders(state->mailbox);
}

static uint32_t runtime_management_mailbox_waiting_receivers(void *context) {
  const struct runtime_management_demo_state *state =
      (const struct runtime_management_demo_state *)context;

  return ((state == 0) || (state->mailbox == 0))
             ? 0u
             : kernel_mailbox_waiting_receivers(state->mailbox);
}

static int runtime_management_mailbox_send(uint32_t message, void *context) {
  struct runtime_management_demo_state *state = (struct runtime_management_demo_state *)context;

  if ((state == 0) || (state->mailbox == 0)) {
    return 0;
  }

  kernel_mailbox_send(state->mailbox, message);
  ++state->mailbox_send_count;
  console_log_u32("PROC", "mailbox_sent_count=", state->mailbox_send_count);
  return 1;
}

static int runtime_management_mailbox_receive(uint32_t *message, void *context) {
  struct runtime_management_demo_state *state = (struct runtime_management_demo_state *)context;

  if ((state == 0) || (state->mailbox == 0) || (message == 0)) {
    return 0;
  }

  *message = kernel_mailbox_receive(state->mailbox);
  ++state->mailbox_receive_count;
  console_log_u32("PROC", "mailbox_message=", *message);
  console_log_u32("PROC", "mailbox_receive_count=", state->mailbox_receive_count);
  return 1;
}

static uint32_t runtime_management_event_signal(void *context) {
  struct runtime_management_demo_state *state = (struct runtime_management_demo_state *)context;
  uint32_t woke;

  if ((state == 0) || (state->event == 0)) {
    return 0u;
  }

  woke = kernel_event_signal(state->event);
  console_log_u32("PROC", "event_signal_woke=", woke);
  return woke;
}

static int runtime_management_process_wake(uint32_t pid, void *context) {
  (void)context;
  return process_wake((process_id_t)pid);
}

static int runtime_management_process_kill(uint32_t pid, int32_t exit_code, void *context) {
  (void)context;
  return process_kill((process_id_t)pid, exit_code);
}

static process_id_t runtime_management_spawn_process(task_entry_t entry, const char *name,
                                                     uint32_t role) {
  process_id_t pid = process_spawn_with_role(name, entry, role);
  console_log_u32("MGMT", "spawn_pid=", pid);
  return pid;
}

static process_id_t runtime_spawn_process_from_task_key(const char *task_key, uint32_t role) {
  const struct runtime_process_manifest *manifest;

  if (task_key == 0) {
    return 0u;
  }

  manifest = runtime_catalog_find(
      g_runtime_process_catalog,
      sizeof(g_runtime_process_catalog) / sizeof(g_runtime_process_catalog[0]), task_key);
  if (manifest == 0) {
    return 0u;
  }

  return runtime_management_spawn_process(manifest->entry, manifest->process_name, role);
}

static uint32_t runtime_management_spawnable_snapshot(struct runtime_manage_spawnable_info *buffer,
                                                      uint32_t capacity, void *context) {
  struct runtime_process_catalog_entry catalog[RUNTIME_MANAGE_SPAWNABLE_SNAPSHOT_CAPACITY];
  uint32_t count;

  (void)context;

  count = runtime_catalog_snapshot(g_runtime_process_catalog,
                                   sizeof(g_runtime_process_catalog) / sizeof(g_runtime_process_catalog[0]),
                                   catalog, RUNTIME_MANAGE_SPAWNABLE_SNAPSHOT_CAPACITY);
  for (uint32_t index = 0u; (buffer != 0) && (index < count) && (index < capacity) &&
                            (index < RUNTIME_MANAGE_SPAWNABLE_SNAPSHOT_CAPACITY);
       ++index) {
    buffer[index].task_name = catalog[index].task_key;
    buffer[index].process_name = catalog[index].process_name;
    buffer[index].default_role = catalog[index].default_role;
  }

  return count;
}

static uint32_t runtime_management_spawn_named_process(const char *task_name, uint32_t role,
                                                       void *context) {
  struct runtime_management_demo_state *state = (struct runtime_management_demo_state *)context;
  const struct runtime_process_manifest *manifest;
  process_id_t pid;

  if (task_name == 0) {
    return 0u;
  }

  ++state->spawn_count;
  manifest = runtime_catalog_find(
      g_runtime_process_catalog,
      sizeof(g_runtime_process_catalog) / sizeof(g_runtime_process_catalog[0]), task_name);
  pid = runtime_spawn_process_from_task_key(task_name, role);
  if (pid == 0u) {
    return 0u;
  }

  if (manifest->entry == process_mailbox_receiver) {
    g_mailbox_receiver_pid = pid;
  } else if (manifest->entry == process_service_server) {
    g_service_pid = pid;
  }

  return pid;
}

static uint32_t runtime_syscall_spawnable_count(void *context) {
  (void)context;
  return (uint32_t)(sizeof(g_runtime_process_catalog) / sizeof(g_runtime_process_catalog[0]));
}

static int32_t runtime_syscall_spawnable_info(uint32_t index,
                                              struct runtime_process_spawnable_info *info,
                                              void *context) {
  const struct runtime_process_manifest *manifest;

  (void)context;

  if (info == 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (index >= (sizeof(g_runtime_process_catalog) / sizeof(g_runtime_process_catalog[0]))) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  manifest = &g_runtime_process_catalog[index];
  info->task_key = manifest->task_key;
  info->process_name = manifest->process_name;
  info->default_role = manifest->default_role;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static uint32_t runtime_management_demo_profile(void *context) {
  const struct runtime_management_demo_state *state =
      (const struct runtime_management_demo_state *)context;
  return (state == 0) ? RUNTIME_MANAGE_DEMO_PROFILE_OFF
                      : (state->demo_profile == RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE
                             ? RUNTIME_MANAGE_DEMO_PROFILE_SMOKE
                             : RUNTIME_MANAGE_DEMO_PROFILE_OFF);
}

static int runtime_management_demo_set_profile(uint32_t profile, void *context) {
  struct runtime_management_demo_state *state = (struct runtime_management_demo_state *)context;

  if (state == 0) {
    return 0;
  }

  if ((profile != RUNTIME_MANAGE_DEMO_PROFILE_OFF) &&
      (profile != RUNTIME_MANAGE_DEMO_PROFILE_SMOKE)) {
    return 0;
  }

  state->demo_profile = profile == RUNTIME_MANAGE_DEMO_PROFILE_SMOKE
                            ? RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE
                            : RUNTIME_MANAGEMENT_DEMO_PROFILE_OFF;
  if (state->demo_profile == RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE) {
    runtime_management_demo_reset(state);
  }
  console_log("MGMT", state->demo_profile == RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE ? "auto_on"
                                                                                    : "auto_off");
  return 1;
}

static uint32_t runtime_management_demo_completed_steps(void *context) {
  const struct runtime_management_demo_state *state =
      (const struct runtime_management_demo_state *)context;
  uint32_t completed = 0u;

  if (state == 0) {
    return 0u;
  }

  completed += state->demo_runtime_logged != 0u ? 1u : 0u;
  completed += state->demo_child_wait_done != 0u ? 1u : 0u;
  completed += state->demo_manual_wake_done != 0u ? 1u : 0u;
  completed += state->demo_event_signal_done != 0u ? 1u : 0u;
  completed += state->demo_mailbox_auto_count >= RUNTIME_MANAGEMENT_DEMO_MAILBOX_TARGET ? 1u : 0u;
  return completed;
}

static uint32_t runtime_management_demo_total_steps(void *context) {
  const struct runtime_management_demo_state *state =
      (const struct runtime_management_demo_state *)context;

  if ((state == 0) || (state->demo_profile != RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE)) {
    return 0u;
  }

  return 5u;
}

static const char *runtime_management_scheduler_mode_name(void *context) {
  (void)context;
  return scheduler_mode_name();
}

static const char *runtime_management_runtime_mode_name(void *context) {
  (void)context;
  return runtime_policy_mode_name();
}

static const char *runtime_management_preempt_status_name(void *context) {
  (void)context;
  return scheduler_preempt_status_name();
}

static void runtime_management_log(const char *tag, const char *message, void *context) {
  (void)context;
  console_log(tag, message);
}

static void runtime_management_log_u32(const char *tag, const char *label, uint32_t value,
                                       void *context) {
  (void)context;
  console_log_u32(tag, label, value);
}

static uint32_t runtime_fs_stress_current_pid(void *context) {
  (void)context;
  return process_current_pid();
}

static int32_t runtime_syscall_process_info(uint32_t pid, struct runtime_process_info *info,
                                            void *context) {
  struct process_info snapshot;

  (void)context;

  if ((pid == 0u) || (info == 0) || !runtime_process_snapshot_find((process_id_t)pid, &snapshot)) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  info->pid = snapshot.pid;
  info->parent_pid = snapshot.parent_pid;
  info->name = snapshot.name;
  info->role = snapshot.role;
  info->state = snapshot.state;
  info->exit_code = snapshot.exit_code;
  info->switch_count = snapshot.switch_count;
  info->yield_count = snapshot.yield_count;
  info->wake_tick = snapshot.wake_tick;
  info->wait_reason = snapshot.wait_reason;
  info->wait_channel = snapshot.wait_channel;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t runtime_fs_stress_mkdir(const char *path, uint32_t mode, void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_mkdir(&state->fs, process_current_pid(), path, mode);
}

static int32_t runtime_fs_stress_open(const char *path, uint32_t flags, uintptr_t *file_object,
                                      void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_open(&state->fs, process_current_pid(), path, flags, file_object);
}

static int32_t runtime_fs_stress_read(uintptr_t file_object, void *buffer, uint32_t size,
                                      void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_read(&state->fs, file_object, buffer, size);
}

static int32_t runtime_fs_stress_write(uintptr_t file_object, const void *buffer, uint32_t size,
                                       void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_write(&state->fs, file_object, buffer, size);
}

static int32_t runtime_fs_stress_close(uintptr_t file_object, void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_close(&state->fs, file_object);
}

static int32_t runtime_fs_stress_stat(uintptr_t file_object, struct runtime_file_stat *stat,
                                      void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_stat(&state->fs, file_object, stat);
}

static int32_t runtime_fs_stress_readdir(uintptr_t file_object, struct runtime_dir_entry *entry,
                                         void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_readdir(&state->fs, file_object, entry);
}

static int32_t runtime_fs_stress_seek(uintptr_t file_object, int32_t offset, uint32_t whence,
                                      void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_seek(&state->fs, file_object, offset, whence);
}

static int32_t runtime_fs_stress_remove(const char *path, void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_remove(&state->fs, process_current_pid(), path);
}

static int32_t runtime_fs_stress_rename(const char *old_path, const char *new_path, void *context) {
  struct runtime_fs_stress_state *state = (struct runtime_fs_stress_state *)context;

  return runtime_fs_rename(&state->fs, process_current_pid(), old_path, new_path);
}

static int runtime_service_string_equals(const char *lhs, const char *rhs) {
  if ((lhs == 0) || (rhs == 0)) {
    return 0;
  }

  while ((*lhs != '\0') && (*rhs != '\0')) {
    if (*lhs != *rhs) {
      return 0;
    }
    ++lhs;
    ++rhs;
  }

  return (*lhs == '\0') && (*rhs == '\0');
}

static int runtime_process_snapshot_find(process_id_t pid, struct process_info *info) {
  struct process_info snapshot[16];
  uint32_t count;

  if ((pid == 0u) || (info == 0)) {
    return 0;
  }

  count = process_snapshot(snapshot, (uint32_t)(sizeof(snapshot) / sizeof(snapshot[0])));
  for (uint32_t index = 0u; index < count; ++index) {
    if (snapshot[index].pid != pid) {
      continue;
    }

    info->pid = snapshot[index].pid;
    info->parent_pid = snapshot[index].parent_pid;
    info->name = snapshot[index].name;
    info->role = snapshot[index].role;
    info->state = snapshot[index].state;
    info->exit_code = snapshot[index].exit_code;
    info->switch_count = snapshot[index].switch_count;
    info->yield_count = snapshot[index].yield_count;
    info->wake_tick = snapshot[index].wake_tick;
    info->wait_reason = snapshot[index].wait_reason;
    info->wait_channel = snapshot[index].wait_channel;
    return 1;
  }

  return 0;
}

static process_id_t runtime_service_provider_pid(void) {
  struct process_info info;

  if ((g_service_pid == 0u) || !runtime_process_snapshot_find(g_service_pid, &info) ||
      (info.state == PROCESS_STATE_ZOMBIE) || (info.state == PROCESS_STATE_UNUSED)) {
    return 0u;
  }

  return g_service_pid;
}

static int runtime_service_request_sync(process_id_t provider_pid, uint32_t request,
                                        uint32_t *response) {
  if ((response == 0) || (provider_pid == 0u) || (g_service_pid != provider_pid) ||
      (g_service_request_event == 0) ||
      (g_service_request_mailbox == 0) || (g_service_reply_mailbox == 0) ||
      (g_service_reply_semaphore == 0)) {
    return 0;
  }

  kernel_mailbox_send(g_service_request_mailbox, request);
  (void)kernel_event_signal(g_service_request_event);
  kernel_semaphore_acquire(g_service_reply_semaphore);
  *response = kernel_mailbox_receive(g_service_reply_mailbox);
  return 1;
}

static uint32_t runtime_syscall_service_count(void *context) {
  (void)context;
  return (uint32_t)(sizeof(g_runtime_service_catalog) / sizeof(g_runtime_service_catalog[0]));
}

static int32_t runtime_syscall_service_info(uint32_t index, struct runtime_service_info *info,
                                            void *context) {
  (void)context;

  if (info == 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (index >= (sizeof(g_runtime_service_catalog) / sizeof(g_runtime_service_catalog[0]))) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  info->service_key = g_runtime_service_catalog[index].service_key;
  info->service_name = g_runtime_service_catalog[index].service_name;
  info->flags = g_runtime_service_catalog[index].flags;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t runtime_syscall_service_open(const char *service_key, uintptr_t *service_object,
                                            void *context) {
  process_id_t provider_pid;

  (void)context;

  if ((service_key == 0) || (service_object == 0)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (!runtime_service_string_equals(service_key, "accum")) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  provider_pid = runtime_service_provider_pid();
  if (provider_pid == 0u) {
    return RUNTIME_SYSCALL_STATUS_ESRCH;
  }

  return runtime_service_session_open(&g_runtime_service_sessions, process_current_pid(),
                                      provider_pid, 0u, service_object);
}

static int32_t runtime_syscall_service_close(uintptr_t service_object, void *context) {
  (void)context;
  return runtime_service_session_close(&g_runtime_service_sessions, service_object);
}

static int32_t runtime_syscall_service_request(uintptr_t service_object, uint32_t request,
                                               uint32_t *response, void *context) {
  struct runtime_service_session_info session;
  process_id_t provider_pid;

  (void)context;

  if (response == 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (runtime_service_session_info(&g_runtime_service_sessions, service_object, &session) !=
      RUNTIME_SYSCALL_STATUS_OK) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  provider_pid = runtime_service_provider_pid();
  if ((provider_pid == 0u) || (provider_pid != session.provider_pid)) {
    (void)runtime_service_session_invalidate_provider(&g_runtime_service_sessions,
                                                      session.provider_pid);
    return RUNTIME_SYSCALL_STATUS_ESRCH;
  }

  return runtime_service_request_sync(provider_pid, request, response)
             ? RUNTIME_SYSCALL_STATUS_OK
             : RUNTIME_SYSCALL_STATUS_EBUSY;
}

static uint32_t runtime_syscall_app_count(void *context) {
  (void)context;
  return (uint32_t)(sizeof(g_runtime_loader_catalog) / sizeof(g_runtime_loader_catalog[0]));
}

static int32_t runtime_syscall_app_info(uint32_t index, struct runtime_app_info *info,
                                        void *context) {
  struct runtime_loader_snapshot_entry snapshot[3];
  uint32_t count;

  (void)context;

  if (info == 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  count = runtime_loader_snapshot(
      g_runtime_loader_catalog,
      (uint32_t)(sizeof(g_runtime_loader_catalog) / sizeof(g_runtime_loader_catalog[0])), snapshot,
      (uint32_t)(sizeof(snapshot) / sizeof(snapshot[0])));
  if (index >= count) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  info->app_key = snapshot[index].app_key;
  info->display_name = snapshot[index].display_name;
  info->task_key = snapshot[index].task_key;
  info->default_role = snapshot[index].default_role;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static uint32_t runtime_syscall_app_launch(const char *app_key, void *context) {
  const struct runtime_loader_record *record;

  if (app_key == 0) {
    return 0u;
  }

  record = runtime_loader_find(
      g_runtime_loader_catalog,
      (uint32_t)(sizeof(g_runtime_loader_catalog) / sizeof(g_runtime_loader_catalog[0])), app_key);
  if (record == 0) {
    return 0u;
  }

  (void)context;
  return runtime_spawn_process_from_task_key(record->task_key, record->default_role);
}

static const struct runtime_syscall_file_ops g_runtime_fs_stress_file_ops = {
    .open = runtime_fs_stress_open,
    .read = runtime_fs_stress_read,
    .write = runtime_fs_stress_write,
    .close = runtime_fs_stress_close,
    .stat = runtime_fs_stress_stat,
    .readdir = runtime_fs_stress_readdir,
    .seek = runtime_fs_stress_seek,
    .remove = runtime_fs_stress_remove,
    .rename = runtime_fs_stress_rename,
};

static const struct runtime_syscall_ops g_runtime_fs_stress_ops = {
    .process_current_pid = runtime_fs_stress_current_pid,
    .process_info = runtime_syscall_process_info,
    .process_spawnable_count = runtime_syscall_spawnable_count,
    .process_spawnable_info = runtime_syscall_spawnable_info,
    .service_count = runtime_syscall_service_count,
    .service_info = runtime_syscall_service_info,
    .service_open = runtime_syscall_service_open,
    .service_close = runtime_syscall_service_close,
    .service_request = runtime_syscall_service_request,
    .app_count = runtime_syscall_app_count,
    .app_info = runtime_syscall_app_info,
    .app_launch = runtime_syscall_app_launch,
    .fs_mkdir = runtime_fs_stress_mkdir,
    .file_ops = &g_runtime_fs_stress_file_ops,
};

static const struct runtime_manage_ops g_runtime_manage_ops = {
    .snapshot = runtime_management_snapshot,
    .spawnable_snapshot = runtime_management_spawnable_snapshot,
    .kmem_stats = runtime_management_kmem_stats,
    .mailbox_has_message = runtime_management_mailbox_has_message,
    .mailbox_waiting_senders = runtime_management_mailbox_waiting_senders,
    .mailbox_waiting_receivers = runtime_management_mailbox_waiting_receivers,
    .mailbox_send = runtime_management_mailbox_send,
    .mailbox_receive = runtime_management_mailbox_receive,
    .event_signal = runtime_management_event_signal,
    .process_wake = runtime_management_process_wake,
    .process_kill = runtime_management_process_kill,
    .spawn_named_process = runtime_management_spawn_named_process,
    .demo_profile = runtime_management_demo_profile,
    .demo_set_profile = runtime_management_demo_set_profile,
    .demo_completed_steps = runtime_management_demo_completed_steps,
    .demo_total_steps = runtime_management_demo_total_steps,
    .scheduler_mode_name = runtime_management_scheduler_mode_name,
    .runtime_mode_name = runtime_management_runtime_mode_name,
    .preempt_status_name = runtime_management_preempt_status_name,
    .log = runtime_management_log,
    .log_u32 = runtime_management_log_u32,
};

static void runtime_fs_stress_fill_payload(uint8_t *buffer, uint32_t size, uint32_t round,
                                           uint32_t salt) {
  uint32_t value = 0x9E3779B9u ^ round ^ (salt * 0x45D9F3Bu);

  for (uint32_t index = 0u; index < size; ++index) {
    value = (value * 1664525u) + 1013904223u;
    buffer[index] = (uint8_t)(value >> 24);
  }
}

static uint32_t runtime_fs_stress_checksum(const uint8_t *buffer, uint32_t size) {
  uint32_t checksum = 0u;

  for (uint32_t index = 0u; index < size; ++index) {
    checksum = (checksum * 131u) + buffer[index];
  }

  return checksum;
}

static int runtime_fs_stress_buffer_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t size) {
  for (uint32_t index = 0u; index < size; ++index) {
    if (lhs[index] != rhs[index]) {
      return 0;
    }
  }

  return 1;
}

static inline void runtime_fs_stress_mark(uint32_t step, uint32_t value) {
  reg_write(RTC_CNTL_STORE0_REG, 0xF500u + (step & 0xFFu));
  reg_write(RTC_CNTL_STORE1_REG, step);
  reg_write(RTC_CNTL_STORE4_REG, value);
}

static int32_t runtime_fs_stress_dispatch(struct runtime_fs_stress_state *state, uint32_t syscall_number,
                                          uintptr_t arg0, uintptr_t arg1, uintptr_t arg2) {
  struct runtime_syscall_args args = {
      .arg0 = arg0,
      .arg1 = arg1,
      .arg2 = arg2,
      .arg3 = 0u,
  };

  return runtime_syscall_dispatch(syscall_number, &args, &g_runtime_fs_stress_ops, &state->table,
                                  state);
}

static int runtime_fs_stress_expect(struct runtime_fs_stress_state *state, uint32_t step,
                                    int32_t actual, int32_t expected) {
  if (actual == expected) {
    return 1;
  }

  ++state->failures;
  state->last_step = step;
  state->last_status = actual;
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_fail_step, step);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_fail_status, (uint32_t)actual);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_expect_status, (uint32_t)expected);
  return 0;
}

static int runtime_fs_probe_name_equals(const char *a, const char *b) {
  uint32_t index = 0u;

  while ((a[index] != '\0') && (b[index] != '\0')) {
    if (a[index] != b[index]) {
      return 0;
    }
    ++index;
  }

  return (a[index] == '\0') && (b[index] == '\0');
}

static int runtime_fs_probe_name_is_empty(const char *name) {
  return name[0] == '\0';
}

static int32_t runtime_fs_stage1_fail(uint32_t code, uint32_t arg0, uint32_t arg1, int32_t status) {
  g_runtime_fs_stage1_diag_code = code;
  g_runtime_fs_stage1_diag_arg0 = arg0;
  g_runtime_fs_stage1_diag_arg1 = arg1;
  reg_write(RTC_CNTL_STORE4_REG, code);
  reg_write(RTC_CNTL_STORE5_REG, arg0);
  reg_write(RTC_CNTL_STORE6_REG, arg1);
  return status;
}

static int runtime_fs_stress_checkpoint_roundtrip(struct runtime_fs_stress_state *state,
                                                  uint32_t export_step, uint32_t validate_step,
                                                  uint32_t import_step) {
  struct runtime_fs_image_layout layout;
  uint32_t image_bytes = 0u;
  int32_t status;

  runtime_fs_stress_mark(export_step, 0u);
  status = runtime_fs_image_export(&state->fs, state->image_buffer,
                                   (uint32_t)sizeof(state->image_buffer), &image_bytes, &layout);
  if (!runtime_fs_stress_expect(state, export_step, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  runtime_fs_stress_mark(validate_step, image_bytes);
  status = runtime_fs_image_validate(state->image_buffer, image_bytes, &layout);
  if (!runtime_fs_stress_expect(state, validate_step, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  runtime_fs_stress_mark(import_step, layout.root_hash);
  status = runtime_fs_image_import(&state->fs, state->image_buffer, image_bytes, &layout);
  if (!runtime_fs_stress_expect(state, import_step, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  state->last_image_bytes = image_bytes;
  state->last_image_root_hash = layout.root_hash;
  return 1;
}

static int __attribute__((unused)) runtime_fs_stress_round(struct runtime_fs_stress_state *state,
                                                           uint32_t round) {
  uint8_t payload_a[RUNTIME_FS_STRESS_PAYLOAD_A];
  uint8_t payload_b[RUNTIME_FS_STRESS_PAYLOAD_B];
  uint8_t expected[RUNTIME_FS_STRESS_PAYLOAD_A - 12u + RUNTIME_FS_STRESS_PAYLOAD_B];
  uint8_t read_buffer[sizeof(expected)];
  struct runtime_file_stat stat;
  runtime_handle_t file_handle;
  runtime_handle_t lookup_handle;
  int32_t status;
  uint32_t find_count = 0u;

  runtime_fs_stress_mark(0u, round);
  runtime_fs_init(&state->fs);
  runtime_syscall_table_init(&state->table);
  if (!runtime_fs_stress_expect(state, 0u, runtime_fs_validate(&state->fs), RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  runtime_fs_stress_fill_payload(payload_a, (uint32_t)sizeof(payload_a), round, 1u);
  runtime_fs_stress_fill_payload(payload_b, (uint32_t)sizeof(payload_b), round, 2u);

  for (uint32_t index = 0u; index < sizeof(payload_a) - 12u; ++index) {
    expected[index] = payload_a[index];
  }
  for (uint32_t index = 0u; index < sizeof(payload_b); ++index) {
    expected[sizeof(payload_a) - 12u + index] = payload_b[index];
  }

  runtime_fs_stress_mark(1u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_MKDIR,
                                      (uintptr_t)g_fs_path_stress_root, 0755u, 0u);
  if (!runtime_fs_stress_expect(state, 1u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  runtime_fs_stress_mark(2u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_MKDIR,
                                      (uintptr_t)g_fs_path_stress_logs, 0755u, 0u);
  if (!runtime_fs_stress_expect(state, 2u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  if (!runtime_fs_stress_expect(state, 2u, runtime_fs_validate(&state->fs), RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  process_yield();

  runtime_fs_stress_mark(3u, round);
  status = runtime_fs_stress_dispatch(
      state, RUNTIME_SYSCALL_FILE_OPEN, (uintptr_t)g_fs_path_stress_a,
      RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_READ, 0u);
  if (status <= 0) {
    ++state->failures;
    state->last_step = 3u;
    state->last_status = status;
    return 0;
  }
  file_handle = (runtime_handle_t)status;

  runtime_fs_stress_mark(4u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_WRITE, (uintptr_t)file_handle,
                                      (uintptr_t)payload_a, (uintptr_t)sizeof(payload_a));
  if (!runtime_fs_stress_expect(state, 4u, status, (int32_t)sizeof(payload_a))) {
    return 0;
  }

  runtime_fs_stress_mark(5u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_SEEK, (uintptr_t)file_handle,
                                      (uintptr_t)-12, RUNTIME_FILE_SEEK_END);
  if (!runtime_fs_stress_expect(state, 5u, status,
                                (int32_t)(sizeof(payload_a) - 12u))) {
    return 0;
  }

  runtime_fs_stress_mark(6u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_WRITE, (uintptr_t)file_handle,
                                      (uintptr_t)payload_b, (uintptr_t)sizeof(payload_b));
  if (!runtime_fs_stress_expect(state, 6u, status, (int32_t)sizeof(payload_b))) {
    return 0;
  }
  if (!runtime_fs_stress_expect(state, 6u, runtime_fs_validate(&state->fs), RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  runtime_fs_stress_mark(7u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_SEEK, (uintptr_t)file_handle, 0u,
                                      RUNTIME_FILE_SEEK_SET);
  if (!runtime_fs_stress_expect(state, 7u, status, 0)) {
    return 0;
  }

  runtime_fs_stress_mark(8u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_READ, (uintptr_t)file_handle,
                                      (uintptr_t)read_buffer, (uintptr_t)sizeof(read_buffer));
  if (!runtime_fs_stress_expect(state, 8u, status, (int32_t)sizeof(read_buffer))) {
    return 0;
  }
  if (!runtime_fs_stress_buffer_equal(read_buffer, expected, (uint32_t)sizeof(read_buffer))) {
    ++state->failures;
    state->last_step = 9u;
    state->last_status = -1;
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_fail_step, 9u);
    return 0;
  }
  state->last_checksum = runtime_fs_stress_checksum(read_buffer, (uint32_t)sizeof(read_buffer));

  runtime_fs_stress_mark(10u, state->last_checksum);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_STAT, (uintptr_t)file_handle,
                                      (uintptr_t)&stat, 0u);
  if (!runtime_fs_stress_expect(state, 10u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  if ((stat.type != RUNTIME_FILE_TYPE_REGULAR) || (stat.size_bytes != sizeof(expected))) {
    ++state->failures;
    state->last_step = 11u;
    state->last_status = -2;
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_fail_step, 11u);
    return 0;
  }

  runtime_fs_stress_mark(12u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_OPEN,
                                      (uintptr_t)g_fs_path_stress_a, RUNTIME_FILE_OPEN_READ, 0u);
  if (status <= 0) {
    ++state->failures;
    state->last_step = 12u;
    state->last_status = status;
    return 0;
  }
  lookup_handle = (runtime_handle_t)status;
  ++find_count;

  runtime_fs_stress_mark(13u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_HANDLE_CLOSE, (uintptr_t)lookup_handle, 0u,
                                      0u);
  if (!runtime_fs_stress_expect(state, 13u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  runtime_fs_stress_mark(14u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_OPEN,
                                      (uintptr_t)g_fs_path_stress_missing, RUNTIME_FILE_OPEN_READ, 0u);
  if (!runtime_fs_stress_expect(state, 14u, status, RUNTIME_SYSCALL_STATUS_ENOENT)) {
    return 0;
  }
  ++find_count;
  state->last_find_count = find_count;
  if (find_count != 2u) {
    ++state->failures;
    state->last_step = 15u;
    state->last_status = (int32_t)find_count;
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_fail_step, 15u);
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_find_count, find_count);
    return 0;
  }

  runtime_fs_stress_mark(16u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_REMOVE,
                                      (uintptr_t)g_fs_path_stress_a, 0u, 0u);
  if (!runtime_fs_stress_expect(state, 16u, status, RUNTIME_SYSCALL_STATUS_EBUSY)) {
    return 0;
  }

  runtime_fs_stress_mark(17u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_RENAME,
                                      (uintptr_t)g_fs_path_stress_a,
                                      (uintptr_t)g_fs_path_stress_b, 0u);
  if (!runtime_fs_stress_expect(state, 17u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  if (!runtime_fs_stress_expect(state, 17u, runtime_fs_validate(&state->fs), RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  runtime_fs_stress_mark(18u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_SEEK, (uintptr_t)file_handle, 0u,
                                      RUNTIME_FILE_SEEK_SET);
  if (!runtime_fs_stress_expect(state, 18u, status, 0)) {
    return 0;
  }
  runtime_fs_stress_mark(19u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_READ, (uintptr_t)file_handle,
                                      (uintptr_t)read_buffer, (uintptr_t)sizeof(read_buffer));
  if (!runtime_fs_stress_expect(state, 19u, status, (int32_t)sizeof(read_buffer))) {
    return 0;
  }
  if (!runtime_fs_stress_buffer_equal(read_buffer, expected, (uint32_t)sizeof(read_buffer))) {
    ++state->failures;
    state->last_step = 20u;
    state->last_status = -3;
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_fail_step, 20u);
    return 0;
  }

  runtime_fs_stress_mark(21u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_HANDLE_CLOSE, (uintptr_t)file_handle, 0u,
                                      0u);
  if (!runtime_fs_stress_expect(state, 21u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  runtime_fs_stress_mark(22u, round);
  if (!runtime_fs_stress_checkpoint_roundtrip(state, 22u, 23u, 24u)) {
    return 0;
  }

  runtime_fs_stress_mark(25u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_REMOVE,
                                      (uintptr_t)g_fs_path_stress_logs, 0u, 0u);
  if (!runtime_fs_stress_expect(state, 25u, status, RUNTIME_SYSCALL_STATUS_ENOTEMPTY)) {
    return 0;
  }

  runtime_fs_stress_mark(26u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_OPEN,
                                      (uintptr_t)g_fs_path_stress_b,
                                      RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE |
                                          RUNTIME_FILE_OPEN_TRUNCATE,
                                      0u);
  if (status <= 0) {
    ++state->failures;
    state->last_step = 26u;
    state->last_status = status;
    return 0;
  }
  file_handle = (runtime_handle_t)status;

  runtime_fs_stress_mark(27u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_STAT, (uintptr_t)file_handle,
                                      (uintptr_t)&stat, 0u);
  if (!runtime_fs_stress_expect(state, 27u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  if (stat.size_bytes != 0u) {
    ++state->failures;
    state->last_step = 28u;
    state->last_status = (int32_t)stat.size_bytes;
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_fail_step, 28u);
    return 0;
  }

  runtime_fs_stress_mark(29u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_HANDLE_CLOSE, (uintptr_t)file_handle, 0u,
                                      0u);
  if (!runtime_fs_stress_expect(state, 29u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }

  runtime_fs_stress_mark(30u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_REMOVE,
                                      (uintptr_t)g_fs_path_stress_b, 0u, 0u);
  if (!runtime_fs_stress_expect(state, 30u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  runtime_fs_stress_mark(31u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_REMOVE,
                                      (uintptr_t)g_fs_path_stress_logs, 0u, 0u);
  if (!runtime_fs_stress_expect(state, 31u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  runtime_fs_stress_mark(32u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_REMOVE,
                                      (uintptr_t)g_fs_path_stress_root, 0u, 0u);
  if (!runtime_fs_stress_expect(state, 32u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  if (!runtime_fs_stress_expect(state, 32u, runtime_fs_validate(&state->fs), RUNTIME_SYSCALL_STATUS_OK)) {
    return 0;
  }
  runtime_fs_stress_mark(33u, round);
  if (!runtime_fs_stress_checkpoint_roundtrip(state, 33u, 34u, 35u)) {
    return 0;
  }
  runtime_fs_stress_mark(36u, round);
  status = runtime_fs_stress_dispatch(state, RUNTIME_SYSCALL_FILE_OPEN,
                                      (uintptr_t)g_fs_path_stress_b, RUNTIME_FILE_OPEN_READ, 0u);
  if (!runtime_fs_stress_expect(state, 36u, status, RUNTIME_SYSCALL_STATUS_ENOENT)) {
    return 0;
  }

  return 1;
}

static int runtime_management_demo_is_active(void) {
  return g_runtime_management_state.demo_profile == RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE;
}

static void runtime_management_service_round_trip(void) {
  process_id_t provider_pid;
  uint32_t request = 0x20u + (g_runtime_service_state.next_client_request & 0x3u);
  uint32_t response;

  console_log_u32("SVC", "client_request=", request);
  provider_pid = runtime_service_provider_pid();
  if (!runtime_service_request_sync(provider_pid, request, &response)) {
    return;
  }
  ++g_runtime_service_state.response_count;
  console_log_u32("SVC", "client_response=", response);
  console_log_u32("SVC", "response_count=", g_runtime_service_state.response_count);
  g_runtime_service_state.next_client_request =
      (g_runtime_service_state.next_client_request + 1u) & 0x3u;
}

static void runtime_management_run_demo_profile(uint32_t loops) {
  static const char *const mailbox_send_commands[RUNTIME_MANAGEMENT_DEMO_MAILBOX_TARGET] = {
      "mailbox send 0x101",
      "mailbox send 0x102",
      "mailbox send 0x103",
  };

  if (!runtime_management_demo_is_active()) {
    return;
  }

  if (g_runtime_management_state.demo_child_wait_done == 0u) {
    process_id_t child_pid =
        process_spawn_with_role("ONESHOT", process_oneshot, RUNTIME_PROCESS_ROLE_SYSTEM);

    console_log_u32("PROC", "child_pid=", child_pid);
    if (child_pid != 0u) {
      int32_t exit_code = 0;
      int waited_pid = process_waitpid(child_pid, &exit_code);
      console_log_u32("PROC", "waited_pid=", (uint32_t)waited_pid);
      console_log_u32("PROC", "wait_exit=", (uint32_t)exit_code);
    }
    g_runtime_management_state.demo_child_wait_done = 1u;
    return;
  }

  if ((g_runtime_management_state.demo_runtime_logged == 0u) && (loops == 1u)) {
    if (runtime_manage_execute("runtime", &g_runtime_manage_ops, &g_runtime_management_state) ==
        RUNTIME_MANAGE_STATUS_OK) {
      g_runtime_management_state.demo_runtime_logged = 1u;
    }
  }

  if ((g_runtime_management_state.demo_manual_wake_done == 0u) && (g_blocked_pid != 0u) &&
      (kernel_ticks_read() >= 32u)) {
    if (process_wake(g_blocked_pid) != 0) {
      g_runtime_management_state.demo_manual_wake_done = 1u;
      console_log_u32("PROC", "woke_pid=", g_blocked_pid);
    }
  }

  if ((g_runtime_management_state.demo_event_signal_done == 0u) && (g_event_waiter_pid != 0u) &&
      (kernel_ticks_read() >= 48u) && (g_worker_event != 0)) {
    uint32_t woke = kernel_event_signal(g_worker_event);
    if (woke != 0u) {
      g_runtime_management_state.demo_event_signal_done = 1u;
      console_log_u32("PROC", "woke_channel_count=", woke);
    }
  }

  if ((g_mailbox_receiver_pid != 0u) && (g_worker_mailbox != 0) &&
      (g_runtime_management_state.demo_mailbox_auto_count < RUNTIME_MANAGEMENT_DEMO_MAILBOX_TARGET) &&
      (kernel_ticks_read() >=
       (64u + ((uint64_t)g_runtime_management_state.demo_mailbox_auto_count * 16u)))) {
    if (runtime_manage_execute(
            mailbox_send_commands[g_runtime_management_state.demo_mailbox_auto_count],
            &g_runtime_manage_ops, &g_runtime_management_state) == RUNTIME_MANAGE_STATUS_OK) {
      ++g_runtime_management_state.demo_mailbox_auto_count;
    }
  }

  if ((g_service_pid != 0u) &&
      ((kernel_ticks_read() - g_runtime_service_state.last_client_tick) >= 24u)) {
    runtime_management_service_round_trip();
    g_runtime_service_state.last_client_tick = kernel_ticks_read();
  }

  if ((loops & 0x3Fu) == 0u) {
    (void)runtime_manage_execute("ps", &g_runtime_manage_ops, &g_runtime_management_state);
  }
}

static void runtime_management_poll_uart_commands(void) {
  for (uint32_t count = 0u; count < 8u; ++count) {
    char ch;
    enum runtime_manage_status status;

    if (!uart0_try_getc(&ch)) {
      return;
    }

    if ((ch == '\b') || (ch == 0x7Fu)) {
      console_write("\b \b");
    } else if (ch == '\r') {
      console_write("\n");
    } else if ((ch >= ' ') && (ch <= '~')) {
      console_putc(ch);
    }

    if (runtime_manage_line_push_char(&g_runtime_management_line_state, ch, &g_runtime_manage_ops,
                                      &g_runtime_management_state, &status) == 0) {
      continue;
    }

    if ((status != RUNTIME_MANAGE_STATUS_OK) && (status != RUNTIME_MANAGE_STATUS_EMPTY)) {
      console_log("MGMT", runtime_manage_status_name(status));
    }

    console_write("mgmt> ");
  }
}

static inline void runtime_interrupt_take_window(void) {
#if CONFIG_SCHEDULER_PREEMPTIVE
  runtime_poll_timer_delivery();
  if (scheduler_preempt_pending() && (preempt_disable_depth() == 0u) &&
      (process_current_pid() != 0u)) {
    process_yield();
  }
#endif
}

static inline uint32_t runtime_csr_read_mie(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mie" : "=r"(value));
  return value;
}

void runtime_poll_timer_delivery(void) {
  if (g_runtime_timer_poll_latched != 0u) {
    return;
  }

  uint32_t int_raw = reg_read(SYSTIMER_INT_RAW_REG);
  uint32_t int_st = reg_read(SYSTIMER_INT_ST_REG);
  uint32_t eip = reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG);

  if ((((int_raw | int_st) & SYSTIMER_TARGET0_INT_ST_BIT) == 0u) &&
      ((eip & BIT(CPU_SYSTIMER_INTR_NUM)) == 0u)) {
    return;
  }

  g_runtime_timer_poll_latched = 1u;
  reg_write(RTC_CNTL_STORE0_REG, 0xD220u);
  reg_write(RTC_CNTL_STORE4_REG, eip);
  reg_write(RTC_CNTL_STORE6_REG, int_raw);
  reg_write(RTC_CNTL_STORE7_REG, int_st);
  /*
   * Cooperative fallback: if the systimer interrupt is pending but has not
   * been taken through the trap path yet, consume one tick from normal task
   * context so sleepers and demo tasks can continue to make forward progress.
   */
  systimer_tick_isr();
  interrupts_ack(CPU_SYSTIMER_INTR_NUM);
  g_runtime_timer_poll_latched = 0u;
}

#define DEFINE_YIELD_TASK(task_name, task_marker)         \
  static void task_name(void) {                           \
    uint32_t counter = 0u;                                \
    for (;;) {                                            \
      ++counter;                                          \
      if (counter == 1u) {                                \
        reg_write(RTC_CNTL_STORE3_REG, task_marker | 1u); \
      } else if ((counter & (TASK_PROGRESS_INTERVAL - 1u)) == 0u) { \
        reg_write(RTC_CNTL_STORE3_REG, task_marker | (counter & 0xFFFFu)); \
      }                                                   \
      runtime_poll_timer_delivery();                      \
      if ((counter & 0x7Fu) == 0u) {                      \
        process_sleep(1u);                                \
      } else {                                            \
        runtime_interrupt_take_window();                  \
        process_yield();                                  \
      }                                                   \
    }                                                     \
  }

DEFINE_YIELD_TASK(task_b, 0xB0000000u)
DEFINE_YIELD_TASK(task_c, 0xC0000000u)
DEFINE_YIELD_TASK(task_e, 0xE0000000u)

static void task_a(void) {
  uint32_t counter = 0u;

  for (;;) {
    ++counter;
    if (counter == 1u) {
      reg_write(RTC_CNTL_STORE3_REG, 0xA0000001u);
      reg_write(RTC_CNTL_STORE0_REG, 0xA201u);
      reg_write(RTC_CNTL_STORE4_REG, interrupts_global_state_read());
#if !CONFIG_SCHEDULER_PREEMPTIVE
      console_log(g_fg_dbg_tag, g_fg_dbg_alive);
#else
      reg_write(RTC_CNTL_STORE0_REG, 0xA202u);
      reg_write(RTC_CNTL_STORE4_REG, interrupts_global_state_read());
#endif
    }
#if CONFIG_SCHEDULER_PREEMPTIVE
    else if (counter == 2u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xA203u);
    }
#endif
#if !CONFIG_SCHEDULER_PREEMPTIVE
    else if ((counter & 0xFFFFu) == 0u) {
      reg_write(RTC_CNTL_STORE3_REG, 0xA0000000u | (counter & 0xFFFFu));
      console_log_u32(g_fg_dbg_tag, "yield_count=", counter);
      console_log_u32(g_fg_dbg_tag, "switch_count=", scheduler_switch_count_read());
    }
#endif
#if CONFIG_SCHEDULER_PREEMPTIVE
    if (counter == 1u) {
      runtime_display_demo_init();
    }
    runtime_display_demo_poll();
    if ((counter & 0x3FFFFFu) == 0u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xA220u);
      reg_write(RTC_CNTL_STORE1_REG, counter);
      reg_write(RTC_CNTL_STORE2_REG, scheduler_switch_count_read());
      reg_write(RTC_CNTL_STORE4_REG, reg_read(SYSTIMER_CONF_REG));
      reg_write(RTC_CNTL_STORE5_REG, reg_read(SYSTIMER_TARGET0_CONF_REG));
      reg_write(RTC_CNTL_STORE6_REG,
                reg_read(SYSTIMER_INT_ENA_REG) |
                    (reg_read(SYSTIMER_INT_RAW_REG) << 8) |
                    (reg_read(SYSTIMER_INT_ST_REG) << 16));
      reg_write(RTC_CNTL_STORE7_REG, reg_read(SYSTIMER_UNIT0_VALUE_LO_REG));
    }
    runtime_interrupt_take_window();
    __asm__ volatile("" ::: "memory");
#else
    if (counter == 1u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xA210u);
    }
    runtime_display_demo_poll();
    if (counter == 1u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xA211u);
    }
    runtime_poll_timer_delivery();
    if (counter == 1u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xA212u);
    }
    process_yield();
#endif
  }
}

static void task_d(void) {
  uint32_t counter = 0u;
  uint32_t tick_value;

  for (;;) {
    ++counter;
    tick_value = (uint32_t)kernel_ticks_read();
    if (counter == 1u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xD201u);
      reg_write(RTC_CNTL_STORE3_REG, 0xD0000001u);
#if CONFIG_SCHEDULER_PREEMPTIVE
      reg_write(RTC_CNTL_STORE5_REG, 0xD202u);
      reg_write(RTC_CNTL_STORE6_REG, tick_value);
      reg_write(RTC_CNTL_STORE7_REG, scheduler_switch_count_read());
      console_log(g_preempt_dbg_tag, g_preempt_dbg_taskd_alive);
#else
      console_log(g_bg_dbg_tag, g_bg_dbg_alive);
#endif
    }
#if !CONFIG_SCHEDULER_PREEMPTIVE
    else if ((counter & 0xFFFFu) == 0u) {
      reg_write(RTC_CNTL_STORE3_REG, 0xD0000000u | (counter & 0xFFFFu));
      console_log_u32(g_bg_dbg_tag, "yield_count=", counter);
      console_log_u32(g_bg_dbg_tag, "switch_count=", scheduler_switch_count_read());
    }
#endif
    runtime_display_demo_publish_tick(tick_value);
#if CONFIG_SCHEDULER_PREEMPTIVE
    if ((counter & 0x1FFu) == 0u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xD220u);
      reg_write(RTC_CNTL_STORE1_REG, tick_value);
      reg_write(RTC_CNTL_STORE2_REG, scheduler_switch_count_read());
      console_log_u32(g_preempt_dbg_tag, "bg_tick=", tick_value);
      console_log_u32(g_preempt_dbg_tag, "bg_switch_count=", scheduler_switch_count_read());
    }
    process_sleep(1u);
#else
    runtime_poll_timer_delivery();
    process_yield();
#endif
  }
}

static void process_service_server(void) {
  for (;;) {
    uint32_t request;
    uint32_t response;

    kernel_event_wait(g_service_request_event);
    request = kernel_mailbox_receive(g_service_request_mailbox);
    kernel_mutex_lock(g_service_state_mutex);
    ++g_runtime_service_state.request_count;
    g_runtime_service_state.total_processed += request;
    g_runtime_service_state.last_request = request;
    response = g_runtime_service_state.total_processed;
    g_runtime_service_state.last_response = response;
    console_log_u32("SVC", "request_count=", g_runtime_service_state.request_count);
    console_log_u32("SVC", "last_request=", request);
    console_log_u32("SVC", "total_processed=", g_runtime_service_state.total_processed);
    kernel_mutex_unlock(g_service_state_mutex);
    kernel_mailbox_send(g_service_reply_mailbox, response);
    (void)kernel_semaphore_release(g_service_reply_semaphore);
    process_yield();
  }
}

static void process_event_waiter(void) {
  uint32_t wake_count = 0u;

  for (;;) {
    kernel_event_wait(g_worker_event);
    ++wake_count;
    console_log_u32("PROC", "event_wake_count=", wake_count);
    process_yield();
  }
}

static void process_mailbox_receiver(void) {
  for (;;) {
    runtime_management_poll_uart_commands();
    (void)runtime_manage_execute("mailbox recv", &g_runtime_manage_ops,
                                 &g_runtime_management_state);
    process_yield();
  }
}

static void process_oneshot(void) {
  for (uint32_t counter = 0u; counter < 4096u; ++counter) {
    if ((counter & 0x3FFu) == 0u) {
      reg_write(RTC_CNTL_STORE3_REG, 0xF0000000u | counter);
    }
    process_yield();
  }
}

static uint32_t runtime_fs_stress_init_fs_chunked(struct runtime_fs *fs) {
  volatile uint8_t *bytes = (volatile uint8_t *)fs;
  uint32_t offset = 0u;
  uint32_t chunk_index = 0u;

  enum {
    RUNTIME_FS_STRESS_INIT_CHUNK_BYTES = 32u,
  };

  if (fs == 0) {
    reg_write(RTC_CNTL_STORE0_REG, 0xF51Eu);
    return 0u;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF510u);
  reg_write(RTC_CNTL_STORE1_REG, (uint32_t)sizeof(*fs));
  reg_write(RTC_CNTL_STORE2_REG, (uint32_t)(uintptr_t)fs);
  reg_write(RTC_CNTL_STORE3_REG, (uint32_t)(uintptr_t)fs + (uint32_t)sizeof(*fs) - 1u);
  reg_write(RTC_CNTL_STORE6_REG, 0u);
  reg_write(RTC_CNTL_STORE7_REG, 0u);

  while (offset < (uint32_t)sizeof(*fs)) {
    uint32_t chunk_end = offset + RUNTIME_FS_STRESS_INIT_CHUNK_BYTES;

    if (chunk_end > (uint32_t)sizeof(*fs)) {
      chunk_end = (uint32_t)sizeof(*fs);
    }

    reg_write(RTC_CNTL_STORE0_REG, 0xF520u);
    reg_write(RTC_CNTL_STORE1_REG, chunk_index);
    reg_write(RTC_CNTL_STORE2_REG, offset);
    reg_write(RTC_CNTL_STORE3_REG, (uint32_t)(uintptr_t)&bytes[offset]);
    reg_write(RTC_CNTL_STORE6_REG, offset);
    reg_write(RTC_CNTL_STORE7_REG, chunk_end);

    while (offset < chunk_end) {
      bytes[offset++] = 0u;
    }

    reg_write(RTC_CNTL_STORE0_REG, 0xF521u);
    reg_write(RTC_CNTL_STORE6_REG, offset);
    reg_write(RTC_CNTL_STORE7_REG, chunk_index + 1u);
    runtime_interrupt_take_window();
    ++chunk_index;
  }

  fs->nodes[0].used = 1u;
  fs->nodes[0].type = RUNTIME_FILE_TYPE_DIRECTORY;
  fs->nodes[0].parent_index = 0u;
  fs->nodes[0].owner_pid = 0u;
  fs->nodes[0].mode = 0755u;
  fs->nodes[0].name[0] = '/';
  fs->nodes[0].name[1] = '\0';

  reg_write(RTC_CNTL_STORE0_REG, 0xF53Fu);
  reg_write(RTC_CNTL_STORE1_REG, chunk_index);
  reg_write(RTC_CNTL_STORE6_REG, (uint32_t)sizeof(*fs));
  reg_write(RTC_CNTL_STORE7_REG, 0xF53Fu);
  return 1u;
}

static int32_t runtime_fs_stage1_probe(const struct runtime_fs *fs) {
  uint32_t index;
  uint32_t other;
  int32_t status;

  if (fs == 0) {
    return runtime_fs_stage1_fail(0xE100u, 0u, 0u, RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  reg_write(RTC_CNTL_STORE4_REG, fs->nodes[0].used);
  reg_write(RTC_CNTL_STORE5_REG, fs->nodes[0].type);
  reg_write(RTC_CNTL_STORE6_REG, fs->nodes[0].parent_index);
  reg_write(RTC_CNTL_STORE7_REG, ((uint32_t)(uint8_t)fs->nodes[0].name[0]) |
                                    (((uint32_t)(uint8_t)fs->nodes[0].name[1]) << 8));

  if (fs->nodes[0].used == 0u) {
    return runtime_fs_stage1_fail(0xE101u, fs->nodes[0].used, 0u, RUNTIME_SYSCALL_STATUS_EINVAL);
  }
  if (fs->nodes[0].type != RUNTIME_FILE_TYPE_DIRECTORY) {
    return runtime_fs_stage1_fail(0xE102u, fs->nodes[0].type, RUNTIME_FILE_TYPE_DIRECTORY,
                                  RUNTIME_SYSCALL_STATUS_EINVAL);
  }
  if (fs->nodes[0].parent_index != 0u) {
    return runtime_fs_stage1_fail(0xE103u, fs->nodes[0].parent_index, 0u, RUNTIME_SYSCALL_STATUS_EINVAL);
  }
  if ((fs->nodes[0].name[0] != '/') || (fs->nodes[0].name[1] != '\0')) {
    return runtime_fs_stage1_fail(0xE104u,
                                  ((uint32_t)(uint8_t)fs->nodes[0].name[0]) |
                                      (((uint32_t)(uint8_t)fs->nodes[0].name[1]) << 8),
                                  0x0000002Fu, RUNTIME_SYSCALL_STATUS_EINVAL);
  }

  for (index = 1u; index < RUNTIME_FS_MAX_NODES; ++index) {
    const struct runtime_fs_node *node = &fs->nodes[index];
    uint32_t current = index;
    uint32_t depth = 0u;

    if (node->used == 0u) {
      continue;
    }

    if ((node->type != RUNTIME_FILE_TYPE_DIRECTORY) && (node->type != RUNTIME_FILE_TYPE_REGULAR)) {
      return runtime_fs_stage1_fail(0xE110u | index, node->type, node->used, RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (runtime_fs_probe_name_is_empty(node->name)) {
      return runtime_fs_stage1_fail(0xE120u | index, index, node->parent_index,
                                    RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (node->parent_index >= RUNTIME_FS_MAX_NODES) {
      return runtime_fs_stage1_fail(0xE130u | index, node->parent_index, RUNTIME_FS_MAX_NODES,
                                    RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if ((fs->nodes[node->parent_index].used == 0u) ||
        (fs->nodes[node->parent_index].type != RUNTIME_FILE_TYPE_DIRECTORY)) {
      return runtime_fs_stage1_fail(0xE140u | index, node->parent_index,
                                    (fs->nodes[node->parent_index].used & 0xFFFFu) |
                                        ((fs->nodes[node->parent_index].type & 0xFFFFu) << 16),
                                    RUNTIME_SYSCALL_STATUS_EINVAL);
    }
    if (node->size_bytes > RUNTIME_FS_FILE_CAPACITY) {
      return runtime_fs_stage1_fail(0xE150u | index, node->size_bytes, RUNTIME_FS_FILE_CAPACITY,
                                    RUNTIME_SYSCALL_STATUS_EINVAL);
    }

    while (current != 0u) {
      current = fs->nodes[current].parent_index;
      ++depth;
      if (depth > RUNTIME_FS_MAX_NODES) {
        return runtime_fs_stage1_fail(0xE160u | index, current, depth, RUNTIME_SYSCALL_STATUS_EINVAL);
      }
      if (fs->nodes[current].used == 0u) {
        return runtime_fs_stage1_fail(0xE170u | index, current, fs->nodes[current].parent_index,
                                      RUNTIME_SYSCALL_STATUS_EINVAL);
      }
    }

    for (other = index + 1u; other < RUNTIME_FS_MAX_NODES; ++other) {
      const struct runtime_fs_node *peer = &fs->nodes[other];

      if (peer->used && (peer->parent_index == node->parent_index) &&
          runtime_fs_probe_name_equals(peer->name, node->name)) {
        return runtime_fs_stage1_fail(0xE180u | index, other,
                                      ((uint32_t)(uint8_t)node->name[0]) |
                                          (((uint32_t)(uint8_t)node->name[1]) << 8),
                                      RUNTIME_SYSCALL_STATUS_EEXIST);
      }
    }
  }

  for (index = 0u; index < RUNTIME_FS_MAX_OPEN_FILES; ++index) {
    if (fs->open_files[index].used != 0u) {
      return runtime_fs_stage1_fail(0xE200u | index, fs->open_files[index].used,
                                    fs->open_files[index].node_index, RUNTIME_SYSCALL_STATUS_EBADF);
    }
  }

  g_runtime_fs_stage1_diag_code = 0xE1FFu;
  g_runtime_fs_stage1_diag_arg0 = 0u;
  g_runtime_fs_stage1_diag_arg1 = 0u;
  reg_write(RTC_CNTL_STORE4_REG, 0xE1FFu);
  status = runtime_fs_validate(fs);
  if (status != RUNTIME_SYSCALL_STATUS_OK) {
    g_runtime_fs_stage1_diag_code = g_runtime_fs_validate_diag_code != 0u ? g_runtime_fs_validate_diag_code : 0xE1FEu;
    g_runtime_fs_stage1_diag_arg0 = g_runtime_fs_validate_diag_arg0 != 0u ? g_runtime_fs_validate_diag_arg0
                                                                          : (uint32_t)status;
    g_runtime_fs_stage1_diag_arg1 = g_runtime_fs_validate_diag_arg1;
  }
  return status;
}

static void __attribute__((unused)) process_fs_stress(void) {
  uint8_t read_buffer[sizeof(g_fs_payload_boot)];
  uintptr_t file_object = 0u;
  struct runtime_fs_image_layout layout;
  uint32_t image_bytes = 0u;
  process_id_t current_pid = process_current_pid();
  int32_t status;

  reg_write(RTC_CNTL_STORE0_REG, 0xF500u);
  reg_write(RTC_CNTL_STORE2_REG, current_pid);
  reg_write(RTC_CNTL_STORE3_REG, 0xF5000001u);
  reg_write(RTC_CNTL_STORE0_REG, 0xF501u);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_pid, current_pid);

  g_runtime_fs_stress_state.rounds_completed = 0u;
  g_runtime_fs_stress_state.failures = 0u;
  g_runtime_fs_stress_state.last_status = 0;
  g_runtime_fs_stress_state.last_step = 0u;
  g_runtime_fs_stress_state.last_checksum = 0u;
  g_runtime_fs_stress_state.last_find_count = 0u;
  g_runtime_fs_stress_state.last_image_bytes = 0u;
  g_runtime_fs_stress_state.last_image_root_hash = 0u;
  g_runtime_fs_stress_state.last_cleanup_count = 0u;
  g_runtime_fs_stress_state.complete = 0u;
  g_runtime_fs_stage1_diag_code = 0u;
  g_runtime_fs_stage1_diag_arg0 = 0u;
  g_runtime_fs_stage1_diag_arg1 = 0u;

  reg_write(RTC_CNTL_STORE0_REG, 0xF502u);
  interrupts_global_disable();
  if (runtime_fs_stress_init_fs_chunked(&g_runtime_fs_stress_state.fs) == 0u) {
    interrupts_global_enable();
    goto fs_done;
  }
  reg_write(RTC_CNTL_STORE0_REG, 0xF503u);
  status = runtime_fs_stage1_probe(&g_runtime_fs_stress_state.fs);
  if (status != RUNTIME_SYSCALL_STATUS_OK) {
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_status, (uint32_t)status);
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_diag_code, g_runtime_fs_stage1_diag_code);
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_diag_arg0, g_runtime_fs_stage1_diag_arg0);
    console_log_u32(g_fs_dbg_tag, g_fs_dbg_diag_arg1, g_runtime_fs_stage1_diag_arg1);
    g_runtime_fs_stress_state.failures = 1u;
    g_runtime_fs_stress_state.last_step = 1u;
    g_runtime_fs_stress_state.last_status = status;
    reg_write(RTC_CNTL_STORE0_REG, 0xF5EEu);
    reg_write(RTC_CNTL_STORE1_REG, g_runtime_fs_stage1_diag_code);
    reg_write(RTC_CNTL_STORE2_REG, g_runtime_fs_stage1_diag_arg0);
    reg_write(RTC_CNTL_STORE3_REG, g_runtime_fs_stage1_diag_arg1);
    reg_write(RTC_CNTL_STORE4_REG, (uint32_t)status);
    reg_write(RTC_CNTL_STORE5_REG, g_runtime_fs_stage1_diag_code);
    reg_write(RTC_CNTL_STORE6_REG, g_runtime_fs_stage1_diag_arg0);
    reg_write(RTC_CNTL_STORE7_REG, g_runtime_fs_stage1_diag_arg1);
    panic_halt();
  }
  reg_write(RTC_CNTL_STORE0_REG, 0xF540u);
  reg_write(RTC_CNTL_STORE0_REG, 0xF541u);

  reg_write(RTC_CNTL_STORE0_REG, 0xF504u);
  reg_write(RTC_CNTL_STORE1_REG, current_pid);
  reg_write(RTC_CNTL_STORE2_REG, (uint32_t)(uintptr_t)g_fs_path_stress_root);
  reg_write(RTC_CNTL_STORE3_REG, ((uint32_t)(uint8_t)'s') | (((uint32_t)(uint8_t)'t') << 8) |
                                    (((uint32_t)(uint8_t)'r') << 16) |
                                    (((uint32_t)(uint8_t)(uint8_t)'e') << 24));
  reg_write(RTC_CNTL_STORE0_REG, 0xF542u);
  interrupts_global_disable();
  reg_write(RTC_CNTL_STORE0_REG, 0xF543u);
  status = runtime_fs_mkdir(&g_runtime_fs_stress_state.fs, current_pid, g_fs_path_stress_root, 0755u);
  reg_write(RTC_CNTL_STORE0_REG, 0xF544u);
  reg_write(RTC_CNTL_STORE0_REG, 0xF545u);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_status, (uint32_t)status);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_diag_code, g_runtime_fs_mkdir_diag_code);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_diag_arg0, g_runtime_fs_mkdir_diag_arg0);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_diag_arg1, g_runtime_fs_mkdir_diag_arg1);
  reg_write(RTC_CNTL_STORE4_REG, (uint32_t)status);
  reg_write(RTC_CNTL_STORE5_REG, g_runtime_fs_mkdir_diag_code);
  reg_write(RTC_CNTL_STORE6_REG, g_runtime_fs_mkdir_diag_arg0);
  reg_write(RTC_CNTL_STORE7_REG, g_runtime_fs_mkdir_diag_arg1);
  if (status != RUNTIME_SYSCALL_STATUS_OK) {
    ++g_runtime_fs_stress_state.failures;
    g_runtime_fs_stress_state.last_step = 2u;
    g_runtime_fs_stress_state.last_status = status;
    reg_write(RTC_CNTL_STORE0_REG, 0xF5E2u);
    panic_halt();
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF505u);
  status = runtime_fs_open(&g_runtime_fs_stress_state.fs, current_pid, g_fs_path_stress_boot,
                           RUNTIME_FILE_OPEN_CREATE | RUNTIME_FILE_OPEN_WRITE | RUNTIME_FILE_OPEN_READ,
                           &file_object);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_open_status, (uint32_t)status);
  if (status != RUNTIME_SYSCALL_STATUS_OK) {
    ++g_runtime_fs_stress_state.failures;
    g_runtime_fs_stress_state.last_step = 3u;
    g_runtime_fs_stress_state.last_status = status;
    goto fs_done;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF506u);
  status = runtime_fs_write(&g_runtime_fs_stress_state.fs, file_object, g_fs_payload_boot,
                            (uint32_t)sizeof(g_fs_payload_boot));
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_write_status, (uint32_t)status);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 4u, status,
                                (int32_t)sizeof(g_fs_payload_boot))) {
    goto fs_done;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF507u);
  status = runtime_fs_seek(&g_runtime_fs_stress_state.fs, file_object, 0, RUNTIME_FILE_SEEK_SET);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_seek_status, (uint32_t)status);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 5u, status, 0)) {
    goto fs_done;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF508u);
  status = runtime_fs_read(&g_runtime_fs_stress_state.fs, file_object, read_buffer,
                           (uint32_t)sizeof(read_buffer));
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_read_status, (uint32_t)status);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 6u, status,
                                (int32_t)sizeof(read_buffer))) {
    goto fs_done;
  }
  if (!runtime_fs_stress_buffer_equal(read_buffer, g_fs_payload_boot,
                                      (uint32_t)sizeof(g_fs_payload_boot))) {
    ++g_runtime_fs_stress_state.failures;
    g_runtime_fs_stress_state.last_step = 7u;
    g_runtime_fs_stress_state.last_status = -1;
    goto fs_done;
  }
  g_runtime_fs_stress_state.last_checksum =
      runtime_fs_stress_checksum(read_buffer, (uint32_t)sizeof(read_buffer));

  reg_write(RTC_CNTL_STORE0_REG, 0xF509u);
  status = runtime_fs_close(&g_runtime_fs_stress_state.fs, file_object);
  file_object = 0u;
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_close_status, (uint32_t)status);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 8u, status,
                                RUNTIME_SYSCALL_STATUS_OK)) {
    goto fs_done;
  }

  status = runtime_fs_validate(&g_runtime_fs_stress_state.fs);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 9u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    goto fs_done;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF50Au);
  status = runtime_fs_image_export(&g_runtime_fs_stress_state.fs, g_runtime_fs_stress_state.image_buffer,
                                   (uint32_t)sizeof(g_runtime_fs_stress_state.image_buffer),
                                   &image_bytes, &layout);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 10u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    goto fs_done;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF50Bu);
  status = runtime_fs_image_validate(g_runtime_fs_stress_state.image_buffer, image_bytes, &layout);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_image_code, g_runtime_fs_image_validate_diag_code);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_image_arg0, g_runtime_fs_image_validate_diag_arg0);
  console_log_u32(g_fs_dbg_tag, g_fs_dbg_image_arg1, g_runtime_fs_image_validate_diag_arg1);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 11u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    goto fs_done;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF50Cu);
  status = runtime_fs_image_import(&g_runtime_fs_stress_state.fs, g_runtime_fs_stress_state.image_buffer,
                                   image_bytes, &layout);
  if (!runtime_fs_stress_expect(&g_runtime_fs_stress_state, 12u, status, RUNTIME_SYSCALL_STATUS_OK)) {
    goto fs_done;
  }

  g_runtime_fs_stress_state.rounds_completed = 1u;
  g_runtime_fs_stress_state.last_image_bytes = image_bytes;
  g_runtime_fs_stress_state.last_image_root_hash = layout.root_hash;

fs_done:
  if (file_object != 0u) {
    (void)runtime_fs_close(&g_runtime_fs_stress_state.fs, file_object);
  }
  g_runtime_fs_stress_state.complete = g_runtime_fs_stress_state.failures == 0u ? 1u : 0u;
  reg_write(RTC_CNTL_STORE0_REG, g_runtime_fs_stress_state.complete != 0u ? 0xF5AAu : 0xF5EEu);
  if ((g_runtime_fs_stress_state.last_step == 1u) &&
      (g_runtime_fs_stress_state.last_status != RUNTIME_SYSCALL_STATUS_OK)) {
    reg_write(RTC_CNTL_STORE1_REG, g_runtime_fs_stage1_diag_code);
    reg_write(RTC_CNTL_STORE2_REG, g_runtime_fs_stage1_diag_arg0);
    reg_write(RTC_CNTL_STORE3_REG, g_runtime_fs_stage1_diag_arg1);
  } else {
    reg_write(RTC_CNTL_STORE1_REG, g_runtime_fs_stress_state.rounds_completed);
    reg_write(RTC_CNTL_STORE2_REG, (uint32_t)g_runtime_fs_stress_state.failures);
    reg_write(RTC_CNTL_STORE3_REG, g_runtime_fs_stress_state.last_step);
  }
  reg_write(RTC_CNTL_STORE4_REG, (uint32_t)g_runtime_fs_stress_state.last_status);
  if ((g_runtime_fs_stress_state.last_step == 2u) &&
      (g_runtime_fs_stress_state.last_status != RUNTIME_SYSCALL_STATUS_OK)) {
    reg_write(RTC_CNTL_STORE5_REG, g_runtime_fs_mkdir_diag_code);
    reg_write(RTC_CNTL_STORE6_REG, g_runtime_fs_mkdir_diag_arg0);
    reg_write(RTC_CNTL_STORE7_REG, g_runtime_fs_mkdir_diag_arg1);
  } else if ((g_runtime_fs_stress_state.last_step == 1u) &&
      (g_runtime_fs_stress_state.last_status != RUNTIME_SYSCALL_STATUS_OK)) {
    reg_write(RTC_CNTL_STORE5_REG, g_runtime_fs_stage1_diag_code);
    reg_write(RTC_CNTL_STORE6_REG, g_runtime_fs_stage1_diag_arg0);
    reg_write(RTC_CNTL_STORE7_REG, g_runtime_fs_stage1_diag_arg1);
  } else {
    reg_write(RTC_CNTL_STORE5_REG, g_runtime_fs_stage1_diag_code);
    reg_write(RTC_CNTL_STORE6_REG, g_runtime_fs_stress_state.last_image_bytes);
    reg_write(RTC_CNTL_STORE7_REG, g_runtime_fs_stress_state.last_image_root_hash);
  }
  task_exit();
}

static void process_supervisor(void) {
  uint32_t loops = 0u;

  for (;;) {
    ++loops;
    runtime_management_poll_uart_commands();
    runtime_management_run_demo_profile(loops);
    process_sleep(10u);
  }
}

#if CONFIG_RUNTIME_MULTIPROCESS
static void kernel_spawn_multiprocess_demo(void) {
  process_id_t taska_pid;
  process_id_t taskd_pid;

  reg_write(RTC_CNTL_STORE0_REG, 0xC094u);
  taska_pid = process_spawn_with_role("TASKA", task_a, RUNTIME_PROCESS_ROLE_FOREGROUND_APP);
  reg_write(RTC_CNTL_STORE1_REG, taska_pid);
  g_blocked_pid = 0u;
  reg_write(RTC_CNTL_STORE0_REG, 0xC095u);
  taskd_pid = process_spawn_with_role("TASKD", task_d, RUNTIME_PROCESS_ROLE_BACKGROUND_APP);
  reg_write(RTC_CNTL_STORE2_REG, taskd_pid);
  reg_write(RTC_CNTL_STORE0_REG, 0xC096u);
  g_event_waiter_pid = 0u;
  g_mailbox_receiver_pid = 0u;
  g_service_pid = 0u;
  g_fs_stress_pid = 0u;
}
#endif

#if CONFIG_RUNTIME_SINGLE_FOREGROUND
static void kernel_spawn_single_foreground_demo(void) {
  process_id_t first_foreground =
      process_spawn_with_role("FGAPP", task_a, RUNTIME_PROCESS_ROLE_FOREGROUND_APP);
  process_id_t second_foreground =
      process_spawn_with_role("FGAPP2", task_b, RUNTIME_PROCESS_ROLE_FOREGROUND_APP);

  g_second_foreground_rejected = (first_foreground != 0u) && (second_foreground == 0u);
  (void)process_spawn_with_role("BGSYNC", task_c, RUNTIME_PROCESS_ROLE_BACKGROUND_APP);
  g_blocked_pid =
      process_spawn_with_role("MANUALBG", task_d, RUNTIME_PROCESS_ROLE_BACKGROUND_APP);
  g_event_waiter_pid = process_spawn_with_role("LIVEACT", process_event_waiter,
                                               RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY);
  g_mailbox_receiver_pid = process_spawn_with_role("MAILBOXRX", process_mailbox_receiver,
                                                   RUNTIME_PROCESS_ROLE_BACKGROUND_APP);
  g_service_pid = process_spawn_with_role("SVCSERVER", process_service_server,
                                          RUNTIME_PROCESS_ROLE_SYSTEM);
  g_fs_stress_pid = 0u;
#if CONFIG_RUNTIME_ENABLE_FS_STRESS
  g_fs_stress_pid = process_spawn_with_role("FSSTRESS", process_fs_stress,
                                            RUNTIME_PROCESS_ROLE_SYSTEM);
#endif
  (void)process_spawn_with_role("SUPERVISOR", process_supervisor,
                                RUNTIME_PROCESS_ROLE_SYSTEM);
}
#endif

void kernel_main(void) {
  int loader_catalog_ok;

  reg_write(RTC_CNTL_STORE0_REG, 0xC001u);
  interrupts_global_disable();
  reg_write(RTC_CNTL_STORE0_REG, 0xC002u);
  scheduler_preempt_init();
  reg_write(RTC_CNTL_STORE0_REG, 0xC003u);
  console_log(g_kernel_dbg_tag, g_kernel_dbg_start);
  reg_write(RTC_CNTL_STORE0_REG, 0xC004u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC005u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC006u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC007u);
  loader_catalog_ok = runtime_loader_validate_catalog(
      g_runtime_loader_catalog,
      (uint32_t)(sizeof(g_runtime_loader_catalog) / sizeof(g_runtime_loader_catalog[0])));
  reg_write(RTC_CNTL_STORE0_REG, loader_catalog_ok ? 0xC071u : 0xC07Eu);
  if (!loader_catalog_ok) {
    panic_halt();
  }
  reg_write(RTC_CNTL_STORE0_REG, 0xC008u);

  if (TEST_DISABLE_INTERRUPT_PIPELINE == 0) {
    interrupts_init();
    reg_write(RTC_CNTL_STORE0_REG, 0xC009u);
    if (TEST_SKIP_SYSTIMER_TICK_INIT == 0) {
      systimer_tick_init();
      reg_write(RTC_CNTL_STORE0_REG, 0xC00Au);
    }

    if (TEST_DELAY_GLOBAL_INTERRUPTS == 0) {
      reg_write(RTC_CNTL_STORE0_REG, 0xC00Bu);
      console_log(g_kernel_dbg_tag, g_kernel_dbg_intr_deferred);
    }
  }

  g_worker_event = kernel_event_create();
  reg_write(RTC_CNTL_STORE0_REG, 0xC00Cu);
  g_worker_mailbox = kernel_mailbox_create();
  reg_write(RTC_CNTL_STORE0_REG, 0xC00Du);
  g_service_request_event = kernel_event_create();
  g_service_request_mailbox = kernel_mailbox_create();
  g_service_reply_mailbox = kernel_mailbox_create();
  g_service_reply_semaphore = kernel_semaphore_create(0u, 1u);
  g_service_state_mutex = kernel_mutex_create();
  reg_write(RTC_CNTL_STORE0_REG, 0xC00Eu);
  if ((g_worker_event == 0) || (g_worker_mailbox == 0) || (g_service_request_event == 0) ||
      (g_service_request_mailbox == 0) || (g_service_reply_mailbox == 0) ||
      (g_service_reply_semaphore == 0) || (g_service_state_mutex == 0)) {
    panic_halt();
  }

  g_runtime_management_state.mailbox = g_worker_mailbox;
  g_runtime_management_state.event = g_worker_event;
  g_runtime_management_state.mailbox_send_count = 0u;
  g_runtime_management_state.mailbox_receive_count = 0u;
  g_runtime_management_state.spawn_count = 0u;
  g_runtime_management_state.demo_profile = RUNTIME_MANAGEMENT_DEMO_PROFILE_SMOKE;
  runtime_management_demo_reset(&g_runtime_management_state);
  g_runtime_service_state.request_count = 0u;
  g_runtime_service_state.response_count = 0u;
  g_runtime_service_state.total_processed = 0u;
  g_runtime_service_state.last_request = 0u;
  g_runtime_service_state.last_response = 0u;
  g_runtime_service_state.next_client_request = 0u;
  g_runtime_service_state.last_client_tick = 0u;
  runtime_service_session_table_init(&g_runtime_service_sessions);
  g_runtime_fs_stress_state.rounds_completed = 0u;
  g_runtime_fs_stress_state.failures = 0u;
  g_runtime_fs_stress_state.last_status = 0;
  g_runtime_fs_stress_state.last_step = 0u;
  g_runtime_fs_stress_state.last_checksum = 0u;
  g_runtime_fs_stress_state.last_find_count = 0u;
  g_runtime_fs_stress_state.last_image_bytes = 0u;
  g_runtime_fs_stress_state.last_image_root_hash = 0u;
  g_runtime_fs_stress_state.last_cleanup_count = 0u;
  g_runtime_fs_stress_state.complete = 0u;
  runtime_manage_line_init(&g_runtime_management_line_state);
  reg_write(RTC_CNTL_STORE0_REG, 0xC00Fu);
  console_log(g_display_dbg_tag, g_display_dbg_call_init);
  reg_write(RTC_CNTL_STORE0_REG, 0xC090u);
  if (!CONFIG_SCHEDULER_PREEMPTIVE) {
    runtime_display_demo_init();
  }
  if ((TEST_DISABLE_INTERRUPT_PIPELINE == 0) && (TEST_DELAY_GLOBAL_INTERRUPTS == 0) &&
      !CONFIG_SCHEDULER_PREEMPTIVE) {
    interrupts_enable();
    reg_write(RTC_CNTL_STORE0_REG, 0xC092u);
  }
  reg_write(RTC_CNTL_STORE0_REG, 0xC093u);
#if CONFIG_RUNTIME_SINGLE_FOREGROUND
  kernel_spawn_single_foreground_demo();
#else
  kernel_spawn_multiprocess_demo();
#endif
  reg_write(RTC_CNTL_STORE0_REG, 0xC097u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC010u);
  reg_write(RTC_CNTL_STORE1_REG, g_blocked_pid);
  reg_write(RTC_CNTL_STORE2_REG, g_event_waiter_pid);
  reg_write(RTC_CNTL_STORE3_REG, g_fs_stress_pid);
  reg_write(RTC_CNTL_STORE4_REG, process_count());

  if ((TEST_DISABLE_INTERRUPT_PIPELINE == 0) && (TEST_DELAY_GLOBAL_INTERRUPTS != 0)) {
    console_log(g_kernel_dbg_tag, g_kernel_dbg_intr_deferred);
  }

  if ((TEST_DISABLE_INTERRUPT_PIPELINE == 0) && (TEST_DELAY_GLOBAL_INTERRUPTS == 0) &&
      CONFIG_SCHEDULER_PREEMPTIVE) {
    interrupts_enable();
    reg_write(RTC_CNTL_STORE0_REG, 0xC092u);
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xC098u);
  scheduler_run();
  panic_halt();
}
