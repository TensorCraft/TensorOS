#include "runtime_shell_path.h"

static int runtime_shell_path_has_slash(const char *command) {
  uint32_t index = 0u;

  if (command == 0) {
    return 0;
  }

  while (command[index] != '\0') {
    if (command[index] == '/') {
      return 1;
    }
    ++index;
  }

  return 0;
}

static int runtime_shell_path_append(char *buffer, uint32_t capacity, uint32_t *length, char ch) {
  if ((*length + 1u) >= capacity) {
    return 0;
  }

  buffer[*length] = ch;
  ++(*length);
  buffer[*length] = '\0';
  return 1;
}

int runtime_shell_path_validate_command_name(const char *command) {
  uint32_t index = 0u;

  if ((command == 0) || (command[0] == '\0')) {
    return 0;
  }

  while (command[index] != '\0') {
    if ((command[index] == '/') || (command[index] == ':') || (command[index] <= ' ')) {
      return 0;
    }
    ++index;
  }

  return 1;
}

int runtime_shell_path_validate_search_path(const char *path_list) {
  uint32_t index = 0u;
  uint32_t segment_length = 0u;

  if ((path_list == 0) || (path_list[0] == '\0')) {
    return 0;
  }

  if (path_list[0] != '/') {
    return 0;
  }

  while (path_list[index] != '\0') {
    if (path_list[index] == ':') {
      if ((segment_length == 0u) || (path_list[index + 1u] != '/')) {
        return 0;
      }
      segment_length = 0u;
      ++index;
      continue;
    }

    if ((path_list[index] <= ' ') || ((path_list[index] == '/') && (path_list[index + 1u] == '/'))) {
      return 0;
    }

    ++segment_length;
    ++index;
  }

  return segment_length != 0u;
}

int32_t runtime_shell_path_resolve(const char *path_list, const char *command,
                                   runtime_shell_path_exists_fn exists, void *context,
                                   char *resolved_path, uint32_t capacity) {
  if ((exists == 0) || (resolved_path == 0) || (capacity == 0u) || (command == 0)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  resolved_path[0] = '\0';

  if (runtime_shell_path_has_slash(command)) {
    uint32_t index = 0u;

    if (command[0] != '/') {
      return RUNTIME_SYSCALL_STATUS_EINVAL;
    }

    while (command[index] != '\0') {
      if (!runtime_shell_path_append(resolved_path, capacity, &index, command[index])) {
        return RUNTIME_SYSCALL_STATUS_ENOSPC;
      }
    }

    return exists(resolved_path, context) ? RUNTIME_SYSCALL_STATUS_OK
                                          : RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  if (!runtime_shell_path_validate_search_path(path_list) ||
      !runtime_shell_path_validate_command_name(command)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  {
    const char *cursor = path_list;

    while (*cursor != '\0') {
      uint32_t length = 0u;
      const char *segment = cursor;

      while ((cursor[length] != '\0') && (cursor[length] != ':')) {
        ++length;
      }

      if (length != 0u) {
        uint32_t out_length = 0u;

        for (uint32_t index = 0u; index < length; ++index) {
          if (!runtime_shell_path_append(resolved_path, capacity, &out_length, segment[index])) {
            return RUNTIME_SYSCALL_STATUS_ENOSPC;
          }
        }

        if ((out_length == 0u) || (resolved_path[out_length - 1u] != '/')) {
          if (!runtime_shell_path_append(resolved_path, capacity, &out_length, '/')) {
            return RUNTIME_SYSCALL_STATUS_ENOSPC;
          }
        }

        for (uint32_t index = 0u; command[index] != '\0'; ++index) {
          if (!runtime_shell_path_append(resolved_path, capacity, &out_length, command[index])) {
            return RUNTIME_SYSCALL_STATUS_ENOSPC;
          }
        }

        if (exists(resolved_path, context)) {
          return RUNTIME_SYSCALL_STATUS_OK;
        }

        resolved_path[0] = '\0';
      }

      cursor += length;
      if (*cursor == ':') {
        ++cursor;
      }
    }
  }

  return RUNTIME_SYSCALL_STATUS_ENOENT;
}
