#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/runtime_display_host_test.c kernel/runtime_display.c \
  -o tests/runtime_display_host_test

./tests/runtime_display_host_test
