#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

host_cc=${HOST_CC:-cc}
coop_out=$(mktemp)
preempt_stubless_out=$(mktemp)
preempt_target_out=$(mktemp)
trap 'rm -f "$coop_out" "$preempt_stubless_out" "$preempt_target_out"' EXIT

"$host_cc" -std=c11 -Wall -Wextra -Werror -Iinclude \
  -DCONFIG_SCHEDULER_COOPERATIVE=1 -DCONFIG_SCHEDULER_PREEMPTIVE=0 \
  -DCONFIG_RUNTIME_SINGLE_FOREGROUND=0 -DCONFIG_RUNTIME_MULTIPROCESS=1 \
  -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=0 \
  tests/preempt_host_test.c kernel/preempt.c -o "$coop_out"

"$host_cc" -std=c11 -Wall -Wextra -Werror -Iinclude \
  -DCONFIG_SCHEDULER_COOPERATIVE=0 -DCONFIG_SCHEDULER_PREEMPTIVE=1 \
  -DCONFIG_RUNTIME_SINGLE_FOREGROUND=0 -DCONFIG_RUNTIME_MULTIPROCESS=1 \
  -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=0 \
  tests/preempt_host_test.c kernel/preempt.c -o "$preempt_stubless_out"

"$host_cc" -std=c11 -Wall -Wextra -Werror -Iinclude \
  -DCONFIG_SCHEDULER_COOPERATIVE=0 -DCONFIG_SCHEDULER_PREEMPTIVE=1 \
  -DCONFIG_RUNTIME_SINGLE_FOREGROUND=0 -DCONFIG_RUNTIME_MULTIPROCESS=1 \
  -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=1 \
  -DTEST_PREEMPT_TARGET_CAPABLE=1 \
  tests/preempt_host_test.c kernel/preempt.c -o "$preempt_target_out"

"$coop_out"
"$preempt_stubless_out"
"$preempt_target_out"
