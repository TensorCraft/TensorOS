#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -DCONFIG_RUNTIME_SINGLE_FOREGROUND=0 -DCONFIG_RUNTIME_MULTIPROCESS=1 \
  -DCONFIG_SCHEDULER_COOPERATIVE=1 -DCONFIG_SCHEDULER_PREEMPTIVE=0 \
  -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=0 \
  -Iinclude \
  tests/runtime_catalog_host_test.c kernel/runtime_catalog.c \
  -o tests/runtime_catalog_host_test

./tests/runtime_catalog_host_test
