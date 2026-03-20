#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

host_cc=${HOST_CC:-cc}
out_file=$(mktemp)
trap 'rm -f "$out_file"' EXIT

"$host_cc" -std=c11 -Wall -Wextra -Werror -Iinclude \
  -DCONFIG_SCHEDULER_COOPERATIVE=1 -DCONFIG_SCHEDULER_PREEMPTIVE=0 \
  -DCONFIG_RUNTIME_SINGLE_FOREGROUND=0 -DCONFIG_RUNTIME_MULTIPROCESS=1 \
  -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=0 \
  tests/sync_host_test.c kernel/kmem.c kernel/wait_queue.c kernel/event.c kernel/semaphore.c kernel/mutex.c kernel/mailbox.c \
  tests/event_runtime_stubs.c -o "$out_file"

"$out_file"
