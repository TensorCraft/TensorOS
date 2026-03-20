#pragma once

#include "runtime_syscall.h"

#include <stdint.h>

#define RUNTIME_SHELL_PATH_BUFFER_CAPACITY 128u

typedef int (*runtime_shell_path_exists_fn)(const char *path, void *context);

int runtime_shell_path_validate_command_name(const char *command);
int runtime_shell_path_validate_search_path(const char *path_list);
int32_t runtime_shell_path_resolve(const char *path_list, const char *command,
                                   runtime_shell_path_exists_fn exists, void *context,
                                   char *resolved_path, uint32_t capacity);
