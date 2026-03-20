#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

host_cc=${HOST_CC:-cc}
single_out=$(mktemp)
multi_out=$(mktemp)
trap 'rm -f "$single_out" "$multi_out"' EXIT

"$host_cc" -std=c11 -Wall -Wextra -Werror -Iinclude \
  -DCONFIG_SCHEDULER_COOPERATIVE=1 -DCONFIG_SCHEDULER_PREEMPTIVE=0 \
  -DCONFIG_RUNTIME_SINGLE_FOREGROUND=0 -DCONFIG_RUNTIME_MULTIPROCESS=1 \
  -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=0 \
  tests/runtime_policy_host_test.c kernel/runtime_policy.c -o "$multi_out"

"$host_cc" -std=c11 -Wall -Wextra -Werror -Iinclude \
  -DCONFIG_SCHEDULER_COOPERATIVE=1 -DCONFIG_SCHEDULER_PREEMPTIVE=0 \
  -DCONFIG_RUNTIME_SINGLE_FOREGROUND=1 -DCONFIG_RUNTIME_MULTIPROCESS=0 \
  -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=0 \
  tests/runtime_policy_host_test.c kernel/runtime_policy.c -o "$single_out"

"$multi_out"
"$single_out"
