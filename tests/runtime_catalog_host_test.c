#include "runtime_catalog.h"

#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_catalog_host_test failed: %s\n", message);
    exit(1);
  }
}

static void task_one(void) {}
static void task_two(void) {}

int main(void) {
  const struct runtime_process_manifest manifests[] = {
      {"a", "TASKA", RUNTIME_PROCESS_ROLE_FOREGROUND_APP, task_one},
      {"ev", "EVENTWAITER", RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY, task_two},
  };
  struct runtime_process_catalog_entry snapshot[2];
  const struct runtime_process_manifest *manifest;

  manifest = runtime_catalog_find(manifests, 2u, "ev");
  expect(manifest != 0, "find returns manifest");
  expect(manifest->entry == task_two, "find preserves entry");
  expect(manifest->default_role == RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY, "find preserves role");
  expect(runtime_catalog_find(manifests, 2u, "missing") == 0, "find rejects unknown key");

  expect(runtime_catalog_snapshot(manifests, 2u, snapshot, 2u) == 2u, "snapshot counts entries");
  expect(snapshot[0].task_key[0] == 'a', "snapshot preserves first key");
  expect(snapshot[0].process_name[0] == 'T', "snapshot preserves first process name");
  expect(snapshot[1].default_role == RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY,
         "snapshot preserves second role");

  puts("runtime catalog host checks passed");
  return 0;
}
