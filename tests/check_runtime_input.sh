#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/runtime_input_host_test.c kernel/runtime_input.c kernel/runtime_ui.c \
  -o tests/runtime_input_host_test

./tests/runtime_input_host_test
