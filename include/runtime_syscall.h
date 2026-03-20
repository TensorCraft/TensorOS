#pragma once

#include <stdint.h>

typedef uint32_t runtime_handle_t;

enum runtime_handle_type {
  RUNTIME_HANDLE_TYPE_NONE = 0u,
  RUNTIME_HANDLE_TYPE_PROCESS = 1u,
  RUNTIME_HANDLE_TYPE_EVENT = 2u,
  RUNTIME_HANDLE_TYPE_MAILBOX = 3u,
  RUNTIME_HANDLE_TYPE_FILE = 4u,
  RUNTIME_HANDLE_TYPE_SERVICE = 5u
};

enum runtime_syscall_number {
  RUNTIME_SYSCALL_GETPID = 0u,
  RUNTIME_SYSCALL_SLEEP = 1u,
  RUNTIME_SYSCALL_KILL = 2u,
  RUNTIME_SYSCALL_HANDLE_CLOSE = 3u,
  RUNTIME_SYSCALL_PROCESS_SELF = 4u,
  RUNTIME_SYSCALL_EVENT_CREATE = 5u,
  RUNTIME_SYSCALL_MAILBOX_CREATE = 6u,
  RUNTIME_SYSCALL_FILE_OPEN = 7u,
  RUNTIME_SYSCALL_FILE_READ = 8u,
  RUNTIME_SYSCALL_FILE_WRITE = 9u,
  RUNTIME_SYSCALL_EVENT_SIGNAL = 10u,
  RUNTIME_SYSCALL_MAILBOX_SEND = 11u,
  RUNTIME_SYSCALL_MAILBOX_RECEIVE = 12u,
  RUNTIME_SYSCALL_HANDLE_DUP = 13u,
  RUNTIME_SYSCALL_HANDLE_INFO = 14u,
  RUNTIME_SYSCALL_FILE_STAT = 15u,
  RUNTIME_SYSCALL_MKDIR = 16u,
  RUNTIME_SYSCALL_FILE_READDIR = 17u,
  RUNTIME_SYSCALL_FILE_SEEK = 18u,
  RUNTIME_SYSCALL_FILE_REMOVE = 19u,
  RUNTIME_SYSCALL_FILE_RENAME = 20u,
  RUNTIME_SYSCALL_PROCESS_SPAWN = 21u,
  RUNTIME_SYSCALL_PROCESS_SPAWNABLE_COUNT = 22u,
  RUNTIME_SYSCALL_PROCESS_SPAWNABLE_INFO = 23u,
  RUNTIME_SYSCALL_SERVICE_COUNT = 24u,
  RUNTIME_SYSCALL_SERVICE_INFO = 25u,
  RUNTIME_SYSCALL_SERVICE_OPEN = 26u,
  RUNTIME_SYSCALL_SERVICE_REQUEST = 27u,
  RUNTIME_SYSCALL_APP_COUNT = 28u,
  RUNTIME_SYSCALL_APP_INFO = 29u,
  RUNTIME_SYSCALL_APP_LAUNCH = 30u,
  RUNTIME_SYSCALL_PROCESS_INFO = 31u,
  RUNTIME_SYSCALL_PROCESS_TERMINATE = 32u
};

enum runtime_syscall_status {
  RUNTIME_SYSCALL_STATUS_OK = 0,
  RUNTIME_SYSCALL_STATUS_ENOENT = -2,
  RUNTIME_SYSCALL_STATUS_ESRCH = -3,
  RUNTIME_SYSCALL_STATUS_EBADF = -9,
  RUNTIME_SYSCALL_STATUS_EACCES = -13,
  RUNTIME_SYSCALL_STATUS_ENOMEM = -12,
  RUNTIME_SYSCALL_STATUS_EEXIST = -17,
  RUNTIME_SYSCALL_STATUS_EBUSY = -16,
  RUNTIME_SYSCALL_STATUS_ENOTDIR = -20,
  RUNTIME_SYSCALL_STATUS_EISDIR = -21,
  RUNTIME_SYSCALL_STATUS_EINVAL = -22,
  RUNTIME_SYSCALL_STATUS_ENOSPC = -28,
  RUNTIME_SYSCALL_STATUS_ENOTEMPTY = -39,
  RUNTIME_SYSCALL_STATUS_ENOSYS = -38,
  RUNTIME_SYSCALL_STATUS_ENOTSUP = -95
};

enum runtime_file_open_flags {
  RUNTIME_FILE_OPEN_READ = 1u << 0,
  RUNTIME_FILE_OPEN_WRITE = 1u << 1,
  RUNTIME_FILE_OPEN_CREATE = 1u << 2,
  RUNTIME_FILE_OPEN_TRUNCATE = 1u << 3
};

enum runtime_file_seek_whence {
  RUNTIME_FILE_SEEK_SET = 0u,
  RUNTIME_FILE_SEEK_CUR = 1u,
  RUNTIME_FILE_SEEK_END = 2u
};

struct runtime_syscall_args {
  uintptr_t arg0;
  uintptr_t arg1;
  uintptr_t arg2;
  uintptr_t arg3;
};

struct runtime_handle_info {
  runtime_handle_t handle;
  uint32_t owner_pid;
  uint32_t type;
  uintptr_t object;
};

struct runtime_file_stat {
  uint32_t type;
  uint32_t mode;
  uint32_t size_bytes;
};

struct runtime_dir_entry {
  uint32_t type;
  char name[16];
};

struct runtime_process_spawnable_info {
  const char *task_key;
  const char *process_name;
  uint32_t default_role;
};

struct runtime_service_info {
  const char *service_key;
  const char *service_name;
  uint32_t flags;
};

struct runtime_app_info {
  const char *app_key;
  const char *display_name;
  const char *task_key;
  uint32_t default_role;
};

struct runtime_process_info {
  uint32_t pid;
  uint32_t parent_pid;
  const char *name;
  uint32_t role;
  uint32_t state;
  int32_t exit_code;
  uint32_t switch_count;
  uint32_t yield_count;
  uint64_t wake_tick;
  uint32_t wait_reason;
  uint32_t wait_channel;
};

enum runtime_service_flags {
  RUNTIME_SERVICE_FLAG_REQUEST_REPLY = 1u << 0
};

enum runtime_file_type {
  RUNTIME_FILE_TYPE_UNKNOWN = 0u,
  RUNTIME_FILE_TYPE_REGULAR = 1u,
  RUNTIME_FILE_TYPE_DIRECTORY = 2u
};

struct runtime_syscall_file_ops {
  int32_t (*open)(const char *path, uint32_t flags, uintptr_t *file_object, void *context);
  int32_t (*read)(uintptr_t file_object, void *buffer, uint32_t size, void *context);
  int32_t (*write)(uintptr_t file_object, const void *buffer, uint32_t size, void *context);
  int32_t (*close)(uintptr_t file_object, void *context);
  int32_t (*stat)(uintptr_t file_object, struct runtime_file_stat *stat, void *context);
  int32_t (*readdir)(uintptr_t file_object, struct runtime_dir_entry *entry, void *context);
  int32_t (*seek)(uintptr_t file_object, int32_t offset, uint32_t whence, void *context);
  int32_t (*remove)(const char *path, void *context);
  int32_t (*rename)(const char *old_path, const char *new_path, void *context);
};

struct runtime_syscall_ops {
  uint32_t (*process_current_pid)(void *context);
  void (*process_sleep)(uint32_t ticks, void *context);
  int (*process_kill)(uint32_t pid, int32_t exit_code, void *context);
  int32_t (*process_info)(uint32_t pid, struct runtime_process_info *info, void *context);
  uint32_t (*process_spawn_named)(const char *task_key, uint32_t role, void *context);
  uint32_t (*process_spawnable_count)(void *context);
  int32_t (*process_spawnable_info)(uint32_t index, struct runtime_process_spawnable_info *info,
                                    void *context);
  uint32_t (*service_count)(void *context);
  int32_t (*service_info)(uint32_t index, struct runtime_service_info *info, void *context);
  int32_t (*service_open)(const char *service_key, uintptr_t *service_object, void *context);
  int32_t (*service_close)(uintptr_t service_object, void *context);
  int32_t (*service_request)(uintptr_t service_object, uint32_t request, uint32_t *response,
                             void *context);
  uint32_t (*app_count)(void *context);
  int32_t (*app_info)(uint32_t index, struct runtime_app_info *info, void *context);
  uint32_t (*app_launch)(const char *app_key, void *context);
  void *(*event_create)(void *context);
  int (*event_destroy)(void *event, void *context);
  uint32_t (*event_signal)(void *event, void *context);
  void *(*mailbox_create)(void *context);
  int (*mailbox_destroy)(void *mailbox, void *context);
  int (*mailbox_send)(void *mailbox, uint32_t message, void *context);
  int (*mailbox_receive)(void *mailbox, uint32_t *message, void *context);
  int32_t (*fs_mkdir)(const char *path, uint32_t mode, void *context);
  const struct runtime_syscall_file_ops *file_ops;
};

#define RUNTIME_SYSCALL_MAX_HANDLES 16u

struct runtime_syscall_table {
  struct runtime_handle_info handles[RUNTIME_SYSCALL_MAX_HANDLES];
};

void runtime_syscall_table_init(struct runtime_syscall_table *table);
int32_t runtime_syscall_dispatch(uint32_t syscall_number, const struct runtime_syscall_args *args,
                                 const struct runtime_syscall_ops *ops,
                                 struct runtime_syscall_table *table, void *context);
int runtime_syscall_query_handle(const struct runtime_syscall_table *table, runtime_handle_t handle,
                                 struct runtime_handle_info *info);
uint32_t runtime_syscall_release_owner_handles(struct runtime_syscall_table *table, uint32_t owner_pid,
                                               const struct runtime_syscall_ops *ops,
                                               void *context);
const char *runtime_syscall_status_name(int32_t status);
