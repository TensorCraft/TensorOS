#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

kernel_file="$repo_root/kernel/kernel.c"

if ! grep -F -A 20 'static void task_a(void) {' "$kernel_file" | grep -F -q 'runtime_display_demo_poll();'; then
  echo "error: task_a no longer drives runtime_display_demo_poll()"
  exit 1
fi

if ! grep -F -A 30 'static void task_d(void) {' "$kernel_file" | grep -F -q 'runtime_display_demo_publish_tick(tick_value);'; then
  echo "error: task_d no longer publishes the current tick into the display demo"
  exit 1
fi

if ! grep -F -A 30 'static void task_d(void) {' "$kernel_file" | grep -F -q 'tick_value = (uint32_t)kernel_ticks_read();'; then
  echo "error: task_d no longer refreshes tick_value from kernel_ticks_read()"
  exit 1
fi

if ! grep -F -A 20 'static void kernel_spawn_multiprocess_demo(void) {' "$kernel_file" | grep -F -q 'process_spawn_with_role("TASKA", task_a, RUNTIME_PROCESS_ROLE_FOREGROUND_APP);'; then
  echo "error: multiprocess baseline no longer spawns TASKA as the foreground task"
  exit 1
fi

if ! grep -F -A 20 'static void kernel_spawn_multiprocess_demo(void) {' "$kernel_file" | grep -F -q 'process_spawn_with_role("TASKD", task_d, RUNTIME_PROCESS_ROLE_BACKGROUND_APP);'; then
  echo "error: multiprocess baseline no longer spawns TASKD as the background task"
  exit 1
fi

echo "runtime stability baseline checks passed"
