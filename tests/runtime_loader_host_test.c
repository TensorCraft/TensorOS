#include "runtime_loader.h"

#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "runtime_loader_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  const struct runtime_loader_record records[] = {
      {"demo.calc", "Demo Calculator", "0.1.0", "a", 2u},
      {"demo.live", "Demo Live Activity", "0.1.0", "ev", 4u},
  };
  const struct runtime_loader_record duplicate_records[] = {
      {"demo.calc", "Demo Calculator", "0.1.0", "a", 2u},
      {"demo.calc", "Demo Calculator Copy", "0.1.1", "b", 2u},
  };
  const struct runtime_loader_record invalid_records[] = {
      {"", "Broken App", "0.1.0", "a", 2u},
  };
  struct runtime_loader_snapshot_entry snapshot[2];
  const struct runtime_loader_record *record;

  record = runtime_loader_find(records, 2u, "demo.live");
  expect(record != NULL, "find returns loader record");
  expect(record->task_key[0] == 'e', "find preserves task key");
  expect(record->version[0] == '0', "find preserves version");
  expect(runtime_loader_find(records, 2u, "missing") == NULL, "find rejects missing app");

  expect(runtime_loader_snapshot(records, 2u, snapshot, 2u) == 2u, "snapshot counts records");
  expect(snapshot[0].app_key[5] == 'c', "snapshot preserves app key");
  expect(snapshot[0].display_name[5] == 'C', "snapshot preserves display name");
  expect(snapshot[1].default_role == 4u, "snapshot preserves role");

  expect(runtime_loader_validate_catalog(records, 2u) == 1, "validate accepts well-formed catalog");
  expect(runtime_loader_validate_catalog(duplicate_records, 2u) == 0,
         "validate rejects duplicate app keys");
  expect(runtime_loader_validate_catalog(invalid_records, 1u) == 0,
         "validate rejects empty fields");

  puts("runtime loader host checks passed");
  return 0;
}
