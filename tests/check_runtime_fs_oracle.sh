#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/runtime_fs_oracle_host_test.c kernel/runtime_fs.c kernel/runtime_fs_image.c kernel/runtime_syscall.c \
  -o tests/runtime_fs_oracle_host_test

./tests/runtime_fs_oracle_host_test
