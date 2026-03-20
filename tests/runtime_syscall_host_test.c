#include "runtime_syscall.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct host_file_state {
  uintptr_t object;
  char path[32];
  uint32_t flags;
  uint32_t read_count;
  uint32_t write_count;
  uint32_t close_count;
  int32_t last_seek_offset;
  uint32_t last_seek_whence;
  uint32_t seek_count;
  char remove_path[32];
  uint32_t remove_count;
  char rename_old_path[32];
  char rename_new_path[32];
  uint32_t rename_count;
  struct runtime_file_stat stat;
};

struct host_state {
  uint32_t current_pid;
  uint32_t last_sleep_ticks;
  uint32_t last_kill_pid;
  int32_t last_kill_exit_code;
  uint32_t kill_result;
  uint32_t last_process_info_pid;
  char last_spawn_task_key[16];
  uint32_t last_spawn_role;
  uint32_t spawn_result_pid;
  uint32_t service_count;
  char last_service_open_key[16];
  uintptr_t next_service_object;
  uintptr_t closed_service_object;
  uint32_t service_close_count;
  uintptr_t last_service_request_object;
  uint32_t last_service_request_value;
  uint32_t next_service_response;
  uint32_t app_count;
  char last_app_launch_key[24];
  uint32_t app_launch_result_pid;
  uint32_t next_event_id;
  uint32_t next_mailbox_id;
  uint32_t destroyed_event_id;
  uint32_t destroyed_mailbox_id;
  uint32_t last_signaled_event_id;
  uint32_t event_signal_count;
  uint32_t last_mailbox_send_id;
  uint32_t last_mailbox_message;
  uint32_t mailbox_send_result;
  uint32_t last_mailbox_receive_id;
  uint32_t next_mailbox_receive_message;
  uint32_t mailbox_receive_result;
  struct host_file_state file;
};

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_syscall_host_test failed: %s\n", message);
    exit(1);
  }
}

static uint32_t host_process_current_pid(void *context) {
  struct host_state *state = (struct host_state *)context;

  return state->current_pid;
}

static void host_process_sleep(uint32_t ticks, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->last_sleep_ticks = ticks;
}

static int host_process_kill(uint32_t pid, int32_t exit_code, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->last_kill_pid = pid;
  state->last_kill_exit_code = exit_code;
  return (int)state->kill_result;
}

static int32_t host_process_info(uint32_t pid, struct runtime_process_info *info, void *context) {
  struct host_state *state = (struct host_state *)context;

  expect(info != NULL, "process info output present");
  state->last_process_info_pid = pid;
  if (pid == 0u) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  info->pid = pid;
  info->parent_pid = 1u;
  info->name = "PROC";
  info->role = 3u;
  info->state = 4u;
  info->exit_code = -9;
  info->switch_count = 12u;
  info->yield_count = 18u;
  info->wake_tick = 44u;
  info->wait_reason = 3u;
  info->wait_channel = 55u;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static uint32_t host_process_spawn_named(const char *task_key, uint32_t role, void *context) {
  struct host_state *state = (struct host_state *)context;
  size_t index = 0u;

  expect(task_key != NULL, "spawn task key present");
  while ((task_key[index] != '\0') && (index + 1u < sizeof(state->last_spawn_task_key))) {
    state->last_spawn_task_key[index] = task_key[index];
    ++index;
  }
  state->last_spawn_task_key[index] = '\0';
  state->last_spawn_role = role;
  return state->spawn_result_pid;
}

static uint32_t host_process_spawnable_count(void *context) {
  (void)context;
  return 2u;
}

static int32_t host_process_spawnable_info(uint32_t index,
                                           struct runtime_process_spawnable_info *info,
                                           void *context) {
  (void)context;

  expect(info != NULL, "spawnable info output present");
  if (index == 0u) {
    info->task_key = "sv";
    info->process_name = "SVCSERVER";
    info->default_role = 1u;
    return RUNTIME_SYSCALL_STATUS_OK;
  }
  if (index == 1u) {
    info->task_key = "mb";
    info->process_name = "MAILBOXRX";
    info->default_role = 3u;
    return RUNTIME_SYSCALL_STATUS_OK;
  }
  return RUNTIME_SYSCALL_STATUS_ENOENT;
}

static uint32_t host_service_count(void *context) {
  struct host_state *state = (struct host_state *)context;

  return state->service_count;
}

static int32_t host_service_info(uint32_t index, struct runtime_service_info *info, void *context) {
  (void)context;

  expect(info != NULL, "service info output present");
  if (index == 0u) {
    info->service_key = "accum";
    info->service_name = "SVCSERVER";
    info->flags = RUNTIME_SERVICE_FLAG_REQUEST_REPLY;
    return RUNTIME_SYSCALL_STATUS_OK;
  }
  return RUNTIME_SYSCALL_STATUS_ENOENT;
}

static int32_t host_service_open(const char *service_key, uintptr_t *service_object, void *context) {
  struct host_state *state = (struct host_state *)context;
  size_t index = 0u;

  expect((service_key != NULL) && (service_object != NULL), "service open args present");
  while ((service_key[index] != '\0') && (index + 1u < sizeof(state->last_service_open_key))) {
    state->last_service_open_key[index] = service_key[index];
    ++index;
  }
  state->last_service_open_key[index] = '\0';
  *service_object = state->next_service_object;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_service_close(uintptr_t service_object, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->closed_service_object = service_object;
  ++state->service_close_count;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_service_request(uintptr_t service_object, uint32_t request, uint32_t *response,
                                    void *context) {
  struct host_state *state = (struct host_state *)context;

  expect(response != NULL, "service request response buffer present");
  state->last_service_request_object = service_object;
  state->last_service_request_value = request;
  *response = state->next_service_response;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static uint32_t host_app_count(void *context) {
  struct host_state *state = (struct host_state *)context;

  return state->app_count;
}

static int32_t host_app_info(uint32_t index, struct runtime_app_info *info, void *context) {
  (void)context;

  expect(info != NULL, "app info output present");
  if (index == 0u) {
    info->app_key = "demo.calc";
    info->display_name = "Demo Calculator";
    info->task_key = "a";
    info->default_role = 2u;
    return RUNTIME_SYSCALL_STATUS_OK;
  }
  return RUNTIME_SYSCALL_STATUS_ENOENT;
}

static uint32_t host_app_launch(const char *app_key, void *context) {
  struct host_state *state = (struct host_state *)context;
  size_t index = 0u;

  expect(app_key != NULL, "app launch key present");
  while ((app_key[index] != '\0') && (index + 1u < sizeof(state->last_app_launch_key))) {
    state->last_app_launch_key[index] = app_key[index];
    ++index;
  }
  state->last_app_launch_key[index] = '\0';
  return state->app_launch_result_pid;
}

static void *host_event_create(void *context) {
  struct host_state *state = (struct host_state *)context;

  ++state->next_event_id;
  return (void *)(uintptr_t)state->next_event_id;
}

static int host_event_destroy(void *event, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->destroyed_event_id = (uint32_t)(uintptr_t)event;
  return 1;
}

static uint32_t host_event_signal(void *event, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->last_signaled_event_id = (uint32_t)(uintptr_t)event;
  ++state->event_signal_count;
  return 3u;
}

static void *host_mailbox_create(void *context) {
  struct host_state *state = (struct host_state *)context;

  ++state->next_mailbox_id;
  return (void *)(uintptr_t)(0x100u + state->next_mailbox_id);
}

static int host_mailbox_destroy(void *mailbox, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->destroyed_mailbox_id = (uint32_t)(uintptr_t)mailbox;
  return 1;
}

static int host_mailbox_send(void *mailbox, uint32_t message, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->last_mailbox_send_id = (uint32_t)(uintptr_t)mailbox;
  state->last_mailbox_message = message;
  return (int)state->mailbox_send_result;
}

static int host_mailbox_receive(void *mailbox, uint32_t *message, void *context) {
  struct host_state *state = (struct host_state *)context;

  state->last_mailbox_receive_id = (uint32_t)(uintptr_t)mailbox;
  *message = state->next_mailbox_receive_message;
  return (int)state->mailbox_receive_result;
}

static int32_t host_file_open(const char *path, uint32_t flags, uintptr_t *file_object,
                              void *context) {
  struct host_state *state = (struct host_state *)context;
  size_t index = 0u;

  if ((path == NULL) || (file_object == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  while ((path[index] != '\0') && (index + 1u < sizeof(state->file.path))) {
    state->file.path[index] = path[index];
    ++index;
  }
  state->file.path[index] = '\0';
  state->file.flags = flags;
  state->file.object = 0xCAFEu;
  *file_object = state->file.object;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_file_read(uintptr_t file_object, void *buffer, uint32_t size, void *context) {
  struct host_state *state = (struct host_state *)context;
  uint32_t index;
  char *chars = (char *)buffer;

  expect(file_object == state->file.object, "file read object matches");
  ++state->file.read_count;
  for (index = 0u; (index < size) && (index < 4u); ++index) {
    chars[index] = "data"[index];
  }
  return 4;
}

static int32_t host_file_write(uintptr_t file_object, const void *buffer, uint32_t size,
                               void *context) {
  const char *chars = (const char *)buffer;
  struct host_state *state = (struct host_state *)context;

  expect(file_object == state->file.object, "file write object matches");
  expect(size == 4u, "file write size matches");
  expect(chars[0] == 't', "file write payload preserved");
  ++state->file.write_count;
  return (int32_t)size;
}

static int32_t host_file_close(uintptr_t file_object, void *context) {
  struct host_state *state = (struct host_state *)context;

  expect(file_object == state->file.object, "file close object matches");
  ++state->file.close_count;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_file_stat(uintptr_t file_object, struct runtime_file_stat *stat,
                              void *context) {
  struct host_state *state = (struct host_state *)context;

  expect(file_object == state->file.object, "file stat object matches");
  expect(stat != NULL, "file stat output buffer present");
  *stat = state->file.stat;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_file_seek(uintptr_t file_object, int32_t offset, uint32_t whence, void *context) {
  struct host_state *state = (struct host_state *)context;

  expect(file_object == state->file.object, "file seek object matches");
  state->file.last_seek_offset = offset;
  state->file.last_seek_whence = whence;
  ++state->file.seek_count;
  return 12;
}

static int32_t host_file_remove(const char *path, void *context) {
  struct host_state *state = (struct host_state *)context;
  size_t index = 0u;

  expect(path != NULL, "file remove path present");
  while ((path[index] != '\0') && (index + 1u < sizeof(state->file.remove_path))) {
    state->file.remove_path[index] = path[index];
    ++index;
  }
  state->file.remove_path[index] = '\0';
  ++state->file.remove_count;
  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t host_file_rename(const char *old_path, const char *new_path, void *context) {
  struct host_state *state = (struct host_state *)context;
  size_t index = 0u;

  expect((old_path != NULL) && (new_path != NULL), "file rename paths present");
  while ((old_path[index] != '\0') && (index + 1u < sizeof(state->file.rename_old_path))) {
    state->file.rename_old_path[index] = old_path[index];
    ++index;
  }
  state->file.rename_old_path[index] = '\0';

  index = 0u;
  while ((new_path[index] != '\0') && (index + 1u < sizeof(state->file.rename_new_path))) {
    state->file.rename_new_path[index] = new_path[index];
    ++index;
  }
  state->file.rename_new_path[index] = '\0';
  ++state->file.rename_count;
  return RUNTIME_SYSCALL_STATUS_OK;
}

int main(void) {
  struct host_state state = {
      .current_pid = 7u,
      .kill_result = 1u,
      .spawn_result_pid = 23u,
      .service_count = 1u,
      .next_service_object = 0x5150u,
      .next_service_response = 0xBEEFu,
      .app_count = 1u,
      .app_launch_result_pid = 41u,
  };
  struct runtime_syscall_table table;
  struct runtime_syscall_args args = {0};
  struct runtime_handle_info info;
  char read_buffer[8] = {0};
  const char write_buffer[4] = {'t', 'e', 's', 't'};
  const struct runtime_syscall_file_ops file_ops = {
      .open = host_file_open,
      .read = host_file_read,
      .write = host_file_write,
      .close = host_file_close,
      .stat = host_file_stat,
      .seek = host_file_seek,
      .remove = host_file_remove,
      .rename = host_file_rename,
  };
  const struct runtime_syscall_ops ops = {
      .process_current_pid = host_process_current_pid,
      .process_sleep = host_process_sleep,
      .process_kill = host_process_kill,
      .process_info = host_process_info,
      .process_spawn_named = host_process_spawn_named,
      .process_spawnable_count = host_process_spawnable_count,
      .process_spawnable_info = host_process_spawnable_info,
      .service_count = host_service_count,
      .service_info = host_service_info,
      .service_open = host_service_open,
      .service_close = host_service_close,
      .service_request = host_service_request,
      .app_count = host_app_count,
      .app_info = host_app_info,
      .app_launch = host_app_launch,
      .event_create = host_event_create,
      .event_destroy = host_event_destroy,
      .event_signal = host_event_signal,
      .mailbox_create = host_mailbox_create,
      .mailbox_destroy = host_mailbox_destroy,
      .mailbox_send = host_mailbox_send,
      .mailbox_receive = host_mailbox_receive,
      .file_ops = &file_ops,
  };
  int32_t result;
  runtime_handle_t file_handle;
  runtime_handle_t duplicated_handle;

  runtime_syscall_table_init(&table);

  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_GETPID, &args, &ops, &table, &state);
  expect(result == 7, "getpid returns current pid");

  args.arg0 = 25u;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_SLEEP, &args, &ops, &table, &state);
  expect(result == RUNTIME_SYSCALL_STATUS_OK, "sleep returns ok");
  expect(state.last_sleep_ticks == 25u, "sleep forwards ticks");

  args.arg0 = 9u;
  args.arg1 = (uintptr_t)-11;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_KILL, &args, &ops, &table, &state);
  expect(result == RUNTIME_SYSCALL_STATUS_OK, "kill returns ok");
  expect(state.last_kill_pid == 9u, "kill forwards pid");
  expect(state.last_kill_exit_code == -11, "kill forwards exit code");

  args.arg0 = 0u;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_SELF, &args, &ops, &table, &state);
  expect(result > 0, "process self returns handle");
  expect(runtime_syscall_query_handle(&table, (runtime_handle_t)result, &info) != 0,
         "process handle query succeeds");
  expect(info.type == RUNTIME_HANDLE_TYPE_PROCESS, "process handle type preserved");
  expect(info.owner_pid == 7u, "process handle owner pid preserved");
  expect(info.object == 7u, "process handle object stores pid");

  {
    struct runtime_process_info process_info = {0};
    args.arg0 = (uintptr_t)result;
    args.arg1 = (uintptr_t)&process_info;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_INFO, &args, &ops, &table, &state) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "process info through handle succeeds");
    expect(state.last_process_info_pid == 7u, "process info forwards pid from handle");
    expect(process_info.parent_pid == 1u, "process info preserves parent pid");
    expect(strcmp(process_info.name, "PROC") == 0, "process info preserves name");
    expect(process_info.role == 3u, "process info preserves role");
    expect(process_info.state == 4u, "process info preserves state");
    expect(process_info.exit_code == -9, "process info preserves exit code");
    expect(process_info.switch_count == 12u, "process info preserves switch count");
    expect(process_info.yield_count == 18u, "process info preserves yield count");
    expect(process_info.wake_tick == 44u, "process info preserves wake tick");
    expect(process_info.wait_reason == 3u, "process info preserves wait reason");
    expect(process_info.wait_channel == 55u, "process info preserves wait channel");
  }

  args.arg0 = (uintptr_t)result;
  args.arg1 = (uintptr_t)-5;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_TERMINATE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "process terminate through handle succeeds");
  expect(state.last_kill_pid == 7u, "process terminate forwards handle pid");
  expect(state.last_kill_exit_code == -5, "process terminate forwards exit code");

  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_SPAWNABLE_COUNT, &args, &ops, &table,
                                    &state);
  expect(result == 2, "spawnable count returns catalog size");

  {
    struct runtime_process_spawnable_info spawnable = {0};
    args.arg0 = 0u;
    args.arg1 = (uintptr_t)&spawnable;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_SPAWNABLE_INFO, &args, &ops, &table,
                                    &state) == RUNTIME_SYSCALL_STATUS_OK,
           "spawnable info for first entry succeeds");
    expect(strcmp(spawnable.task_key, "sv") == 0, "spawnable info preserves task key");
    expect(strcmp(spawnable.process_name, "SVCSERVER") == 0,
           "spawnable info preserves process label");
    expect(spawnable.default_role == 1u, "spawnable info preserves default role");

    args.arg0 = 99u;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_SPAWNABLE_INFO, &args, &ops, &table,
                                    &state) == RUNTIME_SYSCALL_STATUS_ENOENT,
           "spawnable info for missing entry returns enoent");
  }

  args.arg0 = (uintptr_t)"mb";
  args.arg1 = 3u;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_SPAWN, &args, &ops, &table, &state);
  expect(result > 0, "process spawn returns handle");
  expect(strcmp(state.last_spawn_task_key, "mb") == 0, "process spawn forwards task key");
  expect(state.last_spawn_role == 3u, "process spawn forwards role");
  expect(runtime_syscall_query_handle(&table, (runtime_handle_t)result, &info) != 0,
         "spawned process handle query succeeds");
  expect(info.type == RUNTIME_HANDLE_TYPE_PROCESS, "spawned process handle type preserved");
  expect(info.owner_pid == 7u, "spawned process handle owner pid preserved");
  expect(info.object == 23u, "spawned process handle stores spawned pid");

  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_SERVICE_COUNT, &args, &ops, &table, &state);
  expect(result == 1, "service count returns catalog size");

  {
    struct runtime_service_info service = {0};
    args.arg0 = 0u;
    args.arg1 = (uintptr_t)&service;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_SERVICE_INFO, &args, &ops, &table, &state) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "service info succeeds");
    expect(strcmp(service.service_key, "accum") == 0, "service info preserves service key");
    expect(strcmp(service.service_name, "SVCSERVER") == 0, "service info preserves service name");
    expect(service.flags == RUNTIME_SERVICE_FLAG_REQUEST_REPLY,
           "service info preserves service flags");
  }

  args.arg0 = (uintptr_t)"accum";
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_SERVICE_OPEN, &args, &ops, &table, &state);
  expect(result > 0, "service open returns handle");
  expect(strcmp(state.last_service_open_key, "accum") == 0, "service open forwards service key");
  duplicated_handle = (runtime_handle_t)result;
  expect(runtime_syscall_query_handle(&table, duplicated_handle, &info) != 0,
         "service handle query succeeds");
  expect(info.type == RUNTIME_HANDLE_TYPE_SERVICE, "service handle type preserved");
  expect(info.object == state.next_service_object, "service handle stores service object");

  {
    uint32_t service_response = 0u;
    args.arg0 = (uintptr_t)duplicated_handle;
    args.arg1 = 0x44u;
    args.arg2 = (uintptr_t)&service_response;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_SERVICE_REQUEST, &args, &ops, &table, &state) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "service request succeeds");
    expect(state.last_service_request_object == state.next_service_object,
           "service request forwards object");
    expect(state.last_service_request_value == 0x44u, "service request forwards payload");
    expect(service_response == state.next_service_response, "service request stores response");
  }

  args.arg0 = (uintptr_t)duplicated_handle;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "service handle close succeeds");
  expect(state.closed_service_object == state.next_service_object,
         "service handle close forwards service object");

  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_APP_COUNT, &args, &ops, &table, &state);
  expect(result == 1, "app count returns catalog size");

  {
    struct runtime_app_info app = {0};
    args.arg0 = 0u;
    args.arg1 = (uintptr_t)&app;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_APP_INFO, &args, &ops, &table, &state) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "app info succeeds");
    expect(strcmp(app.app_key, "demo.calc") == 0, "app info preserves app key");
    expect(strcmp(app.display_name, "Demo Calculator") == 0, "app info preserves display name");
    expect(strcmp(app.task_key, "a") == 0, "app info preserves task key");
    expect(app.default_role == 2u, "app info preserves role");
  }

  args.arg0 = (uintptr_t)"demo.calc";
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_APP_LAUNCH, &args, &ops, &table, &state);
  expect(result > 0, "app launch returns process handle");
  expect(strcmp(state.last_app_launch_key, "demo.calc") == 0, "app launch forwards app key");
  expect(runtime_syscall_query_handle(&table, (runtime_handle_t)result, &info) != 0,
         "launched app handle query succeeds");
  expect(info.type == RUNTIME_HANDLE_TYPE_PROCESS, "launched app handle type preserved");
  expect(info.object == 41u, "launched app handle stores launched pid");

  args.arg0 = 0u;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_EVENT_CREATE, &args, &ops, &table, &state);
  expect(result > 0, "event create returns handle");
  expect(runtime_syscall_query_handle(&table, (runtime_handle_t)result, &info) != 0,
         "event handle query succeeds");
  expect(info.type == RUNTIME_HANDLE_TYPE_EVENT, "event handle type preserved");
  expect(info.owner_pid == 7u, "event handle owner pid preserved");
  args.arg0 = (uintptr_t)result;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_EVENT_SIGNAL, &args, &ops, &table, &state) == 3,
         "event signal returns wake count");
  expect(state.last_signaled_event_id == 1u, "event signal forwards event");
  expect(state.event_signal_count == 1u, "event signal counted");
  args.arg0 = (uintptr_t)result;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "event handle close succeeds");
  expect(state.destroyed_event_id == 1u, "event destroy called");

  args.arg0 = 0u;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_MAILBOX_CREATE, &args, &ops, &table, &state);
  expect(result > 0, "mailbox create returns handle");
  expect(runtime_syscall_query_handle(&table, (runtime_handle_t)result, &info) != 0,
         "mailbox handle query succeeds");
  expect(info.owner_pid == 7u, "mailbox handle owner pid preserved");
  state.mailbox_send_result = 1u;
  args.arg0 = (uintptr_t)result;
  args.arg1 = 0x1234u;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_MAILBOX_SEND, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "mailbox send succeeds");
  expect(state.last_mailbox_send_id == 0x101u, "mailbox send forwards mailbox");
  expect(state.last_mailbox_message == 0x1234u, "mailbox send forwards message");
  state.next_mailbox_receive_message = 0x55AAu;
  state.mailbox_receive_result = 1u;
  {
    uint32_t received_message = 0u;
    args.arg0 = (uintptr_t)result;
    args.arg1 = (uintptr_t)&received_message;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_MAILBOX_RECEIVE, &args, &ops, &table, &state) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "mailbox receive succeeds");
    expect(state.last_mailbox_receive_id == 0x101u, "mailbox receive forwards mailbox");
    expect(received_message == 0x55AAu, "mailbox receive stores message");
  }
  args.arg0 = (uintptr_t)result;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "mailbox handle close succeeds");
  expect(state.destroyed_mailbox_id == 0x101u, "mailbox destroy called");

  args.arg0 = (uintptr_t)"/sys/demo.txt";
  args.arg1 = RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &table, &state);
  expect(result > 0, "file open returns handle");
  file_handle = (runtime_handle_t)result;
  expect(strcmp(state.file.path, "/sys/demo.txt") == 0, "file open forwards path");
  expect(state.file.flags == (RUNTIME_FILE_OPEN_READ | RUNTIME_FILE_OPEN_WRITE),
         "file open forwards flags");
  state.file.stat.type = RUNTIME_FILE_TYPE_REGULAR;
  state.file.stat.mode = 0644u;
  state.file.stat.size_bytes = 4096u;

  args.arg0 = (uintptr_t)file_handle;
  {
    struct runtime_handle_info handle_info = {0};
    args.arg1 = (uintptr_t)&handle_info;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_INFO, &args, &ops, &table, &state) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "handle info succeeds");
    expect(handle_info.owner_pid == 7u, "handle info preserves owner pid");
    expect(handle_info.type == RUNTIME_HANDLE_TYPE_FILE, "handle info preserves type");
  }

  args.arg0 = (uintptr_t)file_handle;
  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_DUP, &args, &ops, &table, &state);
  expect(result > 0, "handle dup returns new handle");
  duplicated_handle = (runtime_handle_t)result;

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)read_buffer;
  args.arg2 = sizeof(read_buffer);
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &state) == 4,
         "file read returns byte count");
  expect(memcmp(read_buffer, "data", 4u) == 0, "file read fills buffer");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)write_buffer;
  args.arg2 = sizeof(write_buffer);
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_WRITE, &args, &ops, &table, &state) == 4,
         "file write returns byte count");

  args.arg0 = (uintptr_t)file_handle;
  args.arg1 = (uintptr_t)-4;
  args.arg2 = RUNTIME_FILE_SEEK_END;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_SEEK, &args, &ops, &table, &state) == 12,
         "file seek returns forwarded offset");
  expect(state.file.last_seek_offset == -4, "file seek preserves signed offset");
  expect(state.file.last_seek_whence == RUNTIME_FILE_SEEK_END, "file seek preserves whence");
  expect(state.file.seek_count == 1u, "file seek is counted");

  args.arg0 = (uintptr_t)file_handle;
  {
    struct runtime_file_stat stat = {0};
    args.arg1 = (uintptr_t)&stat;
    expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_STAT, &args, &ops, &table, &state) ==
               RUNTIME_SYSCALL_STATUS_OK,
           "file stat succeeds");
    expect(stat.type == RUNTIME_FILE_TYPE_REGULAR, "file stat preserves type");
    expect(stat.mode == 0644u, "file stat preserves mode");
    expect(stat.size_bytes == 4096u, "file stat preserves size");
  }

  args.arg0 = (uintptr_t)file_handle;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "first aliased file handle close succeeds");
  expect(state.file.close_count == 0u, "aliased file handle defers underlying close");

  args.arg0 = (uintptr_t)duplicated_handle;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "duplicated file handle close succeeds");
  expect(state.file.close_count == 1u, "last aliased file handle closes underlying file");

  args.arg0 = 999u;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_EBADF,
         "invalid close returns ebadf");

  args.arg0 = 99u;
  args.arg1 = (uintptr_t)read_buffer;
  args.arg2 = sizeof(read_buffer);
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_READ, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_EBADF,
         "invalid file read returns ebadf");

  result = runtime_syscall_dispatch(RUNTIME_SYSCALL_EVENT_CREATE, &args, &ops, &table, &state);
  expect(result > 0, "second event create returns handle");
  state.current_pid = 88u;
  args.arg0 = (uintptr_t)result;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_EVENT_SIGNAL, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_EBADF,
         "foreign event handle access returns ebadf");
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_EBADF,
         "foreign handle close returns ebadf");
  state.current_pid = 7u;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_HANDLE_CLOSE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "owner can close foreign-test handle once pid restored");

  state.kill_result = 0u;
  args.arg0 = 100u;
  args.arg1 = 0u;
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_KILL, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_ESRCH,
         "missing pid returns esrch");

  args.arg0 = (uintptr_t)"/tmp/demo.txt";
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_REMOVE, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "file remove forwards through syscall bridge");
  expect(strcmp(state.file.remove_path, "/tmp/demo.txt") == 0, "file remove preserves path");
  expect(state.file.remove_count == 1u, "file remove is counted");

  args.arg0 = (uintptr_t)"/tmp/old.txt";
  args.arg1 = (uintptr_t)"/tmp/new.txt";
  expect(runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_RENAME, &args, &ops, &table, &state) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "file rename forwards through syscall bridge");
  expect(strcmp(state.file.rename_old_path, "/tmp/old.txt") == 0, "file rename preserves old path");
  expect(strcmp(state.file.rename_new_path, "/tmp/new.txt") == 0, "file rename preserves new path");
  expect(state.file.rename_count == 1u, "file rename is counted");

  {
    struct runtime_syscall_table cleanup_table;
    uint32_t released;
    uint32_t event_destroyed_before = state.destroyed_event_id;
    uint32_t mailbox_destroyed_before = state.destroyed_mailbox_id;
    uint32_t service_close_count_before = state.service_close_count;
    uint32_t file_close_before = state.file.close_count;

    runtime_syscall_table_init(&cleanup_table);

    args.arg0 = 0u;
    result = runtime_syscall_dispatch(RUNTIME_SYSCALL_PROCESS_SELF, &args, &ops, &cleanup_table,
                                      &state);
    expect(result > 0, "cleanup table process handle create succeeds");

    args.arg0 = 0u;
    result = runtime_syscall_dispatch(RUNTIME_SYSCALL_EVENT_CREATE, &args, &ops, &cleanup_table,
                                      &state);
    expect(result > 0, "cleanup table event create succeeds");

    args.arg0 = 0u;
    result = runtime_syscall_dispatch(RUNTIME_SYSCALL_MAILBOX_CREATE, &args, &ops, &cleanup_table,
                                      &state);
    expect(result > 0, "cleanup table mailbox create succeeds");

    args.arg0 = (uintptr_t)"accum";
    result = runtime_syscall_dispatch(RUNTIME_SYSCALL_SERVICE_OPEN, &args, &ops, &cleanup_table,
                                      &state);
    expect(result > 0, "cleanup table service open succeeds");

    args.arg0 = (uintptr_t)"/sys/cleanup.txt";
    args.arg1 = RUNTIME_FILE_OPEN_READ;
    result = runtime_syscall_dispatch(RUNTIME_SYSCALL_FILE_OPEN, &args, &ops, &cleanup_table,
                                      &state);
    expect(result > 0, "cleanup table file open succeeds");

    released = runtime_syscall_release_owner_handles(&cleanup_table, state.current_pid, &ops, &state);
    expect(released == 5u, "owner cleanup releases all owner handles");
    expect(state.destroyed_event_id != event_destroyed_before, "owner cleanup destroys event");
    expect(state.destroyed_mailbox_id != mailbox_destroyed_before, "owner cleanup destroys mailbox");
    expect(state.service_close_count == (service_close_count_before + 1u),
           "owner cleanup closes service");
    expect(state.closed_service_object == state.next_service_object,
           "owner cleanup closes expected service object");
    expect(state.file.close_count == (file_close_before + 1u), "owner cleanup closes file");
    expect(runtime_syscall_query_handle(&cleanup_table, 1u, &info) == 0,
           "owner cleanup clears released handles");
  }

  expect(strcmp(runtime_syscall_status_name(RUNTIME_SYSCALL_STATUS_EBADF), "ebadf") == 0,
         "status names are stable");
  expect(strcmp(runtime_syscall_status_name(RUNTIME_SYSCALL_STATUS_EACCES), "eacces") == 0,
         "eacces status name is stable");
  expect(strcmp(runtime_syscall_status_name(RUNTIME_SYSCALL_STATUS_ENOENT), "enoent") == 0,
         "enoent status name is stable");
  expect(strcmp(runtime_syscall_status_name(RUNTIME_SYSCALL_STATUS_EEXIST), "eexist") == 0,
         "eexist status name is stable");
  expect(strcmp(runtime_syscall_status_name(RUNTIME_SYSCALL_STATUS_ENOTDIR), "enotdir") == 0,
         "enotdir status name is stable");
  expect(strcmp(runtime_syscall_status_name(RUNTIME_SYSCALL_STATUS_EISDIR), "eisdir") == 0,
         "eisdir status name is stable");
  expect(strcmp(runtime_syscall_status_name(RUNTIME_SYSCALL_STATUS_ENOTEMPTY), "enotempty") == 0,
         "enotempty status name is stable");
  expect(strcmp(runtime_syscall_status_name(-777), "unknown") == 0,
         "unknown status names are reported");

  return 0;
}
