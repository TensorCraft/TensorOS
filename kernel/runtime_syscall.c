#include "runtime_syscall.h"

#include <stddef.h>

static runtime_handle_t runtime_syscall_alloc_handle(struct runtime_syscall_table *table,
                                                     uint32_t owner_pid, uint32_t type,
                                                     uintptr_t object) {
  uint32_t index;

  if (table == NULL) {
    return 0u;
  }

  for (index = 0u; index < RUNTIME_SYSCALL_MAX_HANDLES; ++index) {
    if (table->handles[index].type == RUNTIME_HANDLE_TYPE_NONE) {
      table->handles[index].handle = index + 1u;
      table->handles[index].owner_pid = owner_pid;
      table->handles[index].type = type;
      table->handles[index].object = object;
      return index + 1u;
    }
  }

  return 0u;
}

static struct runtime_handle_info *runtime_syscall_lookup_handle(struct runtime_syscall_table *table,
                                                                 runtime_handle_t handle) {
  uint32_t index;

  if ((table == NULL) || (handle == 0u) || (handle > RUNTIME_SYSCALL_MAX_HANDLES)) {
    return NULL;
  }

  index = handle - 1u;
  if (table->handles[index].type == RUNTIME_HANDLE_TYPE_NONE) {
    return NULL;
  }

  return &table->handles[index];
}

static const struct runtime_handle_info *
runtime_syscall_lookup_handle_const(const struct runtime_syscall_table *table, runtime_handle_t handle) {
  uint32_t index;

  if ((table == NULL) || (handle == 0u) || (handle > RUNTIME_SYSCALL_MAX_HANDLES)) {
    return NULL;
  }

  index = handle - 1u;
  if (table->handles[index].type == RUNTIME_HANDLE_TYPE_NONE) {
    return NULL;
  }

  return &table->handles[index];
}

static void runtime_syscall_clear_handle(struct runtime_handle_info *handle) {
  handle->handle = 0u;
  handle->owner_pid = 0u;
  handle->type = RUNTIME_HANDLE_TYPE_NONE;
  handle->object = 0u;
}

static void runtime_syscall_copy_handle_info(struct runtime_handle_info *dest,
                                             const struct runtime_handle_info *src) {
  if ((dest == NULL) || (src == NULL)) {
    return;
  }

  dest->handle = src->handle;
  dest->owner_pid = src->owner_pid;
  dest->type = src->type;
  dest->object = src->object;
}

static uint32_t runtime_syscall_handle_alias_count(const struct runtime_syscall_table *table,
                                                   const struct runtime_handle_info *handle) {
  uint32_t index;
  uint32_t aliases = 0u;

  if ((table == NULL) || (handle == NULL)) {
    return 0u;
  }

  for (index = 0u; index < RUNTIME_SYSCALL_MAX_HANDLES; ++index) {
    const struct runtime_handle_info *candidate = &table->handles[index];
    if ((candidate->type == handle->type) && (candidate->owner_pid == handle->owner_pid) &&
        (candidate->object == handle->object)) {
      ++aliases;
    }
  }

  return aliases;
}

static int32_t runtime_syscall_check_handle_access(const struct runtime_handle_info *handle,
                                                   const struct runtime_syscall_ops *ops,
                                                   void *context) {
  uint32_t current_pid;

  if ((handle == NULL) || (ops == NULL) || (ops->process_current_pid == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  current_pid = ops->process_current_pid(context);
  if ((handle->owner_pid != 0u) && (handle->owner_pid != current_pid)) {
    return RUNTIME_SYSCALL_STATUS_EBADF;
  }

  return RUNTIME_SYSCALL_STATUS_OK;
}

static int32_t runtime_syscall_close_handle(struct runtime_handle_info *handle,
                                            const struct runtime_syscall_ops *ops,
                                            const struct runtime_syscall_table *table,
                                            void *context, int enforce_access) {
  int result = 1;
  int32_t access_status;
  uint32_t alias_count;

  if ((handle == NULL) || (ops == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (enforce_access != 0) {
    access_status = runtime_syscall_check_handle_access(handle, ops, context);
    if (access_status != RUNTIME_SYSCALL_STATUS_OK) {
      return access_status;
    }
  }

  alias_count = runtime_syscall_handle_alias_count(table, handle);

  switch (handle->type) {
    case RUNTIME_HANDLE_TYPE_PROCESS:
      runtime_syscall_clear_handle(handle);
      return RUNTIME_SYSCALL_STATUS_OK;
    case RUNTIME_HANDLE_TYPE_EVENT:
      if (alias_count > 1u) {
        runtime_syscall_clear_handle(handle);
        return RUNTIME_SYSCALL_STATUS_OK;
      }
      if (ops->event_destroy == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      result = ops->event_destroy((void *)handle->object, context);
      if (result == 0) {
        return RUNTIME_SYSCALL_STATUS_EBUSY;
      }
      runtime_syscall_clear_handle(handle);
      return RUNTIME_SYSCALL_STATUS_OK;
    case RUNTIME_HANDLE_TYPE_MAILBOX:
      if (alias_count > 1u) {
        runtime_syscall_clear_handle(handle);
        return RUNTIME_SYSCALL_STATUS_OK;
      }
      if (ops->mailbox_destroy == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      result = ops->mailbox_destroy((void *)handle->object, context);
      if (result == 0) {
        return RUNTIME_SYSCALL_STATUS_EBUSY;
      }
      runtime_syscall_clear_handle(handle);
      return RUNTIME_SYSCALL_STATUS_OK;
    case RUNTIME_HANDLE_TYPE_FILE:
      if (alias_count > 1u) {
        runtime_syscall_clear_handle(handle);
        return RUNTIME_SYSCALL_STATUS_OK;
      }
      if ((ops->file_ops == NULL) || (ops->file_ops->close == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      if (ops->file_ops->close(handle->object, context) < 0) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      runtime_syscall_clear_handle(handle);
      return RUNTIME_SYSCALL_STATUS_OK;
    case RUNTIME_HANDLE_TYPE_SERVICE:
      if (alias_count > 1u) {
        runtime_syscall_clear_handle(handle);
        return RUNTIME_SYSCALL_STATUS_OK;
      }
      if (ops->service_close != NULL) {
        if (ops->service_close(handle->object, context) < 0) {
          return RUNTIME_SYSCALL_STATUS_EBADF;
        }
      }
      runtime_syscall_clear_handle(handle);
      return RUNTIME_SYSCALL_STATUS_OK;
    default:
      return RUNTIME_SYSCALL_STATUS_EBADF;
  }
}

void runtime_syscall_table_init(struct runtime_syscall_table *table) {
  uint32_t index;

  if (table == NULL) {
    return;
  }

  for (index = 0u; index < RUNTIME_SYSCALL_MAX_HANDLES; ++index) {
    runtime_syscall_clear_handle(&table->handles[index]);
  }
}

int32_t runtime_syscall_dispatch(uint32_t syscall_number, const struct runtime_syscall_args *args,
                                 const struct runtime_syscall_ops *ops,
                                 struct runtime_syscall_table *table, void *context) {
  struct runtime_handle_info *handle;
  runtime_handle_t handle_id;
  uintptr_t file_object = 0u;
  int32_t status;
  uint32_t current_pid = 0u;

  if ((args == NULL) || (ops == NULL)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (ops->process_current_pid != NULL) {
    current_pid = ops->process_current_pid(context);
  }

  switch (syscall_number) {
    case RUNTIME_SYSCALL_GETPID:
      if (ops->process_current_pid == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return (int32_t)ops->process_current_pid(context);

    case RUNTIME_SYSCALL_SLEEP:
      if (ops->process_sleep == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      ops->process_sleep((uint32_t)args->arg0, context);
      return RUNTIME_SYSCALL_STATUS_OK;

    case RUNTIME_SYSCALL_KILL:
      if (ops->process_kill == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      if (ops->process_kill((uint32_t)args->arg0, (int32_t)args->arg1, context) == 0) {
        return RUNTIME_SYSCALL_STATUS_ESRCH;
      }
      return RUNTIME_SYSCALL_STATUS_OK;

    case RUNTIME_SYSCALL_HANDLE_CLOSE:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if (handle == NULL) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      return runtime_syscall_close_handle(handle, ops, table, context, 1);

    case RUNTIME_SYSCALL_PROCESS_SELF:
      if (ops->process_current_pid == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      handle_id = runtime_syscall_alloc_handle(table, current_pid, RUNTIME_HANDLE_TYPE_PROCESS,
                                               (uintptr_t)ops->process_current_pid(context));
      if (handle_id == 0u) {
        return RUNTIME_SYSCALL_STATUS_ENOSPC;
      }
      return (int32_t)handle_id;

    case RUNTIME_SYSCALL_PROCESS_INFO:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_PROCESS)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if ((args->arg1 == (uintptr_t)NULL) || (ops->process_info == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->process_info((uint32_t)handle->object, (struct runtime_process_info *)args->arg1,
                               context);

    case RUNTIME_SYSCALL_PROCESS_TERMINATE:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_PROCESS)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if (ops->process_kill == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      if (ops->process_kill((uint32_t)handle->object, (int32_t)args->arg1, context) == 0) {
        return RUNTIME_SYSCALL_STATUS_ESRCH;
      }
      return RUNTIME_SYSCALL_STATUS_OK;

    case RUNTIME_SYSCALL_EVENT_CREATE:
      if (ops->event_create == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      handle_id =
          runtime_syscall_alloc_handle(table, current_pid, RUNTIME_HANDLE_TYPE_EVENT,
                                       (uintptr_t)ops->event_create(context));
      if (handle_id == 0u) {
        return RUNTIME_SYSCALL_STATUS_ENOSPC;
      }
      if (table->handles[handle_id - 1u].object == 0u) {
        runtime_syscall_clear_handle(&table->handles[handle_id - 1u]);
        return RUNTIME_SYSCALL_STATUS_ENOMEM;
      }
      return (int32_t)handle_id;

    case RUNTIME_SYSCALL_PROCESS_SPAWN:
      if ((args->arg0 == (uintptr_t)NULL) || (ops->process_spawn_named == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      {
        uint32_t spawned_pid =
            ops->process_spawn_named((const char *)args->arg0, (uint32_t)args->arg1, context);
        if (spawned_pid == 0u) {
          return RUNTIME_SYSCALL_STATUS_ENOENT;
        }
        handle_id = runtime_syscall_alloc_handle(table, current_pid, RUNTIME_HANDLE_TYPE_PROCESS,
                                                 (uintptr_t)spawned_pid);
        if (handle_id == 0u) {
          return RUNTIME_SYSCALL_STATUS_ENOSPC;
        }
        return (int32_t)handle_id;
      }

    case RUNTIME_SYSCALL_PROCESS_SPAWNABLE_COUNT:
      if (ops->process_spawnable_count == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return (int32_t)ops->process_spawnable_count(context);

    case RUNTIME_SYSCALL_PROCESS_SPAWNABLE_INFO:
      if ((args->arg1 == (uintptr_t)NULL) || (ops->process_spawnable_info == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->process_spawnable_info((uint32_t)args->arg0,
                                         (struct runtime_process_spawnable_info *)args->arg1,
                                         context);

    case RUNTIME_SYSCALL_SERVICE_COUNT:
      if (ops->service_count == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return (int32_t)ops->service_count(context);

    case RUNTIME_SYSCALL_SERVICE_INFO:
      if ((args->arg1 == (uintptr_t)NULL) || (ops->service_info == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->service_info((uint32_t)args->arg0, (struct runtime_service_info *)args->arg1,
                               context);

    case RUNTIME_SYSCALL_SERVICE_OPEN:
      if ((args->arg0 == (uintptr_t)NULL) || (ops->service_open == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      status = ops->service_open((const char *)args->arg0, &file_object, context);
      if (status < 0) {
        return status;
      }
      handle_id =
          runtime_syscall_alloc_handle(table, current_pid, RUNTIME_HANDLE_TYPE_SERVICE, file_object);
      if (handle_id == 0u) {
        if (ops->service_close != NULL) {
          (void)ops->service_close(file_object, context);
        }
        return RUNTIME_SYSCALL_STATUS_ENOSPC;
      }
      return (int32_t)handle_id;

    case RUNTIME_SYSCALL_SERVICE_REQUEST:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_SERVICE)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if ((args->arg2 == (uintptr_t)NULL) || (ops->service_request == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->service_request(handle->object, (uint32_t)args->arg1, (uint32_t *)args->arg2,
                                  context);

    case RUNTIME_SYSCALL_APP_COUNT:
      if (ops->app_count == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return (int32_t)ops->app_count(context);

    case RUNTIME_SYSCALL_APP_INFO:
      if ((args->arg1 == (uintptr_t)NULL) || (ops->app_info == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->app_info((uint32_t)args->arg0, (struct runtime_app_info *)args->arg1, context);

    case RUNTIME_SYSCALL_APP_LAUNCH:
      if ((args->arg0 == (uintptr_t)NULL) || (ops->app_launch == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      {
        uint32_t launched_pid = ops->app_launch((const char *)args->arg0, context);
        if (launched_pid == 0u) {
          return RUNTIME_SYSCALL_STATUS_ENOENT;
        }
        handle_id = runtime_syscall_alloc_handle(table, current_pid, RUNTIME_HANDLE_TYPE_PROCESS,
                                                 (uintptr_t)launched_pid);
        if (handle_id == 0u) {
          return RUNTIME_SYSCALL_STATUS_ENOSPC;
        }
        return (int32_t)handle_id;
      }

    case RUNTIME_SYSCALL_MAILBOX_CREATE:
      if (ops->mailbox_create == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      handle_id =
          runtime_syscall_alloc_handle(table, current_pid, RUNTIME_HANDLE_TYPE_MAILBOX,
                                       (uintptr_t)ops->mailbox_create(context));
      if (handle_id == 0u) {
        return RUNTIME_SYSCALL_STATUS_ENOSPC;
      }
      if (table->handles[handle_id - 1u].object == 0u) {
        runtime_syscall_clear_handle(&table->handles[handle_id - 1u]);
        return RUNTIME_SYSCALL_STATUS_ENOMEM;
      }
      return (int32_t)handle_id;

    case RUNTIME_SYSCALL_FILE_OPEN:
      if ((ops->file_ops == NULL) || (ops->file_ops->open == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      status = ops->file_ops->open((const char *)args->arg0, (uint32_t)args->arg1, &file_object,
                                   context);
      if (status < 0) {
        return status;
      }
      handle_id =
          runtime_syscall_alloc_handle(table, current_pid, RUNTIME_HANDLE_TYPE_FILE, file_object);
      if (handle_id == 0u) {
        if (ops->file_ops->close != NULL) {
          (void)ops->file_ops->close(file_object, context);
        }
        return RUNTIME_SYSCALL_STATUS_ENOSPC;
      }
      return (int32_t)handle_id;

    case RUNTIME_SYSCALL_FILE_READ:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_FILE)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if ((ops->file_ops == NULL) || (ops->file_ops->read == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->file_ops->read(handle->object, (void *)args->arg1, (uint32_t)args->arg2,
                                 context);

    case RUNTIME_SYSCALL_FILE_WRITE:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_FILE)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if ((ops->file_ops == NULL) || (ops->file_ops->write == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->file_ops->write(handle->object, (const void *)args->arg1, (uint32_t)args->arg2,
                                  context);

    case RUNTIME_SYSCALL_EVENT_SIGNAL:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_EVENT)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if (ops->event_signal == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return (int32_t)ops->event_signal((void *)handle->object, context);

    case RUNTIME_SYSCALL_MAILBOX_SEND:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_MAILBOX)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if (ops->mailbox_send == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->mailbox_send((void *)handle->object, (uint32_t)args->arg1, context) == 0
                 ? RUNTIME_SYSCALL_STATUS_EBUSY
                 : RUNTIME_SYSCALL_STATUS_OK;

    case RUNTIME_SYSCALL_MAILBOX_RECEIVE:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_MAILBOX)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if (ops->mailbox_receive == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->mailbox_receive((void *)handle->object, (uint32_t *)args->arg1, context) == 0
                 ? RUNTIME_SYSCALL_STATUS_EBUSY
                 : RUNTIME_SYSCALL_STATUS_OK;

    case RUNTIME_SYSCALL_HANDLE_DUP:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if (handle == NULL) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      handle_id = runtime_syscall_alloc_handle(table, handle->owner_pid, handle->type, handle->object);
      if (handle_id == 0u) {
        return RUNTIME_SYSCALL_STATUS_ENOSPC;
      }
      return (int32_t)handle_id;

    case RUNTIME_SYSCALL_HANDLE_INFO:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (args->arg1 == (uintptr_t)NULL)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      runtime_syscall_copy_handle_info((struct runtime_handle_info *)args->arg1, handle);
      return RUNTIME_SYSCALL_STATUS_OK;

    case RUNTIME_SYSCALL_FILE_STAT:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_FILE)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if ((args->arg1 == (uintptr_t)NULL) || (ops->file_ops == NULL) || (ops->file_ops->stat == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->file_ops->stat(handle->object, (struct runtime_file_stat *)args->arg1, context);

    case RUNTIME_SYSCALL_MKDIR:
      if (ops->fs_mkdir == NULL) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->fs_mkdir((const char *)args->arg0, (uint32_t)args->arg1, context);

    case RUNTIME_SYSCALL_FILE_READDIR:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_FILE)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if ((args->arg1 == (uintptr_t)NULL) || (ops->file_ops == NULL) ||
          (ops->file_ops->readdir == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->file_ops->readdir(handle->object, (struct runtime_dir_entry *)args->arg1, context);

    case RUNTIME_SYSCALL_FILE_SEEK:
      handle = runtime_syscall_lookup_handle(table, (runtime_handle_t)args->arg0);
      if ((handle == NULL) || (handle->type != RUNTIME_HANDLE_TYPE_FILE)) {
        return RUNTIME_SYSCALL_STATUS_EBADF;
      }
      status = runtime_syscall_check_handle_access(handle, ops, context);
      if (status != RUNTIME_SYSCALL_STATUS_OK) {
        return status;
      }
      if ((ops->file_ops == NULL) || (ops->file_ops->seek == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->file_ops->seek(handle->object, (int32_t)args->arg1, (uint32_t)args->arg2, context);

    case RUNTIME_SYSCALL_FILE_REMOVE:
      if ((args->arg0 == (uintptr_t)NULL) || (ops->file_ops == NULL) || (ops->file_ops->remove == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->file_ops->remove((const char *)args->arg0, context);

    case RUNTIME_SYSCALL_FILE_RENAME:
      if ((args->arg0 == (uintptr_t)NULL) || (args->arg1 == (uintptr_t)NULL) || (ops->file_ops == NULL) ||
          (ops->file_ops->rename == NULL)) {
        return RUNTIME_SYSCALL_STATUS_ENOSYS;
      }
      return ops->file_ops->rename((const char *)args->arg0, (const char *)args->arg1, context);

    default:
      return RUNTIME_SYSCALL_STATUS_ENOSYS;
  }
}

int runtime_syscall_query_handle(const struct runtime_syscall_table *table, runtime_handle_t handle,
                                 struct runtime_handle_info *info) {
  const struct runtime_handle_info *entry = runtime_syscall_lookup_handle_const(table, handle);

  if ((entry == NULL) || (info == NULL)) {
    return 0;
  }

  runtime_syscall_copy_handle_info(info, entry);
  return 1;
}

uint32_t runtime_syscall_release_owner_handles(struct runtime_syscall_table *table, uint32_t owner_pid,
                                               const struct runtime_syscall_ops *ops,
                                               void *context) {
  uint32_t released = 0u;

  if ((table == NULL) || (ops == NULL) || (owner_pid == 0u)) {
    return 0u;
  }

  for (uint32_t index = 0u; index < RUNTIME_SYSCALL_MAX_HANDLES; ++index) {
    if ((table->handles[index].type == RUNTIME_HANDLE_TYPE_NONE) ||
        (table->handles[index].owner_pid != owner_pid)) {
      continue;
    }

    if (runtime_syscall_close_handle(&table->handles[index], ops, table, context, 0) ==
        RUNTIME_SYSCALL_STATUS_OK) {
      ++released;
    }
  }

  return released;
}

const char *runtime_syscall_status_name(int32_t status) {
  switch (status) {
    case RUNTIME_SYSCALL_STATUS_OK:
      return "ok";
    case RUNTIME_SYSCALL_STATUS_ENOENT:
      return "enoent";
    case RUNTIME_SYSCALL_STATUS_ESRCH:
      return "esrch";
    case RUNTIME_SYSCALL_STATUS_EBADF:
      return "ebadf";
    case RUNTIME_SYSCALL_STATUS_EACCES:
      return "eacces";
    case RUNTIME_SYSCALL_STATUS_ENOMEM:
      return "enomem";
    case RUNTIME_SYSCALL_STATUS_EEXIST:
      return "eexist";
    case RUNTIME_SYSCALL_STATUS_EBUSY:
      return "ebusy";
    case RUNTIME_SYSCALL_STATUS_ENOTDIR:
      return "enotdir";
    case RUNTIME_SYSCALL_STATUS_EISDIR:
      return "eisdir";
    case RUNTIME_SYSCALL_STATUS_EINVAL:
      return "einval";
    case RUNTIME_SYSCALL_STATUS_ENOSPC:
      return "enospc";
    case RUNTIME_SYSCALL_STATUS_ENOTEMPTY:
      return "enotempty";
    case RUNTIME_SYSCALL_STATUS_ENOSYS:
      return "enosys";
    case RUNTIME_SYSCALL_STATUS_ENOTSUP:
      return "enotsup";
    default:
      return "unknown";
  }
}
