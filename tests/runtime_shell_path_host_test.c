#include "runtime_shell_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct host_path_state {
  const char *paths[4];
};

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_shell_path_host_test failed: %s\n", message);
    exit(1);
  }
}

static int host_path_exists(const char *path, void *context) {
  struct host_path_state *state = (struct host_path_state *)context;

  for (uint32_t index = 0u; index < 4u; ++index) {
    if ((state->paths[index] != 0) && (strcmp(state->paths[index], path) == 0)) {
      return 1;
    }
  }

  return 0;
}

int main(void) {
  struct host_path_state state = {
      .paths = {"/system/bin/ls", "/system/bin/mkdir", "/bin/echo", 0},
  };
  char resolved[RUNTIME_SHELL_PATH_BUFFER_CAPACITY];

  expect(runtime_shell_path_validate_command_name("ls") != 0, "bare command name is valid");
  expect(runtime_shell_path_validate_command_name("touch") != 0, "second bare command is valid");
  expect(runtime_shell_path_validate_command_name("system/bin/ls") == 0,
         "command name with slash is rejected");
  expect(runtime_shell_path_validate_search_path("/system/bin:/bin") != 0,
         "colon-separated absolute search path is valid");
  expect(runtime_shell_path_validate_search_path("system/bin:/bin") == 0,
         "relative search path is rejected");

  expect(runtime_shell_path_resolve("/system/bin:/bin", "ls", host_path_exists, &state, resolved,
                                    sizeof(resolved)) == RUNTIME_SYSCALL_STATUS_OK,
         "path resolution finds /system/bin command");
  expect(strcmp(resolved, "/system/bin/ls") == 0, "resolved path matches /system/bin entry");

  expect(runtime_shell_path_resolve("/system/bin:/bin", "echo", host_path_exists, &state, resolved,
                                    sizeof(resolved)) == RUNTIME_SYSCALL_STATUS_OK,
         "path resolution falls back to second search directory");
  expect(strcmp(resolved, "/bin/echo") == 0, "resolved path matches second directory entry");

  expect(runtime_shell_path_resolve("/system/bin:/bin", "/system/bin/mkdir", host_path_exists,
                                    &state, resolved, sizeof(resolved)) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "absolute path bypasses search path");
  expect(strcmp(resolved, "/system/bin/mkdir") == 0, "absolute path is preserved");

  expect(runtime_shell_path_resolve("/system/bin:/bin", "missing", host_path_exists, &state,
                                    resolved, sizeof(resolved)) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "missing command returns enoent");
  expect(runtime_shell_path_resolve("/system/bin:/bin", "dir/ls", host_path_exists, &state,
                                    resolved, sizeof(resolved)) == RUNTIME_SYSCALL_STATUS_EINVAL,
         "relative slash path returns einval");

  puts("runtime shell path host checks passed");
  return 0;
}
