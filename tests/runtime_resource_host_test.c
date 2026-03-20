#include "runtime_resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_resource_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  static const struct runtime_loader_record records[] = {
      {.app_key = "demo.calc", .display_name = "Demo Calculator", .version = "1.0.0", .task_key = "calc",
       .default_role = 1u},
      {.app_key = "demo.notes", .display_name = "Demo Notes", .version = "1.0.0", .task_key = "notes",
       .default_role = 1u},
  };
  struct runtime_resource_locator locator = {.app_key = "demo.calc", .resource_path = "fonts/ui.bin"};
  char buffer[128];

  expect(runtime_resource_validate_relative_path("fonts/ui.bin"), "nested resource path is valid");
  expect(runtime_resource_validate_relative_path("icon.raw"), "flat resource path is valid");
  expect(!runtime_resource_validate_relative_path("/fonts/ui.bin"), "absolute path is rejected");
  expect(!runtime_resource_validate_relative_path("../ui.bin"), "parent traversal is rejected");
  expect(!runtime_resource_validate_relative_path("fonts//ui.bin"), "empty segment is rejected");

  expect(runtime_resource_build_path(records, 2u, &locator, buffer, sizeof(buffer)) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "resource path build succeeds");
  expect(strcmp(buffer, "/apps/demo.calc/resources/fonts/ui.bin") == 0,
         "resource path build preserves package layout");

  locator.app_key = "missing.app";
  expect(runtime_resource_build_path(records, 2u, &locator, buffer, sizeof(buffer)) ==
             RUNTIME_SYSCALL_STATUS_ENOENT,
         "missing app key returns enoent");

  locator.app_key = "demo.calc";
  locator.resource_path = "fonts/../../ui.bin";
  expect(runtime_resource_build_path(records, 2u, &locator, buffer, sizeof(buffer)) ==
             RUNTIME_SYSCALL_STATUS_EINVAL,
         "invalid relative resource path returns einval");

  puts("runtime resource host checks passed");
  return 0;
}
