#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/runtime_syscall_host_test.c kernel/runtime_syscall.c \
  -o tests/runtime_syscall_host_test

./tests/runtime_syscall_host_test
