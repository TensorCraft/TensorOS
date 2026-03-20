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
  tests/process_core_host_test.c kernel/process_core.c -o "$out_file"

"$out_file"
